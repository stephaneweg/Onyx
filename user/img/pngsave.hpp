//
// pngsave.hpp -- writing images and archives, integer only, header-only (no libc):
//   * deflate (RFC 1951): LZ77 over a 32 KB window (hash chains) coded with the fixed Huffman
//     codes -- a flat picture (a paint program's) shrinks many times over; zlib's wrapper too.
//   * PNG: 8-bit RGBA (or RGB), each row with the filter that suits it best, one IDAT.
//   * JPEG: baseline, 4:2:0, the standard tables scaled by a quality (1..100), an integer DCT.
//   * GIF: GIF89a, its palette the picture's colours (median cut past 256), one transparent index
//     for the clear pixels, LZW.
//   * BMP: 24-bit (bottom-up, BGR).
//   (JPEG and BMP have no transparency: the clear pixels are laid on white first.)
//   * ZIP: entries stored or deflated, the central directory -- and the entries of one found
//     (their bytes: a deflated one is inflated by the caller, e.g. img_inflate, imgload.hpp).
//
//   unsigned n; unsigned char *png = png_encode (px, w, h, true, &n);   ... delete [] png;
//   ZipOut z; z.add ("mimetype", "image/openraster", 16, false); z.add ("a.png", png, n, false);
//   unsigned char *zip = z.finish (&n);                               ... delete [] zip;
//   ZipEntry e; if (zip_find (zip, n, "stack.xml", &e)) ... e.data, e.csize, e.usize, e.method
//
// The pixels are 0xAARRGGBB (A = 255 opaque). Memory: new / delete (the app's heap).
//
#ifndef ONYX_PNGSAVE_HPP
#define ONYX_PNGSAVE_HPP

