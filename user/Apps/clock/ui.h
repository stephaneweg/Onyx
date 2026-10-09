//
// ui.h -- the Clock's own widgets and drawings (04-ux-design.md), beside the app (one translation unit with main.cpp):
// the faces of the big times, the meaning colours, the small drawings (bell, globe, sun, moon, hourglass, warning,
// struck speaker, map pin), FootText (a tab's footer text), Spin (a NumericUpDown that passes Enter, Esc and Ctrl on),
// Page (a tab's container), Veil (the overlays: a card over the window, what is under it dimmed -- not Modal::run,
// so the window's onTick goes on: 03 §1.1 fact 4, 04 D16).
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
#ifndef CLOCK_UI_H
#define CLOCK_UI_H
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "fontkit/uikitface.h"

using namespace uikit;

static const int MIN_W = 560, MIN_H = 440;		// the client area (04 D1): the French pictures checked at that size
static const int TOP = 48, FOOT = 46;			// the tab bar's band, the footer's

// ---- the meanings' colours (04 §4), lighter on a dark theme --------------------------------------------------------
static inline bool dark_theme () { return uk_bright (C_FIELD) < 128; }
static inline unsigned GREEN () { return dark_theme () ? 0x5BD27A : 0x2E9A44; }
static inline unsigned RED () { return dark_theme () ? 0xFF6B5E : 0xC0302A; }
static inline unsigned AMBER () { return dark_theme () ? 0xF0B040 : 0xB06A00; }
static inline unsigned dim_on (unsigned bg) { return uk_mix (bg, uk_ink_for (bg), 150); }

// ---- the faces: DejaVu Sans at the sizes the big times need, opened when first drawn, then kept --------------------
static FtTextFace *g_face[80];
static inline TextFace *face (int px)
{
	if (px < 6) px = 6;
	if (px > 79) px = 79;
	if (!g_face[px])
	{
		g_face[px] = new FtTextFace;
		if (!g_face[px]->open ("DejaVu Sans", px)) { delete g_face[px]; g_face[px] = 0; return 0; }
	}
	return g_face[px];
}

