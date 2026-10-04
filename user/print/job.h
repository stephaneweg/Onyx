//
// print/job.h -- a print job's file (SD:/var/spool/print/<id>.opj): the pages an app drew, recorded -- not
// yet a PDF, not yet a raster. printd replays it for the printer it goes to: as a PDF (pdf/pdfwrite.h) for
// the PDF printer, as pixels at the printer's resolution (print/raster.h) for a network printer. So a job
// is the same whatever prints it, and an app draws once.
//
// The file: "OPJ1", then records -- a byte (the kind), 32 bits (the payload's length), the payload; numbers
// little-endian, lengths in points (1/72 inch), y going down from the page's top:
//   FONT   id, a TrueType file's bytes            (before the first page that uses it)
//   PAGE   w, h                                   ... the page's drawing ... ENDPAGE
//   RECT   x, y, w, h, rgb
//   GLYPHS font, style, size, rgb, n, n x (x, baseline, glyph, character)
//   IMAGE  x, y, w, h, pixel width, pixel height, kind (deflated 0xAARRGGBB / a JPEG file), the data
//   PATH   rgb, stroke width (0: filled), n, n x (verb, x, y)
//   END    the number of pages
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
#ifndef ONYX_PRINT_JOB_H
#define ONYX_PRINT_JOB_H

#include "print/pio.h"
#include "img/pngsave.hpp"

namespace pjob {

using pngsave::Buf;

enum { R_FONT = 1, R_PAGE, R_RECT, R_GLYPHS, R_IMAGE, R_PATH, R_ENDPAGE, R_END };
enum { IMG_DEFLATE = 0, IMG_JPEG = 1 };
enum { V_MOVE = 0, V_LINE, V_CURVE, V_CLOSE };		// (a curve: three points, the verb on each)
enum { ST_BOLD = 1, ST_ITALIC = 2 };			// a glyph made bold / slanted (the font has no such face)

struct Glyph { float x, y; unsigned short gid, pad; unsigned uni; };
struct PathPt { unsigned verb; float x, y; };

static inline void put_f (Buf &b, float v)	{ b.put (&v, 4); }
static inline void put_u (Buf &b, unsigned v)	{ b.put (&v, 4); }
static inline float get_f (const unsigned char *p)	{ float v; __builtin_memcpy (&v, p, 4); return v; }
static inline unsigned get_u (const unsigned char *p)	{ unsigned v; __builtin_memcpy (&v, p, 4); return v; }

// ---- writing: the recorder ------------------------------------------------------------------------------
class Writer
{
public:
	Writer () : m_f (PIO_NONE), m_ok (false), m_inPage (false), m_pages (0), m_nfont (0), m_gn (0), m_gfont (-1) {}
	~Writer () { pio_close (m_f); }

	bool open (const char *path)
	{
		m_f = pio_create (path);
		m_ok = m_f != PIO_NONE && pio_write (m_f, "OPJ1", 4);
		return m_ok;
	}
	bool ok () const { return m_ok; }
	int pages () const { return m_pages; }

