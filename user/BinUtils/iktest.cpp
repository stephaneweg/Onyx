//
// iktest -- ImageKit's self-test (SD:/lib/imagekit.so, imagekit/imagekit.h): a picture made, written
// in every format and read back, the resize in alpha, the turns, the crop, the adjustments, a real
// picture of the card when one is given. What it writes is under SD:/tmp/iktest, removed at the end.
//   usage: iktest [picture]     -> "iktest: N passed, 0 failed", status 0
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
#include "../onyxpp.hpp"		// (operator new / delete: the library's binding)
#include "../Kits/imagekit/imagekit.h"

static int g_pass, g_fail;
static void check (const char *what, bool ok)
{
	ax_puts (ok ? "PASS " : "FAIL "); ax_putln (what);
	if (ok) g_pass++; else g_fail++;
}
static void num (const char *label, int v) { char b[16]; ax_itoa (v, b); ax_puts (label); ax_puts (b); }

#define W 96
#define H 64
#define D "SD:/tmp/iktest"

// The test picture: four colours in quarters, a transparent hole in the middle.
static unsigned pixel (int x, int y)
{
	if (x >= 40 && x < 56 && y >= 24 && y < 40) return 0x00000000u;
	return 0xFF000000u | (x < W / 2 ? (y < H / 2 ? 0xE02020u : 0x2020E0u) : (y < H / 2 ? 0x20C020u : 0xF0F0F0u));
}
static int cdiff (unsigned a, unsigned b)
{
	int d = 0;
	for (int s = 0; s < 32; s += 8) { int x = (int) (a >> s & 255) - (int) (b >> s & 255); if (x < 0) x = -x; if (x > d) d = x; }
	return d;
}

