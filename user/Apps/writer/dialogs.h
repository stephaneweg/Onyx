//
// dialogs.h -- Writer's dialogs (wtk Modals over the window): Font (the family, the style, the size,
// the effects, the colours, a preview drawn with the chosen font), Paragraph (the alignment, the
// indents -- a first line's or a hanging one --, the spacing, the line spacing, a page break before,
// kept with the next; a preview), Page Setup (the paper, the orientation, the margins, the page
// numbers), Find and Replace, Special Character (the fonts' characters by block), Date and Time,
// Word Count. Lengths in centimetres (or inches: the ruler's unit), spacing in points.
//
#ifndef _writer_dialogs_h
#define _writer_dialogs_h

#include "ui.h"

namespace wr {

using namespace wtk;

// ---- lengths --------------------------------------------------------------------------------------------
static void fmt_len (char *b, int tw)		// twips -> "2.54" (cm or in)
{
	int unit = g_inches ? 1440 : 567;
	int v = (int) (((long long) (tw < 0 ? -tw : tw) * 100 + unit / 2) / unit);
	int n = 0;
	if (tw < 0) b[n++] = '-';
	char t[12]; int j = 0; int ip = v / 100;
	do { t[j++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	while (j) b[n++] = t[--j];
	b[n++] = '.'; b[n++] = (char) ('0' + v / 10 % 10); b[n++] = (char) ('0' + v % 10);
	b[n] = 0;
}
static bool parse_num (const char *s, long *hundredths)
{
	long v = 0; int frac = -1; bool neg = false, any = false;
	for (; *s; s++)
	{
		if (*s == '-') neg = true;
		else if (*s >= '0' && *s <= '9') { any = true; if (frac < 0) v = v * 10 + (*s - '0'); else if (frac < 2) { v = v * 10 + (*s - '0'); frac++; } }
		else if ((*s == '.' || *s == ',') && frac < 0) frac = 0;
		else if (*s != ' ') break;
	}
	if (!any) return false;
	if (frac < 0) frac = 0;
	while (frac < 2) { v *= 10; frac++; }
	*hundredths = neg ? -v : v;
	return true;
}
static bool parse_len (const char *s, int *tw)
{
	long h; if (!parse_num (s, &h)) return false;
	int unit = g_inches ? 1440 : 567;
	*tw = (int) (h * unit / 100);
	return true;
}
static void fmt_int (char *b, int v) { int n = 0; if (v < 0) { b[n++] = '-'; v = -v; } char t[12]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) b[n++] = t[--j]; b[n] = 0; }

// ---- a dialog's frame -----------------------------------------------------------------------------------
class Dialog : public Modal
{
public:
	Dialog (int w, int h, const char *title) : Modal (w, h), m_title (title)
	{
		Root *r = Root::current ();
		int RW = r ? r->width : w, RH = r ? r->height : h;
		left = wmax (0, (RW - w) / 2); top = wmax (0, (RH - h) / 2);
	}
	void onDraw () override { drawBox (m_title); drawBody (); }
	virtual void drawBody () {}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { onButton (1); return true; } return false; }
	void onButton (int tag) override { close (tag); }
	// A button whose click reaches onButton (tag) (through any container).
	Button *button (int x, int y, int w, const char *label, int tag)
	{
		Button *b = new Button (x, y, w, 28, label, act); b->tag = tag; addChild (b); return b;
	}
	void okCancel () { button (width - 184, height - 42, 82, "OK", 1); button (width - 94, height - 42, 82, "Cancel", 0); }
	void label (int x, int y, const char *s) { canvas.text (x, y, s, C_TEXT); }
	Textbox *field (int x, int y, int w, const char *s) { Textbox *t = new Textbox (x, y, w, 26, s); addChild (t); return t; }
	static void act (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->onButton (w.tag); }
private:
	const char *m_title;
};

// A colour swatch: a click drops a palette.
class Swatch : public Widget
{
public:
	unsigned color; const unsigned *cols; int n, per; const char *autoLabel; void (*onPick) (Swatch &);
	Swatch (int x, int y, unsigned c, const unsigned *cs, int n_, int per_, const char *al)
		: Widget (x, y, 64, 26), color (c), cols (cs), n (n_), per (per_), autoLabel (al), onPick (0) {}
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
				ColorPopup p (x, y + height + 2, cols, n, per, autoLabel);
				long c = p.pick ();
				if (c != -1) { color = (unsigned) c; invalidate (true); if (onPick) onPick (*this); }
			}
		}
		return in;
	}
};

// A box drawn by its dialog (a preview).
class Canvasbox : public Widget
{
public:
	void (*paint) (Canvas &cv, int w, int h, void *ctx); void *ctx;
	Canvasbox (int x, int y, int w, int h, void (*p) (Canvas &, int, int, void *), void *c) : Widget (x, y, w, h), paint (p), ctx (c) {}
	void onDraw () override { paint (canvas, width, height, ctx); }
};

// ---- Font ---------------------------------------------------------------------------------------------------
static const char *const STYLE_LIST[4] = { "Regular", "Italic", "Bold", "Bold Italic" };

