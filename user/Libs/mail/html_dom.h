//
// mail/html_dom.h -- the HTML of a message read into a tree: the tags (HTML 4's, and what mail uses of HTML 5), their
// attributes, the text with its character references decoded (&eacute; &#233; &#xE9;), the <style> sheets kept for
// the cascade (mail/html_css.h), <script>, <title>, comments dropped. The tree is built forgivingly, as browsers do
// with HTML 4: <p> closed by a block, <li> by the next <li>, a <td> without its <tr> given one, the end tags with no
// start ignored. Part of Mail's own HTML renderer (mail/html.h; docs/mail/README.md).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_HTML_DOM_H
#define ONYX_MAIL_HTML_DOM_H

#include "mail/util.h"

namespace mail {
namespace html {

// ---- a small growing array of plain structs -----------------------------------------------------------------------------
template <class T> struct Vec
{
	T *p; int n, cap;
	Vec () : p (0), n (0), cap (0) {}
	~Vec () { free (p); }
	Vec (const Vec &) = delete;
	Vec &operator= (const Vec &) = delete;
	T &push () { if (n == cap) { int c = cap ? cap * 2 : 16; T *q = (T *) realloc (p, sizeof (T) * c); if (!q) return p[n - 1]; p = q; cap = c; } memset (&p[n], 0, sizeof (T)); return p[n++]; }
	void push (const T &v) { push () = v; }
	T &operator[] (int i) { return p[i]; }
	const T &operator[] (int i) const { return p[i]; }
	void clear () { n = 0; }
	void take (Vec &o) { free (p); p = o.p; n = o.n; cap = o.cap; o.p = 0; o.n = o.cap = 0; }
};
// strings kept for the tree's life (chunks never moved)
struct Arena
{
	struct Chunk { Chunk *next; int used, cap; char d[1]; };
	Chunk *head;
	Arena () : head (0) {}
	~Arena () { while (head) { Chunk *n = head->next; free (head); head = n; } }
	char *alloc (int n)
	{
		n = (n + 7) & ~7;
		if (!head || head->used + n > head->cap)
		{
			int c = n > 16000 ? n : 16000;
			Chunk *k = (Chunk *) malloc (sizeof (Chunk) + c); if (!k) return 0;
			k->next = head; k->used = 0; k->cap = c; head = k;
		}
		char *r = head->d + head->used; head->used += n; return r;
	}
	const char *dup (const char *s, int n) { char *r = alloc (n + 1); if (!r) return ""; memcpy (r, s, n); r[n] = 0; return r; }
};

// ---- the tags -------------------------------------------------------------------------------------------------------------
enum Tag
{
	T_TEXT = 0, T_ROOT, T_UNKNOWN,
	T_A, T_ABBR, T_ACRONYM, T_ADDRESS, T_ARTICLE, T_ASIDE, T_B, T_BASEFONT, T_BIG, T_BLOCKQUOTE, T_BODY, T_BR, T_BUTTON,
	T_CAPTION, T_CENTER, T_CITE, T_CODE, T_COL, T_COLGROUP, T_DD, T_DEL, T_DFN, T_DIR, T_DIV, T_DL, T_DT, T_EM, T_FIELDSET,
	T_FIGCAPTION, T_FIGURE, T_FONT, T_FOOTER, T_FORM, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6, T_HEAD, T_HEADER, T_HR, T_HTML,
	T_I, T_IMG, T_INPUT, T_INS, T_KBD, T_LABEL, T_LEGEND, T_LI, T_LINK, T_MAIN, T_MARK, T_MENU, T_META, T_NAV, T_NOBR,
	T_NOSCRIPT, T_OL, T_OPTION, T_P, T_PRE, T_Q, T_S, T_SAMP, T_SCRIPT, T_SECTION, T_SELECT, T_SMALL, T_SPAN, T_STRIKE,
	T_STRONG, T_STYLE, T_SUB, T_SUP, T_TABLE, T_TBODY, T_TD, T_TEXTAREA, T_TFOOT, T_TH, T_THEAD, T_TITLE, T_TR, T_TT, T_U,
	T_UL, T_VAR, T_WBR, T_XMP, T_AREA, T_MAP, T_IFRAME, T_OBJECT, T_EMBED, T_PARAM, T_SVG, T_VIDEO, T_AUDIO, T_SOURCE,
	T_PICTURE, T_TIME, T_SUMMARY, T_DETAILS, T_CENTERX,
	T_COUNT
};
static const char *const TAG_NAMES[T_COUNT] = {
	"#text", "#root", "#unknown",
	"a", "abbr", "acronym", "address", "article", "aside", "b", "basefont", "big", "blockquote", "body", "br", "button",
	"caption", "center", "cite", "code", "col", "colgroup", "dd", "del", "dfn", "dir", "div", "dl", "dt", "em", "fieldset",
	"figcaption", "figure", "font", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr", "html",
	"i", "img", "input", "ins", "kbd", "label", "legend", "li", "link", "main", "mark", "menu", "meta", "nav", "nobr",
	"noscript", "ol", "option", "p", "pre", "q", "s", "samp", "script", "section", "select", "small", "span", "strike",
	"strong", "style", "sub", "sup", "table", "tbody", "td", "textarea", "tfoot", "th", "thead", "title", "tr", "tt", "u",
	"ul", "var", "wbr", "xmp", "area", "map", "iframe", "object", "embed", "param", "svg", "video", "audio", "source",
	"picture", "time", "summary", "details", "#centerx" };
static int tag_of (const char *s, int n)
{
	for (int i = T_A; i < T_COUNT; i++)
	{
		const char *t = TAG_NAMES[i];
		if ((int) strlen (t) == n && ieqn (s, t, n)) return i;
	}
	return T_UNKNOWN;
}
static bool tag_void (int t)
{
	switch (t) { case T_AREA: case T_BASEFONT: case T_BR: case T_COL: case T_EMBED: case T_HR: case T_IMG: case T_INPUT:
	case T_LINK: case T_META: case T_PARAM: case T_SOURCE: case T_WBR: return true; default: return false; }
}
// the tags that end an open <p> (HTML 4's block elements, and HTML 5's)
static bool tag_closes_p (int t)
{
	switch (t) { case T_ADDRESS: case T_ARTICLE: case T_ASIDE: case T_BLOCKQUOTE: case T_CENTER: case T_DIR: case T_DIV: case T_DL:
	case T_FIELDSET: case T_FIGURE: case T_FOOTER: case T_FORM: case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6:
	case T_HEADER: case T_HR: case T_MAIN: case T_MENU: case T_NAV: case T_OL: case T_P: case T_PRE: case T_SECTION: case T_TABLE:
	case T_UL: case T_XMP: case T_DETAILS: case T_SUMMARY: return true; default: return false; }
}

// ---- character references ----------------------------------------------------------------------------------------------
// Latin-1's (160 .. 255, HTML 4) in order, then the others
static const char *const ENT_LATIN1[96] = {
	"nbsp", "iexcl", "cent", "pound", "curren", "yen", "brvbar", "sect", "uml", "copy", "ordf", "laquo", "not", "shy", "reg", "macr",
	"deg", "plusmn", "sup2", "sup3", "acute", "micro", "para", "middot", "cedil", "sup1", "ordm", "raquo", "frac14", "frac12", "frac34", "iquest",
	"Agrave", "Aacute", "Acirc", "Atilde", "Auml", "Aring", "AElig", "Ccedil", "Egrave", "Eacute", "Ecirc", "Euml", "Igrave", "Iacute", "Icirc", "Iuml",
	"ETH", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml", "times", "Oslash", "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "THORN", "szlig",
	"agrave", "aacute", "acirc", "atilde", "auml", "aring", "aelig", "ccedil", "egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc", "iuml",
	"eth", "ntilde", "ograve", "oacute", "ocirc", "otilde", "ouml", "divide", "oslash", "ugrave", "uacute", "ucirc", "uuml", "yacute", "thorn", "yuml" };
static const struct { const char *n; unsigned cp; } ENT_OTHER[] = {
	{ "quot", 34 }, { "amp", 38 }, { "apos", 39 }, { "lt", 60 }, { "gt", 62 }, { "OElig", 338 }, { "oelig", 339 }, { "Scaron", 352 },
	{ "scaron", 353 }, { "Yuml", 376 }, { "fnof", 402 }, { "circ", 710 }, { "tilde", 732 }, { "Alpha", 913 }, { "Beta", 914 },
	{ "Gamma", 915 }, { "Delta", 916 }, { "Omega", 937 }, { "alpha", 945 }, { "beta", 946 }, { "gamma", 947 }, { "delta", 948 },
	{ "pi", 960 }, { "mu", 956 }, { "omega", 969 }, { "ensp", 8194 }, { "emsp", 8195 }, { "thinsp", 8201 }, { "zwnj", 8204 },
	{ "zwj", 8205 }, { "lrm", 8206 }, { "rlm", 8207 }, { "ndash", 8211 }, { "mdash", 8212 }, { "lsquo", 8216 }, { "rsquo", 8217 },
	{ "sbquo", 8218 }, { "ldquo", 8220 }, { "rdquo", 8221 }, { "bdquo", 8222 }, { "dagger", 8224 }, { "Dagger", 8225 },
	{ "bull", 8226 }, { "hellip", 8230 }, { "permil", 8240 }, { "prime", 8242 }, { "Prime", 8243 }, { "lsaquo", 8249 },
	{ "rsaquo", 8250 }, { "oline", 8254 }, { "frasl", 8260 }, { "euro", 8364 }, { "trade", 8482 }, { "larr", 8592 },
	{ "uarr", 8593 }, { "rarr", 8594 }, { "darr", 8595 }, { "harr", 8596 }, { "lArr", 8656 }, { "rArr", 8658 }, { "minus", 8722 },
	{ "infin", 8734 }, { "ne", 8800 }, { "le", 8804 }, { "ge", 8805 }, { "loz", 9674 }, { "spades", 9824 }, { "clubs", 9827 },
	{ "hearts", 9829 }, { "diams", 9830 }, { "check", 10003 }, { "star", 9734 }, { "starf", 9733 }, { "nbhy", 8209 },
	{ "zwsp", 8203 }, { "NewLine", 10 }, { "Tab", 9 } };
// "&name;" at s (s at the '&') -> the character, *len the reference's length; 0: not a reference
static unsigned entity (const char *s, const char *end, int *len)
{
	const char *p = s + 1;
	if (p < end && *p == '#')
	{
		p++; unsigned v = 0; bool hex = p < end && (*p == 'x' || *p == 'X'); if (hex) p++;
		const char *d = p;
		while (p < end && (hex ? hexv (*p) >= 0 : (*p >= '0' && *p <= '9')) && p - d < 8) { v = v * (hex ? 16 : 10) + (unsigned) (hex ? hexv (*p) : *p - '0'); p++; }
		if (p == d) return 0;
		if (p < end && *p == ';') p++;
		*len = (int) (p - s);
		if (v >= 0x80 && v < 0xA0) { static const unsigned short W[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 }; v = W[v - 0x80]; }
		if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v < 0xE000)) v = 0xFFFD;
		return v;
	}
	const char *n = p; while (p < end && p - n < 10 && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) p++;
	int nl = (int) (p - n); if (!nl) return 0;
	// the longest known name (a missing ';' tolerated for Latin-1's, as browsers do)
	for (int l = nl; l >= 2; l--)
	{
		unsigned cp = 0;
		for (int i = 0; i < 96 && !cp; i++) if ((int) strlen (ENT_LATIN1[i]) == l && !memcmp (ENT_LATIN1[i], n, l)) cp = 160 + i;
		for (unsigned i = 0; i < sizeof ENT_OTHER / sizeof ENT_OTHER[0] && !cp; i++) if ((int) strlen (ENT_OTHER[i].n) == l && !memcmp (ENT_OTHER[i].n, n, l)) cp = ENT_OTHER[i].cp;
		if (!cp) continue;
		bool semi = n + l < end && n[l] == ';';
		if (!semi && l != nl) continue;				// ("&notit;" is not "¬it;")
		*len = (int) (n + l - s) + (semi ? 1 : 0);
		return cp;
	}
	return 0;
}
// text with its references decoded -> UTF-8 (s is UTF-8)
static void decode_text (Buf &o, const char *s, int n)
{
	const char *end = s + n;
	while (s < end)
	{
		const char *a = (const char *) memchr (s, '&', end - s);
		if (!a) { o.add (s, (int) (end - s)); break; }
		o.add (s, (int) (a - s));
		int l; unsigned cp = entity (a, end, &l);
		if (cp) { char t[4]; o.add (t, u8put (t, cp)); s = a + l; }
		else { o.addc ('&'); s = a + 1; }
	}
}

