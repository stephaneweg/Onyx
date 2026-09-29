//
// odt.h -- OpenDocument text (.odt: LibreOffice's, ODF 1.3) read and written.
//
// Read: the styles (named and automatic ones, their parents, the default style, the font faces), the
// paragraphs and headings (alignment, margins, indent, spacing, line height, page breaks, keep
// options, widows / orphans, tab stops), the spans (fonts, sizes, weight, slant, underline,
// strike-through, super- / subscript, colours, backgrounds), the lists (bullets, numbers, their levels),
// the tables (columns' widths, spanned and covered cells, backgrounds, borders, heading rows), the
// images (frames), the fields (page number and count, date, time, a database's column: a mail merge's
// field), the table of contents (its entries), the master page (the page's size and margins, its
// header and footer -- the first page's own), the mail merge's data (a user field).
//
// Written: all of Writer's document -- its styles as LibreOffice names them (Standard, Heading 1...),
// each paragraph and span with an automatic style, lists as lists, tables, frames for the images
// (Pictures/), fields, the table of contents as an index, the master page with the headers and footers.
//
#ifndef _writer_odt_h
#define _writer_odt_h

#include "fileio.h"
#include "xml.h"

namespace wr {

static const char *const O_STYLE_NAME[ST_COUNT] = { "Standard", "Heading_20_1", "Heading_20_2", "Heading_20_3", "Title", "Subtitle", "Quotations",
						    "Preformatted_20_Text", "Contents_20_1", "Contents_20_2", "Contents_20_3", "Contents_20_Heading", "Header", "Footer" };
static const char *const O_STYLE_DISPLAY[ST_COUNT] = { "Standard", "Heading 1", "Heading 2", "Heading 3", "Title", "Subtitle", "Quotations",
						       "Preformatted Text", "Contents 1", "Contents 2", "Contents 3", "Contents Heading", "Header", "Footer" };

// A zip archive of an OpenDocument text (its mimetype, or its content.xml)?
static bool odt_is (const char *b, int n)
{
	if (n < 4 || b[0] != 'P' || b[1] != 'K') return false;
	const unsigned char *z = (const unsigned char *) b;
	int ml; char *m = zip_get (z, (unsigned) n, "mimetype", &ml);
	if (m) { bool t = strstr (m, "opendocument.text") != 0; delete[] m; return t; }
	return zip_has (z, (unsigned) n, "content.xml") && !zip_has (z, (unsigned) n, "word/document.xml");
}

// ---- reading ---------------------------------------------------------------------------------------------------
enum { OM_FONT = 1, OM_SIZE = 2, OM_COLOR = 4, OM_HILITE = 8, OM_FILL = 16, OM_BORDER = 32, OM_WIDTH = 64, OM_HEIGHT = 128 };
enum { OP_ALIGN = 1, OP_LEFT = 2, OP_RIGHT = 4, OP_FIRST = 8, OP_BEFORE = 16, OP_AFTER = 32, OP_LINE = 64, OP_KEEPN = 128,
       OP_KEEPL = 256, OP_WIDOW = 512, OP_BREAK = 1024, OP_TABS = 2048 };

struct OProps
{
	unsigned cm; char font[48]; short size; unsigned color, hilite; unsigned short on, off;
	unsigned pm; unsigned char align; short left, right, first, before, after, line; bool keepNext, keepLines, widow, pageBreak;
	int ntab; TabStop tab[MAXTABS];
	unsigned fill; int bw; unsigned bcolor; int width, height;	// a cell's background and border, a column's width, a row's height
	int pageStart;
};
static void oprops_init (OProps &p) { memset (&p, 0, sizeof p); p.pageStart = -1; }
static void oprops_apply (OProps &d, const OProps &s)
{
	if (s.cm & OM_FONT) scpy (d.font, s.font, sizeof d.font);
	if (s.cm & OM_SIZE) d.size = s.size;
	if (s.cm & OM_COLOR) d.color = s.color;
	if (s.cm & OM_HILITE) d.hilite = s.hilite;
	if (s.cm & OM_FILL) d.fill = s.fill;
	if (s.cm & OM_BORDER) { d.bw = s.bw; d.bcolor = s.bcolor; }
	if (s.cm & OM_WIDTH) d.width = s.width;
	if (s.cm & OM_HEIGHT) d.height = s.height;
	d.cm |= s.cm;
	d.on = (unsigned short) ((d.on & ~s.off) | s.on); d.off = (unsigned short) ((d.off & ~s.on) | s.off);
	if (s.pm & OP_ALIGN) d.align = s.align;
	if (s.pm & OP_LEFT) d.left = s.left;
	if (s.pm & OP_RIGHT) d.right = s.right;
	if (s.pm & OP_FIRST) d.first = s.first;
	if (s.pm & OP_BEFORE) d.before = s.before;
	if (s.pm & OP_AFTER) d.after = s.after;
	if (s.pm & OP_LINE) d.line = s.line;
	if (s.pm & OP_KEEPN) d.keepNext = s.keepNext;
	if (s.pm & OP_KEEPL) d.keepLines = s.keepLines;
	if (s.pm & OP_WIDOW) d.widow = s.widow;
	if (s.pm & OP_BREAK) d.pageBreak = s.pageBreak;
	if (s.pm & OP_TABS) { d.ntab = s.ntab; for (int i = 0; i < s.ntab; i++) d.tab[i] = s.tab[i]; }
	d.pm |= s.pm;
	if (s.pageStart >= 0) d.pageStart = s.pageStart;
}

struct OStyle { char name[64], parent[64], list[48], data[48]; int family; int st; OProps p; };	// family: 0 paragraph, 1 text, 2 other
struct OList { char name[48]; unsigned char kind[10]; short left[10], indent[10]; };
struct ODate { char name[48]; char pic[64]; };

static int o_style_by_name (const char *n)
{
	for (int i = 0; i < ST_COUNT; i++) if (!sicmp (n, O_STYLE_NAME[i]) || !sicmp (n, O_STYLE_DISPLAY[i])) return i;
	if (!sicmp (n, "Text_20_body") || !sicmp (n, "Text body") || !sicmp (n, "Body_20_Text") || !sicmp (n, "Default_20_Paragraph_20_Style")) return ST_NORMAL;
	if (!sicmp (n, "Heading_20_4") || !sicmp (n, "Heading_20_5") || !sicmp (n, "Heading_20_6")) return ST_H3;
	if (!sicmp (n, "Quotation") || !sicmp (n, "Quote")) return ST_QUOTE;
	if (!sicmp (n, "Plain_20_Text") || !sicmp (n, "Plain Text") || !sicmp (n, "Source_20_Text")) return ST_CODE;
	return -1;
}

struct OdtIn
{
	Doc &d;
	const unsigned char *z; unsigned zn;
	OStyle *sty; int nsty, stycap;
	OList *lst; int nlst, lstcap;
	ODate *dat; int ndat, datcap;
	struct Face { char name[48], family[48]; } *face; int nface, facecap;
	TableBuild *tb;
	int story;
	int listDepth; char listStyle[48];		// the lists being read: their depth, their style
	Para *q; OProps pp, rbase; int pst; unsigned short markCf;
	bool breakNext;

	OdtIn (Doc &d_, const unsigned char *z_, unsigned zn_) : d (d_), z (z_), zn (zn_), sty (0), nsty (0), stycap (0), lst (0), nlst (0), lstcap (0),
		dat (0), ndat (0), datcap (0), face (0), nface (0), facecap (0), tb (0), story (SY_BODY), listDepth (0), q (0), pst (-1), markCf (0), breakNext (false)
	{ listStyle[0] = 0; }
	~OdtIn () { delete[] sty; delete[] lst; delete[] dat; delete[] face; }

