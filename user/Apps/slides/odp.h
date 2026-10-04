//
// odp.h -- OpenDocument presentations (.odp), Slides' own format: read and written.
//
// Written as LibreOffice Impress reads it: the pages (draw:page) with their objects -- draw:frame (a text box,
// a placeholder: presentation:class title / subtitle / outline; a picture: draw:image in Pictures/; a table:
// table:table), draw:custom-shape (its draw:enhanced-geometry's draw:type), draw:line --, their styles
// (automatic: graphic for the fill and line, paragraph and text for the text, drawing-page for the background
// and the transition: smil:type), the speaker's notes (presentation:notes), the page's size (styles.xml's
// page layout), the master page and its objects. What Impress has no words for -- the theme, the master's
// text styles and layouts, a format left to the master (inherit), the effects, a chart's data -- goes into
// attributes of Onyx's own namespace (onyx:*, ignored by the others) and into onyx.xml in the archive: a
// file Slides writes is read back exactly. A chart is written as its picture too (what the others show).
//
// Read: Slides' own files, and Impress's (the pages, the frames, the shapes, the pictures, the lines, the
// tables, the text and its formats -- styles followed through their parents --, the notes, the backgrounds,
// the transitions' kinds); a slide's layout guessed from its placeholders.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_odp_h
#define _slides_odp_h

#include "editor.h"
#include "Apps/sheet/xml.h"

