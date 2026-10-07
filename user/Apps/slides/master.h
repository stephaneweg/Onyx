//
// master.h -- the master view (View > Master): the master and its layouts edited as slides, with every tool of
// the normal view. The first "slide" is the master: its decorations (the shapes, pictures, lines on every slide),
// its background, and two samples whose formats ARE the text styles -- the title, the body's five levels (a size,
// a colour, a font, bold, the alignment, the spacing, the bullets given to their text become the style's, and
// every slide's text that follows the style changes at once). The next eight are the layouts: their
// placeholders (title, text, picture: where they sit, their box), shown on the master's decorations; a text box
// drawn on a layout becomes a text placeholder, a picture a picture placeholder.
//
// While the view is open, g_deck.slides holds those slides (the deck's own are kept aside); every change is
// written back into the deck (master_sync); closing it puts the slides back -- each slide's placeholder that sat
// where its layout's was follows it (one moved by hand stays) -- and the whole visit is one step of Undo.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_master_h
#define _slides_master_h

#include "editor.h"

namespace sl {

static const char *const MASTER_LEVELS[5] = { "Click to edit the text styles", "Second level", "Third level", "Fourth level", "Fifth level" };
static Vec<Slide *> g_mSlides;			// the deck's slides, kept aside
static int g_mCur;
static Vec<Snap> g_mUndo, g_mRedo;		// the normal view's undo
static Deck *g_mBefore;				// the deck when the view opened (the undo step of the visit)
static Layout g_mOld[LY_COUNT];			// the layouts when the view opened (the slides' placeholders follow them)
static int g_mTitle, g_mBody;			// the samples' ids
static int g_mPrev[2][4];			// the samples' boxes at the last sync (the layouts' that sat there follow)
static void (*g_onRefuse) (const char *msg);	// (main.cpp: a message box)

static void layout_copy (Layout &a, const Layout &b)
{
	for (int i = 0; i < a.ph.n; i++) delete a.ph[i];
	a.ph.clear (); memcpy (a.name, b.name, sizeof a.name);
	for (int i = 0; i < b.ph.n; i++) a.ph.push (obj_copy (b.ph[i]));
}
static bool master_slide () { return g_master && g_cur == 0; }
static int layout_of_cur () { return g_master && g_cur > 0 ? g_cur - 1 : -1; }

// ---- the slides of the view ---------------------------------------------------------------------------------------
static Object *sample (Deck &d, const Object *from, int kind)
{
	Object *o = from ? obj_copy (from) : new Object;
	o->id = d.nextId++; o->kind = OB_TEXT; o->ph = (signed char) kind;
	if (kind == PH_TITLE) o->tb.set_text ("Click to edit the title style", cf_inherit ());
	else
	{
		o->tb.clear ();
		for (int l = 0; l < 5; l++) { Para *q = new Para; q->set_utf8 (MASTER_LEVELS[l], cf_inherit ()); q->pf.level = (signed char) l; o->tb.p.push (q); }
	}
	return o;
}
static void master_build (Deck &d, Vec<Slide *> &out)
{
	Slide *m = new Slide;
	m->layout = LY_CONTENT; m->masterObjects = false; scpy (m->section, "Master", sizeof m->section);
	for (int i = 0; i < d.decor.n; i++) m->obj.push (obj_copy (d.decor[i]));
	Object *t = sample (d, d.layout[LY_CONTENT].find (PH_TITLE), PH_TITLE), *b = sample (d, d.layout[LY_CONTENT].find (PH_BODY), PH_BODY);
	g_mTitle = t->id; g_mBody = b->id;
	g_mPrev[0][0] = t->x; g_mPrev[0][1] = t->y; g_mPrev[0][2] = t->w; g_mPrev[0][3] = t->h;
	g_mPrev[1][0] = b->x; g_mPrev[1][1] = b->y; g_mPrev[1][2] = b->w; g_mPrev[1][3] = b->h;
	m->obj.push (t); m->obj.push (b);
	m->notes.ensure ();
	out.push (m);
	for (int l = 0; l < LY_COUNT; l++)
	{
		Slide *s = new Slide;
		s->layout = l; s->masterObjects = true;
		if (!l) scpy (s->section, "Layouts", sizeof s->section);
		for (int i = 0; i < d.layout[l].ph.n; i++) s->obj.push (obj_copy (d.layout[l].ph[i]));
		s->notes.ensure ();
		out.push (s);
	}
}

// ---- the view's changes -> the deck ------------------------------------------------------------------------------------
// A sample's formats folded into a style (what the text was given becomes the style's); the text then follows it.
static void fold (TextStyle &st, Para *q)
{
	CharFmt f = q->len ? q->cf[0] : q->end;
	if (f.font != FONT_INHERIT) st.cf.font = f.font;
	if (f.size) st.cf.size = f.size;
	if (f.color != AUTO) st.cf.color = f.color;
	st.cf.flags = (unsigned short) ((st.cf.flags & ~f.set) | (f.flags & f.set)); st.cf.set |= f.set;
	ParaFmt &p = q->pf;
	if (p.align != AL_INHERIT) st.pf.align = p.align;
	if (p.bullet != BU_INHERIT) st.pf.bullet = p.bullet;
	if (p.before >= 0) st.pf.before = p.before;
	if (p.after >= 0) st.pf.after = p.after;
	if (p.spacing) st.pf.spacing = p.spacing;
	for (int k = 0; k < q->len; k++) q->cf[k] = cf_inherit ();
	q->end = cf_inherit ();
	signed char lvl = q->pf.level; q->pf = pf_inherit (); q->pf.level = lvl;
}
static bool same_box (const Object &a, const Object &b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
static void master_sync ()
{
	if (!g_master || g_deck.slides.n < 1 + LY_COUNT) return;
	Deck &d = g_deck;
	Slide *m = d.slides[0];
	// the decorations, the background
	for (int i = 0; i < d.decor.n; i++) delete d.decor[i];
	d.decor.clear ();
	for (int i = 0; i < m->obj.n; i++) if (m->obj[i]->ph == PH_NONE) d.decor.push (obj_copy (m->obj[i]));
	if (m->bg.type != FILL_INHERIT) { d.masterBg = m->bg; m->bg.type = FILL_INHERIT; }
	for (int l = 1; l <= LY_COUNT; l++) d.slides[l]->bg.type = FILL_INHERIT;
	// the text styles (the samples' formats)
	Object *t = m->by_id (g_mTitle), *b = m->by_id (g_mBody);
	if (t) for (int i = 0; i < t->tb.p.n; i++) fold (d.style[TS_TITLE], t->tb.p[i]);
	if (b) for (int i = 0; i < b->tb.p.n; i++) fold (d.style[TS_BODY1 + iclamp (b->tb.p[i]->pf.level, 0, 4)], b->tb.p[i]);
	// the master's title and body places: the layouts' placeholders that sat where they were follow them
	for (int k = 0; k < 2; k++)
	{
		Object *now = k ? b : t; if (!now) continue;
		int *pv = g_mPrev[k];
		bool moved = now->x != pv[0] || now->y != pv[1] || now->w != pv[2] || now->h != pv[3];
		for (int l = 1; l <= LY_COUNT && moved; l++)
		{
			Slide *s = d.slides[l];
			for (int i = 0; i < s->obj.n; i++)
			{
				Object *o = s->obj[i];
				if (o->ph != (k ? PH_BODY : PH_TITLE)) continue;
				if (l - 1 == LY_CONTENT || (o->x == pv[0] && o->y == pv[1] && o->w == pv[2] && o->h == pv[3])) { o->x = now->x; o->y = now->y; o->w = now->w; o->h = now->h; }
			}
		}
		pv[0] = now->x; pv[1] = now->y; pv[2] = now->w; pv[3] = now->h;
	}
	// the layouts: their placeholders (their text is only a prompt)
	for (int l = 0; l < LY_COUNT; l++)
	{
		Layout &L = d.layout[l];
		for (int i = 0; i < L.ph.n; i++) delete L.ph[i];
		L.ph.clear ();
		Slide *s = d.slides[1 + l];
		for (int i = 0; i < s->obj.n; i++)
		{
			Object *o = s->obj[i];
			if (o->ph == PH_NONE) continue;
			Object *c = obj_copy (o);
			c->tb.clear (); c->tb.ensure (); c->kind = OB_TEXT; c->img = -1;
			L.ph.push (c);
		}
	}
}
// The deck's slides follow their layouts' new places (a placeholder where its layout's was).
static void follow_layouts (Deck &d, Vec<Slide *> &slides)
{
	for (int i = 0; i < slides.n; i++)
	{
		Slide *s = slides[i];
		const Layout &was = g_mOld[iclamp (s->layout, 0, LY_COUNT - 1)], &now = d.layout[iclamp (s->layout, 0, LY_COUNT - 1)];
		int nth[PH_COUNT] = { 0 };
		for (int k = 0; k < s->obj.n; k++)
		{
			Object *o = s->obj[k];
			if (o->ph == PH_NONE || o->ph >= PH_COUNT) continue;
			int n = nth[(int) o->ph]++;
			const Object *a = was.find (o->ph, n), *b = now.find (o->ph, n);
			if (!a || !b) continue;
			if (same_box (*o, *a)) { o->x = b->x; o->y = b->y; o->w = b->w; o->h = b->h; }
			if (o->tb.anchor == a->tb.anchor) o->tb.anchor = b->tb.anchor;
			if (o->tb.fit == a->tb.fit) o->tb.fit = b->tb.fit;
			bool ins = true; for (int q = 0; q < 4; q++) if (o->tb.inset[q] != a->tb.inset[q]) ins = false;
			if (ins) for (int q = 0; q < 4; q++) o->tb.inset[q] = b->tb.inset[q];
		}
	}
}
// The deck as it would be saved now (the view's changes in it): out gets a copy.
static void master_real_deck (Deck &out)
{
	master_sync ();
	Vec<Slide *> keep; for (int i = 0; i < g_deck.slides.n; i++) keep.push (g_deck.slides[i]);
	g_deck.slides.clear ();
	deck_copy (out, g_deck);
	for (int i = 0; i < g_mSlides.n; i++) out.slides.push (slide_copy (g_mSlides[i]));
	follow_layouts (out, out.slides);
	for (int i = 0; i < keep.n; i++) g_deck.slides.push (keep[i]);
}

// ---- opening, closing -------------------------------------------------------------------------------------------------
static void master_open ()
{
	if (g_master) return;
	end_edit (); g_sel.clear ();
	g_mBefore = new Deck; deck_copy (*g_mBefore, g_deck);
	for (int l = 0; l < LY_COUNT; l++) layout_copy (g_mOld[l], g_deck.layout[l]);
	// the normal view's undo, kept aside
	g_mUndo.clear (); g_mRedo.clear ();
	for (int i = 0; i < g_undo.n; i++) g_mUndo.push (g_undo[i]);
	for (int i = 0; i < g_redo.n; i++) g_mRedo.push (g_redo[i]);
	g_undo.clear (); g_redo.clear (); g_lastKind = -1;
	g_mSlides.clear (); for (int i = 0; i < g_deck.slides.n; i++) g_mSlides.push (g_deck.slides[i]);
	g_mCur = g_cur;
	g_deck.slides.clear ();
	master_build (g_deck, g_deck.slides);
	g_master = true;
	g_cur = 0;
	notify_slides ();
}
static void master_close ()
{
	if (!g_master) return;
	end_edit (); g_sel.clear ();
	master_sync ();
	unsigned changes = g_deck.changes;
	bool changed = changes != g_mBefore->changes;
	for (int i = 0; i < g_deck.slides.n; i++) delete g_deck.slides[i];
	g_deck.slides.clear ();
	for (int i = 0; i < g_mSlides.n; i++) g_deck.slides.push (g_mSlides[i]);
	g_mSlides.clear ();
	follow_layouts (g_deck, g_deck.slides);
	g_master = false;
	g_cur = iclamp (g_mCur, 0, g_deck.slides.n - 1);
	// undo: the normal view's again, the visit one step of it
	for (int i = 0; i < g_undo.n; i++) snap_free (g_undo[i]);
	for (int i = 0; i < g_redo.n; i++) snap_free (g_redo[i]);
	g_undo.clear (); g_redo.clear ();
	for (int i = 0; i < g_mUndo.n; i++) g_undo.push (g_mUndo[i]);
	g_mUndo.clear ();
	if (changed)
	{
		for (int i = 0; i < g_mRedo.n; i++) snap_free (g_mRedo[i]);
		g_mRedo.clear ();
		Snap s; s.d = g_mBefore; s.cur = g_cur; s.edit = 0; s.kind = UK_OTHER; g_undo.push (s);
		g_mBefore = 0;
	}
	else { for (int i = 0; i < g_mRedo.n; i++) g_redo.push (g_mRedo[i]); g_mRedo.clear (); delete g_mBefore; g_mBefore = 0; }
	g_lastKind = -1;
	g_deck.changes = changes;
	notify_slides ();
}

// ---- what the view allows ----------------------------------------------------------------------------------------------
// An object added to a layout: a text box -> a text placeholder, a picture -> a picture placeholder; else refused.
static bool master_accepts (Object *o)
{
	if (!g_master || g_cur == 0) return true;
	Slide *s = cur_slide ();
	if (o->kind == OB_TEXT && o->ph == PH_NONE)
	{
		int bodies = 0; for (int i = 0; s && i < s->obj.n; i++) if (s->obj[i]->ph == PH_BODY || s->obj[i]->ph == PH_BODY2) bodies++;
		o->ph = (signed char) (bodies ? PH_BODY2 : PH_BODY); o->tb.fit = FIT_SHRINK; scpy (o->name, "Text placeholder", sizeof o->name);
		return true;
	}
	if (o->kind == OB_PICTURE)
	{
		o->kind = OB_TEXT; o->ph = PH_PICTURE; o->img = -1; o->crop[0] = o->crop[1] = o->crop[2] = o->crop[3] = 0;
		scpy (o->name, "Picture placeholder", sizeof o->name);
		return true;
	}
	if (g_onRefuse) g_onRefuse ("A layout holds placeholders: a text box becomes a text placeholder, a picture a picture placeholder. Shapes, lines, tables and charts for every slide go on the master (the first slide of the view).");
	return false;
}
// A placeholder of a layout: add one of the kinds the layout lacks (Insert > Placeholder).
static void master_add_placeholder (int kind)
{
	if (!g_master || g_cur == 0) return;
	Slide *s = cur_slide (); if (!s) return;
	if (kind == PH_TITLE) for (int i = 0; i < s->obj.n; i++) if (s->obj[i]->ph == PH_TITLE) return;
	const Layout &L = g_deck.layout[LY_CONTENT];
	const Object *from = L.find (kind == PH_TITLE ? PH_TITLE : PH_BODY);
	Object *o = from ? obj_copy (from) : new Object;
	o->id = g_deck.nextId++; o->kind = OB_TEXT; o->ph = (signed char) kind; o->tb.clear (); o->tb.ensure ();
	if (kind != PH_TITLE) { o->x = g_deck.sw / 4; o->y = g_deck.sh / 3; o->w = g_deck.sw / 2; o->h = g_deck.sh / 3; }
	if (kind == PH_BODY) { int bodies = 0; for (int i = 0; i < s->obj.n; i++) if (s->obj[i]->ph == PH_BODY || s->obj[i]->ph == PH_BODY2) bodies++; if (bodies) o->ph = PH_BODY2; }
	begin_change ();
	s->obj.push (o);
	end_edit (); g_sel.clear (); g_sel.push (o->id);
	done_change (); notify_slides ();
}

} // namespace sl

#endif