	// ---- styles ----
	const char *face_family (const char *n)
	{
		for (int i = 0; i < nface; i++) if (!strcmp (face[i].name, n)) return face[i].family;
		return n;
	}
	void font_faces (XmlReader &X)
	{
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is ("font-face-decls")) return; continue; }
			if (t != X_START || !X.is ("font-face")) continue;
			if (nface == facecap) { int c = facecap * 2 + 16; Face *nf = new Face[c]; for (int i = 0; i < nface; i++) nf[i] = face[i]; delete[] face; face = nf; facecap = c; }
			Face &f = face[nface++];
			X.attrs ("style:name", f.name, sizeof f.name);
			char fam[64]; X.attrs ("svg:font-family", fam, sizeof fam);
			int k = 0; for (int i = 0; fam[i] && k < 47; i++) if (fam[i] != '\'' && fam[i] != '"') f.family[k++] = fam[i];
			f.family[k] = 0;
			if (!k) scpy (f.family, f.name, sizeof f.family);
		}
	}
	void text_props (XmlReader &X, OProps &p)
	{
		char v[64];
		if (X.attrs ("style:font-name", v, sizeof v)) { scpy (p.font, face_family (v), sizeof p.font); p.cm |= OM_FONT; }
		else if (X.attrs ("fo:font-family", v, sizeof v)) { int k = 0; for (int i = 0; v[i] && k < 47; i++) if (v[i] != '\'' && v[i] != '"') p.font[k++] = v[i]; p.font[k] = 0; p.cm |= OM_FONT; }
		if (X.attrs ("fo:font-size", v, sizeof v) && !strchr (v, '%')) { p.size = (short) wclamp (len_twips (v) / 10, 2, 3276); p.cm |= OM_SIZE; }
		if (X.attrs ("fo:font-weight", v, sizeof v)) { bool b = !sicmp (v, "bold") || atoi (v) >= 600; if (b) { p.on |= CF_BOLD; p.off &= (unsigned short) ~CF_BOLD; } else { p.off |= CF_BOLD; p.on &= (unsigned short) ~CF_BOLD; } }
		if (X.attrs ("fo:font-style", v, sizeof v)) { bool b = !sicmp (v, "italic") || !sicmp (v, "oblique"); if (b) { p.on |= CF_ITALIC; p.off &= (unsigned short) ~CF_ITALIC; } else { p.off |= CF_ITALIC; p.on &= (unsigned short) ~CF_ITALIC; } }
		if (X.attrs ("style:text-underline-style", v, sizeof v)) { if (sicmp (v, "none")) { p.on |= CF_UNDER; p.off &= (unsigned short) ~CF_UNDER; } else { p.off |= CF_UNDER; p.on &= (unsigned short) ~CF_UNDER; } }
		if (X.attrs ("style:text-line-through-style", v, sizeof v)) { if (sicmp (v, "none")) { p.on |= CF_STRIKE; p.off &= (unsigned short) ~CF_STRIKE; } else { p.off |= CF_STRIKE; p.on &= (unsigned short) ~CF_STRIKE; } }
		if (X.attrs ("style:text-position", v, sizeof v))
		{
			p.off |= CF_SUPER | CF_SUB; p.on &= (unsigned short) ~(CF_SUPER | CF_SUB);
			if (!strncmp (v, "super", 5) || (v[0] >= '1' && v[0] <= '9')) { p.on |= CF_SUPER; p.off &= (unsigned short) ~CF_SUPER; }
			else if (!strncmp (v, "sub", 3) || v[0] == '-') { p.on |= CF_SUB; p.off &= (unsigned short) ~CF_SUB; }
		}
		if (X.attrs ("fo:color", v, sizeof v)) { p.color = hex_color (v); if (p.color == 0) p.color = AUTO; p.cm |= OM_COLOR; }
		if (X.attrs ("style:use-window-font-color", v, sizeof v) && !sicmp (v, "true")) { p.color = AUTO; p.cm |= OM_COLOR; }
		if (X.attrs ("fo:background-color", v, sizeof v)) { p.hilite = !sicmp (v, "transparent") ? AUTO : hex_color (v); p.cm |= OM_HILITE; }
	}
	void para_props (XmlReader &X, OProps &p)
	{
		char v[64];
		if (X.attrs ("fo:text-align", v, sizeof v))
		{
			p.align = (unsigned char) (!sicmp (v, "center") ? AL_CENTER : !sicmp (v, "end") || !sicmp (v, "right") ? AL_RIGHT : !sicmp (v, "justify") ? AL_JUSTIFY : AL_LEFT);
			p.pm |= OP_ALIGN;
		}
		if (X.attrs ("fo:margin-left", v, sizeof v) && !strchr (v, '%')) { p.left = (short) wclamp (len_twips (v), -5000, 30000); p.pm |= OP_LEFT; }
		if (X.attrs ("fo:margin-right", v, sizeof v) && !strchr (v, '%')) { p.right = (short) wclamp (len_twips (v), -5000, 30000); p.pm |= OP_RIGHT; }
		if (X.attrs ("fo:text-indent", v, sizeof v) && !strchr (v, '%')) { p.first = (short) wclamp (len_twips (v), -30000, 30000); p.pm |= OP_FIRST; }
		if (X.attrs ("fo:margin-top", v, sizeof v) && !strchr (v, '%')) { p.before = (short) wclamp (len_twips (v), 0, 30000); p.pm |= OP_BEFORE; }
		if (X.attrs ("fo:margin-bottom", v, sizeof v) && !strchr (v, '%')) { p.after = (short) wclamp (len_twips (v), 0, 30000); p.pm |= OP_AFTER; }
		if (X.attrs ("fo:line-height", v, sizeof v)) { if (strchr (v, '%')) p.line = (short) wclamp (atoi (v), 50, 400); else p.line = 100; p.pm |= OP_LINE; }
		if (X.attrs ("fo:break-before", v, sizeof v)) { p.pageBreak = !sicmp (v, "page"); p.pm |= OP_BREAK; }
		if (X.attrs ("fo:keep-with-next", v, sizeof v)) { p.keepNext = !sicmp (v, "always"); p.pm |= OP_KEEPN; }
		if (X.attrs ("fo:keep-together", v, sizeof v)) { p.keepLines = !sicmp (v, "always"); p.pm |= OP_KEEPL; }
		if (X.attrs ("fo:widows", v, sizeof v)) { p.widow = atoi (v) > 0; p.pm |= OP_WIDOW; }
		if (X.attrs ("style:page-number", v, sizeof v) && v[0] >= '0' && v[0] <= '9') p.pageStart = atoi (v);
		if (X.empty) return;
		for (int t; (t = X.next ()) != X_EOF; )				// (its tab stops)
		{
			if (t == X_END) { if (X.is ("paragraph-properties")) return; continue; }
			if (t != X_START) continue;
			if (X.is ("tab-stops")) { p.ntab = 0; p.pm |= OP_TABS; continue; }
			if (!X.is ("tab-stop") || p.ntab >= MAXTABS) continue;
			char ty[16], ls[16], lt[8];
			X.attrs ("style:position", v, sizeof v); X.attrs ("style:type", ty, sizeof ty);
			X.attrs ("style:leader-style", ls, sizeof ls); X.attrs ("style:leader-text", lt, sizeof lt);
			TabStop ts; ts.pos = len_twips (v);
			ts.align = (unsigned char) (!sicmp (ty, "center") ? TA_CENTER : !sicmp (ty, "right") ? TA_RIGHT : !sicmp (ty, "char") ? TA_DECIMAL : TA_LEFT);
			ts.leader = (unsigned char) (lt[0] == '.' || !sicmp (ls, "dotted") ? TL_DOT : lt[0] == '-' || !sicmp (ls, "dash") ? TL_DASH : lt[0] == '_' || !sicmp (ls, "solid") ? TL_LINE : TL_NONE);
			if (ts.pos < 0) continue;
			int k = 0; while (k < p.ntab && p.tab[k].pos < ts.pos) k++;
			for (int j = p.ntab; j > k; j--) p.tab[j] = p.tab[j - 1];
			p.tab[k] = ts; p.ntab++;
		}
	}
	static int border_width (const char *v, unsigned *color)	// "0.5pt solid #000000" -> eighths of a point
	{
		if (!v[0] || !sicmp (v, "none")) return 0;
		int w = len_twips (v);
		const char *h = strchr (v, '#');
		if (h) *color = hex_color (h);
		return wclamp (w * 2 / 5, 1, 96);
	}
	// A style element's children (X at its start): into s.
	void style_body (XmlReader &X, OProps &p, const char *end)
	{
		if (X.empty) { X.next (); return; }
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is (end)) return; continue; }
			if (t != X_START) continue;
			char v[64];
			if (X.is ("text-properties")) text_props (X, p);
			else if (X.is ("paragraph-properties")) { para_props (X, p); continue; }
			else if (X.is ("table-column-properties")) { if (X.attrs ("style:column-width", v, sizeof v)) { p.width = len_twips (v); p.cm |= OM_WIDTH; } }
			else if (X.is ("table-row-properties"))
			{
				if (X.attrs ("style:min-row-height", v, sizeof v) || X.attrs ("style:row-height", v, sizeof v)) { p.height = len_twips (v); p.cm |= OM_HEIGHT; }
			}
			else if (X.is ("table-cell-properties"))
			{
				if (X.attrs ("fo:background-color", v, sizeof v) && sicmp (v, "transparent")) { p.fill = hex_color (v); p.cm |= OM_FILL; }
				unsigned c = 0; int w = 0;
				if (X.attrs ("fo:border", v, sizeof v)) w = border_width (v, &c);
				static const char *const S[4] = { "fo:border-top", "fo:border-bottom", "fo:border-left", "fo:border-right" };
				for (int k = 0; k < 4 && !w; k++) if (X.attrs (S[k], v, sizeof v)) w = border_width (v, &c);
				if (w) { p.bw = w; p.bcolor = c; p.cm |= OM_BORDER; }
			}
			else if (X.is ("table-properties"))
			{
				if (X.attrs ("style:width", v, sizeof v)) { p.width = len_twips (v); p.cm |= OM_WIDTH; }
				if (X.attrs ("table:align", v, sizeof v)) { p.align = (unsigned char) (!sicmp (v, "center") ? AL_CENTER : !sicmp (v, "right") ? AL_RIGHT : AL_LEFT); p.pm |= OP_ALIGN; }
				if (X.attrs ("fo:margin-left", v, sizeof v)) { p.left = (short) len_twips (v); p.pm |= OP_LEFT; }
			}
			X.skip ();
		}
	}
	// The styles of a part's styles / automatic styles (X at their container's start).
	void styles (XmlReader &X, const char *end)
	{
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is (end)) return; continue; }
			if (t == X_START) style_elem (X);
		}
	}
	// One of them (X at its start): a style, a list style, a date / time style (else skipped).
	void style_elem (XmlReader &X)
	{
		{
			if (X.is ("style") || X.is ("default-style"))
			{
				if (nsty == stycap) { int c = stycap * 2 + 64; OStyle *ns = new OStyle[c]; for (int i = 0; i < nsty; i++) ns[i] = sty[i]; delete[] sty; sty = ns; stycap = c; }
				OStyle &s = sty[nsty++];
				bool dflt = X.is ("default-style");
				char fam[24]; X.attrs ("style:family", fam, sizeof fam);
				s.family = !sicmp (fam, "paragraph") ? 0 : !sicmp (fam, "text") ? 1 : 2;
				if (dflt) scpy (s.name, s.family == 0 ? "#default" : "#default-other", sizeof s.name); else X.attrs ("style:name", s.name, sizeof s.name);
				X.attrs ("style:parent-style-name", s.parent, sizeof s.parent);
				if (!s.parent[0] && !dflt && s.family == 0) scpy (s.parent, "#default", sizeof s.parent);
				X.attrs ("style:list-style-name", s.list, sizeof s.list);
				X.attrs ("style:data-style-name", s.data, sizeof s.data);
				char disp[64]; X.attrs ("style:display-name", disp, sizeof disp);
				s.st = dflt || s.family != 0 ? -1 : o_style_by_name (s.name);
				if (s.st < 0 && disp[0] && s.family == 0) s.st = o_style_by_name (disp);
				oprops_init (s.p);
				style_body (X, s.p, dflt ? "default-style" : "style");
				return;
			}
			if (X.is ("list-style"))
			{
				if (nlst == lstcap) { int c = lstcap * 2 + 8; OList *nl = new OList[c]; for (int i = 0; i < nlst; i++) nl[i] = lst[i]; delete[] lst; lst = nl; lstcap = c; }
				OList &l = lst[nlst++];
				X.attrs ("style:name", l.name, sizeof l.name);
				for (int i = 0; i < 10; i++) { l.kind[i] = LS_BULLET; l.left[i] = (short) (720 * (i + 1)); l.indent[i] = -360; }
				int lv = 0;
				for (int u; (u = X.next ()) != X_EOF; )
				{
					if (u == X_END) { if (X.is ("list-style")) break; continue; }
					if (u != X_START) continue;
					char v[24];
					if (X.is ("list-level-style-bullet") || X.is ("list-level-style-number") || X.is ("list-level-style-image"))
					{
						lv = wclamp ((int) X.attr_int ("text:level", 1) - 1, 0, 9);
						bool number = X.is ("list-level-style-number");
						if (number) { X.attrs ("style:num-format", v, sizeof v); if (!v[0]) number = false; }
						l.kind[lv] = (unsigned char) (number ? LS_NUMBER : LS_BULLET);
					}
					else if (X.is ("list-level-label-alignment"))
					{
						if (X.attrs ("fo:margin-left", v, sizeof v)) l.left[lv] = (short) len_twips (v);
						if (X.attrs ("fo:text-indent", v, sizeof v)) l.indent[lv] = (short) len_twips (v);
					}
				}
				return;
			}
			if (X.is ("date-style") || X.is ("time-style"))
			{
				if (ndat == datcap) { int c = datcap * 2 + 8; ODate *nd = new ODate[c]; for (int i = 0; i < ndat; i++) nd[i] = dat[i]; delete[] dat; dat = nd; datcap = c; }
				ODate &e = dat[ndat++];
				X.attrs ("style:name", e.name, sizeof e.name);
				int n = 0;
				auto put = [&] (const char *s) { while (*s && n < 62) e.pic[n++] = *s++; };
				const char *en = X.is ("date-style") ? "date-style" : "time-style";
				for (int u; (u = X.next ()) != X_EOF; )
				{
					if (u == X_END) { if (X.is (en)) break; continue; }
					if (u == X_TEXT) continue;
					char st[16]; X.attrs ("number:style", st, sizeof st);
					bool lg = !sicmp (st, "long");
					if (X.is ("day")) put (lg ? "dd" : "d");
					else if (X.is ("month")) { char tx[8]; X.attrs ("number:textual", tx, sizeof tx); put (!sicmp (tx, "true") ? (lg ? "MMMM" : "MMM") : (lg ? "MM" : "M")); }
					else if (X.is ("year")) put (lg ? "yyyy" : "yy");
					else if (X.is ("day-of-week")) put (lg ? "dddd" : "ddd");
					else if (X.is ("hours")) put (lg ? "HH" : "H");
					else if (X.is ("minutes")) put ("mm");
					else if (X.is ("seconds")) put ("ss");
					else if (X.is ("am-pm")) put ("AM/PM");
					else if (X.is ("text"))
					{
						XBuf b;
						for (int w; (w = X.next ()) != X_EOF; ) { if (w == X_TEXT) b.putn (X.text.b, X.text.n); else if (w == X_END) break; }
						bool letters = false;			// (quoted only when a picture's letter is in it)
						for (int i = 0; i < b.n; i++) { char c = b.b[i]; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\\') letters = true; }
						if (letters && n < 62) e.pic[n++] = '\'';
						for (int i = 0; i < b.n && n < 61; i++) if (b.b[i] != '\'') e.pic[n++] = b.b[i];
						if (letters && n < 62) e.pic[n++] = '\'';
						continue;
					}
					X.skip ();
				}
				e.pic[n] = 0;
				return;
			}
			X.skip ();
		}
	}
	int style_index (const char *name, int family)
	{
		for (int i = nsty - 1; i >= 0; i--) if (sty[i].family == family && !strcmp (sty[i].name, name)) return i;	// (the automatic ones last: first found)
		return -1;
	}
	void chain (int si, OProps &out, int depth = 0)
	{
		if (si < 0 || depth > 16) return;
		if (sty[si].parent[0]) chain (style_index (sty[si].parent, sty[si].family), out, depth + 1);
		oprops_apply (out, sty[si].p);
	}
	int our_style (int si, int depth = 0)
	{
		if (si < 0 || depth > 16) return -1;
		if (sty[si].st >= 0) return sty[si].st;
		return sty[si].parent[0] ? our_style (style_index (sty[si].parent, sty[si].family), depth + 1) : -1;
	}
	const char *list_style_of (int si, int depth = 0)
	{
		if (si < 0 || depth > 16) return "";
		if (sty[si].list[0]) return sty[si].list;
		return sty[si].parent[0] ? list_style_of (style_index (sty[si].parent, sty[si].family), depth + 1) : "";
	}
	const OList *list_named (const char *n) { for (int i = nlst - 1; i >= 0; i--) if (!strcmp (lst[i].name, n)) return &lst[i]; return 0; }
	const char *date_pic (const char *n) { for (int i = 0; i < ndat; i++) if (!strcmp (dat[i].name, n)) return dat[i].pic; return ""; }

	// ---- the text ----
	unsigned short cf_of (const OProps &p)
	{
		CharFmt f;
		f.font = (short) doc_font (d, p.cm & OM_FONT && p.font[0] ? p.font : "Liberation Serif");
		f.size = p.cm & OM_SIZE ? p.size : 24;
		f.flags = (unsigned short) (p.on & (CF_BOLD | CF_ITALIC | CF_UNDER | CF_STRIKE | CF_SUPER | CF_SUB));
		f.color = p.cm & OM_COLOR ? p.color : AUTO;
		f.hilite = p.cm & OM_HILITE ? p.hilite : AUTO;
		return doc_fmt (d, f);
	}
	void para_begin (const char *style, int outline)
	{
		int si = style[0] ? style_index (style, 0) : -1;
		if (si < 0) si = style_index ("Standard", 0);
		oprops_init (pp); chain (si, pp);
		pst = our_style (si);
		if (outline >= 1 && (pst < ST_H1 || pst > ST_H3)) pst = ST_H1 + wmin (outline, 3) - 1;
		rbase = pp; rbase.pm = 0; rbase.ntab = 0;
		markCf = cf_of (rbase);
		if (pp.pageStart >= 0 && story == SY_BODY) { d.page.start = pp.pageStart; pp.pageStart = -1; }
		// (a list's paragraph: its list's kind, level, indents -- unless its own say)
		q = para_new ();
		ParaFmt pf = style_para (pst < 0 ? ST_NORMAL : pst);
		pf.align = AL_LEFT; pf.left = pf.right = pf.first = 0; pf.before = pf.after = 0; pf.line = 100;
		pf.keepNext = pf.keepLines = pf.pageBreak = false; pf.widow = true;
		if (pp.pm & OP_ALIGN) pf.align = pp.align;
		if (pp.pm & OP_LEFT) pf.left = (short) wmax (0, (int) pp.left);
		if (pp.pm & OP_RIGHT) pf.right = (short) wmax (0, (int) pp.right);
		if (pp.pm & OP_FIRST) pf.first = pp.first;
		if (pp.pm & OP_BEFORE) pf.before = pp.before;
		if (pp.pm & OP_AFTER) pf.after = pp.after;
		if (pp.pm & OP_LINE) pf.line = pp.line;
		if (pp.pm & OP_KEEPN) pf.keepNext = pp.keepNext;
		if (pp.pm & OP_KEEPL) pf.keepLines = pp.keepLines;
		if (pp.pm & OP_WIDOW) pf.widow = pp.widow;
		if (pp.pm & OP_BREAK) pf.pageBreak = pp.pageBreak;
		if (listDepth > 0)
		{
			const OList *l = list_named (listStyle[0] ? listStyle : list_style_of (si));
			int lv = wclamp (listDepth - 1, 0, 9);
			pf.list = (unsigned char) (l ? l->kind[lv] : LS_BULLET); pf.level = (unsigned char) wmin (lv, 5);
			if (!(pp.pm & OP_LEFT)) pf.left = l ? l->left[lv] : (short) (720 * (lv + 1));
			if (!(pp.pm & OP_FIRST)) pf.first = l ? l->indent[lv] : (short) -360;
		}
		// (tab stops: from the paragraph's indent in ODF, from the text's edge here)
		pf.ntab = 0;
		for (int i = 0; i < pp.ntab && pf.ntab < MAXTABS; i++) { TabStop t = pp.tab[i]; t.pos += pf.left; if (t.pos > 0) pf.tab[pf.ntab++] = t; }
		if (pf.first < -pf.left) pf.first = (short) -pf.left;
		if (breakNext) { pf.pageBreak = true; breakNext = false; }
		q->pf = pf;
	}
	void para_end ()
	{
		q->endCf = q->len ? q->cf[q->len - 1] : markCf;
		{ CharFmt e = d.fmt[q->endCf]; if (e.obj || e.fld) { e.obj = 0; e.ow = e.oh = 0; e.fld = 0; q->endCf = doc_fmt (d, e); } }
		if (tb) tb->para (q); else story_append (d, story, q);
		q = 0;
	}
	void put (unsigned c, unsigned short cf) { para_insert (q, q->len, &c, 1, cf); }
	void field (int kind, const char *arg, unsigned short cf)
	{
		CharFmt f = d.fmt[cf]; f.fld = doc_field (d, kind, arg);
		put (FIELD_CHAR, doc_fmt (d, f));
	}
	// Text as ODF lays it: white space collapsed to one space.
	void text (const char *s, int n, unsigned short cf)
	{
		unsigned *u = new unsigned[n + 1];
		int m = utf8_decode (s, n, u, n + 1);
		for (int i = 0; i < m; i++)
		{
			unsigned c = u[i];
			if (c == '\t' || c == '\n' || c == '\r') c = ' ';
			if (c == ' ' && (q->len == 0 || q->ch[q->len - 1] == ' ')) continue;
			if (c < 32) continue;
			put (c, cf);
		}
		delete[] u;
	}
	void frame (XmlReader &X, unsigned short cf)
	{
		char v[64]; int w = 0, h = 0;
		if (X.attrs ("svg:width", v, sizeof v)) w = len_twips (v);
		if (X.attrs ("svg:height", v, sizeof v)) h = len_twips (v);
		char href[200] = "";
		for (int depth = 1; depth > 0; )
		{
			int t = X.next ();
			if (t == X_EOF) break;
			if (t == X_END) { depth--; continue; }
			if (t != X_START) continue;
			depth++;
			if (X.is ("image") && !href[0]) X.attrs ("xlink:href", href, sizeof href);
		}
		if (!href[0] || strstr (href, "://")) return;
		const char *p = href; if (p[0] == '.' && p[1] == '/') p += 2;
		int n; char *b = zip_get (z, zn, p, &n);
		if (!b) return;
		int img = image_from_bytes (d, (const unsigned char *) b, (unsigned) n);
		delete[] b;
		if (!img) return;
		CharFmt f = d.fmt[cf]; f.obj = img;
		f.ow = w > 0 ? w : d.img[img - 1].w * 15; f.oh = h > 0 ? h : d.img[img - 1].h * 15;
		put (OBJ_CHAR, doc_fmt (d, f));
	}
	// A paragraph's content, to the end of the element `end` (spans nest their formats).
	void inl (XmlReader &X, const char *end, const OProps &base)
	{
		unsigned short cf = cf_of (base);
		if (X.empty) { X.next (); return; }
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_TEXT) { text (X.text.b, X.text.n, cf); continue; }
			if (t == X_END) { if (X.is (end)) return; continue; }
			if (t != X_START) continue;
			char v[64];
			if (X.is ("span"))
			{
				OProps sp = base;
				if (X.attrs ("text:style-name", v, sizeof v)) chain (style_index (v, 1), sp);
				inl (X, "span", sp);
			}
			else if (X.is ("s")) { int c = (int) X.attr_int ("text:c", 1); for (int i = 0; i < wclamp (c, 1, 200); i++) put (' ', cf); X.skip (); }
			else if (X.is ("tab")) { put ('\t', cf); X.skip (); }
			else if (X.is ("line-break")) { put (0x0B, cf); X.skip (); }
			else if (X.is ("a") || X.is ("sequence") || X.is ("ruby") || X.is ("ruby-base") || X.is ("meta") || X.is ("text-input") || X.is ("placeholder") || X.is ("user-defined") || X.is ("chapter") || X.is ("sender-firstname"))
			{ char e[24]; int l = 0; for (int i = 0; i < X.nml && l < 23; i++) { if (X.nm[i] == ':') l = 0; else e[l++] = X.nm[i]; } e[l] = 0; inl (X, e, base); }
			else if (X.is ("page-number")) { field (FK_PAGE, "", cf); X.skip (); }
			else if (X.is ("page-count")) { field (FK_PAGES, "", cf); X.skip (); }
			else if (X.is ("date") || X.is ("time"))
			{
				bool date = X.is ("date");
				char ds[48]; X.attrs ("style:data-style-name", ds, sizeof ds);
				const char *pic = ds[0] ? date_pic (ds) : "";
				field (date ? FK_DATE : FK_TIME, pic[0] ? pic : (date ? "dd/MM/yyyy" : "HH:mm"), cf);
				X.skip ();
			}
			else if (X.is ("database-display")) { if (X.attrs ("text:column-name", v, sizeof v) && v[0]) field (FK_MERGE, v, cf); X.skip (); }
			else if (X.is ("frame")) frame (X, cf);
			else X.skip ();
		}
	}
	void paragraph (XmlReader &X, bool heading)
	{
		char st[64]; X.attrs ("text:style-name", st, sizeof st);
		int outline = heading ? (int) X.attr_int ("text:outline-level", 1) : 0;
		para_begin (st, outline);
		inl (X, heading ? "h" : "p", rbase);
		while (q->len > 0 && q->ch[q->len - 1] == ' ') q->len--;	// (collapsed: no space at the end)
		para_end ();
	}
	// Block content (paragraphs, headings, lists, tables, sections, indexes) to the end of `end`.
	void block (XmlReader &X, const char *end)
	{
		if (X.empty) { X.next (); return; }
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is (end)) return; continue; }
			if (t != X_START) continue;
			if (X.is ("p")) paragraph (X, false);
			else if (X.is ("h")) paragraph (X, true);
			else if (X.is ("list"))
			{
				char keep[48]; scpy (keep, listStyle, sizeof keep);
				char v[48]; if (X.attrs ("text:style-name", v, sizeof v)) scpy (listStyle, v, sizeof listStyle);
				listDepth++;
				block (X, "list");
				listDepth--;
				scpy (listStyle, keep, sizeof listStyle);
			}
			else if (X.is ("list-item") || X.is ("list-header")) block (X, X.is ("list-item") ? "list-item" : "list-header");
			else if (X.is ("table")) table (X);
			else if (X.is ("table-of-content") || X.is ("alphabetical-index") || X.is ("illustration-index") || X.is ("user-index") || X.is ("table-index") || X.is ("object-index") || X.is ("bibliography"))
			{
				char e[32]; int l = 0; for (int i = 0; i < X.nml && l < 31; i++) { if (X.nm[i] == ':') l = 0; else e[l++] = X.nm[i]; } e[l] = 0;
				for (int u; (u = X.next ()) != X_EOF; )				// (its source: skipped; its body read)
				{
					if (u == X_END) { if (X.is (e)) break; continue; }
					if (u != X_START) continue;
					if (X.is ("index-body")) block (X, "index-body"); else X.skip ();
				}
			}
			else if (X.is ("section") || X.is ("index-title"))
			{
				char e[24]; int l = 0; for (int i = 0; i < X.nml && l < 23; i++) { if (X.nm[i] == ':') l = 0; else e[l++] = X.nm[i]; } e[l] = 0;
				block (X, e);
			}
			else if (X.is ("user-field-decls"))
			{
				for (int u; (u = X.next ()) != X_EOF; )
				{
					if (u == X_END) { if (X.is ("user-field-decls")) break; continue; }
					if (u != X_START || !X.is ("user-field-decl")) continue;
					char nm[64]; X.attrs ("text:name", nm, sizeof nm);
					if (!sicmp (nm, "OnyxMergeSource")) X.attrs ("office:string-value", d.mergeSrc, sizeof d.mergeSrc);
				}
			}
			else if (X.is ("soft-page-break") || X.is ("sequence-decls") || X.is ("variable-decls") || X.is ("tracked-changes") || X.is ("forms")) X.skip ();
			else X.skip ();
		}
	}
	void table (XmlReader &X)
	{
		if (tb)								// (a table in a table: its cells' paragraphs in the cell)
		{
			for (int t; (t = X.next ()) != X_EOF; )
			{
				if (t == X_END) { if (X.is ("table")) return; continue; }
				if (t != X_START) continue;
				if (X.is ("table-cell")) block (X, "table-cell");
				else if (X.is ("table-column") || X.is ("covered-table-cell")) X.skip ();
			}
			return;
		}
		TableBuild t; t.init ();
		char v[64];
		if (X.attrs ("table:style-name", v, sizeof v))
		{
			OProps tp; oprops_init (tp); chain (style_index (v, 2), tp);
			if (tp.pm & OP_ALIGN) t.align = tp.align;
			if ((tp.pm & OP_LEFT) && t.align == AL_LEFT) t.indent = tp.left;
		}
		tb = &t;
		bool anyBorder = false; int bw = 4; unsigned bc = 0;
		int col = 0, inHeader = 0;
		for (int u; (u = X.next ()) != X_EOF; )
		{
			if (u == X_END) { if (X.is ("table")) break; if (X.is ("table-header-rows")) inHeader = 0; continue; }
			if (u != X_START) continue;
			if (X.is ("table-column"))
			{
				int rep = (int) wclamp (X.attr_int ("table:number-columns-repeated", 1), 1L, 64L);
				int w = 0;
				if (X.attrs ("table:style-name", v, sizeof v)) { OProps cp; oprops_init (cp); chain (style_index (v, 2), cp); if (cp.cm & OM_WIDTH) w = cp.width; }
				for (int k = 0; k < rep && t.ncols < MAXCOLS; k++) t.colW[t.ncols++] = w > 0 ? w : 0;
				X.skip ();
				continue;
			}
			if (X.is ("table-header-rows")) { inHeader = 1; t.header = t.nr == 0; continue; }
			if (X.is ("table-rows") || X.is ("table-row-group") || X.is ("table-columns") || X.is ("table-column-group")) continue;
			if (X.is ("table-row"))
			{
				int h = 0;
				if (X.attrs ("table:style-name", v, sizeof v)) { OProps rp; oprops_init (rp); chain (style_index (v, 2), rp); if (rp.cm & OM_HEIGHT) h = rp.height; }
				t.row (h); col = 0;
				continue;
			}
			if (X.is ("covered-table-cell")) { col += (int) wclamp (X.attr_int ("table:number-columns-repeated", 1), 1L, 64L); X.skip (); continue; }
			if (X.is ("table-cell"))
			{
				int cs = (int) wclamp (X.attr_int ("table:number-columns-spanned", 1), 1L, 63L);
				int rs = (int) wclamp (X.attr_int ("table:number-rows-spanned", 1), 1L, 255L);
				int rep = (int) wclamp (X.attr_int ("table:number-columns-repeated", 1), 1L, 64L);
				unsigned fill = AUTO;
				if (X.attrs ("table:style-name", v, sizeof v))
				{
					OProps cp; oprops_init (cp); chain (style_index (v, 2), cp);
					if (cp.cm & OM_FILL) fill = cp.fill;
					if (cp.cm & OM_BORDER) { anyBorder = true; bw = cp.bw; bc = cp.bcolor; }
				}
				t.cell (col, cs, false, rs, fill);
				block (X, "table-cell");
				for (int k = 1; k < rep; k++) t.cell (col + k, cs, false, rs, fill);
				col += rep;
				continue;
			}
			X.skip ();
		}
		(void) inHeader;
		// (columns with no width: the rest of the text's width shared)
		int text = d.page.w - d.page.left - d.page.right, known = 0, unknown = 0;
		if (t.ncols == 0) { int w = 0; for (int r = 0; r < t.nr; r++) { int e = 0; for (int k = 0; k < t.rows[r].n; k++) e = wmax (e, t.rows[r].c[k].col + t.rows[r].c[k].cs); w = wmax (w, e); } t.ncols = wclamp (w, 1, (int) MAXCOLS); for (int c = 0; c < t.ncols; c++) t.colW[c] = 0; }
		for (int c = 0; c < t.ncols; c++) { if (t.colW[c] > 0) known += t.colW[c]; else unknown++; }
		for (int c = 0; c < t.ncols; c++) if (t.colW[c] <= 0) t.colW[c] = wmax (600, (text - known) / wmax (1, unknown));
		t.border = anyBorder ? TB_ALL : TB_NONE; t.bw = (unsigned char) bw; t.bcolor = bc;
		tb = 0;
		t.finish (d, story);
	}
	// The master page's header / footer (X at its start) into story s.
	void hf (XmlReader &X, int s, const char *end)
	{
		int sn; story_p (d, s, &sn);
		if (sn > 0) { X.skip (); return; }
		story = s;
		block (X, end);
		story = SY_BODY;
	}
};