int main (void)
{
	static char arg[300]; kapi_get_args (arg, sizeof arg);
	char *real = arg; while (*real == ' ') real++;

	ik_image *im = ik_image_new (W, H);
	check ("a picture made", im != 0 && ik_width (im) == W && ik_height (im) == H && ik_format (im) == IK_ARGB8);
	if (im == 0) return 1;
	unsigned *px = ik_pixels (im);
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) px[y * W + x] = pixel (x, y);
	check ("its transparency seen", ik_has_alpha (im) == 1);

	// written and read back
	kapi_mkdir ("SD:/tmp"); kapi_mkdir (D);
	void *b = 0; unsigned n = 0;
	bool ok = ik_encode (im, "png", 0, &b, &n) == 0 && n > 60;
	num ("  (the PNG: ", (int) n); ax_putln (" bytes)");
	ik_image *r = ok ? ik_load_mem (b, n, 0) : 0;
	bool same = r != 0 && ik_width (r) == W && ik_height (r) == H;
	for (int i = 0; same && i < W * H; i++) same = ik_pixels (r)[i] == px[i];
	check ("PNG: written, read back the same, the alpha kept", same && n < W * H);
	ik_image_free (r); ik_free (b); b = 0;
	check ("PNG saved to a file by its name", ik_save (im, D "/t.PNG", 0) == 0);
	r = ik_load (D "/t.PNG", IK_ORIENT);
	check ("... and loaded from it", r != 0 && ik_pixels (r)[10 * W + 10] == pixel (10, 10) && ax_streq (ik_load_format (), "PNG"));
	ik_image_free (r);
	struct ik_info inf;
	check ("the file probed", ik_probe (D "/t.PNG", &inf) == 1 && inf.orientation == 1);
	static const char *const fmts[3] = { "jpg", "bmp", "gif" };
	static const char *const files[3] = { D "/t.jpg", D "/t.bmp", D "/t.gif" };
	for (int f = 0; f < 3; f++)
	{
		ok = ik_save (im, files[f], 92) == 0;
		r = ok ? ik_load (files[f], 0) : 0;
		// (JPEG and BMP have no alpha: the hole is white, JPEG near and not equal; GIF keeps the hole clear)
		bool good = r != 0 && ik_width (r) == W && ik_height (r) == H && cdiff (ik_pixels (r)[10 * W + 10], pixel (10, 10)) <= (f == 0 ? 40 : 8);
		unsigned hole = r != 0 ? ik_pixels (r)[32 * W + 48] : 0;
		good = good && (f == 2 ? (hole >> 24) == 0 : cdiff (hole | 0xFF000000u, 0xFFFFFFFFu) <= (f == 0 ? 40 : 8));
		check (f == 0 ? "JPEG: written and read back" : f == 1 ? "BMP: written and read back" : "GIF: written and read back", good);
		ik_image_free (r);
		(void) fmts;
	}
	ik_frames *fr = ik_frames_load (D "/t.gif");
	check ("the GIF as frames", fr != 0 && ik_frames_count (fr) == 1 && ik_frames_width (fr) == W && ik_frames_pixels (fr, 0) != 0);
	ik_frames_free (fr);
	check ("names told by their extension", ik_is_image_name ("a.JPG") == 1 && ik_is_image_name ("a.txt") == 0);
	check ("what is not a picture is refused", ik_load_mem ("not a picture at all, really", 28, 0) == 0);

	// transforms
	r = ik_resize (im, W / 2, H / 2);
	check ("made smaller: the colours kept, the hole still clear",
	       r != 0 && cdiff (ik_pixels (r)[5 * (W / 2) + 5], pixel (10, 10)) <= 2 && (ik_pixels (r)[16 * (W / 2) + 24] >> 24) == 0);
	ik_image_free (r);
	// a transparent pixel's colour must not bleed: red beside transparent GREEN averages to red, half see-through
	unsigned two[2] = { 0xFFFF0000u, 0x0000FF00u }, one = 0;
	ik_scale (two, 2, 1, 2, 0, 0, 2, 1, &one, 1, 1, 1);
	check ("the resize is right in alpha", (one >> 24) >= 126 && (one >> 24) <= 129 && (one >> 16 & 255) == 255 && (one >> 8 & 255) == 0);
	r = ik_resize (im, W * 2, H * 2);
	check ("made larger", r != 0 && ik_width (r) == W * 2 && cdiff (ik_pixels (r)[20 * W * 2 + 20], pixel (10, 10)) <= 2);
	ik_image_free (r);
	r = ik_fit (im, 30, 30, 0);
	check ("fit in a box, the proportions kept", r != 0 && ik_width (r) == 30 && ik_height (r) == 20);
	ik_image_free (r);
	r = ik_cover (im, 32, 32);
	check ("a square thumbnail", r != 0 && ik_width (r) == 32 && ik_height (r) == 32);
	ik_image_free (r);
	r = ik_image_copy (im);
	ok = r != 0 && ik_rotate (r, 1) == 0 && ik_width (r) == H && ik_height (r) == W;
	// a quarter turn clockwise: the top left corner goes to the top right
	check ("a quarter turn", ok && ik_pixels (r)[2 * H + (H - 3)] == pixel (2, 2));
	ok = ok && ik_rotate (r, 3) == 0;
	bool back = ok && ik_width (r) == W;
	for (int i = 0; back && i < W * H; i++) back = ik_pixels (r)[i] == px[i];
	check ("... and three more: as it was", back);
	check ("a flip left-right", r != 0 && ik_flip (r, 0) == 0 && ik_pixels (r)[2 * W + 2] == pixel (W - 3, 2));
	check ("a crop", r != 0 && ik_crop (r, 10, 10, 20, 12) == 0 && ik_width (r) == 20 && ik_height (r) == 12);
	ik_image_free (r);

	// adjustments
	r = ik_image_copy (im);
	struct ik_adjust a;
	for (unsigned i = 0; i < sizeof a; i++) ((char *) &a)[i] = 0;
	a.filter = IK_FILTER_BW;
	ik_adjust_apply (r, &a);
	unsigned g = r != 0 ? ik_pixels (r)[10 * W + 10] : 1;
	check ("black and white: grey, the alpha kept", r != 0 && (g >> 16 & 255) == (g >> 8 & 255) && (g >> 8 & 255) == (g & 255)
	       && (g >> 24) == 255 && (ik_pixels (r)[32 * W + 48] >> 24) == 0);
	ik_image_free (r);
	r = ik_image_copy (im);
	a.filter = IK_FILTER_NONE; a.exposure = 60;
	ik_adjust_apply (r, &a);
	check ("more exposure: lighter", r != 0 && (ik_pixels (r)[40 * W + 10] & 255) > (px[40 * W + 10] & 255));
	ik_adjust_auto (im, &a);
	check ("enhance proposes values", a.exposure >= -100 && a.exposure <= 100 && a.sharpness >= 0);
	check ("the filters have names", ax_streq (ik_filter_name (IK_FILTER_VIVID), "Vivid"));
	ik_image_free (r);
	ik_flatten (im, 0x0000FF);
	check ("laid on a colour", ik_has_alpha (im) == 0 && px[32 * W + 48] == 0xFF0000FFu);
	ik_image_free (im);

	// a real picture of the card
	if (*real)
	{
		ok = ik_probe (real, &inf) == 1;
		ax_puts ("  "); ax_puts (real); num (": ", inf.w); num (" x ", inf.h); num (", orientation ", inf.orientation);
		ax_puts (", "); ax_puts (inf.camera[0] ? inf.camera : "no camera named"); ax_putln (inf.has_preview ? ", a preview inside" : "");
		r = ik_load (real, IK_ORIENT);
		check ("the picture given is read", ok && r != 0 && ik_width (r) > 0);
		ik_image *t = r != 0 ? ik_cover (r, 256, 256) : 0;
		check ("... and a thumbnail of it written", t != 0 && ik_save (t, D "/thumb.jpg", 85) == 0);
		ik_image_free (t); ik_image_free (r);
	}

	kapi_remove (D "/t.PNG"); kapi_remove (D "/t.jpg"); kapi_remove (D "/t.bmp"); kapi_remove (D "/t.gif"); kapi_remove (D "/thumb.jpg");
	kapi_remove (D);
	char n1[12], n2[12];
	ax_itoa (g_pass, n1); ax_itoa (g_fail, n2);
	ax_puts ("iktest: "); ax_puts (n1); ax_puts (" passed, "); ax_puts (n2); ax_putln (" failed");
	return g_fail ? 1 : 0;
}
