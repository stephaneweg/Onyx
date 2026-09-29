//
// xlsx.h -- Excel's workbook (.xlsx, Office Open XML): the spreadsheet's own format. Read: the sheets,
// the values and formulas (shared formulas unfolded; "_xlfn." taken off), the shared strings (rich text
// joined), the styles (fonts, fills, borders, number formats -- built-in and custom --, alignment; theme
// and indexed colours), the columns' widths, the rows' heights, hidden rows / columns, merged cells,
// frozen panes, tab colours, the charts (bar, column, line, area, pie, scatter: their data ranges).
// Written: the same, with each formula's last value (another program shows it before computing).
//
#ifndef _sheet_xlsx_h
#define _sheet_xlsx_h

#include "undo.h"
#include "xml.h"
#include "img/pngsave.hpp"
#include "img/imgload.hpp"
#ifdef SHEET_APP
#include "kapi.h"
#endif

namespace ss {

// ---- files ---------------------------------------------------------------------------------------------
#ifdef SHEET_APP			// (the app: the kernel's files, SD:/...; the tests on the PC: stdio)
static char *read_file (const char *path, int *len)
{
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned n = kapi_fsize (f);
	if (n > (256u << 20)) { kapi_close (f); return 0; }
	char *b = (char *) malloc (n + 1);
	unsigned got = 0;
	while (got < n) { int k = kapi_read (f, b + got, n - got); if (k <= 0) break; got += (unsigned) k; }
	kapi_close (f);
	if (got != n) { free (b); return 0; }
	b[n] = 0;
	*len = (int) n;
	return b;
}
static bool write_file (const char *path, const void *data, int len) { return kapi_save_file (path, data, (unsigned) len) >= 0; }
#else
static char *read_file (const char *path, int *len)
{
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	if (n < 0 || n > (256l << 20)) { fclose (f); return 0; }
	char *b = (char *) malloc (n + 1);
	long got = (long) fread (b, 1, n, f);
	fclose (f);
	if (got != n) { free (b); return 0; }
	b[n] = 0;
	*len = (int) n;
	return b;
}
static bool write_file (const char *path, const void *data, int len)
{
	FILE *f = fopen (path, "wb");
	if (!f) return false;
	bool ok = (int) fwrite (data, 1, len, f) == len;
	if (fclose (f)) ok = false;
	return ok;
}
#endif

// An entry of the archive, inflated (malloc'd, NUL-terminated).
static char *zip_get (const unsigned char *z, unsigned zn, const char *name, int *len)
{
	pngsave::ZipEntry e;
	if (!pngsave::zip_find (z, zn, name, &e)) return 0;
	if (e.method == 0)
	{
		char *d = (char *) malloc (e.csize + 1); memcpy (d, e.data, e.csize); d[e.csize] = 0;
		*len = (int) e.csize; return d;
	}
	if (e.method != 8) return 0;
	unsigned ol = 0;
	unsigned char *o = img_inflate (e.data, e.csize, false, &ol);
	if (!o) return 0;
	char *d = (char *) malloc (ol + 1); memcpy (d, o, ol); d[ol] = 0;
	delete[] o;
	*len = (int) ol;
	return d;
}
// A relationship's target from the part `base` ("xl/workbook.xml" + "worksheets/sheet1.xml").
static void part_path (const char *base, const char *target, char *out, int cap)
{
	if (target[0] == '/') { scpy (out, target + 1, cap); return; }
	char dir[256]; scpy (dir, base, sizeof dir);
	char *sl = strrchr (dir, '/'); if (sl) sl[1] = 0; else dir[0] = 0;
	const char *t = target;
	while (!strncmp (t, "../", 3))
	{
		t += 3;
		int n = (int) strlen (dir);
		if (n > 0) dir[n - 1] = 0;
		char *s2 = strrchr (dir, '/'); if (s2) s2[1] = 0; else dir[0] = 0;
	}
	scpy (out, dir, cap); scat (out, t, cap);
}
struct Rel { char id[32]; char target[160]; char type[24]; };
static int read_rels (const unsigned char *z, unsigned zn, const char *part, Rel *out, int cap)
{
	char rp[256];
	const char *sl = strrchr (part, '/');
	if (sl) snprintf (rp, sizeof rp, "%.*s_rels/%s.rels", (int) (sl - part + 1), part, sl + 1);
	else snprintf (rp, sizeof rp, "_rels/%s.rels", part);
	int len; char *x = zip_get (z, zn, rp, &len);
	if (!x) return 0;
	int n = 0;
	XmlReader X (x, len);
	while (X.next () != X_EOF)
		if (X.ev == X_START && X.is ("Relationship") && n < cap)
		{
			Buf id, tg, ty;
			X.attr ("Id", id); X.attr ("Target", tg); X.attr ("Type", ty);
			scpy (out[n].id, id.str (), sizeof out[n].id);
			part_path (part, tg.str (), out[n].target, sizeof out[n].target);
			const char *t = strrchr (ty.str (), '/');
			scpy (out[n].type, t ? t + 1 : ty.str (), sizeof out[n].type);
			n++;
		}
	free (x);
	return n;
}
static const char *rel_target (Rel *r, int n, const char *id) { for (int i = 0; i < n; i++) if (!strcmp (r[i].id, id)) return r[i].target; return 0; }

// ---- colours -----------------------------------------------------------------------------------------------
static const unsigned INDEXED[64] = {
	0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
	0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080, 0xC0C0C0, 0x808080, 0x9999FF, 0x993366, 0xFFFFCC, 0xCCFFFF, 0x660066, 0xFF8080, 0x0066CC, 0xCCCCFF,
	0x000080, 0xFF00FF, 0xFFFF00, 0x00FFFF, 0x800080, 0x800000, 0x008080, 0x0000FF, 0x00CCFF, 0xCCFFFF, 0xCCFFCC, 0xFFFF99, 0x99CCFF, 0xFF99CC, 0xCC99FF, 0xFFCC99,
	0x3366FF, 0x33CCCC, 0x99CC00, 0xFFCC00, 0xFF9900, 0xFF6600, 0x666699, 0x969696, 0x003366, 0x339966, 0x003300, 0x333300, 0x993300, 0x993366, 0x333399, 0x333333 };
static unsigned g_theme[12] = { 0x000000, 0xFFFFFF, 0x44546A, 0xE7E6E6, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0x0563C1, 0x954F72 };	// dk1 lt1 dk2 lt2 accents hlink folHlink

static unsigned tint (unsigned c, double t)
{
	if (t == 0) return c;
	unsigned r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
	auto f = [&] (unsigned v) -> unsigned { double x = t < 0 ? v * (1 + t) : v + (255 - v) * t; return (unsigned) (x < 0 ? 0 : x > 255 ? 255 : x + 0.5); };
	return f (r) << 16 | f (g) << 8 | f (b);
}
// A <color .../> (or fgColor...): rgb="FF112233", theme="1" tint="-0.25", indexed="10", auto="1".
static unsigned xml_color (const XmlReader &X, unsigned def)
{
	Buf v;
	if (X.attr ("rgb", v) && v.n >= 6) { unsigned c = (unsigned) strtoul (v.str () + (v.n >= 8 ? 2 : 0), 0, 16) & 0xFFFFFF; return tint (c, X.attr_num ("tint", 0)); }
	if (X.attr ("theme", v))
	{
		int t = atoi (v.str ());
		static const int MAP[12] = { 1, 0, 3, 2, 4, 5, 6, 7, 8, 9, 10, 11 };	// (0 = lt1, 1 = dk1, 2 = lt2, 3 = dk2...)
		if (t >= 0 && t < 12) return tint (g_theme[MAP[t]], X.attr_num ("tint", 0));
	}
	if (X.attr ("indexed", v)) { int i = atoi (v.str ()); if (i >= 0 && i < 64) return INDEXED[i]; return def; }
	return def;
}
static void read_theme (const unsigned char *z, unsigned zn, const char *path)
{
	int len; char *x = zip_get (z, zn, path, &len);
	if (!x) return;
	XmlReader X (x, len);
	static const char *const NAMES[12] = { "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink" };
	int cur = -1;
	while (X.next () != X_EOF)
	{
		if (X.ev == X_START)
		{
			for (int i = 0; i < 12; i++) if (X.is (NAMES[i])) cur = i;
			if (cur >= 0 && (X.is ("srgbClr") || X.is ("sysClr")))
			{
				Buf v;
				if (X.is ("srgbClr") ? X.attr ("val", v) : X.attr ("lastClr", v)) g_theme[cur] = (unsigned) strtoul (v.str (), 0, 16) & 0xFFFFFF;
				cur = -1;
			}
		}
		if (X.ev == X_END && X.is ("clrScheme")) break;
	}
	free (x);
}

// ---- number formats ----------------------------------------------------------------------------------------
static const char *builtin_fmt (int id)
{
	switch (id)
	{
	case 0: return "General"; case 1: return "0"; case 2: return "0.00"; case 3: return "#,##0"; case 4: return "#,##0.00";
	case 5: return "$#,##0_);($#,##0)"; case 6: return "$#,##0_);[Red]($#,##0)"; case 7: return "$#,##0.00_);($#,##0.00)"; case 8: return "$#,##0.00_);[Red]($#,##0.00)";
	case 9: return "0%"; case 10: return "0.00%"; case 11: return "0.00E+00"; case 12: return "# ?/?"; case 13: return "# ?\?/?\?";
	case 14: return "dd/mm/yyyy"; case 15: return "d-mmm-yy"; case 16: return "d-mmm"; case 17: return "mmm-yy"; case 18: return "h:mm AM/PM";
	case 19: return "h:mm:ss AM/PM"; case 20: return "h:mm"; case 21: return "h:mm:ss"; case 22: return "dd/mm/yyyy h:mm";
	case 37: return "#,##0 ;(#,##0)"; case 38: return "#,##0 ;[Red](#,##0)"; case 39: return "#,##0.00;(#,##0.00)"; case 40: return "#,##0.00;[Red](#,##0.00)";
	case 45: return "mm:ss"; case 46: return "[h]:mm:ss"; case 47: return "mmss.0"; case 48: return "##0.0E+0"; case 49: return "@";
	}
	if ((id >= 27 && id <= 36) || (id >= 50 && id <= 58)) return "dd/mm/yyyy";
	return "General";
}
static int builtin_fmt_id (const char *code)
{
	static const int IDS[] = { 0, 1, 2, 3, 4, 9, 10, 11, 12, 13, 18, 19, 20, 21, 45, 46, 47, 48, 49 };
	for (unsigned i = 0; i < sizeof IDS / sizeof IDS[0]; i++) if (!strcmp (builtin_fmt (IDS[i]), code)) return IDS[i];
	return -1;
}

// "_x000D_" (Excel's way to write a control character) decoded.
static void unescape_x (Buf &b)
{
	if (!b.n || !strstr (b.str (), "_x")) return;
	Buf o;
	for (int i = 0; i < b.n; i++)
	{
		if (b.b[i] == '_' && i + 6 < b.n && b.b[i + 1] == 'x' && b.b[i + 6] == '_')
		{
			char h[5]; memcpy (h, b.b + i + 2, 4); h[4] = 0;
			char *end; unsigned v = (unsigned) strtoul (h, &end, 16);
			if (!*end) { o.putu (v); i += 6; continue; }
		}
		o.put (b.b[i]);
	}
	b.clear (); b.putn (o.b ? o.b : "", o.n);
}

// ---- reading ----------------------------------------------------------------------------------------------
struct XFont { char name[48]; int size; bool b, i, s; int u; unsigned color; };
struct XBorder { unsigned char st[4]; unsigned col[4]; };
static int border_style (const char *s)
{
	if (!strcmp (s, "thin")) return BS_THIN;
	if (!strcmp (s, "medium")) return BS_MEDIUM;
	if (!strcmp (s, "thick")) return BS_THICK;
	if (!strcmp (s, "dashed") || !strcmp (s, "mediumDashed") || !strcmp (s, "dashDot") || !strcmp (s, "mediumDashDot") || !strcmp (s, "dashDotDot") || !strcmp (s, "slantDashDot")) return BS_DASHED;
	if (!strcmp (s, "dotted")) return BS_DOTTED;
	if (!strcmp (s, "double")) return BS_DOUBLE;
	if (!strcmp (s, "hair")) return BS_HAIR;
	return BS_NONE;
}
static const char *border_name (int s)
{
	switch (s) { case BS_THIN: return "thin"; case BS_MEDIUM: return "medium"; case BS_THICK: return "thick"; case BS_DASHED: return "dashed"; case BS_DOTTED: return "dotted"; case BS_DOUBLE: return "double"; case BS_HAIR: return "hair"; }
	return 0;
}

// The styles: xf index -> the book's style index (*map, malloc'd; *nmap entries).
// The differential formats (styles.xml's dxfs): the looks the conditional formats give.
struct XDxf { unsigned fill, color; signed char bold, italic; };
static XDxf *g_dxf; static int g_ndxf;
static void read_styles (Book &b, const unsigned char *z, unsigned zn, const char *path, unsigned short **map, int *nmap)
{
	*map = 0; *nmap = 0;
	free (g_dxf); g_dxf = 0; g_ndxf = 0;
	int len; char *x = zip_get (z, zn, path, &len);
	if (!x) return;
	XmlReader X (x, len);
	struct NF { int id; char *code; } *nf = 0; int nnf = 0;
	XFont *fonts = 0; int nfont = 0;
	unsigned *fills = 0; int nfill = 0;
	XBorder *borders = 0; int nborder = 0;
	int where = 0;						// 1 fonts, 2 fills, 3 borders, 4 cellXfs, 5 cellStyleXfs
	XFont *cf = 0; int side = -1; XBorder *cb = 0;
	unsigned short *m = 0; int nm = 0;
	Style *xf = 0; bool inXf = false;
	bool dxfFont = false;
	Style cur;
	while (X.next () != X_EOF)
	{
		if (X.ev == X_END)
		{
			if (X.is ("fonts") || X.is ("fills") || X.is ("borders") || X.is ("cellXfs") || X.is ("cellStyleXfs") || X.is ("dxfs")) where = 0;
			if (X.is ("font")) dxfFont = false;
			if (X.is ("left") || X.is ("right") || X.is ("top") || X.is ("bottom")) side = -1;
			if (X.is ("xf") && inXf)
			{
				inXf = false;
				if (where == 4) { m = (unsigned short *) realloc (m, (nm + 1) * sizeof (unsigned short)); m[nm++] = (unsigned short) b.styles.intern (cur); if (nm == 1) xf = &cur; }
			}
			continue;
		}
		if (X.ev != X_START) continue;
		if (X.is ("numFmt"))
		{
			Buf code; X.attr ("formatCode", code);
			nf = (NF *) realloc (nf, (nnf + 1) * sizeof (NF)); nf[nnf].id = X.attr_int ("numFmtId", 0); nf[nnf].code = sdup (code.str ()); nnf++;
		}
		else if (X.is ("fonts")) where = 1;
		else if (X.is ("fills")) where = 2;
		else if (X.is ("borders")) where = 3;
		else if (X.is ("cellXfs")) where = 4;
		else if (X.is ("cellStyleXfs")) where = 5;
		else if (X.is ("dxfs")) where = 6;
		else if (where == 6 && X.is ("dxf")) { g_dxf = (XDxf *) realloc (g_dxf, (g_ndxf + 1) * sizeof (XDxf)); XDxf &d = g_dxf[g_ndxf++]; d.fill = d.color = AUTO; d.bold = d.italic = -1; dxfFont = false; }
		else if (where == 6 && X.is ("font")) dxfFont = !X.empty;
		else if (where == 6 && g_ndxf)
		{
			XDxf &d = g_dxf[g_ndxf - 1];
			if (X.is ("b")) d.bold = X.attr_bool ("val", true);
			else if (X.is ("i")) d.italic = X.attr_bool ("val", true);
			else if (X.is ("color") && dxfFont) d.color = xml_color (X, AUTO);
			else if (X.is ("bgColor")) d.fill = xml_color (X, AUTO);		// (a dxf's solid fill: its bgColor)
			else if (X.is ("fgColor") && d.fill == AUTO) d.fill = xml_color (X, AUTO);
		}
		else if (where == 1 && X.is ("font"))
		{
			fonts = (XFont *) realloc (fonts, (nfont + 1) * sizeof (XFont));
			cf = &fonts[nfont++]; memset (cf, 0, sizeof *cf); scpy (cf->name, "Calibri", sizeof cf->name); cf->size = 110; cf->color = AUTO;
		}
		else if (where == 1 && cf)
		{
			if (X.is ("b")) cf->b = X.attr_bool ("val", true);
			else if (X.is ("i")) cf->i = X.attr_bool ("val", true);
			else if (X.is ("strike")) cf->s = X.attr_bool ("val", true);
			else if (X.is ("u")) { Buf v; cf->u = X.attr ("val", v) ? (!strcmp (v.str (), "double") || !strcmp (v.str (), "doubleAccounting") ? 2 : !strcmp (v.str (), "none") ? 0 : 1) : 1; }
			else if (X.is ("sz")) cf->size = (int) (X.attr_num ("val", 11) * 10 + 0.5);
			else if (X.is ("name") || X.is ("rFont")) { Buf v; if (X.attr ("val", v)) scpy (cf->name, v.str (), sizeof cf->name); }
			else if (X.is ("color")) { unsigned c = xml_color (X, AUTO); cf->color = c == 0 && !X.attr_bool ("auto", false) ? 0 : c; if (X.attr_bool ("auto", false)) cf->color = AUTO; }
		}
		else if (where == 2 && X.is ("fill")) { fills = (unsigned *) realloc (fills, (nfill + 1) * sizeof (unsigned)); fills[nfill++] = AUTO; }
		else if (where == 2 && X.is ("patternFill") && nfill) { Buf t; if (X.attr ("patternType", t) && strcmp (t.str (), "none") && strcmp (t.str (), "gray125")) fills[nfill - 1] = 0xFE000000u; }
		else if (where == 2 && X.is ("fgColor") && nfill && fills[nfill - 1] == 0xFE000000u) fills[nfill - 1] = xml_color (X, 0xFFFFFF);
		else if (where == 2 && X.is ("bgColor") && nfill && fills[nfill - 1] == 0xFE000000u) fills[nfill - 1] = xml_color (X, 0xFFFFFF);
		else if (where == 3 && X.is ("border")) { borders = (XBorder *) realloc (borders, (nborder + 1) * sizeof (XBorder)); cb = &borders[nborder++]; memset (cb, 0, sizeof *cb); }
		else if (where == 3 && cb && (X.is ("left") || X.is ("right") || X.is ("top") || X.is ("bottom") || X.is ("start") || X.is ("end")))
		{
			side = X.is ("left") || X.is ("start") ? B_LEFT : X.is ("right") || X.is ("end") ? B_RIGHT : X.is ("top") ? B_TOP : B_BOTTOM;
			Buf st; cb->st[side] = (unsigned char) (X.attr ("style", st) ? border_style (st.str ()) : BS_NONE); cb->col[side] = 0;
			if (X.empty) side = -1;
		}
		else if (where == 3 && cb && side >= 0 && X.is ("color")) cb->col[side] = xml_color (X, 0);
		else if ((where == 4 || where == 5) && X.is ("xf"))
		{
			cur.reset ();
			int fi = X.attr_int ("fontId", 0), fl = X.attr_int ("fillId", 0), bi = X.attr_int ("borderId", 0), ni = X.attr_int ("numFmtId", 0);
			if (fi >= 0 && fi < nfont)
			{
				XFont &f = fonts[fi];
				cur.font = (unsigned short) book_font (b, f.name); cur.size = (unsigned short) f.size;
				cur.bold = f.b; cur.italic = f.i; cur.strike = f.s; cur.under = (unsigned char) f.u; cur.color = f.color;
			}
			if (fl >= 0 && fl < nfill) cur.fill = fills[fl] == 0xFE000000u ? AUTO : fills[fl];
			if (bi >= 0 && bi < nborder) for (int k = 0; k < 4; k++) { cur.bs[k] = borders[bi].st[k]; cur.bc[k] = borders[bi].col[k]; }
			const char *code = 0;
			for (int k = 0; k < nnf; k++) if (nf[k].id == ni) code = nf[k].code;
			if (!code) code = builtin_fmt (ni);
			cur.fmt = (unsigned short) book_fmt (b, code);
			inXf = true;
			if (X.empty) { X.next (); inXf = false; if (where == 4) { m = (unsigned short *) realloc (m, (nm + 1) * sizeof (unsigned short)); m[nm++] = (unsigned short) b.styles.intern (cur); } }
		}
		else if (inXf && X.is ("alignment"))
		{
			Buf v;
			if (X.attr ("horizontal", v))
			{
				const char *h = v.str ();
				cur.ha = !strcmp (h, "left") ? HA_LEFT : !strcmp (h, "center") || !strcmp (h, "centerContinuous") ? HA_CENTER : !strcmp (h, "right") ? HA_RIGHT :
					 !strcmp (h, "fill") ? HA_FILL : !strcmp (h, "justify") || !strcmp (h, "distributed") ? HA_JUSTIFY : HA_GENERAL;
			}
			cur.va = VA_BOTTOM;
			if (X.attr ("vertical", v)) cur.va = !strcmp (v.str (), "top") ? VA_TOP : !strcmp (v.str (), "center") ? VA_CENTER : VA_BOTTOM;
			cur.wrap = X.attr_bool ("wrapText", false);
			cur.indent = (unsigned char) iclamp (X.attr_int ("indent", 0), 0, 15);
		}
	}
	(void) xf;
	// the file's default style (xf 0) becomes the book's own default
	if (nm > 0 && m[0] != 0)
	{
		Style d = b.styles.s[m[0]];
		Style old = b.styles.s[0];
		b.styles.s[0] = d; b.styles.s[m[0]] = old;
		b.styles.rehash ();
		for (int k = 0; k < nm; k++) { if (m[k] == 0) m[k] = m[0]; else if (m[k] == m[0]) m[k] = 0; }
		m[0] = 0;
	}
	for (int k = 0; k < nnf; k++) free (nf[k].code);
	free (nf); free (fonts); free (fills); free (borders); free (x);
	*map = m; *nmap = nm;
}

static char **read_shared_strings (const unsigned char *z, unsigned zn, const char *path, int *count)
{
	*count = 0;
	int len; char *x = zip_get (z, zn, path, &len);
	if (!x) return 0;
	XmlReader X (x, len);
	char **ss = 0; int n = 0, cap = 0;
	Buf cur; bool inSi = false, inT = false; int skipDepth = 0;
	while (X.next () != X_EOF)
	{
		if (X.ev == X_START)
		{
			if (X.is ("si")) { inSi = true; cur.clear (); }
			else if (X.is ("rPh") || X.is ("phoneticPr")) { if (!X.empty) skipDepth++; }
			else if (X.is ("t") && inSi && !skipDepth) inT = !X.empty;
		}
		else if (X.ev == X_END)
		{
			if (X.is ("t")) inT = false;
			else if (X.is ("rPh") || X.is ("phoneticPr")) { if (skipDepth) skipDepth--; }
			else if (X.is ("si"))
			{
				unescape_x (cur);
				if (n == cap) { cap = cap ? cap * 2 : 256; ss = (char **) realloc (ss, cap * sizeof (char *)); }
				ss[n++] = sdup (cur.str (), cur.n);
				inSi = false;
			}
		}
		else if (X.ev == X_TEXT && inT && !skipDepth) cur.putn (X.text.b ? X.text.b : "", X.text.n);
	}
	free (x);
	*count = n;
	return ss;
}

// A formula written in a file (no "="): the book's formula, 0 when it cannot be read.
static Formula *file_formula (Book &b, const char *text)
{
	return formula_parse (b, text, -1);
}

struct Shared { int si; int r, c; Formula *f; };

static void read_chart (Book &b, Sheet *s, const unsigned char *z, unsigned zn, const char *path, int x, int y, int w, int h);

// A conditional format's rule added (the sheet's list kept in the rules' priority order).
static void cf_add (Sheet *s, const CondFmt &c, int prio, int **prios)
{
	int i = s->ncf;
	while (i > 0 && (*prios)[i - 1] > prio) i--;
	s->cf = (CondFmt *) realloc (s->cf, (s->ncf + 1) * sizeof (CondFmt));
	*prios = (int *) realloc (*prios, (s->ncf + 1) * sizeof (int));
	memmove (s->cf + i + 1, s->cf + i, (s->ncf - i) * sizeof (CondFmt));
	memmove (*prios + i + 1, *prios + i, (s->ncf - i) * sizeof (int));
	s->cf[i] = c; (*prios)[i] = prio; s->ncf++;
}
// A range list ("B5:D16 F5 H1:H9") -> up to n ranges.
static int parse_sqref (const char *p, Rect *out, int n)
{
	int k = 0;
	while (*p && k < n)
	{
		while (*p == ' ') p++;
		char t[40]; int l = 0;
		while (*p && *p != ' ' && l < 39) t[l++] = *p++;
		t[l] = 0;
		if (!l) break;
		char *colon = strchr (t, ':');
		Rect r;
		if (colon) { *colon = 0; if (!parse_cell_name (t, &r.r0, &r.c0) || !parse_cell_name (colon + 1, &r.r1, &r.c1)) continue; }
		else { if (!parse_cell_name (t, &r.r0, &r.c0)) continue; r.r1 = r.r0; r.c1 = r.c0; }
		if (r.r1 < r.r0) { int q = r.r0; r.r0 = r.r1; r.r1 = q; }
		if (r.c1 < r.c0) { int q = r.c0; r.c0 = r.c1; r.c1 = q; }
		out[k++] = r;
	}
	return k;
}
static void read_sheet (Book &b, Sheet *s, const unsigned char *z, unsigned zn, const char *path, char **sst, int nsst,
			unsigned short *xmap, int nxmap)
{
	int len; char *x = zip_get (z, zn, path, &len);
	if (!x) return;
	XmlReader X (x, len);
	int row = -1, col = -1;
	Cell *cell = 0; int ctype = 0;			// 0 n, 1 s, 2 str, 3 inlineStr, 4 b, 5 e, 6 d
	Buf val, ftext, tmp; bool inV = false, inF = false, inIsT = false, hasV = false;
	Buf fType, fRef; int fSi = -1;
	Shared *sh = 0; int nsh = 0;
	char drawingId[32] = "";
	auto xfstyle = [&] (int i) -> unsigned short { return i >= 0 && i < nxmap ? xmap[i] : 0; };
	// the conditional formats being read
	Rect cfR[8]; int ncfR = 0; CondFmt cfc; bool inRule = false, inCfF = false; int cfPrio = 0, nForm = 0, ncol = 0, ncfvo = 0;
	unsigned cfCol[3] = { 0, 0, 0 }; Buf cfText; int *prios = 0;
	while (X.next () != X_EOF)
	{
		if (X.ev == X_START)
		{
			if (X.is ("conditionalFormatting")) { Buf q; X.attr ("sqref", q); ncfR = parse_sqref (q.str (), cfR, 8); }
			else if (X.is ("cfRule") && ncfR)
			{
				memset (&cfc, 0, sizeof cfc); cfc.fill = cfc.color = AUTO; cfc.bold = cfc.italic = -1;
				Buf t, o; X.attr ("type", t); X.attr ("operator", o);
				const char *ty = t.str (), *op = o.str ();
				cfc.type = -1;
				if (!strcmp (ty, "cellIs"))
				{
					cfc.type = CF_CELL;
					cfc.op = !strcmp (op, "greaterThan") ? CO_GT : !strcmp (op, "greaterThanOrEqual") ? CO_GE : !strcmp (op, "lessThan") ? CO_LT : !strcmp (op, "lessThanOrEqual") ? CO_LE :
						 !strcmp (op, "equal") ? CO_EQ : !strcmp (op, "notEqual") ? CO_NE : !strcmp (op, "notBetween") ? CO_NOTBETWEEN : CO_BETWEEN;
				}
				else if (!strcmp (ty, "containsText") || !strcmp (ty, "notContainsText") || !strcmp (ty, "beginsWith") || !strcmp (ty, "endsWith"))
				{
					cfc.type = CF_TEXT;
					cfc.op = !strcmp (ty, "notContainsText") ? CT_NOTCONTAINS : !strcmp (ty, "beginsWith") ? CT_BEGINS : !strcmp (ty, "endsWith") ? CT_ENDS : CT_CONTAINS;
					Buf tx; X.attr ("text", tx); scpy (cfc.a, tx.str (), sizeof cfc.a);
				}
				else if (!strcmp (ty, "top10")) { cfc.type = CF_TOP; cfc.op = X.attr_bool ("bottom", false) ? 1 : 0; cfc.pct = X.attr_bool ("percent", false); snprintf (cfc.a, sizeof cfc.a, "%d", X.attr_int ("rank", 10)); }
				else if (!strcmp (ty, "aboveAverage")) { cfc.type = CF_AVERAGE; cfc.op = X.attr_bool ("aboveAverage", true) ? 0 : 1; }
				else if (!strcmp (ty, "duplicateValues") || !strcmp (ty, "uniqueValues")) { cfc.type = CF_DUP; cfc.op = !strcmp (ty, "uniqueValues") ? 1 : 0; }
				else if (!strcmp (ty, "expression")) cfc.type = CF_FORMULA;
				else if (!strcmp (ty, "colorScale")) cfc.type = CF_SCALE;
				else if (!strcmp (ty, "dataBar")) cfc.type = CF_BAR;
				int dx = X.attr_int ("dxfId", -1);
				if (dx >= 0 && dx < g_ndxf) { cfc.fill = g_dxf[dx].fill; cfc.color = g_dxf[dx].color; cfc.bold = g_dxf[dx].bold; cfc.italic = g_dxf[dx].italic; }
				cfPrio = X.attr_int ("priority", 1000);
				inRule = !X.empty; nForm = ncol = ncfvo = 0;
				if (X.empty && cfc.type >= 0) for (int k = 0; k < ncfR; k++) { cfc.r = cfR[k]; cf_add (s, cfc, cfPrio, &prios); }
			}
			else if (inRule && X.is ("formula")) { inCfF = true; cfText.clear (); }
			else if (inRule && X.is ("cfvo")) ncfvo++;
			else if (inRule && X.is ("color") && ncol < 3) cfCol[ncol++] = xml_color (X, 0);
			else if (X.is ("c"))
			{
				Buf r;
				if (X.attr ("r", r)) { int rr, cc; if (parse_cell_name (r.str (), &rr, &cc)) { row = rr; col = cc; } else col++; }
				else col++;
				Buf t; ctype = 0;
				if (X.attr ("t", t))
				{
					const char *tt = t.str ();
					ctype = !strcmp (tt, "s") ? 1 : !strcmp (tt, "str") ? 2 : !strcmp (tt, "inlineStr") ? 3 : !strcmp (tt, "b") ? 4 : !strcmp (tt, "e") ? 5 : !strcmp (tt, "d") ? 6 : 0;
				}
				int st = X.attr_int ("s", -1);
				cell = 0;
				if (row >= 0 && row < MAXR && col >= 0 && col < MAXC)
				{
					cell = s->cells.add (row, col);
					cell->style = st >= 0 ? xfstyle (st) : (unsigned short) cell_style_index (s, 0, row, col);
				}
				val.clear (); ftext.clear (); hasV = false; fType.clear (); fRef.clear (); fSi = -1;
				if (X.empty) { X.next (); goto endcell; }
			}
			else if (X.is ("v")) { inV = !X.empty; val.clear (); hasV = true; }
			else if (X.is ("f"))
			{
				inF = !X.empty; ftext.clear ();
				X.attr ("t", fType); X.attr ("ref", fRef); fSi = X.attr_int ("si", -1);
				if (X.empty && fSi >= 0 && cell)			// a shared formula's other cells
				{
					for (int i = 0; i < nsh; i++)
						if (sh[i].si == fSi && sh[i].f)
						{
							cell->kind = K_FORM; cell->f = formula_copy (sh[i].f, row - sh[i].r, col - sh[i].c);
							break;
						}
				}
			}
			else if (X.is ("t") && ctype == 3) inIsT = !X.empty;
			else if (X.is ("row"))
			{
				int r = X.attr_int ("r", row + 2) - 1;
				row = r; col = -1;
				if (r >= 0 && r < MAXR)
				{
					Buf ht; bool custom = X.attr_bool ("customHeight", false), hidden = X.attr_bool ("hidden", false), cf = X.attr_bool ("customFormat", false);
					double hpt = X.attr ("ht", ht) ? strtod (ht.str (), 0) : -1;
					int sidx = X.attr_int ("s", -1);
					if ((custom && hpt >= 0) || hidden || (cf && sidx >= 0))
					{
						RowInfo *ri = row_add (s, r);
						if (custom && hpt >= 0) { ri->fl |= RF_CUSTOM; ri->h = (unsigned short) iclamp ((int) (hpt * 4 / 3 + 0.5), 0, 2000); }
						if (hidden) ri->fl |= RF_HIDDEN;
						if (cf && sidx >= 0) ri->style = xfstyle (sidx);
					}
				}
			}
			else if (X.is ("col"))
			{
				int c0 = X.attr_int ("min", 1) - 1, c1 = X.attr_int ("max", 1) - 1;
				c0 = iclamp (c0, 0, MAXC - 1); c1 = iclamp (c1, c0, MAXC - 1);
				Buf wv; double wch = X.attr ("width", wv) ? strtod (wv.str (), 0) : -1;
				bool hidden = X.attr_bool ("hidden", false), custom = X.attr_bool ("customWidth", false) || wch >= 0;
				int sidx = X.attr_int ("style", -1);
				int px = wch >= 0 ? (int) (((256.0 * wch + 18) / 256.0) * 7) : 0;
				for (int c = c0; c <= c1; c++)
				{
					if (custom && wch >= 0) s->colW[c] = (unsigned short) iclamp (px, 1, 4000);
					if (hidden) s->colFl[c] |= RF_HIDDEN;
					if (sidx > 0) s->colSt[c] = xfstyle (sidx);
				}
				cols_changed (s);
			}
			else if (X.is ("sheetFormatPr"))
			{
				double dh = X.attr_num ("defaultRowHeight", 15);
				s->defRowH = iclamp ((int) (dh * 4 / 3 + 0.5), 8, 400);
				Buf dw;
				if (X.attr ("defaultColWidth", dw)) s->defColW = iclamp ((int) (((256.0 * strtod (dw.str (), 0) + 18) / 256.0) * 7), 8, 1000);
				else { double bw = X.attr_num ("baseColWidth", 8); s->defColW = iclamp ((int) (bw * 7 + 5 + 0.5) + 2, 8, 1000); }
				rows_changed (s); cols_changed (s);
			}
			else if (X.is ("mergeCell"))
			{
				Buf ref; X.attr ("ref", ref);
				char *rs = (char *) ref.str ();
				char *colon = strchr (rs, ':');
				if (colon)
				{
					*colon = 0; Rect m;
					if (parse_cell_name (rs, &m.r0, &m.c0) && parse_cell_name (colon + 1, &m.r1, &m.c1)) merge_add (s, m);
				}
			}
			else if (X.is ("pane"))
			{
				Buf st;
				if (X.attr ("state", st) && (!strcmp (st.str (), "frozen") || !strcmp (st.str (), "frozenSplit")))
				{ s->freezeC = (int) X.attr_num ("xSplit", 0); s->freezeR = (int) X.attr_num ("ySplit", 0); }
			}
			else if (X.is ("sheetView")) s->grid = X.attr_bool ("showGridLines", true);
			else if (X.is ("tabColor")) s->tab = xml_color (X, AUTO);
			else if (X.is ("drawing")) { Buf id; if (X.attr ("r:id", id) || X.attr ("id", id)) scpy (drawingId, id.str (), sizeof drawingId); }
		}
		else if (X.ev == X_TEXT)
		{
			if (inCfF) cfText.putn (X.text.b ? X.text.b : "", X.text.n);
			else if (inV) val.putn (X.text.b ? X.text.b : "", X.text.n);
			else if (inF) ftext.putn (X.text.b ? X.text.b : "", X.text.n);
			else if (inIsT) val.putn (X.text.b ? X.text.b : "", X.text.n);
		}
		else if (X.ev == X_END)
		{
			if (inCfF && X.is ("formula"))
			{
				inCfF = false;
				char *dst = nForm == 0 ? cfc.a : nForm == 1 ? cfc.b : 0;
				nForm++;
				const char *t = cfText.str ();
				double d;
				if (dst && cfc.type == CF_CELL && (input_number (t, cfText.n, &d) || (t[0] == '"' && cfText.n >= 2))) scpy (dst, t, sizeof cfc.a);
				else if (dst && (cfc.type == CF_CELL || cfc.type == CF_FORMULA)) { dst[0] = '='; scpy (dst + 1, t, sizeof cfc.a - 1); }
				continue;
			}
			if (inRule && X.is ("cfRule"))
			{
				inRule = false;
				if (cfc.type == CF_SCALE) { cfc.op = ncfvo >= 3 ? 3 : 2; cfc.c0 = cfCol[0]; if (cfc.op == 3) { cfc.c1 = cfCol[1]; cfc.c2 = cfCol[2]; } else cfc.c2 = cfCol[1]; }
				if (cfc.type == CF_BAR) cfc.c0 = ncol ? cfCol[0] : 0x638EC6;
				if (cfc.type >= 0) for (int k = 0; k < ncfR; k++) { cfc.r = cfR[k]; cf_add (s, cfc, cfPrio, &prios); }
				continue;
			}
			if (X.is ("conditionalFormatting")) { ncfR = 0; continue; }
			if (X.is ("v")) inV = false;
			else if (X.is ("t")) inIsT = false;
			else if (X.is ("f"))
			{
				inF = false;
				if (cell && ftext.n)
				{
					Formula *f = file_formula (b, ftext.str ());
					if (f)
					{
						cell->kind = K_FORM; cell->f = f;
						if (!strcmp (fType.str (), "shared") && fSi >= 0)
						{
							sh = (Shared *) realloc (sh, (nsh + 1) * sizeof (Shared));
							sh[nsh].si = fSi; sh[nsh].r = row; sh[nsh].c = col; sh[nsh].f = formula_dup (f); nsh++;
						}
					}
				}
			}
			else if (X.is ("c"))
			{
endcell:
				if (!cell) continue;
				// the value (a formula's: its last result, shown until the book is computed)
				unsigned char vt = V_EMPTY; double num = 0; char *str = 0; unsigned char err = 0;
				if (ctype == 3 || (hasV && val.n > 0) || (ctype == 3))
				{
					switch (ctype)
					{
					case 1: { int k = atoi (val.str ()); if (k >= 0 && k < nsst) { vt = V_STR; str = sdup (sst[k]); } break; }
					case 2: case 3: { unescape_x (val); vt = V_STR; str = sdup (val.str (), val.n); break; }
					case 4: vt = V_BOOL; num = atoi (val.str ()) ? 1 : 0; break;
					case 5: { vt = V_ERR; err = E_VALUE; for (int k = 1; k <= 7; k++) if (!strcmp (val.str (), ERR_NAMES[k])) err = (unsigned char) k; break; }
					case 6: { double v; if (parse_datetime (val.str (), val.n, &v, 0, 0)) { vt = V_NUM; num = v; } break; }
					default: vt = V_NUM; num = strtod (val.str (), 0); break;
					}
				}
				if (cell->kind == K_FORM)
				{
					free (cell->str); cell->str = str; cell->vt = vt == V_EMPTY ? (unsigned char) V_NUM : vt; cell->num = num; cell->err = err;
				}
				else if (vt != V_EMPTY)
				{
					cell->kind = vt == V_NUM ? K_NUM : vt == V_STR ? K_STR : vt == V_BOOL ? K_BOOL : K_ERR;
					cell->vt = vt; cell->num = num; cell->str = str; cell->err = err;
				}
				else if (!cell->style) { s->cells.del (row, col); }
				cell = 0;
			}
		}
	}
	for (int i = 0; i < nsh; i++) formula_free (sh[i].f);
	free (sh); free (prios);
	free (x);
	sheet_touched (s);
	// its charts: the drawing's anchors, each chart's part
	if (drawingId[0])
	{
		Rel rels[64]; int nr = read_rels (z, zn, path, rels, 64);
		const char *dp = rel_target (rels, nr, drawingId);
		int dl; char *d = dp ? zip_get (z, zn, dp, &dl) : 0;
		if (d)
		{
			Rel drels[64]; int ndr = read_rels (z, zn, dp, drels, 64);
			XmlReader D (d, dl);
			int fc = 0, fr = 0, tc = 0, tr = 0; long long fco = 0, fro = 0, tco = 0, tro = 0;
			int which = 0, part = 0;		// which: 1 from, 2 to; part: 1 col, 2 colOff, 3 row, 4 rowOff
			while (D.next () != X_EOF)
			{
				if (D.ev == X_START)
				{
					if (D.is ("from")) which = 1; else if (D.is ("to")) which = 2;
					else if (D.is ("col")) part = 1; else if (D.is ("colOff")) part = 2; else if (D.is ("row")) part = 3; else if (D.is ("rowOff")) part = 4;
					else if (D.is ("chart"))
					{
						Buf id; if (!D.attr ("r:id", id)) D.attr ("id", id);
						const char *cp = rel_target (drels, ndr, id.str ());
						if (cp)
						{
							int x0 = col_x (s, fc) + (int) (fco / 9525), y0 = (int) row_y (s, fr) + (int) (fro / 9525);
							int x1 = col_x (s, tc) + (int) (tco / 9525), y1 = (int) row_y (s, tr) + (int) (tro / 9525);
							read_chart (b, s, z, zn, cp, x0, y0, imax (60, x1 - x0), imax (40, y1 - y0));
						}
					}
				}
				else if (D.ev == X_END) { if (D.is ("from") || D.is ("to")) which = 0; part = 0; }
				else if (D.ev == X_TEXT && which && part)
				{
					long long v = atoll (D.text.str ());
					if (which == 1) { if (part == 1) fc = (int) v; else if (part == 2) fco = v; else if (part == 3) fr = (int) v; else fro = v; }
					else { if (part == 1) tc = (int) v; else if (part == 2) tco = v; else if (part == 3) tr = (int) v; else tro = v; }
				}
			}
			free (d);
		}
	}
}

// A chart's part: its type, title, legend, grid, and the ranges of its series (put back into one range).
static bool parse_range_ref (Book &b, const char *f, int *sheetId, Rect *r)
{
	Formula *fm = formula_parse (b, f, -1);
	if (!fm) return false;
	bool ok = false;
	for (int i = 0; i < fm->nt; i++)
	{
		const Tok &k = fm->tok[i];
		if ((k.t == TK_REF || k.t == TK_AREA) && !(k.fl & TF_BAD) && k.sheet > 0)
		{
			*sheetId = k.sheet; r->r0 = k.r0; r->c0 = k.c0; r->r1 = k.r1; r->c1 = k.c1; ok = true; break;
		}
	}
	formula_free (fm);
	return ok;
}
static void read_chart (Book &b, Sheet *s, const unsigned char *z, unsigned zn, const char *path, int x, int y, int w, int h)
{
	int len; char *d = zip_get (z, zn, path, &len);
	if (!d) return;
	XmlReader X (d, len);
	Chart c; memset (&c, 0, sizeof c);
	c.type = CH_COLUMN; c.x = x; c.y = y; c.w = w; c.h = h; c.legend = LG_NONE; c.grid = false;
	bool any = false, first = true, inTitle = false, inTx = false, inCat = false, inVal = false, inF = false, anyTx = false, anyCat = false;
	bool valVertical = true, byTitleText = false;
	Rect box = { 0, 0, 0, 0 }; int srcSheet = 0;
	Buf f;
	while (X.next () != X_EOF)
	{
		if (X.ev == X_START)
		{
			if (X.is ("barChart") || X.is ("bar3DChart")) c.type = CH_COLUMN;
			else if (X.is ("barDir")) { Buf v; X.attr ("val", v); c.type = !strcmp (v.str (), "bar") ? CH_BAR : CH_COLUMN; }
			else if (X.is ("grouping")) { Buf v; X.attr ("val", v); c.stacked = !strcmp (v.str (), "stacked") || !strcmp (v.str (), "percentStacked"); }
			else if (X.is ("lineChart") || X.is ("line3DChart")) c.type = CH_LINE;
			else if (X.is ("areaChart") || X.is ("area3DChart")) c.type = CH_AREA;
			else if (X.is ("pieChart") || X.is ("pie3DChart") || X.is ("doughnutChart")) c.type = CH_PIE;
			else if (X.is ("scatterChart")) c.type = CH_SCATTER;
			else if (X.is ("title") && !any) inTitle = true;
			else if (X.is ("legendPos")) { Buf v; X.attr ("val", v); c.legend = !strcmp (v.str (), "b") ? LG_BOTTOM : !strcmp (v.str (), "t") ? LG_TOP : LG_RIGHT; }
			else if (X.is ("legend")) { if (c.legend == LG_NONE) c.legend = LG_RIGHT; }
			else if (X.is ("majorGridlines")) c.grid = true;
			else if (X.is ("ser")) any = true;
			else if (X.is ("tx") && any) inTx = true;
			else if (X.is ("cat") || X.is ("xVal")) inCat = true;
			else if (X.is ("val") || X.is ("yVal")) inVal = true;
			else if (X.is ("f")) { inF = true; f.clear (); }
			else if (X.is ("autoTitleDeleted") && X.attr_bool ("val", false)) {}
		}
		else if (X.ev == X_TEXT)
		{
			if (inF) f.putn (X.text.b ? X.text.b : "", X.text.n);
			else if (inTitle && !any)
			{
				int l = (int) strlen (c.title);
				scpy (c.title + l, X.text.str (), (int) sizeof c.title - l);
				byTitleText = true;
			}
		}
		else if (X.ev == X_END)
		{
			if (X.is ("title")) inTitle = false;
			else if (X.is ("tx")) inTx = false;
			else if (X.is ("cat") || X.is ("xVal")) inCat = false;
			else if (X.is ("val") || X.is ("yVal")) inVal = false;
			else if (X.is ("f"))
			{
				inF = false;
				int sid; Rect r;
				if (parse_range_ref (b, f.str (), &sid, &r))
				{
					if (first) { box = r; srcSheet = sid; first = false; }
					else if (sid == srcSheet)
					{ box.r0 = imin (box.r0, r.r0); box.c0 = imin (box.c0, r.c0); box.r1 = imax (box.r1, r.r1); box.c1 = imax (box.c1, r.c1); }
					if (inTx) anyTx = true;
					if (inCat) anyCat = true;
					if (inVal) valVertical = r.c0 == r.c1 && r.r1 > r.r0 ? true : r.r0 == r.r1 && r.c1 > r.c0 ? false : valVertical;
				}
			}
		}
	}
	free (d);
	(void) byTitleText;
	if (first) return;					// (no data: left out)
	c.srcSheet = srcSheet; c.src = box;
	c.byRows = !valVertical; c.head = anyTx; c.side = anyCat;
	if (c.byRows) { bool t = c.head; c.head = c.side; c.side = t; }	// (head: the first row, side: the first column)
	s->charts = (Chart **) realloc (s->charts, (s->ncharts + 1) * sizeof (Chart *));
	Chart *nc = (Chart *) malloc (sizeof (Chart)); *nc = c;
	chart_anchor (s, nc);
	s->charts[s->ncharts++] = nc;
}

// The whole workbook from its bytes; false (and why) when they are not one.
static bool xlsx_read (Book &b, const char *data, int n, const char **why)
{
	const unsigned char *z = (const unsigned char *) data;
	Rel rels[32]; int nr = read_rels (z, (unsigned) n, "", rels, 32);
	const char *wbp = 0;
	for (int i = 0; i < nr; i++) if (!strcmp (rels[i].type, "officeDocument")) wbp = rels[i].target;
	char wbpath[160]; scpy (wbpath, wbp ? wbp : "xl/workbook.xml", sizeof wbpath);
	int wl; char *wb = zip_get (z, (unsigned) n, wbpath, &wl);
	if (!wb) { if (why) *why = "This file is not an Excel workbook (.xlsx)."; return false; }
	Rel wr[300]; int nwr = read_rels (z, (unsigned) n, wbpath, wr, 300);
	for (int i = 0; i < nwr; i++) if (!strcmp (wr[i].type, "theme")) read_theme (z, (unsigned) n, wr[i].target);
	unsigned short *xmap = 0; int nxmap = 0;
	for (int i = 0; i < nwr; i++) if (!strcmp (wr[i].type, "styles")) read_styles (b, z, (unsigned) n, wr[i].target, &xmap, &nxmap);
	int nsst = 0; char **sst = 0;
	for (int i = 0; i < nwr; i++) if (!strcmp (wr[i].type, "sharedStrings")) sst = read_shared_strings (z, (unsigned) n, wr[i].target, &nsst);
	// the sheets: named first (formulas name them), then read
	struct SheetRef { Sheet *s; char path[160]; } refs[MAXSHEETS]; int nref = 0;
	XmlReader X (wb, wl);
	int active = 0;
	while (X.next () != X_EOF)
	{
		if (X.ev != X_START) continue;
		if (X.is ("workbookPr")) b.dateSystem1904 = X.attr_bool ("date1904", false);
		else if (X.is ("workbookView")) active = X.attr_int ("activeTab", 0);
		else if (X.is ("sheet") && nref < MAXSHEETS)
		{
			Buf nm, id; X.attr ("name", nm);
			if (!X.attr ("r:id", id)) X.attr ("id", id);
			const char *p = rel_target (wr, nwr, id.str ());
			if (!p) continue;
			Sheet *s = book_add_sheet (b, nm.str ());
			if (!s) break;
			if (X.attr_bool ("hidden", false)) {}
			refs[nref].s = s; scpy (refs[nref].path, p, sizeof refs[nref].path); nref++;
		}
		else if (X.is ("definedName"))				// a name ("_xlnm.Print_Area" and the like: Excel's own, left)
		{
			Buf nm, txt; X.attr ("name", nm);
			int local = X.attr_int ("localSheetId", -1);
			if (!X.empty) while (X.next () != X_EOF && X.ev != X_END) if (X.ev == X_TEXT) txt.putn (X.text.b ? X.text.b : "", X.text.n);
			if (!nm.n || !strncmp (nm.str (), "_xlnm.", 6) || !txt.n) continue;
			int scope = local >= 0 && local < nref ? refs[local].s->id : 0;
			name_set (b, nm.str (), scope, txt.str ());
		}
	}
	free (wb);
	for (int i = 0; i < nref; i++) read_sheet (b, refs[i].s, z, (unsigned) n, refs[i].path, sst, nsst, xmap, nxmap);
	for (int i = 0; i < nsst; i++) free (sst[i]);
	free (sst); free (xmap);
	if (b.ns == 0) { if (why) *why = "The workbook holds no sheet."; return false; }
	b.active = iclamp (active, 0, b.ns - 1);
	if (b.dateSystem1904)					// (the dates moved to the 1900 system: 1462 days)
		for (int i = 0; i < b.ns; i++)
			for (int k = 0; k < b.sh[i]->cells.cap; k++)
			{
				Cell *x = b.sh[i]->cells.t[k];
				if (x && x->kind == K_NUM) { int fk = fmt_kind (book_fmt_code (b, b.styles.s[x->style].fmt)); if (fk == FK_DATE || fk == FK_DATETIME) x->num += 1462; }
			}
	b.dateSystem1904 = false;
	return true;
}

// ---- writing ---------------------------------------------------------------------------------------------
static void hexcol (Buf &o, unsigned c) { char t[16]; snprintf (t, sizeof t, "FF%06X", c & 0xFFFFFF); o.puts (t); }

struct SStr { char **str; int n, cap; int *h; int hcap; };
static int sst_add (SStr &t, const char *s)
{
	if (!t.hcap || t.n * 2 >= t.hcap)
	{
		free (t.h); t.hcap = t.hcap ? t.hcap * 2 : 1024; t.h = (int *) calloc (t.hcap, sizeof (int));
		for (int i = 0; i < t.n; i++)
		{
			unsigned hh = 2166136261u; for (const char *p = t.str[i]; *p; p++) { hh ^= (unsigned char) *p; hh *= 16777619u; }
			unsigned k = hh & (t.hcap - 1); while (t.h[k]) k = (k + 1) & (t.hcap - 1); t.h[k] = i + 1;
		}
	}
	unsigned hh = 2166136261u; for (const char *p = s; *p; p++) { hh ^= (unsigned char) *p; hh *= 16777619u; }
	unsigned k = hh & (t.hcap - 1);
	while (t.h[k]) { if (!strcmp (t.str[t.h[k] - 1], s)) return t.h[k] - 1; k = (k + 1) & (t.hcap - 1); }
	if (t.n == t.cap) { t.cap = t.cap ? t.cap * 2 : 256; t.str = (char **) realloc (t.str, t.cap * sizeof (char *)); }
	t.str[t.n] = sdup (s);
	t.h[k] = t.n + 1;
	return t.n++;
}

static void write_chart_xml (Book &b, Chart *c, Buf &o);
// ---- conditional formats written --------------------------------------------------------------------------
static bool cf_has_look (const CondFmt &c) { return c.type != CF_SCALE && c.type != CF_BAR; }
static void put_dxf (Buf &o, const CondFmt &c)
{
	o.puts ("<dxf>");
	if (c.bold >= 0 || c.italic >= 0 || c.color != AUTO)
	{
		o.puts ("<font>");
		if (c.bold >= 0) { o.puts ("<b val=\""); o.puti (c.bold ? 1 : 0); o.puts ("\"/>"); }
		if (c.italic >= 0) { o.puts ("<i val=\""); o.puti (c.italic ? 1 : 0); o.puts ("\"/>"); }
		if (c.color != AUTO) { o.puts ("<color rgb=\""); hexcol (o, c.color); o.puts ("\"/>"); }
		o.puts ("</font>");
	}
	if (c.fill != AUTO) { o.puts ("<fill><patternFill patternType=\"solid\"><fgColor rgb=\""); hexcol (o, c.fill); o.puts ("\"/><bgColor rgb=\""); hexcol (o, c.fill); o.puts ("\"/></patternFill></fill>"); }
	o.puts ("</dxf>");
}
// An operand as a cfRule's formula: "=B5*2" -> B5*2; 100 -> 100; a text -> "text".
static void put_cf_formula (Buf &o, const char *a)
{
	while (*a == ' ') a++;
	o.puts ("<formula>");
	double d;
	if (*a == '=') xml_esc (o, a + 1);
	else if (input_number (a, (int) strlen (a), &d) || (a[0] == '"' && strlen (a) >= 2)) xml_esc (o, a);
	else { Buf q; q.put ('"'); for (const char *p = a; *p; p++) { if (*p == '"') q.put ('"'); q.put (*p); } q.put ('"'); xml_esc (o, q.str ()); }
	o.puts ("</formula>");
}
static void put_cf (Buf &o, const CondFmt &c, int dxf, int prio)
{
	char a1[24], a2[24]; cell_name (c.r.r0, c.r.c0, a1); cell_name (c.r.r1, c.r.c1, a2);
	o.puts ("<conditionalFormatting sqref=\""); o.puts (a1); if (c.r.r1 != c.r.r0 || c.r.c1 != c.r.c0) { o.put (':'); o.puts (a2); } o.puts ("\">");
	o.puts ("<cfRule type=\"");
	static const char *const OPS[8] = { "greaterThan", "greaterThanOrEqual", "lessThan", "lessThanOrEqual", "equal", "notEqual", "between", "notBetween" };
	static const char *const TXT[4] = { "containsText", "notContainsText", "beginsWith", "endsWith" };
	static const char *const TOPS[4] = { "containsText", "notContains", "beginsWith", "endsWith" };
	switch (c.type)
	{
	case CF_CELL: o.puts ("cellIs"); break;
	case CF_TEXT: o.puts (TXT[iclamp (c.op, 0, 3)]); break;
	case CF_TOP: o.puts ("top10"); break;
	case CF_AVERAGE: o.puts ("aboveAverage"); break;
	case CF_DUP: o.puts (c.op ? "uniqueValues" : "duplicateValues"); break;
	case CF_FORMULA: o.puts ("expression"); break;
	case CF_SCALE: o.puts ("colorScale"); break;
	case CF_BAR: o.puts ("dataBar"); break;
	}
	o.put ('"');
	if (dxf >= 0) { o.puts (" dxfId=\""); o.puti (dxf); o.put ('"'); }
	o.puts (" priority=\""); o.puti (prio); o.put ('"');
	if (c.type == CF_CELL) { o.puts (" operator=\""); o.puts (OPS[iclamp (c.op, 0, 7)]); o.put ('"'); }
	if (c.type == CF_TEXT) { o.puts (" operator=\""); o.puts (TOPS[iclamp (c.op, 0, 3)]); o.puts ("\" text=\""); xml_esc (o, c.a); o.put ('"'); }
	if (c.type == CF_TOP) { o.puts (" rank=\""); o.puti (imax (1, atoi (c.a))); o.put ('"'); if (c.pct) o.puts (" percent=\"1\""); if (c.op) o.puts (" bottom=\"1\""); }
	if (c.type == CF_AVERAGE && c.op) o.puts (" aboveAverage=\"0\"");
	o.put ('>');
	switch (c.type)
	{
	case CF_CELL: put_cf_formula (o, c.a); if (c.op == CO_BETWEEN || c.op == CO_NOTBETWEEN) put_cf_formula (o, c.b); break;
	case CF_FORMULA: { char t[130]; t[0] = '='; scpy (t + 1, c.a[0] == '=' ? c.a + 1 : c.a, sizeof t - 1); put_cf_formula (o, t); break; }
	case CF_TEXT:
	{
		Buf q; q.put ('"'); for (const char *p = c.a; *p; p++) { if (*p == '"') q.put ('"'); q.put (*p); } q.put ('"');
		Buf f;
		if (c.op == CT_CONTAINS) { f.puts ("NOT(ISERROR(SEARCH("); f.puts (q.str ()); f.put (','); f.puts (a1); f.puts (")))"); }
		else if (c.op == CT_NOTCONTAINS) { f.puts ("ISERROR(SEARCH("); f.puts (q.str ()); f.put (','); f.puts (a1); f.puts ("))"); }
		else { f.puts (c.op == CT_BEGINS ? "LEFT(" : "RIGHT("); f.puts (a1); f.puts (",LEN("); f.puts (q.str ()); f.puts ("))="); f.puts (q.str ()); }
		o.puts ("<formula>"); xml_esc (o, f.str ()); o.puts ("</formula>");
		break;
	}
	case CF_SCALE:
		o.puts ("<colorScale><cfvo type=\"min\"/>");
		if (c.op == 3) o.puts ("<cfvo type=\"percentile\" val=\"50\"/>");
		o.puts ("<cfvo type=\"max\"/><color rgb=\""); hexcol (o, c.c0); o.puts ("\"/>");
		if (c.op == 3) { o.puts ("<color rgb=\""); hexcol (o, c.c1); o.puts ("\"/>"); }
		o.puts ("<color rgb=\""); hexcol (o, c.c2); o.puts ("\"/></colorScale>");
		break;
	case CF_BAR:
		o.puts ("<dataBar><cfvo type=\"min\"/><cfvo type=\"max\"/><color rgb=\""); hexcol (o, c.c0); o.puts ("\"/></dataBar>");
		break;
	}
	o.puts ("</cfRule></conditionalFormatting>");
}

// The workbook's bytes (malloc'd) and their count.
static char *xlsx_write (Book &b, int *outLen)
{
	pngsave::ZipOut zip;
	// ---- the styles used: fonts, fills, borders, formats, then the cell formats (xfs)
	int ns = b.styles.n;
	int *xfOf = (int *) malloc (ns * sizeof (int));
	for (int i = 0; i < ns; i++) xfOf[i] = -1;
	int *xfStyle = (int *) malloc (ns * sizeof (int)); int nxf = 0;
	auto use = [&] (int st) { if (st >= 0 && st < ns && xfOf[st] < 0) { xfOf[st] = nxf; xfStyle[nxf++] = st; } };
	use (0);
	for (int si = 0; si < b.ns; si++)
	{
		Sheet *s = b.sh[si];
		for (int k = 0; k < s->cells.cap; k++) if (s->cells.t[k]) use (s->cells.t[k]->style);
		for (int k = 0; k < s->nrows; k++) if (s->rows[k].style) use (s->rows[k].style);
		for (int c = 0; c < MAXC; c++) if (s->colSt[c]) use (s->colSt[c]);
	}
	// fonts, fills, borders: by their content
	struct FontK { unsigned short font, size; unsigned char b, i, u, s; unsigned color; } *fonts = (FontK *) malloc (nxf * sizeof (FontK));
	unsigned *fills = (unsigned *) malloc ((nxf + 2) * sizeof (unsigned));
	XBorder *bords = (XBorder *) malloc ((nxf + 1) * sizeof (XBorder));
	int nfont = 0, nfill = 2, nbord = 1;
	fills[0] = fills[1] = AUTO;
	memset (&bords[0], 0, sizeof (XBorder));
	int *fontOf = (int *) malloc (nxf * sizeof (int)), *fillOf = (int *) malloc (nxf * sizeof (int)), *bordOf = (int *) malloc (nxf * sizeof (int)), *fmtOf = (int *) malloc (nxf * sizeof (int));
	int custom[4096]; int ncustom = 0;			// book format index -> its id (164...)
	int customIdx[4096];
	for (int i = 0; i < nxf; i++)
	{
		const Style &st = b.styles.s[xfStyle[i]];
		FontK fk; memset (&fk, 0, sizeof fk); fk.font = st.font; fk.size = st.size; fk.b = st.bold; fk.i = st.italic; fk.u = st.under; fk.s = st.strike; fk.color = st.color;
		int f = -1; for (int k = 0; k < nfont; k++) if (!memcmp (&fonts[k], &fk, sizeof fk)) f = k;
		if (f < 0) { fonts[nfont] = fk; f = nfont++; }
		fontOf[i] = f;
		int fl = 0;
		if (st.fill != AUTO) { fl = -1; for (int k = 2; k < nfill; k++) if (fills[k] == st.fill) fl = k; if (fl < 0) { fills[nfill] = st.fill; fl = nfill++; } }
		fillOf[i] = fl;
		XBorder bk; memset (&bk, 0, sizeof bk); for (int k = 0; k < 4; k++) { bk.st[k] = st.bs[k]; bk.col[k] = st.bs[k] ? st.bc[k] : 0; }
		int bo = -1; for (int k = 0; k < nbord; k++) if (!memcmp (&bords[k], &bk, sizeof bk)) bo = k;
		if (bo < 0) { bords[nbord] = bk; bo = nbord++; }
		bordOf[i] = bo;
		const char *code = book_fmt_code (b, st.fmt);
		int id = builtin_fmt_id (code);
		if (id < 0)
		{
			for (int k = 0; k < ncustom; k++) if (customIdx[k] == st.fmt) id = custom[k];
			if (id < 0 && ncustom < 4096) { customIdx[ncustom] = st.fmt; custom[ncustom] = 164 + ncustom; id = custom[ncustom]; ncustom++; }
		}
		fmtOf[i] = id < 0 ? 0 : id;
	}
	Buf o;
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
	if (ncustom)
	{
		o.puts ("<numFmts count=\""); o.puti (ncustom); o.puts ("\">");
		for (int k = 0; k < ncustom; k++) { o.puts ("<numFmt numFmtId=\""); o.puti (custom[k]); o.puts ("\" formatCode=\""); xml_esc (o, book_fmt_code (b, customIdx[k])); o.puts ("\"/>"); }
		o.puts ("</numFmts>");
	}
	o.puts ("<fonts count=\""); o.puti (nfont); o.puts ("\">");
	for (int k = 0; k < nfont; k++)
	{
		FontK &f = fonts[k];
		o.puts ("<font>");
		if (f.b) o.puts ("<b/>");
		if (f.i) o.puts ("<i/>");
		if (f.s) o.puts ("<strike/>");
		if (f.u) o.puts (f.u == 2 ? "<u val=\"double\"/>" : "<u/>");
		char t[32]; snprintf (t, sizeof t, "%g", f.size / 10.0);
		o.puts ("<sz val=\""); o.puts (t); o.puts ("\"/>");
		if (f.color != AUTO) { o.puts ("<color rgb=\""); hexcol (o, f.color); o.puts ("\"/>"); }
		o.puts ("<name val=\""); xml_esc (o, b.fonts[f.font < b.nfonts ? f.font : 0]); o.puts ("\"/></font>");
	}
	o.puts ("</fonts><fills count=\""); o.puti (nfill); o.puts ("\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>");
	for (int k = 2; k < nfill; k++) { o.puts ("<fill><patternFill patternType=\"solid\"><fgColor rgb=\""); hexcol (o, fills[k]); o.puts ("\"/><bgColor indexed=\"64\"/></patternFill></fill>"); }
	o.puts ("</fills><borders count=\""); o.puti (nbord); o.puts ("\">");
	static const char *const SIDE[4] = { "left", "right", "top", "bottom" };
	for (int k = 0; k < nbord; k++)
	{
		o.puts ("<border>");
		for (int sd = 0; sd < 4; sd++)
		{
			const char *nm = border_name (bords[k].st[sd]);
			if (!nm) { o.put ('<'); o.puts (SIDE[sd]); o.puts ("/>"); continue; }
			o.put ('<'); o.puts (SIDE[sd]); o.puts (" style=\""); o.puts (nm); o.puts ("\"><color rgb=\""); hexcol (o, bords[k].col[sd] == AUTO ? 0 : bords[k].col[sd]); o.puts ("\"/></"); o.puts (SIDE[sd]); o.put ('>');
		}
		o.puts ("<diagonal/></border>");
	}
	o.puts ("</borders><cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs><cellXfs count=\""); o.puti (nxf); o.puts ("\">");
	for (int i = 0; i < nxf; i++)
	{
		const Style &st = b.styles.s[xfStyle[i]];
		o.puts ("<xf numFmtId=\""); o.puti (fmtOf[i]); o.puts ("\" fontId=\""); o.puti (fontOf[i]); o.puts ("\" fillId=\""); o.puti (fillOf[i]);
		o.puts ("\" borderId=\""); o.puti (bordOf[i]); o.puts ("\" xfId=\"0\"");
		if (fmtOf[i]) o.puts (" applyNumberFormat=\"1\"");
		if (fontOf[i]) o.puts (" applyFont=\"1\"");
		if (fillOf[i]) o.puts (" applyFill=\"1\"");
		if (bordOf[i]) o.puts (" applyBorder=\"1\"");
		if (st.ha || st.va || st.wrap || st.indent)
		{
			static const char *const HA[] = { "general", "left", "center", "right", "fill", "justify" };
			o.puts (" applyAlignment=\"1\"><alignment");
			if (st.ha) { o.puts (" horizontal=\""); o.puts (HA[st.ha < 6 ? st.ha : 0]); o.put ('"'); }
			if (st.va) o.puts (st.va == VA_TOP ? " vertical=\"top\"" : " vertical=\"center\"");
			if (st.wrap) o.puts (" wrapText=\"1\"");
			if (st.indent) { o.puts (" indent=\""); o.puti (st.indent); o.put ('"'); }
			o.puts ("/></xf>");
		}
		else o.puts ("/>");
	}
	o.puts ("</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>");
	// the looks of the conditional formats (dxfs), in the sheets' order
	{
		int nd = 0;
		for (int si = 0; si < b.ns; si++) for (int k = 0; k < b.sh[si]->ncf; k++) if (cf_has_look (b.sh[si]->cf[k])) nd++;
		if (nd)
		{
			o.puts ("<dxfs count=\""); o.puti (nd); o.puts ("\">");
			for (int si = 0; si < b.ns; si++) for (int k = 0; k < b.sh[si]->ncf; k++) if (cf_has_look (b.sh[si]->cf[k])) put_dxf (o, b.sh[si]->cf[k]);
			o.puts ("</dxfs>");
		}
	}
	o.puts ("</styleSheet>");
	zip.add ("xl/styles.xml", o.b, (unsigned) o.n, true);

	// ---- the sheets
	SStr sst; memset (&sst, 0, sizeof sst);
	int nchartsAll = 0, dxfNo = 0;
	for (int si = 0; si < b.ns; si++)
	{
		Sheet *s = b.sh[si];
		sheet_bounds (s);
		o.clear ();
		o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
		if (s->tab != AUTO) { o.puts ("<sheetPr><tabColor rgb=\""); hexcol (o, s->tab); o.puts ("\"/></sheetPr>"); }
		char a1[24], a2[24];
		cell_name (0, 0, a1); cell_name (imax (0, s->maxR), imax (0, s->maxC), a2);
		o.puts ("<dimension ref=\""); o.puts (a1); if (s->maxR > 0 || s->maxC > 0) { o.put (':'); o.puts (a2); } o.puts ("\"/>");
		o.puts ("<sheetViews><sheetView workbookViewId=\"0\"");
		if (si == b.active) o.puts (" tabSelected=\"1\"");
		if (!s->grid) o.puts (" showGridLines=\"0\"");
		o.put ('>');
		if (s->freezeR || s->freezeC)
		{
			char tl[24]; cell_name (s->freezeR, s->freezeC, tl);
			o.puts ("<pane");
			if (s->freezeC) { o.puts (" xSplit=\""); o.puti (s->freezeC); o.put ('"'); }
			if (s->freezeR) { o.puts (" ySplit=\""); o.puti (s->freezeR); o.put ('"'); }
			o.puts (" topLeftCell=\""); o.puts (tl); o.puts ("\" activePane=\""); o.puts (s->freezeR && s->freezeC ? "bottomRight" : s->freezeR ? "bottomLeft" : "topRight"); o.puts ("\" state=\"frozen\"/>");
		}
		char cur[24]; cell_name (s->curR, s->curC, cur);
		o.puts ("<selection activeCell=\""); o.puts (cur); o.puts ("\" sqref=\""); o.puts (cur); o.puts ("\"/></sheetView></sheetViews>");
		char t[64];
		snprintf (t, sizeof t, "%g", s->defRowH * 0.75);
		o.puts ("<sheetFormatPr defaultRowHeight=\""); o.puts (t); o.puts ("\"");
		snprintf (t, sizeof t, "%g", floor (s->defColW / 7.0 * 256) / 256); o.puts (" defaultColWidth=\""); o.puts (t); o.puts ("\"/>");
		// the columns: runs of the same width / style / visibility
		{
			bool any = false;
			for (int c = 0; c < MAXC; )
			{
				if (!s->colW[c] && !s->colSt[c] && !s->colFl[c]) { c++; continue; }
				int e = c;
				while (e + 1 < MAXC && s->colW[e + 1] == s->colW[c] && s->colSt[e + 1] == s->colSt[c] && s->colFl[e + 1] == s->colFl[c]) e++;
				if (!any) { o.puts ("<cols>"); any = true; }
				o.puts ("<col min=\""); o.puti (c + 1); o.puts ("\" max=\""); o.puti (e + 1); o.puts ("\" width=\"");
				snprintf (t, sizeof t, "%g", floor ((s->colW[c] ? s->colW[c] : s->defColW) / 7.0 * 256) / 256); o.puts (t); o.put ('"');
				if (s->colW[c]) o.puts (" customWidth=\"1\"");
				if (s->colFl[c] & RF_HIDDEN) o.puts (" hidden=\"1\"");
				if (s->colSt[c]) { o.puts (" style=\""); o.puti (xfOf[s->colSt[c]]); o.put ('"'); }
				o.puts ("/>");
				c = e + 1;
			}
			if (any) o.puts ("</cols>");
		}
		// the rows and their cells, in order
		o.puts ("<sheetData>");
		{
			int cnt = 0;
			Cell **list = (Cell **) malloc (imax (1, s->cells.n) * sizeof (Cell *));
			for (int k = 0; k < s->cells.cap; k++) if (s->cells.t[k]) list[cnt++] = s->cells.t[k];
			qsort (list, cnt, sizeof (Cell *), [] (const void *a, const void *b2) -> int {
				const Cell *x = *(Cell *const *) a, *y = *(Cell *const *) b2;
				return x->r != y->r ? (x->r < y->r ? -1 : 1) : x->c < y->c ? -1 : x->c > y->c ? 1 : 0; });
			int ri = 0, k = 0;
			while (k < cnt || ri < s->nrows)
			{
				int r = k < cnt ? list[k]->r : MAXR;
				if (ri < s->nrows && s->rows[ri].r <= r) r = s->rows[ri].r;
				RowInfo *inf = ri < s->nrows && s->rows[ri].r == r ? &s->rows[ri] : 0;
				if (inf) ri++;
				o.puts ("<row r=\""); o.puti (r + 1); o.put ('"');
				if (inf)
				{
					if (inf->fl & (RF_CUSTOM | RF_AUTO)) { snprintf (t, sizeof t, "%g", inf->h * 0.75); o.puts (" ht=\""); o.puts (t); o.puts ("\" customHeight=\"1\""); }
					if (inf->fl & RF_HIDDEN) o.puts (" hidden=\"1\"");
					if (inf->style) { o.puts (" s=\""); o.puti (xfOf[inf->style]); o.puts ("\" customFormat=\"1\""); }
				}
				o.put ('>');
				for (; k < cnt && list[k]->r == r; k++)
				{
					Cell *x = list[k];
					char ref[24]; cell_name (x->r, x->c, ref);
					o.puts ("<c r=\""); o.puts (ref); o.put ('"');
					if (x->style) { o.puts (" s=\""); o.puti (xfOf[x->style]); o.put ('"'); }
					if (x->kind == K_NONE) { o.puts ("/>"); continue; }
					int vt = x->vt;
					if (x->kind == K_FORM)
					{
						if (vt == V_STR) o.puts (" t=\"str\""); else if (vt == V_BOOL) o.puts (" t=\"b\""); else if (vt == V_ERR) o.puts (" t=\"e\"");
						o.puts ("><f>");
						Buf fb; formula_print (b, x->f, fb, true); xml_esc (o, fb.str (), fb.n);
						o.puts ("</f>");
						if (vt == V_STR) { o.puts ("<v>"); xml_esc (o, x->str ? x->str : ""); o.puts ("</v>"); }
						else if (vt == V_BOOL) o.puts (x->num ? "<v>1</v>" : "<v>0</v>");
						else if (vt == V_ERR) { o.puts ("<v>"); o.puts (ERR_NAMES[x->err]); o.puts ("</v>"); }
						else if (vt == V_NUM) { char nb[40]; snprintf (nb, sizeof nb, "%.17g", x->num); o.puts ("<v>"); o.puts (nb); o.puts ("</v>"); }
						o.puts ("</c>");
						continue;
					}
					switch (vt)
					{
					case V_STR: o.puts (" t=\"s\"><v>"); o.puti (sst_add (sst, x->str)); o.puts ("</v></c>"); break;
					case V_BOOL: o.puts (x->num ? " t=\"b\"><v>1</v></c>" : " t=\"b\"><v>0</v></c>"); break;
					case V_ERR: o.puts (" t=\"e\"><v>"); o.puts (ERR_NAMES[x->err]); o.puts ("</v></c>"); break;
					default: { char nb[40]; snprintf (nb, sizeof nb, "%.17g", x->num); o.puts ("><v>"); o.puts (nb); o.puts ("</v></c>"); }
					}
				}
				o.puts ("</row>");
			}
			free (list);
		}
		o.puts ("</sheetData>");
		if (s->nmerge)
		{
			o.puts ("<mergeCells count=\""); o.puti (s->nmerge); o.puts ("\">");
			for (int k = 0; k < s->nmerge; k++)
			{
				cell_name (s->merges[k].r0, s->merges[k].c0, a1); cell_name (s->merges[k].r1, s->merges[k].c1, a2);
				o.puts ("<mergeCell ref=\""); o.puts (a1); o.put (':'); o.puts (a2); o.puts ("\"/>");
			}
			o.puts ("</mergeCells>");
		}
		// the conditional formats (their looks: the dxfs, numbered through the sheets)
		for (int k = 0; k < s->ncf; k++) { put_cf (o, s->cf[k], cf_has_look (s->cf[k]) ? dxfNo++ : -1, k + 1); }
		o.puts ("<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>");
		if (s->ncharts) o.puts ("<drawing r:id=\"rId1\"/>");
		o.puts ("</worksheet>");
		char path[64]; snprintf (path, sizeof path, "xl/worksheets/sheet%d.xml", si + 1);
		zip.add (path, o.b, (unsigned) o.n, true);
		// its charts: a drawing (the anchors) and a part a chart
		if (s->ncharts)
		{
			Buf rel, dr;
			rel.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
				  "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" Target=\"../drawings/drawing");
			rel.puti (si + 1); rel.puts (".xml\"/></Relationships>");
			snprintf (path, sizeof path, "xl/worksheets/_rels/sheet%d.xml.rels", si + 1);
			zip.add (path, rel.b, (unsigned) rel.n, false);
			dr.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
				 "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
				 "xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">");
			Buf drel;
			drel.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
			for (int k = 0; k < s->ncharts; k++)
			{
				Chart *c = s->charts[k];
				int fc = col_at (s, c->x), fr = row_at (s, c->y), tc = col_at (s, c->x + c->w), tr = row_at (s, c->y + c->h);
				long long fco = (long long) (c->x - col_x (s, fc)) * 9525, fro = (long long) (c->y - row_y (s, fr)) * 9525;
				long long tco = (long long) (c->x + c->w - col_x (s, tc)) * 9525, tro = (long long) (c->y + c->h - row_y (s, tr)) * 9525;
				char a[256];
				snprintf (a, sizeof a, "<xdr:twoCellAnchor><xdr:from><xdr:col>%d</xdr:col><xdr:colOff>%lld</xdr:colOff><xdr:row>%d</xdr:row><xdr:rowOff>%lld</xdr:rowOff></xdr:from>", fc, fco, fr, fro);
				dr.puts (a);
				snprintf (a, sizeof a, "<xdr:to><xdr:col>%d</xdr:col><xdr:colOff>%lld</xdr:colOff><xdr:row>%d</xdr:row><xdr:rowOff>%lld</xdr:rowOff></xdr:to>", tc, tco, tr, tro);
				dr.puts (a);
				snprintf (a, sizeof a, "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"%d\" name=\"Chart %d\"/><xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr>", k + 2, k + 1);
				dr.puts (a);
				dr.puts ("<xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">");
				snprintf (a, sizeof a, "<c:chart r:id=\"rId%d\"/></a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:twoCellAnchor>", k + 1);
				dr.puts (a);
				nchartsAll++;
				snprintf (a, sizeof a, "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\" Target=\"../charts/chart%d.xml\"/>", k + 1, nchartsAll);
				drel.puts (a);
				Buf cb; write_chart_xml (b, c, cb);
				snprintf (path, sizeof path, "xl/charts/chart%d.xml", nchartsAll);
				zip.add (path, cb.b, (unsigned) cb.n, true);
			}
			dr.puts ("</xdr:wsDr>");
			drel.puts ("</Relationships>");
			snprintf (path, sizeof path, "xl/drawings/drawing%d.xml", si + 1);
			zip.add (path, dr.b, (unsigned) dr.n, true);
			snprintf (path, sizeof path, "xl/drawings/_rels/drawing%d.xml.rels", si + 1);
			zip.add (path, drel.b, (unsigned) drel.n, false);
		}
	}
	// ---- the shared strings
	o.clear ();
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"");
	o.puti (sst.n); o.puts ("\" uniqueCount=\""); o.puti (sst.n); o.puts ("\">");
	for (int i = 0; i < sst.n; i++)
	{
		const char *sv = sst.str[i]; int l = (int) strlen (sv);
		bool pre = l && (sv[0] == ' ' || sv[l - 1] == ' ' || strchr (sv, '\n'));
		o.puts (pre ? "<si><t xml:space=\"preserve\">" : "<si><t>"); xml_esc (o, sv, l); o.puts ("</t></si>");
		free (sst.str[i]);
	}
	o.puts ("</sst>");
	free (sst.str); free (sst.h);
	zip.add ("xl/sharedStrings.xml", o.b, (unsigned) o.n, true);
	// ---- the workbook, its relationships, the package's
	o.clear ();
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
	o.puts ("<workbookPr/><bookViews><workbookView activeTab=\""); o.puti (b.active); o.puts ("\"/></bookViews><sheets>");
	for (int si = 0; si < b.ns; si++) { o.puts ("<sheet name=\""); xml_esc (o, b.sh[si]->name); o.puts ("\" sheetId=\""); o.puti (si + 1); o.puts ("\" r:id=\"rId"); o.puti (si + 1); o.puts ("\"/>"); }
	o.puts ("</sheets>");
	if (b.nnames)							// the defined names
	{
		o.puts ("<definedNames>");
		for (int i = 0; i < b.nnames; i++)
		{
			const DefName &d = b.names[i];
			if (!d.f) continue;
			o.puts ("<definedName name=\""); xml_esc (o, d.name); o.put ('"');
			if (d.scope) for (int si = 0; si < b.ns; si++) if (b.sh[si]->id == d.scope) { o.puts (" localSheetId=\""); o.puti (si); o.put ('"'); }
			o.put ('>');
			Buf fb; formula_print (b, d.f, fb, true); xml_esc (o, fb.str (), fb.n);
			o.puts ("</definedName>");
		}
		o.puts ("</definedNames>");
	}
	o.puts ("<calcPr calcId=\"191029\" fullCalcOnLoad=\"1\"/></workbook>");
	zip.add ("xl/workbook.xml", o.b, (unsigned) o.n, true);
	o.clear ();
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
	for (int si = 0; si < b.ns; si++)
	{ o.puts ("<Relationship Id=\"rId"); o.puti (si + 1); o.puts ("\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet"); o.puti (si + 1); o.puts (".xml\"/>"); }
	o.puts ("<Relationship Id=\"rId"); o.puti (b.ns + 1); o.puts ("\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");
	o.puts ("<Relationship Id=\"rId"); o.puti (b.ns + 2); o.puts ("\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>");
	o.puts ("</Relationships>");
	zip.add ("xl/_rels/workbook.xml.rels", o.b, (unsigned) o.n, false);
	o.clear ();
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
		"<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
		"<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
		"<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
		"<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
		"<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>");
	for (int si = 0; si < b.ns; si++)
	{
		o.puts ("<Override PartName=\"/xl/worksheets/sheet"); o.puti (si + 1); o.puts (".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>");
		if (b.sh[si]->ncharts) { o.puts ("<Override PartName=\"/xl/drawings/drawing"); o.puti (si + 1); o.puts (".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>"); }
	}
	for (int k = 1; k <= nchartsAll; k++) { o.puts ("<Override PartName=\"/xl/charts/chart"); o.puti (k); o.puts (".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>"); }
	o.puts ("</Types>");
	zip.add ("[Content_Types].xml", o.b, (unsigned) o.n, false);
	o.clear ();
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
		"<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/></Relationships>");
	zip.add ("_rels/.rels", o.b, (unsigned) o.n, false);
	o.clear ();
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\"><Application>Onyx Spreadsheet</Application></Properties>");
	zip.add ("docProps/app.xml", o.b, (unsigned) o.n, false);
	free (xfOf); free (xfStyle); free (fonts); free (fills); free (bords); free (fontOf); free (fillOf); free (bordOf); free (fmtOf);
	unsigned zl = 0;
	unsigned char *zd = zip.finish (&zl);
	char *out = (char *) malloc (zl);
	memcpy (out, zd, zl);
	delete[] zd;
	*outLen = (int) zl;
	return out;
}

