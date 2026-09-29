//
// docx.h -- Word's documents (.docx: Office Open XML's WordprocessingML) read and written.
//
// Read: the styles (their inheritance, the document's defaults, the theme's fonts), the paragraphs
// (alignment, indents, spacing, keep options, page breaks, tab stops, lists -- numbering.xml's
// bullets and numbers --), the runs (fonts, sizes, bold / italic / underline / strike-through,
// superscript / subscript, colours, highlights and shading), the tables (the grid's columns, a cell's
// span, vertical merges, shading, lines, a heading row), the images (DrawingML's and VML's), the fields
// (simple and complex ones: the page, the number of pages, the date, the time, a mail merge's -- the
// others as their result), the headers and footers (the first page's own), the page's size, margins and
// first number, the mail merge's data (a document variable). Text boxes, footnotes, comments and
// tracked deletions left out.
//
// Written: all of Writer's document -- its styles as Word's (Normal, heading 1...), each paragraph's
// and run's format in full, the lists, the tables, the headers and footers, the fields (a table of
// contents as Word's TOC field), the images (PNG / JPEG parts).
//
#ifndef _writer_docx_h
#define _writer_docx_h

#include "fileio.h"
#include "xml.h"

namespace wr {

static const char *const W_STYLE_ID[ST_COUNT] = { "Normal", "Heading1", "Heading2", "Heading3", "Title", "Subtitle", "Quote", "PlainText",
						  "TOC1", "TOC2", "TOC3", "TOCHeading", "Header", "Footer" };
static const char *const W_STYLE_NAME[ST_COUNT] = { "Normal", "heading 1", "heading 2", "heading 3", "Title", "Subtitle", "Quote", "Plain Text",
						    "toc 1", "toc 2", "toc 3", "TOC Heading", "header", "footer" };

// A zip archive holding word/document.xml (or an officeDocument part)?
static bool docx_is (const char *b, int n)
{
	if (n < 4 || b[0] != 'P' || b[1] != 'K') return false;
	return zip_has ((const unsigned char *) b, (unsigned) n, "word/document.xml");
}

// ---- reading ---------------------------------------------------------------------------------------------------
enum { WM_FONT = 1, WM_SIZE = 2, WM_COLOR = 4, WM_HILITE = 8 };
enum { WP_ALIGN = 1, WP_LEFT = 2, WP_RIGHT = 4, WP_FIRST = 8, WP_BEFORE = 16, WP_AFTER = 32, WP_LINE = 64, WP_KEEPN = 128,
       WP_KEEPL = 256, WP_WIDOW = 512, WP_BREAK = 1024, WP_NUM = 2048 };

// Properties as Word gives them, each only when said (a style's, a paragraph's, a run's): layered.
struct WProps
{
	unsigned cm; char font[48]; short size; unsigned color, hilite; unsigned short on, off; bool hidden;
	unsigned pm; unsigned char align; short left, right, first, before, after, line; bool keepNext, keepLines, widow, pageBreak;
	int numId, ilvl;
	int ntab; TabStop tab[MAXTABS]; int nclear; int clear[MAXTABS];
};
static void wprops_init (WProps &p) { memset (&p, 0, sizeof p); p.numId = -1; }
static void wprops_apply (WProps &d, const WProps &s)
{
	if (s.cm & WM_FONT) scpy (d.font, s.font, sizeof d.font);
	if (s.cm & WM_SIZE) d.size = s.size;
	if (s.cm & WM_COLOR) d.color = s.color;
	if (s.cm & WM_HILITE) d.hilite = s.hilite;
	d.cm |= s.cm;
	d.on = (unsigned short) ((d.on & ~s.off) | s.on); d.off = (unsigned short) ((d.off & ~s.on) | s.off);
	if (s.hidden) d.hidden = true;
	if (s.pm & WP_ALIGN) d.align = s.align;
	if (s.pm & WP_LEFT) d.left = s.left;
	if (s.pm & WP_RIGHT) d.right = s.right;
	if (s.pm & WP_FIRST) d.first = s.first;
	if (s.pm & WP_BEFORE) d.before = s.before;
	if (s.pm & WP_AFTER) d.after = s.after;
	if (s.pm & WP_LINE) d.line = s.line;
	if (s.pm & WP_KEEPN) d.keepNext = s.keepNext;
	if (s.pm & WP_KEEPL) d.keepLines = s.keepLines;
	if (s.pm & WP_WIDOW) d.widow = s.widow;
	if (s.pm & WP_BREAK) d.pageBreak = s.pageBreak;
	if (s.pm & WP_NUM) { d.numId = s.numId; d.ilvl = s.ilvl; }
	d.pm |= s.pm;
	for (int i = 0; i < s.nclear; i++)
		for (int k = 0; k < d.ntab; k++) if (d.tab[k].pos == s.clear[i]) { for (int j = k; j < d.ntab - 1; j++) d.tab[j] = d.tab[j + 1]; d.ntab--; break; }
	for (int i = 0; i < s.ntab; i++)
	{
		int k = 0; while (k < d.ntab && d.tab[k].pos < s.tab[i].pos) k++;
		if (k < d.ntab && d.tab[k].pos == s.tab[i].pos) { d.tab[k] = s.tab[i]; continue; }
		if (d.ntab >= MAXTABS) continue;
		for (int j = d.ntab; j > k; j--) d.tab[j] = d.tab[j - 1];
		d.tab[k] = s.tab[i]; d.ntab++;
	}
}

struct WStyle { char id[48], based[48]; int type; bool dflt; int st; WProps p; };	// type: 0 paragraph, 1 character, 2 table
struct WRel { char id[32]; char type[24]; char target[160]; };
struct WAbs { int id; unsigned char fmt[9]; short left[9], hang[9]; };		// fmt: 0 none, LS_BULLET, LS_NUMBER
struct WNum { int id, abs; };

static int w_style_by_name (const char *n)
{
	for (int i = 0; i < ST_COUNT; i++) if (sicmp (n, W_STYLE_NAME[i]) == 0) return i;
	for (int i = 0; i < ST_COUNT; i++) if (sicmp (n, W_STYLE_ID[i]) == 0) return i;
	if (!sicmp (n, "heading 4") || !sicmp (n, "heading 5") || !sicmp (n, "heading 6")) return ST_H3;
	if (!sicmp (n, "Intense Quote") || !sicmp (n, "Block Text") || !sicmp (n, "Quotations") || !sicmp (n, "Block Quotation")) return ST_QUOTE;
	if (!sicmp (n, "HTML Preformatted") || !sicmp (n, "Preformatted Text") || !sicmp (n, "PreformattedText") || !sicmp (n, "Source Text")) return ST_CODE;
	if (!sicmp (n, "Contents 1")) return ST_TOC1;
	if (!sicmp (n, "Contents 2")) return ST_TOC2;
	if (!sicmp (n, "Contents 3")) return ST_TOC3;
	if (!sicmp (n, "Contents Heading")) return ST_TOCHEAD;
	return -1;
}
static unsigned w_highlight (const char *v)
{
	static const struct { const char *n; unsigned c; } H[] = {
		{ "yellow", 0xFFFF00 }, { "green", 0x00FF00 }, { "cyan", 0x00FFFF }, { "magenta", 0xFF00FF }, { "blue", 0x0000FF },
		{ "red", 0xFF0000 }, { "darkBlue", 0x000080 }, { "darkCyan", 0x008080 }, { "darkGreen", 0x008000 },
		{ "darkMagenta", 0x800080 }, { "darkRed", 0x800000 }, { "darkYellow", 0x808000 }, { "darkGray", 0x808080 },
		{ "lightGray", 0xC0C0C0 }, { "black", 0x000000 }, { "white", 0xFFFFFF } };
	for (unsigned i = 0; i < sizeof H / sizeof H[0]; i++) if (!sicmp (v, H[i].n)) return H[i].c;
	return AUTO;
}

struct DocxIn
{
	Doc &d;
	const unsigned char *z; unsigned zn;
	WStyle *sty; int nsty, stycap;
	WAbs *abs; int nabs; WNum *num; int nnum;
	WProps defs;				// the document's defaults
	char major[48], minor[48];		// the theme's fonts
	WRel *rel; int nrel;			// the part being read's relationships
	char part[160];
	struct Fld { char inst[256]; int n; int stage; bool ours; } fs[8]; int fdepth;
	TableBuild *tb;				// the table being read (its cell takes the paragraphs), 0: none
	int story;
	char hdrId[SY_COUNT][32];		// the headers' and footers' parts (their relationships' ids)
	bool sectNext;				// a section ended: the next paragraph on a new page
	// the paragraph being read
	Para *q; WProps pp, rbase; int pst; unsigned short markCf;
	bool brkPending;			// a page break read: the text after it a new paragraph (none: the next one's)
	int imgCount;

	DocxIn (Doc &d_, const unsigned char *z_, unsigned zn_) : d (d_), z (z_), zn (zn_), sty (0), nsty (0), stycap (0), abs (0), nabs (0),
		num (0), nnum (0), rel (0), nrel (0), fdepth (0), tb (0), story (SY_BODY), sectNext (false), q (0), pst (-1), markCf (0), brkPending (false), imgCount (0)
	{
		wprops_init (defs); scpy (major, "Calibri Light", 48); scpy (minor, "Calibri", 48); part[0] = 0;
		for (int i = 0; i < SY_COUNT; i++) hdrId[i][0] = 0;
	}
	~DocxIn () { delete[] sty; delete[] abs; delete[] num; delete[] rel; }

	char *get (const char *name, int *len) { return zip_get (z, zn, name, len); }

