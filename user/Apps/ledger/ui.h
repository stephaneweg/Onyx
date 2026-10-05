//
// ui.h -- what Ledger's pages share: the books open (g_b, their file, the fiscal year shown), the status
// word, the app's commands the pages call (main.cpp); and the look's pieces, drawn in the theme's colours
// (uikit/paint.h): the pages' icons, a page's header, the flat buttons (an accent one for the page's main
// action), the status pills (paid, open, overdue...), the dashboard's tiles, the table's cells (amounts
// at the right, what is negative in red), the questions asked (AskBox).
//
#ifndef _ledger_ui_h
#define _ledger_ui_h

#include "uikit/uikit.h"
#include "systemkit/clipboard.h"
#include "Apps/cardfile/widgets.h"
#include "setup.h"
#include "fileio.h"
#include "reports.h"

namespace lg {

using namespace uikit;

enum { SIDE_W = 208, HEAD_H = 58, STATUS_H = 24, ROW_H = 26 };

// ---- the pages ---------------------------------------------------------------------------------------------------------
enum { P_OVERVIEW, P_SALES, P_PURCH, P_FIN, P_MISC, P_CUST, P_SUPP, P_ACCOUNTS, P_REPORTS, P_VAT, P_SETTINGS, P_DOCS, P_COUNT,
       E_INVOICE = P_COUNT, E_STATEMENT, E_MISC, E_CDOC, NPAGES };

// ---- the books open ------------------------------------------------------------------------------------------------------
static Book     g_b;
static char     g_path[200];			// their file ("": none open)
static int      g_year = -1;			// the fiscal year shown (g_b.yr's index)
static unsigned g_yearVer = 1;			// bumped when another year is chosen (the pages follow)
static bool     g_saveFailed;			// the last save failed (the status says it)

static int yfrom () { return g_year >= 0 && g_year < g_b.nyr ? g_b.yr[g_year].start : 0; }
static int yto () { return g_year >= 0 && g_year < g_b.nyr ? g_b.yr[g_year].end : 99991231; }
// The day a new document takes: today when it is in the year shown, else the year's nearest end.
static int default_date ()
{
	int t = today_ymd ();
	if (g_year < 0 || g_year >= g_b.nyr) return t;
	if (t < yfrom ()) return yfrom ();
	if (t > yto ()) return yto ();
	return t;
}

// (main.cpp)
static void status (const char *msg);		// a word in the status line for a few seconds
static void changed ();				// the books changed: saved, the pages follow
static void go (int page);			// a page shown (the side bar's)
static void open_entry (int id);		// an entry's document opened in its editor
static void new_document (int journal, bool credit = false, int back = -1);	// (back: the page Save goes back to)
static int  ask (const char *title, const char *text, int buttons, int icon = 1);
static void open_party (int id);		// its page, its card
static void open_account (const char *code);	// the chart, that account's register
static void cmd_import_coda ();			// a bank's CODA file: its statements shown one after the other
static bool coda_saved ();			// (an imported statement saved: the next one shown -- false: none left)
static void coda_stop ();			// (the import given up)
static void cmd_pay ();				// the suppliers paid: a SEPA file (payui.h)

// ---- colours ---------------------------------------------------------------------------------------------------------------
static const unsigned C_GOOD = 0x00248A45, C_BAD = 0x00C8402E, C_WARN = 0x00C7801A, C_BLUE = 0x002F6FC0, C_PURPLE = 0x007A4DB0;
static unsigned dim_ink (unsigned bg) { return uk_mix (bg, uk_ink_for (bg), 150); }
static unsigned field_dim () { return uk_mix (C_FIELD, C_FIELD_TEXT, 140); }

// ---- the icons (20 x 20 at x, y) ----------------------------------------------------------------------------------------------
enum { NI_OVERVIEW, NI_SALES, NI_PURCH, NI_BANK, NI_MISC, NI_CUST, NI_SUPP, NI_CHART, NI_REPORT, NI_VAT, NI_SETTINGS,
       NI_PLUS, NI_TRASH, NI_EDIT, NI_EXPORT, NI_BACK, NI_LINK, NI_CHECK, NI_DOC, NI_LOCK, NI_ORDERS, NI_PRINT, NI_NEXT, NI_IMPORT, NI_COUNT };
static const int PAGE_ICON[NPAGES] = { NI_OVERVIEW, NI_SALES, NI_PURCH, NI_BANK, NI_MISC, NI_CUST, NI_SUPP, NI_CHART, NI_REPORT,
				       NI_VAT, NI_SETTINGS, NI_ORDERS, NI_DOC, NI_BANK, NI_MISC, NI_ORDERS };

static void draw_ni (Canvas &cv, int k, int x, int y, unsigned ink, unsigned accent)
{
	VPath p;
	int X = V (x), Y = V (y);
	switch (k)
	{
	case NI_OVERVIEW:
		p.rrect (X + V (2), Y + V (2), V (7), V (7), V (2)); p.rrect (X + V (11), Y + V (2), V (7), V (7), V (2));
		p.rrect (X + V (2), Y + V (11), V (7), V (7), V (2)); p.fill (cv, ink);
		p.clear (); p.rrect (X + V (11), Y + V (11), V (7), V (7), V (2)); p.fill (cv, accent);
		break;
	case NI_SALES: case NI_DOC:					// an invoice (a sale's: an arrow going out)
	{
		int pts[10] = { X + V (3), Y + V (1), X + V (11), Y + V (1), X + V (16), Y + V (6), X + V (16), Y + V (19), X + V (3), Y + V (19) };
		p.polyline (pts, 5, 24, true); p.fill (cv, ink);
		p.clear (); p.rect (X + V (6), Y + V (8), V (7), 22); p.rect (X + V (6), Y + V (11), V (7), 22); p.rect (X + V (6), Y + V (14), V (4), 22); p.fill (cv, ink);
		if (k == NI_SALES)
		{
			p.clear (); p.circle (X + V (15), Y + V (15), V (5)); p.fill (cv, accent);
			p.clear (); p.line (X + V (13), Y + V (17), X + V (17) + 4, Y + V (13) - 4, 30); p.arrowHead (X + V (18), Y + V (12), 45, V (4), V (3)); p.fill (cv, 0xFFFFFF);
		}
		break;
	}
	case NI_PURCH:							// a shopping bag
	{
		int bag[8] = { X + V (3), Y + V (7), X + V (17), Y + V (7), X + V (16), Y + V (19), X + V (4), Y + V (19) };
		p.poly (bag, 4); p.fill (cv, ink);
		p.clear (); p.arc (X + V (10), Y + V (7), V (4), 0, 180, 28); p.fill (cv, ink);
		p.clear (); p.rect (X + V (5), Y + V (11), V (10), V (2)); p.fill (cv, accent);
		break;
	}
	case NI_BANK:							// a bank: its pediment and columns
	{
		int roof[6] = { X + V (1), Y + V (7), X + V (10), Y + V (1), X + V (19), Y + V (7) };
		p.poly (roof, 3);
		p.rect (X + V (3), Y + V (8), V (3), V (8)); p.rect (X + V (8) + 8, Y + V (8), V (3), V (8)); p.rect (X + V (14), Y + V (8), V (3), V (8));
		p.rect (X + V (1), Y + V (17), V (18), V (2)); p.fill (cv, ink);
		p.clear (); p.circle (X + V (10), Y + V (5), V (1) + 6); p.fill (cv, accent);
		break;
	}
	case NI_MISC:							// the journal: a book, its spine, its lines
	{
		int bk[8] = { X + V (4), Y + V (2), X + V (16), Y + V (2), X + V (16), Y + V (18), X + V (4), Y + V (18) };
		p.polyline (bk, 4, 26, true); p.rect (X + V (4), Y + V (2), V (3), V (16)); p.fill (cv, ink);
		p.clear (); p.rect (X + V (9), Y + V (6), V (5), 24); p.rect (X + V (9), Y + V (9), V (5), 24); p.rect (X + V (9), Y + V (12), V (3), 24); p.fill (cv, accent);
		break;
	}
	case NI_CUST:							// a person
		p.circle (X + V (10), Y + V (6), V (4)); p.fill (cv, ink);
		p.clear (); p.rrect (X + V (3), Y + V (12), V (14), V (7), V (4)); p.fill (cv, ink);
		p.clear (); p.circle (X + V (16), Y + V (4), V (2) + 8); p.fill (cv, accent);
		break;
	case NI_SUPP:							// a delivery van
	{
		p.rrect (X + V (1), Y + V (4), V (11), V (10), V (1));
		int cab[10] = { X + V (12), Y + V (7), X + V (16), Y + V (7), X + V (19), Y + V (10), X + V (19), Y + V (14), X + V (12), Y + V (14) };
		p.poly (cab, 5); p.fill (cv, ink);
		p.clear (); p.circle (X + V (5), Y + V (15), V (2) + 8); p.circle (X + V (15), Y + V (15), V (2) + 8); p.fill (cv, ink);
		p.clear (); p.circle (X + V (5), Y + V (15), V (1)); p.circle (X + V (15), Y + V (15), V (1)); p.fill (cv, accent);
		break;
	}
	case NI_CHART:							// a tree of boxes
		p.rrect (X + V (1), Y + V (1), V (8), V (5), V (1)); p.fill (cv, accent);
		p.clear (); p.rrect (X + V (10), Y + V (8), V (9), V (4), V (1)); p.rrect (X + V (10), Y + V (15), V (9), V (4), V (1));
		p.rect (X + V (4), Y + V (6), 24, V (11)); p.rect (X + V (4), Y + V (10) - 12, V (6), 24); p.rect (X + V (4), Y + V (17) - 12, V (6), 24);
		p.fill (cv, ink);
		break;
	case NI_REPORT:							// a bar chart
		p.rrect (X + V (2), Y + V (10), V (4), V (8), 10); p.rrect (X + V (14), Y + V (7), V (4), V (11), 10); p.rect (X + V (1), Y + V (18), V (18), 24);
		p.fill (cv, ink);
		p.clear (); p.rrect (X + V (8), Y + V (3), V (4), V (15), 10); p.fill (cv, accent);
		break;
	case NI_VAT:							// a percent sign
		p.circle (X + V (5), Y + V (5), V (3) + 8); p.hole (X + V (5), Y + V (5), V (1) + 8);
		p.circle (X + V (15), Y + V (15), V (3) + 8); p.hole (X + V (15), Y + V (15), V (1) + 8);
		p.fill (cv, ink);
		p.clear (); p.line (X + V (16), Y + V (3), X + V (4), Y + V (17), 40); p.fill (cv, accent);
		break;
	case NI_SETTINGS: uk_glyph (cv, WKG_GEAR, x + 10, y + 10, 18, ink); break;
	case NI_PLUS: p.rect (X + V (9), Y + V (3), V (2), V (14)); p.rect (X + V (3), Y + V (9), V (14), V (2)); p.fill (cv, ink); break;
	case NI_TRASH:
		p.rrect (X + V (5), Y + V (6), V (10), V (13), V (2)); p.fill (cv, ink);
		p.clear (); p.rect (X + V (3), Y + V (3), V (14), V (2)); p.rect (X + V (8), Y + V (1), V (4), V (2)); p.fill (cv, ink);
		p.clear (); p.rect (X + V (8), Y + V (9), 24, V (7)); p.rect (X + V (11), Y + V (9), 24, V (7)); p.fill (cv, accent);
		break;
	case NI_EDIT:							// a pencil
		p.line (X + V (4), Y + V (16), X + V (15), Y + V (5), V (4)); p.fill (cv, ink);
		p.clear (); p.line (X + V (14), Y + V (6), X + V (17), Y + V (3), V (4)); p.fill (cv, accent);
		break;
	case NI_EXPORT:							// a tray and an arrow out of it
	{
		int tray[8] = { X + V (2), Y + V (11), X + V (2), Y + V (18), X + V (18), Y + V (18), X + V (18), Y + V (11) };
		p.polyline (tray, 4, 32); p.fill (cv, ink);
		p.clear (); p.line (X + V (10), Y + V (14), X + V (10), Y + V (4), 36); p.arrowHead (X + V (10), Y + V (2), 90, V (6), V (5)); p.fill (cv, accent);
		break;
	}
	case NI_BACK: uk_glyph (cv, WKG_CHEV_LEFT, x + 10, y + 10, 14, ink); break;
	case NI_LINK:							// two links of a chain
		p.rrect (X + V (1), Y + V (6), V (11), V (8), V (4)); p.fill (cv, ink);
		p.clear (); p.rrect (X + V (3), Y + V (8), V (7), V (4), V (2)); p.fill (cv, 0xFFFFFF);
		p.clear (); p.rrect (X + V (8), Y + V (6), V (11), V (8), V (4)); p.fill (cv, accent);
		p.clear (); p.rrect (X + V (10), Y + V (8), V (7), V (4), V (2)); p.fill (cv, 0xFFFFFF);
		break;
	case NI_CHECK: uk_glyph (cv, WKG_CHECK, x + 10, y + 10, 14, ink); break;
	case NI_LOCK: uk_glyph (cv, WKG_LOCK, x + 10, y + 10, 16, ink); break;
	case NI_ORDERS:							// a clipboard: its clip, its ticked lines
	{
		int bd[8] = { X + V (4), Y + V (3), X + V (16), Y + V (3), X + V (16), Y + V (19), X + V (4), Y + V (19) };
		p.polyline (bd, 4, 26, true); p.fill (cv, ink);
		p.clear (); p.rrect (X + V (7), Y + V (1), V (6), V (4), V (1)); p.fill (cv, ink);
		p.clear (); p.rect (X + V (7), Y + V (8), V (2), V (2)); p.rect (X + V (7), Y + V (13), V (2), V (2)); p.fill (cv, accent);
		p.clear (); p.rect (X + V (10), Y + V (8) + 8, V (4), 24); p.rect (X + V (10), Y + V (13) + 8, V (4), 24); p.fill (cv, ink);
		break;
	}
	case NI_PRINT:							// a printer, its sheet
		p.rrect (X + V (1), Y + V (7), V (18), V (8), V (2)); p.fill (cv, ink);
		p.clear (); p.rect (X + V (5), Y + V (2), V (10), V (5)); p.rect (X + V (5), Y + V (12), V (10), V (7)); p.fill (cv, 0xFFFFFF);
		p.clear (); p.rect (X + V (5), Y + V (2), V (10), 20); p.rect (X + V (5), Y + V (2), 20, V (5)); p.rect (X + V (15) - 20, Y + V (2), 20, V (5));
		p.rect (X + V (5), Y + V (12), 20, V (7)); p.rect (X + V (15) - 20, Y + V (12), 20, V (7)); p.rect (X + V (5), Y + V (19) - 20, V (10), 20); p.fill (cv, ink);
		p.clear (); p.rect (X + V (7), Y + V (14), V (6), 20); p.rect (X + V (7), Y + V (16), V (4), 20); p.fill (cv, accent);
		break;
	case NI_IMPORT:							// a tray and an arrow into it
	{
		int tray[8] = { X + V (2), Y + V (11), X + V (2), Y + V (18), X + V (18), Y + V (18), X + V (18), Y + V (11) };
		p.polyline (tray, 4, 32); p.fill (cv, ink);
		p.clear (); p.line (X + V (10), Y + V (1), X + V (10), Y + V (10), 36); p.arrowHead (X + V (10), Y + V (14), 270, V (6), V (5)); p.fill (cv, accent);
		break;
	}
	case NI_NEXT:							// an arrow going on
		p.line (X + V (3), Y + V (10), X + V (13), Y + V (10), 40); p.arrowHead (X + V (17), Y + V (10), 0, V (7), V (6)); p.fill (cv, accent);
		break;
	}
}

// A widget as a widget: its place (DataGrid's own `top` / `left` are its rows and columns scrolled).
static inline Widget *wg (Widget *w) { return w; }

// ---- text helpers ---------------------------------------------------------------------------------------------------------
// The text cut to w px ("..." at its end when it is longer).
static void fit_text (const char *s, int w, char *out, int cap, int style = 0)
{
	scpy (out, s, cap);
	if (uk_text_w (out, style) <= w) return;
	int n = slen (out);
	while (n > 0) { out[--n] = '\0'; if (uk_text_w (out, style) + uk_text_w ("...", style) <= w) break; }
	scat (out, "...", cap);
}
static void text_fit_l (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int style = 0)
{
	char t[256]; fit_text (s, w, t, sizeof t, style);
	uk_text_l (cv, x, y, h, t, c, style);
}
static void text_r (Canvas &cv, int xr, int y, int h, const char *s, unsigned c, int style = 0)
{
	uk_text_l (cv, xr - uk_text_w (s, style), y, h, s, c, style);
}
static const char *money_s (money v, char *buf) { fmt_money (v, buf); return buf; }

// ---- the page's frame ---------------------------------------------------------------------------------------------------------
// What every page is: a view of the books, read again when they change (or another year is shown).
class Page : public Widget
{
public:
	unsigned seen, seenYear;
	int id;
	Page (int id_) : Widget (0, 0, 100, 100), seen (0), seenYear (0), id (id_) { anchor = ANCHOR_FILL; }
	virtual const char *title () = 0;
	virtual void subtitle (char *out, int cap) { out[0] = '\0'; (void) cap; }
	virtual void refresh () {}			// the books / the year: read again
	virtual void enter () {}			// shown: its main control focused
	virtual bool leave () { return true; }		// hidden: false when it must stay (an editor's question)
	virtual void tick () {}				// each turn of the window's loop
	virtual void cmdNew () {}			// Ctrl+N
	virtual void cmdFind () {}			// Ctrl+F
	virtual void cmdDelete () {}			// Delete in a list
	unsigned bgColor () override { return C_BG; }
	void sync () { if (seen != g_b.changes || seenYear != g_yearVer) { seen = g_b.changes; seenYear = g_yearVer; refresh (); } }
	// The header: the page's icon, its title, a line below it; the page's background.
	void drawHead ()
	{
		canvas.clear (C_BG);
		// (the text stops before the header's buttons)
		int lim = width - 16;
		for (Widget *c = firstChild; c; c = c->nextSib) if (!c->hidden && c->top < HEAD_H - 12 && c->left > 160) lim = imin (lim, c->left - 14);
		draw_ni (canvas, PAGE_ICON[id], 18, 12, uk_ink_for (C_BG), C_ACCENT);
		text_fit_l (canvas, 48, 9, lim - 48, 24, title (), C_TEXT, 2);
		char s[200]; subtitle (s, sizeof s);
		if (s[0]) text_fit_l (canvas, 48, 30, lim - 48, 20, s, dim_ink (C_BG));
		uk_etch_h (canvas, 14, HEAD_H - 2, width - 28, C_BG);
	}
	void onDraw () override { drawHead (); }
};

// ---- buttons --------------------------------------------------------------------------------------------------------------------
// A flat button: secondary (the face's tone, an outline), primary (the accent: the page's main action) or
// quiet (text only, until pointed); an icon before its text if it has one.
enum { FB_SECONDARY, FB_PRIMARY, FB_QUIET, FB_DANGER };
class FlatButton : public Widget
{
public:
	char text[48]; void (*cb) (); int icon, kind;
	FlatButton (const char *s, void (*cb_) (), int kind_ = FB_SECONDARY, int icon_ = -1)
		: Widget (0, 0, 10, 30), cb (cb_), icon (icon_), kind (kind_), m_hot (false)
	{ setText (s); }
	void setText (const char *s)
	{
		scpy (text, s, sizeof text);
		int w = uk_text_w (text, kind == FB_PRIMARY ? 2 : 0) + 26 + (icon >= 0 ? 22 : 0);
		if (!text[0]) w = 34;
		if (w != width) resizeTo (w, height);
		invalidate (true);
	}
	void setDisabled (bool d) { if (d != disabled) { disabled = d; invalidate (true); } }
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		bool dn = pressed && m_hot;
		unsigned ink;
		if (kind == FB_PRIMARY)
		{
			unsigned a = disabled ? uk_mix (C_ACCENT, bg, 150) : C_ACCENT;
			int t = disabled ? 128 : dn ? 108 : m_hot ? 150 : 132;
			uk_rbox (canvas, 0, 0, width, height, 6, uk_tone (a, t + 14), uk_tone (a, t - 12));
			uk_rline (canvas, 0, 0, width, height, 6, uk_tone (a, 80), 170);
			ink = uk_ink_on (a);
		}
		else if (kind == FB_QUIET && !m_hot)
			ink = disabled ? uk_mix (bg, C_TEXT, 90) : C_ACCENT;
		else
		{
			unsigned f = C_BUTTON;
			int t = disabled ? 128 : dn ? 112 : m_hot ? 158 : 140;
			uk_rbox (canvas, 0, 0, width, height, 6, uk_tone (f, t + 10), uk_tone (f, t - 8));
			uk_rline (canvas, 0, 0, width, height, 6, uk_tone (f, 84), disabled ? 90 : 160);
			ink = disabled ? uk_mix (f, C_BUTTON_TEXT, 100) : kind == FB_DANGER ? C_BAD : C_BUTTON_TEXT;
			if (kind == FB_QUIET) ink = C_ACCENT;
		}
		int tw = uk_text_w (text, kind == FB_PRIMARY ? 2 : 0), iw = icon >= 0 ? (text[0] ? 22 : 20) : 0;
		int x = (width - tw - iw) / 2 + (dn ? 1 : 0), y = dn ? 1 : 0;
		if (icon >= 0) draw_ni (canvas, icon, x, (height - 20) / 2 + y, ink, kind == FB_PRIMARY ? ink : kind == FB_DANGER ? C_BAD : C_ACCENT);
		if (text[0]) uk_text_l (canvas, x + iw, y, height, text, ink, kind == FB_PRIMARY ? 2 : 0);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != m_hot) { m_hot = in; invalidate (true); }
		if (disabled) return in;
		if (bl && in && !pressed) { pressed = true; invalidate (true); }
		else if (!bl && pressed) { pressed = false; invalidate (true); if (in && cb) cb (); }
		return in;
	}
