//
// fileio.h -- Writer's files: Rich Text Format read and written (the fonts, the colours, the styles
// -- "Normal", "heading 1"... --, bold / italic / underline / strike-through / superscript /
// subscript, the sizes, the highlights, the paragraphs' alignment, indents, spacing, lists (Word's
// \listtext / \pntext), page breaks, the page's size and margins, a footer's page number), plain
// text (UTF-8, or Latin-1: the system's own), and an HTML export.
//
// Reading RTF: groups and their state, control words, \'hh (Windows-1252), \uN (and its \ucN
// fallback skipped), the destinations Writer has no use for skipped (\info, \pict, \*\...,
// fields' instructions, headers).
//
#ifndef _writer_fileio_h
#define _writer_fileio_h

#include "doc.h"
#include "img/imgload.hpp"

namespace wr {

// ---- an output buffer ----------------------------------------------------------------------------------
struct Out
{
	char *b; int n, cap;
	void init () { cap = 1 << 16; b = new char[cap]; n = 0; }
	void grow (int k) { if (n + k <= cap) return; int c = cap * 2; while (c < n + k) c *= 2; char *t = new char[c]; for (int i = 0; i < n; i++) t[i] = b[i]; delete[] b; b = t; cap = c; }
	void put (char c) { grow (1); b[n++] = c; }
	void puts (const char *s) { while (*s) put (*s++); }
	void num (long v) { if (v < 0) { put ('-'); v = -v; } char t[24]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) put (t[--j]); }
	void hex2 (unsigned v) { const char *h = "0123456789abcdef"; put (h[(v >> 4) & 15]); put (h[v & 15]); }
	void utf8 (unsigned c)
	{
		if (c < 0x80) put ((char) c);
		else if (c < 0x800) { put ((char) (0xC0 | c >> 6)); put ((char) (0x80 | (c & 0x3F))); }
		else if (c < 0x10000) { put ((char) (0xE0 | c >> 12)); put ((char) (0x80 | (c >> 6 & 0x3F))); put ((char) (0x80 | (c & 0x3F))); }
		else { put ((char) (0xF0 | c >> 18)); put ((char) (0x80 | (c >> 12 & 0x3F))); put ((char) (0x80 | (c >> 6 & 0x3F))); put ((char) (0x80 | (c & 0x3F))); }
	}
	void free () { delete[] b; b = 0; n = cap = 0; }
};

