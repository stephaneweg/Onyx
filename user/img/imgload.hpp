//
// imgload.hpp -- load an image file into 0xAARRGGBB pixels, for the wtk apps (no newlib).
//
//   BMP, GIF (all frames + delays), PNG, JPEG  -> stb_image.h   (public domain, v2.30)
//   WebP (lossy + lossless)                    -> simplewebp.h  (BSD-3, from libwebp)
//   PCX (1/2/4/8-bit paletted, 24-bit)         -> our own decoder below
//
//   ImgFrames im;
//   if (img_load ("SD:/pics/cat.png", &im)) { ... im.px[f] = w*h pixels, im.delay[f] ms ... }
//   img_free (&im);
//
// Pixels are 0xAARRGGBB (A = 255 opaque). The frames are allocated with new unsigned[]
// (the app's heap), so an app may also keep one and release it with delete []. The codecs
// are compiled ONCE into libwtk.a (wtk/imgload.cpp defines IMGLOAD_IMPLEMENTATION and is
// built with FP/SIMD -- they use floating point): an app just includes this header, and
// only the apps that call img_load link the codecs in. Their scratch memory also comes
// from operator new[] (resolved in the app, like the rest of wtk) -- never from a private
// umm heap, which would be a second heap inside the library.
//
#ifndef ONYX_IMGLOAD_HPP
#define ONYX_IMGLOAD_HPP

#define IMG_MAX_FRAMES	64			// animated GIF: frames kept
#define IMG_MAX_BYTES	(24u * 1024 * 1024)	// refuse bigger files

struct ImgFrames
{
	int w, h, n;				// size, frame count (1 for a still image)
	unsigned *px[IMG_MAX_FRAMES];		// n frames of w*h 0xAARRGGBB (new unsigned[])
	int delay[IMG_MAX_FRAMES];		// per-frame delay, ms (GIF; 0 = still)
	const char *format;			// "PNG", "JPEG", ...
};

void img_free (ImgFrames *im);			// delete [] the frames
bool img_load (const char *path, ImgFrames *im);	// false: unreadable / unknown format
bool img_is_image_name (const char *name);	// by extension (bmp gif png jpg jpeg jpe pcx webp)

#ifdef IMGLOAD_IMPLEMENTATION

#include "kapi.h"

// ---- scratch memory for the codecs: operator new[] with a size prefix (for realloc) ----
static void *img_alloc (unsigned long n)
{
	unsigned char *p = new unsigned char[n + 16];
	if (p == 0) return 0;
	*(unsigned long *) p = n;
	return p + 16;
}
static void img_dealloc (void *p) { if (p) delete [] ((unsigned char *) p - 16); }
static void *img_realloc (void *p, unsigned long n)
{
	if (p == 0) return img_alloc (n);
	unsigned long old = *(unsigned long *) ((unsigned char *) p - 16);
	void *q = img_alloc (n);
	if (q == 0) return 0;
	unsigned long c = old < n ? old : n;
	for (unsigned long i = 0; i < c; i++) ((unsigned char *) q)[i] = ((unsigned char *) p)[i];
	img_dealloc (p);
	return q;
}

