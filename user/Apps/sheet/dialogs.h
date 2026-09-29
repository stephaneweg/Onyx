//
// dialogs.h -- the spreadsheet's dialogs (wtk Modals over the window): Format Cells (Numbers: a category,
// its decimals, separator, red negatives, symbol, the date and time formats, the code itself and a
// preview; Font; Alignment; Borders: presets, each edge, the line, the colour; Fill), Insert Function
// (by category, each one's arguments and purpose), Sort (three keys), Find and Replace, Paste Special,
// Chart (its type, title, legend, series, a live preview), a size, a name.
//
#ifndef _sheet_dialogs_h
#define _sheet_dialogs_h

#include "bars.h"

namespace ss {

// ---- a dialog's frame (as Writer's) --------------------------------------------------------------------------
class Dialog : public Modal
{
public:
	Dialog (int w, int h, const char *title) : Modal (w, h), m_title (title)
	{
		Root *r = Root::current ();
		int RW = r ? r->width : w, RH = r ? r->height : h;
		left = imax (0, (RW - w) / 2); top = imax (0, (RH - h) / 2);
	}
	void onDraw () override { drawBox (m_title); drawBody (); }
	virtual void drawBody () {}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { onButton (1); return true; } return false; }
	void onButton (int tag) override { close (tag); }
	Button *button (int x, int y, int w, const char *label, int tag) { Button *b = new Button (x, y, w, 28, label, act); b->tag = tag; addChild (b); return b; }
	void okCancel () { button (width - 184, height - 42, 82, "OK", 1); button (width - 94, height - 42, 82, "Cancel", 0); }
	void label (int x, int y, const char *s) { canvas.text (x, y, s, C_TEXT); }
	Textbox *field (int x, int y, int w, const char *s) { Textbox *t = new Textbox (x, y, w, 26, s); addChild (t); return t; }
	static void act (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->onButton (w.tag); }
private:
	const char *m_title;
};

// A message or a question, its text wrapped to the box (wtk's message box keeps three short lines).
class NoteDialog : public Dialog
{
public:
	NoteDialog (const char *title, const char *text, int buttons) : Dialog (440, height_for (text), title)
	{
		m_n = wrap (text, m_line);
		if (buttons == MB_OKCANCEL) okCancel ();
		else if (buttons == MB_YESNO) { button (width - 184, height - 42, 82, "Yes", 1); button (width - 94, height - 42, 82, "No", 0); }
		else button (width - 94, height - 42, 82, "OK", 1);
	}
	void drawBody () override { for (int i = 0; i < m_n; i++) canvas.text (16, titleH () + 16 + i * 18, m_line[i], C_TEXT); }
private:
	enum { MAXL = 12, PER = (440 - 32) / 8 };		// (wtk's font: 8 px a character)
	char m_line[MAXL][64]; int m_n;
	// The lines: words wrapped at the box's width.
	static int wrap (const char *p, char (*line)[64])
	{
		int k = 0;
		while (*p && k < MAXL)
		{
			int n = 0, cut = -1;
			while (p[n] && p[n] != '\n' && n < PER) { if (p[n] == ' ') cut = n; n++; }
			if (p[n] && p[n] != '\n' && cut > 0) n = cut;
			if (line) scpy (line[k], p, imin (n + 1, 64));
			k++;
			p += n;
			while (*p == ' ') p++;
			if (*p == '\n') p++;
		}
		return k;
	}
	static int height_for (const char *text) { return titleH () + 22 + wrap (text, 0) * 18 + 58; }
};
static int note (const char *title, const char *text, int buttons = MB_OK) { NoteDialog d (title, text, buttons); return d.run (); }

// A colour swatch: a click drops a palette.
class Swatch : public Widget
{
public:
	unsigned color; const char *autoLabel; void (*onPick) (Swatch &);
	Swatch (int x, int y, unsigned c, const char *al) : Widget (x, y, 64, 26), color (c), autoLabel (al), onPick (0) {}
	unsigned bgColor () override { return C_FACE; }
	void onDraw () override
	{
		canvas.clear (C_FACE);
		wk_raised (canvas, 0, 0, width, height, 5, C_BUTTON, hover ? WK_HOT : WK_NORMAL);
		if (color == AUTO)
		{
			canvas.frameRect (8, 6, 30, 14, wk_mix (C_BUTTON, C_BUTTON_TEXT, 150));
			if (autoLabel[0] == 'A') canvas.fillRect (10, 8, 26, 10, 0); else for (int k = 0; k < 12; k++) canvas.pixel (10 + k * 2, 18 - k * 10 / 12, 0xC0392B);
		}
		else { canvas.fillRect (8, 6, 30, 14, color); canvas.frameRect (8, 6, 30, 14, wk_mix (color, 0, 80)); }
		wk_glyph (canvas, WKG_CHEV_DOWN, width - 13, height / 2, 7, C_BUTTON_TEXT);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != hover) { hover = in; invalidate (true); }
		if (bl && in && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (in)
			{
				int x = 0, y = 0;
				for (Widget *w = this; w && w->parent; w = w->parent) { x += w->left; y += w->top; }
				ColorPopup p (x, y + height + 2, g_cols, 60, 10, autoLabel);
				long c = p.pick ();
				if (c != -1) { color = (unsigned) c; invalidate (true); if (onPick) onPick (*this); }
			}
		}
		return in;
	}
};

// Tabs across a dialog: a click shows its page (the pages: the dialog's children, hidden but one).
class TabStrip : public Widget
{
public:
	const char *const *names; int n, cur; void (*onPick) (TabStrip &);
	TabStrip (int x, int y, int w, const char *const *nm, int n_) : Widget (x, y, w, 30), names (nm), n (n_), cur (0), onPick (0) {}
	unsigned bgColor () override { return C_FACE; }
	int tabX (int i) { int x = 0; for (int k = 0; k < i; k++) x += wk_text_w (names[k]) + 28; return x; }
	void onDraw () override
	{
		canvas.clear (C_FACE);
		canvas.fillRect (0, height - 1, width, 1, wk_tone (C_FACE, 110));
		for (int i = 0; i < n; i++)
		{
			int x = tabX (i), w = wk_text_w (names[i]) + 24;
			bool on = i == cur;
			if (on) { wk_rbox (canvas, x, 2, w, height - 2, 5, wk_tone (C_FACE, 176), wk_tone (C_FACE, 166), 255, WK_TL | WK_TR); wk_rline (canvas, x, 2, w, height, 5, wk_tone (C_FACE, 110), 255, WK_TL | WK_TR); canvas.fillRect (x + 1, height - 1, w - 2, 1, wk_tone (C_FACE, 166)); }
			wk_text_c (canvas, x, 2, w, height - 4, names[i], on ? C_TEXT : wk_mix (C_TEXT, C_FACE, 90), on ? 2 : 0);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && in && !pressed)
		{
			pressed = true;
			for (int i = 0; i < n; i++) { int x = tabX (i), w = wk_text_w (names[i]) + 24; if (mx >= x && mx < x + w && i != cur) { cur = i; invalidate (true); if (onPick) onPick (*this); } }
		}
		if (!bl) pressed = false;
		return in;
	}
};

// ---- Format Cells ---------------------------------------------------------------------------------------
static const char *const NUM_CATS[] = { "General", "Number", "Currency", "Percent", "Scientific", "Fraction", "Date", "Time", "Text", "Custom" };
static const char *const DATE_FMTS[] = { "dd/mm/yyyy", "d/m/yy", "yyyy-mm-dd", "d mmm yyyy", "d mmmm yyyy", "dddd d mmmm yyyy", "mmm yy", "mmmm yyyy", "dd/mm/yyyy hh:mm", "d-mmm-yy" };
static const char *const TIME_FMTS[] = { "hh:mm", "hh:mm:ss", "h:mm AM/PM", "h:mm:ss AM/PM", "[h]:mm:ss", "mm:ss.0" };
static const char *const CURRENCIES[] = { "\xE2\x82\xAC", "$", "\xC2\xA3", "\xC2\xA5", "CHF" };
static const char *const CURRENCY_NAMES[] = { "Euro", "Dollar ($)", "Pound", "Yen", "Franc (CHF)" };
static const char *const HA_NAMES[] = { "General", "Left", "Centre", "Right", "Justify", "Fill" };
static const char *const VA_NAMES[] = { "Bottom", "Middle", "Top" };
static const char *const UNDER_NAMES[] = { "None", "Single", "Double" };
static const char *const FSTYLE_NAMES[] = { "Regular", "Italic", "Bold", "Bold Italic" };
static const char *const BS_NAMES[] = { "Thin", "Medium", "Thick", "Dashed", "Dotted", "Double", "Hair" };
static const int BS_VALS[] = { BS_THIN, BS_MEDIUM, BS_THICK, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_HAIR };
static const char *const SIZE_NAMES[] = { "6", "7", "8", "9", "10", "10.5", "11", "12", "14", "16", "18", "20", "22", "24", "26", "28", "32", "36", "40", "48", "54", "60", "72" };
static const int SIZE_VALS[] = { 60, 70, 80, 90, 100, 105, 110, 120, 140, 160, 180, 200, 220, 240, 260, 280, 320, 360, 400, 480, 540, 600, 720 };

