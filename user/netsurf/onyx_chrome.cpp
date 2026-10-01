//
// onyx_chrome.cpp -- NetSurf's window on Onyx (see onyx_chrome.h): a wtk window whose top band
// is a native toolbar -- back, forward, reload / stop, home, the address field -- above the page
// NetSurf draws (the "onyx" libnsfb surface gets the rest of the window's canvas). The window's
// frame is wtk's: its close box and its window menu close NetSurf, maximise resizes the page.
//
// The events: the kapi pointer / key handlers are ours. The band's pointer events go to the wtk
// tree, the page's to the surface's handlers (their y less the band's height); a press keeps
// its side until the button is released (a drag out of a button, a text selection out of the
// page). Keys go to the address field while it has the focus, else to the page -- but for the
// shortcuts (the menu's, F5, Esc, Alt+Left / Right, F6, Ctrl+H).
//
// The history is a native dialog (Navigate > History..., Ctrl+H, the clock button): the pages
// visited, the most recent first, from NetSurf's global history (gui.c keeps it on the card).
//
#include <time.h>
#include "wtk/wtk.h"
#include "onyx_chrome.h"

using namespace wtk;

namespace {

const int TB = ONYX_TOOLBAR_H;
const int BTN_W = 34, BTN_H = 30, BTN_Y = (TB - BTN_H) / 2 - 1, GAP = 4, PAD = 6;
const int URL_MAX = 2048;

// ---- a toolbar button: the theme's framed button with a glyph --------------------------------
class ToolButton : public Widget
{
public:
	int glyph;
	void (*cmd) (void);
	ToolButton (int l, int t, int g, void (*c) (void)) : Widget (l, t, BTN_W, BTN_H), glyph (g), cmd (c) {}
	void setGlyph (int g) { if (g != glyph) { glyph = g; invalidate (true); } }
	void setDisabled (bool d) { if (d != disabled) { disabled = d; if (d) hover = pressed = false; invalidate (true); } }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int st = disabled ? WK_DISABLED : pressed ? WK_PRESSED : hover ? WK_HOT : WK_NORMAL;
		int bx, by, bw, bh;
		wk_framed (canvas, 0, 0, width, height, C_FACE, st, &bx, &by, &bw, &bh);
		wk_glyph (canvas, glyph, bx + bw / 2, by + bh / 2, 13, disabled ? C_DIS : C_TEXT);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
		if (disabled) return true;
		bool wh = hover, wp = pressed;
		hover = true;
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed) { pressed = false; if (cmd) cmd (); }
		if (hover != wh || pressed != wp) invalidate (true);
		return true;
	}
};

// ---- the address field: a one-line editor (wtk's Textbox holds 63 characters) ----------------
void clip_copy (const char *s, int n) { kapi_clipboard_set (CLIP_TEXT, s, (unsigned) n); }

class UrlField : public Widget
{
public:
	char text[URL_MAX];	// what is shown / edited
	char page[URL_MAX];	// the page's address (Esc goes back to it)
	int  len, caret, start;
	bool all;		// the whole text selected (a click into the field, Ctrl+A)
	UrlField (int l, int t, int w, int h) : Widget (l, t, w, h), len (0), caret (0), start (0), all (false)
	{ canFocus = true; text[0] = page[0] = '\0'; }

	void setPage (const char *s)
	{
		int n = 0;
		for (; s && s[n] && n < URL_MAX - 1; n++) page[n] = s[n];
		page[n] = '\0';
		if (!hasFocus) { revert (); invalidate (true); }	// (not while it is being edited)
	}
	void revert ()
	{
		for (len = 0; page[len]; len++) text[len] = page[len];
		text[len] = '\0'; caret = len; start = 0; all = false;
	}
	void focusIn () { setFocus (); all = true; caret = len; invalidate (true); }
	void focusOut () { if (hasFocus) { hasFocus = false; revert (); invalidate (true); } }	// (back to the page)

