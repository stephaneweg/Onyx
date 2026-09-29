//
// edit.h -- Writer's editing: the selection (a caret and an anchor), typing, deleting, the
// paragraph break, the formats applied to the selection (or to what is typed next), the
// clipboard (the system's plain text, and the document's own piece kept alongside), undo / redo,
// find and replace. Every change is one undoable edit (doc.h) and marks what must be laid out
// again (layout.h); the view (view.h) shows the result.
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
static int g_goalX = -1;			// Up / Down: the column kept (1/64 px), -1 none
static int g_typeCf = -1;			// the format of what is typed next, set by a command (-1:
						// the place's own)
static bool g_relayout = true;			// the layout is stale
static void (*g_onChange) ();			// the app's: the selection or the text changed (toolbar, ruler)

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
		if (g_doc.fmt[f].obj) { CharFmt t = g_doc.fmt[f]; t.obj = 0; t.ow = t.oh = 0; f = doc_fmt (g_doc, t); }	// (an image's: its text's)
		return f;
	}
	return doc_cf_at (g_doc, a);
}

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

// ---- typing ------------------------------------------------------------------------------------------
// Replace the selection by n characters ('\n': paragraph breaks), in the typing format.
static void ed_type (const unsigned *s, int n, int kind = ED_OTHER, int withCf = -1)
{
	Pos a = sel_a (), b = sel_b ();
	unsigned short cf = withCf >= 0 ? (unsigned short) withCf : caret_cf ();
	if (kind == ED_TYPE && n == 1 && s[0] == ' ' && a.o > 0 && g_doc.p[a.p]->ch[a.o - 1] != ' ') doc_seal (g_doc);
	if (has_sel ()) kind = ED_OTHER;
	ed_begin (a.p, b.p - a.p + 1, kind);
	doc_erase_raw (g_doc, a, b);
	g_caret = doc_insert_raw (g_doc, a, s, n, cf);
	g_atEnd = false;
	if (g_typeCf >= 0 && n > 0) g_typeCf = -1;		// (the typed characters carry it now)
	ed_end (a.p, b.p - a.p + 1);
}

// Enter: a new paragraph -- after a heading at its end, a Normal one; in an empty list item, the
// list ends instead.
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

// Delete the selection, or the character (word: ctrl) before / after the caret.
static Pos word_left (Pos p);
static Pos word_right (Pos p);
static void ed_delete (bool forward, bool word)
{
	if (has_sel ())
	{
		Pos a = sel_a (), b = sel_b ();
		ed_begin (a.p, b.p - a.p + 1, ED_OTHER);
		doc_erase_raw (g_doc, a, b);
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
	}
	if (forward && c.o == q->len && c.p == g_doc.n - 1) return;
	Pos a, b;
	if (forward) { a = c; b = word ? word_right (c) : (c.o < q->len ? mkpos (c.p, c.o + 1) : mkpos (c.p + 1, 0)); }
	else { b = c; a = word ? word_left (c) : (c.o > 0 ? mkpos (c.p, c.o - 1) : mkpos (c.p - 1, g_doc.p[c.p - 1]->len)); }
	if (!(a < b)) return;
	ed_begin (a.p, b.p - a.p + 1, a.p == b.p ? (forward ? ED_DELETE : ED_BACKSPACE) : ED_OTHER);
	unsigned short keep = doc_cf_at (g_doc, a.p == c.p && !forward && a.o == 0 && q->len > 0 ? c : a);
	bool emptyAfter = a.p == b.p && a.o == 0 && b.o == g_doc.p[a.p]->len;
	doc_erase_raw (g_doc, a, b);
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
		g_typeCf = doc_fmt (g_doc, cf_apply (g_doc.fmt[caret_cf ()], c));
		Para *q = g_doc.p[g_caret.p];
		if (q->len == 0) { ed_begin (g_caret.p, 1, ED_OTHER); q->endCf = (unsigned short) g_typeCf; q->dirty = true; ed_end (g_caret.p, 1); g_typeCf = q->endCf; }
		if (g_onChange) g_onChange ();
		return;
	}
	Pos a = sel_a (), b = sel_b (), c0 = g_caret, a0 = g_anchor;
	ed_begin (a.p, b.p - a.p + 1, ED_OTHER);
	doc_format_raw (g_doc, a, b, c);
	g_caret = c0;
	int n0 = g_doc.n;
	(void) n0;
	doc_end_edit (g_doc, b.p - a.p + 1, c0, a0);
	g_anchor = a0;
	changed ();
}
// Toggle a flag: set it unless the selection's start already has it.
static void ed_toggle (unsigned short flag)
{
	CfChange c; c.what = CH_FLAGS;
	bool on = (g_doc.fmt[caret_cf ()].flags & flag) != 0;
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
	int l = q->pf.left + dir * step;
	l = dir > 0 ? (q->pf.left / step + 1) * step : ((q->pf.left + step - 1) / step - 1) * step;
	q->pf.left = (short) wclamp (l, 0, 20 * 720);
}
static void pf_pagebreak (Para *q, int on) { q->pf.pageBreak = on != 0; }

// Several paragraph fields at once (the Paragraph dialog): the ones `mask` names from g_pfSet.
enum { PF_ALIGN = 1, PF_LEFT = 2, PF_RIGHT = 4, PF_FIRST = 8, PF_BEFORE = 16, PF_AFTER = 32, PF_LINE = 64, PF_BREAK = 128, PF_KEEP = 256 };
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
		for (int k = o0; k < o1; k++) q->cf[k] = f;
		if (o1 == q->len) q->endCf = f;
		q->dirty = true;
	}
	doc_end_edit (g_doc, b.p - a.p + 1, c0, a0);
	g_caret = c0; g_anchor = a0;
	changed ();
}

// A page break: the paragraph split at the caret, the second part starting a page.
static void ed_page_break ()
{
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
	CharFmt cf = g_doc.fmt[caret_cf ()];
	cf.obj = idx + 1;
	cf.ow = im.w * 15; cf.oh = im.h * 15;			// (96 dpi)
	if (maxTw > 0 && cf.ow > maxTw) { cf.oh = (int) ((long long) cf.oh * maxTw / cf.ow); cf.ow = maxTw; }
	unsigned c = OBJ_CHAR;
	ed_type (&c, 1, ED_OTHER, doc_fmt (g_doc, cf));
	return true;
}

// ---- undo / redo --------------------------------------------------------------------------------------
static void ed_undo () { Pos c, a; if (doc_undo (g_doc, c, a)) { g_caret = doc_clamp (g_doc, c); g_anchor = doc_clamp (g_doc, a); g_typeCf = -1; g_atEnd = false; changed (); } }
static void ed_redo () { Pos c, a; if (doc_redo (g_doc, c, a)) { g_caret = doc_clamp (g_doc, c); g_anchor = doc_clamp (g_doc, a); g_typeCf = -1; g_atEnd = false; changed (); } }

// ---- the clipboard -----------------------------------------------------------------------------------
// Copy: the plain text to the system's clipboard; the piece of document kept here too -- pasted
// back while the clipboard still holds that text, it keeps its formats.
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
		doc_erase_raw (g_doc, a, b);
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
