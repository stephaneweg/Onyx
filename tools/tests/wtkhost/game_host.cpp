//
// game_host.cpp -- build one game's view on the PC (host_kapi.h), play a short scripted
// scenario and save screenshots. Built once per game with -DGAME_<NAME> (see
// ../run_games_test.sh). The app's own main () is renamed away.
//
#include "host_kapi.h"
#define main onyx_app_main
#if defined GAME_ARKANOID
#include "Apps/arkanoid/main.cpp"
#elif defined GAME_INVADERS
#include "Apps/invaders/main.cpp"
#elif defined GAME_PIPES
#include "Apps/pipes/main.cpp"
#elif defined GAME_SOLITAIRE
#include "Apps/solitaire/main.cpp"
#elif defined GAME_FREECELL
#include "Apps/freecell/main.cpp"
#endif
#undef main
#include "img/imgload.hpp"
bool img_load (const char *, ImgFrames *) { return false; }	// (no codecs on the host)

static void run (GameView *g, int frames)
{
	for (int i = 0; i < frames; i++) { host_ticks += 16; g->step (); }
}
static void shot (GameView *g, const char *name)
{
	g->paint ();
	char p[256]; snprintf (p, sizeof p, "%s/%s.ppm", getenv ("OUT") ? getenv ("OUT") : "/tmp", name);
	host_save_ppm (p, g->canvas.px, g->canvas.w, g->canvas.h, g->canvas.stride);
	printf ("saved %s\n", p);
}
static void click (GameView *g, int x, int y, bool right = false)
{
	g->onMouse (x, y, right ? 0 : 1, right ? 1 : 0, 0, 0); run (g, 1);
	g->onMouse (x, y, 0, 0, 0, 0); run (g, 1);
}
static void drag (GameView *g, int x0, int y0, int x1, int y1)
{
	g->onMouse (x0, y0, 1, 0, 0, 0);
	for (int i = 1; i <= 8; i++) g->onMouse (x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8, 1, 0, 0, 0);
	g->onMouse (x1, y1, 0, 0, 0, 0);
	run (g, 1);
}