// The edges of a range the Borders page sets: each one on, off, or left as it is (-1).
struct BorderSet { int edge[6]; int style; unsigned color; };	// left, right, top, bottom, inner horizontal, inner vertical

class FormatDialog : public Dialog
{
public:
	Book *b; Style st, orig; double sample; bool sampleNum; char sampleText[64];
	BorderSet bset; bool mergeOn, mergeOrig;
	char code[160];
	FormatDialog (Book *b_, const Style &s0, double v, bool isNum, const char *txt, bool merged)
		: Dialog (620, 486, "Format Cells"), b (b_), st (s0), orig (s0), sample (v), sampleNum (isNum), mergeOn (merged), mergeOrig (merged)
	{
		scpy (sampleText, txt, sizeof sampleText);
		for (int i = 0; i < 6; i++) bset.edge[i] = -1;
		bset.style = BS_THIN; bset.color = 0;
		scpy (code, book_fmt_code (*b, st.fmt), sizeof code);
		int T = titleH () + 8;
		static const char *const TABS[5] = { "Numbers", "Font", "Alignment", "Borders", "Fill" };
		tabs = new TabStrip (16, T, width - 32, TABS, 5); tabs->onPick = pickTab; addChild (tabs);
		int y = T + 44;
		// Numbers
		cat = new ListBox (16, y, 160, 236, catPicked); for (int i = 0; i < 10; i++) cat->add (NUM_CATS[i]); addChild (cat);
		dec = new NumericUpDown (330, y + 2, 90, 26, 0, 15, 2, 1, optChanged); addChild (dec);
		thou = new Checkbox (196, y + 40, 240, 24, "Thousands separator", true, optChanged, C_FACE); addChild (thou);
		red = new Checkbox (196, y + 70, 240, 24, "Negative numbers in red", false, optChanged, C_FACE); addChild (red);
		sym = new Dropdown (330, y + 102, 150, 26, CURRENCY_NAMES, 5, 0, optChanged); addChild (sym);
		list = new ListBox (196, y + 40, 300, 150, listPicked); addChild (list);
		{ char l1[128]; u8_to_latin1 (code, l1, sizeof l1); codeBox = new Textbox (196, y + 212, 404, 26, l1, codeTyped); addChild (codeBox); }
		// Font
		fam = new ListBox (16, y, 240, 236, fontChanged);
		for (int i = 0; i < fnt::count (); i++) fam->add (fnt::name (i));
		addChild (fam);
		int famNow = book_family (*b, st.font); fam->setSel (famNow);
		fstyle = new Dropdown (380, y, 150, 26, FSTYLE_NAMES, 4, (st.bold ? 2 : 0) + (st.italic ? 1 : 0), fontChanged); addChild (fstyle);
		int si = 4; for (int i = 0; i < 23; i++) if (SIZE_VALS[i] == st.size) si = i;
		fsize = new Dropdown (380, y + 36, 90, 26, SIZE_NAMES, 23, si, fontChanged); addChild (fsize);
		under = new Dropdown (380, y + 72, 150, 26, UNDER_NAMES, 3, st.under, fontChanged); addChild (under);
		strike = new Checkbox (280, y + 108, 200, 24, "Strikethrough", st.strike, fontChanged, C_FACE); addChild (strike);
		fcol = new Swatch (380, y + 138, st.color, "Automatic"); fcol->onPick = swatchPicked; addChild (fcol);
		// Alignment
		ha = new Dropdown (150, y, 160, 26, HA_NAMES, 6, st.ha, alignChanged); addChild (ha);
		va = new Dropdown (150, y + 36, 160, 26, VA_NAMES, 3, st.va, alignChanged); addChild (va);
		ind = new NumericUpDown (150, y + 72, 90, 26, 0, 15, st.indent, 1, alignChanged); addChild (ind);
		wrap = new Checkbox (40, y + 116, 300, 24, "Wrap text in the cell", st.wrap, alignChanged, C_FACE); addChild (wrap);
		merge = new Checkbox (40, y + 146, 300, 24, "Merge the cells", merged, alignChanged, C_FACE); addChild (merge);
		// Borders
		static const char *const PRE[4] = { "None", "Outline", "Inside", "All" };
		for (int i = 0; i < 4; i++) { Button *pb = button (16 + i * 96, y, 88, PRE[i], 10 + i); pres[i] = pb; }
		bstyle = new Dropdown (460, y + 50, 130, 26, BS_NAMES, 7, 0, borderChanged); addChild (bstyle);
		bcol = new Swatch (460, y + 110, 0x000000, "Automatic"); bcol->onPick = swatchPicked; addChild (bcol);
		// Fill
		nofill = button (16, y, 110, "No Fill", 20);
		morefill = button (16, y + 240, 150, "More Colours...", 21);
		okCancel ();
		m_y = y;
		int k = fmt_kind (code);
		int c = k == FK_GENERAL ? 0 : k == FK_NUMBER ? 1 : k == FK_CURRENCY ? 2 : k == FK_PERCENT ? 3 : k == FK_SCI ? 4 : k == FK_FRACTION ? 5 : k == FK_DATE || k == FK_DATETIME ? 6 : k == FK_TIME ? 7 : k == FK_TEXT ? 8 : 9;
		cat->setSel (c);
		int d = fmt_decimals (code); dec->value = d < 0 ? 2 : d;
		thou->checked = strchr (code, ',') != 0;
		red->checked = strstr (code, "[Red]") != 0;
		m_cat = c;
		showTab (0);
	}
	bool onKey (long k) override { if (k == KEY_ENTER && codeBox->hasFocus) { codeTyped (*codeBox); return true; } return Dialog::onKey (k); }
	void onButton (int tag) override
	{
		if (tag >= 10 && tag <= 13)				// the borders' presets
		{
			int p = tag - 10;
			for (int i = 0; i < 6; i++) bset.edge[i] = p == 0 ? 0 : p == 1 ? (i < 4 ? 1 : 0) : p == 2 ? (i < 4 ? 0 : 1) : 1;
			if (p == 1) { bset.edge[4] = bset.edge[5] = -1; }
			if (p == 2) { for (int i = 0; i < 4; i++) bset.edge[i] = -1; }
			invalidate (true);
			return;
		}
		if (tag == 20) { st.fill = AUTO; invalidate (true); return; }
		if (tag == 21) { unsigned c = st.fill == AUTO ? 0xFFFFFF : st.fill; if (wk_color_dialog (&c, "Fill Colour")) { st.fill = c; invalidate (true); } return; }
		if (tag == 1) { applyCode (); }
		close (tag);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		bool r = Dialog::onMouse (mx, my, bl, br, bm, wheel);
		if (bl && !m_down)
		{
			m_down = true;
			if (tabs->cur == 3)					// the edges, clicked in the preview
			{
				int bx = 60, by = m_y + 56, bw = 300, bh = 200;
				if (mx >= bx - 12 && mx <= bx + bw + 12 && my >= by - 12 && my <= by + bh + 12)
				{
					int e = -1;
					if (abs (my - by) < 12) e = 2; else if (abs (my - (by + bh)) < 12) e = 3;
					else if (abs (mx - bx) < 12) e = 0; else if (abs (mx - (bx + bw)) < 12) e = 1;
					else if (abs (my - (by + bh / 2)) < 12) e = 4; else if (abs (mx - (bx + bw / 2)) < 12) e = 5;
					if (e >= 0) { bset.edge[e] = bset.edge[e] == 1 ? 0 : 1; invalidate (true); }
				}
			}
			if (tabs->cur == 4)					// a colour of the palette
			{
				int x0 = 150, y0 = m_y, cs = 24;
				int c = (mx - x0) / (cs + 6), rr = (my - y0) / (cs + 6);
				if (mx >= x0 && my >= y0 && c < 10 && rr < 6 && (mx - x0) % (cs + 6) < cs && (my - y0) % (cs + 6) < cs) { st.fill = g_cols[rr * 10 + c]; invalidate (true); }
			}
		}
		if (!bl) m_down = false;
		return r;
	}
	void drawBody () override
	{
		int y = m_y;
		fnt::Font *pf = ui_font (13);
		switch (tabs->cur)
		{
		case 0:
		{
			bool cur = m_cat == 2, dt = m_cat == 6 || m_cat == 7, num = m_cat >= 1 && m_cat <= 5;
			if (num) canvas.text (196, y + 8, "Decimal places", C_TEXT);
			if (cur) canvas.text (196, y + 108, "Symbol", C_TEXT);
			if (dt) canvas.text (196, y + 8, m_cat == 6 ? "The date as:" : "The time as:", C_TEXT);
			canvas.text (196, y + 194, "Format code", C_TEXT);
			// the preview
			wk_sunken (canvas, 196, y + 250, 404, 34, 5, 0xFFFFFF, false);
			Buf o; unsigned col = AUTO;
			if (sampleNum) fmt_number (code, sample, o, &col, 30); else fmt_text (code, sampleText, o, &col);
			text_at (canvas, pf, 206, y + 272, o.str (), col == AUTO ? 0 : col, 0, Rect { y + 252, 198, y + 282, 598 });
			canvas.text (196, y + 290, "Preview (the cell's value)", wk_mix (C_TEXT, C_FACE, 110));
			break;
		}
		case 1:
		{
			canvas.text (280, y + 6, "Style", C_TEXT); canvas.text (280, y + 42, "Size", C_TEXT); canvas.text (280, y + 78, "Underline", C_TEXT);
			canvas.text (280, y + 144, "Colour", C_TEXT);
			wk_sunken (canvas, 16, y + 250, width - 32, 50, 5, 0xFFFFFF, false);
			Style s2 = st;
			fnt::Font *f = style_font (*b, s2, 100);
			Look L; memset (&L, 0, sizeof L);
			scpy (L.text, "AaBbCcYyZz 123", sizeof L.text); L.n = (int) strlen (L.text); L.f = f; L.st = &s2; L.ink = s2.color == AUTO ? 0 : s2.color;
			L.ha = HA_CENTER; L.va = VA_CENTER;
			draw_text (canvas, L, 16, y + 250, width - 32, 50, 18, width - 18, 100, Rect { y + 252, 18, y + 298, width - 18 });
			break;
		}
		case 2:
			canvas.text (40, y + 6, "Horizontal", C_TEXT); canvas.text (40, y + 42, "Vertical", C_TEXT); canvas.text (40, y + 78, "Indent", C_TEXT);
			break;
		case 3:
		{
			canvas.text (460, y + 30, "Line", C_TEXT); canvas.text (460, y + 90, "Colour", C_TEXT);
			int bx = 60, by = y + 56, bw = 300, bh = 200;
			canvas.fillRect (bx, by, bw, bh, 0xFFFFFF);
			Rect cl = { by - 6, bx - 6, by + bh + 6, bx + bw + 6 };
			// the grid under, the edges set over
			for (int i = 0; i < 3; i++) { hline (canvas, bx, bx + bw, by + i * bh / 2, 0xE0E0E0, BS_DOTTED, cl); vline (canvas, bx + i * bw / 2, by, by + bh, 0xE0E0E0, BS_DOTTED, cl); }
			text_at (canvas, pf, bx + bw / 4, by + bh / 4 + 5, "Text", 0x999999, 1, cl); text_at (canvas, pf, bx + 3 * bw / 4, by + bh / 4 + 5, "Text", 0x999999, 1, cl);
			text_at (canvas, pf, bx + bw / 4, by + 3 * bh / 4 + 5, "Text", 0x999999, 1, cl); text_at (canvas, pf, bx + 3 * bw / 4, by + 3 * bh / 4 + 5, "Text", 0x999999, 1, cl);
			int stl = BS_VALS[bstyle->sel];
			unsigned bc = bcol->color == AUTO ? 0 : bcol->color;
			int th = stl == BS_MEDIUM ? 2 : stl == BS_THICK ? 3 : 1;
			for (int e = 0; e < 6; e++)
			{
				bool on = bset.edge[e] == 1;
				bool same = bset.edge[e] == -1 && e < 4 && orig.bs[e];
				if (!on && !same) continue;
				unsigned c = on ? bc : 0xA0A0A0;
				for (int t = 0; t < (on ? th : 1); t++)
				{
					if (e == 0) vline (canvas, bx + t, by, by + bh, c, on ? stl : BS_THIN, cl);
					if (e == 1) vline (canvas, bx + bw - t, by, by + bh, c, on ? stl : BS_THIN, cl);
					if (e == 2) hline (canvas, bx, bx + bw, by + t, c, on ? stl : BS_THIN, cl);
					if (e == 3) hline (canvas, bx, bx + bw, by + bh - t, c, on ? stl : BS_THIN, cl);
					if (e == 4) hline (canvas, bx, bx + bw, by + bh / 2 + t, c, on ? stl : BS_THIN, cl);
					if (e == 5) vline (canvas, bx + bw / 2 + t, by, by + bh, c, on ? stl : BS_THIN, cl);
				}
			}
			canvas.text (bx, by + bh + 14, "Click an edge to set it or take it off.", wk_mix (C_TEXT, C_FACE, 110));
			break;
		}
		case 4:
		{
			int x0 = 150, cs = 24;
			for (int i = 0; i < 60; i++)
			{
				int x = x0 + (i % 10) * (cs + 6), yy = y + (i / 10) * (cs + 6);
				canvas.fillRect (x, yy, cs, cs, g_cols[i]);
				canvas.frameRect (x, yy, cs, cs, wk_mix (g_cols[i], 0, 60));
				if (st.fill == g_cols[i]) { canvas.frameRect (x - 2, yy - 2, cs + 4, cs + 4, C_ACCENT); canvas.frameRect (x - 3, yy - 3, cs + 6, cs + 6, C_ACCENT); }
			}
			canvas.text (16, y + 290, "Sample:", C_TEXT);
			canvas.fillRect (80, y + 282, 180, 26, st.fill == AUTO ? 0xFFFFFF : st.fill);
			canvas.frameRect (80, y + 282, 180, 26, 0x808080);
			if (st.fill == AUTO) canvas.text (100, y + 290, "(no fill)", 0x808080);
			break;
		}
		}
	}
	// What changed, to be set over the selection.
	void showTab (int t)
	{
		Widget *pages[5][12] = {
			{ cat, dec, thou, red, sym, list, codeBox, 0 },
			{ fam, fstyle, fsize, under, strike, fcol, 0 },
			{ ha, va, ind, wrap, merge, 0 },
			{ pres[0], pres[1], pres[2], pres[3], bstyle, bcol, 0 },
			{ nofill, morefill, 0 } };
		for (int p = 0; p < 5; p++) for (int i = 0; pages[p][i]; i++) pages[p][i]->hidden = p != t;
		if (t == 0) layoutNumbers ();
		invalidate (true);
	}
	void layoutNumbers ()
	{
		bool num = m_cat >= 1 && m_cat <= 5, cur = m_cat == 2, dt = m_cat == 6 || m_cat == 7;
		dec->hidden = !num || m_cat == 5; thou->hidden = !(m_cat == 1 || m_cat == 2); red->hidden = !(m_cat == 1 || m_cat == 2);
		sym->hidden = !cur; list->hidden = !dt;
		if (dt)
		{
			list->clear ();
			const char *const *fm = m_cat == 6 ? DATE_FMTS : TIME_FMTS; int n = m_cat == 6 ? 10 : 6;
			for (int i = 0; i < n; i++)
			{
				Buf o; fmt_number (fm[i], sampleNum && sample > 0 ? sample : 46294.5625, o);
				char t[64]; char l1[48]; u8_to_latin1 (o.str (), l1, sizeof l1);
				snprintf (t, sizeof t, "%s", l1);
				list->add (t);
			}
		}
	}
	void rebuildCode ()
	{
		Buf c;
		int d = dec->value;
		char z[20]; int k = imin (d, 15); for (int i = 0; i < k; i++) z[i] = '0'; z[k] = 0;
		switch (m_cat)
		{
		case 0: c.puts ("General"); break;
		case 1: case 2:
		{
			Buf base; base.puts (thou->checked ? "#,##0" : "0"); if (d) { base.put ('.'); base.puts (z); }
			if (m_cat == 2)
			{
				const char *s = CURRENCIES[sym->sel];
				Buf q; if (!strcmp (s, "$")) { q.put ('$'); q.puts (base.str ()); } else { q.puts (base.str ()); q.puts (" \""); q.puts (s); q.put ('"'); }
				base.clear (); base.puts (q.str ());
			}
			c.puts (base.str ());
			if (red->checked) { c.puts (";[Red]-"); c.puts (base.str ()); }
			break;
		}
		case 3: c.put ('0'); if (d) { c.put ('.'); c.puts (z); } c.put ('%'); break;
		case 4: c.put ('0'); if (d) { c.put ('.'); c.puts (z); } c.puts ("E+00"); break;
		case 5: c.puts (d > 1 ? "# ?\?/?\?" : "# ?/?"); break;
		case 6: c.puts (DATE_FMTS[imax (0, list->sel)]); break;
		case 7: c.puts (TIME_FMTS[imax (0, list->sel)]); break;
		case 8: c.puts ("@"); break;
		default: return;
		}
		scpy (code, c.str (), sizeof code);
		char l1[64]; u8_to_latin1 (code, l1, sizeof l1); codeBox->setText (l1);
		invalidate (true);
	}
	void applyCode () { st.fmt = (unsigned short) book_fmt (*b, code); }
	static FormatDialog *me (Widget &w) { Widget *p = &w; while (p && !p->modal) p = p->parent; return (FormatDialog *) p; }
	static void pickTab (TabStrip &t) { FormatDialog *d = (FormatDialog *) t.parent; d->showTab (t.cur); }
	static void catPicked (Widget &w) { FormatDialog *d = me (w); d->m_cat = d->cat->sel; if (d->m_cat == 6 || d->m_cat == 7) d->list->setSel (0); d->layoutNumbers (); d->rebuildCode (); }
	static void optChanged (Widget &w) { me (w)->rebuildCode (); }
	static void listPicked (Widget &w) { me (w)->rebuildCode (); }
	static void codeTyped (Widget &w)
	{
		FormatDialog *d = me (w);
		char *u = latin1_to_u8 (d->codeBox->text, (int) strlen (d->codeBox->text));
		scpy (d->code, u, sizeof d->code); free (u);
		d->cat->setSel (9); d->m_cat = 9; d->layoutNumbers ();
		d->invalidate (true);
	}
	static void fontChanged (Widget &w)
	{
		FormatDialog *d = me (w);
		if (d->fam->sel >= 0) d->st.font = (unsigned short) book_font (*d->b, fnt::name (d->fam->sel));
		d->st.bold = d->fstyle->sel >= 2; d->st.italic = d->fstyle->sel & 1;
		d->st.size = (unsigned short) SIZE_VALS[iclamp (d->fsize->sel, 0, 22)];
		d->st.under = (unsigned char) d->under->sel; d->st.strike = d->strike->checked;
		d->invalidate (true);
	}
	static void swatchPicked (Swatch &s)
	{
		FormatDialog *d = me (s);
		if (&s == d->fcol) d->st.color = s.color;
		d->invalidate (true);
	}
	static void alignChanged (Widget &w)
	{
		FormatDialog *d = me (w);
		d->st.ha = (unsigned char) d->ha->sel; d->st.va = (unsigned char) d->va->sel; d->st.indent = (unsigned char) d->ind->value; d->st.wrap = d->wrap->checked;
		d->mergeOn = d->merge->checked;
	}
	static void borderChanged (Widget &w) { me (w)->invalidate (true); }
	TabStrip *tabs;
	ListBox *cat, *list, *fam; NumericUpDown *dec, *ind; Checkbox *thou, *red, *strike, *wrap, *merge; Dropdown *sym, *fstyle, *fsize, *under, *ha, *va, *bstyle;
	Textbox *codeBox; Swatch *fcol, *bcol; Button *pres[4], *nofill, *morefill;
	int m_y, m_cat; bool m_down = false;
};