// Windows-1252's 0x80..0x9F -> Unicode.
static unsigned cp1252 (unsigned c)
{
	static const unsigned short T[32] = {
		0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
		0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
	return c >= 0x80 && c < 0xA0 ? T[c - 0x80] : c;
}

// ---- images' bytes -------------------------------------------------------------------------------------
// An image as PNG: its pixels (RGBA), the deflate stream "stored" (no compression: no zlib here).
static unsigned g_crc[256];
static unsigned crc32 (unsigned c, const unsigned char *p, int n)
{
	if (!g_crc[1]) for (unsigned i = 0; i < 256; i++) { unsigned v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; g_crc[i] = v; }
	c = ~c;
	for (int i = 0; i < n; i++) c = g_crc[(c ^ p[i]) & 255] ^ (c >> 8);
	return ~c;
}
static void be32 (unsigned char *p, unsigned v) { p[0] = (unsigned char) (v >> 24); p[1] = (unsigned char) (v >> 16); p[2] = (unsigned char) (v >> 8); p[3] = (unsigned char) v; }
static unsigned char *png_encode (const unsigned *px, int w, int h, unsigned *outLen)
{
	unsigned raw = (unsigned) h * (1 + 4 * (unsigned) w);
	unsigned blocks = raw / 65535 + 1;
	unsigned idat = 2 + blocks * 5 + raw + 4;
	unsigned total = 8 + (12 + 13) + (12 + idat) + 12;
	unsigned char *o = new unsigned char[total], *p = o;
	static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
	for (int i = 0; i < 8; i++) *p++ = sig[i];
	// IHDR
	be32 (p, 13); p += 4;
	unsigned char *t = p;
	*p++ = 'I'; *p++ = 'H'; *p++ = 'D'; *p++ = 'R';
	be32 (p, (unsigned) w); p += 4; be32 (p, (unsigned) h); p += 4;
	*p++ = 8; *p++ = 6; *p++ = 0; *p++ = 0; *p++ = 0;
	be32 (p, crc32 (0, t, 17)); p += 4;
	// IDAT: zlib, stored blocks, Adler-32
	be32 (p, idat); p += 4;
	t = p;
	*p++ = 'I'; *p++ = 'D'; *p++ = 'A'; *p++ = 'T';
	*p++ = 0x78; *p++ = 0x01;
	unsigned a1 = 1, a2 = 0, done = 0, row = 0, col = 0;	// (the raw bytes made on the way)
	while (done < raw)
	{
		unsigned n = raw - done < 65535 ? raw - done : 65535;
		*p++ = (unsigned char) (done + n == raw ? 1 : 0);
		*p++ = (unsigned char) n; *p++ = (unsigned char) (n >> 8); *p++ = (unsigned char) ~n; *p++ = (unsigned char) (~n >> 8);
		for (unsigned k = 0; k < n; k++)
		{
			unsigned char b;
			if (col == 0) b = 0;					// (each row's filter: none)
			else { unsigned v = px[row * (unsigned) w + (col - 1) / 4]; int c = (col - 1) & 3; b = (unsigned char) (c == 0 ? v >> 16 : c == 1 ? v >> 8 : c == 2 ? v : v >> 24); }
			if (++col == 1 + 4 * (unsigned) w) { col = 0; row++; }
			*p++ = b;
			a1 = (a1 + b) % 65521; a2 = (a2 + a1) % 65521;
		}
		done += n;
	}
	be32 (p, a2 << 16 | a1); p += 4;
	be32 (p, crc32 (0, t, (int) (p - t))); p += 4;
	be32 (p, 0); p += 4;
	t = p; *p++ = 'I'; *p++ = 'E'; *p++ = 'N'; *p++ = 'D';
	be32 (p, crc32 (0, t, 4)); p += 4;
	*outLen = (unsigned) (p - o);
	return o;
}
// An image's file bytes (its own PNG / JPEG, else a PNG made): *jpeg says which; delete[] when *made.
static const unsigned char *image_bytes (const Image &im, unsigned *len, bool *jpeg, bool *made)
{
	if (im.data && im.len) { *len = im.len; *jpeg = im.jpeg; *made = false; return im.data; }
	*jpeg = false; *made = true;
	return png_encode (im.px, im.w, im.h, len);
}

// ---- RTF: reading --------------------------------------------------------------------------------------
enum { DS_TEXT, DS_SKIP, DS_FONTTBL, DS_COLORTBL, DS_STYLESHEET, DS_LISTTEXT, DS_FOOTER, DS_FLDINST, DS_PICT };

struct RtfState
{
	CharFmt cf; int cfColor, cfHilite;	// (colour table indices, -1 none)
	ParaFmt pf;
	int dest, uc;
	int styleNo;				// \sN of a stylesheet entry being read
	int fontNo;				// \fN of a font table entry being read
	bool hidden;				// \v: not shown
};

static bool rtf_is (const char *b, int n) { return n >= 5 && b[0] == '{' && b[1] == '\\' && b[2] == 'r' && b[3] == 't' && b[4] == 'f'; }

static int rtf_style_by_name (const char *n)
{
	static const char *const names[][2] = {
		{ "normal", "Normal" }, { "heading 1", "Heading 1" }, { "heading 2", "Heading 2" }, { "heading 3", "Heading 3" },
		{ "title", "Title" }, { "subtitle", "Subtitle" }, { "quote", "Quote" }, { "plain text", "Plain Text" } };
	for (int i = 0; i < ST_COUNT; i++) if (sicmp (n, names[i][0]) == 0) return i;
	if (sicmp (n, "block text") == 0 || sicmp (n, "intense quote") == 0) return ST_QUOTE;
	if (sicmp (n, "heading 4") == 0 || sicmp (n, "heading 5") == 0) return ST_H3;
	return -1;
}

static bool rtf_load (Doc &d, const char *b, int n)
{
	doc_clear (d);
	enum { MAXF = 256, MAXC = 256, MAXS = 64, DEPTH = 64 };
	int *fontMap = new int[MAXF];			// RTF \fN -> the document's font
	unsigned *colors = new unsigned[MAXC]; int ncolors = 0;
	int styleMap[MAXS];				// \sN -> ST_*
	for (int i = 0; i < MAXF; i++) fontMap[i] = -1;
	for (int i = 0; i < MAXS; i++) styleMap[i] = i == 0 ? ST_NORMAL : -1;
	RtfState *st = new RtfState[DEPTH]; int sp = 0;
	CharFmt base; base.font = (short) doc_font (d, "Liberation Serif"); base.size = 24; base.flags = 0; base.color = AUTO; base.hilite = AUTO;
	RtfState &s0 = st[0];
	s0.cf = base; s0.cfColor = -1; s0.cfHilite = -1; s0.pf = style_para (ST_NORMAL); s0.pf.after = 0; s0.pf.line = 100;
	s0.dest = DS_TEXT; s0.uc = 1; s0.styleNo = -1; s0.fontNo = -1; s0.hidden = false;
	int deff = 0;
	char name[64]; int nameLen = 0;			// a font / style name being read
	int red = 0, green = 0, blue = 0; bool anyColor = false;
	unsigned lastCf = 0xFFFF; CharFmt lastFmt = base;
	Para *q = para_new ();
	char listBuf[16]; int listLen = 0; int listKind = LS_NONE, listLevel = 0;
	int skipChars = 0;				// (a \uN's fallback characters to skip)
	bool pageNext = false;				// a \page: the next paragraph starts a page
	bool footerHasPage = false;
	PageSetup &pg = d.page;
	bool landscape = false;
	// a picture being read: its kind (1 PNG, 2 JPEG), sizes, bytes
	int pkind = 0, picw = 0, pich = 0, goalw = 0, goalh = 0, scalex = 100, scaley = 100;
	unsigned char *pbuf = 0; unsigned plen = 0, pcap = 0; int nib = -1;

	auto fmtIndex = [&] (const RtfState &s) -> unsigned short {
		CharFmt f = s.cf;
		f.color = s.cfColor >= 0 && s.cfColor < ncolors ? colors[s.cfColor] : AUTO;
		f.hilite = s.cfHilite > 0 && s.cfHilite < ncolors ? colors[s.cfHilite] : AUTO;
		if (f.color == 0x000000 && s.cfColor >= 0) f.color = AUTO;	// (black: the automatic one)
		if (lastCf != 0xFFFF && f.same (lastFmt)) return (unsigned short) lastCf;
		lastFmt = f; lastCf = doc_fmt (d, f);
		return (unsigned short) lastCf;
	};
	auto emit = [&] (unsigned c) {
		RtfState &s = st[sp];
		if (skipChars > 0) { skipChars--; return; }
		if (s.dest == DS_LISTTEXT) { if (listLen < 15) listBuf[listLen++] = c < 128 ? (char) c : '*'; return; }
		if (s.dest == DS_FONTTBL || s.dest == DS_STYLESHEET) { if (c == ';') { name[nameLen] = 0; if (s.dest == DS_FONTTBL && s.fontNo >= 0 && s.fontNo < MAXF) fontMap[s.fontNo] = doc_font (d, name); else if (s.dest == DS_STYLESHEET && s.styleNo >= 0 && s.styleNo < MAXS) { int k = rtf_style_by_name (name); if (k >= 0) styleMap[s.styleNo] = k; } nameLen = 0; } else if (nameLen < 63 && (c >= 32 || nameLen)) name[nameLen++] = c < 256 ? (char) c : '?'; return; }
		if (s.dest == DS_PICT)
		{
			int h = c >= '0' && c <= '9' ? (int) c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (int) (c | 32) - 'a' + 10 : -1;
			if (h < 0) return;
			if (nib < 0) { nib = h; return; }
			if (plen == pcap) { unsigned nc = pcap ? pcap * 2 : 65536; unsigned char *t = new unsigned char[nc]; for (unsigned k = 0; k < plen; k++) t[k] = pbuf[k]; delete[] pbuf; pbuf = t; pcap = nc; }
			pbuf[plen++] = (unsigned char) (nib << 4 | h); nib = -1;
			return;
		}
		if (s.dest == DS_COLORTBL) { if (c == ';') { if (ncolors < MAXC) colors[ncolors++] = anyColor ? (unsigned) (red << 16 | green << 8 | blue) : 0x000000; red = green = blue = 0; anyColor = false; } return; }
		if (s.dest != DS_TEXT || s.hidden) return;
		unsigned short f = fmtIndex (s);
		para_insert (q, q->len, &c, 1, f);
	};
	auto endPara = [&] (bool last) {
		RtfState &s = st[sp];
		q->pf = s.pf;
		if (listKind != LS_NONE && q->pf.list == LS_NONE) { q->pf.list = (unsigned char) listKind; q->pf.level = (unsigned char) listLevel; }
		if (q->pf.list != LS_NONE && q->pf.first >= 0) { if (q->pf.left < 360) q->pf.left = 720; q->pf.first = -360; }
		if (pageNext) { q->pf.pageBreak = true; pageNext = false; }
		q->endCf = fmtIndex (s);
		if (!last || q->len > 0 || d.n == 0) { doc_put (d, d.n, q); q = para_new (); }
		listKind = LS_NONE; listLevel = 0;
	};

	int i = 0;
	while (i < n)
	{
		char c = b[i];
		if (c == '{')
		{
			if (sp + 1 < DEPTH) { st[sp + 1] = st[sp]; sp++; }
			i++;
			continue;
		}
		if (c == '}')
		{
			if (st[sp].dest == DS_LISTTEXT && (sp == 0 || st[sp - 1].dest != DS_LISTTEXT))
			{
				bool digits = false;
				for (int k = 0; k < listLen; k++) if ((listBuf[k] >= '0' && listBuf[k] <= '9') || ((listBuf[k] | 32) >= 'a' && (listBuf[k] | 32) <= 'z')) digits = true;
				listKind = digits ? LS_NUMBER : LS_BULLET;
				listLen = 0;
			}
			if (st[sp].dest == DS_PICT && (sp == 0 || st[sp - 1].dest != DS_PICT))
			{
				ImgFrames im;
				int k = sp - 1;					// (the text it stands in: under any \\*\\shppict)
				while (k >= 0 && st[k].dest != DS_TEXT) k--;
				if (pkind && plen && k >= 0 && !st[k].hidden && img_load_mem (pbuf, plen, &im))
				{
					for (int f2 = 1; f2 < im.n; f2++) delete[] im.px[f2];
					unsigned char *data = new unsigned char[plen];
					for (unsigned j = 0; j < plen; j++) data[j] = pbuf[j];
					int idx = doc_image (d, im.px[0], im.w, im.h, data, plen, pkind == 2);
					CharFmt cfm = d.fmt[fmtIndex (st[k])];
					cfm.obj = idx + 1;
					cfm.ow = (goalw > 0 ? goalw : (picw > 0 ? picw : im.w) * 15) * scalex / 100;
					cfm.oh = (goalh > 0 ? goalh : (pich > 0 ? pich : im.h) * 15) * scaley / 100;
					if (cfm.ow <= 0 || cfm.oh <= 0) { cfm.ow = im.w * 15; cfm.oh = im.h * 15; }
					unsigned oc = OBJ_CHAR;
					para_insert (q, q->len, &oc, 1, doc_fmt (d, cfm));
					lastCf = 0xFFFF;
				}
				delete[] pbuf; pbuf = 0; plen = pcap = 0; nib = -1; pkind = 0;
			}
			if (sp > 0) sp--;
			lastCf = 0xFFFF;
			i++;
			continue;
		}
		if (c == '\r' || c == '\n') { i++; continue; }
		if (c != '\\') { emit ((unsigned char) c); i++; continue; }
		// a control symbol / word
		i++;
		if (i >= n) break;
		c = b[i];
		if (c == '\'' && i + 2 < n)
		{
			auto hv = [] (char h) { return h >= '0' && h <= '9' ? h - '0' : (h | 32) >= 'a' && (h | 32) <= 'f' ? (h | 32) - 'a' + 10 : 0; };
			emit (cp1252 ((unsigned) (hv (b[i + 1]) * 16 + hv (b[i + 2]))));
			i += 3;
			continue;
		}
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
		{
			i++;
			switch (c)
			{
			case '\\': case '{': case '}': emit ((unsigned char) c); break;
			case '~': emit (0xA0); break;
			case '_': emit (0x2011); break;
			case '*': st[sp].dest = DS_SKIP; break;
			case '\n': case '\r': if (st[sp].dest == DS_TEXT) endPara (false); break;
			case '\t': emit ('\t'); break;
			}
			continue;
		}
		char w[32]; int wl = 0;
		while (i < n && ((b[i] >= 'a' && b[i] <= 'z') || (b[i] >= 'A' && b[i] <= 'Z'))) { if (wl < 31) w[wl++] = b[i]; i++; }
		w[wl] = 0;
		long v = 0; bool hasV = false, neg = false;
		if (i < n && b[i] == '-') { neg = true; i++; }
		while (i < n && b[i] >= '0' && b[i] <= '9') { v = v * 10 + (b[i] - '0'); hasV = true; i++; }
		if (neg) v = -v;
		if (i < n && b[i] == ' ') i++;
		RtfState &s = st[sp];
		auto is = [&] (const char *k) { int j = 0; while (k[j] && w[j] == k[j]) j++; return k[j] == 0 && w[j] == 0; };
		int on = hasV ? v != 0 : 1;
		// destinations
		if (is ("pict")) { s.dest = DS_PICT; pkind = 0; picw = pich = goalw = goalh = 0; scalex = scaley = 100; plen = 0; nib = -1; continue; }
		if (s.dest == DS_PICT)
		{
			if (is ("pngblip")) pkind = 1; else if (is ("jpegblip")) pkind = 2;
			else if (is ("picw")) picw = (int) v; else if (is ("pich")) pich = (int) v;
			else if (is ("picwgoal")) goalw = (int) v; else if (is ("pichgoal")) goalh = (int) v;
			else if (is ("picscalex")) scalex = (int) wclamp (v, 1L, 1000L); else if (is ("picscaley")) scaley = (int) wclamp (v, 1L, 1000L);
			else if (is ("bin")) i += (int) wclamp (v, 0L, (long) (n - i));	// (binary bytes: not kept)
			continue;
		}
		if (is ("fonttbl")) { s.dest = DS_FONTTBL; continue; }
		if (is ("colortbl")) { s.dest = DS_COLORTBL; continue; }
		if (is ("stylesheet")) { s.dest = DS_STYLESHEET; continue; }
		if (is ("listtext") || is ("pntext")) { s.dest = DS_LISTTEXT; listLen = 0; continue; }
		if (is ("footer") || is ("footerr") || is ("footerl") || is ("footerf")) { s.dest = DS_FOOTER; continue; }
		if (is ("fldinst")) { s.dest = s.dest == DS_FOOTER ? DS_FOOTER : DS_SKIP; continue; }
		if (is ("info") || is ("pict") || is ("header") || is ("headerr") || is ("headerl") || is ("headerf") || is ("object")
		    || is ("footnote") || is ("annotation") || is ("xe") || is ("tc") || is ("bkmkstart") || is ("bkmkend") || is ("nonshppict")
		    || is ("themedata") || is ("colorschememapping") || is ("latentstyles") || is ("datastore") || is ("listtable")
		    || is ("listoverridetable") || is ("rsidtbl") || is ("generator") || is ("mmathPr") || is ("xmlnstbl") || is ("pgdsctbl"))
		{ s.dest = DS_SKIP; continue; }
		if (s.dest == DS_SKIP) continue;
		if (s.dest == DS_FOOTER) { if (is ("chpgn")) footerHasPage = true; if (is ("fldrslt")) {} continue; }
		if (s.dest == DS_COLORTBL)
		{
			if (is ("red")) { red = (int) v; anyColor = true; } else if (is ("green")) { green = (int) v; anyColor = true; } else if (is ("blue")) { blue = (int) v; anyColor = true; }
			continue;
		}
		if (s.dest == DS_FONTTBL) { if (is ("f")) { s.fontNo = (int) v; nameLen = 0; } continue; }
		if (s.dest == DS_STYLESHEET) { if (is ("s")) { s.styleNo = (int) v; nameLen = 0; } continue; }
		if (s.dest == DS_LISTTEXT) continue;
		// the document
		if (is ("deff")) deff = (int) v;
		else if (is ("paperw")) pg.w = (int) v;
		else if (is ("paperh")) pg.h = (int) v;
		else if (is ("margl")) pg.left = (int) v;
		else if (is ("margr")) pg.right = (int) v;
		else if (is ("margt")) pg.top = (int) v;
		else if (is ("margb")) pg.bottom = (int) v;
		else if (is ("landscape")) landscape = true;
		else if (is ("uc")) s.uc = (int) v;
		else if (is ("u")) { long u = v < 0 ? v + 65536 : v; emit ((unsigned) u); skipChars = s.uc; }
		// characters
		else if (is ("par") || is ("sect")) endPara (false);
		else if (is ("line")) emit (0x0B);
		else if (is ("tab")) emit ('\t');
		else if (is ("page")) { if (q->len > 0) endPara (false); pageNext = true; }
		else if (is ("emdash")) emit (0x2014);
		else if (is ("endash")) emit (0x2013);
		else if (is ("bullet")) emit (0x2022);
		else if (is ("lquote")) emit (0x2018);
		else if (is ("rquote")) emit (0x2019);
		else if (is ("ldblquote")) emit (0x201C);
		else if (is ("rdblquote")) emit (0x201D);
		else if (is ("emspace") || is ("enspace")) emit (' ');
		else if (is ("cell") || is ("nestcell")) emit ('\t');
		else if (is ("row")) endPara (false);
		// character formats
		else if (is ("plain")) { s.cf = base; s.cf.font = (short) (fontMap[deff & (MAXF - 1)] >= 0 ? fontMap[deff & (MAXF - 1)] : base.font); s.cfColor = -1; s.cfHilite = -1; s.hidden = false; }
		else if (is ("b")) s.cf.flags = (unsigned short) (on ? s.cf.flags | CF_BOLD : s.cf.flags & ~CF_BOLD);
		else if (is ("i")) s.cf.flags = (unsigned short) (on ? s.cf.flags | CF_ITALIC : s.cf.flags & ~CF_ITALIC);
		else if (is ("ul") || is ("uld") || is ("uldb") || is ("ulw") || is ("ulth") || is ("uldash")) s.cf.flags = (unsigned short) (on ? s.cf.flags | CF_UNDER : s.cf.flags & ~CF_UNDER);
		else if (is ("ulnone")) s.cf.flags &= (unsigned short) ~CF_UNDER;
		else if (is ("strike") || is ("striked")) s.cf.flags = (unsigned short) (on ? s.cf.flags | CF_STRIKE : s.cf.flags & ~CF_STRIKE);
		else if (is ("super")) s.cf.flags = (unsigned short) ((s.cf.flags & ~CF_SUB) | CF_SUPER);
		else if (is ("sub")) s.cf.flags = (unsigned short) ((s.cf.flags & ~CF_SUPER) | CF_SUB);
		else if (is ("nosupersub")) s.cf.flags &= (unsigned short) ~(CF_SUPER | CF_SUB);
		else if (is ("up")) s.cf.flags = (unsigned short) (v ? (s.cf.flags & ~CF_SUB) | CF_SUPER : s.cf.flags & ~CF_SUPER);
		else if (is ("dn")) s.cf.flags = (unsigned short) (v ? (s.cf.flags & ~CF_SUPER) | CF_SUB : s.cf.flags & ~CF_SUB);
		else if (is ("fs")) { if (v > 0) s.cf.size = (short) wclamp ((int) v, 2, 3276); }
		else if (is ("f")) { int f = (int) v & (MAXF - 1); if (fontMap[f] >= 0) s.cf.font = (short) fontMap[f]; }
		else if (is ("cf")) s.cfColor = (int) v;
		else if (is ("highlight") || is ("cb") || is ("chcbpat")) s.cfHilite = (int) v;
		else if (is ("v")) s.hidden = on != 0;
		// paragraph formats
		else if (is ("pard"))
		{
			s.pf = style_para (ST_NORMAL); s.pf.before = 0; s.pf.after = 0; s.pf.line = 100; s.pf.keepNext = false;
		}
		else if (is ("s"))
		{
			int k = v >= 0 && v < MAXS ? styleMap[v] : -1;
			if (k < 0) k = ST_NORMAL;
			ParaFmt keep = s.pf;
			s.pf = style_para (k);
			if (k == ST_NORMAL) { s.pf.before = keep.before; s.pf.after = keep.after; s.pf.line = keep.line; }
		}
		else if (is ("ql")) s.pf.align = AL_LEFT;
		else if (is ("qc")) s.pf.align = AL_CENTER;
		else if (is ("qr")) s.pf.align = AL_RIGHT;
		else if (is ("qj")) s.pf.align = AL_JUSTIFY;
		else if (is ("li")) s.pf.left = (short) wclamp ((int) v, 0, 30000);
		else if (is ("ri")) s.pf.right = (short) wclamp ((int) v, 0, 30000);
		else if (is ("fi")) s.pf.first = (short) wclamp ((int) v, -30000, 30000);
		else if (is ("sb")) s.pf.before = (short) wclamp ((int) v, 0, 30000);
		else if (is ("sa")) s.pf.after = (short) wclamp ((int) v, 0, 30000);
		else if (is ("sl")) { if (v > 0) s.pf.line = (short) wclamp ((int) (v * 100 / 240), 50, 400); else s.pf.line = 100; }
		else if (is ("pagebb")) s.pf.pageBreak = on != 0;
		else if (is ("keepn")) s.pf.keepNext = on != 0;
		else if (is ("ilvl")) listLevel = (int) wclamp (v, 0L, 5L);
		else if (is ("pnlvlblt")) s.pf.list = LS_BULLET;
		else if (is ("pnlvlbody") || is ("pndec")) s.pf.list = s.pf.list == LS_BULLET ? LS_BULLET : LS_NUMBER;
		else if (is ("pnlvl")) { s.pf.list = LS_NUMBER; s.pf.level = (unsigned char) wclamp ((int) v - 1, 0, 5); }
	}
	if (q->len > 0 || d.n == 0) endPara (true);
	else para_free (q);
	if (landscape && pg.w < pg.h) { int t = pg.w; pg.w = pg.h; pg.h = t; }
	pg.w = wclamp (pg.w, 2880, 40000); pg.h = wclamp (pg.h, 2880, 40000);
	pg.left = wclamp (pg.left, 0, pg.w / 3); pg.right = wclamp (pg.right, 0, pg.w / 3);
	pg.top = wclamp (pg.top, 0, pg.h / 3); pg.bottom = wclamp (pg.bottom, 0, pg.h / 3);
	pg.numbers = footerHasPage;
	delete[] fontMap; delete[] colors; delete[] st; delete[] pbuf;
	return d.n > 0;
}

// ---- RTF: writing ----------------------------------------------------------------------------------------
static void rtf_text (Out &o, unsigned c)
{
	if (c == '\\' || c == '{' || c == '}') { o.put ('\\'); o.put ((char) c); }
	else if (c == '\t') o.puts ("\\tab ");
	else if (c == 0x0B) o.puts ("\\line ");
	else if (c == 0xA0) o.puts ("\\~");
	else if (c < 0x80) o.put ((char) c);
	else if (c >= 0xA0 && c < 0x100) { o.puts ("\\'"); o.hex2 (c); }
	else { o.puts ("\\u"); o.num (c < 0x8000 ? (long) c : (long) c - 65536); o.put ('?'); }
}

static int rtf_save (Doc &d, Out &o)
{
	o.init ();
	// the colours used
	unsigned cols[256]; int ncol = 0;
	auto colIdx = [&] (unsigned c) -> int {
		if (c == AUTO) return 0;
		for (int i = 0; i < ncol; i++) if (cols[i] == c) return i + 1;
		if (ncol < 255) { cols[ncol++] = c; return ncol; }
		return 0;
	};
	// (only the formats used: their fonts, their colours -- the tables keep what undone edits added)
	bool *used = new bool[d.nfmt + 1];
	for (int i = 0; i < d.nfmt; i++) used[i] = false;
	for (int p = 0; p < d.n; p++) { const Para *q = d.p[p]; used[q->endCf] = true; for (int k = 0; k < q->len; k++) used[q->cf[k]] = true; }
	int *fmap = new int[d.nfont + 1], nf = 0;
	for (int i = 0; i < d.nfont; i++) fmap[i] = -1;
	for (int i = 0; i < d.nfmt; i++) if (used[i]) { colIdx (d.fmt[i].color); colIdx (d.fmt[i].hilite); if (fmap[d.fmt[i].font] < 0) fmap[d.fmt[i].font] = nf++; }
	o.puts ("{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl");
	for (int r = 0; r < nf; r++)
	{
		int i = 0; while (fmap[i] != r) i++;
		o.puts ("{\\f"); o.num (r);
		const char *nm = d.fontName[i];
		bool mono = false, sans = false;
		for (int k = 0; nm[k]; k++) { if (lower (nm[k]) == 'm' && lower (nm[k + 1]) == 'o' && lower (nm[k + 2]) == 'n') mono = true; if (lower (nm[k]) == 's' && lower (nm[k + 1]) == 'a' && lower (nm[k + 2]) == 'n') sans = true; }
		o.puts (mono ? "\\fmodern" : sans ? "\\fswiss" : "\\froman");
		o.puts ("\\fcharset0 ");
		for (int k = 0; nm[k]; k++) rtf_text (o, (unsigned char) nm[k]);
		o.puts (";}");
	}
	o.puts ("}\n{\\colortbl;");
	for (int i = 0; i < ncol; i++) { o.puts ("\\red"); o.num (cols[i] >> 16 & 255); o.puts ("\\green"); o.num (cols[i] >> 8 & 255); o.puts ("\\blue"); o.num (cols[i] & 255); o.put (';'); }
	o.puts ("}\n{\\stylesheet");
	static const char *const snames[ST_COUNT] = { "Normal", "heading 1", "heading 2", "heading 3", "Title", "Subtitle", "Quote", "Plain Text" };
	for (int i = 0; i < ST_COUNT; i++) { o.puts ("{\\s"); o.num (i); o.put (' '); o.puts (snames[i]); o.puts (";}"); }
	o.puts ("}\n{\\*\\generator Onyx Writer;}\n");
	const PageSetup &pg = d.page;
	o.puts ("\\paperw"); o.num (pg.w); o.puts ("\\paperh"); o.num (pg.h);
	o.puts ("\\margl"); o.num (pg.left); o.puts ("\\margr"); o.num (pg.right);
	o.puts ("\\margt"); o.num (pg.top); o.puts ("\\margb"); o.num (pg.bottom);
	if (pg.w > pg.h) o.puts ("\\landscape");
	o.puts ("\\viewkind1\n");
	if (pg.numbers) o.puts ("{\\footer\\pard\\qc{\\field{\\*\\fldinst PAGE}{\\fldrslt 1}}\\par}\n");
	int counter[8] = { 0 };
	for (int p = 0; p < d.n; p++)
	{
		const Para *q = d.p[p];
		const ParaFmt &pf = q->pf;
		o.puts ("\\pard\\plain\\s"); o.num (pf.style);
		static const char *const al[4] = { "\\ql", "\\qc", "\\qr", "\\qj" };
		o.puts (al[pf.align & 3]);
		if (pf.left) { o.puts ("\\li"); o.num (pf.left); }
		if (pf.right) { o.puts ("\\ri"); o.num (pf.right); }
		if (pf.first) { o.puts ("\\fi"); o.num (pf.first); }
		if (pf.before) { o.puts ("\\sb"); o.num (pf.before); }
		if (pf.after) { o.puts ("\\sa"); o.num (pf.after); }
		if (pf.line != 100) { o.puts ("\\sl"); o.num (pf.line * 240 / 100); o.puts ("\\slmult1"); }
		if (pf.pageBreak) o.puts ("\\pagebb");
		if (pf.keepNext) o.puts ("\\keepn");
		const CharFmt &f0 = d.fmt[q->len ? q->cf[0] : q->endCf];
		if (pf.list != LS_NONE)
		{
			int lv = pf.level & 7;
			if (pf.list == LS_NUMBER) { counter[lv]++; for (int k = lv + 1; k < 8; k++) counter[k] = 0; }
			o.puts ("{\\listtext\\f"); o.num (fmap[f0.font]); o.puts ("\\fs"); o.num (f0.size); o.put (' ');
			if (pf.list == LS_BULLET) o.puts ("\\'b7");
			else { o.num (counter[lv]); o.put ('.'); }
			o.puts ("\\tab}");
			o.puts (pf.list == LS_BULLET ? "{\\*\\pn\\pnlvlblt\\pnf0{\\pntxtb\\'b7}}" : "{\\*\\pn\\pnlvlbody\\pndec{\\pntxta .}}");
			if (lv) { o.puts ("\\ilvl"); o.num (lv); }
		}
		else for (int k = 0; k < 8; k++) counter[k] = 0;
		for (int i = 0; i < q->len; )
		{
			int j = i; while (j < q->len && q->cf[j] == q->cf[i]) j++;
			const CharFmt &f = d.fmt[q->cf[i]];
			o.puts ("{\\f"); o.num (fmap[f.font]); o.puts ("\\fs"); o.num (f.size);
			if (f.flags & CF_BOLD) o.puts ("\\b");
			if (f.flags & CF_ITALIC) o.puts ("\\i");
			if (f.flags & CF_UNDER) o.puts ("\\ul");
			if (f.flags & CF_STRIKE) o.puts ("\\strike");
			if (f.flags & CF_SUPER) o.puts ("\\super");
			if (f.flags & CF_SUB) o.puts ("\\sub");
			if (f.color != AUTO) { o.puts ("\\cf"); o.num (colIdx (f.color)); }
			if (f.hilite != AUTO) { o.puts ("\\highlight"); o.num (colIdx (f.hilite)); }
			o.put (' ');
			for (int k = i; k < j; k++)
			{
				if (q->ch[k] != OBJ_CHAR || !f.obj) { rtf_text (o, q->ch[k]); continue; }
				const Image &im = d.img[f.obj - 1];
				unsigned len; bool jpeg, made;
				const unsigned char *b = image_bytes (im, &len, &jpeg, &made);
				o.puts ("{\\pict"); o.puts (jpeg ? "\\jpegblip" : "\\pngblip");
				o.puts ("\\picw"); o.num (im.w); o.puts ("\\pich"); o.num (im.h);
				o.puts ("\\picwgoal"); o.num (f.ow); o.puts ("\\pichgoal"); o.num (f.oh); o.put ('\n');
				for (unsigned x = 0; x < len; x++) { o.hex2 (b[x]); if ((x & 63) == 63) o.put ('\n'); }
				o.puts ("}");
				if (made) delete[] b;
			}
			o.put ('}');
			i = j;
		}
		const CharFmt &e = d.fmt[q->endCf];
		o.puts ("{\\f"); o.num (fmap[e.font]); o.puts ("\\fs"); o.num (e.size); o.puts ("\\par}\n");
	}
	o.puts ("}\n");
	delete[] used; delete[] fmap;
	return o.n;
}

// ---- plain text ------------------------------------------------------------------------------------------
static void txt_load (Doc &d, const char *b, int n)
{
	doc_new (d);
	unsigned *u = new unsigned[n + 1];
	int m = decode_text (b, n, u, n + 1);
	int k = 0;
	for (int i = 0; i < m; i++) if (u[i] >= 32 || u[i] == '\n' || u[i] == '\t' || u[i] == 0x0C) u[k++] = u[i] == 0x0C ? '\n' : u[i];
	if (k > 0 && u[k - 1] == '\n') k--;			// (the last line's end: no empty paragraph after it)
	unsigned short cf = d.p[0]->endCf;
	doc_insert_raw (d, mkpos (0, 0), u, k, cf);
	delete[] u;
	for (int i = 0; i < d.n; i++) d.p[i]->pf.after = 0, d.p[i]->pf.line = 100;
}
static int txt_save (Doc &d, Out &o)
{
	o.init ();
	int total = doc_text_len (d, mkpos (0, 0), doc_end (d));
	unsigned *u = new unsigned[total + 1];
	int m = doc_text (d, mkpos (0, 0), doc_end (d), u, total);
	u[m++] = '\n';
	o.grow (m * 4 + 1);
	o.n = encode_text (u, m, o.b, o.cap);
	delete[] u;
	return o.n;
}

// ---- HTML export ------------------------------------------------------------------------------------------
static void html_text (Out &o, unsigned c)
{
	if (c == '<') o.puts ("&lt;"); else if (c == '>') o.puts ("&gt;"); else if (c == '&') o.puts ("&amp;");
	else if (c == 0xA0) o.puts ("&nbsp;"); else if (c == 0x0B) o.puts ("<br>"); else if (c == '\t') o.puts ("&emsp;");
	else o.utf8 (c);
}
static void html_color (Out &o, unsigned c) { o.put ('#'); o.hex2 (c >> 16 & 255); o.hex2 (c >> 8 & 255); o.hex2 (c & 255); }

static int html_save (Doc &d, Out &o, const char *title)
{
	o.init ();
	o.puts ("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>");
	for (const char *t = title; *t; t++) html_text (o, (unsigned char) *t);
	o.puts ("</title>\n<style>\nbody { max-width: 46em; margin: 2em auto; padding: 0 1em; font-family: 'Liberation Serif', 'Times New Roman', serif; font-size: 12pt; line-height: 1.3; }\n"
		"p, li { margin: 0 0 0.5em 0; } h1, h2, h3 { font-family: 'Liberation Sans', Arial, sans-serif; }\n"
		"blockquote { font-style: italic; color: #404040; } pre { font-family: 'DejaVu Sans Mono', monospace; }\n</style>\n</head>\n<body>\n");
	static const char *const tag[ST_COUNT] = { "p", "h1", "h2", "h3", "h1", "h2", "blockquote", "pre" };
	int openList = LS_NONE;
	for (int p = 0; p < d.n; p++)
	{
		const Para *q = d.p[p];
		const ParaFmt &pf = q->pf;
		if (pf.list != openList)
		{
			if (openList != LS_NONE) o.puts (openList == LS_BULLET ? "</ul>\n" : "</ol>\n");
			if (pf.list != LS_NONE) o.puts (pf.list == LS_BULLET ? "<ul>\n" : "<ol>\n");
			openList = pf.list;
		}
		const char *t = pf.list != LS_NONE ? "li" : tag[pf.style];
		o.put ('<'); o.puts (t);
		static const char *const al[4] = { "left", "center", "right", "justify" };
		bool style = pf.align != STYLES[pf.style].align || pf.left != STYLES[pf.style].left || pf.first || pf.pageBreak;
		if (style && pf.list == LS_NONE)
		{
			o.puts (" style=\"text-align:"); o.puts (al[pf.align & 3]);
			if (pf.left) { o.puts ("; margin-left:"); o.num (pf.left / 20); o.puts ("pt"); }
			if (pf.first) { o.puts ("; text-indent:"); o.num (pf.first / 20); o.puts ("pt"); }
			if (pf.pageBreak) o.puts ("; page-break-before:always");
			o.put ('"');
		}
		o.put ('>');
		CharFmt sf = style_fmt (d, pf.style);
		for (int i = 0; i < q->len; )
		{
			int j = i; while (j < q->len && q->cf[j] == q->cf[i]) j++;
			const CharFmt &f = d.fmt[q->cf[i]];
			bool span = f.font != sf.font || f.size != sf.size || f.color != AUTO || f.hilite != AUTO;
			bool b = (f.flags & CF_BOLD) && !(sf.flags & CF_BOLD), it = (f.flags & CF_ITALIC) && !(sf.flags & CF_ITALIC);
			if (span)
			{
				o.puts ("<span style=\"");
				if (f.font != sf.font) { o.puts ("font-family:'"); o.puts (d.fontName[f.font]); o.puts ("'; "); }
				if (f.size != sf.size) { o.puts ("font-size:"); o.num (f.size / 2); if (f.size & 1) o.puts (".5"); o.puts ("pt; "); }
				if (f.color != AUTO) { o.puts ("color:"); html_color (o, f.color); o.puts ("; "); }
				if (f.hilite != AUTO) { o.puts ("background:"); html_color (o, f.hilite); o.puts ("; "); }
				o.puts ("\">");
			}
			if (b) o.puts ("<b>");
			if (it) o.puts ("<i>");
			if (f.flags & CF_UNDER) o.puts ("<u>");
			if (f.flags & CF_STRIKE) o.puts ("<s>");
			if (f.flags & CF_SUPER) o.puts ("<sup>");
			if (f.flags & CF_SUB) o.puts ("<sub>");
			for (int k = i; k < j; k++)
			{
				if (q->ch[k] != OBJ_CHAR || !f.obj) { html_text (o, q->ch[k]); continue; }
				const Image &im = d.img[f.obj - 1];
				unsigned len; bool jpeg, made;
				const unsigned char *b = image_bytes (im, &len, &jpeg, &made);
				o.puts ("<img style=\"width:"); o.num (f.ow / 20); o.puts ("pt; height:"); o.num (f.oh / 20);
				o.puts ("pt; vertical-align:baseline\" alt=\"\" src=\"data:image/"); o.puts (jpeg ? "jpeg" : "png"); o.puts (";base64,");
				static const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				for (unsigned x = 0; x < len; x += 3)
				{
					unsigned v = (unsigned) b[x] << 16 | (x + 1 < len ? (unsigned) b[x + 1] << 8 : 0) | (x + 2 < len ? b[x + 2] : 0);
					o.put (B64[v >> 18 & 63]); o.put (B64[v >> 12 & 63]);
					o.put (x + 1 < len ? B64[v >> 6 & 63] : '='); o.put (x + 2 < len ? B64[v & 63] : '=');
				}
				o.puts ("\">");
				if (made) delete[] b;
			}
			if (f.flags & CF_SUB) o.puts ("</sub>");
			if (f.flags & CF_SUPER) o.puts ("</sup>");
			if (f.flags & CF_STRIKE) o.puts ("</s>");
			if (f.flags & CF_UNDER) o.puts ("</u>");
			if (it) o.puts ("</i>");
			if (b) o.puts ("</b>");
			if (span) o.puts ("</span>");
			i = j;
		}
		if (q->len == 0 && pf.list == LS_NONE) o.puts ("&nbsp;");
		o.puts ("</"); o.puts (t); o.puts (">\n");
	}
	if (openList != LS_NONE) o.puts (openList == LS_BULLET ? "</ul>\n" : "</ol>\n");
	o.puts ("</body>\n</html>\n");
	return o.n;
}

} // namespace wr

#endif
