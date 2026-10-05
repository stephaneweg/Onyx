//
// ikcore.cpp -- ImageKit (imagekit.h): pictures read (img/imgload.hpp: stb_image, simplewebp, PCX --
// compiled here), written (img/pngsave.hpp's JPEG, BMP and GIF; PNG through FileKit's zlib), what a
// file says (Photos' exif.h), the transforms (one resize, right in alpha) and the adjustments
// (Photos' imgops.h). Built into SD:/lib/imagekit.so (user/Makefile) with the FPU; its interface is
// integer only. It opens FileKit (SD:/lib/filekit.so) when a PNG is first written.
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "appkit/appkit.h"
#include "lib.h"
#include "imagekit.h"
#include "filekit/filekit.h"

// ---- FileKit, opened when first needed (its import stubs are linked in: lib/filekit_stubs.o) -------
extern "C" {
int onyx_lib_init (const TLibImports *imp);		// (librt.cpp)
const void *onyx_filekit_table;
}
static TLibImports s_imp;
extern "C" int ik_lib_init (const TLibImports *imp)
{
	int r = onyx_lib_init (imp);
	if (r < 0) return -1;
	if (r > 0) { s_imp = *imp; s_imp.size = sizeof s_imp; s_imp.ndata = 0; s_imp.data = 0; }
	return 0;
}
static bool need_filekit (void)
{
	if (onyx_filekit_table == 0)
	{
		int err = 0;
		const TLibHeader *t = (const TLibHeader *) kapi_lib_open ("filekit", 43, &err);
		if (t != 0 && t->init (&s_imp) >= 0) onyx_filekit_table = t;
	}
	return onyx_filekit_table != 0;
}

// ---- the decoders ------------------------------------------------------------------------------------
#define IMGLOAD_IMPLEMENTATION
#include "img/imgload.hpp"

// ---- the encoders: PNG's compression is zlib's (FileKit) ---------------------------------------------
static unsigned char *ik__png_deflate (const unsigned char *in, unsigned n, unsigned *outLen);
#define PNGSAVE_DEFLATE ik__png_deflate
#include "img/pngsave.hpp"
static unsigned char *ik__png_deflate (const unsigned char *in, unsigned n, unsigned *outLen)
{
	if (need_filekit ())
	{
		void *z = 0; unsigned zn = 0;
		if (fk_deflate (in, n, FK_ZLIB, 6, &z, &zn) == 0)
		{
			unsigned char *o = new unsigned char[zn ? zn : 1];
			memcpy (o, z, zn);
			fk_free (z);
			*outLen = zn;
			return o;
		}
	}
	return pngsave::deflate (in, n, true, outLen);		// (no FileKit: the small deflate of pngsave.hpp)
}

// ---- Photos' pixels and picture facts ----------------------------------------------------------------
#include "Apps/photos/imgops.h"
#include "Apps/photos/exif.h"

using namespace photos;

extern "C" void ik_free (void *p)		{ free (p); }

// ---- a picture in memory ---------------------------------------------------------------------------

struct ik_image { Pix p; int format; };

static ik_image *image_of (int w, int h)
{
	if (w < 1 || h < 1 || w > 32768 || h > 32768 || (long long) w * h > (256ll << 20)) return 0;
	ik_image *im = new ik_image;
	im->format = IK_ARGB8;
	im->p.alloc (w, h);
	if (im->p.px == 0) { delete im; return 0; }
	return im;
}
// The first frame of what the decoder gave, as a picture (the frames freed).
static ik_image *image_take (ImgFrames &f)
{
	if (f.n < 1 || f.px[0] == 0) { img_free (&f); return 0; }
	ik_image *im = new ik_image;
	im->format = IK_ARGB8;
	im->p.px = f.px[0]; im->p.w = f.w; im->p.h = f.h;	// (new unsigned[]: Pix frees it the same way)
	f.px[0] = 0;
	img_free (&f);
	return im;
}