class FontDialog : public Dialog
{
public:
	CharFmt f;					// what it shows (and gives back)
	ListBox *fam, *sty, *siz; Textbox *sizeTb;
	Checkbox *cbU, *cbS, *cbSup, *cbSub;
	Swatch *col, *hil;
	Canvasbox *prev;
	FontDialog (const CharFmt &cur) : Dialog (580, 460, "Font"), f (cur)
	{
		fam = new ListBox (16, 64, 230, 180, onFam); addChild (fam);
		for (int i = 0; i < fnt::count (); i++) fam->add (fnt::name (i));
		fam->setSel (font_family (f.font));
		sty = new ListBox (256, 64, 130, 180, onSty); addChild (sty);
		for (int i = 0; i < 4; i++) sty->add (STYLE_LIST[i]);
		sty->setSel ((f.flags & CF_BOLD ? 2 : 0) + (f.flags & CF_ITALIC ? 1 : 0));
		char b[16]; fmt_int (b, f.size / 2); if (f.size & 1) { int k = slen (b); b[k] = '.'; b[k + 1] = '5'; b[k + 2] = 0; }
		sizeTb = field (396, 64, 168, b); sizeTb->cb = onSizeTb;
		siz = new ListBox (396, 94, 168, 150, onSiz); addChild (siz);
		for (unsigned i = 0; i < sizeof SIZES / sizeof SIZES[0]; i++) { fmt_int (b, SIZES[i] / 2); siz->add (b); if (SIZES[i] == f.size) siz->setSel ((int) i); }
		cbU = new Checkbox (28, 282, 150, 24, "Underline", f.flags & CF_UNDER, onFx, C_FACE); addChild (cbU);
		cbS = new Checkbox (28, 310, 150, 24, "Strikethrough", f.flags & CF_STRIKE, onFx, C_FACE); addChild (cbS);
		cbSup = new Checkbox (190, 282, 130, 24, "Superscript", f.flags & CF_SUPER, onFx, C_FACE); addChild (cbSup);
		cbSub = new Checkbox (190, 310, 130, 24, "Subscript", f.flags & CF_SUB, onFx, C_FACE); addChild (cbSub);
		col = new Swatch (426, 280, f.color, g_textCols, 60, 10, "Automatic"); addChild (col); col->onPick = onCol;
		hil = new Swatch (426, 310, f.hilite, g_hiliteCols, 15, 5, "No Colour"); addChild (hil); hil->onPick = onCol;
		prev = new Canvasbox (16, 352, 548, 58, paintPreview, this); addChild (prev);
		okCancel ();
	}
	void drawBody () override
	{
		label (16, 42, "Font:"); label (256, 42, "Style:"); label (396, 42, "Size:");
		wk_etch_box (canvas, 16, 262, 548, 80, 6, C_FACE);
		canvas.fillRect (26, 255, wk_text_w ("Effects") + 8, 14, C_FACE);
		label (30, 255, "Effects");
		label (344, 286, "Colour:"); label (344, 316, "Highlight:");
	}
	static FontDialog *me (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; return (FontDialog *) p; }
	static void onFam (Widget &w) { FontDialog *d = me (w); d->f.font = (short) doc_font (g_doc, fnt::name (d->fam->sel)); d->prev->invalidate (true); }
	static void onSty (Widget &w) { FontDialog *d = me (w); int s = d->sty->sel; d->f.flags = (unsigned short) ((d->f.flags & ~(CF_BOLD | CF_ITALIC)) | (s & 2 ? CF_BOLD : 0) | (s & 1 ? CF_ITALIC : 0)); d->prev->invalidate (true); }
	static void onSiz (Widget &w) { FontDialog *d = me (w); d->f.size = SIZES[d->siz->sel]; char b[8]; fmt_int (b, d->f.size / 2); d->sizeTb->setText (b); d->prev->invalidate (true); }
	static void onSizeTb (Widget &w) { FontDialog *d = me (w); long h; if (parse_num (d->sizeTb->text, &h) && h >= 100 && h <= 163800) d->f.size = (short) ((h * 2 + 50) / 100); d->prev->invalidate (true); }
	static void onFx (Widget &w)
	{
		FontDialog *d = me (w);
		if (&w == d->cbSup && d->cbSup->checked && d->cbSub->checked) { d->cbSub->checked = false; d->cbSub->invalidate (true); }
		if (&w == d->cbSub && d->cbSub->checked && d->cbSup->checked) { d->cbSup->checked = false; d->cbSup->invalidate (true); }
		unsigned short fl = (unsigned short) (d->f.flags & (CF_BOLD | CF_ITALIC));
		if (d->cbU->checked) fl |= CF_UNDER;
		if (d->cbS->checked) fl |= CF_STRIKE;
		if (d->cbSup->checked) fl |= CF_SUPER;
		if (d->cbSub->checked) fl |= CF_SUB;
		d->f.flags = fl;
		d->prev->invalidate (true);
	}
	static void onCol (Swatch &s) { FontDialog *d = me (s); d->f.color = d->col->color; d->f.hilite = d->hil->color; d->prev->invalidate (true); }
	static void paintPreview (Canvas &cv, int w, int h, void *ctx)
	{
		FontDialog *d = (FontDialog *) ctx;
		cv.clear (C_FACE);
		wk_sunken (cv, 0, 0, w, h, 5, 0xFFFFFF);
		const CharFmt &f = d->f;
		int size64 = wclamp ((int) f.size * 64 * 4 / 6, 8 * 64, 40 * 64);	// (points at 96 dpi, capped)
		if (f.flags & (CF_SUPER | CF_SUB)) size64 = size64 * 58 / 100;
		int st = (f.flags & CF_BOLD ? fnt::BOLD : 0) | (f.flags & CF_ITALIC ? fnt::ITALIC : 0);
		fnt::Font *font = fnt::get (font_family (f.font), st, size64);
		if (!font) return;
		const char *s = "The quick brown fox jumps";
		unsigned buf[64]; int n = 0;
		if (has_sel ())				// (the selection's own words)
		{
			Pos a = sel_a (), b = sel_b ();
			const Para *q = g_doc.p[a.p];
			int e = a.p == b.p ? b.o : q->len;
			for (int k = a.o; k < e && n < 40; k++) if (q->ch[k] >= 32) buf[n++] = q->ch[k];
		}
		if (n == 0) for (const char *p = s; *p; p++) buf[n++] = (unsigned char) *p;
		int tw = 0; for (int i = 0; i < n; i++) tw += fnt::advance (font, buf[i]);
		int x64 = wmax (8 * 64, (w * 64 - tw) / 2);
		int base = h / 2 + ((font->ascent - font->descent) >> 7) + (f.flags & CF_SUPER ? -h / 6 : f.flags & CF_SUB ? h / 8 : 0);
		unsigned c = f.color == AUTO ? 0 : f.color;
		if (f.hilite != AUTO) cv.fillRect (x64 >> 6, base - (font->ascent >> 6) - 2, wmin (tw >> 6, w - 16), (font->ascent + font->descent) / 64 + 4, f.hilite);
		int x = x64;
		for (int i = 0; i < n && (x >> 6) < w - 8; i++) { fnt::draw (cv, font, x, base, buf[i], c, 2, 2, w - 2, h - 2); x += fnt::advance (font, buf[i]); }
		int th = wmax (1, font->ulThick >> 6), end = wmin (x >> 6, w - 8);
		if (f.flags & CF_UNDER) cv.fillRect (x64 >> 6, base + (font->ulPos >> 6), end - (x64 >> 6), th, c);
		if (f.flags & CF_STRIKE) cv.fillRect (x64 >> 6, base - (font->xHeight >> 7), end - (x64 >> 6), th, c);
	}
};