// ---- Insert Function ------------------------------------------------------------------------------------
static const char *const CAT_ALL[CAT_COUNT + 1] = { "All", "Mathematical", "Statistical", "Logical", "Text", "Lookup and Reference", "Date and Time", "Information", "Financial" };
class FuncDialog : public Dialog
{
public:
	int map[400]; int n; int chosen;
	FuncDialog () : Dialog (560, 440, "Insert Function"), n (0), chosen (-1)
	{
		int y = titleH () + 16;
		catDd = new Dropdown (110, y, 230, 26, CAT_ALL, CAT_COUNT + 1, 0, catPicked); addChild (catDd);
		lb = new ListBox (16, y + 40, 220, 300, picked, activated); addChild (lb);
		okCancel ();
		fill ();
	}
	void fill ()
	{
		lb->clear (); n = 0;
		int c = catDd->sel - 1;
		// (by name within the category)
		for (char ch = 'A'; ch <= 'Z'; ch++)
			for (int i = 0; i < NFNS && n < 400; i++)
				if (FNS[i].name[0] == ch && (c < 0 || FNS[i].cat == c)) { map[n++] = i; lb->add (FNS[i].name); }
		lb->setSel (0);
		invalidate (true);
	}
	void drawBody () override
	{
		int y = titleH () + 22;
		canvas.text (16, y, "Category", C_TEXT);
		if (lb->sel < 0 || lb->sel >= n) return;
		const FnDef &d = FNS[map[lb->sel]];
		fnt::Font *fb = ui_font (14, true), *f = ui_font (13);
		int x = 256, yy = titleH () + 64;
		Buf sig; sig.puts (d.name); sig.put ('('); sig.puts (d.args); sig.put (')');
		// (wrapped to the pane)
		int st[16], en[16];
		int k = wrap_lines (fb, sig.str (), sig.n, (width - x - 16) * 64, st, en, 16);
		for (int i = 0; i < k; i++) text_at (canvas, fb, x, yy + 16 + i * 20, Buf_sub (sig, st[i], en[i]), C_TEXT, 0, Rect { 0, x, height, width - 12 });
		yy += k * 20 + 20;
		k = wrap_lines (f, d.help, (int) strlen (d.help), (width - x - 16) * 64, st, en, 16);
		for (int i = 0; i < k; i++) { char t[200]; int l = imin (en[i] - st[i], 199); memcpy (t, d.help + st[i], l); t[l] = 0; text_at (canvas, f, x, yy + 14 + i * 19, t, wk_mix (C_TEXT, C_FACE, 40), 0, Rect { 0, x, height, width - 12 }); }
		yy += k * 19 + 24;
		char cat[64]; snprintf (cat, sizeof cat, "Category: %s", CAT_NAMES[d.cat]);
		text_at (canvas, f, x, yy, cat, wk_mix (C_TEXT, C_FACE, 110), 0, Rect { 0, x, height, width - 12 });
	}
	static const char *Buf_sub (const Buf &b, int a, int z) { static char t[256]; int l = imin (z - a, 255); memcpy (t, b.b + a, l); t[l] = 0; return t; }
	void onButton (int tag) override { if (tag == 1 && lb->sel >= 0 && lb->sel < n) chosen = map[lb->sel]; close (tag); }
	static FuncDialog *me (Widget &w) { Widget *p = &w; while (p && !p->modal) p = p->parent; return (FuncDialog *) p; }
	static void catPicked (Widget &w) { me (w)->fill (); }
	static void picked (Widget &w) { me (w)->invalidate (true); }
	static void activated (Widget &w) { me (w)->onButton (1); }
	Dropdown *catDd; ListBox *lb;
};