	int visible () const { int n = (width - 2 * PAD) / wk_fw (); return n < 1 ? 1 : n; }
	void onDraw () override
	{
		int fw = wk_fw (), fh = wk_fh ();
		canvas.clear (bgColor ());
		wk_sunken (canvas, 0, 0, width, height, 4, C_FIELD, hasFocus);
		int vis = visible ();
		if (caret < start) start = caret;
		if (caret > start + vis - 1) start = caret - (vis - 1);
		if (start < 0) start = 0;
		char buf[URL_MAX]; int j = 0;
		for (int c = start; c < len && j < vis; c++) buf[j++] = text[c];
		buf[j] = '\0';
		int ty = (height - fh) / 2;
		if (hasFocus && all && len > 0)
		{
			canvas.fillRect (PAD - 1, ty - 1, j * fw + 2, fh + 2, C_ACCENT);
			canvas.text (PAD, ty, buf, C_SEL_TEXT);
		}
		else canvas.text (PAD, ty, buf, C_FIELD_TEXT);
		if (hasFocus && !all)
			canvas.fillRect (PAD + (caret - start) * fw, ty, 1, fh, C_ACCENT);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0 || !bl) return mx >= 0;
		if (!hasFocus) { focusIn (); return true; }
		int c = start + (mx - PAD + wk_fw () / 2) / wk_fw ();
		caret = c < 0 ? 0 : c > len ? len : c;
		all = false;
		invalidate (true);
		return true;
	}
	void erase () { len = caret = start = 0; text[0] = '\0'; all = false; }
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) { onyx_browser_go (text); focusOut (); return true; }
		if (k == 27) { focusOut (); return true; }
		if (k == 1) { all = true; invalidate (true); return true; }			// ^A
		if (k == 3) { clip_copy (text, len); return true; }				// ^C
		if (k == 24) { clip_copy (text, len); erase (); invalidate (true); return true; }	// ^X
		if (k == 22)									// ^V
		{
			static char clip[URL_MAX];
			int type = 0; unsigned serial = 0;
			int n = kapi_clipboard_get (&type, clip, sizeof clip - 1, &serial);
			if (n > 0 && type == CLIP_TEXT)
			{
				if (all) erase ();
				for (int i = 0; i < n && len < URL_MAX - 1; i++)
				{
					char ch = clip[i];
					if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
					if ((unsigned char) ch < 32) continue;
					for (int m = len; m > caret; m--) text[m] = text[m - 1];
					text[caret++] = ch; len++;
				}
				text[len] = '\0';
				invalidate (true);
			}
			return true;
		}
		switch (k)
		{
		case KEY_LEFT:  if (all) caret = 0; else if (caret > 0) caret--; all = false; break;
		case KEY_RIGHT: if (!all && caret < len) caret++; all = false; break;
		case KEY_HOME:  caret = 0; all = false; break;
		case KEY_END:   caret = len; all = false; break;
		case KEY_BACKSPACE:
			if (all) erase ();
			else if (caret > 0) { for (int m = caret - 1; m < len; m++) text[m] = text[m + 1]; caret--; len--; }
			break;
		case KEY_DEL:
			if (all) erase ();
			else if (caret < len) { for (int m = caret; m < len; m++) text[m] = text[m + 1]; len--; }
			break;
		default:
			if (k < 32 || k > 126) return false;
			if (all) erase ();
			if (len >= URL_MAX - 1) return true;
			for (int m = len; m > caret; m--) text[m] = text[m - 1];
			text[caret++] = (char) k; len++; text[len] = '\0';
			break;
		}
		invalidate (true);
		return true;
	}
};

// ---- the history dialog --------------------------------------------------------------------
// A page visited: its address, its title and its time, as the dialog shows them (the wtk font
// is Latin-1: the UTF-8 title converted).
struct Visit
{
	char *url;		// the address (to go back to it)
	char  title[160];	// Latin-1
	char  when[24];		// "14:05", "Mon 14:05", "12 Sep", "12 Sep 2025"
	char  low[160];		// the title, lower case (the filter)
};

// UTF-8 -> Latin-1 (the wtk font's): a few typographic marks folded to ASCII, the rest '?'.
void to_latin1 (const char *s, char *out, int cap)
{
	const unsigned char *p = (const unsigned char *) s;
	int n = 0;
	while (p && *p && n < cap - 1)
	{
		unsigned c = *p++;
		if (c >= 0x80)
		{
			int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
			if (extra == 0) c = '?';
			else c &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : 0x1F;
			for (; extra > 0 && (*p & 0xC0) == 0x80; extra--) c = (c << 6) | (*p++ & 0x3F);
			if (c == 0x2018 || c == 0x2019) c = '\'';
			else if (c == 0x201C || c == 0x201D) c = '"';
			else if (c == 0x2013 || c == 0x2014 || c == 0x2022) c = '-';
			else if (c == 0x2026 && n < cap - 3) { out[n++] = '.'; out[n++] = '.'; c = '.'; }
			else if (c == 0xA0) c = ' ';
			else if (c > 0xFF) c = '?';
		}
		if (c < 32) c = ' ';
		if (c == ' ' && (n == 0 || out[n - 1] == ' ')) continue;	// (spaces squashed)
		out[n++] = (char) c;
	}
	while (n > 0 && out[n - 1] == ' ') n--;
	out[n] = '\0';
}

char lower_l1 (char ch)
{
	unsigned c = (unsigned char) ch;
	if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) c += 32;
	return (char) c;
}

