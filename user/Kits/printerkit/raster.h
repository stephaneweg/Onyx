//
// printerkit/raster.h -- a print job's pages as pixels, for a printer that takes no PDF (most home printers: they
// take PWG Raster, the format of IPP Everywhere / AirPrint): a page replayed (printerkit/job.h) into a
// 0x00RRGGBB buffer at the printer's resolution -- the glyphs by FreeType from the job's own fonts, the
// rectangles, the images scaled, the paths filled with smoothed edges --, then written as a PWG Raster page.
//
// The page is laid on the paper: turned a quarter when one is upright and the other lies, scaled to fit
// and centred when their sizes differ (a slide on A4); the same size: point for point.
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
#ifndef ONYX_PRINT_RASTER_H
#define ONYX_PRINT_RASTER_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include "printerkit/job.h"
#include "img/imgload.hpp"

namespace praster {

using pjob::Glyph;
using pjob::PathPt;

class Raster : public pjob::Sink
{
public:
	// the paper: its size in points as the printer feeds it, the resolution, grey or colour
	Raster (float paperW, float paperH, int dpi, bool gray)
		: m_px (0), m_bw (0), m_bh (0), m_rot (false), m_dpi (dpi), m_gray (gray), m_lib (0), m_cov (0)
	{
		m_outW = (int) (paperW * dpi / 72.0f + 0.5f); m_outH = (int) (paperH * dpi / 72.0f + 0.5f);
		for (int i = 0; i < 64; i++) m_face[i] = 0;
		failed = false; m_ne = 0;
	}
	~Raster ()
	{
		for (int i = 0; i < 64; i++) if (m_face[i]) FT_Done_Face (m_face[i]);
		if (m_lib) FT_Done_FreeType (m_lib);
		delete[] m_px; delete[] m_cov;
	}
	int width () const { return m_outW; }
	int height () const { return m_outH; }
	int bytes_per_pixel () const { return m_gray ? 1 : 3; }
	int dpi () const { return m_dpi; }
	bool gray () const { return m_gray; }

	// the finished page's row y as the printer wants it (grey: a byte a pixel; colour: R, G, B)
	void row (int y, unsigned char *d) const
	{
		for (int x = 0; x < m_outW; x++)
		{
			unsigned c = m_rot ? m_px[(m_bh - 1 - x) * m_bw + y] : m_px[y * m_bw + x];
			if (m_gray) *d++ = (unsigned char) ((((c >> 16) & 255) * 77 + ((c >> 8) & 255) * 150 + (c & 255) * 29) >> 8);
			else { *d++ = (unsigned char) (c >> 16); *d++ = (unsigned char) (c >> 8); *d++ = (unsigned char) c; }
		}
	}
	// the page is drawn: the printer's back end takes its rows (false: stop the job)
	virtual bool page_ready () = 0;
	bool failed;

