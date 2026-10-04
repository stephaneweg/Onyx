//
// pptx.h -- PowerPoint presentations (.pptx, Office Open XML): read and written.
//
// Written as PowerPoint writes them: the presentation (its slides, their size, the sections: p14:sectionLst), the
// theme (the deck's colours and fonts), one master (its background, its decorations, its text styles: the title,
// the body's levels, the other text) and a layout for each of Slides' (their placeholders: type and idx), the
// slides (p:sp -- a text box, a shape, a placeholder --, p:cxnSp a line, p:pic a picture in ppt/media, p:graphicFrame
// a table -- PowerPoint's own table style, the heading row and the bands -- or a chart: ppt/charts, its data cached),
// the footers (the slides' ftr / sldNum / dt placeholders), the transitions (p:transition, p14:dur), the effects
// (p:timing: PowerPoint's presets and their behaviours, a click a step; by paragraph: p:txEl), the speaker's notes
// (ppt/notesSlides, a notes master). Everything Slides holds has its word in the format: a file Slides writes is
// read back the same (the effects through a table of presets, both ways).
//
// Read: Slides' own files and PowerPoint's / LibreOffice's -- the theme (its colour map), the master's styles, the
// layouts (their placeholders: where a slide's placeholder sits when it says nothing, its layout's text formats),
// the shapes and their theme's style references, groups (flattened), pictures, connectors, tables, charts (bar,
// column, line, pie, area; the others as the nearest), the notes, the backgrounds, the transitions, the effects
// (PowerPoint's presets mapped to Slides' effects), the sections, the hidden slides.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_pptx_h
#define _slides_pptx_h

#include "odp.h"
#include <limits.h>