// ---- the tree ---------------------------------------------------------------------------------------------------------------
struct Attr { const char *name, *value; };		// (name lower case)
struct Node
{
	int tag;
	int parent, first, last, next, prev;		// (-1: none)
	const char *name;				// an unknown tag's name (lower case)
	const char *text; int tn;			// a text node's text (UTF-8, decoded)
	int attr, nattr;				// its attributes: Doc::attrs[attr .. attr + nattr - 1]
	int style;					// its computed style (html_css.h): Doc::styles[style]
	int index;					// its place among its parent's element children (1-based: :first-child)
};
struct Doc
{
	Arena arena;
	Vec<Node> nodes;
	Vec<Attr> attrs;
	Buf css;					// the <style> sheets, one after another
	char title[200];
	Doc () { title[0] = 0; }

	const char *attr (int node, const char *name) const
	{
		const Node &e = nodes[node];
		for (int i = 0; i < e.nattr; i++) if (!strcmp (attrs[e.attr + i].name, name)) return attrs[e.attr + i].value;
		return 0;
	}
	int add (int tag, int parent)
	{
		Node &n = nodes.push ();
		n.tag = tag; n.parent = parent; n.first = n.last = n.next = n.prev = -1; n.style = -1;
		int id = nodes.n - 1;
		if (parent >= 0)
		{
			Node &P = nodes[parent];
			if (P.last >= 0) { nodes[P.last].next = id; nodes[id].prev = P.last; } else P.first = id;
			P.last = id;
			if (tag != T_TEXT) { int k = 1; for (int c = nodes[parent].first; c >= 0 && c != id; c = nodes[c].next) if (nodes[c].tag != T_TEXT) k++; nodes[id].index = k; }
		}
		return id;
	}
	// ---- the parser ---------------------------------------------------------------------------------------------------
	int stack[256]; int depth;			// the open elements (stack[0]: the root)
	int top () const { return stack[depth - 1]; }
	int open_tag (int i) const { return nodes[stack[i]].tag; }
	// the open element of a tag, searched down to a boundary (a table's cells do not reach out of their table)
	int find_open (int tag, int stopA = -1, int stopB = -1, int stopC = -1) const
	{
		for (int i = depth - 1; i > 0; i--) { int t = open_tag (i); if (t == tag) return i; if (t == stopA || t == stopB || t == stopC) return -1; }
		return -1;
	}
	void close_to (int i) { if (i > 0) depth = i; }	// (the element at i closed, and what is inside it)
	void push_el (int tag, int at, int na, const char *name)
	{
		int id = add (tag, top ());
		nodes[id].attr = at; nodes[id].nattr = na; nodes[id].name = name;
		if (!tag_void (tag) && depth < 255) stack[depth++] = id;
	}
	void text (const char *s, int n)
	{
		if (n <= 0) return;
		int t = open_tag (depth - 1);
		// text right inside a table / row: browsers move it out (here: dropped when only spaces, else kept in place)
		if (t == T_TABLE || t == T_TR || t == T_TBODY || t == T_THEAD || t == T_TFOOT || t == T_HTML || t == T_HEAD)
		{ bool sp = true; for (int i = 0; i < n; i++) if (!strchr (" \t\r\n", s[i])) sp = false; if (sp) return; }
		Buf d; decode_text (d, s, n);
		// joined to the text just before (a reference split it)
		int p = top (); int l = nodes[p].last;
		if (l >= 0 && nodes[l].tag == T_TEXT)
		{
			Node &L = nodes[l]; char *j = arena.alloc (L.tn + d.n + 1); if (!j) return;
			memcpy (j, L.text, L.tn); memcpy (j + L.tn, d.c (), d.n); j[L.tn + d.n] = 0; L.text = j; L.tn += d.n;
			return;
		}
		int id = add (T_TEXT, p);
		nodes[id].text = arena.dup (d.c (), d.n); nodes[id].tn = d.n;
	}
	void start (int tag, int at, int na, const char *name)
	{
		switch (tag)
		{
		case T_HTML: case T_HEAD: return;
		case T_BODY:	// its attributes (bgcolor, text, link) given to the root
			if (!nodes[0].nattr) { nodes[0].attr = at; nodes[0].nattr = na; }
			return;
		default: break;
		}
		if (tag_closes_p (tag)) { int i = find_open (T_P, T_TABLE, T_TD, T_TH); if (i > 0) close_to (i); }
		if (tag == T_LI) { int i = find_open (T_LI, T_UL, T_OL, T_TABLE); if (i > 0) close_to (i); }
		if (tag == T_DT || tag == T_DD) { int i = find_open (T_DT, T_DL, T_TABLE); if (i > 0) close_to (i); i = find_open (T_DD, T_DL, T_TABLE); if (i > 0) close_to (i); }
		if (tag == T_OPTION) { int i = find_open (T_OPTION, T_SELECT); if (i > 0) close_to (i); }
		if (tag == T_A) { int i = find_open (T_A, T_TABLE, T_TD); if (i > 0) close_to (i); }
		if (tag == T_TR || tag == T_TBODY || tag == T_THEAD || tag == T_TFOOT)
		{	// the open cell / row of this table closed
			int tb = find_open (T_TABLE);
			for (int i = depth - 1; i > tb && tb > 0; i--) { int t = open_tag (i); if (t == T_TD || t == T_TH || t == T_TR || (tag != T_TR && (t == T_TBODY || t == T_THEAD || t == T_TFOOT))) { close_to (i); break; } }
			if (tb > 0) { int t = open_tag (depth - 1); if (t == T_TR || t == T_TD || t == T_TH) { int i = find_open (T_TR, T_TABLE); if (i > 0) close_to (i); } }
		}
		if (tag == T_TD || tag == T_TH)
		{
			int i = find_open (T_TD, T_TABLE); int j = find_open (T_TH, T_TABLE);
			if (j > i) i = j;
			if (i > 0) close_to (i);
			int t = open_tag (depth - 1);
			if (t == T_TABLE || t == T_TBODY || t == T_THEAD || t == T_TFOOT) push_el (T_TR, 0, 0, 0);
		}
		if (tag == T_TABLE)
		{	// a table in a table's row (not in a cell): a cell for it
			int t = open_tag (depth - 1);
			if (t == T_TR) push_el (T_TD, 0, 0, 0);
		}
		push_el (tag, at, na, name);
	}
	void end_tag (int tag)
	{
		if (tag == T_HTML || tag == T_HEAD || tag == T_BODY) return;
		if (tag == T_BR) { push_el (T_BR, 0, 0, 0); return; }		// (</br>: a <br>, as browsers)
		if (tag == T_P && find_open (T_P, T_TABLE, T_TD, T_TH) < 0) { push_el (T_P, 0, 0, 0); depth--; return; }	// (</p> alone: an empty paragraph)
		int stopA = -1, stopB = -1;
		if (tag != T_TABLE && tag != T_TD && tag != T_TH && tag != T_TR) { stopA = T_TD; stopB = T_TH; }
		int i = find_open (tag, stopA, stopB, tag == T_TABLE ? -1 : T_TABLE);
		if (i > 0) close_to (i);
	}
	void end_unknown (const char *name, int n)
	{
		for (int i = depth - 1; i > 0; i--)
		{
			const Node &e = nodes[stack[i]];
			if (e.tag == T_UNKNOWN && e.name && (int) strlen (e.name) == n && ieqn (e.name, name, n)) { close_to (i); return; }
			if (e.tag == T_TD || e.tag == T_TH || e.tag == T_TABLE) return;
		}
	}