extern "C" ik_image *ik_image_new (int w, int h)
{
	ik_image *im = image_of (w, h);
	if (im != 0) memset (im->p.px, 0, (size_t) w * h * 4);
	return im;
}
extern "C" ik_image *ik_image_from (const unsigned *px, int w, int h, int stride)
{
	if (px == 0 || stride < w) return 0;
	ik_image *im = image_of (w, h);
	if (im == 0) return 0;
	for (int y = 0; y < h; y++) memcpy (im->p.px + (size_t) y * w, px + (size_t) y * stride, (size_t) w * 4);
	return im;
}
extern "C" ik_image *ik_image_copy (const ik_image *s)		{ return s != 0 ? ik_image_from (s->p.px, s->p.w, s->p.h, s->p.w) : 0; }
extern "C" void ik_image_free (ik_image *im)			{ if (im != 0) { im->p.free_ (); delete im; } }
extern "C" int ik_width (const ik_image *im)			{ return im != 0 ? im->p.w : 0; }
extern "C" int ik_height (const ik_image *im)			{ return im != 0 ? im->p.h : 0; }
extern "C" int ik_format (const ik_image *im)			{ return im != 0 ? im->format : 0; }
extern "C" unsigned *ik_pixels (ik_image *im)			{ return im != 0 ? im->p.px : 0; }
extern "C" void ik_fill (ik_image *im, unsigned argb)
{
	if (im == 0) return;
	for (size_t i = 0, n = (size_t) im->p.w * im->p.h; i < n; i++) im->p.px[i] = argb;
}
extern "C" void ik_opaque (ik_image *im)			{ if (im != 0) opaque (im->p); }
extern "C" void ik_flatten (ik_image *im, unsigned rgb)		{ if (im != 0) { flatten (im->p, rgb); opaque (im->p); } }
extern "C" int ik_has_alpha (const ik_image *im)
{
	if (im == 0) return 0;
	for (size_t i = 0, n = (size_t) im->p.w * im->p.h; i < n; i++) if ((im->p.px[i] >> 24) != 255) return 1;
	return 0;
}

// ---- reading ---------------------------------------------------------------------------------------

static const char *s_format = "";

static const struct { const char *name, *exts; int rd, wr, alpha, anim; } s_formats[] = {
	{ "PNG",  "png",          1, 1, 1, 0 },
	{ "JPEG", "jpg jpeg jpe", 1, 1, 0, 0 },
	{ "GIF",  "gif",          1, 1, 1, 1 },
	{ "BMP",  "bmp",          1, 1, 0, 0 },
	{ "WebP", "webp",         1, 0, 1, 0 },
	{ "PCX",  "pcx",          1, 0, 0, 0 },
};
extern "C" int ik_formats (struct ik_format *out, int max)
{
	int n = (int) (sizeof s_formats / sizeof s_formats[0]);
	for (int i = 0; out != 0 && i < n && i < max; i++)
	{
		memset (&out[i], 0, sizeof out[i]);
		snprintf (out[i].name, sizeof out[i].name, "%s", s_formats[i].name);
		snprintf (out[i].extensions, sizeof out[i].extensions, "%s", s_formats[i].exts);
		out[i].can_read = s_formats[i].rd; out[i].can_write = s_formats[i].wr;
		out[i].alpha = s_formats[i].alpha; out[i].animated = s_formats[i].anim;
	}
	return n;
}
extern "C" int ik_is_image_name (const char *name)		{ return name != 0 && img_is_image_name (name) ? 1 : 0; }
extern "C" const char *ik_load_format (void)			{ return s_format; }