// ---- Sort --------------------------------------------------------------------------------------------------
class SortDialog : public Dialog
{
public:
	char names[64][40]; const char *ptrs[65]; int ncols; Rect r;
	Book *b; Sheet *s;
	SortDialog (Book *b_, Sheet *s_, Rect r_, bool header) : Dialog (460, 300, "Sort"), ncols (0), r (r_), b (b_), s (s_)
	{
		int y = titleH () + 20;
		hdr = new Checkbox (16, y, 428, 24, "The first row holds the column labels", header, hdrChanged, C_FACE); addChild (hdr);
		names_fill ();
		static const char *const ORD[2] = { "Ascending", "Descending" };
		for (int k = 0; k < 3; k++)
		{
			key[k] = new Dropdown (110, y + 44 + k * 40, 180, 26, ptrs, ncols + (k ? 1 : 0), k ? ncols : 0, 0); addChild (key[k]);
			ord[k] = new Dropdown (304, y + 44 + k * 40, 140, 26, ORD, 2, 0, 0); addChild (ord[k]);
		}
		okCancel ();
	}
	void names_fill ()
	{
		ncols = imin (64, r.c1 - r.c0 + 1);
		for (int i = 0; i < ncols; i++)
		{
			int c = r.c0 + i;
			char cn[8]; col_name (c, cn);
			Cell *x = s->cells.get (r.r0, c);
			if (hdr && hdr->checked && x && x->kind != K_NONE) { Shown sh; cell_shown (*b, s, x, sh, 30); char l1[32]; u8_to_latin1 (sh.text, l1, sizeof l1); snprintf (names[i], sizeof names[i], "%s", l1); }
			else snprintf (names[i], sizeof names[i], "Column %s", cn);
			ptrs[i] = names[i];
		}
		ptrs[ncols] = "(none)";
	}
	void drawBody () override
	{
		int y = titleH () + 20;
		static const char *const L[3] = { "Sort by", "Then by", "Then by" };
		for (int k = 0; k < 3; k++) canvas.text (16, y + 50 + k * 40, L[k], C_TEXT);
	}
	static void hdrChanged (Widget &w) { SortDialog *d = (SortDialog *) w.parent; d->names_fill (); for (int k = 0; k < 3; k++) d->key[k]->invalidate (true); }
	Checkbox *hdr; Dropdown *key[3], *ord[3];
};