// Does `hay` (lower case) contain `needle` (as typed)?
bool contains (const char *hay, const char *needle)
{
	if (needle[0] == '\0') return true;
	for (; *hay; hay++)
	{
		int i = 0;
		while (needle[i] && hay[i] && hay[i] == lower_l1 (needle[i])) i++;
		if (needle[i] == '\0') return true;
	}
	return false;
}

// `s` in at most `max` characters (an ellipsis at the end when cut).
void fit (char *out, const char *s, int max)
{
	int n = wk_len (s);
	if (max < 4) max = 4;
	if (n <= max) { for (int i = 0; i <= n; i++) out[i] = s[i]; return; }
	int k = 0;
	for (; k < max - 3; k++) out[k] = s[k];
	out[k++] = '.'; out[k++] = '.'; out[k++] = '.'; out[k] = '\0';
}

// When a page was visited, told the way a person would: the time today, the day this week,
// else the date.
void tell_time (long long when, char *out)
{
	static const char *const MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
					   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static const char *const DAY[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	time_t t = (time_t) when, now = time (0);
	struct tm a, b;
	gmtime_r (&t, &a);		// (Onyx's clock is the local time, counted as UTC)
	gmtime_r (&now, &b);
	long days = (long) (now / 86400) - (long) (t / 86400);
	char *o = out;
	if (days == 0 || days < 0) { }
	else if (days < 7) { for (const char *d = DAY[a.tm_wday]; *d; ) *o++ = *d++; *o++ = ' '; }
	else
	{
		if (a.tm_mday >= 10) *o++ = (char) ('0' + a.tm_mday / 10);
		*o++ = (char) ('0' + a.tm_mday % 10); *o++ = ' ';
		for (const char *m = MON[a.tm_mon % 12]; *m; ) *o++ = *m++;
		if (a.tm_year != b.tm_year)
		{
			int y = a.tm_year + 1900;
			*o++ = ' '; *o++ = (char) ('0' + y / 1000 % 10); *o++ = (char) ('0' + y / 100 % 10);
			*o++ = (char) ('0' + y / 10 % 10); *o++ = (char) ('0' + y % 10);
		}
		*o = '\0';
		return;
	}
	*o++ = (char) ('0' + a.tm_hour / 10); *o++ = (char) ('0' + a.tm_hour % 10); *o++ = ':';
	*o++ = (char) ('0' + a.tm_min / 10);  *o++ = (char) ('0' + a.tm_min % 10);
	*o = '\0';
}

class HistoryDialog;

// The list: a page a row -- its title, its address under it (dimmer), its time at the right.
// Click selects, double-click / Enter opens; the wheel, the scroll bar and the keys scroll.
class HistoryList : public Widget
{
public:
	HistoryDialog *dlg;
	int sel, top;
	bool thumb;
	unsigned lastClick; int lastRow;
	HistoryList (int l, int t, int w, int h, HistoryDialog *d)
	  : Widget (l, t, w, h), dlg (d), sel (-1), top (0), thumb (false), lastClick (0), lastRow (-1)
	{ canFocus = true; }
	static int rowH () { return 2 * wk_fh () + 10; }
	int rows () const { int r = (height - 4) / rowH (); return r < 1 ? 1 : r; }
	int count () const;
	void scrollTo (int t)
	{
		int mx = count () - rows (); if (mx < 0) mx = 0;
		if (t > mx) t = mx;
		if (t < 0) t = 0;
		if (t != top) { top = t; invalidate (true); }
	}
	void select (int i)
	{
		int n = count ();
		if (n == 0) { sel = -1; invalidate (true); return; }
		if (i < 0) i = 0;
		if (i >= n) i = n - 1;
		sel = i;
		if (sel < top) scrollTo (sel);
		if (sel >= top + rows ()) scrollTo (sel - rows () + 1);
		invalidate (true);
	}
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
};

// The filter: a Textbox that tells the dialog when its text changed.
class FilterBox : public Textbox
{
public:
	HistoryDialog *dlg;
	FilterBox (int l, int t, int w, int h, HistoryDialog *d) : Textbox (l, t, w, h, ""), dlg (d) {}
	bool onKey (long k) override;
};

void hd_button (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }

class HistoryDialog : public Modal
{
public:
	enum { OPEN = 1, DELETE = 2, CLEAR = 3 };
	Visit *all; int n, cap;			// every page, the most recent first
	int *vis; int nvis;			// the ones the filter lets through
	HistoryList *list;
	FilterBox *filter;
	char *chosen;				// the address to go to (Open), or 0
	HistoryDialog (int W, int H) : Modal (W < 760 + 40 ? W - 40 : 760, H < 540 + 40 ? H - 40 : 540),
		all (0), n (0), cap (0), vis (0), nvis (0), chosen (0)
	{
		left = (W - width) / 2; top = (H - height) / 2;
		int fh = wk_fh (), y = titleH () + 10;
		filter = new FilterBox (70, y, width - 80, fh + 10, this);
		addChild (filter);
		y += fh + 18;
		list = new HistoryList (10, y, width - 20, height - y - 50, this);
		addChild (list);
		int by = height - 38;
		Button *b;
		b = new Button (10, by, 82, 28, "Open", hd_button);        b->tag = OPEN;   addChild (b);
		b = new Button (100, by, 82, 28, "Delete", hd_button);     b->tag = DELETE; addChild (b);
		b = new Button (190, by, 96, 28, "Clear all", hd_button);  b->tag = CLEAR;  addChild (b);
		b = new Button (width - 92, by, 82, 28, "Close", hd_button); b->tag = 0;    addChild (b);
		load ();
	}
	~HistoryDialog ()
	{
		for (int i = 0; i < n; i++) delete [] all[i].url;
		delete [] all; delete [] vis; delete [] chosen;
	}
	static void add (void *ctx, const char *url, const char *title, long long when)
	{
		HistoryDialog *d = (HistoryDialog *) ctx;
		if (d->n == d->cap)
		{
			int nc = d->cap ? d->cap * 2 : 128;
			Visit *nv = new Visit[nc];
			for (int i = 0; i < d->n; i++) nv[i] = d->all[i];
			delete [] d->all; d->all = nv; d->cap = nc;
		}
		Visit &v = d->all[d->n++];
		int len = wk_len (url);
		v.url = new char[len + 1];
		for (int i = 0; i <= len; i++) v.url[i] = url[i];
		to_latin1 (title, v.title, sizeof v.title);
		if (v.title[0] == '\0') to_latin1 (url, v.title, sizeof v.title);	// (no title: its address)
		int i = 0;
		for (; v.title[i]; i++) v.low[i] = lower_l1 (v.title[i]);
		v.low[i] = '\0';
		tell_time (when, v.when);
	}
	void load ()
	{
		for (int i = 0; i < n; i++) delete [] all[i].url;
		n = 0;
		onyx_browser_history (add, this);
		delete [] vis;
		vis = new int[n > 0 ? n : 1];
		refilter ();
	}
	void refilter ()
	{
		const char *f = filter->text;
		nvis = 0;
		for (int i = 0; i < n; i++)
			if (contains (all[i].low, f) || contains (all[i].url, f)) vis[nvis++] = i;
		list->top = 0;
		list->select (0);
		invalidate (true);
	}
	Visit *at (int row) { return row >= 0 && row < nvis ? &all[vis[row]] : 0; }
	void open (int row)
	{
		Visit *v = at (row);
		if (v == 0) return;
		int len = wk_len (v->url);
		chosen = new char[len + 1];
		for (int i = 0; i <= len; i++) chosen[i] = v->url[i];
		close (OPEN);
	}
	void forget (int row)
	{
		Visit *v = at (row);
		if (v == 0) return;
		onyx_browser_history_forget (v->url);
		int keep = list->sel;
		load ();
		list->select (keep);
	}
	void onButton (int tag) override
	{
		if (tag == OPEN) open (list->sel);
		else if (tag == DELETE) forget (list->sel);
		else if (tag == CLEAR)
		{
			if (n > 0 && wk_messagebox ("Clear history", "Forget every page visited?", MB_YESNO) == 1)
			{ onyx_browser_history_clear (); load (); }
			filter->setFocus ();
		}
		else close (0);
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { open (list->sel); return true; }
		return list->onKey (k);			// (the filter has the focus: the list's keys)
	}
	bool onMouse (int, int, int, int, int, int) override { return true; }	// (modal: all of it)
	void onDraw () override
	{
		drawBox ("History");
		int fh = wk_fh ();
		wk_text_l (canvas, 12, filter->top, filter->height, "Find:", C_TEXT);
		char buf[64];
		int k = 0, v = nvis;
		char num[12]; int m = 0;
		do { num[m++] = (char) ('0' + v % 10); v /= 10; } while (v && m < 11);
		while (m > 0) buf[k++] = num[--m];
		const char *tail = nvis == 1 ? " page" : " pages";
		for (int i = 0; tail[i]; i++) buf[k++] = tail[i];
		buf[k] = '\0';
		wk_text_l (canvas, width - 102 - wk_text_w (buf), height - 38, 28, buf, C_DIS);
		(void) fh;
	}
};

int HistoryList::count () const { return dlg->nvis; }

void HistoryList::onDraw ()
{
	int fw = wk_fw (), fh = wk_fh (), rh = rowH (), R = rows ();
	WkThumb t = wk_thumb (count (), R, top, height - 4);
	int tw = width - (t.show ? WK_SBW + 2 : 0);
	canvas.clear (bgColor ());
	wk_sunken (canvas, 0, 0, width, height, 4, C_FIELD, hasFocus);
	if (count () == 0)
	{
		const char *e = dlg->n == 0 ? "No page visited yet." : "No page matches.";
		wk_text_c (canvas, 0, 0, width, height, e, C_DIS);
		return;
	}
	for (int r = 0; r < R && top + r < count (); r++)
	{
		int i = top + r, y = 2 + r * rh;
		Visit *v = dlg->at (i);
		bool s = i == sel;
		if (s) wk_hilite (canvas, 3, y + 1, tw - 6, rh - 2, 4, true);
		unsigned ink = s ? wk_hilite_ink (true) : C_FIELD_TEXT;
		unsigned dim = s ? wk_hilite_ink (true) : wk_mix (C_FIELD_TEXT, C_FIELD, 110);
		int tcols = (tw - 16) / fw - wk_len (v->when) - 2;
		char line[200];
		fit (line, v->title, tcols);
		wk_text_l (canvas, 10, y + 4, fh, line, ink, 2);			// the title, bold
		wk_text_l (canvas, tw - 8 - wk_text_w (v->when), y + 4, fh, v->when, dim);
		fit (line, v->url, (tw - 16) / fw);
		wk_text_l (canvas, 10, y + 5 + fh, fh, line, dim);		// its address
		if (!s && top + r + 1 < count ())
			canvas.fillRect (8, y + rh - 1, tw - 16, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 28));
	}
	if (t.show) wk_draw_vscroll (canvas, width - WK_SBW - 2, 2, WK_SBW, height - 4, t, C_FIELD, thumb);
}

