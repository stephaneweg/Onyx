//
// pdf/pdfwrite.h -- writing PDF documents (PDF 1.7): pages of text, rectangles, lines and images; the TrueType
// fonts embedded as subsets (only the glyphs used: the document looks the same everywhere, its text can be
// selected and searched -- a ToUnicode map); links (the web, a page); bookmarks (the outline); the document's
// title and author. Header-only, integer-light C++ on new / delete; the streams deflated (img/pngsave.hpp).
// Used by Writer's and the Spreadsheet's File > Export as PDF (docs/pdf/README.md).
//
//   pdfw::Writer w;
//   int f = w.add_font (ttfBytes, ttfLen);            a TrueType file (its bytes kept by the caller until finish)
//   w.begin_page (595.3f, 841.9f);                     points (1/72 inch); y goes down from the page's top
//   w.fill_rect (x, y, w, h, 0xRRGGBB);
//   w.glyph (f, 12, x, baseline, gid, unicode, 0xRRGGBB);   a glyph of the font, its character (for the text)
//   w.image (argb, pw, ph, x, y, w, h, jpeg, quality); w.link_uri (x, y, w, h, "https://..."); w.link_page (...)
//   w.end_page ();
//   w.outline (level, "Title", page, y);  w.info ("Title", "Author", 0, "Writer (Onyx)");
//   unsigned n; unsigned char *pdf = w.finish (&n);    ... delete[] pdf
//
// The drawing calls are virtual: print/pdfprint.h's PrintWriter is a Writer whose pages go to a printer
// (an app's "Export as PDF" code prints as it is).
//
// ---------------------------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions
// of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------------------------
//
#ifndef ONYX_PDFWRITE_H
#define ONYX_PDFWRITE_H

#include "img/pngsave.hpp"

namespace pdfw {

using pngsave::Buf;

// ---- small helpers ---------------------------------------------------------------------------------------------
static inline unsigned rd16 (const unsigned char *p) { return (unsigned) p[0] << 8 | p[1]; }
static inline unsigned rd32 (const unsigned char *p) { return (unsigned) p[0] << 24 | (unsigned) p[1] << 16 | (unsigned) p[2] << 8 | p[3]; }
static inline int rds16 (const unsigned char *p) { return (short) rd16 (p); }
static inline void wr16 (unsigned char *p, unsigned v) { p[0] = (unsigned char) (v >> 8); p[1] = (unsigned char) v; }
static inline void wr32 (unsigned char *p, unsigned v) { p[0] = (unsigned char) (v >> 24); p[1] = (unsigned char) (v >> 16); p[2] = (unsigned char) (v >> 8); p[3] = (unsigned char) v; }
static inline int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }

