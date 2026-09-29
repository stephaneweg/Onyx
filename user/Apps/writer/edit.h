//
// edit.h -- Writer's editing: the selection (a caret and an anchor), typing, deleting, the
// paragraph break, the formats applied to the selection (or to what is typed next), the
// clipboard (the system's plain text, and the document's own piece kept alongside), undo / redo,
// find and replace; the stories (the header and the footer edited in place of the body); the tables
// (inserted, their rows and columns added and deleted, their cells merged and split, their widths and
// lines, a cell's shading; Tab from cell to cell -- from the last one, a new row); the fields (the page,
// the number of pages, the date, a mail merge's), the page numbers, the table of contents. Every change
// is one undoable edit (doc.h) and marks what must be laid out again (layout.h); the view (view.h)
// shows the result.
//
#ifndef _writer_edit_h
#define _writer_edit_h

#include "clipboard.h"
#include "img/imgload.hpp"
#include "layout.h"

namespace wr {

static Doc g_doc;
static Pos g_caret, g_anchor;			// the selection: between them
static bool g_atEnd;				// the caret at the end of the line before (a line's end)
static int g_goalX = -1;			// Up / Down: the column kept (1/64 px from the page's left edge), -1 none
static int g_typeCf = -1;			// the format of what is typed next, set by a command (-1:
						// the place's own)
static bool g_relayout = true;			// the layout is stale
static void (*g_onChange) ();			// the app's: the selection or the text changed (toolbar, ruler)
static Pos g_bodyCaret, g_bodyAnchor;		// the body's selection while a header / footer is edited

static inline bool has_sel () { return g_caret != g_anchor; }
static inline Pos sel_a () { return g_caret < g_anchor ? g_caret : g_anchor; }
static inline Pos sel_b () { return g_caret < g_anchor ? g_anchor : g_caret; }

static void changed () { g_relayout = true; if (g_onChange) g_onChange (); }

// Move the caret (shift: the selection grows from the anchor).
static void set_caret (Pos p, bool extend, bool atEnd = false)
{
	p = doc_clamp (g_doc, p);
	g_caret = p;
	if (!extend) g_anchor = p;
	g_atEnd = atEnd;
	g_typeCf = -1;
	doc_seal (g_doc);
	if (g_onChange) g_onChange ();
}

// The format at the caret (the one typed there): the selection's start's, or the typing one.
static unsigned short caret_cf ()
{
	if (!has_sel () && g_typeCf >= 0) return (unsigned short) g_typeCf;
	Pos a = has_sel () ? sel_a () : g_caret;
	const Para *q = g_doc.p[a.p];
	if (has_sel () && a.o < q->len)
	{
		unsigned short f = q->cf[a.o];
		if (g_doc.fmt[f].obj || g_doc.fmt[f].fld) { CharFmt t = g_doc.fmt[f]; t.obj = 0; t.ow = t.oh = 0; t.fld = 0; f = doc_fmt (g_doc, t); }	// (an image's, a field's: its text's)
		return f;
	}
	return doc_cf_at (g_doc, a);
}

// ... its format (a copy: finding the format may grow the table).
static CharFmt caret_fmt () { unsigned short f = caret_cf (); return g_doc.fmt[f]; }

// ---- an edit's bracket ------------------------------------------------------------------------------
static int g_edN0;
static void ed_begin (int first, int count, int kind)
{
	g_edN0 = g_doc.n;
	doc_begin (g_doc, first, count, g_caret, g_anchor, kind);
}
static void ed_end (int first, int count)
{
	(void) first;
	int nNew = count + (g_doc.n - g_edN0);
	g_anchor = g_caret;
	doc_end_edit (g_doc, nNew, g_caret, g_anchor);
	g_goalX = -1;
	changed ();
}

// ---- the stories --------------------------------------------------------------------------------------
// Edit story s (a header, a footer: the caret at its start; the body: its selection as it was).
static void ed_story (int s)
{
	if (s == g_doc.cur) return;
	if (g_doc.cur == SY_BODY) { g_bodyCaret = g_caret; g_bodyAnchor = g_anchor; }
	doc_seal (g_doc);
	doc_story (g_doc, s);
	if (s == SY_BODY) { g_caret = doc_clamp (g_doc, g_bodyCaret); g_anchor = doc_clamp (g_doc, g_bodyAnchor); }
	else g_caret = g_anchor = mkpos (0, 0);
	g_atEnd = false; g_typeCf = -1; g_goalX = -1;
	changed ();
}

// ---- typing ------------------------------------------------------------------------------------------
// Replace the selection by n characters ('\n': paragraph breaks), in the typing format.
static void ed_type (const unsigned *s, int n, int kind = ED_OTHER, int withCf = -1)
{
	Pos a = sel_a (), b = sel_b ();
	unsigned short cf = withCf >= 0 ? (unsigned short) withCf : caret_cf ();
	if (kind == ED_TYPE && n == 1 && s[0] == ' ' && a.o > 0 && g_doc.p[a.p]->ch[a.o - 1] != ' ') doc_seal (g_doc);
	if (has_sel ()) kind = ED_OTHER;
	ed_begin (a.p, b.p - a.p + 1, kind);
	doc_erase_safe (g_doc, a, b);
	g_caret = doc_insert_raw (g_doc, a, s, n, cf);
	g_atEnd = false;
	if (g_typeCf >= 0 && n > 0) g_typeCf = -1;		// (the typed characters carry it now)
	ed_end (a.p, b.p - a.p + 1);
}

// Enter: a new paragraph -- after a heading at its end, a Normal one; in an empty list item, the
// list ends instead; at the start of a table opening the document (or after another table), a
// paragraph before it.
static void ed_enter ()
{
	Pos a = sel_a ();
	Para *q = g_doc.p[a.p];
	if (!has_sel () && q->len == 0 && q->pf.list != LS_NONE)
	{
		ed_begin (a.p, 1, ED_OTHER);
		q->pf.list = LS_NONE; q->pf.left = (short) wmax (0, q->pf.left - 720 * (q->pf.level + 1)); q->pf.first = 0; q->pf.level = 0;
		q->dirty = true;
		ed_end (a.p, 1);
		return;
	}
	if (!has_sel () && in_table (q) && a.o == 0)
	{
		int ta, tb; table_span (g_doc, a.p, &ta, &tb);
		if (a.p == ta && (ta == 0 || in_table (g_doc.p[ta - 1])))
		{
			ed_begin (ta, 0, ED_OTHER);
			Para *np = para_styled (g_doc, ST_NORMAL);
			np->endCf = doc_cf_at (g_doc, a);
			doc_put (g_doc, ta, np);
			g_caret = mkpos (ta, 0); g_atEnd = false;
			ed_end (ta, 0);
			return;
		}
	}
	bool atEndOfHeading = !has_sel () && a.o == q->len && STYLES[q->pf.style].keepNext;
	unsigned nl = '\n';
	ed_type (&nl, 1, ED_OTHER);
	if (atEndOfHeading)
	{
		Para *r = g_doc.p[g_caret.p];
		para_set_style (g_doc, r, ST_NORMAL);
		r->pf.pageBreak = false;
	}
}

// An empty paragraph beside a table taken out (Backspace after it, Delete before it); the caret at c.
static void drop_para (int p, Pos c)
{
	ed_begin (p, 1, ED_OTHER);
	para_free (doc_take (g_doc, p));
	g_caret = doc_clamp (g_doc, c); g_atEnd = false;
	ed_end (p, 1);
}

// Delete the selection, or the character (word: ctrl) before / after the caret. A table's cells are
// never joined: at a cell's edge nothing goes; after a table, Backspace goes into its last cell (an
// empty paragraph there goes); before one, Delete takes an empty paragraph away.
static Pos word_left (Pos p);
static Pos word_right (Pos p);
static void ed_delete (bool forward, bool word)
{
	if (has_sel ())
	{
		Pos a = sel_a (), b = sel_b ();
		ed_begin (a.p, b.p - a.p + 1, ED_OTHER);
		doc_erase_safe (g_doc, a, b);
		g_caret = a; g_atEnd = false;
		ed_end (a.p, b.p - a.p + 1);
		return;
	}
	Pos c = g_caret;
	Para *q = g_doc.p[c.p];
	if (!forward && c.o == 0)
	{
		if (q->pf.list != LS_NONE || q->pf.first > 0)	// (at a list item's start: out of the list first)
		{
			ed_begin (c.p, 1, ED_OTHER);
			if (q->pf.list != LS_NONE) { q->pf.left = (short) wmax (0, q->pf.left - 720 * (q->pf.level + 1)); q->pf.list = LS_NONE; q->pf.level = 0; }
			q->pf.first = 0; q->dirty = true;
			ed_end (c.p, 1);
			return;
		}
		if (c.p == 0) return;
		Para *pr = g_doc.p[c.p - 1];
		if (in_table (q) && !same_cell (pr, q)) return;			// (a cell's start)
		if (!in_table (q) && in_table (pr))				// (after a table)
		{
			if (q->len == 0 && c.p < g_doc.n - 1) drop_para (c.p, mkpos (c.p - 1, pr->len));
			else set_caret (mkpos (c.p - 1, pr->len), false);
			return;
		}
	}
	if (forward && c.o == q->len)
	{
		if (c.p == g_doc.n - 1) return;
		Para *nx = g_doc.p[c.p + 1];
		if (in_table (q) && !same_cell (q, nx)) return;			// (a cell's end)
		if (!in_table (q) && in_table (nx))				// (before a table)
		{
			if (q->len == 0) drop_para (c.p, mkpos (c.p, 0));
			return;
		}
	}
	Pos a, b;
	if (forward) { a = c; b = word ? word_right (c) : (c.o < q->len ? mkpos (c.p, c.o + 1) : mkpos (c.p + 1, 0)); }
	else { b = c; a = word ? word_left (c) : (c.o > 0 ? mkpos (c.p, c.o - 1) : mkpos (c.p - 1, g_doc.p[c.p - 1]->len)); }
	if (!(a < b)) return;
	ed_begin (a.p, b.p - a.p + 1, a.p == b.p ? (forward ? ED_DELETE : ED_BACKSPACE) : ED_OTHER);
	unsigned short keep = doc_cf_at (g_doc, a.p == c.p && !forward && a.o == 0 && q->len > 0 ? c : a);
	bool emptyAfter = a.p == b.p && a.o == 0 && b.o == g_doc.p[a.p]->len;
	doc_erase_safe (g_doc, a, b);
	if (emptyAfter) g_doc.p[a.p]->endCf = keep;		// (a paragraph emptied: typing goes on alike)
	g_caret = a; g_atEnd = false;
	ed_end (a.p, b.p - a.p + 1);
}

// ---- moving -----------------------------------------------------------------------------------------
static Pos pos_left (Pos p) { if (p.o > 0) p.o--; else if (p.p > 0) { p.p--; p.o = g_doc.p[p.p]->len; } return p; }
static Pos pos_right (Pos p) { if (p.o < g_doc.p[p.p]->len) p.o++; else if (p.p < g_doc.n - 1) { p.p++; p.o = 0; } return p; }
static Pos word_left (Pos p)
{
	if (p.o == 0) return pos_left (p);
	const Para *q = g_doc.p[p.p];
	int o = p.o;
	while (o > 0 && is_space (q->ch[o - 1])) o--;
	if (o > 0 && is_word (q->ch[o - 1])) while (o > 0 && is_word (q->ch[o - 1])) o--;
	else if (o > 0) o--;
	return mkpos (p.p, o);
}
static Pos word_right (Pos p)
{
	const Para *q = g_doc.p[p.p];
	if (p.o >= q->len) return pos_right (p);
	int o = p.o;
	if (is_word (q->ch[o])) while (o < q->len && is_word (q->ch[o])) o++;
	else if (!is_space (q->ch[o])) o++;
	while (o < q->len && is_space (q->ch[o])) o++;
	return mkpos (p.p, o);
}
// The word around a place (a double click).
static void word_at (Pos p, Pos &a, Pos &b)
{
	const Para *q = g_doc.p[p.p];
	int s = p.o, e = p.o;
	if (s < q->len && is_word (q->ch[s])) { while (s > 0 && is_word (q->ch[s - 1])) s--; while (e < q->len && is_word (q->ch[e])) e++; while (e < q->len && q->ch[e] == ' ') e++; }
	else if (s > 0 && is_word (q->ch[s - 1])) { while (s > 0 && is_word (q->ch[s - 1])) s--; }
	else if (s < q->len) e = s + 1;
	a = mkpos (p.p, s); b = mkpos (p.p, e);
}

// ---- formats ----------------------------------------------------------------------------------------
// A character format change: over the selection, or for what is typed next.
static void ed_format (const CfChange &c)
{
	if (!has_sel ())
	{
		g_typeCf = doc_fmt (g_doc, cf_apply (caret_fmt (), c));
		Para *q = g_doc.p[g_caret.p];
		if (q->len == 0) { ed_begin (g_caret.p, 1, ED_OTHER); q->endCf = (unsigned short) g_typeCf; q->dirty = true; ed_end (g_caret.p, 1); g_typeCf = q->endCf; }
		if (g_onChange) g_onChange ();
		return;
	}
	Pos a = sel_a (), b = sel_b (), c0 = g_caret, a0 = g_anchor;
	ed_begin (a.p, b.p - a.p + 1, ED_OTHER);
	doc_format_raw (g_doc, a, b, c);
	g_caret = c0;
	doc_end_edit (g_doc, b.p - a.p + 1, c0, a0);
	g_anchor = a0;
	changed ();
}
// Toggle a flag: set it unless the selection's start already has it.
static void ed_toggle (unsigned short flag)
{
	CfChange c; c.what = CH_FLAGS;
	bool on = (caret_fmt ().flags & flag) != 0;
	c.setFlags = on ? 0 : flag; c.clearFlags = on ? flag : 0;
	ed_format (c);
}

// A paragraph format change over the selection's paragraphs (fn changes one).
static void ed_para (void (*fn) (Para *q, int arg), int arg)
{
	Pos a = sel_a (), b = sel_b (), c0 = g_caret, a0 = g_anchor;
	ed_begin (a.p, b.p - a.p + 1, ED_OTHER);
	for (int p = a.p; p <= b.p; p++) { fn (g_doc.p[p], arg); g_doc.p[p]->dirty = true; }
	doc_end_edit (g_doc, b.p - a.p + 1, c0, a0);
	g_caret = c0; g_anchor = a0;
	g_goalX = -1;
	changed ();
}
static void pf_align (Para *q, int a) { q->pf.align = (unsigned char) a; }
static void pf_style (Para *q, int st) { para_set_style (g_doc, q, st); }
static void pf_list (Para *q, int kind)
{
	if (q->pf.list == kind) { q->pf.left = (short) wmax (0, q->pf.left - 720 * (q->pf.level + 1)); q->pf.first = 0; q->pf.list = LS_NONE; q->pf.level = 0; return; }
	if (q->pf.list == LS_NONE) { q->pf.left = (short) (q->pf.left + 720); q->pf.first = -360; q->pf.level = 0; }
	q->pf.list = (unsigned char) kind;
}
static void pf_indent (Para *q, int dir)
{
	if (q->pf.list != LS_NONE)
	{
		int lv = wclamp (q->pf.level + dir, 0, 5);
		q->pf.left = (short) (q->pf.left + (lv - q->pf.level) * 720);
		q->pf.level = (unsigned char) lv;
		return;
	}
	int step = 720;
	int l = dir > 0 ? (q->pf.left / step + 1) * step : ((q->pf.left + step - 1) / step - 1) * step;
	q->pf.left = (short) wclamp (l, 0, 20 * 720);
}
static void pf_pagebreak (Para *q, int on) { q->pf.pageBreak = on != 0; }

// Several paragraph fields at once (the Paragraph dialog): the ones `mask` names from g_pfSet.
enum { PF_ALIGN = 1, PF_LEFT = 2, PF_RIGHT = 4, PF_FIRST = 8, PF_BEFORE = 16, PF_AFTER = 32, PF_LINE = 64, PF_BREAK = 128, PF_KEEP = 256,
       PF_KEEPLINES = 512, PF_WIDOW = 1024, PF_TABS = 2048 };
static ParaFmt g_pfSet;
static void pf_set (Para *q, int mask)
{
	if (mask & PF_ALIGN) q->pf.align = g_pfSet.align;
	if (mask & PF_LEFT) q->pf.left = g_pfSet.left;
	if (mask & PF_RIGHT) q->pf.right = g_pfSet.right;
	if (mask & PF_FIRST) q->pf.first = g_pfSet.first;
	if (mask & PF_BEFORE) q->pf.before = g_pfSet.before;
	if (mask & PF_AFTER) q->pf.after = g_pfSet.after;
	if (mask & PF_LINE) q->pf.line = g_pfSet.line;
	if (mask & PF_BREAK) q->pf.pageBreak = g_pfSet.pageBreak;
	if (mask & PF_KEEP) q->pf.keepNext = g_pfSet.keepNext;
	if (mask & PF_KEEPLINES) q->pf.keepLines = g_pfSet.keepLines;
	if (mask & PF_WIDOW) q->pf.widow = g_pfSet.widow;
	if (mask & PF_TABS) { q->pf.ntab = g_pfSet.ntab; for (int i = 0; i < g_pfSet.ntab; i++) q->pf.tab[i] = g_pfSet.tab[i]; }
}

// Clear formatting: the selection's characters back to their paragraph style's format.
static void ed_clear_format ()
{
	Pos a = sel_a (), b = sel_b ();
	if (!has_sel ()) { g_typeCf = doc_fmt (g_doc, style_fmt (g_doc, g_doc.p[a.p]->pf.style)); if (g_onChange) g_onChange (); return; }
	Pos c0 = g_caret, a0 = g_anchor;
	ed_begin (a.p, b.p - a.p + 1, ED_OTHER);
	for (int p = a.p; p <= b.p; p++)
	{
		Para *q = g_doc.p[p];
		unsigned short f = doc_fmt (g_doc, style_fmt (g_doc, q->pf.style));
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		for (int k = o0; k < o1; k++)
		{
			const CharFmt &old = g_doc.fmt[q->cf[k]];
			if (old.obj || old.fld) { CharFmt t = g_doc.fmt[f]; t.obj = old.obj; t.ow = old.ow; t.oh = old.oh; t.fld = old.fld; q->cf[k] = doc_fmt (g_doc, t); }
			else q->cf[k] = f;
		}
		if (o1 == q->len) q->endCf = f;
		q->dirty = true;
	}
	doc_end_edit (g_doc, b.p - a.p + 1, c0, a0);
	g_caret = c0; g_anchor = a0;
	changed ();
}

// A page break: the paragraph split at the caret, the second part starting a page (not in a table,
// nor a header).
static void ed_page_break ()
{
	if (in_table (g_doc.p[sel_a ().p]) || g_doc.cur != SY_BODY) return;
	unsigned nl = '\n';
	ed_type (&nl, 1, ED_OTHER);
	Para *q = g_doc.p[g_caret.p];
	q->pf.pageBreak = true; q->dirty = true;
	changed ();
}

// An image at the caret (its file read: PNG and JPEG kept as they are), at its size at 96 dpi --
// narrowed to the text's width. False: not an image.
static bool ed_insert_image (const char *path, int maxTw)
{
	ImgFrames im;
	if (!img_load (path, &im) || im.n < 1) return false;
	for (int i = 1; i < im.n; i++) delete[] im.px[i];		// (a GIF: its first frame)
	if (im.format && im.format[0] == 'B')				// (a BMP: its magenta see-through, as Onyx's icons)
		for (int i = 0; i < im.w * im.h; i++) if ((im.px[0][i] & 0xFFFFFF) == 0xFF00FF) im.px[0][i] = 0x00FF00FF;
	unsigned char *data = 0; unsigned len = 0; bool jpeg = false;
	void *f = kapi_open (path);
	if (f)
	{
		unsigned n = kapi_fsize (f);
		unsigned char hd[4] = { 0, 0, 0, 0 };
		if (n > 8 && kapi_read (f, hd, 4) == 4 && ((hd[0] == 0x89 && hd[1] == 'P') || (hd[0] == 0xFF && hd[1] == 0xD8)))
		{
			jpeg = hd[0] == 0xFF;
			data = new unsigned char[n];
			for (int k = 0; k < 4; k++) data[k] = hd[k];
			if (kapi_read (f, data + 4, n - 4) != (int) (n - 4)) { delete[] data; data = 0; }
			else len = n;
		}
		kapi_close (f);
	}
	int idx = doc_image (g_doc, im.px[0], im.w, im.h, data, len, jpeg);
	CharFmt cf = caret_fmt ();
	cf.obj = idx + 1;
	cf.ow = im.w * 15; cf.oh = im.h * 15;			// (96 dpi)
	if (maxTw > 0 && cf.ow > maxTw) { cf.oh = (int) ((long long) cf.oh * maxTw / cf.ow); cf.ow = maxTw; }
	unsigned c = OBJ_CHAR;
	ed_type (&c, 1, ED_OTHER, doc_fmt (g_doc, cf));
	return true;
}

// ---- fields ---------------------------------------------------------------------------------------------
// A field at the caret (in the caret's format).
static void ed_insert_field (int kind, const char *arg)
{
	CharFmt f = caret_fmt ();
	f.fld = doc_field (g_doc, kind, arg);
	unsigned c = FIELD_CHAR;
	ed_type (&c, 1, ED_OTHER, doc_fmt (g_doc, f));
}

// The page numbers in the footer (or the header): its paragraph with a page field replaced, else one
// added -- aligned; "1", "Page 1", "Page 1 of 3", "1 / 3". Not on the first page: that one gets its
// own (empty) header and footer.
enum { PN_PLAIN, PN_PAGE, PN_PAGE_OF, PN_SLASH };
static void ed_page_numbers (bool footer, int align, int style, bool onFirst)
{
	int s = footer ? SY_FOOTER : SY_HEADER;
	ed_story (s);
	int n0 = g_doc.n, at = -1;
	for (int i = 0; i < g_doc.n && at < 0; i++)
		for (int k = 0; k < g_doc.p[i]->len; k++)
		{
			const CharFmt &f = g_doc.fmt[g_doc.p[i]->cf[k]];
			if (g_doc.p[i]->ch[k] == FIELD_CHAR && f.fld && g_doc.fld[f.fld - 1].kind == FK_PAGE) { at = i; break; }
		}
	bool add = at < 0 && !story_empty (g_doc, s);
	if (at < 0) at = add ? g_doc.n - 1 : 0;
	ed_begin (at, 1, ED_OTHER);
	Para *q = g_doc.p[at];
	if (add) { Para *r = para_styled (g_doc, footer ? ST_FOOTER : ST_HEADER); doc_put (g_doc, at + 1, r); q = r; at++; }
	CharFmt base = style_fmt (g_doc, footer ? ST_FOOTER : ST_HEADER);
	if (q->len) { base = g_doc.fmt[q->cf[0]]; base.fld = 0; base.obj = 0; }
	unsigned short cf = doc_fmt (g_doc, base);
	CharFmt pf = base; pf.fld = doc_field (g_doc, FK_PAGE, ""); unsigned short fPage = doc_fmt (g_doc, pf);
	CharFmt nf = base; nf.fld = doc_field (g_doc, FK_PAGES, ""); unsigned short fPages = doc_fmt (g_doc, nf);
	q->len = 0; q->dirty = true;
	auto put = [&] (const char *t) { for (; *t; t++) { unsigned c = (unsigned char) *t; para_insert (q, q->len, &c, 1, cf); } };
	unsigned F = FIELD_CHAR;
	if (style == PN_PAGE || style == PN_PAGE_OF) put ("Page ");
	para_insert (q, q->len, &F, 1, fPage);
	if (style == PN_PAGE_OF) { put (" of "); para_insert (q, q->len, &F, 1, fPages); }
	if (style == PN_SLASH) { put (" / "); para_insert (q, q->len, &F, 1, fPages); }
	q->pf.align = (unsigned char) align; q->endCf = cf;
	g_caret = mkpos (at, q->len);
	ed_end (add ? at - 1 : at, 1);
	(void) n0;
	g_doc.page.titlePg = !onFirst;
	ed_story (SY_BODY);
}

// ---- the table of contents ----------------------------------------------------------------------------------
static const char *TOC_EMPTY = "No table of contents entries found.";

// The body's table of contents: its entries [*a, *b) (the paragraphs of the TOC styles), its heading
// (-1: none). False: none.
static bool toc_find (int *a, int *b, int *head)
{
	for (int i = 0; i < g_doc.n; i++)
		if (style_toc (g_doc.p[i]->pf.style))
		{
			int j = i;
			while (j < g_doc.n && style_toc (g_doc.p[j]->pf.style)) j++;
			*a = i; *b = j; *head = i > 0 && g_doc.p[i - 1]->pf.style == ST_TOCHEAD ? i - 1 : -1;
			return true;
		}
	return false;
}
static bool is_heading (const Para *q)
{
	if (!style_outline (q->pf.style) || in_table (q)) return false;
	for (int k = 0; k < q->len; k++) if (!is_space (q->ch[k]) && q->ch[k] != OBJ_CHAR && q->ch[k] != FIELD_CHAR) return true;
	return false;
}
// An entry: the heading's text, a tab (a dotted right stop at the text's edge), its page.
static Para *toc_entry (const Para *h, int page)
{
	int lv = style_outline (h->pf.style);
	Para *q = para_styled (g_doc, ST_TOC1 + lv - 1);
	unsigned short f = q->endCf;
	for (int k = 0; k < h->len; k++)
	{
		unsigned c = h->ch[k];
		if (c == OBJ_CHAR || c == FIELD_CHAR) continue;
		if (c == '\t' || c == 0x0B) c = ' ';
		para_insert (q, q->len, &c, 1, f);
	}
	unsigned t = '\t'; para_insert (q, q->len, &t, 1, f);
	char b[12]; int n = 0; int v = page; char r[12]; int j = 0;
	do { r[j++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (j) b[n++] = r[--j];
	for (int i = 0; i < n; i++) { unsigned c = (unsigned char) b[i]; para_insert (q, q->len, &c, 1, f); }
	pf_add_tab (q->pf, g_doc.page.w - g_doc.page.left - g_doc.page.right, TA_RIGHT, TL_DOT);
	return q;
}
// The entries made from the headings (with their pages as laid out) in place of [a, b).
static void toc_fill (int a, int b)
{
	for (int i = b - 1; i >= a; i--) para_free (doc_take (g_doc, i));
	int nh = 0;
	for (int i = 0; i < g_doc.n; i++) if (is_heading (g_doc.p[i])) nh++;
	Para **hs = new Para *[nh + 1]; int *pg = new int[nh + 1];
	nh = 0;
	for (int i = 0; i < g_doc.n; i++)
		if (is_heading (g_doc.p[i])) { hs[nh] = g_doc.p[i]; pg[nh] = (hs[nh]->nln ? hs[nh]->ln[0].page : 0) + g_doc.page.start; nh++; }
	for (int k = 0; k < nh; k++) doc_put (g_doc, a + k, toc_entry (hs[k], pg[k]));
	if (!nh)
	{
		Para *q = para_styled (g_doc, ST_TOC1);
		for (const char *t = TOC_EMPTY; *t; t++) { unsigned c = (unsigned char) *t; para_insert (q, q->len, &c, 1, q->endCf); }
		doc_put (g_doc, a, q);
	}
	delete[] hs; delete[] pg;
}
// Insert a table of contents at the caret (a "Contents" heading, the entries), or bring the one there
// is up to date -- laid out twice: its own place moves the pages.
static void ed_toc (bool update)
{
	ed_story (SY_BODY);
	int a, b, head;
	bool have = toc_find (&a, &b, &head);
	if (update && !have) return;
	int first, count, o = 0;
	if (have && update) { first = head >= 0 ? head : a; count = b - first; }
	else
	{
		Pos c = sel_a ();
		if (in_table (g_doc.p[c.p])) return;
		first = c.p; count = 1; o = c.o;
	}
	ed_begin (first, count, ED_OTHER);
	if (!(have && update))
	{
		int at = o == 0 ? first : first + 1;
		Para *h = para_styled (g_doc, ST_TOCHEAD);
		for (const char *t = "Contents"; *t; t++) { unsigned c = (unsigned char) *t; para_insert (h, h->len, &c, 1, h->endCf); }
		doc_put (g_doc, at, h);
		Para *e = para_styled (g_doc, ST_TOC1);
		doc_put (g_doc, at + 1, e);
		a = at + 1; b = at + 2;
	}
	for (int pass = 0; pass < 2; pass++)
	{
		layout_all ();
		toc_fill (a, b);
		b = a; while (b < g_doc.n && style_toc (g_doc.p[b]->pf.style)) b++;
	}
	g_caret = doc_clamp (g_doc, mkpos (b < g_doc.n ? b : b - 1, 0)); g_atEnd = false;
	ed_end (first, count);
}

// ---- tables ----------------------------------------------------------------------------------------------
// A table's paragraphs by cell while it changes: each cell's own, in order.
struct CellParas
{
	int nr, nc;
	Para ***p; int *n, *cap;
	void init (int r, int c)
	{
		nr = r; nc = c;
		p = new Para **[r * c]; n = new int[r * c]; cap = new int[r * c];
		for (int i = 0; i < r * c; i++) { p[i] = 0; n[i] = cap[i] = 0; }
	}
	void add (int k, Para *q)
	{
		if (n[k] == cap[k]) { int c = cap[k] * 2 + 2; Para **t = new Para *[c]; for (int i = 0; i < n[k]; i++) t[i] = p[k][i]; delete[] p[k]; p[k] = t; cap[k] = c; }
		p[k][n[k]++] = q;
	}
	void move (int to, CellParas &from, int k) { for (int i = 0; i < from.n[k]; i++) add (to, from.p[k][i]); from.n[k] = 0; }
	void drop (int k) { for (int i = 0; i < n[k]; i++) para_free (p[k][i]); n[k] = 0; }
	bool blank (int k) const { return n[k] == 0 || (n[k] == 1 && p[k][0]->len == 0); }
	void done () { for (int i = 0; i < nr * nc; i++) { for (int j = 0; j < n[i]; j++) para_free (p[i][j]); delete[] p[i]; } delete[] p; delete[] n; delete[] cap; }
};

static int g_ta, g_tb;				// (the table being changed: its paragraphs' run)
static ParaFmt g_cellPf; static unsigned short g_cellCf; static bool g_tblBreak;

// The table holding paragraph pi, its run, the cells the selection covers (a rectangle, spans in).
static Table *table_at (int pi, int *ta, int *tb)
{
	if (pi < 0 || pi >= g_doc.n) return 0;
	Table *t = para_table (g_doc, g_doc.p[pi]);
	if (t) table_span (g_doc, pi, ta, tb);
	return t;
}
static bool sel_cells (int *r0, int *c0, int *r1, int *c1)
{
	int ta, tb;
	Pos a = sel_a (), b = sel_b ();
	Table *t = table_at (a.p, &ta, &tb);
	if (!t) return false;
	int pb = wclamp (b.p, ta, tb - 1);
	const Para *qa = g_doc.p[a.p], *qb = g_doc.p[pb];
	int ra = wclamp ((int) qa->pf.row, 0, t->nrows - 1), ca = wclamp ((int) qa->pf.col, 0, t->ncols - 1);
	int rb = wclamp ((int) qb->pf.row, 0, t->nrows - 1), cb = wclamp ((int) qb->pf.col, 0, t->ncols - 1);
	*r0 = wmin (ra, rb); *c0 = wmin (ca, cb);
	*r1 = wmax (ra + tcell (t, ra, ca).rs, rb + tcell (t, rb, cb).rs) - 1;
	*c1 = wmax (ca + tcell (t, ra, ca).cs, cb + tcell (t, rb, cb).cs) - 1;
	*r1 = wmin (*r1, t->nrows - 1); *c1 = wmin (*c1, t->ncols - 1);
	for (bool grown = true; grown; )				// (the cells a span of which reaches in: whole)
	{
		grown = false;
		for (int r = 0; r < t->nrows; r++)
			for (int c = 0; c < t->ncols; c++)
			{
				const TCell &k = tcell (t, r, c);
				if (k.covered) continue;
				int re = r + k.rs - 1, ce = c + k.cs - 1;
				if (re < *r0 || r > *r1 || ce < *c0 || c > *c1) continue;
				if (r < *r0) { *r0 = r; grown = true; }
				if (c < *c0) { *c0 = c; grown = true; }
				if (re > *r1) { *r1 = re; grown = true; }
				if (ce > *c1) { *c1 = ce; grown = true; }
			}
	}
	return true;
}

// A table change begins (the table holding paragraph pi): its paragraphs taken out, by cell.
static bool table_begin (int pi, CellParas &cp, Table **t0)
{
	Table *t = table_at (pi, &g_ta, &g_tb);
	if (!t) return false;
	ed_begin (g_ta, g_tb - g_ta, ED_OTHER);
	const Para *q = g_doc.p[pi];
	g_cellPf = q->pf; g_cellPf.list = LS_NONE; g_cellPf.pageBreak = false; g_cellPf.level = 0;
	g_cellCf = doc_cf_at (g_doc, mkpos (pi, 0));
	g_tblBreak = g_doc.p[g_ta]->pf.pageBreak;
	cp.init (t->nrows, t->ncols);
	for (int k = g_ta; k < g_tb; k++)
	{
		Para *r = doc_take (g_doc, g_ta);
		int rr = wclamp ((int) r->pf.row, 0, t->nrows - 1), cc = wclamp ((int) r->pf.col, 0, t->ncols - 1), orow, ocol;
		cell_owner (t, rr, cc, &orow, &ocol);
		cp.add (orow * t->ncols + ocol, r);
	}
	*t0 = t;
	return true;
}
// ... and ends: the new table's paragraphs put back (an empty one in a cell with none), the caret in
// cell (cr, cc) -- or where it was (keep: the same cells).
static void table_end (Table *t, CellParas &cp, int cr, int cc, bool keep = false)
{
	Pos kc = g_caret;
	int idx = doc_table (g_doc, t);
	int at = g_ta, caret = -1;
	for (int r = 0; r < t->nrows; r++)
		for (int c = 0; c < t->ncols; c++)
		{
			int k = r * t->ncols + c;
			if (tcell (t, r, c).covered) { cp.drop (k); continue; }
			if (cp.n[k] == 0) { Para *e = para_new (); e->pf = g_cellPf; e->endCf = g_cellCf; cp.add (k, e); }
			for (int i = 0; i < cp.n[k]; i++)
			{
				Para *q = cp.p[k][i];
				q->pf.tbl = (short) idx; q->pf.row = (short) r; q->pf.col = (short) c; q->pf.pageBreak = false; q->dirty = true;
				if (r == wclamp (cr, 0, t->nrows - 1) && c <= cc && (caret < 0 || i == 0)) caret = at;
				doc_put (g_doc, at++, q);
			}
			cp.n[k] = 0;
		}
	cp.done ();
	g_doc.p[g_ta]->pf.pageBreak = g_tblBreak;
	g_caret = keep ? doc_clamp (g_doc, kc) : mkpos (caret < 0 ? g_ta : caret, 0); g_atEnd = false;
	ed_end (g_ta, g_tb - g_ta);
}
static void table_props (Table *t, const Table *s)
{
	t->border = s->border; t->bw = s->bw; t->bcolor = s->bcolor; t->header = s->header; t->align = s->align; t->indent = s->indent;
}
static int text_tw () { return g_doc.page.w - g_doc.page.left - g_doc.page.right; }
static void fit_width (Table *t)				// (wider than the text: its columns narrowed alike)
{
	int w = table_width (t), max = text_tw ();
	if (w <= max) return;
	int done = 0;
	for (int c = 0; c < t->ncols; c++) { int nw = (int) ((long long) t->colW[c] * max / w); t->colW[c] = wmax (nw, 120); done += t->colW[c]; }
}

// A table of nr x nc cells at the caret (not in a table): before its paragraph at its start, after it
// at its end (with a paragraph after the table), else the paragraph split there. Its columns share
// the text's width.
static void ed_insert_table (int nr, int nc)
{
	if (g_doc.cur != SY_BODY) ed_story (SY_BODY);
	Pos a = sel_a ();
	Para *q = g_doc.p[a.p];
	if (in_table (q) || nr < 1 || nc < 1) return;
	nc = wmin (nc, (int) MAXCOLS); nr = wmin (nr, 500);
	unsigned short cf = doc_cf_at (g_doc, a);
	ed_begin (a.p, 1, ED_OTHER);
	int at;
	if (a.o == 0) at = a.p;
	else if (a.o == q->len)
	{
		at = a.p + 1;
		if (at >= g_doc.n || in_table (g_doc.p[at]))
		{
			Para *r = para_new (); r->pf = q->pf; r->pf.pageBreak = false; r->pf.list = LS_NONE; r->endCf = cf;
			doc_put (g_doc, at, r);
		}
	}
	else { unsigned nl = '\n'; doc_insert_raw (g_doc, a, &nl, 1, cf); at = a.p + 1; }
	Table *t = table_new (nr, nc, text_tw ());
	int idx = doc_table (g_doc, t);
	ParaFmt pf = style_para (ST_NORMAL); pf.after = 0; pf.line = 100;
	unsigned short ncf = doc_fmt (g_doc, style_fmt (g_doc, ST_NORMAL));
	const CharFmt &c0 = g_doc.fmt[cf];
	CharFmt cc = g_doc.fmt[ncf]; cc.font = c0.font; cc.size = c0.size; cc.color = c0.color;
	ncf = doc_fmt (g_doc, cc);
	for (int r = 0; r < nr; r++)
		for (int c = 0; c < nc; c++)
		{
			Para *np = para_new ();
			np->pf = pf; np->pf.tbl = (short) idx; np->pf.row = (short) r; np->pf.col = (short) c; np->endCf = ncf;
			doc_put (g_doc, at + r * nc + c, np);
		}
	g_caret = mkpos (at, 0); g_atEnd = false;
	ed_end (a.p, 1);
}

// Rows added above the selection's (or below): as many as it covers.
static void ed_table_rows (bool below)
{
	int r0, c0, r1, c1;
	if (!sel_cells (&r0, &c0, &r1, &c1)) return;
	CellParas cp; Table *t0;
	if (!table_begin (sel_a ().p, cp, &t0)) return;
	int k = r1 - r0 + 1, at = below ? r1 + 1 : r0, nc = t0->ncols, ref = below ? r1 : r0;
	Table *t = table_alloc (t0->nrows + k, nc);
	table_props (t, t0);
	for (int c = 0; c < nc; c++) t->colW[c] = t0->colW[c];
	CellParas np; np.init (t->nrows, nc);
	for (int r = 0; r < t0->nrows; r++)
	{
		int nr = r < at ? r : r + k;
		t->rowH[nr] = t0->rowH[r];
		for (int c = 0; c < nc; c++)
		{
			TCell cl = tcell (t0, r, c);
			if (!cl.covered && r < at && r + cl.rs > at) cl.rs = (unsigned char) (cl.rs + k);	// (over the new rows too)
			tcell (t, nr, c) = cl;
			np.move (nr * nc + c, cp, r * nc + c);
		}
	}
	for (int r = at; r < at + k; r++)
		for (int c = 0; c < nc; c++) { TCell &cl = tcell (t, r, c); cl.cs = cl.rs = 1; cl.covered = false; cl.fill = tcell (t0, ref, c).fill; }
	for (int r = 0; r < at; r++)
		for (int c = 0; c < nc; c++)
		{
			const TCell &cl = tcell (t, r, c);
			if (cl.covered || r + cl.rs <= at) continue;
			for (int rr = at; rr < at + k; rr++) for (int cc = c; cc < c + cl.cs && cc < nc; cc++) tcell (t, rr, cc).covered = true;
		}
	cp.done ();
	table_end (t, np, at, c0);
}

// The table (its paragraphs) taken away: the caret in the paragraph after it.
static void ed_table_delete ()
{
	int ta, tb;
	if (!table_at (sel_a ().p, &ta, &tb)) return;
	ed_begin (ta, tb - ta, ED_OTHER);
	for (int k = ta; k < tb; k++) para_free (doc_take (g_doc, ta));
	if (ta >= g_doc.n) doc_put (g_doc, ta, para_styled (g_doc, ST_NORMAL));
	g_caret = mkpos (ta, 0); g_atEnd = false;
	ed_end (ta, tb - ta);
}

// The selection's rows taken away (all of them: the table).
static void ed_table_del_rows ()
{
	int r0, c0, r1, c1;
	if (!sel_cells (&r0, &c0, &r1, &c1)) return;
	int ta, tb; Table *tt = table_at (sel_a ().p, &ta, &tb);
	if (r1 - r0 + 1 >= tt->nrows) { ed_table_delete (); return; }
	CellParas cp; Table *t0;
	if (!table_begin (sel_a ().p, cp, &t0)) return;
	int k = r1 - r0 + 1, nc = t0->ncols;
	Table *t = table_alloc (t0->nrows - k, nc);
	table_props (t, t0);
	for (int c = 0; c < nc; c++) t->colW[c] = t0->colW[c];
	CellParas np; np.init (t->nrows, nc);
	for (int r = 0; r < t0->nrows; r++)
	{
		if (r >= r0 && r <= r1) continue;
		int nr = r < r0 ? r : r - k;
		t->rowH[nr] = t0->rowH[r];
		for (int c = 0; c < nc; c++) { tcell (t, nr, c) = tcell (t0, r, c); np.move (nr * nc + c, cp, r * nc + c); }
	}
	for (int r = 0; r < r0; r++)				// (a cell from above over them: shorter)
		for (int c = 0; c < nc; c++)
		{
			TCell &cl = tcell (t, r, c);
			if (cl.covered || r + cl.rs <= r0) continue;
			int end = r + cl.rs;
			cl.rs = (unsigned char) ((end <= r1 + 1 ? r0 : end - k) - r);
		}
	for (int r = r0; r <= r1; r++)				// (a cell from them over the rows below: their part its own cell)
		for (int c = 0; c < nc; c++)
		{
			const TCell &cl = tcell (t0, r, c);
			if (cl.covered || r + cl.rs <= r1 + 1 || r0 >= t->nrows) continue;
			TCell &n = tcell (t, r0, c);
			n.covered = false; n.cs = cl.cs; n.rs = (unsigned char) (r + cl.rs - (r1 + 1)); n.fill = cl.fill;
		}
	cp.done ();
	table_end (t, np, wmin (r0, t->nrows - 1), c0);
}

// Columns added left of the selection's (or right): as many as it covers, as wide as the one beside;
// the table narrowed to the text's width if it grows past it.
static void ed_table_cols (bool right)
{
	int r0, c0, r1, c1;
	if (!sel_cells (&r0, &c0, &r1, &c1)) return;
	int ta, tb; Table *tt = table_at (sel_a ().p, &ta, &tb);
	int k = c1 - c0 + 1;
	if (tt->ncols + k > MAXCOLS) return;
	CellParas cp; Table *t0;
	if (!table_begin (sel_a ().p, cp, &t0)) return;
	int at = right ? c1 + 1 : c0, nr = t0->nrows, ref = right ? c1 : c0, nc0 = t0->ncols, nc = nc0 + k;
	Table *t = table_alloc (nr, nc);
	table_props (t, t0);
	for (int r = 0; r < nr; r++) t->rowH[r] = t0->rowH[r];
	for (int c = 0; c < nc0; c++) t->colW[c < at ? c : c + k] = t0->colW[c];
	for (int c = at; c < at + k; c++) t->colW[c] = t0->colW[ref];
	CellParas np; np.init (nr, nc);
	for (int r = 0; r < nr; r++)
		for (int c = 0; c < nc0; c++)
		{
			int ncol = c < at ? c : c + k;
			TCell cl = tcell (t0, r, c);
			if (!cl.covered && c < at && c + cl.cs > at) cl.cs = (unsigned char) (cl.cs + k);
			tcell (t, r, ncol) = cl;
			np.move (r * nc + ncol, cp, r * nc0 + c);
		}
	for (int r = 0; r < nr; r++)
		for (int c = at; c < at + k; c++) { TCell &cl = tcell (t, r, c); cl.cs = cl.rs = 1; cl.covered = false; cl.fill = tcell (t0, r, ref).fill; }
	for (int r = 0; r < nr; r++)
		for (int c = 0; c < at; c++)
		{
			const TCell &cl = tcell (t, r, c);
			if (cl.covered || c + cl.cs <= at) continue;
			for (int rr = r; rr < r + cl.rs && rr < nr; rr++) for (int cc = at; cc < at + k; cc++) tcell (t, rr, cc).covered = true;
		}
	fit_width (t);
	cp.done ();
	table_end (t, np, r0, at);
}

// The selection's columns taken away (all of them: the table).
static void ed_table_del_cols ()
{
	int r0, c0, r1, c1;
	if (!sel_cells (&r0, &c0, &r1, &c1)) return;
	int ta, tb; Table *tt = table_at (sel_a ().p, &ta, &tb);
	if (c1 - c0 + 1 >= tt->ncols) { ed_table_delete (); return; }
	CellParas cp; Table *t0;
	if (!table_begin (sel_a ().p, cp, &t0)) return;
	int k = c1 - c0 + 1, nr = t0->nrows, nc0 = t0->ncols, nc = nc0 - k;
	Table *t = table_alloc (nr, nc);
	table_props (t, t0);
	for (int r = 0; r < nr; r++) t->rowH[r] = t0->rowH[r];
	CellParas np; np.init (nr, nc);
	for (int c = 0; c < nc0; c++)
	{
		if (c >= c0 && c <= c1) continue;
		int ncol = c < c0 ? c : c - k;
		t->colW[ncol] = t0->colW[c];
		for (int r = 0; r < nr; r++) { tcell (t, r, ncol) = tcell (t0, r, c); np.move (r * nc + ncol, cp, r * nc0 + c); }
	}
	for (int r = 0; r < nr; r++)
		for (int c = 0; c < c0; c++)
		{
			TCell &cl = tcell (t, r, c);
			if (cl.covered || c + cl.cs <= c0) continue;
			int end = c + cl.cs;
			cl.cs = (unsigned char) ((end <= c1 + 1 ? c0 : end - k) - c);
		}
	for (int r = 0; r < nr; r++)
		for (int c = c0; c <= c1; c++)
		{
			const TCell &cl = tcell (t0, r, c);
			if (cl.covered || c + cl.cs <= c1 + 1 || c0 >= nc) continue;
			TCell &n = tcell (t, r, c0);
			n.covered = false; n.cs = (unsigned char) (c + cl.cs - (c1 + 1)); n.rs = cl.rs; n.fill = cl.fill;
		}
	cp.done ();
	table_end (t, np, r0, wmin (c0, nc - 1));
}

// The selection's cells made one (their paragraphs in it, the empty ones dropped).
static void ed_table_merge ()
{
	int r0, c0, r1, c1;
	if (!sel_cells (&r0, &c0, &r1, &c1) || (r0 == r1 && c0 == c1)) return;
	CellParas cp; Table *t0;
	if (!table_begin (sel_a ().p, cp, &t0)) return;
	Table *t = table_copy (t0);
	int nc = t->ncols, own = r0 * nc + c0;
	for (int r = r0; r <= r1; r++)
		for (int c = c0; c <= c1; c++)
		{
			TCell &cl = tcell (t, r, c);
			int k = r * nc + c;
			if (k != own) { if (cp.blank (k)) cp.drop (k); else { if (cp.blank (own)) cp.drop (own); cp.move (own, cp, k); } }
			cl.covered = k != own; cl.cs = cl.rs = 1;
		}
	tcell (t, r0, c0).cs = (unsigned char) (c1 - c0 + 1); tcell (t, r0, c0).rs = (unsigned char) (r1 - r0 + 1);
	table_end (t, cp, r0, c0);
}

// The cell at the caret split back into the cells it spans.
static void ed_table_split ()
{
	int ta, tb;
	Pos a = sel_a ();
	Table *tt = table_at (a.p, &ta, &tb);
	if (!tt) return;
	const Para *q = g_doc.p[a.p];
	int r = wclamp ((int) q->pf.row, 0, tt->nrows - 1), c = wclamp ((int) q->pf.col, 0, tt->ncols - 1);
	const TCell &k0 = tcell (tt, r, c);
	if (k0.cs <= 1 && k0.rs <= 1) return;
	CellParas cp; Table *t0;
	if (!table_begin (a.p, cp, &t0)) return;
	Table *t = table_copy (t0);
	int cs = k0.cs, rs = k0.rs;
	for (int rr = r; rr < r + rs && rr < t->nrows; rr++)
		for (int cc = c; cc < c + cs && cc < t->ncols; cc++) { TCell &cl = tcell (t, rr, cc); cl.cs = cl.rs = 1; cl.covered = false; cl.fill = tcell (t0, r, c).fill; }
	table_end (t, cp, r, c);
}

// A table's look changed (the Table Properties dialog; a column's border dragged): the new table given.
static void ed_table_set (int pi, Table *nt)
{
	CellParas cp; Table *t0;
	g_caret = sel_a ();
	if (!table_begin (pi, cp, &t0)) { table_free (nt); return; }
	table_end (nt, cp, 0, 0, true);
}

// The selection's cells' shading.
static void ed_table_fill (unsigned color)
{
	int r0, c0, r1, c1;
	if (!sel_cells (&r0, &c0, &r1, &c1)) return;
	CellParas cp; Table *t0;
	g_caret = sel_a ();
	if (!table_begin (g_caret.p, cp, &t0)) return;
	Table *t = table_copy (t0);
	for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) tcell (t, r, c).fill = color;
	table_end (t, cp, r0, c0, true);
}

// The columns (the selection's, else all) as wide as one another.
static void ed_table_even ()
{
	int r0, c0, r1, c1;
	if (!sel_cells (&r0, &c0, &r1, &c1)) return;
	CellParas cp; Table *t0;
	g_caret = sel_a ();
	if (!table_begin (g_caret.p, cp, &t0)) return;
	Table *t = table_copy (t0);
	if (c0 == c1) { c0 = 0; c1 = t->ncols - 1; }
	int w = 0; for (int c = c0; c <= c1; c++) w += t->colW[c];
	for (int c = c0; c <= c1; c++) t->colW[c] = w / (c1 - c0 + 1);
	table_end (t, cp, r0, c0, true);
}

// Column c's right edge (a border dragged) at x twips from the table's left edge: the column and the
// next one share their width (the last one: the table's width changes).
static void ed_table_col_edge (int pi, int c, int xTw)
{
	int ta, tb;
	Table *tt = table_at (pi, &ta, &tb);
	if (!tt || c < 0 || c >= tt->ncols) return;
	int left = 0; for (int k = 0; k < c; k++) left += tt->colW[k];
	int w = xTw - left;
	int maxW = c + 1 < tt->ncols ? tt->colW[c] + tt->colW[c + 1] - 240 : 40000;
	w = wclamp (w, 240, maxW);
	if (w == tt->colW[c]) return;
	CellParas cp; Table *t0;
	g_anchor = g_caret;
	if (!table_begin (pi, cp, &t0)) return;
	Table *t = table_copy (t0);
	if (c + 1 < t->ncols) t->colW[c + 1] = t0->colW[c] + t0->colW[c + 1] - w;
	t->colW[c] = w;
	table_end (t, cp, 0, 0, true);
}

// Tab in a table: the next cell's text selected (back: the previous one's); after the last cell, a
// new row.
static void ed_cell_tab (bool back)
{
	Pos c = g_caret;
	int ca, cb, ta, tb;
	if (!table_at (c.p, &ta, &tb)) return;
	cell_span (g_doc, c.p, &ca, &cb);
	if (back)
	{
		if (ca == ta) return;
		int pa, pb; cell_span (g_doc, ca - 1, &pa, &pb);
		set_caret (mkpos (pa, 0), false); set_caret (mkpos (pb - 1, g_doc.p[pb - 1]->len), true);
		return;
	}
	if (cb >= tb)
	{
		set_caret (mkpos (cb - 1, 0), false);
		ed_table_rows (true);
		int p = g_caret.p;
		while (p > 0 && in_table (g_doc.p[p - 1]) && g_doc.p[p - 1]->pf.row == g_doc.p[p]->pf.row) p--;
		set_caret (mkpos (p, 0), false);
		return;
	}
	int na, nb; cell_span (g_doc, cb, &na, &nb);
	set_caret (mkpos (na, 0), false); set_caret (mkpos (nb - 1, g_doc.p[nb - 1]->len), true);
}

// The selection grown to the cell / the row / the table at the caret.
enum { SEL_CELL, SEL_ROW, SEL_TABLE };
static void ed_table_select (int what)
{
	int ta, tb;
	Pos a = sel_a ();
	Table *t = table_at (a.p, &ta, &tb);
	if (!t) return;
	int p0 = ta, p1 = tb - 1;
	if (what == SEL_CELL) { int ca, cb; cell_span (g_doc, a.p, &ca, &cb); p0 = ca; p1 = cb - 1; }
	else if (what == SEL_ROW)
	{
		int row = g_doc.p[a.p]->pf.row;
		p0 = -1;
		for (int k = ta; k < tb; k++) if (g_doc.p[k]->pf.row == row) { if (p0 < 0) p0 = k; p1 = k; }
	}
	set_caret (mkpos (p0, 0), false);
	set_caret (mkpos (p1, g_doc.p[p1]->len), true);
}

// ---- undo / redo --------------------------------------------------------------------------------------
static void ed_undo_redo (bool redo)
{
	Pos c = g_caret, a = g_anchor, b0 = g_caret, a0 = g_anchor;
	int was = g_doc.cur;
	if (!(redo ? doc_redo (g_doc, c, a) : doc_undo (g_doc, c, a))) return;
	if (was == SY_BODY && g_doc.cur != SY_BODY) { g_bodyCaret = b0; g_bodyAnchor = a0; }
	g_caret = doc_clamp (g_doc, c); g_anchor = doc_clamp (g_doc, a); g_typeCf = -1; g_atEnd = false;
	changed ();
}
static void ed_undo () { ed_undo_redo (false); }
static void ed_redo () { ed_undo_redo (true); }

// ---- the clipboard -----------------------------------------------------------------------------------
// Copy: the plain text to the system's clipboard; the piece of document kept here too -- pasted
// back while the clipboard still holds that text, it keeps its formats (its tables too).
static Doc g_clip; static bool g_clipOk;
static unsigned g_clipHash; static int g_clipLen;
static unsigned text_hash (const char *s, int n) { unsigned h = 2166136261u; for (int i = 0; i < n; i++) h = (h ^ (unsigned char) s[i]) * 16777619u; return h; }

static void ed_copy ()
{
	if (!has_sel ()) return;
	Pos a = sel_a (), b = sel_b ();
	int n = doc_text_len (g_doc, a, b);
	unsigned *u = new unsigned[n + 1];
	n = doc_text (g_doc, a, b, u, n);
	int cap = n * 4 + 1;
	char *s = new char[cap];
	int m = encode_text (u, n, s, cap);
	clip_set_text_n (s, m);
	g_clipHash = text_hash (s, m); g_clipLen = m;
	doc_extract (g_doc, a, b, g_clip); g_clipOk = true;
	delete[] s; delete[] u;
}
static void ed_cut () { if (has_sel ()) { ed_copy (); ed_delete (false, false); } }

static void ed_paste (bool plain = false)
{
	enum { CAP = 4 << 20 };
	char *s = new char[CAP];
	int m = clip_get_text (s, CAP);
	if (m <= 0) { delete[] s; return; }
	if (!plain && g_clipOk && m == g_clipLen && text_hash (s, m) == g_clipHash)
	{
		Pos a = sel_a (), b = sel_b ();
		ed_begin (a.p, b.p - a.p + 1, ED_OTHER);
		doc_erase_safe (g_doc, a, b);
		g_caret = doc_insert_doc_raw (g_doc, a, g_clip);
		g_atEnd = false;
		ed_end (a.p, b.p - a.p + 1);
	}
	else
	{
		unsigned *u = new unsigned[m + 1];
		int n = decode_text (s, m, u, m + 1);
		int k = 0;
		for (int i = 0; i < n; i++) if (u[i] >= 32 || u[i] == '\n' || u[i] == '\t') u[k++] = u[i];
		ed_type (u, k);
		delete[] u;
	}
	delete[] s;
}

// ---- find / replace -------------------------------------------------------------------------------------
static inline unsigned fold (unsigned c)
{
	if (c >= 'A' && c <= 'Z') return c + 32;
	if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
	return c;
}
// The next occurrence of pat (n characters, no '\n') from `from` (forward), wrapping round once.
static bool find_next (const unsigned *pat, int n, bool matchCase, Pos from, Pos &a, Pos &b)
{
	if (n <= 0) return false;
	for (int pass = 0; pass < 2; pass++)
	{
		int p0 = pass == 0 ? from.p : 0, p1 = pass == 0 ? g_doc.n - 1 : from.p;
		for (int p = p0; p <= p1; p++)
		{
			const Para *q = g_doc.p[p];
			int o0 = pass == 0 && p == from.p ? from.o : 0;
			for (int o = o0; o + n <= q->len; o++)
			{
				int k = 0;
				while (k < n && (matchCase ? q->ch[o + k] == pat[k] : fold (q->ch[o + k]) == fold (pat[k]))) k++;
				if (k == n) { a = mkpos (p, o); b = mkpos (p, o + n); return true; }
			}
		}
	}
	return false;
}
// Replace every occurrence: one edit; the count returned.
static int replace_all (const unsigned *pat, int n, const unsigned *rep, int rn, bool matchCase)
{
	if (n <= 0) return 0;
	int count = 0;
	for (int p = 0; p < g_doc.n; p++)
	{
		const Para *q = g_doc.p[p];
		for (int o = 0; o + n <= q->len; o++)
		{
			int k = 0;
			while (k < n && (matchCase ? q->ch[o + k] == pat[k] : fold (q->ch[o + k]) == fold (pat[k]))) k++;
			if (k == n) { count++; o += n - 1; }
		}
	}
	if (!count) return 0;
	ed_begin (0, g_doc.n, ED_OTHER);
	for (int p = 0; p < g_doc.n; p++)
	{
		Para *q = g_doc.p[p];
		for (int o = 0; o + n <= q->len; o++)
		{
			int k = 0;
			while (k < n && (matchCase ? q->ch[o + k] == pat[k] : fold (q->ch[o + k]) == fold (pat[k]))) k++;
			if (k < n) continue;
			unsigned short f = q->cf[o];
			para_erase (q, o, o + n);
			para_insert (q, o, rep, rn, f);
			o += rn - 1;
		}
	}
	g_caret = doc_clamp (g_doc, g_caret);
	ed_end (0, g_doc.n);
	return count;
}

} // namespace wr

#endif