extern "C" int ik_probe (const char *path, struct ik_info *o)
{
	if (path == 0 || o == 0) return 0;
	memset (o, 0, sizeof *o); o->orientation = 1;
	PicInfo pi;
	if (!pic_info (path, pi)) return 0;
	o->w = pi.w; o->h = pi.h; o->orientation = pi.orient >= 1 && pi.orient <= 8 ? pi.orient : 1; o->taken = pi.taken;
	snprintf (o->camera, sizeof o->camera, "%s", pi.camera);
	snprintf (o->exposure, sizeof o->exposure, "%s", pi.expo);
	o->has_preview = pi.thumbOff != 0 && pi.thumbLen >= 100 ? 1 : 0;
	return 1;
}
extern "C" ik_image *ik_load (const char *path, int flags)
{
	if (path == 0) return 0;
	ImgFrames f; memset (&f, 0, sizeof f);
	if (!img_load (path, &f)) return 0;
	s_format = f.format != 0 ? f.format : "";
	ik_image *im = image_take (f);
	if (im != 0 && (flags & IK_ORIENT))
	{
		PicInfo pi;
		if (pic_info (path, pi) && pi.orient > 1) orient (im->p, pi.orient);
	}
	return im;
}
extern "C" ik_image *ik_load_mem (const void *data, unsigned n, int flags)
{
	(void) flags;						// (the orientation is a file's: ik_probe + ik_orient)
	if (data == 0 || n == 0) return 0;
	ImgFrames f; memset (&f, 0, sizeof f);
	if (!img_load_mem (data, n, &f)) return 0;
	s_format = f.format != 0 ? f.format : "";
	return image_take (f);
}
extern "C" ik_image *ik_load_preview (const char *path)
{
	if (path == 0) return 0;
	PicInfo pi;
	if (!pic_info (path, pi)) return 0;
	unsigned n = 0;
	unsigned char *b = exif_thumb (path, pi, &n);
	if (b == 0) return 0;
	ik_image *im = ik_load_mem (b, n, 0);
	free (b);
	if (im != 0 && pi.orient > 1) orient (im->p, pi.orient);
	return im;
}

struct ik_frames { ImgFrames f; };
static ik_frames *frames_of (ImgFrames &f)
{
	if (f.n < 1) { img_free (&f); return 0; }
	ik_frames *r = new ik_frames;
	r->f = f;
	s_format = f.format != 0 ? f.format : "";
	return r;
}
extern "C" ik_frames *ik_frames_load (const char *path)
{
	ImgFrames f; memset (&f, 0, sizeof f);
	return path != 0 && img_load (path, &f) ? frames_of (f) : 0;
}
extern "C" ik_frames *ik_frames_load_mem (const void *data, unsigned n)
{
	ImgFrames f; memset (&f, 0, sizeof f);
	return data != 0 && n != 0 && img_load_mem (data, n, &f) ? frames_of (f) : 0;
}
extern "C" int ik_frames_count (const ik_frames *f)		{ return f != 0 ? f->f.n : 0; }
extern "C" const unsigned *ik_frames_pixels (const ik_frames *f, int i)	{ return f != 0 && i >= 0 && i < f->f.n ? f->f.px[i] : 0; }
extern "C" int ik_frames_delay (const ik_frames *f, int i)	{ return f != 0 && i >= 0 && i < f->f.n ? f->f.delay[i] : 0; }
extern "C" int ik_frames_width (const ik_frames *f)		{ return f != 0 ? f->f.w : 0; }
extern "C" int ik_frames_height (const ik_frames *f)		{ return f != 0 ? f->f.h : 0; }
extern "C" int ik_frames_take (ik_frames *f, unsigned **px, int *delay, int max)
{
	if (f == 0 || px == 0) return 0;
	int n = 0;
	for (int i = 0; i < f->f.n && n < max; i++)
	{
		px[n] = f->f.px[i]; f->f.px[i] = 0;
		if (delay != 0) delay[n] = f->f.delay[i];
		n++;
	}
	return n;
}
extern "C" unsigned char *ik_inflate (const void *data, unsigned n, int zlib, unsigned *out_n)
{
	return img_inflate (data, n, zlib != 0, out_n);
}
extern "C" void ik_frames_free (ik_frames *f)			{ if (f != 0) { img_free (&f->f); delete f; } }

// ---- writing ---------------------------------------------------------------------------------------

