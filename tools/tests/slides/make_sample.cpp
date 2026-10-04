// make_sample.cpp -- Slides' sample presentation, made with Slides' own model and saved by its .odp writer:
// "Onyx Café -- 2026, the year in review" (Sheet's sample figures, sdcard/docs/cafe-2026.xlsx; a picture of
// sdcard/docs/pictures), the deck of the mock-ups (docs/slides). Built for the PC over the stand-in kernel:
//
//   sh tools/tests/run_slides_test.sh     (makes it, then reads it back: the round trip checked)
//   make_sample OUT.odp                   (SIM_SD: the card, for the fonts and the picture)
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#include "wtk/wtk.h"
#include "Apps/slides/odp.h"
#include <stdio.h>

using namespace sl;

static CharFmt F (unsigned color = AUTO, int size = 0, unsigned flags = 0, short font = FONT_INHERIT)
{
	CharFmt f = cf_inherit (); f.color = color; f.size = (short) size; f.flags = (unsigned short) flags; f.set = (unsigned short) (flags ? flags : 0); f.font = font;
	if (flags & CF_BOLD) f.set |= CF_BOLD;
	return f;
}
static Object *ph (Slide *s, int kind) { for (int i = 0; i < s->obj.n; i++) if (s->obj[i]->ph == kind) return s->obj[i]; return 0; }
static void put (Object *o, const char *text, CharFmt f = F (), int align = AL_INHERIT, int bullet = BU_INHERIT)
{
	ParaFmt pf = pf_inherit (); pf.align = (signed char) align; pf.bullet = (signed char) bullet;
	o->tb.set_text (text, f, &pf);
}
static void add_para (Object *o, const char *text, CharFmt f, int align = AL_INHERIT, int level = 0, int bullet = BU_INHERIT, int before = -1)
{
	Para *q = new Para; q->set_utf8 (text, f);
	q->pf.align = (signed char) align; q->pf.level = (signed char) level; q->pf.bullet = (signed char) bullet; q->pf.before = (short) before;
	if (o->tb.p.n == 1 && o->tb.p[0]->len == 0) { delete o->tb.p[0]; o->tb.p.clear (); }
	o->tb.p.push (q);
}
static Object *box (Slide *s, int x, int y, int w, int h)
{
	Object *o = new Object; o->id = g_deck.nextId++; o->kind = OB_TEXT; o->x = x; o->y = y; o->w = w; o->h = h; o->tb.ensure ();
	s->obj.push (o);
	return o;
}
static Object *shape (Slide *s, int sh, int x, int y, int w, int h, Fill f)
{
	Object *o = new Object; o->id = g_deck.nextId++; o->kind = OB_SHAPE; o->shape = (signed char) sh; o->x = x; o->y = y; o->w = w; o->h = h; o->fill = f; o->tb.anchor = AN_MIDDLE; o->tb.ensure ();
	s->obj.push (o);
	return o;
}
static Anim fx (int obj, int cls, int effect, int start, int dur, int dir = DIR_DOWN)
{
	Anim a; a.obj = obj; a.cls = (signed char) cls; a.fx = (signed char) effect; a.start = (signed char) start; a.dir = (signed char) dir; a.byPara = false; a.delay = 0; a.dur = (short) dur;
	return a;
}
static void notes (Slide *s, const char *t) { s->notes.set_text (t, cf_inherit ()); }
static bool read_file (const char *path, unsigned char **b, unsigned *n)
{
	FILE *f = fopen (path, "rb"); if (!f) return false;
	fseek (f, 0, SEEK_END); long l = ftell (f); fseek (f, 0, SEEK_SET);
	*b = (unsigned char *) malloc ((size_t) l); *n = (unsigned) fread (*b, 1, (size_t) l, f); fclose (f);
	return true;
}

