//
// wrap_test.cpp -- UIKit's uk_text_wrap and uk_text_over (user/Kits/uikit/text.cpp; AutoDev round 1, step 1)
// and its two new tool icons (WKT_TRASH, WKT_PIN), on the PC: UIKit's objects built for the host, the desktop
// simulator's fakekapi.o. The bitmap fonts' fixed cell, then a test face of known widths (a narrow 'i', a wide
// UTF-8 character) for the breaks; an alpha canvas for the blending. Run by tools/tests/run_notes_test.sh.
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
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include <stdio.h>
#include <string.h>

using namespace uikit;

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { fprintf (stderr, "wrap: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

// The lines a wrap gave, as "a|b|c" (+ "+" when more is left), into a static buffer. (No <string>: UIKit's
// headers bring the programs' own operator new, onyxpp.hpp.)
static const char *wrapped (const char *s, int w, int maxLines, int style = 0)
{
	static char r[1024];
	int st[64], ln[64]; bool more = true;
	int k = uk_text_wrap (s, (int) strlen (s), w, maxLines, st, ln, &more, style);
	int o = 0;
	for (int i = 0; i < k && o < 900; i++) { if (i) r[o++] = '|'; memcpy (r + o, s + st[i], (size_t) ln[i]); o += ln[i]; }
	if (more) r[o++] = '+';
	r[o] = 0;
	return r;
}
#define WRAP(s, w, m, want) do { const char *got = wrapped (s, w, m); g_checks++; \
	if (strcmp (got, want)) { fprintf (stderr, "wrap: FAIL %s:%d: wrap (\"%s\", %d, %d) = \"%s\", not \"%s\"\n", __FILE__, __LINE__, s, w, m, got, want); g_fail++; } } while (0)

// A face of known widths: 'i' 3 px, an ASCII character 7, a UTF-8 character (any length) 9; its glyphs a full
// box, but '.' half covered (grey) -- for uk_text_over's coverage.
struct TestFace : TextFace
{
	int height () override { return 12; }
	int ascent () override { return 10; }
	static int cw (const char *s, int *k) { unsigned char c = (unsigned char) *s; *k = uk_u8_len (s, 4); return c >= 0x80 ? 9 : c == 'i' ? 3 : 7; }
	int width (const char *s, int) override { int w = 0, k; while (*s) { w += cw (s, &k); s += k; } return w; }
	void draw (Canvas &cv, int x, int y, const char *s, unsigned color, int) override
	{
		int k;
		while (*s)
		{
			int w = cw (s, &k);
			unsigned c = *s == '.' ? (color & 0x00FEFEFE) >> 1 : color;
			for (int yy = 2; yy < 10; yy++) for (int xx = 0; xx < w - 1; xx++)
				if (x + xx >= 0 && x + xx < cv.w && y + yy >= 0 && y + yy < cv.h) cv.px[(y + yy) * cv.stride + x + xx] = c;
			x += w; s += k;
		}
	}
};

static void bitmap_wraps ()
{
	int fw = uk_tw ("a");
	CHECK (fw > 0 && uk_tw ("abcd") == 4 * fw);
	WRAP ("hello world", 11 * fw, 4, "hello world");
	WRAP ("hello world", 10 * fw, 4, "hello|world");
	WRAP ("hello world", 5 * fw, 4, "hello|world");
	WRAP ("hello   world  again", 8 * fw, 4, "hello|world|again");	// (the spaces at a break dropped)
	WRAP ("one two three four", 9 * fw, 4, "one two|three|four");
	WRAP ("a\n\nb", 10 * fw, 4, "a||b");				// (an empty line kept)
	WRAP ("a\nb\n", 10 * fw, 4, "a|b");				// (no line after the last '\n')
	WRAP ("abcdefghij", 4 * fw, 4, "abcd|efgh|ij");			// (a word wider than the line: cut)
	WRAP ("abcdefghijklmnop", 4 * fw, 2, "abcd|efgh+");		// (more left)
	WRAP ("one\ntwo\nthree", 10 * fw, 2, "one|two+");
	WRAP ("one\ntwo\n\n  \n", 10 * fw, 2, "one|two");		// (only blanks left: not "more")
	WRAP ("x", 0, 3, "x");						// (a line too narrow: a character a line)
	WRAP ("  indented line", 20 * fw, 2, "  indented line");	// (an indent kept)
	WRAP ("trailing   \nnext", 20 * fw, 3, "trailing|next");
	WRAP ("", 10 * fw, 3, "");
	// a 300-character word, 6 lines of 40: cut at the width, more left
	static char w300[301];
	memset (w300, 'w', 300);
	int st[8], ln[8]; bool more = false;
	int k = uk_text_wrap (w300, 300, 40 * fw, 6, st, ln, &more);
	CHECK (k == 6 && more);
	for (int i = 0; i < k; i++) CHECK (ln[i] == 40 && st[i] == 40 * i);
	// only n bytes looked at; no arrays: nothing but "more"
	CHECK (uk_text_wrap ("abc def", 3, 100, 4, st, ln, &more) == 1 && ln[0] == 3 && !more);
	CHECK (uk_text_wrap ("abc", 3, 100, 0, st, ln, &more) == 0 && more);
	CHECK (uk_text_wrap ("abc", 3, 100, 4, st, ln) == 1);		// (more not asked)
}

static void face_wraps (TestFace &f)
{
	UkFaceScope sc (&f);
	CHECK (uk_tw ("iii") == 9 && uk_tw ("L\xC3\xA9" "a") == 23);
	WRAP ("iiii iiii", 12, 3, "iiii|iiii");				// 12 px: four i's
	WRAP ("iiii iiii", 31, 3, "iiii iiii");				// 4 * 3 + 7 + 4 * 3 = 31: one line
	WRAP ("iiii iiii", 30, 3, "iiii|iiii");
	WRAP ("ab cd", 21, 3, "ab|cd");					// "ab c" = 28 > 21: at the space
	// a UTF-8 word cut at a character, never inside one: each "é" 9 px, 20 px a line -> two a line
	WRAP ("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 20, 4, "\xC3\xA9\xC3\xA9|\xC3\xA9\xC3\xA9|\xC3\xA9");
	// a character wider than the line: one a line all the same
	WRAP ("\xE2\x82\xAC\xE2\x82\xAC", 5, 4, "\xE2\x82\xAC|\xE2\x82\xAC");
}

static unsigned px (Canvas &c, int x, int y) { return c.px[y * c.stride + x]; }

static void text_over (TestFace &f)
{
	Canvas cv; cv.resize (60, 30);
	// with a face, on a see-through canvas: a glyph's pixel the opaque ink, beside it still see-through
	cv.clear (0xFF000000);
	uk_paint_alpha (true);
	{
		UkFaceScope sc (&f);
		uk_text_over (cv, 4, 4, "A.", 0x00FAFCFF, 0, 0, 0);
	}
	CHECK (px (cv, 4 + 1, 4 + 5) == 0x00FAFCFF);			// inside 'A'
	CHECK (px (cv, 4 + 6, 4 + 5) == 0xFF000000);			// its box's gap column
	CHECK (px (cv, 2, 2) == 0xFF000000);
	unsigned half = px (cv, 4 + 7 + 1, 4 + 5);			// '.': half covered -> half see-through, the ink's colour
	CHECK ((half >> 24) > 0x60 && (half >> 24) < 0xA0 && (half & 0x00FFFFFF) == 0x00FAFCFF);
	// a soft shadow: the pixel below-right of the glyph's last row darkened, partly see-through
	cv.clear (0xFF000000);
	{
		UkFaceScope sc (&f);
		uk_text_over (cv, 4, 4, "A", 0x00FAFCFF, 0, 1, 0);
	}
	unsigned sh = px (cv, 4 + 1 + 1, 4 + 9 + 1);
	CHECK ((sh >> 24) < 0xFF && (sh >> 24) > 0 && (sh & 0x00FFFFFF) == 0);
	CHECK (px (cv, 4 + 1, 4 + 5) == 0x00FAFCFF);			// (the ink over its own shadow)
	// engraved: a light edge of `back` under the glyph
	cv.clear (0xFF000000);
	{
		UkFaceScope sc (&f);
		uk_text_over (cv, 4, 4, "A", 0x00182232, 0, 2, 0x00C0C0C0);
	}
	unsigned eg = px (cv, 4 + 1, 4 + 10);
	CHECK ((eg >> 24) < 0xFF && (eg & 0x00FFFFFF) == (uk_tone (0x00C0C0C0, 205) & 0x00FFFFFF));
	uk_paint_alpha (false);
	// with the bitmap fonts: the same pixels as uk_text draws, in the ink, on an opaque canvas
	Canvas a, b; a.resize (60, 30); b.resize (60, 30);
	a.clear (0x00102030); b.clear (0x00102030);
	uk_text (a, 3, 3, "Hi", 0x00FFFFFF);
	uk_text_over (b, 3, 3, "Hi", 0x00FFFFFF);
	int same = 0, lit = 0;
	for (int y = 0; y < 30; y++) for (int x = 0; x < 60; x++) { lit += px (a, x, y) == 0x00FFFFFF; same += px (a, x, y) == px (b, x, y); }
	CHECK (lit > 10 && same == 60 * 30);
	uk_text_over (b, 3, 3, 0, 0); uk_text_over (b, 3, 3, "", 0);	// (nothing: no crash)
}

static void icons ()
{
	CHECK (WKT_TRASH == WKT_GEAR + 1 && WKT_PIN == WKT_GEAR + 2 && WKT_COUNT == WKT_GEAR + 3);
	for (int kind = WKT_TRASH; kind <= WKT_PIN; kind++)
		for (int size = 12; size <= 30; size += 6)
		{
			Canvas c; c.resize (size + 4, size + 4); c.clear (0x00FFFFFF);
			uk_tool_glyph (c, kind, 2, 2, size, 0x00000000);
			int ink = 0, out = 0;
			for (int y = 0; y < c.h; y++) for (int x = 0; x < c.w; x++)
			{
				bool on = px (c, x, y) != 0x00FFFFFF;
				ink += on;
				if (on && (x < 1 || y < 1 || x > size + 2 || y > size + 2)) out++;
			}
			CHECK (ink > size && ink < size * size * 2 / 3);	// (drawn, not a filled box)
			CHECK (out == 0);					// (within its box)
		}
}

int main ()
{
	TestFace f;
	bitmap_wraps ();
	face_wraps (f);
	text_over (f);
	icons ();
	if (g_fail) { fprintf (stderr, "wrap: %d of %d checks FAILED\n", g_fail, g_checks); return 1; }
	fprintf (stderr, "wrap: %d checks passed\n", g_checks);
	return 0;
}