	// ---- the relationships of a part ----
	void rels (const char *p)
	{
		delete[] rel; rel = 0; nrel = 0;
		scpy (part, p, sizeof part);
		char rp[200]; int cut = 0;
		for (int i = 0; p[i]; i++) if (p[i] == '/') cut = i + 1;
		scpy (rp, p, cut + 1); int k = slen (rp); scpy (rp + k, "_rels/", 200 - k); k = slen (rp); scpy (rp + k, p + cut, 200 - k); k = slen (rp); scpy (rp + k, ".rels", 200 - k);
		int n; char *x = get (rp, &n);
		if (!x) return;
		int cap = 16; rel = new WRel[cap];
		XmlReader X (x, n);
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t != X_START || !X.is ("Relationship")) continue;
			XBuf mode; if (X.attr ("TargetMode", mode) && !sicmp (mode.str (), "External")) continue;
			if (nrel == cap) { WRel *nr = new WRel[cap * 2]; for (int i = 0; i < nrel; i++) nr[i] = rel[i]; delete[] rel; rel = nr; cap *= 2; }
			WRel &r = rel[nrel++];
			X.attrs ("Id", r.id, sizeof r.id);
			char ty[200]; X.attrs ("Type", ty, sizeof ty);
			int s = 0; for (int i = 0; ty[i]; i++) if (ty[i] == '/') s = i + 1;
			scpy (r.type, ty + s, sizeof r.type);
			char tg[160]; X.attrs ("Target", tg, sizeof tg);
			part_path (p, tg, r.target, sizeof r.target);
		}
		delete[] x;
	}
	const char *target (const char *id) { for (int i = 0; i < nrel; i++) if (!strcmp (rel[i].id, id)) return rel[i].target; return 0; }
	const char *target_of (const char *type) { for (int i = 0; i < nrel; i++) if (!sicmp (rel[i].type, type)) return rel[i].target; return 0; }

	// ---- properties ----
	void font_attr (XmlReader &X, WProps &p)
	{
		char v[48];
		if (X.attrs ("ascii", v, sizeof v) || X.attrs ("hAnsi", v, sizeof v)) { scpy (p.font, v, sizeof p.font); p.cm |= WM_FONT; return; }
		if (X.attrs ("asciiTheme", v, sizeof v) || X.attrs ("hAnsiTheme", v, sizeof v))
		{ scpy (p.font, v[0] == 'm' && v[1] == 'a' ? major : minor, sizeof p.font); p.cm |= WM_FONT; }
	}
	void flag (XmlReader &X, WProps &p, unsigned short f) { if (X.attr_on ()) { p.on |= f; p.off &= (unsigned short) ~f; } else { p.off |= f; p.on &= (unsigned short) ~f; } }
	// A w:rPr's children (X at its start): into p; its character style's id.
	void rPr (XmlReader &X, WProps &p, char *rstyle = 0)
	{
		if (rstyle) rstyle[0] = 0;
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is ("rPr")) return; continue; }
			if (t != X_START) continue;
			char v[48];
			if (X.is ("rStyle")) { if (rstyle) X.attrs ("val", rstyle, 48); }
			else if (X.is ("rFonts")) font_attr (X, p);
			else if (X.is ("b")) flag (X, p, CF_BOLD);
			else if (X.is ("i")) flag (X, p, CF_ITALIC);
			else if (X.is ("strike") || X.is ("dstrike")) flag (X, p, CF_STRIKE);
			else if (X.is ("u")) { X.attrs ("val", v, sizeof v); if (!sicmp (v, "none")) { p.off |= CF_UNDER; p.on &= (unsigned short) ~CF_UNDER; } else { p.on |= CF_UNDER; p.off &= (unsigned short) ~CF_UNDER; } }
			else if (X.is ("vertAlign"))
			{
				X.attrs ("val", v, sizeof v);
				p.off |= CF_SUPER | CF_SUB; p.on &= (unsigned short) ~(CF_SUPER | CF_SUB);
				if (!sicmp (v, "superscript")) { p.on |= CF_SUPER; p.off &= (unsigned short) ~CF_SUPER; }
				else if (!sicmp (v, "subscript")) { p.on |= CF_SUB; p.off &= (unsigned short) ~CF_SUB; }
			}
			else if (X.is ("sz")) { long s = X.attr_int ("val", 0); if (s > 0) { p.size = (short) wclamp ((int) s, 2, 3276); p.cm |= WM_SIZE; } }
			else if (X.is ("color")) { X.attrs ("val", v, sizeof v); p.color = !sicmp (v, "auto") ? AUTO : hex_color (v); if (p.color == 0) p.color = AUTO; p.cm |= WM_COLOR; }
			else if (X.is ("highlight")) { X.attrs ("val", v, sizeof v); p.hilite = w_highlight (v); p.cm |= WM_HILITE; }
			else if (X.is ("shd")) { X.attrs ("fill", v, sizeof v); unsigned c = !sicmp (v, "auto") ? AUTO : hex_color (v); if (c != AUTO) { p.hilite = c; p.cm |= WM_HILITE; } }
			else if (X.is ("vanish")) p.hidden = X.attr_on ();
			X.skip ();
		}
	}
	// A w:pPr's children (X at its start): into p; its style's id, the paragraph mark's properties, a
	// section's end.
	void pPr (XmlReader &X, WProps &p, char *pstyle, WProps *mark, bool *sect)
	{
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is ("pPr")) return; continue; }
			if (t != X_START) continue;
			char v[48];
			if (X.is ("pStyle")) { if (pstyle) X.attrs ("val", pstyle, 48); }
			else if (X.is ("keepNext")) { p.keepNext = X.attr_on (); p.pm |= WP_KEEPN; }
			else if (X.is ("keepLines")) { p.keepLines = X.attr_on (); p.pm |= WP_KEEPL; }
			else if (X.is ("widowControl")) { p.widow = X.attr_on (); p.pm |= WP_WIDOW; }
			else if (X.is ("pageBreakBefore")) { p.pageBreak = X.attr_on (); p.pm |= WP_BREAK; }
			else if (X.is ("numPr"))
			{
				int lv = 0, id = 0;
				for (int u; (u = X.next ()) != X_EOF; )
				{
					if (u == X_END && X.is ("numPr")) break;
					if (u != X_START) continue;
					if (X.is ("ilvl")) lv = (int) X.attr_int ("val", 0);
					else if (X.is ("numId")) id = (int) X.attr_int ("val", 0);
					X.skip ();
				}
				p.numId = id; p.ilvl = wclamp (lv, 0, 8); p.pm |= WP_NUM;
				continue;
			}
			else if (X.is ("tabs"))
			{
				for (int u; (u = X.next ()) != X_EOF; )
				{
					if (u == X_END && X.is ("tabs")) break;
					if (u != X_START || !X.is ("tab")) continue;
					char val[16], ld[16]; X.attrs ("val", val, sizeof val); X.attrs ("leader", ld, sizeof ld);
					int pos = (int) X.attr_int ("pos", 0);
					X.skip ();
					if (!sicmp (val, "clear")) { if (p.nclear < MAXTABS) p.clear[p.nclear++] = pos; continue; }
					if (pos <= 0 || p.ntab >= MAXTABS || !sicmp (val, "bar")) continue;
					TabStop ts; ts.pos = pos;
					ts.align = (unsigned char) (!sicmp (val, "center") ? TA_CENTER : !sicmp (val, "right") || !sicmp (val, "end") ? TA_RIGHT : !sicmp (val, "decimal") ? TA_DECIMAL : TA_LEFT);
					ts.leader = (unsigned char) (!sicmp (ld, "dot") || !sicmp (ld, "middleDot") ? TL_DOT : !sicmp (ld, "hyphen") ? TL_DASH : !sicmp (ld, "underscore") || !sicmp (ld, "heavy") ? TL_LINE : TL_NONE);
					int k = 0; while (k < p.ntab && p.tab[k].pos < pos) k++;
					for (int j = p.ntab; j > k; j--) p.tab[j] = p.tab[j - 1];
					p.tab[k] = ts; p.ntab++;
				}
				continue;
			}
			else if (X.is ("spacing"))
			{
				XBuf b;
				if (X.attr ("before", b)) { p.before = (short) wclamp ((int) strtol (b.str (), 0, 10), 0, 30000); p.pm |= WP_BEFORE; }
				if (X.attr_on ("beforeAutospacing") && X.attr ("beforeAutospacing", b)) { p.before = 280; p.pm |= WP_BEFORE; }
				if (X.attr ("after", b)) { p.after = (short) wclamp ((int) strtol (b.str (), 0, 10), 0, 30000); p.pm |= WP_AFTER; }
				if (X.attr_on ("afterAutospacing") && X.attr ("afterAutospacing", b)) { p.after = 280; p.pm |= WP_AFTER; }
				if (X.attr ("line", b))
				{
					int l = (int) strtol (b.str (), 0, 10);
					X.attrs ("lineRule", v, sizeof v);
					p.line = (short) (!v[0] || !sicmp (v, "auto") ? wclamp (l * 100 / 240, 50, 400) : 100);
					p.pm |= WP_LINE;
				}
			}
			else if (X.is ("ind"))
			{
				XBuf b;
				if (X.attr ("left", b) || X.attr ("start", b)) { p.left = (short) wclamp ((int) strtol (b.str (), 0, 10), -5000, 30000); p.pm |= WP_LEFT; }
				if (X.attr ("right", b) || X.attr ("end", b)) { p.right = (short) wclamp ((int) strtol (b.str (), 0, 10), -5000, 30000); p.pm |= WP_RIGHT; }
				if (X.attr ("firstLine", b)) { p.first = (short) wclamp ((int) strtol (b.str (), 0, 10), 0, 30000); p.pm |= WP_FIRST; }
				if (X.attr ("hanging", b)) { p.first = (short) -wclamp ((int) strtol (b.str (), 0, 10), 0, 30000); p.pm |= WP_FIRST; }
			}
			else if (X.is ("jc"))
			{
				X.attrs ("val", v, sizeof v);
				p.align = (unsigned char) (!sicmp (v, "center") ? AL_CENTER : !sicmp (v, "right") || !sicmp (v, "end") ? AL_RIGHT : !sicmp (v, "both") || !sicmp (v, "distribute") ? AL_JUSTIFY : AL_LEFT);
				p.pm |= WP_ALIGN;
			}
			else if (X.is ("rPr")) { if (mark) rPr (X, *mark); else X.skip (); continue; }
			else if (X.is ("sectPr")) { sectPr (X); if (sect) *sect = true; continue; }
			X.skip ();
		}
	}
	// A section's page (the last one read wins), its headers' and footers' parts.
	void sectPr (XmlReader &X)
	{
		PageSetup &pg = d.page;
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is ("sectPr")) return; continue; }
			if (t != X_START) continue;
			char v[32], id[32];
			if (X.is ("pgSz")) { pg.w = (int) X.attr_int ("w", pg.w); pg.h = (int) X.attr_int ("h", pg.h); }
			else if (X.is ("pgMar"))
			{
				auto a = [&] (const char *k, int def) { long x = X.attr_int (k, def); return (int) (x < 0 ? -x : x); };
				pg.top = a ("top", pg.top); pg.bottom = a ("bottom", pg.bottom); pg.left = a ("left", pg.left); pg.right = a ("right", pg.right);
				pg.hdr = a ("header", pg.hdr); pg.ftr = a ("footer", pg.ftr);
			}
			else if (X.is ("titlePg")) pg.titlePg = X.attr_on ();
			else if (X.is ("pgNumType")) { long s = X.attr_int ("start", -1); if (s >= 0) pg.start = (int) wclamp (s, 0L, 9999L); }
			else if (X.is ("headerReference") || X.is ("footerReference"))
			{
				bool f = X.is ("footerReference");
				X.attrs ("type", v, sizeof v); X.attrs ("id", id, sizeof id);
				int s = !sicmp (v, "first") ? (f ? SY_FOOTER1 : SY_HEADER1) : !sicmp (v, "even") ? -1 : (f ? SY_FOOTER : SY_HEADER);
				if (s > 0) scpy (hdrId[s], id, 32);
			}
			X.skip ();
		}
	}

	// ---- styles, lists, theme, settings ----
	int style_index (const char *id, int type)
	{
		for (int i = 0; i < nsty; i++) if (sty[i].type == type && !strcmp (sty[i].id, id)) return i;
		return -1;
	}
	int default_style (int type) { for (int i = 0; i < nsty; i++) if (sty[i].type == type && sty[i].dflt) return i; return -1; }
	void chain (int si, WProps &out, int depth = 0)		// (a style's properties, its bases' first)
	{
		if (si < 0 || depth > 12) return;
		if (sty[si].based[0]) chain (style_index (sty[si].based, sty[si].type), out, depth + 1);
		wprops_apply (out, sty[si].p);
	}
	int our_style (int si, int depth = 0)
	{
		if (si < 0 || depth > 12) return -1;
		if (sty[si].st >= 0) return sty[si].st;
		return sty[si].based[0] ? our_style (style_index (sty[si].based, sty[si].type), depth + 1) : -1;
	}
	void read_theme ()
	{
		const char *p = target_of ("theme");
		int n; char *x = p ? get (p, &n) : 0;
		if (!x) return;
		XmlReader X (x, n);
		int which = 0;
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t != X_START) continue;
			if (X.is ("majorFont")) which = 1; else if (X.is ("minorFont")) which = 2;
			else if (X.is ("latin") && which) { char f[48]; if (X.attrs ("typeface", f, sizeof f) && f[0]) scpy (which == 1 ? major : minor, f, 48); which = 0; }
		}
		delete[] x;
	}
	void read_styles ()
	{
		const char *p = target_of ("styles");
		int n; char *x = get (p ? p : "word/styles.xml", &n);
		if (!x) return;
		XmlReader X (x, n);
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t != X_START) continue;
			if (X.is ("rPrDefault")) { for (int u; (u = X.next ()) != X_EOF; ) { if (u == X_END && X.is ("rPrDefault")) break; if (u == X_START && X.is ("rPr")) rPr (X, defs); } continue; }
			if (X.is ("pPrDefault")) { for (int u; (u = X.next ()) != X_EOF; ) { if (u == X_END && X.is ("pPrDefault")) break; if (u == X_START && X.is ("pPr")) pPr (X, defs, 0, 0, 0); } continue; }
			if (!X.is ("style")) continue;
			if (nsty == stycap) { int c = stycap * 2 + 32; WStyle *ns = new WStyle[c]; for (int i = 0; i < nsty; i++) ns[i] = sty[i]; delete[] sty; sty = ns; stycap = c; }
			WStyle &s = sty[nsty++];
			char ty[16]; X.attrs ("type", ty, sizeof ty);
			s.type = !sicmp (ty, "paragraph") ? 0 : !sicmp (ty, "character") ? 1 : !sicmp (ty, "table") ? 2 : 3;
			X.attrs ("styleId", s.id, sizeof s.id);
			s.dflt = X.attr_on ("default") && X.attrs ("default", ty, sizeof ty);
			s.based[0] = 0; s.st = -1; wprops_init (s.p);
			char name[64] = "";
			for (int u; (u = X.next ()) != X_EOF; )
			{
				if (u == X_END && X.is ("style")) break;
				if (u != X_START) continue;
				if (X.is ("name")) X.attrs ("val", name, sizeof name);
				else if (X.is ("basedOn")) X.attrs ("val", s.based, sizeof s.based);
				else if (X.is ("rPr")) { rPr (X, s.p); continue; }
				else if (X.is ("pPr")) { pPr (X, s.p, 0, 0, 0); continue; }
				X.skip ();
			}
			if (s.type == 0) s.st = w_style_by_name (name[0] ? name : s.id);
			if (s.type == 2 && (!sicmp (name, "Table Grid") || !sicmp (s.id, "TableGrid"))) s.st = 1;	// (a table style with its grid's lines)
		}
		delete[] x;
	}
	void read_numbering ()
	{
		const char *p = target_of ("numbering");
		int n; char *x = p ? get (p, &n) : 0;
		if (!x) return;
		int acap = 16, ncap = 16;
		abs = new WAbs[acap]; num = new WNum[ncap];
		XmlReader X (x, n);
		WAbs *cur = 0; int lvl = 0; WNum *cn = 0;
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is ("abstractNum")) cur = 0; else if (X.is ("num")) cn = 0; continue; }
			if (t != X_START) continue;
			if (X.is ("abstractNum"))
			{
				if (nabs == acap) { WAbs *na = new WAbs[acap * 2]; for (int i = 0; i < nabs; i++) na[i] = abs[i]; delete[] abs; abs = na; acap *= 2; }
				cur = &abs[nabs++]; cur->id = (int) X.attr_int ("abstractNumId", -1);
				for (int i = 0; i < 9; i++) { cur->fmt[i] = LS_NUMBER; cur->left[i] = (short) (720 * (i + 1)); cur->hang[i] = 360; }
			}
			else if (X.is ("lvl") && cur) lvl = wclamp ((int) X.attr_int ("ilvl", 0), 0, 8);
			else if (X.is ("numFmt") && cur) { char v[24]; X.attrs ("val", v, sizeof v); cur->fmt[lvl] = (unsigned char) (!sicmp (v, "bullet") ? LS_BULLET : !sicmp (v, "none") ? LS_NONE : LS_NUMBER); }
			else if (X.is ("ind") && cur)
			{
				XBuf b;
				if (X.attr ("left", b) || X.attr ("start", b)) cur->left[lvl] = (short) strtol (b.str (), 0, 10);
				if (X.attr ("hanging", b)) cur->hang[lvl] = (short) strtol (b.str (), 0, 10);
			}
			else if (X.is ("num"))
			{
				if (nnum == ncap) { WNum *nn = new WNum[ncap * 2]; for (int i = 0; i < nnum; i++) nn[i] = num[i]; delete[] num; num = nn; ncap *= 2; }
				cn = &num[nnum++]; cn->id = (int) X.attr_int ("numId", -1); cn->abs = -1;
			}
			else if (X.is ("abstractNumId") && cn) cn->abs = (int) X.attr_int ("val", -1);
		}
		delete[] x;
	}
	const WAbs *list_of (int numId)
	{
		for (int i = 0; i < nnum; i++)
			if (num[i].id == numId) { for (int k = 0; k < nabs; k++) if (abs[k].id == num[i].abs) return &abs[k]; return 0; }
		return 0;
	}
	void read_settings ()
	{
		const char *p = target_of ("settings");
		int n; char *x = p ? get (p, &n) : 0;
		if (!x) return;
		XmlReader X (x, n);
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t != X_START || !X.is ("docVar")) continue;
			char nm[64]; X.attrs ("name", nm, sizeof nm);
			if (!sicmp (nm, "OnyxMergeSource")) X.attrs ("val", d.mergeSrc, sizeof d.mergeSrc);
		}
		delete[] x;
	}

	// ---- the text ----
	unsigned short cf_of (const WProps &p)
	{
		CharFmt f;
		f.font = (short) doc_font (d, p.cm & WM_FONT && p.font[0] ? p.font : minor);
		f.size = p.cm & WM_SIZE ? p.size : 20;
		f.flags = (unsigned short) (p.on & (CF_BOLD | CF_ITALIC | CF_UNDER | CF_STRIKE | CF_SUPER | CF_SUB));
		f.color = p.cm & WM_COLOR ? p.color : AUTO;
		f.hilite = p.cm & WM_HILITE ? p.hilite : AUTO;
		return doc_fmt (d, f);
	}
	// The paragraph's format from Word's properties (its style ours, else Normal).
	void para_fmt (Para *p, const WProps &w, int st)
	{
		ParaFmt pf = style_para (st < 0 ? ST_NORMAL : st);
		pf.align = AL_LEFT; pf.left = pf.right = pf.first = 0; pf.before = pf.after = 0; pf.line = 100;
		pf.keepNext = false; pf.keepLines = false; pf.widow = true; pf.pageBreak = false;
		if (w.pm & WP_ALIGN) pf.align = w.align;
		if (w.pm & WP_LEFT) pf.left = (short) wmax (0, (int) w.left);
		if (w.pm & WP_RIGHT) pf.right = (short) wmax (0, (int) w.right);
		if (w.pm & WP_FIRST) pf.first = w.first;
		if (w.pm & WP_BEFORE) pf.before = w.before;
		if (w.pm & WP_AFTER) pf.after = w.after;
		if (w.pm & WP_LINE) pf.line = w.line;
		if (w.pm & WP_KEEPN) pf.keepNext = w.keepNext;
		if (w.pm & WP_KEEPL) pf.keepLines = w.keepLines;
		if (w.pm & WP_WIDOW) pf.widow = w.widow;
		if (w.pm & WP_BREAK) pf.pageBreak = w.pageBreak;
		pf.ntab = (unsigned char) wmin (w.ntab, (int) MAXTABS);
		for (int i = 0; i < pf.ntab; i++) pf.tab[i] = w.tab[i];
		if ((w.pm & WP_NUM) && w.numId > 0)
		{
			const WAbs *a = list_of (w.numId);
			int lv = wclamp (w.ilvl, 0, 8);
			int kind = a ? a->fmt[lv] : LS_NUMBER;
			if (kind != LS_NONE)
			{
				pf.list = (unsigned char) kind; pf.level = (unsigned char) wmin (lv, 5);
				if (!(w.pm & WP_LEFT)) pf.left = a ? a->left[lv] : (short) (720 * (lv + 1));
				if (!(w.pm & WP_FIRST)) pf.first = (short) -(a ? a->hang[lv] : 360);
			}
		}
		if (pf.first < -pf.left) pf.first = (short) -pf.left;
		p->pf = pf;
	}
	// A paragraph begins: its properties (Word's layers), its runs' base.
	void para_begin (const char *pstyle, const WProps &direct, const WProps &mark)
	{
		int si = pstyle[0] ? style_index (pstyle, 0) : -1;
		if (si < 0) si = default_style (0);
		pst = our_style (si);
		wprops_init (pp); wprops_apply (pp, defs); chain (si, pp); wprops_apply (pp, direct);
		wprops_init (rbase); wprops_apply (rbase, defs); chain (si, rbase);
		rbase.pm = 0; rbase.ntab = rbase.nclear = 0;
		WProps m = rbase; wprops_apply (m, mark);
		markCf = cf_of (m);
		q = para_new ();
		if (brkPending) { pp.pageBreak = true; pp.pm |= WP_BREAK; brkPending = false; }
	}
	// ... ends: into the table's cell, or the story.
	void para_end ()
	{
		para_fmt (q, pp, pst);
		q->endCf = markCf;
		if (sectNext && !tb) { q->pf.pageBreak = true; sectNext = false; }
		if (tb) tb->para (q); else story_append (d, story, q);
		q = 0;
	}
	// Text after a page break: the paragraph split there (the rest on a new page).
	void split ()
	{
		if (!brkPending) return;
		brkPending = false;
		WProps keep = pp; unsigned short mk = markCf; int st = pst;
		para_end ();
		q = para_new (); pp = keep; markCf = mk; pst = st; pp.pageBreak = true; pp.pm |= WP_BREAK;
	}
	void put (unsigned c, unsigned short cf) { split (); para_insert (q, q->len, &c, 1, cf); }
	bool hidden_now () { for (int i = 0; i < fdepth; i++) if (fs[i].stage == 0 || (fs[i].stage == 1 && fs[i].ours)) return true; return false; }
	void field_char (const char *inst, unsigned short cf)
	{
		char arg[64]; int kind = field_parse (inst, arg, sizeof arg);
		if (kind == FK_NONE) return;
		CharFmt f = d.fmt[cf]; f.fld = doc_field (d, kind, arg);
		put (FIELD_CHAR, doc_fmt (d, f));
	}
	void image (const char *rid, long cx, long cy, unsigned short cf)
	{
		const char *p = rid[0] ? target (rid) : 0;
		if (!p) return;
		pngsave::ZipEntry e;
		if (!pngsave::zip_find (z, zn, p, &e)) return;
		int len; char *b = get (p, &len);
		if (!b) return;
		int img = image_from_bytes (d, (const unsigned char *) b, (unsigned) len);
		delete[] b;
		if (!img) return;
		CharFmt f = d.fmt[cf]; f.obj = img;
		const Image &im = d.img[img - 1];
		f.ow = cx > 0 ? (int) (cx / 635) : im.w * 15; f.oh = cy > 0 ? (int) (cy / 635) : im.h * 15;
		put (OBJ_CHAR, doc_fmt (d, f));
	}
	// A run (X at w:r's start).
	void run (XmlReader &X)
	{
		WProps rp = rbase;
		unsigned short cf = 0xFFFF;
		auto fmt = [&] () { if (cf == 0xFFFF) cf = cf_of (rp); return cf; };
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is ("r")) return; continue; }
			if (t != X_START) continue;
			if (X.is ("rPr"))
			{
				WProps dr; wprops_init (dr); char rs[48];
				rPr (X, dr, rs);
				if (rs[0]) chain (style_index (rs, 1), rp);
				wprops_apply (rp, dr);
				cf = 0xFFFF;
				continue;
			}
			if (X.is ("fldChar"))
			{
				char ty[16]; X.attrs ("fldCharType", ty, sizeof ty);
				X.skip ();
				if (!sicmp (ty, "begin")) { if (fdepth < 8) { fs[fdepth].n = 0; fs[fdepth].inst[0] = 0; fs[fdepth].stage = 0; fs[fdepth].ours = false; } fdepth++; }
				else if (fdepth > 0 && fdepth <= 8 && (!sicmp (ty, "separate") || !sicmp (ty, "end")))
				{
					Fld &f = fs[fdepth - 1];
					if (f.stage == 0)
					{
						f.stage = 1;
						char arg[64];
						f.ours = field_parse (f.inst, arg, sizeof arg) != FK_NONE;
						bool outerHidden = false;
						for (int i = 0; i < fdepth - 1; i++) if (fs[i].stage == 0 || fs[i].ours) outerHidden = true;
						if (f.ours && !outerHidden) field_char (f.inst, fmt ());
					}
					if (!sicmp (ty, "end")) fdepth--;
				}
				else if (!sicmp (ty, "end") && fdepth > 0) fdepth--;
				continue;
			}
			if (X.is ("instrText"))
			{
				XBuf b;
				for (int u; (u = X.next ()) != X_EOF; ) { if (u == X_TEXT) b.putn (X.text.b, X.text.n); else if (u == X_END) break; else if (u == X_START) X.skip (); }
				if (fdepth > 0 && fdepth <= 8) { Fld &f = fs[fdepth - 1]; if (f.stage == 0) { for (int i = 0; i < b.n && f.n < 255; i++) f.inst[f.n++] = b.b[i]; f.inst[f.n] = 0; } }
				continue;
			}
			if (X.is ("AlternateContent") || X.is ("Choice")) continue;	// (markup compatibility: its choice read)
			if (X.is ("Fallback")) { X.skip (); continue; }
			if (hidden_now () || rp.hidden) { X.skip (); continue; }
			if (X.is ("t"))
			{
				XBuf b;
				for (int u; (u = X.next ()) != X_EOF; ) { if (u == X_TEXT) b.putn (X.text.b, X.text.n); else if (u == X_END) break; else if (u == X_START) X.skip (); }
				unsigned *u = new unsigned[b.n + 1];
				int m = utf8_decode (b.b ? b.b : "", b.n, u, b.n + 1);
				int k = 0; for (int i = 0; i < m; i++) if (u[i] >= 32 || u[i] == '\t') u[k++] = u[i] == 0x2028 ? 0x0B : u[i];
				if (k) { split (); para_insert (q, q->len, u, k, fmt ()); }
				delete[] u;
				continue;
			}
			if (X.is ("tab")) put ('\t', fmt ());
			else if (X.is ("br"))
			{
				char ty[16]; X.attrs ("type", ty, sizeof ty);
				if (!sicmp (ty, "page") && !tb)			// (a page break: the rest a new paragraph, on a new page)
				{
					if (q->len == 0) { pp.pageBreak = true; pp.pm |= WP_BREAK; }
					else brkPending = true;
				}
				else if (sicmp (ty, "column")) put (0x0B, fmt ());
			}
			else if (X.is ("cr")) put (0x0B, fmt ());
			else if (X.is ("noBreakHyphen")) put (0x2011, fmt ());
			else if (X.is ("sym"))
			{
				char v[16]; X.attrs ("char", v, sizeof v);
				unsigned c = (unsigned) strtoul (v, 0, 16);
				if (c >= 0xF000) c -= 0xF000;
				if (c >= 32) put (c == 0xB7 || c == 0xA7 ? 0x2022 : c, fmt ());
			}
			else if (X.is ("drawing"))
			{
				long cx = 0, cy = 0; char rid[32] = "";
				for (int depth = 1; depth > 0; )
				{
					int u = X.next ();
					if (u == X_EOF) break;
					if (u == X_END) { depth--; continue; }
					if (u != X_START) continue;
					depth++;
					if (X.is ("extent") && !cx) { cx = X.attr_int ("cx", 0); cy = X.attr_int ("cy", 0); }
					else if (X.is ("blip") && !rid[0]) X.attrs ("embed", rid, sizeof rid);
				}
				image (rid, cx, cy, fmt ());
				continue;
			}
			else if (X.is ("pict") || X.is ("object"))
			{
				long cx = 0, cy = 0; char rid[32] = "";
				for (int depth = 1; depth > 0; )
				{
					int u = X.next ();
					if (u == X_EOF) break;
					if (u == X_END) { depth--; continue; }
					if (u != X_START) continue;
					depth++;
					if (X.is ("shape") && !cx)
					{
						char s[200]; X.attrs ("style", s, sizeof s);
						for (char *k = s; *k; k++)
						{
							if (!strncmp (k, "width:", 6)) cx = (long) len_twips (k + 6) * 635;
							if (!strncmp (k, "height:", 7)) cy = (long) len_twips (k + 7) * 635;
						}
					}
					else if (X.is ("imagedata") && !rid[0]) X.attrs ("id", rid, sizeof rid);
				}
				image (rid, cx, cy, fmt ());
				continue;
			}
			X.skip ();
		}
	}
	// A paragraph's content (runs, links, fields...), to the end of the element `end`.
	void inl (XmlReader &X, const char *end)
	{
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is (end)) return; continue; }
			if (t != X_START) continue;
			if (X.is ("r")) { run (X); continue; }
			if (X.is ("hyperlink") || X.is ("smartTag") || X.is ("ins") || X.is ("customXml") || X.is ("sdtContent") || X.is ("moveTo") || X.is ("dir") || X.is ("bdo"))
			{ char e[24]; int l = 0; for (int i = 0; i < X.nml && l < 23; i++) { if (X.nm[i] == ':') l = 0; else e[l++] = X.nm[i]; } e[l] = 0; inl (X, e); continue; }
			if (X.is ("sdt")) continue;
			if (X.is ("fldSimple")) { simple (X); continue; }
			X.skip ();
		}
	}
	// A simple field (X at w:fldSimple's start): one of Writer's, in its result's format; else its result.
	void simple (XmlReader &X)
	{
		char in[256]; X.attrs ("instr", in, sizeof in);
		char arg[64];
		if (field_parse (in, arg, sizeof arg) == FK_NONE || hidden_now ()) { inl (X, "fldSimple"); return; }
		WProps rp = rbase; bool got = false;
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is ("fldSimple")) break; continue; }
			if (t == X_START && X.is ("rPr") && !got) { WProps dr; wprops_init (dr); char rs[48]; rPr (X, dr, rs); if (rs[0]) chain (style_index (rs, 1), rp); wprops_apply (rp, dr); got = true; }
		}
		field_char (in, cf_of (rp));
	}
	// A paragraph (X at w:p's start).
	void paragraph (XmlReader &X)
	{
		WProps direct, mark; wprops_init (direct); wprops_init (mark);
		char pstyle[48] = ""; bool sect = false, begun = false;
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is ("p")) break; continue; }
			if (t != X_START) continue;
			if (X.is ("pPr")) { pPr (X, direct, pstyle, &mark, &sect); continue; }
			if (!begun) { para_begin (pstyle, direct, mark); begun = true; }
			if (X.is ("r")) { run (X); continue; }
			if (X.is ("hyperlink") || X.is ("smartTag") || X.is ("ins") || X.is ("customXml") || X.is ("sdtContent") || X.is ("moveTo") || X.is ("fldSimple") || X.is ("sdt") || X.is ("dir") || X.is ("bdo"))
			{
				if (X.is ("sdt")) continue;
				if (X.is ("fldSimple")) { simple (X); continue; }
				char e[24]; int l = 0; for (int i = 0; i < X.nml && l < 23; i++) { if (X.nm[i] == ':') l = 0; else e[l++] = X.nm[i]; } e[l] = 0;
				inl (X, e);
				continue;
			}
			X.skip ();
		}
		if (!begun) para_begin (pstyle, direct, mark);
		para_end ();
		if (sect) sectNext = true;
	}
	// Block content (paragraphs, tables) to the end of the element `end`.
	void block (XmlReader &X, const char *end)
	{
		for (int t; (t = X.next ()) != X_EOF; )
		{
			if (t == X_END) { if (X.is (end)) return; continue; }
			if (t != X_START) continue;
			if (X.is ("p")) paragraph (X);
			else if (X.is ("tbl")) table (X);
			else if (X.is ("sdt") || X.is ("customXml")) continue;
			else if (X.is ("sdtContent")) block (X, "sdtContent");
			else if (X.is ("sectPr")) sectPr (X);
			else X.skip ();
		}
	}
	// A table (X at w:tbl's start); in a cell: its cells' paragraphs in that cell.
	void table (XmlReader &X)
	{
		if (tb)								// (a table in a table: flattened)
		{
			for (int t; (t = X.next ()) != X_EOF; )
			{
				if (t == X_END) { if (X.is ("tbl")) return; continue; }
				if (t != X_START) continue;
				if (X.is ("tc")) block (X, "tc");
				else if (X.is ("tblPr") || X.is ("tblGrid") || X.is ("trPr") || X.is ("tblPrEx")) X.skip ();
			}
			return;
		}
		TableBuild t; t.init ();
		tb = &t;
		bool bIn = false, bInH = false, bInV = false, bOut = false, bAny = false, bSet = false, gridStyle = false;
		int bsz = 4; unsigned bcol = 0;
		int col = 0; bool rowMade = false; int rowH = 0; bool hdrRow = false;
		for (int u; (u = X.next ()) != X_EOF; )
		{
			if (u == X_END) { if (X.is ("tbl")) break; if (X.is ("tr")) { if (!rowMade) t.row (rowH); rowMade = false; } continue; }
			if (u != X_START) continue;
			if (X.is ("tblPr"))
			{
				for (int v; (v = X.next ()) != X_EOF; )
				{
					if (v == X_END && X.is ("tblPr")) break;
					if (v != X_START) continue;
					char s[48];
					if (X.is ("tblStyle")) { X.attrs ("val", s, sizeof s); int si = style_index (s, 2); if ((si >= 0 && sty[si].st == 1) || !sicmp (s, "TableGrid")) gridStyle = true; }
					else if (X.is ("jc")) { X.attrs ("val", s, sizeof s); t.align = (unsigned char) (!sicmp (s, "center") ? AL_CENTER : !sicmp (s, "right") || !sicmp (s, "end") ? AL_RIGHT : AL_LEFT); }
					else if (X.is ("tblInd")) t.indent = (int) wclamp (X.attr_int ("w", 0), -5000L, 20000L);
					else if (X.is ("tblBorders"))
					{
						bSet = true;
						for (int w; (w = X.next ()) != X_EOF; )
						{
							if (w == X_END && X.is ("tblBorders")) break;
							if (w != X_START) continue;
							char val[24]; X.attrs ("val", val, sizeof val);
							bool on = val[0] && sicmp (val, "none") && sicmp (val, "nil");
							if (on)
							{
								if (X.is ("insideH")) bInH = true; else if (X.is ("insideV")) bInV = true; else bOut = true;
								long sz = X.attr_int ("sz", 4); if (sz > 0) bsz = (int) sz;
								char c[16]; if (X.attrs ("color", c, sizeof c) && sicmp (c, "auto")) { unsigned cc = hex_color (c); if (cc != AUTO) bcol = cc; }
							}
							X.skip ();
						}
						continue;
					}
					X.skip ();
				}
				continue;
			}
			if (X.is ("tblGrid"))
			{
				for (int v; (v = X.next ()) != X_EOF; )
				{
					if (v == X_END && X.is ("tblGrid")) break;
					if (v == X_START && X.is ("gridCol") && t.ncols < MAXCOLS) t.colW[t.ncols++] = (int) wclamp (X.attr_int ("w", 1440), 60L, 30000L);
					if (v == X_START) X.skip ();
				}
				continue;
			}
			if (X.is ("tr")) { col = 0; rowMade = false; rowH = 0; hdrRow = false; continue; }
			if (X.is ("trPr") || X.is ("tblPrEx"))
			{
				bool pr = X.is ("trPr");
				for (int v; (v = X.next ()) != X_EOF; )
				{
					if (v == X_END && (X.is ("trPr") || X.is ("tblPrEx"))) break;
					if (v != X_START) continue;
					if (pr && X.is ("gridBefore")) col = (int) X.attr_int ("val", 0);
					else if (pr && X.is ("trHeight")) rowH = (int) X.attr_int ("val", 0);
					else if (pr && X.is ("tblHeader") && X.attr_on ()) hdrRow = true;
					X.skip ();
				}
				if (hdrRow && t.nr == 0) t.header = true;
				continue;
			}
			if (X.is ("tc"))
			{
				if (!rowMade) { t.row (rowH); rowMade = true; }
				int span = 1; bool vcont = false; unsigned fill = AUTO;
				bool made = false;
				for (int v; (v = X.next ()) != X_EOF; )
				{
					if (v == X_END) { if (X.is ("tc")) break; continue; }
					if (v != X_START) continue;
					if (X.is ("tcPr"))
					{
						for (int w; (w = X.next ()) != X_EOF; )
						{
							if (w == X_END && X.is ("tcPr")) break;
							if (w != X_START) continue;
							char s[24];
							if (X.is ("gridSpan")) span = (int) wclamp (X.attr_int ("val", 1), 1L, 63L);
							else if (X.is ("vMerge")) { X.attrs ("val", s, sizeof s); vcont = sicmp (s, "restart") != 0; }
							else if (X.is ("shd")) { X.attrs ("fill", s, sizeof s); if (sicmp (s, "auto")) fill = hex_color (s); }
							else if (X.is ("tcBorders"))
							{
								for (int y; (y = X.next ()) != X_EOF; )
								{
									if (y == X_END && X.is ("tcBorders")) break;
									if (y != X_START) continue;
									char val[24]; X.attrs ("val", val, sizeof val);
									if (val[0] && sicmp (val, "none") && sicmp (val, "nil")) bAny = true;
									X.skip ();
								}
								continue;
							}
							X.skip ();
						}
						continue;
					}
					if (!made) { t.cell (col, span, vcont, 1, fill); made = true; }
					if (X.is ("p")) paragraph (X);
					else if (X.is ("tbl")) table (X);
					else if (X.is ("sdtContent")) block (X, "sdtContent");
					else if (X.is ("sdt") || X.is ("customXml")) continue;
					else X.skip ();
				}
				if (!made) t.cell (col, span, vcont, 1, fill);
				col += span;
				continue;
			}
		}
		if (t.ncols == 0)							// (no grid: the widest row's cells, alike)
		{
			int w = 0; for (int r = 0; r < t.nr; r++) { int e = 0; for (int k = 0; k < t.rows[r].n; k++) e = wmax (e, t.rows[r].c[k].col + t.rows[r].c[k].cs); w = wmax (w, e); }
			t.ncols = wclamp (w, 1, (int) MAXCOLS);
			for (int c = 0; c < t.ncols; c++) t.colW[c] = (d.page.w - d.page.left - d.page.right) / t.ncols;
		}
		t.border = (unsigned char) (bSet ? (bIn = bInH && bInV, bIn ? TB_ALL : bInH && bOut ? TB_ROWS : bOut ? TB_OUTER : TB_NONE)
						  : gridStyle || bAny ? TB_ALL : TB_NONE);
		t.bw = (unsigned char) wclamp (bsz, 1, 96); t.bcolor = bcol;
		tb = 0;
		t.finish (d, story);
	}
	// A header's / footer's part into its story.
	void hf (int s)
	{
		if (!hdrId[s][0]) return;
		const char *p = target (hdrId[s]);
		if (!p) return;
		char path[160]; scpy (path, p, sizeof path);
		int n; char *x = get (path, &n);
		if (!x) return;
		WRel *keep = rel; int nk = nrel; char pk[160]; scpy (pk, part, sizeof pk);
		rel = 0; nrel = 0;
		rels (path);
		story = s;
		XmlReader X (x, n);
		for (int t; (t = X.next ()) != X_EOF; )
			if (t == X_START && (X.is ("hdr") || X.is ("ftr"))) { block (X, X.is ("hdr") ? "hdr" : "ftr"); break; }
		story = SY_BODY;
		delete[] rel; rel = keep; nrel = nk; scpy (part, pk, sizeof part);
		delete[] x;
	}
};