static bool dlg_font ()
{
	CharFmt cur = g_doc.fmt[caret_cf ()];
	FontDialog d (cur);
	if (d.run () != 1) return false;
	FontDialog::onSizeTb (*d.sizeTb);
	CfChange c; c.what = CH_FONT | CH_SIZE | CH_FLAGS | CH_COLOR | CH_HILITE;
	c.font = d.f.font; c.size = d.f.size; c.color = d.f.color; c.hilite = d.f.hilite;
	c.setFlags = d.f.flags; c.clearFlags = CF_BOLD | CF_ITALIC | CF_UNDER | CF_STRIKE | CF_SUPER | CF_SUB;
	ed_format (c);
	return true;
}

// ---- Paragraph -------------------------------------------------------------------------------------------
static const char *const ALIGN_NAMES[4] = { "Left", "Centred", "Right", "Justified" };
static const char *const SPECIAL_NAMES[3] = { "(none)", "First line", "Hanging" };
static const char *const LINE_NAMES[4] = { "Single", "1.15 lines", "1.5 lines", "Double" };
static const short LINE_VALS[4] = { 100, 115, 150, 200 };

class ParaDialog : public Dialog
{
public:
	ParaFmt pf;
	Dropdown *al, *special, *line;
	Textbox *tLeft, *tRight, *tBy, *tBefore, *tAfter;
	Checkbox *cbBreak, *cbKeep;
	Canvasbox *prev;
	ParaDialog (const ParaFmt &cur) : Dialog (500, 470, "Paragraph"), pf (cur)
	{
		char b[16];
		al = new Dropdown (140, 42, 150, 26, ALIGN_NAMES, 4, pf.align & 3, onAny); addChild (al);
		fmt_len (b, pf.left); tLeft = field (140, 102, 90, b);
		fmt_len (b, pf.right); tRight = field (140, 134, 90, b);
		int sp = pf.first > 0 ? 1 : pf.first < 0 ? 2 : 0;
		special = new Dropdown (330, 102, 150, 26, SPECIAL_NAMES, 3, sp, onAny); addChild (special);
		fmt_len (b, pf.first < 0 ? -pf.first : pf.first); tBy = field (390, 134, 90, b);
		fmt_int (b, pf.before / 20); tBefore = field (140, 212, 90, b);
		fmt_int (b, pf.after / 20); tAfter = field (140, 244, 90, b);
		int li = 0; for (int i = 0; i < 4; i++) if (LINE_VALS[i] == pf.line) li = i;
		line = new Dropdown (330, 212, 150, 26, LINE_NAMES, 4, li, onAny); addChild (line);
		cbBreak = new Checkbox (24, 296, 200, 24, "Page break before", pf.pageBreak, onAny, C_FACE); addChild (cbBreak);
		cbKeep = new Checkbox (250, 296, 200, 24, "Keep with next", pf.keepNext, onAny, C_FACE); addChild (cbKeep);
		prev = new Canvasbox (16, 332, 468, 80, paintPreview, this); addChild (prev);
		tLeft->cb = tRight->cb = tBy->cb = tBefore->cb = tAfter->cb = onAny;
		okCancel ();
	}
	void drawBody () override
	{
		const char *u = g_inches ? "in" : "cm";
		label (24, 48, "Alignment:");
		groupTitle (16, 82, 468, 90, "Indentation");
		label (32, 108, "Left:"); label (236, 108, u); label (32, 140, "Right:"); label (236, 140, u);
		label (266, 108, "Special:"); label (330, 140, "By:"); label (486 - 0, 140, "");
		groupTitle (16, 192, 468, 90, "Spacing");
		label (32, 218, "Before:"); label (236, 218, "pt"); label (32, 250, "After:"); label (236, 250, "pt");
		label (266, 218, "Line:");
	}
	void groupTitle (int x, int y, int w, int h, const char *t)
	{
		wk_etch_box (canvas, x, y, w, h, 6, C_FACE);
		canvas.fillRect (x + 10, y - 7, wk_text_w (t) + 8, 14, C_FACE);
		label (x + 14, y - 7, t);
	}
	void read ()
	{
		int v;
		pf.align = (unsigned char) al->sel;
		if (parse_len (tLeft->text, &v)) pf.left = (short) wclamp (v, 0, 20000);
		if (parse_len (tRight->text, &v)) pf.right = (short) wclamp (v, 0, 20000);
		int by = 0; parse_len (tBy->text, &by); by = wclamp (by, 0, 20000);
		pf.first = (short) (special->sel == 1 ? by : special->sel == 2 ? -by : 0);
		if (pf.first < -pf.left) pf.first = (short) -pf.left;
		long h;
		if (parse_num (tBefore->text, &h)) pf.before = (short) wclamp ((int) (h * 20 / 100), 0, 20000);
		if (parse_num (tAfter->text, &h)) pf.after = (short) wclamp ((int) (h * 20 / 100), 0, 20000);
		pf.line = LINE_VALS[line->sel & 3];
		pf.pageBreak = cbBreak->checked; pf.keepNext = cbKeep->checked;
	}
	static ParaDialog *me (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; return (ParaDialog *) p; }
	static void onAny (Widget &w) { ParaDialog *d = me (w); d->read (); d->prev->invalidate (true); }
	static void paintPreview (Canvas &cv, int w, int h, void *ctx)
	{
		ParaDialog *d = (ParaDialog *) ctx;
		cv.clear (C_FACE);
		wk_sunken (cv, 0, 0, w, h, 5, 0xFFFFFF);
		const ParaFmt &pf = d->pf;
		int ml = 16, tw = w - 32;			// (a text width standing for the page's)
		int pageTw = wmax (1440, g_doc.page.w - g_doc.page.left - g_doc.page.right);
		auto px = [&] (int t) { return (int) ((long long) t * tw / pageTw); };
		unsigned grey = 0xD0D0D0, ink = 0x606060;
		for (int k = 0; k < 2; k++) cv.fillRect (ml, 8 + k * 6, tw - (k ? 60 : 0), 3, grey);
		int y = 24 + (pf.before > 0 ? 3 : 0);
		int widths[4] = { 100, 96, 98, 60 };
		int step = pf.line >= 150 ? 9 : 7;
		for (int k = 0; k < 4 && y < h - 12; k++, y += step)
		{
			int x0 = ml + px (pf.left + (k == 0 ? pf.first : 0)), x1 = ml + tw - px (pf.right);
			int lw = (x1 - x0) * widths[k] / 100;
			if (pf.align == AL_JUSTIFY && k < 3) lw = x1 - x0;
			int x = pf.align == AL_CENTER ? x0 + (x1 - x0 - lw) / 2 : pf.align == AL_RIGHT ? x1 - lw : x0;
			cv.fillRect (x, y, wmax (4, lw), 3, ink);
		}
		y += pf.after > 0 ? 4 : 1;
		for (int k = 0; k < 2 && y < h - 6; k++, y += 6) cv.fillRect (ml, y, tw - (k ? 90 : 0), 3, grey);
	}
};