namespace pngsave {

// ---- a growing byte buffer, and a bit writer (LSB first, as deflate wants) ---------------------------
struct Buf
{
	unsigned char *b; unsigned n, cap;
	unsigned bits; int nbits;
	Buf () : b (0), n (0), cap (0), bits (0), nbits (0) {}
	void grow (unsigned k)
	{
		if (n + k <= cap) return;
		unsigned c = cap ? cap * 2 : 65536; while (c < n + k) c *= 2;
		unsigned char *t = new unsigned char[c];
		for (unsigned i = 0; i < n; i++) t[i] = b[i];
		delete[] b; b = t; cap = c;
	}
	void put (unsigned char c) { grow (1); b[n++] = c; }
	void put (const void *p, unsigned k) { grow (k); for (unsigned i = 0; i < k; i++) b[n++] = ((const unsigned char *) p)[i]; }
	void be32 (unsigned v) { put ((unsigned char) (v >> 24)); put ((unsigned char) (v >> 16)); put ((unsigned char) (v >> 8)); put ((unsigned char) v); }
	void le16 (unsigned v) { put ((unsigned char) v); put ((unsigned char) (v >> 8)); }
	void le32 (unsigned v) { le16 (v & 0xFFFF); le16 (v >> 16); }
	void putBits (unsigned v, int k) { bits |= v << nbits; nbits += k; while (nbits >= 8) { put ((unsigned char) bits); bits >>= 8; nbits -= 8; } }
	void flushBits () { if (nbits > 0) put ((unsigned char) bits); bits = 0; nbits = 0; }
	unsigned char *take (unsigned *len) { unsigned char *r = b; *len = n; b = 0; n = cap = 0; return r; }
	~Buf () { delete[] b; }
};

// ---- checksums ------------------------------------------------------------------------------------------
static unsigned crc_tab[256];
static unsigned crc32 (unsigned c, const unsigned char *p, unsigned n)
{
	if (!crc_tab[1]) for (unsigned i = 0; i < 256; i++) { unsigned v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_tab[i] = v; }
	c = ~c;
	for (unsigned i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 255] ^ (c >> 8);
	return ~c;
}
static unsigned adler32 (const unsigned char *p, unsigned n)
{
	unsigned a = 1, b = 0;
	while (n)
	{
		unsigned k = n < 5552 ? n : 5552;			// (the sums fit before the modulo)
		n -= k;
		while (k--) { a += *p++; b += a; }
		a %= 65521; b %= 65521;
	}
	return b << 16 | a;
}

// ---- deflate --------------------------------------------------------------------------------------------
static const unsigned short LEN_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char LEN_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short DIST_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char DIST_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static unsigned rev (unsigned v, int k) { unsigned r = 0; for (int i = 0; i < k; i++) { r = r << 1 | (v & 1); v >>= 1; } return r; }

// A literal / length symbol with the fixed codes (MSB first: reversed into the LSB-first stream).
static void fixedSym (Buf &o, unsigned s)
{
	if (s < 144) o.putBits (rev (0x30 + s, 8), 8);
	else if (s < 256) o.putBits (rev (0x190 + s - 144, 9), 9);
	else if (s < 280) o.putBits (rev (s - 256, 7), 7);
	else o.putBits (rev (0xC0 + s - 280, 8), 8);
}
static void fixedMatch (Buf &o, unsigned len, unsigned dist)
{
	int li = 28; while (LEN_BASE[li] > len) li--;
	fixedSym (o, 257 + li);
	if (LEN_EXTRA[li]) o.putBits (len - LEN_BASE[li], LEN_EXTRA[li]);
	int di = 29; while (DIST_BASE[di] > dist) di--;
	o.putBits (rev ((unsigned) di, 5), 5);
	if (DIST_EXTRA[di]) o.putBits (dist - DIST_BASE[di], DIST_EXTRA[di]);
}

// Compress len bytes: one fixed-Huffman block (zlib: its 2-byte header and Adler-32 around it).
static unsigned char *deflate (const unsigned char *in, unsigned len, bool zlib, unsigned *outLen)
{
	enum { WSIZE = 32768, HBITS = 15, HSIZE = 1 << HBITS, MAXCHAIN = 48, NICE = 128 };
	Buf o;
	o.grow (len / 4 + 64);
	if (zlib) { o.put (0x78); o.put (0x01); }
	o.putBits (1, 1); o.putBits (1, 2);			// (the last block, fixed codes)
	int *head = new int[HSIZE], *prev = new int[WSIZE];
	for (int i = 0; i < HSIZE; i++) head[i] = -1;
	auto hash = [&] (unsigned p) { return ((unsigned) in[p] << 10 ^ (unsigned) in[p + 1] << 5 ^ in[p + 2]) & (HSIZE - 1); };
	auto insert = [&] (unsigned p) { if (p + 2 < len) { unsigned h = hash (p); prev[p & (WSIZE - 1)] = head[h]; head[h] = (int) p; } };
	unsigned p = 0;
	while (p < len)
	{
		unsigned best = 0, bestD = 0;
		if (p + 2 < len)
		{
			int cand = head[hash (p)], chain = MAXCHAIN;
			unsigned maxLen = len - p < 258 ? len - p : 258;
			while (cand >= 0 && chain-- > 0 && p - (unsigned) cand <= WSIZE)
			{
				const unsigned char *a = in + cand, *b = in + p;
				if (a[best] == b[best])
				{
					unsigned k = 0;
					while (k < maxLen && a[k] == b[k]) k++;
					if (k > best) { best = k; bestD = p - (unsigned) cand; if (k >= NICE || k == maxLen) break; }
				}
				int nx = prev[cand & (WSIZE - 1)];
				if (nx >= cand) break;					// (a stale link from the window's last round)
				cand = nx;
			}
		}
		if (best >= 3)
		{
			fixedMatch (o, best, bestD);
			for (unsigned k = 0; k < best; k++) insert (p + k);
			p += best;
		}
		else { fixedSym (o, in[p]); insert (p); p++; }
	}
	fixedSym (o, 256);
	o.flushBits ();
	if (zlib) o.be32 (adler32 (in, len));
	delete[] head; delete[] prev;
	return o.take (outLen);
}

// ---- PNG ------------------------------------------------------------------------------------------------
static void chunk (Buf &o, const char *type, const unsigned char *d, unsigned n)
{
	o.be32 (n);
	unsigned start = o.n;
	o.put (type, 4);
	if (n) o.put (d, n);
	o.be32 (crc32 (0, o.b + start, n + 4));
}
static inline int pabs (int v) { return v < 0 ? -v : v; }
static unsigned char paeth (int a, int b, int c)
{
	int p = a + b - c, pa = pabs (p - a), pb = pabs (p - b), pc = pabs (p - c);
	return (unsigned char) (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

// w x h pixels 0xAARRGGBB -> a PNG file (alpha: RGBA, else RGB).
static unsigned char *png_encode (const unsigned *px, int w, int h, bool alpha, unsigned *outLen)
{
	int bpp = alpha ? 4 : 3;
	unsigned rowLen = (unsigned) w * bpp;
	unsigned char *raw = new unsigned char[(rowLen + 1) * (unsigned) h];
	unsigned char *cur = new unsigned char[rowLen], *up = new unsigned char[rowLen], *tr = new unsigned char[rowLen];
	for (unsigned i = 0; i < rowLen; i++) up[i] = 0;
	for (int y = 0; y < h; y++)
	{
		const unsigned *s = px + (unsigned) y * w;
		for (int x = 0; x < w; x++)
		{
			unsigned c = s[x];
			unsigned char *d = cur + x * bpp;
			d[0] = (unsigned char) (c >> 16); d[1] = (unsigned char) (c >> 8); d[2] = (unsigned char) c;
			if (alpha) d[3] = (unsigned char) (c >> 24);
		}
		// the filter with the smallest sum of (signed) bytes
		int bestF = 0; unsigned bestS = ~0u;
		for (int f = 0; f < 5; f++)
		{
			unsigned sum = 0;
			for (unsigned i = 0; i < rowLen; i++)
			{
				int a = i >= (unsigned) bpp ? cur[i - bpp] : 0, b = up[i], c = i >= (unsigned) bpp ? up[i - bpp] : 0;
				unsigned char v = (unsigned char) (f == 0 ? cur[i] : f == 1 ? cur[i] - a : f == 2 ? cur[i] - b : f == 3 ? cur[i] - ((a + b) >> 1) : cur[i] - paeth (a, b, c));
				sum += v < 128 ? v : 256 - v;
				if (sum >= bestS) break;
			}
			if (sum < bestS) { bestS = sum; bestF = f; }
		}
		unsigned char *d = raw + (unsigned) y * (rowLen + 1);
		d[0] = (unsigned char) bestF;
		for (unsigned i = 0; i < rowLen; i++)
		{
			int a = i >= (unsigned) bpp ? cur[i - bpp] : 0, b = up[i], c = i >= (unsigned) bpp ? up[i - bpp] : 0;
			d[1 + i] = (unsigned char) (bestF == 0 ? cur[i] : bestF == 1 ? cur[i] - a : bestF == 2 ? cur[i] - b : bestF == 3 ? cur[i] - ((a + b) >> 1) : cur[i] - paeth (a, b, c));
		}
		unsigned char *t = up; up = cur; cur = t;
	}
	delete[] cur; delete[] up; delete[] tr;
	unsigned zn;
	unsigned char *z = deflate (raw, (rowLen + 1) * (unsigned) h, true, &zn);
	delete[] raw;
	Buf o;
	static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
	o.put (sig, 8);
	unsigned char ih[13] = { (unsigned char) (w >> 24), (unsigned char) (w >> 16), (unsigned char) (w >> 8), (unsigned char) w,
				 (unsigned char) (h >> 24), (unsigned char) (h >> 16), (unsigned char) (h >> 8), (unsigned char) h,
				 8, (unsigned char) (alpha ? 6 : 2), 0, 0, 0 };
	chunk (o, "IHDR", ih, 13);
	chunk (o, "IDAT", z, zn);
	chunk (o, "IEND", 0, 0);
	delete[] z;
	return o.take (outLen);
}

// ---- pixels laid on white (the formats without alpha) ----------------------------------------------------
static inline unsigned on_white (unsigned c)
{
	unsigned a = c >> 24;
	if (a == 255) return c & 0xFFFFFF;
	unsigned r = (((c >> 16) & 255) * a + 255 * (255 - a)) / 255, g = (((c >> 8) & 255) * a + 255 * (255 - a)) / 255,
		 b = ((c & 255) * a + 255 * (255 - a)) / 255;
	return r << 16 | g << 8 | b;
}

// ---- BMP ------------------------------------------------------------------------------------------------
static unsigned char *bmp_encode (const unsigned *px, int w, int h, unsigned *outLen)
{
	unsigned row = ((unsigned) w * 3 + 3) & ~3u, size = 54 + row * (unsigned) h;
	unsigned char *o = new unsigned char[size];
	for (unsigned i = 0; i < 54; i++) o[i] = 0;
	o[0] = 'B'; o[1] = 'M';
	auto le32 = [&] (int at, unsigned v) { o[at] = (unsigned char) v; o[at + 1] = (unsigned char) (v >> 8); o[at + 2] = (unsigned char) (v >> 16); o[at + 3] = (unsigned char) (v >> 24); };
	le32 (2, size); le32 (10, 54); le32 (14, 40); le32 (18, (unsigned) w); le32 (22, (unsigned) h);
	o[26] = 1; o[28] = 24; le32 (34, row * (unsigned) h); le32 (38, 2835); le32 (42, 2835);	// (72 dpi)
	for (int y = 0; y < h; y++)
	{
		unsigned char *d = o + 54 + (unsigned) (h - 1 - y) * row;
		for (int x = 0; x < w; x++) { unsigned c = on_white (px[(unsigned) y * w + x]); d[3 * x] = (unsigned char) c; d[3 * x + 1] = (unsigned char) (c >> 8); d[3 * x + 2] = (unsigned char) (c >> 16); }
		for (unsigned k = (unsigned) w * 3; k < row; k++) d[k] = 0;
	}
	*outLen = size;
	return o;
}

// ---- JPEG -----------------------------------------------------------------------------------------------
static const unsigned char ZIGZAG[64] = {
	0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
static const unsigned char QLUM[64] = {
	16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
	18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
static const unsigned char QCHR[64] = {
	17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 };
static const unsigned char DC_LUM_BITS[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const unsigned char DC_CHR_BITS[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const unsigned char DC_VALS[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const unsigned char AC_LUM_BITS[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D };
static const unsigned char AC_LUM_VALS[162] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
	0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
	0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
	0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94,
	0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
	0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
	0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
	0xF9, 0xFA };
static const unsigned char AC_CHR_BITS[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const unsigned char AC_CHR_VALS[162] = {
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81,
	0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
	0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92,
	0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
	0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
	0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
	0xF9, 0xFA };
// The DCT's basis, x 4096: C(u) / 2 cos ((2x + 1) u pi / 16).
static const short DCT_T[64] = {
	1448, 1448, 1448, 1448, 1448, 1448, 1448, 1448, 2009, 1703, 1138, 400, -400, -1138, -1703, -2009,
	1892, 784, -784, -1892, -1892, -784, 784, 1892, 1703, -400, -2009, -1138, 1138, 2009, 400, -1703,
	1448, -1448, -1448, 1448, 1448, -1448, -1448, 1448, 1138, -2009, 400, 1703, -1703, -400, 2009, -1138,
	784, -1892, 1892, -784, -784, 1892, -1892, 784, 400, -1138, 1703, -2009, 2009, -1703, 1138, -400 };

struct Huff { unsigned short code[256]; unsigned char size[256]; };
static void huff_make (Huff &h, const unsigned char *bits, const unsigned char *vals)
{
	unsigned code = 0; int k = 0;
	for (int i = 0; i < 256; i++) h.size[i] = 0;
	for (int len = 1; len <= 16; len++)
	{
		for (int i = 0; i < bits[len - 1]; i++) { h.code[vals[k]] = (unsigned short) code; h.size[vals[k]] = (unsigned char) len; k++; code++; }
		code <<= 1;
	}
}

struct JpegOut
{
	Buf o; unsigned acc; int n;			// (the bits, MSB first)
	JpegOut () : acc (0), n (0) {}
	void bits (unsigned v, int k)
	{
		for (int i = k - 1; i >= 0; i--)
		{
			acc = acc << 1 | ((v >> i) & 1);
			if (++n == 8) { o.put ((unsigned char) acc); if ((acc & 255) == 255) o.put (0); acc = 0; n = 0; }
		}
	}
	void flush () { while (n) bits (1, 1); }
	void w16 (unsigned v) { o.put ((unsigned char) (v >> 8)); o.put ((unsigned char) v); }
};

static void jpeg_block (JpegOut &j, const int *f, const unsigned char *q, int &prevDC, const Huff &dc, const Huff &ac)
{
	long long tmp[64];
	for (int y = 0; y < 8; y++)					// (rows, then columns)
		for (int u = 0; u < 8; u++)
		{
			long long s = 0;
			for (int x = 0; x < 8; x++) s += (long long) f[y * 8 + x] * DCT_T[u * 8 + x];
			tmp[y * 8 + u] = s;
		}
	int coef[64], zz[64];
	for (int v = 0; v < 8; v++)
		for (int u = 0; u < 8; u++)
		{
			long long sum = 0;
			for (int y = 0; y < 8; y++) sum += tmp[y * 8 + u] * DCT_T[v * 8 + y];
			long long d = (long long) q[v * 8 + u] << 24;		// (F / 4096^2, then / q, rounded)
			coef[v * 8 + u] = (int) (sum >= 0 ? (sum + d / 2) / d : -((-sum + d / 2) / d));
		}
	for (int k = 0; k < 64; k++) zz[k] = coef[ZIGZAG[k]];
	auto category = [] (int v) { int a = v < 0 ? -v : v, s = 0; while (a) { s++; a >>= 1; } return s; };
	int diff = zz[0] - prevDC; prevDC = zz[0];
	int s = category (diff);
	j.bits (dc.code[s], dc.size[s]);
	if (s) j.bits ((unsigned) (diff < 0 ? diff + (1 << s) - 1 : diff) & ((1u << s) - 1), s);
	int run = 0;
	for (int k = 1; k < 64; k++)
	{
		int v = zz[k];
		if (v == 0) { run++; continue; }
		while (run > 15) { j.bits (ac.code[0xF0], ac.size[0xF0]); run -= 16; }
		s = category (v);
		int sym = run << 4 | s;
		j.bits (ac.code[sym], ac.size[sym]);
		j.bits ((unsigned) (v < 0 ? v + (1 << s) - 1 : v) & ((1u << s) - 1), s);
		run = 0;
	}
	if (run) j.bits (ac.code[0], ac.size[0]);
}

// w x h pixels -> a JPEG file (quality 1..100; the clear pixels on white).
static unsigned char *jpeg_encode (const unsigned *px, int w, int h, int quality, unsigned *outLen)
{
	if (quality < 1) quality = 1;
	if (quality > 100) quality = 100;
	int sc = quality < 50 ? 5000 / quality : 200 - quality * 2;
	unsigned char ql[64], qc[64];
	for (int i = 0; i < 64; i++)
	{
		int a = (QLUM[i] * sc + 50) / 100, b = (QCHR[i] * sc + 50) / 100;
		ql[i] = (unsigned char) (a < 1 ? 1 : a > 255 ? 255 : a); qc[i] = (unsigned char) (b < 1 ? 1 : b > 255 ? 255 : b);
	}
	Huff dcl, dcc, acl, acc;
	huff_make (dcl, DC_LUM_BITS, DC_VALS); huff_make (dcc, DC_CHR_BITS, DC_VALS);
	huff_make (acl, AC_LUM_BITS, AC_LUM_VALS); huff_make (acc, AC_CHR_BITS, AC_CHR_VALS);
	JpegOut j;
	Buf &o = j.o;
	o.put (0xFF); o.put (0xD8);						// SOI
	o.put (0xFF); o.put (0xE0); j.w16 (16); o.put ("JFIF", 5); o.put (1); o.put (1); o.put (0); j.w16 (1); j.w16 (1); o.put (0); o.put (0);
	for (int t = 0; t < 2; t++)						// DQT (zigzag order)
	{
		o.put (0xFF); o.put (0xDB); j.w16 (67); o.put ((unsigned char) t);
		for (int k = 0; k < 64; k++) o.put ((t ? qc : ql)[ZIGZAG[k]]);
	}
	o.put (0xFF); o.put (0xC0); j.w16 (17); o.put (8); j.w16 ((unsigned) h); j.w16 ((unsigned) w); o.put (3);	// SOF0
	o.put (1); o.put (0x22); o.put (0); o.put (2); o.put (0x11); o.put (1); o.put (3); o.put (0x11); o.put (1);
	struct T { int cls, id; const unsigned char *bits, *vals; int n; } tabs[4] = {
		{ 0, 0, DC_LUM_BITS, DC_VALS, 12 }, { 1, 0, AC_LUM_BITS, AC_LUM_VALS, 162 },
		{ 0, 1, DC_CHR_BITS, DC_VALS, 12 }, { 1, 1, AC_CHR_BITS, AC_CHR_VALS, 162 } };
	for (int t = 0; t < 4; t++)						// DHT
	{
		o.put (0xFF); o.put (0xC4); j.w16 ((unsigned) (19 + tabs[t].n)); o.put ((unsigned char) (tabs[t].cls << 4 | tabs[t].id));
		o.put (tabs[t].bits, 16); o.put (tabs[t].vals, (unsigned) tabs[t].n);
	}
	o.put (0xFF); o.put (0xDA); j.w16 (12); o.put (3);			// SOS
	o.put (1); o.put (0x00); o.put (2); o.put (0x11); o.put (3); o.put (0x11); o.put (0); o.put (63); o.put (0);
	int pdY = 0, pdCb = 0, pdCr = 0;
	int Y[4][64], Cb[64], Cr[64];
	for (int my = 0; my < h; my += 16)
		for (int mx = 0; mx < w; mx += 16)
		{
			int sb[64], sr[64];
			for (int i = 0; i < 64; i++) sb[i] = sr[i] = 0;
			for (int yy = 0; yy < 16; yy++)
				for (int xx = 0; xx < 16; xx++)
				{
					int X = mx + xx < w ? mx + xx : w - 1, Yy = my + yy < h ? my + yy : h - 1;
					unsigned c = on_white (px[(unsigned) Yy * w + X]);
					int r = (int) (c >> 16 & 255), g = (int) (c >> 8 & 255), b = (int) (c & 255);
					int lum = (77 * r + 150 * g + 29 * b + 128) >> 8;
					Y[(yy >> 3) * 2 + (xx >> 3)][(yy & 7) * 8 + (xx & 7)] = lum - 128;
					sb[(yy >> 1) * 8 + (xx >> 1)] += ((-43 * r - 85 * g + 128 * b) >> 8);
					sr[(yy >> 1) * 8 + (xx >> 1)] += ((128 * r - 107 * g - 21 * b) >> 8);
				}
			for (int i = 0; i < 64; i++) { Cb[i] = (sb[i] + 2) >> 2; Cr[i] = (sr[i] + 2) >> 2; }
			for (int k = 0; k < 4; k++) jpeg_block (j, Y[k], ql, pdY, dcl, acl);
			jpeg_block (j, Cb, qc, pdCb, dcc, acc);
			jpeg_block (j, Cr, qc, pdCr, dcc, acc);
		}
	j.flush ();
	o.put (0xFF); o.put (0xD9);						// EOI
	return o.take (outLen);
}

// ---- GIF ------------------------------------------------------------------------------------------------
// The palette: the picture's colours when they are few enough, else a median cut of them (on a
// 5-bit-a-channel histogram); the clear pixels (alpha < 128) one more index.
static unsigned char *gif_encode (const unsigned *px, int w, int h, unsigned *outLen)
{
	unsigned n = (unsigned) w * h;
	bool clear = false;
	for (unsigned i = 0; i < n; i++) if ((px[i] >> 24) < 128) { clear = true; break; }
	int maxCols = clear ? 255 : 256;
	unsigned pal[256]; int np = 0;
	unsigned char *idx = new unsigned char[n];
	// exact colours: an open-addressing table
	enum { HS = 1024 };
	unsigned hk[HS]; short hv[HS]; bool exact = true;
	for (int i = 0; i < HS; i++) hk[i] = 0xFFFFFFFFu;
	for (unsigned i = 0; i < n && exact; i++)
	{
		if ((px[i] >> 24) < 128) continue;
		unsigned c = px[i] & 0xFFFFFF, h2 = (c * 2654435761u) >> 22;
		while (hk[h2] != 0xFFFFFFFFu && hk[h2] != c) h2 = (h2 + 1) & (HS - 1);
		if (hk[h2] == c) continue;
		if (np == maxCols) { exact = false; break; }
		hk[h2] = c; hv[h2] = (short) np; pal[np++] = c;
	}
	if (exact)
		for (unsigned i = 0; i < n; i++)
		{
			if ((px[i] >> 24) < 128) { idx[i] = (unsigned char) np; continue; }
			unsigned c = px[i] & 0xFFFFFF, h2 = (c * 2654435761u) >> 22;
			while (hk[h2] != c) h2 = (h2 + 1) & (HS - 1);
			idx[i] = (unsigned char) hv[h2];
		}
	else
	{
		// median cut over the 15-bit histogram
		unsigned *hist = new unsigned[32768];
		for (int i = 0; i < 32768; i++) hist[i] = 0;
		for (unsigned i = 0; i < n; i++) if ((px[i] >> 24) >= 128) { unsigned c = px[i]; hist[((c >> 19) & 31) << 10 | ((c >> 11) & 31) << 5 | ((c >> 3) & 31)]++; }
		struct Box { int lo[3], hi[3]; unsigned count; } box[256];
		int nb = 1;
		box[0].lo[0] = box[0].lo[1] = box[0].lo[2] = 0; box[0].hi[0] = box[0].hi[1] = box[0].hi[2] = 31;
		auto shrink = [&] (Box &b) {			// (to its colours' extent, and its count)
			int lo[3] = { 31, 31, 31 }, hi[3] = { 0, 0, 0 }; unsigned cnt = 0;
			for (int r = b.lo[0]; r <= b.hi[0]; r++) for (int g = b.lo[1]; g <= b.hi[1]; g++) for (int bb = b.lo[2]; bb <= b.hi[2]; bb++)
			{
				unsigned k = hist[r << 10 | g << 5 | bb]; if (!k) continue;
				cnt += k;
				lo[0] = r < lo[0] ? r : lo[0]; hi[0] = r > hi[0] ? r : hi[0]; lo[1] = g < lo[1] ? g : lo[1]; hi[1] = g > hi[1] ? g : hi[1];
				lo[2] = bb < lo[2] ? bb : lo[2]; hi[2] = bb > hi[2] ? bb : hi[2];
			}
			if (cnt) for (int a = 0; a < 3; a++) { b.lo[a] = lo[a]; b.hi[a] = hi[a]; }
			b.count = cnt;
		};
		shrink (box[0]);
		while (nb < maxCols)
		{
			int best = -1; long long bs = 0;
			for (int i = 0; i < nb; i++)
			{
				int span = 0; for (int a = 0; a < 3; a++) if (box[i].hi[a] - box[i].lo[a] > span) span = box[i].hi[a] - box[i].lo[a];
				long long sc2 = (long long) span * box[i].count;
				if (span > 0 && sc2 > bs) { bs = sc2; best = i; }
			}
			if (best < 0) break;
			Box &b = box[best];
			int ax = 0; for (int a = 1; a < 3; a++) if (b.hi[a] - b.lo[a] > b.hi[ax] - b.lo[ax]) ax = a;
			// the median along ax
			unsigned half = b.count / 2, acc2 = 0; int cut = b.lo[ax];
			for (int v = b.lo[ax]; v < b.hi[ax]; v++)
			{
				int lo2[3] = { b.lo[0], b.lo[1], b.lo[2] }, hi2[3] = { b.hi[0], b.hi[1], b.hi[2] }; lo2[ax] = hi2[ax] = v;
				for (int r = lo2[0]; r <= hi2[0]; r++) for (int g = lo2[1]; g <= hi2[1]; g++) for (int bb = lo2[2]; bb <= hi2[2]; bb++) acc2 += hist[r << 10 | g << 5 | bb];
				cut = v;
				if (acc2 >= half) break;
			}
			Box nbx = b; nbx.lo[ax] = cut + 1; b.hi[ax] = cut;
			shrink (b); shrink (nbx);
			box[nb++] = nbx;
		}
		for (int i = 0; i < nb; i++)				// (each box's colour: its average)
		{
			unsigned long long sr = 0, sg = 0, sb = 0, cnt = 0;
			for (int r = box[i].lo[0]; r <= box[i].hi[0]; r++) for (int g = box[i].lo[1]; g <= box[i].hi[1]; g++) for (int bb = box[i].lo[2]; bb <= box[i].hi[2]; bb++)
			{ unsigned k = hist[r << 10 | g << 5 | bb]; sr += (unsigned long long) k * (r * 8 + 4); sg += (unsigned long long) k * (g * 8 + 4); sb += (unsigned long long) k * (bb * 8 + 4); cnt += k; }
			if (!cnt) cnt = 1;
			pal[i] = (unsigned) (sr / cnt) << 16 | (unsigned) (sg / cnt) << 8 | (unsigned) (sb / cnt);
		}
		np = nb;
		short *map = new short[32768];
		for (int i = 0; i < 32768; i++) map[i] = -1;
		for (unsigned i = 0; i < n; i++)
		{
			unsigned c = px[i];
			if ((c >> 24) < 128) { idx[i] = (unsigned char) np; continue; }
			unsigned k = ((c >> 19) & 31) << 10 | ((c >> 11) & 31) << 5 | ((c >> 3) & 31);
			if (map[k] < 0)
			{
				int r = (int) (c >> 16 & 255), g = (int) (c >> 8 & 255), b = (int) (c & 255), bi = 0; long long bd = -1;
				for (int p2 = 0; p2 < np; p2++)
				{
					int dr = r - (int) (pal[p2] >> 16 & 255), dg = g - (int) (pal[p2] >> 8 & 255), db = b - (int) (pal[p2] & 255);
					long long d = (long long) dr * dr * 3 + dg * dg * 4 + db * db * 2;
					if (bd < 0 || d < bd) { bd = d; bi = p2; }
				}
				map[k] = (short) bi;
			}
			idx[i] = (unsigned char) map[k];
		}
		delete[] map; delete[] hist;
	}
	int total = np + (clear ? 1 : 0), bitsPal = 1;
	while ((1 << bitsPal) < total) bitsPal++;
	int minCode = bitsPal < 2 ? 2 : bitsPal;
	Buf o;
	o.put ("GIF89a", 6);
	o.le16 ((unsigned) w); o.le16 ((unsigned) h);
	o.put ((unsigned char) (0x80 | (bitsPal - 1) << 4 | (bitsPal - 1))); o.put (0); o.put (0);
	for (int i = 0; i < (1 << bitsPal); i++) { unsigned c = i < np ? pal[i] : 0; o.put ((unsigned char) (c >> 16)); o.put ((unsigned char) (c >> 8)); o.put ((unsigned char) c); }
	if (clear) { o.put (0x21); o.put (0xF9); o.put (4); o.put (1); o.le16 (0); o.put ((unsigned char) np); o.put (0); }
	o.put (0x2C); o.le16 (0); o.le16 (0); o.le16 ((unsigned) w); o.le16 ((unsigned) h); o.put (0);
	o.put ((unsigned char) minCode);
	// LZW, into 255-byte sub-blocks
	unsigned char blk[256]; int bl = 0; unsigned acc3 = 0; int nbits = 0;
	auto flushBlk = [&] () { if (bl) { o.put ((unsigned char) bl); o.put (blk, (unsigned) bl); bl = 0; } };
	auto emit = [&] (unsigned code, int size) {
		acc3 |= code << nbits; nbits += size;
		while (nbits >= 8) { blk[bl++] = (unsigned char) acc3; acc3 >>= 8; nbits -= 8; if (bl == 255) flushBlk (); }
	};
	unsigned clr = 1u << minCode, eoi = clr + 1, next = clr + 2; int size = minCode + 1;
	enum { TS = 5003 };
	int *tk = new int[TS]; short *tc = new short[TS];
	for (int i = 0; i < TS; i++) tk[i] = -1;
	emit (clr, size);
	unsigned prefix = idx[0];
	for (unsigned i = 1; i < n; i++)
	{
		unsigned c = idx[i];
		int key = (int) (prefix << 8 | c);
		unsigned hh = (unsigned) key % TS;
		while (tk[hh] != -1 && tk[hh] != key) hh = (hh + 1) % TS;
		if (tk[hh] == key) { prefix = (unsigned) tc[hh]; continue; }
		emit (prefix, size);
		if (next >= (1u << size) && size < 12) size++;
		if (next < 4096) { tk[hh] = key; tc[hh] = (short) next++; }
		else { emit (clr, size); for (int k = 0; k < TS; k++) tk[k] = -1; next = clr + 2; size = minCode + 1; }
		prefix = c;
	}
	emit (prefix, size);
	if (next >= (1u << size) && size < 12) size++;
	emit (eoi, size);
	if (nbits > 0) { blk[bl++] = (unsigned char) acc3; if (bl == 255) flushBlk (); }
	flushBlk ();
	o.put (0);
	o.put (0x3B);
	delete[] tk; delete[] tc; delete[] idx;
	return o.take (outLen);
}

// ---- ZIP ------------------------------------------------------------------------------------------------
class ZipOut
{
public:
	ZipOut () : m_n (0) {}
	~ZipOut () { for (int i = 0; i < m_n; i++) delete[] m_e[i].name; }
	// An entry: deflated (compress) or stored.
	void add (const char *name, const void *data, unsigned len, bool compress)
	{
		if (m_n >= MAXE) return;
		E &e = m_e[m_n++];
		int nl = 0; while (name[nl]) nl++;
		e.name = new char[nl + 1]; for (int i = 0; i <= nl; i++) e.name[i] = name[i];
		e.nlen = nl; e.usize = len; e.crc = crc32 (0, (const unsigned char *) data, len); e.offset = m_o.n;
		unsigned char *z = 0; unsigned zn = 0;
		if (compress && len > 64) { z = deflate ((const unsigned char *) data, len, false, &zn); if (zn >= len) { delete[] z; z = 0; } }
		e.method = z ? 8 : 0; e.csize = z ? zn : len;
		m_o.le32 (0x04034B50); m_o.le16 (20); m_o.le16 (0); m_o.le16 (e.method); m_o.le16 (0); m_o.le16 (0x21);	// (1980-01-01)
		m_o.le32 (e.crc); m_o.le32 (e.csize); m_o.le32 (e.usize); m_o.le16 ((unsigned) nl); m_o.le16 (0);
		m_o.put (name, (unsigned) nl);
		if (z) { m_o.put (z, zn); delete[] z; } else m_o.put (data, len);
	}
	// The archive's bytes (new []): the central directory and its end written.
	unsigned char *finish (unsigned *len)
	{
		unsigned cd = m_o.n;
		for (int i = 0; i < m_n; i++)
		{
			const E &e = m_e[i];
			m_o.le32 (0x02014B50); m_o.le16 (20); m_o.le16 (20); m_o.le16 (0); m_o.le16 (e.method); m_o.le16 (0); m_o.le16 (0x21);
			m_o.le32 (e.crc); m_o.le32 (e.csize); m_o.le32 (e.usize); m_o.le16 ((unsigned) e.nlen);
			m_o.le16 (0); m_o.le16 (0); m_o.le16 (0); m_o.le16 (0); m_o.le32 (0); m_o.le32 (e.offset);
			m_o.put (e.name, (unsigned) e.nlen);
		}
		unsigned cdSize = m_o.n - cd;
		m_o.le32 (0x06054B50); m_o.le16 (0); m_o.le16 (0); m_o.le16 ((unsigned) m_n); m_o.le16 ((unsigned) m_n);
		m_o.le32 (cdSize); m_o.le32 (cd); m_o.le16 (0);
		return m_o.take (len);
	}
private:
	enum { MAXE = 256 };
	struct E { char *name; int nlen; unsigned crc, csize, usize, offset, method; };
	E m_e[MAXE]; int m_n;
	Buf m_o;
};

struct ZipEntry { const unsigned char *data; unsigned csize, usize, method; };
static unsigned rd16 (const unsigned char *p) { return (unsigned) p[0] | (unsigned) p[1] << 8; }
static unsigned rd32 (const unsigned char *p) { return rd16 (p) | rd16 (p + 2) << 16; }

// An entry of a ZIP archive by its name (its bytes: stored, or deflated -- method 8).
static bool zip_find (const unsigned char *z, unsigned n, const char *name, ZipEntry *e)
{
	if (n < 22) return false;
	unsigned eocd = n - 22, lim = n > 65557 ? n - 65557 : 0;
	while (eocd > lim && rd32 (z + eocd) != 0x06054B50) eocd--;
	if (rd32 (z + eocd) != 0x06054B50) return false;
	unsigned count = rd16 (z + eocd + 10), cd = rd32 (z + eocd + 16);
	int nl = 0; while (name[nl]) nl++;
	for (unsigned i = 0; i < count && cd + 46 <= n; i++)
	{
		const unsigned char *c = z + cd;
		if (rd32 (c) != 0x02014B50) return false;
		unsigned fnl = rd16 (c + 28), xl = rd16 (c + 30), cl = rd16 (c + 32), off = rd32 (c + 42);
		bool same = (int) fnl == nl && cd + 46 + fnl <= n;
		for (unsigned k = 0; same && k < fnl; k++) if (c[46 + k] != (unsigned char) name[k]) same = false;
		if (same && off + 30 <= n && rd32 (z + off) == 0x04034B50)
		{
			unsigned dataOff = off + 30 + rd16 (z + off + 26) + rd16 (z + off + 28);
			e->method = rd16 (c + 10); e->csize = rd32 (c + 20); e->usize = rd32 (c + 24);
			if (dataOff + e->csize > n) return false;
			e->data = z + dataOff;
			return true;
		}
		cd += 46 + fnl + xl + cl;
	}
	return false;
}

} // namespace pngsave

#endif