static bool word_is (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char) (*a + 32) : *a;
		if (x != *b) return false;
		if (x == 0) return true;
	}
}
extern "C" int ik_encode_pixels (const unsigned *src, int w, int h, int stride, const char *format, int quality, int alpha,
				 void **out, unsigned *out_n)
{
	if (src == 0 || format == 0 || out == 0 || out_n == 0 || w < 1 || h < 1 || stride < w) return -1;
	*out = 0; *out_n = 0;
	unsigned *tight = 0;					// (the encoders take rows one after the other)
	const unsigned *px = src;
	if (stride != w)
	{
		tight = new unsigned[(size_t) w * h];
		for (int y = 0; y < h; y++) memcpy (tight + (size_t) y * w, src + (size_t) y * stride, (size_t) w * 4);
		px = tight;
	}
	if (alpha < 0)
	{
		alpha = 0;
		for (size_t i = 0, n = (size_t) w * h; i < n && !alpha; i++) if ((px[i] >> 24) != 255) alpha = 1;
	}
	unsigned n = 0; unsigned char *b = 0;
	if (word_is (format, "png")) b = pngsave::png_encode (px, w, h, alpha != 0, &n);
	else if (word_is (format, "jpg") || word_is (format, "jpeg") || word_is (format, "jpe"))
		b = pngsave::jpeg_encode (px, w, h, quality <= 0 ? 90 : quality, &n);
	else if (word_is (format, "bmp")) b = pngsave::bmp_encode (px, w, h, &n);
	else if (word_is (format, "gif")) b = pngsave::gif_encode (px, w, h, &n);
	delete[] tight;
	if (b == 0 || n == 0) { delete[] b; return -1; }
	void *o = malloc (n);					// (the caller's: ik_free)
	if (o != 0) memcpy (o, b, n);
	delete[] b;
	if (o == 0) return -1;
	*out = o; *out_n = n;
	return 0;
}
extern "C" int ik_encode (const ik_image *im, const char *format, int quality, void **out, unsigned *out_n)
{
	if (im == 0) return -1;
	return ik_encode_pixels (im->p.px, im->p.w, im->p.h, im->p.w, format, quality, -1, out, out_n);
}
extern "C" int ik_save (const ik_image *im, const char *path, int quality)
{
	if (im == 0 || path == 0) return -1;
	const char *dot = strrchr (path, '.'), *sl = strrchr (path, '/');
	if (dot == 0 || (sl != 0 && dot < sl)) return -1;
	void *b = 0; unsigned n = 0;
	if (ik_encode (im, dot + 1, quality, &b, &n) != 0) return -1;
	int r = kapi_save_file (path, b, n);
	free (b);
	return r < 0 ? -1 : 0;
}

// ---- transforms ------------------------------------------------------------------------------------