	// ---- pjob::Sink ----
	void font (int id, const unsigned char *ttf, unsigned len)
	{
		if (id < 0 || id >= 64 || m_face[id]) return;
		if (!m_lib && FT_Init_FreeType (&m_lib)) { m_lib = 0; return; }
		if (FT_New_Memory_Face (m_lib, ttf, (FT_Long) len, 0, &m_face[id])) m_face[id] = 0;
	}
	void begin_page (float w, float h)
	{
		if (w <= 0 || h <= 0) { w = m_outW * 72.0f / m_dpi; h = m_outH * 72.0f / m_dpi; }
		m_rot = (w > h * 1.02f) != (m_outW > m_outH * 1.02f) && !(w > h * 0.98f && w < h * 1.02f);
		int bw = m_rot ? m_outH : m_outW, bh = m_rot ? m_outW : m_outH;
		if (bw != m_bw || bh != m_bh) { delete[] m_px; delete[] m_cov; m_bw = bw; m_bh = bh; m_px = new unsigned[(size_t) bw * bh]; m_cov = new unsigned short[bw + 2]; }
		for (size_t i = 0, n = (size_t) bw * bh; i < n; i++) m_px[i] = 0x00FFFFFF;
		float s0 = m_dpi / 72.0f;
		float fw = bw / (w * s0), fh = bh / (h * s0);
		float fit = fw < fh ? fw : fh;
		m_s = fit > 0.99f && fit < 1.01f ? s0 : s0 * fit;		// (the same paper: no scaling at all)
		m_ox = (bw - w * m_s) / 2; m_oy = (bh - h * m_s) / 2;
		if (m_ox < 0.5f && m_ox > -0.5f) m_ox = 0;
		if (m_oy < 0.5f && m_oy > -0.5f) m_oy = 0;
	}
	void rect (float x, float y, float w, float h, unsigned rgb)
	{
		int x0 = rnd (m_ox + x * m_s), y0 = rnd (m_oy + y * m_s), x1 = rnd (m_ox + (x + w) * m_s), y1 = rnd (m_oy + (y + h) * m_s);
		if (x1 <= x0) x1 = x0 + 1;				// (a hairline keeps a pixel)
		if (y1 <= y0) y1 = y0 + 1;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > m_bw) x1 = m_bw;
		if (y1 > m_bh) y1 = m_bh;
		for (int yy = y0; yy < y1; yy++) { unsigned *p = m_px + (size_t) yy * m_bw; for (int xx = x0; xx < x1; xx++) p[xx] = rgb; }
	}
	void glyphs (int font, unsigned style, float size, unsigned rgb, const Glyph *g, int n)
	{
		FT_Face f = font >= 0 && font < 64 ? m_face[font] : 0;
		if (!f) return;
		float px = size * m_s;
		if (px < 1 || FT_Set_Char_Size (f, 0, (FT_F26Dot6) (px * 64 + 0.5f), 72, 72)) return;
		for (int i = 0; i < n; i++)
		{
			if (FT_Load_Glyph (f, g[i].gid, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP)) continue;
			FT_GlyphSlot sl = f->glyph;
			if (sl->format != FT_GLYPH_FORMAT_OUTLINE) continue;
			if (style & pjob::ST_BOLD) FT_Outline_Embolden (&sl->outline, (FT_Pos) (px * 64 * 0.03f));
			if (style & pjob::ST_ITALIC) { FT_Matrix m = { 0x10000, 0x35C2, 0, 0x10000 }; FT_Outline_Transform (&sl->outline, &m); }
			float X = m_ox + g[i].x * m_s, Y = m_oy + g[i].y * m_s;
			int ix = flr (X), iy = flr (Y);
			FT_Outline_Translate (&sl->outline, (FT_Pos) ((X - ix) * 64), -(FT_Pos) ((Y - iy) * 64));
			if (FT_Render_Glyph (sl, FT_RENDER_MODE_NORMAL)) continue;
			const FT_Bitmap &b = sl->bitmap;
			int bx = ix + sl->bitmap_left, by = iy - sl->bitmap_top;
			for (int r = 0; r < (int) b.rows; r++)
			{
				int yy = by + r;
				if (yy < 0 || yy >= m_bh) continue;
				const unsigned char *src = b.buffer + r * b.pitch;
				unsigned *d = m_px + (size_t) yy * m_bw;
				for (int c = 0; c < (int) b.width; c++)
				{
					int xx = bx + c;
					if (src[c] && xx >= 0 && xx < m_bw) d[xx] = mix (d[xx], rgb, src[c]);
				}
			}
		}
	}
	void image (float x, float y, float w, float h, int pw, int ph, int kind, const unsigned char *data, unsigned len)
	{
		unsigned *src = 0; unsigned char *raw = 0; ImgFrames im; im.n = 0;
		if (kind == pjob::IMG_JPEG)
		{
			if (!img_load_mem (data, len, &im) || im.n < 1) return;
			src = im.px[0]; pw = im.w; ph = im.h;
		}
		else
		{
			unsigned n = 0;
			raw = img_inflate (data, len, true, &n);
			if (!raw || n < (unsigned) pw * ph * 4) { delete[] raw; return; }
			src = (unsigned *) raw;					// (new[]: aligned for its words)
		}
		float X0 = m_ox + x * m_s, Y0 = m_oy + y * m_s, W = w * m_s, H = h * m_s;
		int x0 = flr (X0), y0 = flr (Y0), x1 = flr (X0 + W + 0.999f), y1 = flr (Y0 + H + 0.999f);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > m_bw) x1 = m_bw;
		if (y1 > m_bh) y1 = m_bh;
		float sx = pw / W, sy = ph / H;					// source pixels per device pixel
		bool box = sx > 1.5f || sy > 1.5f;
		for (int yy = y0; yy < y1; yy++)
		{
			unsigned *d = m_px + (size_t) yy * m_bw;
			float v0 = (yy - Y0) * sy, v1 = (yy + 1 - Y0) * sy;
			for (int xx = x0; xx < x1; xx++)
			{
				float u0 = (xx - X0) * sx, u1 = (xx + 1 - X0) * sx;
				unsigned a, r, g, b;
				if (box)
				{	// shrunk: the mean of the source pixels under the device pixel
					int ua = clampi (flr (u0), 0, pw - 1), ub = clampi (flr (u1 + 0.999f), ua + 1, pw);
					int va = clampi (flr (v0), 0, ph - 1), vb = clampi (flr (v1 + 0.999f), va + 1, ph);
					unsigned sa = 0, sr = 0, sg = 0, sb = 0, cnt = 0;
					for (int v = va; v < vb; v++) for (int u = ua; u < ub; u++)
					{
						unsigned c = src[(size_t) v * pw + u], ca = c >> 24;
						sa += ca; sr += ((c >> 16) & 255) * ca; sg += ((c >> 8) & 255) * ca; sb += (c & 255) * ca; cnt++;
					}
					if (!sa) continue;
					a = sa / cnt; r = sr / sa; g = sg / sa; b = sb / sa;
				}
				else
				{	// enlarged or the same size: between the four nearest
					float u = (u0 + u1) / 2 - 0.5f, v = (v0 + v1) / 2 - 0.5f;
					if (u < 0) u = 0;
					if (v < 0) v = 0;
					int ui = (int) u, vi = (int) v;
					if (ui > pw - 1) ui = pw - 1;
					if (vi > ph - 1) vi = ph - 1;
					int uj = ui + 1 < pw ? ui + 1 : ui, vj = vi + 1 < ph ? vi + 1 : vi;
					unsigned fu = (unsigned) ((u - ui) * 256), fv = (unsigned) ((v - vi) * 256);
					if (fu > 256) fu = 256;
					if (fv > 256) fv = 256;
					unsigned c00 = src[(size_t) vi * pw + ui], c10 = src[(size_t) vi * pw + uj], c01 = src[(size_t) vj * pw + ui], c11 = src[(size_t) vj * pw + uj];
					unsigned w00 = (256 - fu) * (256 - fv), w10 = fu * (256 - fv), w01 = (256 - fu) * fv, w11 = fu * fv;
					a = ((c00 >> 24) * w00 + (c10 >> 24) * w10 + (c01 >> 24) * w01 + (c11 >> 24) * w11) >> 16;
					r = (((c00 >> 16) & 255) * w00 + ((c10 >> 16) & 255) * w10 + ((c01 >> 16) & 255) * w01 + ((c11 >> 16) & 255) * w11) >> 16;
					g = (((c00 >> 8) & 255) * w00 + ((c10 >> 8) & 255) * w10 + ((c01 >> 8) & 255) * w01 + ((c11 >> 8) & 255) * w11) >> 16;
					b = ((c00 & 255) * w00 + (c10 & 255) * w10 + (c01 & 255) * w01 + (c11 & 255) * w11) >> 16;
				}
				if (a >= 255) d[xx] = r << 16 | g << 8 | b;
				else if (a) d[xx] = mix (d[xx], r << 16 | g << 8 | b, a);
			}
		}
		delete[] raw;
		if (im.n) img_free (&im);
	}
	void path (const PathPt *pt, int n, unsigned rgb, float strokeW)
	{
		// the path's segments in device pixels (the curves flattened)
		m_ne = 0;
		float cx = 0, cy = 0, sx = 0, sy = 0; bool open = false;
		float hw = strokeW * m_s / 2;
		if (strokeW > 0 && hw < 0.5f) hw = 0.5f;
		for (int i = 0; i < n; i++)
		{
			float x = m_ox + pt[i].x * m_s, y = m_oy + pt[i].y * m_s;
			switch (pt[i].verb)
			{
			case pjob::V_MOVE:
				if (open && strokeW <= 0) seg (cx, cy, sx, sy, 0);
				cx = sx = x; cy = sy = y; open = true; break;
			case pjob::V_LINE: seg (cx, cy, x, y, hw); cx = x; cy = y; break;
			case pjob::V_CURVE:
				if (i + 2 < n)
				{
					float x1 = x, y1 = y, x2 = m_ox + pt[i + 1].x * m_s, y2 = m_oy + pt[i + 1].y * m_s, x3 = m_ox + pt[i + 2].x * m_s, y3 = m_oy + pt[i + 2].y * m_s;
					float px = cx, py = cy;
					for (int k = 1; k <= 24; k++)
					{
						float t = k / 24.0f, u = 1 - t;
						float qx = u * u * u * cx + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
						float qy = u * u * u * cy + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
						seg (px, py, qx, qy, hw); px = qx; py = qy;
					}
					cx = x3; cy = y3; i += 2;
				}
				break;
			case pjob::V_CLOSE: seg (cx, cy, sx, sy, hw); cx = sx; cy = sy; break;
			}
		}
		if (open && strokeW <= 0 && (cx != sx || cy != sy)) seg (cx, cy, sx, sy, 0);
		fill_edges (rgb);
	}
	void end_page () { if (!page_ready ()) failed = true; }