static bool odt_load (Doc &d, const char *b, int n)
{
	doc_clear (d);
	const unsigned char *z = (const unsigned char *) b;
	OdtIn in (d, z, (unsigned) n);
	PageSetup &pg = d.page;
	pg.w = A4_W; pg.h = A4_H; pg.top = pg.bottom = pg.left = pg.right = 1134; pg.hdr = pg.ftr = 1134;
	// styles.xml: the fonts, the named styles, the page layouts, the master page
	int sn; char *sx = zip_get (z, (unsigned) n, "styles.xml", &sn);
	int cn; char *cx = zip_get (z, (unsigned) n, "content.xml", &cn);
	if (!cx) { delete[] sx; return false; }
	struct Layout0 { char name[48]; int w, h, top, bottom, left, right; bool land; int hMin, hSp, fMin, fSp; bool hOn, fOn; } lay[8]; int nlay = 0;
	char master[48] = "";
	if (sx)
	{
		XmlReader X (sx, sn);
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t != X_START) continue;
			if (X.is ("font-face-decls")) in.font_faces (X);
			else if (X.is ("styles")) in.styles (X, "styles");
			else if (X.is ("automatic-styles"))
			{
				for (int u; (u = X.next ()) != X_EOF; )
				{
					if (u == X_END) { if (X.is ("automatic-styles")) break; continue; }
					if (u != X_START) continue;
					if (X.is ("page-layout") && nlay < 8)
					{
						Layout0 &l = lay[nlay++];
						X.attrs ("style:name", l.name, sizeof l.name);
						l.w = A4_W; l.h = A4_H; l.top = l.bottom = l.left = l.right = 1134; l.land = false; l.hMin = l.hSp = l.fMin = l.fSp = 0; l.hOn = l.fOn = false;
						int which = 0;
						for (int w; (w = X.next ()) != X_EOF; )
						{
							if (w == X_END) { if (X.is ("page-layout")) break; continue; }
							if (w != X_START) continue;
							char v[32];
							if (X.is ("page-layout-properties"))
							{
								if (X.attrs ("fo:page-width", v, sizeof v)) l.w = len_twips (v);
								if (X.attrs ("fo:page-height", v, sizeof v)) l.h = len_twips (v);
								if (X.attrs ("fo:margin-top", v, sizeof v)) l.top = len_twips (v);
								if (X.attrs ("fo:margin-bottom", v, sizeof v)) l.bottom = len_twips (v);
								if (X.attrs ("fo:margin-left", v, sizeof v)) l.left = len_twips (v);
								if (X.attrs ("fo:margin-right", v, sizeof v)) l.right = len_twips (v);
								if (X.attrs ("style:print-orientation", v, sizeof v)) l.land = !sicmp (v, "landscape");
							}
							else if (X.is ("header-style")) which = 1;
							else if (X.is ("footer-style")) which = 2;
							else if (X.is ("header-footer-properties") && which)
							{
								int mn = X.attrs ("fo:min-height", v, sizeof v) ? len_twips (v) : 0;
								int sp = X.attrs (which == 1 ? "fo:margin-bottom" : "fo:margin-top", v, sizeof v) ? len_twips (v) : 0;
								if (which == 1) { l.hMin = mn; l.hSp = sp; l.hOn = true; } else { l.fMin = mn; l.fSp = sp; l.fOn = true; }
							}
						}
						continue;
					}
					in.style_elem (X);				// (the headers' own automatic styles)
				}
			}
			else if (X.is ("master-page") && !master[0])
			{
				char ml[48]; X.attrs ("style:page-layout-name", ml, sizeof ml); scpy (master, ml, sizeof master);
				for (int u; (u = X.next ()) != X_EOF; )
				{
					if (u == X_END) { if (X.is ("master-page")) break; continue; }
					if (u != X_START) continue;
					if (X.is ("header")) in.hf (X, SY_HEADER, "header");
					else if (X.is ("footer")) in.hf (X, SY_FOOTER, "footer");
					else if (X.is ("header-first")) { in.hf (X, SY_HEADER1, "header-first"); pg.titlePg = true; }
					else if (X.is ("footer-first")) { in.hf (X, SY_FOOTER1, "footer-first"); pg.titlePg = true; }
					else X.skip ();
				}
			}
		}
	}
	for (int i = 0; i < nlay; i++)
		if (!master[0] || !strcmp (lay[i].name, master))
		{
			const Layout0 &l = lay[i];
			pg.w = l.w; pg.h = l.h; pg.left = l.left; pg.right = l.right;
			if (l.land && pg.w < pg.h) { int t = pg.w; pg.w = pg.h; pg.h = t; }
			pg.top = l.top; pg.bottom = l.bottom;
			bool hOn = l.hOn && (!story_empty (d, SY_HEADER) || !story_empty (d, SY_HEADER1));
			bool fOn = l.fOn && (!story_empty (d, SY_FOOTER) || !story_empty (d, SY_FOOTER1));
			if (hOn) { pg.hdr = l.top; pg.top = l.top + l.hMin + l.hSp; } else pg.hdr = wmin (709, l.top);
			if (fOn) { pg.ftr = l.bottom; pg.bottom = l.bottom + l.fMin + l.fSp; } else pg.ftr = wmin (709, l.bottom);
			break;
		}
	delete[] sx;
	// content.xml: its fonts, automatic styles, body
	XmlReader X (cx, cn);
	for (int t; (t = X.next ()) != X_EOF; )
	{
		if (t != X_START) continue;
		if (X.is ("font-face-decls")) in.font_faces (X);
		else if (X.is ("automatic-styles")) in.styles (X, "automatic-styles");
		else if (X.is ("text") && X.isq ("office:text")) { in.block (X, "text"); break; }
	}
	delete[] cx;
	int bn; story_p (d, SY_BODY, &bn);
	if (bn == 0) story_append (d, SY_BODY, para_styled (d, ST_NORMAL));
	doc_fix (d);
	pg.w = wclamp (pg.w, 2880, 40000); pg.h = wclamp (pg.h, 2880, 40000);
	pg.left = wclamp (pg.left, 0, pg.w / 3); pg.right = wclamp (pg.right, 0, pg.w / 3);
	pg.top = wclamp (pg.top, 0, pg.h / 3); pg.bottom = wclamp (pg.bottom, 0, pg.h / 3);
	pg.hdr = wclamp (pg.hdr, 0, pg.h / 3); pg.ftr = wclamp (pg.ftr, 0, pg.h / 3);
	return true;
}

