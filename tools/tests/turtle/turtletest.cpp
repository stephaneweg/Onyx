//
// turtletest.cpp -- Turtle Quest's engine on the PC (user/Apps/turtle/world.h): every level of the packs given is
// read, its solution run and won with three stars (a drawing level against its own figure), written back and read
// again the same; then the player's mistakes: a wall hit at its line, a locked door, nothing to pick, a word the
// level does not know, a loop that never ends, a syntax error, French words; the gems (in order), the portals, the
// drawings in colour, a recursion without end, the level editor's gem and portal tools (edit_gem, edit_pad) -- on
// small packs written here, so a change to the card's packs does not break them; and the packs' lint (unique ids, every text in both languages and not cut, the lesson cards, the
// colour levels' pens). Run by tools/tests/run_turtle_test.sh.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "turtle/world.h"

using namespace turtle;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf ("FAIL %s:%d ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static char *slurp (const char *path)
{
	FILE *f = fopen (path, "rb"); if (!f) return 0;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	char *b = (char *) malloc (n + 1); size_t got = fread (b, 1, n, f); b[got] = 0; fclose (f);
	return b;
}
static Level *find (Pack **pk, int npk, const char *id)
{
	for (int p = 0; p < npk; p++) for (int i = 0; i < pk[p]->levels.n; i++) if (!strcmp (pk[p]->levels[i]->id, id)) return pk[p]->levels[i];
	return 0;
}
static void play (const Level &L, const char *src, int lang, Run &R)
{
	Arr<Seg> target; if (L.draw) target_of (L, target);
	run_program (L, src, lang, R, L.draw ? &target : 0);
}
// A pack in a string (one level is enough): parsed, or 0 (why)
static Pack *pack_of (const char *src, char *why, int cap)
{
	Pack *pk = new Pack;
	if (parse_pack (*pk, src, why, cap)) return pk;
	delete pk; return 0;
}
// The world at the end of a record
static void replay (const Level &L, const Run &R, World &w)
{
	w.reset (L);
	for (int i = 0; i < R.ev.n; i++) apply (w, R.ev[i]);
}
static int count_kind (const Run &R, int kind) { int n = 0; for (int i = 0; i < R.ev.n; i++) if (R.ev[i].kind == kind) n++; return n; }

// The gems: in order, the sensors, the message when one is left, the numbering refused (steps 2)
static void test_gems ()
{
	char why[160] = "";
	Pack *pk = pack_of ("[pack]\ntitle = t\n[level]\nid = t-gems\nwords = FORWARD BACK LEFT RIGHT PICK ITEM GEM FRONT WALL\nmap =\n"
		"| #########\n| #>1.3.2*#\n| #########\nsolution =\n| FORWARD\n| PICK\n| FORWARD 4\n| PICK\n| BACK 2\n| PICK\n| FORWARD 3\n"
		"[level]\nid = t-nogem\nwords = FORWARD PICK\nmap =\n| #>1*#\n", why, sizeof why);
	CHECK (pk, "the gems' pack: %s", why); if (!pk) return;
	const Level &L = *pk->levels[0], &N = *pk->levels[1];
	Run R;
	play (L, "FORWARD\nPICK\nFORWARD 2\nPICK\n", LANG_EN, R);
	CHECK (R.result == R_ERROR && R.errLine == 4 && strstr (R.msg, "Gem 2 first! This is gem 3."), "a gem out of order: %d line %d %s", R.result, R.errLine, R.msg);
	play (L, "AVANCER\nRAMASSER\nAVANCER 2\nRAMASSER\n", LANG_FR, R);
	CHECK (R.result == R_ERROR && R.errLine == 4 && strstr (R.msg, "D'abord la gemme 2 ! Celle-ci est la 3."), "a gem out of order, in French: %d line %d %s", R.result, R.errLine, R.msg);
	play (L, L.solution, LANG_EN, R);
	CHECK (R.result == R_WON, "the gems in order: %d line %d %s", R.result, R.errLine, R.msg);
	{ World w; replay (L, R, w); CHECK (w.gems == 3 && w.gemsTotal == 3, "gems %d / %d", w.gems, w.gemsTotal);
	  World c; c.copyFrom (w); CHECK (c.gems == 3 && c.gemsTotal == 3 && c.npad[0] == w.npad[0] && c.npad[1] == w.npad[1], "a copied world keeps its gems"); }
	play (L, "FORWARD\nPICK\nFORWARD 4\nPICK\nFORWARD\n", LANG_EN, R);
	CHECK (R.result == R_LOST && strstr (R.msg, "1 gem(s)"), "a gem left: %d %s", R.result, R.msg);
	play (L, "FORWARD\nPICK\nFORWARD 4\nPICK\nFORWARD\n", LANG_FR, R);
	CHECK (R.result == R_LOST && strstr (R.msg, "1 gemme(s) par terre"), "a gem left, in French: %d %s", R.result, R.msg);
	// GEM (): 0 on the floor, the number on a gem, 0 once picked; ITEM () on a gem; FRONT () = 2 facing one
	play (L, "IF FRONT () = 2 THEN FORWARD\nIF GEM () = 1 AND ITEM () THEN PICK\nIF GEM () = 0 THEN FORWARD\nIF GEM () = 0 THEN FORWARD 3\nPICK\nBACK 2\nIF GEM () = 3 THEN PICK\nFORWARD 3\n", LANG_EN, R);
	CHECK (R.result == R_WON, "GEM (), ITEM (), FRONT () = 2: %d line %d %s", R.result, R.errLine, R.msg);
	play (L, "SI DEVANT () = 2 ALORS AVANCER\nSI GEMME () = 1 ALORS RAMASSER\nSI GEMME () = 0 ALORS AVANCER 4\nRAMASSER\nRECULER 2\nSI GEMME () = 3 ALORS RAMASSER\nAVANCER 3\n", LANG_FR, R);
	CHECK (R.result == R_WON, "GEMME () in French: %d line %d %s", R.result, R.errLine, R.msg);
	play (N, "FORWARD\nPRINT GEM ()\n", LANG_EN, R);
	CHECK (R.result == R_ERROR && strstr (R.msg, "does not know GEM"), "a level without GEM: %s", R.msg);
	play (N, "AVANCER\nAFFICHER GEMME ()\n", LANG_FR, R);
	CHECK (R.result == R_ERROR && strstr (R.msg, "ne connaît pas encore GEMME"), "a level without GEM, in French: %s", R.msg);
	delete pk;
	// the numbering: no gap, no repeat -- the pack refused, the level and the gem named; the editor's sentences
	Pack *bad = pack_of ("[level]\nid = a\nmap =\n| #>..*#\n[level]\nid = gap\nmap =\n| #>1.3*#\n", why, sizeof why);
	CHECK (!bad && !strcmp (why, "level 2 (gap): gem 2 is missing"), "a gem missing: %s", why); delete bad;
	bad = pack_of ("[level]\nid = twice\nmap =\n| #>12.2*#\n", why, sizeof why);
	CHECK (!bad && !strcmp (why, "level 1 (twice): two gems 2"), "two gems 2: %s", why); delete bad;
	Level g; g.w = 6; g.h = 1; strcpy (g.map[0], "#>1.3#");
	LevelFault f; char t[160];
	CHECK (!check_level (g, f) && f.kind == LF_GEM_MISSING && f.n == 2 && f.c == 4 && f.r == 0, "check_level: gem 2 missing at the gem after the gap (%d %d %d,%d)", f.kind, f.n, f.c, f.r);
	level_fault_text (f, LANG_EN, t, sizeof t); CHECK (!strcmp (t, "Gem 2 is missing: number the gems 1, 2, 3... without a gap."), "%s", t);
	level_fault_text (f, LANG_FR, t, sizeof t); CHECK (!strcmp (t, "Il manque la gemme 2 : numérote les gemmes 1, 2, 3... sans trou."), "%s", t);
	strcpy (g.map[0], "#2>1.2");
	CHECK (!check_level (g, f) && f.kind == LF_GEM_TWICE && f.n == 2 && f.c == 5, "check_level: the second gem 2 (%d %d %d)", f.kind, f.n, f.c);
	level_fault_text (f, LANG_FR, t, sizeof t); CHECK (!strcmp (t, "Deux gemmes 2 : chaque numéro une fois."), "%s", t);
	g.draw = 1; CHECK (check_level (g, f), "a drawing level's map is a page: not checked");
	g.draw = 0; strcpy (g.map[0], "#>12.3");
	CHECK (check_level (g, f) && f.kind == LF_NONE, "gems 1, 2, 3: fine");
}

// The portals: the jump, the steps left, no jump back, no line across, FRONT () = 5, the pairs refused (step 3)
static void test_portals ()
{
	char why[160] = "";
	Pack *pk = pack_of ("[level]\nid = t-portal\nwords = FORWARD BACK LEFT RIGHT FRONT WALL\nmap =\n| ############\n| #>.T##.T..*#\n| ############\n"
		"[level]\nid = t-two\nmap =\n| ##########\n| #.>T#T...#\n| #U......U#\n| ##########\n"
		"[level]\nid = t-page\ndraw = 1\nmap =\n| >..T..T..\n| .........\nsolution =\n| FORWARD 7\n", why, sizeof why);
	CHECK (pk, "the portals' pack: %s", why); if (!pk) return;
	const Level &L = *pk->levels[0], &L2 = *pk->levels[1], &P = *pk->levels[2];
	Run R; World w;
	play (L, "FORWARD 2\n", LANG_EN, R); replay (L, R, w);
	int nt = 0, ti = -1; for (int i = 0; i < R.ev.n; i++) if (R.ev[i].kind == EV_TELEPORT) { nt++; ti = i; }
	CHECK (nt == 1 && ti >= 0 && R.ev[ti].a == 3 && R.ev[ti].b == 1 && R.ev[ti].c == 7 && R.ev[ti].d == 1, "one jump from (3,1) to (7,1): %d", nt);
	CHECK (ti > 0 && R.ev[ti - 1].kind == EV_MOVE, "the jump after the step's move");
	CHECK (w.x == 7 && w.y == 1 && w.h == 90, "on the twin, the heading kept: (%g,%g) %g", w.x, w.y, w.h);
	CHECK (w.painted[1][7], "the twin's cell painted (the pen down)");
	for (int i = 0; i < w.segs.n; i++)
	{
		const Seg &s = w.segs[i]; double l = hypot (s.x2 - s.x1, s.y2 - s.y1);
		CHECK (l <= 1.0001, "a line across the board: (%g,%g)-(%g,%g)", s.x1, s.y1, s.x2, s.y2);
	}
	play (L, "FORWARD 5\n", LANG_EN, R);
	CHECK (R.result == R_WON && count_kind (R, EV_TELEPORT) == 1, "FORWARD 5: two steps, the jump, three steps to the flag: %d %s", R.result, R.msg);
	// stepping off the twin and back onto it jumps again (BACK as well)
	play (L, "FORWARD 2\nFORWARD\nBACK\n", LANG_EN, R); replay (L, R, w);
	CHECK (count_kind (R, EV_TELEPORT) == 2 && w.x == 3 && w.y == 1 && w.h == 90, "BACK onto a portal jumps: %d jumps, (%g,%g)", count_kind (R, EV_TELEPORT), w.x, w.y);
	// FRONT () = 5 facing a portal, WALL () false
	play (L, "FORWARD\nIF FRONT () = 5 AND NOT WALL () THEN FORWARD 4\n", LANG_EN, R);
	CHECK (R.result == R_WON, "FRONT () = 5: %d line %d %s", R.result, R.errLine, R.msg);
	// FORWARD 3 with the portal one square ahead: one step, the jump, two steps from the twin; the second pair too
	play (L2, "FORWARD 3\n", LANG_EN, R); replay (L2, R, w);
	CHECK (w.x == 7 && w.y == 1 && count_kind (R, EV_TELEPORT) == 1, "FORWARD 3: 2 squares past the twin: (%g,%g)", w.x, w.y);
	play (L2, "RIGHT\nFORWARD\nRIGHT\nFORWARD\n", LANG_EN, R); replay (L2, R, w);
	CHECK (w.x == 8 && w.y == 2 && w.h == 270 && count_kind (R, EV_TELEPORT) == 1, "the pair U: (%g,%g) %g", w.x, w.y, w.h);
	// a fractional move: no jump half-way, the jump when the centre is reached
	play (L, "FORWARD 1.5\n", LANG_EN, R); CHECK (count_kind (R, EV_TELEPORT) == 0, "no jump half-way");
	play (L, "FORWARD 1.5\nFORWARD 0.5\n", LANG_EN, R); CHECK (count_kind (R, EV_TELEPORT) == 1, "the jump on the centre");
	// a drawing level: the map is a page, the portals do nothing
	play (P, P.solution, LANG_EN, R);
	CHECK (R.result == R_WON && count_kind (R, EV_TELEPORT) == 0, "no jump in a drawing level: %d", count_kind (R, EV_TELEPORT));
	delete pk;
	Pack *bad = pack_of ("[level]\nid = portal\nmap =\n| #>.T..*#\n", why, sizeof why);
	CHECK (!bad && !strcmp (why, "level 1 (portal): the teleporter T has no twin"), "a lone T: %s", why); delete bad;
	bad = pack_of ("[level]\nid = a\nmap =\n| #>*#\n[level]\nid = many\nmap =\n| #>U.U.U*#\n", why, sizeof why);
	CHECK (!bad && !strcmp (why, "level 2 (many): three teleporters U"), "three U: %s", why); delete bad;
	Level g; g.w = 8; g.h = 1; strcpy (g.map[0], "#>.T..*#");
	LevelFault f; char t[160];
	CHECK (!check_level (g, f) && f.kind == LF_PAD_ALONE && f.n == 1 && f.c == 3, "check_level: the lone T (%d %d %d)", f.kind, f.n, f.c);
	level_fault_text (f, LANG_EN, t, sizeof t); CHECK (!strcmp (t, "Portal 1 has no twin: place its second pad."), "%s", t);
	level_fault_text (f, LANG_FR, t, sizeof t); CHECK (!strcmp (t, "Le portail 1 n'a pas de jumeau : place son deuxième portail."), "%s", t);
	strcpy (g.map[0], "#UU>.U*#");
	CHECK (!check_level (g, f) && f.kind == LF_PAD_MANY && f.n == 2 && f.c == 5, "check_level: the third U (%d %d %d)", f.kind, f.n, f.c);
	g.draw = 2; strcpy (g.map[0], "#>.T..*#"); CHECK (check_level (g, f), "a lone T on a page: not checked");
}

// The drawings in colour (step 4)
static void test_colours (Level *sq)
{
	char why[160] = "";
	Pack *pk = pack_of ("[level]\nid = t-colour\ndraw = color\nmap =\n| ........\n| ........\n| .^......\n| ........\n"
		"solution =\n| COLOR 4\n| FORWARD 2\n| RIGHT\n| COLOR 2\n| FORWARD 3\n"
		"[level]\nid = t-shape\ndraw = shape\nmap =\n| ..\n| >.\nsolution =\n| FORWARD\n", why, sizeof why);
	CHECK (pk, "the colours' pack: %s", why); if (!pk) return;
	const Level &L = *pk->levels[0];
	CHECK (L.draw == 2 && pk->levels[1]->draw == 1, "draw = color reads 2, draw = shape 1: %d %d", L.draw, pk->levels[1]->draw);
	char *w = write_pack (*pk);
	CHECK (strstr (w, "draw = color\n") && strstr (w, "draw = 1\n"), "written back: draw = color, draw = 1");
	free (w);
	Run R;
	play (L, L.solution, LANG_EN, R);
	CHECK (R.result == R_WON, "a colour level's own solution: %d %s", R.result, R.msg);
	play (L, "COLOR 4\nFORWARD 2\nRIGHT\nCOLOR 5\nFORWARD 3\n", LANG_EN, R);
	CHECK (R.result == R_LOST && strstr (R.msg, "not the right colours"), "one colour changed: %d %s", R.result, R.msg);
	play (L, "COULEUR 4\nAVANCER 2\nDROITE\nCOULEUR 5\nAVANCER 3\n", LANG_FR, R);
	CHECK (R.result == R_LOST && strstr (R.msg, "pas les bonnes couleurs"), "one colour changed, in French: %d %s", R.result, R.msg);
	play (L, "COLOR 4\nFORWARD 2\nRIGHT\nCOLOR 2\nFORWARD 2\n", LANG_EN, R);
	CHECK (R.result == R_LOST && strstr (R.msg, "Not quite the same figure"), "a wrong shape: %d %s", R.result, R.msg);
	delete pk;
	if (sq && sq->solution)
	{
		char src[600]; snprintf (src, sizeof src, "COLOR 4\n%s", sq->solution);
		play (*sq, src, LANG_EN, R);
		CHECK (R.result == R_WON, "a shape level drawn in red: %d %s", R.result, R.msg);
	}
}

// A recursion without a stop, the lesson cards, the word GEM (step 5)
static void test_recursion_and_cards ()
{
	char why[160] = "";
	Pack *pk = pack_of ("[level]\nid = t-rec\ndraw = 1\nmap =\n| .....\n| ..^..\nsolution =\n| FORWARD\n", why, sizeof why);
	CHECK (pk, "the recursion's pack: %s", why); if (!pk) return;
	Run R;
	play (*pk->levels[0], "SUB Spin (n)\n  RIGHT 10\n  Spin n + 1\nEND SUB\nSpin 1\n", LANG_EN, R);
	CHECK (R.result == R_ERROR && strstr (R.msg, "calls itself without end") && !strstr (R.msg, "stack"), "a recursion without end: %d %s", R.result, R.msg);
	play (*pk->levels[0], "SUB Tourne (n)\n  DROITE 10\n  Tourne n + 1\nFIN SUB\nTourne 1\n", LANG_FR, R);
	CHECK (R.result == R_ERROR && strstr (R.msg, "s'appelle lui-même sans fin"), "a recursion without end, in French: %d %s", R.result, R.msg);
	delete pk;
	static const char *const NEW[] = { "gems", "teleport", "color", "params", "function", "recursion", 0 };
	for (int i = 0; NEW[i]; i++)
	{
		const Concept *c = find_concept (NEW[i]);
		CHECK (c && c->title[0][0] && c->title[1][0] && c->text[0][0] && c->text[1][0], "the lesson card %s", NEW[i]);
	}
	CHECK (!strcmp (word_name (W_GEM, LANG_FR), "GEMME") && !strcmp (word_name (W_GEM, LANG_EN), "GEM") && word_id ("gemme") == W_GEM, "GEM / GEMME");
	CHECK (word_help (W_GEM, LANG_EN)[0] && word_help (W_GEM, LANG_FR)[0] && strstr (word_help (W_FRONT, LANG_EN), "5 a portal") && strstr (word_help (W_FRONT, LANG_FR), "5 un portail"), "the words' help");
}

// A colour level's pens: 1 ... 14 but 7, 8 (light enough to tint, never white, never a grey shape target); no line
// of one colour along a line of another (a hidden requirement: only the top colour shows) -- a crossing is fine
static void lint_colours (const Level &L, const Arr<Seg> &t)
{
	for (int i = 0; i < t.n; i++)
	{
		int c = t[i].color;
		CHECK (c >= 1 && c <= 14 && c != 7 && c != 8, "%s: a line in pen %d (colour levels: 1 to 14 but 7, 8)", L.id, c);
		const Seg &s = t[i];
		double dx = s.x2 - s.x1, dy = s.y2 - s.y1, len = sqrt (dx * dx + dy * dy);
		int k = (int) (len / 0.1) + 1, run = 0, worst = 0;
		for (int j = 0; j <= k; j++)
		{
			double px = s.x1 + dx * j / k, py = s.y1 + dy * j / k;
			bool near = false;
			if (hypot (px - s.x1, py - s.y1) > 0.2 && hypot (px - s.x2, py - s.y2) > 0.2)
				for (int m = 0; m < t.n && !near; m++) if (t[m].color != c && seg_dist (t[m], px, py) < 0.05) near = true;
			run = near ? run + 1 : 0; if (run > worst) worst = run;
		}
		CHECK (worst < 3, "%s: a line in pen %d along a line of another colour, (%g,%g)-(%g,%g)", L.id, c, s.x1, s.y1, s.x2, s.y2);
	}
}
// The packs' lint: every text in both languages, none cut; the pack's French title; the concept's card
static void lint_level (const Pack &pk, const Level &L)
{
	for (int g = 0; g < 2; g++)
	{
		CHECK (L.title[g][0] && L.text[g][0] && L.hint[g][0], "%s: a text missing (%s)", L.id, g ? "French" : "English");
		CHECK (strlen (L.title[g]) < sizeof L.title[g] - 1 && strlen (L.text[g]) < sizeof L.text[g] - 1 && strlen (L.hint[g]) < sizeof L.hint[g] - 1, "%s: a text cut", L.id);
	}
	CHECK (pk.title[0][0] && pk.title[1][0], "%s: its pack has no title in both languages", L.id);
	const Concept *c = find_concept (L.topic);
	CHECK (c && c->title[0][0] && c->title[1][0] && c->text[0][0] && c->text[1][0], "%s: the card '%s'", L.id, L.topic);
}

// The level editor's tools (step 10): edit_gem, edit_pad; a level made with them, checked, written back and read again
static void test_editor ()
{
	char why[160] = "";
	Pack *pk = pack_of ("[pack]\ntitle = t\n[level]\nid = t-ed\nmap =\n| ##########\n| #>.......#\n| #........#\n| ##########\n", why, sizeof why);
	CHECK (pk, "the editor's pack: %s", why); if (!pk) return;
	Level &L = *pk->levels[0];
	// gems: the lowest free number, a click cycles, a drag only places, never on the turtle
	CHECK (edit_gem (L, 3, 1, false) && L.map[1][3] == '1', "a first gem is 1: %c", L.map[1][3]);
	CHECK (edit_gem (L, 4, 1, true) && L.map[1][4] == '2', "a drag places the next: %c", L.map[1][4]);
	CHECK (!edit_gem (L, 4, 1, true) && L.map[1][4] == '2', "a drag over a gem does not cycle it: %c", L.map[1][4]);
	CHECK (edit_gem (L, 4, 1, false) && L.map[1][4] == '3', "a click on gem 2 makes it 3: %c", L.map[1][4]);
	CHECK (edit_gem (L, 5, 1, false) && L.map[1][5] == '2', "the lowest free number fills the gap: %c", L.map[1][5]);
	CHECK (!edit_gem (L, 1, 1, false) && L.map[1][1] == '>', "the turtle's cell is kept: %c", L.map[1][1]);
	L.map[1][3] = '9'; CHECK (edit_gem (L, 3, 1, false) && L.map[1][3] == '1', "9 cycles back to 1: %c", L.map[1][3]);
	{
		Level F; F.set (L); for (int c = 1; c <= 8; c++) F.map[2][c] = (char) ('0' + c); F.map[1][3] = '9';
		CHECK (!edit_gem (F, 7, 1, false) && F.map[1][7] == '.', "all nine on the map: nothing placed (%c)", F.map[1][7]);
	}
	LevelFault f;
	CHECK (check_level (L, f), "gems 1 2 3 made by the tool: fine (%d)", f.kind);
	// pads: a pair keeps two, the older goes; the other pair untouched
	int lc[2] = { -1, -1 }, lr[2] = { -1, -1 };
	CHECK (edit_pad (L, 6, 1, 1, lc[0], lr[0]) && L.map[1][6] == 'T', "a first T");
	CHECK (!check_level (L, f) && f.kind == LF_PAD_ALONE && f.c == 6 && f.r == 1, "a lone T refused, ringed (%d %d,%d)", f.kind, f.c, f.r);
	char t[160]; level_fault_text (f, LANG_EN, t, sizeof t); CHECK (!strcmp (t, "Portal 1 has no twin: place its second pad."), "%s", t);
	CHECK (edit_pad (L, 2, 2, 1, lc[0], lr[0]) && check_level (L, f), "the T's twin: fine (%d)", f.kind);
	CHECK (edit_pad (L, 8, 2, 1, lc[0], lr[0]) && L.map[1][6] == '.' && L.map[2][2] == 'T' && L.map[2][8] == 'T', "a third T: the older (6,1) goes, the last placed (2,2) stays");
	CHECK (count_pads (L, 1) == 2 && check_level (L, f), "never more than two T: %d", count_pads (L, 1));
	CHECK (edit_pad (L, 7, 1, 2, lc[1], lr[1]) && edit_pad (L, 6, 2, 2, lc[1], lr[1]) && count_pads (L, 2) == 2 && count_pads (L, 1) == 2, "the U pair beside");
	CHECK (!edit_pad (L, 1, 1, 2, lc[1], lr[1]) && L.map[1][1] == '>', "a pad never on the turtle");
	{
		Level M; M.set (L); int a = -1, b = -1;			// (a level read from a pack: no pad placed last -- the first one goes)
		CHECK (edit_pad (M, 3, 2, 1, a, b) && M.map[2][2] == '.' && M.map[2][8] == 'T' && M.map[2][3] == 'T' && a == 3 && b == 2, "no last known: the first T in reading order goes");
	}
	// written back and read again: the digits, the pads, a colour level
	L.draw = 0; char *txt = write_pack (*pk);
	Pack p2; CHECK (parse_pack (p2, txt, why, sizeof why) && p2.levels.n == 1 && !memcmp (p2.levels[0]->map, L.map, sizeof L.map), "the edited level written back: %s", why);
	free (txt);
	L.draw = 2; txt = write_pack (*pk);
	Pack p3; CHECK (parse_pack (p3, txt, why, sizeof why) && p3.levels.n == 1 && p3.levels[0]->draw == 2, "a colour level written back: %s", why);
	free (txt);
	delete pk;
}

int main (int argc, char **argv)
{
	Pack *packs[16]; int np = 0, total = 0;
	for (int a = 1; a < argc && np < 16; a++)
	{
		char *src = slurp (argv[a]);
		CHECK (src, "cannot read %s", argv[a]); if (!src) continue;
		Pack *pk = new Pack; char why[120] = "";
		bool ok = parse_pack (*pk, src, why, sizeof why);
		CHECK (ok, "%s: %s", argv[a], why);
		packs[np++] = pk;
		// written back, read again: the same levels
		char *again = write_pack (*pk);
		Pack p2; CHECK (parse_pack (p2, again, why, sizeof why) && p2.levels.n == pk->levels.n, "%s: written back: %s", argv[a], why);
		for (int i = 0; i < pk->levels.n && i < p2.levels.n; i++)
		{
			const Level &x = *pk->levels[i], &y = *p2.levels[i];
			CHECK (!strcmp (x.id, y.id) && x.w == y.w && x.h == y.h && !memcmp (x.map, y.map, sizeof x.map) && x.par3 == y.par3 && x.par2 == y.par2
				&& !strcmp (x.title[0], y.title[0]) && !strcmp (x.title[1], y.title[1]) && !strcmp (x.text[0], y.text[0]) && !strcmp (x.text[1], y.text[1])
				&& !strcmp (x.hint[0], y.hint[0]) && !strcmp (x.hint[1], y.hint[1]) && !strcmp (x.topic, y.topic) && !strcmp (x.words, y.words)
				&& !strcmp (x.solution ? x.solution : "", y.solution ? y.solution : "") && !strcmp (x.start ? x.start : "", y.start ? y.start : "") && x.draw == y.draw,
				"%s: level %s not the same once written back", argv[a], x.id);
		}
		free (again); free (src);
		for (int i = 0; i < pk->levels.n; i++)
		{
			const Level &L = *pk->levels[i];
			total++;
			CHECK (L.solution, "%s: no solution", L.id);
			CHECK (find_concept (L.topic), "%s: unknown concept '%s'", L.id, L.topic);
			lint_level (*pk, L);
			for (int p = 0; p < np; p++) for (int j = 0; j < packs[p]->levels.n; j++)
				if (packs[p]->levels[j] != &L && !strcmp (packs[p]->levels[j]->id, L.id) && (p < np - 1 || j < i)) CHECK (0, "two levels %s", L.id);
			if (!L.solution) continue;
			if (L.draw == 2) { Arr<Seg> t; CHECK (target_of (L, t), "%s: no figure", L.id); lint_colours (L, t); }
			Run R; play (L, L.solution, LANG_EN, R);
			CHECK (R.result == R_WON, "%s: the solution does not win (%d: line %d %s)", L.id, R.result, R.errLine, R.msg);
			CHECK (R.stars == 3, "%s: the solution has %d instructions, par %d: %d star(s)", L.id, R.count, L.par3, R.stars);
			printf ("  %-15s %-28s %2d instructions, %5d events\n", L.id, L.title[0], R.count, R.ev.n);
		}
	}
	CHECK (total == 48, "%d levels, not 48", total);
	test_gems ();
	test_portals ();
	test_editor ();
	test_recursion_and_cards ();
	Level *hello = find (packs, np, "hello"), *door = find (packs, np, "door"), *coins = find (packs, np, "coins"), *corner = find (packs, np, "corner");
	Level *sq = find (packs, np, "draw-square"), *maze = find (packs, np, "maze");
	test_colours (sq);
	if (hello && door && coins && corner && sq && maze)
	{
		Run R;
		play (*hello, "FORWARD 2\nFORWARD 4\n", LANG_EN, R);
		CHECK (R.result == R_ERROR && R.errLine == 2 && strstr (R.msg, "wall"), "a wall: %d line %d %s", R.result, R.errLine, R.msg);
		play (*hello, "FORWARD 2\n", LANG_EN, R);
		CHECK (R.result == R_LOST && strstr (R.msg, "flag"), "not there: %s", R.msg);
		play (*door, "FORWARD 4\n", LANG_EN, R);
		CHECK (R.result == R_ERROR && strstr (R.msg, "locked"), "a locked door: %s", R.msg);
		play (*coins, "PICK\n", LANG_EN, R);
		CHECK (R.result == R_ERROR && R.errLine == 1 && strstr (R.msg, "nothing"), "nothing to pick: %s", R.msg);
		play (*hello, "LEFT\n", LANG_EN, R);
		CHECK (R.result == R_ERROR && strstr (R.msg, "does not know LEFT"), "a word the level does not know: %s", R.msg);
		play (*hello, "DO\nLOOP\n", LANG_EN, R);
		CHECK (R.result == R_ENDLESS, "an endless loop: %d %s", R.result, R.msg);
		play (*corner, "WHILE 1\n  RIGHT\nWEND\n", LANG_EN, R);
		CHECK (R.result == R_ENDLESS && R.errLine >= 1, "an endless loop that turns: %d %s", R.result, R.msg);
		play (*hello, "FORWARD 1 +\n", LANG_EN, R);
		CHECK (R.result == R_COMPILE && R.errLine == 1, "a syntax error: %d %s", R.result, R.msg);
		play (*hello, "REPEAT 4\n FORWARD\n", LANG_FR, R);
		CHECK (R.result == R_COMPILE && strstr (R.msg, "FIN REPETER"), "an unclosed REPEAT, in French: %s", R.msg);
		play (*hello, "SI MUR ()\n", LANG_FR, R);
		CHECK (R.result == R_COMPILE && strstr (R.msg, "ALORS") && !strstr (R.msg, "THEN"), "a compiler's message with French words: %s", R.msg);
		play (*corner, "AVANCER 2\nDROITE\nAVANCER 2\nGAUCHE\nAVANCER 2\nDROITE\nAVANCER\n", LANG_FR, R);
		CHECK (R.result == R_WON && R.stars == 3, "French words: %d %s", R.result, R.msg);
		play (*corner, "AVANCE 2\nTD\nAV 2\nTG\nAVANCE 2\nDROITE\nAVANCE\n", LANG_FR, R);
		CHECK (R.result == R_WON && R.stars == 3, "the first French words, the short ones: %d %s", R.result, R.msg);
		play (*maze, "TANTQUE NON SURBUT ()\n  SI NON MURDROITE () ALORS\n    DROITE\n    AVANCER\n  SINON SI NON MUR () ALORS\n    AVANCER\n  SINON\n    GAUCHE\n  FIN SI\nFIN TANTQUE\n", LANG_FR, R);
		CHECK (R.result == R_WON, "the maze in French: %d line %d %s", R.result, R.errLine, R.msg);
		{
			Run E; play (*maze, "WHILE NOT ONGOAL ()\n  IF NOT WALLRIGHT () THEN\n    RIGHT\n    FORWARD\n  ELSEIF NOT WALL () THEN\n    FORWARD\n  ELSE\n    LEFT\n  END IF\nWEND\n", LANG_EN, E);
			CHECK (E.result == R_WON && E.count == R.count, "the maze counts the same in both languages: %d / %d", E.count, R.count);
		}
		play (*maze, "TANTQUE NON SURBUT ()\n  SI NON MURDROITE () ALORS\n    DROITE\n    AVANCE\n  SINONSI NON MUR () ALORS\n    AVANCE\n  SINON\n    GAUCHE\n  FIN SI\nFINTANTQUE\n", LANG_FR, R);
		CHECK (R.result == R_WON, "the maze in the first French words: %d line %d %s", R.result, R.errLine, R.msg);
		// POUR ... JUSQUE ... PAS ... SUITE, SUB / FONCTION and their ends, the types, a class
		play (*corner, "DIM n COMME ENTIER\nDIM s COMME CHAINE\ns = \"ok\"\n"
			"FONCTION Double (x COMME ENTIER)\n  Double = x * 2\nFIN FONCTION\n"
			"SUB Marche (cote)\n  AVANCER cote\nFIN SUB\n"
			"POUR i = 1 JUSQUE 3 PAS 2\n  n = n + 1\nSUITE\n"
			"Marche Double (1)\nDROITE\nMarche n\nGAUCHE\nAVANCER 2\nDROITE\nAVANCER\nAFFICHER s\n", LANG_FR, R);
		CHECK (R.result == R_WON && R.out && strstr (R.out, "ok"), "POUR, SUB, FONCTION, ENTIER, CHAINE: %d line %d %s", R.result, R.errLine, R.msg);
		play (*hello, "CLASSE Compteur\n  n COMME ENTIER\nFIN CLASSE\nSUB Compteur.Plus ()\n  CECI.n = CECI.n + 1\nFIN SUB\n"
			"DIM c COMME Compteur\nc = NOUVEAU Compteur\nREPETER 4\n  c.Plus\nFIN REPETER\nAVANCER c.n\n", LANG_FR, R);
		CHECK (R.result == R_WON, "CLASSE: %d line %d %s", R.result, R.errLine, R.msg);
		play (*corner, "AVANCER 2\nRIGHT\nPOUR i = 1 TO 2\n  FORWARD\nSUITE\nGAUCHE\nAVANCER 2\nDROITE\nSI 1 THEN FORWARD\n", LANG_EN, R);
		CHECK (R.result == R_WON, "French words when the language is English, mixed: %d line %d %s", R.result, R.errLine, R.msg);
		play (*hello, "DIM a COMME ENTIER\nDIM x COMME REEL\nDIM d COMME REEL64\nDIM l COMME ENTIER32\nDIM o COMME OCTET\na = 7 / 2\nx = 0.5\nd = 3000000000 + x\na = a + d - 3000000000\nl = 70000\no = ASC (\"A\") + 0.2\nAVANCER a\nAFFICHER STR$ (a) + STR$ (l) + STR$ (o) + CHR$ (o)\n", LANG_FR, R);
		CHECK (R.result == R_WON && R.out && strstr (R.out, " 4 70000 65A"), "ENTIER (a whole number of 64 bits), REEL, REEL64, ENTIER32, OCTET: %d line %d %s [%s]", R.result, R.errLine, R.msg, R.out ? R.out : "");
		play (*hello, "DIM o AS BYTE\nDIM n AS INTEGER\nn = 100000 * 100000\no = 256\n", LANG_EN, R);
		CHECK (R.result == R_ERROR && R.errLine == 4, "an INTEGER is wide, a BYTE holds 0 to 255: %d line %d %s", R.result, R.errLine, R.msg);
		play (*hello, "SI 1 ALORS AVANCER 4 SINON SI 0 ALORS AVANCER 1\n", LANG_FR, R);
		CHECK (R.result == R_WON, "SINON SI inside a line stays ELSE IF: %d line %d %s", R.result, R.errLine, R.msg);
		play (*corner, "FORWARD 2\nRIGHT\nFORWARD 2\nLEFT\nFORWARD 2\nRIGHT\nFORWARD\nPRINT \"done\"; 4 * 2\n", LANG_EN, R);
		CHECK (R.result == R_WON && R.out && strstr (R.out, "done"), "PRINT: %s", R.out ? R.out : "(none)");
		play (*sq, "REPEAT 4\n  FORWARD 3\n  RIGHT\nEND REPEAT\n", LANG_EN, R);
		CHECK (R.result == R_LOST, "a smaller square is not the figure: %d", R.result);
		play (*sq, "RIGHT\nRIGHT\nRIGHT\nRIGHT\nFORWARD 4\nRIGHT\nFORWARD 4\nRIGHT\nFORWARD 4\nRIGHT\nFORWARD 4\n", LANG_EN, R);
		CHECK (R.result == R_WON && R.stars == 1, "the square another way, longer: %d, %d stars", R.result, R.stars);
		// the line events: the hook's lines, in order
		play (*hello, "FORWARD 1\nFORWARD 3\n", LANG_EN, R);
		int lines[4], nl = 0; for (int i = 0; i < R.ev.n && nl < 4; i++) if (R.ev[i].kind == EV_LINE) lines[nl++] = R.ev[i].line;
		CHECK (nl == 2 && lines[0] == 1 && lines[1] == 2, "line events: %d", nl);
		CHECK (count_instructions ("REPEAT 3 ' three\n  FORWARD: PICK\nEND REPEAT\n\nEND\n") == 4, "count: %d", count_instructions ("REPEAT 3 ' three\n  FORWARD: PICK\nEND REPEAT\n\nEND\n"));
	}
	else CHECK (0, "the levels the checks use are missing");
	// on the card's packs 4 and 5: a colour changed on the rainbow; a recursion without its stop on the tree; gem 3
	// first on the first gems' level
	Level *rainbow = find (packs, np, "rainbow-spiral"), *tree = find (packs, np, "tree"), *gl = find (packs, np, "gems-line");
	if (rainbow && tree && gl)
	{
		Run R;
		play (*rainbow, "FOR i = 1 TO 16\n  COLOR i MOD 6 + 2\n  FORWARD i\n  RIGHT\nNEXT\n", LANG_EN, R);
		CHECK (R.result == R_LOST && strstr (R.msg, "not the right colours"), "the rainbow in other colours: %d %s", R.result, R.msg);
		play (*rainbow, "FOR i = 1 TO 15\n  COLOR i MOD 6 + 1\n  FORWARD i\n  RIGHT\nNEXT\n", LANG_FR, R);
		CHECK (R.result == R_LOST && strstr (R.msg, "figure claire"), "the rainbow one leg short: %d %s", R.result, R.msg);
		play (*tree, "SUB Tree (size)\n  FORWARD size\n  LEFT 30\n  Tree size * 0.6\n  RIGHT 60\n  Tree size * 0.6\n  LEFT 30\n  BACK size\nEND SUB\nTree 6\n", LANG_EN, R);
		CHECK (R.result == R_ERROR && strstr (R.msg, "calls itself without end"), "a tree without its stop: %d line %d %s", R.result, R.errLine, R.msg);
		play (*gl, "FORWARD 4\nPICK\n", LANG_EN, R);
		CHECK (R.result == R_ERROR && R.errLine == 2 && strstr (R.msg, "Gem 1 first! This is gem 2."), "gem 2 first: %s", R.msg);
	}
	else CHECK (0, "the levels of packs 4 and 5 the checks use are missing");
	for (int i = 0; i < np; i++) delete packs[i];
	printf (fails ? "turtle: %d failure(s)\n" : "ok   turtle (%d levels: solved, written back; the errors)\n", fails ? fails : total);
	return fails != 0;
}