namespace sl {

// ---- the vocabulary -------------------------------------------------------------------------------------------------
static const char *const PRST[SH_COUNT] = { "rect", "roundRect", "ellipse", "triangle", "rtTriangle", "diamond", "pentagon", "hexagon",
	"octagon", "star5", "star4", "heart", "rightArrow", "leftArrow", "upArrow", "downArrow", "leftRightArrow", "chevron", "homePlate",
	"wedgeRoundRectCallout", "cloud", "plus", "parallelogram", "trapezoid", "can", "donut", "flowChartDocument", "flowChartTerminator" };
static const char *const SCHEME[TC_COUNT] = { "tx1", "bg1", "tx2", "bg2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink" };
static const char *const THEME_SLOT[TC_COUNT] = { "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink" };
static const char *const LAYOUT_TYPE[LY_COUNT] = { "title", "obj", "twoObj", "twoTxTwoObj", "secHead", "titleOnly", "picTx", "blank" };
static const char *const PH_TYPE[PH_COUNT] = { "", "title", "subTitle", "body", "body", "pic", "ftr", "sldNum", "dt" };
static const char *const TABLE_STYLE = "{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}";	// PowerPoint's "Medium Style 2 - Accent 1"

// The effects: (class, effect) <-> PowerPoint's preset (its id in the class), both ways
static const short FX_PRESET[3][FX_COUNT] = {
	// Appear Fade Fly Wipe Zoom Float Grow Pulse Spin Colour
	{ 1, 10, 2, 22, 53, 42, 23, 9, 31, 3 },			// entrance
	{ 35, 9, 32, 16, 14, 10, 6, 26, 8, 1 },			// emphasis
	{ 1, 10, 2, 22, 53, 42, 23, 9, 31, 3 },			// exit
};
static const char *const FX_CLASS[3] = { "entr", "emph", "exit" };
static const int DIR_SUB[5] = { 0, 8, 2, 1, 4 };		// DIR_* -> presetSubtype (from the left 8, right 2, top 1, bottom 4)

static const char *const R_NS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
static const char *const PML_NS = " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
	" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"";
static const char *const P14_NS = " xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" xmlns:p14=\"http://schemas.microsoft.com/office/powerpoint/2010/main\" mc:Ignorable=\"p14\"";

static inline long emu (double hmm) { return lround (hmm * 360); }
static inline int hmm_of (double e) { return (int) lround (e / 360.0); }
static void attrl (Buf &o, const char *name, long v) { char t[24]; snprintf (t, sizeof t, "%ld", v); attr (o, name, t); }

// The kind of picture its bytes are (the extension of the part)
static const char *pic_ext (const Picture &p)
{
	const unsigned char *b = p.bytes;
	if (p.len > 3 && b[0] == 0xFF && b[1] == 0xD8) return "jpeg";
	if (p.len > 3 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') return "gif";
	if (p.len > 2 && b[0] == 'B' && b[1] == 'M') return "bmp";
	if (p.len > 12 && !memcmp (b, "RIFF", 4) && !memcmp (b + 8, "WEBP", 4)) return "webp";
	return "png";
}

// ---- writing --------------------------------------------------------------------------------------------------------
// A part's relationships
struct Rels
{
	Buf b; int n;
	Rels () : n (0) {}
	int add (const char *type, const char *target)
	{
		n++;
		char t[600]; snprintf (t, sizeof t, "<Relationship Id=\"rId%d\" Type=\"%s/%s\" Target=\"%s\"/>", n, R_NS, type, target);
		b.puts (t); return n;
	}
	void into (pngsave::ZipOut &z, const char *path)
	{
		Buf o; o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
		o.puts (b.str ()); o.puts ("</Relationships>");
		z.add (path, o.b, (unsigned) o.n, true);
	}
};

struct PptxOut
{
	Deck &d;
	pngsave::ZipOut &z;
	Buf types;				// [Content_Types].xml's overrides
	Vec<int> media;				// the pictures written (ppt/media/image<k>)
	int nchart;
	Rels *rels;				// the part being written's
	int ids[4096]; int nids;		// a slide's objects: their ids -> the part's shape ids
	PptxOut (Deck &d_, pngsave::ZipOut &z_) : d (d_), z (z_), nchart (0), rels (0), nids (0) {}

	void part (const char *path, const char *ctype, Buf &body)
	{
		z.add (path, body.b, (unsigned) body.n, true);
		types.puts ("<Override PartName=\"/"); types.puts (path); types.puts ("\" ContentType=\""); types.puts (ctype); types.puts ("\"/>");
	}
	int spid (int id) { for (int i = 0; i < nids; i++) if (ids[i] == id) return i + 2; if (nids < 4096) { ids[nids++] = id; return nids + 1; } return 2; }

	// ---- colours, fills, lines
	void clr (Buf &o, unsigned c, int alpha = 255)
	{
		char t[96];
		const char *close;
		if (is_theme (c)) { snprintf (t, sizeof t, "<a:schemeClr val=\"%s\"", SCHEME[(c & 0xFF) % TC_COUNT]); close = "</a:schemeClr>"; }
		else { snprintf (t, sizeof t, "<a:srgbClr val=\"%06X\"", c & 0xFFFFFF); close = "</a:srgbClr>"; }
		o.puts (t);
		if (alpha < 255) { snprintf (t, sizeof t, "><a:alpha val=\"%d\"/>", alpha * 100000 / 255); o.puts (t); o.puts (close); }
		else o.puts ("/>");
	}
	void solid (Buf &o, unsigned c, int alpha = 255) { o.puts ("<a:solidFill>"); clr (o, c, alpha); o.puts ("</a:solidFill>"); }
	void fill (Buf &o, const Fill &f)
	{
		switch (f.type)
		{
		case FILL_NONE: o.puts ("<a:noFill/>"); break;
		case FILL_SOLID: solid (o, f.c1, f.alpha); break;
		case FILL_GRADIENT:
		{
			o.puts ("<a:gradFill rotWithShape=\"1\"><a:gsLst><a:gs pos=\"0\">"); clr (o, f.c1, f.alpha); o.puts ("</a:gs><a:gs pos=\"100000\">"); clr (o, f.c2, f.alpha);
			char t[128]; snprintf (t, sizeof t, "</a:gs></a:gsLst><a:lin ang=\"%ld\" scaled=\"0\"/></a:gradFill>", (long) (((f.angle % 360) + 360) % 360) * 60000); o.puts (t);
			break;
		}
		default: break;
		}
	}
	void line (Buf &o, const Line &l)
	{
		o.puts ("<a:ln"); attrl (o, "w", emu (l.width)); o.put ('>');
		if (l.type == LN_NONE) { o.puts ("<a:noFill/></a:ln>"); return; }
		solid (o, l.color);
		if (l.type == LN_DASH) o.puts ("<a:prstDash val=\"dash\"/>");
		else if (l.type == LN_DOT) o.puts ("<a:prstDash val=\"sysDot\"/>");
		static const char *const AH[4] = { "none", "triangle", "arrow", "oval" };
		if (l.head0) { o.puts ("<a:headEnd"); attr (o, "type", AH[iclamp (l.head0, 0, 3)]); o.puts ("/>"); }
		if (l.head1) { o.puts ("<a:tailEnd"); attr (o, "type", AH[iclamp (l.head1, 0, 3)]); o.puts ("/>"); }
		o.puts ("</a:ln>");
	}

	// ---- text
	void rpr (Buf &o, const char *tag, const CharFmt &f)
	{
		o.put ('<'); o.puts (tag);
		if (f.size) attri (o, "sz", f.size * 10);
		if (f.set & CF_BOLD) attr (o, "b", f.flags & CF_BOLD ? "1" : "0");
		if (f.set & CF_ITALIC) attr (o, "i", f.flags & CF_ITALIC ? "1" : "0");
		if (f.set & CF_UNDER) attr (o, "u", f.flags & CF_UNDER ? "sng" : "none");
		if (f.set & CF_STRIKE) attr (o, "strike", f.flags & CF_STRIKE ? "sngStrike" : "noStrike");
		if (f.set & (CF_SUPER | CF_SUB)) attr (o, "baseline", f.flags & CF_SUPER ? "30000" : f.flags & CF_SUB ? "-25000" : "0");
		if (f.color == AUTO && f.font == FONT_INHERIT) { o.puts ("/>"); return; }
		o.put ('>');
		if (f.color != AUTO) solid (o, f.color);
		if (f.font != FONT_INHERIT) { o.puts ("<a:latin"); attr (o, "typeface", f.font == FONT_MAJOR ? "+mj-lt" : f.font == FONT_MINOR ? "+mn-lt" : d.font_name (f.font)); o.puts ("/>"); }
		o.puts ("</"); o.puts (tag); o.put ('>');
	}
	// a paragraph's properties (or a style's level: tag "a:lvl<n>pPr", its defRPr); marL / indent in hmm (-1: none)
	void ppr (Buf &o, const char *tag, const ParaFmt &p, int level, unsigned bullet, int marL, int indent, const CharFmt *def)
	{
		Buf a, k;
		if (marL >= 0) attrl (a, "marL", emu (marL));
		if (indent != INT_MIN) attrl (a, "indent", emu (indent));
		if (level > 0 && !strcmp (tag, "a:pPr")) attri (a, "lvl", level);
		static const char *const AL[4] = { "l", "ctr", "r", "just" };
		if (p.align != AL_INHERIT) attr (a, "algn", AL[iclamp (p.align, 0, 3)]);
		char t[96];
		if (p.spacing) { snprintf (t, sizeof t, "<a:lnSpc><a:spcPct val=\"%d\"/></a:lnSpc>", p.spacing * 1000); k.puts (t); }
		if (p.before >= 0) { snprintf (t, sizeof t, "<a:spcBef><a:spcPts val=\"%d\"/></a:spcBef>", p.before * 10); k.puts (t); }
		if (p.after >= 0) { snprintf (t, sizeof t, "<a:spcAft><a:spcPts val=\"%d\"/></a:spcAft>", p.after * 10); k.puts (t); }
		if (p.bullet == BU_NONE) k.puts ("<a:buNone/>");
		else if (p.bullet == BU_BULLET)
		{
			k.puts ("<a:buFont typeface=\"Arial\"/><a:buChar char=\"");
			Buf c; c.putu (bullet ? bullet : 0x2022); xml_esc (k, c.str ()); k.puts ("\"/>");
		}
		else if (p.bullet == BU_NUMBER) k.puts (level % 3 == 1 ? "<a:buAutoNum type=\"alphaLcPeriod\"/>" : level % 3 == 2 ? "<a:buAutoNum type=\"romanLcPeriod\"/>" : "<a:buAutoNum type=\"arabicPeriod\"/>");
		if (def) rpr (k, "a:defRPr", *def);
		if (!a.n && !k.n) return;
		o.put ('<'); o.puts (tag); o.puts (a.str ());
		if (!k.n) { o.puts ("/>"); return; }
		o.put ('>'); o.puts (k.str ()); o.puts ("</"); o.puts (tag); o.put ('>');
	}
	void paras (Buf &o, const TextBody &tb, int ph)
	{
		// a list's paragraphs: their margins where Slides puts them (the bullet's or the number's width, its level)
		TextLayout L;
		bool lists = false;
		for (int i = 0; i < tb.p.n; i++) if (pf_resolve (d, ph, tb.p[i]->pf).bullet != BU_NONE) lists = true;
		if (lists) layout_text (d, tb, ph, 1e6f, 1000, L);
		for (int i = 0; i < tb.p.n; i++)
		{
			const Para *q = tb.p[i];
			o.puts ("<a:p>");
			int marL = -1, ind = INT_MIN;
			if (lists && i < L.npp && L.pp[i].pf.bullet != BU_NONE) { marL = (int) lroundf (L.pp[i].left); ind = (int) lroundf (L.pp[i].bulletX - L.pp[i].left); }
			const TextStyle &st = style_for (d, ph, q->pf.level);
			ppr (o, "a:pPr", q->pf, q->pf.level, st.bullet, marL, ind, 0);
			for (int a = 0; a < q->len; )
			{
				if (q->ch[a] == 0x0B) { o.puts ("<a:br>"); rpr (o, "a:rPr", q->cf[a]); o.puts ("</a:br>"); a++; continue; }
				int b = a + 1;
				while (b < q->len && q->ch[b] != 0x0B && cf_same (q->cf[b], q->cf[a])) b++;
				o.puts ("<a:r>"); rpr (o, "a:rPr", q->cf[a]); o.puts ("<a:t>");
				Buf t; for (int k = a; k < b; k++) if (q->ch[k] >= 32 || q->ch[k] == '\t') t.putu (q->ch[k]);
				xml_esc (o, t.str (), t.n); o.puts ("</a:t></a:r>");
				a = b;
			}
			rpr (o, "a:endParaRPr", q->end);
			o.puts ("</a:p>");
		}
		if (!tb.p.n) o.puts ("<a:p/>");
	}
	void body_pr (Buf &o, const TextBody &tb)
	{
		o.puts ("<a:bodyPr"); attr (o, "wrap", tb.wrap ? "square" : "none");
		attrl (o, "lIns", emu (tb.inset[0])); attrl (o, "tIns", emu (tb.inset[1])); attrl (o, "rIns", emu (tb.inset[2])); attrl (o, "bIns", emu (tb.inset[3]));
		attr (o, "anchor", tb.anchor == AN_MIDDLE ? "ctr" : tb.anchor == AN_BOTTOM ? "b" : "t");
		o.puts (" rtlCol=\"0\">");
		if (tb.fit == FIT_SHRINK) { if (tb.scale > 0 && tb.scale < 1000) { o.puts ("<a:normAutofit"); attri (o, "fontScale", tb.scale * 100); o.puts ("/>"); } else o.puts ("<a:normAutofit/>"); }
		else if (tb.fit == FIT_GROW) o.puts ("<a:spAutoFit/>");
		else o.puts ("<a:noAutofit/>");
		o.puts ("</a:bodyPr>");
	}
	void tx_body (Buf &o, const TextBody &tb, int ph, const char *tag = "p:txBody", const TextStyle *lst = 0)
	{
		o.put ('<'); o.puts (tag); o.put ('>');
		body_pr (o, tb);
		if (lst) { o.puts ("<a:lstStyle>"); ppr (o, "a:lvl1pPr", lst->pf, 0, lst->bullet, 0, 0, &lst->cf); o.puts ("</a:lstStyle>"); }
		else o.puts ("<a:lstStyle/>");
		paras (o, tb, ph);
		o.puts ("</"); o.puts (tag); o.put ('>');
	}

	// ---- the objects
	void xfrm (Buf &o, const Object &ob, const char *tag = "a:xfrm")
	{
		o.put ('<'); o.puts (tag);
		if (ob.rot) attrl (o, "rot", (long) ob.rot * 60000);
		if (ob.flipH) o.puts (" flipH=\"1\"");
		if (ob.flipV) o.puts (" flipV=\"1\"");
		o.puts ("><a:off"); attrl (o, "x", emu (ob.x)); attrl (o, "y", emu (ob.y)); o.puts ("/><a:ext"); attrl (o, "cx", emu (ob.w)); attrl (o, "cy", emu (ob.h));
		o.puts ("/></"); o.puts (tag); o.put ('>');
	}
	void geom (Buf &o, int shape, int radius)
	{
		o.puts ("<a:prstGeom"); attr (o, "prst", PRST[iclamp (shape, 0, SH_COUNT - 1)]); o.puts ("><a:avLst>");
		if (shape == SH_ROUND) { char t[64]; snprintf (t, sizeof t, "<a:gd name=\"adj\" fmla=\"val %d\"/>", radius * 100); o.puts (t); }
		o.puts ("</a:avLst></a:prstGeom>");
	}
	void shadow (Buf &o) { o.puts ("<a:effectLst><a:outerShdw blurRad=\"50800\" dist=\"38100\" dir=\"5400000\" algn=\"tl\" rotWithShape=\"0\"><a:prstClr val=\"black\"><a:alpha val=\"40000\"/></a:prstClr></a:outerShdw></a:effectLst>"); }
	void nv (Buf &o, const char *tag, int id, const char *name)
	{
		o.puts ("<p:cNvPr"); attri (o, "id", id); attr (o, "name", name); o.puts ("/>"); (void) tag;
	}
	// The placeholder's idx: its place in its layout (the n-th of its kind there)
	int ph_idx (const Slide *s, const Object &ob)
	{
		if (ob.ph == PH_TITLE || ob.ph == PH_NONE) return -1;
		if (ob.ph == PH_FOOTER) return 3; if (ob.ph == PH_NUMBER) return 4; if (ob.ph == PH_DATE) return 2;
		if (!s) return -1;
		int nth = 0;
		for (int i = 0; i < s->obj.n && s->obj[i] != &ob; i++) if (s->obj[i]->ph == ob.ph) nth++;
		const Layout &L = d.layout[s->layout];
		for (int i = 0, k = 0; i < L.ph.n; i++) if (L.ph[i]->ph == ob.ph && k++ == nth) return i + 1;
		return 90 + nth;
	}
	void ph (Buf &o, int kind, int idx, bool ctr)
	{
		o.puts ("<p:ph");
		if (kind == PH_TITLE) attr (o, "type", ctr ? "ctrTitle" : "title");
		else if (kind != PH_BODY && kind != PH_BODY2) attr (o, "type", PH_TYPE[kind]);
		else if (kind == PH_BODY2 || idx >= 0) attr (o, "type", "body");
		if (idx >= 0) attri (o, "idx", idx);
		o.puts ("/>");
	}
	void object (Buf &o, Object &ob, const Slide *s, int layout, int idx = -2, const TextStyle *lst = 0)
	{
		int id = spid (ob.id);
		char name[48]; scpy (name, ob.name, sizeof name);
		if (idx == -2) idx = ph_idx (s, ob);
		bool ctr = layout == LY_TITLE;
		switch (ob.kind)
		{
		case OB_LINE:
			o.puts ("<p:cxnSp><p:nvCxnSpPr>"); nv (o, "", id, name); o.puts ("<p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr><p:spPr>");
			xfrm (o, ob); o.puts ("<a:prstGeom prst=\"line\"><a:avLst/></a:prstGeom><a:noFill/>"); line (o, ob.line);
			if (ob.shadow) shadow (o);
			o.puts ("</p:spPr></p:cxnSp>");
			break;
		case OB_PICTURE:
			if (ob.img >= 0 && ob.img < g_pics.n)
			{
				int k = ob.img;
				char path[64]; snprintf (path, sizeof path, "ppt/media/image%d.%s", k + 1, pic_ext (g_pics[k]));
				if (media.find (k) < 0) { media.push (k); z.add (path, g_pics[k].bytes, g_pics[k].len, false); }
				char tg[80]; snprintf (tg, sizeof tg, "../%s", path + 4); int rid = rels->add ("image", tg);
				o.puts ("<p:pic><p:nvPicPr>"); nv (o, "", id, name); o.puts ("<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr>");
				if (ob.ph == PH_PICTURE) ph (o, PH_PICTURE, idx, false);
				char t[80]; snprintf (t, sizeof t, "</p:nvPr></p:nvPicPr><p:blipFill><a:blip r:embed=\"rId%d\"/>", rid); o.puts (t);
				if (ob.crop[0] || ob.crop[1] || ob.crop[2] || ob.crop[3])
				{ o.puts ("<a:srcRect"); attri (o, "l", ob.crop[0] * 100); attri (o, "t", ob.crop[1] * 100); attri (o, "r", ob.crop[2] * 100); attri (o, "b", ob.crop[3] * 100); o.puts ("/>"); }
				o.puts ("<a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>"); xfrm (o, ob); geom (o, ob.shape, ob.radius);
				fill (o, ob.fill); line (o, ob.line); if (ob.shadow) shadow (o);
				o.puts ("</p:spPr></p:pic>");
				break;
			}
			// (no picture: an empty frame, as a shape)
		case OB_TEXT: case OB_SHAPE: default:
			if (ob.kind == OB_TABLE && ob.tbl) { table (o, ob, id, name); break; }
			if (ob.kind == OB_CHART && ob.chart) { chart (o, ob, id, name); break; }
			o.puts ("<p:sp><p:nvSpPr>"); nv (o, "", id, name);
			if (ob.ph != PH_NONE) o.puts ("<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>");
			else if (ob.kind == OB_TEXT) o.puts ("<p:cNvSpPr txBox=\"1\"/>");
			else o.puts ("<p:cNvSpPr/>");
			o.puts ("<p:nvPr>"); if (ob.ph != PH_NONE) ph (o, ob.ph, idx, ctr); o.puts ("</p:nvPr></p:nvSpPr><p:spPr>");
			xfrm (o, ob); geom (o, ob.shape, ob.radius); fill (o, ob.fill); line (o, ob.line); if (ob.shadow) shadow (o);
			o.puts ("</p:spPr>");
			tx_body (o, ob.tb, ob.ph, "p:txBody", lst);
			o.puts ("</p:sp>");
			break;
		}
	}
	void frame_head (Buf &o, const Object &ob, int id, const char *name)
	{
		o.puts ("<p:graphicFrame><p:nvGraphicFramePr>"); nv (o, "", id, name);
		o.puts ("<p:cNvGraphicFramePr><a:graphicFrameLocks noGrp=\"1\"/></p:cNvGraphicFramePr><p:nvPr/></p:nvGraphicFramePr>");
		xfrm (o, ob, "p:xfrm");
	}
	void table (Buf &o, Object &ob, int id, const char *name)
	{
		Table &t = *ob.tbl;
		frame_head (o, ob, id, name);
		o.puts ("<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\"><a:tbl><a:tblPr");
		if (t.header) o.puts (" firstRow=\"1\""); if (t.banded) o.puts (" bandRow=\"1\"");
		o.puts ("><a:tableStyleId>"); o.puts (TABLE_STYLE); o.puts ("</a:tableStyleId></a:tblPr><a:tblGrid>");
		for (int c = 0; c < t.cols; c++) { o.puts ("<a:gridCol"); attrl (o, "w", emu (t.colW[c])); o.puts ("/>"); }
		o.puts ("</a:tblGrid>");
		for (int r = 0; r < t.rows; r++)
		{
			o.puts ("<a:tr"); attrl (o, "h", emu (t.rowH[r])); o.put ('>');
			for (int c = 0; c < t.cols; c++)
			{
				TextBody &tb = t.at (r, c);
				o.puts ("<a:tc><a:txBody><a:bodyPr/><a:lstStyle/>"); paras (o, tb, PH_NONE); o.puts ("</a:txBody><a:tcPr");
				attrl (o, "marL", emu (tb.inset[0])); attrl (o, "marR", emu (tb.inset[2])); attrl (o, "marT", emu (tb.inset[1])); attrl (o, "marB", emu (tb.inset[3]));
				attr (o, "anchor", tb.anchor == AN_MIDDLE ? "ctr" : tb.anchor == AN_BOTTOM ? "b" : "t");
				const Fill &f = t.cfill[r * t.cols + c];
				if (f.type == FILL_INHERIT) o.puts ("/>");
				else { o.put ('>'); fill (o, f); o.puts ("</a:tcPr>"); }
				o.puts ("</a:tc>");
			}
			o.puts ("</a:tr>");
		}
		o.puts ("</a:tbl></a:graphicData></a:graphic></p:graphicFrame>");
	}
	// ---- a chart: its part, its data cached (no workbook)
	static void col_name (char *o, int c) { if (c < 26) { o[0] = (char) ('A' + c); o[1] = 0; } else { o[0] = (char) ('A' + c / 26 - 1); o[1] = (char) ('A' + c % 26); o[2] = 0; } }
	void str_cache (Buf &o, const char *ref, const char *const *v, int n)
	{
		o.puts ("<c:strRef><c:f>"); xml_esc (o, ref); o.puts ("</c:f><c:strCache>"); char t[64]; snprintf (t, sizeof t, "<c:ptCount val=\"%d\"/>", n); o.puts (t);
		for (int i = 0; i < n; i++) { snprintf (t, sizeof t, "<c:pt idx=\"%d\"><c:v>", i); o.puts (t); xml_esc (o, v[i]); o.puts ("</c:v></c:pt>"); }
		o.puts ("</c:strCache></c:strRef>");
	}
	void chart_ser (Buf &o, const Chart &c, int k)
	{
		char t[160], a[4], b[4];
		col_name (a, k + 1);
		snprintf (t, sizeof t, "<c:ser><c:idx val=\"%d\"/><c:order val=\"%d\"/><c:tx>", k, k); o.puts (t);
		snprintf (t, sizeof t, "Sheet1!$%s$1", a); const char *nm = c.ser[k]; str_cache (o, t, &nm, 1); o.puts ("</c:tx>");
		static const unsigned SC[8] = { THEME | TC_ACC1, THEME | TC_ACC2, THEME | TC_ACC3, THEME | TC_ACC4, THEME | TC_ACC5, THEME | TC_ACC6, 0x7F7F7F, 0x2F2F2F };
		if (c.type == CH_LINE) { o.puts ("<c:spPr><a:ln w=\"28575\" cap=\"rnd\">"); solid (o, SC[k % 8]); o.puts ("<a:round/></a:ln></c:spPr><c:marker><c:symbol val=\"none\"/></c:marker>"); }
		else if (c.type == CH_PIE)
		{
			o.puts ("<c:spPr><a:ln w=\"12700\"><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill></a:ln></c:spPr>");
			for (int i = 0; i < c.ncat; i++) { snprintf (t, sizeof t, "<c:dPt><c:idx val=\"%d\"/><c:bubble3D val=\"0\"/><c:spPr>", i); o.puts (t); solid (o, SC[i % 8]); o.puts ("<a:ln w=\"12700\"><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill></a:ln></c:spPr></c:dPt>"); }
		}
		else { o.puts ("<c:spPr>"); solid (o, SC[k % 8]); o.puts ("</c:spPr>"); if (c.type != CH_AREA) o.puts ("<c:invertIfNegative val=\"0\"/>"); }
		o.puts ("<c:cat>");
		const char *cats[24]; for (int i = 0; i < c.ncat && i < 24; i++) cats[i] = c.cat[i];
		snprintf (t, sizeof t, "Sheet1!$A$2:$A$%d", c.ncat + 1); str_cache (o, t, cats, c.ncat);
		snprintf (t, sizeof t, "</c:cat><c:val><c:numRef><c:f>Sheet1!$%s$2:$%s$%d</c:f><c:numCache><c:formatCode>General</c:formatCode><c:ptCount val=\"%d\"/>", a, a, c.ncat + 1, c.ncat); o.puts (t);
		for (int i = 0; i < c.ncat; i++) { snprintf (t, sizeof t, "<c:pt idx=\"%d\"><c:v>%.15g</c:v></c:pt>", i, c.val[k][i]); o.puts (t); }
		o.puts ("</c:numCache></c:numRef></c:val>");
		if (c.type == CH_LINE) o.puts ("<c:smooth val=\"0\"/>");
		o.puts ("</c:ser>");
		(void) b;
	}
	void chart (Buf &o, Object &ob, int id, const char *name)
	{
		Chart &c = *ob.chart;
		char path[48]; snprintf (path, sizeof path, "ppt/charts/chart%d.xml", ++nchart);
		char tg[80]; snprintf (tg, sizeof tg, "../%s", path + 4); int rid = rels->add ("chart", tg);
		frame_head (o, ob, id, name);
		char t[320]; snprintf (t, sizeof t, "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"><c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" r:id=\"rId%d\"/>", rid);
		o.puts (t); o.puts ("</a:graphicData></a:graphic></p:graphicFrame>");
		// the part
		Buf x;
		x.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
		x.puts ("<c:roundedCorners val=\"0\"/><c:chart>");
		if (c.title[0]) { x.puts ("<c:title><c:tx><c:rich><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"1800\" b=\"1\"/></a:pPr><a:r><a:rPr lang=\"en-US\" sz=\"1800\" b=\"1\"/><a:t>"); xml_esc (x, c.title); x.puts ("</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title><c:autoTitleDeleted val=\"0\"/>"); }
		else x.puts ("<c:autoTitleDeleted val=\"1\"/>");
		x.puts ("<c:plotArea><c:layout/>");
		const char *dl = c.labels ? "<c:dLbls><c:showLegendKey val=\"0\"/><c:showVal val=\"1\"/><c:showCatName val=\"0\"/><c:showSerName val=\"0\"/><c:showPercent val=\"0\"/><c:showBubbleSize val=\"0\"/></c:dLbls>" : "";
		bool axes = c.type != CH_PIE;
		switch (c.type)
		{
		case CH_BAR: case CH_COLUMN: default:
			x.puts ("<c:barChart><c:barDir val=\""); x.puts (c.type == CH_BAR ? "bar" : "col"); x.puts ("\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>");
			for (int k = 0; k < c.nser; k++) chart_ser (x, c, k);
			x.puts (dl); x.puts ("<c:gapWidth val=\"80\"/><c:axId val=\"111\"/><c:axId val=\"222\"/></c:barChart>");
			break;
		case CH_LINE:
			x.puts ("<c:lineChart><c:grouping val=\"standard\"/><c:varyColors val=\"0\"/>");
			for (int k = 0; k < c.nser; k++) chart_ser (x, c, k);
			x.puts (dl); x.puts ("<c:marker val=\"1\"/><c:axId val=\"111\"/><c:axId val=\"222\"/></c:lineChart>");
			break;
		case CH_AREA:
			x.puts ("<c:areaChart><c:grouping val=\"standard\"/><c:varyColors val=\"0\"/>");
			for (int k = 0; k < c.nser; k++) chart_ser (x, c, k);
			x.puts (dl); x.puts ("<c:axId val=\"111\"/><c:axId val=\"222\"/></c:areaChart>");
			break;
		case CH_PIE:
			x.puts ("<c:pieChart><c:varyColors val=\"1\"/>");
			for (int k = 0; k < c.nser && k < 1; k++) chart_ser (x, c, k);
			x.puts (dl); x.puts ("<c:firstSliceAng val=\"0\"/></c:pieChart>");
			break;
		}
		if (axes)
		{
			bool bar = c.type == CH_BAR;
			x.puts ("<c:catAx><c:axId val=\"111\"/><c:scaling><c:orientation val=\""); x.puts (bar ? "maxMin" : "minMax"); x.puts ("\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\""); x.puts (bar ? "l" : "b");
			x.puts ("\"/><c:numFmt formatCode=\"General\" sourceLinked=\"0\"/><c:majorTickMark val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"222\"/><c:crosses val=\"autoZero\"/><c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/></c:catAx>");
			x.puts ("<c:valAx><c:axId val=\"222\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\""); x.puts (bar ? "b" : "l");
			x.puts ("\"/><c:majorGridlines><c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"D9D9D9\"/></a:solidFill></a:ln></c:spPr></c:majorGridlines><c:numFmt formatCode=\"General\" sourceLinked=\"0\"/><c:majorTickMark val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"111\"/><c:crosses val=\""); x.puts (bar ? "max" : "autoZero"); x.puts ("\"/><c:crossBetween val=\"between\"/></c:valAx>");
		}
		x.puts ("</c:plotArea>");
		if (c.legend) x.puts ("<c:legend><c:legendPos val=\"b\"/><c:overlay val=\"0\"/></c:legend>");
		x.puts ("<c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/></c:chart>");
		x.puts ("<c:txPr><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"1200\">"); solid (x, THEME | TC_DK1); x.puts ("<a:latin typeface=\"+mn-lt\"/></a:defRPr></a:pPr><a:endParaRPr lang=\"en-US\"/></a:p></c:txPr></c:chartSpace>");
		part (path, "application/vnd.openxmlformats-officedocument.drawingml.chart+xml", x);
	}

	// ---- the parts
	void tree_head (Buf &o)
	{
		o.puts ("<p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>");
	}
	void bg (Buf &o, const Fill &f) { o.puts ("<p:bg><p:bgPr>"); fill (o, f); o.puts ("<a:effectLst/></p:bgPr></p:bg>"); }
	// The footers' places (as render.h's footer_objects draws them)
	void footer_box (Object &o, int kind)
	{
		int W = d.sw, H = d.sh;
		o.kind = OB_TEXT; o.ph = (signed char) kind; o.tb.inset[0] = o.tb.inset[1] = o.tb.inset[2] = o.tb.inset[3] = 0;
		o.y = H * 945 / 1000; o.h = H * 45 / 1000;
		if (kind == PH_FOOTER) { o.x = W * 57 / 1000; o.w = W * 60 / 100; }
		else if (kind == PH_NUMBER) { o.x = W * 80 / 100; o.w = W * 143 / 1000; }
		else { o.x = W * 57 / 1000 + W * 62 / 100; o.w = W * 16 / 100; }
	}
	void footer_text (Object &o, int kind, int number)
	{
		CharFmt f = cf_inherit (); f.size = 120; f.color = 0x707C84;
		ParaFmt none = pf_inherit (); none.bullet = BU_NONE;
		if (kind == PH_FOOTER) o.tb.set_text (d.footerText, f, &none);
		else if (kind == PH_NUMBER)
		{
			char t[16]; snprintf (t, sizeof t, "%d", number);
			CharFmt g = f; g.color = THEME | TC_ACC1; g.flags = CF_BOLD; g.set = CF_BOLD;
			ParaFmt pf = none; pf.align = AL_RIGHT;
			o.tb.set_text (t, g, &pf);
		}
		else { o.tb.set_text ("", f, &none); }
	}
	// a footer placeholder of a slide (its number a field)
	void footer_ph (Buf &o, int kind, int number, int id)
	{
		Object b; footer_box (b, kind); footer_text (b, kind, number);
		static const char *const NM[3] = { "Footer", "Slide Number", "Date" };
		o.puts ("<p:sp><p:nvSpPr>"); nv (o, "", id, NM[kind == PH_FOOTER ? 0 : kind == PH_NUMBER ? 1 : 2]);
		o.puts ("<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr>"); ph (o, kind, kind == PH_FOOTER ? 3 : kind == PH_NUMBER ? 4 : 2, false);
		o.puts ("</p:nvPr></p:nvSpPr><p:spPr>"); xfrm (o, b); o.puts ("</p:spPr><p:txBody>"); body_pr (o, b.tb); o.puts ("<a:lstStyle/>");
		if (kind == PH_NUMBER)
		{
			const Para *q = b.tb.p[0];
			o.puts ("<a:p>"); ppr (o, "a:pPr", q->pf, 0, 0, -1, INT_MIN, 0);
			o.puts ("<a:fld id=\"{B6F15528-21DE-4FAA-801E-634DDDAF4B2B}\" type=\"slidenum\">"); rpr (o, "a:rPr", q->cf[0]);
			char t[32]; snprintf (t, sizeof t, "<a:t>%d</a:t></a:fld>", number); o.puts (t); rpr (o, "a:endParaRPr", q->end); o.puts ("</a:p>");
		}
		else if (kind == PH_DATE)
		{
			o.puts ("<a:p><a:pPr><a:buNone/></a:pPr><a:fld id=\"{4D3C3A6E-2E1E-4E52-9C61-3F6B6E0C2C11}\" type=\"datetime1\">"); rpr (o, "a:rPr", b.tb.p[0]->end); o.puts ("<a:t></a:t></a:fld></a:p>");
		}
		else paras (o, b.tb, PH_FOOTER);
		o.puts ("</p:txBody></p:sp>");
	}
	// a text style's levels (the master's)
	void style_levels (Buf &o, int ts, int nlevels, bool body)
	{
		for (int l = 0; l < 9 && l < nlevels; l++)
		{
			const TextStyle &st = d.style[ts + (body ? imin (l, 4) : 0)];
			char tag[16]; snprintf (tag, sizeof tag, "a:lvl%dpPr", l + 1);
			int step = st.indent;
			int marL = body ? (l + 1) * step : 0, ind = body ? -step : 0;
			if (body && l > 4) { marL = (l + 1) * step; }
			ParaFmt pf = st.pf;
			ppr (o, tag, pf, body ? imin (l, 4) : 0, st.bullet, marL, ind, &st.cf);
		}
	}
	void theme (Buf &o, const char *name)
	{
		o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""); attr (o, "name", name);
		o.puts ("><a:themeElements><a:clrScheme"); attr (o, "name", d.theme.name); o.put ('>');
		for (int i = 0; i < TC_COUNT; i++) { char t[80]; snprintf (t, sizeof t, "<a:%s><a:srgbClr val=\"%06X\"/></a:%s>", THEME_SLOT[i], d.theme.col[i] & 0xFFFFFF, THEME_SLOT[i]); o.puts (t); }
		{ char t[80]; snprintf (t, sizeof t, "<a:folHlink><a:srgbClr val=\"%06X\"/></a:folHlink>", d.theme.col[TC_LINK] & 0xFFFFFF); o.puts (t); }
		o.puts ("</a:clrScheme><a:fontScheme"); attr (o, "name", d.theme.name); o.puts ("><a:majorFont><a:latin"); attr (o, "typeface", d.theme.major);
		o.puts ("/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont><a:minorFont><a:latin"); attr (o, "typeface", d.theme.minor);
		o.puts ("/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont></a:fontScheme><a:fmtScheme name=\"Onyx\"><a:fillStyleLst>");
		for (int i = 0; i < 3; i++) o.puts ("<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>");
		o.puts ("</a:fillStyleLst><a:lnStyleLst>");
		for (int i = 0; i < 3; i++) { char t[120]; snprintf (t, sizeof t, "<a:ln w=\"%d\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln>", 6350 * (i + 1)); o.puts (t); }
		o.puts ("</a:lnStyleLst><a:effectStyleLst>");
		for (int i = 0; i < 3; i++) o.puts ("<a:effectStyle><a:effectLst/></a:effectStyle>");
		o.puts ("</a:effectStyleLst><a:bgFillStyleLst>");
		for (int i = 0; i < 3; i++) o.puts ("<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>");
		o.puts ("</a:bgFillStyleLst></a:fmtScheme></a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/></a:theme>");
	}
	static void clr_map (Buf &o) { o.puts ("<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"); }

	void master (Buf &o)
	{
		nids = 0;
		o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:sldMaster"); o.puts (PML_NS); o.puts ("><p:cSld>");
		bg (o, d.masterBg);
		tree_head (o);
		for (int i = 0; i < d.decor.n; i++) object (o, *d.decor[i], 0, -1, -1);
		// the title's and the body's places: the "Title and content" layout's
		const Layout &L = d.layout[LY_CONTENT];
		Object *t = L.find (PH_TITLE), *b = L.find (PH_BODY);
		Object title, body;
		if (t) { title.x = t->x; title.y = t->y; title.w = t->w; title.h = t->h; title.tb.copy_from (t->tb); }
		if (b) { body.x = b->x; body.y = b->y; body.w = b->w; body.h = b->h; body.tb.copy_from (b->tb); }
		title.ph = PH_TITLE; title.id = -100; body.ph = PH_BODY; body.id = -101;
		title.tb.set_text ("Click to edit the title", cf_inherit ());
		body.tb.set_text ("Click to edit the text\nSecond level\nThird level", cf_inherit ());
		for (int k = 1; k < body.tb.p.n; k++) body.tb.p[k]->pf.level = (signed char) k;
		object (o, title, 0, -1, -1); object (o, body, 0, -1, 1);
		for (int k = 0; k < 3; k++) { static const int K[3] = { PH_DATE, PH_FOOTER, PH_NUMBER }; footer_ph (o, K[k], 1, spid (-110 - k)); }
		o.puts ("</p:spTree></p:cSld>"); clr_map (o);
		o.puts ("<p:sldLayoutIdLst>");
		for (int l = 0; l < LY_COUNT; l++) { char x[96]; snprintf (x, sizeof x, "<p:sldLayoutId id=\"%u\" r:id=\"rId%d\"/>", 2147483649u + l, l + 1); o.puts (x); }
		o.puts ("</p:sldLayoutIdLst><p:hf"); attr (o, "sldNum", d.number ? "1" : "0"); o.puts (" hdr=\"0\""); attr (o, "ftr", d.footer ? "1" : "0"); attr (o, "dt", d.date ? "1" : "0");
		o.puts ("/><p:txStyles><p:titleStyle>"); style_levels (o, TS_TITLE, 1, false);
		o.puts ("</p:titleStyle><p:bodyStyle>"); style_levels (o, TS_BODY1, 9, true);
		o.puts ("</p:bodyStyle><p:otherStyle>"); style_levels (o, TS_OTHER, 1, false);
		o.puts ("</p:otherStyle></p:txStyles></p:sldMaster>");
		// the master's footer: its text, for the slides' footers to take
		(void) 0;
	}
	void layout (Buf &o, int l)
	{
		nids = 0;
		const Layout &L = d.layout[l];
		o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:sldLayout"); o.puts (PML_NS); attr (o, "type", LAYOUT_TYPE[l]); o.puts (" preserve=\"1\"><p:cSld"); attr (o, "name", L.name); o.put ('>');
		tree_head (o);
		for (int i = 0; i < L.ph.n; i++)
		{
			Object *p = obj_copy (L.ph[i]);
			// its prompt, as PowerPoint shows it on an empty slide's placeholder
			if (p->tb.empty () && PH_PROMPTS[(int) p->ph][0]) { CharFmt f = cf_inherit (); p->tb.set_text (PH_PROMPTS[(int) p->ph], f); }
			object (o, *p, 0, l, p->ph == PH_TITLE ? -1 : i + 1, p->ph == PH_SUBTITLE ? &d.style[TS_SUBTITLE] : 0);
			delete p;
		}
		o.puts ("</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>");
	}
	// ---- the effects: p:timing
	int ctn;
	void tgt (Buf &o, int sp, int para)
	{
		char t[96]; snprintf (t, sizeof t, "<p:tgtEl><p:spTgt spid=\"%d\">", sp); o.puts (t);
		if (para >= 0) { snprintf (t, sizeof t, "<p:txEl><p:pRg st=\"%d\" end=\"%d\"/></p:txEl>", para, para); o.puts (t); }
		o.puts ("</p:spTgt></p:tgtEl>");
	}
	void bhv_set (Buf &o, int sp, int para, const char *attrName, const char *val, int delay, int dur = 1)
	{
		char t[160]; snprintf (t, sizeof t, "<p:set><p:cBhvr><p:cTn id=\"%d\" dur=\"%d\" fill=\"hold\"><p:stCondLst><p:cond delay=\"%d\"/></p:stCondLst></p:cTn>", ++ctn, dur, delay); o.puts (t);
		tgt (o, sp, para); o.puts ("<p:attrNameLst><p:attrName>"); o.puts (attrName); o.puts ("</p:attrName></p:attrNameLst></p:cBhvr><p:to><p:strVal"); attr (o, "val", val); o.puts ("/></p:to></p:set>");
	}
	void bhv_effect (Buf &o, int sp, int para, const char *tr, const char *filter, int dur)
	{
		char t[160]; snprintf (t, sizeof t, "<p:animEffect transition=\"%s\"", tr); o.puts (t); attr (o, "filter", filter);
		snprintf (t, sizeof t, "><p:cBhvr><p:cTn id=\"%d\" dur=\"%d\"/>", ++ctn, dur); o.puts (t); tgt (o, sp, para); o.puts ("</p:cBhvr></p:animEffect>");
	}
	void bhv_anim (Buf &o, int sp, int para, const char *prop, const char *from, const char *to, int dur)
	{
		char t[200]; snprintf (t, sizeof t, "<p:anim calcmode=\"lin\" valueType=\"num\"><p:cBhvr additive=\"base\"><p:cTn id=\"%d\" dur=\"%d\" fill=\"hold\"/>", ++ctn, dur); o.puts (t);
		tgt (o, sp, para); o.puts ("<p:attrNameLst><p:attrName>"); o.puts (prop); o.puts ("</p:attrName></p:attrNameLst></p:cBhvr><p:tavLst><p:tav tm=\"0\"><p:val><p:strVal"); attr (o, "val", from);
		o.puts ("/></p:val></p:tav><p:tav tm=\"100000\"><p:val><p:strVal"); attr (o, "val", to); o.puts ("/></p:val></p:tav></p:tavLst></p:anim>");
	}
	void bhv_scale (Buf &o, int sp, int para, int by, int dur, bool rev)
	{
		char t[200]; snprintf (t, sizeof t, "<p:animScale><p:cBhvr><p:cTn id=\"%d\" dur=\"%d\" fill=\"hold\"%s/>", ++ctn, rev ? dur / 2 : dur, rev ? " autoRev=\"1\"" : ""); o.puts (t);
		tgt (o, sp, para); snprintf (t, sizeof t, "</p:cBhvr><p:by x=\"%d\" y=\"%d\"/></p:animScale>", by, by); o.puts (t);
	}
	void bhv_rot (Buf &o, int sp, int para, long by, int dur)
	{
		char t[200]; snprintf (t, sizeof t, "<p:animRot by=\"%ld\"><p:cBhvr><p:cTn id=\"%d\" dur=\"%d\" fill=\"hold\"/>", by, ++ctn, dur); o.puts (t);
		tgt (o, sp, para); o.puts ("<p:attrNameLst><p:attrName>r</p:attrName></p:attrNameLst></p:cBhvr></p:animRot>");
	}
	// One effect's node: its preset, its behaviours
	void effect (Buf &o, const Anim &a, int start, int sp, int para, int delay)
	{
		int cls = iclamp (a.cls, 0, 2), fx = iclamp (a.fx, 0, FX_COUNT - 1), dur = imax (1, a.dur);
		static const char *const NT[3] = { "clickEffect", "withEffect", "afterEffect" };
		char t[300];
		snprintf (t, sizeof t, "<p:par><p:cTn id=\"%d\" presetID=\"%d\" presetClass=\"%s\" presetSubtype=\"%d\" fill=\"hold\"%s nodeType=\"%s\"><p:stCondLst><p:cond delay=\"%d\"/></p:stCondLst><p:childTnLst>",
			  ++ctn, FX_PRESET[cls][fx], FX_CLASS[cls], DIR_SUB[iclamp (a.dir, 0, 4)], cls == AC_EMPHASIS ? "" : " grpId=\"0\"", NT[iclamp (start, 0, 2)], delay);
		o.puts (t);
		const char *dir = a.dir == DIR_RIGHT ? "right" : a.dir == DIR_UP ? "up" : a.dir == DIR_DOWN ? "down" : "left";
		if (cls == AC_EMPHASIS)
		{
			switch (fx)
			{
			case FX_SPIN: bhv_rot (o, sp, para, 21600000, dur); break;
			case FX_GROW: case FX_ZOOM: bhv_scale (o, sp, para, 118000, dur, true); break;
			case FX_COLOR:
				snprintf (t, sizeof t, "<p:animClr clrSpc=\"rgb\" dir=\"cw\"><p:cBhvr><p:cTn id=\"%d\" dur=\"%d\" fill=\"hold\"/>", ++ctn, dur); o.puts (t); tgt (o, sp, para);
				o.puts ("<p:attrNameLst><p:attrName>fillcolor</p:attrName></p:attrNameLst></p:cBhvr><p:to><a:schemeClr val=\"accent2\"/></p:to></p:animClr>");
				break;
			case FX_FADE: bhv_set (o, sp, para, "style.opacity", "0.5", 0, dur); break;
			default: bhv_scale (o, sp, para, 109000, dur, true); break;
			}
		}
		else
		{
			bool in = cls == AC_ENTRANCE;
			const char *tr = in ? "in" : "out";
			if (in) bhv_set (o, sp, para, "style.visibility", "visible", 0);
			char w[24];
			switch (fx)
			{
			case FX_APPEAR: break;
			case FX_FADE: bhv_effect (o, sp, para, tr, "fade", dur); break;
			case FX_PULSE: bhv_effect (o, sp, para, tr, "dissolve", dur); break;
			case FX_COLOR: bhv_effect (o, sp, para, tr, "blinds(horizontal)", dur); break;
			case FX_WIPE: snprintf (w, sizeof w, "wipe(%s)", dir); bhv_effect (o, sp, para, tr, w, dur); break;
			case FX_FLY:
			{
				const char *x0 = a.dir == DIR_LEFT ? "0-#ppt_w/2" : a.dir == DIR_RIGHT ? "1+#ppt_w/2" : "#ppt_x";
				const char *y0 = a.dir == DIR_UP ? "0-#ppt_h/2" : a.dir == DIR_DOWN || a.dir == DIR_NONE ? "1+#ppt_h/2" : "#ppt_y";
				bhv_anim (o, sp, para, "ppt_x", in ? x0 : "#ppt_x", in ? "#ppt_x" : x0, dur);
				bhv_anim (o, sp, para, "ppt_y", in ? y0 : "#ppt_y", in ? "#ppt_y" : y0, dur);
				break;
			}
			case FX_ZOOM:
				bhv_effect (o, sp, para, tr, "fade", dur);
				bhv_anim (o, sp, para, "ppt_w", in ? "#ppt_w*0.3" : "#ppt_w", in ? "#ppt_w" : "#ppt_w*0.3", dur);
				bhv_anim (o, sp, para, "ppt_h", in ? "#ppt_h*0.3" : "#ppt_h", in ? "#ppt_h" : "#ppt_h*0.3", dur);
				break;
			case FX_GROW:
				bhv_anim (o, sp, para, "ppt_w", in ? "0" : "#ppt_w", in ? "#ppt_w" : "0", dur);
				bhv_anim (o, sp, para, "ppt_h", in ? "0" : "#ppt_h", in ? "#ppt_h" : "0", dur);
				break;
			case FX_FLOAT:
				bhv_effect (o, sp, para, tr, "fade", dur);
				bhv_anim (o, sp, para, "ppt_y", in ? "#ppt_y+0.1" : "#ppt_y", in ? "#ppt_y" : "#ppt_y-0.1", dur);
				break;
			case FX_SPIN:
				bhv_effect (o, sp, para, tr, "fade", dur);
				bhv_rot (o, sp, para, in ? -21600000L : 21600000L, dur);
				break;
			}
			if (!in) bhv_set (o, sp, para, "style.visibility", "hidden", fx == FX_APPEAR ? 0 : imax (0, dur - 1));
		}
		o.puts ("</p:childTnLst></p:cTn></p:par>");
	}
	void timing (Buf &o, const Slide &s)
	{
		if (!s.anim.n) return;
		// the effects, a paragraph each when by paragraph: (anim, its start, its paragraph)
		struct E { int a, start, para; };
		Vec<E> ev;
		for (int i = 0; i < s.anim.n; i++)
		{
			const Anim &a = s.anim[i];
			const Object *ob = s.by_id (a.obj); if (!ob) continue;
			if (a.byPara && ob->tb.p.n > 1)
			{
				bool first = true;
				for (int p = 0; p < ob->tb.p.n; p++)
				{
					if (!ob->tb.p[p]->len) continue;
					E e; e.a = i; e.start = first ? a.start : a.start == ST_CLICK ? ST_CLICK : ST_AFTER; e.para = p; ev.push (e); first = false;
				}
				if (first) { E e; e.a = i; e.start = a.start; e.para = -1; ev.push (e); }
			}
			else { E e; e.a = i; e.start = a.start; e.para = -1; ev.push (e); }
		}
		if (!ev.n) return;
		ctn = 2;
		Buf seq;
		// the click groups; in each, the "after" groups (each starts when the one before ends)
		int k = 0;
		while (k < ev.n)
		{
			int g0 = k, g1 = k + 1;
			while (g1 < ev.n && ev[g1].start != ST_CLICK) g1++;
			char t[200];
			snprintf (t, sizeof t, "<p:par><p:cTn id=\"%d\" fill=\"hold\"><p:stCondLst><p:cond delay=\"indefinite\"/>", ++ctn); seq.puts (t);
			if (g0 == 0 && ev[0].start != ST_CLICK) seq.puts ("<p:cond evt=\"onBegin\" delay=\"0\"><p:tn val=\"2\"/></p:cond>");
			seq.puts ("</p:stCondLst><p:childTnLst>");
			int at = 0, end = 0;
			int m = g0;
			while (m < g1)
			{
				int m1 = m + 1;
				while (m1 < g1 && ev[m1].start == ST_WITH) m1++;
				if (m > g0) at = end;
				snprintf (t, sizeof t, "<p:par><p:cTn id=\"%d\" fill=\"hold\"><p:stCondLst><p:cond delay=\"%d\"/></p:stCondLst><p:childTnLst>", ++ctn, at); seq.puts (t);
				for (int j = m; j < m1; j++)
				{
					const Anim &a = s.anim[ev[j].a];
					const Object *ob = s.by_id (a.obj);
					effect (seq, a, ev[j].start, spid (ob->id), ev[j].para, a.delay);
					int e = at + a.delay + (a.fx == FX_APPEAR ? 1 : imax (1, a.dur));
					if (e > end) end = e;
				}
				seq.puts ("</p:childTnLst></p:cTn></p:par>");
				m = m1;
			}
			seq.puts ("</p:childTnLst></p:cTn></p:par>");
			k = g1;
		}
		o.puts ("<p:timing><p:tnLst><p:par><p:cTn id=\"1\" dur=\"indefinite\" restart=\"never\" nodeType=\"tmRoot\"><p:childTnLst><p:seq concurrent=\"1\" nextAc=\"seek\"><p:cTn id=\"2\" dur=\"indefinite\" nodeType=\"mainSeq\"><p:childTnLst>");
		o.puts (seq.str ());
		o.puts ("</p:childTnLst></p:cTn><p:prevCondLst><p:cond evt=\"onPrev\" delay=\"0\"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst><p:nextCondLst><p:cond evt=\"onNext\" delay=\"0\"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst></p:seq></p:childTnLst></p:cTn></p:par></p:tnLst>");
		// the builds: a text's shapes (whole, or by paragraph)
		Buf bl;
		for (int i = 0; i < s.anim.n; i++)
		{
			const Anim &a = s.anim[i];
			if (a.cls == AC_EMPHASIS) continue;
			const Object *ob = s.by_id (a.obj); if (!ob || ob->kind == OB_PICTURE || ob->kind == OB_TABLE || ob->kind == OB_CHART || ob->kind == OB_LINE) continue;
			char t[24]; snprintf (t, sizeof t, "spid=\"%d\"", spid (ob->id));
			if (strstr (bl.str (), t)) continue;
			snprintf (t, sizeof t, "%d", spid (ob->id));
			bl.puts ("<p:bldP"); attr (bl, "spid", t); bl.puts (" grpId=\"0\"");
			if (a.byPara && ob->tb.p.n > 1) bl.puts (" build=\"p\""); else bl.puts (" animBg=\"1\"");
			bl.puts ("/>");
		}
		if (bl.n) { o.puts ("<p:bldLst>"); o.puts (bl.str ()); o.puts ("</p:bldLst>"); }
		o.puts ("</p:timing>");
	}
	void transition (Buf &o, const Slide &s)
	{
		if (s.tr.type == TR_NONE && s.tr.after < 0) return;
		o.puts ("<p:transition"); attr (o, "spd", s.tr.dur < 500 ? "fast" : s.tr.dur > 1200 ? "slow" : "med"); attri (o, "p14:dur", s.tr.dur);
		if (s.tr.after >= 0) attri (o, "advTm", s.tr.after);
		o.put ('>');
		const char *dir = s.tr.dir == DIR_RIGHT ? "l" : s.tr.dir == DIR_LEFT ? "r" : s.tr.dir == DIR_UP ? "d" : s.tr.dir == DIR_DOWN ? "u" : "l";
		char t[80];
		switch (s.tr.type)
		{
		case TR_FADE: o.puts ("<p:fade/>"); break;
		case TR_PUSH: snprintf (t, sizeof t, "<p:push dir=\"%s\"/>", dir); o.puts (t); break;
		case TR_WIPE: snprintf (t, sizeof t, "<p:wipe dir=\"%s\"/>", dir); o.puts (t); break;
		case TR_COVER: snprintf (t, sizeof t, "<p:cover dir=\"%s\"/>", dir); o.puts (t); break;
		case TR_UNCOVER: snprintf (t, sizeof t, "<p:pull dir=\"%s\"/>", dir); o.puts (t); break;
		case TR_SPLIT: o.puts ("<p:split orient=\"vert\" dir=\"out\"/>"); break;
		case TR_ZOOM: o.puts ("<p:zoom/>"); break;
		case TR_DISSOLVE: o.puts ("<p:dissolve/>"); break;
		default: break;
		}
		o.puts ("</p:transition>");
	}
	void slide (Buf &o, Slide &s, int index)
	{
		nids = 0;
		// the slide's shape ids: its objects' first (the effects name them)
		for (int i = 0; i < s.obj.n; i++) spid (s.obj[i]->id);
		o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:sld"); o.puts (PML_NS); o.puts (P14_NS);
		if (s.hidden) o.puts (" show=\"0\"");
		if (!s.masterObjects) o.puts (" showMasterSp=\"0\"");
		o.puts ("><p:cSld>");
		if (s.bg.type != FILL_INHERIT) bg (o, s.bg);
		tree_head (o);
		for (int i = 0; i < s.obj.n; i++) object (o, *s.obj[i], &s, s.layout);
		if (s.masterObjects)
		{
			if (d.footer && d.footerText[0]) footer_ph (o, PH_FOOTER, index + 1, spid (-200));
			if (d.number && s.layout != LY_TITLE) footer_ph (o, PH_NUMBER, index + 1, spid (-201));
			if (d.date) footer_ph (o, PH_DATE, index + 1, spid (-202));
		}
		o.puts ("</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>");
		transition (o, s);
		timing (o, s);
		o.puts ("</p:sld>");
	}
	void notes (Buf &o, const Slide &s)
	{
		o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:notes"); o.puts (PML_NS); o.puts ("><p:cSld>"); tree_head (o);
		o.puts ("<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Slide Image\"/><p:cNvSpPr><a:spLocks noGrp=\"1\" noRot=\"1\" noChangeAspect=\"1\"/></p:cNvSpPr><p:nvPr><p:ph type=\"sldImg\"/></p:nvPr></p:nvSpPr><p:spPr/></p:sp>");
		o.puts ("<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Notes\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr><p:ph type=\"body\" idx=\"1\"/></p:nvPr></p:nvSpPr><p:spPr/><p:txBody><a:bodyPr/><a:lstStyle/>");
		for (int i = 0; i < s.notes.p.n; i++)
		{
			const Para *q = s.notes.p[i];
			if (!q->len) { o.puts ("<a:p/>"); continue; }
			o.puts ("<a:p><a:r><a:rPr lang=\"en-US\" dirty=\"0\"/><a:t>");
			Buf t; for (int k = 0; k < q->len; k++) if (q->ch[k] >= 32 || q->ch[k] == '\t') t.putu (q->ch[k]);
			xml_esc (o, t.str (), t.n); o.puts ("</a:t></a:r></a:p>");
		}
		if (!s.notes.p.n) o.puts ("<a:p/>");
		o.puts ("</p:txBody></p:sp></p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:notes>");
	}
	void notes_master (Buf &o)
	{
		o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:notesMaster"); o.puts (PML_NS); o.puts ("><p:cSld><p:bg><p:bgRef idx=\"1001\"><a:schemeClr val=\"bg1\"/></p:bgRef></p:bg>"); tree_head (o);
		o.puts ("<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Slide Image\"/><p:cNvSpPr><a:spLocks noGrp=\"1\" noRot=\"1\" noChangeAspect=\"1\"/></p:cNvSpPr><p:nvPr><p:ph type=\"sldImg\" idx=\"2\"/></p:nvPr></p:nvSpPr><p:spPr>");
		long w = 6096000, h = (long) ((double) w * d.sh / d.sw);
		char t[200]; snprintf (t, sizeof t, "<a:xfrm><a:off x=\"381000\" y=\"685800\"/><a:ext cx=\"%ld\" cy=\"%ld\"/></a:xfrm>", w, h); o.puts (t);
		o.puts ("<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/><a:ln w=\"12700\"><a:solidFill><a:prstClr val=\"black\"/></a:solidFill></a:ln></p:spPr></p:sp>");
		o.puts ("<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Notes\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr><p:ph type=\"body\" sz=\"quarter\" idx=\"3\"/></p:nvPr></p:nvSpPr><p:spPr>");
		snprintf (t, sizeof t, "<a:xfrm><a:off x=\"685800\" y=\"%ld\"/><a:ext cx=\"5486400\" cy=\"%ld\"/></a:xfrm>", 685800 + h + 300000, 9144000 - (685800 + h + 300000) - 600000); o.puts (t);
		o.puts ("<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr><p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang=\"en-US\"/><a:t>Notes</a:t></a:r></a:p></p:txBody></p:sp>");
		o.puts ("</p:spTree></p:cSld>"); clr_map (o);
		o.puts ("<p:notesStyle><a:lvl1pPr marL=\"0\" algn=\"l\"><a:defRPr sz=\"1200\"><a:solidFill><a:schemeClr val=\"tx1\"/></a:solidFill><a:latin typeface=\"+mn-lt\"/></a:defRPr></a:lvl1pPr></p:notesStyle></p:notesMaster>");
	}
};

