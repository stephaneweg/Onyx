//
// bmp.hpp -- user-side BMP decoder for Onyx apps / the uikit toolkit.
//
// Reads a 24-bpp uncompressed BMP from the SD card (kapi_open/read) and decodes it
// into a freshly heap-allocated 0x00RRGGBB pixel buffer (top-down). This is the
// userland reimplementation of the kernel's GImage::LoadBMP -- the goal is that the
// toolkit owns its graphics (icons + skins) with no kernel call; the kernel keeps its
// own copy only until window chrome moves user-side too.
//
// Magenta (0x00FF00FF) pixels are preserved as-is; the drawer color-keys them
// transparent (the Onyx icon convention).
//
#ifndef ONYX_BMP_HPP
#define ONYX_BMP_HPP

#include "kapi.h"
#include "onyxpp.hpp"		// operator new[]/delete[] (umm)

namespace ui {

static inline unsigned bmp_u32 (const unsigned char *p)
{ return (unsigned) p[0] | ((unsigned) p[1] << 8) | ((unsigned) p[2] << 16) | ((unsigned) p[3] << 24); }

// Decode `path` into a new[]'d 0x00RRGGBB buffer (caller delete[]s); sets *pw,*ph.
// Returns 0 on any failure (missing file, not 24-bpp, compressed, absurd size).
static inline unsigned *bmp_decode (const char *path, int *pw, int *ph)
{
	*pw = 0; *ph = 0;
	if (path == 0 || path[0] == '\0') return 0;
	void *f = kapi_open (path);
	if (f == 0) return 0;
	unsigned sz = kapi_fsize (f);
	if (sz < 54 || sz > 8u * 1024 * 1024) { kapi_close (f); return 0; }

	unsigned char *raw = new unsigned char[sz];
	if (raw == 0) { kapi_close (f); return 0; }
	int got = kapi_read (f, raw, sz);
	kapi_close (f);
	if (got < 54 || raw[0] != 'B' || raw[1] != 'M') { delete[] raw; return 0; }

	unsigned off = bmp_u32 (raw + 10);
	int W = (int) bmp_u32 (raw + 18);
	int H = (int) bmp_u32 (raw + 22);
	unsigned bpp  = (unsigned) raw[28] | ((unsigned) raw[29] << 8);
	unsigned comp = bmp_u32 (raw + 30);
	bool bottomUp = true;
	if (H < 0) { H = -H; bottomUp = false; }		// top-down if height < 0
	if (bpp != 24 || comp != 0 || W <= 0 || H <= 0 || (unsigned) W * (unsigned) H > 4u * 1024 * 1024)
	{ delete[] raw; return 0; }

	unsigned *out = new unsigned[(unsigned) (W * H)];
	if (out == 0) { delete[] raw; return 0; }
	int rowBytes = (W * 3 + 3) & ~3;			// rows padded to 4 bytes
	for (int y = 0; y < H; y++)
	{
		int srcRow = bottomUp ? (H - 1 - y) : y;
		unsigned rowOff = off + (unsigned) srcRow * rowBytes;
		unsigned *d = out + (unsigned) y * W;
		if (rowOff + (unsigned) (W * 3) > sz) { for (int x = 0; x < W; x++) d[x] = 0; continue; }
		const unsigned char *p = raw + rowOff;
		for (int x = 0; x < W; x++)
		{
			unsigned b = p[x * 3 + 0], g = p[x * 3 + 1], r = p[x * 3 + 2];
			d[x] = (r << 16) | (g << 8) | b;
		}
	}
	delete[] raw;
	*pw = W; *ph = H;
	return out;
}

} // namespace ui

#endif // ONYX_BMP_HPP