int main (int argc, char **argv)
{
	if (argc < 2) { fprintf (stderr, "make_sample OUT.odp\n"); return 2; }
	if (!fnt::init ()) { fprintf (stderr, "no fonts (SIM_SD?)\n"); return 1; }
	Deck &d = g_deck;
	deck_new (d, 0);
	d.slides.clear ();
	scpy (d.footerText, "Onyx Café  ·  2026, the year in review", sizeof d.footerText); d.footer = true;
	const unsigned TEAL = THEME | TC_ACC1, PEACH = THEME | TC_ACC2, DARK = THEME | TC_DK2, PALE = THEME | TC_LT2, MUTED = 0x707C84, WHITE = THEME | TC_LT1;
	int W = d.sw, H = d.sh;

	// 1. the title
	Slide *s = slide_new (d, LY_TITLE); d.slides.push (s);
	scpy (s->section, "Introduction", sizeof s->section);
	s->masterObjects = false;
	s->bg.type = FILL_GRADIENT; s->bg.c1 = 0x143A48; s->bg.c2 = 0x347C8E; s->bg.angle = 90; s->bg.alpha = 255;
	for (int k = 0; k < 3; k++)		// the waves: translucent ellipses low on the slide
	{
		static const unsigned c[3] = { 0xF0A86E, 0x5AA0B0, 0x10303C };
		Fill f = fill_solid (c[k]); f.alpha = (unsigned char) (k == 1 ? 200 : 230);
		Object *w = shape (s, SH_ELLIPSE, -W / 5 + k * W / 9, H * 72 / 100 + k * 900, W * 3 / 2, H * 60 / 100, f);
		w->tb.clear (); w->tb.ensure (); scpy (w->name, "Wave", sizeof w->name);
		// (under the text: moved to the bottom of the order)
		s->obj.erase (s->obj.n - 1); s->obj.insert (0, w);
	}
	for (int k = 0; k < 3; k++)
	{
		static const int cx[3] = { 78, 88, 71 }, cy[3] = { 20, 35, 39 }, r[3] = { 1100, 560, 320 };
		Fill f = fill_solid (0xFFFFFF); f.alpha = 26;
		Object *c = shape (s, SH_ELLIPSE, W * cx[k] / 100 - r[k], H * cy[k] / 100 - r[k], 2 * r[k], 2 * r[k], f);
		c->tb.clear (); c->tb.ensure (); scpy (c->name, "Bubble", sizeof c->name);
		s->obj.erase (s->obj.n - 1); s->obj.insert (3, c);
	}
	Object *t = ph (s, PH_TITLE); put (t, "Onyx Café", F (0xFFFFFF, 720, CF_BOLD)); t->y = H * 22 / 100; t->h = H * 26 / 100;
	Object *st = ph (s, PH_SUBTITLE); st->y = H * 49 / 100; st->h = H * 22 / 100;
	put (st, "2026, the year in review", F (0xFFDEC0, 320));
	add_para (st, "Board meeting  ·  14 January 2027", F (0xDCECF0, 180), AL_INHERIT, 0, BU_INHERIT, 240);
	s->tr.type = TR_FADE; s->tr.dur = 800;
	notes (s, "Welcome. Twenty minutes: the figures, the best sellers, the terrace, and what we plan for 2027.");

	// 2. the agenda
	s = slide_new (d, LY_TWO); d.slides.push (s);
	put (ph (s, PH_TITLE), "Agenda");
	Object *b = ph (s, PH_BODY);
	b->tb.clear ();
	static const char *items[4][2] = { { "Takings in 2026", "84,455 € — the best year so far" }, { "Best sellers", "what sold, what did not" },
					   { "The new terrace", "opened in June" }, { "Plans for 2027", "four steps, one per quarter" } };
	for (int k = 0; k < 4; k++) { add_para (b, items[k][0], F (AUTO, 280, CF_BOLD), AL_INHERIT, 0, BU_NUMBER, k ? 220 : 0); add_para (b, items[k][1], F (MUTED, 200), AL_INHERIT, 1, BU_NONE, 20); }
	b->w = W * 52 / 100;
	Object *b2 = ph (s, PH_BODY2);
	for (int i = 0; i < s->obj.n; i++) if (s->obj[i] == b2) { delete b2; s->obj.erase (i); break; }
	Object *panel = shape (s, SH_ROUND, W * 64 / 100, H * 22 / 100, W * 30 / 100, H * 64 / 100, fill_solid (PALE));
	panel->radius = 90; scpy (panel->name, "Panel", sizeof panel->name);
	for (int k = 0; k < 5; k++)
	{
		Fill f = fill_solid (k % 2 ? PEACH : TEAL); f.alpha = (unsigned char) (220 - k * 25);
		Object *bar = shape (s, SH_ROUND, W * 67 / 100, H * (29 + k * 10) / 100, W * (23 - k * 3) / 100, H * 4 / 100, f);
		bar->radius = 500; scpy (bar->name, "Bar", sizeof bar->name);
	}
	s->tr.type = TR_PUSH; s->tr.dir = DIR_LEFT; s->tr.dur = 700;
	notes (s, "Four parts. Questions at the end, please.");

	// 3. takings by month: the chart and its callout
	s = slide_new (d, LY_TITLE_ONLY); d.slides.push (s);
	scpy (s->section, "Results", sizeof s->section);
	put (ph (s, PH_TITLE), "Takings by month");
	Object *c = new Object; c->id = d.nextId++; c->kind = OB_CHART; c->x = W * 5 / 100; c->y = H * 25 / 100; c->w = W * 59 / 100; c->h = H * 66 / 100;
	c->chart = new Chart; Chart &ch = *c->chart; ch.type = CH_COLUMN; ch.ncat = 12; ch.nser = 3; scpy (ch.title, "Takings by month (€)", sizeof ch.title);
	static const char *M[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static const double COF[12] = { 3120.5, 2980.25, 3410, 3562.75, 3890.4, 4210, 4480.6, 3950.2, 3720, 3641.5, 3380.9, 4120.3 };
	static const double TEA[12] = { 1240.2, 1310, 1150.5, 980.75, 870, 760.3, 690.1, 720, 910.45, 1120, 1290.8, 1420.6 };
	static const double PAS[12] = { 2050, 1920.4, 2180.7, 2240, 2360.2, 2410.9, 2520.5, 2130, 2290.6, 2310, 2240.35, 2870.25 };
	for (int i = 0; i < 12; i++) { scpy (ch.cat[i], M[i], 24); ch.val[0][i] = COF[i]; ch.val[1][i] = TEA[i]; ch.val[2][i] = PAS[i]; }
	scpy (ch.ser[0], "Coffee", 32); scpy (ch.ser[1], "Tea", 32); scpy (ch.ser[2], "Pastries", 32);
	scpy (c->name, "Chart", sizeof c->name);
	s->obj.push (c);
	Object *co = shape (s, SH_ROUND, W * 69 / 100, H * 25 / 100, W * 25 / 100, H * 58 / 100, fill_solid (PALE));
	co->radius = 80; co->tb.anchor = AN_TOP; co->tb.inset[0] = 600; co->tb.inset[1] = 500; scpy (co->name, "Callout", sizeof co->name);
	co->tb.clear ();
	add_para (co, "84,455 €", F (DARK, 360, CF_BOLD), AL_LEFT, 0, BU_NONE);
	add_para (co, "takings in 2026", F (MUTED, 160), AL_LEFT, 0, BU_NONE, 0);
	add_para (co, "▲ +31.2 %", F (0x288246, 240, CF_BOLD), AL_LEFT, 0, BU_NONE, 200);
	add_para (co, "over 2025", F (MUTED, 160), AL_LEFT, 0, BU_NONE, 0);
	add_para (co, "Best month", F (AUTO, 160, CF_BOLD), AL_LEFT, 0, BU_NONE, 260);
	add_para (co, "December: 8,411 €", F (AUTO, 160), AL_LEFT, 0, BU_NONE, 0);
	Object *bar = shape (s, SH_ROUND, W * 69 / 100, H * 25 / 100, 260, H * 58 / 100, fill_solid (PEACH)); bar->radius = 500; scpy (bar->name, "Accent", sizeof bar->name);
	s->anim.push (fx (c->id, AC_ENTRANCE, FX_WIPE, ST_CLICK, 800, DIR_DOWN));
	s->anim.push (fx (co->id, AC_ENTRANCE, FX_ZOOM, ST_AFTER, 500));
	s->anim.push (fx (bar->id, AC_ENTRANCE, FX_FADE, ST_WITH, 500));
	s->anim.push (fx (co->id, AC_EMPHASIS, FX_PULSE, ST_CLICK, 600));
	s->tr.type = TR_WIPE; s->tr.dir = DIR_LEFT; s->tr.dur = 700;
	notes (s, "Say first: the best year since we opened (2019). Thank the team.\nPoint to December: the Christmas menu (+22 % on the month).\nThen the summer: the terrace — next slide.");

	// 4. best sellers: a table
	s = slide_new (d, LY_TITLE_ONLY); d.slides.push (s);
	put (ph (s, PH_TITLE), "Best sellers");
	Object *tb = new Object; tb->id = d.nextId++; tb->kind = OB_TABLE; scpy (tb->name, "Table", sizeof tb->name);
	static const char *rows[7][4] = { { "Product", "Units", "Takings", "vs 2025" }, { "Flat white", "6,412", "22,442 €", "+18 %" }, { "Croissant", "5,980", "11,960 €", "+9 %" },
					   { "Espresso", "5,104", "12,760 €", "+4 %" }, { "Chai latte", "2,233", "8,932 €", "+41 %" }, { "Carrot cake", "1,876", "7,504 €", "+12 %" }, { "Iced tea", "1,140", "3,420 €", "−6 %" } };
	tb->tbl = new Table (7, 4);
	static const int cw[4] = { 7600, 3200, 3800, 3400 };
	for (int k = 0; k < 4; k++) tb->tbl->colW[k] = cw[k];
	for (int r = 0; r < 7; r++)
	{
		tb->tbl->rowH[r] = 1080;
		for (int k = 0; k < 4; k++)
		{
			TextBody &cell = tb->tbl->at (r, k);
			unsigned col = r && k == 3 ? (rows[r][3][0] == '+' ? 0x288246 : 0xBE463C) : AUTO;
			ParaFmt pf = pf_inherit (); pf.align = k ? AL_RIGHT : AL_LEFT;
			cell.set_text (rows[r][k], F (col, 180, r == 1 && k == 0 ? CF_BOLD : 0), &pf);
			cell.inset[0] = cell.inset[2] = 360; cell.inset[1] = cell.inset[3] = 120;
		}
	}
	tb->x = W * 6 / 100; tb->y = H * 24 / 100; tb->w = 18000;
	table_fit (d, *tb);
	s->obj.push (tb);
	Object *star = shape (s, SH_STAR5, W * 89 / 100, H * 24 / 100, 1100, 1050, fill_solid (PEACH)); scpy (star->name, "Star", sizeof star->name);
	s->anim.push (fx (star->id, AC_EMPHASIS, FX_SPIN, ST_CLICK, 900));
	s->tr.type = TR_FADE; s->tr.dur = 600;
	notes (s, "The flat white leads again. The chai latte: +41 %, the surprise of the year.");

	// 5. the terrace: a picture
	s = slide_new (d, LY_PICTURE); d.slides.push (s);
	unsigned char *pb; unsigned pn;
	const char *sd = getenv ("SIM_SD") ? getenv ("SIM_SD") : "sdcard";
	char pp[300]; snprintf (pp, sizeof pp, "%s/docs/pictures/sunset-sea.jpg", sd);
	Object *pph = ph (s, PH_PICTURE);
	if (read_file (pp, &pb, &pn))
	{
		Object *pic = make_picture ("sunset-sea.jpg", pb, pn, pph);
		free (pb);
		if (pic) { for (int i = 0; i < s->obj.n; i++) if (s->obj[i] == pph) { delete pph; s->obj[i] = pic; break; } pic->ph = PH_PICTURE; scpy (pic->name, "Terrace", sizeof pic->name); }
	}
	put (ph (s, PH_TITLE), "The new terrace");
	Object *pb2 = ph (s, PH_BODY); pb2->tb.clear ();
	static const char *ter[4] = { "Opened on 12 June", "40 seats, facing the sea", "+22 % takings in summer", "Open until 23:00 in July and August" };
	for (int k = 0; k < 4; k++) add_para (pb2, ter[k], F (AUTO, 200), AL_INHERIT, 0, BU_INHERIT);
	s->tr.type = TR_COVER; s->tr.dir = DIR_LEFT; s->tr.dur = 700;
	{ Anim f = fx (pb2->id, AC_ENTRANCE, FX_FLOAT, ST_CLICK, 600); f.byPara = true; s->anim.push (f); }	// (the list: a paragraph a click)
	notes (s, "The terrace paid for itself in one summer.");

	// 6. plans for 2027: a timeline
	s = slide_new (d, LY_TITLE_ONLY); d.slides.push (s);
	scpy (s->section, "Next year", sizeof s->section);
	put (ph (s, PH_TITLE), "Plans for 2027");
	Object *ln = new Object; ln->id = d.nextId++; ln->kind = OB_LINE; ln->x = W * 9 / 100; ln->y = H * 52 / 100; ln->w = W * 82 / 100; ln->h = 0; ln->line = line_solid (0xD2CEC8, 180); scpy (ln->name, "Timeline", sizeof ln->name);
	s->obj.push (ln);
	static const char *steps[4][3] = { { "Q1", "Loyalty card", "on the phone app" }, { "Q2", "Brunch menu", "weekends, 9:00–14:00" }, { "Q3", "A second shop", "near the station" }, { "Q4", "Roast our own", "a small roaster, 5 kg" } };
	for (int k = 0; k < 4; k++)
	{
		int cx = W * (15 + k * 23) / 100, cy = H * 52 / 100;
		Object *dot = shape (s, SH_ELLIPSE, cx - 1000, cy - 1000, 2000, 2000, fill_solid (k % 2 ? PEACH : TEAL));
		put (dot, steps[k][0], F (WHITE, 200, CF_BOLD), AL_CENTER); scpy (dot->name, steps[k][0], sizeof dot->name);
		for (int i = 0; i < 4; i++) dot->tb.inset[i] = 0;
		int ty = k % 2 ? H * 62 / 100 : H * 24 / 100;
		Object *card = shape (s, SH_ROUND, cx - 3300, ty, 6600, 3200, fill_solid (PALE)); card->radius = 140;
		card->tb.clear (); add_para (card, steps[k][1], F (AUTO, 200, CF_BOLD), AL_CENTER, 0, BU_NONE); add_para (card, steps[k][2], F (MUTED, 150), AL_CENTER, 0, BU_NONE, 60);
		scpy (card->name, steps[k][1], sizeof card->name);
		s->anim.push (fx (dot->id, AC_ENTRANCE, FX_ZOOM, k ? ST_CLICK : ST_CLICK, 400));
		s->anim.push (fx (card->id, AC_ENTRANCE, FX_FLY, ST_WITH, 500, k % 2 ? DIR_DOWN : DIR_UP));
	}
	s->tr.type = TR_PUSH; s->tr.dir = DIR_UP; s->tr.dur = 700;
	notes (s, "Four steps, one per quarter. The second shop depends on the bank (decision in March).");

	// 7. a quote
	s = slide_new (d, LY_TITLE_ONLY); d.slides.push (s);
	put (ph (s, PH_TITLE), "What our customers say");
	Object *qm = box (s, W * 7 / 100, H * 22 / 100, W * 14 / 100, H * 30 / 100); put (qm, "“", F (PEACH, 1600, CF_BOLD, (short) d.font_index ("Liberation Serif"))); scpy (qm->name, "Quote mark", sizeof qm->name);
	Object *q = box (s, W * 16 / 100, H * 33 / 100, W * 76 / 100, H * 24 / 100);
	put (q, "The best flat white in town, and now with a view of the sea.", F (AUTO, 340, CF_ITALIC, (short) d.font_index ("Liberation Serif")));
	scpy (q->name, "Quote", sizeof q->name);
	Object *by = box (s, W * 16 / 100, H * 60 / 100, W * 70 / 100, H * 8 / 100); put (by, "— Marie L., a regular since 2021", F (MUTED, 200)); scpy (by->name, "By", sizeof by->name);
	Object *stars = box (s, W * 16 / 100, H * 70 / 100, W * 70 / 100, H * 8 / 100); put (stars, "★★★★★   4.8 / 5 on 1,204 reviews", F (PEACH, 200)); scpy (stars->name, "Stars", sizeof stars->name);
	s->tr.type = TR_DISSOLVE; s->tr.dur = 900;
	s->anim.push (fx (q->id, AC_ENTRANCE, FX_FADE, ST_CLICK, 800));
	notes (s, "Read it slowly. Then the score.");

	// 8. thanks
	s = slide_new (d, LY_SECTION); d.slides.push (s);
	s->masterObjects = false;
	s->bg.type = FILL_GRADIENT; s->bg.c1 = 0x143A48; s->bg.c2 = 0x347C8E; s->bg.angle = 90; s->bg.alpha = 255;
	{ Fill f = fill_solid (0xF0A86E); f.alpha = 235; Object *w = shape (s, SH_ELLIPSE, -W / 4, H * 80 / 100, W * 3 / 2, H * 60 / 100, f); w->tb.clear (); w->tb.ensure (); s->obj.erase (s->obj.n - 1); s->obj.insert (0, w); scpy (w->name, "Wave", sizeof w->name); }
	Object *tt = ph (s, PH_TITLE); put (tt, "Thank you", F (0xFFFFFF, 800, CF_BOLD), AL_CENTER); tt->tb.anchor = AN_MIDDLE; tt->y = H * 28 / 100; tt->h = H * 24 / 100;
	Object *ts = ph (s, PH_SUBTITLE); put (ts, "Questions?", F (0xFFDEC0, 360), AL_CENTER); ts->y = H * 54 / 100;
	s->tr.type = TR_ZOOM; s->tr.dur = 900;
	notes (s, "Thank everyone; the coffee is on us.");

	unsigned n = 0; unsigned char *z = odp_save (d, &n);
	FILE *f = fopen (argv[1], "wb"); if (!f || !z) { fprintf (stderr, "cannot write %s\n", argv[1]); return 1; }
	fwrite (z, 1, n, f); fclose (f);
	printf ("wrote %s (%u bytes, %d slides)\n", argv[1], n, d.slides.n);
	return 0;
}