// The deck as a .pptx archive (new []): *len bytes.
static unsigned char *pptx_save (Deck &d, unsigned *len)
{
	pngsave::ZipOut z;
	PptxOut w (d, z);
	char path[80], t[400];
	bool anyNotes = false;
	for (int i = 0; i < d.slides.n; i++) if (!d.slides[i]->notes.empty ()) anyNotes = true;
	// the theme(s)
	{ Buf o; w.theme (o, d.theme.name); w.part ("ppt/theme/theme1.xml", "application/vnd.openxmlformats-officedocument.theme+xml", o); }
	// the master, its layouts
	{
		Rels r; w.rels = &r;
		for (int l = 0; l < LY_COUNT; l++) { snprintf (path, sizeof path, "../slideLayouts/slideLayout%d.xml", l + 1); r.add ("slideLayout", path); }
		r.add ("theme", "../theme/theme1.xml");
		Buf o; w.master (o);
		w.part ("ppt/slideMasters/slideMaster1.xml", "application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml", o);
		r.into (z, "ppt/slideMasters/_rels/slideMaster1.xml.rels");
	}
	for (int l = 0; l < LY_COUNT; l++)
	{
		Rels r; w.rels = &r; r.add ("slideMaster", "../slideMasters/slideMaster1.xml");
		Buf o; w.layout (o, l);
		snprintf (path, sizeof path, "ppt/slideLayouts/slideLayout%d.xml", l + 1);
		w.part (path, "application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml", o);
		snprintf (path, sizeof path, "ppt/slideLayouts/_rels/slideLayout%d.xml.rels", l + 1); r.into (z, path);
	}
	if (anyNotes)
	{
		Buf th; w.theme (th, "Notes"); w.part ("ppt/theme/theme2.xml", "application/vnd.openxmlformats-officedocument.theme+xml", th);
		Rels r; r.add ("theme", "../theme/theme2.xml");
		Buf o; w.notes_master (o);
		w.part ("ppt/notesMasters/notesMaster1.xml", "application/vnd.openxmlformats-officedocument.presentationml.notesMaster+xml", o);
		r.into (z, "ppt/notesMasters/_rels/notesMaster1.xml.rels");
	}
	// the slides, their notes
	for (int i = 0; i < d.slides.n; i++)
	{
		Slide &s = *d.slides[i];
		Rels r; w.rels = &r;
		snprintf (path, sizeof path, "../slideLayouts/slideLayout%d.xml", iclamp (s.layout, 0, LY_COUNT - 1) + 1); r.add ("slideLayout", path);
		Buf o; w.slide (o, s, i);
		bool notes = !s.notes.empty ();
		if (notes) { snprintf (path, sizeof path, "../notesSlides/notesSlide%d.xml", i + 1); r.add ("notesSlide", path); }
		snprintf (path, sizeof path, "ppt/slides/slide%d.xml", i + 1);
		w.part (path, "application/vnd.openxmlformats-officedocument.presentationml.slide+xml", o);
		snprintf (path, sizeof path, "ppt/slides/_rels/slide%d.xml.rels", i + 1); r.into (z, path);
		if (notes)
		{
			Rels nr; nr.add ("notesMaster", "../notesMasters/notesMaster1.xml");
			snprintf (path, sizeof path, "../slides/slide%d.xml", i + 1); nr.add ("slide", path);
			Buf no; w.notes (no, s);
			snprintf (path, sizeof path, "ppt/notesSlides/notesSlide%d.xml", i + 1);
			w.part (path, "application/vnd.openxmlformats-officedocument.presentationml.notesSlide+xml", no);
			snprintf (path, sizeof path, "ppt/notesSlides/_rels/notesSlide%d.xml.rels", i + 1); nr.into (z, path);
		}
	}
	// the presentation
	{
		Rels r;
		r.add ("slideMaster", "slideMasters/slideMaster1.xml");
		int firstSlide = r.n + 1;
		for (int i = 0; i < d.slides.n; i++) { snprintf (path, sizeof path, "slides/slide%d.xml", i + 1); r.add ("slide", path); }
		int nm = anyNotes ? r.add ("notesMaster", "notesMasters/notesMaster1.xml") : 0;
		r.add ("theme", "theme/theme1.xml"); r.add ("presProps", "presProps.xml"); r.add ("viewProps", "viewProps.xml"); r.add ("tableStyles", "tableStyles.xml");
		Buf o;
		o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:presentation"); o.puts (PML_NS); o.puts (" saveSubsetFonts=\"1\"><p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>");
		if (nm) { snprintf (t, sizeof t, "<p:notesMasterIdLst><p:notesMasterId r:id=\"rId%d\"/></p:notesMasterIdLst>", nm); o.puts (t); }
		o.puts ("<p:sldIdLst>");
		for (int i = 0; i < d.slides.n; i++) { snprintf (t, sizeof t, "<p:sldId id=\"%d\" r:id=\"rId%d\"/>", 256 + i, firstSlide + i); o.puts (t); }
		o.puts ("</p:sldIdLst><p:sldSz"); attrl (o, "cx", emu (d.sw)); attrl (o, "cy", emu (d.sh)); o.puts ("/><p:notesSz cx=\"6858000\" cy=\"9144000\"/><p:defaultTextStyle>");
		{
			// the text outside placeholders: the "other" style at every level
			const TextStyle &st = d.style[TS_OTHER];
			for (int l = 0; l < 9; l++) { char tag[16]; snprintf (tag, sizeof tag, "a:lvl%dpPr", l + 1); w.ppr (o, tag, st.pf, 0, st.bullet, 0, 0, &st.cf); }
		}
		o.puts ("</p:defaultTextStyle>");
		bool sections = false; for (int i = 0; i < d.slides.n; i++) if (d.slides[i]->section[0]) sections = true;
		if (sections)
		{
			o.puts ("<p:extLst><p:ext uri=\"{521415D9-36F7-43E2-AB2F-B90AF26B5E84}\"><p14:sectionLst xmlns:p14=\"http://schemas.microsoft.com/office/powerpoint/2010/main\">");
			for (int i = 0; i < d.slides.n; )
			{
				int j = i + 1; while (j < d.slides.n && !d.slides[j]->section[0]) j++;
				o.puts ("<p14:section"); attr (o, "name", d.slides[i]->section[0] ? d.slides[i]->section : "Default Section");
				snprintf (t, sizeof t, " id=\"{6E1A3B2C-%04X-4C5D-8E9F-%012X}\"><p14:sldIdLst>", i & 0xFFFF, i + 1); o.puts (t);
				for (int k = i; k < j; k++) { snprintf (t, sizeof t, "<p14:sldId id=\"%d\"/>", 256 + k); o.puts (t); }
				o.puts ("</p14:sldIdLst></p14:section>");
				i = j;
			}
			o.puts ("</p14:sectionLst></p:ext></p:extLst>");
		}
		o.puts ("</p:presentation>");
		w.part ("ppt/presentation.xml", "application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml", o);
		r.into (z, "ppt/_rels/presentation.xml.rels");
	}
	{ Buf o; o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:presentationPr"); o.puts (PML_NS); o.puts ("/>"); w.part ("ppt/presProps.xml", "application/vnd.openxmlformats-officedocument.presentationml.presProps+xml", o); }
	{ Buf o; o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:viewPr"); o.puts (PML_NS); o.puts ("><p:normalViewPr><p:restoredLeft sz=\"15620\"/><p:restoredTop sz=\"94660\"/></p:normalViewPr><p:gridSpacing cx=\"76200\" cy=\"76200\"/></p:viewPr>"); w.part ("ppt/viewProps.xml", "application/vnd.openxmlformats-officedocument.presentationml.viewProps+xml", o); }
	{ Buf o; o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<a:tblStyleLst xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" def=\""); o.puts (TABLE_STYLE); o.puts ("\"/>"); w.part ("ppt/tableStyles.xml", "application/vnd.openxmlformats-officedocument.presentationml.tableStyles+xml", o); }
	// the package's properties
	{
		Buf o; o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\"><Application>Onyx Slides</Application>");
		snprintf (t, sizeof t, "<Slides>%d</Slides><AppVersion>1.0000</AppVersion></Properties>", d.slides.n); o.puts (t);
		w.part ("docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml", o);
		Buf c; c.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><dc:creator>Onyx Slides</dc:creator></cp:coreProperties>");
		w.part ("docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml", c);
		Buf r; r.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
		r.puts ("<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"ppt/presentation.xml\"/>");
		r.puts ("<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>");
		r.puts ("<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/></Relationships>");
		z.add ("_rels/.rels", r.b, (unsigned) r.n, true);
	}
	{
		Buf o; o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">");
		o.puts ("<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>");
		o.puts ("<Default Extension=\"png\" ContentType=\"image/png\"/><Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/><Default Extension=\"gif\" ContentType=\"image/gif\"/>");
		o.puts ("<Default Extension=\"bmp\" ContentType=\"image/bmp\"/><Default Extension=\"webp\" ContentType=\"image/webp\"/>");
		o.puts (w.types.str ()); o.puts ("</Types>");
		z.add ("[Content_Types].xml", o.b, (unsigned) o.n, true);
	}
	return z.finish (len);
}


