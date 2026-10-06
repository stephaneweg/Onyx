//
// turtletest.cpp -- Turtle Quest's engine on the PC (user/Apps/turtle/world.h): every level of the packs given is
// read, its solution run and won with three stars (a drawing level against its own figure), written back and read
// again the same; then the player's mistakes: a wall hit at its line, a locked door, nothing to pick, a word the
// level does not know, a loop that never ends, a syntax error, French words. Run by tools/tests/run_turtle_test.sh.
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
				&& !strcmp (x.text[1], y.text[1]) && !strcmp (x.solution ? x.solution : "", y.solution ? y.solution : "") && x.draw == y.draw,
				"%s: level %s not the same once written back", argv[a], x.id);
		}
		free (again); free (src);
		for (int i = 0; i < pk->levels.n; i++)
		{
			const Level &L = *pk->levels[i];
			total++;
			CHECK (L.solution, "%s: no solution", L.id);
			CHECK (find_concept (L.topic), "%s: unknown concept '%s'", L.id, L.topic);
			if (!L.solution) continue;
			Run R; play (L, L.solution, LANG_EN, R);
			CHECK (R.result == R_WON, "%s: the solution does not win (%d: line %d %s)", L.id, R.result, R.errLine, R.msg);
			CHECK (R.stars == 3, "%s: the solution has %d instructions, par %d: %d star(s)", L.id, R.count, L.par3, R.stars);
			printf ("  %-14s %-28s %2d instructions, %5d events\n", L.id, L.title[0], R.count, R.ev.n);
		}
	}
	CHECK (total >= 20, "only %d levels", total);
	Level *hello = find (packs, np, "hello"), *door = find (packs, np, "door"), *coins = find (packs, np, "coins"), *corner = find (packs, np, "corner");
	Level *sq = find (packs, np, "draw-square"), *maze = find (packs, np, "maze");
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
		play (*hello, "DIM a COMME ENTIER64\nDIM x COMME SIMPLE\nDIM d COMME DOUBLE\nDIM l COMME LONG\nDIM o COMME OCTET\na = 7 / 2\nx = 0.5\nd = 3000000000 + x\na = a + d - 3000000000\nl = 70000\no = ASC (\"A\") + 0.2\nAVANCER a\nAFFICHER STR$ (a) + STR$ (l) + STR$ (o) + CHR$ (o)\n", LANG_FR, R);
		CHECK (R.result == R_WON && R.out && strstr (R.out, " 4 70000 65A"), "ENTIER64 (a whole number), SIMPLE, DOUBLE, LONG, OCTET: %d line %d %s [%s]", R.result, R.errLine, R.msg, R.out ? R.out : "");
		play (*hello, "DIM o AS BYTE\no = 256\n", LANG_EN, R);
		CHECK (R.result == R_ERROR && R.errLine == 2, "a BYTE holds 0 to 255: %d line %d %s", R.result, R.errLine, R.msg);
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
	for (int i = 0; i < np; i++) delete packs[i];
	printf (fails ? "turtle: %d failure(s)\n" : "ok   turtle (%d levels: solved, written back; the errors)\n", fails ? fails : total);
	return fails != 0;
}