// ---- writing ------------------------------------------------------------------------------------------------------
static const char *O_NS = " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
			  " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
			  " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
			  " xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
			  " xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\" xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
			  " xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:loext=\"urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0\" office:version=\"1.3\"";

// The automatic styles a part uses (the paragraphs' formats, the spans', the tables', the date
// pictures): each one written once, named by its prefix and number.
struct OdtOut
{
	Doc &d;
	const char *pre;			// "P" / "T"... for content.xml, "MP" / "MT"... for styles.xml
	XBuf st;				// the automatic styles
	struct PKey { ParaFmt pf; bool list; } *pk; int npk, pkcap;
	unsigned short *tk; int ntk, tkcap;
	char (*dk)[64]; int ndk, dkcap;
	int ntbl, nimg, nlist;
	int *imgNo; int *imgCount; bool *imgJpeg;	// (shared: the images' parts)
	int pageStart;					// the first paragraph's page number (-1: none to say)
	OdtOut (Doc &d_, const char *p, int *ino, int *icount) : d (d_), pre (p), pk (0), npk (0), pkcap (0), tk (0), ntk (0), tkcap (0), dk (0), ndk (0), dkcap (0),
		ntbl (0), nimg (0), nlist (0), imgNo (ino), imgCount (icount), imgJpeg (0), pageStart (-1) {}
	~OdtOut () { delete[] pk; delete[] tk; delete[] dk; }