static bool docx_load (Doc &d, const char *b, int n)
{
	doc_clear (d);
	const unsigned char *z = (const unsigned char *) b;
	DocxIn in (d, z, (unsigned) n);
	char main[160] = "word/document.xml";
	in.rels ("");
	if (const char *m = in.target_of ("officeDocument")) scpy (main, m, sizeof main);
	int dn; char *x = in.get (main, &dn);
	if (!x) return false;
	in.rels (main);
	in.read_theme ();
	in.read_styles ();
	in.read_numbering ();
	in.read_settings ();
	d.page.w = A4_W; d.page.h = A4_H; d.page.top = d.page.bottom = 1440; d.page.left = d.page.right = 1440; d.page.hdr = d.page.ftr = 709;
	XmlReader X (x, dn);
	for (int t; (t = X.next ()) != X_EOF; )
		if (t == X_START && X.is ("body")) { in.block (X, "body"); break; }
	delete[] x;
	for (int s = SY_HEADER; s < SY_COUNT; s++) in.hf (s);
	int bn; story_p (d, SY_BODY, &bn);
	if (bn == 0) story_append (d, SY_BODY, para_styled (d, ST_NORMAL));
	doc_fix (d);
	PageSetup &pg = d.page;
	pg.w = wclamp (pg.w, 2880, 40000); pg.h = wclamp (pg.h, 2880, 40000);
	pg.left = wclamp (pg.left, 0, pg.w / 3); pg.right = wclamp (pg.right, 0, pg.w / 3);
	pg.top = wclamp (pg.top, 0, pg.h / 3); pg.bottom = wclamp (pg.bottom, 0, pg.h / 3);
	pg.hdr = wclamp (pg.hdr, 0, pg.h / 3); pg.ftr = wclamp (pg.ftr, 0, pg.h / 3);
	return true;
}

