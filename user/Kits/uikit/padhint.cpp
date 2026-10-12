//
// uikit/padhint.cpp -- a handheld's buttons as glyphs, lines of hints (uikit/padhint.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "uikit/padhint.h"
#include "uikit/paint.h"
#include "uikit/theme.h"
#include "uikit/text.h"
#include "uikit/root.h"
#include "uikit/lang.h"
#include "uikit/internal/adapt_int.h"

namespace uikit {

static unsigned plate ()	{ return UK_FLAT ? (unsigned) UK_N_BG3 : uk_mix (C_BG, C_TEXT, 60); }
static unsigned legend ()	{ return UK_FLAT ? (unsigned) UK_N_TEXT1 : C_TEXT; }

int uk_pad_glyph (Canvas &cv, int x, int y, int h, int kind, bool measure)
{
	if (h < 8) h = 8;
	switch (kind)
	{
	case 'A': case 'B': case 'X': case 'Y':
		if (!measure)
		{
			char s[2] = { (char) kind, 0 };
			unsigned f = kind == 'A' ? C_ACCENT : plate ();
			uk_rbox (cv, x, y, h, h, h / 2, f, f);
			uk_text_c (cv, x, y, h, h, s, kind == 'A' ? 0x00FFFFFFu : legend (), 2);
		}
		return h;
	case 'L': case 'R': case 'S': case 's':
		{
			const char *s = kind == 'L' ? "L" : kind == 'R' ? "R" : kind == 'S' ? "Start" : "Select";
			int w = uk_tw (s, 2) + h * 7 / 10;
			if (w < h) w = h;
			if (!measure)
			{
				uk_rbox (cv, x, y, w, h, h * 3 / 10, plate (), plate ());
				uk_text_c (cv, x, y, w, h, s, legend (), 2);
			}
			return w;
		}
	case '+': case '-': case '|':
		if (!measure)
		{
			int a = h / 3, r = a / 4 < 1 ? 1 : a / 4;
			unsigned dim = plate (), lit = kind == '+' ? (UK_FLAT ? (unsigned) UK_N_TEXT2 : uk_mix (C_BG, C_TEXT, 150)) : legend ();
			uk_rbox (cv, x, y + a, h, h - 2 * a, r, kind == '|' ? dim : lit, kind == '|' ? dim : lit);
			uk_rbox (cv, x + a, y, h - 2 * a, h, r, kind == '-' ? dim : lit, kind == '-' ? dim : lit);
			if (kind == '-') uk_rbox (cv, x, y + a, h, h - 2 * a, r, lit, lit);
		}
		return h;
	}
	return 0;
}

int uk_pad_key (Canvas &cv, int x, int y, int h, const char *label, bool measure)
{
	int w = uk_tw (label) + h * 6 / 10;
	if (w < h) w = h;
	if (!measure)
	{
		uk_rline (cv, x, y, w, h, h / 4, UK_FLAT ? (unsigned) UK_N_TEXT3 : C_DIS, 255);
		uk_text_c (cv, x, y, w, h, label, UK_FLAT ? (unsigned) UK_N_TEXT2 : C_TEXT);
	}
	return w;
}

int uk_pad_hints (Canvas &cv, int x, int y, int h, const UkPadHint *hints, int n, unsigned ink, bool measure)
{
	int x0 = x;
	for (int i = 0; i < n; i++)
	{
		for (const char *g = hints[i].glyphs; g && *g; g++)
		{
			if (*g == '[')						// a key's cap
			{
				char k[24]; int m = 0;
				for (g++; *g && *g != ']' && m < 23; g++) k[m++] = *g;
				k[m] = 0;
				x += uk_pad_key (cv, x, y, h, k, measure) + 4;
				if (!*g) break;
				continue;
			}
			x += uk_pad_glyph (cv, x, y, h, *g, measure) + 4;
		}
		if (hints[i].word && hints[i].word[0])
		{
			if (!measure) uk_text_l (cv, x + 1, y, h, hints[i].word, ink);
			x += 1 + uk_tw (hints[i].word);
		}
		if (i + 1 < n) x += 14;
	}
	return x - x0;
}

// ---- the hint line (uikit/padhint.h): a widget of UIKit's own at the foot of the app's window ------------------------
namespace {
enum { HL_MAX = 8, HL_H = 30, HL_ROOTS = 8 };
struct HintBar;
struct { Root *root; HintBar *bar; } s_bars[HL_ROOTS];
struct HintBar : public Widget
{
	char	  glyphs[HL_MAX][24], words[HL_MAX][48], mode[24];
	UkPadHint h[HL_MAX];
	int	  n;
	HintBar () : Widget (0, 0, 16, HL_H), n (0) { mode[0] = 0; anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM; }
	~HintBar () { for (int i = 0; i < HL_ROOTS; i++) if (s_bars[i].bar == this) { s_bars[i].bar = 0; s_bars[i].root = 0; } }
	unsigned bgColor () override { return UK_FLAT ? 0x00000000u : C_MENUBAR; }
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, bgColor ());
		canvas.fillRect (0, 0, width, 1, UK_FLAT ? (unsigned) UK_N_LINE : uk_mix (C_MENUBAR, C_TEXT, 60));
		int x = 14, y = (height - 18) / 2 + 1;
		if (mode[0])
		{
			int w = uk_tw (mode, 2) + 16;
			uk_rbox (canvas, x, y, w, 18, 5, C_ACCENT, C_ACCENT);
			uk_text_c (canvas, x, y, w, 18, mode, 0x00FFFFFFu, 2);
			x += w + 14;
		}
		int k = n;					// (what does not fit is left out, from the end)
		while (k > 1 && x + uk_pad_hints (canvas, 0, 0, 18, h, k, 0, true) > width - 10) k--;
		if (k > 0) uk_pad_hints (canvas, x, y, 18, h, k, UK_FLAT ? (unsigned) UK_N_TEXT2 : C_TEXT);
	}
};
HintBar *bar_of (Root *r)
{
	for (int i = 0; i < HL_ROOTS; i++) if (s_bars[i].root == r) return s_bars[i].bar;
	return 0;
}
void copy (char *d, const char *s, int cap) { int k = 0; for (; s && s[k] && k < cap - 1; k++) d[k] = s[k]; d[k] = 0; }
bool same (const char *a, const char *b) { int k = 0; for (; a[k] && b && a[k] == b[k]; k++) ; return a[k] == (b ? b[k] : 0); }
}

