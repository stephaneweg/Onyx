//
// Apps/media/covers.h -- the albums' covers: the picture inside a song (ID3 APIC, FLAC PICTURE) or the
// folder's (cover.jpg, folder.jpg...), decoded by a thread of its own (img/imgload.hpp: JPEG, PNG, BMP,
// GIF, WebP), cropped square and brought to BASE px; then each size asked for made once and kept
// (a small cache, the oldest dropped). An album without a picture gets one drawn from its name (two
// colours, shapes: always the same for the same album). Nothing is downloaded.
//
//   g_covers.draw (canvas, albumIndex, x, y, size, radius, bg);   (a placeholder until it is loaded)
//
#ifndef _media_covers_h
#define _media_covers_h

#include "lib.h"
#include "img/imgload.hpp"
#include "wtk/wtk.h"

namespace media {

using namespace wtk;

static inline unsigned hash_str (const char *s, unsigned h = 2166136261u) { for (; s && *s; s++) h = (h ^ (unsigned char) (*s >= 'A' && *s <= 'Z' ? *s + 32 : *s)) * 16777619u; return h; }

class Covers
{
public:
	enum { BASE = 320, NBASE = 96, NSCALED = 220, QN = 64 };
	Library *lib;
	void (*onLoaded) ();			// the window: something to redraw
	Covers () : lib (0), onLoaded (0), m_tick (0), m_lk (0), m_qn (0), m_tid (-1), m_quit (false), m_ev (-1)
	{ memset (m_base, 0, sizeof m_base); memset (m_sc, 0, sizeof m_sc); }

	void start () { m_ev = kapi_event_create (0, 0); m_tid = kapi_thread_create (thread_main, this, 256 * 1024, "covers"); }
	void quit () { m_quit = true; if (m_ev > 0) kapi_event_set (m_ev); if (m_tid > 0) kapi_thread_join (m_tid, 2000, 0); }
	// the library changed: the albums' numbers are new (the pictures are kept: by name)
	void set_library (Library *l) { kapi_lock (&m_lk); lib = l; m_qn = 0; kapi_unlock (&m_lk); }

	unsigned key_of (int album) const { const Album &a = lib->al[album]; return hash_str (a.artist, hash_str (a.title)) | 1; }

	// the cover of album, size x size at (x, y), its corners rounded over bg
	void draw (Canvas &cv, int album, int x, int y, int size, int radius, unsigned bg)
	{
		if (!lib || album < 0 || album >= lib->nal || size <= 0) return;
		unsigned key = key_of (album);
		const unsigned *px = scaled (key, album, size);
		if (px)
		{
			for (int j = 0; j < size; j++)
			{
				int yy = y + j; if (yy < 0 || yy >= cv.h) continue;
				unsigned *row = cv.px + (long) yy * cv.stride;
				for (int i = 0; i < size; i++) { int xx = x + i; if (xx >= 0 && xx < cv.w) row[xx] = px[j * size + i]; }
			}
		}
		if (radius > 0) round_corners (cv, x, y, size, size, radius, bg);
	}
	static void round_corners (Canvas &cv, int x, int y, int w, int h, int r, unsigned bg)
	{	// each corner: the square's corner less the quarter disc (a polygon: the corner, then the arc)
		if (r <= 0) return;
		const int N = 10;
		int cx[4] = { x + r, x + w - r, x + w - r, x + r }, cy[4] = { y + r, y + r, y + h - r, y + h - r };
		int ox[4] = { x, x + w, x + w, x }, oy[4] = { y, y, y + h, y + h };
		int a0[4] = { 180, 270, 0, 90 };
		for (int k = 0; k < 4; k++)
		{
			int pts[2 * (N + 2)], n = 0;
			pts[n++] = V (ox[k]); pts[n++] = V (oy[k]);
			for (int i = 0; i <= N; i++)
			{
				int a = a0[k] + i * 90 / N;
				pts[n++] = V (cx[k]) + V (r) * wk_cos (a) / 16384; pts[n++] = V (cy[k]) + V (r) * wk_sin (a) / 16384;
			}
			VPath p; p.poly (pts, N + 2); p.fill (cv, bg);
		}
	}
	// the cover's main colour (a page's band, now playing's backdrop)
	unsigned tone (int album)
	{
		const unsigned *px = scaled (key_of (album), album, 8);
		if (!px) return 0x303848;
		unsigned r = 0, g = 0, b = 0;
		for (int i = 0; i < 64; i++) { r += px[i] >> 16 & 255; g += px[i] >> 8 & 255; b += px[i] & 255; }
		return (r / 64) << 16 | (g / 64) << 8 | (b / 64);
	}

private:
	struct Base { unsigned key; unsigned *px; unsigned use; bool pending, drawn; };
	struct Scaled { unsigned key; int size; unsigned *px; unsigned use; };
	Base m_base[NBASE]; Scaled m_sc[NSCALED]; unsigned m_tick;
	volatile int m_lk;
	struct Req { unsigned key; char path[300]; u64 off; unsigned len; };
	Req m_q[QN]; int m_qn;
	int m_tid; volatile bool m_quit; int m_ev;