	void name (XBuf &o, const char *kind, int n) { o.puts (pre); o.puts (kind); o.num (n); }
	void len (XBuf &o, const char *attr, int tw) { o.put (' '); o.puts (attr); o.puts ("=\""); twips_cm (o, tw); o.put ('"'); }
	// A span's style (its number).
	int text_style (unsigned short cf)
	{
		const CharFmt &f0 = d.fmt[cf];
		for (int i = 0; i < ntk; i++) { CharFmt f = d.fmt[tk[i]]; f.obj = f0.obj; f.ow = f0.ow; f.oh = f0.oh; f.fld = f0.fld; if (f.same (f0)) return i + 1; }
		if (ntk == tkcap) { int c = tkcap * 2 + 16; unsigned short *n = new unsigned short[c]; for (int i = 0; i < ntk; i++) n[i] = tk[i]; delete[] tk; tk = n; tkcap = c; }
		tk[ntk++] = cf;
		const CharFmt &f = f0;
		st.puts ("<style:style style:name=\""); name (st, "T", ntk); st.puts ("\" style:family=\"text\"><style:text-properties style:font-name=\"");
		xml_esc1 (st, d.fontName[f.font]); st.puts ("\" fo:font-size=\""); st.num (f.size / 2); if (f.size & 1) st.puts (".5"); st.puts ("pt\"");
		st.puts (f.flags & CF_BOLD ? " fo:font-weight=\"bold\"" : " fo:font-weight=\"normal\"");
		st.puts (f.flags & CF_ITALIC ? " fo:font-style=\"italic\"" : " fo:font-style=\"normal\"");
		st.puts (f.flags & CF_UNDER ? " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"" : " style:text-underline-style=\"none\"");
		st.puts (f.flags & CF_STRIKE ? " style:text-line-through-style=\"solid\"" : " style:text-line-through-style=\"none\"");
		if (f.flags & (CF_SUPER | CF_SUB)) st.puts (f.flags & CF_SUPER ? " style:text-position=\"super 58%\"" : " style:text-position=\"sub 58%\"");
		if (f.color != AUTO) { st.puts (" fo:color=\"#"); color_hex (st, f.color); st.put ('"'); }
		else st.puts (" style:use-window-font-color=\"true\"");
		if (f.hilite != AUTO) { st.puts (" fo:background-color=\"#"); color_hex (st, f.hilite); st.put ('"'); }
		st.puts ("/></style:style>");
		return ntk;
	}
	// A paragraph's style (its number); list: in a list (its indents the list's).
	int para_style (const ParaFmt &pf0, bool list)
	{
		ParaFmt pf = pf0; pf.tbl = pf.row = pf.col = 0;
		int start = pageStart; pageStart = -1;
		if (start < 0) for (int i = 0; i < npk; i++) if (pk[i].pf.same (pf) && pk[i].list == list) return i + 1;
		if (npk == pkcap) { int c = pkcap * 2 + 16; PKey *n = new PKey[c]; for (int i = 0; i < npk; i++) n[i] = pk[i]; delete[] pk; pk = n; pkcap = c; }
		pk[npk].pf = pf; pk[npk].list = list; if (start >= 0) pk[npk].pf.style = 255; npk++;	// (the first page's: its own)
		st.puts ("<style:style style:name=\""); name (st, "P", npk); st.puts ("\" style:family=\"paragraph\" style:parent-style-name=\"");
		st.puts (O_STYLE_NAME[pf.style < ST_COUNT ? pf.style : 0]); st.puts ("\"");
		if (list) { st.puts (" style:list-style-name=\""); st.puts (pf.list == LS_NUMBER ? "LN" : "LB"); st.puts ("\""); }
		if (start >= 0) st.puts (" style:master-page-name=\"Standard\"");
		st.puts ("><style:paragraph-properties");
		if (start >= 0) { st.puts (" style:page-number=\""); st.num (start); st.puts ("\""); }
		static const char *const al[4] = { "start", "center", "end", "justify" };
		st.puts (" fo:text-align=\""); st.puts (al[pf.align & 3]); st.puts ("\"");
		len (st, "fo:margin-left", pf.left); len (st, "fo:margin-right", pf.right); len (st, "fo:text-indent", pf.first);
		len (st, "fo:margin-top", pf.before); len (st, "fo:margin-bottom", pf.after);
		st.puts (" fo:line-height=\""); st.num (pf.line); st.puts ("%\"");
		if (pf.pageBreak) st.puts (" fo:break-before=\"page\"");
		st.puts (pf.keepNext ? " fo:keep-with-next=\"always\"" : " fo:keep-with-next=\"auto\"");
		if (pf.keepLines) st.puts (" fo:keep-together=\"always\"");
		st.puts (pf.widow ? " fo:widows=\"2\" fo:orphans=\"2\"" : " fo:widows=\"0\" fo:orphans=\"0\"");
		st.puts (" style:auto-text-indent=\"false\"");
		if (pf.ntab)
		{
			st.puts ("><style:tab-stops>");
			static const char *const ty[4] = { "left", "center", "right", "char" };
			for (int i = 0; i < pf.ntab; i++)
			{
				st.puts ("<style:tab-stop"); len (st, "style:position", pf.tab[i].pos - pf.left);
				if (pf.tab[i].align) { st.puts (" style:type=\""); st.puts (ty[pf.tab[i].align & 3]); st.puts ("\""); }
				if (pf.tab[i].align == TA_DECIMAL) st.puts (" style:char=\".\"");
				if (pf.tab[i].leader == TL_DOT) st.puts (" style:leader-style=\"dotted\" style:leader-text=\".\"");
				else if (pf.tab[i].leader == TL_DASH) st.puts (" style:leader-style=\"dash\" style:leader-text=\"-\"");
				else if (pf.tab[i].leader == TL_LINE) st.puts (" style:leader-style=\"solid\" style:leader-text=\"_\"");
				st.puts ("/>");
			}
			st.puts ("</style:tab-stops></style:paragraph-properties></style:style>");
		}
		else st.puts ("/></style:style>");
		return npk;
	}
	int date_style (const char *pic)
	{
		for (int i = 0; i < ndk; i++) if (!strcmp (dk[i], pic)) return i + 1;
		if (ndk == dkcap) { int c = dkcap * 2 + 4; char (*n)[64] = new char[c][64]; for (int i = 0; i < ndk; i++) scpy (n[i], dk[i], 64); delete[] dk; dk = n; dkcap = c; }
		scpy (dk[ndk++], pic, 64);
		st.puts ("<number:date-style style:name=\""); name (st, "N", ndk); st.puts ("\">");
		for (const char *p = pic; *p; )
		{
			char c = *p; int k = 1; while (p[k] == c) k++;
			if (c == '\'') { p++; st.puts ("<number:text>"); while (*p && *p != '\'') { char s[2] = { *p++, 0 }; xml_esc (st, s); } if (*p) p++; st.puts ("</number:text>"); continue; }
			if (c == '\\') { p++; if (*p) { st.puts ("<number:text>"); char s[2] = { *p++, 0 }; xml_esc (st, s); st.puts ("</number:text>"); } continue; }
			if (c == 'd') st.puts (k >= 4 ? "<number:day-of-week number:style=\"long\"/>" : k == 3 ? "<number:day-of-week/>" : k == 2 ? "<number:day number:style=\"long\"/>" : "<number:day/>");
			else if (c == 'M') st.puts (k >= 4 ? "<number:month number:style=\"long\" number:textual=\"true\"/>" : k == 3 ? "<number:month number:textual=\"true\"/>" : k == 2 ? "<number:month number:style=\"long\"/>" : "<number:month/>");
			else if (c == 'y') st.puts (k >= 3 ? "<number:year number:style=\"long\"/>" : "<number:year/>");
			else if (c == 'H' || c == 'h') st.puts (k >= 2 ? "<number:hours number:style=\"long\"/>" : "<number:hours/>");
			else if (c == 'm') st.puts (k >= 2 ? "<number:minutes number:style=\"long\"/>" : "<number:minutes/>");
			else if (c == 's') st.puts (k >= 2 ? "<number:seconds number:style=\"long\"/>" : "<number:seconds/>");
			else if ((c == 'A' || c == 'a') && p[1] && (p[1] == 'M' || p[1] == 'm')) { st.puts ("<number:am-pm/>"); p += 5; continue; }
			else { st.puts ("<number:text>"); for (int i = 0; i < k; i++) { char s[2] = { c, 0 }; xml_esc (st, s); } st.puts ("</number:text>"); }
			p += k;
		}
		st.puts ("</number:date-style>");
		return ndk;
	}
	// A span of text (spaces: text:s, tabs, line breaks).
	void text (XBuf &o, const unsigned *u, int n)
	{
		for (int i = 0; i < n; )
		{
			unsigned c = u[i];
			if (c == '\t') { o.puts ("<text:tab/>"); i++; continue; }
			if (c == 0x0B) { o.puts ("<text:line-break/>"); i++; continue; }
			if (c == ' ')
			{
				int j = i; while (j < n && u[j] == ' ') j++;
				int k = j - i;
				bool lead = i == 0;
				if (!lead) { o.put (' '); k--; }
				if (k > 0) { o.puts ("<text:s text:c=\""); o.num (k); o.puts ("\"/>"); }
				i = j;
				continue;
			}
			xml_escu (o, &c, 1);
			i++;
		}
	}
	void para (XBuf &o, const Para *q, bool list)
	{
		int ps = para_style (q->pf, list);
		bool h = style_outline (q->pf.style) > 0;
		o.puts (h ? "<text:h" : "<text:p"); o.puts (" text:style-name=\""); name (o, "P", ps); o.puts ("\"");
		if (h) { o.puts (" text:outline-level=\""); o.num (style_outline (q->pf.style)); o.puts ("\""); }
		o.put ('>');
		for (int i = 0; i < q->len; )
		{
			const CharFmt &f = d.fmt[q->cf[i]];
			int ts = text_style (q->cf[i]);
			o.puts ("<text:span text:style-name=\""); name (o, "T", ts); o.puts ("\">");
			if (q->ch[i] == OBJ_CHAR && f.obj)
			{
				int im = f.obj - 1;
				if (!imgNo[im]) imgNo[im] = ++*imgCount;
				o.puts ("<draw:frame draw:style-name=\"fr1\" draw:name=\"Image"); o.num (++nimg + 1000 * (pre[0] == 'M')); o.puts ("\" text:anchor-type=\"as-char\"");
				len (o, "svg:width", f.ow); len (o, "svg:height", f.oh); o.puts (" draw:z-index=\"0\"><draw:image xlink:href=\"Pictures/image");
				o.num (imgNo[im]); o.puts (d.img[im].data && d.img[im].len && d.img[im].jpeg ? ".jpg" : ".png");
				o.puts ("\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/></draw:frame></text:span>");
				i++;
				continue;
			}
			if (q->ch[i] == FIELD_CHAR && f.fld)
			{
				const Field &fd = d.fld[f.fld - 1];
				unsigned t[80]; int tn = field_text (d, f.fld, 1, 1, t, 80);
				switch (fd.kind)
				{
				case FK_PAGE: o.puts ("<text:page-number text:select-page=\"current\">"); text (o, t, tn); o.puts ("</text:page-number>"); break;
				case FK_PAGES: o.puts ("<text:page-count>"); text (o, t, tn); o.puts ("</text:page-count>"); break;
				case FK_DATE: case FK_TIME:
				{
					int ds = date_style (fd.arg[0] ? fd.arg : fd.kind == FK_DATE ? "dd/MM/yyyy" : "HH:mm");
					o.puts (fd.kind == FK_DATE ? "<text:date style:data-style-name=\"" : "<text:time style:data-style-name=\""); name (o, "N", ds); o.puts ("\">");
					text (o, t, tn); o.puts (fd.kind == FK_DATE ? "</text:date>" : "</text:time>");
					break;
				}
				case FK_MERGE:
					o.puts ("<text:database-display text:database-name=\"Onyx\" text:table-name=\"Records\" text:table-type=\"table\" text:column-name=\""); xml_esc1 (o, fd.arg); o.puts ("\">");
					text (o, t, tn); o.puts ("</text:database-display>");
					break;
				}
				o.puts ("</text:span>");
				i++;
				continue;
			}
			int j = i + 1;
			while (j < q->len && q->cf[j] == q->cf[i] && q->ch[j] != OBJ_CHAR && q->ch[j] != FIELD_CHAR) j++;
			text (o, q->ch + i, j - i);
			o.puts ("</text:span>");
			i = j;
		}
		o.puts (h ? "</text:h>" : "</text:p>");
	}
	void table (XBuf &o, Para *const *p, int a, int b)
	{
		const Table *t = para_table (d, p[a]);
		int k = ++ntbl, nc = t->ncols;
		// its styles: the table, its columns, its rows, its cells
		st.puts ("<style:style style:name=\""); name (st, "Tbl", k); st.puts ("\" style:family=\"table\"><style:table-properties");
		len (st, "style:width", table_width (t));
		st.puts (t->align == AL_CENTER ? " table:align=\"center\"" : t->align == AL_RIGHT ? " table:align=\"right\"" : " table:align=\"left\"");
		if (t->align == AL_LEFT && t->indent) len (st, "fo:margin-left", t->indent);
		st.puts ("/></style:style>");
		for (int c = 0; c < nc; c++)
		{
			st.puts ("<style:style style:name=\""); name (st, "Tbl", k); st.put ('.'); st.num (c); st.puts ("C\" style:family=\"table-column\"><style:table-column-properties");
			len (st, "style:column-width", t->colW[c]); st.puts ("/></style:style>");
		}
		for (int r = 0; r < t->nrows; r++)
			if (t->rowH[r]) { st.puts ("<style:style style:name=\""); name (st, "Tbl", k); st.put ('.'); st.num (r); st.puts ("R\" style:family=\"table-row\"><style:table-row-properties"); len (st, "style:min-row-height", t->rowH[r]); st.puts ("/></style:style>"); }
		auto bstr = [&] (XBuf &s, bool on) {
			if (!on) { s.puts ("none"); return; }
			s.num (t->bw / 8); s.put ('.'); s.num (t->bw % 8 * 125 / 10); s.puts ("pt solid #"); color_hex (s, t->bcolor == AUTO ? 0 : t->bcolor);
		};
		o.puts ("<table:table table:name=\"Table"); o.num (k + 100 * (pre[0] == 'M')); o.puts ("\" table:style-name=\""); name (o, "Tbl", k); o.puts ("\">");
		for (int c = 0; c < nc; c++) { o.puts ("<table:table-column table:style-name=\""); name (o, "Tbl", k); o.put ('.'); o.num (c); o.puts ("C\"/>"); }
		for (int r = 0; r < t->nrows; r++)
		{
			if (r == 0 && t->header) o.puts ("<table:table-header-rows>");
			o.puts ("<table:table-row");
			if (t->rowH[r]) { o.puts (" table:style-name=\""); name (o, "Tbl", k); o.put ('.'); o.num (r); o.puts ("R\""); }
			o.put ('>');
			for (int c = 0; c < nc; c++)
			{
				const TCell &cl = tcell (t, r, c);
				if (cl.covered) { o.puts ("<table:covered-table-cell/>"); continue; }
				// the cell's style: its shading, its lines
				int re = wmin (r + (int) cl.rs, t->nrows) - 1, ce = wmin (c + (int) cl.cs, nc) - 1;
				bool all = t->border == TB_ALL, outer = t->border == TB_OUTER, rows = t->border == TB_ROWS;
				bool top = all || rows || (outer && r == 0), bot = all || rows || (outer && re == t->nrows - 1);
				bool lft = all || (outer && c == 0), rgt = all || (outer && ce == nc - 1);
				st.puts ("<style:style style:name=\""); name (st, "Tbl", k); st.put ('.'); st.num (r); st.put ('.'); st.num (c);
				st.puts ("K\" style:family=\"table-cell\"><style:table-cell-properties");
				len (st, "fo:padding-left", CELL_PAD_X); len (st, "fo:padding-right", CELL_PAD_X);
				len (st, "fo:padding-top", CELL_PAD_Y); len (st, "fo:padding-bottom", CELL_PAD_Y);
				if (cl.fill != AUTO) { st.puts (" fo:background-color=\"#"); color_hex (st, cl.fill); st.put ('"'); }
				st.puts (" fo:border-top=\""); bstr (st, top); st.puts ("\" fo:border-bottom=\""); bstr (st, bot);
				st.puts ("\" fo:border-left=\""); bstr (st, lft); st.puts ("\" fo:border-right=\""); bstr (st, rgt); st.puts ("\"/></style:style>");
				o.puts ("<table:table-cell table:style-name=\""); name (o, "Tbl", k); o.put ('.'); o.num (r); o.put ('.'); o.num (c); o.puts ("K\"");
				if (cl.cs > 1) { o.puts (" table:number-columns-spanned=\""); o.num (cl.cs); o.puts ("\""); }
				if (cl.rs > 1) { o.puts (" table:number-rows-spanned=\""); o.num (cl.rs); o.puts ("\""); }
				o.puts (" office:value-type=\"string\">");
				bool any = false;
				for (int i = a; i < b; i++) if (p[i]->pf.row == r && p[i]->pf.col == c) { para (o, p[i], false); any = true; }
				if (!any) o.puts ("<text:p/>");
				o.puts ("</table:table-cell>");
			}
			o.puts ("</table:table-row>");
			if (r == 0 && t->header) o.puts ("</table:table-header-rows>");
		}
		o.puts ("</table:table>");
	}
	// A story's paragraphs: its lists as lists, its tables, its table of contents as an index.
	void story (XBuf &o, Para *const *p, int n, bool body)
	{
		int depth = 0, kind = LS_NONE;
		auto closeLists = [&] (int to) { while (depth > to) { o.puts ("</text:list-item></text:list>"); depth--; } };
		for (int i = 0; i < n; )
		{
			const Para *q = p[i];
			if (body && in_table (q) && para_table (d, q))
			{
				closeLists (0); kind = LS_NONE;
				int j = i + 1; while (j < n && p[j]->pf.tbl == q->pf.tbl) j++;
				table (o, p, i, j);
				i = j;
				continue;
			}
			if (body && (style_toc (q->pf.style) || (q->pf.style == ST_TOCHEAD && i + 1 < n && style_toc (p[i + 1]->pf.style))))
			{
				closeLists (0); kind = LS_NONE;
				o.puts ("<text:table-of-content text:name=\"Table of Contents1\" text:protected=\"false\"><text:table-of-content-source text:outline-level=\"3\">"
					"<text:index-title-template text:style-name=\"Contents_20_Heading\">Contents</text:index-title-template>");
				for (int l = 1; l <= 3; l++)
				{
					o.puts ("<text:table-of-content-entry-template text:outline-level=\""); o.num (l); o.puts ("\" text:style-name=\"Contents_20_"); o.num (l);
					o.puts ("\"><text:index-entry-text/><text:index-entry-tab-stop style:type=\"right\" style:leader-char=\".\"/><text:index-entry-page-number/></text:table-of-content-entry-template>");
				}
				o.puts ("</text:table-of-content-source><text:index-body>");
				if (q->pf.style == ST_TOCHEAD) { o.puts ("<text:index-title text:name=\"Table of Contents1_Head\">"); para (o, q, false); o.puts ("</text:index-title>"); i++; }
				while (i < n && style_toc (p[i]->pf.style)) para (o, p[i++], false);
				o.puts ("</text:index-body></text:table-of-content>");
				continue;
			}
			if (q->pf.list == LS_NONE) { closeLists (0); kind = LS_NONE; para (o, q, false); i++; continue; }
			int lv = q->pf.level + 1;
			if (q->pf.list != kind) { closeLists (0); kind = q->pf.list; }
			if (depth >= lv) { closeLists (lv); o.puts ("</text:list-item><text:list-item>"); }
			while (depth < lv)
			{
				if (depth == 0) { o.puts ("<text:list text:style-name=\""); o.puts (kind == LS_NUMBER ? "LN" : "LB"); o.puts ("\"><text:list-item>"); }
				else o.puts ("<text:list><text:list-item>");
				depth++;
			}
			para (o, q, true);
			i++;
		}
		closeLists (0);
	}
};