// ---- reading --------------------------------------------------------------------------------------------------------
// An attribute without a prefix, exactly (the reader's own attr() also takes "r:id" for "id")
static bool attr_np (const XmlReader &r, const char *name, Buf &o)
{
	if (r.ev != X_START || !r.at) return false;
	int k = (int) strlen (name);
	const char *q = r.at;
	while (q < r.ate)
	{
		while (q < r.ate && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
		const char *an = q;
		while (q < r.ate && *q != '=' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
		int al = (int) (q - an);
		while (q < r.ate && *q != '=') q++;
		if (q >= r.ate) return false;
		q++;
		while (q < r.ate && *q != '"' && *q != '\'') q++;
		if (q >= r.ate) return false;
		char quote = *q++;
		const char *vs = q;
		while (q < r.ate && *q != quote) q++;
		if (al == k && !memcmp (an, name, k)) { XmlReader::decode (vs, q, o); return true; }
		if (q < r.ate) q++;
	}
	return false;
}
static long attr_l (const XmlReader &r, const char *name, long def) { Buf b; if (!(strchr (name, ':') ? r.attr (name, b) : attr_np (r, name, b))) return def; return strtol (b.str (), 0, 10); }
static bool attr_s (const XmlReader &r, const char *name, char *o, int cap) { Buf b; if (!(strchr (name, ':') ? r.attr (name, b) : attr_np (r, name, b))) return false; scpy (o, b.str (), cap); return true; }
static bool attr_on (const XmlReader &r, const char *name, bool def) { Buf b; if (!attr_np (r, name, b)) return def; return b.n && (b.b[0] == '1' || b.b[0] == 't'); }

// The element's direct children, each handed to f (at its X_START; f consumes it, or returns false: skipped).
// mc:AlternateContent: its Fallback's children read as the parent's (a Choice needs what Slides may not know).
template <class F> static void children (XmlReader &r, F f)
{
	if (r.ev != X_START) return;
	for (;;)
	{
		int t = r.next ();
		if (t == X_EOF || t == X_END) return;
		if (t != X_START) continue;
		if (r.is ("AlternateContent"))
		{
			for (;;)
			{
				int t2 = r.next ();
				if (t2 == X_EOF) return;
				if (t2 == X_END) break;
				if (t2 != X_START) continue;
				if (!r.is ("Fallback")) { r.skip (); continue; }
				for (;;)
				{
					int t3 = r.next ();
					if (t3 == X_EOF) return;
					if (t3 == X_END) break;
					if (t3 == X_START && !f ()) r.skip ();
				}
			}
			continue;
		}
		if (!f ()) r.skip ();
	}
}
// The text of the element (its X_TEXT, through its end)
static void elem_text (XmlReader &r, Buf &o)
{
	o.clear ();
	int depth = 1;
	while (depth > 0) { int t = r.next (); if (t == X_EOF) return; if (t == X_START) depth++; else if (t == X_END) depth--; else if (t == X_TEXT && depth == 1) o.putn (r.text.b, r.text.n); }
}

enum { PT_NONE, PT_TITLE, PT_CTRTITLE, PT_SUBTITLE, PT_BODY, PT_OBJ, PT_PIC, PT_DT, PT_FTR, PT_SLDNUM, PT_OTHER };
static int pt_of (const char *t)
{
	if (!t || !*t) return PT_OBJ;
	static const char *const N[] = { "", "title", "ctrTitle", "subTitle", "body", "obj", "pic", "dt", "ftr", "sldNum" };
	for (int i = 1; i < 10; i++) if (!strcmp (t, N[i])) return i;
	return PT_OTHER;
}
static int pt_kind (int pt) { return pt == PT_TITLE || pt == PT_CTRTITLE ? PH_TITLE : pt == PT_SUBTITLE ? PH_SUBTITLE : pt == PT_BODY || pt == PT_OBJ ? PH_BODY : pt == PT_PIC ? PH_PICTURE : pt == PT_DT ? PH_DATE : pt == PT_FTR ? PH_FOOTER : pt == PT_SLDNUM ? PH_NUMBER : PH_NONE; }
static int pt_class (int pt) { return pt == PT_CTRTITLE ? PT_TITLE : pt == PT_OBJ ? PT_BODY : pt; }

// A shape as read: the object, and what the file said of it (the rest comes from its placeholder, its style)
struct PS
{
	Object *o;
	int src;				// cNvPr id
	int pt, idx;				// placeholder: PT_*, its idx (-1)
	bool xf, fillSet, lineSet;
	bool bAnchor, bFit, bWrap, bIns[4];
	bool lst[5]; CharFmt lcf[5]; ParaFmt lpf[5]; unsigned lbu[5]; int lmar[5], lind[5]; bool lhasMar[5], lhasInd[5];
	int fillRef, lnRef; unsigned fillRefC, lnRefC, fontRefC; bool fontRef;
	char chart[160];			// a chart's part
	PS () : o (new Object), src (0), pt (PT_NONE), idx (-1), xf (false), fillSet (false), lineSet (false), bAnchor (false), bFit (false), bWrap (false),
		fillRef (0), lnRef (0), fillRefC (0), lnRefC (0), fontRefC (0), fontRef (false)
	{
		for (int i = 0; i < 4; i++) bIns[i] = false;
		for (int i = 0; i < 5; i++) { lst[i] = false; lcf[i] = cf_inherit (); lpf[i] = pf_inherit (); lbu[i] = 0; lmar[i] = lind[i] = 0; lhasMar[i] = lhasInd[i] = false; }
		chart[0] = 0;
		o->tb.inset[0] = o->tb.inset[2] = 254; o->tb.inset[1] = o->tb.inset[3] = 127;
	}
	~PS () { delete o; }
	Object *take () { Object *r = o; o = 0; return r; }
};
struct PRel { char id[24]; char type[40]; char target[200]; };
struct PLayout { char path[200]; int type; char name[32]; Vec<PS *> ph; ~PLayout () { for (int i = 0; i < ph.n; i++) delete ph[i]; } };
struct PFx { int src, para, cls, preset, sub, start, delay, dur; };

struct PptxIn
{
	Deck &d;
	const unsigned char *z; unsigned zn;
	bool own;
	unsigned theme[12];			// dk1 lt1 dk2 lt2 accent1..6 hlink folHlink
	int map[4];				// the colour map: bg1 tx1 bg2 tx2 -> a theme slot
	int lnW[3];
	Vec<PS *> masterPh;
	Vec<PLayout *> layouts;
	Vec<PRel> rels;				// the part being read's
	char partPath[200];
	PptxIn (Deck &d_, const unsigned char *z_, unsigned zn_) : d (d_), z (z_), zn (zn_), own (false)
	{
		static const unsigned T[12] = { 0x000000, 0xFFFFFF, 0x44546A, 0xE7E6E6, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0x0563C1, 0x954F72 };
		memcpy (theme, T, sizeof theme);
		map[0] = 1; map[1] = 0; map[2] = 3; map[3] = 2;
		lnW[0] = 6350; lnW[1] = 12700; lnW[2] = 19050;
		partPath[0] = 0;
	}
	~PptxIn () { for (int i = 0; i < masterPh.n; i++) delete masterPh[i]; for (int i = 0; i < layouts.n; i++) delete layouts[i]; }

	char *entry (const char *name, int *len)
	{
		pngsave::ZipEntry e;
		if (!pngsave::zip_find (z, zn, name, &e)) return 0;
		if (e.method == 0) { char *b = (char *) malloc (e.usize + 1); memcpy (b, e.data, e.usize); b[e.usize] = 0; *len = (int) e.usize; return b; }
		unsigned n = 0; unsigned char *u = img_inflate (e.data, e.csize, false, &n);
		if (!u) return 0;
		char *b = (char *) malloc (n + 1); memcpy (b, u, n); b[n] = 0; delete[] u; *len = (int) n;
		return b;
	}
	// a target relative to the part's folder -> the archive's path
	static void resolve (const char *part, const char *target, char *out, int cap)
	{
		char buf[400];
		if (target[0] == '/') scpy (buf, target + 1, sizeof buf);
		else
		{
			scpy (buf, part, sizeof buf);
			char *sl = strrchr (buf, '/'); if (sl) sl[1] = 0; else buf[0] = 0;
			scat (buf, target, sizeof buf);
		}
		// "a/b/../c" -> "a/c"
		char segs[16][64]; int n = 0;
		for (char *p = buf; *p; )
		{
			char *e = strchr (p, '/'); int l = e ? (int) (e - p) : (int) strlen (p);
			if (l == 2 && !memcmp (p, "..", 2)) { if (n) n--; }
			else if (!(l == 1 && p[0] == '.') && l > 0 && n < 16) { int k = l < 63 ? l : 63; memcpy (segs[n], p, k); segs[n][k] = 0; n++; }
			if (!e) break; p = e + 1;
		}
		out[0] = 0;
		for (int i = 0; i < n; i++) { if (i) scat (out, "/", cap); scat (out, segs[i], cap); }
	}
	// the part's relationships
	void read_rels (const char *part)
	{
		rels.clear (); scpy (partPath, part, sizeof partPath);
		char rp[260]; scpy (rp, part, sizeof rp);
		char *sl = strrchr (rp, '/');
		char path[300];
		if (sl) { *sl = 0; snprintf (path, sizeof path, "%s/_rels/%s.rels", rp, sl + 1); }
		else snprintf (path, sizeof path, "_rels/%s.rels", rp);
		int len; char *x = entry (path, &len); if (!x) return;
		XmlReader r (x, len);
		while (r.next () != X_EOF)
		{
			if (r.ev != X_START || !r.is ("Relationship")) continue;
			PRel e; Buf v;
			attr_s (r, "Id", e.id, sizeof e.id);
			e.type[0] = 0; if (attr_np (r, "Type", v)) { const char *t = strrchr (v.str (), '/'); scpy (e.type, t ? t + 1 : v.str (), sizeof e.type); }
			e.target[0] = 0;
			if (attr_np (r, "Target", v)) { if (attr_np (r, "TargetMode", v) && !strcmp (v.str (), "External")) continue; attr_np (r, "Target", v); resolve (part, v.str (), e.target, sizeof e.target); }
			rels.push (e);
		}
		free (x);
	}
	const char *rel (const char *id) { for (int i = 0; i < rels.n; i++) if (!strcmp (rels[i].id, id)) return rels[i].target; return 0; }
	const char *rel_type (const char *type) { for (int i = 0; i < rels.n; i++) if (!strcmp (rels[i].type, type)) return rels[i].target; return 0; }

	// ---- colours
	static unsigned hsl_mod (unsigned c, double lumMod, double lumOff)
	{
		double r = ((c >> 16) & 255) / 255.0, g = ((c >> 8) & 255) / 255.0, b = (c & 255) / 255.0;
		double mx = fmax (r, fmax (g, b)), mn = fmin (r, fmin (g, b)), l = (mx + mn) / 2, h = 0, s = 0;
		if (mx != mn)
		{
			double dd = mx - mn; s = l > 0.5 ? dd / (2 - mx - mn) : dd / (mx + mn);
			h = mx == r ? (g - b) / dd + (g < b ? 6 : 0) : mx == g ? (b - r) / dd + 2 : (r - g) / dd + 4; h /= 6;
		}
		l = l * lumMod + lumOff; if (l < 0) l = 0; if (l > 1) l = 1;
		auto hue = [] (double p, double q, double t) { if (t < 0) t += 1; if (t > 1) t -= 1; if (t < 1 / 6.0) return p + (q - p) * 6 * t; if (t < 0.5) return q; if (t < 2 / 3.0) return p + (q - p) * (2 / 3.0 - t) * 6; return p; };
		if (s == 0) r = g = b = l;
		else { double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q; r = hue (p, q, h + 1 / 3.0); g = hue (p, q, h); b = hue (p, q, h - 1 / 3.0); }
		return (unsigned) lround (r * 255) << 16 | (unsigned) lround (g * 255) << 8 | (unsigned) lround (b * 255);
	}
	int slot_of (const char *v)
	{
		static const char *const S[12] = { "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink" };
		for (int i = 0; i < 12; i++) if (!strcmp (v, S[i])) return i;
		if (!strcmp (v, "bg1")) return map[0]; if (!strcmp (v, "tx1")) return map[1]; if (!strcmp (v, "bg2")) return map[2]; if (!strcmp (v, "tx2")) return map[3];
		return 0;
	}
	// at a colour element (srgbClr, schemeClr...): the colour (THEME | slot when the theme's as it is), its alpha
	unsigned colour (XmlReader &r, int *alpha)
	{
		unsigned c = 0; int slot = -1;
		Buf v;
		if (r.is ("srgbClr") && attr_np (r, "val", v)) c = (unsigned) strtoul (v.str (), 0, 16) & 0xFFFFFF;
		else if (r.is ("schemeClr") && attr_np (r, "val", v)) { slot = slot_of (v.str ()); c = theme[slot]; }
		else if (r.is ("sysClr")) { if (attr_np (r, "lastClr", v)) c = (unsigned) strtoul (v.str (), 0, 16); else if (attr_np (r, "val", v)) c = !strcmp (v.str (), "window") ? 0xFFFFFF : 0; }
		else if (r.is ("prstClr") && attr_np (r, "val", v))
		{
			static const struct { const char *n; unsigned c; } P[] = { { "black", 0 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 }, { "blue", 0x0000FF }, { "yellow", 0xFFFF00 },
				{ "gray", 0x808080 }, { "grey", 0x808080 }, { "orange", 0xFFA500 }, { "darkGray", 0xA9A9A9 }, { "lightGray", 0xD3D3D3 } };
			for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) if (!strcmp (v.str (), P[i].n)) c = P[i].c;
		}
		else if (r.is ("scrgbClr")) c = (unsigned) (attr_l (r, "r", 0) * 255 / 100000) << 16 | (unsigned) (attr_l (r, "g", 0) * 255 / 100000) << 8 | (unsigned) (attr_l (r, "b", 0) * 255 / 100000);
		bool mod = false; double lm = 1, lo = 0;
		children (r, [&] () -> bool
		{
			long val = attr_l (r, "val", 100000);
			if (r.is ("alpha")) { if (alpha) *alpha = (int) ((val * 255 + 50000) / 100000); }
			else if (r.is ("lumMod")) { lm = val / 100000.0; mod = true; }
			else if (r.is ("lumOff")) { lo = val / 100000.0; mod = true; }
			else if (r.is ("tint") || r.is ("shade"))
			{
				// (in linear light, as PowerPoint: a tint towards white, a shade towards black)
				double t = val / 100000.0; bool tint = r.is ("tint");
				unsigned o2 = 0;
				for (int sh = 16; sh >= 0; sh -= 8)
				{
					double v = ((c >> sh) & 255) / 255.0;
					v = v <= 0.04045 ? v / 12.92 : pow ((v + 0.055) / 1.055, 2.4);
					v = tint ? v * t + (1 - t) : v * t;
					v = v <= 0.0031308 ? v * 12.92 : 1.055 * pow (v, 1 / 2.4) - 0.055;
					o2 |= (unsigned) lround (fmin (1, fmax (0, v)) * 255) << sh;
				}
				c = o2; mod = true;
			}
			return false;
		});
		if (lm != 1 || lo != 0) c = hsl_mod (c, lm, lo);
		if (slot >= 0 && !mod)
		{
			// the theme's colour: Slides' slot (folHlink: the link's)
			static const int TC[12] = { TC_DK1, TC_LT1, TC_DK2, TC_LT2, TC_ACC1, TC_ACC2, TC_ACC3, TC_ACC4, TC_ACC5, TC_ACC6, TC_LINK, TC_LINK };
			if (slot < 11) return THEME | TC[slot];
		}
		return c & 0xFFFFFF;
	}
	// at an element holding a colour (solidFill, gs, fgClr, a style's reference): its colour
	unsigned colour_in (XmlReader &r, int *alpha, unsigned def = 0)
	{
		unsigned c = def;
		children (r, [&] () -> bool { if (r.is ("srgbClr") || r.is ("schemeClr") || r.is ("sysClr") || r.is ("prstClr") || r.is ("scrgbClr")) { c = colour (r, alpha); return true; } return false; });
		return c;
	}
	// at a fill element: the fill (false: not one)
	bool fill (XmlReader &r, Fill &f)
	{
		if (r.is ("noFill")) { f = fill_none (); r.skip (); return true; }
		if (r.is ("solidFill")) { int a = 255; f = fill_solid (colour_in (r, &a)); f.alpha = (unsigned char) a; return true; }
		if (r.is ("pattFill")) { f = fill_none (); f.type = FILL_SOLID; children (r, [&] () -> bool { if (r.is ("fgClr")) { f.c1 = colour_in (r, 0); return true; } return false; }); return true; }
		if (r.is ("gradFill"))
		{
			f = fill_none (); f.type = FILL_GRADIENT; f.angle = 90;
			long p0 = 200000, p1 = -1; int a = 255;
			children (r, [&] () -> bool
			{
				if (r.is ("gsLst")) { children (r, [&] () -> bool { if (!r.is ("gs")) return false; long pos = attr_l (r, "pos", 0); int aa = 255; unsigned c = colour_in (r, &aa); if (pos < p0) { p0 = pos; f.c1 = c; a = aa; } if (pos > p1) { p1 = pos; f.c2 = c; } return true; }); return true; }
				if (r.is ("lin")) { f.angle = (short) ((attr_l (r, "ang", 0) / 60000) % 360); return false; }
				return false;
			});
			f.alpha = (unsigned char) a;
			return true;
		}
		if (r.is ("blipFill") || r.is ("grpFill")) { f = fill_none (); r.skip (); return true; }
		return false;
	}
	void line (XmlReader &r, Line &l, bool *set)
	{
		long w = attr_l (r, "w", -1);
		bool has = false, none = false;
		children (r, [&] () -> bool
		{
			Fill f;
			if (fill (r, f)) { has = true; if (f.type == FILL_NONE) none = true; else { l.color = f.c1; if (l.type == LN_NONE) l.type = LN_SOLID; } return true; }
			Buf v;
			if (r.is ("prstDash") && attr_np (r, "val", v)) { const char *s = v.str (); l.type = !strcmp (s, "solid") ? LN_SOLID : strstr (s, "Dot") || !strcmp (s, "dot") ? LN_DOT : LN_DASH; }
			if ((r.is ("headEnd") || r.is ("tailEnd")) && attr_np (r, "type", v))
			{
				const char *s = v.str (); signed char h = !strcmp (s, "none") ? AH_NONE : !strcmp (s, "arrow") ? AH_OPEN : !strcmp (s, "oval") ? AH_DOT : AH_ARROW;
				if (r.is ("headEnd")) l.head0 = h; else l.head1 = h;
			}
			return false;
		});
		if (w >= 0) l.width = (short) hmm_of (w);
		if (has) { *set = true; if (none) l.type = LN_NONE; else if (l.type == LN_NONE) l.type = LN_SOLID; }
	}

	// ---- text
	void rpr (XmlReader &r, CharFmt &f)
	{
		long sz = attr_l (r, "sz", 0); if (sz) f.size = (short) ((sz + 5) / 10);
		Buf v;
		if (attr_np (r, "b", v)) { f.set |= CF_BOLD; if (v.b[0] == '1' || v.b[0] == 't') f.flags |= CF_BOLD; else f.flags &= ~CF_BOLD; }
		if (attr_np (r, "i", v)) { f.set |= CF_ITALIC; if (v.b[0] == '1' || v.b[0] == 't') f.flags |= CF_ITALIC; else f.flags &= ~CF_ITALIC; }
		if (attr_np (r, "u", v)) { f.set |= CF_UNDER; if (strcmp (v.str (), "none")) f.flags |= CF_UNDER; else f.flags &= ~CF_UNDER; }
		if (attr_np (r, "strike", v)) { f.set |= CF_STRIKE; if (strcmp (v.str (), "noStrike")) f.flags |= CF_STRIKE; else f.flags &= ~CF_STRIKE; }
		if (attr_np (r, "baseline", v)) { long b = atol (v.str ()); f.set |= CF_SUPER | CF_SUB; f.flags &= ~(CF_SUPER | CF_SUB); if (b > 0) f.flags |= CF_SUPER; else if (b < 0) f.flags |= CF_SUB; }
		children (r, [&] () -> bool
		{
			Fill fl;
			if (r.is ("solidFill") || r.is ("gradFill")) { if (fill (r, fl)) f.color = fl.c1; return true; }
			if (r.is ("latin"))
			{
				Buf t;
				if (attr_np (r, "typeface", t) && t.n)
				{
					const char *s = t.str ();
					f.font = !strncmp (s, "+mj", 3) ? FONT_MAJOR : !strncmp (s, "+mn", 3) ? FONT_MINOR : (short) d.font_index (s);
				}
			}
			return false;
		});
	}
	// a paragraph's (or a level's) properties: marL / indent (EMU) when given
	void ppr (XmlReader &r, ParaFmt &p, unsigned *bullet, CharFmt *def, int *marL, int *ind, bool *hasMar, bool *hasInd)
	{
		Buf v;
		if (attr_np (r, "algn", v)) { const char *s = v.str (); p.align = !strcmp (s, "ctr") ? AL_CENTER : !strcmp (s, "r") ? AL_RIGHT : !strcmp (s, "just") || !strcmp (s, "dist") ? AL_JUSTIFY : AL_LEFT; }
		if (attr_np (r, "lvl", v)) p.level = (signed char) iclamp (atoi (v.str ()), 0, 4);
		if (marL && attr_np (r, "marL", v)) { *marL = atoi (v.str ()); if (hasMar) *hasMar = true; }
		if (ind && attr_np (r, "indent", v)) { *ind = atoi (v.str ()); if (hasInd) *hasInd = true; }
		children (r, [&] () -> bool
		{
			if (r.is ("lnSpc")) { children (r, [&] () -> bool { if (r.is ("spcPct")) p.spacing = (short) (attr_l (r, "val", 100000) / 1000); return false; }); return true; }
			if (r.is ("spcBef") || r.is ("spcAft"))
			{
				bool bef = r.is ("spcBef");
				children (r, [&] () -> bool { if (r.is ("spcPts")) { short v2 = (short) (attr_l (r, "val", 0) / 10); if (bef) p.before = v2; else p.after = v2; } return false; });
				return true;
			}
			if (r.is ("buNone")) p.bullet = BU_NONE;
			else if (r.is ("buChar")) { p.bullet = BU_BULLET; Buf c; if (bullet && attr_np (r, "char", c) && c.n) { int l; *bullet = ss::u8_dec (c.b, c.n, &l); } }
			else if (r.is ("buAutoNum")) p.bullet = BU_NUMBER;
			else if (r.is ("buBlip")) p.bullet = BU_BULLET;
			else if (r.is ("defRPr") && def) { rpr (r, *def); return true; }
			return false;
		});
	}
	void para (XmlReader &r, TextBody &tb)
	{
		Para *q = new Para; tb.p.push (q);
		children (r, [&] () -> bool
		{
			if (r.is ("pPr")) { ppr (r, q->pf, 0, 0, 0, 0, 0, 0); return true; }
			if (r.is ("endParaRPr")) { CharFmt f = cf_inherit (); rpr (r, f); q->end = f; return true; }
			if (r.is ("r") || r.is ("fld"))
			{
				CharFmt f = cf_inherit (); Buf t; bool hasT = false;
				children (r, [&] () -> bool
				{
					if (r.is ("rPr")) { rpr (r, f); return true; }
					if (r.is ("t")) { elem_text (r, t); hasT = true; return true; }
					return false;
				});
				if (hasT)
				{
					unsigned cs[512]; int n = 0;
					for (int i = 0; i < t.n; ) { int l; unsigned c = ss::u8_dec (t.b + i, t.n - i, &l); i += l > 0 ? l : 1; if (c == '\n' || c == '\r') c = ' '; if (n < 512) cs[n++] = c; if (n == 512) { q->insert (q->len, cs, n, f); n = 0; } }
					if (n) q->insert (q->len, cs, n, f);
				}
				q->end = f;
				return true;
			}
			if (r.is ("br"))
			{
				// a line break: a new paragraph, the same format
				CharFmt f = cf_inherit ();
				children (r, [&] () -> bool { if (r.is ("rPr")) { rpr (r, f); return true; } return false; });
				q->end = f;
				Para *n2 = new Para; n2->pf = q->pf; n2->end = f; tb.p.push (n2); q = n2;
				return true;
			}
			return false;
		});
	}
	void body_pr (XmlReader &r, PS &s, TextBody &tb)
	{
		Buf v;
		if (attr_np (r, "wrap", v)) { tb.wrap = strcmp (v.str (), "none") != 0; s.bWrap = true; }
		static const char *const IN[4] = { "lIns", "tIns", "rIns", "bIns" };
		for (int i = 0; i < 4; i++) if (attr_np (r, IN[i], v)) { tb.inset[i] = (short) hmm_of (atol (v.str ())); s.bIns[i] = true; }
		if (attr_np (r, "anchor", v)) { const char *a = v.str (); tb.anchor = !strcmp (a, "ctr") ? AN_MIDDLE : !strcmp (a, "b") ? AN_BOTTOM : AN_TOP; s.bAnchor = true; }
		children (r, [&] () -> bool
		{
			if (r.is ("normAutofit")) { tb.fit = FIT_SHRINK; s.bFit = true; }
			else if (r.is ("spAutoFit")) { tb.fit = FIT_GROW; s.bFit = true; }
			else if (r.is ("noAutofit")) { tb.fit = FIT_NONE; s.bFit = true; }
			return false;
		});
	}
	void tx_body (XmlReader &r, PS &s, TextBody &tb)
	{
		tb.clear ();
		children (r, [&] () -> bool
		{
			if (r.is ("bodyPr")) { body_pr (r, s, tb); return true; }
			if (r.is ("lstStyle"))
			{
				children (r, [&] () -> bool
				{
					int l = -1;
					for (int k = 0; k < 5; k++) { char t[12]; snprintf (t, sizeof t, "lvl%dpPr", k + 1); if (r.is (t)) l = k; }
					if (l < 0) return false;
					s.lst[l] = true;
					ppr (r, s.lpf[l], &s.lbu[l], &s.lcf[l], &s.lmar[l], &s.lind[l], &s.lhasMar[l], &s.lhasInd[l]);
					return true;
				});
				return true;
			}
			if (r.is ("p")) { para (r, tb); return true; }
			return false;
		});
	}

	// ---- shapes
	struct Xf { double ox, oy, sx, sy; };
	void xfrm (XmlReader &r, PS &s, const Xf &x)
	{
		Object &o = *s.o;
		long rot = attr_l (r, "rot", 0);
		o.rot = (short) (((rot / 60000) % 360 + 360) % 360);
		o.flipH = attr_on (r, "flipH", false); o.flipV = attr_on (r, "flipV", false);
		double X = 0, Y = 0, W = 0, H = 0;
		children (r, [&] () -> bool
		{
			if (r.is ("off")) { X = attr_l (r, "x", 0); Y = attr_l (r, "y", 0); }
			else if (r.is ("ext")) { W = attr_l (r, "cx", 0); H = attr_l (r, "cy", 0); }
			return false;
		});
		o.x = hmm_of (x.ox + X * x.sx); o.y = hmm_of (x.oy + Y * x.sy); o.w = hmm_of (W * x.sx); o.h = hmm_of (H * x.sy);
		s.xf = true;
	}
	void sp_pr (XmlReader &r, PS &s, const Xf &x)
	{
		Object &o = *s.o;
		children (r, [&] () -> bool
		{
			if (r.is ("xfrm")) { xfrm (r, s, x); return true; }
			if (r.is ("prstGeom"))
			{
				Buf v; attr_np (r, "prst", v);
				const char *p = v.str ();
				if (!strcmp (p, "line") || !strncmp (p, "straightConnector", 17) || !strncmp (p, "bentConnector", 13) || !strncmp (p, "curvedConnector", 15)) o.kind = OB_LINE;
				else
				{
					int sh = SH_RECT;
					for (int i = 0; i < SH_COUNT; i++) if (!strcmp (p, PRST[i])) sh = i;
					if (!strcmp (p, "flowChartProcess") || !strcmp (p, "snip1Rect")) sh = SH_RECT;
					else if (!strcmp (p, "flowChartAlternateProcess") || !strcmp (p, "round2SameRect") || !strcmp (p, "round1Rect")) sh = SH_ROUND;
					else if (!strcmp (p, "flowChartConnector")) sh = SH_ELLIPSE;
					else if (!strcmp (p, "flowChartDecision")) sh = SH_DIAMOND;
					else if (!strncmp (p, "wedge", 5) || !strncmp (p, "callout", 7)) sh = SH_CALLOUT;
					else if (!strcmp (p, "mathPlus")) sh = SH_PLUS;
					o.shape = (signed char) sh;
				}
				children (r, [&] () -> bool
				{
					if (!r.is ("avLst")) return false;
					children (r, [&] () -> bool
					{
						Buf n, f;
						if (r.is ("gd") && attr_np (r, "name", n) && !strcmp (n.str (), "adj") && attr_np (r, "fmla", f) && !strncmp (f.str (), "val ", 4) && o.shape == SH_ROUND)
							o.radius = (short) (atol (f.str () + 4) / 100);
						return false;
					});
					return true;
				});
				return true;
			}
			Fill f;
			if (fill (r, f)) { o.fill = f; s.fillSet = true; return true; }
			if (r.is ("ln")) { line (r, o.line, &s.lineSet); return true; }
			if (r.is ("effectLst")) { children (r, [&] () -> bool { if (r.is ("outerShdw")) o.shadow = true; return false; }); return true; }
			return false;
		});
	}
	void style_refs (XmlReader &r, PS &s)
	{
		children (r, [&] () -> bool
		{
			if (r.is ("fillRef")) { s.fillRef = (int) attr_l (r, "idx", 0); s.fillRefC = colour_in (r, 0); return true; }
			if (r.is ("lnRef")) { s.lnRef = (int) attr_l (r, "idx", 0); s.lnRefC = colour_in (r, 0); return true; }
			if (r.is ("fontRef")) { bool any = false; unsigned c = 0; children (r, [&] () -> bool { if (r.is ("srgbClr") || r.is ("schemeClr") || r.is ("sysClr") || r.is ("prstClr")) { c = colour (r, 0); any = true; return true; } return false; }); if (any) { s.fontRef = true; s.fontRefC = c; } return true; }
			return false;
		});
	}
	void nv_pr (XmlReader &r, PS &s)
	{
		// (cNvPr, cNvSpPr, nvPr: their contents)
		children (r, [&] () -> bool
		{
			if (r.is ("cNvPr")) { s.src = (int) attr_l (r, "id", 0); Buf v; if (attr_np (r, "name", v)) scpy (s.o->name, v.str (), sizeof s.o->name); return false; }
			if (r.is ("cNvSpPr")) { if (attr_on (r, "txBox", false)) s.o->kind = OB_TEXT; return false; }
			if (r.is ("nvPr"))
			{
				children (r, [&] () -> bool
				{
					if (!r.is ("ph")) return false;
					Buf t; attr_np (r, "type", t); s.pt = pt_of (t.str ()); s.idx = (int) attr_l (r, "idx", -1);
					return false;
				});
				return true;
			}
			return false;
		});
	}
	PS *shape (XmlReader &r, const Xf &x, int kind)
	{
		PS *s = new PS; s->o->kind = (signed char) kind;
		bool isTextBox = false;
		children (r, [&] () -> bool
		{
			if (r.is ("nvSpPr") || r.is ("nvCxnSpPr") || r.is ("nvPicPr") || r.is ("nvGraphicFramePr")) { nv_pr (r, *s); if (s->o->kind == OB_TEXT && kind == OB_SHAPE) isTextBox = true; return true; }
			if (r.is ("spPr")) { int k = s->o->kind; sp_pr (r, *s, x); if (k == OB_PICTURE && s->o->kind == OB_LINE) s->o->kind = OB_PICTURE; return true; }
			if (r.is ("style")) { style_refs (r, *s); return true; }
			if (r.is ("txBody")) { tx_body (r, *s, s->o->tb); return true; }
			if (r.is ("blipFill"))
			{
				children (r, [&] () -> bool
				{
					Buf id;
					if (r.is ("blip") && (r.attr ("r:embed", id) || r.attr ("embed", id))) { const char *t = rel (id.str ()); if (t) s->o->img = picture (t); return false; }
					if (r.is ("srcRect")) { s->o->crop[0] = (short) (attr_l (r, "l", 0) / 100); s->o->crop[1] = (short) (attr_l (r, "t", 0) / 100); s->o->crop[2] = (short) (attr_l (r, "r", 0) / 100); s->o->crop[3] = (short) (attr_l (r, "b", 0) / 100); return false; }
					return false;
				});
				return true;
			}
			if (r.is ("xfrm")) { xfrm (r, *s, x); return true; }			// (a graphic frame's)
			if (r.is ("graphic")) { graphic (r, *s); return true; }
			return false;
		});
		(void) isTextBox;
		if (s->o->kind == OB_SHAPE && s->pt != PT_NONE) s->o->kind = OB_TEXT;
		return s;
	}
	int picture (const char *path)
	{
		pngsave::ZipEntry e;
		if (!pngsave::zip_find (z, zn, path, &e)) return -1;
		const char *base = strrchr (path, '/'); base = base ? base + 1 : path;
		if (e.method == 0) return pic_add (base, e.data, e.usize);
		unsigned n = 0; unsigned char *u = img_inflate (e.data, e.csize, false, &n);
		if (!u) return -1;
		int k = pic_add (base, u, n); delete[] u;
		return k;
	}
	void graphic (XmlReader &r, PS &s)
	{
		children (r, [&] () -> bool
		{
			if (!r.is ("graphicData")) return false;
			children (r, [&] () -> bool
			{
				if (r.is ("tbl")) { table (r, s); return true; }
				if (r.is ("chart")) { Buf id; if (r.attr ("r:id", id) || r.attr ("id", id)) { const char *t = rel (id.str ()); if (t) scpy (s.chart, t, sizeof s.chart); } s.o->kind = OB_CHART; return false; }
				return false;
			});
			return true;
		});
	}
	void table (XmlReader &r, PS &s)
	{
		int colW[64], nc = 0, rowH[64], nr = 0;
		bool header = false, banded = false;
		struct C { TextBody *tb; Fill f; };
		Vec<C> cells;
		children (r, [&] () -> bool
		{
			if (r.is ("tblPr")) { header = attr_on (r, "firstRow", false); banded = attr_on (r, "bandRow", false); return false; }
			if (r.is ("tblGrid")) { children (r, [&] () -> bool { if (r.is ("gridCol") && nc < 64) colW[nc++] = hmm_of (attr_l (r, "w", 0)); return false; }); return true; }
			if (r.is ("tr"))
			{
				if (nr >= 64) return false;
				rowH[nr] = hmm_of (attr_l (r, "h", 0));
				int c = 0;
				children (r, [&] () -> bool
				{
					if (!r.is ("tc")) return false;
					C cell; cell.tb = new TextBody; cell.f = fill_none (); cell.f.type = FILL_INHERIT;
					PS dummy;
					cell.tb->inset[0] = cell.tb->inset[2] = 254; cell.tb->inset[1] = cell.tb->inset[3] = 127;
					children (r, [&] () -> bool
					{
						if (r.is ("txBody")) { TextBody keep; for (int i = 0; i < 4; i++) keep.inset[i] = cell.tb->inset[i]; tx_body (r, dummy, *cell.tb); for (int i = 0; i < 4; i++) cell.tb->inset[i] = keep.inset[i]; return true; }
						if (r.is ("tcPr"))
						{
							Buf v;
							static const char *const M[4] = { "marL", "marT", "marR", "marB" };
							for (int i = 0; i < 4; i++) if (attr_np (r, M[i], v)) cell.tb->inset[i] = (short) hmm_of (atol (v.str ()));
							if (attr_np (r, "anchor", v)) { const char *a = v.str (); cell.tb->anchor = !strcmp (a, "ctr") ? AN_MIDDLE : !strcmp (a, "b") ? AN_BOTTOM : AN_TOP; }
							children (r, [&] () -> bool { Fill f; if (fill (r, f)) { cell.f = f; return true; } return false; });
							return true;
						}
						return false;
					});
					if (c < 64) cells.push (cell); else delete cell.tb;
					c++;
					return true;
				});
				for (; c < nc; c++) { C e; e.tb = new TextBody; e.f = fill_none (); e.f.type = FILL_INHERIT; cells.push (e); }
				nr++;
				return true;
			}
			return false;
		});
		if (!nc || !nr) { for (int i = 0; i < cells.n; i++) delete cells[i].tb; return; }
		Table *t = new Table (nr, nc);
		for (int c = 0; c < nc; c++) t->colW[c] = colW[c];
		for (int i = 0; i < nr; i++) t->rowH[i] = rowH[i];
		t->header = header; t->banded = banded;
		// the cells, row by row (a row with more cells than the grid: the extra ones dropped)
		int k = 0;
		for (int i = 0; i < nr; i++)
			for (int c = 0; c < nc; c++)
			{
				if (k < cells.n) { t->at (i, c).copy_from (*cells[k].tb); t->at (i, c).ensure (); t->cfill[i * nc + c] = cells[k].f; }
				k++;
			}
		for (int i = 0; i < cells.n; i++) delete cells[i].tb;
		s.o->tbl = t; s.o->kind = OB_TABLE;
	}
	// a shape tree (spTree, grpSp): its shapes, groups flattened
	void tree (XmlReader &r, const Xf &x, Vec<PS *> &out)
	{
		Xf cx = x;
		children (r, [&] () -> bool
		{
			if (r.is ("grpSpPr"))
			{
				double gx = 0, gy = 0, gw = 0, gh = 0, chx = 0, chy = 0, chw = 0, chh = 0;
				children (r, [&] () -> bool
				{
					if (!r.is ("xfrm")) return false;
					children (r, [&] () -> bool
					{
						if (r.is ("off")) { gx = attr_l (r, "x", 0); gy = attr_l (r, "y", 0); }
						else if (r.is ("ext")) { gw = attr_l (r, "cx", 0); gh = attr_l (r, "cy", 0); }
						else if (r.is ("chOff")) { chx = attr_l (r, "x", 0); chy = attr_l (r, "y", 0); }
						else if (r.is ("chExt")) { chw = attr_l (r, "cx", 0); chh = attr_l (r, "cy", 0); }
						return false;
					});
					return true;
				});
				if (chw > 0 && chh > 0 && gw > 0 && gh > 0)
				{
					double kx = gw / chw, ky = gh / chh;
					cx.ox = x.ox + x.sx * (gx - chx * kx); cx.oy = x.oy + x.sy * (gy - chy * ky); cx.sx = x.sx * kx; cx.sy = x.sy * ky;
				}
				return true;
			}
			if (r.is ("sp")) { out.push (shape (r, cx, OB_SHAPE)); return true; }
			if (r.is ("cxnSp")) { PS *s = shape (r, cx, OB_LINE); s->o->kind = OB_LINE; out.push (s); return true; }
			if (r.is ("pic")) { PS *s = shape (r, cx, OB_PICTURE); s->o->kind = OB_PICTURE; out.push (s); return true; }
			if (r.is ("graphicFrame")) { PS *s = shape (r, cx, OB_SHAPE); if (s->o->kind == OB_TABLE || s->o->kind == OB_CHART) out.push (s); else delete s; return true; }
			if (r.is ("grpSp")) { tree (r, cx, out); return true; }
			return false;
		});
	}
	// a cSld's background and shapes
	void c_sld (XmlReader &r, Fill *bg, Vec<PS *> &out, char *name)
	{
		if (name) { Buf v; if (attr_np (r, "name", v)) scpy (name, v.str (), 32); }
		Xf x = { 0, 0, 1, 1 };
		children (r, [&] () -> bool
		{
			if (r.is ("bg"))
			{
				children (r, [&] () -> bool
				{
					if (r.is ("bgPr")) { children (r, [&] () -> bool { Fill f; if (fill (r, f)) { if (bg) *bg = f; return true; } return false; }); return true; }
					if (r.is ("bgRef")) { unsigned c = colour_in (r, 0, 0xFFFFFF); if (bg) *bg = fill_solid (c); return true; }
					return false;
				});
				return true;
			}
			if (r.is ("spTree")) { tree (r, x, out); return true; }
			return false;
		});
	}

	// ---- the chart part
	void chart (const char *path, Object &o)
	{
		int len; char *x = entry (path, &len); if (!x) return;
		Chart *c = new Chart; c->legend = false;
		XmlReader r (x, len);
		bool typeSet = false, inTitle = false, titleRich = false;
		int ser = -1; int where = 0;			// 1 tx, 2 cat, 3 val
		int ptIdx = -1;
		Buf title;
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START)
			{
				Buf v;
				if (r.is ("title") && ser < 0) inTitle = true;
				else if (inTitle && r.is ("rich")) titleRich = true;
				else if (inTitle && titleRich && r.is ("t")) { Buf t; elem_text (r, t); title.puts (t.str ()); continue; }
				else if (!typeSet && (r.is ("barChart") || r.is ("bar3DChart"))) { c->type = CH_COLUMN; typeSet = true; }
				else if (!typeSet && (r.is ("lineChart") || r.is ("line3DChart") || r.is ("scatterChart") || r.is ("radarChart") || r.is ("stockChart"))) { c->type = CH_LINE; typeSet = true; }
				else if (!typeSet && (r.is ("pieChart") || r.is ("pie3DChart") || r.is ("doughnutChart") || r.is ("ofPieChart"))) { c->type = CH_PIE; typeSet = true; }
				else if (!typeSet && (r.is ("areaChart") || r.is ("area3DChart"))) { c->type = CH_AREA; typeSet = true; }
				else if (r.is ("barDir") && attr_np (r, "val", v) && !strcmp (v.str (), "bar")) c->type = CH_BAR;
				else if (r.is ("ser")) { ser++; where = 0; }
				else if (ser >= 0 && ser < 8 && r.is ("tx") && where == 0) where = 1;
				else if (ser >= 0 && ser < 8 && (r.is ("cat") || r.is ("xVal"))) where = 2;
				else if (ser >= 0 && ser < 8 && (r.is ("val") || r.is ("yVal"))) where = 3;
				else if (r.is ("pt")) ptIdx = (int) attr_l (r, "idx", -1);
				else if (r.is ("v") && ser >= 0 && ser < 8)
				{
					Buf t; elem_text (r, t);
					if (where == 1) scpy (c->ser[ser], t.str (), 32);
					else if (where == 2 && ptIdx >= 0 && ptIdx < 24) { if (!ser) scpy (c->cat[ptIdx], t.str (), 24); if (!ser && ptIdx + 1 > c->ncat) c->ncat = ptIdx + 1; }
					else if (where == 3 && ptIdx >= 0 && ptIdx < 24) { c->val[ser][ptIdx] = strtod (t.str (), 0); if (ptIdx + 1 > c->ncat) c->ncat = ptIdx + 1; }
					continue;
				}
				else if (r.is ("legend")) c->legend = true;
				else if (r.is ("showVal") && attr_np (r, "val", v) && v.b[0] == '1') c->labels = true;
				else if (r.is ("autoTitleDeleted") && attr_np (r, "val", v) && v.b[0] == '1') {}
			}
			else if (r.ev == X_END)
			{
				if (r.is ("title")) inTitle = titleRich = false;
				else if (r.is ("tx") && where == 1) where = 0;
			}
		}
		free (x);
		c->nser = imin (ser + 1, 8);
		if (c->type == CH_PIE) c->nser = imin (c->nser, 1);
		for (int i = 0; i < c->ncat; i++) if (!c->cat[i][0]) snprintf (c->cat[i], 24, "%d", i + 1);
		scpy (c->title, title.str (), sizeof c->title);
		o.chart = c;
	}

	// ---- the text formats a placeholder takes from its layout's, its master's (a file of the others)
	static void cf_fill (CharFmt &c, const CharFmt &f)
	{
		if (c.font == FONT_INHERIT) c.font = f.font;
		if (!c.size) c.size = f.size;
		if (c.color == AUTO) c.color = f.color;
		unsigned short add = (unsigned short) (f.set & ~c.set);
		c.flags = (unsigned short) ((c.flags & ~add) | (f.flags & add)); c.set |= add;
	}
	static void pf_fill (ParaFmt &p, const ParaFmt &f)
	{
		if (p.align == AL_INHERIT) p.align = f.align;
		if (p.bullet == BU_INHERIT) p.bullet = f.bullet;
		if (p.before < 0) p.before = f.before;
		if (p.after < 0) p.after = f.after;
		if (!p.spacing) p.spacing = f.spacing;
	}
	static void bake (TextBody &tb, const PS *from)
	{
		if (!from) return;
		for (int i = 0; i < tb.p.n; i++)
		{
			Para *q = tb.p[i]; int l = iclamp (q->pf.level, 0, 4);
			if (!from->lst[l]) continue;
			pf_fill (q->pf, from->lpf[l]);
			for (int k = 0; k < q->len; k++) cf_fill (q->cf[k], from->lcf[l]);
			cf_fill (q->end, from->lcf[l]);
		}
	}
	static void inherit_body (PS &s, const PS *f)
	{
		if (!f) return;
		TextBody &a = s.o->tb; const TextBody &b = f->o->tb;
		if (!s.bAnchor && f->bAnchor) { a.anchor = b.anchor; s.bAnchor = true; }
		if (!s.bFit && f->bFit) { a.fit = b.fit; s.bFit = true; }
		if (!s.bWrap && f->bWrap) { a.wrap = b.wrap; s.bWrap = true; }
		for (int i = 0; i < 4; i++) if (!s.bIns[i] && f->bIns[i]) { a.inset[i] = b.inset[i]; s.bIns[i] = true; }
		if (!s.xf && f->xf) { s.o->x = f->o->x; s.o->y = f->o->y; s.o->w = f->o->w; s.o->h = f->o->h; s.xf = true; }
	}
	PS *master_ph (int pt)
	{
		int c = pt_class (pt);
		if (c == PT_SUBTITLE || c == PT_PIC || c == PT_OTHER) c = PT_BODY;
		for (int i = 0; i < masterPh.n; i++) if (pt_class (masterPh[i]->pt) == c) return masterPh[i];
		return 0;
	}
	static PS *layout_ph (const PLayout *L, const PS &s)
	{
		if (!L) return 0;
		if (s.idx >= 0) for (int i = 0; i < L->ph.n; i++) if (L->ph[i]->idx == s.idx) return L->ph[i];
		int c = pt_class (s.pt);
		for (int i = 0; i < L->ph.n; i++) if (pt_class (L->ph[i]->pt) == c) return L->ph[i];
		return 0;
	}
	// a shape's style references -> its fill, line, text colour (when the shape says none itself)
	void apply_refs (PS &s)
	{
		Object &o = *s.o;
		if (!s.fillSet && s.fillRef > 0 && o.kind != OB_LINE) o.fill = fill_solid (s.fillRefC);
		if (!s.lineSet && s.lnRef > 0) { o.line.type = LN_SOLID; o.line.color = s.lnRefC; o.line.width = (short) hmm_of (lnW[iclamp (s.lnRef, 1, 3) - 1]); }
		if (s.fontRef)
			for (int i = 0; i < o.tb.p.n; i++) { Para *q = o.tb.p[i]; for (int k = 0; k < q->len; k++) if (q->cf[k].color == AUTO) q->cf[k].color = s.fontRefC; if (q->end.color == AUTO) q->end.color = s.fontRefC; }
	}

	// ---- the parts
	void read_theme (const char *path)
	{
		int len; char *x = entry (path, &len); if (!x) return;
		XmlReader r (x, len);
		static const char *const S[12] = { "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink" };
		bool clr = false, major = false, minor = false, lnLst = false; int nln = 0;
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START)
			{
				Buf v;
				if (r.is ("theme") && attr_np (r, "name", v)) scpy (d.theme.name, v.str (), sizeof d.theme.name);
				else if (r.is ("clrScheme")) clr = true;
				else if (clr) { for (int i = 0; i < 12; i++) if (r.is (S[i])) { int a; theme[i] = colour_in (r, &a, theme[i]) & 0xFFFFFF; } }
				if (r.ev == X_START && r.is ("majorFont")) major = true;
				else if (r.ev == X_START && r.is ("minorFont")) minor = true;
				else if (r.ev == X_START && r.is ("latin") && (major || minor) && attr_np (r, "typeface", v) && v.n) scpy (major ? d.theme.major : d.theme.minor, v.str (), 48);
				else if (r.ev == X_START && r.is ("lnStyleLst")) lnLst = true;
				else if (r.ev == X_START && lnLst && r.is ("ln") && nln < 3) lnW[nln++] = (int) attr_l (r, "w", 9525);
			}
			else if (r.ev == X_END)
			{
				if (r.is ("clrScheme")) clr = false;
				else if (r.is ("majorFont")) major = false;
				else if (r.is ("minorFont")) minor = false;
				else if (r.is ("lnStyleLst")) lnLst = false;
			}
		}
		free (x);
		static const int TC[11] = { TC_DK1, TC_LT1, TC_DK2, TC_LT2, TC_ACC1, TC_ACC2, TC_ACC3, TC_ACC4, TC_ACC5, TC_ACC6, TC_LINK };
		for (int i = 0; i < 11; i++) d.theme.col[TC[i]] = theme[i];
	}
	// a text style's levels -> Slides' styles from ts (body: the five levels)
	void text_style (XmlReader &r, int ts, bool body)
	{
		children (r, [&] () -> bool
		{
			int l = -1;
			for (int k = 0; k < 9; k++) { char t[12]; snprintf (t, sizeof t, "lvl%dpPr", k + 1); if (r.is (t)) l = k; }
			if (l < 0 || (!body && l > 0) || l > 4) return false;
			TextStyle &st = d.style[ts + (body ? l : 0)];
			st.cf = cf_inherit (); st.pf = pf_inherit (); st.pf.level = 0;
			int marL = 0, ind = 0; bool hm = false, hi = false;
			unsigned bu = st.bullet;
			ppr (r, st.pf, &bu, &st.cf, &marL, &ind, &hm, &hi);
			st.pf.level = 0;
			st.bullet = bu;
			// the level's step: where its bullet sits / its level (the first: its hang)
			int pos = hmod (marL + ind);
			if (body && l > 0 && pos > 0) st.indent = (short) (pos / l);
			else if (hi) st.indent = (short) imax (0, hmod (-ind));
			else st.indent = (short) (body ? 800 : 0);
			return true;
		});
	}
	static int hmod (long e) { return hmm_of ((double) e); }
	void read_master (const char *path, bool *hfNum, bool *hfFtr, bool *hfDt, bool *hf)
	{
		read_rels (path);
		const char *th = rel_type ("theme"); if (th) { char t[200]; scpy (t, th, sizeof t); read_theme (t); read_rels (path); }
		int len; char *x = entry (path, &len); if (!x) return;
		XmlReader r (x, len);
		Vec<PS *> shapes;
		Fill bg = d.masterBg;
		while (r.next () != X_EOF)
		{
			if (r.ev != X_START) continue;
			if (r.is ("clrMap"))
			{
				Buf v; static const char *const K[4] = { "bg1", "tx1", "bg2", "tx2" };
				for (int i = 0; i < 4; i++) if (attr_np (r, K[i], v)) map[i] = slot_of (v.str ());
				// (the colours read before the map: the master's own background, read again below)
			}
		}
		XmlReader r2 (x, len);
		while (r2.next () != X_EOF)
		{
			if (r2.ev != X_START) continue;
			if (r2.is ("cSld")) c_sld (r2, &bg, shapes, 0);
			else if (r2.is ("hf")) { *hf = true; *hfNum = attr_on (r2, "sldNum", true); *hfFtr = attr_on (r2, "ftr", true); *hfDt = attr_on (r2, "dt", true); }
			else if (r2.is ("titleStyle")) text_style (r2, TS_TITLE, false);
			else if (r2.is ("bodyStyle")) text_style (r2, TS_BODY1, true);
			else if (r2.is ("otherStyle") && own) text_style (r2, TS_OTHER, false);
		}
		free (x);
		d.masterBg = bg;
		for (int i = 0; i < d.decor.n; i++) delete d.decor[i];
		d.decor.clear ();
		for (int i = 0; i < shapes.n; i++)
		{
			PS *s = shapes[i];
			if (s->pt != PT_NONE)
			{
				if (s->pt == PT_FTR && own) { Buf t; s->o->tb.text_utf8 (t); scpy (d.footerText, t.str (), sizeof d.footerText); }
				masterPh.push (s); continue;
			}
			apply_refs (*s);
			if (s->chart[0]) chart (s->chart, *s->o);
			Object *o = s->take (); o->id = d.nextId++; o->ph = PH_NONE; o->tb.ensure ();
			d.decor.push (o); delete s;
		}
	}
	PLayout *read_layout (const char *path)
	{
		PLayout *L = new PLayout; scpy (L->path, path, sizeof L->path); L->type = -1; L->name[0] = 0;
		read_rels (path);
		int len; char *x = entry (path, &len); if (!x) return L;
		XmlReader r (x, len);
		Vec<PS *> shapes;
		while (r.next () != X_EOF)
		{
			if (r.ev != X_START) continue;
			if (r.is ("sldLayout"))
			{
				Buf v; attr_np (r, "type", v);
				const char *t = v.str ();
				L->type = -1;
				for (int i = 0; i < LY_COUNT; i++) if (!strcmp (t, LAYOUT_TYPE[i])) L->type = i;
				if (L->type < 0)
				{
					if (!strcmp (t, "tx") || !strcmp (t, "objTx") || !strcmp (t, "txAndObj") || !strcmp (t, "objAndTx") || !strcmp (t, "chart") || !strcmp (t, "tbl") || !strcmp (t, "dgm")) L->type = LY_CONTENT;
					else if (!strcmp (t, "twoColTx") || !strcmp (t, "twoTxTwoObj")) L->type = !strcmp (t, "twoColTx") ? LY_TWO : LY_COMPARE;
					else if (!strcmp (t, "vertTitleAndTx") || !strcmp (t, "vertTx")) L->type = LY_CONTENT;
				}
			}
			else if (r.is ("cSld")) c_sld (r, 0, shapes, L->name);
		}
		free (x);
		for (int i = 0; i < shapes.n; i++)
		{
			PS *s = shapes[i];
			if (s->pt == PT_NONE) { delete s; continue; }			// (a layout's own decorations: not kept)
			if (!own) inherit_body (*s, master_ph (s->pt));
			L->ph.push (s);
		}
		return L;
	}
	// the layout's placeholders -> Slides' layout l
	void use_layout (PLayout *L, int l)
	{
		Layout &Y = d.layout[l];
		for (int i = 0; i < Y.ph.n; i++) delete Y.ph[i];
		Y.ph.clear ();
		if (L->name[0]) scpy (Y.name, L->name, sizeof Y.name);
		int bodies = 0;
		for (int i = 0; i < L->ph.n; i++)
		{
			PS *s = L->ph[i];
			int k = pt_kind (s->pt);
			if (k == PH_NONE || k == PH_FOOTER || k == PH_NUMBER || k == PH_DATE) continue;
			if (k == PH_BODY && bodies++ > 0) k = PH_BODY2;
			Object *o = obj_copy (s->o); o->id = d.nextId++; o->ph = (signed char) k; o->kind = OB_TEXT; o->img = -1;
			CharFmt f = cf_inherit (); o->tb.set_text ("", f);
			Y.ph.push (o);
		}
	}
	// ---- the effects
	void timing (XmlReader &r, Vec<PFx> &fx)
	{
		// every effect node (a cTn with a preset class), its delay, its target, its longest behaviour
		int depth = 0, effDepth = -1;
		PFx cur; bool wantDelay = false;
		for (;;)
		{
			int t = r.next ();
			if (t == X_EOF) return;
			if (t == X_START)
			{
				depth++;
				Buf v;
				if (r.is ("cTn") && effDepth < 0 && attr_np (r, "presetClass", v))
				{
					const char *c = v.str ();
					cur.cls = !strcmp (c, "entr") ? AC_ENTRANCE : !strcmp (c, "emph") ? AC_EMPHASIS : !strcmp (c, "exit") ? AC_EXIT : -1;
					cur.preset = (int) attr_l (r, "presetID", 0); cur.sub = (int) attr_l (r, "presetSubtype", 0);
					Buf nt; attr_np (r, "nodeType", nt);
					cur.start = !strcmp (nt.str (), "clickEffect") ? ST_CLICK : !strcmp (nt.str (), "afterEffect") ? ST_AFTER : ST_WITH;
					cur.delay = 0; cur.dur = 0; cur.src = 0; cur.para = -1;
					effDepth = depth; wantDelay = true;
				}
				else if (effDepth >= 0)
				{
					if (r.is ("cond") && wantDelay) { cur.delay = (int) attr_l (r, "delay", 0); wantDelay = false; }
					else if (r.is ("cTn"))
					{
						wantDelay = false;
						Buf dv; if (attr_np (r, "dur", dv) && isdigit ((unsigned char) dv.b[0])) { int du = atoi (dv.str ()); if (attr_on (r, "autoRev", false)) du *= 2; if (du > cur.dur) cur.dur = du; }
					}
					else if (r.is ("spTgt") && !cur.src) cur.src = (int) attr_l (r, "spid", 0);
					else if (r.is ("pRg") && cur.para < 0) cur.para = (int) attr_l (r, "st", 0);
					else if (r.is ("childTnLst")) wantDelay = false;
				}
				if (r.empty) { /* its X_END follows */ }
			}
			else if (t == X_END)
			{
				if (effDepth >= 0 && depth == effDepth && r.is ("cTn")) { if (cur.cls >= 0 && cur.src) fx.push (cur); effDepth = -1; }
				depth--;
				if (depth < 0) return;
			}
		}
	}
	static void anim_of (const PFx &e, Anim &a)
	{
		a.cls = (signed char) e.cls; a.start = (signed char) e.start; a.delay = (short) iclamp (e.delay, 0, 30000);
		a.dur = (short) (e.dur > 1 ? iclamp (e.dur, 1, 30000) : 500);
		a.fx = -1;
		for (int i = 0; i < FX_COUNT; i++) if (FX_PRESET[e.cls][i] == e.preset) { a.fx = (signed char) i; break; }
		if (a.fx < 0) a.fx = e.cls == AC_EMPHASIS ? FX_PULSE : FX_FADE;
		a.dir = DIR_NONE;
		for (int i = 0; i < 5; i++) if (DIR_SUB[i] == e.sub) a.dir = (signed char) i;
		if (a.dir == DIR_NONE && e.sub) a.dir = e.sub & 8 ? DIR_LEFT : e.sub & 2 ? DIR_RIGHT : e.sub & 1 ? DIR_UP : DIR_DOWN;
		a.byPara = false;
	}
	void transition (XmlReader &r, Slide &s)
	{
		Buf v;
		s.tr.type = TR_NONE;
		long du = -1;
		if (r.attr ("p14:dur", v)) du = atol (v.str ());
		else if (attr_np (r, "dur", v)) du = atol (v.str ());
		else if (attr_np (r, "spd", v)) du = !strcmp (v.str (), "fast") ? 500 : !strcmp (v.str (), "slow") ? 1500 : 750;
		if (du > 0) s.tr.dur = (short) iclamp ((int) du, 50, 30000);
		long adv = attr_l (r, "advTm", -1); s.tr.after = adv >= 0 ? (int) adv : -1;
		children (r, [&] () -> bool
		{
			Buf dv; attr_np (r, "dir", dv);
			const char *dd = dv.str ();
			signed char dir = !strcmp (dd, "l") ? DIR_RIGHT : !strcmp (dd, "r") ? DIR_LEFT : !strcmp (dd, "d") ? DIR_UP : !strcmp (dd, "u") ? DIR_DOWN : s.tr.dir;
			if (r.is ("fade")) s.tr.type = TR_FADE;
			else if (r.is ("push")) { s.tr.type = TR_PUSH; s.tr.dir = dir; }
			else if (r.is ("wipe")) { s.tr.type = TR_WIPE; s.tr.dir = dir; }
			else if (r.is ("cover")) { s.tr.type = TR_COVER; s.tr.dir = dir; }
			else if (r.is ("pull")) { s.tr.type = TR_UNCOVER; s.tr.dir = dir; }
			else if (r.is ("split")) s.tr.type = TR_SPLIT;
			else if (r.is ("zoom") || r.is ("newsflash")) s.tr.type = TR_ZOOM;
			else if (r.is ("dissolve") || r.is ("randomBar")) s.tr.type = TR_DISSOLVE;
			else if (r.is ("sndAc") || r.is ("extLst")) {}
			else s.tr.type = TR_FADE;
			return false;
		});
	}
	void read_notes (const char *path, Slide &s)
	{
		int len; char *x = entry (path, &len); if (!x) return;
		XmlReader r (x, len);
		Vec<PS *> shapes;
		while (r.next () != X_EOF) if (r.ev == X_START && r.is ("cSld")) c_sld (r, 0, shapes, 0);
		free (x);
		for (int i = 0; i < shapes.n; i++)
		{
			PS *p = shapes[i];
			if ((p->pt == PT_BODY || p->pt == PT_OBJ) && s.notes.empty ())
			{
				Buf t; p->o->tb.text_utf8 (t);
				s.notes.set_text (t.str (), cf_inherit ());
			}
			delete p;
		}
	}
	// a slide part -> the slide; its footers' flags
	Slide *read_slide (const char *path, int *ftr, int *num, int *dt, char *ftrText)
	{
		read_rels (path);
		Slide *s = new Slide;
		s->layout = -1;
		const char *lp = rel_type ("slideLayout");
		PLayout *L = 0;
		for (int i = 0; lp && i < layouts.n; i++) if (!strcmp (layouts[i]->path, lp)) { L = layouts[i]; if (L->type >= 0) s->layout = L->type; }
		char notes[200] = ""; { const char *n = rel_type ("notesSlide"); if (n) scpy (notes, n, sizeof notes); }
		int len; char *x = entry (path, &len); if (!x) return s;
		XmlReader r (x, len);
		Vec<PS *> shapes; Vec<PFx> fx;
		bool trDone = false;
		while (r.next () != X_EOF)
		{
			if (r.ev != X_START) continue;
			if (r.is ("sld")) { s->hidden = !attr_on (r, "show", true); s->masterObjects = attr_on (r, "showMasterSp", true); }
			else if (r.is ("cSld")) c_sld (r, &s->bg, shapes, 0);
			else if (r.is ("Choice")) { Buf q; if (!attr_np (r, "Requires", q) || strcmp (q.str (), "p14")) r.skip (); }	// (p14: PowerPoint 2010's transitions, their durations: read)
			else if (r.is ("transition") && !trDone) { transition (r, *s); trDone = true; }
			else if (r.is ("timing")) timing (r, fx);
		}
		free (x);
		// the objects
		int srcOf[1024]; int ids[1024]; int nmap = 0;
		for (int i = 0; i < shapes.n; i++)
		{
			PS *p = shapes[i];
			if (p->pt == PT_FTR || p->pt == PT_SLDNUM || p->pt == PT_DT)
			{
				if (p->pt == PT_FTR) { Buf t; p->o->tb.text_utf8 (t); (*ftr)++; if (t.n && !ftrText[0]) scpy (ftrText, t.str (), 96); }
				else if (p->pt == PT_SLDNUM) (*num)++; else (*dt)++;
				delete p; continue;
			}
			if (p->pt != PT_NONE)
			{
				PS *lph = layout_ph (L, *p);
				int k = lph ? pt_kind (lph->pt) : pt_kind (p->pt);
				if (lph && k == PH_BODY) { int nb = 0; for (int j = 0; j < L->ph.n && L->ph[j] != lph; j++) if (pt_kind (L->ph[j]->pt) == PH_BODY) nb++; if (nb) k = PH_BODY2; }
				if (k == PH_PICTURE && p->o->kind != OB_PICTURE) k = p->o->kind == OB_TEXT ? PH_PICTURE : PH_NONE;
				if (p->o->kind == OB_TABLE || p->o->kind == OB_CHART || (p->o->kind == OB_PICTURE && k != PH_PICTURE)) k = PH_NONE;
				p->o->ph = (signed char) k;
				inherit_body (*p, lph);
				if (!own) { if (lph) bake (p->o->tb, lph); PS *m = master_ph (p->pt); inherit_body (*p, m); bake (p->o->tb, m); }
			}
			apply_refs (*p);
			if (p->chart[0]) chart (p->chart, *p->o);
			Object *o = p->take ();
			o->id = d.nextId++;
			if (o->kind == OB_SHAPE && o->ph != PH_NONE) o->kind = OB_TEXT;
			o->tb.ensure ();
			if (nmap < 1024) { srcOf[nmap] = p->src; ids[nmap] = o->id; nmap++; }
			s->obj.push (o);
			delete p;
		}
		// the effects (a text's paragraphs one after the other: by paragraph)
		for (int i = 0; i < fx.n; i++)
		{
			PFx &e = fx[i];
			int id = -1; for (int k = 0; k < nmap; k++) if (srcOf[k] == e.src) id = ids[k];
			if (id < 0) continue;
			if (e.para >= 0 && s->anim.n)
			{
				Anim &pv = s->anim[s->anim.n - 1];
				if (pv.obj == id && pv.byPara && pv.cls == e.cls) continue;
			}
			Anim a; a.obj = id; anim_of (e, a);
			if (e.para >= 0)
			{
				int more = 0; for (int k = i + 1; k < fx.n && fx[k].src == e.src && fx[k].para >= 0 && fx[k].cls == e.cls; k++) more++;
				a.byPara = more > 0;
			}
			s->anim.push (a);
		}
		if (notes[0]) read_notes (notes, *s);
		return s;
	}
};