private:
	bool m_hot;
};

// Buttons in a row at the right of a page's header (they ride its right edge).
struct HeadRow
{
	Widget *page; int x;
	HeadRow (Widget *p) : page (p), x (p->width - 16) {}
	FlatButton *add (const char *s, void (*cb) (), int kind = FB_SECONDARY, int icon = -1, const char *tip = 0)
	{
		FlatButton *b = new FlatButton (s, cb, kind, icon);
		x -= b->width; b->left = x; b->top = (HEAD_H - b->height) / 2 - 1; x -= 8;
		b->anchor = ANCHOR_RIGHT | ANCHOR_TOP; b->tip = tip;
		page->addChild (b);
		return b;
	}
	void gap (int g) { x -= g; }
};

// Segments side by side, one lit (a filter: All / Open / Overdue; a settings' part).
class Segmented : public Widget
{
public:
	enum { MAXS = 8 };
	const char *seg[MAXS]; int n, cur; void (*onPick) (int);
	Segmented (int l, int t, const char *const *s, int n_, void (*cb) (int)) : Widget (l, t, 10, 28), n (imin (n_, MAXS)), cur (0), onPick (cb), m_hot (-1), m_down (-1)
	{
		int w = 0;
		for (int i = 0; i < n; i++) { seg[i] = s[i]; w += uk_text_w (s[i], 2) + 26; }
		resizeTo (w, 28);
	}
	int segX (int i) const { int x = 0; for (int k = 0; k < i; k++) x += uk_text_w (seg[k], 2) + 26; return x; }
	void set (int v) { if (v != cur) { cur = v; invalidate (true); } }
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		uk_rbox (canvas, 0, 0, width, height, 6, uk_tone (C_BUTTON, 160), uk_tone (C_BUTTON, 128));
		for (int i = 0; i < n; i++)
		{
			int x = segX (i), w = i == n - 1 ? width - x : segX (i + 1) - x;
			int corners = (i == 0 ? UK_TL | UK_BL : 0) | (i == n - 1 ? UK_TR | UK_BR : 0);
			if (i == cur) uk_rbox (canvas, x, 0, w, height, corners ? 6 : 0, uk_tone (C_ACCENT, 142), uk_tone (C_ACCENT, 114), 255, corners);
			else if (i == m_hot) uk_rbox (canvas, x, 0, w, height, corners ? 6 : 0, uk_tone (C_BUTTON, m_down == i ? 118 : 184), uk_tone (C_BUTTON, m_down == i ? 128 : 146), 255, corners);
			if (i > 0 && i != cur && i - 1 != cur) canvas.fillRect (x, 5, 1, height - 10, uk_tone (C_BUTTON, 100));
			uk_text_c (canvas, x, 0, w, height, seg[i], i == cur ? C_SEL_TEXT : C_BUTTON_TEXT, i == cur ? 2 : 0);
		}
		uk_rline (canvas, 0, 0, width, height, 6, uk_tone (C_BUTTON, 80), 170);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		int h = -1;
		if (in) for (int i = 0; i < n; i++) if (mx >= segX (i)) h = i;
		if (h != m_hot) { m_hot = h; invalidate (true); }
		if (bl && in && m_down < 0) { m_down = h; invalidate (true); }
		else if (!bl && m_down >= 0)
		{
			int d = m_down; m_down = -1; invalidate (true);
			if (d == h && d != cur) { cur = d; invalidate (true); if (onPick) onPick (d); }
		}
		return in;
	}