	// a TrueType file -> the font's number (the same bytes: the same font)
	int font (const unsigned char *ttf, unsigned len)
	{
		for (int i = 0; i < m_nfont; i++) if (m_fontKey[i] == ttf) return i;
		if (m_nfont >= MAXFONT || !ttf || len < 12) return -1;
		flush_glyphs ();
		unsigned char h[9]; h[0] = R_FONT; unsigned l = len + 4, id = (unsigned) m_nfont;
		__builtin_memcpy (h + 1, &l, 4); __builtin_memcpy (h + 5, &id, 4);
		// (a font met inside a page goes to the file at once, before the page's records: fonts are for the job)
		if (!pio_write (m_f, h, 9) || !pio_write (m_f, ttf, len)) m_ok = false;
		m_fontKey[m_nfont] = ttf;
		return m_nfont++;
	}
	void begin_page (float w, float h)
	{
		if (m_inPage) end_page ();
		m_inPage = true; m_b.n = 0;
		head (R_PAGE, 8); put_f (m_b, w); put_f (m_b, h);
	}
	void rect (float x, float y, float w, float h, unsigned rgb)
	{
		if (!m_inPage || w <= 0 || h <= 0) return;
		flush_glyphs ();
		head (R_RECT, 20); put_f (m_b, x); put_f (m_b, y); put_f (m_b, w); put_f (m_b, h); put_u (m_b, rgb & 0xFFFFFF);
	}
	void glyph (int font, float size, float x, float y, unsigned gid, unsigned uni, unsigned rgb, unsigned style)
	{
		if (!m_inPage || font < 0 || font >= m_nfont || size <= 0) return;
		rgb &= 0xFFFFFF;
		if (m_gn && (font != m_gfont || size != m_gsize || rgb != m_grgb || style != m_gstyle)) flush_glyphs ();
		m_gfont = font; m_gsize = size; m_grgb = rgb; m_gstyle = style;
		Glyph g; g.x = x; g.y = y; g.gid = (unsigned short) gid; g.pad = 0; g.uni = uni;
		m_g.put (&g, sizeof g); m_gn++;
	}
	void image (float x, float y, float w, float h, int pw, int ph, int kind, const unsigned char *data, unsigned len)
	{
		if (!m_inPage || !data || !len || pw <= 0 || ph <= 0 || w <= 0 || h <= 0) return;
		flush_glyphs ();
		head (R_IMAGE, 28 + len);
		put_f (m_b, x); put_f (m_b, y); put_f (m_b, w); put_f (m_b, h); put_u (m_b, (unsigned) pw); put_u (m_b, (unsigned) ph); put_u (m_b, (unsigned) kind);
		m_b.put (data, len);
	}
	void path (const PathPt *pt, int n, unsigned rgb, float strokeW)
	{
		if (!m_inPage || n < 2) return;
		flush_glyphs ();
		head (R_PATH, 12 + (unsigned) n * sizeof (PathPt));
		put_u (m_b, rgb & 0xFFFFFF); put_f (m_b, strokeW); put_u (m_b, (unsigned) n);
		m_b.put (pt, (unsigned) n * sizeof (PathPt));
	}
	void end_page ()
	{
		if (!m_inPage) return;
		flush_glyphs ();
		head (R_ENDPAGE, 0);
		if (!pio_write (m_f, m_b.b, m_b.n)) m_ok = false;
		m_b.n = 0; m_inPage = false; m_pages++;
	}
	// the page begun is not wanted after all (outside the range to print)
	void drop_page () { m_inPage = false; m_b.n = 0; m_gn = 0; m_g.n = 0; }
	bool close ()
	{
		if (m_inPage) end_page ();
		if (m_f == PIO_NONE) return false;
		unsigned char e[9]; e[0] = R_END; unsigned l = 4, n = (unsigned) m_pages;
		__builtin_memcpy (e + 1, &l, 4); __builtin_memcpy (e + 5, &n, 4);
		if (!pio_write (m_f, e, 9)) m_ok = false;
		pio_close (m_f); m_f = PIO_NONE;
		return m_ok;
	}

private:
	enum { MAXFONT = 64 };
	pio_file m_f; bool m_ok, m_inPage; int m_pages;
	const unsigned char *m_fontKey[MAXFONT]; int m_nfont;
	Buf m_b, m_g; unsigned m_gn; int m_gfont; float m_gsize; unsigned m_grgb, m_gstyle;

