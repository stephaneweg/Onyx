//
// rtf.h -- Rich Text Format for wtk::RichTextBox: rtf_load parses an RTF document into the
// box (text + bold / italic / underline / strike / colour / highlight / size), rtf_save
// writes the box back as RTF. Used by the RTF reader (rtfview) and Writer (.rtf files).
//
// Reading: groups, control words and symbols; the colour table (colours map to the
// box's 16-colour palette, nearest match); \fs (half-points) -> size x1..x4; \par \line
// \tab; \'hh (Windows-1252 -> Latin-1, typographic punctuation folded to ASCII); \uN
// (+ the \ucN fallback skip); \b \i \ul \strike \cf \highlight \cb \plain; skipped
// destinations (\fonttbl, \stylesheet, \info, \pict, \*, fields' instructions, headers...).
// Paragraph layout (alignment, indents, tables) is not kept: table cells become tabs.
//
#ifndef _onyx_rtf_h
#define _onyx_rtf_h

#include "wtk/richtextbox.h"

namespace rtf {

using wtk::RtStyle;

// Windows-1252 0x80..0x9F -> Latin-1 / ASCII.
static inline const char *cp1252 (unsigned c)
{
	switch (c)
	{
	case 0x80: return "EUR"; case 0x82: return ","; case 0x84: return "\""; case 0x85: return "...";
	case 0x8B: return "<"; case 0x91: case 0x92: return "'"; case 0x93: case 0x94: return "\"";
	case 0x95: return "\xB7"; case 0x96: case 0x97: return "-"; case 0x99: return "(TM)"; case 0x9B: return ">";
	}
	return "";
}
static inline const char *unicode (long u, char one[2])
{
	if (u < 0) u += 65536;
	if (u < 0x80 || (u >= 0xA0 && u < 0x100)) { one[0] = (char) u; one[1] = 0; return one; }
	switch (u)
	{
	case 0x2018: case 0x2019: case 0x201A: return "'";
	case 0x201C: case 0x201D: case 0x201E: return "\"";
	case 0x2013: case 0x2014: case 0x2212: return "-";
	case 0x2026: return "...";
	case 0x2022: return "\xB7";
	case 0x20AC: return "EUR";
	case 0x00A0: case 0x2009: case 0x202F: return " ";
	}
	return "?";
}

// Nearest palette index to an RGB colour.
static inline int nearest (unsigned rgb)
{
	int best = 0; long bd = -1;
	for (int i = 0; i < 16; i++)
	{
		unsigned p = wtk::rt_color (i);
		long dr = (long) ((rgb >> 16) & 255) - (long) ((p >> 16) & 255), dg = (long) ((rgb >> 8) & 255) - (long) ((p >> 8) & 255),
		     db = (long) (rgb & 255) - (long) (p & 255);
		long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (bd < 0 || d < bd) { bd = d; best = i; }
	}
	return best;
}

struct State { RtStyle st; bool skip; int uc; };

class Reader
{
public:
	wtk::RichTextBox *box;
	const char *s; int n, p;
	State stack[64]; int depth;
	State cur;
	int colors[256]; int ncolors;			// \colortbl -> palette index (-1 = auto)
	bool inColorTbl; unsigned cr, cg, cb; bool haveColor;
	char run[512]; int rlen; RtStyle runSt;
	int pendingSkip;				// chars to drop after \uN

	void flush ()
	{
		if (!rlen) return;
		run[rlen] = 0;
		box->cur = runSt;
		box->insertText (run);
		rlen = 0;
	}
	void out (const char *t)
	{
		if (cur.skip) return;
		if (rlen && (runSt.flags != cur.st.flags || runSt.fg != cur.st.fg || runSt.bg != cur.st.bg || runSt.size != cur.st.size)) flush ();
		if (!rlen) runSt = cur.st;
		for (; *t; t++) { if (rlen >= (int) sizeof run - 1) { flush (); runSt = cur.st; } run[rlen++] = *t; }
	}
	void outc (char c) { char t[2] = { c, 0 }; out (t); }

