//
// uikit/form.cpp -- FormDialog (uikit/form.h): the rows laid out for the mode -- two columns in a box (the desktop,
// pocket's landscape, console), one column in a sheet the window's width (portrait). The rows behind Widget::ext.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "uikit/form.h"
#include "uikit/adapt.h"
#include "uikit/paint.h"
#include "uikit/root.h"
#include "uikit/internal/adapt_int.h"
#include "appkit/appkit.h"

namespace uikit {

namespace {
enum { FM_ROW = 0, FM_SECTION, FM_TEXT, FM_MAXROWS = 48, FM_MAXBTN = 6, FM_PAD = 16, FM_GAP = 8 };
struct FmRow { int kind; char label[64]; char *text; Widget *w; int lx, ly, y, h; };
struct FmBtn { Button *b; int role, result; };
struct FmExt : internal::ExtHead
{
	char	 title[64];
	int	 wantW = 0;
	FmRow	 rows[FM_MAXROWS]; int n = 0;
	FmBtn	 btn[FM_MAXBTN]; int nb = 0;
	bool	 sheet = false;			// (portrait: the window's width, one column)
	int	 labW = 0;
};
FmExt *fm (FormDialog *d) { return (FmExt *) internal::ext_head (d); }
void fm_btn (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->onButton (w.tag); }
void fm_copy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

// A paragraph's lines at w px -> how many (each its start and length).
int fm_wrap (const char *t, int w, int *start, int *len, int max) { return t ? uk_text_wrap (t, uk_len (t), w, max, start, len) : 0; }
}

FormDialog::FormDialog (const char *title, int width) : Modal (360, 120)
{
	FmExt *e = internal::ext_of<FmExt> (this, internal::EXT_FORM);
	e->destroy = [] (internal::ExtHead *x) { FmExt *f = (FmExt *) x; for (int i = 0; i < f->n; i++) delete [] f->rows[i].text; delete f; };
	fm_copy (e->title, title, sizeof e->title);
	e->wantW = width;
}

FormDialog::~FormDialog () {}

void FormDialog::addSection (const char *title)
{
	FmExt *e = fm (this);
	if (e->n >= FM_MAXROWS) return;
	FmRow &r = e->rows[e->n++];
	r.kind = FM_SECTION; fm_copy (r.label, title, sizeof r.label); r.text = 0; r.w = 0;
}

Widget *FormDialog::addRow (const char *label, Widget *w)
{
	FmExt *e = fm (this);
	if (e->n >= FM_MAXROWS || w == 0) return w;
	FmRow &r = e->rows[e->n++];
	r.kind = FM_ROW; fm_copy (r.label, label, sizeof r.label); r.text = 0; r.w = w;
	r.lx = w->width;						// (its own width: kept for the narrow controls)
	addChild (w);
	return w;
}

void FormDialog::addText (const char *text)
{
	FmExt *e = fm (this);
	if (e->n >= FM_MAXROWS) return;
	FmRow &r = e->rows[e->n++];
	r.kind = FM_TEXT; r.label[0] = 0; r.w = 0;
	int n = uk_len (text);
	r.text = new char[n + 1];
	for (int i = 0; i <= n; i++) r.text[i] = text ? text[i] : 0;
}

Button *FormDialog::addButton (const char *label, int role, int result)
{
	FmExt *e = fm (this);
	if (e->nb >= FM_MAXBTN) return 0;
	int bw = uk_tw (label) + 28;
	if (bw < 82) bw = 82;
	Button *b = new Button (0, 0, bw, 28, label, fm_btn);
	b->tag = e->nb;
	e->btn[e->nb].b = b; e->btn[e->nb].role = role; e->btn[e->nb].result = result;
	e->nb++;
	addChild (b);
	return b;
}

void FormDialog::onButton (int tag)
{
	FmExt *e = fm (this);
	if (tag >= 0 && tag < e->nb) close (e->btn[tag].result);
}

bool FormDialog::onKey (long k)
{
	FmExt *e = fm (this);
	int want = k == KEY_ENTER ? UK_FB_DEFAULT : k == 27 ? UK_FB_CANCEL : -1;
	if (want < 0) return false;
	for (int i = 0; i < e->nb; i++) if (e->btn[i].role == want) { close (e->btn[i].result); return true; }
	if (k == 27) { close (0); return true; }
	return false;
}

// The rows placed for the mode; the box's size set (centred in the window by run ()).
void FormDialog::layout ()
{
	FmExt *e = fm (this);
	if (e == 0) { Widget::layout (); return; }
	Root *r = Root::current ();
	int RW = r ? r->width : 800;
	int sc = uk_size_class ();
	e->sheet = sc == UK_SC_NARROW;
	int fh = uk_fh (), th = titleH ();
	int labW = 0, ctlW = 0;
	for (int i = 0; i < e->n; i++)
		if (e->rows[i].kind == FM_ROW)
		{
			if (e->rows[i].label[0]) { int t = uk_tw (e->rows[i].label); if (t > labW) labW = t; }
			if (e->rows[i].lx > ctlW) ctlW = e->rows[i].lx;
		}
	int W;
	if (e->sheet) W = RW;
	else
	{
		W = e->wantW > 0 ? e->wantW : FM_PAD * 2 + (labW ? labW + 12 : 0) + ctlW;
		int bw = FM_PAD * 2; for (int i = 0; i < e->nb; i++) bw += e->btn[i].b->width + 8;
		if (W < bw) W = bw;
		if (W < 360) W = 360;
		if (W > RW - 20) W = RW - 20;
	}
	if (!e->sheet && labW > W * 45 / 100) labW = W * 45 / 100;
	e->labW = labW;
	int colX = e->sheet ? FM_PAD : FM_PAD + (labW ? labW + 12 : 0);
	int colW = W - colX - FM_PAD;
	int y = th + (e->sheet ? 12 : 14);
	for (int i = 0; i < e->n; i++)
	{
		FmRow &row = e->rows[i];
		if (row.kind == FM_SECTION)
		{
			y += i > 0 ? 6 : 0;
			row.y = y; row.h = fh + 8;
			y += row.h + 4;
			continue;
		}
		if (row.kind == FM_TEXT)
		{
			int st[32], ln[32];
			int lines = fm_wrap (row.text, W - 2 * FM_PAD, st, ln, 32);
			row.y = y; row.h = lines * (fh + 3);
			y += row.h + FM_GAP;
			continue;
		}
		Widget *w = row.w;
		int x = colX, cw = colW;
		if (!row.label[0]) { x = FM_PAD; cw = W - 2 * FM_PAD; }
		if (e->sheet && row.label[0]) { row.ly = y; y += fh + 4; }	// (portrait: the label over its control)
		int ww = row.lx >= 100 || e->sheet ? cw : row.lx;
		if (ww > cw) ww = cw;
		if (ww != w->width) w->resizeTo (ww, w->height);
		w->left = x; w->top = y;
		int rh = w->height > fh + 4 ? w->height : fh + 4;
		if (!e->sheet) row.ly = y + (w->height - fh) / 2;
		row.y = y; row.h = rh;
		y += rh + FM_GAP;
	}
	// the buttons: the desktop's bottom right (the default, then Cancel); a destructive one at the left
	y += 6;
	int bh = 28, H;
	if (e->sheet)
	{
		int bx = W - FM_PAD, lx = FM_PAD;
		for (int i = 0; i < e->nb; i++)				// (portrait: Cancel at the title's left, the default at its right)
		{
			Button *b = e->btn[i].b;
			if (e->btn[i].role == UK_FB_CANCEL) { b->left = 6; b->top = (th - bh) / 2 + 1; }
			else if (e->btn[i].role == UK_FB_DEFAULT) { b->left = W - 6 - b->width; b->top = (th - bh) / 2 + 1; }
			else { b->left = lx; b->top = y; lx += b->width + 8; (void) bx; }
		}
		H = y + bh + FM_PAD;
		int RH = r ? r->height : H;
		if (H < RH) H = RH;					// (the whole window; taller: the sheet scrolls)
	}
	else
	{
		int bx = W - FM_PAD;
		for (int pass = 0; pass < 3; pass++)			// (the right end: Cancel, then the default, then the others)
			for (int i = e->nb - 1; i >= 0; i--)
			{
				int role = e->btn[i].role;
				bool mine = pass == 0 ? role == UK_FB_CANCEL : pass == 1 ? role == UK_FB_DEFAULT : role == UK_FB_OTHER;
				if (!mine) continue;
				Button *b = e->btn[i].b;
				bx -= b->width; b->left = bx; b->top = y; bx -= sc == UK_SC_CONSOLE ? 28 : 8;	// (console: room for the pad's mark)
			}
		int lx = FM_PAD;
		for (int i = 0; i < e->nb; i++) if (e->btn[i].role == UK_FB_DESTRUCTIVE) { Button *b = e->btn[i].b; b->left = lx; b->top = y; lx += b->width + 8; }
		H = y + bh + 12;
	}
	if (W != width || H != height) { width = W; height = H; canvas.resize (W, H); }
	lytW = width; lytH = height;
	invalidate (true);
}

void FormDialog::onDraw ()
{
	FmExt *e = fm (this);
	drawBox (e->sheet ? "" : e->title);
	int fh = uk_fh ();
	bool con = uk_size_class () == UK_SC_CONSOLE;
	if (e->sheet) uk_text_c (canvas, 90, 1, width - 180, titleH (), e->title, C_TEXT, 2);	// (between Cancel and the default)
	for (int i = 0; i < e->n; i++)
	{
		FmRow &row = e->rows[i];
		if (row.kind == FM_SECTION)
		{
			uk_text (canvas, FM_PAD, row.y, row.label, C_TEXT, 2);
			int tw = uk_tw (row.label, 2) + 8;
			uk_etch_h (canvas, FM_PAD + tw, row.y + fh / 2, width - 2 * FM_PAD - tw, C_FACE);
			continue;
		}
		if (row.kind == FM_TEXT)
		{
			int st[32], ln[32];
			int lines = fm_wrap (row.text, width - 2 * FM_PAD, st, ln, 32);
			for (int k = 0; k < lines; k++)
			{
				char b[200]; int n = ln[k] < 199 ? ln[k] : 199;
				for (int c = 0; c < n; c++) b[c] = row.text[st[k] + c];
				b[n] = 0;
				canvas.text (FM_PAD, row.y + k * (fh + 3), b, C_TEXT);
			}
			continue;
		}
		if (row.label[0])
		{
			char b[64];
			int lw = e->sheet ? width - 2 * FM_PAD : e->labW;
			uk_text_fit (row.label, lw, b, sizeof b);
			canvas.text (FM_PAD, row.ly, b, C_TEXT);
		}
	}
	if (con)							// console: the pad's buttons beside the default and Cancel
		for (int i = 0; i < e->nb; i++)
		{
			Button *b = e->btn[i].b;
			if (e->btn[i].role != UK_FB_DEFAULT && e->btn[i].role != UK_FB_CANCEL) continue;
			int cx = b->left - 11, cy = b->top + b->height / 2;
			uk_glyph (canvas, e->btn[i].role == UK_FB_DEFAULT ? WKG_CLOSE : WKG_RING, cx, cy, 10, e->btn[i].role == UK_FB_DEFAULT ? 0x003C7CE0 : 0x00D04848);
		}
}

int FormDialog::run ()
{
	layout ();
	Root *r = Root::current ();
	FmExt *e = fm (this);
	if (r)
	{
		left = e->sheet ? 0 : (r->width - width) / 2;
		top = e->sheet ? 0 : (r->height - height) / 2;
		if (top < 0) top = 0;
		if (left < 0) left = 0;
	}
	return Modal::run ();
}

} // namespace uikit