static bool dlg_paragraph ()
{
	ParaDialog d (g_doc.p[sel_a ().p]->pf);
	if (d.run () != 1) return false;
	d.read ();
	g_pfSet = d.pf;
	ed_para (pf_set, PF_ALIGN | PF_LEFT | PF_RIGHT | PF_FIRST | PF_BEFORE | PF_AFTER | PF_LINE | PF_BREAK | PF_KEEP);
	return true;
}

// ---- Page Setup ------------------------------------------------------------------------------------------
struct Paper { const char *name; int w, h; };
static const Paper PAPERS[] = { { "A4 (21 x 29.7 cm)", 11906, 16838 }, { "A5 (14.8 x 21 cm)", 8391, 11906 },
				{ "A3 (29.7 x 42 cm)", 16838, 23811 }, { "Letter (8.5 x 11 in)", 12240, 15840 },
				{ "Legal (8.5 x 14 in)", 12240, 20160 } };
static const char *PAPER_NAMES[5] = { PAPERS[0].name, PAPERS[1].name, PAPERS[2].name, PAPERS[3].name, PAPERS[4].name };

class PageDialog : public Dialog
{
public:
	PageSetup ps;
	Dropdown *paper; RadioButton *portrait, *landscape;
	Textbox *tTop, *tBottom, *tLeft, *tRight;
	Checkbox *cbNum;
	Canvasbox *prev;
	PageDialog (const PageSetup &cur) : Dialog (500, 400, "Page Setup"), ps (cur)
	{
		int pw = wmin (ps.w, ps.h), ph = wmax (ps.w, ps.h), sel = 0;
		for (int i = 0; i < 5; i++) if (PAPERS[i].w == pw && PAPERS[i].h == ph) sel = i;
		paper = new Dropdown (130, 42, 210, 26, PAPER_NAMES, 5, sel, onAny); addChild (paper);
		portrait = new RadioButton (130, 78, 110, 24, "Portrait", 1, ps.w <= ps.h, onAny, C_FACE); addChild (portrait);
		landscape = new RadioButton (240, 78, 120, 24, "Landscape", 1, ps.w > ps.h, onAny, C_FACE); addChild (landscape);
		char b[16];
		fmt_len (b, ps.top); tTop = field (110, 142, 80, b);
		fmt_len (b, ps.bottom); tBottom = field (110, 174, 80, b);
		fmt_len (b, ps.left); tLeft = field (110, 206, 80, b);
		fmt_len (b, ps.right); tRight = field (110, 238, 80, b);
		tTop->cb = tBottom->cb = tLeft->cb = tRight->cb = onAny;
		cbNum = new Checkbox (24, 290, 300, 24, "Page numbers at the bottom", ps.numbers, onAny, C_FACE); addChild (cbNum);
		prev = new Canvasbox (340, 110, 144, 170, paintPreview, this); addChild (prev);
		okCancel ();
	}
	void drawBody () override
	{
		const char *u = g_inches ? "in" : "cm";
		label (24, 48, "Paper:"); label (24, 82, "Orientation:");
		wk_etch_box (canvas, 16, 122, 300, 152, 6, C_FACE);
		canvas.fillRect (26, 115, wk_text_w ("Margins") + 8, 14, C_FACE); label (30, 115, "Margins");
		label (32, 148, "Top:"); label (32, 180, "Bottom:"); label (32, 212, "Left:"); label (32, 244, "Right:");
		for (int k = 0; k < 4; k++) label (198, 148 + k * 32, u);
	}
	void read ()
	{
		const Paper &p = PAPERS[paper->sel];
		bool land = landscape->checked;
		ps.w = land ? p.h : p.w; ps.h = land ? p.w : p.h;
		int v;
		if (parse_len (tTop->text, &v)) ps.top = wclamp (v, 0, ps.h / 3);
		if (parse_len (tBottom->text, &v)) ps.bottom = wclamp (v, 0, ps.h / 3);
		if (parse_len (tLeft->text, &v)) ps.left = wclamp (v, 0, ps.w / 3);
		if (parse_len (tRight->text, &v)) ps.right = wclamp (v, 0, ps.w / 3);
		ps.numbers = cbNum->checked;
	}
	static PageDialog *me (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; return (PageDialog *) p; }
	static void onAny (Widget &w) { PageDialog *d = me (w); d->read (); d->prev->invalidate (true); }
	static void paintPreview (Canvas &cv, int w, int h, void *ctx)
	{
		PageDialog *d = (PageDialog *) ctx;
		cv.clear (C_FACE);
		const PageSetup &ps = d->ps;
		int maxW = w - 12, maxH = h - 12;
		int pw = maxW, ph = (int) ((long long) pw * ps.h / ps.w);
		if (ph > maxH) { ph = maxH; pw = (int) ((long long) ph * ps.w / ps.h); }
		int x = (w - pw) / 2, y = (h - ph) / 2;
		cv.fillRect (x + 3, y + 3, pw, ph, wk_mix (C_FACE, 0, 70));
		cv.fillRect (x, y, pw, ph, 0xFFFFFF);
		cv.frameRect (x, y, pw, ph, 0x808080);
		int l = (int) ((long long) ps.left * pw / ps.w), r = (int) ((long long) ps.right * pw / ps.w);
		int t = (int) ((long long) ps.top * ph / ps.h), b = (int) ((long long) ps.bottom * ph / ps.h);
		for (int yy = y + t; yy < y + ph - b - 2; yy += 5) cv.fillRect (x + l, yy, pw - l - r, 2, 0xB8B8B8);
		if (ps.numbers) cv.fillRect (x + pw / 2 - 2, y + ph - b / 2 - 1, 4, 3, 0x606060);
	}
};