	static bool eq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

	void word (const char *w, bool has, long v)
	{
		// destinations to skip entirely
		static const char *const SKIP[] = { "fonttbl", "stylesheet", "info", "pict", "object", "header", "footer",
			"headerl", "headerr", "headerf", "footerl", "footerr", "footerf", "fldinst", "listtable",
			"listoverridetable", "rsidtbl", "generator", "themedata", "colorschememapping", "latentstyles",
			"datastore", "xmlnstbl", "pgdsctbl", "filetbl", "revtbl", "bkmkstart", "bkmkend", "shppict",
			"nonshppict", "template", "userprops", "docvar", "comment", "annotation", "atnid", "atnauthor", "ftnsep",
			"ftnsepc", "aftnsep", "aftnsepc", "wgrffmtfilter", "mmathPr", "blipuid", "footnote" };
		for (unsigned i = 0; i < sizeof SKIP / sizeof SKIP[0]; i++) if (eq (w, SKIP[i])) { cur.skip = true; return; }
		if (eq (w, "colortbl")) { inColorTbl = true; ncolors = 0; cr = cg = cb = 0; haveColor = false; cur.skip = true; return; }
		if (inColorTbl)
		{
			if (eq (w, "red")) { cr = (unsigned) v; haveColor = true; }
			else if (eq (w, "green")) { cg = (unsigned) v; haveColor = true; }
			else if (eq (w, "blue")) { cb = (unsigned) v; haveColor = true; }
			return;
		}
		if (cur.skip) return;
		if (eq (w, "par") || eq (w, "line") || eq (w, "sect") || eq (w, "page") || eq (w, "row")) { out ("\n"); return; }
		if (eq (w, "tab") || eq (w, "cell")) { out ("    "); return; }
		if (eq (w, "plain")) { RtStyle d; cur.st = d; return; }
		if (eq (w, "b"))      { if (has && v == 0) cur.st.flags &= (unsigned char) ~wtk::RT_BOLD; else cur.st.flags |= wtk::RT_BOLD; return; }
		if (eq (w, "i"))      { if (has && v == 0) cur.st.flags &= (unsigned char) ~wtk::RT_ITALIC; else cur.st.flags |= wtk::RT_ITALIC; return; }
		if (eq (w, "ul") || eq (w, "uld") || eq (w, "uldb") || eq (w, "ulw") || eq (w, "ulth"))
		{ if (has && v == 0) cur.st.flags &= (unsigned char) ~wtk::RT_UNDER; else cur.st.flags |= wtk::RT_UNDER; return; }
		if (eq (w, "ulnone")) { cur.st.flags &= (unsigned char) ~wtk::RT_UNDER; return; }
		if (eq (w, "strike") || eq (w, "striked")) { if (has && v == 0) cur.st.flags &= (unsigned char) ~wtk::RT_STRIKE; else cur.st.flags |= wtk::RT_STRIKE; return; }
		if (eq (w, "cf")) { cur.st.fg = (unsigned char) (v > 0 && v < ncolors && colors[v] >= 0 ? colors[v] : wtk::RT_BLACK); return; }
		if (eq (w, "highlight") || eq (w, "cb") || eq (w, "chcbpat"))
		{
			if (v > 0 && v < ncolors && colors[v] >= 0) { cur.st.bg = (unsigned char) colors[v]; cur.st.flags |= wtk::RT_HILITE; }
			else cur.st.flags &= (unsigned char) ~wtk::RT_HILITE;
			return;
		}
		if (eq (w, "fs")) { cur.st.size = (unsigned char) (v <= 28 ? 1 : v <= 40 ? 2 : v <= 56 ? 3 : 4); return; }
		if (eq (w, "uc")) { cur.uc = (int) v; return; }
		if (eq (w, "u")) { char one[2]; out (unicode (v, one)); pendingSkip = cur.uc; return; }
		if (eq (w, "emdash") || eq (w, "endash")) { out ("-"); return; }
		if (eq (w, "bullet")) { out ("\xB7"); return; }
		if (eq (w, "lquote") || eq (w, "rquote")) { out ("'"); return; }
		if (eq (w, "ldblquote") || eq (w, "rdblquote")) { out ("\""); return; }
		// everything else (fonts, paragraph layout, ...) is ignored
	}

