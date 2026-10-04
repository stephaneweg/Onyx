// slides_test.cpp -- Slides' files on the PC (user/Apps/slides/odp.h): the sample deck read, its slides,
// objects, texts, notes, effects checked; written again and read back (the round trip: the same deck); a
// file of LibreOffice's (when given) read and described.
//
//   slides_test SAMPLE.odp [OTHER.odp]        (sh tools/tests/run_slides_test.sh)
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#include "wtk/wtk.h"
#include "Apps/slides/odp.h"
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
	printf ("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