int main (int argc, char **argv)
{
	host_kapi_init (argc > 1 ? argv[1] : "sdcard");
	wtk::init ();
#if defined GAME_ARKANOID
	Arkanoid *g = new Arkanoid (0, 0, W, H); g->canvas.alloc (W, H);
	shot (g, "arkanoid_0");
	g->key (' '); g->onMouse (W / 2 + 40, 300, 0, 0, 0, 0);
	run (g, 90); shot (g, "arkanoid_1");
	for (int i = 0; i < 600 && g->state == 0; i++) { g->padX = g->ball[0].x / FP - g->padW / 2 + 3; if (g->padX < WALL) g->padX = WALL; g->mx = -1; run (g, 1); }
	printf ("arkanoid: score %d lives %d state %d bricks left %d\n", g->score, g->lives, g->state, g->breakable ());
	shot (g, "arkanoid_2");
	g->collect ('D'); run (g, 20); shot (g, "arkanoid_3");
#elif defined GAME_INVADERS
	Invaders *g = new Invaders (0, 0, W, H); g->canvas.alloc (W, H);
	shot (g, "invaders_0");
	int kills = 0;
	for (int i = 0; i < 3000 && g->state != 3; i++)
	{
		host_held[' '] = (i % 10) < 5;
		host_held[KEY_LEFT] = (i / 120) % 2; host_held[KEY_RIGHT] = !host_held[KEY_LEFT];
		run (g, 1);
		if (i == 400) shot (g, "invaders_1");
	}
	(void) kills;
	printf ("invaders: score %d lives %d wave %d state %d aliens %d sound %d\n", g->score, g->lives, g->wave, g->state, g->remaining (), host_sound_calls);
	shot (g, "invaders_2");
#elif defined GAME_PIPES
	Pipes *g = new Pipes (0, 0, W, H); g->canvas.alloc (W, H);
	shot (g, "pipes_0");
	// build a path from the start valve (placing each piece through place (), with the
	// queue's next piece forced to the one that continues the path): straight when the
	// next square is free, else turn
	int x = 0, y = 0;
	for (int yy = 0; yy < GH; yy++) for (int xx = 0; xx < GW; xx++) if (g->t[yy][xx].type == 8) { x = xx; y = yy; }
	int out = g->t[y][x].startDir;
	auto freeAt = [&] (int xx, int yy) { return xx >= 0 && xx < GW && yy >= 0 && yy < GH && g->t[yy][xx].type == 0; };
	for (int n = 0; n < 16; n++)
	{
		int nx = x + dx_of (out), ny = y + dy_of (out);
		if (!freeAt (nx, ny)) break;
		int in = opposite (out), o = out;
		if (!freeAt (nx + dx_of (out), ny + dy_of (out)))
		{
			static const int dirs[4] = { N, E, S, Wd };
			for (int d : dirs) if (d != in && d != out && freeAt (nx + dx_of (d), ny + dy_of (d))) { o = d; break; }
		}
		int piece = 0;
		for (int p = 1; p <= 6; p++) if (CONN[p] == (in | o)) piece = p;
		if (!piece) piece = 7;
		g->queue[NQ - 1] = piece;
		g->place (nx, ny);
		x = nx; y = ny; out = o;
	}
	shot (g, "pipes_1");
	g->key ('f'); run (g, 1);
	for (int i = 0; i < 4000 && g->state == 1; i++) { run (g, 1); if (i == 30) shot (g, "pipes_flow"); }
	run (g, 1);
	printf ("pipes: done %d need %d state %d score %d\n", g->done, g->need, g->state, g->score);
	shot (g, "pipes_2");
#elif defined GAME_SOLITAIRE
	Klondike *g = new Klondike (0, 0, W, H); g->canvas.alloc (W, H);
	shot (g, "solitaire_0");
	// play greedily with the rules: foundations, then tableau moves, then the stock
	for (int it = 0; it < 3000 && !g->won; it++)
	{
		bool movedAny = false;
		g->autoAll ();
		for (int p = WASTE; p < NPILE && !movedAny; p++)
		{
			if (p >= FOUND && p < TAB) continue;
			Pile &q = g->s.p[p];
			for (int i = 0; i < q.n && !movedAny; i++)
			{
				if (!q.up[i]) continue;
				if (p == WASTE && i != q.n - 1) continue;
				if (p >= TAB && i == 0 && card_rank (q.c[i]) == 12) continue;	// king already at the bottom
				for (int t = TAB; t < NPILE; t++) if (t != p && g->canTab (q.c[i], t))
				{
					int x0, y0, x1, y1; g->cardPos (p, i, x0, y0);
					if (g->s.p[t].n) g->cardPos (t, g->s.p[t].n - 1, x1, y1); else { x1 = g->pileX (t); y1 = TABY; }
					drag (g, x0 + 20, y0 + 5, x1 + 20, y1 + 25);
					movedAny = true; break;
				}
			}
		}
		if (!movedAny) click (g, g->pileX (STOCK) + 10, TOPY + 10);
		if (it == 40) shot (g, "solitaire_1");
	}
	int f = 0; for (int i = FOUND; i < TAB; i++) f += g->s.p[i].n;
	printf ("solitaire: won %d foundations %d score %d undo %d\n", g->won, f, g->s.score, g->nundo);
	shot (g, "solitaire_2");
	if (g->won) { for (int i = 0; i < 200; i++) { run (g, 1); g->paint (); } shot (g, "solitaire_3"); }
#elif defined GAME_FREECELL
	FreeCell *g = new FreeCell (0, 0, W, H); g->canvas.alloc (W, H);
	g->newGame (1);
	// Microsoft FreeCell game #1 starts "JD 2D 9H JC 5D 7H 7C 5H" / "KD KC 9S 5S AD QC KH 3H" ...
	static const char RK[] = "A23456789TJQK", SU[] = "CDHS";
	for (int row = 0; row < 2; row++)
	{
		printf ("freecell #1 row %d:", row);
		for (int c = 0; c < 8; c++) { int k = g->col[c].c[row]; printf (" %c%c", RK[card_rank (k)], SU[card_suit (k)]); }
		printf ("\n");
	}
	shot (g, "freecell_0");
	// a double click sends the top card of column 0 to a free cell; undo brings it back
	int x = g->colX (0) + 20, y = g->cardY (0, g->col[0].n - 1) + 30;
	click (g, x, y); click (g, x, y);
	printf ("freecell: after double click free cells %d, moves %d\n", g->freeCells (), g->s.moves);
	g->doUndo ();
	printf ("freecell: after undo free cells %d\n", g->freeCells ());
	// drag column 4's top card onto column 1 if legal, else onto a free cell
	int top4 = g->col[4].c[g->col[4].n - 1];
	drag (g, g->colX (4) + 20, g->cardY (4, g->col[4].n - 1) + 30, g->colX (2) + 20, g->cardY (2, g->col[2].n - 1) + 40);
	printf ("freecell: drag %c%c onto column 2 -> column 2 has %d cards\n", RK[card_rank (top4)], SU[card_suit (top4)], g->col[2].n);
	shot (g, "freecell_1");
	// the victory animation
	for (int f = 0; f < 4; f++) g->s.found[f] = 12;
	for (int c = 0; c < 8; c++) g->col[c].n = 0;
	for (int i = 0; i < 4; i++) g->s.cell[i] = -1;
	g->checkWin (); g->paint ();
	for (int i = 0; i < 400 && g->animOn; i++) { run (g, 1); g->paint (); if (i == 150) shot (g, "freecell_win"); }
	printf ("freecell: win animation %s\n", g->animOn ? "still running" : "done");
#endif
	return 0;
}