	Base *base_of (unsigned key) { for (int i = 0; i < NBASE; i++) if (m_base[i].key == key) return &m_base[i]; return 0; }
	Base *base_slot ()
	{
		int best = -1;
		for (int i = 0; i < NBASE; i++) { if (!m_base[i].key) return &m_base[i]; if (!m_base[i].pending && (best < 0 || m_base[i].use < m_base[best].use)) best = i; }
		if (best < 0) return 0;
		Base &b = m_base[best]; delete[] b.px; drop_scaled (b.key); memset (&b, 0, sizeof b); return &b;
	}
	void drop_scaled (unsigned key) { for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == key) { delete[] m_sc[i].px; memset (&m_sc[i], 0, sizeof m_sc[i]); } }

	const unsigned *scaled (unsigned key, int album, int size)
	{
		for (int i = 0; i < NSCALED; i++) if (m_sc[i].key == key && m_sc[i].size == size && m_sc[i].px) { m_sc[i].use = ++m_tick; return m_sc[i].px; }
		Base *b = base_of (key);
		if (!b)
		{
			b = base_slot (); if (!b) return 0;
			b->key = key; b->use = ++m_tick;
			const Album &a = lib->al[album];
			if (a.cover >= 0) { b->pending = true; ask (key, lib->s[a.cover]); }
			else { b->px = generated (a.title, a.artist); b->drawn = true; }
		}
		b->use = ++m_tick;
		if (!b->px) return 0;					// (loading)
		int k = -1; unsigned old = ~0u;
		for (int i = 0; i < NSCALED; i++) { if (!m_sc[i].px) { k = i; break; } if (m_sc[i].use < old) { old = m_sc[i].use; k = i; } }
		Scaled &s = m_sc[k]; delete[] s.px;
		s.key = key; s.size = size; s.use = ++m_tick; s.px = new unsigned[(unsigned) size * size];
		resample (b->px, BASE, s.px, size);
		return s.px;
	}
	void ask (unsigned key, const Song &x)
	{
		kapi_lock (&m_lk);
		if (m_qn < QN)
		{
			Req &r = m_q[m_qn++]; r.key = key;
			if (x.coverLen) { scopy (r.path, x.path, sizeof r.path); r.off = x.coverOff; r.len = x.coverLen; }
			else { scopy (r.path, x.folderCover, sizeof r.path); r.off = 0; r.len = 0; }
		}
		kapi_unlock (&m_lk);
		if (m_ev > 0) kapi_event_set (m_ev);
	}
	// src (n x n) -> dst (m x m): the area of each pixel averaged (smaller), else bilinear
	static void resample (const unsigned *src, int n, unsigned *dst, int m)
	{
		if (m <= n)
			for (int y = 0; y < m; y++)
			{
				int y0 = y * n / m, y1 = (y + 1) * n / m; if (y1 <= y0) y1 = y0 + 1;
				for (int x = 0; x < m; x++)
				{
					int x0 = x * n / m, x1 = (x + 1) * n / m; if (x1 <= x0) x1 = x0 + 1;
					unsigned r = 0, g = 0, b = 0, c = 0;
					for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { unsigned p = src[yy * n + xx]; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; c++; }
					dst[y * m + x] = (r / c) << 16 | (g / c) << 8 | (b / c);
				}
			}
		else
			for (int y = 0; y < m; y++)
				for (int x = 0; x < m; x++)
				{
					int fx = x * (n - 1) * 256 / (m - 1), fy = y * (n - 1) * 256 / (m - 1);
					int ix = fx >> 8, iy = fy >> 8, ax = fx & 255, ay = fy & 255;
					int ix1 = ix + 1 < n ? ix + 1 : ix, iy1 = iy + 1 < n ? iy + 1 : iy;
					unsigned p00 = src[iy * n + ix], p10 = src[iy * n + ix1], p01 = src[iy1 * n + ix], p11 = src[iy1 * n + ix1], out = 0;
					for (int sh = 0; sh <= 16; sh += 8)
					{
						int a = (int) (p00 >> sh & 255) * (256 - ax) + (int) (p10 >> sh & 255) * ax, b = (int) (p01 >> sh & 255) * (256 - ax) + (int) (p11 >> sh & 255) * ax;
						out |= (unsigned) ((a * (256 - ay) + b * ay) >> 16) << sh;
					}
					dst[y * m + x] = out;
				}
	}
	// a picture drawn from the album's name: two colours, a gradient, shapes
	static unsigned *generated (const char *title, const char *artist)
	{
		static const unsigned PAL[10][3] = { { 0x16284F, 0x28AAA0, 0xB4F0C8 }, { 0xF0E4CD, 0x28466E, 0xDC5A46 }, { 0x1E3C46, 0x5A96A0, 0xE6E6DC },
			{ 0xFAC85A, 0xC8503C, 0x3C2832 }, { 0x3C3C42, 0x969696, 0xE67828 }, { 0x141E3C, 0x3C5AAA, 0xF0C878 }, { 0xF5F0E1, 0x785A32, 0x281E14 },
			{ 0x5A1E46, 0xDC5A8C, 0xFAD2AA }, { 0xC8DCE6, 0x5A788C, 0xE68C5A }, { 0x1E1432, 0x7850C8, 0x78E6DC } };
		unsigned h = hash_str (artist, hash_str (title));
		const unsigned *c = PAL[h % 10];
		unsigned *px = new unsigned[BASE * BASE];
		Canvas cv; cv.adopt (px, BASE, BASE);
		for (int y = 0; y < BASE; y++) cv.fillRect (0, y, BASE, 1, wk_mix (c[0], wk_mix (c[0], c[1], 90), y * 256 / BASE));
		int style = (int) (h >> 8) % 4;
		unsigned r = h;
		auto rnd = [&r] (int m) { r = r * 1103515245u + 12345u; return (int) ((r >> 16) % (unsigned) m); };
		if (style == 0)
			for (int k = 0; k < 6; k++)
			{
				VPath p; int y0 = 70 + k * 30 + rnd (20), pts[2 * 17];
				for (int i = 0; i <= 16; i++) { pts[2 * i] = V (i * BASE / 16); pts[2 * i + 1] = V (y0) + wk_sin (i * 40 + k * 50 + rnd (30)) * 20 * 16 / 16384; }
				p.polyline (pts, 17, V (10)); p.fill (cv, k % 2 ? c[1] : c[2], 150);
			}
		else if (style == 1)
		{
			VPath a; a.circle (V (BASE / 2), V (BASE / 2 - 16), V (96)); a.fill (cv, c[1]);
			VPath b; b.circle (V (BASE / 2), V (BASE / 2 - 16), V (44)); b.fill (cv, c[2]);
			cv.fillRect (0, BASE * 3 / 4, BASE, BASE / 4, c[2]);
		}
		else if (style == 2)
			for (int k = -6; k < 12; k++)
			{
				int x = k * 40; int q[] = { V (x), 0, V (x + 18), 0, V (x + 18 + BASE), V (BASE), V (x + BASE), V (BASE) };
				VPath p; p.poly (q, 4); p.fill (cv, k % 3 ? c[1] : c[2], 200);
			}
		else
			for (int yy = 0; yy < 6; yy++)
				for (int xx = 0; xx < 6; xx++)
				{
					int cx = xx * BASE / 6 + BASE / 12, cy = yy * BASE / 6 + BASE / 12, rr = 10 + rnd (14);
					VPath p; if ((xx + yy + (int) h) % 3) p.circle (V (cx), V (cy), V (rr)); else p.rect (V (cx - rr), V (cy - rr), V (2 * rr), V (2 * rr));
					p.fill (cv, (xx + yy) % 3 ? c[1] : c[2]);
				}
		return px;
	}
	// ---- the loader's thread ----
	static int thread_main (void *p) { ((Covers *) p)->run (); return 0; }
	struct Done { Covers *c; unsigned key; unsigned *px; };
	static void loaded (void *ctx, long)
	{
		Done *d = (Done *) ctx;
		Base *b = d->c->base_of (d->key);
		if (b && b->pending) { b->pending = false; if (d->px) b->px = d->px; else { b->px = new unsigned[BASE * BASE]; memset (b->px, 0x50, sizeof (unsigned) * BASE * BASE); } d->px = 0; }
		delete[] d->px;
		if (d->c->onLoaded) d->c->onLoaded ();
		delete d;
	}
	void run ()
	{
		while (!m_quit)
		{
			Req r; bool have = false;
			kapi_lock (&m_lk);
			if (m_qn > 0) { r = m_q[0]; memmove (m_q, m_q + 1, sizeof (Req) * (m_qn - 1)); m_qn--; have = true; }
			kapi_unlock (&m_lk);
			if (!have) { kapi_event_wait (m_ev, 500); continue; }
			Done *d = new Done { this, r.key, load (r) };
			kapi_post (loaded, d, 0);
		}
	}
	static unsigned *load (const Req &r)
	{
		ImgFrames im; memset (&im, 0, sizeof im);
		bool ok = false;
		if (r.len)
		{
			if (r.len > (16u << 20)) return 0;
			Src s; if (!s.open (r.path) || !s.seek (r.off)) return 0;
			unsigned char *b = new unsigned char[r.len];
			ok = s.read (b, r.len) == r.len && img_load_mem (b, r.len, &im);
			delete[] b;
		}
		else ok = img_load (r.path, &im);
		if (!ok || im.n < 1 || im.w <= 0 || im.h <= 0) { img_free (&im); return 0; }
		// cropped to a square in the middle, brought to BASE
		int sq = im.w < im.h ? im.w : im.h, ox = (im.w - sq) / 2, oy = (im.h - sq) / 2;
		unsigned *px = new unsigned[BASE * BASE];
		for (int y = 0; y < BASE; y++)
		{
			int y0 = oy + y * sq / BASE, y1 = oy + (y + 1) * sq / BASE; if (y1 <= y0) y1 = y0 + 1;
			for (int x = 0; x < BASE; x++)
			{
				int x0 = ox + x * sq / BASE, x1 = ox + (x + 1) * sq / BASE; if (x1 <= x0) x1 = x0 + 1;
				unsigned rr = 0, g = 0, b = 0, c = 0;
				for (int yy = y0; yy < y1; yy += 1 + (y1 - y0) / 4) for (int xx = x0; xx < x1; xx += 1 + (x1 - x0) / 4)
				{ unsigned p = im.px[0][yy * im.w + xx]; unsigned a = p >> 24; if (a < 255) p = wk_mix (0xFFFFFF, p & 0xFFFFFF, (int) a); rr += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; c++; }
				px[y * BASE + x] = (rr / c) << 16 | (g / c) << 8 | (b / c);
			}
		}
		img_free (&im);
		return px;
	}
};

} // namespace media

#endif
