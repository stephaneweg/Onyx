//
// ods.h -- OpenDocument spreadsheets (.ods, LibreOffice's), read: the sheets, the values (numbers,
// percentages, amounts, dates, times, booleans, texts of several paragraphs), the formulas (OpenFormula,
// "of:=SUM([.A1:.A5])", turned into Excel's syntax), the cell styles (fonts, colours, borders,
// alignment, wrapping; the number styles made into format codes), the columns' widths, the rows'
// heights, merged and hidden cells, frozen panes (settings.xml), the charts (their objects:
// "Object 1/content.xml"), the named ranges and expressions, the conditional formats (LibreOffice's
// calcext ones), the AutoFilter (a database range's filter).
//
#ifndef _sheet_ods_h
#define _sheet_ods_h

#include "xlsx.h"

namespace ss {

// A length ("2.258cm", "0.35in", "12pt", "3mm") in pixels at 96 dpi.
static double ods_len (const char *s)
{
	char *e; double v = strtod (s, &e);
	if (!strncmp (e, "cm", 2)) return v / 2.54 * 96;
	if (!strncmp (e, "mm", 2)) return v / 25.4 * 96;
	if (!strncmp (e, "in", 2)) return v * 96;
	if (!strncmp (e, "pt", 2)) return v * 96 / 72;
	if (!strncmp (e, "pc", 2)) return v * 16;
	return v;
}
static unsigned ods_color (const char *s) { if (s[0] != '#') return AUTO; return (unsigned) strtoul (s + 1, 0, 16) & 0xFFFFFF; }
// "0.06pt solid #000000" -> a line and its colour.
static void ods_border (const char *s, unsigned char *st, unsigned *col)
{
	*st = BS_NONE; *col = 0;
	if (!s[0] || !strcmp (s, "none")) return;
	double w = ods_len (s) * 72 / 96;
	*st = strstr (s, "double") ? BS_DOUBLE : strstr (s, "dashed") ? BS_DASHED : strstr (s, "dotted") ? BS_DOTTED : w >= 2.2 ? BS_THICK : w >= 1.1 ? BS_MEDIUM : BS_THIN;
	const char *h = strchr (s, '#');
	if (h) *col = ods_color (h);
}

// An OpenFormula into Excel's: "of:=SUM([.A1:.B2];[$Sheet2.C3])" -> "SUM(A1:B2,Sheet2!C3)".
static void ods_formula (const char *f, Buf &o)
{
	const char *p = f;
	const char *eq = strchr (p, '=');
	if (eq && eq - p < 8) p = eq + 1;
	bool str = false;
	for (; *p; p++)
	{
		char c = *p;
		if (str) { o.put (c); if (c == '"') str = false; continue; }
		if (c == '"') { str = true; o.put (c); continue; }
		if (c == ';') { o.put (','); continue; }
		if (c == '[')
		{
			const char *q = p + 1;
			while (*q && *q != ']') q++;
			// "[.A1]", "[.A1:.B2]", "[$Sheet2.A1]", "['My sheet'.A1:'My sheet'.B2]"
			Buf ref; ref.putn (p + 1, (int) (q - p - 1));
			const char *r = ref.str ();
			bool first = true;
			for (const char *part = r; *part; )
			{
				const char *colon = part;
				bool q2 = false;
				while (*colon && (q2 || *colon != ':')) { if (*colon == '\'') q2 = !q2; colon++; }
				int len = (int) (colon - part);
				// sheet . cell
				const char *dot = 0; q2 = false;
				for (int i = 0; i < len; i++) { if (part[i] == '\'') q2 = !q2; else if (part[i] == '.' && !q2) dot = part + i; }
				if (!first) o.put (':');
				if (dot)
				{
					const char *sh = part; int shl = (int) (dot - part);
					if (shl > 0 && sh[0] == '$') { sh++; shl--; }
					if (shl > 0 && first) { o.putn (sh, shl); o.put ('!'); }
					o.putn (dot + 1, (int) (part + len - dot - 1));
				}
				else o.putn (part, len);
				first = false;
				part = *colon ? colon + 1 : colon;
			}
			p = *q ? q : q - 1;
			continue;
		}
		o.put (c);
	}
}

struct OStyle
{
	char name[48], parent[48], data[48], dname[48];	// (dname: its display name, as the conditions name it)
	int family;					// 1 cell, 2 column, 3 row
	bool has[16];					// which of the properties below it sets
	Style st;
	int colW, rowH; bool optimal;
};
enum { OP_FONT, OP_SIZE, OP_BOLD, OP_ITALIC, OP_UNDER, OP_STRIKE, OP_COLOR, OP_FILL, OP_HA, OP_VA, OP_WRAP, OP_BORDER, OP_INDENT };
enum { MC_NONE, MC_GE, MC_LT, MC_GT, MC_EQ };		// a number style's map: value() >= 0, < 0, > 0, = 0
struct ONum { char name[48]; char code[96]; char mapName[3][48]; int mapCond[3], nmap; };
// The font faces declared (style:font-face): a face's name -> its family ("Liberation Sans1" -> Liberation Sans).
struct OFace { char name[64], family[64]; };
static OFace g_faces[64]; static int g_nfaces;
static const char *face_family (const char *name)
{
	for (int i = 0; i < g_nfaces; i++) if (!strcmp (g_faces[i].name, name)) return g_faces[i].family;
	return name;
}
static void unquote_family (const char *s, char *o, int cap)	// "'Liberation Sans', Arial" -> Liberation Sans
{
	while (*s == ' ') s++;
	char q = *s == '\'' || *s == '"' ? *s++ : 0;
	int k = 0;
	while (*s && k < cap - 1 && (q ? *s != q : *s != ',')) o[k++] = *s++;
	while (k > 0 && o[k - 1] == ' ') k--;
	o[k] = 0;
}

static void ods_styles (Book &b, const char *x, int len, OStyle **st, int *nst, ONum **nm, int *nnm)
{
	XmlReader X (x, len);
	OStyle *cur = 0; ONum *num = 0;
	Buf code, v;
	bool inNum = false; unsigned numColor = AUTO;
	while (X.next () != X_EOF)
	{
		if (X.ev == X_START)
		{
			if (X.isq ("style:font-face"))
			{
				if (g_nfaces < 64 && X.attr ("style:name", v))
				{
					OFace &f = g_faces[g_nfaces];
					scpy (f.name, v.str (), sizeof f.name);
					Buf fam;
					if (X.attr ("svg:font-family", fam)) unquote_family (fam.str (), f.family, sizeof f.family); else scpy (f.family, f.name, sizeof f.family);
					g_nfaces++;
				}
			}
			else if (X.isq ("style:style") || X.isq ("style:default-style"))
			{
				Buf fam; X.attr ("style:family", fam);
				int family = !strcmp (fam.str (), "table-cell") ? 1 : !strcmp (fam.str (), "table-column") ? 2 : !strcmp (fam.str (), "table-row") ? 3 : 0;
				if (family == 0) { cur = 0; continue; }			// (the graphics', the paragraphs': not the cells')
				*st = (OStyle *) realloc (*st, (*nst + 1) * sizeof (OStyle));
				cur = &(*st)[(*nst)++]; memset (cur, 0, sizeof *cur); cur->st.reset ();
				cur->family = family;
				if (X.attr ("style:name", v)) scpy (cur->name, v.str (), sizeof cur->name);
				else scpy (cur->name, family == 1 ? "!default" : "!default-other", sizeof cur->name);
				if (X.attr ("style:display-name", v)) scpy (cur->dname, v.str (), sizeof cur->dname);
				if (X.attr ("style:parent-style-name", v)) scpy (cur->parent, v.str (), sizeof cur->parent);
				else if (strncmp (cur->name, "!default", 8) && strcmp (cur->name, "Default") && family == 1) scpy (cur->parent, "Default", sizeof cur->parent);
				if (X.attr ("style:data-style-name", v)) scpy (cur->data, v.str (), sizeof cur->data);
				if (X.empty) cur = 0;
			}
			else if (cur && X.isq ("style:text-properties"))
			{
				char famName[64] = "";
				if (X.attr ("fo:font-family", v)) unquote_family (v.str (), famName, sizeof famName);
				else if (X.attr ("style:font-name", v)) scpy (famName, face_family (v.str ()), sizeof famName);
				if (famName[0]) { cur->st.font = (unsigned short) book_font (b, famName); cur->has[OP_FONT] = true; }
				if (X.attr ("fo:font-size", v)) { cur->st.size = (unsigned short) (strtod (v.str (), 0) * 10 + 0.5); cur->has[OP_SIZE] = true; }
				if (X.attr ("fo:font-weight", v)) { cur->st.bold = !strcmp (v.str (), "bold") || atoi (v.str ()) >= 600; cur->has[OP_BOLD] = true; }
				if (X.attr ("fo:font-style", v)) { cur->st.italic = !strcmp (v.str (), "italic") || !strcmp (v.str (), "oblique"); cur->has[OP_ITALIC] = true; }
				if (X.attr ("style:text-underline-style", v)) { cur->st.under = strcmp (v.str (), "none") ? 1 : 0; cur->has[OP_UNDER] = true; }
				if (X.attr ("style:text-underline-type", v) && !strcmp (v.str (), "double")) cur->st.under = 2;
				if (X.attr ("style:text-line-through-style", v)) { cur->st.strike = strcmp (v.str (), "none") != 0; cur->has[OP_STRIKE] = true; }
				if (X.attr ("fo:color", v)) { cur->st.color = ods_color (v.str ()); cur->has[OP_COLOR] = true; }
			}
			else if (cur && X.isq ("style:table-cell-properties"))
			{
				if (X.attr ("fo:background-color", v)) { cur->st.fill = strcmp (v.str (), "transparent") ? ods_color (v.str ()) : AUTO; cur->has[OP_FILL] = true; }
				if (X.attr ("style:vertical-align", v)) { cur->st.va = !strcmp (v.str (), "top") ? VA_TOP : !strcmp (v.str (), "middle") ? VA_CENTER : VA_BOTTOM; cur->has[OP_VA] = true; }
				if (X.attr ("fo:wrap-option", v)) { cur->st.wrap = !strcmp (v.str (), "wrap"); cur->has[OP_WRAP] = true; }
				if (X.attr ("fo:border", v)) { for (int k = 0; k < 4; k++) ods_border (v.str (), &cur->st.bs[k], &cur->st.bc[k]); cur->has[OP_BORDER] = true; }
				static const char *const SIDE[4] = { "fo:border-left", "fo:border-right", "fo:border-top", "fo:border-bottom" };
				for (int k = 0; k < 4; k++) if (X.attr (SIDE[k], v)) { ods_border (v.str (), &cur->st.bs[k], &cur->st.bc[k]); cur->has[OP_BORDER] = true; }
			}
			else if (cur && X.isq ("style:paragraph-properties"))
			{
				if (X.attr ("fo:text-align", v))
				{
					const char *a = v.str ();
					cur->st.ha = !strcmp (a, "center") ? HA_CENTER : !strcmp (a, "end") || !strcmp (a, "right") ? HA_RIGHT : !strcmp (a, "start") || !strcmp (a, "left") ? HA_LEFT : !strcmp (a, "justify") ? HA_JUSTIFY : HA_GENERAL;
					cur->has[OP_HA] = true;
				}
				if (X.attr ("fo:margin-left", v)) { int ind = (int) (ods_len (v.str ()) / 9 + 0.5); if (ind > 0) { cur->st.indent = (unsigned char) iclamp (ind, 0, 15); cur->has[OP_INDENT] = true; } }
			}
			else if (cur && X.isq ("style:table-column-properties") && X.attr ("style:column-width", v)) cur->colW = (int) (ods_len (v.str ()) + 0.5);
			else if (cur && X.isq ("style:table-row-properties"))
			{
				if (X.attr ("style:row-height", v)) cur->rowH = (int) (ods_len (v.str ()) + 0.5);
				cur->optimal = X.attr_bool ("style:use-optimal-row-height", true);
			}
			// the number styles
			else if (X.isq ("number:number-style") || X.isq ("number:percentage-style") || X.isq ("number:currency-style") || X.isq ("number:date-style") ||
				 X.isq ("number:time-style") || X.isq ("number:text-style") || X.isq ("number:boolean-style"))
			{
				*nm = (ONum *) realloc (*nm, (*nnm + 1) * sizeof (ONum));
				num = &(*nm)[(*nnm)++]; memset (num, 0, sizeof *num);
				X.attr ("style:name", v); scpy (num->name, v.str (), sizeof num->name);
				code.clear (); inNum = true; numColor = AUTO;
				if (X.isq ("number:time-style") && !X.attr_bool ("number:truncate-on-overflow", true)) code.puts ("");
				if (X.isq ("number:boolean-style")) code.puts ("General");
			}
			else if (inNum && X.isq ("number:number"))
			{
				int dec = X.attr_int ("number:decimal-places", 0), mi = X.attr_int ("number:min-integer-digits", 1);
				bool grp = X.attr_bool ("number:grouping", false);
				if (grp) { code.puts ("#,##"); for (int k = 0; k < mi - 1 && k < 3; k++) {} }
				for (int k = 0; k < imax (1, mi); k++) code.put (k < mi ? '0' : '#');
				if (grp && mi <= 1) {}
				if (dec > 0) { code.put ('.'); for (int k = 0; k < dec && k < 15; k++) code.put ('0'); }
			}
			else if (inNum && X.isq ("number:scientific-number"))
			{
				int dec = X.attr_int ("number:decimal-places", 2), ed = X.attr_int ("number:min-exponent-digits", 2);
				code.put ('0'); if (dec) { code.put ('.'); for (int k = 0; k < dec; k++) code.put ('0'); }
				code.puts ("E+"); for (int k = 0; k < imax (1, ed); k++) code.put ('0');
			}
			else if (inNum && X.isq ("number:fraction"))
			{
				int dd = X.attr_int ("number:min-denominator-digits", 1), dv = X.attr_int ("number:denominator-value", 0);
				code.puts ("# ");
				for (int k = 0; k < imax (1, dd); k++) code.put ('?');
				code.put ('/');
				if (dv) code.puti (dv); else for (int k = 0; k < imax (1, dd); k++) code.put ('?');
			}
			else if (inNum && X.isq ("number:day")) { X.attr ("number:style", v); code.puts (!strcmp (v.str (), "long") ? "dd" : "d"); }
			else if (inNum && X.isq ("number:month"))
			{
				bool text = X.attr_bool ("number:textual", false); X.attr ("number:style", v);
				bool lng = !strcmp (v.str (), "long");
				code.puts (text ? (lng ? "mmmm" : "mmm") : (lng ? "mm" : "m"));
			}
			else if (inNum && X.isq ("number:year")) { X.attr ("number:style", v); code.puts (!strcmp (v.str (), "long") ? "yyyy" : "yy"); }
			else if (inNum && X.isq ("number:day-of-week")) { X.attr ("number:style", v); code.puts (!strcmp (v.str (), "long") ? "dddd" : "ddd"); }
			else if (inNum && X.isq ("number:hours")) { X.attr ("number:style", v); code.puts (!strcmp (v.str (), "long") ? "hh" : "h"); }
			else if (inNum && X.isq ("number:minutes")) { X.attr ("number:style", v); code.puts (!strcmp (v.str (), "long") ? "mm" : "m"); }
			else if (inNum && X.isq ("number:seconds"))
			{
				X.attr ("number:style", v); code.puts (!strcmp (v.str (), "long") ? "ss" : "s");
				int dec = X.attr_int ("number:decimal-places", 0);
				if (dec) { code.put ('.'); for (int k = 0; k < dec && k < 3; k++) code.put ('0'); }
			}
			else if (inNum && X.isq ("number:am-pm")) code.puts ("AM/PM");
			else if (inNum && X.isq ("number:text-content")) code.put ('@');
			else if (inNum && X.isq ("style:text-properties")) { if (X.attr ("fo:color", v)) numColor = ods_color (v.str ()); }
			else if (inNum && X.isq ("style:map") && num && num->nmap < 3)
			{
				Buf cnd, an; X.attr ("style:condition", cnd); X.attr ("style:apply-style-name", an);
				const char *c = cnd.str ();
				int mc = strstr (c, ">=0") ? MC_GE : strstr (c, "<0") ? MC_LT : strstr (c, ">0") ? MC_GT : strstr (c, "=0") ? MC_EQ : MC_NONE;
				if (mc != MC_NONE) { scpy (num->mapName[num->nmap], an.str (), sizeof num->mapName[0]); num->mapCond[num->nmap++] = mc; }
			}
			else if (inNum && (X.isq ("number:text") || X.isq ("number:currency-symbol")))
			{
				bool cur2 = X.isq ("number:currency-symbol");
				Buf t;
				while (X.next () != X_EOF && X.ev != X_END) if (X.ev == X_TEXT) t.putn (X.text.b ? X.text.b : "", X.text.n);
				if (!t.n) continue;
				if (cur2 || !strcmp (t.str (), "%") || !strcmp (t.str (), " ") || !strcmp (t.str (), "-") || !strcmp (t.str (), "/") || !strcmp (t.str (), ":") || !strcmp (t.str (), ".") || !strcmp (t.str (), ","))
				{
					if (!strcmp (t.str (), "%")) code.put ('%');
					else if (cur2 && !strcmp (t.str (), "$")) code.put ('$');
					else if (!cur2 && t.n == 1) { if (t.b[0] == ' ' || t.b[0] == '-' || t.b[0] == '/' || t.b[0] == ':' || t.b[0] == '.' || t.b[0] == ',') { if (t.b[0] == '.' || t.b[0] == ',') code.put ('\\'); code.put (t.b[0]); } }
					else { code.put ('"'); code.putn (t.b, t.n); code.put ('"'); }
				}
				else { code.put ('"'); for (int k = 0; k < t.n; k++) if (t.b[k] != '"') code.put (t.b[k]); code.put ('"'); }
			}
		}
		else if (X.ev == X_END)
		{
			if (X.isq ("style:style") || X.isq ("style:default-style")) cur = 0;
			else if (inNum && (X.isq ("number:number-style") || X.isq ("number:percentage-style") || X.isq ("number:currency-style") || X.isq ("number:date-style") ||
					   X.isq ("number:time-style") || X.isq ("number:text-style") || X.isq ("number:boolean-style")))
			{
				inNum = false;
				Buf full;
				if (numColor == 0xFF0000) full.puts ("[Red]");
				full.putn (code.b ? code.b : "", code.n);
				if (X.isq ("number:time-style") && strstr (full.str (), "hh") == full.str ()) {}
				if (num) scpy (num->code, full.n ? full.str () : "General", sizeof num->code);
				num = 0;
			}
		}
	}
}
static const ONum *find_num (ONum *nm, int n, const char *name) { for (int i = 0; i < n; i++) if (!strcmp (nm[i].name, name)) return &nm[i]; return 0; }
// A number style and its maps made one Excel code: "positive;negative;zero" (a style shows a negative
// number with its sign, a map's section as it is written: "-0.0%").
static void num_code (ONum *nm, int n, const char *name, char *out, int cap)
{
	const ONum *x = find_num (nm, n, name);
	if (!x) { scpy (out, "General", cap); return; }
	const char *pos = x->code, *neg = 0, *zero = 0;
	bool geo = false;						// (the positive section takes 0 too)
	for (int i = 0; i < x->nmap; i++)
	{
		const ONum *y = find_num (nm, n, x->mapName[i]);
		if (!y) continue;
		if (x->mapCond[i] == MC_GT) pos = y->code;
		else if (x->mapCond[i] == MC_GE) { pos = y->code; geo = true; }
		else if (x->mapCond[i] == MC_LT) neg = y->code;
		else if (x->mapCond[i] == MC_EQ) zero = y->code;
	}
	if (pos == x->code && !neg && !zero) { scpy (out, x->code, cap); return; }
	scpy (out, pos, cap); scat (out, ";", cap);
	if (neg) scat (out, neg, cap); else { scat (out, "-", cap); scat (out, x->code, cap); }
	if (zero) { scat (out, ";", cap); scat (out, zero, cap); }
	else if (!geo && pos != x->code) { scat (out, ";", cap); scat (out, x->code, cap); }
}
static const OStyle *find_style (OStyle *st, int n, const char *name) { for (int i = n - 1; i >= 0; i--) if (!strcmp (st[i].name, name)) return &st[i]; return 0; }
// A cell style resolved (its parents' properties under its own) -> the book's style index.
static unsigned short resolve_style (Book &b, OStyle *st, int n, ONum *nm, int nnm, const char *name)
{
	const OStyle *chain[12]; int k = 0;
	for (const OStyle *s = find_style (st, n, name); s && k < 12; s = s->parent[0] ? find_style (st, n, s->parent) : 0) { chain[k++] = s; if (!strcmp (s->name, s->parent)) break; }
	const OStyle *def = find_style (st, n, "!default");
	Style out; out.reset ();
	const char *data = 0;
	for (int i = -1; i < k; i++)
	{
		const OStyle *s = i < 0 ? def : chain[k - 1 - i];
		if (!s) continue;
		const Style &p = s->st;
		if (s->has[OP_FONT]) out.font = p.font;
		if (s->has[OP_SIZE]) out.size = p.size;
		if (s->has[OP_BOLD]) out.bold = p.bold;
		if (s->has[OP_ITALIC]) out.italic = p.italic;
		if (s->has[OP_UNDER]) out.under = p.under;
		if (s->has[OP_STRIKE]) out.strike = p.strike;
		if (s->has[OP_COLOR]) out.color = p.color;
		if (s->has[OP_FILL]) out.fill = p.fill;
		if (s->has[OP_HA]) out.ha = p.ha;
		if (s->has[OP_VA]) out.va = p.va;
		if (s->has[OP_WRAP]) out.wrap = p.wrap;
		if (s->has[OP_INDENT]) out.indent = p.indent;
		if (s->has[OP_BORDER]) for (int j = 0; j < 4; j++) { out.bs[j] = p.bs[j]; out.bc[j] = p.bc[j]; }
		if (s->data[0]) data = s->data;
	}
	if (data) { char code[160]; num_code (nm, nnm, data, code, sizeof code); out.fmt = (unsigned short) book_fmt (b, code); }
	return (unsigned short) b.styles.intern (out);
}

// A range's address in a chart ("Sales.A4:Sales.D16", "$'My sheet'.$B$5:.$B$16"): its sheet and cells.
static bool ods_range (Book &b, const char *a, int *sheetId, Rect *r)
{
	char part[2][160]; int np = 0;
	const char *p = a; bool q = false; int k = 0;
	while (*p == ' ') p++;
	for (; *p && np < 2; p++)
	{
		if (*p == '\'') q = !q;
		if ((*p == ':' || *p == ' ') && !q) { part[np][k] = 0; np++; k = 0; if (*p == ' ') break; continue; }
		if (k < 159) part[np][k++] = *p;
	}
	if (np < 2) { part[np][k] = 0; np++; }
	int rr[2] = { 0, 0 }, cc[2] = { 0, 0 };
	*sheetId = 0;
	for (int i = 0; i < np && i < 2; i++)
	{
		char *t = part[i];
		// the last '.' outside quotes: the sheet before it
		char *dot = 0; bool q2 = false;
		for (char *x = t; *x; x++) { if (*x == '\'') q2 = !q2; else if (*x == '.' && !q2) dot = x; }
		const char *cell = t;
		if (dot)
		{
			*dot = 0; cell = dot + 1;
			char *nm = t; if (*nm == '$') nm++;
			int L = (int) strlen (nm);
			if (L >= 2 && nm[0] == '\'' && nm[L - 1] == '\'') { nm[L - 1] = 0; nm++; }
			if (nm[0] && !*sheetId) { int k2 = book_sheet_index (b, nm); if (k2 >= 0) *sheetId = b.sh[k2]->id; }
		}
		if (!parse_cell_name (cell, &rr[i], &cc[i])) return false;
	}
	if (np == 1) { rr[1] = rr[0]; cc[1] = cc[0]; }
	*r = Rect { imin (rr[0], rr[1]), imin (cc[0], cc[1]), imax (rr[0], rr[1]), imax (cc[0], cc[1]) };
	return *sheetId != 0;
}
// A chart object's content.xml ("Object 1/content.xml") -> the chart (its place set by the caller).
static bool ods_chart (Book &b, const char *x, int len, Chart &c)
{
	XmlReader X (x, len);
	struct CS { char name[24]; bool vertical, stacked; } cs[32]; int ncs = 0;
	char plotStyle[24] = "";
	Buf v, title;
	bool inTitle = false, haveRange = false, firstSeries = true, inY = false;
	c.legend = LG_NONE; c.grid = false;
	while (X.next () != X_EOF)
	{
		if (X.ev == X_START)
		{
			if (X.isq ("style:style") && ncs < 32) { X.attr ("style:name", v); scpy (cs[ncs].name, v.str (), sizeof cs[ncs].name); cs[ncs].vertical = cs[ncs].stacked = false; ncs++; }
			else if (X.isq ("style:chart-properties") && ncs)
			{
				cs[ncs - 1].vertical = X.attr_bool ("chart:vertical", false);
				cs[ncs - 1].stacked = X.attr_bool ("chart:stacked", false) || X.attr_bool ("chart:percentage", false);
			}
			else if (X.isq ("chart:chart"))
			{
				X.attr ("chart:class", v);
				const char *k = v.str (); if (!strncmp (k, "chart:", 6)) k += 6;
				c.type = !strcmp (k, "line") ? CH_LINE : !strcmp (k, "area") ? CH_AREA : !strcmp (k, "circle") || !strcmp (k, "ring") ? CH_PIE : !strcmp (k, "scatter") ? CH_SCATTER : CH_COLUMN;
			}
			else if (X.isq ("chart:title")) inTitle = true;
			else if (X.isq ("chart:legend"))
			{
				X.attr ("chart:legend-position", v);
				c.legend = !strcmp (v.str (), "bottom") ? LG_BOTTOM : !strcmp (v.str (), "top") ? LG_TOP : LG_RIGHT;
			}
			else if (X.isq ("chart:plot-area"))
			{
				if (X.attr ("chart:style-name", v)) scpy (plotStyle, v.str (), sizeof plotStyle);
				if (X.attr ("table:cell-range-address", v)) haveRange = ods_range (b, v.str (), &c.srcSheet, &c.src);
				X.attr ("chart:data-source-has-labels", v);
				c.head = !strcmp (v.str (), "both") || !strcmp (v.str (), "row");
				c.side = !strcmp (v.str (), "both") || !strcmp (v.str (), "column");
			}
			else if (X.isq ("chart:series") && firstSeries)
			{
				firstSeries = false;
				int sid; Rect r;
				if (X.attr ("chart:values-cell-range-address", v) && ods_range (b, v.str (), &sid, &r)) c.byRows = r.r1 == r.r0 && r.c1 > r.c0;
			}
			else if (X.isq ("chart:axis")) { X.attr ("chart:dimension", v); inY = !strcmp (v.str (), "y"); }
			else if (X.isq ("chart:grid") && inY) c.grid = true;
		}
		else if (X.ev == X_TEXT && inTitle) title.putn (X.text.b ? X.text.b : "", X.text.n);
		else if (X.ev == X_END)
		{
			if (X.isq ("chart:title")) inTitle = false;
			else if (X.isq ("chart:axis")) inY = false;
		}
	}
	for (int i = 0; i < ncs; i++)
		if (!strcmp (cs[i].name, plotStyle))
		{
			if (cs[i].vertical && c.type == CH_COLUMN) c.type = CH_BAR;
			c.stacked = cs[i].stacked && c.type != CH_PIE && c.type != CH_SCATTER;
		}
	scpy (c.title, title.str (), sizeof c.title);
	return haveRange;
}
struct OChart { int sheet, r, c, x, y, w, h; char href[64]; };
// A condition of LibreOffice's ("top-elements(3)", ">4000", "between(1;5)", "contains-text(\"ap\")",
// "formula-is(MOD(ROW();2)=0)") -> a rule (its look set by the caller).
static bool ods_condition (Book &b, const char *v, CondFmt &c)
{
	memset (&c, 0, sizeof c); c.fill = c.color = AUTO; c.bold = c.italic = -1;
	while (*v == ' ') v++;
	auto arg = [&] (const char *p, char *out, int cap) {		// the text between the parentheses (an OpenFormula made Excel's)
		const char *o = strchr (p, '('); if (!o) { out[0] = 0; return; }
		const char *e = strrchr (o, ')'); if (!e) e = o + strlen (o);
		Buf in; in.puts ("of:="); in.putn (o + 1, (int) (e - o - 1));
		Buf ex; ods_formula (in.str (), ex);
		scpy (out, ex.str (), cap);
	};
	auto unq = [] (char *t) { int l = (int) strlen (t); if (l >= 2 && t[0] == '"' && t[l - 1] == '"') { memmove (t, t + 1, l - 2); t[l - 2] = 0; } };
	char a[120];
	if (!strncmp (v, "top-elements", 12) || !strncmp (v, "bottom-elements", 15) || !strncmp (v, "top-percent", 11) || !strncmp (v, "bottom-percent", 14))
	{ c.type = CF_TOP; c.op = v[0] == 'b'; c.pct = strstr (v, "percent") != 0; arg (v, a, sizeof a); scpy (c.a, a, sizeof c.a); return true; }
	if (!strncmp (v, "above", 5) || !strncmp (v, "below", 5)) { c.type = CF_AVERAGE; c.op = v[1] == 'e'; return true; }
	if (!strcmp (v, "duplicate") || !strcmp (v, "unique")) { c.type = CF_DUP; c.op = v[0] == 'u'; return true; }
	static const char *const TX[4] = { "contains-text", "not-contains-text", "begins-with", "ends-with" };
	for (int k = 0; k < 4; k++) if (!strncmp (v, TX[k], strlen (TX[k]))) { c.type = CF_TEXT; c.op = k; arg (v, a, sizeof a); unq (a); scpy (c.a, a, sizeof c.a); return true; }
	if (!strncmp (v, "formula-is", 10)) { c.type = CF_FORMULA; arg (v, a, sizeof a); c.a[0] = '='; scpy (c.a + 1, a, sizeof c.a - 1); return true; }
	if (!strncmp (v, "between", 7) || !strncmp (v, "not-between", 11))
	{
		c.type = CF_CELL; c.op = v[0] == 'n' ? CO_NOTBETWEEN : CO_BETWEEN;
		arg (v, a, sizeof a);					// "1,5" (the separator made a comma)
		char *comma = 0; int depth = 0; bool q = false;
		for (char *p = a; *p; p++) { if (*p == '"') q = !q; else if (!q && *p == '(') depth++; else if (!q && *p == ')') depth--; else if (!q && !depth && *p == ',') { comma = p; break; } }
		if (!comma) return false;
		*comma = 0;
		auto operand = [&] (const char *t, char *o, int cap) { double d; if (input_number (t, (int) strlen (t), &d) || t[0] == '"') scpy (o, t, cap); else { o[0] = '='; scpy (o + 1, t, cap - 1); } };
		operand (a, c.a, sizeof c.a); operand (comma + 1, c.b, sizeof c.b);
		return true;
	}
	static const char *const OPS[6] = { ">=", "<=", "!=", ">", "<", "=" };
	static const int OPC[6] = { CO_GE, CO_LE, CO_NE, CO_GT, CO_LT, CO_EQ };
	for (int k = 0; k < 6; k++)
		if (!strncmp (v, OPS[k], strlen (OPS[k])))
		{
			c.type = CF_CELL; c.op = OPC[k];
			const char *t = v + strlen (OPS[k]); while (*t == ' ') t++;
			Buf in; in.puts ("of:="); in.puts (t);
			Buf ex; ods_formula (in.str (), ex);
			double d;
			if (input_number (ex.str (), ex.n, &d) || ex.str ()[0] == '"') scpy (c.a, ex.str (), sizeof c.a);
			else { c.a[0] = '='; scpy (c.a + 1, ex.str (), sizeof c.a - 1); }
			return true;
		}
	return false;
}

static bool ods_read (Book &b, const char *data, int n, const char **why)
{
	const unsigned char *z = (const unsigned char *) data;
	int cl; char *content = zip_get (z, (unsigned) n, "content.xml", &cl);
	if (!content) { if (why) *why = "This file is not an OpenDocument spreadsheet (.ods)."; return false; }
	OStyle *st = 0; int nst = 0; ONum *nm = 0; int nnm = 0;
	g_nfaces = 0;
	int sl; char *styles = zip_get (z, (unsigned) n, "styles.xml", &sl);
	if (styles) { ods_styles (b, styles, sl, &st, &nst, &nm, &nnm); free (styles); }
	ods_styles (b, content, cl, &st, &nst, &nm, &nnm);
	// the default style becomes the book's default
	{
		unsigned short d = resolve_style (b, st, nst, nm, nnm, "Default");
		if (d)
		{
			Style ds = b.styles.s[d], old = b.styles.s[0];
			b.styles.s[0] = ds; b.styles.s[d] = old; b.styles.rehash ();
		}
	}
	// style name -> index, cached
	struct Cache { char name[48]; unsigned short idx; } *cache = 0; int ncache = 0;
	auto cell_st = [&] (const char *name) -> unsigned short {
		if (!name[0] || !strcmp (name, "Default")) return 0;
		for (int i = 0; i < ncache; i++) if (!strcmp (cache[i].name, name)) return cache[i].idx;
		unsigned short ix = resolve_style (b, st, nst, nm, nnm, name);
		cache = (Cache *) realloc (cache, (ncache + 1) * sizeof (Cache));
		scpy (cache[ncache].name, name, sizeof cache[ncache].name); cache[ncache].idx = ix; ncache++;
		return ix; };
	// the sheets first (a formula may name one that comes later)
	{
		XmlReader P (content, cl);
		Buf nm2;
		while (P.next () != X_EOF)
			if (P.ev == X_START && P.isq ("table:table"))
			{
				P.attr ("table:name", nm2);
				if (!book_add_sheet (b, nm2.n ? nm2.str () : "Sheet")) break;
				P.skip ();
			}
	}
	XmlReader X (content, cl);
	Sheet *s = 0;
	int tableNo = 0;
	int row = 0, col = 0, rowRep = 1;
	long long cellsMade = 0;
	Buf v, text, para;
	int colDefault[1] = { 0 };
	(void) colDefault;
	unsigned short *colCellStyle = 0;
	OChart och[32]; int nch = 0;
	Rect cfRange = { -1, 0, 0, 0 };
	Sheet *afSheet = 0; int afN = 0, afC[AF_MAXCOLS]; Buf afV[AF_MAXCOLS];	// (an AutoFilter being read)
	while (X.next () != X_EOF)
	{
		if (X.ev == X_START)
		{
			if (X.isq ("table:table"))
			{
				s = tableNo < b.ns ? b.sh[tableNo] : 0;
				tableNo++;
				row = 0; col = 0;
				free (colCellStyle); colCellStyle = (unsigned short *) calloc (MAXC, sizeof (unsigned short));
				if (!s) break;
			}
			else if (s && X.isq ("calcext:conditional-format"))		// LibreOffice's conditional formats
			{
				Buf ad; X.attr ("calcext:target-range-address", ad);
				int sid; cfRange.r0 = -1;
				if (!ods_range (b, ad.str (), &sid, &cfRange)) cfRange.r0 = -1;
			}
			else if (s && cfRange.r0 >= 0 && X.isq ("calcext:condition"))
			{
				Buf val, sty; X.attr ("calcext:value", val); X.attr ("calcext:apply-style-name", sty);
				CondFmt c;
				if (ods_condition (b, val.str (), c))
				{
					c.r = cfRange;
					const OStyle *os = find_style (st, nst, sty.str ());
					if (!os) for (int i = nst - 1; i >= 0; i--) if (!strcmp (st[i].dname, sty.str ())) { os = &st[i]; break; }
					for (int k = 0; os && k < 8; k++)			// (its own properties only, then its parents' but the default's)
					{
						if (os->has[OP_FILL] && c.fill == AUTO) c.fill = os->st.fill;
						if (os->has[OP_COLOR] && c.color == AUTO) c.color = os->st.color;
						if (os->has[OP_BOLD] && c.bold < 0) c.bold = os->st.bold;
						if (os->has[OP_ITALIC] && c.italic < 0) c.italic = os->st.italic;
						if (!os->parent[0] || !strcmp (os->parent, "Default")) break;
						os = find_style (st, nst, os->parent);
					}
					s->cf = (CondFmt *) realloc (s->cf, (s->ncf + 1) * sizeof (CondFmt)); s->cf[s->ncf++] = c;
				}
			}
			else if (s && cfRange.r0 >= 0 && (X.isq ("calcext:color-scale") || X.isq ("calcext:data-bar")))
			{
				CondFmt c; memset (&c, 0, sizeof c); c.fill = c.color = AUTO; c.bold = c.italic = -1; c.r = cfRange;
				if (X.isq ("calcext:data-bar")) { Buf pc; c.type = CF_BAR; c.c0 = X.attr ("calcext:positive-color", pc) ? ods_color (pc.str ()) : 0x638EC6; }
				else
				{
					c.type = CF_SCALE;
					unsigned col[3] = { 0, 0, 0 }; int nc2 = 0;
					while (X.next () != X_EOF && !(X.ev == X_END && X.isq ("calcext:color-scale")))
						if (X.ev == X_START && X.isq ("calcext:color-scale-entry") && nc2 < 3) { Buf cc; X.attr ("calcext:color", cc); col[nc2++] = ods_color (cc.str ()); }
					c.op = nc2 >= 3 ? 3 : 2; c.c0 = col[0];
					if (c.op == 3) { c.c1 = col[1]; c.c2 = col[2]; } else c.c2 = col[1];
				}
				s->cf = (CondFmt *) realloc (s->cf, (s->ncf + 1) * sizeof (CondFmt)); s->cf[s->ncf++] = c;
			}
			else if (X.isq ("table:database-range") && X.attr_bool ("table:display-filter-buttons", false))	// an AutoFilter
			{
				Buf ad; X.attr ("table:target-range-address", ad);
				int sid; Rect ar;
				afSheet = ods_range (b, ad.str (), &sid, &ar) ? book_sheet_by_id (b, sid) : 0;
				if (afSheet && !afSheet->af.on) { afSheet->af.on = true; afSheet->af.r = ar; afN = 0; }
				else afSheet = 0;
			}
			else if (afSheet && X.isq ("table:filter-condition"))
			{
				Buf op, val; X.attr ("table:operator", op); X.attr ("table:value", val);
				int fc = afSheet->af.r.c0 + X.attr_int ("table:field-number", 0);
				bool empty = !strcmp (op.str (), "empty");		// (the empty cells shown)
				if ((!strcmp (op.str (), "=") || empty) && afN < AF_MAXCOLS)
				{
					int k = -1; for (int i = 0; i < afN; i++) if (afC[i] == fc) k = i;
					if (k < 0) { k = afN++; afC[k] = fc; afV[k].clear (); }
					if (empty) afV[k].put ('\x1F');
					else if (val.n || X.empty) { afV[k].puts (val.str ()); afV[k].put ('\x1F'); }
				}
			}
			else if (afSheet && X.isq ("table:filter-set-item") && afN)
			{
				Buf val; X.attr ("table:value", val);
				afV[afN - 1].puts (val.str ()); afV[afN - 1].put ('\x1F');
			}
			else if (X.isq ("table:named-range") || X.isq ("table:named-expression"))	// a name (in a table: that sheet's own)
			{
				Buf nm, addr, of, ef;
				X.attr ("table:name", nm);
				if (X.isq ("table:named-range")) { X.attr ("table:cell-range-address", addr); of.puts ("of:=["); of.puts (addr.str ()); of.put (']'); }
				else X.attr ("table:expression", of);
				ods_formula (of.str (), ef);
				if (nm.n && ef.n) name_set (b, nm.str (), s ? s->id : 0, ef.str ());
			}
			else if (s && X.isq ("table:table-column"))
			{
				int rep = iclamp (X.attr_int ("table:number-columns-repeated", 1), 1, MAXC);
				X.attr ("table:style-name", v);
				const OStyle *cs = v.n ? find_style (st, nst, v.str ()) : 0;
				Buf dcs; bool hasDcs = X.attr ("table:default-cell-style-name", dcs);
				bool hid = X.attr ("table:visibility", v) && strcmp (v.str (), "visible");
				unsigned short dst = hasDcs ? cell_st (dcs.str ()) : 0;
				for (int k = 0; k < rep && col < MAXC; k++, col++)
				{
					if (cs && cs->colW > 0) s->colW[col] = (unsigned short) iclamp (cs->colW, 1, 4000);
					if (hid) s->colFl[col] |= RF_HIDDEN;
					colCellStyle[col] = dst;
				}
				cols_changed (s);
			}
			else if (s && X.isq ("table:table-row"))
			{
				col = 0;
				rowRep = iclamp (X.attr_int ("table:number-rows-repeated", 1), 1, MAXR);
				X.attr ("table:style-name", v);
				const OStyle *rs = v.n ? find_style (st, nst, v.str ()) : 0;
				bool hid = X.attr ("table:visibility", v) && strcmp (v.str (), "visible"), filt = hid && !strcmp (v.str (), "filter");
				if (rowRep <= 1000 && ((rs && rs->rowH > 0 && !rs->optimal) || hid))
					for (int k = 0; k < rowRep && row + k < MAXR; k++)
					{
						RowInfo *ri = row_add (s, row + k);
						if (rs && rs->rowH > 0 && !rs->optimal) { ri->fl |= RF_CUSTOM; ri->h = (unsigned short) iclamp (rs->rowH, 1, 2000); }
						if (hid) ri->fl |= RF_HIDDEN | (filt ? RF_FILTER : 0);
					}
			}
			else if (s && (X.isq ("table:table-cell") || X.isq ("table:covered-table-cell")))
			{
				bool covered = X.isq ("table:covered-table-cell");
				int rep = iclamp (X.attr_int ("table:number-columns-repeated", 1), 1, MAXC);
				Buf type, sname, formula, val, dval, tval, bval, sval;
				X.attr ("office:value-type", type); X.attr ("table:style-name", sname); X.attr ("table:formula", formula);
				X.attr ("office:value", val); X.attr ("office:date-value", dval); X.attr ("office:time-value", tval);
				X.attr ("office:boolean-value", bval); X.attr ("office:string-value", sval);
				int spanC = X.attr_int ("table:number-columns-spanned", 1), spanR = X.attr_int ("table:number-rows-spanned", 1);
				// its text: the paragraphs
				text.clear ();
				if (!X.empty)
				{
					int depth = 1, paras = 0;
					while (depth > 0 && X.next () != X_EOF)
					{
						if (X.ev == X_START)
						{
							depth++;
							if (X.isq ("text:p")) { if (paras++) text.put ('\n'); }
							else if (X.isq ("text:s")) { int c = X.attr_int ("text:c", 1); for (int k = 0; k < c && k < 1000; k++) text.put (' '); }
							else if (X.isq ("text:tab")) text.put ('\t');
							else if (X.isq ("text:line-break")) text.put ('\n');
							else if (X.isq ("office:annotation")) { X.skip (); depth--; }
							else if (X.isq ("draw:frame"))			// (a chart anchored in the cell)
							{
								Buf fx, fy, fw, fh;
								X.attr ("svg:x", fx); X.attr ("svg:y", fy); X.attr ("svg:width", fw); X.attr ("svg:height", fh);
								int fd = 1;
								while (fd > 0 && X.next () != X_EOF)
								{
									if (X.ev == X_START)
									{
										fd++;
										Buf href;
										if (X.isq ("draw:object") && X.attr ("xlink:href", href) && nch < 32)
										{
											OChart &oc = och[nch++];
											oc.sheet = tableNo - 1; oc.r = row; oc.c = col;
											oc.x = (int) ods_len (fx.str ()); oc.y = (int) ods_len (fy.str ());
											oc.w = (int) ods_len (fw.str ()); oc.h = (int) ods_len (fh.str ());
											const char *hr = href.str (); if (!strncmp (hr, "./", 2)) hr += 2;
											scpy (oc.href, hr, sizeof oc.href);
										}
									}
									else if (X.ev == X_END) fd--;
								}
								depth--;
							}
						}
						else if (X.ev == X_END) depth--;
						else if (X.ev == X_TEXT && depth >= 2) text.putn (X.text.b ? X.text.b : "", X.text.n);
					}
				}
				else X.next ();					// (its own end)
				bool empty = !type.n && !formula.n && !text.n;
				unsigned short cst = sname.n ? cell_st (sname.str ()) : colCellStyle[iclamp (col, 0, MAXC - 1)];
				if (covered || (empty && !cst)) { col += rep; continue; }
				if (empty && rep > 64) { col += rep; continue; }
				for (int k = 0; k < rep && col < MAXC; k++, col++)
				{
					if (cellsMade > 5000000) break;
					for (int rr = 0; rr < (empty ? 1 : rowRep) && rr < 1000 && row + rr < MAXR; rr++)
					{
						Cell *x = s->cells.add (row + rr, col);
						cellsMade++;
						x->style = cst;
						if (empty) continue;
						const char *t = type.str ();
						if (!strcmp (t, "float") || !strcmp (t, "percentage") || !strcmp (t, "currency")) { x->kind = K_NUM; x->vt = V_NUM; x->num = strtod (val.str (), 0); }
						else if (!strcmp (t, "date")) { double d; if (parse_datetime (dval.str (), 10, &d, 0, 0)) { const char *T = strchr (dval.str (), 'T'); if (T) { double tt; if (parse_datetime (T + 1, (int) strlen (T + 1), &tt, 0, 0)) d += tt; } x->kind = K_NUM; x->vt = V_NUM; x->num = d; } }
						else if (!strcmp (t, "time"))
						{
							// PT14H30M00S
							double h = 0, m = 0, se = 0; const char *p = tval.str ();
							while (*p) { char *e; double q = strtod (p, &e); if (e == p) { p++; continue; } if (*e == 'H') h = q; else if (*e == 'M') m = q; else if (*e == 'S') se = q; p = *e ? e + 1 : e; }
							x->kind = K_NUM; x->vt = V_NUM; x->num = (h * 3600 + m * 60 + se) / 86400.0;
						}
						else if (!strcmp (t, "boolean")) { x->kind = K_BOOL; x->vt = V_BOOL; x->num = !strcmp (bval.str (), "true") ? 1 : 0; }
						else if (!strcmp (t, "string") || (text.n && !t[0])) { x->kind = K_STR; x->vt = V_STR; x->str = sdup (sval.n ? sval.str () : text.str ()); }
						if (formula.n)
						{
							Buf ef; ods_formula (formula.str (), ef);
							Formula *f = formula_parse (b, ef.str (), ef.n);
							if (f)
							{
								unsigned char vt = x->vt; double num = x->num; char *str = x->str;
								x->kind = K_FORM; x->f = f;
								x->vt = vt == V_EMPTY ? (unsigned char) V_NUM : vt; x->num = num; x->str = str;
								if (!type.n && text.n && text.b[0] == '#') { x->vt = V_ERR; x->err = E_VALUE; for (int e = 1; e <= 7; e++) if (!strcmp (text.str (), ERR_NAMES[e])) x->err = (unsigned char) e; }
							}
						}
						if (k == 0 && rr == 0 && (spanC > 1 || spanR > 1))
						{ Rect m = { row, col, imin (MAXR - 1, row + spanR - 1), imin (MAXC - 1, col + spanC - 1) }; merge_add (s, m); }
					}
				}
			}
		}
		else if (X.ev == X_END)
		{
			if (afSheet && X.isq ("table:database-range")) { for (int i = 0; i < afN; i++) af_set (afSheet, afC[i], afV[i].str ()); afSheet = 0; }
			if (s && X.isq ("table:table-row")) { row += rowRep; rowRep = 1; }
			else if (X.isq ("table:table")) { if (s) { sheet_touched (s); rows_changed (s); } s = 0; }
		}
	}
	free (colCellStyle);
	free (cache); free (st); free (nm); free (content);
	if (b.ns == 0) { if (why) *why = "The spreadsheet holds no sheet."; return false; }
	// the charts: their objects read
	for (int i = 0; i < nch; i++)
	{
		const OChart &oc = och[i];
		if (oc.sheet < 0 || oc.sheet >= b.ns || oc.w < 20 || oc.h < 20) continue;
		char path[96]; snprintf (path, sizeof path, "%s/content.xml", oc.href);
		int ol; char *ox = zip_get (z, (unsigned) n, path, &ol);
		if (!ox) continue;
		Chart c; memset (&c, 0, sizeof c);
		if (ods_chart (b, ox, ol, c))
		{
			Sheet *t = b.sh[oc.sheet];
			c.x = col_x (t, oc.c) + oc.x; c.y = (int) row_y (t, oc.r) + oc.y; c.w = oc.w; c.h = oc.h;
			Chart *nc = (Chart *) malloc (sizeof (Chart)); *nc = c;
			chart_anchor (t, nc);
			t->charts = (Chart **) realloc (t->charts, (t->ncharts + 1) * sizeof (Chart *));
			t->charts[t->ncharts++] = nc;
		}
		free (ox);
	}
	// frozen panes and the sheet shown (settings.xml)
	int sl2; char *set = zip_get (z, (unsigned) n, "settings.xml", &sl2);
	if (set)
	{
		XmlReader S (set, sl2);
		Sheet *cur = 0; Buf item, name;
		int hmode = 0, vmode = 0, hpos = 0, vpos = 0;
		while (S.next () != X_EOF)
		{
			if (S.ev == X_START && S.isq ("config:config-item-map-entry"))
			{
				if (S.attr ("config:name", name)) { int k = book_sheet_index (b, name.str ()); cur = k >= 0 ? b.sh[k] : 0; hmode = vmode = hpos = vpos = 0; }
			}
			else if (S.ev == X_START && S.isq ("config:config-item"))
			{
				S.attr ("config:name", item);
				Buf val2;
				if (S.next () == X_TEXT) val2.putn (S.text.b ? S.text.b : "", S.text.n);
				const char *it = item.str ();
				int iv = atoi (val2.str ());
				if (!strcmp (it, "ActiveTable")) { int k = book_sheet_index (b, val2.str ()); if (k >= 0) b.active = k; }
				else if (cur)
				{
					if (!strcmp (it, "HorizontalSplitMode")) hmode = iv;
					else if (!strcmp (it, "VerticalSplitMode")) vmode = iv;
					else if (!strcmp (it, "HorizontalSplitPosition")) hpos = iv;
					else if (!strcmp (it, "VerticalSplitPosition")) vpos = iv;
					else if (!strcmp (it, "ShowGrid")) cur->grid = strcmp (val2.str (), "false") != 0;
					if (hmode == 2) cur->freezeC = hpos;
					if (vmode == 2) cur->freezeR = vpos;
				}
			}
		}
		free (set);
	}
	return true;
}

// ---- CSV -------------------------------------------------------------------------------------------------
// Text separated by commas, semicolons or tabs (the most regular of them in the first lines), quotes
// as RFC 4180; UTF-8 (a BOM taken off) or else Latin-1. Each field read as typed (not a formula).
static char csv_guess (const char *t, int n)
{
	static const char C[3] = { ',', ';', '\t' };
	int best = 0, bestScore = -1;
	for (int k = 0; k < 3; k++)
	{
		int lines = 0, first = -1, same = 0, cnt = 0; bool q = false;
		for (int i = 0; i < n && lines < 20; i++)
		{
			char c = t[i];
			if (c == '"') q = !q;
			else if (!q && c == C[k]) cnt++;
			else if (!q && c == '\n')
			{
				if (first < 0) first = cnt; else if (cnt == first && cnt > 0) same++;
				lines++; cnt = 0;
			}
		}
		int score = first > 0 ? same * 100 + first : 0;
		if (score > bestScore) { bestScore = score; best = k; }
	}
	return C[best];
}
static void csv_read (Book &b, Sheet *s, const char *data, int n)
{
	char *own = 0;
	if (n >= 3 && (unsigned char) data[0] == 0xEF && (unsigned char) data[1] == 0xBB && (unsigned char) data[2] == 0xBF) { data += 3; n -= 3; }
	if (!u8_valid (data, n)) { own = latin1_to_u8 (data, n); data = own; n = (int) strlen (own); }
	char sep = csv_guess (data, n);
	int r = 0, c = 0;
	Buf f; bool q = false, quoted = false;
	for (int i = 0; i <= n; i++)
	{
		char ch = i < n ? data[i] : '\n';
		if (q)
		{
			if (ch == '"') { if (i + 1 < n && data[i + 1] == '"') { f.put ('"'); i++; } else q = false; }
			else f.put (ch);
			continue;
		}
		if (ch == '"' && f.n == 0) { q = true; quoted = true; continue; }
		if (ch == sep || ch == '\n' || (ch == '\r' && i + 1 < n && data[i + 1] == '\n'))
		{
			if (ch == '\r') i++;
			if (i == n && f.n == 0 && c == 0 && !quoted) break;
			if (r < MAXR && c < MAXC && (f.n || quoted))
			{
				if (f.n && (f.b[0] == '=' || f.b[0] == '+' || f.b[0] == '-' || f.b[0] == '@'))
				{
					double v; if (parse_typed_number (f.str (), f.n, &v, 0, 0)) cell_input (b, s, r, c, f.str ()); else cell_set_str (s, r, c, f.str (), f.n);
				}
				else cell_input (b, s, r, c, f.str ());
			}
			f.clear (); quoted = false;
			if (ch == sep) c++; else { r++; c = 0; }
			continue;
		}
		f.put (ch);
	}
	free (own);
	sheet_touched (s);
}
// A sheet as CSV (the values as shown), separator sep.
static char *csv_write (Book &b, Sheet *s, char sep, int *len)
{
	sheet_bounds (s);
	Buf o;
	for (int r = 0; r <= s->maxR; r++)
	{
		for (int c = 0; c <= s->maxC; c++)
		{
			if (c) o.put (sep);
			Cell *x = s->cells.get (r, c);
			Shown sh; cell_shown (b, s, x, sh, 64);
			const char *t = sh.text;
			if (x && x->vt == V_STR) t = x->str;
			bool need = strchr (t, sep) || strchr (t, '"') || strchr (t, '\n') || strchr (t, '\r');
			if (need) { o.put ('"'); for (const char *p = t; *p; p++) { if (*p == '"') o.put ('"'); o.put (*p); } o.put ('"'); }
			else o.puts (t);
		}
		o.puts ("\r\n");
	}
	*len = o.n;
	return o.take ();
}

// ---- a file opened, saved --------------------------------------------------------------------------------
static bool ends_with (const char *s, const char *e) { int n = (int) strlen (s), k = (int) strlen (e); return n >= k && ascii_ieq (s + n - k, e, k); }
// The book from a file (it is emptied first): .xlsx, .ods, .csv / .txt / .tsv (by its content too).
static bool book_load (Book &b, const char *path, const char **why)
{
	int n; char *d = read_file (path, &n);
	if (!d) { if (why) *why = "The file cannot be read."; return false; }
	book_clear (b); book_init (b);
	bool zip = n > 4 && d[0] == 'P' && d[1] == 'K';
	bool ok;
	if (zip)
	{
		pngsave::ZipEntry e;
		if (pngsave::zip_find ((const unsigned char *) d, (unsigned) n, "content.xml", &e)) ok = ods_read (b, d, n, why);
		else ok = xlsx_read (b, d, n, why);
	}
	else
	{
		Sheet *s = book_add_sheet (b, "Sheet1");
		const char *base = strrchr (path, '/'); base = base ? base + 1 : path;
		char nm[64]; scpy (nm, base, sizeof nm); char *dot = strrchr (nm, '.'); if (dot) *dot = 0;
		if (nm[0] && strlen (nm) < 31) scpy (s->name, nm, sizeof s->name);
		csv_read (b, s, d, n);
		ok = true;
	}
	free (d);
	if (!ok) { book_clear (b); book_init (b); book_add_sheet (b, "Sheet1"); return false; }
	for (int i = 0; i < b.ns; i++) { sheet_touched (b.sh[i]); cols_changed (b.sh[i]); rows_changed (b.sh[i]); }
	recalc (b);
	return true;
}
// The book written as the file's name says (.xlsx; .csv: the sheet shown).
static bool book_save (Book &b, const char *path, const char **why)
{
	int n = 0; char *d;
	if (ends_with (path, ".csv") || ends_with (path, ".txt")) d = csv_write (b, b.sh[b.active], ',', &n);
	else if (ends_with (path, ".tsv")) d = csv_write (b, b.sh[b.active], '\t', &n);
	else d = xlsx_write (b, &n);
	bool ok = d && write_file (path, d, n);
	free (d);
	if (!ok && why) *why = "The file cannot be written.";
	return ok;
}

} // namespace ss

#endif