bool HistoryList::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; thumb = false; return false; }
	if (wheel) { scrollTo (top - wheel); return true; }
	WkThumb t = wk_thumb (count (), rows (), top, height - 4);
	if (thumb)
	{
		if (!bl) { thumb = false; invalidate (true); }
		else scrollTo ((int) wk_thumb_pos (my - 2, height - 4, count (), rows (), t.h));
		return true;
	}
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (t.show && mx >= width - WK_SBW - 2)
		{ thumb = true; invalidate (true); scrollTo ((int) wk_thumb_pos (my - 2, height - 4, count (), rows (), t.h)); return true; }
		int i = top + (my - 2) / rowH ();
		if (i >= count ()) return true;
		unsigned now = kapi_get_ticks ();
		bool dbl = i == lastRow && now - lastClick < 70;
		select (i);
		if (dbl) { dlg->open (i); lastRow = -1; }
		else { lastRow = i; lastClick = now; }
	}
	else if (!bl) pressed = false;
	return true;
}

bool HistoryList::onKey (long k)
{
	int R = rows ();
	switch (k)
	{
	case KEY_UP:   select (sel - 1); return true;
	case KEY_DOWN: select (sel + 1); return true;
	case KEY_PGUP: select (sel - R); return true;
	case KEY_PGDN: select (sel + R); return true;
	case KEY_HOME: select (0); return true;
	case KEY_END:  select (count () - 1); return true;
	case KEY_ENTER: dlg->open (sel); return true;
	case KEY_DEL:  if (hasFocus) dlg->forget (sel); return true;	// (the filter's Del edits it)
	}
	if ((k >= 32 && k <= 126) || (k >= 0xA0 && k <= 0xFF) || k == KEY_BACKSPACE)
	{	// typing in the list: into the filter
		dlg->filter->setFocus ();
		return dlg->filter->onKey (k);
	}
	return false;
}

