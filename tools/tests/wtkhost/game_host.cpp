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
#elif defined GAME_GRAPHCALC
#include "Apps/graphcalc/main.cpp"
#elif defined GAME_ICONEDIT
#include "Apps/iconedit/main.cpp"
#elif defined GAME_RTF
#include "Apps/rtfview/main.cpp"
#elif defined GAME_BASICRT
#include "basic/runtime.cpp"
#endif
#undef main
#include "img/imgload.hpp"
bool img_load (const char *, ImgFrames *) { return false; }	// (no codecs on the host)

#if defined GAME_BASICRT
// the kernel font stand-in: the wtk font's glyphs
static void wtkText (unsigned *d, int w, int h, int x, int y, const char *s, unsigned c)
{
	wtk::Font &f = wtk::font ();
	if (!f.valid ()) return;
	for (; *s; s++, x += f.width ())
	{
		const unsigned char *g = f.glyph ((unsigned char) *s, 0);
		if (!g) continue;
		for (int r = 0; r < f.height (); r++) for (int i = 0; i < f.width (); i++)
			if ((g[r] << i) & 0x80) { int px = x + i, py = y + r; if (px >= 0 && py >= 0 && px < w && py < h) d[py * w + px] = c; }
	}
}
// A scripted game session (arkanoid.bas): at given times press keys, hold keys, take shots.
struct Step { unsigned t; int key; int held; int down; const char *shot; };
static const Step *g_script = 0; static unsigned g_t0 = 0; static int g_step = 0;
static void savePage (OnyxHost &h, const char *name);
static void scriptTick (unsigned ticks)
{
	while (g_script && g_script[g_step].t && ticks - g_t0 >= g_script[g_step].t)
	{
		const Step &s = g_script[g_step++];
		if (s.key && g_host->root) g_host->root->onKey (s.key);
		if (s.held) host_held[s.held] = s.down;
		if (s.shot) savePage (*g_host, s.shot);
	}
}
static void savePage (OnyxHost &h, const char *name)
{
	char p[256]; snprintf (p, sizeof p, "%s/%s.ppm", getenv ("OUT") ? getenv ("OUT") : "/tmp", name);
	host_save_ppm (p, h.pg[h.vpage], h.W, h.H, h.W);
	printf ("saved %s\n", p);
}
#endif
#if defined GAME_GRAPHCALC || defined GAME_ICONEDIT || defined GAME_RTF || defined GAME_BASICRT
static void wshot (wtk::Widget *w, const char *name)
{
	w->canvas.alloc (w->width, w->height); w->onDraw ();
	char p[256]; snprintf (p, sizeof p, "%s/%s.ppm", getenv ("OUT") ? getenv ("OUT") : "/tmp", name);
	host_save_ppm (p, w->canvas.px, w->canvas.w, w->canvas.h, w->canvas.stride);
	printf ("saved %s\n", p);
}
#else
static void run (GameView *g, int frames)
{
	for (int i = 0; i < frames; i++) { host_ticks += 2; g->step (); }
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
#endif

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
#elif defined GAME_GRAPHCALC
	static const char *src[NF] = { "sin(x)", "x^2/4-3", "1/x", "2x+" };
	for (int i = 0; i < NF; i++)
	{
		g_f[i].on = new Checkbox (0, 0, 10, 10, "", true, 0);
		g_f[i].tb = new Textbox (0, 0, 10, 10, src[i]);
		compile (i);
		printf ("graphcalc y%d = %-10s ok %d err %d\n", i + 1, src[i], g_f[i].prog.ok, g_f[i].err);
	}
	g_plot = new Plot (0, 0, W - PANEL - 4, H - 8);
	g_mx = 300; g_my = 200;
	wshot (g_plot, "graphcalc");
	view_trig (); g_mx = -1;
	wshot (g_plot, "graphcalc_trig");
#elif defined GAME_ICONEDIT
	int fh = 16;
	for (int i = 0; i < NTOOL; i++) { g_toolBtn[i] = new Button (6, 8 + i * 34, TOOL_W - 12, 28, TOOL_NAME[i], btn_tool); g_toolBtn[i]->tag = i; }
	g_grid = new Grid (TOOL_W, 4, W - TOOL_W - SIDE_W, H - fh - 16);
	g_pal = new Palette (W - SIDE_W + 6, 8);
	g_prev = new Preview (W - SIDE_W + 2, 8 + g_pal->height + fh + 10, SIDE_W - 4, 2 * MAXS + 20);
	g_status = new Label (6, H - fh - 8, W - 12, fh + 2, "", C_TEXT, C_BG);
	bool ok = load_bmp ("SD:/apps/invaders.app/icon.bmp");
	printf ("iconedit: loaded %d (%d x %d)\n", ok, g_w, g_h);
	// draw a yellow ellipse with the Ellipse tool, then fill inside it
	g_col[0] = 0x00FFE000; g_tool = T_ELLIPSE; g_grid->layoutCells ();
	int c = g_grid->cell, ox = g_grid->ox, oy = g_grid->oy;
	g_grid->onMouse (ox + 4 * c + 2, oy + 24 * c + 2, 1, 0, 0, 0);
	g_grid->onMouse (ox + 20 * c + 2, oy + 36 * c + 2, 1, 0, 0, 0);
	g_grid->onMouse (ox + 20 * c + 2, oy + 36 * c + 2, 0, 0, 0, 0);
	g_col[0] = 0x00FF4040; g_tool = T_FILL;
	g_grid->onMouse (ox + 12 * c + 2, oy + 30 * c + 2, 1, 0, 0, 0);
	g_grid->onMouse (ox + 12 * c + 2, oy + 30 * c + 2, 0, 0, 0, 0);
	printf ("iconedit: undo levels %d, pixel (12,30) = %06x\n", g_nundo, g_img[30 * MAXS + 12]);
	g_hx = 12; g_hy = 30; refresh ();
	// compose the window
	wtk::Canvas win; win.alloc (W, H); win.clear (C_BG);
	wtk::Widget *parts[] = { g_grid, g_pal, g_prev, g_status };
	for (wtk::Widget *w : parts) { w->canvas.alloc (w->width, w->height); w->onDraw (); win.putOther (w->canvas, w->left, w->top, false); }
	for (int i = 0; i < NTOOL; i++) { wtk::Widget *w = g_toolBtn[i]; w->canvas.alloc (w->width, w->height); w->onDraw (); win.putOther (w->canvas, w->left, w->top, false); }
	char p[256]; snprintf (p, sizeof p, "%s/iconedit.ppm", getenv ("OUT") ? getenv ("OUT") : "/tmp");
	host_save_ppm (p, win.px, W, H, W);
	// save and reload round trip
	host_sd = "/tmp"; save_bmp ("SD:/onyx_icon_test.bmp");
	unsigned before = hash_img (); load_bmp ("SD:/onyx_icon_test.bmp");
	printf ("iconedit: save / load round trip %s\n", hash_img () == before ? "ok" : "DIFFERS");
#elif defined GAME_RTF
	char fp[512]; snprintf (fp, sizeof fp, "%s/docs/onyx-rtf-sample.rtf", argc > 1 ? argv[1] : "sdcard");
	FILE *f = fopen (fp, "rb");
	static char src[65536]; int n = f ? (int) fread (src, 1, sizeof src - 1, f) : 0; if (f) fclose (f);
	RichTextBox *b = new RichTextBox (0, 0, W - 12, H - PATH_H - 6, 65536);
	b->readonly = true;
	rtf::load (*b, src, n);
	printf ("rtf: %d chars, first line: ", b->len); for (int i = 0; i < b->len && b->buf[i] != '\n'; i++) putchar (b->buf[i]); printf ("\n");
	int nb = 0; for (int i = 0; i < b->len; i++) if (wtk::rt_unpack (b->attr[i]).flags & wtk::RT_BOLD) nb++;
	printf ("rtf: %d bold chars\n", nb);
	for (int i = 0; i < b->len; i++) if (b->buf[i] == 'q' && b->buf[i+1] == 'u' && b->buf[i+2]=='o') { for (int k = i - 4; k < i + 12; k++) printf ("%02x ", (unsigned char) b->buf[k]); printf ("\n"); break; }
	wshot (b, "rtfview");
	// round trip: save, reload, compare text + styles
	static char out[262144]; int m = rtf::save (*b, out, sizeof out);
	RichTextBox *c = new RichTextBox (0, 0, W - 12, H - PATH_H - 6, 65536);
	rtf::load (*c, out, m);
	bool same = c->len == b->len;
	for (int i = 0; same && i < b->len; i++) if (c->buf[i] != b->buf[i] || c->attr[i] != b->attr[i]) { printf ("rtf: differs at %d\n", i); same = false; }
	printf ("rtf: save %d bytes, reload %s\n", m, same ? "identical" : "DIFFERENT");
#elif defined GAME_BASICRT
	host_text_hook = wtkText; setvbuf (stdout, 0, _IONBF, 0); printf ("start\n");
	static const char *const progs[] = { "gfx", "gfx12", "fs", "../../../../sdcard/basic/examples/arkanoid", 0 };
	static const Step ark[] = {		// (ticks = 1/100 s from the start)
		{ 150, 0, 0, 0, "ark_title" }, { 200, ' ', 0, 0, 0 }, { 300, 0, 0, 0, "ark_ready" },
		{ 420, ' ', 0, 0, 0 }, { 430, 0, KEY_RIGHT, 1, 0 }, { 470, 0, KEY_RIGHT, 0, 0 },
		{ 600, 0, 0, 0, "ark_play1" }, { 900, 0, KEY_LEFT, 1, 0 }, { 960, 0, KEY_LEFT, 0, 0 },
		{ 1200, 0, 0, 0, "ark_play2" }, { 1400, 'p', 0, 0, 0 }, { 1450, 0, 0, 0, "ark_pause" },
		{ 1500, 'p', 0, 0, 0 }, { 2500, 0, 0, 0, "ark_play3" },
		{ 3000, 27, 0, 0, 0 }, { 3100, 27, 0, 0, 0 }, { 0, 0, 0, 0, 0 } };
	for (int k = 0; progs[k]; k++)
	{
		char fp[512]; snprintf (fp, sizeof fp, "%s/../tools/tests/basic/rt/%s.bas", argc > 1 ? argv[1] : "sdcard", progs[k]);
		FILE *f = fopen (fp, "rb"); if (!f) { printf ("no %s\n", fp); continue; }
		static char src[65536]; int n = (int) fread (src, 1, sizeof src - 1, f); fclose (f); src[n] = 0;
		OnyxHost *h = new OnyxHost; g_host = h; h->console = false; scpy (h->title, progs[k], sizeof h->title);
		bas::Error e;
		bas::Program *pr = bas::compile (src, &e);
		if (!pr) { printf ("%s: compile error line %d: %s\n", progs[k], e.line, e.msg); continue; }
		h->windowCmd = true;				// (no "press any key" at the end)
		const char *base = strrchr (progs[k], '/') ? strrchr (progs[k], '/') + 1 : progs[k];
		if (!strcmp (base, "arkanoid")) { g_script = ark; g_step = 0; g_t0 = host_ticks; host_tick_hook = scriptTick; }
		else host_tick_hook = 0;
		host_fs = 0;
		struct Keep : public bas::Host {};
		int r = bas::run (pr, *h, &e);
		if (r) printf ("%s: runtime error line %d: %s\n", progs[k], e.line, e.msg);
		printf ("%s: mode %d %dx%d scale %dx%d\n", base, h->mode, h->W, h->H, h->sx, h->sy);
		savePage (*h, base);
		if (host_fs)
		{
			char p[256]; snprintf (p, sizeof p, "%s/%s_fullscreen.ppm", getenv ("OUT") ? getenv ("OUT") : "/tmp", base);
			host_save_ppm (p, host_fs, host_fsw, host_fsh, host_fsw);
		}
	}
#endif
	return 0;
}