// ---- Find and Replace ---------------------------------------------------------------------------------------
struct FindState { char find[64], repl[64]; bool matchCase, whole, formulas, allSheets; };
static FindState g_find = { "", "", false, false, false, false };
class FindDialog : public Dialog
{
public:
	int action;						// 2 find next, 3 replace, 4 replace all
	FindDialog () : Dialog (460, 240, "Find and Replace"), action (0)
	{
		int y = titleH () + 16;
		fb = field (120, y, 320, g_find.find);
		rb = field (120, y + 36, 320, g_find.repl);
		mc = new Checkbox (16, y + 76, 200, 24, "Match case", g_find.matchCase, 0, C_FACE); addChild (mc);
		wh = new Checkbox (230, y + 76, 220, 24, "Entire cells only", g_find.whole, 0, C_FACE); addChild (wh);
		fo = new Checkbox (16, y + 104, 200, 24, "Search in formulas", g_find.formulas, 0, C_FACE); addChild (fo);
		al = new Checkbox (230, y + 104, 220, 24, "All sheets", g_find.allSheets, 0, C_FACE); addChild (al);
		button (16, height - 42, 104, "Find Next", 2);
		button (126, height - 42, 90, "Replace", 3);
		button (222, height - 42, 112, "Replace All", 4);
		button (width - 94, height - 42, 82, "Close", 0);
		fb->setFocus ();
	}
	void drawBody () override { int y = titleH () + 22; canvas.text (16, y, "Find", C_TEXT); canvas.text (16, y + 36, "Replace with", C_TEXT); }
	void onButton (int tag) override
	{
		scpy (g_find.find, fb->text, sizeof g_find.find); scpy (g_find.repl, rb->text, sizeof g_find.repl);
		g_find.matchCase = mc->checked; g_find.whole = wh->checked; g_find.formulas = fo->checked; g_find.allSheets = al->checked;
		action = tag == 1 ? 2 : tag;
		close (tag == 1 ? 2 : tag);
	}
	Textbox *fb, *rb; Checkbox *mc, *wh, *fo, *al;
};

// ---- Paste Special ------------------------------------------------------------------------------------------
class PasteDialog : public Dialog
{
public:
	PasteDialog () : Dialog (360, 250, "Paste Special")
	{
		int y = titleH () + 16;
		static const char *const W[4] = { "All", "Values only", "Formats only", "Formulas only (no formats)" };
		for (int i = 0; i < 4; i++) { rb[i] = new RadioButton (24, y + i * 30, 300, 24, W[i], 1, i == 0, 0, C_FACE); addChild (rb[i]); }
		tr = new Checkbox (24, y + 128, 300, 24, "Transpose (rows become columns)", false, 0, C_FACE); addChild (tr);
		okCancel ();
	}
	int what () { for (int i = 0; i < 4; i++) if (rb[i]->checked) return i; return 0; }
	RadioButton *rb[4]; Checkbox *tr;
};

// ---- a number, a name ---------------------------------------------------------------------------------------
class AskDialog : public Dialog
{
public:
	char value[64];
	AskDialog (const char *title, const char *prompt, const char *initial) : Dialog (400, 170, title), m_prompt (prompt)
	{
		value[0] = 0;
		int y = titleH () + 44;
		box = field (16, y, 368, initial);
		box->setFocus ();
		okCancel ();
	}
	void drawBody () override { canvas.text (16, titleH () + 20, m_prompt, C_TEXT); }
	void onButton (int tag) override { scpy (value, box->text, sizeof value); close (tag); }
	Textbox *box;
private:
	const char *m_prompt;
};