namespace sl {

using ss::XmlReader; using ss::X_START; using ss::X_END; using ss::X_TEXT; using ss::X_EOF; using ss::xml_esc;

// ---- small writers ---------------------------------------------------------------------------------------------------
static void put_cm (Buf &o, double hmm) { char t[32]; snprintf (t, sizeof t, "%.3fcm", hmm / 1000.0); o.puts (t); }
static void put_hex (Buf &o, unsigned c) { char t[16]; snprintf (t, sizeof t, "#%06x", c & 0xFFFFFF); o.puts (t); }
static void attr (Buf &o, const char *name, const char *v) { o.put (' '); o.puts (name); o.puts ("=\""); xml_esc (o, v); o.put ('"'); }
static void attri (Buf &o, const char *name, long v) { char t[24]; snprintf (t, sizeof t, "%ld", v); attr (o, name, t); }
static void attrcm (Buf &o, const char *name, double hmm) { char t[32]; snprintf (t, sizeof t, "%.3fcm", hmm / 1000.0); attr (o, name, t); }
static void colour_code (char *t, int cap, unsigned c)
{
	if (c == AUTO) snprintf (t, cap, "a");
	else if (is_theme (c)) snprintf (t, cap, "t%u", c & 0xFF);
	else snprintf (t, cap, "%06x", c & 0xFFFFFF);
}
static unsigned colour_parse (const char *s)
{
	if (!s || !*s || *s == 'a') return AUTO;
	if (*s == 't') return THEME | (unsigned) atoi (s + 1);
	if (*s == '#') s++;
	return (unsigned) strtoul (s, 0, 16) & 0xFFFFFF;
}
static unsigned colour_of (const char *s, unsigned def)		// ODF's "#rrggbb"
{
	if (!s || s[0] != '#' || strlen (s) < 7) return def;
	return (unsigned) strtoul (s + 1, 0, 16) & 0xFFFFFF;
}
// A length of ODF ("2.54cm", "1in", "12pt", "10mm", "0.5") -> hmm
static double len_hmm (const char *s)
{
	if (!s || !*s) return 0;
	char *e; double v = strtod (s, &e);
	if (!strncmp (e, "cm", 2)) return v * 1000;
	if (!strncmp (e, "mm", 2)) return v * 100;
	if (!strncmp (e, "in", 2)) return v * 2540;
	if (!strncmp (e, "pt", 2)) return v * 35.277778;
	if (!strncmp (e, "pc", 2)) return v * 423.33;
	if (!strncmp (e, "px", 2)) return v * 26.458;
	return v * 1000;
}

static void cf_code (Buf &o, const Deck &d, const CharFmt &f)
{
	char c[16]; colour_code (c, sizeof c, f.color);
	char t[160];
	const char *font = f.font == FONT_INHERIT ? "-" : f.font == FONT_MAJOR ? "+major" : f.font == FONT_MINOR ? "+minor" : d.font_name (f.font);
	snprintf (t, sizeof t, "%s|%d|%s|%u|%u", font, f.size, c, f.flags, f.set);
	o.puts (t);
}
static CharFmt cf_parse (Deck &d, const char *s)
{
	CharFmt f = cf_inherit ();
	char a[5][64]; int n = 0, k = 0; a[0][0] = 0;
	for (const char *p = s; *p && n < 5; p++)
	{
		if (*p == '|') { a[n][k] = 0; n++; k = 0; if (n < 5) a[n][0] = 0; continue; }
		if (k < 63) a[n][k++] = *p;
	}
	if (n < 5) a[n][k] = 0;
	if (n < 4) return f;
	f.font = !strcmp (a[0], "-") ? FONT_INHERIT : !strcmp (a[0], "+major") ? FONT_MAJOR : !strcmp (a[0], "+minor") ? FONT_MINOR : (short) d.font_index (a[0]);
	f.size = (short) atoi (a[1]); f.color = colour_parse (a[2]); f.flags = (unsigned short) atoi (a[3]); f.set = (unsigned short) atoi (a[4]);
	return f;
}
static void pf_code (Buf &o, const ParaFmt &p) { char t[80]; snprintf (t, sizeof t, "%d|%d|%d|%d|%d|%d", p.align, p.level, p.bullet, p.before, p.after, p.spacing); o.puts (t); }
static ParaFmt pf_parse (const char *s)
{
	ParaFmt p = pf_inherit ();
	int v[6] = { -1, 0, -1, -1, -1, 0 };
	sscanf (s, "%d|%d|%d|%d|%d|%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
	p.align = (signed char) v[0]; p.level = (signed char) v[1]; p.bullet = (signed char) v[2]; p.before = (short) v[3]; p.after = (short) v[4]; p.spacing = (short) v[5];
	return p;
}
static void fill_code (Buf &o, const Fill &f) { char a[16], b[16]; colour_code (a, 16, f.c1); colour_code (b, 16, f.c2); char t[80]; snprintf (t, sizeof t, "%d|%s|%s|%d|%d", f.type, a, b, f.angle, f.alpha); o.puts (t); }
static Fill fill_parse (const char *s)
{
	Fill f = fill_none ();
	char a[16] = "", b[16] = ""; int type = 0, ang = 90, al = 255;
	sscanf (s, "%d|%15[^|]|%15[^|]|%d|%d", &type, a, b, &ang, &al);
	f.type = (signed char) type; f.c1 = colour_parse (a); f.c2 = colour_parse (b); f.angle = (short) ang; f.alpha = (unsigned char) al;
	return f;
}
static void line_code (Buf &o, const Line &l) { char c[16]; colour_code (c, 16, l.color); char t[80]; snprintf (t, sizeof t, "%d|%d|%d|%s|%d", l.type, l.head0, l.head1, c, l.width); o.puts (t); }
static Line line_parse (const char *s)
{
	Line l = line_none (); char c[16] = ""; int a = 0, b = 0, h = 0, w = 26;
	sscanf (s, "%d|%d|%d|%15[^|]|%d", &a, &b, &h, c, &w);
	l.type = (signed char) a; l.head0 = (signed char) b; l.head1 = (signed char) h; l.color = colour_parse (c); l.width = (short) w;
	return l;
}

// ---- writing -------------------------------------------------------------------------------------------------------
static const char *const DRAW_TYPES[SH_COUNT] = { "rectangle", "round-rectangle", "ellipse", "isosceles-triangle", "right-triangle", "diamond",
	"pentagon", "hexagon", "octagon", "star5", "star4", "heart", "right-arrow", "left-arrow", "up-arrow", "down-arrow", "left-right-arrow",
	"chevron", "pentagon-right", "round-rectangular-callout", "cloud", "cross", "parallelogram", "trapezoid", "can", "ring", "flowchart-document",
	"flowchart-terminator" };
static const char *const PH_CLASS[PH_COUNT] = { "", "title", "subtitle", "outline", "outline", "graphic", "footer", "page-number", "date-time" };

struct OdpOut
{
	Deck &d;
	Buf autoStyles;				// content.xml's automatic styles
	int ngr, nP, nT, ndp;
	Vec<int> pics;				// the pictures written (Pictures/)
	pngsave::ZipOut *zip;
	int nextChart;
	Buf grads; int ngrad;			// the gradients (draw:gradient, styles.xml's office:styles)
	bool hdr;				// a table's heading row being written: its text white and bold (as Slides draws it)
	OdpOut (Deck &d_) : d (d_), ngr (0), nP (0), nT (0), ndp (0), zip (0), nextChart (1), ngrad (0), hdr (false) {}
	// A gradient's name (its definition kept for styles.xml); ODF's angle: tenths of a degree, 0 = top to bottom, counter-clockwise
	void gradient (const Fill &f, char *name)
	{
		snprintf (name, 16, "Gr%d", ++ngrad);
		grads.puts ("<draw:gradient"); attr (grads, "draw:name", name); grads.puts (" draw:style=\"linear\" draw:start-color=\""); put_hex (grads, col (f.c1));
		grads.puts ("\" draw:end-color=\""); put_hex (grads, col (f.c2)); grads.puts ("\" draw:start-intensity=\"100%\" draw:end-intensity=\"100%\" draw:border=\"0%\"");
		attri (grads, "draw:angle", (((90 - f.angle) * 10) % 3600 + 3600) % 3600); grads.puts ("/>");
	}

	unsigned col (unsigned c) { return d.rgb (c); }
	// A graphic style for an object's fill, line, text box -> its name
	void graphic_style (Object &o, char *name)
	{
		snprintf (name, 16, "gr%d", ++ngr);
		Buf &s = autoStyles;
		s.puts ("<style:style style:family=\"graphic\""); attr (s, "style:name", name); s.puts ("><style:graphic-properties");
		if (o.fill.type == FILL_SOLID) { s.puts (" draw:fill=\"solid\" draw:fill-color=\""); put_hex (s, col (o.fill.c1)); s.puts ("\""); }
		else if (o.fill.type == FILL_GRADIENT) { char g[16]; gradient (o.fill, g); s.puts (" draw:fill=\"gradient\""); attr (s, "draw:fill-gradient-name", g); }
		else s.puts (" draw:fill=\"none\"");
		if (o.fill.type != FILL_NONE && o.fill.alpha < 255) { char t[32]; snprintf (t, sizeof t, " draw:opacity=\"%d%%\"", o.fill.alpha * 100 / 255); s.puts (t); }
		if (o.line.type == LN_NONE) s.puts (" draw:stroke=\"none\"");
		else
		{
			s.puts (o.line.type == LN_SOLID ? " draw:stroke=\"solid\"" : " draw:stroke=\"dash\"");
			s.puts (" svg:stroke-color=\""); put_hex (s, col (o.line.color)); s.puts ("\"");
			attrcm (s, "svg:stroke-width", o.line.width);
			if (o.line.head1) s.puts (" draw:marker-end=\"Arrow\" draw:marker-end-width=\"0.3cm\"");
			if (o.line.head0) s.puts (" draw:marker-start=\"Arrow\" draw:marker-start-width=\"0.3cm\"");
		}
		s.puts (o.tb.anchor == AN_MIDDLE ? " draw:textarea-vertical-align=\"middle\"" : o.tb.anchor == AN_BOTTOM ? " draw:textarea-vertical-align=\"bottom\"" : " draw:textarea-vertical-align=\"top\"");
		attrcm (s, "fo:padding-left", o.tb.inset[0]); attrcm (s, "fo:padding-top", o.tb.inset[1]); attrcm (s, "fo:padding-right", o.tb.inset[2]); attrcm (s, "fo:padding-bottom", o.tb.inset[3]);
		if (o.tb.fit == FIT_SHRINK) s.puts (" draw:fit-to-size=\"shrink-to-fit\" style:shrink-to-fit=\"true\"");
		if (o.tb.fit == FIT_GROW) s.puts (" draw:auto-grow-height=\"true\""); else s.puts (" draw:auto-grow-height=\"false\"");
		if (!o.tb.wrap) s.puts (" fo:wrap-option=\"no-wrap\"");
		if (o.shadow) s.puts (" draw:shadow=\"visible\" draw:shadow-offset-x=\"0.1cm\" draw:shadow-offset-y=\"0.15cm\" draw:shadow-color=\"#000000\" draw:shadow-opacity=\"35%\"");
		s.puts ("/></style:style>");
	}
	void para_style (const ParaFmt &pf, int ph, char *name)
	{
		snprintf (name, 16, "P%d", ++nP);
		ParaFmt r = pf_resolve (d, ph, pf);
		Buf &s = autoStyles;
		s.puts ("<style:style style:family=\"paragraph\""); attr (s, "style:name", name); s.puts ("><style:paragraph-properties");
		static const char *AL[4] = { "start", "center", "end", "justify" };
		attr (s, "fo:text-align", AL[iclamp (r.align, 0, 3)]);
		attrcm (s, "fo:margin-top", r.before * 3.5277778); attrcm (s, "fo:margin-bottom", r.after * 3.5277778);
		char t[24]; snprintf (t, sizeof t, "%d%%", r.spacing); attr (s, "fo:line-height", t);
		s.puts ("/></style:style>");
	}
	void text_style (const CharFmt &f, int ph, int lvl, char *name)
	{
		snprintf (name, 16, "T%d", ++nT);
		CharFmt r = cf_resolve (d, ph, lvl, f);
		if (hdr) { if (f.color == AUTO) r.color = THEME | TC_LT1; if (!(f.set & CF_BOLD)) r.flags |= CF_BOLD; }
		Buf &s = autoStyles;
		s.puts ("<style:style style:family=\"text\""); attr (s, "style:name", name); s.puts ("><style:text-properties");
		char t[32]; snprintf (t, sizeof t, "%.1fpt", r.size / 10.0); attr (s, "fo:font-size", t);
		attr (s, "fo:font-family", d.font_name (r.font));
		s.puts (" fo:color=\""); put_hex (s, col (r.color)); s.puts ("\"");
		if (r.flags & CF_BOLD) s.puts (" fo:font-weight=\"bold\""); else s.puts (" fo:font-weight=\"normal\"");
		if (r.flags & CF_ITALIC) s.puts (" fo:font-style=\"italic\""); else s.puts (" fo:font-style=\"normal\"");
		if (r.flags & CF_UNDER) s.puts (" style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"");
		if (r.flags & CF_STRIKE) s.puts (" style:text-line-through-style=\"solid\"");
		if (r.flags & CF_SUPER) s.puts (" style:text-position=\"super 58%\"");
		if (r.flags & CF_SUB) s.puts (" style:text-position=\"sub 58%\"");
		s.puts ("/></style:style>");
	}
	// A paragraph's characters, in spans of one format
	void runs (Buf &o, const Para *q, int ph)
	{
		int i = 0;
		while (i < q->len)
		{
			int j = i + 1; while (j < q->len && cf_same (q->cf[j], q->cf[i])) j++;
			char ts[16]; text_style (q->cf[i], ph, q->pf.level, ts);
			o.puts ("<text:span"); attr (o, "text:style-name", ts); o.puts (" onyx:cf=\""); cf_code (o, d, q->cf[i]); o.puts ("\">");
			int sp = 0;
			for (int k = i; k < j; k++)
			{
				unsigned c = q->ch[k];
				if (c == ' ') { if (sp || k == i || k + 1 == j) { o.puts ("<text:s/>"); } else o.put (' '); sp = 1; continue; }
				sp = 0;
				if (c == '\t') o.puts ("<text:tab/>");
				else if (c == '\n' || c == 0x0B) o.puts ("<text:line-break/>");
				else if (c == '&') o.puts ("&amp;"); else if (c == '<') o.puts ("&lt;"); else if (c == '>') o.puts ("&gt;");
				else if (c >= 32) o.putu (c);
			}
			o.puts ("</text:span>");
			i = j;
		}
	}
	void para (Buf &o, const Para *q, int ph)
	{
		char ps[16]; para_style (q->pf, ph, ps);
		o.puts ("<text:p"); attr (o, "text:style-name", ps); o.puts (" onyx:pf=\""); pf_code (o, q->pf); o.puts ("\" onyx:end=\""); cf_code (o, d, q->end); o.puts ("\">");
		runs (o, q, ph);
		o.puts ("</text:p>");
	}
	// A text body: its paragraphs (in a list where they have a bullet: the level as the list's depth)
	void text (Buf &o, const TextBody &tb, int ph)
	{
		for (int i = 0; i < tb.p.n; i++)
		{
			const Para *q = tb.p[i];
			ParaFmt r = pf_resolve (d, ph, q->pf);
			if (r.bullet != BU_NONE)
			{
				int lvl = iclamp (q->pf.level, 0, 4);
				o.puts (r.bullet == BU_NUMBER ? "<text:list text:style-name=\"LN\">" : "<text:list text:style-name=\"LB\">");
				for (int k = 0; k < lvl; k++) o.puts ("<text:list-item><text:list>");
				o.puts ("<text:list-item>"); para (o, q, ph); o.puts ("</text:list-item>");
				for (int k = 0; k < lvl; k++) o.puts ("</text:list></text:list-item>");
				o.puts ("</text:list>");
			}
			else para (o, q, ph);
		}
	}
	void geometry (Buf &o, Object &ob)
	{
		if (ob.rot)
		{
			// ODF's rotate turns about the top-left (counter-clockwise, radians), then translates
			double a = -ob.rot * 3.14159265358979 / 180;
			double cx = ob.x + ob.w / 2.0, cy = ob.y + ob.h / 2.0;
			double ca = cos (a), sa = sin (a);
			double tx = cx - (ob.w / 2.0 * ca + ob.h / 2.0 * sa), ty = cy - (-ob.w / 2.0 * sa + ob.h / 2.0 * ca);
			char t[160]; snprintf (t, sizeof t, "rotate (%.6f) translate (%.3fcm %.3fcm)", a, tx / 1000, ty / 1000);
			attrcm (o, "svg:width", ob.w); attrcm (o, "svg:height", ob.h); attr (o, "draw:transform", t);
		}
		else { attrcm (o, "svg:x", ob.x); attrcm (o, "svg:y", ob.y); attrcm (o, "svg:width", ob.w); attrcm (o, "svg:height", ob.h); }
	}
	void onyx_attrs (Buf &o, Object &ob)
	{
		attri (o, "onyx:id", ob.id); attri (o, "onyx:kind", ob.kind); attri (o, "onyx:shape", ob.shape); attri (o, "onyx:ph", ob.ph);
		attri (o, "onyx:rot", ob.rot); attri (o, "onyx:radius", ob.radius);
		o.puts (" onyx:fill=\""); fill_code (o, ob.fill); o.puts ("\" onyx:line=\""); line_code (o, ob.line); o.puts ("\"");
		char t[240]; snprintf (t, sizeof t, "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d", ob.x, ob.y, ob.w, ob.h, ob.flipH, ob.flipV, ob.shadow, ob.tb.anchor, ob.tb.fit, ob.tb.wrap, ob.tb.inset[0], ob.tb.inset[1], ob.tb.inset[2], ob.tb.inset[3]);
		attr (o, "onyx:box", t);
		if (ob.name[0]) attr (o, "draw:name", ob.name);
		if (ob.kind == OB_PICTURE) { snprintf (t, sizeof t, "%d|%d|%d|%d", ob.crop[0], ob.crop[1], ob.crop[2], ob.crop[3]); attr (o, "onyx:crop", t); }
	}
	const char *pic_path (int k, char *out)
	{
		Picture &p = g_pics[k];
		const char *ext = strrchr (p.name, '.');
		snprintf (out, 80, "Pictures/p%d%s", k, ext ? ext : ".png");
		if (pics.find (k) < 0) { pics.push (k); zip->add (out, p.bytes, p.len, false); }
		return out;
	}
	void object (Buf &o, Object &ob, bool master = false)
	{
		char gs[16]; graphic_style (ob, gs);
		char id[24]; snprintf (id, sizeof id, "o%d", ob.id);
		switch (ob.kind)
		{
		case OB_LINE:
		{
			double x1 = ob.flipH ? ob.x + ob.w : ob.x, x2 = ob.flipH ? ob.x : ob.x + ob.w, y1 = ob.flipV ? ob.y + ob.h : ob.y, y2 = ob.flipV ? ob.y : ob.y + ob.h;
			o.puts ("<draw:line"); attr (o, "draw:style-name", gs); attr (o, "xml:id", id); attr (o, "draw:id", id);
			attrcm (o, "svg:x1", x1); attrcm (o, "svg:y1", y1); attrcm (o, "svg:x2", x2); attrcm (o, "svg:y2", y2);
			onyx_attrs (o, ob); o.puts ("><text:p/></draw:line>");
			break;
		}
		case OB_SHAPE:
			o.puts ("<draw:custom-shape"); attr (o, "draw:style-name", gs); attr (o, "xml:id", id); attr (o, "draw:id", id); geometry (o, ob); onyx_attrs (o, ob); o.put ('>');
			text (o, ob.tb, PH_NONE);
			o.puts ("<draw:enhanced-geometry svg:viewBox=\"0 0 21600 21600\""); attr (o, "draw:type", DRAW_TYPES[(int) ob.shape]);
			if (ob.flipH) o.puts (" draw:mirror-horizontal=\"true\""); if (ob.flipV) o.puts (" draw:mirror-vertical=\"true\"");
			o.puts ("/></draw:custom-shape>");
			break;
		case OB_PICTURE:
		{
			o.puts ("<draw:frame"); attr (o, "draw:style-name", gs); attr (o, "xml:id", id); attr (o, "draw:id", id); geometry (o, ob); onyx_attrs (o, ob);
			if (ob.ph == PH_PICTURE) o.puts (" presentation:class=\"graphic\"");
			o.put ('>');
			if (ob.img >= 0 && ob.img < g_pics.n) { char p[80]; pic_path (ob.img, p); o.puts ("<draw:image"); attr (o, "xlink:href", p); o.puts (" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"><text:p/></draw:image>"); }
			o.puts ("</draw:frame>");
			break;
		}
		case OB_TABLE:
		{
			Table &t = *ob.tbl;
			o.puts ("<draw:frame"); attr (o, "draw:style-name", gs); attr (o, "xml:id", id); attr (o, "draw:id", id); geometry (o, ob); onyx_attrs (o, ob);
			char tt[32]; snprintf (tt, sizeof tt, "%d|%d", t.header, t.banded); attr (o, "onyx:table", tt);
			o.puts ("><table:table");
			if (t.header) o.puts (" table:use-first-row-styles=\"true\""); if (t.banded) o.puts (" table:use-banding-rows-styles=\"true\"");
			o.put ('>');
			for (int c = 0; c < t.cols; c++) { o.puts ("<table:table-column"); char cs[16]; snprintf (cs, sizeof cs, "co%d_%d", ob.id, c); attr (o, "table:style-name", cs); attri (o, "onyx:w", t.colW[c]); o.puts ("/>");
				autoStyles.puts ("<style:style style:family=\"table-column\""); attr (autoStyles, "style:name", cs); autoStyles.puts ("><style:table-column-properties"); attrcm (autoStyles, "style:column-width", t.colW[c]); autoStyles.puts ("/></style:style>"); }
			for (int r = 0; r < t.rows; r++)
			{
				o.puts ("<table:table-row"); attri (o, "onyx:h", t.rowH[r]); o.put ('>');
				for (int c = 0; c < t.cols; c++)
				{
					o.puts ("<table:table-cell office:value-type=\"string\"");
					const Fill &cf = t.cfill[r * t.cols + c];
					{
						unsigned bg = cf.type == FILL_SOLID ? col (cf.c1) : cf.type == FILL_NONE ? 0xFFFFFF : t.header && r == 0 ? col (THEME | TC_ACC1) : t.banded && (r & 1) == (t.header ? 0 : 1) ? col (THEME | TC_LT2) : 0xFFFFFF;
						char cs[24]; snprintf (cs, sizeof cs, "ce%06x", bg);
						if (!strstr (autoStyles.str (), cs)) { autoStyles.puts ("<style:style style:family=\"table-cell\""); attr (autoStyles, "style:name", cs); autoStyles.puts ("><style:graphic-properties draw:fill=\"solid\" draw:fill-color=\""); put_hex (autoStyles, bg); autoStyles.puts ("\"/></style:style>"); }
						attr (o, "table:style-name", cs);
					}
					if (cf.type != FILL_INHERIT) { o.puts (" onyx:fill=\""); fill_code (o, cf); o.puts ("\""); }
					o.put ('>'); hdr = t.header && r == 0; text (o, t.at (r, c), PH_NONE); hdr = false; o.puts ("</table:table-cell>");
				}
				o.puts ("</table:table-row>");
			}
			o.puts ("</table:table></draw:frame>");
			break;
		}
		case OB_CHART:
		{
			Chart &c = *ob.chart;
			o.puts ("<draw:frame"); attr (o, "draw:style-name", gs); attr (o, "xml:id", id); attr (o, "draw:id", id); geometry (o, ob); onyx_attrs (o, ob);
			Buf cd; char t[64];
			snprintf (t, sizeof t, "%d|%d|%d|%d|%d|", c.type, c.ncat, c.nser, c.legend, c.labels); cd.puts (t); cd.puts (c.title);
			attr (o, "onyx:chart", cd.str ());
			o.put ('>');
			// its data as onyx elements, its picture for the others
			o.puts ("<onyx:data>");
			for (int i = 0; i < c.ncat; i++) { o.puts ("<onyx:cat"); attr (o, "v", c.cat[i]); o.puts ("/>"); }
			for (int k = 0; k < c.nser; k++)
			{
				o.puts ("<onyx:ser"); attr (o, "name", c.ser[k]);
				Buf v; for (int i = 0; i < c.ncat; i++) { char n[32]; snprintf (n, sizeof n, "%s%g", i ? " " : "", c.val[k][i]); v.puts (n); }
				attr (o, "v", v.str ()); o.puts ("/>");
			}
			o.puts ("</onyx:data>");
			// the picture: the chart rendered (twice the slide at 96 dpi)
			Layer L; float sc = 2 * 96.0f / 2540; render_object (d, ob, sc, L, false);
			for (int i = 0; i < L.w * L.h; i++) { unsigned p = L.px[i], a = p >> 24; if (a && a < 255) { unsigned r = ((p >> 16) & 255) * 255 / a, g = ((p >> 8) & 255) * 255 / a, b = (p & 255) * 255 / a; L.px[i] = a << 24 | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b); } }
			unsigned pn; unsigned char *png = pngsave::png_encode (L.px, L.w, L.h, true, &pn);
			char path[48]; snprintf (path, sizeof path, "Pictures/chart%d.png", nextChart++);
			if (png) { zip->add (path, png, pn, false); delete[] png; pics.push (-1 - nextChart); }
			L.release ();
			o.puts ("<draw:image"); attr (o, "xlink:href", path); o.puts (" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"><text:p/></draw:image>");
			o.puts ("</draw:frame>");
			break;
		}
		default:			// a text box, a placeholder
			o.puts ("<draw:frame"); attr (o, "draw:style-name", gs); attr (o, "xml:id", id); attr (o, "draw:id", id); geometry (o, ob); onyx_attrs (o, ob);
			if (ob.ph != PH_NONE && PH_CLASS[(int) ob.ph][0]) { attr (o, "presentation:class", PH_CLASS[(int) ob.ph]); if (ob.tb.empty () && !master) o.puts (" presentation:placeholder=\"true\""); }
			o.puts ("><draw:text-box>");
			text (o, ob.tb, ob.ph);
			o.puts ("</draw:text-box></draw:frame>");
			break;
		}
	}
	void dp_style (Slide &s, char *name)
	{
		snprintf (name, 16, "dp%d", ++ndp);
		Buf &o = autoStyles;
		o.puts ("<style:style style:family=\"drawing-page\""); attr (o, "style:name", name); o.puts ("><style:drawing-page-properties");
		if (s.bg.type == FILL_GRADIENT) { char g[16]; gradient (s.bg, g); o.puts (" draw:fill=\"gradient\""); attr (o, "draw:fill-gradient-name", g); o.puts (" draw:background-size=\"full\""); }
		else if (s.bg.type != FILL_INHERIT)
		{
			o.puts (" draw:fill=\"solid\" draw:fill-color=\""); put_hex (o, col (s.bg.c1)); o.puts ("\"");
			o.puts (" draw:background-size=\"full\"");
		}
		static const char *const SMIL[TR_COUNT][2] = { { "", "" }, { "fade", "crossfade" }, { "pushWipe", "fromRight" }, { "barWipe", "leftToRight" },
			{ "slideWipe", "fromRight" }, { "slideWipe", "fromRight" }, { "barnDoorWipe", "vertical" }, { "zoom", "in" }, { "dissolve", "" } };
		if (s.tr.type != TR_NONE)
		{
			attr (o, "smil:type", SMIL[(int) s.tr.type][0]);
			if (SMIL[(int) s.tr.type][1][0]) attr (o, "smil:subtype", SMIL[(int) s.tr.type][1]);
			if (s.tr.type == TR_UNCOVER) o.puts (" smil:direction=\"reverse\"");
			char t[32]; snprintf (t, sizeof t, "%.1fs", s.tr.dur / 1000.0); attr (o, "smil:dur", t);
			attr (o, "presentation:transition-speed", s.tr.dur < 500 ? "fast" : s.tr.dur > 1200 ? "slow" : "medium");
		}
		if (s.tr.after >= 0) { o.puts (" presentation:transition-type=\"automatic\""); char t[32]; snprintf (t, sizeof t, "PT%dS", s.tr.after / 1000); attr (o, "presentation:duration", t); }
		if (!s.masterObjects) o.puts (" presentation:background-objects-visible=\"false\"");
		if (d.number && s.masterObjects) o.puts (" presentation:display-page-number=\"true\"");
		if (d.footer && d.footerText[0] && s.masterObjects) o.puts (" presentation:display-footer=\"true\"");
		o.puts ("/></style:style>");
	}
	void page (Buf &o, Slide &s, int index)
	{
		char dp[16]; dp_style (s, dp);
		char name[24]; snprintf (name, sizeof name, "page%d", index + 1);
		o.puts ("<draw:page"); attr (o, "draw:name", name); attr (o, "draw:style-name", dp); o.puts (" draw:master-page-name=\"Onyx\"");
		static const char *PL[LY_COUNT] = { "AL1T0", "AL1T1", "AL1T3", "AL1T3", "AL1T19", "AL1T19", "AL1T1", "AL1T19" };
		attr (o, "presentation:presentation-page-layout-name", PL[s.layout]);
		attri (o, "onyx:layout", s.layout);
		char tr[48]; snprintf (tr, sizeof tr, "%d|%d|%d|%d", s.tr.type, s.tr.dir, s.tr.dur, s.tr.after); attr (o, "onyx:transition", tr);
		o.puts (" onyx:bg=\""); fill_code (o, s.bg); o.puts ("\"");
		attri (o, "onyx:master", s.masterObjects); attri (o, "onyx:hidden", s.hidden);
		if (s.hidden) o.puts (" presentation:visibility=\"hidden\"");
		if (s.section[0]) attr (o, "onyx:section", s.section);
		o.put ('>');
		for (int i = 0; i < s.obj.n; i++) object (o, *s.obj[i]);
		// the effects (Onyx's own)
		if (s.anim.n)
		{
			o.puts ("<onyx:effects>");
			for (int i = 0; i < s.anim.n; i++)
			{
				Anim &a = s.anim[i];
				char t[96]; snprintf (t, sizeof t, "%d|%d|%d|%d|%d|%d|%d|%d", a.obj, a.cls, a.fx, a.start, a.dir, a.byPara, a.delay, a.dur);
				o.puts ("<onyx:fx"); attr (o, "v", t); o.puts ("/>");
			}
			o.puts ("</onyx:effects>");
		}
		// the notes
		o.puts ("<presentation:notes><draw:page-thumbnail");
		attrcm (o, "svg:x", 2000); attrcm (o, "svg:y", 2500); attrcm (o, "svg:width", 17000); attrcm (o, "svg:height", 17000 * d.sh / d.sw);
		attri (o, "draw:page-number", index + 1); o.puts (" presentation:class=\"page\"/>");
		o.puts ("<draw:frame presentation:class=\"notes\""); attrcm (o, "svg:x", 2000); attrcm (o, "svg:y", 13000); attrcm (o, "svg:width", 17000); attrcm (o, "svg:height", 13000);
		o.puts ("><draw:text-box>");
		for (int i = 0; i < s.notes.p.n; i++)
		{
			o.puts ("<text:p>");
			const Para *q = s.notes.p[i];
			for (int k = 0; k < q->len; k++) { unsigned c = q->ch[k]; if (c == '&') o.puts ("&amp;"); else if (c == '<') o.puts ("&lt;"); else if (c == '>') o.puts ("&gt;"); else if (c == '\t') o.puts ("<text:tab/>"); else if (c >= 32) o.putu (c); }
			o.puts ("</text:p>");
		}
		o.puts ("</draw:text-box></draw:frame></presentation:notes>");
		o.puts ("</draw:page>");
	}
	// onyx.xml: the theme, the master's styles and layouts, the deck's settings
	void deck_xml (Buf &o)
	{
		o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<onyx:deck xmlns:onyx=\"urn:onyx:slides:1.0\" xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" xmlns:xml=\"http://www.w3.org/XML/1998/namespace\" version=\"1\"");
		attri (o, "width", d.sw); attri (o, "height", d.sh); attri (o, "nextId", d.nextId);
		attri (o, "footer", d.footer); attri (o, "number", d.number); attri (o, "date", d.date); attr (o, "footerText", d.footerText);
		o.puts (" bg=\""); fill_code (o, d.masterBg); o.puts ("\">");
		o.puts ("<onyx:theme"); attr (o, "name", d.theme.name); attr (o, "major", d.theme.major); attr (o, "minor", d.theme.minor);
		Buf cs; for (int i = 0; i < TC_COUNT; i++) { char t[12]; snprintf (t, sizeof t, "%s%06x", i ? " " : "", d.theme.col[i]); cs.puts (t); }
		attr (o, "colours", cs.str ()); o.puts ("/>");
		for (int i = 0; i < TS_COUNT; i++)
		{
			o.puts ("<onyx:style"); attri (o, "k", i); o.puts (" cf=\""); cf_code (o, d, d.style[i].cf); o.puts ("\" pf=\""); pf_code (o, d.style[i].pf); o.puts ("\"");
			attri (o, "bullet", d.style[i].bullet); attri (o, "indent", d.style[i].indent); o.puts ("/>");
		}
		o.puts ("<onyx:decor>"); for (int i = 0; i < d.decor.n; i++) object (o, *d.decor[i], true); o.puts ("</onyx:decor>");
		for (int l = 0; l < LY_COUNT; l++)
		{
			o.puts ("<onyx:layout"); attri (o, "k", l); attr (o, "name", d.layout[l].name); o.put ('>');
			for (int i = 0; i < d.layout[l].ph.n; i++) object (o, *d.layout[l].ph[i], true);
			o.puts ("</onyx:layout>");
		}
		o.puts ("</onyx:deck>\n");
	}
};

static const char *ODF_NS = " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
	" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
	" xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
	" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
	" xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
	" xmlns:smil=\"urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0\" xmlns:anim=\"urn:oasis:names:tc:opendocument:xmlns:animation:1.0\""
	" xmlns:xml=\"http://www.w3.org/XML/1998/namespace\" xmlns:onyx=\"urn:onyx:slides:1.0\" office:version=\"1.3\"";

// The list styles (bullets, numbers) with their levels' indents
static void list_styles (Buf &o, const Deck &d)
{
	o.puts ("<text:list-style style:name=\"LB\">");
	for (int l = 0; l < 5; l++)
	{
		char t[400];
		snprintf (t, sizeof t, "<text:list-level-style-bullet text:level=\"%d\" text:bullet-char=\"", l + 1); o.puts (t);
		o.putu (d.style[TS_BODY1 + l].bullet ? d.style[TS_BODY1 + l].bullet : 0x2022);
		snprintf (t, sizeof t, "\"><style:list-level-properties text:space-before=\"%.2fcm\" text:min-label-width=\"0.7cm\"/></text:list-level-style-bullet>", l * d.style[TS_BODY1].indent / 1000.0);
		o.puts (t);
	}
	o.puts ("</text:list-style><text:list-style style:name=\"LN\">");
	for (int l = 0; l < 5; l++)
	{
		char t[400];
		snprintf (t, sizeof t, "<text:list-level-style-number text:level=\"%d\" style:num-suffix=\".\" style:num-format=\"%s\"><style:list-level-properties text:space-before=\"%.2fcm\" text:min-label-width=\"0.8cm\"/></text:list-level-style-number>",
			  l + 1, l % 3 == 1 ? "a" : l % 3 == 2 ? "i" : "1", l * d.style[TS_BODY1].indent / 1000.0);
		o.puts (t);
	}
	o.puts ("</text:list-style>");
}

// The deck as an .odp archive (new []): *len bytes.
static unsigned char *odp_save (Deck &d, unsigned *len)
{
	pngsave::ZipOut z;
	static const char mt[] = "application/vnd.oasis.opendocument.presentation";
	z.add ("mimetype", mt, (unsigned) strlen (mt), false);
	OdpOut w (d); w.zip = &z;
	// content.xml
	Buf body;
	for (int i = 0; i < d.slides.n; i++) w.page (body, *d.slides[i], i);
	Buf c;
	c.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content"); c.puts (ODF_NS); c.puts (">");
	c.puts ("<office:font-face-decls>");
	Vec<const char *> names; names.push (d.theme.major); if (strcmp (d.theme.minor, d.theme.major)) names.push (d.theme.minor);
	for (int i = 0; i < d.font.n; i++) { bool dup = false; for (int k = 0; k < names.n; k++) if (!strcmp (names[k], d.font[i])) dup = true; if (!dup) names.push (d.font[i]); }
	for (int i = 0; i < names.n; i++) { c.puts ("<style:font-face"); attr (c, "style:name", names[i]); Buf q; q.puts ("'"); q.puts (names[i]); q.puts ("'"); attr (c, "svg:font-family", q.str ()); c.puts ("/>"); }
	c.puts ("</office:font-face-decls><office:automatic-styles>");
	c.puts (w.autoStyles.str ());
	list_styles (c, d);
	c.puts ("</office:automatic-styles><office:body><office:presentation>");
	c.puts (body.str ());
	c.puts ("</office:presentation></office:body></office:document-content>\n");
	z.add ("content.xml", c.b, (unsigned) c.n, true);
	// styles.xml: the page's size, the master page and its background, its objects (content.xml's gradients kept: styles.xml has them all)
	w.autoStyles.clear ();
	Buf mo;
	for (int i = 0; i < d.decor.n; i++) w.object (mo, *d.decor[i], true);
	Buf s;
	s.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles"); s.puts (ODF_NS); s.puts (">");
	s.puts ("<office:styles>"); s.puts (w.grads.str ()); s.puts ("<style:default-style style:family=\"graphic\"><style:text-properties");
	attr (s, "fo:font-family", d.theme.minor); s.puts (" fo:font-size=\"18pt\"/></style:default-style></office:styles>");
	s.puts ("<office:automatic-styles><style:page-layout style:name=\"PM1\"><style:page-layout-properties fo:margin-top=\"0cm\" fo:margin-bottom=\"0cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\"");
	attrcm (s, "fo:page-width", d.sw); attrcm (s, "fo:page-height", d.sh); s.puts (" style:print-orientation=\"landscape\"/></style:page-layout>");
	s.puts ("<style:style style:family=\"drawing-page\" style:name=\"Mdp1\"><style:drawing-page-properties draw:fill=\"solid\" draw:fill-color=\""); put_hex (s, d.rgb (d.masterBg.c1)); s.puts ("\" draw:background-size=\"full\"/></style:style>");
	s.puts (w.autoStyles.str ());
	s.puts ("</office:automatic-styles><office:master-styles><style:master-page style:name=\"Onyx\" style:page-layout-name=\"PM1\" draw:style-name=\"Mdp1\">");
	s.puts (mo.str ());
	s.puts ("</style:master-page></office:master-styles></office:document-styles>\n");
	z.add ("styles.xml", s.b, (unsigned) s.n, true);
	// onyx.xml
	w.autoStyles.clear ();
	Buf dx; w.deck_xml (dx);
	z.add ("onyx.xml", dx.b, (unsigned) dx.n, true);
	// meta, manifest
	Buf m;
	m.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta"); m.puts (ODF_NS); m.puts ("><office:meta><meta:generator>Onyx Slides</meta:generator></office:meta></office:document-meta>\n");
	z.add ("meta.xml", m.b, (unsigned) m.n, true);
	Buf mf;
	mf.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">");
	mf.puts ("<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.presentation\"/>");
	mf.puts ("<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/><manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>");
	mf.puts ("<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/><manifest:file-entry manifest:full-path=\"onyx.xml\" manifest:media-type=\"text/xml\"/>");
	for (int i = 0; i < w.pics.n; i++)
	{
		char p[80];
		if (w.pics[i] >= 0) { Picture &pc = g_pics[w.pics[i]]; const char *ext = strrchr (pc.name, '.'); snprintf (p, sizeof p, "Pictures/p%d%s", w.pics[i], ext ? ext : ".png"); }
		else snprintf (p, sizeof p, "Pictures/chart%d.png", -1 - w.pics[i] - 1);
		const char *e = strrchr (p, '.');
		const char *mtp = e && (!strcmp (e, ".jpg") || !strcmp (e, ".jpeg")) ? "image/jpeg" : e && !strcmp (e, ".gif") ? "image/gif" : "image/png";
		mf.puts ("<manifest:file-entry"); attr (mf, "manifest:full-path", p); attr (mf, "manifest:media-type", mtp); mf.puts ("/>");
	}
	mf.puts ("</manifest:manifest>\n");
	z.add ("META-INF/manifest.xml", mf.b, (unsigned) mf.n, true);
	return z.finish (len);
}

// ---- reading -------------------------------------------------------------------------------------------------------
// The styles of an ODF document: each one's properties (as attributes' text), its parent's name.
struct OStyle { char name[48], parent[48]; char family[16]; Buf props; char listStyle[32]; };
struct OGrad { char name[48]; unsigned c1, c2; int angle; };
struct OdpIn
{
	Deck &d;
	Vec<OGrad> grads;
	const unsigned char *z; unsigned zn;
	Vec<OStyle *> styles;
	char fonts[32][2][48]; int nfonts;		// style:font-face: its name -> its family
	bool own;					// written by Slides (onyx attributes)
	OdpIn (Deck &d_, const unsigned char *z_, unsigned zn_) : d (d_), z (z_), zn (zn_), nfonts (0), own (false) {}
	~OdpIn () { for (int i = 0; i < styles.n; i++) delete styles[i]; }

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
	// the styles of a part (styles.xml, content.xml): automatic and common
	void read_styles (const char *xml, int n)
	{
		XmlReader r (xml, n);
		OStyle *cur = 0;
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START && r.isq ("draw:gradient"))
			{
				OGrad g; Buf v; g.name[0] = 0;
				if (r.attr ("draw:name", v)) scpy (g.name, v.str (), 48);
				g.c1 = r.attr ("draw:start-color", v) ? colour_of (v.str (), 0) : 0; g.c2 = r.attr ("draw:end-color", v) ? colour_of (v.str (), 0xFFFFFF) : 0xFFFFFF;
				int a = r.attr ("draw:angle", v) ? atoi (v.str ()) : 0; if (strstr (v.str (), "deg")) a *= 10;
				g.angle = ((90 - a / 10) % 360 + 360) % 360;
				grads.push (g);
			}
			else if (r.ev == X_START && r.is ("font-face"))
			{
				Buf a, b; r.attr ("style:name", a); r.attr ("svg:font-family", b);
				if (nfonts < 32) { scpy (fonts[nfonts][0], a.str (), 48); const char *f = b.str (); if (*f == '\'') f++; scpy (fonts[nfonts][1], f, 48); int l = (int) strlen (fonts[nfonts][1]); if (l && fonts[nfonts][1][l - 1] == '\'') fonts[nfonts][1][l - 1] = 0; nfonts++; }
			}
			else if (r.ev == X_START && (r.is ("style") || r.is ("default-style")) && !r.isq ("style:style") == false)
			{
				cur = new OStyle; memset (cur->name, 0, sizeof cur->name); cur->parent[0] = 0; cur->family[0] = 0; cur->listStyle[0] = 0;
				Buf a; if (r.attr ("style:name", a)) scpy (cur->name, a.str (), 48);
				if (r.attr ("style:parent-style-name", a)) scpy (cur->parent, a.str (), 48);
				if (r.attr ("style:family", a)) scpy (cur->family, a.str (), 16);
				if (r.attr ("style:list-style-name", a)) scpy (cur->listStyle, a.str (), 32);
				styles.push (cur);
				if (r.empty) cur = 0;
			}
			else if (r.ev == X_START && cur && (r.is ("graphic-properties") || r.is ("text-properties") || r.is ("paragraph-properties") || r.is ("drawing-page-properties")))
			{
				cur->props.put (' '); cur->props.putn (r.at, (int) (r.ate - r.at));
			}
			else if (r.ev == X_END && (r.is ("style") || r.is ("default-style"))) cur = 0;
		}
	}
	OStyle *style (const char *name) { if (!name || !*name) return 0; for (int i = styles.n - 1; i >= 0; i--) if (!strcmp (styles[i]->name, name)) return styles[i]; return 0; }
	// a property of a style (its parents followed): the value, or false
	bool prop (const char *styleName, const char *key, Buf &out, int depth = 0)
	{
		OStyle *s = style (styleName);
		if (!s || depth > 8) return false;
		// the props: attributes' text
		const char *p = s->props.str ();
		int kl = (int) strlen (key);
		for (const char *q = p; (q = strstr (q, key)) != 0; q += kl)
		{
			if ((q == p || q[-1] == ' ' || q[-1] == '\n' || q[-1] == '\t') && q[kl] == '=')
			{
				const char *v = q + kl + 1; char quote = *v++;
				const char *e = strchr (v, quote);
				if (!e) break;
				out.clear (); out.putn (v, (int) (e - v));
				return true;
			}
		}
		return prop (s->parent, key, out, depth + 1);
	}
	const char *family_name (const char *fontName)
	{
		for (int i = 0; i < nfonts; i++) if (!strcmp (fonts[i][0], fontName)) return fonts[i][1];
		return fontName;
	}
	// A character format from a text style (an Impress file's), over the paragraph's and the object's
	CharFmt cf_from (const char *ts, const char *ps, const char *gs)
	{
		CharFmt f = cf_inherit ();
		const char *chain[3] = { gs, ps, ts };
		Buf v;
		for (int i = 0; i < 3; i++)
		{
			const char *s = chain[i]; if (!s || !*s) continue;
			if (prop (s, "fo:font-size", v) && !strchr (v.str (), '%')) f.size = (short) (len_hmm (v.str ()) / 3.5277778 + 0.5);
			if (prop (s, "fo:color", v)) f.color = colour_of (v.str (), AUTO);
			if (prop (s, "style:font-name", v)) f.font = (short) d.font_index (family_name (v.str ()));
			if (prop (s, "fo:font-family", v)) { const char *q = v.str (); char t[48]; scpy (t, *q == '\'' ? q + 1 : q, 48); int l = (int) strlen (t); if (l && t[l - 1] == '\'') t[l - 1] = 0; f.font = (short) d.font_index (t); }
			if (prop (s, "fo:font-weight", v)) { f.set |= CF_BOLD; if (!strcmp (v.str (), "bold") || atoi (v.str ()) >= 600) f.flags |= CF_BOLD; else f.flags &= ~CF_BOLD; }
			if (prop (s, "fo:font-style", v)) { f.set |= CF_ITALIC; if (!strcmp (v.str (), "italic") || !strcmp (v.str (), "oblique")) f.flags |= CF_ITALIC; else f.flags &= ~CF_ITALIC; }
			if (prop (s, "style:text-underline-style", v)) { f.set |= CF_UNDER; if (strcmp (v.str (), "none")) f.flags |= CF_UNDER; else f.flags &= ~CF_UNDER; }
			if (prop (s, "style:text-line-through-style", v)) { f.set |= CF_STRIKE; if (strcmp (v.str (), "none")) f.flags |= CF_STRIKE; else f.flags &= ~CF_STRIKE; }
			if (prop (s, "style:text-position", v)) { if (!strncmp (v.str (), "super", 5) || (atoi (v.str ()) > 0)) f.flags |= CF_SUPER; else if (!strncmp (v.str (), "sub", 3) || atoi (v.str ()) < 0) f.flags |= CF_SUB; f.set |= CF_SUPER | CF_SUB; }
		}
		return f;
	}
	ParaFmt pf_from (const char *ps)
	{
		ParaFmt p = pf_inherit ();
		Buf v;
		if (prop (ps, "fo:text-align", v))
		{
			const char *a = v.str ();
			p.align = !strcmp (a, "center") ? AL_CENTER : (!strcmp (a, "end") || !strcmp (a, "right")) ? AL_RIGHT : !strcmp (a, "justify") ? AL_JUSTIFY : AL_LEFT;
		}
		if (prop (ps, "fo:margin-top", v)) p.before = (short) (len_hmm (v.str ()) / 3.5277778);
		if (prop (ps, "fo:margin-bottom", v)) p.after = (short) (len_hmm (v.str ()) / 3.5277778);
		if (prop (ps, "fo:line-height", v) && strchr (v.str (), '%')) p.spacing = (short) atoi (v.str ());
		return p;
	}
	Fill gradient_of (const char *st)
	{
		Buf g; Fill f = fill_solid (0x729FCF);
		if (!prop (st, "draw:fill-gradient-name", g)) return f;
		for (int i = 0; i < grads.n; i++) if (!strcmp (grads[i].name, g.str ())) { f.type = FILL_GRADIENT; f.c1 = grads[i].c1; f.c2 = grads[i].c2; f.angle = (short) grads[i].angle; }
		return f;
	}
	// The object's fill and line from its graphic style
	void graphic_from (Object &o, const char *gs)
	{
		Buf v;
		if (prop (gs, "draw:fill", v))
		{
			if (!strcmp (v.str (), "none")) o.fill = fill_none ();
			else if (!strcmp (v.str (), "gradient")) o.fill = gradient_of (gs);
			else { Buf c; o.fill = fill_solid (prop (gs, "draw:fill-color", c) ? colour_of (c.str (), 0x729FCF) : 0x729FCF); }
		}
		if (prop (gs, "draw:opacity", v)) o.fill.alpha = (unsigned char) (atoi (v.str ()) * 255 / 100);
		if (prop (gs, "draw:stroke", v))
		{
			if (!strcmp (v.str (), "none")) o.line = line_none ();
			else
			{
				Buf c, w; unsigned col = prop (gs, "svg:stroke-color", c) ? colour_of (c.str (), 0x3465A4) : 0x3465A4;
				double wd = prop (gs, "svg:stroke-width", w) ? len_hmm (w.str ()) : 26;
				o.line = line_solid (col, (int) (wd < 10 ? 26 : wd)); if (!strcmp (v.str (), "dash")) o.line.type = LN_DASH;
			}
		}
		if (prop (gs, "draw:marker-end", v) && v.n) o.line.head1 = AH_ARROW;
		if (prop (gs, "draw:marker-start", v) && v.n) o.line.head0 = AH_ARROW;
		if (prop (gs, "draw:textarea-vertical-align", v)) o.tb.anchor = !strcmp (v.str (), "middle") ? AN_MIDDLE : !strcmp (v.str (), "bottom") ? AN_BOTTOM : AN_TOP;
		if (prop (gs, "draw:shadow", v)) o.shadow = !strcmp (v.str (), "visible");
		if (prop (gs, "fo:padding-left", v)) o.tb.inset[0] = (short) len_hmm (v.str ());
		if (prop (gs, "fo:padding-top", v)) o.tb.inset[1] = (short) len_hmm (v.str ());
		if (prop (gs, "fo:padding-right", v)) o.tb.inset[2] = (short) len_hmm (v.str ());
		if (prop (gs, "fo:padding-bottom", v)) o.tb.inset[3] = (short) len_hmm (v.str ());
		if (prop (gs, "draw:auto-grow-height", v) && !strcmp (v.str (), "true")) o.tb.fit = FIT_GROW;
		if (prop (gs, "style:shrink-to-fit", v) && !strcmp (v.str (), "true")) o.tb.fit = FIT_SHRINK;
		if (prop (gs, "draw:fit-to-size", v) && !strcmp (v.str (), "shrink-to-fit")) o.tb.fit = FIT_SHRINK;
	}
	// The geometry: svg:x / y / width / height, or a draw:transform's rotate and translate
	void geometry_from (XmlReader &r, Object &o)
	{
		Buf v;
		o.w = (int) (r.attr ("svg:width", v) ? len_hmm (v.str ()) : 0); o.h = (int) (r.attr ("svg:height", v) ? len_hmm (v.str ()) : 0);
		o.x = (int) (r.attr ("svg:x", v) ? len_hmm (v.str ()) : 0); o.y = (int) (r.attr ("svg:y", v) ? len_hmm (v.str ()) : 0);
		if (r.attr ("draw:transform", v))
		{
			const char *t = v.str (); double a = 0, tx = 0, ty = 0;
			const char *ro = strstr (t, "rotate"); if (ro) { ro = strchr (ro, '('); if (ro) a = strtod (ro + 1, 0); }
			const char *tr = strstr (t, "translate");
			if (tr) { tr = strchr (tr, '('); if (tr) { char *e; tx = len_hmm (tr + 1); strtod (tr + 1, &e); while (*e && *e != ' ' && *e != ',') e++; while (*e == ' ' || *e == ',') e++; ty = len_hmm (e); } }
			// the box's centre: (w/2, h/2) turned by a (counter-clockwise) then translated
			double ca = cos (a), sa = sin (a);
			double cx = tx + o.w / 2.0 * ca + o.h / 2.0 * sa, cy = ty - o.w / 2.0 * sa + o.h / 2.0 * ca;
			o.x = (int) (cx - o.w / 2.0); o.y = (int) (cy - o.h / 2.0);
			int deg = (int) lround (-a * 180 / 3.14159265358979);
			o.rot = (short) (((deg % 360) + 360) % 360);
		}
	}
	// The onyx attributes on an object's element (Slides' own file): true if there
	bool onyx_from (XmlReader &r, Object &o)
	{
		Buf v;
		if (!r.attr ("onyx:kind", v)) return false;
		o.kind = (signed char) atoi (v.str ());
		if (r.attr ("onyx:id", v)) o.id = atoi (v.str ());
		if (r.attr ("onyx:shape", v)) o.shape = (signed char) atoi (v.str ());
		if (r.attr ("onyx:ph", v)) o.ph = (signed char) atoi (v.str ());
		if (r.attr ("onyx:rot", v)) o.rot = (short) atoi (v.str ());
		if (r.attr ("onyx:radius", v)) o.radius = (short) atoi (v.str ());
		if (r.attr ("onyx:fill", v)) o.fill = fill_parse (v.str ());
		if (r.attr ("onyx:line", v)) o.line = line_parse (v.str ());
		if (r.attr ("onyx:box", v))
		{
			int a[14] = { 0 };
			sscanf (v.str (), "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10], &a[11], &a[12], &a[13]);
			o.x = a[0]; o.y = a[1]; o.w = a[2]; o.h = a[3]; o.flipH = a[4]; o.flipV = a[5]; o.shadow = a[6];
			o.tb.anchor = (signed char) a[7]; o.tb.fit = (signed char) a[8]; o.tb.wrap = a[9];
			for (int i = 0; i < 4; i++) o.tb.inset[i] = (short) a[10 + i];
		}
		if (r.attr ("onyx:crop", v)) { int c[4] = { 0 }; sscanf (v.str (), "%d|%d|%d|%d", &c[0], &c[1], &c[2], &c[3]); for (int i = 0; i < 4; i++) o.crop[i] = (short) c[i]; }
		if (r.attr ("draw:name", v)) scpy (o.name, v.str (), sizeof o.name);
		return true;
	}

	// The text of an element (a draw:text-box, a shape, a table cell): its paragraphs, until its end
	void read_text (XmlReader &r, TextBody &tb, const char *gs, int endDepth)
	{
		tb.clear ();
		int depth = 0, listDepth = 0;
		Para *q = 0; char ps[48] = ""; Vec<int> spanStack;
		char tsStack[8][48]; int nts = 0; CharFmt cfStack[8]; bool cfOwn[8];
		CharFmt cur = cf_inherit ();
		bool curOwn = false;
		(void) endDepth;
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START)
			{
				depth++;				// (an empty element's end comes too: X_END)
				if (r.is ("list")) listDepth++;
				else if (r.is ("p") || r.is ("h"))
				{
					q = new Para;
					Buf v;
					ps[0] = 0; if (r.attr ("text:style-name", v)) scpy (ps, v.str (), 48);
					if (r.attr ("onyx:pf", v)) q->pf = pf_parse (v.str ());
					else
					{
						q->pf = pf_from (ps);
						if (listDepth > 0) { q->pf.level = (signed char) iclamp (listDepth - 1, 0, 4); q->pf.bullet = own ? BU_INHERIT : BU_BULLET; }
						else if (!own) q->pf.bullet = BU_INHERIT;
					}
					if (r.attr ("onyx:end", v)) q->end = cf_parse (d, v.str ());
					else q->end = cf_from (0, ps, gs);
					cur = q->end; curOwn = r.attr ("onyx:end", v);
					tb.p.push (q);
					nts = 0;
				}
				else if (r.is ("span") && q)
				{
					Buf v, cfv;
					if (nts < 8) { cfStack[nts] = cur; cfOwn[nts] = curOwn; scpy (tsStack[nts], "", 48); nts++; }
					if (r.attr ("onyx:cf", cfv)) { cur = cf_parse (d, cfv.str ()); curOwn = true; }
					else if (r.attr ("text:style-name", v)) { cur = cf_from (v.str (), ps, gs); curOwn = false; }
				}
				else if (r.is ("s") && q) { Buf v; int c = r.attr ("text:c", v) ? atoi (v.str ()) : 1; for (int i = 0; i < c && i < 200; i++) { unsigned sp = ' '; q->insert (q->len, &sp, 1, cur); } }
				else if (r.is ("tab") && q) { unsigned t = '\t'; q->insert (q->len, &t, 1, cur); }
				else if (r.is ("line-break") && q) { unsigned n = ' '; q->insert (q->len, &n, 1, cur); }
			}
			else if (r.ev == X_TEXT && q)
			{
				const char *s = r.text.str (); int n = r.text.n;
				for (int i = 0; i < n; )
				{
					int l; unsigned c = ss::u8_dec (s + i, n - i, &l); i += l > 0 ? l : 1;
					if (c == '\n' || c == '\r' || c == '\t') c = ' ';
					// XML's spaces: runs collapsed (text:s carries the kept ones)
					if (c == ' ' && q->len && q->ch[q->len - 1] == ' ' && !own) continue;
					q->insert (q->len, &c, 1, cur);
				}
			}
			else if (r.ev == X_END)
			{
				if (depth == 0) return;			// the element holding the text ended
				depth--;
				if (r.is ("list")) listDepth--;
				else if (r.is ("p") || r.is ("h")) q = 0;
				else if (r.is ("span") && nts) { nts--; cur = cfStack[nts]; curOwn = cfOwn[nts]; }
			}
		}
	}
	// An object's element (its start just read): the object made, its content read to its end
	Object *read_object (XmlReader &r, bool notesFrame = false)
	{
		Object *o = new Object;
		Buf v;
		char gs[48] = ""; if (r.attr ("draw:style-name", v)) scpy (gs, v.str (), 48); else if (r.attr ("presentation:style-name", v)) scpy (gs, v.str (), 48);
		bool hasOnyx = onyx_from (r, *o);
		if (!hasOnyx)
		{
			geometry_from (r, *o);
			graphic_from (*o, gs);
			if (r.attr ("draw:name", v)) scpy (o->name, v.str (), sizeof o->name);
		}
		if (!o->id) o->id = d.nextId++;
		if (o->id >= d.nextId) d.nextId = o->id + 1;
		(void) notesFrame;
		if (r.is ("line"))
		{
			if (!hasOnyx)
			{
				double x1 = r.attr ("svg:x1", v) ? len_hmm (v.str ()) : 0, y1 = r.attr ("svg:y1", v) ? len_hmm (v.str ()) : 0;
				double x2 = r.attr ("svg:x2", v) ? len_hmm (v.str ()) : 0, y2 = r.attr ("svg:y2", v) ? len_hmm (v.str ()) : 0;
				o->kind = OB_LINE; o->x = (int) fmin (x1, x2); o->y = (int) fmin (y1, y2); o->w = (int) fabs (x2 - x1); o->h = (int) fabs (y2 - y1);
				o->flipH = x2 < x1; o->flipV = y2 < y1;
				if (o->line.type == LN_NONE) o->line = line_solid (0x3465A4, 26);
			}
			r.skip ();
			return o;
		}
		if (r.is ("custom-shape") || r.is ("rect") || r.is ("ellipse"))
		{
			if (!hasOnyx) { o->kind = OB_SHAPE; o->shape = r.is ("ellipse") ? SH_ELLIPSE : SH_RECT; }
			if (r.empty) { r.skip (); o->tb.ensure (); return o; }
			// its text, its geometry's type
			int depth = 0;
			while (r.next () != X_EOF)
			{
				if (r.ev == X_START)
				{
					if (r.is ("enhanced-geometry"))
					{
						if (!hasOnyx && r.attr ("draw:type", v))
						{
							for (int k = 0; k < SH_COUNT; k++) if (!strcmp (DRAW_TYPES[k], v.str ())) o->shape = (signed char) k;
							if (!strcmp (v.str (), "flowchart-process")) o->shape = SH_RECT;
							if (!strcmp (v.str (), "flowchart-decision")) o->shape = SH_DIAMOND;
							if (!strcmp (v.str (), "star24") || !strcmp (v.str (), "bang")) o->shape = SH_STAR5;
						}
						r.skip ();
						continue;
					}
					if (r.is ("p") || r.is ("list") || r.is ("h"))
					{
						// the text: gathered here (the shape's paragraphs come before its geometry)
						TextBody tmp; read_one (r, tmp, gs);
						for (int i = 0; i < tmp.p.n; i++) o->tb.p.push (tmp.p[i]);
						tmp.p.clear ();
						continue;
					}
					depth++;
				}
				else if (r.ev == X_END) { if (depth == 0) break; depth--; }
			}
			o->tb.ensure ();
			return o;
		}
		// a frame: a text box, a picture, a table, a chart
		if (!hasOnyx) o->kind = OB_TEXT;
		if (r.attr ("presentation:class", v) && !hasOnyx)
		{
			const char *c = v.str ();
			o->ph = !strcmp (c, "title") ? PH_TITLE : !strcmp (c, "subtitle") ? PH_SUBTITLE : !strcmp (c, "outline") ? PH_BODY : !strcmp (c, "graphic") ? PH_PICTURE : PH_NONE;
			if (o->ph == PH_TITLE) o->tb.fit = FIT_SHRINK;
		}
		Buf chartAttr; bool isChart = r.attr ("onyx:chart", chartAttr);
		Buf tableAttr; bool isTable = r.attr ("onyx:table", tableAttr);
		if (r.empty) { r.skip (); o->tb.ensure (); return o; }
		int depth = 0;
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START)
			{
				if (r.is ("text-box")) { if (r.empty) r.skip (); else read_text (r, o->tb, gs, 0); continue; }
				if (r.is ("image") && !isChart)
				{
					Buf h;
					if (r.attr ("xlink:href", h))
					{
						int n = 0; char *b = entry (h.str (), &n);
						const char *e = strrchr (h.str (), '.');
						bool replacement = o->tbl || (e && (!strcmp (e, ".svm") || !strcmp (e, ".wmf") || !strcmp (e, ".emf")));	// (a table's preview, a metafile)
						if (b && !replacement) { int k = pic_add (h.str () + (strncmp (h.str (), "Pictures/", 9) ? 0 : 9), (unsigned char *) b, (unsigned) n); o->img = k; if (!hasOnyx || o->kind == OB_TEXT) o->kind = OB_PICTURE; }
						free (b);
					}
					r.skip ();
					continue;
				}
				if (r.is ("data") && isChart) { read_chart (r, *o, chartAttr.str ()); continue; }
				if (r.is ("table")) { if (r.empty) r.skip (); else read_table (r, *o, isTable ? tableAttr.str () : 0, gs); continue; }
				depth++;
			}
			else if (r.ev == X_END) { if (depth == 0) break; depth--; }
		}
		if (isChart && !o->chart) { o->chart = new Chart; }
		if (o->chart) o->kind = OB_CHART;
		if (o->tbl) { o->kind = OB_TABLE; o->img = -1; if (!hasOnyx) { o->fill = fill_none (); o->line = line_none (); } }
		o->tb.ensure ();
		return o;
	}
	// One paragraph or list (its start read) into tb
	void read_one (XmlReader &r, TextBody &tb, const char *gs)
	{
		// a wrapper: read_text wants to be inside an element; this one's content is the text itself
		bool list = r.is ("list");
		if (r.empty) { if (!list) { Para *q = new Para; Buf v; if (r.attr ("onyx:pf", v)) q->pf = pf_parse (v.str ()); tb.p.push (q); } r.skip (); return; }
		// read the paragraph by hand: a tiny copy of read_text with the start element already consumed
		TextBody t;
		read_text_from_start (r, t, gs, list);
		for (int i = 0; i < t.p.n; i++) tb.p.push (t.p[i]);
		t.p.clear ();
	}
	void read_text_from_start (XmlReader &r, TextBody &tb, const char *gs, bool list)
	{
		// the start element (p / h / list) is current: its attributes, then its content up to its end
		int listDepth = list ? 1 : 0, depth = 0;
		Para *q = 0; char ps[48] = "";
		CharFmt cur = cf_inherit (); CharFmt stack[8]; int ns = 0;
		auto start_para = [&] () {
			q = new Para; Buf v;
			ps[0] = 0; if (r.attr ("text:style-name", v)) scpy (ps, v.str (), 48);
			if (r.attr ("onyx:pf", v)) q->pf = pf_parse (v.str ());
			else { q->pf = pf_from (ps); if (listDepth > 0) { q->pf.level = (signed char) iclamp (listDepth - 1, 0, 4); q->pf.bullet = BU_BULLET; } }
			q->end = r.attr ("onyx:end", v) ? cf_parse (d, v.str ()) : cf_from (0, ps, gs);
			cur = q->end;
			tb.p.push (q);
		};
		if (!list) start_para ();
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START)
			{
				depth++;			// (an empty element's end comes too)
				if (r.is ("list")) { listDepth++; continue; }
				if (r.is ("p") || r.is ("h")) { start_para (); continue; }
				if (r.is ("span") && q)
				{
					Buf v; if (ns < 8) stack[ns++] = cur;
					if (r.attr ("onyx:cf", v)) cur = cf_parse (d, v.str ()); else if (r.attr ("text:style-name", v)) cur = cf_from (v.str (), ps, gs);
					continue;
				}
				if (r.is ("s") && q) { Buf v; int c = r.attr ("text:c", v) ? atoi (v.str ()) : 1; for (int i = 0; i < c && i < 200; i++) { unsigned sp = ' '; q->insert (q->len, &sp, 1, cur); } continue; }
				if (r.is ("tab") && q) { unsigned t = '\t'; q->insert (q->len, &t, 1, cur); continue; }
				if (r.is ("line-break") && q) { unsigned t = ' '; q->insert (q->len, &t, 1, cur); continue; }
			}
			else if (r.ev == X_TEXT && q)
			{
				const char *s = r.text.str (); int n = r.text.n;
				for (int i = 0; i < n; ) { int l; unsigned c = ss::u8_dec (s + i, n - i, &l); i += l > 0 ? l : 1; if (c == '\n' || c == '\r' || c == '\t') c = ' '; if (c == ' ' && q->len && q->ch[q->len - 1] == ' ' && !own) continue; q->insert (q->len, &c, 1, cur); }
			}
			else if (r.ev == X_END)
			{
				if (depth == 0) return;
				depth--;
				if (r.is ("list")) listDepth--;
				else if (r.is ("p") || r.is ("h")) q = 0;
				else if (r.is ("span") && ns) cur = stack[--ns];
			}
		}
	}
	void read_table (XmlReader &r, Object &o, const char *own_, const char *gs)
	{
		// first the rows and columns counted: the table read into lists
		Vec<int> colW; Vec<int> rowH; Vec<TextBody *> cells; Vec<Fill> fills; int cols = 0, row = -1, colInRow = 0;
		int depth = 0;
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START)
			{
				Buf v;
				if (r.is ("table-column"))
				{
					int rep = r.attr ("table:number-columns-repeated", v) ? atoi (v.str ()) : 1;
					int w = r.attr ("onyx:w", v) ? atoi (v.str ()) : 0;
					if (!w && r.attr ("table:style-name", v)) { Buf cw; if (prop (v.str (), "style:column-width", cw)) w = (int) len_hmm (cw.str ()); }
					for (int k = 0; k < rep && colW.n < 32; k++) colW.push (w);
					depth++;
					continue;
				}
				if (r.is ("table-row")) { row++; colInRow = 0; rowH.push (r.attr ("onyx:h", v) ? atoi (v.str ()) : 1000); depth++; continue; }
				if (r.is ("table-cell") || r.is ("covered-table-cell"))
				{
					TextBody *tb = new TextBody;
					Fill f = fill_none (); f.type = FILL_INHERIT;
					if (r.attr ("onyx:fill", v)) f = fill_parse (v.str ());
					if (r.empty) r.skip (); else read_text (r, *tb, gs, 0);
					tb->ensure ();
					cells.push (tb); fills.push (f); colInRow++;
					if (colInRow > cols) cols = colInRow;
					continue;
				}
				depth++;
			}
			else if (r.ev == X_END) { if (depth == 0) break; depth--; }
		}
		int rows = rowH.n;
		if (!rows || !cols) { for (int i = 0; i < cells.n; i++) delete cells[i]; return; }
		Table *t = new Table (rows, cols);
		int sumW = 0;
		for (int c = 0; c < cols; c++) { t->colW[c] = c < colW.n && colW[c] > 0 ? colW[c] : o.w / cols; sumW += t->colW[c]; }
		for (int k = 0; k < rows; k++) t->rowH[k] = rowH[k];
		int i = 0;
		for (int rr = 0; rr < rows; rr++)
			for (int c = 0; c < cols && i < cells.n; c++, i++) { t->at (rr, c).copy_from (*cells[i]); t->cfill[rr * cols + c] = fills[i]; for (int k = 0; k < 4; k++) t->at (rr, c).inset[k] = k % 2 ? 100 : 200; }
		for (int k = 0; k < cells.n; k++) delete cells[k];
		if (own_) { int a = 1, b = 1; sscanf (own_, "%d|%d", &a, &b); t->header = a; t->banded = b; }
		delete o.tbl; o.tbl = t; o.kind = OB_TABLE;
		(void) sumW;
	}
	void read_chart (XmlReader &r, Object &o, const char *head)
	{
		Chart *c = new Chart;
		int type = 0, ncat = 0, nser = 0, leg = 1, lab = 0; int consumed = 0;
		sscanf (head, "%d|%d|%d|%d|%d|%n", &type, &ncat, &nser, &leg, &lab, &consumed);
		c->type = type; c->legend = leg; c->labels = lab;
		if (consumed > 0) scpy (c->title, head + consumed, sizeof c->title);
		int depth = 0;
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START)
			{
				Buf v;
				if (r.is ("cat") && c->ncat < 24) { r.attr ("v", v); scpy (c->cat[c->ncat++], v.str (), 24); }
				else if (r.is ("ser") && c->nser < 8)
				{
					r.attr ("name", v); scpy (c->ser[c->nser], v.str (), 32);
					Buf vals; r.attr ("v", vals);
					const char *p = vals.str (); int i = 0; char *e;
					while (*p && i < 24) { c->val[c->nser][i++] = strtod (p, &e); if (e == p) break; p = e; }
					c->nser++;
				}
				depth++;
			}
			else if (r.ev == X_END) { if (depth == 0) break; depth--; }
		}
		delete o.chart; o.chart = c;
	}

	// The pages of content.xml
	void read_pages (const char *xml, int n)
	{
		XmlReader r (xml, n);
		Slide *s = 0;
		while (r.next () != X_EOF)
		{
			if (r.ev == X_START && r.is ("page") && r.isq ("draw:page"))
			{
				s = new Slide;
				Buf v;
				if (r.attr ("onyx:layout", v)) s->layout = atoi (v.str ()); else s->layout = -1;
				if (r.attr ("onyx:transition", v)) { int a[4] = { 0, 1, 700, -1 }; sscanf (v.str (), "%d|%d|%d|%d", &a[0], &a[1], &a[2], &a[3]); s->tr.type = (signed char) a[0]; s->tr.dir = (signed char) a[1]; s->tr.dur = (short) a[2]; s->tr.after = a[3]; }
				else if (r.attr ("draw:style-name", v)) transition_from (*s, v.str ());
				if (r.attr ("onyx:bg", v)) s->bg = fill_parse (v.str ());
				else if (r.attr ("draw:style-name", v)) { Buf c, f; if (prop (v.str (), "draw:fill", f)) { if (!strcmp (f.str (), "solid") && prop (v.str (), "draw:fill-color", c)) s->bg = fill_solid (colour_of (c.str (), 0xFFFFFF)); else if (!strcmp (f.str (), "gradient")) s->bg = gradient_of (v.str ()); } }
				if (r.attr ("onyx:master", v)) s->masterObjects = atoi (v.str ());
				if (r.attr ("onyx:hidden", v)) s->hidden = atoi (v.str ()); else if (r.attr ("presentation:visibility", v)) s->hidden = !strcmp (v.str (), "hidden");
				if (r.attr ("onyx:section", v)) scpy (s->section, v.str (), sizeof s->section);
				d.slides.push (s);
				continue;
			}
			if (!s) continue;
			if (r.ev == X_START && (r.is ("frame") || r.is ("custom-shape") || r.is ("line") || r.is ("rect") || r.is ("ellipse")))
			{
				Object *o = read_object (r);
				s->obj.push (o);
				continue;
			}
			if (r.ev == X_START && r.is ("fx")) { Buf v; if (r.attr ("v", v)) { Anim a; int x[8] = { 0 }; sscanf (v.str (), "%d|%d|%d|%d|%d|%d|%d|%d", &x[0], &x[1], &x[2], &x[3], &x[4], &x[5], &x[6], &x[7]); a.obj = x[0]; a.cls = (signed char) x[1]; a.fx = (signed char) x[2]; a.start = (signed char) x[3]; a.dir = (signed char) x[4]; a.byPara = x[5]; a.delay = (short) x[6]; a.dur = (short) x[7]; s->anim.push (a); } continue; }
			if (r.ev == X_START && r.is ("notes"))
			{
				int depth = 0;
				while (r.next () != X_EOF)
				{
					if (r.ev == X_START)
					{
						Buf v;
						if (r.is ("frame") && !(r.attr ("presentation:class", v) && strcmp (v.str (), "notes")))	// (the notes' frame: its class said, or not)
						{
							int dd = 0;
							while (r.next () != X_EOF) { if (r.ev == X_START && r.is ("text-box")) { if (r.empty) r.skip (); else read_text (r, s->notes, 0, 0); } else if (r.ev == X_START) dd++; else if (r.ev == X_END) { if (dd == 0) break; dd--; } }
							continue;
						}
						depth++;
					}
					else if (r.ev == X_END) { if (depth == 0) break; depth--; }
				}
				// (the notes' plain text: their formats dropped)
				for (int i = 0; i < s->notes.p.n; i++) for (int k = 0; k < s->notes.p[i]->len; k++) s->notes.p[i]->cf[k] = cf_inherit ();
				continue;
			}
			if (r.ev == X_END && r.isq ("draw:page")) s = 0;
		}
	}
	void transition_from (Slide &s, const char *dp)
	{
		Buf v, sub, dir, dur;
		if (prop (dp, "smil:type", v))
		{
			const char *t = v.str (); prop (dp, "smil:subtype", sub); prop (dp, "smil:direction", dir);
			s.tr.type = !strcmp (t, "fade") ? TR_FADE : !strcmp (t, "pushWipe") ? TR_PUSH : !strcmp (t, "barWipe") ? TR_WIPE : !strcmp (t, "slideWipe") ? (!strcmp (dir.str (), "reverse") ? TR_UNCOVER : TR_COVER)
				: !strcmp (t, "barnDoorWipe") ? TR_SPLIT : !strcmp (t, "zoom") ? TR_ZOOM : !strcmp (t, "dissolve") ? TR_DISSOLVE : TR_FADE;
			const char *st = sub.str ();
			s.tr.dir = !strcmp (st, "fromLeft") || !strcmp (st, "leftToRight") ? DIR_RIGHT : !strcmp (st, "fromTop") || !strcmp (st, "topToBottom") ? DIR_DOWN : !strcmp (st, "fromBottom") ? DIR_UP : DIR_LEFT;
			if (prop (dp, "smil:dur", dur)) s.tr.dur = (short) (atof (dur.str ()) * 1000);
		}
		if (prop (dp, "presentation:transition-type", v) && !strcmp (v.str (), "automatic") && prop (dp, "presentation:duration", dur))
		{
			// PT5S, PT1M30S
			const char *p = dur.str (); int sec = 0, val = 0;
			for (; *p; p++) { if (*p >= '0' && *p <= '9') val = val * 10 + (*p - '0'); else if (*p == 'H') { sec += val * 3600; val = 0; } else if (*p == 'M') { sec += val * 60; val = 0; } else if (*p == 'S') { sec += val; val = 0; } }
			s.tr.after = sec * 1000;
		}
		if (prop (dp, "presentation:background-objects-visible", v) && !strcmp (v.str (), "false")) s.masterObjects = false;
	}
	// onyx.xml: the deck's master (Slides' own files)
	void read_deck (const char *xml, int n)
	{
		XmlReader r (xml, n);
		int layout = -1; bool decor = false;
		while (r.next () != X_EOF)
		{
			if (r.ev != X_START) { if (r.ev == X_END) { if (r.is ("layout")) layout = -1; if (r.is ("decor")) decor = false; } continue; }
			Buf v;
			if (r.is ("deck"))
			{
				if (r.attr ("width", v)) d.sw = atoi (v.str ()); if (r.attr ("height", v)) d.sh = atoi (v.str ());
				if (r.attr ("nextId", v)) d.nextId = atoi (v.str ());
				if (r.attr ("footer", v)) d.footer = atoi (v.str ()); if (r.attr ("number", v)) d.number = atoi (v.str ()); if (r.attr ("date", v)) d.date = atoi (v.str ());
				if (r.attr ("footerText", v)) scpy (d.footerText, v.str (), sizeof d.footerText);
				if (r.attr ("bg", v)) d.masterBg = fill_parse (v.str ());
			}
			else if (r.is ("theme"))
			{
				if (r.attr ("name", v)) scpy (d.theme.name, v.str (), sizeof d.theme.name);
				if (r.attr ("major", v)) scpy (d.theme.major, v.str (), sizeof d.theme.major);
				if (r.attr ("minor", v)) scpy (d.theme.minor, v.str (), sizeof d.theme.minor);
				if (r.attr ("colours", v)) { const char *p = v.str (); for (int i = 0; i < TC_COUNT && *p; i++) { char *e; d.theme.col[i] = (unsigned) strtoul (p, &e, 16); p = e; while (*p == ' ') p++; } }
			}
			else if (r.is ("style"))
			{
				int k = r.attr ("k", v) ? atoi (v.str ()) : -1;
				if (k >= 0 && k < TS_COUNT)
				{
					if (r.attr ("cf", v)) d.style[k].cf = cf_parse (d, v.str ());
					if (r.attr ("pf", v)) d.style[k].pf = pf_parse (v.str ());
					if (r.attr ("bullet", v)) d.style[k].bullet = (unsigned) atoi (v.str ());
					if (r.attr ("indent", v)) d.style[k].indent = (short) atoi (v.str ());
				}
			}
			else if (r.is ("decor")) { decor = true; for (int i = 0; i < d.decor.n; i++) delete d.decor[i]; d.decor.clear (); }
			else if (r.is ("layout"))
			{
				layout = r.attr ("k", v) ? atoi (v.str ()) : -1;
				if (layout >= 0 && layout < LY_COUNT) { Layout &L = d.layout[layout]; for (int i = 0; i < L.ph.n; i++) delete L.ph[i]; L.ph.clear (); if (r.attr ("name", v)) scpy (L.name, v.str (), sizeof L.name); }
				else layout = -1;
			}
			else if (r.is ("frame") || r.is ("custom-shape") || r.is ("line"))
			{
				Object *o = read_object (r);
				if (decor) d.decor.push (o);
				else if (layout >= 0) d.layout[layout].ph.push (o);
				else delete o;
			}
		}
	}
	// A slide's layout guessed from its placeholders (an Impress file)
	static int guess_layout (const Slide &s)
	{
		int t = 0, sub = 0, body = 0, pic = 0;
		for (int i = 0; i < s.obj.n; i++) { int p = s.obj[i]->ph; if (p == PH_TITLE) t++; else if (p == PH_SUBTITLE) sub++; else if (p == PH_BODY) body++; else if (p == PH_PICTURE) pic++; }
		if (t && sub && !body) return LY_TITLE;
		if (t && body >= 2) return LY_TWO;
		if (t && body) return pic ? LY_PICTURE : LY_CONTENT;
		if (t) return LY_TITLE_ONLY;
		return LY_BLANK;
	}
};

