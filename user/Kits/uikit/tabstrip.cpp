//
// uikit/tabstrip.cpp -- TabStrip (uikit/tabstrip.h): the tabs share the strip's width up to a wide
// tab each; the chosen one rounded at its top in the content's face, open onto it; the others a shade
// of the strip; the cross lit under the pointer; the "+" after the last tab.
//
// MIT licence (Onyx).
//
#include "uikit/tabstrip.h"
#include "uikit/paint.h"
#include "uikit/text.h"
#include "uikit/adapt.h"
#include "uikit/lang.h"
#include "uikit/dialog.h"
#include "uikit/root.h"
#include "uikit/internal/adapt_int.h"

namespace uikit {

static const int TAB_MAX_W = 200, TAB_MIN_W = 60, TAB_GAP = 3, TAB_X0 = 6, PLUS_W = 26, CROSS = 16;

// ---- (P6) the adaptive part -----------------------------------------------------------------------------------------
namespace {
struct TabExt : internal::ExtHead
{
	int edge = UK_TAB_TOP, style = UK_TAB_AUTO;
	int scroll = 0;				// (UK_TABP_SCROLL) px the tabs are scrolled by
	int moreX = -1;				// (the "..." button's left; -1: none)
	int hotPart = 0;			// 3: "...", 4: the title's button (UK_TABP_MENU)
};
const int MORE_W = 30;

bool tab_step (Widget *w, int dir)		// (the console's L1 / R1)
{
	TabStrip *t = (TabStrip *) w;
	if (t->count () < 2) return false;
	t->selectNext (dir, true);
	return true;
}

TabExt *tab_ext (TabStrip *t)
{
	TabExt *e = internal::ext_of<TabExt> (t, internal::EXT_TABSTRIP);
	if (e->destroy != 0 && e->onClass == 0)			// (made now: its nav, its end)
	{
		e->onClass = [] (Widget *w, internal::ExtHead *) { w->invalidate (true); };
		e->destroy = [] (internal::ExtHead *x) { internal::nav_unregister (x->owner); delete (TabExt *) x; };
		internal::adaptive_add (e);
	}
	return e;
}

// The list of the tabs (a "..." button, portrait's title): each its mark, its title, a close cross; "+ New tab" at its
// end -> 1 + i: tab i chosen, -(1 + i): its close asked, 1000: a new tab, 0: nothing.
class TabList : public Modal
{
public:
	TabStrip *ts; int n, hot, hotX; bool news, crosses;
	TabList (TabStrip *t, int x, int y, int w) : Modal (w, 8), ts (t), n (t->count ()), hot (-1), hotX (0), news (t->newButton && t->onNew != 0), crosses (t->closable && t->onClose != 0)
	{
		left = x; top = y;
		resizeTo (w, 8 + (n + (news ? 1 : 0)) * row ());
	}
	static int row () { return uk_metrics ().menuRow; }
	int rowAt (int mx, int my) const { if (mx < 0 || mx >= width || my < 4) return -1; int r = (my - 4) / row (); return r < n + (news ? 1 : 0) ? r : -1; }
	void onDraw () override
	{
		bool con = uk_size_class () == UK_SC_CONSOLE;
		unsigned bg = con ? 0x00182644 : C_FIELD, ink = con ? 0x00F2F6FF : C_FIELD_TEXT;
		canvas.clear (UK_TRANSPARENT_KEY);
		if (con) { uk_rbox (canvas, 0, 0, width, height, 10, bg, 0x000C1428); uk_rline (canvas, 0, 0, width, height, 10, 0x0060C8FF, 200); }
		else uk_popup (canvas, 0, 0, width, height, 8, bg);
		for (int i = 0; i < n + (news ? 1 : 0); i++)
		{
			int y = 4 + i * row ();
			bool h = i == hot, cur = i == ts->selected;
			unsigned rowInk = ink;
			if (h || cur)
			{
				if (con) uk_rbox (canvas, 4, y, width - 8, row (), 6, cur ? 0x002E5FA8 : 0x00223A66, cur ? 0x00224A88 : 0x001A2E52);
				else uk_hilite (canvas, 4, y, width - 8, row (), 5, h);
				if (!con) rowInk = uk_hilite_ink (h);
			}
			if (i == n) { uk_glyph (canvas, WKG_PLUS, 20, y + row () / 2, 10, rowInk); uk_text_l (canvas, 36, y, row (), TR ("New Tab"), rowInk); continue; }
			int tx = 14;
			if (ts->marked (i)) uk_glyph (canvas, WKG_DOT, tx + 6, y + row () / 2, 8, con ? 0x0060C8FF : C_ACCENT);
			char b[48];
			uk_text_fit (ts->title (i), width - tx - (crosses ? 44 : 14) - 14, b, sizeof b, cur ? 2 : 0);
			uk_text_l (canvas, tx + 14, y, row (), b, rowInk, cur ? 2 : 0);
			if (crosses) uk_glyph (canvas, WKG_CLOSE, width - 24, y + row () / 2, 9, h && hotX >= width - 40 ? (con ? 0x0060C8FF : C_ACCENT) : rowInk);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int r = rowAt (mx, my);
		if (r != hot || mx != hotX) { hot = r; hotX = mx; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (mx < 0 || my < 0 || mx >= width || my >= height) close (0); }
		else if (!bl && pressed)
		{
			pressed = false;
			if (r == n && news) close (1000);
			else if (r >= 0) close (crosses && mx >= width - 40 ? -(1 + r) : 1 + r);
		}
		return true;
	}
	bool onKey (long k) override
	{
		int all = n + (news ? 1 : 0);
		if (k == 27) { close (0); return true; }
		if (k == KEY_DOWN) { hot = (hot + 1) % all; invalidate (true); return true; }
		if (k == KEY_UP) { hot = hot <= 0 ? all - 1 : hot - 1; invalidate (true); return true; }
		if (k == KEY_DEL && hot >= 0 && hot < n && crosses) { close (-(1 + hot)); return true; }
		if (k == KEY_ENTER && hot >= 0) { close (hot == n ? 1000 : 1 + hot); return true; }
		return true;
	}
};
}

void TabStrip::setEdge (int edge) { tab_ext (this)->edge = edge == UK_TAB_BOTTOM ? UK_TAB_BOTTOM : UK_TAB_TOP; invalidate (true); }
void TabStrip::setStyle (int style) { tab_ext (this)->style = style; invalidate (true); }
void TabStrip::setNav (bool on)
{
	tab_ext (this);
	if (on) internal::nav_register (this, 3, tab_step); else internal::nav_unregister (this);
}

int TabStrip::presentation ()
{
	int sc = uk_size_class ();
	if (sc == UK_SC_REGULAR) return UK_TABP_STRIP;
	if (sc == UK_SC_CONSOLE) return UK_TABP_HEADER;
	TabExt *e = tab_ext (this);
	if (sc == UK_SC_NARROW)
	{
		if (e->style == UK_TAB_STRIP) return UK_TABP_SCROLL;
		bool shortOnes = m_n >= 2 && m_n <= 3 && !(newButton && onNew) && !(closable && onClose);	// (fixed tabs only: no "+", no cross)
		for (int i = 0; i < m_n && shortOnes; i++) if (uk_tw (m_title[i], 2) + 24 > width / m_n) shortOnes = false;
		if (e->style == UK_TAB_SEGMENTED || (e->style == UK_TAB_AUTO && shortOnes)) return UK_TABP_SEGMENTED;
		return UK_TABP_MENU;
	}
	return e->style == UK_TAB_SEGMENTED && m_n <= 3 ? UK_TABP_SEGMENTED : UK_TABP_SCROLL;
}

void TabStrip::showList ()
{
	Root *r = Root::current ();
	if (r == 0) return;
	int ax, ay; internal::abs_pos (this, &ax, &ay);
	int w = 300, x = ax + width - w - 4, y = ay + height + 2;
	if (uk_size_class () == UK_SC_NARROW) { w = r->width - 16; x = 8; }
	if (x < 4) x = 4;
	if (w > r->width - 8) w = r->width - 8;
	TabList l (this, x, y, w);
	if (uk_size_class () == UK_SC_NARROW || uk_size_class () == UK_SC_CONSOLE)	// (a sheet at the foot; console: the middle)
	{
		l.left = (r->width - l.width) / 2;
		l.top = uk_size_class () == UK_SC_CONSOLE ? (r->height - l.height) / 2 : r->height - l.height - 8;
		if (l.top < 0) l.top = 0;
	}
	else if (l.top + l.height > r->height) l.top = r->height - l.height;
	int res = l.run ();
	if (res == 1000) { if (onNew) onNew (*this); }
	else if (res > 0) select (res - 1, true);
	else if (res < 0) { closing = -res - 1; if (onClose) onClose (*this); }
	invalidate (true);
}

TabStrip::TabStrip (int l, int t, int w, int h, Action change)
  : Widget (l, t, w, h), selected (-1), closing (-1), closable (true), newButton (true), activeFace (0),
    onChange (change), onClose (0), onNew (0), m_n (0), m_hot (-1), m_hotPart (0), m_plusX (0), m_mid (false)
{
	m_x[0] = TAB_X0;
	tab_ext (this);
	internal::nav_register (this, 2, tab_step);		// (P6: the console's L1 / R1 -- the first strip of a window)
}

int TabStrip::add (const char *title, void *data)
{
	if (m_n >= MAXTABS) return -1;
	int i = m_n++;
	m_title[i][0] = '\0'; m_data[i] = data; m_mark[i] = false;
	setTitle (i, title);
	if (selected < 0) selected = i;
	invalidate (true);
	return i;
}

void TabStrip::remove (int i)
{
	if (i < 0 || i >= m_n) return;
	for (int k = i; k < m_n - 1; k++)
	{
		for (int c = 0; c < 48; c++) m_title[k][c] = m_title[k + 1][c];
		m_data[k] = m_data[k + 1]; m_mark[k] = m_mark[k + 1];
	}
	m_n--;
	if (selected > i || selected >= m_n) selected--;
	if (selected < 0 && m_n > 0) selected = 0;
	m_hot = -1; m_hotPart = 0;
	invalidate (true);
}

void TabStrip::setTitle (int i, const char *title)
{
	if (i < 0 || i >= m_n) return;
	int k = 0;
	while (title && title[k] && k < 47) k++;
	bool same = m_title[i][k] == '\0';
	for (int c = 0; same && c < k; c++) if (m_title[i][c] != title[c]) same = false;
	if (same) return;					// (unchanged: no repaint)
	for (int c = 0; c < k; c++) m_title[i][c] = title[c];
	m_title[i][k] = '\0';
	invalidate (true);
}

void TabStrip::setMark (int i, bool on)
{ if (i >= 0 && i < m_n && m_mark[i] != on) { m_mark[i] = on; invalidate (true); } }

void TabStrip::select (int i, bool fire)
{
	if (i < 0 || i >= m_n || i == selected) return;
	selected = i;
	invalidate (true);
	if (fire && onChange) onChange (*this);
}

void TabStrip::selectNext (int d, bool fire)
{
	if (m_n < 2) return;
	select (((selected < 0 ? 0 : selected) + (d < 0 ? m_n - 1 : 1)) % m_n, fire);
}

// The tabs' edges: the room (less the "+") shared out, a tab at most TAB_MAX_W wide.
void TabStrip::place ()
{
	int pres = presentation ();
	TabExt *e = tab_ext (this);
	e->moreX = -1;
	if (pres == UK_TABP_SCROLL)				// (P6) pocket: each tab its title's width, the strip scrolled
	{
		int w[MAXTABS + 1], total = 0;
		for (int i = 0; i < m_n; i++)
		{
			int t = uk_tw (m_title[i], i == selected ? 2 : 0) + 24 + (m_mark[i] ? 12 : 0) + (closable ? CROSS + 6 : 0);
			w[i] = t < 80 ? 80 : t > TAB_MAX_W ? TAB_MAX_W : t;
			total += w[i];
		}
		int room = width - TAB_X0 - (newButton ? PLUS_W + 8 : 4);
		bool over = total > room;
		if (over) { room = width - TAB_X0 - MORE_W - 6; e->moreX = width - MORE_W - 2; }
		int sx = 0, x0 = 0;					// (the chosen tab kept in view)
		for (int i = 0; i < m_n; i++) { if (i == selected) sx = x0; x0 += w[i]; }
		int sw = selected >= 0 && selected < m_n ? w[selected] : 0;
		if (!over) e->scroll = 0;
		else
		{
			if (sx < e->scroll) e->scroll = sx;
			if (sx + sw > e->scroll + room) e->scroll = sx + sw - room;
			if (e->scroll > total - room) e->scroll = total - room;
			if (e->scroll < 0) e->scroll = 0;
		}
		m_x[0] = TAB_X0 - e->scroll;
		for (int i = 0; i < m_n; i++) m_x[i + 1] = m_x[i] + w[i];
		m_plusX = over ? width + 100 : m_x[m_n] + 2;		// (overflowing: "New Tab" in the list)
		return;
	}
	int room = width - TAB_X0 - (newButton ? PLUS_W + 8 : 4);
	int each = m_n ? room / m_n : TAB_MAX_W;
	if (each > TAB_MAX_W) each = TAB_MAX_W;
	if (each < TAB_MIN_W) each = TAB_MIN_W;		// (many tabs on a narrow strip: the last ones cut)
	for (int i = 0; i <= m_n; i++) m_x[i] = TAB_X0 + i * each;
	m_plusX = m_x[m_n] + 2;
}

int TabStrip::tabAt (int mx) const
{
	for (int i = 0; i < m_n; i++) if (mx >= m_x[i] && mx < m_x[i + 1] - TAB_GAP) return i;
	return -1;
}

bool TabStrip::inCross (int i, int mx, int my) const
{
	if (!closable || i < 0 || i >= m_n) return false;
	int cx = m_x[i + 1] - TAB_GAP - CROSS - 6, cy = 4 + (height - 4 - CROSS) / 2;
	return mx >= cx - 2 && mx < cx + CROSS + 2 && my >= cy - 2 && my < cy + CROSS + 2;
}

// (P6) The strip in pocket and console, and on its bottom edge: the tabs (scrolled, a "..." button), a segmented
// control, the current tab's title and its list, the console's header.
static void tab_more (Canvas &cv, int x, int h, unsigned strip, bool hot)
{
	int cy = h / 2 + 1;
	cv.fillRect (x - 4, 0, cv.w - x + 4, h, strip);
	if (hot) uk_rbox (cv, x, cy - 12, MORE_W - 4, 24, 12, uk_tone (strip, 128), uk_tone (strip, 128));
	for (int k = 0; k < 3; k++) uk_glyph (cv, WKG_DOT, x + (MORE_W - 4) / 2 - 7 + k * 7, cy, 5, uk_mix (strip, uk_ink_for (strip), 200));
}

void TabStrip::onDraw ()
{
	int pres = presentation ();
	TabExt *e = tab_ext (this);
	if (pres != UK_TABP_STRIP || e->edge == UK_TAB_BOTTOM)
	{
		place ();
		unsigned bg = bgColor (), strip = uk_tone (bg, 112), act = activeFace ? activeFace : C_BG;
		if (pres == UK_TABP_HEADER)				// console: the current tab big, its neighbours dim
		{
			for (int y = 0; y < height; y++) canvas.fillRect (0, y, width, 1, uk_mix (0x00243A66, 0x000C1428, y * 255 / (height > 1 ? height - 1 : 1)));
			canvas.fillRect (0, height - 1, width, 1, 0x0060C8FF);
			if (m_n == 0) return;
			int cur = selected < 0 ? 0 : selected;
			char b[48];
			uk_text_fit (m_title[cur], width / 2, b, sizeof b, 2);
			uk_text_c (canvas, width / 4, 0, width / 2, height, b, 0x00FFFFFF, 2);
			if (m_n > 1)
			{
				int p = (cur + m_n - 1) % m_n, n = (cur + 1) % m_n;
				uk_text_fit (m_title[p], width / 4 - 44, b, sizeof b); uk_text_l (canvas, 44, 0, height, b, 0x007088A8);
				uk_text_fit (m_title[n], width / 4 - 44, b, sizeof b); uk_text_l (canvas, width - 44 - uk_tw (b), 0, height, b, 0x007088A8);
				uk_rbox (canvas, 6, height / 2 - 9, 30, 18, 5, 0x00304870, 0x00304870); uk_text_c (canvas, 6, height / 2 - 9, 30, 18, "L1", 0x00D8E4F4, 2);
				uk_rbox (canvas, width - 36, height / 2 - 9, 30, 18, 5, 0x00304870, 0x00304870); uk_text_c (canvas, width - 36, height / 2 - 9, 30, 18, "R1", 0x00D8E4F4, 2);
			}
			return;
		}
		canvas.clear (strip);
		if (pres == UK_TABP_SEGMENTED)				// portrait, 3 short tabs at most: a segmented control
		{
			int h = height - 8, x0 = 6, w = width - 12;
			uk_rbox (canvas, x0, 4, w, h, h / 2, uk_tone (strip, 96), uk_tone (strip, 104));
			for (int i = 0; i < m_n; i++)
			{
				int sx = x0 + 2 + i * (w - 4) / m_n, sw = (w - 4) / m_n;
				bool on = i == selected;
				if (on) uk_rbox (canvas, sx, 6, sw, h - 4, (h - 4) / 2, C_ACCENT, uk_tone (C_ACCENT, 90));
				char b[48];
				uk_text_fit (m_title[i], sw - 12, b, sizeof b, on ? 2 : 0);
				uk_text_c (canvas, sx, 6, sw, h - 4, b, on ? uk_ink_on (C_ACCENT) : uk_ink_for (strip), on ? 2 : 0);
			}
			return;
		}
		if (pres == UK_TABP_MENU)				// portrait: the current tab's title, its list on a click
		{
			bool hot = e->hotPart == 4 && !disabled;
			int bw = width - 12 - (newButton && onNew ? PLUS_W + 4 : 0);
			uk_rbox (canvas, 6, 4, bw, height - 8, 6, hot ? uk_tone (act, 104) : act, act);
			uk_rline (canvas, 6, 4, bw, height - 8, 6, uk_tone (act, 70), 120);
			char b[64], t[48];
			int cur = selected < 0 ? 0 : selected;
			if (m_n > 0) uk_text_fit (m_title[cur], bw - 80, t, sizeof t, 2); else t[0] = 0;
			uk_text_l (canvas, 16, 4, height - 8, t, uk_ink_for (act), 2);
			int n = 0; b[n++] = '('; if (m_n >= 10) b[n++] = (char) ('0' + m_n / 10); b[n++] = (char) ('0' + m_n % 10); b[n++] = ')'; b[n] = 0;
			uk_text_l (canvas, 16 + uk_tw (t, 2) + 8, 4, height - 8, b, uk_mix (act, uk_ink_for (act), 150));
			uk_glyph (canvas, WKG_CHEV_DOWN, 6 + bw - 16, height / 2, 9, uk_ink_for (act));
			if (newButton && onNew)
			{
				m_plusX = width - PLUS_W - 4;
				bool hp = m_hotPart == 2 && !disabled;
				int py = (height - 22) / 2;
				if (hp) uk_rbox (canvas, m_plusX, py, 22, 22, 11, uk_tone (strip, 128), uk_tone (strip, 128));
				uk_glyph (canvas, WKG_PLUS, m_plusX + 11, py + 11, 10, uk_mix (strip, uk_ink_for (strip), 190));
			}
			return;
		}
		bool bot = e->edge == UK_TAB_BOTTOM;			// the tabs: a strip (scrolled in pocket), on its edge
		const int top = 4, R = 6;
		canvas.fillRect (0, bot ? 0 : height - 1, width, 1, uk_tone (strip, 96));
		int lim = e->moreX >= 0 ? e->moreX : width;
		for (int i = 0; i < m_n; i++)
		{
			int x = m_x[i], w = m_x[i + 1] - TAB_GAP - x;
			if (x >= lim) break;
			if (x + w < 0) continue;
			bool on = i == selected, hot = i == m_hot && !disabled;
			unsigned face = on ? act : (hot ? uk_tone (strip, 124) : uk_tone (strip, 118));
			if (bot) uk_rbox (canvas, x, -R, w, height - top + R, R, face, face, 255, UK_BL | UK_BR);
			else uk_rbox (canvas, x, top, w, height - top + R, R, face, face, 255, UK_TL | UK_TR);
			if (on) { if (bot) uk_rline (canvas, x, -R, w, height - top + R, R, uk_tone (act, 60), 90, UK_BL | UK_BR); else uk_rline (canvas, x, top, w, height - top + R, R, uk_tone (act, 60), 90, UK_TL | UK_TR); }
			unsigned ink = uk_ink_for (face);
			if (!on) ink = uk_mix (face, ink, 170);
			int ty = bot ? 0 : top, th = height - top, tx = x + 10;
			if (m_mark[i]) { uk_glyph (canvas, WKG_DOT, tx + 3, ty + th / 2, 8, on ? C_ACCENT : uk_mix (face, C_ACCENT, 200)); tx += 12; }
			char b[48];
			uk_text_fit (m_title[i], x + w - tx - (closable ? CROSS + 10 : 8), b, sizeof b, on ? 2 : 0);
			uk_text_l (canvas, tx, ty, th, b, ink, on ? 2 : 0);
			if (closable && (on || hot || pres == UK_TABP_STRIP))
			{
				int cx = x + w - CROSS - 6, cy = ty + (th - CROSS) / 2;
				bool hc = hot && m_hotPart == 1;
				if (hc) uk_rbox (canvas, cx, cy, CROSS, CROSS, CROSS / 2, uk_tone (face, 100), uk_tone (face, 100));
				if (on || hot) uk_glyph (canvas, WKG_CLOSE, cx + CROSS / 2, cy + CROSS / 2, 8, hc ? uk_ink_for (uk_tone (face, 100)) : ink);
			}
		}
		if (e->moreX >= 0) tab_more (canvas, e->moreX, height, strip, e->hotPart == 3);
		if (newButton && m_plusX < lim)
		{
			bool hp = m_hotPart == 2 && !disabled;
			int py = (bot ? 0 : top) + (height - top - 22) / 2;
			if (hp) uk_rbox (canvas, m_plusX, py, 22, 22, 11, uk_tone (strip, 128), uk_tone (strip, 128));
			uk_glyph (canvas, WKG_PLUS, m_plusX + 11, py + 11, 10, uk_mix (strip, uk_ink_for (strip), 190));
		}
		return;
	}
	place ();
	unsigned bg = bgColor ();
	unsigned strip = uk_tone (bg, 112), act = activeFace ? activeFace : C_BG;
	canvas.clear (strip);
	canvas.fillRect (0, height - 1, width, 1, uk_tone (strip, 96));	// (the strip's edge under the others)
	const int top = 4, R = 6;
	for (int i = 0; i < m_n; i++)
	{
		int x = m_x[i], w = m_x[i + 1] - TAB_GAP - x;
		if (x >= width) break;
		bool on = i == selected, hot = i == m_hot && !disabled;
		unsigned face = on ? act : (hot ? uk_tone (strip, 124) : uk_tone (strip, 118));
		uk_rbox (canvas, x, top, w, height - top + R, R, face, face, 255, UK_TL | UK_TR);
		if (on) uk_rline (canvas, x, top, w, height - top + R, R, uk_tone (act, 60), 90, UK_TL | UK_TR);
		unsigned ink = uk_ink_for (face);
		if (!on) ink = uk_mix (face, ink, 170);
		int tx = x + 10;
		if (m_mark[i])					// the dot: something runs there
		{
			uk_glyph (canvas, WKG_DOT, tx + 3, top + (height - top) / 2, 8, on ? C_ACCENT : uk_mix (face, C_ACCENT, 200));
			tx += 12;
		}
		int tw = x + w - tx - (closable ? CROSS + 10 : 8);
		char b[48];
		uk_text_fit (m_title[i], tw, b, sizeof b, on ? 2 : 0);
		uk_text_l (canvas, tx, top, height - top, b, ink, on ? 2 : 0);
		if (closable)
		{
			int cx = x + w - CROSS - 6, cy = top + (height - top - CROSS) / 2;
			bool hc = hot && m_hotPart == 1;
			if (hc) uk_rbox (canvas, cx, cy, CROSS, CROSS, CROSS / 2, uk_tone (face, 100), uk_tone (face, 100));
			if (on || hot) uk_glyph (canvas, WKG_CLOSE, cx + CROSS / 2, cy + CROSS / 2, 8, hc ? uk_ink_for (uk_tone (face, 100)) : ink);
		}
	}
	if (newButton && m_plusX < width)
	{
		bool hp = m_hotPart == 2 && !disabled;
		int py = top + (height - top - 22) / 2;
		if (hp) uk_rbox (canvas, m_plusX, py, 22, 22, 11, uk_tone (strip, 128), uk_tone (strip, 128));
		uk_glyph (canvas, WKG_PLUS, m_plusX + 11, py + 11, 10, uk_mix (strip, uk_ink_for (strip), 190));
	}
}

bool TabStrip::onMouse (int mx, int my, int bl, int, int bm, int wheel)
{
	bool in = mx >= 0 && my >= 0 && mx < width && my < height;
	int pres = presentation ();
	if (pres == UK_TABP_SEGMENTED || pres == UK_TABP_MENU || pres == UK_TABP_HEADER)	// (P6)
	{
		TabExt *e = tab_ext (this);
		int part = !in ? 0 : pres == UK_TABP_MENU && newButton && onNew && mx >= width - PLUS_W - 4 ? 2 : pres == UK_TABP_MENU ? 4 : 0;
		if (part != e->hotPart || (part == 2) != (m_hotPart == 2)) { e->hotPart = part; m_hotPart = part == 2 ? 2 : 0; invalidate (true); }
		if (!in) { pressed = false; return false; }
		if (disabled) return true;
		if (wheel) { selectNext (wheel > 0 ? -1 : 1, true); return true; }
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (pres == UK_TABP_SEGMENTED && m_n > 0) { int i = (mx - 6) * m_n / (width - 12 > 1 ? width - 12 : 1); select (i < 0 ? 0 : i >= m_n ? m_n - 1 : i, true); }
			else if (pres == UK_TABP_HEADER) { if (mx < width / 3) selectNext (-1, true); else if (mx >= width * 2 / 3) selectNext (1, true); }
			else if (part == 2) { if (onNew) onNew (*this); }
			else showList ();
		}
		return true;
	}
	place ();
	if (pres == UK_TABP_SCROLL)				// (P6) the "..." button: the list of the tabs
	{
		TabExt *e = tab_ext (this);
		bool onMore = in && e->moreX >= 0 && mx >= e->moreX;
		if ((e->hotPart == 3) != onMore) { e->hotPart = onMore ? 3 : 0; invalidate (true); }
		if (onMore)
		{
			if (m_hot != -1) { m_hot = -1; m_hotPart = 0; invalidate (true); }
			if (bl && !pressed) pressed = true;
			else if (!bl && pressed) { pressed = false; showList (); }
			return true;
		}
	}
	int hot = in ? tabAt (mx) : -1, part = 0;
	if (hot >= 0 && inCross (hot, mx, my)) part = 1;
	else if (in && hot < 0 && newButton && mx >= m_plusX && mx < m_plusX + 22) part = 2;
	if (hot != m_hot || part != m_hotPart) { m_hot = hot; m_hotPart = part; invalidate (true); }
	if (!in) { pressed = false; m_mid = bm != 0; return false; }
	if (disabled) return true;
	if (wheel) { selectNext (wheel > 0 ? -1 : 1, true); return true; }
	if (bm && !m_mid && hot >= 0 && closable && onClose) { closing = hot; m_mid = true; onClose (*this); return true; }
	m_mid = bm != 0;
	if (bl && !pressed)
	{
		pressed = true;
		if (hot >= 0 && part != 1) select (hot, true);	// (a tab is chosen at the press)
	}
	else if (!bl && pressed)				// the cross and the "+" at the release
	{
		pressed = false;
		if (part == 1 && onClose) { closing = hot; onClose (*this); }
		else if (part == 2 && onNew) onNew (*this);
	}
	return true;
}

} // namespace uikit