private:
	int m_hot, m_down;
};

// ---- pills, tiles, cells ------------------------------------------------------------------------------------------------------------
// A rounded label in a colour's tint (a status): its width.
static int pill_w (const char *s) { return uk_text_w (s) + 16; }
static int draw_pill (Canvas &cv, int x, int y, int h, const char *s, unsigned col, bool strong = false)
{
	int w = pill_w (s);
	unsigned face = strong ? col : uk_mix (C_FIELD, col, 52), ink = strong ? uk_ink_on (col) : uk_mix (col, 0, 40);
	uk_rbox (cv, x, y, w, h, h / 2, face, face);
	uk_text_c (cv, x, y, w, h, s, ink);
	return w;
}
// A dashboard's tile: a caption, a value in bold, a line below it (in a colour: overdue...).
static void draw_tile (Canvas &cv, int x, int y, int w, int h, const char *caption, const char *value, const char *sub, unsigned subCol, unsigned accent)
{
	uk_rbox (cv, x, y, w, h, 10, uk_tone (C_FIELD, 132), uk_tone (C_FIELD, 124));
	uk_rline (cv, x, y, w, h, 10, uk_mix (C_BG, 0, 60), 110);
	uk_rbox (cv, x + 1, y + 12, 4, h - 24, 2, accent, accent);
	uk_text_l (cv, x + 18, y + 10, 18, caption, field_dim ());
	text_fit_l (cv, x + 18, y + 32, w - 30, 22, value, C_FIELD_TEXT, 2);
	if (sub && sub[0]) text_fit_l (cv, x + 18, y + 56, w - 30, 18, sub, subCol);
}
// A cell's text in a grid's cell box (DataGrid's cellDraw): left, or right-aligned (amounts), cut to fit.
static void cell_text (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned ink, bool right, int style = 0)
{
	char t[160]; fit_text (s, w - 12, t, sizeof t, style);
	if (right) text_r (cv, x + w - 6, y, h, t, ink, style); else uk_text_l (cv, x + 6, y, h, t, ink, style);
}

