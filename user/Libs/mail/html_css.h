//
// mail/html_css.h -- CSS 2 for Mail's HTML renderer: the sheets parsed (rules, selectors, declarations; @media kept
// when it applies to a screen this wide, other at-rules skipped), the selectors matched (type, .class, #id, *, [attr],
// [attr=v], [attr~=v], [attr^=v], [attr*=v], :first-child, :link, descendant, child, adjacent), the cascade (a user
// agent sheet of HTML 4's looks, the presentational attributes -- bgcolor, align, width, <font>... --, the author's
// sheets by specificity and order, style="", !important) and the computed style of each element (inherited where CSS
// says so; lengths in px, percentages kept for the layout). Part of Mail's own HTML renderer (mail/html.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_HTML_CSS_H
#define ONYX_MAIL_HTML_CSS_H

#include "mail/html_dom.h"

namespace mail {
namespace html {

// ---- values ---------------------------------------------------------------------------------------------------------------
enum { U_NONE = 0, U_PX, U_PCT, U_AUTO };
struct Len { float v; unsigned char u; };				// (px once computed; % kept; auto)
static inline Len px (float v) { Len l; l.v = v; l.u = U_PX; return l; }
static inline Len pct (float v) { Len l; l.v = v; l.u = U_PCT; return l; }
static inline Len autol () { Len l; l.v = 0; l.u = U_AUTO; return l; }
static inline Len nonel () { Len l; l.v = 0; l.u = U_NONE; return l; }
static inline int resolve (const Len &l, int base, int def = 0) { return l.u == U_PX ? (int) (l.v + 0.5f) : l.u == U_PCT ? (int) (l.v * base / 100.0f + 0.5f) : def; }

enum Display { D_INLINE, D_BLOCK, D_LIST_ITEM, D_INLINE_BLOCK, D_TABLE, D_INLINE_TABLE, D_ROW_GROUP, D_HEADER_GROUP, D_FOOTER_GROUP,
	D_ROW, D_CELL, D_CAPTION, D_COLUMN, D_COLUMN_GROUP, D_NONE };
enum { WS_NORMAL, WS_NOWRAP, WS_PRE, WS_PRE_WRAP, WS_PRE_LINE };
enum { TA_LEFT, TA_RIGHT, TA_CENTER, TA_JUSTIFY };
enum { VA_BASELINE, VA_TOP, VA_MIDDLE, VA_BOTTOM, VA_SUB, VA_SUPER, VA_TEXT_TOP, VA_TEXT_BOTTOM };
enum { FF_SANS, FF_SERIF, FF_MONO, FF_GEORGIA, FF_SEGOE, FF_COUNT };
enum { TD_UNDERLINE = 1, TD_LINE_THROUGH = 2, TD_OVERLINE = 4 };
enum { LS_DISC, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_LOWER_ALPHA, LS_UPPER_ALPHA, LS_LOWER_ROMAN, LS_UPPER_ROMAN, LS_NONE };
enum { BS_NONE, BS_SOLID, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_GROOVE, BS_RIDGE, BS_INSET, BS_OUTSET, BS_HIDDEN };
enum { FL_NONE, FL_LEFT, FL_RIGHT };
enum { TT_NONE, TT_UPPER, TT_LOWER, TT_CAPITALIZE };

struct Style
{
	// inherited
	unsigned color; int fontSize; unsigned char family, bold, italic, whiteSpace, textAlign, listStyle, transform, visible, collapse;
	Len lineHeight; float lineNum;				// (lineNum > 0: a factor of the font size)
	int borderSpacing; Len textIndent;
	// not inherited
	unsigned char display, vAlign, decoration, floatSide, clearSide, noWrapCell;
	unsigned bg;						// 0xAARRGGBB (A 0: none)
	Len width, height, minWidth, maxWidth, minHeight;
	Len margin[4], padding[4];				// top, right, bottom, left
	int borderW[4]; unsigned char borderS[4]; unsigned borderC[4]; bool borderCSet[4];
	unsigned decoColor;
	int link;						// the <a href>'s node (inherited: what a click opens), -1 none
	const char *bgImage;					// a background picture's url (cid: shown, else as the pictures)
};

// ---- colours ----------------------------------------------------------------------------------------------------------
static const struct { const char *n; unsigned c; } COLOURS[] = {
	{ "black", 0x000000 }, { "silver", 0xC0C0C0 }, { "gray", 0x808080 }, { "grey", 0x808080 }, { "white", 0xFFFFFF }, { "maroon", 0x800000 },
	{ "red", 0xFF0000 }, { "purple", 0x800080 }, { "fuchsia", 0xFF00FF }, { "magenta", 0xFF00FF }, { "green", 0x008000 }, { "lime", 0x00FF00 },
	{ "olive", 0x808000 }, { "yellow", 0xFFFF00 }, { "navy", 0x000080 }, { "blue", 0x0000FF }, { "teal", 0x008080 }, { "aqua", 0x00FFFF },
	{ "cyan", 0x00FFFF }, { "orange", 0xFFA500 }, { "darkgray", 0xA9A9A9 }, { "darkgrey", 0xA9A9A9 }, { "lightgray", 0xD3D3D3 },
	{ "lightgrey", 0xD3D3D3 }, { "dimgray", 0x696969 }, { "gainsboro", 0xDCDCDC }, { "whitesmoke", 0xF5F5F5 }, { "darkblue", 0x00008B },
	{ "darkred", 0x8B0000 }, { "darkgreen", 0x006400 }, { "lightblue", 0xADD8E6 }, { "lightgreen", 0x90EE90 }, { "pink", 0xFFC0CB },
	{ "brown", 0xA52A2A }, { "gold", 0xFFD700 }, { "beige", 0xF5F5DC }, { "ivory", 0xFFFFF0 }, { "lavender", 0xE6E6FA },
	{ "orangered", 0xFF4500 }, { "tomato", 0xFF6347 }, { "crimson", 0xDC143C }, { "royalblue", 0x4169E1 }, { "steelblue", 0x4682B4 },
	{ "dodgerblue", 0x1E90FF }, { "skyblue", 0x87CEEB }, { "slategray", 0x708090 }, { "darkslategray", 0x2F4F4F }, { "indigo", 0x4B0082 },
	{ "violet", 0xEE82EE }, { "coral", 0xFF7F50 }, { "salmon", 0xFA8072 }, { "khaki", 0xF0E68C }, { "tan", 0xD2B48C }, { "chocolate", 0xD2691E },
	{ "firebrick", 0xB22222 }, { "seagreen", 0x2E8B57 }, { "forestgreen", 0x228B22 }, { "limegreen", 0x32CD32 }, { "midnightblue", 0x191970 },
	{ "aliceblue", 0xF0F8FF }, { "ghostwhite", 0xF8F8FF }, { "snow", 0xFFFAFA }, { "linen", 0xFAF0E6 }, { "mintcream", 0xF5FFFA },
	{ "honeydew", 0xF0FFF0 }, { "azure", 0xF0FFFF }, { "lightyellow", 0xFFFFE0 }, { "lightcyan", 0xE0FFFF }, { "darkorange", 0xFF8C00 } };
// a colour -> 0xAARRGGBB; false: not one ("transparent": alpha 0)
static bool parse_colour (const char *s, int n, unsigned *out)
{
	while (n && (*s == ' ' || *s == '\t')) s++, n--;
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
	if (!n) return false;
	if (*s == '#' || (n == 6 && hexv (s[0]) >= 0 && hexv (s[1]) >= 0 && hexv (s[2]) >= 0 && hexv (s[5]) >= 0))	// (bgcolor="ffffff": browsers take it)
	{
		const char *h = *s == '#' ? s + 1 : s; int k = (int) (n - (h - s));
		for (int i = 0; i < k; i++) if (hexv (h[i]) < 0) return false;
		if (k == 3 || k == 4) { unsigned r = hexv (h[0]) * 17, g = hexv (h[1]) * 17, b = hexv (h[2]) * 17, a = k == 4 ? hexv (h[3]) * 17 : 255; *out = a << 24 | r << 16 | g << 8 | b; return true; }
		if (k == 6 || k == 8) { unsigned v = 0; for (int i = 0; i < 6; i++) v = v << 4 | (unsigned) hexv (h[i]); unsigned a = k == 8 ? (unsigned) (hexv (h[6]) * 16 + hexv (h[7])) : 255; *out = a << 24 | v; return true; }
		return false;
	}
	if (n >= 4 && (ieqn (s, "rgb(", 4) || ieqn (s, "rgba(", 5)))
	{
		const char *p = strchr (s, '(') + 1; float v[4] = { 0, 0, 0, 1 }; int k = 0;
		while (k < 4 && p < s + n)
		{
			while (p < s + n && (*p == ' ' || *p == ',' || *p == '/')) p++;
			char *e; float f = strtof (p, &e); if (e == p) break;
			if (*e == '%') { f = k < 3 ? f * 2.55f : f / 100.0f; e++; }
			v[k++] = f; p = e;
		}
		if (k < 3) return false;
		for (int i = 0; i < 3; i++) v[i] = v[i] < 0 ? 0 : v[i] > 255 ? 255 : v[i];
		float a = v[3] < 0 ? 0 : v[3] > 1 ? 1 : v[3];
		*out = (unsigned) (a * 255 + 0.5f) << 24 | (unsigned) (v[0] + 0.5f) << 16 | (unsigned) (v[1] + 0.5f) << 8 | (unsigned) (v[2] + 0.5f);
		return true;
	}
	if (n == 11 && ieqn (s, "transparent", 11)) { *out = 0; return true; }
	for (unsigned i = 0; i < sizeof COLOURS / sizeof COLOURS[0]; i++)
		if ((int) strlen (COLOURS[i].n) == n && ieqn (s, COLOURS[i].n, n)) { *out = 0xFF000000u | COLOURS[i].c; return true; }
	return false;
}

// ---- the sheets ---------------------------------------------------------------------------------------------------------------
enum { SEL_DESC = 0, SEL_CHILD, SEL_ADJ, SEL_SIB };
enum { AT_HAS, AT_EQ, AT_WORD, AT_PREFIX, AT_SUFFIX, AT_SUB, AT_DASH };
struct Simple					// one compound selector: tag.class#id[attr]:pseudo, and what joins it to the one before
{
	int tag;				// -1: any (an unknown tag's name: name)
	const char *name;
	const char *id;
	const char *cls[4]; int ncls;
	struct { const char *name, *value; unsigned char op; } at[3]; int nat;
	unsigned char firstChild, lastChild, link, never;	// (never: :hover and the like -- the rule ignored)
	unsigned char comb;			// how it joins to the previous one
};
struct Selector { int first, n; int spec; };		// Sheet::simples[first .. first + n - 1], left to right
struct Decl { int prop; const char *value; bool important; };
struct Rule { int sel; int decl, ndecl; int order; };
struct Sheet
{
	Arena arena;
	Vec<Simple> simples;
	Vec<Selector> sels;
	Vec<Decl> decls;
	Vec<Rule> rules;
	int order;
	Sheet () : order (0) {}
	void reset () { while (arena.head) { Arena::Chunk *n = arena.head->next; free (arena.head); arena.head = n; } simples.clear (); sels.clear (); decls.clear (); rules.clear (); order = 0; }
};

// the properties known
enum Prop {
	P_DISPLAY, P_COLOR, P_BACKGROUND, P_BACKGROUND_COLOR, P_BACKGROUND_IMAGE, P_FONT, P_FONT_FAMILY, P_FONT_SIZE, P_FONT_WEIGHT,
	P_FONT_STYLE, P_TEXT_DECORATION, P_TEXT_ALIGN, P_VERTICAL_ALIGN, P_LINE_HEIGHT, P_WHITE_SPACE, P_MARGIN, P_MARGIN_TOP,
	P_MARGIN_RIGHT, P_MARGIN_BOTTOM, P_MARGIN_LEFT, P_PADDING, P_PADDING_TOP, P_PADDING_RIGHT, P_PADDING_BOTTOM, P_PADDING_LEFT,
	P_BORDER, P_BORDER_TOP, P_BORDER_RIGHT, P_BORDER_BOTTOM, P_BORDER_LEFT, P_BORDER_WIDTH, P_BORDER_STYLE, P_BORDER_COLOR,
	P_BORDER_TOP_WIDTH, P_BORDER_RIGHT_WIDTH, P_BORDER_BOTTOM_WIDTH, P_BORDER_LEFT_WIDTH, P_BORDER_TOP_STYLE,
	P_BORDER_RIGHT_STYLE, P_BORDER_BOTTOM_STYLE, P_BORDER_LEFT_STYLE, P_BORDER_TOP_COLOR, P_BORDER_RIGHT_COLOR,
	P_BORDER_BOTTOM_COLOR, P_BORDER_LEFT_COLOR, P_WIDTH, P_HEIGHT, P_MIN_WIDTH, P_MAX_WIDTH, P_MIN_HEIGHT, P_LIST_STYLE,
	P_LIST_STYLE_TYPE, P_TEXT_TRANSFORM, P_TEXT_INDENT, P_FLOAT, P_CLEAR, P_VISIBILITY, P_BORDER_COLLAPSE, P_BORDER_SPACING,
	P_TEXT_DECORATION_LINE, P_TEXT_DECORATION_COLOR, P_MAX_HEIGHT, P_OVERFLOW, P_MSO_HIDE, P_COUNT
};
static const char *const PROP_NAMES[P_COUNT] = {
	"display", "color", "background", "background-color", "background-image", "font", "font-family", "font-size", "font-weight",
	"font-style", "text-decoration", "text-align", "vertical-align", "line-height", "white-space", "margin", "margin-top",
	"margin-right", "margin-bottom", "margin-left", "padding", "padding-top", "padding-right", "padding-bottom", "padding-left",
	"border", "border-top", "border-right", "border-bottom", "border-left", "border-width", "border-style", "border-color",
	"border-top-width", "border-right-width", "border-bottom-width", "border-left-width", "border-top-style",
	"border-right-style", "border-bottom-style", "border-left-style", "border-top-color", "border-right-color",
	"border-bottom-color", "border-left-color", "width", "height", "min-width", "max-width", "min-height", "list-style",
	"list-style-type", "text-transform", "text-indent", "float", "clear", "visibility", "border-collapse", "border-spacing",
	"text-decoration-line", "text-decoration-color", "max-height", "overflow", "mso-hide" };
static int prop_of (const char *s, int n)
{
	for (int i = 0; i < P_COUNT; i++) if ((int) strlen (PROP_NAMES[i]) == n && ieqn (s, PROP_NAMES[i], n)) return i;
	return -1;
}

static inline bool is_ident (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || (unsigned char) c >= 0x80 || c == '\\'; }
static const char *skip_ws_comments (const char *p, const char *end)
{
	for (;;)
	{
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '\f')) p++;
		if (p + 1 < end && p[0] == '/' && p[1] == '*') { p += 2; while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++; p = p + 2 <= end ? p + 2 : end; continue; }
		if (p + 3 < end && !memcmp (p, "<!--", 4)) { p += 4; continue; }
		if (p + 2 < end && !memcmp (p, "-->", 3)) { p += 3; continue; }
		return p;
	}
}
// a selector group's one selector (s .. e) -> its index in sh.sels, -1 when not understood (the rule dropped)
static int parse_selector (Sheet &sh, const char *s, const char *e)
{
	Selector S; S.first = sh.simples.n; S.n = 0; S.spec = 0;
	const char *p = s; int comb = SEL_DESC;
	while (p < e)
	{
		while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
		if (p >= e) break;
		if (*p == '>' || *p == '+' || *p == '~') { comb = *p == '>' ? SEL_CHILD : *p == '+' ? SEL_ADJ : SEL_SIB; p++; continue; }
		Simple &m = sh.simples.push (); m.tag = -1; m.comb = (unsigned char) comb; S.n++;
		comb = SEL_DESC;
		bool any = false;
		while (p < e && !(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '>' || *p == '+' || *p == '~'))
		{
			if (*p == '*') { p++; any = true; continue; }
			if (*p == '.' || *p == '#')
			{
				char k = *p++; const char *a = p; while (p < e && is_ident (*p)) p++;
				const char *v = sh.arena.dup (a, (int) (p - a));
				if (k == '#') { m.id = v; S.spec += 10000; } else { if (m.ncls < 4) m.cls[m.ncls++] = v; S.spec += 100; }
				any = true; continue;
			}
			if (*p == '[')
			{
				p++; const char *a = p; while (p < e && is_ident (*p)) p++;
				int an = (int) (p - a); unsigned char op = AT_HAS; const char *v = 0;
				while (p < e && *p == ' ') p++;
				if (p < e && *p != ']')
				{
					if (*p == '=') { op = AT_EQ; p++; }
					else if (p + 1 < e && p[1] == '=') { op = *p == '~' ? AT_WORD : *p == '^' ? AT_PREFIX : *p == '$' ? AT_SUFFIX : *p == '*' ? AT_SUB : *p == '|' ? AT_DASH : AT_EQ; p += 2; }
					while (p < e && *p == ' ') p++;
					const char *b = p; int bn;
					if (p < e && (*p == '"' || *p == '\'')) { char q = *p++; b = p; while (p < e && *p != q) p++; bn = (int) (p - b); if (p < e) p++; }
					else { while (p < e && *p != ']' && *p != ' ') p++; bn = (int) (p - b); }
					v = sh.arena.dup (b, bn);
				}
				while (p < e && *p != ']') p++; if (p < e) p++;
				if (m.nat < 3) { char *n = (char *) sh.arena.dup (a, an); for (char *c = n; *c; c++) *c = (char) lc (*c); m.at[m.nat].name = n; m.at[m.nat].value = v; m.at[m.nat].op = op; m.nat++; }
				S.spec += 100; any = true; continue;
			}
			if (*p == ':')
			{
				p++; bool el = p < e && *p == ':'; if (el) p++;
				const char *a = p; while (p < e && is_ident (*p)) p++;
				int an = (int) (p - a);
				if (p < e && *p == '(') { int d = 0; while (p < e) { if (*p == '(') d++; else if (*p == ')' && --d == 0) { p++; break; } p++; } m.never = 1; }
				if (el || (an == 6 && ieqn (a, "before", 6)) || (an == 5 && ieqn (a, "after", 5)) || (an == 10 && ieqn (a, "first-line", 10)) || (an == 12 && ieqn (a, "first-letter", 12))) m.never = 1;
				else if (an == 11 && ieqn (a, "first-child", 11)) m.firstChild = 1;
				else if (an == 10 && ieqn (a, "last-child", 10)) m.lastChild = 1;
				else if (an == 4 && ieqn (a, "link", 4)) m.link = 1;
				else if (an == 4 && ieqn (a, "root", 4)) { m.tag = T_ROOT; }
				else m.never = 1;						// (:hover, :visited, :active, :focus...)
				S.spec += 100; any = true; continue;
			}
			if (is_ident (*p))
			{
				const char *a = p; while (p < e && is_ident (*p)) p++;
				int t = tag_of (a, (int) (p - a));
				if (t == T_HTML || t == T_BODY) t = T_ROOT;			// (body: the root here)
				m.tag = t;
				if (t == T_UNKNOWN) { char *n = (char *) sh.arena.dup (a, (int) (p - a)); for (char *c = n; *c; c++) *c = (char) lc (*c); m.name = n; }
				S.spec += 1; any = true; continue;
			}
			return -1;							// (not understood)
		}
		if (!any) return -1;
	}
	if (!S.n) return -1;
	sh.sels.push (S);
	return sh.sels.n - 1;
}
// declarations "a: b; c: d !important" -> sh.decls; how many
static int parse_decls (Sheet &sh, const char *p, const char *e)
{
	int k = 0;
	while (p < e)
	{
		p = skip_ws_comments (p, e);
		const char *n = p; while (p < e && *p != ':' && *p != ';') p++;
		if (p >= e || *p == ';') { p++; continue; }
		int nl = (int) (p - n); while (nl && (n[nl - 1] == ' ' || n[nl - 1] == '\t' || n[nl - 1] == '\r' || n[nl - 1] == '\n')) nl--;
		p++;
		const char *v = p; int par = 0; char q = 0;
		while (p < e && (q || par || *p != ';')) { if (q) { if (*p == q) q = 0; } else if (*p == '"' || *p == '\'') q = *p; else if (*p == '(') par++; else if (*p == ')' && par) par--; p++; }
		const char *ve = p; if (p < e) p++;
		while (v < ve && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')) v++;
		while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r' || ve[-1] == '\n')) ve--;
		bool imp = false;
		const char *bang = 0; for (const char *c = v; c < ve; c++) if (*c == '!') bang = c;
		if (bang && ve - bang >= 10 && ieqn (bang + 1, "important", 9)) { imp = true; ve = bang; while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) ve--; }
		else if (bang) { const char *c = bang + 1; while (c < ve && *c == ' ') c++; if (ve - c == 9 && ieqn (c, "important", 9)) { imp = true; ve = bang; while (ve > v && ve[-1] == ' ') ve--; } }
		int pr = prop_of (n, nl);
		if (pr < 0) continue;
		Decl &d = sh.decls.push (); d.prop = pr; d.value = sh.arena.dup (v, (int) (ve - v)); d.important = imp; k++;
	}
	return k;
}
// @media's query: true when it applies to a screen viewW px wide
static bool media_applies (const char *s, const char *e, int viewW)
{
	// a list: any of its queries
	const char *q = s;
	while (q < e)
	{
		const char *qe = q; int par = 0; while (qe < e && (par || *qe != ',')) { if (*qe == '(') par++; else if (*qe == ')') par--; qe++; }
		bool ok = true, sawType = false; bool notq = false;
		for (const char *p = q; p < qe; )
		{
			while (p < qe && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
			if (p >= qe) break;
			if (*p == '(')
			{
				const char *c = p + 1; const char *ce = c; while (ce < qe && *ce != ')') ce++;
				const char *colon = (const char *) memchr (c, ':', ce - c);
				if (colon)
				{
					const char *f = c; while (*f == ' ') f++;
					int fl = (int) (colon - f); while (fl && f[fl - 1] == ' ') fl--;
					float v = strtof (colon + 1, 0);
					const char *u = colon + 1; while (u < ce && (*u == ' ' || (*u >= '0' && *u <= '9') || *u == '.')) u++;
					if (u + 1 < ce && ieqn (u, "em", 2)) v *= 16;
					if ((fl == 9 && ieqn (f, "max-width", 9)) || (fl == 16 && ieqn (f, "max-device-width", 16))) { if (!(viewW <= v)) ok = false; }
					else if ((fl == 9 && ieqn (f, "min-width", 9)) || (fl == 16 && ieqn (f, "min-device-width", 16))) { if (!(viewW >= v)) ok = false; }
					else if (fl == 23 && ieqn (f, "-webkit-min-device-pixel-ratio", 23)) ok = false;
					else if (!(fl >= 6 && ieqn (f, "prefers", 7))) {}			// (orientation...: taken)
					else ok = false;							// (prefers-color-scheme: dark... the light look)
				}
				p = ce < qe ? ce + 1 : qe; continue;
			}
			const char *w = p; while (p < qe && is_ident (*p)) p++;
			int wl = (int) (p - w); if (!wl) { p++; continue; }
			if (wl == 3 && ieqn (w, "and", 3)) continue;
			if (wl == 4 && ieqn (w, "only", 4)) continue;
			if (wl == 3 && ieqn (w, "not", 3)) { notq = true; continue; }
			sawType = true;
			if (!((wl == 6 && ieqn (w, "screen", 6)) || (wl == 3 && ieqn (w, "all", 3)))) ok = false;
		}
		(void) sawType;
		if (notq) ok = !ok;
		if (ok) return true;
		q = qe < e ? qe + 1 : e;
	}
	return false;
}
// a whole sheet (or a @media block's inside) added
static void parse_sheet (Sheet &sh, const char *s, int n, int viewW)
{
	const char *p = s, *end = s + n;
	while (p < end)
	{
		p = skip_ws_comments (p, end);
		if (p >= end) break;
		if (*p == '}') { p++; continue; }
		if (*p == '@')
		{
			const char *a = p + 1; const char *w = a; while (w < end && is_ident (*w)) w++;
			const char *b = w; while (b < end && *b != '{' && *b != ';') b++;
			if (b >= end) break;
			if (*b == ';') { p = b + 1; continue; }					// (@import, @charset)
			// the block's end
			const char *c = b + 1; int d = 1; while (c < end && d) { if (*c == '{') d++; else if (*c == '}') d--; c++; }
			if (w - a == 5 && ieqn (a, "media", 5) && media_applies (w, b, viewW)) parse_sheet (sh, b + 1, (int) (c - 1 - (b + 1)), viewW);
			p = c; continue;
		}
		const char *b = p; while (b < end && *b != '{') b++;
		if (b >= end) break;
		const char *c = b + 1; while (c < end && *c != '}') c++;
		int d0 = sh.decls.n; int nd = parse_decls (sh, b + 1, c);
		// each selector of the group
		for (const char *s0 = p; s0 < b; )
		{
			const char *s1 = s0; int br = 0; while (s1 < b && (br || *s1 != ',')) { if (*s1 == '(' || *s1 == '[') br++; else if ((*s1 == ')' || *s1 == ']') && br) br--; s1++; }
			int si = parse_selector (sh, s0, s1);
			if (si >= 0 && nd) { Rule &r = sh.rules.push (); r.sel = si; r.decl = d0; r.ndecl = nd; r.order = sh.order++; }
			s0 = s1 + 1;
		}
		p = c < end ? c + 1 : end;
	}
}

// ---- matching -----------------------------------------------------------------------------------------------------------------
static bool has_word (const char *list, const char *w)
{
	if (!list) return false;
	int n = (int) strlen (w);
	for (const char *p = list; *p; )
	{
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
		const char *s = p; while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
		if (p - s == n && !memcmp (s, w, n)) return true;
	}
	return false;
}
static bool match_simple (const Doc &d, int node, const Simple &m)
{
	const Node &e = d.nodes[node];
	if (e.tag == T_TEXT || m.never) return false;
	if (m.tag >= 0)
	{
		if (m.tag == T_UNKNOWN) { if (e.tag != T_UNKNOWN || !e.name || strcmp (e.name, m.name)) return false; }
		else if (m.tag != e.tag) return false;
	}
	if (m.id) { const char *v = d.attr (node, "id"); if (!v || strcmp (v, m.id)) return false; }
	for (int i = 0; i < m.ncls; i++) if (!has_word (d.attr (node, "class"), m.cls[i])) return false;
	for (int i = 0; i < m.nat; i++)
	{
		const char *v = d.attr (node, m.at[i].name);
		if (!v) return false;
		const char *w = m.at[i].value; int wn = w ? (int) strlen (w) : 0, vn = (int) strlen (v);
		switch (m.at[i].op)
		{
		case AT_EQ: if (strcmp (v, w)) return false; break;
		case AT_WORD: if (!has_word (v, w)) return false; break;
		case AT_PREFIX: if (vn < wn || memcmp (v, w, wn)) return false; break;
		case AT_SUFFIX: if (vn < wn || memcmp (v + vn - wn, w, wn)) return false; break;
		case AT_SUB: if (!strstr (v, w)) return false; break;
		case AT_DASH: if (strcmp (v, w) && !(vn > wn && !memcmp (v, w, wn) && v[wn] == '-')) return false; break;
		default: break;
		}
	}
	if (m.firstChild && e.index != 1) return false;
	if (m.lastChild) { for (int c = e.next; c >= 0; c = d.nodes[c].next) if (d.nodes[c].tag != T_TEXT) return false; }
	if (m.link && !(e.tag == T_A && d.attr (node, "href"))) return false;
	return true;
}
static int prev_element (const Doc &d, int n) { for (int c = d.nodes[n].prev; c >= 0; c = d.nodes[c].prev) if (d.nodes[c].tag != T_TEXT) return c; return -1; }
// the selector's simples from k leftward, the k-th matched by node
static bool match_from (const Doc &d, const Sheet &sh, const Selector &S, int k, int node)
{
	const Simple &m = sh.simples[S.first + k];
	if (!match_simple (d, node, m)) return false;
	if (k == 0) return true;
	switch (m.comb)
	{
	case SEL_CHILD: { int p = d.nodes[node].parent; return p >= 0 && match_from (d, sh, S, k - 1, p); }
	case SEL_ADJ: { int p = prev_element (d, node); return p >= 0 && match_from (d, sh, S, k - 1, p); }
	case SEL_SIB: for (int p = prev_element (d, node); p >= 0; p = prev_element (d, p)) if (match_from (d, sh, S, k - 1, p)) return true; return false;
	default: for (int p = d.nodes[node].parent; p >= 0; p = d.nodes[p].parent) if (match_from (d, sh, S, k - 1, p)) return true; return false;
	}
}

// ---- the values applied -----------------------------------------------------------------------------------------------------
// a length: px, pt, em, ex, %, cm, mm, in, pc, rem, a bare number (0, or px in HTML's attributes) -> false when not one
static bool parse_len (const char *s, int n, int fontSize, Len *out, bool bareIsPx = false)
{
	while (n && (*s == ' ' || *s == '\t')) s++, n--;
	if (!n) return false;
	if (n == 4 && ieqn (s, "auto", 4)) { *out = autol (); return true; }
	char *e; float v = strtof (s, &e);
	if (e == s) return false;
	int ul = (int) (n - (e - s)); const char *u = e;
	while (ul && (u[ul - 1] == ' ' || u[ul - 1] == '\t')) ul--;
	if (ul == 0) { if (v == 0 || bareIsPx) { *out = px (v); return true; } return false; }
	if (ul == 1 && *u == '%') { *out = pct (v); return true; }
	if (ul == 2)
	{
		if (ieqn (u, "px", 2)) { *out = px (v); return true; }
		if (ieqn (u, "pt", 2)) { *out = px (v * 4 / 3); return true; }
		if (ieqn (u, "em", 2)) { *out = px (v * fontSize); return true; }
		if (ieqn (u, "ex", 2)) { *out = px (v * fontSize / 2); return true; }
		if (ieqn (u, "cm", 2)) { *out = px (v * 96 / 2.54f); return true; }
		if (ieqn (u, "mm", 2)) { *out = px (v * 96 / 25.4f); return true; }
		if (ieqn (u, "in", 2)) { *out = px (v * 96); return true; }
		if (ieqn (u, "pc", 2)) { *out = px (v * 16); return true; }
		if (ieqn (u, "vw", 2)) { *out = px (v * 6); return true; }		// (a 600 px view, mail's width)
		if (ieqn (u, "vh", 2)) { *out = px (v * 6); return true; }
	}
	if (ul == 3 && ieqn (u, "rem", 3)) { *out = px (v * 16); return true; }
	return false;
}
// a value's words (space separated, a function's parentheses kept whole)
struct Words { const char *w[8]; int n[8]; int k; };
static void words (const char *s, Words &W)
{
	W.k = 0;
	const char *p = s;
	while (*p && W.k < 8)
	{
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
		if (!*p) break;
		const char *a = p; int par = 0; char q = 0;
		while (*p && (q || par || !(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ','))) { if (q) { if (*p == q) q = 0; } else if (*p == '"' || *p == '\'') q = *p; else if (*p == '(') par++; else if (*p == ')' && par) par--; p++; }
		W.w[W.k] = a; W.n[W.k] = (int) (p - a); W.k++;
	}
}
static bool wis (const char *w, int n, const char *k) { return (int) strlen (k) == n && ieqn (w, k, n); }
static int border_style_of (const char *w, int n)
{
	static const char *const N[] = { "none", "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset", "hidden" };
	for (int i = 0; i < 10; i++) if (wis (w, n, N[i])) return i;
	return -1;
}
static bool border_width_of (const char *w, int n, int fs, int *out)
{
	if (wis (w, n, "thin")) { *out = 1; return true; }
	if (wis (w, n, "medium")) { *out = 3; return true; }
	if (wis (w, n, "thick")) { *out = 5; return true; }
	Len l; if (parse_len (w, n, fs, &l) && l.u == U_PX) { *out = (int) (l.v + (l.v > 0 && l.v < 1 ? 1 : 0.5f)); return true; }
	return false;
}
static int family_of (const char *v)
{
	// the first family the card can stand in for
	Words W; words (v, W);
	for (int i = 0; i < W.k; i++)
	{
		const char *w = W.w[i]; int n = W.n[i];
		if (*w == '"' || *w == '\'') { w++; n -= 2; }
		if (n <= 0) continue;
		if (ifind (w, "mono") && ieqn (w, "mono", 4)) return FF_MONO;
		char t[48]; scpy (t, w, n + 1 < (int) sizeof t ? n + 1 : (int) sizeof t);
		if (ifind (t, "courier") || ifind (t, "monospace") || ifind (t, "consolas") || ifind (t, "menlo") || ifind (t, "monaco") || ifind (t, "mono")) return FF_MONO;
		if (ifind (t, "georgia") || ifind (t, "cambria") || ifind (t, "palatino") || ifind (t, "garamond") || ifind (t, "book antiqua")) return FF_GEORGIA;
		if (ifind (t, "segoe") || ifind (t, "selawik") || ifind (t, "calibri") || ifind (t, "-apple-system") || ifind (t, "system-ui") || ifind (t, "blinkmacsystemfont") || ifind (t, "roboto") || ifind (t, "open sans") || ifind (t, "lato") || ifind (t, "inter")) return FF_SEGOE;
		if (ifind (t, "times") || (ifind (t, "serif") && !ifind (t, "sans"))) return FF_SERIF;
		if (ifind (t, "arial") || ifind (t, "helvetica") || ifind (t, "verdana") || ifind (t, "tahoma") || ifind (t, "trebuchet") || ifind (t, "sans") || ifind (t, "geneva") || ifind (t, "lucida")) return FF_SANS;
	}
	return -1;
}
static const int FONT_SIZES[7] = { 10, 13, 16, 18, 24, 32, 48 };	// <font size=1..7>, xx-small..xx-large
static void four (const Words &W, int fs, Len *side, bool allowAuto)
{
	Len v[4]; int k = 0;
	for (int i = 0; i < W.k && k < 4; i++) { Len l; if (parse_len (W.w[i], W.n[i], fs, &l) && (allowAuto || l.u != U_AUTO)) v[k++] = l; }
	if (!k) return;
	side[0] = v[0]; side[1] = k > 1 ? v[1] : v[0]; side[2] = k > 2 ? v[2] : v[0]; side[3] = k > 3 ? v[3] : side[1];
}
static void border_one (Style &st, int side, const char *v)
{
	Words W; words (v, W);
	bool setS = false;
	for (int i = 0; i < W.k; i++)
	{
		int b = border_style_of (W.w[i], W.n[i]); unsigned c; int w;
		if (b >= 0) { st.borderS[side] = (unsigned char) b; setS = true; }
		else if (border_width_of (W.w[i], W.n[i], st.fontSize, &w)) st.borderW[side] = w;
		else if (parse_colour (W.w[i], W.n[i], &c)) { st.borderC[side] = c; st.borderCSet[side] = true; }
	}
	if (!setS && W.k) st.borderS[side] = BS_NONE;
	if (W.k == 1 && wis (W.w[0], W.n[0], "0")) { st.borderS[side] = BS_NONE; st.borderW[side] = 0; }
	if (setS && st.borderW[side] == 0 && st.borderS[side] != BS_NONE) { bool w = false; for (int i = 0; i < W.k; i++) { int x; if (border_width_of (W.w[i], W.n[i], st.fontSize, &x)) w = true; } if (!w) st.borderW[side] = 3; }
}
static void font_size (Style &st, const char *v, int parentSize)
{
	static const char *const K[7] = { "xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large" };
	static const int KS[7] = { 9, 10, 13, 16, 18, 24, 32 };
	int n = (int) strlen (v);
	for (int i = 0; i < 7; i++) if (wis (v, n, K[i])) { st.fontSize = KS[i]; return; }
	if (wis (v, n, "smaller")) { st.fontSize = parentSize * 5 / 6; return; }
	if (wis (v, n, "larger")) { st.fontSize = parentSize * 6 / 5; return; }
	Len l; if (!parse_len (v, n, parentSize, &l)) return;
	if (l.u == U_PCT) st.fontSize = (int) (parentSize * l.v / 100 + 0.5f);
	else if (l.u == U_PX) st.fontSize = (int) (l.v + 0.5f);
	if (st.fontSize < 1) st.fontSize = 1;
	if (st.fontSize > 200) st.fontSize = 200;
}
static void apply (Style &st, int prop, const char *v, const Style &parent)
{
	int n = (int) strlen (v); Words W; Len l; unsigned c; int x;
	bool inherit = wis (v, n, "inherit");
	switch (prop)
	{
	case P_DISPLAY:
	{
		static const char *const N[] = { "inline", "block", "list-item", "inline-block", "table", "inline-table", "table-row-group",
			"table-header-group", "table-footer-group", "table-row", "table-cell", "table-caption", "table-column", "table-column-group", "none" };
		for (int i = 0; i < 15; i++) if (wis (v, n, N[i])) st.display = (unsigned char) i;
		if (wis (v, n, "flex") || wis (v, n, "grid")) st.display = D_BLOCK;
		if (wis (v, n, "inline-flex")) st.display = D_INLINE_BLOCK;
		break;
	}
	case P_COLOR: if (inherit) st.color = parent.color; else if (parse_colour (v, n, &c) && c >> 24) st.color = c; break;
	case P_BACKGROUND_COLOR: if (parse_colour (v, n, &c)) st.bg = c; break;
	case P_BACKGROUND:
		words (v, W);
		for (int i = 0; i < W.k; i++)
		{
			if (parse_colour (W.w[i], W.n[i], &c)) st.bg = c;
			else if (W.n[i] > 4 && ieqn (W.w[i], "url(", 4)) { const char *u = W.w[i] + 4; int un = W.n[i] - 5; while (un > 0 && (*u == '"' || *u == '\'' || *u == ' ')) u++, un--; while (un > 0 && (u[un - 1] == '"' || u[un - 1] == '\'' || u[un - 1] == ' ')) un--; Buf t; t.add (u, un); st.bgImage = (const char *) 1; (void) t; }
			else if (wis (W.w[i], W.n[i], "none")) {}
		}
		break;
	case P_BACKGROUND_IMAGE: break;
	case P_FONT_FAMILY: { int f = family_of (v); if (f >= 0) st.family = (unsigned char) f; break; }
	case P_FONT_SIZE: font_size (st, v, parent.fontSize); break;
	case P_FONT_WEIGHT: if (wis (v, n, "bold") || wis (v, n, "bolder") || atoi (v) >= 600) st.bold = 1; else if (wis (v, n, "normal") || wis (v, n, "lighter") || (atoi (v) > 0 && atoi (v) < 600)) st.bold = 0; else if (inherit) st.bold = parent.bold; break;
	case P_FONT_STYLE: st.italic = wis (v, n, "italic") || wis (v, n, "oblique") ? 1 : wis (v, n, "normal") ? 0 : st.italic; break;
	case P_FONT:
	{	// [style] [weight] size[/line-height] family
		words (v, W);
		for (int i = 0; i < W.k; i++)
		{
			const char *w = W.w[i]; int wn = W.n[i];
			if (wis (w, wn, "italic") || wis (w, wn, "oblique")) st.italic = 1;
			else if (wis (w, wn, "bold") || (wn == 3 && w[0] >= '6' && w[0] <= '9' && w[1] == '0')) st.bold = 1;
			else if (wis (w, wn, "normal")) {}
			else if ((w[0] >= '0' && w[0] <= '9') || w[0] == '.' || (wn > 3 && (ieqn (w, "small", 5) || ieqn (w, "large", 5) || ieqn (w, "x-", 2) || ieqn (w, "xx-", 3) || ieqn (w, "medium", 6))))
			{
				char t[40]; scpy (t, w, wn + 1 < 40 ? wn + 1 : 40);
				char *sl = strchr (t, '/'); if (sl) *sl = 0;
				font_size (st, t, parent.fontSize);
				if (sl) { Len lh; if (parse_len (sl + 1, (int) strlen (sl + 1), st.fontSize, &lh)) { st.lineHeight = lh; st.lineNum = 0; } else { float f = strtof (sl + 1, 0); if (f > 0) { st.lineNum = f; st.lineHeight = nonel (); } } }
				// the rest: the family
				Buf rest; for (int j = i + 1; j < W.k; j++) { rest.add (W.w[j], W.n[j]); rest.addc (','); }
				if (i + 1 < W.k && W.w[i + 1][0] == '/') {}
				int f = family_of (rest.c ()); if (f >= 0) st.family = (unsigned char) f;
				break;
			}
		}
		break;
	}
	case P_TEXT_DECORATION: case P_TEXT_DECORATION_LINE:
		words (v, W); st.decoration = 0;
		for (int i = 0; i < W.k; i++)
		{
			if (wis (W.w[i], W.n[i], "underline")) st.decoration |= TD_UNDERLINE;
			else if (wis (W.w[i], W.n[i], "line-through")) st.decoration |= TD_LINE_THROUGH;
			else if (wis (W.w[i], W.n[i], "overline")) st.decoration |= TD_OVERLINE;
			else if (parse_colour (W.w[i], W.n[i], &c)) st.decoColor = c;
		}
		break;
	case P_TEXT_DECORATION_COLOR: if (parse_colour (v, n, &c)) st.decoColor = c; break;
	case P_TEXT_ALIGN:
		if (wis (v, n, "left") || wis (v, n, "start")) st.textAlign = TA_LEFT;
		else if (wis (v, n, "right") || wis (v, n, "end")) st.textAlign = TA_RIGHT;
		else if (wis (v, n, "center") || wis (v, n, "-webkit-center") || wis (v, n, "-moz-center")) st.textAlign = TA_CENTER;
		else if (wis (v, n, "justify")) st.textAlign = TA_JUSTIFY;
		break;
	case P_VERTICAL_ALIGN:
	{
		static const char *const N[] = { "baseline", "top", "middle", "bottom", "sub", "super", "text-top", "text-bottom" };
		for (int i = 0; i < 8; i++) if (wis (v, n, N[i])) st.vAlign = (unsigned char) i;
		break;
	}
	case P_LINE_HEIGHT:
		if (wis (v, n, "normal")) { st.lineHeight = nonel (); st.lineNum = 0; }
		else if (parse_len (v, n, st.fontSize, &l) && l.u != U_AUTO && !(l.u == U_PX && l.v == 0 && strtof (v, 0) == 0 && n == 1))
		{ if (l.u == U_PCT) { st.lineHeight = px (st.fontSize * l.v / 100); } else st.lineHeight = l; st.lineNum = 0; }
		else { float f = strtof (v, 0); if (f > 0) { st.lineNum = f; st.lineHeight = nonel (); } }
		break;
	case P_WHITE_SPACE:
		st.whiteSpace = wis (v, n, "nowrap") ? WS_NOWRAP : wis (v, n, "pre") ? WS_PRE : wis (v, n, "pre-wrap") ? WS_PRE_WRAP : wis (v, n, "pre-line") ? WS_PRE_LINE : WS_NORMAL;
		break;
	case P_MARGIN: words (v, W); four (W, st.fontSize, st.margin, true); break;
	case P_MARGIN_TOP: case P_MARGIN_RIGHT: case P_MARGIN_BOTTOM: case P_MARGIN_LEFT: if (parse_len (v, n, st.fontSize, &l)) st.margin[prop - P_MARGIN_TOP] = l; break;
	case P_PADDING: words (v, W); four (W, st.fontSize, st.padding, false); break;
	case P_PADDING_TOP: case P_PADDING_RIGHT: case P_PADDING_BOTTOM: case P_PADDING_LEFT: if (parse_len (v, n, st.fontSize, &l) && l.u != U_AUTO) st.padding[prop - P_PADDING_TOP] = l; break;
	case P_BORDER: for (int s = 0; s < 4; s++) border_one (st, s, v); break;
	case P_BORDER_TOP: case P_BORDER_RIGHT: case P_BORDER_BOTTOM: case P_BORDER_LEFT: border_one (st, prop - P_BORDER_TOP, v); break;
	case P_BORDER_WIDTH:
	{
		words (v, W); int w4[4]; int k = 0;
		for (int i = 0; i < W.k && k < 4; i++) if (border_width_of (W.w[i], W.n[i], st.fontSize, &x)) w4[k++] = x;
		if (k) { st.borderW[0] = w4[0]; st.borderW[1] = k > 1 ? w4[1] : w4[0]; st.borderW[2] = k > 2 ? w4[2] : w4[0]; st.borderW[3] = k > 3 ? w4[3] : st.borderW[1]; }
		break;
	}
	case P_BORDER_STYLE:
	{
		words (v, W); int s4[4]; int k = 0;
		for (int i = 0; i < W.k && k < 4; i++) { int b = border_style_of (W.w[i], W.n[i]); if (b >= 0) s4[k++] = b; }
		if (k) { st.borderS[0] = (unsigned char) s4[0]; st.borderS[1] = (unsigned char) (k > 1 ? s4[1] : s4[0]); st.borderS[2] = (unsigned char) (k > 2 ? s4[2] : s4[0]); st.borderS[3] = (unsigned char) (k > 3 ? s4[3] : st.borderS[1]); }
		break;
	}
	case P_BORDER_COLOR:
	{
		words (v, W); unsigned c4[4]; int k = 0;
		for (int i = 0; i < W.k && k < 4; i++) if (parse_colour (W.w[i], W.n[i], &c)) c4[k++] = c;
		if (k) { st.borderC[0] = c4[0]; st.borderC[1] = k > 1 ? c4[1] : c4[0]; st.borderC[2] = k > 2 ? c4[2] : c4[0]; st.borderC[3] = k > 3 ? c4[3] : st.borderC[1]; for (int s = 0; s < 4; s++) st.borderCSet[s] = true; }
		break;
	}
	case P_BORDER_TOP_WIDTH: case P_BORDER_RIGHT_WIDTH: case P_BORDER_BOTTOM_WIDTH: case P_BORDER_LEFT_WIDTH:
		if (border_width_of (v, n, st.fontSize, &x)) st.borderW[prop - P_BORDER_TOP_WIDTH] = x; break;
	case P_BORDER_TOP_STYLE: case P_BORDER_RIGHT_STYLE: case P_BORDER_BOTTOM_STYLE: case P_BORDER_LEFT_STYLE:
		x = border_style_of (v, n); if (x >= 0) st.borderS[prop - P_BORDER_TOP_STYLE] = (unsigned char) x; break;
	case P_BORDER_TOP_COLOR: case P_BORDER_RIGHT_COLOR: case P_BORDER_BOTTOM_COLOR: case P_BORDER_LEFT_COLOR:
		if (parse_colour (v, n, &c)) { st.borderC[prop - P_BORDER_TOP_COLOR] = c; st.borderCSet[prop - P_BORDER_TOP_COLOR] = true; } break;
	case P_WIDTH: if (parse_len (v, n, st.fontSize, &l)) st.width = l; break;
	case P_HEIGHT: if (parse_len (v, n, st.fontSize, &l)) st.height = l; break;
	case P_MIN_WIDTH: if (parse_len (v, n, st.fontSize, &l) && l.u != U_AUTO) st.minWidth = l; break;
	case P_MAX_WIDTH: if (wis (v, n, "none")) st.maxWidth = nonel (); else if (parse_len (v, n, st.fontSize, &l) && l.u != U_AUTO) st.maxWidth = l; break;
	case P_MIN_HEIGHT: if (parse_len (v, n, st.fontSize, &l) && l.u != U_AUTO) st.minHeight = l; break;
	case P_MAX_HEIGHT: if (parse_len (v, n, st.fontSize, &l) && l.u == U_PX && l.v <= 0) st.display = D_NONE; break;	// (max-height:0: the hidden preheader)
	case P_LIST_STYLE: case P_LIST_STYLE_TYPE:
	{
		static const char *const N[] = { "disc", "circle", "square", "decimal", "lower-alpha", "upper-alpha", "lower-roman", "upper-roman", "none" };
		words (v, W);
		for (int i = 0; i < W.k; i++) for (int k = 0; k < 9; k++) if (wis (W.w[i], W.n[i], N[k])) st.listStyle = (unsigned char) k;
		for (int i = 0; i < W.k; i++) { if (wis (W.w[i], W.n[i], "lower-latin")) st.listStyle = LS_LOWER_ALPHA; if (wis (W.w[i], W.n[i], "upper-latin")) st.listStyle = LS_UPPER_ALPHA; }
		break;
	}
	case P_TEXT_TRANSFORM: st.transform = wis (v, n, "uppercase") ? TT_UPPER : wis (v, n, "lowercase") ? TT_LOWER : wis (v, n, "capitalize") ? TT_CAPITALIZE : TT_NONE; break;
	case P_TEXT_INDENT: if (parse_len (v, n, st.fontSize, &l) && l.u != U_AUTO) st.textIndent = l; break;
	case P_FLOAT: st.floatSide = wis (v, n, "left") ? FL_LEFT : wis (v, n, "right") ? FL_RIGHT : FL_NONE; break;
	case P_CLEAR: st.clearSide = wis (v, n, "left") ? 1 : wis (v, n, "right") ? 2 : wis (v, n, "both") ? 3 : 0; break;
	case P_VISIBILITY: st.visible = wis (v, n, "hidden") || wis (v, n, "collapse") ? 0 : 1; break;
	case P_BORDER_COLLAPSE: st.collapse = wis (v, n, "collapse") ? 1 : 0; break;
	case P_BORDER_SPACING: if (parse_len (v, n, st.fontSize, &l, true) && l.u == U_PX) st.borderSpacing = (int) l.v; break;
	case P_MSO_HIDE: if (wis (v, n, "all")) st.display = D_NONE; break;
	default: break;
	}
}

// ---- the user agent's sheet: HTML 4's looks ------------------------------------------------------------------------------------
static const char UA_SHEET[] =
	"address,article,aside,blockquote,center,dd,details,dir,div,dl,dt,fieldset,figcaption,figure,footer,form,h1,h2,h3,h4,h5,h6,"
	"header,hr,main,menu,nav,ol,p,pre,section,summary,ul,xmp,legend{display:block}"
	"li{display:list-item}table{display:table;border-spacing:2px;text-align:left}caption{display:table-caption;text-align:center}"
	"thead{display:table-header-group}tbody{display:table-row-group}tfoot{display:table-footer-group}tr{display:table-row}"
	"td,th{display:table-cell;padding:1px;vertical-align:middle}th{font-weight:bold;text-align:center}"
	"col{display:table-column}colgroup{display:table-column-group}"
	"head,script,style,title,meta,link,noscript,area,map,param,source,iframe,object,embed,video,audio,select,option,input,button,textarea,svg{display:none}"
	"img,picture{display:inline-block}"
	"p,dl,blockquote,figure{margin:1em 0}ul,ol,menu,dir{margin:1em 0;padding-left:40px}ul ul,ol ul,ul ol,ol ol{margin:0}"
	"ol{list-style-type:decimal}ul ul,ol ul{list-style-type:circle}ul ul ul{list-style-type:square}dd{margin-left:40px}"
	"blockquote{margin:1em 40px}h1{font-size:2em;margin:.67em 0;font-weight:bold}h2{font-size:1.5em;margin:.75em 0;font-weight:bold}"
	"h3{font-size:1.17em;margin:.83em 0;font-weight:bold}h4{margin:1.12em 0;font-weight:bold}h5{font-size:.83em;margin:1.5em 0;font-weight:bold}"
	"h6{font-size:.75em;margin:1.67em 0;font-weight:bold}b,strong{font-weight:bold}i,em,cite,var,dfn,address{font-style:italic}"
	"pre,xmp,code,kbd,samp,tt{font-family:monospace}pre,xmp{white-space:pre;margin:1em 0}"
	"big{font-size:larger}small{font-size:smaller}sub{vertical-align:sub;font-size:smaller}sup{vertical-align:super;font-size:smaller}"
	"u,ins{text-decoration:underline}s,strike,del{text-decoration:line-through}a:link{color:#0b57d0;text-decoration:underline}"
	"center{text-align:center}hr{border:1px inset #bbb;margin:.5em 0}mark{background:#ffeb3b}nobr{white-space:nowrap}"
	"fieldset{border:2px groove #ccc;padding:.35em .75em .6em;margin:0 2px}legend{padding:0 2px}"
	"[hidden]{display:none}q{font-style:italic}";

// ---- the cascade ---------------------------------------------------------------------------------------------------------------------
struct Styles
{
	Vec<Style> s;
	Sheet ua, author;
	int viewW;
	Style root;
	Styles () : viewW (600) {}
};
static void style_initial (Style &st)
{
	memset (&st, 0, sizeof st);
	st.color = 0xFF202124; st.fontSize = 14; st.family = FF_SANS; st.visible = 1; st.borderSpacing = 0;
	st.lineHeight = nonel (); st.width = autol (); st.height = autol (); st.maxWidth = nonel (); st.minWidth = nonel (); st.minHeight = nonel ();
	for (int i = 0; i < 4; i++) { st.margin[i] = px (0); st.padding[i] = px (0); }
	st.textIndent = px (0); st.link = -1; st.listStyle = LS_DISC;
}
// the inherited ones from the parent, the rest initial
static void style_inherit (Style &st, const Style &p)
{
	style_initial (st);
	st.color = p.color; st.fontSize = p.fontSize; st.family = p.family; st.bold = p.bold; st.italic = p.italic; st.whiteSpace = p.whiteSpace;
	st.textAlign = p.textAlign; st.listStyle = p.listStyle; st.transform = p.transform; st.visible = p.visible; st.collapse = p.collapse;
	st.lineHeight = p.lineHeight; st.lineNum = p.lineNum; st.borderSpacing = p.borderSpacing; st.textIndent = p.textIndent; st.link = p.link;
	st.display = D_INLINE;
}
// HTML's attributes that say how it looks (author level, before the author's sheets)
static void presentational (const Doc &d, int node, Style &st, const Style &parent)
{
	const Node &e = d.nodes[node];
	const char *v;
	Len l; unsigned c;
	if ((v = d.attr (node, "bgcolor")) && parse_colour (v, (int) strlen (v), &c)) st.bg = c;
	if ((v = d.attr (node, "background")) && v[0]) st.bgImage = (const char *) 1;
	if ((v = d.attr (node, "align")))
	{
		bool img = e.tag == T_IMG || e.tag == T_TABLE;
		if (img && ieq (v, "left")) st.floatSide = FL_LEFT;
		else if (img && ieq (v, "right")) st.floatSide = FL_RIGHT;
		else if (e.tag == T_TABLE && ieq (v, "center")) { st.margin[1] = autol (); st.margin[3] = autol (); }
		else if (e.tag == T_IMG && (ieq (v, "middle") || ieq (v, "absmiddle"))) st.vAlign = VA_MIDDLE;
		else if (e.tag == T_IMG && ieq (v, "top")) st.vAlign = VA_TOP;
		else if (e.tag == T_IMG && (ieq (v, "bottom") || ieq (v, "baseline"))) st.vAlign = VA_BASELINE;
		else if (ieq (v, "center") || ieq (v, "middle")) st.textAlign = TA_CENTER;
		else if (ieq (v, "right")) st.textAlign = TA_RIGHT;
		else if (ieq (v, "left")) st.textAlign = TA_LEFT;
		else if (ieq (v, "justify")) st.textAlign = TA_JUSTIFY;
	}
	if ((v = d.attr (node, "valign")))
	{
		if (ieq (v, "top")) st.vAlign = VA_TOP; else if (ieq (v, "bottom")) st.vAlign = VA_BOTTOM; else if (ieq (v, "middle") || ieq (v, "center")) st.vAlign = VA_MIDDLE; else if (ieq (v, "baseline")) st.vAlign = VA_BASELINE;
	}
	if (e.tag == T_TABLE || e.tag == T_TD || e.tag == T_TH || e.tag == T_IMG || e.tag == T_HR || e.tag == T_COL || e.tag == T_TR)
	{
		if ((v = d.attr (node, "width")) && parse_len (v, (int) strlen (v), st.fontSize, &l, true) && l.u != U_AUTO && l.v > 0) st.width = l;
		if ((v = d.attr (node, "height")) && parse_len (v, (int) strlen (v), st.fontSize, &l, true) && l.u == U_PX) st.height = l;
	}
	if ((e.tag == T_TD || e.tag == T_TH) && d.attr (node, "nowrap")) st.whiteSpace = WS_NOWRAP;
	if (e.tag == T_TABLE)
	{
		if ((v = d.attr (node, "cellspacing"))) st.borderSpacing = atoi (v);
		if ((v = d.attr (node, "border")))
		{
			int b = *v ? atoi (v) : 1;
			if (b > 0) for (int s = 0; s < 4; s++) { st.borderW[s] = b; st.borderS[s] = BS_OUTSET; st.borderC[s] = 0xFF808080; }
		}
		if ((v = d.attr (node, "bordercolor")) && parse_colour (v, (int) strlen (v), &c)) for (int s = 0; s < 4; s++) { st.borderC[s] = c; st.borderCSet[s] = true; if (st.borderS[s] == BS_OUTSET) st.borderS[s] = BS_SOLID; }
	}
	if (e.tag == T_TD || e.tag == T_TH)
	{
		// the table's border="n" and cellpadding="n" reach its cells
		int tb = e.parent; while (tb >= 0 && d.nodes[tb].tag != T_TABLE) tb = d.nodes[tb].parent;
		if (tb >= 0)
		{
			if ((v = d.attr (tb, "cellpadding"))) { int p = atoi (v); for (int s = 0; s < 4; s++) st.padding[s] = px ((float) p); }
			if ((v = d.attr (tb, "border")) && (!*v || atoi (v) > 0))
			{
				unsigned bc = 0xFF808080; const char *bcv = d.attr (tb, "bordercolor"); bool solid = false;
				if (bcv && parse_colour (bcv, (int) strlen (bcv), &c)) { bc = c; solid = true; }
				for (int s = 0; s < 4; s++) { st.borderW[s] = 1; st.borderS[s] = solid ? BS_SOLID : BS_INSET; st.borderC[s] = bc; }
			}
		}
	}
	if (e.tag == T_IMG)
	{
		if ((v = d.attr (node, "border")) && atoi (v) > 0) for (int s = 0; s < 4; s++) { st.borderW[s] = atoi (v); st.borderS[s] = BS_SOLID; }
		if ((v = d.attr (node, "hspace"))) { st.margin[1] = st.margin[3] = px ((float) atoi (v)); }
		if ((v = d.attr (node, "vspace"))) { st.margin[0] = st.margin[2] = px ((float) atoi (v)); }
	}
	if (e.tag == T_FONT)
	{
		if ((v = d.attr (node, "color")) && parse_colour (v, (int) strlen (v), &c)) st.color = c;
		if ((v = d.attr (node, "face"))) { int f = family_of (v); if (f >= 0) st.family = (unsigned char) f; }
		if ((v = d.attr (node, "size")))
		{
			int k = atoi (v + (*v == '+' || *v == '-' ? 1 : 0));
			if (*v == '+') k = 3 + k; else if (*v == '-') k = 3 - k;
			if (k < 1) k = 1; if (k > 7) k = 7;
			st.fontSize = FONT_SIZES[k - 1];
		}
	}
	if (e.tag == T_ROOT)
	{
		if ((v = d.attr (node, "text")) && parse_colour (v, (int) strlen (v), &c)) st.color = c;
	}
	if (e.tag == T_HR)
	{
		if ((v = d.attr (node, "color")) && parse_colour (v, (int) strlen (v), &c)) { for (int s = 0; s < 4; s++) { st.borderC[s] = c; st.borderS[s] = BS_SOLID; } st.bg = c; }
		if ((v = d.attr (node, "noshade"))) for (int s = 0; s < 4; s++) st.borderS[s] = BS_SOLID;
		if ((v = d.attr (node, "size")) && atoi (v) > 0) { st.height = px ((float) (atoi (v) > 2 ? atoi (v) - 2 : 0)); }
	}
	if ((e.tag == T_OL || e.tag == T_UL || e.tag == T_LI) && (v = d.attr (node, "type")))
	{
		if (!strcmp (v, "1")) st.listStyle = LS_DECIMAL; else if (!strcmp (v, "a")) st.listStyle = LS_LOWER_ALPHA; else if (!strcmp (v, "A")) st.listStyle = LS_UPPER_ALPHA;
		else if (!strcmp (v, "i")) st.listStyle = LS_LOWER_ROMAN; else if (!strcmp (v, "I")) st.listStyle = LS_UPPER_ROMAN;
		else if (ieq (v, "circle")) st.listStyle = LS_CIRCLE; else if (ieq (v, "square")) st.listStyle = LS_SQUARE; else if (ieq (v, "disc")) st.listStyle = LS_DISC;
	}
	(void) parent;
}
struct Match { int spec, order; const Rule *r; const Sheet *sh; };
static void sort_matches (Match *m, int n)
{
	for (int i = 1; i < n; i++) { Match k = m[i]; int j = i - 1; while (j >= 0 && (m[j].spec > k.spec || (m[j].spec == k.spec && m[j].order > k.order))) { m[j + 1] = m[j]; j--; } m[j + 1] = k; }
}
// one element's style: the parent's inherited, the UA's rules, the attributes, the author's rules, style=""; !important last
static void compute (const Doc &d, Styles &S, int node, const Style &parent)
{
	Style st; style_inherit (st, parent);
	const Node &e = d.nodes[node];
	// the matching rules, UA first (its order before the author's)
	Match m[160]; int nm = 0;
	for (int pass = 0; pass < 2; pass++)
	{
		const Sheet &sh = pass ? S.author : S.ua;
		for (int i = 0; i < sh.rules.n && nm < 160; i++)
		{
			const Rule &r = sh.rules[i]; const Selector &sel = sh.sels[r.sel];
			if (!match_from (d, sh, sel, sel.n - 1, node)) continue;
			m[nm].spec = pass ? sel.spec + 1000000 : sel.spec; m[nm].order = r.order + (pass ? 1000000 : 0); m[nm].r = &r; m[nm].sh = &sh; nm++;
		}
	}
	sort_matches (m, nm);
	// the UA's first, then the attributes, then the author's; importants after
	int i = 0;
	for (; i < nm && m[i].sh == &S.ua; i++) for (int k = 0; k < m[i].r->ndecl; k++) { const Decl &dc = m[i].sh->decls[m[i].r->decl + k]; if (!dc.important) apply (st, dc.prop, dc.value, parent); }
	presentational (d, node, st, parent);
	for (int j = i; j < nm; j++) for (int k = 0; k < m[j].r->ndecl; k++) { const Decl &dc = m[j].sh->decls[m[j].r->decl + k]; if (!dc.important) apply (st, dc.prop, dc.value, parent); }
	// style=""
	const char *inl = d.attr (node, "style");
	Sheet tmp;
	int nInline = 0;
	if (inl) { nInline = parse_decls (tmp, inl, inl + strlen (inl)); for (int k = 0; k < nInline; k++) if (!tmp.decls[k].important) apply (st, tmp.decls[k].prop, tmp.decls[k].value, parent); }
	for (int j = 0; j < nm; j++) for (int k = 0; k < m[j].r->ndecl; k++) { const Decl &dc = m[j].sh->decls[m[j].r->decl + k]; if (dc.important) apply (st, dc.prop, dc.value, parent); }
	for (int k = 0; k < nInline; k++) if (tmp.decls[k].important) apply (st, tmp.decls[k].prop, tmp.decls[k].value, parent);
	// what follows from the rest
	if (e.tag == T_A && d.attr (node, "href")) st.link = node;
	for (int s = 0; s < 4; s++)
	{
		if (!st.borderCSet[s] && st.borderS[s] != BS_NONE && st.borderC[s] == 0) st.borderC[s] = st.color;
		if (st.borderS[s] == BS_NONE || st.borderS[s] == BS_HIDDEN) st.borderW[s] = 0;
	}
	if (!st.decoColor) st.decoColor = st.color;
	if (e.tag == T_ROOT) st.display = D_BLOCK;
	if (e.tag == T_BR || e.tag == T_WBR) st.display = D_INLINE;
	if (st.floatSide && st.display != D_NONE && (st.display == D_INLINE || st.display == D_INLINE_TABLE)) st.display = st.display == D_INLINE_TABLE ? D_TABLE : D_BLOCK;
	S.s[node] = st;
}
// all the elements' styles (the tree walked in order: a parent before its children)
static void cascade (const Doc &d, Styles &S, int viewW, int baseFontSize, unsigned textColour)
{
	S.viewW = viewW;
	S.ua.reset (); S.author.reset ();
	parse_sheet (S.ua, UA_SHEET, (int) sizeof UA_SHEET - 1, viewW);
	parse_sheet (S.author, d.css.c (), d.css.n, viewW);
	S.s.clear ();
	for (int i = 0; i < d.nodes.n; i++) S.s.push ();
	Style top; style_initial (top); top.fontSize = baseFontSize; top.color = textColour; top.display = D_BLOCK;
	S.root = top;
	for (int i = 0; i < d.nodes.n; i++)
	{
		const Node &e = d.nodes[i];
		const Style &par = e.parent >= 0 ? S.s[e.parent] : top;
		if (e.tag == T_TEXT) { style_inherit (S.s[i], par); S.s[i].decoColor = par.decoColor ? par.decoColor : par.color; continue; }
		compute (d, S, i, par);
	}
}

} // namespace html
} // namespace mail

#endif