static void odt_list_styles (XBuf &o)
{
	static const char *const B[3] = { "\xE2\x80\xA2", "\xE2\x97\xA6", "\xE2\x96\xAA" };
	static const char *const F[3] = { "1", "a", "i" };
	for (int k = 0; k < 2; k++)
	{
		o.puts (k ? "<text:list-style style:name=\"LN\">" : "<text:list-style style:name=\"LB\">");
		for (int l = 1; l <= 10; l++)
		{
			if (k) { o.puts ("<text:list-level-style-number text:level=\""); o.num (l); o.puts ("\" style:num-suffix=\".\" style:num-format=\""); o.puts (F[(l - 1) % 3]); o.puts ("\">"); }
			else { o.puts ("<text:list-level-style-bullet text:level=\""); o.num (l); o.puts ("\" text:bullet-char=\""); o.puts (B[(l - 1) % 3]); o.puts ("\">"); }
			o.puts ("<style:list-level-properties text:list-level-position-and-space-mode=\"label-alignment\"><style:list-level-label-alignment text:label-followed-by=\"listtab\"");
			o.puts (" fo:text-indent=\""); twips_cm (o, -360); o.puts ("\" fo:margin-left=\""); twips_cm (o, 720 * l); o.puts ("\"/></style:list-level-properties>");
			o.puts (k ? "</text:list-level-style-number>" : "</text:list-level-style-bullet>");
		}
		o.puts ("</text:list-style>");
	}
}

