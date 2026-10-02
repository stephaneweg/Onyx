// skiatest -- the smoke test of Onyx's Skia port (tools/ports/skia; docs/03 §5.5): the scene of scene.h
// (paths, gradients, a dash, a drop shadow and a blur, PNG / JPEG / WebP encoded and decoded by Skia, text
// shaped by HarfBuzz with ICU's Unicode functions over the card's fonts through Skia's directory font
// manager) rendered by the CPU raster back end into an RGBA buffer, checked, and written as a PNG.
// One line per check (PASS / FAIL), a summary; the exit status is the number of failures.
//
//   skiatest [out.png [font directory [width height]]]     default RAM:/skiatest.png SD:/res/fonts/ 800 600
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is hereby granted, free
// of charge, to any person obtaining a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice
// and this permission notice shall be included in all copies or substantial portions of the Software. THE
// SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
#include "scene.h"
#include "include/core/SkStream.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int g_pass, g_fail;

static void check (bool ok, const char *what, const char *fmt = nullptr, ...)
{
	if (ok)
		g_pass++;
	else
		g_fail++;
	printf ("%s  %s", ok ? "PASS" : "FAIL", what);
	if (fmt) {
		va_list ap;
		va_start (ap, fmt);
		printf ("  [");
		vprintf (fmt, ap);
		printf ("]");
		va_end (ap);
	}
	printf ("\n");
}

static double now_ms ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static bool near (SkColor c, SkColor want, int tol)
{
	return abs ((int) SkColorGetR (c) - (int) SkColorGetR (want)) <= tol && abs ((int) SkColorGetG (c) - (int) SkColorGetG (want)) <= tol &&
	       abs ((int) SkColorGetB (c) - (int) SkColorGetB (want)) <= tol;
}