// Smaller: each pixel the average of ALL those it covers; larger: bilinear. The colours are weighed
// by the alpha (a transparent pixel's colour does not bleed into its neighbours).
// rgb: the top byte is not an alpha (0x00RRGGBB pixels, or opaque ones whatever it holds): the colours
// alone, the result's top byte 0.
static void scale_impl (const unsigned *src, int srcW, int srcH, int srcStride, int sx, int sy, int sw, int sh,
			unsigned *dst, int dstStride, int dw, int dh, bool rgb)
{
	if (src == 0 || dst == 0 || dw <= 0 || dh <= 0) return;
	if (sx < 0) { sw += sx; sx = 0; }
	if (sy < 0) { sh += sy; sy = 0; }
	if (sx + sw > srcW) sw = srcW - sx;
	if (sy + sh > srcH) sh = srcH - sy;
	if (sw <= 0 || sh <= 0) return;
	if (sw == dw && sh == dh)
	{
		for (int y = 0; y < dh; y++) memcpy (dst + (size_t) y * dstStride, src + (size_t) (sy + y) * srcStride + sx, (size_t) dw * 4);
		return;
	}
	if (sw >= dw && sh >= dh)				// the average of the box (rows first accumulated a column at a time)
	{
		for (int y = 0; y < dh; y++)
		{
			int y0 = sy + (int) ((long long) y * sh / dh), y1 = sy + (int) ((long long) (y + 1) * sh / dh);
			if (y1 <= y0) y1 = y0 + 1;
			int ys = 1 + (y1 - y0) / 8;			// (a big shrink: at most ~8 x 8 of the pixels covered are read)
			unsigned *o = dst + (size_t) y * dstStride;
			for (int x = 0; x < dw; x++)
			{
				int x0 = sx + (int) ((long long) x * sw / dw), x1 = sx + (int) ((long long) (x + 1) * sw / dw);
				if (x1 <= x0) x1 = x0 + 1;
				int xs = 1 + (x1 - x0) / 8;
				unsigned long long a = 0, r = 0, g = 0, b = 0; unsigned c = 0;
				for (int yy = y0; yy < y1; yy += ys)
				{
					const unsigned *row = src + (size_t) yy * srcStride;
					for (int xx = x0; xx < x1; xx += xs)
					{
						unsigned p = row[xx], pa = rgb ? 255 : p >> 24;
						a += pa; r += (p >> 16 & 255) * pa; g += (p >> 8 & 255) * pa; b += (p & 255) * pa; c++;
					}
				}
				if (a == 0) { o[x] = 0; continue; }
				o[x] = (rgb ? 0 : (unsigned) ((a + c / 2) / c) << 24) | (unsigned) ((r + a / 2) / a) << 16 | (unsigned) ((g + a / 2) / a) << 8 | (unsigned) ((b + a / 2) / a);
			}
		}
		return;
	}
	// bilinear (16.16), on colours multiplied by their alpha
	long long kx = ((long long) sw << 16) / dw, ky = ((long long) sh << 16) / dh;
	int xmax = sx + sw - 1, ymax = sy + sh - 1;
	for (int y = 0; y < dh; y++)
	{
		long long fy = ((long long) sy << 16) + ky * y + ky / 2 - 32768;
		if (fy < ((long long) sy << 16)) fy = (long long) sy << 16;
		int y0 = (int) (fy >> 16); if (y0 > ymax) y0 = ymax;
		int y1 = y0 < ymax ? y0 + 1 : y0; unsigned wy = (unsigned) ((fy >> 8) & 255);
		unsigned *o = dst + (size_t) y * dstStride;
		for (int x = 0; x < dw; x++)
		{
			long long fx = ((long long) sx << 16) + kx * x + kx / 2 - 32768;
			if (fx < ((long long) sx << 16)) fx = (long long) sx << 16;
			int x0 = (int) (fx >> 16); if (x0 > xmax) x0 = xmax;
			int x1 = x0 < xmax ? x0 + 1 : x0; unsigned wx = (unsigned) ((fx >> 8) & 255);
			unsigned q[4] = { src[(size_t) y0 * srcStride + x0], src[(size_t) y0 * srcStride + x1],
					  src[(size_t) y1 * srcStride + x0], src[(size_t) y1 * srcStride + x1] };
			unsigned wq[4] = { (256 - wx) * (256 - wy), wx * (256 - wy), (256 - wx) * wy, wx * wy };	// sum 65536
			unsigned long long a = 0, r = 0, g = 0, b = 0;
			for (int k = 0; k < 4; k++)
			{
				unsigned pa = rgb ? 255 : q[k] >> 24; unsigned long long w = (unsigned long long) wq[k] * pa;
				a += w; r += (q[k] >> 16 & 255) * w; g += (q[k] >> 8 & 255) * w; b += (q[k] & 255) * w;
			}
			if (a == 0) { o[x] = 0; continue; }
			unsigned A = (unsigned) ((a + 32768) >> 16); if (A > 255) A = 255;
			o[x] = (rgb ? 0 : A << 24) | (unsigned) ((r + a / 2) / a) << 16 | (unsigned) ((g + a / 2) / a) << 8 | (unsigned) ((b + a / 2) / a);
		}
	}
}
extern "C" void ik_scale (const unsigned *src, int srcW, int srcH, int srcStride, int sx, int sy, int sw, int sh,
			  unsigned *dst, int dstStride, int dw, int dh)
{
	scale_impl (src, srcW, srcH, srcStride, sx, sy, sw, sh, dst, dstStride, dw, dh, false);
}
extern "C" void ik_scale_rgb (const unsigned *src, int srcW, int srcH, int srcStride, int sx, int sy, int sw, int sh,
			      unsigned *dst, int dstStride, int dw, int dh)
{
	scale_impl (src, srcW, srcH, srcStride, sx, sy, sw, sh, dst, dstStride, dw, dh, true);
}
extern "C" ik_image *ik_resize (const ik_image *s, int w, int h)
{
	if (s == 0) return 0;
	ik_image *d = image_of (w, h);
	if (d != 0) ik_scale (s->p.px, s->p.w, s->p.h, s->p.w, 0, 0, s->p.w, s->p.h, d->p.px, w, w, h);
	return d;
}
extern "C" ik_image *ik_fit (const ik_image *s, int maxW, int maxH, int grow)
{
	if (s == 0 || maxW < 1 || maxH < 1) return 0;
	int w = maxW, h = (int) ((long long) s->p.h * maxW / s->p.w);
	if (h > maxH) { h = maxH; w = (int) ((long long) s->p.w * maxH / s->p.h); }
	if (!grow && (w > s->p.w || h > s->p.h)) { w = s->p.w; h = s->p.h; }
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	return ik_resize (s, w, h);
}
extern "C" ik_image *ik_cover (const ik_image *s, int w, int h)
{
	if (s == 0 || w < 1 || h < 1) return 0;
	ik_image *d = image_of (w, h);
	if (d == 0) return 0;
	int cw = s->p.w, ch = (int) ((long long) s->p.w * h / w);
	if (ch > s->p.h) { ch = s->p.h; cw = (int) ((long long) s->p.h * w / h); }
	if (cw < 1) cw = 1;
	if (ch < 1) ch = 1;
	ik_scale (s->p.px, s->p.w, s->p.h, s->p.w, (s->p.w - cw) / 2, (s->p.h - ch) / 2, cw, ch, d->p.px, w, w, h);
	return d;
}
extern "C" int ik_crop (ik_image *im, int x, int y, int w, int h)
{
	if (im == 0 || w < 1 || h < 1 || x >= im->p.w || y >= im->p.h || x + w <= 0 || y + h <= 0) return -1;
	crop (im->p, x, y, w, h);
	return 0;
}
extern "C" int ik_orient (ik_image *im, int o)
{
	if (im == 0 || o < 1 || o > 8) return -1;
	orient (im->p, o);
	return 0;
}
extern "C" int ik_rotate (ik_image *im, int quarter)
{
	if (im == 0) return -1;
	rotate90 (im->p, quarter);
	return 0;
}
extern "C" int ik_flip (ik_image *im, int vertical)		{ return ik_orient (im, vertical ? 4 : 2); }
extern "C" int ik_straighten (ik_image *im, int millideg)
{
	if (im == 0) return -1;
	straighten (im->p, (float) millideg / 1000.0f);
	opaque (im->p);
	return 0;
}

