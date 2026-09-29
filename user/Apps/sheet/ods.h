//
// ods.h -- OpenDocument spreadsheets (.ods, LibreOffice's), read: the sheets, the values (numbers,
// percentages, amounts, dates, times, booleans, texts of several paragraphs), the formulas (OpenFormula,
// "of:=SUM([.A1:.A5])", turned into Excel's syntax), the cell styles (fonts, colours, borders,
// alignment, wrapping; the number styles made into format codes), the columns' widths, the rows'
// heights, merged and hidden cells, frozen panes (settings.xml).
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
	char name[48], parent[48], data[48];
	int family;					// 1 cell, 2 column, 3 row
	bool has[16];					// which of the properties below it sets
	Style st;
	int colW, rowH; bool optimal;
};
enum { OP_FONT, OP_SIZE, OP_BOLD, OP_ITALIC, OP_UNDER, OP_STRIKE, OP_COLOR, OP_FILL, OP_HA, OP_VA, OP_WRAP, OP_BORDER, OP_INDENT };
struct ONum { char name[48]; char code[96]; char mapName[48]; int mapCond; };	// mapCond: 1 >=0, 2 <0, 3 >0

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
			if (X.isq ("style:style") || X.isq ("style:default-style"))
			{
				*st = (OStyle *) realloc (*st, (*nst + 1) * sizeof (OStyle));
				cur = &(*st)[(*nst)++]; memset (cur, 0, sizeof *cur); cur->st.reset ();
				if (X.attr ("style:name", v)) scpy (cur->name, v.str (), sizeof cur->name); else scpy (cur->name, "!default", sizeof cur->name);
				if (X.attr ("style:parent-style-name", v)) scpy (cur->parent, v.str (), sizeof cur->parent);
				else if (strcmp (cur->name, "!default") && strcmp (cur->name, "Default")) scpy (cur->parent, "Default", sizeof cur->parent);
				if (X.attr ("style:data-style-name", v)) scpy (cur->data, v.str (), sizeof cur->data);
				X.attr ("style:family", v);
				cur->family = !strcmp (v.str (), "table-cell") ? 1 : !strcmp (v.str (), "table-column") ? 2 : !strcmp (v.str (), "table-row") ? 3 : 0;
				if (X.empty) cur = 0;
			}
			else if (cur && X.isq ("style:text-properties"))
			{
				if (X.attr ("style:font-name", v) || X.attr ("fo:font-family", v)) { cur->st.font = (unsigned short) book_font (b, v.str ()); cur->has[OP_FONT] = true; }
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
			else if (inNum && X.isq ("style:map") && num)
			{
				Buf cnd, an; X.attr ("style:condition", cnd); X.attr ("style:apply-style-name", an);
				scpy (num->mapName, an.str (), sizeof num->mapName);
				num->mapCond = strstr (cnd.str (), ">=0") ? 1 : strstr (cnd.str (), "<0") ? 2 : strstr (cnd.str (), ">0") ? 3 : 0;
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
static void num_code (ONum *nm, int n, const char *name, char *out, int cap)
{
	const ONum *x = find_num (nm, n, name);
	if (!x) { scpy (out, "General", cap); return; }
	if (x->mapName[0])
	{
		const ONum *y = find_num (nm, n, x->mapName);
		if (y && (x->mapCond == 1 || x->mapCond == 2))
		{
			const ONum *a = x->mapCond == 1 ? y : x, *z = x->mapCond == 1 ? x : y;
			scpy (out, a->code, cap); scat (out, ";", cap); scat (out, z->code, cap);
			return;
		}
	}
	scpy (out, x->code, cap);
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

static bool ods_read (Book &b, const char *data, int n, const char **why)
{
	const unsigned char *z = (const unsigned char *) data;
	int cl; char *content = zip_get (z, (unsigned) n, "content.xml", &cl);
	if (!content) { if (why) *why = "This file is not an OpenDocument spreadsheet (.ods)."; return false; }
	OStyle *st = 0; int nst = 0; ONum *nm = 0; int nnm = 0;
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
				bool hid = X.attr ("table:visibility", v) && strcmp (v.str (), "visible");
				if (rowRep <= 1000 && ((rs && rs->rowH > 0 && !rs->optimal) || hid))
					for (int k = 0; k < rowRep && row + k < MAXR; k++)
					{
						RowInfo *ri = row_add (s, row + k);
						if (rs && rs->rowH > 0 && !rs->optimal) { ri->fl |= RF_CUSTOM; ri->h = (unsigned short) iclamp (rs->rowH, 1, 2000); }
						if (hid) ri->fl |= RF_HIDDEN;
					}
				if (X.empty) { row += rowRep; rowRep = 1; }
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
			if (s && X.isq ("table:table-row")) { row += rowRep; rowRep = 1; }
			else if (X.isq ("table:table")) { if (s) { sheet_touched (s); rows_changed (s); } s = 0; }
		}
	}
	free (colCellStyle);
	free (cache); free (st); free (nm); free (content);
	if (b.ns == 0) { if (why) *why = "The spreadsheet holds no sheet."; return false; }
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