// A chart's part: its series' ranges named in the sheet (the other programs compute them).
static void ref_text (Book &b, int sheetId, int r0, int c0, int r1, int c1, Buf &o)
{
	Sheet *s = book_sheet_by_id (b, sheetId);
	if (!s) { o.puts ("#REF!"); return; }
	put_sheet_name (o, s->name);
	o.put ('!');
	char t[24]; cell_name (r0, c0, t, true, true); o.puts (t);
	if (r1 != r0 || c1 != c0) { o.put (':'); cell_name (r1, c1, t, true, true); o.puts (t); }
}
static void write_chart_xml (Book &b, Chart *c, Buf &o)
{
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
		"xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><c:chart>");
	if (c->title[0])
	{
		o.puts ("<c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r><a:t>"); xml_esc (o, c->title); o.puts ("</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title><c:autoTitleDeleted val=\"0\"/>");
	}
	else o.puts ("<c:autoTitleDeleted val=\"1\"/>");
	o.puts ("<c:plotArea><c:layout/>");
	const char *tag = c->type == CH_LINE ? "c:lineChart" : c->type == CH_AREA ? "c:areaChart" : c->type == CH_PIE ? "c:pieChart" : c->type == CH_SCATTER ? "c:scatterChart" : "c:barChart";
	o.put ('<'); o.puts (tag); o.put ('>');
	if (c->type == CH_COLUMN || c->type == CH_BAR) { o.puts (c->type == CH_BAR ? "<c:barDir val=\"bar\"/>" : "<c:barDir val=\"col\"/>"); o.puts (c->stacked ? "<c:grouping val=\"stacked\"/>" : "<c:grouping val=\"clustered\"/>"); }
	else if (c->type == CH_LINE || c->type == CH_AREA) o.puts (c->stacked ? "<c:grouping val=\"stacked\"/>" : "<c:grouping val=\"standard\"/>");
	else if (c->type == CH_SCATTER) o.puts ("<c:scatterStyle val=\"lineMarker\"/>");
	o.puts ("<c:varyColors val=\""); o.puts (c->type == CH_PIE ? "1" : "0"); o.puts ("\"/>");
	Rect r = c->src;
	int first = c->byRows ? r.r0 + (c->head ? 1 : 0) : r.c0 + (c->side ? 1 : 0);
	int last = c->byRows ? r.r1 : r.c1;
	int nser = 0;
	for (int k = first; k <= last && nser < 64; k++, nser++)
	{
		o.puts ("<c:ser><c:idx val=\""); o.puti (nser); o.puts ("\"/><c:order val=\""); o.puti (nser); o.puts ("\"/>");
		bool named = c->byRows ? c->side : c->head;
		if (named)
		{
			o.puts ("<c:tx><c:strRef><c:f>");
			Buf t; if (c->byRows) ref_text (b, c->srcSheet, k, r.c0, k, r.c0, t); else ref_text (b, c->srcSheet, r.r0, k, r.r0, k, t);
			xml_esc (o, t.str ()); o.puts ("</c:f></c:strRef></c:tx>");
		}
		if (c->type == CH_SCATTER) o.puts ("<c:marker><c:symbol val=\"circle\"/></c:marker>");
		bool hasCat = c->byRows ? c->head : c->side;
		int v0 = c->byRows ? r.c0 + (c->side ? 1 : 0) : r.r0 + (c->head ? 1 : 0), v1 = c->byRows ? r.c1 : r.r1;
		if (hasCat)
		{
			o.puts (c->type == CH_SCATTER ? "<c:xVal><c:numRef><c:f>" : "<c:cat><c:strRef><c:f>");
			Buf t; if (c->byRows) ref_text (b, c->srcSheet, r.r0, v0, r.r0, v1, t); else ref_text (b, c->srcSheet, v0, r.c0, v1, r.c0, t);
			xml_esc (o, t.str ());
			o.puts (c->type == CH_SCATTER ? "</c:f></c:numRef></c:xVal>" : "</c:f></c:strRef></c:cat>");
		}
		o.puts (c->type == CH_SCATTER ? "<c:yVal><c:numRef><c:f>" : "<c:val><c:numRef><c:f>");
		Buf t; if (c->byRows) ref_text (b, c->srcSheet, k, v0, k, v1, t); else ref_text (b, c->srcSheet, v0, k, v1, k, t);
		xml_esc (o, t.str ());
		o.puts (c->type == CH_SCATTER ? "</c:f></c:numRef></c:yVal>" : "</c:f></c:numRef></c:val>");
		if (c->type == CH_LINE || c->type == CH_SCATTER) o.puts ("<c:smooth val=\"0\"/>");
		o.puts ("</c:ser>");
	}
	if (c->type == CH_COLUMN || c->type == CH_BAR) o.puts (c->stacked ? "<c:overlap val=\"100\"/>" : "<c:gapWidth val=\"150\"/>");
	if (c->type != CH_PIE) o.puts ("<c:axId val=\"111\"/><c:axId val=\"222\"/>");
	else o.puts ("<c:firstSliceAng val=\"0\"/>");
	o.puts ("</"); o.puts (tag); o.put ('>');
	if (c->type != CH_PIE)
	{
		bool bar = c->type == CH_BAR;
		if (c->type == CH_SCATTER) o.puts ("<c:valAx><c:axId val=\"111\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\"b\"/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/><c:crossAx val=\"222\"/></c:valAx>");
		else { o.puts ("<c:catAx><c:axId val=\"111\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\""); o.puts (bar ? "l" : "b"); o.puts ("\"/><c:crossAx val=\"222\"/></c:catAx>"); }
		o.puts ("<c:valAx><c:axId val=\"222\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\""); o.puts (bar ? "b" : "l"); o.puts ("\"/>");
		if (c->grid) o.puts ("<c:majorGridlines/>");
		o.puts ("<c:numFmt formatCode=\"General\" sourceLinked=\"1\"/><c:crossAx val=\"111\"/></c:valAx>");
	}
	o.puts ("</c:plotArea>");
	if (c->legend != LG_NONE) { o.puts ("<c:legend><c:legendPos val=\""); o.puts (c->legend == LG_BOTTOM ? "b" : c->legend == LG_TOP ? "t" : "r"); o.puts ("\"/><c:overlay val=\"0\"/></c:legend>"); }
	o.puts ("<c:plotVisOnly val=\"1\"/></c:chart></c:chartSpace>");
}

} // namespace ss

#endif
