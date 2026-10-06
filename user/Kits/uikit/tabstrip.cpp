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

namespace uikit {

static const int TAB_MAX_W = 200, TAB_MIN_W = 60, TAB_GAP = 3, TAB_X0 = 6, PLUS_W = 26, CROSS = 16;

TabStrip::TabStrip (int l, int t, int w, int h, Action change)
  : Widget (l, t, w, h), selected (-1), closing (-1), closable (true), newButton (true), activeFace (0),
    onChange (change), onClose (0), onNew (0), m_n (0), m_hot (-1), m_hotPart (0), m_plusX (0), m_mid (false)
{
	m_x[0] = TAB_X0;
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

void TabStrip::onDraw ()
{
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
	place ();
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