static bool dlg_page_setup ()
{
	PageDialog d (g_doc.page);
	if (d.run () != 1) return false;
	d.read ();
	g_doc.page = d.ps;
	g_doc.changes++;
	return true;
}

// ---- Word Count ------------------------------------------------------------------------------------------
class CountDialog : public Dialog
{
public:
	char lines[6][64]; int n;
	CountDialog () : Dialog (340, 250, has_sel () ? "Word Count (selection)" : "Word Count"), n (0)
	{
		Pos a = has_sel () ? sel_a () : mkpos (0, 0), b = has_sel () ? sel_b () : doc_end (g_doc);
		Counts c = doc_count (g_doc, a, b);
		int nl = 0;
		for (int p = a.p; p <= b.p; p++) nl += g_doc.p[p]->nln;
		add ("Pages", L.npages); add ("Words", c.words); add ("Characters (no spaces)", c.chars);
		add ("Characters (with spaces)", c.charsSp); add ("Paragraphs", c.paras); add ("Lines", nl);
		button (width - 94, height - 42, 82, "Close", 1);
	}
	void add (const char *k, int v) { scpy (lines[n], k, 40); int m = slen (lines[n]); lines[n][m++] = '\t'; fmt_int (lines[n] + m, v); n++; }
	void drawBody () override
	{
		for (int i = 0; i < n; i++)
		{
			char k[64]; int j = 0; while (lines[i][j] && lines[i][j] != '\t') { k[j] = lines[i][j]; j++; } k[j] = 0;
			label (24, 44 + i * 24, k);
			const char *v = lines[i] + j + 1;
			canvas.text (width - 24 - wk_text_w (v), 44 + i * 24, v, C_TEXT);
		}
	}
};
static void dlg_word_count () { CountDialog d; d.run (); }

