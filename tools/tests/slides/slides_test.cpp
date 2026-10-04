// slides_test.cpp -- Slides' files on the PC (user/Apps/slides/odp.h): the sample deck read, its slides,
// objects, texts, notes, effects checked; written again and read back (the round trip: the same deck); a
// file of LibreOffice's (when given) read and described.
//
//   slides_test SAMPLE.odp [OTHER.odp [OURS-BY-LO.pptx [LO.pptx]]]        (sh tools/tests/run_slides_test.sh)
//
// The same deck as .pptx: written, read back the same; written by Slides and resaved by LibreOffice, and the sample
// converted to .pptx by LibreOffice: read and described.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#include "wtk/wtk.h"
#include "Apps/slides/pptx.h"
#include "Apps/slides/master.h"
#include <stdio.h>

using namespace sl;

static int g_fail, g_checks;
static void check (bool ok, const char *what) { g_checks++; if (!ok) { g_fail++; printf ("FAIL %s\n", what); } }
static bool read_file (const char *path, unsigned char **b, unsigned *n)
{
	FILE *f = fopen (path, "rb"); if (!f) return false;
	fseek (f, 0, SEEK_END); long l = ftell (f); fseek (f, 0, SEEK_SET);
	*b = (unsigned char *) malloc ((size_t) l + 1); *n = (unsigned) fread (*b, 1, (size_t) l, f); fclose (f);
	return true;
}
// A deck described as text: what must survive a round trip
static void describe (const Deck &d, Buf &o, bool brief = false)
{
	char t[512];
	snprintf (t, sizeof t, "deck %dx%d theme=%s major=%s minor=%s footer=%d '%s' number=%d slides=%d\n", d.sw, d.sh, d.theme.name, d.theme.major, d.theme.minor, d.footer, d.footerText, d.number, d.slides.n); o.puts (t);
	if (!brief)
	{
		for (int i = 0; i < TC_COUNT; i++) { snprintf (t, sizeof t, "%06x ", d.theme.col[i]); o.puts (t); }
		o.puts ("\n");
		for (int i = 0; i < TS_COUNT; i++) { const TextStyle &s = d.style[i]; snprintf (t, sizeof t, "style %d font=%d size=%d col=%08x fl=%u/%u al=%d bu=%d %u %d\n", i, s.cf.font, s.cf.size, s.cf.color, s.cf.flags, s.cf.set, s.pf.align, s.pf.bullet, s.bullet, s.indent); o.puts (t); }
		for (int l = 0; l < LY_COUNT; l++) { snprintf (t, sizeof t, "layout %d '%s' %d\n", l, d.layout[l].name, d.layout[l].ph.n); o.puts (t); }
		snprintf (t, sizeof t, "decor %d\n", d.decor.n); o.puts (t);
	}
	for (int i = 0; i < d.slides.n; i++)
	{
		const Slide &s = *d.slides[i];
		snprintf (t, sizeof t, "slide %d layout=%d objs=%d tr=%d/%d/%d/%d bg=%d/%06x hidden=%d master=%d section='%s' anims=%d\n", i + 1, s.layout, s.obj.n, s.tr.type, s.tr.dir, s.tr.dur, s.tr.after,
			  s.bg.type, s.bg.c1 & 0xFFFFFF, s.hidden, s.masterObjects, s.section, s.anim.n);
		o.puts (t);
		for (int k = 0; k < s.obj.n; k++)
		{
			const Object &b = *s.obj[k];
			Buf tx; b.tb.text_utf8 (tx);
			snprintf (t, sizeof t, "  obj kind=%d sh=%d ph=%d %d,%d %dx%d rot=%d fill=%d/%08x line=%d img=%d tbl=%dx%d chart=%d '%.60s'\n", b.kind, b.shape, b.ph, b.x, b.y, b.w, b.h, b.rot,
				  b.fill.type, b.fill.c1, b.line.type, b.img >= 0, b.tbl ? b.tbl->rows : 0, b.tbl ? b.tbl->cols : 0, b.chart ? b.chart->ncat * 10 + b.chart->nser : 0, tx.str ());
			o.puts (t);
			if (!brief) for (int p = 0; p < b.tb.p.n; p++) { const Para *q = b.tb.p[p]; snprintf (t, sizeof t, "    p al=%d lv=%d bu=%d len=%d", q->pf.align, q->pf.level, q->pf.bullet, q->len); o.puts (t); if (q->len) { snprintf (t, sizeof t, " cf=%d/%d/%08x/%u", q->cf[0].font, q->cf[0].size, q->cf[0].color, q->cf[0].flags); o.puts (t); } o.puts ("\n"); }
			if (b.tbl) { Buf c; b.tbl->cell[b.tbl->cols + 1].text_utf8 (c); snprintf (t, sizeof t, "    cell(1,1)='%s'\n", c.str ()); o.puts (t); }
		}
		Buf n; s.notes.text_utf8 (n);
		snprintf (t, sizeof t, "  notes '%.80s'\n", n.str ()); o.puts (t);
	}
}