	void head (int kind, unsigned len) { m_b.put ((unsigned char) kind); put_u (m_b, len); }
	void flush_glyphs ()
	{
		if (!m_gn) return;
		head (R_GLYPHS, 20 + m_g.n);
		put_u (m_b, (unsigned) m_gfont); put_u (m_b, m_gstyle); put_f (m_b, m_gsize); put_u (m_b, m_grgb); put_u (m_b, m_gn);
		m_b.put (m_g.b, m_g.n);
		m_g.n = 0; m_gn = 0;
	}
};

// ---- reading: what a page is replayed into ----------------------------------------------------------------
struct Sink
{
	virtual ~Sink () {}
	virtual void font (int id, const unsigned char *ttf, unsigned len) = 0;		// (the bytes stay until the job's end)
	virtual void begin_page (float w, float h) = 0;
	virtual void rect (float x, float y, float w, float h, unsigned rgb) = 0;
	virtual void glyphs (int font, unsigned style, float size, unsigned rgb, const Glyph *g, int n) = 0;
	virtual void image (float x, float y, float w, float h, int pw, int ph, int kind, const unsigned char *data, unsigned len) = 0;
	virtual void path (const PathPt *pt, int n, unsigned rgb, float strokeW) = 0;
	virtual void end_page () = 0;
};

class Reader
{
public:
	Reader () : m_f (0), m_nfont (0), m_buf (0), m_cap (0) {}
	~Reader () { pio_close_in (m_f); for (int i = 0; i < m_nfont; i++) delete[] m_font[i]; delete[] m_buf; }

	bool open (const char *path)
	{
		m_f = pio_open (path);
		char m[4];
		return m_f && pio_read (m_f, m, 4) == 4 && m[0] == 'O' && m[1] == 'P' && m[2] == 'J' && m[3] == '1';
	}
	// the next page replayed into the sink -> 1, 0: no more pages, -1: a damaged file
	int page (Sink &s)
	{
		bool in = false;
		for (;;)
		{
			unsigned char h[5];
			if (pio_read (m_f, h, 5) != 5) return in ? -1 : 0;
			unsigned len = get_u (h + 1);
			if (len > 0x20000000u) return -1;
			if (h[0] == R_END) return 0;
			if (h[0] == R_FONT)
			{
				if (len < 4 || m_nfont >= 64) return -1;
				unsigned char *d = new unsigned char[len];
				if (pio_read (m_f, d, len) != (int) len) { delete[] d; return -1; }
				m_font[m_nfont++] = d;
				s.font ((int) get_u (d), d + 4, len - 4);
				continue;
			}
			if (len > m_cap) { delete[] m_buf; m_cap = len + len / 2 + 4096; m_buf = new unsigned char[m_cap]; }
			if (len && pio_read (m_f, m_buf, len) != (int) len) return -1;
			const unsigned char *p = m_buf;
			switch (h[0])
			{
			case R_PAGE:	if (len < 8) return -1; s.begin_page (get_f (p), get_f (p + 4)); in = true; break;
			case R_RECT:	if (len < 20) return -1; s.rect (get_f (p), get_f (p + 4), get_f (p + 8), get_f (p + 12), get_u (p + 16)); break;
			case R_GLYPHS:
			{
				if (len < 20) return -1;
				unsigned n = get_u (p + 16);
				if (n > (len - 20) / sizeof (Glyph)) return -1;
				s.glyphs ((int) get_u (p), get_u (p + 4), get_f (p + 8), get_u (p + 12), (const Glyph *) (p + 20), (int) n);
				break;
			}
			case R_IMAGE:
				if (len < 28) return -1;
				s.image (get_f (p), get_f (p + 4), get_f (p + 8), get_f (p + 12), (int) get_u (p + 16), (int) get_u (p + 20), (int) get_u (p + 24), p + 28, len - 28);
				break;
			case R_PATH:
			{
				if (len < 12) return -1;
				unsigned n = get_u (p + 8);
				if (n > (len - 12) / sizeof (PathPt)) return -1;
				s.path ((const PathPt *) (p + 12), (int) n, get_u (p), get_f (p + 4));
				break;
			}
			case R_ENDPAGE:	s.end_page (); return 1;
			default:	break;				// (a record of a later version: skipped)
			}
		}
	}

private:
	pio_in m_f;
	unsigned char *m_font[64]; int m_nfont;
	unsigned char *m_buf; unsigned m_cap;
};

} // namespace pjob

#endif