	void parse (const char *s, int n)
	{
		nodes.clear (); attrs.clear (); css.clear (); title[0] = 0;
		add (T_ROOT, -1);
		depth = 1; stack[0] = 0;
		const char *p = s, *end = s + n, *t0 = p;
		while (p < end)
		{
			if (*p != '<') { p++; continue; }
			const char *lt = p;
			if (p + 1 >= end) break;
			char c = p[1];
			if (c == '!')
			{
				text (t0, (int) (lt - t0));
				if (p + 3 < end && p[2] == '-' && p[3] == '-')
				{	// a comment: up to "-->" (an IE conditional comment's inside dropped with it)
					const char *e = p + 4;
					while (e + 2 < end && !(e[0] == '-' && e[1] == '-' && e[2] == '>')) e++;
					p = e + 2 < end ? e + 3 : end;
				}
				else { while (p < end && *p != '>') p++; if (p < end) p++; }		// (<!DOCTYPE>, <![endif]>)
				t0 = p; continue;
			}
			if (c == '?') { text (t0, (int) (lt - t0)); while (p < end && *p != '>') p++; if (p < end) p++; t0 = p; continue; }
			bool close = c == '/';
			const char *nm = p + 1 + (close ? 1 : 0);
			if (nm >= end || !((*nm >= 'a' && *nm <= 'z') || (*nm >= 'A' && *nm <= 'Z'))) { p++; continue; }	// ("a < b": text)
			text (t0, (int) (lt - t0));
			const char *q = nm; while (q < end && !strchr (" \t\r\n/>", *q)) q++;
			int nl = (int) (q - nm);
			int tag = tag_of (nm, nl);
			const char *unk = 0;
			if (tag == T_UNKNOWN) { char *u = arena.alloc (nl + 1); for (int i = 0; i < nl; i++) u[i] = (char) lc (nm[i]); u[nl] = 0; unk = u; }
			// the attributes
			int at = attrs.n, na = 0;
			p = q;
			while (p < end && *p != '>')
			{
				while (p < end && strchr (" \t\r\n/", *p)) p++;
				if (p >= end || *p == '>') break;
				const char *an = p; while (p < end && !strchr (" \t\r\n/>=", *p)) p++;
				int anl = (int) (p - an);
				while (p < end && strchr (" \t\r\n", *p)) p++;
				const char *av = ""; int avl = 0;
				if (p < end && *p == '=')
				{
					p++; while (p < end && strchr (" \t\r\n", *p)) p++;
					if (p < end && (*p == '"' || *p == '\''))
					{ char qc = *p++; av = p; while (p < end && *p != qc) p++; avl = (int) (p - av); if (p < end) p++; }
					else { av = p; while (p < end && !strchr (" \t\r\n>", *p)) p++; avl = (int) (p - av); }
				}
				if (!close && anl > 0 && anl < 64)
				{
					char *k = arena.alloc (anl + 1); for (int i = 0; i < anl; i++) k[i] = (char) lc (an[i]); k[anl] = 0;
					bool dup = false; for (int i = 0; i < na; i++) if (!strcmp (attrs[at + i].name, k)) dup = true;
					if (!dup) { Buf v; decode_text (v, av, avl); Attr &a = attrs.push (); a.name = k; a.value = arena.dup (v.c (), v.n); na++; }
				}
				if (p == an) p++;
			}
			if (p < end) p++;
			t0 = p;
			if (close) { if (tag == T_UNKNOWN) end_unknown (nm, nl); else end_tag (tag); continue; }
			// the raw text elements: their inside up to their end tag
			if (tag == T_STYLE || tag == T_SCRIPT || tag == T_TITLE || tag == T_TEXTAREA || tag == T_XMP || tag == T_NOSCRIPT || tag == T_SVG)
			{
				const char *e = p;
				for (; e < end; e++)
					if (e[0] == '<' && e + 1 < end && e[1] == '/' && e + 2 + nl <= end && ieqn (e + 2, nm, nl) && (e + 2 + nl == end || strchr (" \t\r\n>", e[2 + nl]))) break;
				if (tag == T_STYLE) { css.add (p, (int) (e - p)); css.add ("\n"); }
				else if (tag == T_TITLE) { Buf d; decode_text (d, p, (int) (e - p)); scpy (title, d.c (), sizeof title); }
				else if (tag == T_TEXTAREA || tag == T_XMP) { start (tag, at, na, 0); text (p, (int) (e - p)); end_tag (tag); }
				p = e; while (p < end && *p != '>') p++; if (p < end) p++;
				t0 = p;
				continue;
			}
			start (tag, at, na, unk);
		}
		text (t0, (int) (end - t0));
	}
};

} // namespace html
} // namespace mail

#endif
