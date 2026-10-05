//
// xml.h -- the XML the office files (.docx, .odt) are made of: a pull reader (start tags with their
// attributes, end tags, text with its entities decoded -- as UTF-8 --; comments, declarations, CDATA
// handled; a name compared without its prefix, "w:p" = "p") and a writer's escaping; the ZIP around
// them (an entry inflated), and ODF's / Word's lengths.
//
#ifndef _writer_xml_h
#define _writer_xml_h

#include <string.h>
#include <stdlib.h>
#include "doc.h"
#include "imagekit/img/imgload.hpp"
#include "imagekit/img/pngsave.hpp"

namespace wr {

// ---- a growing text ----------------------------------------------------------------------------------------
struct XBuf
{
	char *b; int n, cap;
	XBuf () : b (0), n (0), cap (0) {}
	~XBuf () { delete[] b; }
	void reserve (int k) { if (n + k + 1 <= cap) return; int c = cap ? cap * 2 : 256; while (c < n + k + 1) c *= 2; char *t = new char[c]; for (int i = 0; i < n; i++) t[i] = b[i]; delete[] b; b = t; cap = c; }
	void put (char c) { reserve (1); b[n++] = c; b[n] = 0; }
	void putn (const char *s, int k) { reserve (k); for (int i = 0; i < k; i++) b[n++] = s[i]; b[n] = 0; }
	void puts (const char *s) { putn (s, (int) strlen (s)); }
	void putu (unsigned c)				// (a code point, as UTF-8)
	{
		if (c < 0x80) put ((char) c);
		else if (c < 0x800) { put ((char) (0xC0 | c >> 6)); put ((char) (0x80 | (c & 0x3F))); }
		else if (c < 0x10000) { put ((char) (0xE0 | c >> 12)); put ((char) (0x80 | (c >> 6 & 0x3F))); put ((char) (0x80 | (c & 0x3F))); }
		else { put ((char) (0xF0 | c >> 18)); put ((char) (0x80 | (c >> 12 & 0x3F))); put ((char) (0x80 | (c >> 6 & 0x3F))); put ((char) (0x80 | (c & 0x3F))); }
	}
	void num (long v) { char t[24]; int j = 0; bool ng = v < 0; unsigned long u = ng ? (unsigned long) -v : (unsigned long) v; do { t[j++] = (char) ('0' + u % 10); u /= 10; } while (u); if (ng) put ('-'); while (j) put (t[--j]); }
	void clear () { n = 0; if (b) b[0] = 0; }
	const char *str () { reserve (0); b[n] = 0; return b; }	// (an empty one: its buffer made, ended)
	char *take () { reserve (0); b[n] = 0; char *r = b; b = 0; n = cap = 0; return r; }
};

// UTF-8 -> code points (a byte not UTF-8: as Latin-1).
static int utf8_decode (const char *s, int n, unsigned *out, int cap)
{
	int m = 0;
	for (int i = 0; i < n && m < cap; )
	{
		unsigned c = (unsigned char) s[i];
		int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		bool ok = k >= 0 && i + k < n;
		for (int j = 1; ok && j <= k; j++) if (((unsigned char) s[i + j] & 0xC0) != 0x80) ok = false;
		if (!ok || k == 0) { out[m++] = c; i++; continue; }
		c &= k == 1 ? 0x1F : k == 2 ? 0x0F : 0x07;
		for (int j = 1; j <= k; j++) c = c << 6 | ((unsigned char) s[i + j] & 0x3F);
		out[m++] = c; i += k + 1;
	}
	return m;
}

// ---- the reader ------------------------------------------------------------------------------------------------
enum { X_EOF, X_START, X_END, X_TEXT };

struct XmlReader
{
	const char *p, *e;
	int ev;						// the current event
	const char *nm; int nml;			// X_START / X_END: the element's name (with its prefix)
	const char *at, *ate;				// X_START: its attributes' text
	bool empty;					// X_START of <a/>: an X_END follows by itself
	bool pendingEnd;
	XBuf text;					// X_TEXT: decoded (UTF-8)

	XmlReader (const char *d, int n) : p (d), e (d + n), ev (X_EOF), nm (0), nml (0), at (0), ate (0), empty (false), pendingEnd (false) {}