// An .odp's bytes -> the deck (false: not one)
static bool odp_load (Deck &d, const unsigned char *b, unsigned n)
{
	pngsave::ZipEntry e;
	if (!pngsave::zip_find (b, n, "content.xml", &e)) return false;
	d.clear ();
	d.nextId = 1;
	master_default (d, 0);
	OdpIn in (d, b, n);
	int len = 0;
	char *dx = in.entry ("onyx.xml", &len);
	in.own = dx != 0;
	if (dx) { in.read_deck (dx, len); free (dx); }
	char *st = in.entry ("styles.xml", &len);
	if (st)
	{
		in.read_styles (st, len);
		if (!in.own)
		{
			// the page's size: the page layout of the first master page (the others: the notes', the handouts')
			char layouts[8][32]; int lw[8], lh[8], nl = 0; char useLayout[32] = "";
			{
				XmlReader r (st, len); char cur[32] = "";
				while (r.next () != X_EOF)
				{
					if (r.ev != X_START) continue;
					Buf v;
					if (r.is ("page-layout")) { cur[0] = 0; if (r.attr ("style:name", v)) scpy (cur, v.str (), 32); }
					else if (r.is ("page-layout-properties") && nl < 8)
					{
						scpy (layouts[nl], cur, 32);
						lw[nl] = r.attr ("fo:page-width", v) ? (int) len_hmm (v.str ()) : 0; lh[nl] = r.attr ("fo:page-height", v) ? (int) len_hmm (v.str ()) : 0;
						nl++;
					}
					else if (r.is ("master-page") && !useLayout[0] && r.attr ("style:page-layout-name", v)) scpy (useLayout, v.str (), 32);
				}
			}
			for (int i = 0; i < nl; i++)
				if (!strcmp (layouts[i], useLayout) && lw[i] > 0 && lh[i] > 0) { d.sw = lw[i]; d.sh = lh[i]; master_default (d, 0); }
			for (int i = 0; i < in.styles.n; i++)
				if (!strcmp (in.styles[i]->family, "drawing-page") && in.styles[i]->name[0] == 'M')
				{ Buf f, c; if (in.prop (in.styles[i]->name, "draw:fill", f) && !strcmp (f.str (), "solid") && in.prop (in.styles[i]->name, "draw:fill-color", c)) d.masterBg = fill_solid (colour_of (c.str (), 0xFFFFFF)); }
			// an Impress master's own objects are not drawn by Slides' master (its decorations dropped)
			for (int i = 0; i < d.decor.n; i++) delete d.decor[i];
			d.decor.clear ();
		}
		free (st);
	}
	char *ct = in.entry ("content.xml", &len);
	if (!ct) return false;
	in.read_styles (ct, len);
	in.read_pages (ct, len);
	free (ct);
	for (int i = 0; i < d.slides.n; i++)
	{
		Slide *s = d.slides[i];
		if (s->layout < 0 || s->layout >= LY_COUNT) s->layout = OdpIn::guess_layout (*s);
		s->notes.ensure ();
		for (int k = 0; k < s->obj.n; k++) s->obj[k]->tb.ensure ();
	}
	if (!d.slides.n) d.slides.push (slide_new (d, LY_TITLE));
	return true;
}

} // namespace sl

#endif