// ---- Names (Insert > Names): the book's defined names -- added, changed, deleted -------------------------------
class NamesDialog : public Dialog
{
public:
	Book *b; bool changed;
	NamesDialog (Book *b_, const char *selRef) : Dialog (580, 390, "Names"), b (b_), changed (false)
	{
		m_msg[0] = 0;
		int y = titleH () + 14;
		list = new ListBox (16, y, width - 32, 150, picked); addChild (list);
		int y2 = y + 166;
		name = field (130, y2, 220, "");
		char l1[64]; u8_to_latin1 (selRef, l1, sizeof l1);
		ref = field (130, y2 + 36, width - 146, l1);
		// the scopes: the book, each sheet
		m_nsc = 0; m_scope[m_nsc] = 0; scpy (m_scName[m_nsc], "The whole workbook", sizeof m_scName[0]); m_scPtr[m_nsc] = m_scName[m_nsc]; m_nsc++;
		for (int i = 0; i < b->ns && m_nsc < 33; i++) { m_scope[m_nsc] = b->sh[i]->id; char t[64]; u8_to_latin1 (b->sh[i]->name, t, sizeof t); snprintf (m_scName[m_nsc], sizeof m_scName[0], "Sheet %s", t); m_scPtr[m_nsc] = m_scName[m_nsc]; m_nsc++; }
		scope = new Dropdown (130, y2 + 72, 220, 26, m_scPtr, m_nsc, 0, 0); addChild (scope);
		button (16, height - 42, 90, "Add", 10);
		button (112, height - 42, 90, "Delete", 11);
		button (width - 94, height - 42, 82, "Close", 0);
		fill ();
		name->setFocus ();
	}
	void drawBody () override
	{
		int y2 = titleH () + 14 + 166;
		canvas.text (16, y2 + 6, "Name", C_TEXT); canvas.text (16, y2 + 42, "Refers to", C_TEXT); canvas.text (16, y2 + 78, "Scope", C_TEXT);
		if (m_msg[0]) canvas.text (16, y2 + 112, m_msg, 0xB00000);
	}
	void onButton (int tag) override
	{
		if (tag == 0) { close (0); return; }
		m_msg[0] = 0;
		if (tag == 1 || tag == 10)				// Add (or change)
		{
			char *nm = latin1_to_u8 (name->text, (int) strlen (name->text)), *rf = latin1_to_u8 (ref->text, (int) strlen (ref->text));
			const char *why = 0;
			if (!name_set (*b, nm, m_scope[iclamp (scope->sel, 0, m_nsc - 1)], rf, &why)) scpy (m_msg, why ? why : "This formula cannot be read.", sizeof m_msg);
			else { changed = true; fill (); for (int i = 0; i < m_nidx; i++) if (!strcmp (b->names[m_idx[i]].name, nm)) list->setSel (i); }
			free (nm); free (rf);
		}
		else if (tag == 11 && list->sel >= 0 && list->sel < m_nidx)	// Delete
		{
			name_del (*b, m_idx[list->sel]); changed = true;
			fill (); name->setText (""); 
		}
		invalidate (true);
	}
	ListBox *list; Textbox *name, *ref; Dropdown *scope;
private:
	int m_idx[256], m_nidx;
	int m_scope[34], m_nsc; char m_scName[34][72]; const char *m_scPtr[34];
	char m_msg[120];
	void fill ()
	{
		list->clear (); m_nidx = 0;
		for (int i = 0; i < b->nnames && m_nidx < 256; i++)
		{
			const DefName &d = b->names[i];
			Buf f; if (d.f) formula_print (*b, d.f, f);
			char row[200], l1[160];
			snprintf (row, sizeof row, "%-18s %s", d.name, f.str ());
			u8_to_latin1 (row, l1, sizeof l1);
			if (d.scope) { Sheet *hs = book_sheet_by_id (*b, d.scope); if (hs) { int n = (int) strlen (l1); snprintf (l1 + n, sizeof l1 - n, "  (%s)", hs->name); } }
			list->add (l1);
			m_idx[m_nidx++] = i;
		}
	}
	static void picked (Widget &w)
	{
		NamesDialog *d = (NamesDialog *) w.parent;
		if (d->list->sel < 0 || d->list->sel >= d->m_nidx) return;
		const DefName &n = d->b->names[d->m_idx[d->list->sel]];
		char l1[64]; u8_to_latin1 (n.name, l1, sizeof l1); d->name->setText (l1);
		Buf f; if (n.f) formula_print (*d->b, n.f, f);
		u8_to_latin1 (f.str (), l1, sizeof l1); d->ref->setText (l1);
		for (int i = 0; i < d->m_nsc; i++) if (d->m_scope[i] == n.scope) { d->scope->sel = i; d->scope->invalidate (true); }
		d->m_msg[0] = 0; d->invalidate (true);
	}
};