bool FilterBox::onKey (long k)
{
	char was[64];
	for (int i = 0; i < 64; i++) was[i] = text[i];
	if (k == KEY_ENTER) return false;			// (the dialog's: open the page)
	bool r = Textbox::onKey (k);
	for (int i = 0; i < 64; i++)
		if (was[i] != text[i]) { dlg->refilter (); break; }
		else if (was[i] == '\0') break;
	return r;
}

void open_history ();

// ---- the window ----------------------------------------------------------------------------
bool g_resized;

class NsWindow : public Root
{
public:
	ToolButton *back, *fwd, *reload, *home, *hist;
	UrlField   *url;
	bool        busy;
	NsWindow (int w, int h) : Root (w, h + TB, "NetSurf"), busy (false)
	{
		int x = PAD;
		back   = new ToolButton (x, BTN_Y, WKG_CHEV_LEFT, onyx_browser_back);     x += BTN_W + GAP;
		fwd    = new ToolButton (x, BTN_Y, WKG_CHEV_RIGHT, onyx_browser_forward); x += BTN_W + GAP;
		reload = new ToolButton (x, BTN_Y, WKG_RELOAD, reload_or_stop);           x += BTN_W + GAP;
		home   = new ToolButton (x, BTN_Y, WKG_HOME, onyx_browser_home);          x += BTN_W + GAP;
		hist   = new ToolButton (x, BTN_Y, WKG_HISTORY, open_history);             x += BTN_W + GAP + 2;
		url    = new UrlField (x, BTN_Y, width - x - PAD, BTN_H);
		url->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_TOP;
		back->setDisabled (true);
		fwd->setDisabled (true);
		back->tip = "Back (Alt+Left)"; fwd->tip = "Forward (Alt+Right)"; reload->tip = "Reload (F5)";
		home->tip = "Home"; hist->tip = "History (Ctrl+H)";
		addChild (back); addChild (fwd); addChild (reload); addChild (home); addChild (hist); addChild (url);
		setResizable (true);
	}
	// Only the band is the window's own: the rest is the page's (NetSurf draws it).
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, TB, C_BG);
		wk_etch_h (canvas, 0, TB - 2, width, C_BG);
	}
	void onResized () override { g_resized = true; }
	static void reload_or_stop ();
	bool pageFocus () const { return !url->hasFocus; }
};