// ---- a table whose first column ticks its rows -----------------------------------------------------------------------------------
// A press on a row's first column: onTickRow (tickRow: the row), the row chosen as well.
class TickGrid : public DataGrid
{
public:
	Action onTickRow; int tickRow;
	TickGrid (int l, int t, int w, int h) : DataGrid (l, t, w, h), onTickRow (0), tickRow (-1), m_down (false) {}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		if (!wheel && bl && !m_down && mx >= 1 && mx < 1 + column (0).width - left && my > headH)
		{
			m_down = true;
			int r = rowAt (my);
			bool h = DataGrid::onMouse (mx, my, bl, br, bm, wheel);
			if (r >= 0 && onTickRow) { tickRow = r; onTickRow (*this); }
			return h;
		}
		if (!bl) m_down = false;
		return DataGrid::onMouse (mx, my, bl, br, bm, wheel);
	}
private:
	bool m_down;
};

// ---- questions ------------------------------------------------------------------------------------------------------------------------
// A question or a message: the text wrapped to the box, an icon (0 information, 1 a question, 2 a warning).
class AskBox : public Modal
{
public:
	enum { MAXL = 12 };
	AskBox (const char *title, const char *text, int buttons, int icon) : Modal (460, 120), m_title (title), m_icon (icon), m_n (0), m_def (1), m_cancel (0)
	{
		int maxc = imin (95, (width - 70 - 22) / uk_fw ());
		const char *p = text;
		while (*p && m_n < MAXL)
		{
			int n = 0, cut = -1;
			while (p[n] && p[n] != '\n' && n < maxc) { if (p[n] == ' ') cut = n; n++; }
			if (p[n] && p[n] != '\n' && p[n] != ' ' && cut > 0) n = cut;
			for (int i = 0; i < n; i++) m_line[m_n][i] = p[i];
			m_line[m_n][n] = '\0';
			m_n++;
			p += n;
			if (*p == '\n' || *p == ' ') p++;
		}
		int h = titleH () + 22 + imax (m_n * 20, 36) + 22 + 44;
		resizeTo (width, h);
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = imax (0, (r->height - height) / 2); }
		int by = height - 42;
		if (buttons == MB_YESNOCANCEL) { button (width - 282, by, TR ("Yes"), 1); button (width - 192, by, TR ("No"), 2); button (width - 102, by, TR ("Cancel"), 0); }
		else if (buttons == MB_YESNO) { button (width - 192, by, TR ("Yes"), 1); button (width - 102, by, TR ("No"), 0); }
		else if (buttons == MB_OKCANCEL) { button (width - 192, by, TR ("OK"), 1); button (width - 102, by, TR ("Cancel"), 0); }
		else { button (width - 102, by, TR ("OK"), 1); m_cancel = 1; }
	}
	void button (int x, int y, const char *s, int tag) { Button *b = new Button (x, y, 88, 30, s, act); b->tag = tag; addChild (b); }
	static void act (Widget &w) { ((Modal *) w.parent)->onButton (w.tag); }
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) { close (m_def); return true; }
		if (k == 27) { close (m_cancel); return true; }
		return true;
	}
	void onDraw () override
	{
		drawBox (m_title);
		int cx = 20, cy = titleH () + 22;
		unsigned c = m_icon == 2 ? 0x00D8902A : C_ACCENT;
		uk_rbox (canvas, cx, cy, 32, 32, 16, uk_tone (c, 156), uk_tone (c, 112));
		uk_rline (canvas, cx, cy, 32, 32, 16, uk_tone (c, 80), 160);
		uk_text_c (canvas, cx, cy, 32, 32, m_icon == 2 ? "!" : m_icon == 1 ? "?" : "i", 0x00FFFFFF, 2);
		int y = titleH () + 22 + (m_n == 1 ? 8 : 0);
		for (int i = 0; i < m_n; i++) canvas.text (68, y + i * 20, m_line[i], C_TEXT);
	}