	static void decode (const char *s, const char *z, XBuf &o)
	{
		o.clear ();
		while (s < z)
		{
			if (*s != '&') { o.put (*s++); continue; }
			const char *q = s + 1;
			while (q < z && *q != ';' && q - s < 12) q++;
			if (q >= z || *q != ';') { o.put (*s++); continue; }
			int l = (int) (q - s - 1);
			const char *en = s + 1;
			if (l == 2 && !memcmp (en, "lt", 2)) o.put ('<');
			else if (l == 2 && !memcmp (en, "gt", 2)) o.put ('>');
			else if (l == 3 && !memcmp (en, "amp", 3)) o.put ('&');
			else if (l == 4 && !memcmp (en, "quot", 4)) o.put ('"');
			else if (l == 4 && !memcmp (en, "apos", 4)) o.put ('\'');
			else if (l > 1 && en[0] == '#')
			{
				unsigned v = en[1] == 'x' || en[1] == 'X' ? (unsigned) strtoul (en + 2, 0, 16) : (unsigned) strtoul (en + 1, 0, 10);
				if (v) o.putu (v);
			}
			else o.putn (s, (int) (q - s + 1));
			s = q + 1;
		}
	}
	int next ()
	{
		if (pendingEnd) { pendingEnd = false; ev = X_END; at = ate = 0; return ev; }
		for (;;)
		{
			if (p >= e) return ev = X_EOF;
			if (*p != '<')
			{
				const char *s = p;
				while (p < e && *p != '<') p++;
				decode (s, p, text);
				return ev = X_TEXT;
			}
			if (p + 4 <= e && !memcmp (p, "<!--", 4)) { const char *q = p + 4; while (q + 3 <= e && memcmp (q, "-->", 3)) q++; p = q + 3 <= e ? q + 3 : e; continue; }
			if (p + 9 <= e && !memcmp (p, "<![CDATA[", 9))
			{
				const char *s = p + 9, *q = s;
				while (q + 3 <= e && memcmp (q, "]]>", 3)) q++;
				text.clear (); text.putn (s, (int) (q - s));
				p = q + 3 <= e ? q + 3 : e;
				return ev = X_TEXT;
			}
			if (p + 1 < e && (p[1] == '?' || p[1] == '!')) { while (p < e && *p != '>') p++; p++; continue; }
			if (p + 1 < e && p[1] == '/')
			{
				const char *s = p + 2, *q = s;
				while (q < e && *q != '>' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
				nm = s; nml = (int) (q - s);
				while (q < e && *q != '>') q++;
				p = q < e ? q + 1 : e;
				at = ate = 0;
				return ev = X_END;
			}
			const char *s = p + 1, *q = s;
			while (q < e && *q != '>' && *q != '/' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
			nm = s; nml = (int) (q - s);
			at = q;
			char quote = 0;
			while (q < e && (quote || (*q != '>')))
			{
				if (quote) { if (*q == quote) quote = 0; }
				else if (*q == '"' || *q == '\'') quote = *q;
				q++;
			}
			ate = q;
			empty = q > at && q[-1] == '/';
			if (empty) ate = q - 1;
			p = q < e ? q + 1 : e;
			if (empty) pendingEnd = true;
			return ev = X_START;
		}
	}
	// The element's name without its prefix is n?
	bool is (const char *n) const
	{
		const char *s = nm; int l = nml;
		for (int i = 0; i < l; i++) if (nm[i] == ':') { s = nm + i + 1; l = nml - i - 1; break; }
		int k = (int) strlen (n);
		return l == k && !memcmp (s, n, k);
	}
	// ... with its prefix ("text:p")?
	bool isq (const char *qn) const { int k = (int) strlen (qn); return nml == k && !memcmp (nm, qn, k); }
	// An attribute's value (decoded) by its name -- with its prefix ("table:name"), or without one when
	// the name given has none (and the attribute's is not "xmlns").
	bool attr (const char *name, XBuf &o) const
	{
		if (ev != X_START || !at) return false;
		int k = (int) strlen (name);
		bool plain = !strchr (name, ':');
		const char *q = at;
		while (q < ate)
		{
			while (q < ate && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
			const char *an = q;
			while (q < ate && *q != '=' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
			int al = (int) (q - an);
			while (q < ate && *q != '=') q++;
			if (q >= ate) return false;
			q++;
			while (q < ate && *q != '"' && *q != '\'') q++;
			if (q >= ate) return false;
			char quote = *q++;
			const char *vs = q;
			while (q < ate && *q != quote) q++;
			const char *ve = q;
			if (q < ate) q++;
			bool match = al == k && !memcmp (an, name, k);
			if (!match && plain)
			{
				for (int i = 0; i < al; i++)
					if (an[i] == ':') { if (al - i - 1 == k && !memcmp (an + i + 1, name, k) && !(i == 5 && !memcmp (an, "xmlns", 5))) match = true; break; }
			}
			if (match) { decode (vs, ve, o); return true; }
		}
		return false;
	}
	// An attribute as a string into a fixed buffer ("" when absent).
	bool attrs (const char *name, char *out, int cap) const { XBuf b; if (!attr (name, b)) { out[0] = 0; return false; } scpy (out, b.str (), cap); return true; }
	long attr_int (const char *name, long def) const { XBuf b; if (!attr (name, b)) return def; return strtol (b.str (), 0, 10); }
	// A Word on / off attribute ("w:val": absent, "1", "true", "on" -- on; "0", "false", "off" -- off).
	bool attr_on (const char *name = "val") const
	{
		XBuf b; if (!attr (name, b)) return true;
		const char *s = b.str ();
		return !(s[0] == '0' || s[0] == 'f' || s[0] == 'F' || (s[0] == 'o' && s[1] == 'f') || (s[0] == 'n' && s[1] == 'o'));
	}
	// Past the element just started (its whole content skipped).
	void skip ()
	{
		if (ev != X_START) return;
		int depth = 1;
		while (depth > 0) { int t = next (); if (t == X_EOF) return; if (t == X_START) depth++; else if (t == X_END) depth--; }
	}
};

// Text for XML (a node's text, or an attribute's value: quotes too); control characters dropped.
static void xml_esc (XBuf &o, const char *s, int n = -1)
{
	if (n < 0) n = (int) strlen (s);
	for (int i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) s[i];
		switch (c)
		{
		case '&': o.puts ("&amp;"); break;
		case '<': o.puts ("&lt;"); break;
		case '>': o.puts ("&gt;"); break;
		case '"': o.puts ("&quot;"); break;
		default: if (c >= 32 || c == '\t' || c == '\n' || c == '\r') o.put ((char) c); break;
		}
	}
}
// Code points as XML text (UTF-8).
static void xml_escu (XBuf &o, const unsigned *s, int n)
{
	for (int i = 0; i < n; i++)
	{
		unsigned c = s[i];
		if (c == '&') o.puts ("&amp;"); else if (c == '<') o.puts ("&lt;"); else if (c == '>') o.puts ("&gt;"); else if (c == '"') o.puts ("&quot;");
		else if (c >= 32 && !(c >= 0xD800 && c < 0xE000) && c != 0xFFFE && c != 0xFFFF) o.putu (c);
	}
}
// A Latin-1 string (the system's) as XML text.
static void xml_esc1 (XBuf &o, const char *s)
{
	for (; *s; s++)
	{
		unsigned c = (unsigned char) *s;
		if (c == '&') o.puts ("&amp;"); else if (c == '<') o.puts ("&lt;"); else if (c == '>') o.puts ("&gt;"); else if (c == '"') o.puts ("&quot;");
		else if (c >= 32) o.putu (c);
	}
}

// ---- ZIP ------------------------------------------------------------------------------------------------------
// An entry of an archive, inflated (new[], NUL-terminated), or 0.
static char *zip_get (const unsigned char *z, unsigned zn, const char *name, int *len)
{
	pngsave::ZipEntry e;
	if (!pngsave::zip_find (z, zn, name, &e)) return 0;
	if (e.method == 0)
	{
		char *d = new char[e.csize + 1];
		for (unsigned i = 0; i < e.csize; i++) d[i] = (char) e.data[i];
		d[e.csize] = 0; *len = (int) e.csize;
		return d;
	}
	if (e.method != 8) return 0;
	unsigned ol = 0;
	unsigned char *o = img_inflate (e.data, e.csize, false, &ol);
	if (!o) return 0;
	char *d = new char[ol + 1];
	for (unsigned i = 0; i < ol; i++) d[i] = (char) o[i];
	d[ol] = 0; *len = (int) ol;
	delete[] o;
	return d;
}
static bool zip_has (const unsigned char *z, unsigned zn, const char *name) { pngsave::ZipEntry e; return pngsave::zip_find (z, zn, name, &e); }

// A part's path from another's relative one ("word/document.xml" + "media/a.png" -> "word/media/a.png").
static void part_path (const char *base, const char *rel, char *out, int cap)
{
	if (rel[0] == '/') { scpy (out, rel + 1, cap); return; }
	int n = 0, cut = 0;
	for (int i = 0; base[i]; i++) if (base[i] == '/') cut = i + 1;
	for (int i = 0; i < cut && n < cap - 1; i++) out[n++] = base[i];
	while (rel[0] == '.' && rel[1] == '.' && rel[2] == '/')		// (../)
	{
		rel += 3;
		if (n > 0) { n--; while (n > 0 && out[n - 1] != '/') n--; }
	}
	for (int i = 0; rel[i] && n < cap - 1; i++) out[n++] = rel[i];
	out[n] = 0;
}

// ---- lengths ---------------------------------------------------------------------------------------------------
// "2.54cm", "1in", "12pt", "20mm", "1pc", "0.5em"... -> twips (a bare number: `bare` twips each).
static int len_twips (const char *s, int bare = 1)
{
	while (*s == ' ') s++;
	bool neg = false; if (*s == '-') { neg = true; s++; } else if (*s == '+') s++;
	long long ip = 0, fp = 0, fd = 1;
	while (*s >= '0' && *s <= '9') { ip = ip * 10 + (*s - '0'); s++; }
	if (*s == '.' || *s == ',') { s++; while (*s >= '0' && *s <= '9') { if (fd < 1000000) { fp = fp * 10 + (*s - '0'); fd *= 10; } s++; } }
	// value * unit / 1e6, rounded
	long long v = ip * 1000000 + fp * 1000000 / fd;
	long long tw;
	if (s[0] == 'c' && s[1] == 'm') tw = (v * 567 + 500000) / 1000000;
	else if (s[0] == 'm' && s[1] == 'm') tw = (v * 567 + 5000000) / 10000000;
	else if (s[0] == 'i' && s[1] == 'n') tw = (v * 1440 + 500000) / 1000000;
	else if (s[0] == 'p' && s[1] == 't') tw = (v * 20 + 500000) / 1000000;
	else if (s[0] == 'p' && s[1] == 'c') tw = (v * 240 + 500000) / 1000000;
	else if (s[0] == 'p' && s[1] == 'x') tw = (v * 15 + 500000) / 1000000;
	else tw = (v * bare + 500000) / 1000000;
	return (int) (neg ? -tw : tw);
}
// Twips -> "1.234cm" (an ODF length).
static void twips_cm (XBuf &o, int tw)
{
	long v = (long) (((long long) (tw < 0 ? -tw : tw) * 10000 + 283) / 567);	// (1/10000 cm, rounded)
	if (tw < 0) o.put ('-');
	o.num (v / 10000); o.put ('.');
	char d[5]; long f = v % 10000; for (int i = 3; i >= 0; i--) { d[i] = (char) ('0' + f % 10); f /= 10; } d[4] = 0;
	o.puts (d); o.puts ("cm");
}
// "#RRGGBB" / "RRGGBB" -> 0xRRGGBB (AUTO: "auto", none, not a colour).
static unsigned hex_color (const char *s)
{
	if (*s == '#') s++;
	unsigned v = 0; int k = 0;
	for (; k < 6; k++)
	{
		char c = s[k];
		int h = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if (h < 0) return AUTO;
		v = v << 4 | (unsigned) h;
	}
	return v;
}
static void color_hex (XBuf &o, unsigned c) { const char *h = "0123456789ABCDEF"; for (int s = 20; s >= 0; s -= 4) o.put (h[(c >> s) & 15]); }

// An image's bytes read (PNG, JPEG, else anything the image loader knows): into the document (its
// index + 1; 0: not an image).
static int image_from_bytes (Doc &d, const unsigned char *b, unsigned n)
{
	ImgFrames im;
	if (!n || !img_load_mem (b, n, &im) || im.n < 1) return 0;
	for (int i = 1; i < im.n; i++) delete[] im.px[i];
	bool png = n > 4 && b[0] == 0x89 && b[1] == 'P', jpeg = n > 2 && b[0] == 0xFF && b[1] == 0xD8;
	unsigned char *data = 0;
	if (png || jpeg) { data = new unsigned char[n]; for (unsigned i = 0; i < n; i++) data[i] = b[i]; }
	return doc_image (d, im.px[0], im.w, im.h, data, png || jpeg ? n : 0, jpeg) + 1;
}

} // namespace wr

#endif