// The C symbols the codecs reference (no libc linked).
#ifndef IMG_HOST_TEST			// (a host unit test links the real libc instead)
extern "C" {
__attribute__ ((weak)) void *malloc (unsigned long n) { return img_alloc (n); }
__attribute__ ((weak)) void  free (void *p) { img_dealloc (p); }
__attribute__ ((weak)) void *calloc (unsigned long n, unsigned long s)
{
	unsigned char *p = (unsigned char *) img_alloc (n * s);
	if (p) for (unsigned long i = 0; i < n * s; i++) p[i] = 0;
	return p;
}
__attribute__ ((weak)) void *realloc (void *p, unsigned long n) { return img_realloc (p, n); }
__attribute__ ((weak)) int memcmp (const void *a, const void *b, unsigned long n)
{
	const unsigned char *x = (const unsigned char *) a, *y = (const unsigned char *) b;
	for (unsigned long i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
	return 0;
}
__attribute__ ((weak)) int abs (int v) { return v < 0 ? -v : v; }
}
#endif

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_TGA
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STBI_ASSERT(x)		((void) 0)
#define STBI_MALLOC(n)		img_alloc (n)
#define STBI_REALLOC(p, n)	img_realloc (p, n)
#define STBI_FREE(p)		img_dealloc (p)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "stb_image.h"

#define NDEBUG
#define SIMPLEWEBP_DISABLE_STDIO
#define SIMPLEWEBP_IMPLEMENTATION
#include "simplewebp.h"
#pragma GCC diagnostic pop

void img_free (ImgFrames *im)
{
	for (int i = 0; i < im->n; i++) delete [] im->px[i];
	im->n = 0; im->w = im->h = 0;
}

// Whole file into an img_alloc buffer (*len bytes), or 0.
static unsigned char *img_read_file (const char *path, unsigned *len)
{
	void *f = kapi_open (path);
	if (f == 0) return 0;
	unsigned cap = 64 * 1024, n = 0;
	unsigned char *buf = (unsigned char *) img_alloc (cap);
	for (;;)
	{
		if (buf == 0) { kapi_close (f); return 0; }
		if (n == cap)
		{
			if (cap >= IMG_MAX_BYTES) { img_dealloc (buf); kapi_close (f); return 0; }
			cap *= 2;
			buf = (unsigned char *) img_realloc (buf, cap);
			continue;
		}
		int r = kapi_read (f, buf + n, cap - n);
		if (r <= 0) break;
		n += (unsigned) r;
	}
	kapi_close (f);
	*len = n;
	return buf;
}

// RGBA bytes -> a new unsigned[] frame of 0xAARRGGBB.
static unsigned *img_frame_from_rgba (const unsigned char *p, int count)
{
	unsigned *o = new unsigned[count];
	if (o == 0) return 0;
	for (int i = 0; i < count; i++)
		o[i] = ((unsigned) p[i * 4 + 3] << 24) | ((unsigned) p[i * 4] << 16) | ((unsigned) p[i * 4 + 1] << 8) | p[i * 4 + 2];
	return o;
}

// ---- PCX (ZSoft): RLE scanlines of `planes` planes x `bpl` bytes ------------------------
static unsigned *img_pcx (const unsigned char *d, unsigned len, int *pw, int *ph)
{
	if (len < 128 || d[0] != 0x0A || d[2] != 1) return 0;
	int bpp = d[3], planes = d[65], bpl = d[66] | (d[67] << 8);
	int w = (d[8] | (d[9] << 8)) - (d[4] | (d[5] << 8)) + 1;
	int h = (d[10] | (d[11] << 8)) - (d[6] | (d[7] << 8)) + 1;
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || bpl <= 0 || planes < 1 || planes > 4) return 0;
	unsigned pal[256];
	for (int i = 0; i < 256; i++) pal[i] = 0xFF000000u;
	for (int i = 0; i < 16; i++) pal[i] = 0xFF000000u | (d[16 + i * 3] << 16) | (d[17 + i * 3] << 8) | d[18 + i * 3];
	if (bpp == 8 && planes == 1 && len >= 769 && d[len - 769] == 0x0C)	// VGA palette at the end
		for (int i = 0; i < 256; i++)
			pal[i] = 0xFF000000u | (d[len - 768 + i * 3] << 16) | (d[len - 767 + i * 3] << 8) | d[len - 766 + i * 3];
	if (bpp == 1 && planes == 1) { pal[0] = 0xFF000000u; pal[1] = 0xFFFFFFFFu; }
	unsigned *out = new unsigned[(unsigned long) w * h];
	unsigned char *line = (unsigned char *) img_alloc ((unsigned long) bpl * planes);
	if (!out || !line) { delete [] out; img_dealloc (line); return 0; }
	unsigned pos = 128;
	for (int y = 0; y < h; y++)
	{
		int total = bpl * planes;
		for (int i = 0; i < total; )		// RLE: 0b11xxxxxx = run length, then a byte
		{
			unsigned char b = pos < len ? d[pos++] : 0, v = b;
			int run = 1;
			if ((b & 0xC0) == 0xC0) { run = b & 0x3F; v = pos < len ? d[pos++] : 0; }
			while (run-- > 0 && i < total) line[i++] = v;
		}
		unsigned *row = out + (unsigned long) y * w;
		for (int x = 0; x < w; x++)
		{
			if (bpp == 8 && planes >= 3)				// 24-bit: R, G, B planes
				row[x] = 0xFF000000u | (line[x] << 16) | (line[bpl + x] << 8) | line[2 * bpl + x];
			else if (bpp == 8)
				row[x] = pal[line[x]];
			else							// 1/2/4-bit, 1..4 planes
			{
				int idx = 0;
				for (int p = 0; p < planes; p++)
				{
					int bit = x * bpp, byte = p * bpl + bit / 8, sh = 8 - bpp - bit % 8;
					idx |= ((line[byte] >> sh) & ((1 << bpp) - 1)) << (p * bpp);
				}
				row[x] = pal[idx & 255];
			}
		}
	}
	img_dealloc (line);
	*pw = w; *ph = h;
	return out;
}