int main (int argc, char **argv)
{
	if (argc < 2) { fprintf (stderr, "slides_test SAMPLE.odp [OTHER.odp]\n"); return 2; }
	fnt::init ();
	unsigned char *b; unsigned n;
	if (!read_file (argv[1], &b, &n)) { printf ("FAIL cannot read %s\n", argv[1]); return 1; }
	Deck &d = g_deck;
	check (odp_load (d, b, n), "the sample read");
	free (b);
	check (d.slides.n == 8, "8 slides");
	check (!strcmp (d.theme.name, "Café"), "the theme: Café");
	if (d.slides.n == 8)
	{
		Slide &s3 = *d.slides[2];
		check (!strcmp (d.slides[0]->section, "Introduction") && !strcmp (s3.section, "Results") && !strcmp (d.slides[5]->section, "Next year"), "the sections");
		bool chart = false; for (int i = 0; i < s3.obj.n; i++) if (s3.obj[i]->chart && s3.obj[i]->chart->ncat == 12 && s3.obj[i]->chart->nser == 3 && s3.obj[i]->chart->val[0][11] == 4120.3) chart = true;
		check (chart, "slide 3: the chart and its data");
		check (s3.anim.n == 4 && s3.anim[0].fx == FX_WIPE && s3.anim[1].start == ST_AFTER && s3.anim[3].cls == AC_EMPHASIS, "slide 3: its four effects");
		check (s3.tr.type == TR_WIPE, "slide 3: its transition");
		Buf nt; s3.notes.text_utf8 (nt);
		check (strstr (nt.str (), "Christmas menu") && strstr (nt.str (), "the terrace — next slide"), "slide 3: its notes (UTF-8 kept)");
		bool tbl = false; for (int i = 0; i < d.slides[3]->obj.n; i++) { Object *o = d.slides[3]->obj[i]; if (o->tbl && o->tbl->rows == 7 && o->tbl->cols == 4) { Buf c; o->tbl->at (4, 0).text_utf8 (c); tbl = !strcmp (c.str (), "Chai latte"); } }
		check (tbl, "slide 4: the table");
		bool hasPic = false; for (int i = 0; i < d.slides[4]->obj.n; i++) { Object *o = d.slides[4]->obj[i]; if (o->kind == OB_PICTURE && sl::pic (o->img)) hasPic = true; }
		check (hasPic, "slide 5: the picture");
		Object *t = 0; for (int i = 0; i < d.slides[0]->obj.n; i++) if (d.slides[0]->obj[i]->ph == PH_TITLE) t = d.slides[0]->obj[i];
		Buf tt; if (t) t->tb.text_utf8 (tt);
		check (t && !strcmp (tt.str (), "Onyx Café") && t->tb.p[0]->cf[0].size == 720 && (t->tb.p[0]->cf[0].flags & CF_BOLD), "slide 1: the title, its size, bold");
		check (d.slides[0]->bg.type == FILL_GRADIENT && !d.slides[0]->masterObjects, "slide 1: its gradient, no master objects");
	}
	// the round trip: written, read back: the same description
	Buf a; describe (d, a);
	unsigned zn; unsigned char *z = odp_save (d, &zn);
	check (z != 0, "written");
	Deck *d2 = new Deck;
	check (odp_load (*d2, z, zn), "read back");
	Buf c; describe (*d2, c);
	bool same = a.n == c.n && !memcmp (a.str (), c.str (), a.n);
	check (same, "the round trip: the same deck");
	if (!same)
	{
		// the first difference
		int i = 0; while (i < a.n && i < c.n && a.b[i] == c.b[i]) i++;
		int s0 = i > 200 ? i - 200 : 0;
		printf ("--- before: %.400s\n--- after:  %.400s\n", a.str () + s0, c.str () + s0);
	}
	delete[] z; delete d2;
	// .pptx: written, read back: the same description
	{
		unsigned pn; unsigned char *pz = pptx_save (d, &pn);
		check (pz != 0, "pptx written");
		FILE *f = fopen (getenv ("PPTX_OUT") ? getenv ("PPTX_OUT") : "/tmp/onyx_slides_test.pptx", "wb"); if (f) { fwrite (pz, 1, pn, f); fclose (f); }
		Deck *d3 = new Deck;
		check (pptx_load (*d3, pz, pn), "pptx read back");
		Buf e; describe (*d3, e);
		bool same2 = a.n == e.n && !memcmp (a.str (), e.str (), a.n);
		check (same2, "the pptx round trip: the same deck");
		if (!same2)
		{
			int i = 0; while (i < a.n && i < e.n && a.b[i] == e.b[i]) i++;
			int s0 = i > 300 ? i - 300 : 0;
			printf ("--- odp:  %.600s\n--- pptx: %.600s\n", a.str () + s0, e.str () + s0);
			if (getenv ("DESC_DIR")) { char p[300]; snprintf (p, sizeof p, "%s/a.txt", getenv ("DESC_DIR")); FILE *f = fopen (p, "w"); if (f) { fputs (a.str (), f); fclose (f); } snprintf (p, sizeof p, "%s/b.txt", getenv ("DESC_DIR")); f = fopen (p, "w"); if (f) { fputs (e.str (), f); fclose (f); } }
		}
		delete[] pz; delete d3;
	}
	// a file of LibreOffice's: read, described
	if (argc > 2)
	{
		if (read_file (argv[2], &b, &n))
		{
			Deck *e = new Deck;
			check (odp_load (*e, b, n), "LibreOffice's file read");
			free (b);
			Buf t; describe (*e, t, true);
			printf ("%s", t.str ());
			check (e->slides.n == 8 && e->sh < e->sw, "LibreOffice's: 8 slides, landscape");
			delete e;
		}
	}
	// every shape, effect, transition, chart kind: .pptx written, read back the same
	{
		Deck *x = new Deck; deck_copy (*x, d);
		x->date = true;
		Slide *s = slide_new (*x, LY_BLANK); x->slides.push (s);
		int k = 0;
		for (int sh = 0; sh < SH_COUNT; sh++)
		{
			Object *o = new Object; o->id = x->nextId++; o->kind = OB_SHAPE; o->shape = (signed char) sh;
			o->x = 500 + (sh % 7) * 3800; o->y = 500 + (sh / 7) * 3600; o->w = 3000; o->h = 2600; o->rot = (short) (sh * 13 % 360); o->flipH = sh & 1; o->flipV = (sh & 2) != 0;
			o->fill = sh % 3 ? fill_solid (sh % 2 ? (THEME | (TC_ACC1 + sh % 6)) : 0x123456) : fill_none ();
			if (sh % 5 == 0) { o->fill.type = FILL_GRADIENT; o->fill.c1 = THEME | TC_ACC2; o->fill.c2 = 0xFFEEDD; o->fill.angle = (short) (sh * 30 % 360); }
			o->fill.alpha = (unsigned char) (sh % 4 ? 255 : 128);
			o->line = sh % 4 ? line_solid (sh % 2 ? 0x804020 : (THEME | TC_DK2), 20 + sh) : line_none ();
			if (sh % 4 == 3) { o->line.type = LN_DASH; o->line.head0 = AH_ARROW; o->line.head1 = AH_DOT; } else if (sh % 4 == 2) o->line.type = LN_DOT;
			o->shadow = sh % 3 == 0; o->radius = (short) (sh == SH_ROUND ? 250 : 160);
			o->tb.anchor = (signed char) (sh % 3); o->tb.fit = (signed char) (sh % 3); o->tb.wrap = sh % 2;
			CharFmt f = cf_inherit (); f.size = (short) (100 + sh * 10); f.flags = (unsigned short) (sh & 63); f.set = (unsigned short) (sh % 2 ? 63 : 15); if (f.set == 15) f.flags &= 15;
			if (sh % 3 == 1) f.color = THEME | TC_LT1; if (sh % 4 == 1) f.font = FONT_MAJOR; if (sh % 4 == 2) f.font = (short) x->font_index ("DejaVu Serif");
			ParaFmt pf = pf_inherit (); pf.align = (signed char) (sh % 4); pf.bullet = (signed char) (sh % 3); pf.level = (signed char) (sh % 5); pf.before = (short) (sh * 5); pf.spacing = (short) (90 + sh);
			o->tb.set_text (sh % 2 ? "One\nTwo\nThree" : "Shape", f, &pf);
			s->obj.push (o);
			Anim a; a.obj = o->id; a.cls = (signed char) (k % 3); a.fx = (signed char) (k % FX_COUNT); a.start = (signed char) (k % 3); a.dir = (signed char) (k % 5);
			a.byPara = (sh % 2) && a.cls != AC_EMPHASIS; a.delay = (short) (k * 100 % 1500); a.dur = (short) (a.fx == FX_APPEAR ? 500 : 200 + k * 100 % 2000);	/* (Appear: no duration in the file) */
			s->anim.push (a); k++;
		}
		static const int CT[4] = { CH_BAR, CH_LINE, CH_PIE, CH_AREA };
		for (int c = 0; c < 4; c++)
		{
			Object *o = new Object; o->id = x->nextId++; o->kind = OB_CHART; o->x = 1000 + c * 6000; o->y = 11000; o->w = 5500; o->h = 4000;
			Chart *ch = new Chart; ch->type = CT[c]; ch->ncat = 3; ch->nser = CT[c] == CH_PIE ? 1 : 2; ch->legend = c % 2; ch->labels = c > 1;
			if (c) snprintf (ch->title, sizeof ch->title, "Chart %d & co", c);
			for (int i = 0; i < 3; i++) { snprintf (ch->cat[i], 24, "C<%d>", i); for (int j = 0; j < 2; j++) ch->val[j][i] = i * 1.5 + j; }
			snprintf (ch->ser[0], 32, "S \"one\""); snprintf (ch->ser[1], 32, "S two");
			o->chart = ch; o->tb.ensure (); s->obj.push (o);
		}
		for (int i = 0; i < x->slides.n; i++) { Slide *q = x->slides[i]; q->tr.type = (signed char) (i % TR_COUNT); q->tr.dir = (signed char) (q->tr.type == TR_PUSH || q->tr.type == TR_WIPE || q->tr.type == TR_COVER || q->tr.type == TR_UNCOVER ? 1 + i % 4 : DIR_LEFT);	/* (the others: no direction in the file) */ q->tr.dur = (short) (300 + i * 100); q->tr.after = i % 3 ? -1 : i * 1000; }
		x->slides[1]->hidden = true;
		x->slides[2]->bg.type = FILL_GRADIENT; x->slides[2]->bg.c1 = 0x102030; x->slides[2]->bg.c2 = THEME | TC_ACC3; x->slides[2]->bg.angle = 45;
		Buf A; describe (*x, A);
		unsigned pn; unsigned char *pz = pptx_save (*x, &pn);
		FILE *f = fopen (getenv ("PPTX_STRESS") ? getenv ("PPTX_STRESS") : "/tmp/onyx_slides_stress.pptx", "wb"); if (f) { fwrite (pz, 1, pn, f); fclose (f); }
		Deck *y = new Deck;
		check (pptx_load (*y, pz, pn), "the stress deck read back");
		Buf B; describe (*y, B);
		// its effects too
		for (int i = 0; i < x->slides.n && i < y->slides.n; i++)
			for (int j = 0; j < x->slides[i]->anim.n; j++)
			{
				const Anim &a = x->slides[i]->anim[j];
				char t[96]; snprintf (t, sizeof t, "fx %d/%d %d %d %d %d %d %d %d\n", i, j, a.cls, a.fx, a.start, a.dir, a.byPara, a.delay, a.dur); A.puts (t);
				if (j < y->slides[i]->anim.n) { const Anim &b2 = y->slides[i]->anim[j]; snprintf (t, sizeof t, "fx %d/%d %d %d %d %d %d %d %d\n", i, j, b2.cls, b2.fx, b2.start, b2.dir, b2.byPara, b2.delay, b2.dur); B.puts (t); }
			}
		bool same = A.n == B.n && !memcmp (A.str (), B.str (), A.n);
		check (same, "the stress deck: the same");
		if (!same && getenv ("DESC_DIR"))
		{
			char p[300]; snprintf (p, sizeof p, "%s/sa.txt", getenv ("DESC_DIR")); FILE *g = fopen (p, "w"); if (g) { fputs (A.str (), g); fclose (g); }
			snprintf (p, sizeof p, "%s/sb.txt", getenv ("DESC_DIR")); g = fopen (p, "w"); if (g) { fputs (B.str (), g); fclose (g); }
		}
		delete[] pz; delete x; delete y;
	}
	// the master view: a style through its sample, a layout's placeholder moved (the slides that sat there follow,
	// one moved by hand stays), a text box on a layout -> a placeholder, a shape refused; one Undo for the visit
	{
		Deck *keep = new Deck; deck_copy (*keep, g_deck);
		g_onDone = master_sync; g_accepts = master_accepts;
		Slide *a = slide_new (g_deck, LY_CONTENT), *b2 = slide_new (g_deck, LY_CONTENT);
		g_deck.slides.push (a); g_deck.slides.push (b2);
		Object *bb = 0; for (int i = 0; i < b2->obj.n; i++) if (b2->obj[i]->ph == PH_BODY) bb = b2->obj[i];
		if (bb) bb->y += 77;
		int nslides = g_deck.slides.n, size0 = g_deck.style[TS_TITLE].cf.size;
		int bodyY = g_deck.layout[LY_CONTENT].find (PH_BODY)->y;
		master_open ();
		check (g_master && g_deck.slides.n == 1 + LY_COUNT, "master view: the master and its 8 layouts");
		Object *t = g_deck.slides[0]->by_id (g_mTitle);
		begin_change (); for (int k = 0; t && k < t->tb.p[0]->len; k++) t->tb.p[0]->cf[k].size = 520; done_change ();
		check (g_deck.style[TS_TITLE].cf.size == 520 && t && t->tb.p[0]->cf[0].size == 0, "master view: the title's style from its sample");
		Slide *ls = g_deck.slides[1 + LY_CONTENT]; Object *lb = 0; for (int i = 0; i < ls->obj.n; i++) if (ls->obj[i]->ph == PH_BODY) lb = ls->obj[i];
		g_cur = 1 + LY_CONTENT;
		begin_change (); if (lb) lb->y += 1000; done_change ();
		check (g_deck.layout[LY_CONTENT].find (PH_BODY)->y == bodyY + 1000, "master view: a layout's placeholder moved");
		g_cur = 1 + LY_TITLE_ONLY; int n0 = cur_slide ()->obj.n;
		add_object (make_text_box (1000, 5000, 8000, 3000));
		check (cur_slide ()->obj.n == n0 + 1 && cur_slide ()->obj[n0]->ph == PH_BODY && g_deck.layout[LY_TITLE_ONLY].find (PH_BODY), "master view: a text box on a layout -> a text placeholder");
		g_cur = 1 + LY_BLANK; n0 = cur_slide ()->obj.n;
		add_object (make_shape (SH_STAR5, 1000, 1000, 3000, 3000));
		check (cur_slide ()->obj.n == n0, "master view: a shape on a layout refused");
		master_close ();
		check (!g_master && g_deck.slides.n == nslides, "master view closed: the slides back");
		Object *fa = 0, *fb = 0;
		for (int i = 0; i < a->obj.n; i++) if (a->obj[i]->ph == PH_BODY) fa = a->obj[i];
		for (int i = 0; i < b2->obj.n; i++) if (b2->obj[i]->ph == PH_BODY) fb = b2->obj[i];
		check (fa && fa->y == bodyY + 1000 && fb && fb->y == bodyY + 77, "the slides follow their layout (one moved by hand stays)");
		cmd_undo ();
		check (g_deck.style[TS_TITLE].cf.size == size0 && g_deck.layout[LY_CONTENT].find (PH_BODY)->y == bodyY && g_deck.slides.n == nslides, "one Undo: the master view's changes undone");
		deck_copy (g_deck, *keep); delete keep; undo_clear (); g_onDone = 0; g_accepts = 0;
	}
	// .pptx files of LibreOffice's: ours resaved, the sample converted
	for (int k = 3; k < argc && k < 5; k++)
	{
		if (!read_file (argv[k], &b, &n)) continue;
		Deck *e = new Deck;
		check (pptx_load (*e, b, n), k == 3 ? "LibreOffice's resave of our pptx read" : "LibreOffice's pptx read");
		free (b);
		Buf t; describe (*e, t, true);
		printf ("%s", t.str ());
		check (e->slides.n == 8 && e->sh < e->sw, "LibreOffice's pptx: 8 slides, landscape");
		delete e;
	}
	printf ("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