// ---- small drawings (VPath: anti-aliased, any size; coordinates in 1/16 px) ----------------------------------------
static inline int VV (double x) { return (int) lround (x * 16); }
static inline void draw_bell (Canvas &cv, double cx, double cy, double s, unsigned c, bool waves = false)
{
	VPath p;
	p.circle (VV (cx), VV (cy - s * 0.12), VV (s * 0.30));
	int body[8] = { VV (cx - s * 0.30), VV (cy - s * 0.12), VV (cx + s * 0.30), VV (cy - s * 0.12), VV (cx + s * 0.40), VV (cy + s * 0.26), VV (cx - s * 0.40), VV (cy + s * 0.26) };
	p.poly (body, 4);
	p.rrect (VV (cx - s * 0.48), VV (cy + s * 0.20), VV (s * 0.96), VV (s * 0.12), VV (s * 0.06));
	p.circle (VV (cx), VV (cy + s * 0.40), VV (s * 0.10));
	p.circle (VV (cx), VV (cy - s * 0.45), VV (s * 0.07));
	p.fill (cv, c);
	if (!waves) return;
	VPath w;
	for (int k = 0; k < 2; k++)
	{
		double r = s * (0.66 + 0.2 * k);
		w.arc (VV (cx), VV (cy), VV (r), -28, 28, VV (s * 0.06 + 0.6));
		w.arc (VV (cx), VV (cy), VV (r), 152, 208, VV (s * 0.06 + 0.6));
	}
	w.fill (cv, c);
}
static inline void draw_slash (Canvas &cv, double cx, double cy, double s, unsigned c, unsigned under)	// a bell struck through
{
	VPath p; p.line (VV (cx - s * 0.45), VV (cy - s * 0.45), VV (cx + s * 0.45), VV (cy + s * 0.45), VV (s * 0.16)); p.fill (cv, under);
	VPath q; q.line (VV (cx - s * 0.45), VV (cy - s * 0.45), VV (cx + s * 0.45), VV (cy + s * 0.45), VV (s * 0.07)); q.fill (cv, c);
}
static inline void draw_globe (Canvas &cv, double cx, double cy, double r, unsigned c)
{
	VPath p; int w = VV (r * 0.07 + 0.5);
	p.arc (VV (cx), VV (cy), VV (r), 0, 360, w);
	p.line (VV (cx - r), VV (cy), VV (cx + r), VV (cy), w);
	for (int k = 0; k < 2; k++)
	{
		double yy = cy + (k ? 0.5 : -0.5) * r, hw = sqrt (1 - 0.25) * r;
		p.line (VV (cx - hw), VV (yy), VV (cx + hw), VV (yy), w);
	}
	p.line (VV (cx), VV (cy - r), VV (cx), VV (cy + r), w);
	int xy[2 * 25];
	for (int i = 0; i < 25; i++) { double t = M_PI * 2 * i / 24; xy[2 * i] = VV (cx + r * 0.45 * cos (t)); xy[2 * i + 1] = VV (cy + r * sin (t)); }
	p.polyline (xy, 24, w, true);
	p.fill (cv, c);
}
static inline void draw_sun (Canvas &cv, double cx, double cy, double r)
{
	VPath p; p.circle (VV (cx), VV (cy), VV (r * 0.5));
	for (int i = 0; i < 8; i++)
	{
		double t = M_PI / 4 * i;
		p.line (VV (cx + cos (t) * r * 0.72), VV (cy + sin (t) * r * 0.72), VV (cx + cos (t) * r), VV (cy + sin (t) * r), VV (r * 0.16));
	}
	p.fill (cv, 0xF0A020);
}
static inline void draw_moon (Canvas &cv, double cx, double cy, double r)
{
	VPath p; p.circle (VV (cx), VV (cy), VV (r * 0.8)); p.hole (VV (cx + r * 0.45), VV (cy - r * 0.3), VV (r * 0.68));
	p.fill (cv, dark_theme () ? 0x9AA8D8 : 0x5A6A9A);
}
static inline void draw_hourglass (Canvas &cv, double cx, double cy, double s, unsigned c)
{
	VPath p;
	p.rrect (VV (cx - s * 0.36), VV (cy - s * 0.5), VV (s * 0.72), VV (s * 0.1), VV (s * 0.04));
	p.rrect (VV (cx - s * 0.36), VV (cy + s * 0.4), VV (s * 0.72), VV (s * 0.1), VV (s * 0.04));
	int a[6] = { VV (cx - s * 0.28), VV (cy - s * 0.4), VV (cx + s * 0.28), VV (cy - s * 0.4), VV (cx), VV (cy - s * 0.02) };
	int b[6] = { VV (cx), VV (cy + s * 0.02), VV (cx + s * 0.28), VV (cy + s * 0.4), VV (cx - s * 0.28), VV (cy + s * 0.4) };
	p.polyline (a, 3, VV (s * 0.06), true); p.polyline (b, 3, VV (s * 0.06), true);
	int sand[6] = { VV (cx - s * 0.2), VV (cy + s * 0.37), VV (cx + s * 0.2), VV (cy + s * 0.37), VV (cx), VV (cy + s * 0.16) };
	p.poly (sand, 3);
	p.fill (cv, c);
}
static inline void draw_warn (Canvas &cv, double cx, double cy, double s)
{
	VPath p; int t[6] = { VV (cx), VV (cy - s * 0.5), VV (cx + s * 0.55), VV (cy + s * 0.45), VV (cx - s * 0.55), VV (cy + s * 0.45) };
	p.poly (t, 3); p.fill (cv, 0xE0A020);
	VPath q; q.line (VV (cx), VV (cy - s * 0.18), VV (cx), VV (cy + s * 0.12), VV (s * 0.12)); q.circle (VV (cx), VV (cy + s * 0.29), VV (s * 0.07));
	q.fill (cv, 0x000000);
}
static inline void draw_speaker_off (Canvas &cv, double cx, double cy, double s, unsigned c)
{
	VPath p;
	int b[12] = { VV (cx - s * 0.5), VV (cy - s * 0.16), VV (cx - s * 0.28), VV (cy - s * 0.16), VV (cx), VV (cy - s * 0.42),
		      VV (cx), VV (cy + s * 0.42), VV (cx - s * 0.28), VV (cy + s * 0.16), VV (cx - s * 0.5), VV (cy + s * 0.16) };
	p.poly (b, 6);
	p.line (VV (cx + s * 0.14), VV (cy - s * 0.18), VV (cx + s * 0.5), VV (cy + s * 0.18), VV (s * 0.1));
	p.line (VV (cx + s * 0.14), VV (cy + s * 0.18), VV (cx + s * 0.5), VV (cy - s * 0.18), VV (s * 0.1));
	p.fill (cv, c);
}
static inline void draw_pin (Canvas &cv, double cx, double cy, double s, unsigned c)		// a map pin: "here"
{
	VPath p; p.circle (VV (cx), VV (cy - s * 0.18), VV (s * 0.3));
	int t[6] = { VV (cx - s * 0.26), VV (cy - s * 0.04), VV (cx + s * 0.26), VV (cy - s * 0.04), VV (cx), VV (cy + s * 0.5) };
	p.poly (t, 3);
	p.hole (VV (cx), VV (cy - s * 0.18), VV (s * 0.12));
	p.fill (cv, c);
}
// ToolButton::setIcon's drawer for the Move up / Move down arrows (id 0 up, 1 down)
static inline void chev_icon (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool)
{
	uk_glyph (cv, id ? WKG_CHEV_DOWN : WKG_CHEV_UP, x + size / 2, y + size / 2, size - 6, ink);
}