NsWindow *g_win;
onyx_chrome_handler g_pagePtr, g_pageKey;
int g_grab;		// 0 none, 1 the band, 2 the page: where the pressed buttons went
bool g_shown;		// the band drawn at least once

void NsWindow::reload_or_stop () { if (g_win && g_win->busy) onyx_browser_stop (); else onyx_browser_reload (); }

bool has_modal (void)
{
	for (Widget *c = g_win ? g_win->firstChild : 0; c; c = c->nextSib)
		if (c->modal) return true;
	return false;
}

// The pointer value with y moved up by the band (clamped to the page's top).
long page_value (long v)
{
	long y = GUI_PTR_Y (v) - TB;
	if (y < 0) y = 0;
	return (v & ~0xFFFFL) | (y & 0xFFFF);
}

unsigned g_inputs;	// the clicks, wheel turns and keys so far (onyx_chrome_input_pending)

// Events taken while a script runs (onyx_chrome_pump_deferred): kept, handled after it.
struct DeferredEvent { unsigned long sender; int ev; long v; bool key; };
enum { DEFER_MAX = 256 };
DeferredEvent g_deferred[DEFER_MAX];
int g_ndeferred;
bool g_defer, g_pumping;

bool defer_event (unsigned long sender, int ev, long v, bool key)
{
	if (!g_defer) return false;
	bool move = !key && ev == GUI_EVENT_PTR_MOVE;
	if (move && g_ndeferred > 0 && !g_deferred[g_ndeferred - 1].key &&
	    g_deferred[g_ndeferred - 1].ev == GUI_EVENT_PTR_MOVE)
		g_deferred[g_ndeferred - 1] = { sender, ev, v, key };	// (a move replaces the move before)
	else if (g_ndeferred < DEFER_MAX)
		g_deferred[g_ndeferred++] = { sender, ev, v, key };	// (full: this one lost)
	return true;
}

void ptr_event (unsigned long sender, int ev, long v);
void key_event (unsigned long sender, int ev, long k);

void replay_deferred ()
{
	for (int i = 0; i < g_ndeferred; i++)
	{
		DeferredEvent e = g_deferred[i];
		if (e.key) key_event (e.sender, e.ev, e.v);
		else ptr_event (e.sender, e.ev, e.v);
	}
	g_ndeferred = 0;
}