int uk_hint_line_height (Root *root)
{
	HintBar *b = bar_of (root);
	return b != 0 && !b->hidden ? HL_H : 0;
}

int uk_hint_line (Root *root, const UkPadHint *hints, int n, const char *mode)
{
	if (root == 0 || !internal::ring_on ()) return 0;	// (the desktop: no line, nothing made)
	HintBar *b = bar_of (root);
	UkPadHint std3[3] = { { "[F6]", TR ("tools") }, { "[Alt]+", TR ("scroll") }, { "[CMD]", TR ("Menu") } };
	if (hints == 0 && n == UK_HINTS_STANDARD) { hints = std3; n = 3; }
	if (n <= 0 || hints == 0)
	{
		if (b != 0 && !b->hidden) { b->hidden = true; root->invalidate (true); }
		return 0;
	}
	if (b == 0)
	{
		int slot = -1;
		for (int i = 0; i < HL_ROOTS && slot < 0; i++) if (s_bars[i].root == 0) slot = i;
		if (slot < 0) return 0;
		b = new HintBar ();
		s_bars[slot].root = root; s_bars[slot].bar = b;
		root->addChild (b);
	}
	if (n > HL_MAX) n = HL_MAX;
	bool changed = b->hidden || n != b->n || !same (b->mode, mode ? mode : "");
	for (int i = 0; i < n && !changed; i++) changed = !same (b->glyphs[i], hints[i].glyphs) || !same (b->words[i], hints[i].word);
	if (changed)
	{
		b->n = n; copy (b->mode, mode, sizeof b->mode);
		for (int i = 0; i < n; i++)
		{
			copy (b->glyphs[i], hints[i].glyphs, sizeof b->glyphs[i]); copy (b->words[i], hints[i].word, sizeof b->words[i]);
			b->h[i].glyphs = b->glyphs[i]; b->h[i].word = b->words[i];
		}
		b->hidden = false;
		b->invalidate (true);
	}
	internal::hint_fit (root);
	return HL_H;
}

int internal::hint_room (Widget *parent)
{
	for (int i = 0; i < HL_ROOTS; i++)
		if (s_bars[i].root != 0 && (Widget *) s_bars[i].root == parent && s_bars[i].bar != 0 && !s_bars[i].bar->hidden) return HL_H;
	return 0;
}

// The line kept at the window's foot, on its whole width (a window resized: Root's turn, uikit/adapt.cpp).
void internal::hint_fit (Root *root)
{
	HintBar *b = bar_of (root);
	if (b == 0 || b->hidden) return;
	if (b->left == 0 && b->top == root->height - HL_H && b->width == root->width) return;
	b->left = 0; b->top = root->height - HL_H;
	if (b->width != root->width) b->resizeTo (root->width, HL_H);
	b->invalidate (true); root->invalidate (true);
}

} // namespace uikit