// A .pptx's bytes -> the deck (false: not one)
static bool pptx_load (Deck &d, const unsigned char *b, unsigned n)
{
	pngsave::ZipEntry e;
	if (!pngsave::zip_find (b, n, "[Content_Types].xml", &e)) return false;
	PptxIn in (d, b, n);
	// the presentation's part
	char pres[200] = "ppt/presentation.xml";
	in.read_rels ("");
	{ const char *t = in.rel_type ("officeDocument"); if (t) scpy (pres, t, sizeof pres); }
	int len;
	char *app = in.entry ("docProps/app.xml", &len);
	if (app) { in.own = strstr (app, "<Application>Onyx Slides") != 0; free (app); }
	char *px = in.entry (pres, &len);
	if (!px) return false;
	d.clear ();
	d.nextId = 1;
	long cx = 0, cy = 0;
	struct SId { int id; char rid[24]; };
	Vec<SId> sids; char masterRid[24] = "";
	struct Sec { char name[48]; int first; };
	Vec<Sec> secs;
	PS other;
	bool otherSet = false;
	{
		XmlReader r (px, len);
		Sec cur; bool inSec = false;
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START)
			{
				if (r.is ("sldSz")) { cx = attr_l (r, "cx", 0); cy = attr_l (r, "cy", 0); }
				else if (r.is ("sldMasterId") && !masterRid[0]) { Buf v; if (r.attr ("r:id", v)) scpy (masterRid, v.str (), sizeof masterRid); }
				else if (r.isq ("p:sldId") || (r.is ("sldId") && !inSec)) { SId s; s.id = (int) attr_l (r, "id", 0); Buf v; s.rid[0] = 0; if (r.attr ("r:id", v)) scpy (s.rid, v.str (), sizeof s.rid); sids.push (s); }
				else if (r.is ("section")) { inSec = true; Buf v; cur.name[0] = 0; if (attr_np (r, "name", v)) scpy (cur.name, v.str (), sizeof cur.name); cur.first = -1; }
				else if (inSec && r.is ("sldId") && cur.first < 0) cur.first = (int) attr_l (r, "id", 0);
				else if (r.is ("defaultTextStyle") && !in.own)
				{
					children (r, [&] () -> bool
					{
						if (!r.is ("lvl1pPr")) return false;
						otherSet = true;
						in.ppr (r, other.lpf[0], &other.lbu[0], &other.lcf[0], 0, 0, 0, 0);
						return true;
					});
				}
			}
			else if (r.ev == X_END && r.is ("section") && inSec) { inSec = false; secs.push (cur); }
		}
		free (px);
	}
	if (cx > 0 && cy > 0) { d.sw = hmm_of (cx); d.sh = hmm_of (cy); }
	master_default (d, 0);
	in.read_rels (pres);
	Vec<PRel> prels; for (int i = 0; i < in.rels.n; i++) prels.push (in.rels[i]);
	const char *mp = 0; for (int i = 0; i < prels.n; i++) if (!strcmp (prels[i].id, masterRid)) mp = prels[i].target;
	if (!mp) for (int i = 0; i < prels.n; i++) if (!strcmp (prels[i].type, "slideMaster")) { mp = prels[i].target; break; }
	bool hf = false, hfNum = true, hfFtr = true, hfDt = true;
	if (mp)
	{
		char master[200]; scpy (master, mp, sizeof master);
		in.read_master (master, &hfNum, &hfFtr, &hfDt, &hf);
		// the "other" text, the subtitle (the others: the presentation's default text, the body's first level without its bullet)
		if (!in.own)
		{
			TextStyle &o = d.style[TS_OTHER];
			o.cf = cf_inherit (); o.pf = pf_inherit (); o.pf.bullet = BU_NONE; o.indent = 0;
			if (otherSet) { o.cf = other.lcf[0]; o.pf = other.lpf[0]; o.pf.level = 0; if (o.pf.bullet == BU_INHERIT) o.pf.bullet = BU_NONE; }
			if (o.cf.font == FONT_INHERIT) o.cf.font = FONT_MINOR;
			if (!o.cf.size) o.cf.size = 180;
			if (o.cf.color == AUTO) o.cf.color = THEME | TC_DK1;
			TextStyle &s = d.style[TS_SUBTITLE];
			s = d.style[TS_BODY1]; s.pf.bullet = BU_NONE; s.indent = 0;
		}
		// the layouts: every master's (LibreOffice writes a master for each layout)
		Vec<PRel> mrels;
		in.read_rels (master);
		for (int i = 0; i < in.rels.n; i++) mrels.push (in.rels[i]);
		for (int m = 0; m < prels.n; m++)
		{
			if (strcmp (prels[m].type, "slideMaster") || !strcmp (prels[m].target, master)) continue;
			char other[200]; scpy (other, prels[m].target, sizeof other);
			in.read_rels (other);
			for (int i = 0; i < in.rels.n; i++) mrels.push (in.rels[i]);
		}
		bool used[LY_COUNT] = { false };
		for (int i = 0; i < mrels.n; i++)
		{
			if (strcmp (mrels[i].type, "slideLayout")) continue;
			PLayout *L = in.read_layout (mrels[i].target);
			in.layouts.push (L);
			if (L->type >= 0 && !used[L->type])
			{
				used[L->type] = true; in.use_layout (L, L->type);
				if (in.own && L->type == LY_TITLE)
					for (int k = 0; k < L->ph.n; k++)
						if (L->ph[k]->pt == PT_SUBTITLE && L->ph[k]->lst[0])
						{ TextStyle &s = d.style[TS_SUBTITLE]; s.cf = L->ph[k]->lcf[0]; s.pf = L->ph[k]->lpf[0]; s.pf.level = 0; if (L->ph[k]->lbu[0]) s.bullet = L->ph[k]->lbu[0]; s.indent = 0; }
			}
		}
	}
	// the slides
	int ftr = 0, num = 0, dt = 0; char ftrText[96] = "";
	for (int i = 0; i < sids.n; i++)
	{
		const char *sp = 0; for (int k = 0; k < prels.n; k++) if (!strcmp (prels[k].id, sids[i].rid)) sp = prels[k].target;
		if (!sp) continue;
		char path[200]; scpy (path, sp, sizeof path);
		Slide *s = in.read_slide (path, &ftr, &num, &dt, ftrText);
		for (int k = 0; k < secs.n; k++) if (secs[k].first == sids[i].id) scpy (s->section, secs[k].name, sizeof s->section);
		d.slides.push (s);
	}
	// the footers: the master's word (Slides' files), else the slides'
	if (in.own && hf) { d.number = hfNum; d.footer = hfFtr; d.date = hfDt; }
	else
	{
		d.footer = ftr > 0; d.number = num > 0; d.date = dt > 0;
		if (ftrText[0]) scpy (d.footerText, ftrText, sizeof d.footerText);
	}
	for (int i = 0; i < d.slides.n; i++)
	{
		Slide *s = d.slides[i];
		if (s->layout < 0 || s->layout >= LY_COUNT) s->layout = OdpIn::guess_layout (*s);
		s->notes.ensure ();
		for (int k = 0; k < s->obj.n; k++) { s->obj[k]->tb.ensure (); if (s->obj[k]->tbl && !in.own) table_fit (d, *s->obj[k]); }
	}
	if (!d.slides.n) d.slides.push (slide_new (d, LY_TITLE));
	return true;
}

// .odp or .pptx: by what the archive holds
static bool deck_load (Deck &d, const unsigned char *b, unsigned n)
{
	pngsave::ZipEntry e;
	if (pngsave::zip_find (b, n, "ppt/presentation.xml", &e) || pngsave::zip_find (b, n, "[Content_Types].xml", &e)) return pptx_load (d, b, n);
	return odp_load (d, b, n);
}

} // namespace sl

#endif