static bool odt_save (Doc &d, unsigned char **out, unsigned *len)
{
	int *imgNo = new int[d.nimg + 1]; int imgCount = 0;
	for (int i = 0; i <= d.nimg; i++) imgNo[i] = 0;
	// the fonts used
	bool *used = formats_used (d);
	bool *fontUsed = new bool[d.nfont + 1];
	for (int i = 0; i <= d.nfont; i++) fontUsed[i] = false;
	for (int i = 0; i < d.nfmt; i++) if (used[i]) fontUsed[d.fmt[i].font] = true;
	for (int i = 0; i < ST_COUNT; i++) fontUsed[doc_font (d, STYLES[i].font)] = true;
	delete[] used;
	XBuf faces;
	faces.puts ("<office:font-face-decls>");
	for (int i = 0; i < d.nfont; i++)
	{
		if (!fontUsed[i]) continue;
		faces.puts ("<style:font-face style:name=\""); xml_esc1 (faces, d.fontName[i]); faces.puts ("\" svg:font-family=\"&apos;"); xml_esc1 (faces, d.fontName[i]); faces.puts ("&apos;\"/>");
	}
	faces.puts ("</office:font-face-decls>");
	delete[] fontUsed;
	// content.xml
	OdtOut C (d, "", imgNo, &imgCount);
	if (d.page.start != 1) C.pageStart = d.page.start;
	XBuf body;
	body.puts ("<office:body><office:text>");
	if (d.mergeSrc[0]) { body.puts ("<text:user-field-decls><text:user-field-decl office:value-type=\"string\" office:string-value=\""); xml_esc1 (body, d.mergeSrc); body.puts ("\" text:name=\"OnyxMergeSource\"/></text:user-field-decls>"); }
	int n; Para **p = story_p (d, SY_BODY, &n);
	C.story (body, p, n, true);
	body.puts ("</office:text></office:body>");
	XBuf content;
	content.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content"); content.puts (O_NS); content.put ('>');
	content.putn (faces.b, faces.n);
	content.puts ("<office:automatic-styles><style:style style:name=\"fr1\" style:family=\"graphic\"><style:graphic-properties style:vertical-pos=\"top\" style:vertical-rel=\"baseline\"/></style:style>");
	odt_list_styles (content);
	content.putn (C.st.b, C.st.n);
	content.puts ("</office:automatic-styles>");
	content.putn (body.b, body.n);
	content.puts ("</office:document-content>");
	// styles.xml: the named styles, the page, the master page and its headers and footers
	OdtOut S (d, "M", imgNo, &imgCount);
	XBuf master;
	const PageSetup &pg = d.page;
	bool hOn = !story_empty (d, SY_HEADER) || (pg.titlePg && !story_empty (d, SY_HEADER1));
	bool fOn = !story_empty (d, SY_FOOTER) || (pg.titlePg && !story_empty (d, SY_FOOTER1));
	master.puts ("<office:master-styles><style:master-page style:name=\"Standard\" style:page-layout-name=\"pm1\">");
	static const char *const tag[SY_COUNT] = { "", "style:header", "style:footer", "style:header-first", "style:footer-first" };
	for (int s = SY_HEADER; s < SY_COUNT; s++)
	{
		bool first = s == SY_HEADER1 || s == SY_FOOTER1;
		if (first && !pg.titlePg) continue;
		if (!(is_footer (s) ? fOn : hOn)) continue;
		master.put ('<'); master.puts (tag[s]); master.put ('>');
		int sn; Para **sp = story_p (d, s, &sn);
		if (story_empty (d, s)) master.puts ("<text:p/>"); else S.story (master, sp, sn, false);
		master.puts ("</"); master.puts (tag[s]); master.put ('>');
	}
	master.puts ("</style:master-page></office:master-styles>");
	XBuf styles;
	styles.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles"); styles.puts (O_NS); styles.put ('>');
	styles.putn (faces.b, faces.n);
	CharFmt n0 = style_fmt (d, ST_NORMAL);
	styles.puts ("<office:styles><style:default-style style:family=\"paragraph\"><style:paragraph-properties style:tab-stop-distance=\"1.25cm\"/>"
		     "<style:text-properties style:font-name=\""); xml_esc1 (styles, d.fontName[n0.font]); styles.puts ("\" fo:font-size=\"12pt\" fo:language=\"en\" fo:country=\"GB\"/></style:default-style>");
	for (int i = 0; i < ST_COUNT; i++)
	{
		const Style &s = STYLES[i];
		styles.puts ("<style:style style:name=\""); styles.puts (O_STYLE_NAME[i]); styles.puts ("\" style:display-name=\""); styles.puts (O_STYLE_DISPLAY[i]);
		styles.puts ("\" style:family=\"paragraph\"");
		if (i != ST_NORMAL) styles.puts (" style:parent-style-name=\"Standard\"");
		if (style_outline (i)) { styles.puts (" style:default-outline-level=\""); styles.num (style_outline (i)); styles.puts ("\""); }
		styles.puts ("><style:paragraph-properties");
		static const char *const al[4] = { "start", "center", "end", "justify" };
		styles.puts (" fo:text-align=\""); styles.puts (al[s.align & 3]); styles.puts ("\"");
		styles.puts (" fo:margin-top=\""); twips_cm (styles, s.before); styles.puts ("\" fo:margin-bottom=\""); twips_cm (styles, s.after);
		styles.puts ("\" fo:margin-left=\""); twips_cm (styles, s.left); styles.puts ("\" fo:margin-right=\""); twips_cm (styles, s.right);
		styles.puts ("\" fo:line-height=\""); styles.num (s.line); styles.puts ("%\"");
		if (s.keepNext) styles.puts (" fo:keep-with-next=\"always\"");
		styles.puts ("/><style:text-properties style:font-name=\""); xml_esc1 (styles, s.font); styles.puts ("\" fo:font-size=\""); styles.num (s.size / 2); styles.puts ("pt\"");
		if (s.flags & CF_BOLD) styles.puts (" fo:font-weight=\"bold\"");
		if (s.flags & CF_ITALIC) styles.puts (" fo:font-style=\"italic\"");
		if (s.color != AUTO) { styles.puts (" fo:color=\"#"); color_hex (styles, s.color); styles.put ('"'); }
		styles.puts ("/></style:style>");
	}
	styles.puts ("</office:styles><office:automatic-styles><style:page-layout style:name=\"pm1\"><style:page-layout-properties");
	OdtOut &L0 = S;
	L0.len (styles, "fo:page-width", pg.w); L0.len (styles, "fo:page-height", pg.h);
	styles.puts (pg.w > pg.h ? " style:print-orientation=\"landscape\"" : " style:print-orientation=\"portrait\"");
	L0.len (styles, "fo:margin-top", hOn ? pg.hdr : pg.top); L0.len (styles, "fo:margin-bottom", fOn ? pg.ftr : pg.bottom);
	L0.len (styles, "fo:margin-left", pg.left); L0.len (styles, "fo:margin-right", pg.right);
	styles.puts ("/><style:header-style>");
	if (hOn) { styles.puts ("<style:header-footer-properties"); L0.len (styles, "fo:min-height", wmax (0, pg.top - pg.hdr)); styles.puts (" fo:margin-bottom=\"0cm\" style:dynamic-spacing=\"false\"/>"); }
	styles.puts ("</style:header-style><style:footer-style>");
	if (fOn) { styles.puts ("<style:header-footer-properties"); L0.len (styles, "fo:min-height", wmax (0, pg.bottom - pg.ftr)); styles.puts (" fo:margin-top=\"0cm\" style:dynamic-spacing=\"false\"/>"); }
	styles.puts ("</style:footer-style></style:page-layout>");
	styles.puts ("<style:style style:name=\"fr1\" style:family=\"graphic\"><style:graphic-properties style:vertical-pos=\"top\" style:vertical-rel=\"baseline\"/></style:style>");
	odt_list_styles (styles);
	styles.putn (S.st.b, S.st.n);
	styles.puts ("</office:automatic-styles>");
	styles.putn (master.b, master.n);
	styles.puts ("</office:document-styles>");
	// the package
	XBuf mani, meta;
	mani.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
		   "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
		   "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
		   "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
		   "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>");
	meta.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta"); meta.puts (O_NS);
	meta.puts ("><office:meta><meta:generator>Onyx Writer</meta:generator></office:meta></office:document-meta>");
	pngsave::ZipOut zip;
	static const char *MT = "application/vnd.oasis.opendocument.text";
	zip.add ("mimetype", MT, (unsigned) strlen (MT), false);
	zip.add ("content.xml", content.b, (unsigned) content.n, true);
	zip.add ("styles.xml", styles.b, (unsigned) styles.n, true);
	zip.add ("meta.xml", meta.b, (unsigned) meta.n, true);
	for (int i = 0; i < d.nimg; i++)
	{
		if (!imgNo[i]) continue;
		unsigned ln; bool jpeg, made;
		const unsigned char *bb = image_bytes (d.img[i], &ln, &jpeg, &made);
		char nm[48]; scpy (nm, "Pictures/image", sizeof nm); int k = slen (nm);
		char t[12]; int j = 0, v = imgNo[i]; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) nm[k++] = t[--j];
		scpy (nm + k, jpeg ? ".jpg" : ".png", (int) sizeof nm - k);
		zip.add (nm, bb, ln, false);
		mani.puts ("<manifest:file-entry manifest:full-path=\""); mani.puts (nm); mani.puts (jpeg ? "\" manifest:media-type=\"image/jpeg\"/>" : "\" manifest:media-type=\"image/png\"/>");
		if (made) delete[] bb;
	}
	mani.puts ("</manifest:manifest>");
	zip.add ("META-INF/manifest.xml", mani.b, (unsigned) mani.n, true);
	delete[] imgNo;
	*out = zip.finish (len);
	return *out != 0;
}

} // namespace wr

#endif