// ---- small helpers ---------------------------------------------------------------------------------------------------
static inline void cat (char *d, int cap, const char *s) { int n = (int) strlen (d); snprintf (d + n, cap > n ? cap - n : 0, "%s", s); }
#define DOT	"  \xC2\xB7  "			// the " · " between two facts of a line

// A tool button in the raised style of the footers (04 D3): its glyph, its text, its width fitted + 8 px.
static inline ToolButton *tool_button (const char *tip, int glyph, const char *text, Action cb, int w = 0, int h = 30)
{
	ToolButton *b = new ToolButton (w, h, tip, cb);
	if (glyph != WKT_NONE) b->setGlyph (glyph);
	if (text) b->setText (text);
	if (!w) { b->fitWidth (); b->resizeTo (b->width + 8, b->height); }
	b->raised = true;
	return b;
}

// ---- FootText: the footer's text -- what the tab holds (dim), then what happened (a check and green, or red) ------------
class FootText : public Widget
{
public:
	char l[128], msg[128]; bool bad;
	FootText (int x, int y, int w, int h) : Widget (x, y, w, h), bad (false) { l[0] = msg[0] = 0; }
	void set (const char *s) { if (strcmp (s, l)) { snprintf (l, sizeof l, "%s", s); invalidate (true); } }
	void say (const char *s, bool isBad = false)
	{
		if (strcmp (s, msg) || bad != isBad) { snprintf (msg, sizeof msg, "%s", s); bad = isBad; invalidate (true); }
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_text_l (canvas, 0, 0, height, l, uk_mix (C_BG, C_TEXT, 170));
		if (!msg[0]) return;
		int x = l[0] ? uk_tw (l) + 18 : 0;
		unsigned c = bad ? RED () : GREEN ();
		uk_glyph (canvas, bad ? WKG_CLOSE : WKG_CHECK, x + 5, height / 2, 10, c);
		uk_text_l (canvas, x + 14, 0, height, msg, c);
	}
};

// ---- Spin: every spin box of the app (04 §7, 03 G9) ------------------------------------------------------------------
// UIKit's NumericUpDown takes Enter, Esc and every digit (Ctrl held or not); the focused child is asked first. A Spin
// passes on any key with Ctrl held (Ctrl+1..4, Ctrl+N reach the window), Enter after committing the typed digits (the
// card's OK), Esc after dropping them (the card's Cancel).
class Spin : public NumericUpDown
{
public:
	Spin (int l, int t, int w, int h, int lo, int hi, int val, Action cb) : NumericUpDown (l, t, w, h, lo, hi, val, 1, cb) {}
	bool onKey (long k) override
	{
		if (kapi_get_modifiers () & MOD_CTRL) return false;
		if (k == KEY_ENTER || k == 27) { NumericUpDown::onKey (k); return false; }
		return NumericUpDown::onKey (k);
	}
};