// ---- writing ------------------------------------------------------------------------------------------------------
static const char *W_NS = " xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""
			  " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
			  " xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\""
			  " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
			  " xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"";

struct DocxOut
{
	Doc &d;
	int *imgRel;				// each image's part number (0: not written yet)
	int nimgParts; bool *imgJpeg;
	int *numRun;				// each numbered list's own numbering (the body's runs)
	int docPr;
	DocxOut (Doc &d_) : d (d_), nimgParts (0), docPr (0)
	{
		imgRel = new int[d.nimg + 1]; imgJpeg = new bool[d.nimg + 1]; numRun = 0;
		for (int i = 0; i <= d.nimg; i++) { imgRel[i] = 0; imgJpeg[i] = false; }
	}
	~DocxOut () { delete[] imgRel; delete[] imgJpeg; delete[] numRun; }

	void rPr (XBuf &o, const CharFmt &f)
	{
		o.puts ("<w:rPr><w:rFonts w:ascii=\""); xml_esc1 (o, d.fontName[f.font]); o.puts ("\" w:hAnsi=\""); xml_esc1 (o, d.fontName[f.font]);
		o.puts ("\" w:cs=\""); xml_esc1 (o, d.fontName[f.font]); o.puts ("\"/>");
		o.puts (f.flags & CF_BOLD ? "<w:b/>" : "<w:b w:val=\"0\"/>");
		o.puts (f.flags & CF_ITALIC ? "<w:i/>" : "<w:i w:val=\"0\"/>");
		if (f.flags & CF_STRIKE) o.puts ("<w:strike/>");
		o.puts ("<w:color w:val=\""); if (f.color == AUTO) o.puts ("auto"); else color_hex (o, f.color); o.puts ("\"/>");
		o.puts ("<w:sz w:val=\""); o.num (f.size); o.puts ("\"/><w:szCs w:val=\""); o.num (f.size); o.puts ("\"/>");
		if (f.flags & CF_UNDER) o.puts ("<w:u w:val=\"single\"/>");
		if (f.hilite != AUTO) { o.puts ("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\""); color_hex (o, f.hilite); o.puts ("\"/>"); }
		if (f.flags & (CF_SUPER | CF_SUB)) o.puts (f.flags & CF_SUPER ? "<w:vertAlign w:val=\"superscript\"/>" : "<w:vertAlign w:val=\"subscript\"/>");
		o.puts ("</w:rPr>");
	}
	void text_run (XBuf &o, const CharFmt &f, const unsigned *u, int n)
	{
		o.puts ("<w:r>"); rPr (o, f);
		int i = 0;
		while (i < n)
		{
			if (u[i] == '\t') { o.puts ("<w:tab/>"); i++; continue; }
			if (u[i] == 0x0B) { o.puts ("<w:br/>"); i++; continue; }
			int j = i; while (j < n && u[j] != '\t' && u[j] != 0x0B) j++;
			o.puts ("<w:t xml:space=\"preserve\">"); xml_escu (o, u + i, j - i); o.puts ("</w:t>");
			i = j;
		}
		o.puts ("</w:r>");
	}
	void image_run (XBuf &o, const CharFmt &f)
	{
		int im = f.obj - 1;
		if (!imgRel[im]) imgRel[im] = ++nimgParts;
		docPr++;
		long cx = (long) f.ow * 635, cy = (long) f.oh * 635;
		o.puts ("<w:r>"); rPr (o, f);
		o.puts ("<w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\""); o.num (cx); o.puts ("\" cy=\""); o.num (cy);
		o.puts ("\"/><wp:docPr id=\""); o.num (docPr); o.puts ("\" name=\"Picture "); o.num (docPr);
		o.puts ("\"/><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr><pic:cNvPr id=\"");
		o.num (docPr); o.puts ("\" name=\"image"); o.num (imgRel[im]); o.puts ("\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"rIdImg");
		o.num (imgRel[im]); o.puts ("\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"");
		o.num (cx); o.puts ("\" cy=\""); o.num (cy); o.puts ("\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>");
	}
	// A paragraph (its number in a numbered list's own numbering: numId).
	void para (XBuf &o, const Para *q, int numId, bool tocBegin, bool tocEnd)
	{
		const ParaFmt &pf = q->pf;
		o.puts ("<w:p><w:pPr><w:pStyle w:val=\""); o.puts (W_STYLE_ID[pf.style < ST_COUNT ? pf.style : 0]); o.puts ("\"/>");
		o.puts (pf.keepNext ? "<w:keepNext/>" : "<w:keepNext w:val=\"0\"/>");
		if (pf.keepLines) o.puts ("<w:keepLines/>");
		if (pf.pageBreak) o.puts ("<w:pageBreakBefore/>");
		o.puts (pf.widow ? "<w:widowControl/>" : "<w:widowControl w:val=\"0\"/>");
		if (pf.list != LS_NONE) { o.puts ("<w:numPr><w:ilvl w:val=\""); o.num (pf.level); o.puts ("\"/><w:numId w:val=\""); o.num (numId); o.puts ("\"/></w:numPr>"); }
		if (pf.ntab)
		{
			o.puts ("<w:tabs>");
			static const char *const al[4] = { "left", "center", "right", "decimal" };
			static const char *const ld[4] = { "none", "dot", "hyphen", "underscore" };
			for (int i = 0; i < pf.ntab; i++)
			{
				o.puts ("<w:tab w:val=\""); o.puts (al[pf.tab[i].align & 3]); o.puts ("\"");
				if (pf.tab[i].leader) { o.puts (" w:leader=\""); o.puts (ld[pf.tab[i].leader & 3]); o.puts ("\""); }
				o.puts (" w:pos=\""); o.num (pf.tab[i].pos); o.puts ("\"/>");
			}
			o.puts ("</w:tabs>");
		}
		o.puts ("<w:spacing w:before=\""); o.num (pf.before); o.puts ("\" w:after=\""); o.num (pf.after);
		o.puts ("\" w:line=\""); o.num (pf.line * 240 / 100); o.puts ("\" w:lineRule=\"auto\"/>");
		o.puts ("<w:ind w:left=\""); o.num (pf.left); o.puts ("\" w:right=\""); o.num (pf.right); o.puts ("\"");
		if (pf.first > 0) { o.puts (" w:firstLine=\""); o.num (pf.first); o.puts ("\""); }
		else if (pf.first < 0) { o.puts (" w:hanging=\""); o.num (-pf.first); o.puts ("\""); }
		o.puts ("/>");
		static const char *const jc[4] = { "left", "center", "right", "both" };
		o.puts ("<w:jc w:val=\""); o.puts (jc[pf.align & 3]); o.puts ("\"/>");
		rPr (o, d.fmt[q->endCf]);
		o.puts ("</w:pPr>");
		if (tocBegin) o.puts ("<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> TOC \\o &quot;1-3&quot; \\h \\z \\u </w:instrText></w:r><w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>");
		for (int i = 0; i < q->len; )
		{
			const CharFmt &f = d.fmt[q->cf[i]];
			if (q->ch[i] == OBJ_CHAR && f.obj) { image_run (o, f); i++; continue; }
			if (q->ch[i] == FIELD_CHAR && f.fld)
			{
				char in[96]; field_instr (d.fld[f.fld - 1], in, sizeof in);
				o.puts ("<w:fldSimple w:instr=\" "); xml_esc (o, in); o.puts (" \">");
				unsigned t[80]; int tn = field_text (d, f.fld, 1, 1, t, 80);
				text_run (o, f, t, tn);
				o.puts ("</w:fldSimple>");
				i++;
				continue;
			}
			int j = i + 1;
			while (j < q->len && q->cf[j] == q->cf[i] && q->ch[j] != OBJ_CHAR && q->ch[j] != FIELD_CHAR) j++;
			text_run (o, f, q->ch + i, j - i);
			i = j;
		}
		if (tocEnd) o.puts ("<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>");
		o.puts ("</w:p>");
	}
	void border (XBuf &o, const char *side, bool on, const Table *t)
	{
		o.puts ("<w:"); o.puts (side);
		if (!on) { o.puts (" w:val=\"nil\"/>"); return; }
		o.puts (" w:val=\"single\" w:sz=\""); o.num (t->bw); o.puts ("\" w:space=\"0\" w:color=\"");
		if (t->bcolor == AUTO || t->bcolor == 0) o.puts ("auto"); else color_hex (o, t->bcolor);
		o.puts ("\"/>");
	}
	void table (XBuf &o, Para *const *p, int a, int b, int *numIds, int base)
	{
		const Table *t = para_table (d, p[a]);
		int nc = t->ncols;
		o.puts ("<w:tbl><w:tblPr><w:tblW w:w=\""); o.num (table_width (t)); o.puts ("\" w:type=\"dxa\"/>");
		if (t->align != AL_LEFT) o.puts (t->align == AL_CENTER ? "<w:jc w:val=\"center\"/>" : "<w:jc w:val=\"right\"/>");
		o.puts ("<w:tblInd w:w=\""); o.num (t->indent); o.puts ("\" w:type=\"dxa\"/>");
		bool all = t->border == TB_ALL, out = all || t->border == TB_OUTER, rows = all || t->border == TB_ROWS;
		o.puts ("<w:tblBorders>");
		border (o, "top", out || rows, t); border (o, "left", out, t); border (o, "bottom", out || rows, t); border (o, "right", out, t);
		border (o, "insideH", rows, t); border (o, "insideV", all, t);
		o.puts ("</w:tblBorders><w:tblLayout w:type=\"fixed\"/><w:tblCellMar><w:left w:w=\""); o.num (CELL_PAD_X);
		o.puts ("\" w:type=\"dxa\"/><w:right w:w=\""); o.num (CELL_PAD_X); o.puts ("\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr><w:tblGrid>");
		for (int c = 0; c < nc; c++) { o.puts ("<w:gridCol w:w=\""); o.num (t->colW[c]); o.puts ("\"/>"); }
		o.puts ("</w:tblGrid>");
		for (int r = 0; r < t->nrows; r++)
		{
			o.puts ("<w:tr>");
			if (t->rowH[r] || (r == 0 && t->header))
			{
				o.puts ("<w:trPr>");
				if (t->rowH[r]) { o.puts ("<w:trHeight w:val=\""); o.num (t->rowH[r]); o.puts ("\" w:hRule=\"atLeast\"/>"); }
				if (r == 0 && t->header) o.puts ("<w:tblHeader/>");
				o.puts ("</w:trPr>");
			}
			for (int c = 0; c < nc; )
			{
				int orow, ocol; cell_owner (t, r, c, &orow, &ocol);
				const TCell &k = tcell (t, orow, ocol);
				if (orow == r && ocol < c) { c++; continue; }
				bool cont = orow < r;
				int cs = cont ? k.cs - (c - ocol) : k.cs;
				if (cs < 1) cs = 1;
				int w = 0; for (int cc = c; cc < c + cs && cc < nc; cc++) w += t->colW[cc];
				o.puts ("<w:tc><w:tcPr><w:tcW w:w=\""); o.num (w); o.puts ("\" w:type=\"dxa\"/>");
				if (cs > 1) { o.puts ("<w:gridSpan w:val=\""); o.num (cs); o.puts ("\"/>"); }
				if (cont) o.puts ("<w:vMerge/>"); else if (k.rs > 1) o.puts ("<w:vMerge w:val=\"restart\"/>");
				if (k.fill != AUTO) { o.puts ("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\""); color_hex (o, k.fill); o.puts ("\"/>"); }
				o.puts ("</w:tcPr>");
				int first = -1, last = -1;
				if (!cont) for (int i = a; i < b; i++) if (p[i]->pf.row == r && p[i]->pf.col == c) { if (first < 0) first = i; last = i; }
				if (first < 0) o.puts ("<w:p/>");
				else for (int i = first; i <= last; i++) para (o, p[i], numIds ? numIds[i - base] : 1, false, false);
				o.puts ("</w:tc>");
				c += cs;
			}
			o.puts ("</w:tr>");
		}
		o.puts ("</w:tbl>");
	}
	// A story's paragraphs (the body's tables as tables, its table of contents as Word's field).
	void story (XBuf &o, Para *const *p, int n)
	{
		// each numbered list its own numbering (a list broken by other paragraphs starts again)
		int *ids = new int[n + 1];
		int run = numLists; bool in = false;
		for (int i = 0; i < n; i++)
		{
			if (p[i]->pf.list == LS_NUMBER) { if (!in) { run++; in = true; } ids[i] = 2 + run; }
			else { if (p[i]->pf.list == LS_NONE) in = false; ids[i] = 1; }
		}
		numLists = run;
		for (int i = 0; i < n; )
		{
			if (in_table (p[i]) && para_table (d, p[i]))
			{
				int j = i + 1; while (j < n && p[j]->pf.tbl == p[i]->pf.tbl) j++;
				table (o, p, i, j, ids, 0);
				i = j;
				continue;
			}
			bool toc = style_toc (p[i]->pf.style);
			bool tb = toc && (i == 0 || !style_toc (p[i - 1]->pf.style)), te = toc && (i + 1 >= n || !style_toc (p[i + 1]->pf.style));
			para (o, p[i], ids[i], tb, te);
			i++;
		}
		delete[] ids;
	}
	int numLists = 0;
};

static void docx_styles (XBuf &o, Doc &d)
{
	CharFmt n0 = style_fmt (d, ST_NORMAL);
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
	o.puts ("<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\""); xml_esc1 (o, d.fontName[n0.font]); o.puts ("\" w:hAnsi=\""); xml_esc1 (o, d.fontName[n0.font]);
	o.puts ("\" w:cs=\""); xml_esc1 (o, d.fontName[n0.font]); o.puts ("\"/><w:sz w:val=\"24\"/><w:szCs w:val=\"24\"/><w:lang w:val=\"en-GB\"/></w:rPr></w:rPrDefault>");
	o.puts ("<w:pPrDefault><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>");
	for (int i = 0; i < ST_COUNT; i++)
	{
		const Style &s = STYLES[i];
		o.puts ("<w:style w:type=\"paragraph\""); if (i == ST_NORMAL) o.puts (" w:default=\"1\"");
		o.puts (" w:styleId=\""); o.puts (W_STYLE_ID[i]); o.puts ("\"><w:name w:val=\""); o.puts (W_STYLE_NAME[i]); o.puts ("\"/>");
		if (i != ST_NORMAL) o.puts ("<w:basedOn w:val=\"Normal\"/>");
		o.puts ("<w:next w:val=\""); o.puts (i >= ST_H1 && i <= ST_SUBTITLE ? "Normal" : W_STYLE_ID[i]); o.puts ("\"/><w:qFormat/><w:pPr>");
		if (s.keepNext) o.puts ("<w:keepNext/>");
		o.puts ("<w:spacing w:before=\""); o.num (s.before); o.puts ("\" w:after=\""); o.num (s.after); o.puts ("\" w:line=\""); o.num (s.line * 240 / 100); o.puts ("\" w:lineRule=\"auto\"/>");
		if (s.left || s.right) { o.puts ("<w:ind w:left=\""); o.num (s.left); o.puts ("\" w:right=\""); o.num (s.right); o.puts ("\"/>"); }
		static const char *const jc[4] = { "left", "center", "right", "both" };
		o.puts ("<w:jc w:val=\""); o.puts (jc[s.align & 3]); o.puts ("\"/>");
		if (style_outline (i)) { o.puts ("<w:outlineLvl w:val=\""); o.num (style_outline (i) - 1); o.puts ("\"/>"); }
		o.puts ("</w:pPr><w:rPr><w:rFonts w:ascii=\""); xml_esc1 (o, s.font); o.puts ("\" w:hAnsi=\""); xml_esc1 (o, s.font); o.puts ("\"/>");
		if (s.flags & CF_BOLD) o.puts ("<w:b/>");
		if (s.flags & CF_ITALIC) o.puts ("<w:i/>");
		if (s.color != AUTO) { o.puts ("<w:color w:val=\""); color_hex (o, s.color); o.puts ("\"/>"); }
		o.puts ("<w:sz w:val=\""); o.num (s.size); o.puts ("\"/></w:rPr></w:style>");
	}
	o.puts ("</w:styles>");
}
static void docx_numbering (XBuf &o, int lists)
{
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
	static const char *const B[3] = { "\xE2\x80\xA2", "\xE2\x97\xA6", "\xE2\x96\xAA" };
	static const char *const F[3] = { "decimal", "lowerLetter", "lowerRoman" };
	for (int a = 0; a < 2; a++)
	{
		o.puts ("<w:abstractNum w:abstractNumId=\""); o.num (a); o.puts ("\"><w:multiLevelType w:val=\"hybridMultilevel\"/>");
		for (int l = 0; l < 9; l++)
		{
			o.puts ("<w:lvl w:ilvl=\""); o.num (l); o.puts ("\"><w:start w:val=\"1\"/><w:numFmt w:val=\"");
			o.puts (a == 0 ? "bullet" : F[l % 3]); o.puts ("\"/><w:lvlText w:val=\"");
			if (a == 0) o.puts (B[l % 3]); else { o.put ('%'); o.num (l + 1); o.put ('.'); }
			o.puts ("\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\""); o.num (720 * (l + 1)); o.puts ("\" w:hanging=\"360\"/></w:pPr></w:lvl>");
		}
		o.puts ("</w:abstractNum>");
	}
	o.puts ("<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num><w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>");
	for (int k = 1; k <= lists; k++)
	{
		o.puts ("<w:num w:numId=\""); o.num (2 + k); o.puts ("\"><w:abstractNumId w:val=\"1\"/>");
		for (int l = 0; l < 9; l++) { o.puts ("<w:lvlOverride w:ilvl=\""); o.num (l); o.puts ("\"><w:startOverride w:val=\"1\"/></w:lvlOverride>"); }
		o.puts ("</w:num>");
	}
	o.puts ("</w:numbering>");
}

static bool docx_save (Doc &d, unsigned char **out, unsigned *len)
{
	DocxOut w (d);
	XBuf doc, hf[SY_COUNT];
	// the headers and footers
	static const char *const hname[SY_COUNT] = { "", "header1.xml", "footer1.xml", "header2.xml", "footer2.xml" };
	for (int s = SY_HEADER; s < SY_COUNT; s++)
	{
		if (story_empty (d, s)) continue;
		int n; Para **p = story_p (d, s, &n);
		hf[s].puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
		hf[s].puts (is_footer (s) ? "<w:ftr" : "<w:hdr"); hf[s].puts (W_NS); hf[s].puts (">");
		w.story (hf[s], p, n);
		hf[s].puts (is_footer (s) ? "</w:ftr>" : "</w:hdr>");
	}
	// the body, its section
	doc.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document"); doc.puts (W_NS); doc.puts ("><w:body>");
	int n; Para **p = story_p (d, SY_BODY, &n);
	w.story (doc, p, n);
	const PageSetup &pg = d.page;
	doc.puts ("<w:sectPr>");
	static const char *const rid[SY_COUNT] = { "", "rIdH1", "rIdF1", "rIdH2", "rIdF2" };
	for (int s = SY_HEADER; s < SY_COUNT; s++)
	{
		if (!hf[s].n) continue;
		bool f = is_footer (s), first = s == SY_HEADER1 || s == SY_FOOTER1;
		doc.puts (f ? "<w:footerReference" : "<w:headerReference"); doc.puts (first ? " w:type=\"first\"" : " w:type=\"default\"");
		doc.puts (" r:id=\""); doc.puts (rid[s]); doc.puts ("\"/>");
	}
	doc.puts ("<w:pgSz w:w=\""); doc.num (pg.w); doc.puts ("\" w:h=\""); doc.num (pg.h); doc.puts ("\""); if (pg.w > pg.h) doc.puts (" w:orient=\"landscape\""); doc.puts ("/>");
	doc.puts ("<w:pgMar w:top=\""); doc.num (pg.top); doc.puts ("\" w:right=\""); doc.num (pg.right); doc.puts ("\" w:bottom=\""); doc.num (pg.bottom);
	doc.puts ("\" w:left=\""); doc.num (pg.left); doc.puts ("\" w:header=\""); doc.num (pg.hdr); doc.puts ("\" w:footer=\""); doc.num (pg.ftr); doc.puts ("\" w:gutter=\"0\"/>");
	if (pg.start != 1) { doc.puts ("<w:pgNumType w:start=\""); doc.num (pg.start); doc.puts ("\"/>"); }
	if (pg.titlePg) doc.puts ("<w:titlePg/>");
	doc.puts ("</w:sectPr></w:body></w:document>");
	// the package's parts
	XBuf ct, rels, drels, sty, num, set;
	ct.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
		 "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
		 "<Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/>"
		 "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
		 "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
		 "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
		 "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>"
		 "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>");
	for (int s = SY_HEADER; s < SY_COUNT; s++)
	{
		if (!hf[s].n) continue;
		ct.puts ("<Override PartName=\"/word/"); ct.puts (hname[s]); ct.puts ("\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.");
		ct.puts (is_footer (s) ? "footer+xml\"/>" : "header+xml\"/>");
	}
	ct.puts ("</Types>");
	rels.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		   "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>");
	drels.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
		    "<Relationship Id=\"rIdS\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
		    "<Relationship Id=\"rIdSet\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>"
		    "<Relationship Id=\"rIdN\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>");
	for (int s = SY_HEADER; s < SY_COUNT; s++)
	{
		if (!hf[s].n) continue;
		drels.puts ("<Relationship Id=\""); drels.puts (rid[s]); drels.puts ("\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/");
		drels.puts (is_footer (s) ? "footer" : "header"); drels.puts ("\" Target=\""); drels.puts (hname[s]); drels.puts ("\"/>");
	}
	pngsave::ZipOut zip;
	for (int i = 0; i < d.nimg; i++)
	{
		if (!w.imgRel[i]) continue;
		unsigned ln; bool jpeg, made;
		const unsigned char *bb = image_bytes (d.img[i], &ln, &jpeg, &made);
		char nm[64]; scpy (nm, "word/media/image", sizeof nm); int k = slen (nm);
		char t[12]; int j = 0, v = w.imgRel[i]; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) nm[k++] = t[--j];
		scpy (nm + k, jpeg ? ".jpeg" : ".png", (int) sizeof nm - k);
		zip.add (nm, bb, ln, false);
		drels.puts ("<Relationship Id=\"rIdImg"); drels.num (w.imgRel[i]); drels.puts ("\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"");
		drels.puts (nm + 5); drels.puts ("\"/>");
		if (made) delete[] bb;
	}
	drels.puts ("</Relationships>");
	XBuf hrels;						// (the headers' and footers' own: their images)
	hrels.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
	for (int i = 0; i < d.nimg; i++)
	{
		if (!w.imgRel[i]) continue;
		hrels.puts ("<Relationship Id=\"rIdImg"); hrels.num (w.imgRel[i]); hrels.puts ("\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/image");
		hrels.num (w.imgRel[i]); hrels.puts (d.img[i].data && d.img[i].len && d.img[i].jpeg ? ".jpeg\"/>" : ".png\"/>");
	}
	hrels.puts ("</Relationships>");
	docx_styles (sty, d);
	docx_numbering (num, w.numLists);
	set.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
		  "<w:zoom w:percent=\"100\"/><w:defaultTabStop w:val=\"709\"/><w:characterSpacingControl w:val=\"doNotCompress\"/>"
		  "<w:compat><w:compatSetting w:name=\"compatibilityMode\" w:uri=\"http://schemas.microsoft.com/office/word\" w:val=\"15\"/></w:compat>");
	if (d.mergeSrc[0]) { set.puts ("<w:docVars><w:docVar w:name=\"OnyxMergeSource\" w:val=\""); xml_esc1 (set, d.mergeSrc); set.puts ("\"/></w:docVars>"); }
	set.puts ("</w:settings>");
	zip.add ("[Content_Types].xml", ct.b, (unsigned) ct.n, true);
	zip.add ("_rels/.rels", rels.b, (unsigned) rels.n, true);
	zip.add ("word/document.xml", doc.b, (unsigned) doc.n, true);
	zip.add ("word/_rels/document.xml.rels", drels.b, (unsigned) drels.n, true);
	zip.add ("word/styles.xml", sty.b, (unsigned) sty.n, true);
	zip.add ("word/numbering.xml", num.b, (unsigned) num.n, true);
	zip.add ("word/settings.xml", set.b, (unsigned) set.n, true);
	for (int s = SY_HEADER; s < SY_COUNT; s++)
		if (hf[s].n)
		{
			char nm[48]; scpy (nm, "word/", sizeof nm); scpy (nm + 5, hname[s], 35); zip.add (nm, hf[s].b, (unsigned) hf[s].n, true);
			scpy (nm, "word/_rels/", sizeof nm); int k = slen (nm); scpy (nm + k, hname[s], 48 - k); k = slen (nm); scpy (nm + k, ".rels", 48 - k);
			zip.add (nm, hrels.b, (unsigned) hrels.n, true);
		}
	*out = zip.finish (len);
	return *out != 0;
}

} // namespace wr

#endif
