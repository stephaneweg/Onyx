//
// editor.h -- Slides' editing state and its commands: the deck, the current slide, the objects selected,
// the text being edited (an object's, a table cell's: its caret, its selection), undo / redo (copies of
// the deck), the clipboard (objects, slides; the text and pictures of the system's), the changes every
// command makes (new slide, duplicate, delete, move; insert a text box, a shape, a picture, a table, a
// chart, a line; delete, arrange, align; the formats).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_editor_h
#define _slides_editor_h

#include "render.h"
#include "clipboard.h"
#include "img/pngsave.hpp"

namespace sl {

static Deck g_deck;
static int g_cur;				// the current slide
static Vec<int> g_sel;				// the objects selected (ids)
static int g_edit;				// the object whose text is edited (0: none)
static int g_cellR = -1, g_cellC = -1;		// ... a table's cell
static TPos g_caret, g_anchor;
static float g_goalX = -1;			// (up / down keep the column)
static CharFmt g_typeFmt; static bool g_typeSet;	// a format chosen with no text selected: what is typed next
static char g_path[200];
static unsigned g_saved;
static void (*g_onChange) ();			// the window shows the change (main.cpp)
static void (*g_onSlides) ();			// the slides' list changed (the thumbnails)
static bool g_master;				// the master view (master.h): g_deck.slides holds the master and its layouts
static void (*g_onDone) ();			// after every change (the master view writes it back)
static bool (*g_accepts) (Object *o);		// may this object be added here? (the master view's layouts)

static Slide *cur_slide () { return g_deck.slides.n ? g_deck.slides[iclamp (g_cur, 0, g_deck.slides.n - 1)] : 0; }
static Object *obj_of (int id) { Slide *s = cur_slide (); return s ? s->by_id (id) : 0; }
static bool is_sel (int id) { return g_sel.find (id) >= 0; }
static Object *sel_one () { return g_sel.n == 1 ? obj_of (g_sel[0]) : 0; }
static bool changed_doc () { return g_deck.changes != g_saved; }
static void notify () { if (g_onChange) g_onChange (); }
static void notify_slides () { if (g_onSlides) g_onSlides (); notify (); }

// ---- the text being edited -------------------------------------------------------------------------------------
static TextBody *edit_body ()
{
	Object *o = g_edit ? obj_of (g_edit) : 0;
	if (!o) return 0;
	if (o->tbl && g_cellR >= 0) return &o->tbl->at (iclamp (g_cellR, 0, o->tbl->rows - 1), iclamp (g_cellC, 0, o->tbl->cols - 1));
	return &o->tb;
}
// The edited text's box (hmm, the slide's) and an object standing for it (its size, its insets, its placeholder).
static bool edit_box (Object &stand, float *x, float *y)
{
	Object *o = g_edit ? obj_of (g_edit) : 0;
	if (!o) return false;
	if (o->tbl && g_cellR >= 0)
	{
		Table &t = *o->tbl;
		int rh[64]; table_heights (g_deck, *o, rh);
		float cx = (float) o->x, cy = (float) o->y;
		for (int c = 0; c < g_cellC; c++) cx += t.colW[c];
		for (int r = 0; r < g_cellR; r++) cy += rh[r];
		stand.w = t.colW[g_cellC]; stand.h = rh[g_cellR]; stand.ph = PH_NONE;
		stand.tb.copy_from (t.at (g_cellR, g_cellC)); stand.tb.anchor = AN_MIDDLE;
		*x = cx; *y = cy;
		return true;
	}
	stand.w = o->w; stand.h = o->h; stand.ph = o->ph; stand.tb.copy_from (o->tb); stand.rot = o->rot;
	*x = (float) o->x; *y = (float) o->y;
	return true;
}
static bool has_tsel () { return g_edit && tcmp (g_caret, g_anchor) != 0; }
static TPos tsel_a () { return tmin (g_caret, g_anchor); }
static TPos tsel_b () { return tmax (g_caret, g_anchor); }
static void end_edit () { g_edit = 0; g_cellR = g_cellC = -1; g_typeSet = false; }
static void begin_edit (int id, int r = -1, int c = -1)
{
	g_edit = id; g_cellR = r; g_cellC = c; g_typeSet = false; g_goalX = -1;
	TextBody *tb = edit_body ();
	if (!tb) { g_edit = 0; return; }
	tb->ensure ();
	int lp = tb->p.n - 1;
	g_caret = g_anchor = tpos (lp, tb->p[lp]->len);
	g_sel.clear (); g_sel.push (id);
}

// ---- undo -----------------------------------------------------------------------------------------------------
struct Snap { Deck *d; int cur; int edit; int kind; };
static Vec<Snap> g_undo, g_redo;
enum { UK_OTHER, UK_TYPE, UK_MOVE };
static int g_lastKind = -1, g_lastEdit = -1;
static void snap_free (Snap &s) { delete s.d; }
// Called before a change: the deck as it was kept (typing in the same text: one step for all of it).
static void begin_change (int kind = UK_OTHER)
{
	if (kind == UK_TYPE && g_lastKind == UK_TYPE && g_lastEdit == g_edit) return;
	g_lastKind = kind; g_lastEdit = g_edit;
	Snap s; s.d = new Deck; deck_copy (*s.d, g_deck); s.cur = g_cur; s.edit = g_edit; s.kind = kind;
	g_undo.push (s);
	if (g_undo.n > 60) { snap_free (g_undo[0]); g_undo.erase (0); }
	for (int i = 0; i < g_redo.n; i++) snap_free (g_redo[i]);
	g_redo.clear ();
}
static void done_change () { g_deck.changes++; if (g_onDone) g_onDone (); notify (); }
static void restore (Vec<Snap> &from, Vec<Snap> &to)
{
	if (!from.n) return;
	Snap s = from[from.n - 1]; from.n--;
	Snap now; now.d = new Deck; deck_copy (*now.d, g_deck); now.cur = g_cur; now.edit = g_edit; now.kind = UK_OTHER;
	to.push (now);
	unsigned ch = g_deck.changes;
	deck_copy (g_deck, *s.d); snap_free (s);
	g_deck.changes = ch + 1;
	g_cur = iclamp (s.cur, 0, g_deck.slides.n - 1);
	end_edit (); g_sel.clear ();
	g_lastKind = -1;
	notify_slides ();
}
static void cmd_undo () { restore (g_undo, g_redo); }
static void cmd_redo () { restore (g_redo, g_undo); }
static void undo_clear () { for (int i = 0; i < g_undo.n; i++) snap_free (g_undo[i]); for (int i = 0; i < g_redo.n; i++) snap_free (g_redo[i]); g_undo.clear (); g_redo.clear (); g_lastKind = -1; }

// ---- slides -------------------------------------------------------------------------------------------------------
static void go_slide (int i)
{
	if (!g_deck.slides.n) return;
	i = iclamp (i, 0, g_deck.slides.n - 1);
	if (i == g_cur) return;
	end_edit (); g_sel.clear ();
	g_cur = i;
	g_lastKind = -1;
	notify ();
}
static void cmd_new_slide (int layout = -1)
{
	if (g_master) return;
	begin_change ();
	Slide *c = cur_slide ();
	if (layout < 0) layout = c ? (c->layout == LY_TITLE ? LY_CONTENT : c->layout) : LY_CONTENT;
	Slide *s = slide_new (g_deck, layout);
	if (c) { s->tr = c->tr; s->masterObjects = c->masterObjects; }
	g_deck.slides.insert (g_cur + 1, s);
	end_edit (); g_sel.clear ();
	g_cur++;
	done_change (); notify_slides ();
}
static void cmd_duplicate_slide ()
{
	if (g_master) return;
	Slide *c = cur_slide (); if (!c) return;
	begin_change ();
	Slide *s = slide_copy (c);
	s->section[0] = 0;
	for (int i = 0; i < s->obj.n; i++)			// new ids (the effects follow them)
	{
		int old = s->obj[i]->id, nid = g_deck.nextId++;
		s->obj[i]->id = nid;
		for (int k = 0; k < s->anim.n; k++) if (s->anim[k].obj == old) s->anim[k].obj = nid;
	}
	g_deck.slides.insert (g_cur + 1, s);
	end_edit (); g_sel.clear (); g_cur++;
	done_change (); notify_slides ();
}
static void cmd_delete_slide ()
{
	if (g_master) return;
	if (g_deck.slides.n <= 1) { if (cur_slide ()) { begin_change (); Slide *s = slide_new (g_deck, LY_TITLE); delete g_deck.slides[0]; g_deck.slides[0] = s; end_edit (); g_sel.clear (); done_change (); notify_slides (); } return; }
	begin_change ();
	Slide *s = g_deck.slides[g_cur];
	// a section starting here moves to the next slide
	if (s->section[0] && g_cur + 1 < g_deck.slides.n && !g_deck.slides[g_cur + 1]->section[0]) scpy (g_deck.slides[g_cur + 1]->section, s->section, 48);
	delete s; g_deck.slides.erase (g_cur);
	if (g_cur >= g_deck.slides.n) g_cur = g_deck.slides.n - 1;
	end_edit (); g_sel.clear ();
	done_change (); notify_slides ();
}
static void move_slide (int from, int to)
{
	if (g_master) return;
	if (from == to || from < 0 || to < 0 || from >= g_deck.slides.n || to >= g_deck.slides.n) return;
	begin_change ();
	Slide *s = g_deck.slides[from];
	g_deck.slides.erase (from); g_deck.slides.insert (to, s);
	g_cur = to;
	done_change (); notify_slides ();
}
static void cmd_hide_slide () { if (g_master) return; Slide *s = cur_slide (); if (!s) return; begin_change (); s->hidden = !s->hidden; done_change (); notify_slides (); }
static void set_layout (int l)
{
	if (g_master) return;
	Slide *s = cur_slide (); if (!s) return;
	begin_change ();
	end_edit (); g_sel.clear ();
	slide_apply_layout (g_deck, s, l);
	done_change (); notify_slides ();
}

// ---- objects ------------------------------------------------------------------------------------------------------
static Object *new_object (int kind)
{
	Object *o = new Object;
	o->id = g_deck.nextId++; o->kind = (signed char) kind;
	o->tb.ensure ();
	return o;
}
static void add_object (Object *o, bool editNow = false)
{
	Slide *s = cur_slide (); if (!s) { delete o; return; }
	if (g_accepts && !g_accepts (o)) { delete o; return; }
	begin_change ();
	s->obj.push (o);
	end_edit (); g_sel.clear (); g_sel.push (o->id);
	if (editNow) begin_edit (o->id);
	done_change (); if (g_onSlides) g_onSlides ();
}
// The default box: centred, w x h hmm.
static void centre_box (Object *o, int w, int h) { o->w = w; o->h = h; o->x = (g_deck.sw - w) / 2; o->y = (g_deck.sh - h) / 2; }

static Object *make_text_box (int x, int y, int w, int h)
{
	Object *o = new_object (OB_TEXT);
	o->x = x; o->y = y; o->w = w; o->h = h; o->tb.fit = FIT_GROW;
	scpy (o->name, "Text box", sizeof o->name);
	return o;
}
static Object *make_shape (int shape, int x, int y, int w, int h)
{
	Object *o = new_object (OB_SHAPE);
	o->shape = (signed char) shape; o->x = x; o->y = y; o->w = w; o->h = h;
	o->fill = fill_solid (THEME | TC_ACC1); o->line = line_solid (THEME | TC_DK2, 26); o->line.type = LN_NONE;
	o->tb.anchor = AN_MIDDLE;
	CharFmt f = cf_inherit (); f.color = THEME | TC_LT1;
	ParaFmt pf = pf_inherit (); pf.align = AL_CENTER;
	o->tb.set_text ("", f, &pf);
	scpy (o->name, "Shape", sizeof o->name);
	return o;
}
static Object *make_line (int x0, int y0, int x1, int y1, bool arrow)
{
	Object *o = new_object (OB_LINE);
	o->x = imin (x0, x1); o->y = imin (y0, y1); o->w = abs (x1 - x0); o->h = abs (y1 - y0);
	o->flipH = x1 < x0; o->flipV = y1 < y0;
	o->line = line_solid (THEME | TC_DK2, 53);
	if (arrow) o->line.head1 = AH_ARROW;
	scpy (o->name, arrow ? "Arrow" : "Line", sizeof o->name);
	return o;
}
// A picture from a file's bytes, fitted into the box (or the slide's middle, 60 % of it).
static Object *make_picture (const char *name, const unsigned char *b, unsigned n, const Object *into = 0)
{
	int k = pic_add (name, b, n);
	Picture *pc = pic (k);
	if (!pc) return 0;
	Object *o = new_object (OB_PICTURE);
	o->img = k;
	scpy (o->name, "Picture", sizeof o->name);
	if (into)
	{
		// fill the placeholder: cropped to its shape
		o->x = into->x; o->y = into->y; o->w = into->w; o->h = into->h; o->ph = into->ph;
		double ar = (double) pc->w / pc->h, br = (double) into->w / into->h;
		if (ar > br) { int c = (int) ((1 - br / ar) * 500); o->crop[0] = o->crop[2] = (short) c; }
		else { int c = (int) ((1 - ar / br) * 500); o->crop[1] = o->crop[3] = (short) c; }
	}
	else
	{
		double ar = (double) pc->w / pc->h;
		int w = g_deck.sw * 6 / 10, h = (int) (w / ar);
		if (h > g_deck.sh * 7 / 10) { h = g_deck.sh * 7 / 10; w = (int) (h * ar); }
		centre_box (o, w, h);
	}
	return o;
}
static Object *make_table (int rows, int cols)
{
	Object *o = new_object (OB_TABLE);
	o->tbl = new Table (rows, cols);
	int w = g_deck.sw * 80 / 100;
	for (int c = 0; c < cols; c++) o->tbl->colW[c] = w / cols;
	for (int r = 0; r < rows; r++) o->tbl->rowH[r] = 1000;
	for (int i = 0; i < rows * cols; i++) { o->tbl->cell[i].inset[0] = o->tbl->cell[i].inset[2] = 200; o->tbl->cell[i].inset[1] = o->tbl->cell[i].inset[3] = 100; }
	o->w = w; o->h = rows * 1000;
	o->x = (g_deck.sw - w) / 2; o->y = g_deck.sh * 25 / 100;
	scpy (o->name, "Table", sizeof o->name);
	return o;
}
static Object *make_chart (int type)
{
	Object *o = new_object (OB_CHART);
	Chart *c = o->chart = new Chart;
	c->type = type; c->ncat = 4; c->nser = type == CH_PIE ? 1 : 3;
	static const char *cats[4] = { "Q1", "Q2", "Q3", "Q4" }, *sers[3] = { "Series 1", "Series 2", "Series 3" };
	static const double v[3][4] = { { 4.3, 2.5, 3.5, 4.5 }, { 2.4, 4.4, 1.8, 2.8 }, { 2, 2, 3, 5 } };
	for (int i = 0; i < 4; i++) scpy (c->cat[i], cats[i], 24);
	for (int k = 0; k < 3; k++) { scpy (c->ser[k], sers[k], 32); for (int i = 0; i < 4; i++) c->val[k][i] = v[k][i]; }
	centre_box (o, g_deck.sw * 60 / 100, g_deck.sh * 60 / 100);
	scpy (o->name, "Chart", sizeof o->name);
	return o;
}

static void cmd_delete_objects ()
{
	Slide *s = cur_slide (); if (!s || !g_sel.n) return;
	begin_change ();
	for (int k = 0; k < g_sel.n; k++)
	{
		int i = s->index_of (g_sel[k]);
		if (i < 0) continue;
		Object *o = s->obj[i];
		// an emptied placeholder stays (its prompt shows again)
		if (o->ph != PH_NONE && o->kind == OB_TEXT && !o->tb.empty ()) { o->tb.clear (); o->tb.ensure (); continue; }
		if (o->ph == PH_PICTURE && o->kind == OB_PICTURE) { o->kind = OB_TEXT; o->img = -1; continue; }
		delete o; s->obj.erase (i);
		for (int a = 0; a < s->anim.n; ) if (s->anim[a].obj == g_sel[k]) s->anim.erase (a); else a++;
	}
	g_sel.clear (); end_edit ();
	done_change (); if (g_onSlides) g_onSlides ();
}
// The stacking order: dir +1 forward, -1 backward, +2 to the front, -2 to the back
static void cmd_arrange (int dir)
{
	Slide *s = cur_slide (); if (!s || !g_sel.n) return;
	begin_change ();
	for (int k = 0; k < g_sel.n; k++)
	{
		int i = s->index_of (g_sel[k]); if (i < 0) continue;
		Object *o = s->obj[i];
		int to = dir == 2 ? s->obj.n - 1 : dir == -2 ? 0 : iclamp (i + dir, 0, s->obj.n - 1);
		s->obj.erase (i); s->obj.insert (to, o);
	}
	done_change (); if (g_onSlides) g_onSlides ();
}
// Align: 0 left, 1 centre, 2 right, 3 top, 4 middle, 5 bottom; one object: to the slide, several: to them
static void cmd_align (int how)
{
	Slide *s = cur_slide (); if (!s || !g_sel.n) return;
	int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
	if (g_sel.n == 1) { x0 = 0; y0 = 0; x1 = g_deck.sw; y1 = g_deck.sh; }
	else for (int k = 0; k < g_sel.n; k++) { Object *o = obj_of (g_sel[k]); if (!o) continue; x0 = imin (x0, o->x); y0 = imin (y0, o->y); x1 = imax (x1, o->x + o->w); y1 = imax (y1, o->y + o->h); }
	begin_change ();
	for (int k = 0; k < g_sel.n; k++)
	{
		Object *o = obj_of (g_sel[k]); if (!o) continue;
		switch (how)
		{
		case 0: o->x = x0; break; case 1: o->x = (x0 + x1 - o->w) / 2; break; case 2: o->x = x1 - o->w; break;
		case 3: o->y = y0; break; case 4: o->y = (y0 + y1 - o->h) / 2; break; case 5: o->y = y1 - o->h; break;
		}
	}
	done_change (); if (g_onSlides) g_onSlides ();
}
// Distribute evenly: 0 across, 1 down (three or more)
static void cmd_distribute (int down)
{
	if (g_sel.n < 3) return;
	Vec<Object *> v; for (int k = 0; k < g_sel.n; k++) { Object *o = obj_of (g_sel[k]); if (o) v.push (o); }
	for (int i = 1; i < v.n; i++) for (int j = i; j > 0 && (down ? v[j]->y < v[j - 1]->y : v[j]->x < v[j - 1]->x); j--) { Object *t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
	begin_change ();
	int a = down ? v[0]->y : v[0]->x, b = down ? v[v.n - 1]->y + v[v.n - 1]->h : v[v.n - 1]->x + v[v.n - 1]->w, tot = 0;
	for (int i = 0; i < v.n; i++) tot += down ? v[i]->h : v[i]->w;
	int gap = (b - a - tot) / (v.n - 1), p = a;
	for (int i = 0; i < v.n; i++) { if (down) { v[i]->y = p; p += v[i]->h + gap; } else { v[i]->x = p; p += v[i]->w + gap; } }
	done_change (); if (g_onSlides) g_onSlides ();
}

// ---- the formats ---------------------------------------------------------------------------------------------------
// A character format: over the text selected; with only a caret, what is typed next; objects selected: all their text.
static void apply_format (const FmtChange &ch)
{
	if (g_edit)
	{
		TextBody *tb = edit_body (); if (!tb) return;
		if (!has_tsel ())
		{
			if (!g_typeSet) { g_typeFmt = tb->p[g_caret.p]->fmt_at (g_caret.o); g_typeSet = true; }
			cf_apply (g_typeFmt, ch);
			// an empty paragraph: its end format too (what is shown)
			if (tb->p[g_caret.p]->len == 0) { begin_change (); cf_apply (tb->p[g_caret.p]->end, ch); done_change (); }
			notify ();
			return;
		}
		begin_change ();
		tb_format (*tb, tsel_a (), tsel_b (), ch);
		done_change ();
		return;
	}
	if (!g_sel.n) return;
	begin_change ();
	for (int k = 0; k < g_sel.n; k++)
	{
		Object *o = obj_of (g_sel[k]); if (!o) continue;
		if (o->tbl) for (int i = 0; i < o->tbl->rows * o->tbl->cols; i++) tb_format_all (o->tbl->cell[i], ch);
		else tb_format_all (o->tb, ch);
	}
	done_change ();
}
static void apply_flag (unsigned short flag, bool on) { FmtChange c; c.what = FC_FLAGS; c.flags = on ? flag : 0; c.mask = flag; apply_format (c); }
// The paragraphs touched: the ones of the selection, or every one of the objects selected.
typedef void (*ParaFn) (ParaFmt &pf, int arg);
static void apply_para (ParaFn fn, int arg)
{
	begin_change ();
	if (g_edit)
	{
		TextBody *tb = edit_body ();
		if (tb) for (int p = tsel_a ().p; p <= tsel_b ().p && p < tb->p.n; p++) fn (tb->p[p]->pf, arg);
	}
	else
		for (int k = 0; k < g_sel.n; k++)
		{
			Object *o = obj_of (g_sel[k]); if (!o) continue;
			TextBody *bodies[1] = { &o->tb };
			if (o->tbl) { for (int i = 0; i < o->tbl->rows * o->tbl->cols; i++) for (int p = 0; p < o->tbl->cell[i].p.n; p++) fn (o->tbl->cell[i].p[p]->pf, arg); continue; }
			for (int p = 0; p < bodies[0]->p.n; p++) fn (bodies[0]->p[p]->pf, arg);
		}
	done_change ();
}
static void pf_align (ParaFmt &pf, int a) { pf.align = (signed char) a; }
static void pf_bullet (ParaFmt &pf, int b) { pf.bullet = (signed char) b; }
static void pf_level (ParaFmt &pf, int d) { pf.level = (signed char) iclamp (pf.level + d, 0, 4); }
static void pf_spacing (ParaFmt &pf, int s) { pf.spacing = (short) s; }
// The format the toolbar shows: the caret's, or the first selected object's first character's.
static CharFmt shown_format ()
{
	if (g_edit)
	{
		if (g_typeSet && !has_tsel ())
		{
			Object *o = obj_of (g_edit); TextBody *tb = edit_body ();
			return cf_resolve (g_deck, o && !(o->tbl && g_cellR >= 0) ? o->ph : PH_NONE, tb ? tb->p[g_caret.p]->pf.level : 0, g_typeFmt);
		}
		Object *o = obj_of (g_edit);
		TextBody *tb = edit_body ();
		if (o && tb)
		{
			const Para *q = tb->p[iclamp (tsel_a ().p, 0, tb->p.n - 1)];
			int off = has_tsel () ? iclamp (tsel_a ().o + 1, 0, q->len) : g_caret.o;
			return cf_resolve (g_deck, o->tbl && g_cellR >= 0 ? PH_NONE : o->ph, q->pf.level, q->fmt_at (off));
		}
	}
	Object *o = g_sel.n ? obj_of (g_sel[0]) : 0;
	if (o && o->tb.p.n) return cf_resolve (g_deck, o->ph, o->tb.p[0]->pf.level, o->tb.p[0]->len ? o->tb.p[0]->cf[0] : o->tb.p[0]->end);
	return cf_resolve (g_deck, PH_NONE, 0, cf_inherit ());
}
static ParaFmt shown_para ()
{
	Object *o = g_edit ? obj_of (g_edit) : g_sel.n ? obj_of (g_sel[0]) : 0;
	TextBody *tb = g_edit ? edit_body () : o ? &o->tb : 0;
	if (!o || !tb || !tb->p.n) return pf_resolve (g_deck, PH_NONE, pf_inherit ());
	int p = g_edit ? iclamp (g_caret.p, 0, tb->p.n - 1) : 0;
	return pf_resolve (g_deck, o->tbl && g_cellR >= 0 ? PH_NONE : o->ph, tb->p[p]->pf);
}

// ---- the clipboard ----------------------------------------------------------------------------------------------
static Vec<Object *> g_clipObj; static Vec<Slide *> g_clipSlides; static int g_clipKind;	// 1 objects, 2 slides
static unsigned g_clipStamp;
static void clip_clear_own () { for (int i = 0; i < g_clipObj.n; i++) delete g_clipObj[i]; g_clipObj.clear (); for (int i = 0; i < g_clipSlides.n; i++) delete g_clipSlides[i]; g_clipSlides.clear (); }
static void text_selected_utf8 (Buf &o)
{
	TextBody *tb = edit_body (); if (!tb || !has_tsel ()) return;
	TPos a = tsel_a (), b = tsel_b ();
	for (int p = a.p; p <= b.p; p++)
	{
		const Para *q = tb->p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		for (int i = o0; i < o1; i++) o.putu (q->ch[i]);
		if (p < b.p) o.put ('\n');
	}
}
static void copy_objects ()
{
	clip_clear_own ();
	Slide *s = cur_slide (); if (!s) return;
	for (int i = 0; i < s->obj.n; i++) if (is_sel (s->obj[i]->id)) g_clipObj.push (obj_copy (s->obj[i]));
	g_clipKind = 1;
	// the text of them for other apps
	Buf t; for (int i = 0; i < g_clipObj.n; i++) { if (t.n) t.put ('\n'); g_clipObj[i]->tb.text_utf8 (t); }
	clip_set_text (t.n ? t.str () : " ");
	char b[16]; clip_get_text (b, sizeof b); g_clipStamp = 0;
}

} // namespace sl

#endif