// a number as PDF writes it: at most 3 decimals, no exponent
static void put_num (Buf &o, float v)
{
	if (v < 0) { o.put ('-'); v = -v; }
	long long m = (long long) (v * 1000.0f + 0.5f);
	long long ip = m / 1000; int fp = (int) (m % 1000);
	char t[24]; int n = 0;
	do { t[n++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	while (n) o.put ((unsigned char) t[--n]);
	if (fp) { o.put ('.'); char d[3] = { (char) ('0' + fp / 100), (char) ('0' + fp / 10 % 10), (char) ('0' + fp % 10) }; int k = 3; while (k && d[k - 1] == '0') k--; o.put (d, (unsigned) k); }
}
static void put_int (Buf &o, long long v) { if (v < 0) { o.put ('-'); v = -v; } char t[24]; int n = 0; do { t[n++] = (char) ('0' + v % 10); v /= 10; } while (v); while (n) o.put ((unsigned char) t[--n]); }
static void put_s (Buf &o, const char *s) { o.put (s, (unsigned) slen (s)); }
static void put_hex4 (Buf &o, unsigned v) { static const char *H = "0123456789ABCDEF"; for (int k = 12; k >= 0; k -= 4) o.put ((unsigned char) H[(v >> k) & 15]); }
static void put_rgb (Buf &o, unsigned c, const char *op)
{
	put_num (o, ((c >> 16) & 255) / 255.0f); o.put (' '); put_num (o, ((c >> 8) & 255) / 255.0f); o.put (' '); put_num (o, (c & 255) / 255.0f);
	o.put (' '); put_s (o, op); o.put ('\n');
}
// a text string: UTF-16BE with its mark (any language), as a hex string
static void put_text (Buf &o, const char *u8)
{
	put_s (o, "<FEFF");
	const unsigned char *s = (const unsigned char *) u8;
	while (s && *s)
	{
		unsigned c = *s++;
		if (c >= 0xC0) { int k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1; c &= 0x3F >> k; while (k-- && (*s & 0xC0) == 0x80) c = c << 6 | (*s++ & 0x3F); }
		if (c >= 0x10000) { c -= 0x10000; put_hex4 (o, 0xD800 | (c >> 10)); put_hex4 (o, 0xDC00 | (c & 0x3FF)); }
		else put_hex4 (o, c);
	}
	o.put ('>');
}

// ---- a TrueType font: its facts, the glyphs used --------------------------------------------------------------------
struct Font
{
	const unsigned char *ttf; unsigned len;
	unsigned tabOff[16]; unsigned tabLen[16];		// head hhea hmtx maxp loca glyf cvt fpgm prep OS/2 post name
	int upem, nglyphs, nmetrics, longLoca;
	int ascent, descent, capH, bbox[4], italic, flags;
	char psname[64];
	unsigned char *used;					// a glyph used (and the glyphs its composites take)
	unsigned *uni;						// a glyph's character (the first met)
	int objFont;						// (finish: its Type 0 font's object)
};
enum { T_HEAD, T_HHEA, T_HMTX, T_MAXP, T_LOCA, T_GLYF, T_CVT, T_FPGM, T_PREP, T_OS2, T_POST, T_NAME, T_N };
static const char *TAGS[T_N] = { "head", "hhea", "hmtx", "maxp", "loca", "glyf", "cvt ", "fpgm", "prep", "OS/2", "post", "name" };

struct Image { unsigned char *data; unsigned len; int w, h; bool jpeg; unsigned char *alpha; unsigned alen; int obj; };
struct Link { int page; float x, y, w, h; char *uri; int toPage; float toY; };
struct Mark { int level; char *title; int page; float y; };
struct Page { float w, h; unsigned char *content; unsigned clen; int nimg; int *imgs; };

class Writer
{
public:
	Writer () : m_fonts (0), m_nfont (0), m_pages (0), m_npage (0), m_imgs (0), m_nimg (0), m_links (0), m_nlink (0), m_marks (0), m_nmark (0),
		    m_inPage (false), m_curFont (-1), m_curSize (-1), m_curFill (0xFFFFFFFF), m_inText (false)
	{ for (int i = 0; i < 4; i++) m_info[i] = 0; }
	virtual ~Writer ()
	{
		for (int i = 0; i < m_nfont; i++) { delete[] m_fonts[i].used; delete[] m_fonts[i].uni; }
		delete[] m_fonts;
		for (int i = 0; i < m_npage; i++) { delete[] m_pages[i].content; delete[] m_pages[i].imgs; }
		delete[] m_pages;
		for (int i = 0; i < m_nimg; i++) { delete[] m_imgs[i].data; delete[] m_imgs[i].alpha; }
		delete[] m_imgs;
		for (int i = 0; i < m_nlink; i++) delete[] m_links[i].uri;
		delete[] m_links;
		for (int i = 0; i < m_nmark; i++) delete[] m_marks[i].title;
		delete[] m_marks;
		for (int i = 0; i < 4; i++) delete[] m_info[i];
	}

	// a TrueType font (its bytes stay the caller's until finish) -> its number, -1 not a TrueType font
	virtual int add_font (const unsigned char *ttf, unsigned len)
	{
		for (int i = 0; i < m_nfont; i++) if (m_fonts[i].ttf == ttf) return i;
		Font f; for (unsigned i = 0; i < sizeof f; i++) ((unsigned char *) &f)[i] = 0;
		if (!parse (f, ttf, len)) return -1;
		Font *n = new Font[m_nfont + 1];
		for (int i = 0; i < m_nfont; i++) n[i] = m_fonts[i];
		n[m_nfont] = f; delete[] m_fonts; m_fonts = n;
		return m_nfont++;
	}
	int glyph_count (int f) const { return f >= 0 && f < m_nfont ? m_fonts[f].nglyphs : 0; }

	void info (const char *title, const char *author, const char *subject, const char *creator)
	{
		const char *v[4] = { title, author, subject, creator };
		for (int i = 0; i < 4; i++) { delete[] m_info[i]; m_info[i] = 0; if (v[i] && v[i][0]) { int n = slen (v[i]); m_info[i] = new char[n + 1]; for (int k = 0; k <= n; k++) m_info[i][k] = v[i][k]; } }
	}

	virtual void begin_page (float w, float h)
	{
		if (m_inPage) end_page ();
		m_pw = w; m_ph = h; m_inPage = true; m_curFont = -1; m_curSize = -1; m_curFill = m_curStroke = 0xFFFFFFFF; m_inText = false; m_curRender = 0;
		m_c.n = 0; m_pageImgs.n = 0;
	}
	virtual void fill_rect (float x, float y, float w, float h, unsigned rgb)
	{
		if (w <= 0 || h <= 0) return;
		text_end ();
		fill (rgb);
		put_num (m_c, x); m_c.put (' '); put_num (m_c, m_ph - y - h); m_c.put (' '); put_num (m_c, w); m_c.put (' '); put_num (m_c, h); put_s (m_c, " re f\n");
	}
	// a glyph of font f at size (points), its pen at (x, baseline); bold: a made bold (thickened), italic: slanted
	virtual void glyph (int f, float size, float x, float y, unsigned gid, unsigned unicode, unsigned rgb, bool bold = false, bool italic = false)
	{
		if (f < 0 || f >= m_nfont) return;
		Font &F = m_fonts[f];
		if ((int) gid >= F.nglyphs) gid = 0;
		mark_used (F, gid);
		if (gid && !F.uni[gid]) F.uni[gid] = unicode;
		if (!m_inText) { put_s (m_c, "BT\n"); m_inText = true; m_curFont = -1; m_curRender = 0; }
		fill (rgb);
		if (f != m_curFont || size != m_curSize) { put_s (m_c, "/F"); put_int (m_c, f); m_c.put (' '); put_num (m_c, size); put_s (m_c, " Tf\n"); m_curFont = f; m_curSize = size; }
		int render = bold ? 2 : 0;
		if (render != m_curRender)
		{
			if (bold) { put_num (m_c, size * 0.03f); put_s (m_c, " w\n"); if (m_curStroke != rgb) { put_rgb (m_c, rgb, "RG"); m_curStroke = rgb; } }
			put_int (m_c, render); put_s (m_c, " Tr\n"); m_curRender = render;
		}
		else if (bold && m_curStroke != rgb) { put_rgb (m_c, rgb, "RG"); m_curStroke = rgb; }
		put_s (m_c, italic ? "1 0 0.21 1 " : "1 0 0 1 "); put_num (m_c, x); m_c.put (' '); put_num (m_c, m_ph - y); put_s (m_c, " Tm <"); put_hex4 (m_c, gid); put_s (m_c, "> Tj\n");
	}
	// an image (0xAARRGGBB) drawn at (x, y, w, h); jpeg: kept as a JPEG (a photo: smaller), its quality 1..100
	virtual void image (const unsigned *px, int pw, int ph, float x, float y, float w, float h, bool jpeg = false, int quality = 85)
	{
		if (!px || pw <= 0 || ph <= 0 || w <= 0 || h <= 0) return;
		text_end ();
		Image im; im.w = pw; im.h = ph; im.jpeg = false; im.alpha = 0; im.alen = 0; im.obj = 0;
		bool opaque = true;
		for (int i = 0; i < pw * ph; i++) if ((px[i] >> 24) != 255) { opaque = false; break; }
		if (jpeg && opaque) { im.data = pngsave::jpeg_encode (px, pw, ph, quality, &im.len); im.jpeg = im.data != 0; }
		if (!im.jpeg)
		{
			unsigned char *raw = new unsigned char[(size_t) pw * ph * 3];
			for (int i = 0; i < pw * ph; i++) { raw[i * 3] = (unsigned char) (px[i] >> 16); raw[i * 3 + 1] = (unsigned char) (px[i] >> 8); raw[i * 3 + 2] = (unsigned char) px[i]; }
			im.data = pngsave::deflate (raw, (unsigned) (pw * ph * 3), true, &im.len);
			delete[] raw;
		}
		if (!opaque)
		{
			unsigned char *a = new unsigned char[(size_t) pw * ph];
			for (int i = 0; i < pw * ph; i++) a[i] = (unsigned char) (px[i] >> 24);
			im.alpha = pngsave::deflate (a, (unsigned) (pw * ph), true, &im.alen);
			delete[] a;
		}
		Image *n = new Image[m_nimg + 1];
		for (int i = 0; i < m_nimg; i++) n[i] = m_imgs[i];
		n[m_nimg] = im; delete[] m_imgs; m_imgs = n;
		int k = m_nimg++;
		m_pageImgs.put (&k, sizeof k);
		put_s (m_c, "q "); put_num (m_c, w); put_s (m_c, " 0 0 "); put_num (m_c, h); m_c.put (' '); put_num (m_c, x); m_c.put (' '); put_num (m_c, m_ph - y - h);
		put_s (m_c, " cm /Im"); put_int (m_c, k); put_s (m_c, " Do Q\n");
	}
	// a JPEG file's bytes as they are (baseline, RGB or grey: pw x ph pixels), drawn at (x, y, w, h)
	void image_jpeg (const unsigned char *jpg, unsigned len, int pw, int ph, float x, float y, float w, float h)
	{
		if (!jpg || !len || pw <= 0 || ph <= 0 || w <= 0 || h <= 0) return;
		text_end ();
		Image im; im.w = pw; im.h = ph; im.jpeg = true; im.alpha = 0; im.alen = 0; im.obj = 0; im.len = len;
		im.data = new unsigned char[len];
		for (unsigned i = 0; i < len; i++) im.data[i] = jpg[i];
		Image *n = new Image[m_nimg + 1];
		for (int i = 0; i < m_nimg; i++) n[i] = m_imgs[i];
		n[m_nimg] = im; delete[] m_imgs; m_imgs = n;
		int k = m_nimg++;
		m_pageImgs.put (&k, sizeof k);
		put_s (m_c, "q "); put_num (m_c, w); put_s (m_c, " 0 0 "); put_num (m_c, h); m_c.put (' '); put_num (m_c, x); m_c.put (' '); put_num (m_c, m_ph - y - h);
		put_s (m_c, " cm /Im"); put_int (m_c, k); put_s (m_c, " Do Q\n");
	}
	// a path: n points, each with its verb (0 move to, 1 line to, 2 a Bezier curve's three points, 3 close);
	// filled (strokeW 0: the non-zero rule) or stroked with a line strokeW wide (square ends, mitred corners)
	void path (const float *xy, const unsigned char *verb, int n, unsigned rgb, float strokeW)
	{
		if (n < 2) return;
		text_end ();
		if (strokeW > 0)
		{
			if (m_curStroke != rgb) { put_rgb (m_c, rgb, "RG"); m_curStroke = rgb; }
			put_num (m_c, strokeW); put_s (m_c, " w 2 J\n");
		}
		else fill (rgb);
		for (int i = 0; i < n; i++)
		{
			if (verb[i] == 3) { put_s (m_c, "h\n"); continue; }
			if (verb[i] == 2 && i + 2 < n)
			{
				for (int k = 0; k < 3; k++) { put_num (m_c, xy[(i + k) * 2]); m_c.put (' '); put_num (m_c, m_ph - xy[(i + k) * 2 + 1]); m_c.put (' '); }
				put_s (m_c, "c\n"); i += 2; continue;
			}
			put_num (m_c, xy[i * 2]); m_c.put (' '); put_num (m_c, m_ph - xy[i * 2 + 1]); put_s (m_c, verb[i] == 0 ? " m\n" : " l\n");
		}
		put_s (m_c, strokeW > 0 ? "S\n" : "f\n");
	}
	void link_uri (float x, float y, float w, float h, const char *uri) { add_link (x, y, w, h, uri, -1, 0); }
	void link_page (float x, float y, float w, float h, int page, float ty) { add_link (x, y, w, h, 0, page, ty); }
	virtual void end_page ()
	{
		if (!m_inPage) return;
		text_end ();
		Page p; p.w = m_pw; p.h = m_ph;
		p.content = pngsave::deflate (m_c.b ? m_c.b : (unsigned char *) "", m_c.n, true, &p.clen);
		p.nimg = (int) (m_pageImgs.n / sizeof (int)); p.imgs = new int[p.nimg ? p.nimg : 1];
		for (int i = 0; i < p.nimg; i++) p.imgs[i] = ((int *) m_pageImgs.b)[i];
		Page *n = new Page[m_npage + 1];
		for (int i = 0; i < m_npage; i++) n[i] = m_pages[i];
		n[m_npage] = p; delete[] m_pages; m_pages = n; m_npage++;
		m_inPage = false;
	}
	int pages () const { return m_npage + (m_inPage ? 1 : 0); }
	// the page begun will be kept (a Writer that prints only some pages says no: its drawing can be skipped)
	virtual bool page_wanted () const { return true; }
	// a bookmark: level 0 (a chapter), 1 (its sections)...; page from 0; y: from the page's top (points)
	void outline (int level, const char *title, int page, float y)
	{
		Mark m; m.level = level < 0 ? 0 : level; m.page = page; m.y = y;
		int n = slen (title); m.title = new char[n + 1]; for (int k = 0; k <= n; k++) m.title[k] = title[k];
		Mark *a = new Mark[m_nmark + 1];
		for (int i = 0; i < m_nmark; i++) a[i] = m_marks[i];
		a[m_nmark] = m; delete[] m_marks; m_marks = a; m_nmark++;
	}

	// the whole document -> its bytes (new[]; 0: nothing)
	unsigned char *finish (unsigned *len)
	{
		if (m_inPage) end_page ();
		if (!m_npage) begin_page (595.28f, 841.89f), end_page ();
		Buf o;
		put_s (o, "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
		// the objects' numbers: 1 catalog, 2 pages, 3 info; then the fonts (5 objects each), the images (2), the pages (2), the
		// links, the outline
		int next = 4;
		for (int i = 0; i < m_nfont; i++) { m_fonts[i].objFont = next; next += 5; }
		for (int i = 0; i < m_nimg; i++) { m_imgs[i].obj = next; next += 2; }
		int pageObj0 = next; next += 2 * m_npage;
		int linkObj0 = next; next += m_nlink;
		int olRoot = m_nmark ? next : 0; int olObj0 = next + 1; if (m_nmark) next += 1 + m_nmark;
		int nobj = next;
		unsigned *off = new unsigned[nobj]; for (int i = 0; i < nobj; i++) off[i] = 0;
		// the catalog
		off[1] = o.n; put_s (o, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R");
		if (m_nmark) { put_s (o, " /Outlines "); put_int (o, olRoot); put_s (o, " 0 R /PageMode /UseOutlines"); }
		put_s (o, " >>\nendobj\n");
		off[2] = o.n; put_s (o, "2 0 obj\n<< /Type /Pages /Count "); put_int (o, m_npage); put_s (o, " /Kids [");
		for (int i = 0; i < m_npage; i++) { put_int (o, pageObj0 + 2 * i); put_s (o, " 0 R "); }
		put_s (o, "] >>\nendobj\n");
		off[3] = o.n; put_s (o, "3 0 obj\n<< /Producer (Onyx pdfwrite)");
		static const char *K[4] = { " /Title ", " /Author ", " /Subject ", " /Creator " };
		for (int i = 0; i < 4; i++) if (m_info[i]) { put_s (o, K[i]); put_text (o, m_info[i]); }
		put_s (o, " >>\nendobj\n");
		// the fonts
		for (int i = 0; i < m_nfont; i++) write_font (o, m_fonts[i], i, off);
		// the images
		for (int i = 0; i < m_nimg; i++)
		{
			Image &im = m_imgs[i]; int ob = im.obj;
			off[ob] = o.n; put_int (o, ob); put_s (o, " 0 obj\n<< /Type /XObject /Subtype /Image /Width "); put_int (o, im.w); put_s (o, " /Height "); put_int (o, im.h);
			put_s (o, " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter "); put_s (o, im.jpeg ? "/DCTDecode" : "/FlateDecode");
			if (im.alpha) { put_s (o, " /SMask "); put_int (o, ob + 1); put_s (o, " 0 R"); }
			put_s (o, " /Length "); put_int (o, im.len); put_s (o, " >>\nstream\n"); o.put (im.data, im.len); put_s (o, "\nendstream\nendobj\n");
			off[ob + 1] = o.n; put_int (o, ob + 1); put_s (o, " 0 obj\n");
			if (im.alpha)
			{
				put_s (o, "<< /Type /XObject /Subtype /Image /Width "); put_int (o, im.w); put_s (o, " /Height "); put_int (o, im.h);
				put_s (o, " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length "); put_int (o, im.alen); put_s (o, " >>\nstream\n");
				o.put (im.alpha, im.alen); put_s (o, "\nendstream\nendobj\n");
			}
			else put_s (o, "null\nendobj\n");
		}
		// the pages
		for (int i = 0; i < m_npage; i++)
		{
			Page &p = m_pages[i]; int ob = pageObj0 + 2 * i;
			off[ob] = o.n; put_int (o, ob); put_s (o, " 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "); put_num (o, p.w); o.put (' '); put_num (o, p.h);
			put_s (o, "] /Contents "); put_int (o, ob + 1); put_s (o, " 0 R /Resources << /ProcSet [/PDF /Text /ImageB /ImageC]");
			if (m_nfont) { put_s (o, " /Font <<"); for (int f = 0; f < m_nfont; f++) { put_s (o, " /F"); put_int (o, f); o.put (' '); put_int (o, m_fonts[f].objFont); put_s (o, " 0 R"); } put_s (o, " >>"); }
			if (p.nimg) { put_s (o, " /XObject <<"); for (int k = 0; k < p.nimg; k++) { put_s (o, " /Im"); put_int (o, p.imgs[k]); o.put (' '); put_int (o, m_imgs[p.imgs[k]].obj); put_s (o, " 0 R"); } put_s (o, " >>"); }
			put_s (o, " >>");
			bool any = false;
			for (int k = 0; k < m_nlink; k++) if (m_links[k].page == i) { if (!any) put_s (o, " /Annots ["); any = true; put_int (o, linkObj0 + k); put_s (o, " 0 R "); }
			if (any) o.put (']');
			put_s (o, " >>\nendobj\n");
			off[ob + 1] = o.n; put_int (o, ob + 1); put_s (o, " 0 obj\n<< /Filter /FlateDecode /Length "); put_int (o, p.clen); put_s (o, " >>\nstream\n");
			o.put (p.content, p.clen); put_s (o, "\nendstream\nendobj\n");
		}
		// the links
		for (int k = 0; k < m_nlink; k++)
		{
			Link &l = m_links[k]; int ob = linkObj0 + k; float ph = m_pages[l.page].h;
			off[ob] = o.n; put_int (o, ob); put_s (o, " 0 obj\n<< /Type /Annot /Subtype /Link /Border [0 0 0] /Rect [");
			put_num (o, l.x); o.put (' '); put_num (o, ph - l.y - l.h); o.put (' '); put_num (o, l.x + l.w); o.put (' '); put_num (o, ph - l.y); o.put (']');
			if (l.uri) { put_s (o, " /A << /S /URI /URI "); put_lit (o, l.uri); put_s (o, " >>"); }
			else if (l.toPage >= 0 && l.toPage < m_npage) dest (o, " /Dest ", pageObj0, l.toPage, l.toY);
			put_s (o, " >>\nendobj\n");
		}
		// the outline: its entries' parents, siblings, children from their levels
		if (m_nmark)
		{
			int *par = new int[m_nmark], *prev = new int[m_nmark], *nxt = new int[m_nmark], *first = new int[m_nmark], *last = new int[m_nmark], *cnt = new int[m_nmark];
			int stack[16]; int sd = 0; int lastTop = -1;
			for (int i = 0; i < m_nmark; i++) { par[i] = -1; prev[i] = nxt[i] = first[i] = last[i] = -1; cnt[i] = 0; }
			int rootFirst = -1, rootLast = -1, rootCnt = 0;
			for (int i = 0; i < m_nmark; i++)
			{
				int lv = m_marks[i].level; if (lv > sd) lv = sd; if (lv > 15) lv = 15;
				sd = lv;
				int p = lv ? stack[lv - 1] : -1;
				par[i] = p;
				int sib = p >= 0 ? last[p] : rootLast;
				prev[i] = sib; if (sib >= 0) nxt[sib] = i;
				if (p >= 0) { if (first[p] < 0) first[p] = i; last[p] = i; } else { if (rootFirst < 0) rootFirst = i; rootLast = i; rootCnt++; }
				for (int q = p; q >= 0; q = par[q]) cnt[q]++;
				stack[lv] = i; sd = lv + 1; lastTop = i;
			}
			(void) lastTop;
			off[olRoot] = o.n; put_int (o, olRoot); put_s (o, " 0 obj\n<< /Type /Outlines /First "); put_int (o, olObj0 + rootFirst); put_s (o, " 0 R /Last ");
			put_int (o, olObj0 + rootLast); put_s (o, " 0 R /Count "); put_int (o, rootCnt); put_s (o, " >>\nendobj\n");
			for (int i = 0; i < m_nmark; i++)
			{
				int ob = olObj0 + i;
				off[ob] = o.n; put_int (o, ob); put_s (o, " 0 obj\n<< /Title "); put_text (o, m_marks[i].title);
				put_s (o, " /Parent "); put_int (o, par[i] >= 0 ? olObj0 + par[i] : olRoot); put_s (o, " 0 R");
				if (prev[i] >= 0) { put_s (o, " /Prev "); put_int (o, olObj0 + prev[i]); put_s (o, " 0 R"); }
				if (nxt[i] >= 0) { put_s (o, " /Next "); put_int (o, olObj0 + nxt[i]); put_s (o, " 0 R"); }
				if (first[i] >= 0) { put_s (o, " /First "); put_int (o, olObj0 + first[i]); put_s (o, " 0 R /Last "); put_int (o, olObj0 + last[i]); put_s (o, " 0 R /Count -"); put_int (o, cnt[i]); }
				if (m_marks[i].page >= 0 && m_marks[i].page < m_npage) dest (o, " /Dest ", pageObj0, m_marks[i].page, m_marks[i].y);
				put_s (o, " >>\nendobj\n");
			}
			delete[] par; delete[] prev; delete[] nxt; delete[] first; delete[] last; delete[] cnt;
		}
		// the cross-reference table, the trailer
		unsigned xref = o.n;
		put_s (o, "xref\n0 "); put_int (o, nobj); put_s (o, "\n0000000000 65535 f \n");
		for (int i = 1; i < nobj; i++)
		{
			char t[24]; unsigned v = off[i];
			for (int k = 9; k >= 0; k--) { t[k] = (char) ('0' + v % 10); v /= 10; }
			o.put (t, 10); put_s (o, off[i] ? " 00000 n \n" : " 00000 f \n");
		}
		put_s (o, "trailer\n<< /Size "); put_int (o, nobj); put_s (o, " /Root 1 0 R /Info 3 0 R /ID [<");
		unsigned h = pngsave::crc32 (0xFFFFFFFFu, o.b, o.n) ^ 0xFFFFFFFFu;
		for (int k = 0; k < 4; k++) { put_hex4 (o, h & 0xFFFF); put_hex4 (o, (h >> 16) ^ (unsigned) k * 0x9E37); }
		put_s (o, "> <");
		for (int k = 0; k < 4; k++) { put_hex4 (o, h & 0xFFFF); put_hex4 (o, (h >> 16) ^ (unsigned) k * 0x9E37); }
		put_s (o, ">] >>\nstartxref\n"); put_int (o, xref); put_s (o, "\n%%EOF\n");
		delete[] off;
		return o.take (len);
	}

private:
	Font *m_fonts; int m_nfont;
	Page *m_pages; int m_npage;
	Image *m_imgs; int m_nimg;
	Link *m_links; int m_nlink;
	Mark *m_marks; int m_nmark;
	char *m_info[4];
	bool m_inPage; float m_pw, m_ph;
	Buf m_c, m_pageImgs;
	int m_curFont; float m_curSize; unsigned m_curFill, m_curStroke; bool m_inText; int m_curRender;

	void fill (unsigned rgb) { if (rgb != m_curFill) { put_rgb (m_c, rgb, "rg"); m_curFill = rgb; } }
	void text_end () { if (m_inText) { if (m_curRender) put_s (m_c, "0 Tr\n"); put_s (m_c, "ET\n"); m_inText = false; m_curRender = 0; } }
	void add_link (float x, float y, float w, float h, const char *uri, int page, float ty)
	{
		if (!m_inPage) return;
		Link l; l.page = m_npage; l.x = x; l.y = y; l.w = w; l.h = h; l.toPage = page; l.toY = ty; l.uri = 0;
		if (uri) { int n = slen (uri); l.uri = new char[n + 1]; for (int k = 0; k <= n; k++) l.uri[k] = uri[k]; }
		Link *a = new Link[m_nlink + 1];
		for (int i = 0; i < m_nlink; i++) a[i] = m_links[i];
		a[m_nlink] = l; delete[] m_links; m_links = a; m_nlink++;
	}
	void dest (Buf &o, const char *key, int pageObj0, int page, float y)
	{
		put_s (o, key); o.put ('['); put_int (o, pageObj0 + 2 * page); put_s (o, " 0 R /XYZ 0 "); put_num (o, m_pages[page].h - y); put_s (o, " 0]");
	}
	static void put_lit (Buf &o, const char *s)
	{
		o.put ('(');
		for (; *s; s++) { if (*s == '(' || *s == ')' || *s == '\\') o.put ('\\'); o.put ((unsigned char) *s); }
		o.put (')');
	}

	// ---- TrueType ----
	static bool parse (Font &f, const unsigned char *t, unsigned len)
	{
		if (len < 12) return false;
		unsigned ver = rd32 (t);
		if (ver != 0x00010000 && ver != 0x74727565) return false;		// (TrueType outlines: not CFF)
		int n = (int) rd16 (t + 4);
		if (12 + n * 16 > (int) len) return false;
		for (int i = 0; i < n; i++)
		{
			const unsigned char *e = t + 12 + i * 16;
			for (int k = 0; k < T_N; k++)
				if (e[0] == TAGS[k][0] && e[1] == TAGS[k][1] && e[2] == TAGS[k][2] && e[3] == TAGS[k][3])
				{
					unsigned o = rd32 (e + 8), l = rd32 (e + 12);
					if (o <= len && l <= len - o) { f.tabOff[k] = o; f.tabLen[k] = l; }
				}
		}
		for (int k = 0; k <= T_GLYF; k++) if (!f.tabLen[k]) return false;
		f.ttf = t; f.len = len;
		const unsigned char *head = t + f.tabOff[T_HEAD], *hhea = t + f.tabOff[T_HHEA], *maxp = t + f.tabOff[T_MAXP];
		f.upem = (int) rd16 (head + 18); if (f.upem <= 0) f.upem = 1000;
		f.bbox[0] = rds16 (head + 36); f.bbox[1] = rds16 (head + 38); f.bbox[2] = rds16 (head + 40); f.bbox[3] = rds16 (head + 42);
		f.longLoca = rds16 (head + 50);
		f.nglyphs = (int) rd16 (maxp + 4);
		f.nmetrics = (int) rd16 (hhea + 34); if (f.nmetrics < 1) f.nmetrics = 1;
		f.ascent = rds16 (hhea + 4); f.descent = rds16 (hhea + 6);
		f.capH = f.ascent * 7 / 10;
		if (f.tabLen[T_OS2] >= 90) { const unsigned char *os2 = t + f.tabOff[T_OS2]; if (rd16 (os2) >= 2) f.capH = rds16 (os2 + 88); }
		f.italic = 0; f.flags = 32;						// (nonsymbolic)
		if (f.tabLen[T_POST] >= 16) { const unsigned char *post = t + f.tabOff[T_POST]; f.italic = (int) ((int) rd32 (post + 4) >> 16); if (rd32 (post + 12)) f.flags |= 1; }
		if (f.italic) f.flags |= 64;
		// the PostScript name (name 6), Windows (UTF-16BE) or Mac (bytes)
		f.psname[0] = 0;
		if (f.tabLen[T_NAME] >= 6)
		{
			const unsigned char *nm = t + f.tabOff[T_NAME]; unsigned cnt = rd16 (nm + 2), so = rd16 (nm + 4);
			for (unsigned i = 0; i < cnt && 6 + i * 12 + 12 <= f.tabLen[T_NAME]; i++)
			{
				const unsigned char *r = nm + 6 + i * 12;
				if (rd16 (r + 6) != 6) continue;
				unsigned pid = rd16 (r), l = rd16 (r + 8), o = rd16 (r + 10);
				if (so + o + l > f.tabLen[T_NAME]) continue;
				const unsigned char *s = nm + so + o; int k = 0;
				if (pid == 3 || pid == 0) for (unsigned j = 1; j < l && k < 60; j += 2) { unsigned char c = s[j]; if (c > 32 && c < 127 && c != '/' && c != '(' && c != ')' && c != '[' && c != ']' && c != '<' && c != '>' && c != '%') f.psname[k++] = (char) c; }
				else for (unsigned j = 0; j < l && k < 60; j++) { unsigned char c = s[j]; if (c > 32 && c < 127 && c != '/' && c != '(' && c != ')' && c != '[' && c != ']' && c != '<' && c != '>' && c != '%') f.psname[k++] = (char) c; }
				f.psname[k] = 0;
				if (k) break;
			}
		}
		if (!f.psname[0]) { const char *d = "Font"; int k = 0; while (d[k]) { f.psname[k] = d[k]; k++; } f.psname[k] = 0; }
		f.used = new unsigned char[f.nglyphs + 1]; f.uni = new unsigned[f.nglyphs + 1];
		for (int i = 0; i <= f.nglyphs; i++) { f.used[i] = 0; f.uni[i] = 0; }
		f.used[0] = 1;
		return true;
	}
	static unsigned loca (const Font &f, int g)
	{
		const unsigned char *l = f.ttf + f.tabOff[T_LOCA];
		if (f.longLoca) return (unsigned) (g * 4 + 4) <= f.tabLen[T_LOCA] ? rd32 (l + g * 4) : 0;
		return (unsigned) (g * 2 + 2) <= f.tabLen[T_LOCA] ? rd16 (l + g * 2) * 2 : 0;
	}
	// a glyph used, and the glyphs a composite one is made of
	static void mark_used (Font &f, unsigned g, int depth = 0)
	{
		if ((int) g >= f.nglyphs || depth > 8) return;
		if (f.used[g] && depth == 0) return;
		f.used[g] = 1;
		unsigned a = loca (f, (int) g), b = loca (f, (int) g + 1);
		if (b <= a + 10 || b > f.tabLen[T_GLYF]) return;
		const unsigned char *p = f.ttf + f.tabOff[T_GLYF] + a;
		if (rds16 (p) >= 0) return;
		p += 10; const unsigned char *end = f.ttf + f.tabOff[T_GLYF] + b;
		for (;;)
		{
			if (p + 4 > end) break;
			unsigned fl = rd16 (p), cg = rd16 (p + 2);
			if (cg < (unsigned) f.nglyphs && !f.used[cg]) mark_used (f, cg, depth + 1);
			p += 4 + ((fl & 1) ? 4 : 2) + ((fl & 8) ? 2 : (fl & 0x40) ? 4 : (fl & 0x80) ? 8 : 0);
			if (!(fl & 0x20)) break;
		}
	}
	static unsigned table_sum (const unsigned char *p, unsigned n)
	{
		unsigned s = 0;
		for (unsigned i = 0; i < n; i += 4) { unsigned v = 0; for (int k = 0; k < 4; k++) v = v << 8 | (i + k < n ? p[i + k] : 0); s += v; }
		return s;
	}
	// the subset: the glyphs used keep their numbers (the others empty: no renumbering), loca long
	unsigned char *subset (const Font &f, unsigned *outLen)
	{
		int ng = f.nglyphs;
		unsigned glen = 0;
		for (int g = 0; g < ng; g++) if (f.used[g]) { unsigned a = loca (f, g), b = loca (f, g + 1); if (b > a && b <= f.tabLen[T_GLYF]) glen += (b - a + 3) & ~3u; }
		unsigned char *glyf = new unsigned char[glen ? glen : 4], *lo = new unsigned char[(ng + 1) * 4];
		unsigned at = 0;
		for (int g = 0; g < ng; g++)
		{
			wr32 (lo + g * 4, at);
			if (!f.used[g]) continue;
			unsigned a = loca (f, g), b = loca (f, g + 1);
			if (b <= a || b > f.tabLen[T_GLYF]) continue;
			for (unsigned i = a; i < b; i++) glyf[at++] = f.ttf[f.tabOff[T_GLYF] + i];
			while (at & 3) glyf[at++] = 0;
		}
		wr32 (lo + ng * 4, at);
		// the tables kept: head (loca long, its sum to redo), hhea, hmtx, maxp, cvt, fpgm, prep, loca, glyf
		struct T { const char *tag; const unsigned char *d; unsigned n; };
		unsigned char head[64]; unsigned hl = f.tabLen[T_HEAD] < 64 ? f.tabLen[T_HEAD] : 54;
		for (unsigned i = 0; i < hl; i++) head[i] = f.ttf[f.tabOff[T_HEAD] + i];
		wr32 (head + 8, 0); wr16 (head + 50, 1);
		T t[9] = { { "cvt ", f.ttf + f.tabOff[T_CVT], f.tabLen[T_CVT] }, { "fpgm", f.ttf + f.tabOff[T_FPGM], f.tabLen[T_FPGM] },
			   { "glyf", glyf, at }, { "head", head, hl }, { "hhea", f.ttf + f.tabOff[T_HHEA], f.tabLen[T_HHEA] },
			   { "hmtx", f.ttf + f.tabOff[T_HMTX], f.tabLen[T_HMTX] }, { "loca", lo, (unsigned) (ng + 1) * 4 },
			   { "maxp", f.ttf + f.tabOff[T_MAXP], f.tabLen[T_MAXP] }, { "prep", f.ttf + f.tabOff[T_PREP], f.tabLen[T_PREP] } };
		int nt = 0; T k[9]; for (int i = 0; i < 9; i++) if (t[i].n) k[nt++] = t[i];
		unsigned total = 12 + 16 * nt; for (int i = 0; i < nt; i++) total += (k[i].n + 3) & ~3u;
		unsigned char *o = new unsigned char[total];
		for (unsigned i = 0; i < total; i++) o[i] = 0;
		int es = 1, lg = 0; while (es * 2 <= nt) { es *= 2; lg++; }
		wr32 (o, 0x00010000); wr16 (o + 4, (unsigned) nt); wr16 (o + 6, (unsigned) es * 16); wr16 (o + 8, (unsigned) lg); wr16 (o + 10, (unsigned) (nt * 16 - es * 16));
		unsigned pos = 12 + 16 * nt, headAt = 0;
		for (int i = 0; i < nt; i++)
		{
			unsigned char *e = o + 12 + i * 16;
			for (int c = 0; c < 4; c++) e[c] = (unsigned char) k[i].tag[c];
			wr32 (e + 4, table_sum (k[i].d, k[i].n)); wr32 (e + 8, pos); wr32 (e + 12, k[i].n);
			for (unsigned b = 0; b < k[i].n; b++) o[pos + b] = k[i].d[b];
			if (k[i].tag[0] == 'h' && k[i].tag[1] == 'e') headAt = pos;
			pos += (k[i].n + 3) & ~3u;
		}
		if (headAt) wr32 (o + headAt + 8, 0xB1B0AFBAu - table_sum (o, total));
		delete[] glyf; delete[] lo;
		*outLen = total;
		return o;
	}
	int advance (const Font &f, int g)
	{
		const unsigned char *h = f.ttf + f.tabOff[T_HMTX];
		int i = g < f.nmetrics ? g : f.nmetrics - 1;
		if ((unsigned) (i * 4 + 2) > f.tabLen[T_HMTX]) return 0;
		return (int) rd16 (h + i * 4) * 1000 / f.upem;
	}
	// a font as PDF wants it: Type 0 (Identity-H: the glyphs' numbers are the codes), its CID font, its descriptor,
	// the subset file, the ToUnicode map
	void write_font (Buf &o, Font &f, int idx, unsigned *off)
	{
		int ob = f.objFont;
		char tag[8]; unsigned h = (unsigned) idx * 2654435761u ^ (unsigned) f.nglyphs * 40503u;
		for (int i = 0; i < 6; i++) { tag[i] = (char) ('A' + h % 26); h /= 26; } tag[6] = '+'; tag[7] = 0;
		off[ob] = o.n; put_int (o, ob); put_s (o, " 0 obj\n<< /Type /Font /Subtype /Type0 /BaseFont /"); put_s (o, tag); put_s (o, f.psname);
		put_s (o, " /Encoding /Identity-H /DescendantFonts ["); put_int (o, ob + 1); put_s (o, " 0 R] /ToUnicode "); put_int (o, ob + 4); put_s (o, " 0 R >>\nendobj\n");
		off[ob + 1] = o.n; put_int (o, ob + 1); put_s (o, " 0 obj\n<< /Type /Font /Subtype /CIDFontType2 /BaseFont /"); put_s (o, tag); put_s (o, f.psname);
		put_s (o, " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor "); put_int (o, ob + 2);
		put_s (o, " 0 R /CIDToGIDMap /Identity /DW "); put_int (o, advance (f, 0)); put_s (o, " /W [");
		for (int g = 0; g < f.nglyphs; g++) if (f.used[g]) { put_int (o, g); put_s (o, " ["); put_int (o, advance (f, g)); put_s (o, "] "); }
		put_s (o, "] >>\nendobj\n");
		off[ob + 2] = o.n; put_int (o, ob + 2); put_s (o, " 0 obj\n<< /Type /FontDescriptor /FontName /"); put_s (o, tag); put_s (o, f.psname);
		put_s (o, " /Flags "); put_int (o, f.flags); put_s (o, " /FontBBox [");
		for (int i = 0; i < 4; i++) { put_int (o, f.bbox[i] * 1000 / f.upem); o.put (' '); }
		put_s (o, "] /ItalicAngle "); put_int (o, f.italic); put_s (o, " /Ascent "); put_int (o, f.ascent * 1000 / f.upem);
		put_s (o, " /Descent "); put_int (o, f.descent * 1000 / f.upem); put_s (o, " /CapHeight "); put_int (o, f.capH * 1000 / f.upem);
		put_s (o, " /StemV 80 /FontFile2 "); put_int (o, ob + 3); put_s (o, " 0 R >>\nendobj\n");
		unsigned sl; unsigned char *sub = subset (f, &sl);
		unsigned zl; unsigned char *z = pngsave::deflate (sub, sl, true, &zl);
		off[ob + 3] = o.n; put_int (o, ob + 3); put_s (o, " 0 obj\n<< /Length1 "); put_int (o, sl); put_s (o, " /Filter /FlateDecode /Length "); put_int (o, zl);
		put_s (o, " >>\nstream\n"); o.put (z, zl); put_s (o, "\nendstream\nendobj\n");
		delete[] sub; delete[] z;
		// ToUnicode: the glyphs' characters, by blocks of 100
		Buf c;
		put_s (c, "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
			  "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n");
		int n = 0; for (int g = 1; g < f.nglyphs; g++) if (f.used[g] && f.uni[g]) n++;
		int g = 1;
		while (n > 0)
		{
			int k = n < 100 ? n : 100;
			put_int (c, k); put_s (c, " beginbfchar\n");
			for (int done = 0; done < k; g++)
			{
				if (!f.used[g] || !f.uni[g]) continue;
				c.put ('<'); put_hex4 (c, (unsigned) g); put_s (c, "> <");
				unsigned u = f.uni[g];
				if (u >= 0x10000) { u -= 0x10000; put_hex4 (c, 0xD800 | (u >> 10)); put_hex4 (c, 0xDC00 | (u & 0x3FF)); } else put_hex4 (c, u);
				put_s (c, ">\n"); done++;
			}
			put_s (c, "endbfchar\n"); n -= k;
		}
		put_s (c, "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n");
		unsigned cl; unsigned char *cz = pngsave::deflate (c.b, c.n, true, &cl);
		off[ob + 4] = o.n; put_int (o, ob + 4); put_s (o, " 0 obj\n<< /Filter /FlateDecode /Length "); put_int (o, cl); put_s (o, " >>\nstream\n");
		o.put (cz, cl); put_s (o, "\nendstream\nendobj\n");
		delete[] cz;
	}
};

} // namespace pdfw

#endif
