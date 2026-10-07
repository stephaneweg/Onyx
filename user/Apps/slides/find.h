//
// find.h -- Find and Replace in a deck: every text in order (the slides from a given one, their objects, a table's
// cells, the paragraphs), a match found from a place (wrapping round once), every match replaced (one Undo step).
// The dialog is main.cpp's.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_find_h
#define _slides_find_h

#include "editor.h"

namespace sl {

// Every text of the deck in order: the slides from the current one, their objects (a table: its cells), their paragraphs.
struct FindAt { int slide, obj, r, c, p, o; };
static TextBody *find_body (const FindAt &a)
{
	if (a.slide < 0 || a.slide >= g_deck.slides.n) return 0;
	Slide *s = g_deck.slides[a.slide];
	if (a.obj < 0 || a.obj >= s->obj.n) return 0;
	Object *o = s->obj[a.obj];
	if (o->tbl) return a.r >= 0 && a.r < o->tbl->rows && a.c >= 0 && a.c < o->tbl->cols ? &o->tbl->at (a.r, a.c) : 0;
	return a.r < 0 ? &o->tb : 0;
}
static unsigned fold_ch (unsigned c, bool mc) { if (mc) return c; if (c >= 'A' && c <= 'Z') return c + 32; if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32; return c; }
// The next match at or after a (wrapping round the deck once); false: none
static bool find_next (const unsigned *w, int n, bool mc, FindAt &a)
{
	if (!n || !g_deck.slides.n) return false;
	FindAt start = a;
	for (int pass = 0; pass <= g_deck.slides.n; pass++)
	{
		int si = (start.slide + pass) % g_deck.slides.n;
		Slide *s = g_deck.slides[si];
		for (int oi = pass ? 0 : start.obj; oi < s->obj.n; oi++)
		{
			Object *o = s->obj[oi];
			int cells = o->tbl ? o->tbl->rows * o->tbl->cols : 1;
			int c0 = pass == 0 && oi == start.obj && o->tbl && start.r >= 0 ? start.r * o->tbl->cols + start.c : 0;
			for (int ci = c0; ci < cells; ci++)
			{
				FindAt h; h.slide = si; h.obj = oi; h.r = o->tbl ? ci / o->tbl->cols : -1; h.c = o->tbl ? ci % o->tbl->cols : -1;
				TextBody *tb = find_body (h); if (!tb) continue;
				bool here = pass == 0 && oi == start.obj && ci == c0;
				for (int pi = here ? start.p : 0; pi < tb->p.n; pi++)
				{
					Para *q = tb->p[pi];
					for (int k = here && pi == start.p ? start.o : 0; k + n <= q->len; k++)
					{
						int m = 0; while (m < n && fold_ch (q->ch[k + m], mc) == fold_ch (w[m], mc)) m++;
						if (m == n) { h.p = pi; h.o = k; a = h; return true; }
					}
				}
			}
		}
		if (pass == 0) start.obj = 0;
	}
	return false;
}
// Every match replaced by r (its format: the match's first character's): how many
static int replace_all (const unsigned *w, int n, const unsigned *r, int rn, bool cs)
{
	if (!n) return 0;
	int count = 0;
	end_edit ();
	bool began = false;
	for (int si = 0; si < g_deck.slides.n; si++)
		for (int oi = 0; oi < g_deck.slides[si]->obj.n; oi++)
		{
			Object *o = g_deck.slides[si]->obj[oi];
			int cells = o->tbl ? o->tbl->rows * o->tbl->cols : 1;
			for (int ci = 0; ci < cells; ci++)
			{
				TextBody *tb = o->tbl ? &o->tbl->cell[ci] : &o->tb;
				for (int pi = 0; pi < tb->p.n; pi++)
				{
					Para *q = tb->p[pi];
					for (int k = 0; k + n <= q->len; )
					{
						int m = 0; while (m < n && fold_ch (q->ch[k + m], cs) == fold_ch (w[m], cs)) m++;
						if (m < n) { k++; continue; }
						if (!began) { begin_change (); began = true; }
						CharFmt f = q->cf[k];
						tb_delete (*tb, tpos (pi, k), tpos (pi, k + n));
						if (rn) tb_insert (*tb, tpos (pi, k), r, rn, f);
						q = tb->p[pi];
						k += rn; count++;
					}
				}
			}
		}
	if (began) { done_change (); notify_slides (); }
	return count;
}

} // namespace sl

#endif