// ---- Conditional formatting (Format > Conditional Formatting): the sheet's rules ---------------------------------
static const char *const CF_TYPE_NAMES[CF_TYPES] = { "Cell value is", "Text", "Top / bottom", "Above / below average", "Duplicate / unique values", "Formula is true", "Colour scale", "Data bar" };
static const char *const CF_CELL_OPS[8] = { "greater than", "greater than or equal to", "less than", "less than or equal to", "equal to", "not equal to", "between", "not between" };
static const char *const CF_TEXT_OPS[4] = { "contains", "does not contain", "begins with", "ends with" };
static const char *const CF_TOP_OPS[4] = { "the top ... values", "the bottom ... values", "the top ... %", "the bottom ... %" };
static const char *const CF_AVG_OPS[2] = { "above the average", "below the average" };
static const char *const CF_DUP_OPS[2] = { "met more than once", "met once only" };
static const char *const CF_NO_OPS[1] = { "-" };
static void cf_describe (const CondFmt &c, char *o, int cap)
{
	char a1[24], a2[24], r[56]; cell_name (c.r.r0, c.r.c0, a1); cell_name (c.r.r1, c.r.c1, a2);
	if (c.r.r0 == c.r.r1 && c.r.c0 == c.r.c1) scpy (r, a1, sizeof r); else snprintf (r, sizeof r, "%s:%s", a1, a2);
	static const char *const SYM[8] = { ">", ">=", "<", "<=", "=", "<>", "between", "not between" };
	char w[160];
	switch (c.type)
	{
	case CF_CELL: if (c.op >= CO_BETWEEN) snprintf (w, sizeof w, "Value %s %.60s and %.60s", SYM[iclamp (c.op, 0, 7)], c.a, c.b); else snprintf (w, sizeof w, "Value %s %.100s", SYM[iclamp (c.op, 0, 7)], c.a); break;
	case CF_TEXT: snprintf (w, sizeof w, "Text %s \"%s\"", CF_TEXT_OPS[iclamp (c.op, 0, 3)], c.a); break;
	case CF_TOP: snprintf (w, sizeof w, "%s %s%s", c.op ? "Bottom" : "Top", c.a, c.pct ? " %" : ""); break;
	case CF_AVERAGE: scpy (w, c.op ? "Below the average" : "Above the average", sizeof w); break;
	case CF_DUP: scpy (w, c.op ? "Unique values" : "Duplicate values", sizeof w); break;
	case CF_FORMULA: snprintf (w, sizeof w, "Formula %s%s", c.a[0] == '=' ? "" : "=", c.a); break;
	case CF_SCALE: snprintf (w, sizeof w, "Colour scale (%d colours)", c.op == 3 ? 3 : 2); break;
	default: scpy (w, "Data bars", sizeof w); break;
	}
	const char *look = "";
	if (c.type != CF_SCALE && c.type != CF_BAR)
	{
		look = "its own look";
		for (int i = 0; i < NCF_LOOKS; i++) if (CF_LOOKS[i].fill == c.fill && CF_LOOKS[i].color == c.color && CF_LOOKS[i].bold == c.bold) look = CF_LOOKS[i].name;
	}
	snprintf (o, cap, "%-10s %s%s%s", r, w, look[0] ? " -- " : "", look);
}
class CondDialog : public Dialog
{
public:
	Book *b; Sheet *s; bool changed;
	CondDialog (Book *b_, Sheet *s_, const char *rangeText) : Dialog (660, 470, "Conditional Formatting"), b (b_), s (s_), changed (false)
	{
		m_msg[0] = 0; m_hasOwn = false;
		int y = titleH () + 14;
		list = new ListBox (16, y, width - 32, 150, picked); addChild (list);
		int y2 = y + 166;
		range = field (130, y2, 200, rangeText);
		type = new Dropdown (130, y2 + 36, 250, 26, CF_TYPE_NAMES, CF_TYPES, 0, typeChanged); addChild (type);
		cond = new Dropdown (130, y2 + 72, 250, 26, CF_CELL_OPS, 8, 0, condChanged); addChild (cond);
		va = field (130, y2 + 108, 200, "");
		vb = field (380, y2 + 108, 200, "");
		for (int i = 0; i < NCF_LOOKS; i++) m_lookNames[i] = CF_LOOKS[i].name;
		m_lookNames[NCF_LOOKS] = "Its own look (as read)";
		look = new Dropdown (130, y2 + 144, 330, 26, m_lookNames, NCF_LOOKS, 0, 0); addChild (look);
		button (16, height - 42, 84, "New", 10);
		button (106, height - 42, 84, "Change", 11);
		button (196, height - 42, 84, "Delete", 12);
		button (300, height - 42, 60, "Up", 13);
		button (366, height - 42, 70, "Down", 14);
		button (width - 94, height - 42, 82, "Close", 0);
		fill ();
		sync ();
	}
	void drawBody () override
	{
		int y2 = titleH () + 14 + 166;
		canvas.text (16, y2 + 6, "Cells", C_TEXT); canvas.text (16, y2 + 42, "Rule", C_TEXT); canvas.text (16, y2 + 78, "Condition", C_TEXT);
		if (!va->hidden) canvas.text (16, y2 + 114, type->sel == CF_FORMULA ? "Formula" : "Value", C_TEXT);
		if (!vb->hidden) canvas.text (346, y2 + 114, "and", C_TEXT);
		canvas.text (16, y2 + 150, type->sel == CF_SCALE ? "Colours" : type->sel == CF_BAR ? "Bars" : "Look", C_TEXT);
		if (m_msg[0]) canvas.text (16, height - 64, m_msg, 0xB00000);
	}
	void onButton (int tag) override
	{
		if (tag == 0) { close (0); return; }
		m_msg[0] = 0;
		int sel = list->sel;
		if (tag == 1 || tag == 10 || tag == 11)			// New / Change
		{
			CondFmt c;
			if (build (c))
			{
				if (tag == 11 && sel >= 0 && sel < s->ncf) s->cf[sel] = c;
				else { s->cf = (CondFmt *) realloc (s->cf, (s->ncf + 1) * sizeof (CondFmt)); s->cf[s->ncf++] = c; sel = s->ncf - 1; }
				done_change (sel);
			}
		}
		else if (tag == 12 && sel >= 0 && sel < s->ncf)		// Delete
		{
			memmove (s->cf + sel, s->cf + sel + 1, (s->ncf - sel - 1) * sizeof (CondFmt)); s->ncf--;
			done_change (imin (sel, s->ncf - 1));
		}
		else if ((tag == 13 || tag == 14) && sel >= 0 && sel < s->ncf)	// Up / Down: which rule comes first
		{
			int to = tag == 13 ? sel - 1 : sel + 1;
			if (to >= 0 && to < s->ncf) { CondFmt t = s->cf[sel]; s->cf[sel] = s->cf[to]; s->cf[to] = t; done_change (to); }
		}
		invalidate (true);
	}
	ListBox *list; Textbox *range, *va, *vb; Dropdown *type, *cond, *look;
private:
	const char *m_lookNames[NCF_LOOKS + 1]; const char *m_opt[8];
	CondFmt m_own; bool m_hasOwn;
	char m_msg[120];
	void done_change (int sel)
	{
		changed = true; cf_changed ();
		fill ();
		if (sel >= 0) list->setSel (sel);
		if (parent) parent->invalidate (true);
	}
	void fill ()
	{
		list->clear ();
		for (int i = 0; i < s->ncf; i++) { char t[200], l1[120]; cf_describe (s->cf[i], t, sizeof t); u8_to_latin1 (t, l1, sizeof l1); list->add (l1); }
	}
	// The fields as the rule's type wants them.
	void sync ()
	{
		int t = type->sel;
		const char *const *ops = CF_NO_OPS; int nops = 1;
		switch (t)
		{
		case CF_CELL: ops = CF_CELL_OPS; nops = 8; break;
		case CF_TEXT: ops = CF_TEXT_OPS; nops = 4; break;
		case CF_TOP: ops = CF_TOP_OPS; nops = 4; break;
		case CF_AVERAGE: ops = CF_AVG_OPS; nops = 2; break;
		case CF_DUP: ops = CF_DUP_OPS; nops = 2; break;
		}
		if (cond->opts != ops) cond->setOptions (ops, nops, 0);
		cond->disabled = nops == 1;
		va->hidden = !(t == CF_CELL || t == CF_TEXT || t == CF_TOP || t == CF_FORMULA);
		vb->hidden = !(t == CF_CELL && cond->sel >= CO_BETWEEN);
		if (t == CF_FORMULA) { va->width = width - 146; } else va->width = 200;
		if (t == CF_SCALE) { for (int i = 0; i < NCF_SCALES; i++) m_opt[i] = CF_SCALES[i].name; look->setOptions (m_opt, NCF_SCALES, 0); }
		else if (t == CF_BAR) { for (int i = 0; i < NCF_BARS; i++) m_opt[i] = CF_BARS[i].name; look->setOptions (m_opt, NCF_BARS, 0); }
		else if (look->opts != m_lookNames) look->setOptions (m_lookNames, NCF_LOOKS + (m_hasOwn ? 1 : 0), 0);
		invalidate (true);
	}
	bool build (CondFmt &c)
	{
		memset (&c, 0, sizeof c); c.fill = c.color = AUTO; c.bold = c.italic = -1;
		Rect r;
		if (parse_sqref (range->text, &r, 1) != 1) { scpy (m_msg, "Type the cells the rule covers: B5:D16.", sizeof m_msg); return false; }
		c.r = r; c.type = type->sel;
		char *ua = latin1_to_u8 (va->text, (int) strlen (va->text)), *ub = latin1_to_u8 (vb->text, (int) strlen (vb->text));
		scpy (c.a, ua, sizeof c.a); scpy (c.b, ub, sizeof c.b); free (ua); free (ub);
		switch (c.type)
		{
		case CF_CELL: c.op = cond->sel; if (c.op < CO_BETWEEN) c.b[0] = 0; break;
		case CF_TEXT: c.op = cond->sel; c.b[0] = 0; break;
		case CF_TOP: c.op = cond->sel & 1; c.pct = cond->sel >= 2; if (atoi (c.a) < 1) scpy (c.a, "10", sizeof c.a); c.b[0] = 0; break;
		case CF_AVERAGE: case CF_DUP: c.op = cond->sel; c.a[0] = c.b[0] = 0; break;
		case CF_FORMULA: c.b[0] = 0; if (c.a[0] != '=') { char t[120]; t[0] = '='; scpy (t + 1, c.a, sizeof t - 1); scpy (c.a, t, sizeof c.a); } break;
		default: c.a[0] = c.b[0] = 0; break;
		}
		if ((c.type == CF_CELL || c.type == CF_TEXT || c.type == CF_FORMULA) && !c.a[0]) { scpy (m_msg, "Type the value the cells are compared with.", sizeof m_msg); return false; }
		if (c.type == CF_CELL && c.op >= CO_BETWEEN && !c.b[0]) { scpy (m_msg, "Type the second value (between ... and ...).", sizeof m_msg); return false; }
		for (int i = 0; i < 2; i++)				// (the formulas read)
		{
			const char *t = i ? c.b : c.a;
			if (t[0] != '=') continue;
			const char *why = 0;
			Formula *f = formula_parse (*b, t + 1, (int) strlen (t + 1), &why);
			if (!f) { snprintf (m_msg, sizeof m_msg, "This formula cannot be read: %s", why ? why : "?"); return false; }
			formula_free (f);
		}
		if (c.type == CF_SCALE) { const CfScale &p = CF_SCALES[iclamp (look->sel, 0, NCF_SCALES - 1)]; c.op = p.n; c.c0 = p.c0; c.c1 = p.c1; c.c2 = p.c2; }
		else if (c.type == CF_BAR) c.c0 = CF_BARS[iclamp (look->sel, 0, NCF_BARS - 1)].c;
		else if (look->sel >= NCF_LOOKS && m_hasOwn) { c.fill = m_own.fill; c.color = m_own.color; c.bold = m_own.bold; c.italic = m_own.italic; }
		else { const CfPreset &p = CF_LOOKS[iclamp (look->sel, 0, NCF_LOOKS - 1)]; c.fill = p.fill; c.color = p.color; c.bold = p.bold; }
		return true;
	}
	static CondDialog *me (Widget &w) { Widget *p = &w; while (p && !p->modal) p = p->parent; return (CondDialog *) p; }
	static void typeChanged (Widget &w) { me (w)->sync (); }
	static void condChanged (Widget &w) { CondDialog *d = me (w); d->vb->hidden = !(d->type->sel == CF_CELL && d->cond->sel >= CO_BETWEEN); d->invalidate (true); }
	static void picked (Widget &w)
	{
		CondDialog *d = me (w);
		int i = d->list->sel;
		if (i < 0 || i >= d->s->ncf) return;
		const CondFmt &c = d->s->cf[i];
		char a1[24], a2[24], t[64]; cell_name (c.r.r0, c.r.c0, a1); cell_name (c.r.r1, c.r.c1, a2);
		if (c.r.r0 == c.r.r1 && c.r.c0 == c.r.c1) scpy (t, a1, sizeof t); else snprintf (t, sizeof t, "%s:%s", a1, a2);
		d->range->setText (t);
		d->m_hasOwn = false;
		int li = -1;
		if (c.type != CF_SCALE && c.type != CF_BAR)
		{
			for (int k = 0; k < NCF_LOOKS; k++) if (CF_LOOKS[k].fill == c.fill && CF_LOOKS[k].color == c.color && CF_LOOKS[k].bold == c.bold) li = k;
			if (li < 0) { d->m_hasOwn = true; d->m_own = c; li = NCF_LOOKS; }
		}
		d->type->sel = c.type; d->look->opts = 0;			// (the options made again)
		d->sync ();
		int op = c.type == CF_TOP ? c.op + (c.pct ? 2 : 0) : c.type == CF_FORMULA || c.type >= CF_SCALE ? 0 : c.op;
		d->cond->sel = iclamp (op, 0, d->cond->nopts - 1);
		char l1[128]; u8_to_latin1 (c.a, l1, sizeof l1); d->va->setText (l1);
		u8_to_latin1 (c.b, l1, sizeof l1); d->vb->setText (l1);
		d->vb->hidden = !(c.type == CF_CELL && c.op >= CO_BETWEEN);
		if (c.type == CF_SCALE) { for (int k = 0; k < NCF_SCALES; k++) if (CF_SCALES[k].c0 == c.c0 && CF_SCALES[k].c2 == c.c2 && CF_SCALES[k].n == (c.op == 3 ? 3 : 2)) li = k; }
		else if (c.type == CF_BAR) { for (int k = 0; k < NCF_BARS; k++) if (CF_BARS[k].c == c.c0) li = k; }
		d->look->sel = iclamp (li, 0, d->look->nopts - 1);
		d->m_msg[0] = 0;
		d->invalidate (true);
	}
};