// ---- Page: one tab's widgets (shown / hidden by the tab bar), the area under the tab bar ------------------------------
class Page : public Widget
{
public:
	Page (int w, int h) : Widget (0, TOP, w, h - TOP) { anchor = ANCHOR_FILL; }
	unsigned bgColor () override { return C_BG; }
	void onDraw () override { canvas.clear (C_BG); }
};
// A caption (bold or dim) on the face
class Caption : public Widget
{
public:
	char s[96]; bool bold; bool centred;
	Caption (int x, int y, int w, int h, const char *t, bool b, bool c = false) : Widget (x, y, w, h), bold (b), centred (c) { snprintf (s, sizeof s, "%s", t); }
	void set (const char *t) { if (strcmp (t, s)) { snprintf (s, sizeof s, "%s", t); invalidate (true); } }
	void onDraw () override
	{
		canvas.clear (C_BG);
		if (centred) uk_text_c (canvas, 0, 0, width, height, s, bold ? C_TEXT : dim_on (C_BG), bold ? 2 : 0);
		else uk_text_l (canvas, 0, 0, height, s, bold ? C_TEXT : dim_on (C_BG), bold ? 2 : 0);
	}
};

// ---- Veil: an overlay card (04 D16) ----------------------------------------------------------------------------------
// The client area's size, the last child of the window, modal (UIKit routes every key and click to it, Tab walks its
// controls, the menus' shortcuts are off). Its controls are its children, placed from the card's corner (place ()). It
// draws what was under it when it came, dimmed (frozen until a resize), then the card: a rounded face, a title strip.
// One object a kind of card, kept: show () puts it on top, hide () takes it off the window.
class Veil : public Widget
{
public:
	int cw, ch;				// the card's size
	int cx, cy;				// its corner
	char title[64];
	Veil (int cardW, int cardH, const char *t) : Widget (0, 0, MIN_W, MIN_H), cw (cardW), ch (cardH), m_snap (0), m_snapW (0), m_snapH (0), m_snapOk (false)
	{
		snprintf (title, sizeof title, "%s", t);
		modal = true; anchor = ANCHOR_FILL; canFocus = true;
		cx = (width - cw) / 2; cy = (height - ch) / 2;
	}
	int th () const { return uk_fh () + 12; }		// the title strip's height
	// a control on the card: (x, y) from the card's corner
	Widget *place (Widget *w, int x, int y) { w->left = cx + x; w->top = cy + y; addChild (w); return w; }
	bool shown () const { return parent != 0; }
	void show (Widget *root, Widget *focus)
	{
		if (parent) bringToFront ();
		else { resizeTo (root->width, root->height); root->addChild (this); }
		m_snapOk = false;
		invalidate (true);
		if (focus) focus->setFocus (); else setFocus ();
	}
	void hide ()
	{
		Widget *p = parent;
		if (!p) return;
		p->removeChild (this);
		clearFocusTree ();
		for (Widget *n = p->lastChild; n; n = n->prevSib)		// the focus back: to the card under it, if any
			if (n->modal) { n->setFocus (); return; }
		onHidden ();
	}
	virtual void onHidden () {}			// no card left: the window gives the focus back
	virtual void drawCard () {}			// what the card shows besides its controls
	// the card follows the window's size (centred); the controls with it; what is under it taken again
	void layout () override
	{
		int nx = (width - cw) / 2, ny = (height - ch) / 2;
		for (Widget *c = firstChild; c; c = c->nextSib) { c->left += nx - cx; c->top += ny - cy; }
		cx = nx; cy = ny;
		m_snapOk = false;
		lytW = width; lytH = height;
	}
	void onDraw () override
	{
		Canvas &pc = parent->canvas;
		if (!m_snapOk || m_snapW != width || m_snapH != height)
		{
			if (m_snapW != width || m_snapH != height) { delete [] m_snap; m_snap = new unsigned[(size_t) width * height]; m_snapW = width; m_snapH = height; }
			for (int y = 0; y < height; y++)
				for (int x = 0; x < width; x++)
					m_snap[y * width + x] = y < pc.h && x < pc.w ? uk_mix (pc.px[y * pc.stride + x], 0x000000, 96) : 0;
			m_snapOk = true;
		}
		for (int y = 0; y < height; y++) memcpy (&canvas.px[y * canvas.stride], &m_snap[y * width], sizeof (unsigned) * width);
		uk_rbox (canvas, cx, cy, cw, ch, 9, C_BG, C_BG);
		uk_title_strip (canvas, cx + 1, cy + 1, cw - 2, th (), title, 8);
		uk_rline (canvas, cx, cy, cw, ch, 9, UK_OUTLINE == 2 ? 0 : uk_tone (C_FRAME_ACTIVE, 44), 255);
		drawCard ();
	}
private:
	unsigned *m_snap; int m_snapW, m_snapH; bool m_snapOk;
};

#endif