// ---- Find and Replace ------------------------------------------------------------------------------------
class FindDialog : public Dialog
{
public:
	Textbox *tFind, *tRep; Checkbox *cbCase; PageView *view; char msg[64];
	FindDialog (PageView *v) : Dialog (470, 214, "Find and Replace"), view (v)
	{
		Root *r = Root::current ();
		if (r) { left = wmax (0, r->width - width - 30); top = 2 * TB_H + RULER_H + 16; }
		static char lastFind[64], lastRep[64];
		s_find = lastFind; s_rep = lastRep;
		if (has_sel () && sel_a ().p == sel_b ().p && sel_b ().o - sel_a ().o < 60)	// (the selection: what is looked for)
		{
			unsigned u[64]; int n = doc_text (g_doc, sel_a (), sel_b (), u, 63);
			encode_text (u, n, lastFind, 64);
		}
		tFind = field (130, 44, 320, lastFind); tFind->cb = onFindEnter;
		tRep = field (130, 78, 320, lastRep);
		cbCase = new Checkbox (130, 110, 200, 24, "Match case", false, 0, C_FACE); addChild (cbCase);
		button (16, height - 44, 104, "Find Next", 2);
		button (126, height - 44, 96, "Replace", 3);
		button (228, height - 44, 110, "Replace All", 4);
		button (width - 94, height - 44, 82, "Close", 0);
		msg[0] = 0;
		tFind->setFocus ();
	}
	void drawBody () override
	{
		label (20, 50, "Find:"); label (20, 84, "Replace with:");
		if (msg[0]) canvas.text (130, 142, msg, wk_mix (C_FACE, C_TEXT, 170));
	}
	int pattern (const char *s, unsigned *u) { return decode_text (s, slen (s), u, 64); }
	bool findNext ()
	{
		unsigned pat[64]; int n = pattern (tFind->text, pat);
		Pos a, b;
		if (n && find_next (pat, n, cbCase->checked, has_sel () ? sel_b () : g_caret, a, b))
		{
			set_caret (a, false); set_caret (b, true);
			view->ensureVisible (); view->invalidate (true);
			msg[0] = 0; invalidate (true);
			// (the dialog out of the way of what was found)
			int x, y, h; view->caretBox (b, false, &x, &y, &h);
			y += view->top;
			Root *r = Root::current ();
			if (r && y + h > top - 8 && y < top + height + 8)
			{
				top = y < r->height / 2 ? wmin (r->height - height - 30, y + h + 40) : wmax (2 * TB_H + 8, y - height - 40);
				r->invalidate (true);
			}
			return true;
		}
		scpy (msg, n ? "Not found." : "", sizeof msg); invalidate (true);
		return false;
	}
	void onButton (int tag) override
	{
		scpy (s_find, tFind->text, 64); scpy (s_rep, tRep->text, 64);
		if (tag == 0 || tag == 1) { if (tag == 1) findNext (); else close (0); return; }
		if (tag == 2) { findNext (); return; }
		unsigned pat[64], rep[64]; int n = pattern (tFind->text, pat), rn = pattern (tRep->text, rep);
		if (!n) return;
		if (tag == 3)
		{
			// (the selection is an occurrence: replaced, then the next one found)
			if (has_sel () && sel_a ().p == sel_b ().p && sel_b ().o - sel_a ().o == n)
			{
				const Para *q = g_doc.p[sel_a ().p];
				int k = 0;
				while (k < n && (cbCase->checked ? q->ch[sel_a ().o + k] == pat[k] : fold (q->ch[sel_a ().o + k]) == fold (pat[k]))) k++;
				if (k == n) { ed_type (rep, rn); view->invalidate (true); }
			}
			findNext ();
			return;
		}
		int c = replace_all (pat, n, rep, rn, cbCase->checked);
		fmt_int (msg, c);
		int m = slen (msg); scpy (msg + m, c == 1 ? " replacement." : " replacements.", 64 - m);
		view->invalidate (true); invalidate (true);
	}
	static void onFindEnter (Widget &w) { Widget *p = w.parent; ((FindDialog *) p)->onButton (2); }
	static char *s_find, *s_rep;
};
char *FindDialog::s_find, *FindDialog::s_rep;
static void dlg_find (PageView *v) { FindDialog d (v); d.run (); }