// ---- Chart ------------------------------------------------------------------------------------------------
static const char *const LEGEND_NAMES[4] = { "None", "Right", "Bottom", "Top" };
// A chart type's button: a little picture of it and its name; the chosen one framed.
class TypeButton : public Widget
{
public:
	int type; int *cur;
	TypeButton (int x, int y, int t, int *cur_) : Widget (x, y, 86, 64), type (t), cur (cur_) {}
	unsigned bgColor () override { return C_FACE; }
	void onDraw () override
	{
		canvas.clear (C_FACE);
		bool on = *cur == type;
		wk_raised (canvas, 0, 0, width, height, 6, on ? wk_mix (C_BUTTON, C_ACCENT, 60) : C_BUTTON, hover ? WK_HOT : WK_NORMAL);
		if (on) { wk_rline (canvas, 0, 0, width, height, 6, C_ACCENT); wk_rline (canvas, 1, 1, width - 2, height - 2, 5, C_ACCENT); }
		VPath p;
		int X = V (22), Y = V (6);
		auto bar = [&] (int x, int y, int w, int h, unsigned c) { p.clear (); p.rect (X + V (x), Y + V (y), V (w), V (h)); p.fill (canvas, c); };
		switch (type)
		{
		case CH_COLUMN: bar (4, 14, 8, 20, SERIES[0]); bar (16, 6, 8, 28, SERIES[1]); bar (28, 18, 8, 16, SERIES[2]); break;
		case CH_BAR: bar (4, 4, 30, 7, SERIES[0]); bar (4, 14, 20, 7, SERIES[1]); bar (4, 24, 36, 7, SERIES[2]); break;
		case CH_LINE: { int a[8] = { X + V (2), Y + V (28), X + V (14), Y + V (14), X + V (26), Y + V (20), X + V (40), Y + V (4) }; p.polyline (a, 4, 40); p.fill (canvas, SERIES[0]);
				int b2[8] = { X + V (2), Y + V (32), X + V (14), Y + V (26), X + V (26), Y + V (30), X + V (40), Y + V (18) }; p.clear (); p.polyline (b2, 4, 40); p.fill (canvas, SERIES[1]); break; }
		case CH_AREA: { int a[12] = { X + V (2), Y + V (34), X + V (2), Y + V (22), X + V (14), Y + V (10), X + V (26), Y + V (16), X + V (40), Y + V (6), X + V (40), Y + V (34) }; p.poly (a, 6); p.fill (canvas, SERIES[0], 200); break; }
		case CH_PIE: { p.circle (X + V (20), Y + V (18), V (16)); p.fill (canvas, SERIES[0]);
				int a[8] = { X + V (20), Y + V (18), X + V (20), Y + V (2), X + V (36), Y + V (18), X + V (20), Y + V (18) }; p.clear (); p.poly (a, 3); p.fill (canvas, SERIES[1]); break; }
		case CH_SCATTER: { static const int P[12] = { 4, 28, 10, 20, 16, 24, 22, 12, 30, 14, 38, 6 }; for (int i = 0; i < 6; i++) { p.clear (); p.circle (X + V (P[2 * i]), Y + V (P[2 * i + 1]), V (3)); p.fill (canvas, SERIES[0]); } break; }
		}
		wk_text_c (canvas, 0, height - 20, width, 18, CHART_NAMES[type], C_BUTTON_TEXT, on ? 2 : 0);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != hover) { hover = in; invalidate (true); }
		if (bl && in && !pressed) pressed = true;
		else if (!bl && pressed) { pressed = false; if (in) { Widget *p = parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->onButton (100 + type); } }
		return in;
	}
};
class ChartDialog : public Dialog
{
public:
	Book *b; Chart c;
	ChartDialog (Book *b_, const Chart &c0, bool isNew) : Dialog (720, 470, isNew ? "Insert Chart" : "Chart"), b (b_), c (c0)
	{
		int y = titleH () + 14;
		for (int i = 0; i < CH_COUNT; i++) { tb[i] = new TypeButton (16 + (i % 3) * 92, y + (i / 3) * 70, i, &c.type); addChild (tb[i]); }
		int y2 = y + 150;
		char l1[100]; u8_to_latin1 (c.title, l1, sizeof l1);
		title = field (96, y2, 190, l1); title->cb = changed;
		legend = new Dropdown (96, y2 + 36, 190, 26, LEGEND_NAMES, 4, c.legend, changedA); addChild (legend);
		rows = new RadioButton (96, y2 + 76, 100, 24, "Columns", 2, !c.byRows, changedA, C_FACE); addChild (rows);
		rows2 = new RadioButton (200, y2 + 76, 90, 24, "Rows", 2, c.byRows, changedA, C_FACE); addChild (rows2);
		head = new Checkbox (16, y2 + 104, 280, 24, "The first row names", c.head, changedA, C_FACE); addChild (head);
		side = new Checkbox (16, y2 + 132, 280, 24, "The first column names", c.side, changedA, C_FACE); addChild (side);
		stack = new Checkbox (16, y2 + 160, 140, 24, "Stacked", c.stacked, changedA, C_FACE); addChild (stack);
		grid = new Checkbox (156, y2 + 160, 140, 24, "Gridlines", c.grid, changedA, C_FACE); addChild (grid);
		okCancel ();
	}
	void drawBody () override
	{
		int y = titleH () + 14;
		int y2 = y + 150;
		canvas.text (16, y2 + 6, "Title", C_TEXT); canvas.text (16, y2 + 42, "Legend", C_TEXT); canvas.text (16, y2 + 80, "Series", C_TEXT);
		// the preview
		int px = 316, py = titleH () + 14, pw = width - px - 16, ph = height - py - 60;
		Canvas sub; sub.adopt (canvas.px + py * canvas.stride + px, pw, ph, canvas.stride);
		draw_chart (sub, *b, &c, 0, 0, pw, ph, 100, Rect { 0, 0, ph - 1, pw - 1 });
	}
	void onDraw () override
	{
		Dialog::onDraw ();
	}
	void sync ()
	{
		char *u = latin1_to_u8 (title->text, (int) strlen (title->text)); scpy (c.title, u, sizeof c.title); free (u);
		c.legend = legend->sel; c.byRows = rows2->checked; c.head = head->checked; c.side = side->checked; c.stacked = stack->checked; c.grid = grid->checked;
		invalidate (true);
	}
	void onButton (int tag) override { if (tag >= 100) { c.type = tag - 100; for (int i = 0; i < CH_COUNT; i++) tb[i]->invalidate (true); sync (); return; } sync (); close (tag); }
	static ChartDialog *me (Widget &w) { Widget *p = &w; while (p && !p->modal) p = p->parent; return (ChartDialog *) p; }
	static void changed (Widget &w) { me (w)->sync (); }
	static void changedA (Widget &w) { me (w)->sync (); }
	TypeButton *tb[CH_COUNT]; Textbox *title; Dropdown *legend; RadioButton *rows, *rows2; Checkbox *head, *side, *stack, *grid;
};

} // namespace ss

#endif