// ---- adjustments -----------------------------------------------------------------------------------

static void adjust_of (const struct ik_adjust *a, Adjust &o)
{
	o.v[A_EXPOSURE] = a->exposure; o.v[A_CONTRAST] = a->contrast; o.v[A_HIGHLIGHTS] = a->highlights; o.v[A_SHADOWS] = a->shadows;
	o.v[A_SATURATION] = a->saturation; o.v[A_WARMTH] = a->warmth; o.v[A_SHARPNESS] = a->sharpness;
	for (int i = 0; i < A_N; i++) { if (o.v[i] > 100) o.v[i] = 100; if (o.v[i] < -100) o.v[i] = -100; }
	if (o.v[A_SHARPNESS] < 0) o.v[A_SHARPNESS] = 0;
	o.filter = a->filter >= 0 && a->filter < FL_N ? a->filter : FL_NONE;
}
extern "C" void ik_adjust_apply (ik_image *im, const struct ik_adjust *a)
{
	if (im == 0 || a == 0) return;
	Adjust o; adjust_of (a, o);
	if (o.identity ()) return;
	size_t n = (size_t) im->p.w * im->p.h;
	unsigned char *alpha = (unsigned char *) malloc (n);	// (the curve works on the colours: the alpha put back)
	if (alpha != 0) for (size_t i = 0; i < n; i++) alpha[i] = (unsigned char) (im->p.px[i] >> 24);
	adjust (im->p, o);
	for (size_t i = 0; i < n; i++) im->p.px[i] = (im->p.px[i] & 0xFFFFFF) | (alpha != 0 ? (unsigned) alpha[i] << 24 : 0xFF000000u);
	free (alpha);
}
extern "C" void ik_adjust_auto (const ik_image *im, struct ik_adjust *out)
{
	if (out == 0) return;
	memset (out, 0, sizeof *out);
	if (im == 0) return;
	Adjust o; memset (&o, 0, sizeof o);
	auto_enhance (im->p, o);
	out->exposure = o.v[A_EXPOSURE]; out->contrast = o.v[A_CONTRAST]; out->highlights = o.v[A_HIGHLIGHTS]; out->shadows = o.v[A_SHADOWS];
	out->saturation = o.v[A_SATURATION]; out->warmth = o.v[A_WARMTH]; out->sharpness = o.v[A_SHARPNESS];
}
extern "C" const char *ik_filter_name (int f)			{ return f >= 0 && f < FL_N ? FILTER_NAME[f] : ""; }