void ptr_event (unsigned long sender, int ev, long v)
{
	static int bl, br, bm;
	if (g_win == 0) return;
	if (defer_event (sender, ev, v, false)) return;
	if (ev == GUI_EVENT_PTR_DOWN || ev == GUI_EVENT_PTR_UP || ev == GUI_EVENT_PTR_WHEEL) g_inputs++;
	if (ev == GUI_EVENT_WINCTL)			// a title button (v64)
	{
		if (v == KAPI_FRAME_MENU) { g_win->windowMenu (); onyx_browser_redraw (); }
		else if (v == KAPI_FRAME_MAXIMISE) g_win->maximise (!g_win->maximised ());
		return;
	}
	if (ev == GUI_EVENT_DROP)			// something dropped on the window: open it
	{
		static char data[URL_MAX];
		int type = 0, n = kapi_drag_data (&type, data, sizeof data - 1);
		if (n > 0)
		{
			data[n < URL_MAX - 1 ? n : URL_MAX - 1] = '\0';
			for (char *p = data; *p; p++) if (*p == '\n') { *p = '\0'; break; }	// (the first path)
			onyx_browser_go (data);
		}
		return;
	}
	if (ev < GUI_EVENT_PTR_MOVE || ev > GUI_EVENT_PTR_WHEEL) return;
	int c = GUI_PTR_CHANGED (v);
	if (ev == GUI_EVENT_PTR_DOWN) { if (c & 1) bl = 1; if (c & 2) br = 1; if (c & 4) bm = 1; }
	if (ev == GUI_EVENT_PTR_UP)   { if (c & 1) bl = 0; if (c & 2) br = 0; if (c & 4) bm = 0; }
	int x = GUI_PTR_X (v), y = GUI_PTR_Y (v);
	bool band = has_modal () || (g_grab ? g_grab == 1 : y < TB);
	if (ev == GUI_EVENT_PTR_DOWN && !g_grab) g_grab = band ? 1 : 2;
	if (ev == GUI_EVENT_PTR_LEAVE)
	{
		g_win->handleMouse (-1, -1, 0, 0, 0, 0);
		if (g_pagePtr) g_pagePtr (sender, ev, v);
	}
	else if (band)
	{
		g_win->handleMouse (x, y, bl, br, bm, ev == GUI_EVENT_PTR_WHEEL ? GUI_PTR_WHEEL (v) : 0);
		if (ev == GUI_EVENT_PTR_MOVE && g_pagePtr) g_pagePtr (sender, GUI_EVENT_PTR_LEAVE, 0);
	}
	else
	{
		if (ev == GUI_EVENT_PTR_DOWN && !g_win->pageFocus ()) g_win->url->focusOut ();
		g_win->handleMouse (-1, -1, 0, 0, 0, 0);		// (the band: the pointer left it)
		if (g_pagePtr) g_pagePtr (sender, ev, page_value (v));
	}
	if (ev == GUI_EVENT_PTR_UP && !bl && !br && !bm) g_grab = 0;
}

void key_event (unsigned long sender, int ev, long k)
{
	if (g_win == 0 || ev != GUI_EVENT_KEY) return;
	if (defer_event (sender, ev, k, true)) return;
	g_inputs++;
	if (has_modal ()) { g_win->handleKey (k); return; }
	unsigned mods = kapi_get_modifiers ();
	if (k == KEY_BACKSPACE && (mods & MOD_CTRL)) { open_history (); return; }	// Ctrl+H (^H is 8)
	if (Menu::current () && Menu::current ()->shortcut (k)) return;
	if ((mods & MOD_ALT) && k == KEY_LEFT)  { onyx_browser_back (); return; }
	if ((mods & MOD_ALT) && k == KEY_RIGHT) { onyx_browser_forward (); return; }
	if (k == KEY_F1 + 4) { onyx_browser_reload (); return; }		// F5
	if (k == KEY_F1 + 5) { g_win->url->focusIn (); return; }		// F6
	if (!g_win->pageFocus ()) { g_win->handleKey (k); return; }
	if (k == 27 && g_win->busy) { onyx_browser_stop (); return; }
	if (g_pageKey) g_pageKey (sender, ev, k);
}

// ---- the system menu bar -------------------------------------------------------------------
void m_location () { if (g_win) g_win->url->focusIn (); }
void m_back ()     { onyx_browser_back (); }
void m_forward ()  { onyx_browser_forward (); }
void m_reload ()   { onyx_browser_reload (); }
void m_stop ()     { onyx_browser_stop (); }
void m_home ()     { onyx_browser_home (); }
void m_desktop ()  { onyx_browser_toggle_desktop (); }	// the site's desktop / mobile version

// The History dialog: shown over the page (NetSurf waits meanwhile); a page chosen is opened.
void open_history ()
{
	if (g_win == 0 || has_modal ()) return;
	if (g_win->url->hasFocus) g_win->url->focusOut ();
	HistoryDialog *d = new HistoryDialog (g_win->width, g_win->height);
	d->filter->setFocus ();
	int r = d->run ();
	char *go = r == HistoryDialog::OPEN ? d->chosen : 0;
	d->chosen = 0;
	delete d;
	onyx_browser_redraw ();			// (the page under the dialog)
	if (go) { onyx_browser_go (go); delete [] go; }
}

Menu g_menu;

} // namespace