// ---- Special Character -------------------------------------------------------------------------------------
struct Block { const char *name; unsigned a, b; };
static const Block BLOCKS[] = {
	{ "Latin-1 Supplement", 0xA1, 0xFF }, { "Latin Extended-A", 0x100, 0x17F }, { "Greek", 0x384, 0x3CE },
	{ "Cyrillic", 0x400, 0x45F }, { "Punctuation", 0x2010, 0x205E }, { "Currency", 0x20A0, 0x20C0 },
	{ "Letterlike, Numbers", 0x2100, 0x218B }, { "Arrows", 0x2190, 0x21FF }, { "Mathematical", 0x2200, 0x22FF },
	{ "Box Drawing, Shapes", 0x2500, 0x25FF }, { "Symbols", 0x2600, 0x26FF }, { "Dingbats", 0x2700, 0x27BF } };
static const char *BLOCK_NAMES[12];

class SymbolDialog : public Dialog
{
public:
	enum { COLS = 16, CELL = 30, ROWS = 8 };
	Dropdown *blk;
	unsigned cps[512]; int n, top, sel;
	int fam; unsigned last;
	SymbolDialog () : Dialog (COLS * CELL + 150, ROWS * CELL + 124, "Special Character"), n (0), top (0), sel (0), last (0)
	{
		for (int i = 0; i < 12; i++) BLOCK_NAMES[i] = BLOCKS[i].name;
		fam = font_family (g_doc.fmt[caret_cf ()].font);
		blk = new Dropdown (116, 40, 230, 26, BLOCK_NAMES, 12, s_block, onBlock); addChild (blk);
		button (width - 120, 80, 104, "Insert", 2);
		button (width - 94, height - 42, 82, "Close", 0);
		fill ();
	}
	static int s_block;
	void fill ()
	{
		const Block &b = BLOCKS[blk->sel];
		fnt::Font *f = fnt::get (fam, 0, 18 * 64);
		n = 0; top = 0; sel = 0;
		for (unsigned c = b.a; c <= b.b && n < 512; c++)
		{
			if (c == 0xAD) continue;
			if (f) { fnt::Glyph *g = fnt::glyph (f, c); if (!g->gi) continue; }
			cps[n++] = c;
		}
	}
	static void onBlock (Widget &w) { SymbolDialog *d = (SymbolDialog *) w.parent; s_block = d->blk->sel; d->fill (); d->invalidate (true); }
	int gx () const { return 16; }
	int gy () const { return 76; }
	void drawBody () override
	{
		label (16, 46, "Characters:");
		int x0 = gx (), y0 = gy ();
		canvas.fillRect (x0, y0, COLS * CELL + 1, ROWS * CELL + 1, 0xFFFFFF);
		fnt::Font *f = fnt::get (fam, 0, 18 * 64);
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < COLS; c++)
			{
				int i = (top + r) * COLS + c, x = x0 + c * CELL, y = y0 + r * CELL;
				if (i < n && i == sel) canvas.fillRect (x + 1, y + 1, CELL - 1, CELL - 1, wk_mix (0xFFFFFF, C_ACCENT, 90));
				if (i < n && f) { int a = fnt::advance (f, cps[i]); fnt::draw (canvas, f, x * 64 + (CELL * 64 - a) / 2, y + 22, cps[i], 0, x, y, x + CELL, y + CELL); }
			}
		for (int r = 0; r <= ROWS; r++) canvas.fillRect (x0, y0 + r * CELL, COLS * CELL + 1, 1, 0xD0D0D0);
		for (int c = 0; c <= COLS; c++) canvas.fillRect (x0 + c * CELL, y0, 1, ROWS * CELL + 1, 0xD0D0D0);
		// the chosen one, large, and its code
		int px = x0 + COLS * CELL + 16, pw = width - px - 16;
		if (sel < n)
		{
			wk_sunken (canvas, px, 124, pw, 90, 6, 0xFFFFFF);
			fnt::Font *big = fnt::get (fam, 0, 48 * 64);
			if (big) { int a = fnt::advance (big, cps[sel]); fnt::draw (canvas, big, (px + pw / 2) * 64 - a / 2, 124 + 64, cps[sel], 0, px, 124, px + pw, 214); }
			char u[12] = "U+"; const char *h = "0123456789ABCDEF"; unsigned c = cps[sel];
			int k = 2, digits = c > 0xFFFF ? 5 : 4;
			for (int s = (digits - 1) * 4; s >= 0; s -= 4) u[k++] = h[(c >> s) & 15];
			u[k] = 0;
			canvas.text (px + (pw - wk_text_w (u)) / 2, 222, u, C_TEXT);
		}
		if (n > ROWS * COLS)
		{
			int rows = (n + COLS - 1) / COLS;
			WkThumb t = wk_thumb (rows, ROWS, top, ROWS * CELL);
			wk_draw_vscroll (canvas, x0 + COLS * CELL + 3, y0, WK_SBW, ROWS * CELL, t, C_FACE);
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		int x0 = gx (), y0 = gy ();
		bool inGrid = mx >= x0 && my >= y0 && mx < x0 + COLS * CELL && my < y0 + ROWS * CELL;
		if (wheel && inGrid) { int rows = (n + COLS - 1) / COLS; top = wclamp (top - wheel, 0, wmax (0, rows - ROWS)); invalidate (true); return true; }
		if (inGrid && bl && !pressed)
		{
			pressed = true;
			int i = (top + (my - y0) / CELL) * COLS + (mx - x0) / CELL;
			if (i < n)
			{
				unsigned t = kapi_get_ticks ();
				if (i == sel && t - last < 45) { insert (); last = 0; }
				else { sel = i; last = t; }
				invalidate (true);
			}
			return true;
		}
		if (!bl) pressed = false;
		return Dialog::onMouse (mx, my, bl, br, bm, wheel);
	}
	void insert () { if (sel < n) { unsigned c = cps[sel]; ed_type (&c, 1); } }
	void onButton (int tag) override { if (tag == 2 || tag == 1) insert (); else close (0); }
};
int SymbolDialog::s_block;
static void dlg_symbol () { SymbolDialog d; d.run (); }