int main (int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "RAM:/skiatest.png";
	const char *fonts = argc > 2 ? argv[2] : "SD:/res/fonts/";
	int W = argc > 4 ? atoi (argv[3]) : 800, H = argc > 4 ? atoi (argv[4]) : 600;
	printf ("skiatest: Skia m%d (CPU raster), HarfBuzz %s, %dx%d, fonts %s -> %s\n", SK_MILESTONE, hb_version_string (), W, H, fonts, out);

	double t0 = now_ms ();
	sk_sp<SkFontMgr> mgr = SkFontMgr_New_Onyx (fonts);
	double t1 = now_ms ();
	int nf = mgr ? mgr->countFamilies () : 0;
	std::string fam;
	for (int i = 0; i < nf; i++) {
		SkString n;
		mgr->getFamilyName (i, &n);
		fam += (i ? ", " : "") + std::string (n.c_str ());
	}
	printf ("      font families: %s\n", fam.c_str ());
	check (nf >= 4, "font manager (SkFontMgr_New_Onyx over the directory): the card's families", "%d in %.0f ms", nf, t1 - t0);

	// Onyx's font manager: CSS aliases, case, fallback per character (what WebKit asks fontconfig for)
	auto family_of = [] (const sk_sp<SkTypeface> &tf) {
		SkString n;
		if (tf)
			tf->getFamilyName (&n);
		return std::string (tf ? n.c_str () : "(none)");
	};
	if (mgr) {
		std::string a = family_of (mgr->matchFamilyStyle ("sans-serif", SkFontStyle ()));
		std::string b = family_of (mgr->matchFamilyStyle ("ARIAL", SkFontStyle::Bold ()));
		std::string c = family_of (mgr->matchFamilyStyle ("monospace", SkFontStyle ()));
		std::string d = family_of (mgr->matchFamilyStyle ("Comic Sans MS", SkFontStyle ()));
		check (a == "DejaVu Sans" && b == "Liberation Sans" && c == "DejaVu Sans Mono" && d == "(none)",
		       "fonts: CSS generic families and web names (case-insensitive), an unknown family not matched",
		       "sans-serif=%s, ARIAL=%s, monospace=%s, Comic Sans MS=%s", a.c_str (), b.c_str (), c.c_str (), d.c_str ());
		std::string e = family_of (mgr->matchFamilyStyleCharacter ("Liberation Sans", SkFontStyle (), nullptr, 0, 0x0645));
		std::string f = family_of (mgr->matchFamilyStyleCharacter ("Liberation Serif", SkFontStyle (), nullptr, 0, 0x0416));
		std::string g = family_of (mgr->matchFamilyStyleCharacter ("Liberation Sans", SkFontStyle (), nullptr, 0, 0x3042));
		check (e == "DejaVu Sans" && f == "Liberation Serif" && g == "(none)",
		       "fonts: fallback for a character (matchFamilyStyleCharacter)",
		       "Arabic meem=%s, Cyrillic zhe=%s, hiragana a=%s", e.c_str (), f.c_str (), g.c_str ());
	}

	sk_sp<SkSurface> surface = SkSurfaces::Raster (SkImageInfo::MakeN32Premul (W, H));
	check (surface != nullptr, "raster surface (N32 premultiplied)", "%dx%d", W, H);
	if (!surface)
		return 1;
	t0 = now_ms ();
	scene::Result r = scene::draw (surface->getCanvas (), W, H, mgr);
	t1 = now_ms ();
	printf ("      scene drawn in %.0f ms\n", t1 - t0);
	check (r.haveSans && r.haveBold, "typefaces: DejaVu Sans regular and bold matched by family and style");

	// text: HarfBuzz runs
	check (r.latin.glyphs.size () > 0 && r.latin.glyphs.size () < r.latin.chars, "HarfBuzz over SkTypeface tables: Latin ligatures (ffi, fl, ff)",
	       "%u chars -> %zu glyphs", r.latin.chars, r.latin.glyphs.size ());
	check (r.arabic.dir == HB_DIRECTION_RTL && r.arabic.script == HB_SCRIPT_ARABIC && r.arabic.glyphs.size () > 0,
	       "HarfBuzz + hb-icu: Arabic, right to left", "%zu glyphs, %.1f px", r.arabic.glyphs.size (), r.arabic.width);

	// codecs
	for (auto &d : r.images) {
		double tol = d.name[0] == 'P' ? 0.0 : 6.0;
		check (d.image && d.meanDiff >= 0 && d.meanDiff <= tol, (std::string ("codec: ") + d.name + " encoded and decoded by Skia").c_str (),
		       "%zu bytes, mean difference %.2f", d.bytes, d.meanDiff);
	}

	// the pixels
	SkBitmap bm;
	bm.allocPixels (SkImageInfo::MakeN32Premul (W, H));
	surface->readPixels (bm.pixmap (), 0, 0);
	float s = W / 800.f;
	auto at = [&] (float x, float y) { return bm.getColor ((int) (x * s), (int) (y * s)); };
	check (near (at (2, 2), 0xff1d3557, 6), "pixels: the background gradient's first colour in the corner", "%08X", at (2, 2));
	check (near (at (150, 180), 0xffffd166, 12), "pixels: the star's radial gradient at its centre", "%08X", at (150, 180));
	check (near (at (660, 170), 0xff1d3557, 2), "pixels: the colour wheel's hole", "%08X", at (660, 170));
	check (SkColorGetR (at (725, 170)) > 200 && SkColorGetB (at (725, 170)) < 60, "pixels: the sweep gradient (red at 0 degrees)", "%08X", at (725, 170));
	int ink = 0;
	for (int y = (int) (322 * s); y < (int) (360 * s); y++)
		for (int x = (int) (60 * s); x < (int) (400 * s); x++)
			if (SkColorGetR (bm.getColor (x, y)) < 120)
				ink++;
	check (ink > 400, "pixels: the Latin text line is drawn (dark glyph pixels in the card)", "%d", ink);
	bool drawn = true;
	for (int k = 0; k < 3; k++)
		drawn = drawn && !near (at (60 + k * 130 + 30, 420 + 70), 0xfff1faee, 16);
	check (drawn, "pixels: the three decoded images are drawn on the card");

	// the PNG: written, read back, identical
	t0 = now_ms ();
	SkPixmap pm;
	bm.peekPixels (&pm);
	bool wrote = false;
	{
		SkFILEWStream f (out);
		wrote = f.isValid () && SkPngEncoder::Encode (&f, pm, {});
	}
	t1 = now_ms ();
	check (wrote, "PNG written (SkPngEncoder, SkFILEWStream)", "%s in %.0f ms", out, t1 - t0);
	sk_sp<SkData> back = SkData::MakeFromFileName (out);
	std::unique_ptr<SkCodec> codec = back ? SkPngDecoder::Decode (back, nullptr) : nullptr;
	double diff = -1;
	if (codec) {
		SkBitmap rb;
		rb.allocPixels (codec->getInfo ().makeColorType (kN32_SkColorType).makeAlphaType (kPremul_SkAlphaType));
		if (codec->getPixels (rb.pixmap ()) == SkCodec::kSuccess)
			diff = scene::mean_diff (bm, rb.pixmap ());
	}
	check (diff == 0, "PNG read back (SkData::MakeFromFileName, SkPngDecoder): identical", "%zu bytes, difference %.2f", back ? back->size () : 0, diff);

	unsigned long h = 5381;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			h = h * 33 + bm.getColor (x, y);
	printf ("skiatest: image hash %016lx\n", h);
	printf ("skiatest: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail;
}