private:
	const char *m_title; int m_icon; char m_line[MAXL][100]; int m_n, m_def, m_cancel;
};
static int ask (const char *title, const char *text, int buttons, int icon)
{
	AskBox a (title, text, buttons, icon);
	return a.run ();
}
static void warn (const char *title, const char *text) { ask (title, text, MB_OK, 2); }

// ---- a dialog's frame -----------------------------------------------------------------------------------------------------------------
// A form in a box: labels at the left, fields after them; OK / Cancel at the foot (Enter, Esc).
class FormBox : public Modal
{
public:
	enum { MAXLAB = 32 };
	const char *m_title; int labX, fieldX, y0;
	struct Lab { int x, y; const char *s; bool dim; } lab[MAXLAB]; int nlab;
	FormBox (const char *title, int w, int h, int labelW = 130) : Modal (w, h), m_title (title), labX (20), fieldX (20 + labelW), y0 (titleH () + 16), nlab (0)
	{
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = imax (0, (r->height - height) / 2); }
	}
	void label (int y, const char *s, int x = -1, bool dim = false)
	{
		if (nlab < MAXLAB) { lab[nlab].x = x < 0 ? labX : x; lab[nlab].y = y; lab[nlab].s = s; lab[nlab].dim = dim; nlab++; }
	}
	LineEdit *edit (int y, const char *labelText, int w, const char *val = "", int x = -1)
	{
		if (labelText) label (y, labelText, x < 0 ? -1 : x - 110);
		LineEdit *e = new LineEdit (x < 0 ? fieldX : x, y, w); e->setText (val); addChild (e);
		return e;
	}
	void buttons (const char *ok = 0)
	{
		Button *b = new Button (width - 196, height - 44, 88, 30, ok ? ok : TR ("OK"), act); b->tag = 1; addChild (b);
		b = new Button (width - 102, height - 44, 88, 30, TR ("Cancel"), act); b->tag = 0; addChild (b);
	}
	static void act (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->onButton (w.tag); }
	void onButton (int tag) override { if (tag == 0 || validate ()) close (tag); }
	virtual bool validate () { return true; }
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { if (validate ()) close (1); return true; }
		if (k == KEY_TAB) { focusNext ((kapi_get_modifiers () & MOD_SHIFT) ? -1 : 1); return true; }
		return false;
	}
	// The next / previous focusable child (Tab).
	void focusNext (int dir)
	{
		Widget *list[64]; int n = 0, cur = -1;
		for (Widget *c = firstChild; c && n < 64; c = c->nextSib) if (c->canFocus && !c->hidden && !c->disabled) { if (c->hasFocus) cur = n; list[n++] = c; }
		if (!n) return;
		int i = cur < 0 ? 0 : (cur + dir + n) % n;
		list[i]->setFocus ();
		invalidate (true);
	}
	virtual void drawMore () {}
	void onDraw () override
	{
		drawBox (m_title);
		for (int i = 0; i < nlab; i++) uk_text_l (canvas, lab[i].x, lab[i].y, ED_H, lab[i].s, lab[i].dim ? dim_ink (C_FACE) : C_TEXT);
		drawMore ();
	}
};

} // namespace lg

#endif