private:
	unsigned *m_px; int m_bw, m_bh; bool m_rot;
	int m_outW, m_outH, m_dpi; bool m_gray;
	float m_s, m_ox, m_oy;
	FT_Library m_lib; FT_Face m_face[64];
	struct Edge { float x0, y0, x1, y1; int dir; };
	pngsave::Buf m_edges; int m_ne;
	unsigned short *m_cov;

	static int flr (float v) { int i = (int) v; return v < i ? i - 1 : i; }
	static int rnd (float v) { return flr (v + 0.5f); }
	static int clampi (int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
	static unsigned mix (unsigned d, unsigned s, unsigned a)
	{
		unsigned r = (((s >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a) + 127) / 255;
		unsigned g = (((s >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a) + 127) / 255;
		unsigned b = ((s & 255) * a + (d & 255) * (255 - a) + 127) / 255;
		return r << 16 | g << 8 | b;
	}
	void edge (float x0, float y0, float x1, float y1)
	{
		if (y0 == y1) return;
		Edge e; e.dir = 1;
		if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; e.dir = -1; }
		e.x0 = x0; e.y0 = y0; e.x1 = x1; e.y1 = y1;
		if (m_ne == 0) m_edges.n = 0;
		m_edges.put (&e, sizeof e); m_ne++;
	}
	// a segment of the path: an edge of the shape (hw 0), or the quad of a line 2 hw wide (square ends) --
	// the quads all turn the same way: where they overlap the non-zero rule keeps one layer
	void seg (float x0, float y0, float x1, float y1, float hw)
	{
		if (hw <= 0) { edge (x0, y0, x1, y1); return; }
		float dx = x1 - x0, dy = y1 - y0, l = dx * dx + dy * dy;
		if (l < 1e-6f) return;
		float inv = hw / fsqrt (l);
		dx *= inv; dy *= inv;
		float ax = x0 - dx - dy, ay = y0 - dy + dx, bx = x1 + dx - dy, by = y1 + dy + dx;
		float cx = x1 + dx + dy, cy = y1 + dy - dx, ex = x0 - dx + dy, ey = y0 - dy - dx;
		edge (ax, ay, bx, by); edge (bx, by, cx, cy); edge (cx, cy, ex, ey); edge (ex, ey, ax, ay);
	}
	static float fsqrt (float v) { float r = v > 1 ? v / 2 : 1; for (int i = 0; i < 20; i++) r = (r + v / r) / 2; return r; }
	// the edges' shape filled: four sample rows a pixel row, each span's ends weighed by what they cover
	void fill_edges (unsigned rgb)
	{
		if (!m_ne) return;
		const Edge *e = (const Edge *) m_edges.b;
		float ymin = e[0].y0, ymax = e[0].y1;
		for (int i = 1; i < m_ne; i++) { if (e[i].y0 < ymin) ymin = e[i].y0; if (e[i].y1 > ymax) ymax = e[i].y1; }
		int y0 = clampi (flr (ymin), 0, m_bh), y1 = clampi (flr (ymax) + 1, 0, m_bh);
		struct X { float x; int dir; };
		X *xs = new X[m_ne + 1];
		for (int y = y0; y < y1; y++)
		{
			for (int i = 0; i < m_bw + 2; i++) m_cov[i] = 0;
			int lo = m_bw, hi = -1;
			for (int sub = 0; sub < 4; sub++)
			{
				float sy = y + (sub + 0.5f) / 4;
				int nx = 0;
				for (int i = 0; i < m_ne; i++)
				{
					if (sy < e[i].y0 || sy >= e[i].y1) continue;
					X v; v.x = e[i].x0 + (e[i].x1 - e[i].x0) * (sy - e[i].y0) / (e[i].y1 - e[i].y0); v.dir = e[i].dir;
					int k = nx++;
					while (k > 0 && xs[k - 1].x > v.x) { xs[k] = xs[k - 1]; k--; }
					xs[k] = v;
				}
				int wind = 0;
				for (int i = 0; i + 1 < nx; i++)
				{
					wind += xs[i].dir;
					if (!wind) continue;
					float a = xs[i].x, b = xs[i + 1].x;
					if (a < 0) a = 0;
					if (b > m_bw) b = (float) m_bw;
					if (b <= a) continue;
					int ia = (int) a, ib = (int) b;
					if (ia < lo) lo = ia;
					if (ib > hi) hi = ib;
					if (ia == ib) m_cov[ia] += (unsigned short) ((b - a) * 64);
					else
					{
						m_cov[ia] += (unsigned short) ((ia + 1 - a) * 64);
						for (int x = ia + 1; x < ib; x++) m_cov[x] += 64;
						if (ib < m_bw) m_cov[ib] += (unsigned short) ((b - ib) * 64);
					}
				}
			}
			unsigned *d = m_px + (size_t) y * m_bw;
			if (hi >= m_bw) hi = m_bw - 1;
			for (int x = lo; x <= hi; x++)
			{
				unsigned c = m_cov[x];
				if (c >= 255) d[x] = rgb; else if (c) d[x] = mix (d[x], rgb, c);
			}
		}
		delete[] xs;
		m_ne = 0;
	}
};

// ---- PWG Raster (PWG 5102.4): "RaS2", then for each page a 1796-byte header and its rows, packed ---------
typedef bool (*pwg_write_fn) (void *ctx, const void *b, unsigned n);

static inline void pwg_be (unsigned char *p, unsigned v) { p[0] = (unsigned char) (v >> 24); p[1] = (unsigned char) (v >> 16); p[2] = (unsigned char) (v >> 8); p[3] = (unsigned char) v; }
static inline void pwg_str (unsigned char *p, const char *s) { for (int i = 0; i < 63 && s[i]; i++) p[i] = (unsigned char) s[i]; }

static inline bool pwg_begin (pwg_write_fn wr, void *ctx) { return wr (ctx, "RaS2", 4); }

// a page: its header (the paper's name as IPP calls it -- "iso_a4_210x297mm" --, quality 3 draft, 4 normal,
// 5 high), then the rows: a byte (the times the row repeats - 1), then runs -- 0..127: the next pixel n + 1
// times, 129..255: 257 - n pixels as they are
static bool pwg_page (const Raster &r, const char *media, int quality, int totalPages, pwg_write_fn wr, void *ctx)
{
	int w = r.width (), h = r.height (), bpp = r.bytes_per_pixel ();
	unsigned char hd[1796];
	for (unsigned i = 0; i < sizeof hd; i++) hd[i] = 0;
	pwg_str (hd, "PwgRaster");
	pwg_be (hd + 276, (unsigned) r.dpi ()); pwg_be (hd + 280, (unsigned) r.dpi ());		// HWResolution
	pwg_be (hd + 340, 1);										// NumCopies (the job's attribute rules)
	pwg_be (hd + 352, (unsigned) (w * 72.0f / r.dpi () + 0.5f)); pwg_be (hd + 356, (unsigned) (h * 72.0f / r.dpi () + 0.5f));	// PageSize
	pwg_be (hd + 372, (unsigned) w); pwg_be (hd + 376, (unsigned) h);
	pwg_be (hd + 384, 8); pwg_be (hd + 388, (unsigned) bpp * 8); pwg_be (hd + 392, (unsigned) (w * bpp));
	pwg_be (hd + 396, 0);										// ColorOrder: chunky
	pwg_be (hd + 400, r.gray () ? 18u : 19u);							// ColorSpace: sGray / sRGB
	pwg_be (hd + 420, (unsigned) bpp);								// NumColors
	pwg_be (hd + 452, (unsigned) totalPages);							// TotalPageCount
	pwg_be (hd + 456, 1); pwg_be (hd + 460, 1);							// CrossFeed / FeedTransform
	pwg_be (hd + 472, (unsigned) w); pwg_be (hd + 476, (unsigned) h);				// ImageBoxRight / Bottom
	pwg_be (hd + 480, 0x00FFFFFF);									// AlternatePrimary: white
	pwg_be (hd + 484, (unsigned) quality);								// PrintQuality
	pwg_str (hd + 1668, "auto"); pwg_str (hd + 1732, media ? media : "");
	if (!wr (ctx, hd, sizeof hd)) return false;

	unsigned char *cur = new unsigned char[(size_t) w * bpp], *nxt = new unsigned char[(size_t) w * bpp];
	pngsave::Buf o;
	bool ok = true;
	int y = 0;
	r.row (0, cur);
	while (y < h && ok)
	{
		int rep = 1; bool have = false;
		while (y + rep < h && rep < 256)
		{
			r.row (y + rep, nxt); have = true;
			if (__builtin_memcmp (cur, nxt, (size_t) w * bpp)) break;
			rep++; have = false;
		}
		o.put ((unsigned char) (rep - 1));
		for (int x = 0; x < w; )
		{
			const unsigned char *p = cur + (size_t) x * bpp;
			int run = 1;
			while (x + run < w && run < 128 && !__builtin_memcmp (p, p + (size_t) run * bpp, (size_t) bpp)) run++;
			if (run > 1 || x + 1 == w) { o.put ((unsigned char) (run - 1)); o.put (p, (unsigned) bpp); x += run; continue; }
			int lit = 1;				// pixels that differ from their neighbour
			while (x + lit < w && lit < 128 && (x + lit + 1 >= w || __builtin_memcmp (p + (size_t) lit * bpp, p + (size_t) (lit + 1) * bpp, (size_t) bpp))) lit++;
			if (lit == 1) { o.put ((unsigned char) 0); o.put (p, (unsigned) bpp); }
			else { o.put ((unsigned char) (257 - lit)); o.put (p, (unsigned) (lit * bpp)); }
			x += lit;
		}
		y += rep;
		if (have) { unsigned char *t = cur; cur = nxt; nxt = t; }
		else if (y < h) r.row (y, cur);
		if (o.n >= 60000 || y >= h) { ok = wr (ctx, o.b, o.n); o.n = 0; }
	}
	delete[] cur; delete[] nxt;
	return ok;
}

} // namespace praster

#endif