	void load (wtk::RichTextBox &b, const char *src, int len)
	{
		box = &b; s = src; n = len; p = 0; depth = 0; rlen = 0; ncolors = 0; inColorTbl = false; pendingSkip = 0;
		RtStyle d; cur.st = d; cur.skip = false; cur.uc = 1;
		for (int i = 0; i < 256; i++) colors[i] = -1;
		bool wasRO = box->readonly; box->readonly = false;
		box->setContent ("");
		while (p < n)
		{
			char c = s[p];
			if (c == '{')
			{
				p++;
				if (depth < 64) stack[depth] = cur;
				depth++;
				if (p + 1 < n && s[p] == '\\' && s[p + 1] == '*') { cur.skip = true; p += 2; }	// ignorable destination
				continue;
			}
			if (c == '}')
			{
				p++;
				if (inColorTbl && depth > 0 && stack[depth - 1].skip == false) inColorTbl = false;
				depth--;
				if (depth >= 0 && depth < 64) cur = stack[depth];
				if (depth <= 0) break;
				continue;
			}
			if (c == '\\')
			{
				p++;
				if (p >= n) break;
				char k = s[p];
				if ((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z'))
				{
					char w[32]; int wl = 0;
					while (p < n && ((s[p] >= 'a' && s[p] <= 'z') || (s[p] >= 'A' && s[p] <= 'Z'))) { if (wl < 31) w[wl++] = s[p]; p++; }
					w[wl] = 0;
					bool has = false, neg = false; long v = 0;
					if (p < n && s[p] == '-') { neg = true; p++; }
					while (p < n && s[p] >= '0' && s[p] <= '9') { v = v * 10 + (s[p] - '0'); p++; has = true; }
					if (neg) v = -v;
					if (p < n && s[p] == ' ') p++;			// the delimiter space
					bool wasU = w[0] == 'u' && w[1] == 0;
					word (w, has, v);
					if (!wasU) continue;
					// \uN: drop the next `uc` fallback characters (a \'hh counts as one)
					while (pendingSkip > 0 && p < n)
					{
						if (s[p] == '\\' && p + 1 < n && s[p + 1] == '\'') p += 4;
						else if (s[p] == '{' || s[p] == '}' || s[p] == '\\') break;
						else p++;
						pendingSkip--;
					}
					pendingSkip = 0;
					continue;
				}
				p++;
				switch (k)
				{
				case '\'':
				{
					unsigned h = 0;
					for (int i = 0; i < 2 && p < n; i++, p++)
					{
						char x = s[p]; h <<= 4;
						h |= (unsigned) (x >= '0' && x <= '9' ? x - '0' : x >= 'a' && x <= 'f' ? x - 'a' + 10 : x >= 'A' && x <= 'F' ? x - 'A' + 10 : 0);
					}
					if (h >= 0x80 && h < 0xA0) out (cp1252 (h));
					else outc ((char) h);
					break;
				}
				case '~': out (" "); break;
				case '_': out ("-"); break;
				case '-': break;						// optional hyphen
				case '\\': case '{': case '}': outc (k); break;
				case '\n': case '\r': out ("\n"); break;			// \<newline> = \par
				case '*': cur.skip = true; break;
				default: break;
				}
				continue;
			}
			if (c == '\r' || c == '\n') { p++; continue; }
			if (inColorTbl && c == ';')
			{
				if (ncolors < 256) colors[ncolors++] = haveColor ? nearest ((cr << 16) | (cg << 8) | cb) : -1;
				cr = cg = cb = 0; haveColor = false; p++;
				continue;
			}
			// plain text: take a whole stretch
			int e = p;
			while (e < n && s[e] != '\\' && s[e] != '{' && s[e] != '}' && s[e] != '\r' && s[e] != '\n') e++;
			if (!cur.skip)
			{
				char chunk[256];
				while (p < e)
				{
					int k = 0;
					while (p < e && k < 255) chunk[k++] = s[p++];
					chunk[k] = 0;
					out (chunk);
				}
			}
			p = e;
		}
		flush ();
		// drop trailing blank lines
		while (box->len > 0 && box->buf[box->len - 1] == '\n') { box->len--; box->buf[box->len] = 0; }
		box->caret = 0; box->sel = -1; box->setTopRow (0);
		RtStyle d2; box->cur = d2;
		box->readonly = wasRO;
		box->invalidate (true);
	}
};

// Is this RTF? ("{\rtf")
static inline bool is_rtf (const char *s, int n) { return n >= 5 && s[0] == '{' && s[1] == '\\' && s[2] == 'r' && s[3] == 't' && s[4] == 'f'; }

static inline void load (wtk::RichTextBox &b, const char *src, int len) { static Reader r; r.load (b, src, len); }

// Write the box as RTF into out[cap]; returns the length (0 = didn't fit).
static inline int save (const wtk::RichTextBox &b, char *out, int cap)
{
	int n = 0; bool ok = true;
	auto put = [&] (const char *t) { while (*t) { if (n >= cap - 1) { ok = false; return; } out[n++] = *t++; } };
	auto num = [&] (long v) { char t[16]; int k = 0; if (v == 0) t[k++] = '0'; while (v) { t[k++] = (char) ('0' + v % 10); v /= 10; } char r[16]; int m = 0; while (k) r[m++] = t[--k]; r[m] = 0; put (r); };
	put ("{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Helvetica;}}\n{\\colortbl;");
	for (int i = 0; i < 16; i++)
	{
		unsigned c = wtk::rt_color (i);
		put ("\\red"); num ((c >> 16) & 255); put ("\\green"); num ((c >> 8) & 255); put ("\\blue"); num (c & 255); put (";");
	}
	put ("}\n\\f0\\fs24 ");
	RtStyle prev; bool first = true;
	for (int i = 0; i < b.len && ok; i++)
	{
		RtStyle st = wtk::rt_unpack (b.attr[i]);
		if (first || st.flags != prev.flags || st.fg != prev.fg || st.bg != prev.bg || st.size != prev.size)
		{
			put ("\\plain\\f0");
			if (st.flags & wtk::RT_BOLD) put ("\\b");
			if (st.flags & wtk::RT_ITALIC) put ("\\i");
			if (st.flags & wtk::RT_UNDER) put ("\\ul");
			if (st.flags & wtk::RT_STRIKE) put ("\\strike");
			if (st.fg != wtk::RT_BLACK) { put ("\\cf"); num (st.fg + 1); }
			if (st.flags & wtk::RT_HILITE) { put ("\\highlight"); num (st.bg + 1); }
			put ("\\fs"); num (st.size <= 1 ? 24 : st.size == 2 ? 36 : st.size == 3 ? 48 : 12 * (st.size + 1));
			put (" ");
			prev = st; first = false;
		}
		unsigned char c = (unsigned char) b.buf[i];
		if (c == '\n') put ("\\par\n");
		else if (c == '\\' || c == '{' || c == '}') { char t[3] = { '\\', (char) c, 0 }; put (t); }
		else if (c >= 0x80) { static const char hx[] = "0123456789abcdef"; char t[5] = { '\\', '\'', hx[c >> 4], hx[c & 15], 0 }; put (t); }
		else if (c >= 0x20) { char t[2] = { (char) c, 0 }; put (t); }
	}
	put ("\\par\n}\n");
	if (!ok) return 0;
	out[n] = 0;
	return n;
}

} // namespace rtf

#endif