// ---- WebP (simplewebp) -------------------------------------------------------------------
static void *img__wa (void *, size_t n) { return img_alloc (n); }
static void  img__wf (void *, void *p) { img_dealloc (p); }
static unsigned *img_webp (unsigned char *d, unsigned len, int *pw, int *ph)
{
	simplewebp_allocator al = { img__wa, img__wf, 0 };
	simplewebp *wp = 0;
	if (simplewebp_load_from_memory (d, len, &al, &wp) != SIMPLEWEBP_NO_ERROR) return 0;
	size_t w = 0, h = 0;
	simplewebp_get_dimensions (wp, &w, &h);
	unsigned char *rgba = (unsigned char *) img_alloc (w * h * 4);
	if (rgba == 0 || simplewebp_decode (wp, rgba, 0) != SIMPLEWEBP_NO_ERROR)
	{ img_dealloc (rgba); simplewebp_unload (wp); return 0; }
	simplewebp_unload (wp);
	unsigned *px = img_frame_from_rgba (rgba, (int) (w * h));
	img_dealloc (rgba);
	*pw = (int) w; *ph = (int) h;
	return px;
}

bool img_load (const char *path, ImgFrames *im)
{
	im->n = 0; im->w = im->h = 0; im->format = "?";
	unsigned len = 0;
	unsigned char *d = img_read_file (path, &len);
	if (d == 0) return false;
	int w = 0, h = 0;
	unsigned *px = 0;
	if (len >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' && d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P')
	{ im->format = "WebP"; px = img_webp (d, len, &w, &h); }
	else if (len >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F')
	{
		im->format = "GIF";
		int *delays = 0, comp = 0, frames = 0;
		stbi_uc *all = stbi_load_gif_from_memory (d, (int) len, &delays, &w, &h, &frames, &comp, 4);
		if (all)
		{
			int n = frames < IMG_MAX_FRAMES ? frames : IMG_MAX_FRAMES;
			unsigned long fs = (unsigned long) w * h * 4;
			for (int i = 0; i < n; i++)
			{
				unsigned *f = img_frame_from_rgba (all + i * fs, w * h);
				if (!f) break;
				im->px[im->n] = f;
				im->delay[im->n] = delays ? delays[i] : 0;
				if (frames > 1 && im->delay[im->n] < 20) im->delay[im->n] = 100;	// 0 = "as fast as possible"
				im->n++;
			}
			stbi_image_free (all);
			if (delays) STBI_FREE (delays);
		}
		img_dealloc (d);
		im->w = w; im->h = h;
		return im->n > 0;
	}
	else if (len >= 128 && d[0] == 0x0A && d[2] == 1)
	{ im->format = "PCX"; px = img_pcx (d, len, &w, &h); }
	else
	{
		int comp = 0;
		im->format = (d[0] == 0x89 && d[1] == 'P') ? "PNG" : (d[0] == 0xFF && d[1] == 0xD8) ? "JPEG"
			   : (d[0] == 'B' && d[1] == 'M') ? "BMP" : "?";
		stbi_uc *rgba = stbi_load_from_memory (d, (int) len, &w, &h, &comp, 4);
		if (rgba) { px = img_frame_from_rgba (rgba, w * h); stbi_image_free (rgba); }
	}
	img_dealloc (d);
	if (px == 0) return false;
	im->px[0] = px; im->delay[0] = 0; im->n = 1; im->w = w; im->h = h;
	return true;
}

bool img_is_image_name (const char *n)
{
	static const char *ext[] = { "bmp", "gif", "png", "jpg", "jpeg", "jpe", "pcx", "webp", 0 };
	int len = 0; while (n[len]) len++;
	for (int e = 0; ext[e]; e++)
	{
		int el = 0; while (ext[e][el]) el++;
		if (len < el + 1 || n[len - el - 1] != '.') continue;
		bool ok = true;
		for (int i = 0; i < el; i++)
		{
			char c = n[len - el + i]; if (c >= 'A' && c <= 'Z') c += 32;
			if (c != ext[e][i]) { ok = false; break; }
		}
		if (ok) return true;
	}
	return false;
}

#endif // IMGLOAD_IMPLEMENTATION

#endif