// ---- the C interface -------------------------------------------------------------------------
extern "C" {

unsigned *onyx_chrome_open (int w, int h, int *stride)
{
	if (g_win) return onyx_chrome_page (stride, 0, 0);
	g_win = new NsWindow (w, h);
	g_menu.menu ("File");
	g_menu.item ("Open Location...", "^L", WK_CTRL ('L'), m_location);
	g_menu.menu ("Navigate");
	g_menu.item ("Back", "Alt+Left", 0, m_back);
	g_menu.item ("Forward", "Alt+Right", 0, m_forward);
	g_menu.separator ();
	g_menu.item ("Reload", "^R", WK_CTRL ('R'), m_reload);
	g_menu.item ("Stop", "Esc", 0, m_stop);
	g_menu.separator ();
	g_menu.item ("Desktop Site / Mobile Site", "", 0, m_desktop);
	g_menu.separator ();
	g_menu.item ("Home", "", 0, m_home);
	g_menu.item ("History...", "^H", 0, open_history);	// (^H: key_event -- it is Backspace's code)
	g_menu.publish ();
	kapi_set_pointer_handler (ptr_event);
	kapi_set_key_handler (key_event);
	return onyx_chrome_page (stride, 0, 0);
}

unsigned *onyx_chrome_page (int *stride, int *w, int *h)
{
	if (g_win == 0) return 0;
	Canvas &c = g_win->canvas;
	if (stride) *stride = c.stride;
	if (w) *w = c.w;
	if (h) *h = c.h - TB;
	return c.px + (long) TB * c.stride;
}

void onyx_chrome_default_size (int *w, int *h)
{
	int sw = 1024, sh = 768;
	kapi_screen_size (&sw, &sh);
	int pw = sw - 64, ph = sh - TB - 28 - 8 - 24 - 90;	// the band, the title + border, the menu bar, the dock
	if (pw > 1280) pw = 1280;
	if (ph > 960) ph = 960;
	if (pw < 480) pw = 480;
	if (ph < 300) ph = 300;
	*w = pw; *h = ph;
}

void onyx_chrome_theme (unsigned *face, unsigned *shade)
{
	*face = C_BG;
	*shade = wk_tone (C_BG, 96);
}

void onyx_chrome_set_page_handlers (onyx_chrome_handler ptr, onyx_chrome_handler key)
{
	g_pagePtr = ptr;
	g_pageKey = key;
}

int onyx_chrome_pump (void)
{
	return onyx_chrome_pump_wait (0);
}

// Pump the window's events, first waiting up to ms for one (an input event, a post from a
// fetch thread, the close box) -- not a blind sleep. 1: the window was resized.
int onyx_chrome_pump_wait (int ms)
{
	if (g_win == 0) return 0;
	if (g_ndeferred > 0) { replay_deferred (); ms = 0; }	// (taken while a script ran)
	g_pumping = true;
	if (ms > 0) kapi_pump_wait ((unsigned) ms);
	else kapi_pump_events ();
	g_pumping = false;
	if (!g_win->valid || !g_shown)
	{
		g_win->draw ();
		g_shown = true;
		kapi_present ();
	}
	int r = g_resized;
	g_resized = false;
	return r;
}

// The scheduler's question between two callbacks: did the user click, turn the wheel, type,
// resize or close since? The events are taken (the page's wait in the surface's ring for the
// main loop, which then runs before the next callbacks: a heavy page's timers and scripts
// no longer keep a click waiting for the whole budget).
int onyx_chrome_input_pending (void)
{
	unsigned before = g_inputs;
	if (g_win == 0) return 0;
	if (g_ndeferred > 0) return 1;
	g_pumping = true;
	kapi_pump_events ();
	g_pumping = false;
	return g_inputs != before || g_resized || kapi_should_exit ();
}

// While a script runs long (its interrupt handler, every 100 ms): the window's events taken
// (the system's queue drained -- no "not pumping" freeze, nothing dropped) but kept, handled
// once the script is done (a click then, as the user did it); the fetch threads' posts run.
void onyx_chrome_pump_deferred (void)
{
	if (g_win == 0 || g_pumping || g_defer) return;
	g_defer = true;
	g_pumping = true;
	kapi_pump_events ();
	g_pumping = false;
	g_defer = false;
}

void onyx_chrome_present (void)
{
	if (g_win && !g_win->valid) g_win->draw ();
	kapi_present ();
}

// The page's redraws say only that the window changed; the main loop presents once an
// iteration, after all of them (a scroll's moved pixels and its new band shown together).
static bool g_present_due;

void onyx_chrome_present_later (void)
{
	g_present_due = true;
}

void onyx_chrome_flush (void)
{
	if (!g_present_due) return;
	g_present_due = false;
	onyx_chrome_present ();
}

void onyx_chrome_set_url (const char *url)
{
	if (g_win) g_win->url->setPage (url);
}

void onyx_chrome_set_busy (int busy)
{
	if (g_win == 0) return;
	g_win->busy = busy != 0;
	g_win->reload->setGlyph (busy ? WKG_CLOSE : WKG_RELOAD);
}

void onyx_chrome_set_nav (int can_back, int can_forward)
{
	if (g_win == 0) return;
	g_win->back->setDisabled (!can_back);
	g_win->fwd->setDisabled (!can_forward);
}

} // extern "C"