// ---- Date and Time -----------------------------------------------------------------------------------------
class DateDialog : public Dialog
{
public:
	ListBox *list; char items[8][48]; int n;
	DateDialog () : Dialog (360, 320, "Date and Time"), n (0)
	{
		int y = 2026, mo = 1, d = 1, h = 0, mi = 0, se = 0;
		kapi_get_datetime (&y, &mo, &d, &h, &mi, &se);
		static const char *const MONTHS[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
		static const char *const DAYS[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
		// (the day of the week: Sakamoto's)
		static const int T[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
		int yy = mo < 3 ? y - 1 : y, dow = (yy + yy / 4 - yy / 100 + yy / 400 + T[(mo - 1) % 12] + d) % 7;
		char dd[4], mm[4], yyyy[8], hh[4], mn[4], dn[4];
		auto two = [] (char *b, int v) { b[0] = (char) ('0' + v / 10 % 10); b[1] = (char) ('0' + v % 10); b[2] = 0; };
		two (dd, d); two (mm, mo); fmt_int (yyyy, y); two (hh, h); two (mn, mi); fmt_int (dn, d);
		const char *M = MONTHS[(mo - 1) % 12];
		add (dd, "/", mm, "/", yyyy);
		add (DAYS[dow], " ", dn, " ", M, " ", yyyy);
		add (dn, " ", M, " ", yyyy);
		add (M, " ", dn, ", ", yyyy);
		add (yyyy, "-", mm, "-", dd);
		add (hh, ":", mn);
		add (dd, "/", mm, "/", yyyy, " ", hh, ":", mn);
		list = new ListBox (16, 44, width - 32, height - 104, 0, onPick); addChild (list);
		for (int i = 0; i < n; i++) list->add (items[i]);
		list->setSel (0);
		okCancel ();
	}
	void add (const char *a, const char *b = "", const char *c = "", const char *d = "", const char *e = "", const char *f = "", const char *g = "", const char *h = "", const char *i = "")
	{
		const char *p[9] = { a, b, c, d, e, f, g, h, i };
		int k = 0;
		for (int j = 0; j < 9; j++) for (const char *s = p[j]; *s && k < 47; s++) items[n][k++] = *s;
		items[n][k] = 0; n++;
	}
	static void onPick (Widget &w) { Widget *p = w.parent; ((Modal *) p)->close (1); }
};
static void dlg_datetime ()
{
	DateDialog d;
	if (d.run () != 1 || d.list->sel < 0) return;
	unsigned u[64]; int n = decode_text (d.items[d.list->sel], slen (d.items[d.list->sel]), u, 64);
	ed_type (u, n);
}

} // namespace wr

#endif
