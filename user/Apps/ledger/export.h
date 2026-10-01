//
// export.h -- a report as a document of the other programs:
//   * a Writer document (RTF): A4 (landscape when it has many columns), the company in the page's header,
//     the pages numbered at the foot; the report's title and period; its rows in a table whose title row
//     is repeated on each page -- the headings shaded across the table, the subtotals and totals in bold,
//     the amounts at the right;
//   * a workbook for the Spreadsheet (XLSX): the title, the period, the table -- the amounts as numbers
//     formatted #.##0,00 (so they add up there), the headings and totals in bold, the columns' widths;
//   * a CSV file (';' between the cells, as a Belgian spreadsheet reads it).
// Written in SD:/docs/Reports/ (or where the user says), then shown in Writer or the Spreadsheet when
// asked ("Open in Writer" / "Open in the Spreadsheet").
//
#ifndef _ledger_export_h
#define _ledger_export_h

#include "ui.h"
#include "img/pngsave.hpp"

namespace lg {

// ---- RTF ---------------------------------------------------------------------------------------------------------------------------
static void rtf_text (Out &o, const char *s)
{
	static const char hx[] = "0123456789abcdef";
	for (const unsigned char *p = (const unsigned char *) s; *p; p++)
	{
		if (*p == '\\' || *p == '{' || *p == '}') { o.put ('\\'); o.put ((char) *p); }
		else if (*p >= 0x80) { o.puts ("\\'"); o.put (hx[*p >> 4]); o.put (hx[*p & 15]); }
		else if (*p == '\t') o.puts ("\\tab ");
		else if (*p >= 32) o.put ((char) *p);
	}
}
static void rtf_num (Out &o, const char *ctl, int v) { o.puts (ctl); char t[16]; itoa10 (v, t); o.puts (t); }
// The columns' widths in twips: in proportion to the report's (their characters), the text's width shared.
static void col_twips (const Report &p, int total, int *w)
{
	int s = 0; for (int c = 0; c < p.ncol; c++) s += imax (4, p.col[c].width);
	int x = 0;
	for (int c = 0; c < p.ncol; c++) { w[c] = (int) ((long long) total * imax (4, p.col[c].width) / s); x += w[c]; }
	if (p.ncol) w[p.ncol - 1] += total - x;
}
static void company_line (const Book &b, char *out, int cap)
{
	scpy (out, b.name, cap);
	if (b.vat[0]) { char v[24]; vat_show (b.vat, v, sizeof v); scat (out, "  -  ", cap); scat (out, v, cap); }
}
static void rpt_rtf (const Book &b, const Report &p, Out &o)
{
	bool wide = p.ncol >= 6;
	int pw = wide ? 16838 : 11906, ph = wide ? 11906 : 16838, m = 850, tw = pw - 2 * m;
	int w[RMAXCOL]; col_twips (p, tw, w);
	o.puts ("{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Liberation Sans;}}\n");
	o.puts ("{\\colortbl;\\red0\\green0\\blue0;\\red232\\green237\\blue243;\\red205\\green217\\blue230;\\red178\\green46\\blue36;\\red105\\green105\\blue105;}\n");
	rtf_num (o, "\\paperw", pw); rtf_num (o, "\\paperh", ph); rtf_num (o, "\\margl", m); rtf_num (o, "\\margr", m); rtf_num (o, "\\margt", m); rtf_num (o, "\\margb", m);
	if (wide) o.puts ("\\landscape");
	o.puts ("\n");
	char cl[160]; company_line (b, cl, sizeof cl);
	o.puts ("{\\header\\pard\\plain\\f0\\fs16\\cf5 "); rtf_text (o, cl); o.puts ("\\par}\n");
	char d[16]; date_show (today_ymd (), d);
	o.puts ("{\\footer\\pard\\plain\\qc\\f0\\fs16\\cf5 "); rtf_text (o, p.title); o.puts (" - "); rtf_text (o, d); o.puts (" - "); rtf_text (o, TR ("page")); o.puts (" \\chpgn\\par}\n");
	o.puts ("\\pard\\plain\\f0\\sa60\\b\\fs32 "); rtf_text (o, p.title); o.puts ("\\par\n");
	o.puts ("\\pard\\plain\\f0\\sa240\\fs19\\cf5 "); rtf_text (o, cl); if (p.sub[0]) { o.puts ("  -  "); rtf_text (o, p.sub); } o.puts ("\\par\n");
	// the rows: the title row first (repeated on every page)
	for (int i = -1; i < p.nr; i++)
	{
		int style = i < 0 ? (int) RS_HEAD : (int) p.r[i].style;
		if (style == RS_BLANK) continue;
		bool title = i < 0, head = !title && style == RS_HEAD, bold = title || style == RS_SUB || style == RS_TOTAL;
		o.puts ("\\trowd\\trgaph70\\trleft0");
		if (title) o.puts ("\\trhdr");
		int x = 0;
		for (int c = 0; c < p.ncol; c++)
		{
			x += w[c];
			if (title) o.puts ("\\clcbpat3\\clbrdrb\\brdrs\\brdrw10");
			else if (head) { o.puts ("\\clcbpat2"); if (c) o.puts ("\\clmrg"); }
			else if (style == RS_TOTAL) o.puts ("\\clcbpat2\\clbrdrt\\brdrs\\brdrw10");
			else if (style == RS_SUB && p.col[c].money) o.puts ("\\clbrdrt\\brdrs\\brdrw5");
			rtf_num (o, "\\cellx", x);
		}
		o.puts ("\n");
		for (int c = 0; c < p.ncol; c++)
		{
			const char *s = title ? p.col[c].title : p.r[i].cell[c];
			o.puts ("\\pard\\plain\\intbl\\f0\\fs17");
			if (p.col[c].align == 1 && !head) o.puts ("\\qr");
			if (bold || head) o.puts ("\\b");
			if (!title && style == RS_DIM) o.puts ("\\i\\cf5");
			if (!title && p.col[c].money && !p.col[c].bal && s[0] == '-' && style == RS_LINE) o.puts ("\\cf4");
			o.puts (" ");
			if (!title && c == 0 && p.r[i].indent) { for (int k = 0; k < p.r[i].indent; k++) o.puts ("    "); }
			if (head && c > 0) {}
			else if (!title && p.col[c].bal) { char dc[48]; dc_text (s, dc, sizeof dc); rtf_text (o, dc); }
			else rtf_text (o, s);
			o.puts ("\\cell\n");
		}
		o.puts ("\\row\n");
	}
	o.puts ("\\pard\\plain\\f0\\fs16\\par}\n");
}

// ---- XLSX --------------------------------------------------------------------------------------------------------------------------
static void xml_text (Out &o, const char *s) { put_xml (o, s); }
static void col_letters (int c, char *out) { int n = 0; char t[4]; c++; while (c) { t[n++] = (char) ('A' + (c - 1) % 26); c = (c - 1) / 26; } int k = 0; while (n) out[k++] = t[--n]; out[k] = '\0'; }
// A shown amount ("1.234,56", "-12,00") as the number it is ("1234.56"); false when it is none.
static bool amount_value (const char *s, char *out)
{
	money v;
	if (!s[0] || !parse_money (s, &v)) return false;
	fmt_plain (v, out);
	return true;
}
// Styles: 0 normal, 1 bold, 2 money, 3 money bold, 4 title (bold, larger), 5 header (bold, shaded, a line
// below), 6 heading (bold, shaded), 7 money on a heading's shade, 8 dim (italic grey).
static const char XLSX_STYLES[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
	"<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
	"<numFmts count=\"1\"><numFmt numFmtId=\"164\" formatCode=\"#,##0.00\"/></numFmts>"
	"<fonts count=\"4\"><font><sz val=\"10\"/><name val=\"Liberation Sans\"/></font><font><b/><sz val=\"10\"/><name val=\"Liberation Sans\"/></font>"
	"<font><b/><sz val=\"14\"/><name val=\"Liberation Sans\"/></font><font><i/><sz val=\"10\"/><color rgb=\"FF696969\"/><name val=\"Liberation Sans\"/></font></fonts>"
	"<fills count=\"4\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
	"<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFCDD9E6\"/><bgColor indexed=\"64\"/></patternFill></fill>"
	"<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE8EDF3\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
	"<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
	"<border><left/><right/><top/><bottom style=\"thin\"><color auto=\"1\"/></bottom><diagonal/></border></borders>"
	"<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
	"<cellXfs count=\"9\">"
	"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
	"<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
	"<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
	"<xf numFmtId=\"164\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\"/>"
	"<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
	"<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\"/>"
	"<xf numFmtId=\"0\" fontId=\"1\" fillId=\"3\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\"/>"
	"<xf numFmtId=\"164\" fontId=\"1\" fillId=\"3\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\" applyFill=\"1\"/>"
	"<xf numFmtId=\"0\" fontId=\"3\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
	"</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>\n";

static void xl_cell (Out &o, int row, int col, const char *s, int style, bool number)
{
	char ref[12]; col_letters (col, ref); scat_num (ref, row, sizeof ref);
	o.puts ("<c r=\""); o.puts (ref); o.puts ("\"");
	if (style) { o.puts (" s=\""); char t[8]; itoa10 (style, t); o.puts (t); o.puts ("\""); }
	if (number) { o.puts ("><v>"); o.puts (s); o.puts ("</v></c>"); return; }
	o.puts (" t=\"inlineStr\"><is><t xml:space=\"preserve\">"); xml_text (o, s); o.puts ("</t></is></c>");
}
// The workbook's bytes (new []) -> its length.
static unsigned char *rpt_xlsx (const Book &b, const Report &p, unsigned *len)
{
	Out s;
	s.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
	s.puts ("<cols>");
	for (int c = 0; c < p.ncol; c++)
	{
		char t[16]; itoa10 (c + 1, t);
		s.puts ("<col min=\""); s.puts (t); s.puts ("\" max=\""); s.puts (t); s.puts ("\" width=\"");
		itoa10 (imax (8, p.col[c].width + 2), t); s.puts (t); s.puts ("\" customWidth=\"1\"/>");
	}
	s.puts ("</cols><sheetData>");
	char cl[160]; company_line (b, cl, sizeof cl);
	int row = 1;
	s.puts ("<row r=\"1\">"); xl_cell (s, 1, 0, p.title, 4, false); s.puts ("</row>"); row++;
	s.puts ("<row r=\"2\">"); { char t[400]; scpy (t, cl, sizeof t); if (p.sub[0]) { scat (t, "  -  ", sizeof t); scat (t, p.sub, sizeof t); } xl_cell (s, 2, 0, t, 8, false); } s.puts ("</row>");
	row = 4;
	{
		char t[12]; itoa10 (row, t);
		s.puts ("<row r=\""); s.puts (t); s.puts ("\">");
		for (int c = 0; c < p.ncol; c++) xl_cell (s, row, c, p.col[c].title, 5, false);
		s.puts ("</row>");
		row++;
	}
	for (int i = 0; i < p.nr; i++)
	{
		const RRow &r = p.r[i];
		if (r.style == RS_BLANK) { row++; continue; }
		char t[12]; itoa10 (row, t);
		s.puts ("<row r=\""); s.puts (t); s.puts ("\">");
		bool bold = r.style == RS_SUB || r.style == RS_TOTAL, head = r.style == RS_HEAD;
		for (int c = 0; c < p.ncol; c++)
		{
			const char *v = r.cell[c];
			if (!v[0] && !head) continue;
			char num[32];
			if (p.col[c].money && amount_value (v, num)) xl_cell (s, row, c, num, head || r.style == RS_TOTAL ? 7 : bold ? 3 : 2, true);
			else
			{
				char ind[200] = "";
				if (c == 0 && r.indent) { for (int k = 0; k < r.indent; k++) scat (ind, "    ", sizeof ind); }
				scat (ind, v, sizeof ind);
				xl_cell (s, row, c, ind, head || r.style == RS_TOTAL ? 6 : bold ? 1 : r.style == RS_DIM ? 8 : 0, false);
			}
		}
		s.puts ("</row>");
		row++;
	}
	s.puts ("</sheetData><pageSetup paperSize=\"9\" orientation=\""); s.puts (p.ncol >= 6 ? "landscape" : "portrait"); s.puts ("\"/></worksheet>\n");
	char sheetName[32]; int k = 0;
	for (const char *q = p.title; *q && k < 28; q++) if (*q != '/' && *q != '\\' && *q != '?' && *q != '*' && *q != '[' && *q != ']' && *q != ':' && *q != '\'') sheetName[k++] = *q;
	sheetName[k] = '\0'; if (!k) scpy (sheetName, TR ("Report"), sizeof sheetName);
	Out wb;
	wb.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
		 "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"");
	xml_text (wb, sheetName); wb.puts ("\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>\n");
	static const char CT[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
		"<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
		"<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
		"<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
		"<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/></Types>\n";
	static const char RELS[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>\n";
	static const char WBRELS[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
		"<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>\n";
	pngsave::ZipOut z;
	z.add ("[Content_Types].xml", CT, (unsigned) slen (CT), true);
	z.add ("_rels/.rels", RELS, (unsigned) slen (RELS), true);
	z.add ("xl/workbook.xml", wb.b, (unsigned) wb.n, true);
	z.add ("xl/_rels/workbook.xml.rels", WBRELS, (unsigned) slen (WBRELS), true);
	z.add ("xl/styles.xml", XLSX_STYLES, (unsigned) slen (XLSX_STYLES), true);
	z.add ("xl/worksheets/sheet1.xml", s.b, (unsigned) s.n, true);
	return z.finish (len);
}

// ---- files ---------------------------------------------------------------------------------------------------------------------------
enum { XF_RTF, XF_XLSX, XF_CSV };
static const char *const XF_EXT[3] = { ".rtf", ".xlsx", ".csv" };
// A file's name from a report's title and period: "General ledger 2026.rtf".
static void export_name (const Report &p, int fmt, char *out, int cap)
{
	int n = 0;
	for (const char *q = p.title; *q && n < cap - 16; q++)
	{
		unsigned char c = (unsigned char) *q;
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0 || c == ' ' || c == '-') out[n++] = (char) c;
		else if (c == '\'' || c == '(' || c == ')') {}
		else out[n++] = ' ';
	}
	while (n > 0 && out[n - 1] == ' ') n--;
	out[n] = '\0';
	if (g_year >= 0 && g_year < g_b.nyr) { char y[16]; year_label (g_b.yr[g_year], y, sizeof y); scat (out, " ", cap); scat (out, y, cap); }
	scat (out, XF_EXT[fmt], cap);
}
static bool write_report (const Report &p, int fmt, const char *path)
{
	if (fmt == XF_XLSX)
	{
		unsigned n; unsigned char *b = rpt_xlsx (g_b, p, &n);
		bool ok = kapi_save_file (path, b, n) >= 0;
		delete [] b;
		return ok;
	}
	Out o;
	if (fmt == XF_RTF) rpt_rtf (g_b, p, o); else rpt_csv (p, o);
	return kapi_save_file (path, o.b ? o.b : "", (unsigned) o.n) >= 0;
}
// The report written in SD:/docs/Reports and opened in Writer (RTF) or the Spreadsheet (XLSX).
static void open_report (const Report &p, int fmt)
{
	kapi_mkdir ("SD:/docs"); kapi_mkdir ("SD:/docs/Reports");
	char name[120], path[220];
	export_name (p, fmt, name, sizeof name);
	scpy (path, "SD:/docs/Reports/", sizeof path); scat (path, name, sizeof path);
	if (!write_report (p, fmt, path)) { warn (TR ("Export"), TR ("The file could not be written.")); return; }
	const char *app = fmt == XF_RTF ? "SD:/apps/writer.app/main" : "SD:/apps/sheet.app/main";
	if (!kapi_exec (app, path)) { warn (TR ("Export"), fmt == XF_RTF ? TR ("Writer could not be started.") : TR ("The Spreadsheet could not be started.")); return; }
	char m[240]; scpy (m, TR ("Opened: "), sizeof m); scat (m, path, sizeof m); status (m);
}
static void save_report (const Report &p, int fmt)
{
	char name[120], path[220];
	export_name (p, fmt, name, sizeof name);
	kapi_mkdir ("SD:/docs/Reports");
	if (!wk_file_save (path, sizeof path, "SD:/docs/Reports", name)) return;
	int n = slen (path), k = slen (XF_EXT[fmt]);
	if (n < k || !ci_eq (path + n - k, XF_EXT[fmt])) scat (path, XF_EXT[fmt], sizeof path);
	if (!write_report (p, fmt, path)) { warn (TR ("Export"), TR ("The file could not be written.")); return; }
	char m[240]; scpy (m, TR ("Written: "), sizeof m); scat (m, path, sizeof m); status (m);
}

} // namespace lg

#endif
