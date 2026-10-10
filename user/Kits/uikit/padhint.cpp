//
// uikit/padhint.cpp -- a handheld's buttons as glyphs, lines of hints (uikit/padhint.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "uikit/padhint.h"
#include "uikit/paint.h"
#include "uikit/theme.h"
#include "uikit/text.h"

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

} // namespace uikit
