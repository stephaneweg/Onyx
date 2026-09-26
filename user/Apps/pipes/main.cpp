//
// pipes/main.cpp -- Pipes (after "Pipe Dream"). Lay pipe pieces on the board before the
// water arrives: the next pieces wait in the queue on the left (the bottom one goes
// first). Click a square (or move with the arrows and press Space) to put the next piece
// there; putting one on an unfilled piece replaces it (-50). After the countdown the
// water leaves the start valve and flows through whatever you built -- each piece it
// crosses scores 50, a cross crossed twice 500 more. When it spills, the round is won if
// it went through at least the required number of pieces. F makes the water flow fast
// (double points). Walls appear from round 3; the water gets faster every round.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "game.h"

using namespace wtk;

#define GW	10
#define GH	7
#define CELL	44
#define QX	14			// the queue column
#define QW	CELL
#define BX	(QX + QW + 22)		// the board
#define HUD	28
#define BY	(HUD + 10)
#define W	(BX + GW * CELL + 14)
#define H	(BY + GH * CELL + 36)
#define NQ	5

enum { N = 1, E = 2, S = 4, Wd = 8 };		// sides (bitmask)
// Piece types: 0 none, 1 ─, 2 │, 3 └ (N+E), 4 ┌ (E+S), 5 ┐ (S+W), 6 ┘ (W+N), 7 ┼, 8 start, 9 wall
static const int CONN[10] = { 0, E | Wd, N | S, N | E, E | S, S | Wd, Wd | N, N | E | S | Wd, 0, 0 };
static int opposite (int d) { return d == N ? S : d == S ? N : d == E ? Wd : E; }
static int dx_of (int d) { return d == E ? 1 : d == Wd ? -1 : 0; }
static int dy_of (int d) { return d == S ? 1 : d == N ? -1 : 0; }

struct Tile
{
	int type;
	int startDir;			// start: its outlet
	int fillH, fillV;		// cross: filled horizontally / vertically (0..1000); others use fillH
	int inSide;			// side the water came in by (for drawing), per axis for the cross
	int inSideV;
};

class Pipes : public GameView
{
public:
	Tile t[GH][GW];
	int  queue[NQ];
	int  curX, curY;			// keyboard cursor
	int  level, score, hiscore, need, done;
	int  state;				// 0 building (countdown), 1 flowing, 2 round won, 3 lost, 4 paused
	int  prevState;
	unsigned countdown;			// ms before the water starts
	int  flowMs;				// ms per piece
	bool fast;
	int  wx, wy, wside;			// the tile being filled + the side the water entered
	unsigned stateT;
	int  bombX, bombY; unsigned bombT;	// replaced piece flash

	Pipes (int l, int t_, int w, int h) : GameView (l, t_, w, h), hiscore (0)
	{ rng_seed (gms () * 2654435761u + 7); newGame (); }

	int randPiece () { int r = rng_n (100); return r < 8 ? 7 : 1 + rng_n (6); }
	void newGame () { level = 0; score = 0; newRound (); }
	void newRound ()
	{
		for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) { Tile &k = t[y][x]; k.type = 0; k.fillH = k.fillV = 0; k.inSide = k.inSideV = 0; }
		// the start valve: away from the edges, its outlet pointing inwards
		int sx = 1 + rng_n (GW - 2), sy = 1 + rng_n (GH - 2);
		static const int dirs[4] = { N, E, S, Wd };
		int d;
		do d = dirs[rng_n (4)]; while (sx + dx_of (d) < 1 || sx + dx_of (d) > GW - 2 || sy + dy_of (d) < 1 || sy + dy_of (d) > GH - 2);
		t[sy][sx].type = 8; t[sy][sx].startDir = d;
		// walls from round 3
		int walls = level >= 2 ? (level - 1 < 6 ? level - 1 : 6) : 0;
		for (int i = 0; i < walls; i++)
		{
			int x = rng_n (GW), y = rng_n (GH);
			if (t[y][x].type || (x == sx + dx_of (d) && y == sy + dy_of (d))) { i--; continue; }
			t[y][x].type = 9;
		}
		for (int i = 0; i < NQ; i++) queue[i] = randPiece ();
		curX = sx + dx_of (d); curY = sy + dy_of (d);
		need = 10 + level * 2; if (need > 30) need = 30;
		done = 0;
		countdown = 22000 - level * 1500; if ((int) countdown < 8000) countdown = 8000;
		flowMs = 3200 - level * 250; if (flowMs < 1000) flowMs = 1000;
		fast = false;
		wx = sx; wy = sy; wside = 0;
		state = 0;
	}

	void place (int x, int y)
	{
		if (state != 0 && state != 1) return;
		Tile &k = t[y][x];
		if (k.type == 8 || k.type == 9 || k.fillH || k.fillV) { sfx (140, 60, SOUND_SQUARE, 70); return; }
		if (k.type) { score -= 50; bombX = x; bombY = y; bombT = gms (); sfx (90, 160, SOUND_NOISE, 120); }
		else sfx (700, 25, SOUND_TRIANGLE, 100);
		k.type = queue[NQ - 1];
		for (int i = NQ - 1; i > 0; i--) queue[i] = queue[i - 1];
		queue[0] = randPiece ();
		redraw ();
	}

	// The water filled (wx,wy): move on to the next tile through its outlet.
	void advance ()
	{
		Tile &k = t[wy][wx];
		int out;
		if (k.type == 8) out = k.startDir;
		else if (k.type == 7) out = opposite (wside);
		else out = CONN[k.type] & ~wside;
		int nx = wx + dx_of (out), ny = wy + dy_of (out);
		int in = opposite (out);
		if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) { spill (); return; }
		Tile &n = t[ny][nx];
		if (n.type == 0 || n.type >= 8 || !(CONN[n.type] & in)) { spill (); return; }
		if (n.type == 7)
		{
			bool horiz = in == E || in == Wd;
			if ((horiz ? n.fillH : n.fillV) > 0) { spill (); return; }	// already used this way
		}
		else if (n.fillH > 0) { spill (); return; }
		wx = nx; wy = ny; wside = in;
		if (n.type == 7 && (in == N || in == S)) n.inSideV = in; else n.inSide = in;
	}
	int &fillOf (Tile &k) { return (k.type == 7 && (wside == N || wside == S)) ? k.fillV : k.fillH; }

	void spill ()
	{
		if (done >= need)
		{
			state = 2; score += 1000 + level * 250; sfx_win ();
		}
		else { state = 3; sfx_lose (); }
		stateT = gms ();
		if (score > hiscore) hiscore = score;
	}

	void tick (unsigned dt) override
	{
		if (state == 4) return;
		if (state == 0)
		{
			if (fast || dt >= countdown) { countdown = 0; state = 1; sfx (220, 200, SOUND_NOISE, 60); }
			else countdown -= dt;
			if (countdown && countdown % 1000 < dt && countdown < 5000) sfx (1000, 30);
			redraw ();
			return;
		}
		if (state != 1) { redraw (); return; }
		Tile &k = t[wy][wx];
		int &f = fillOf (k);
		int ms = fast ? 160 : flowMs;
		if (k.type == 8) ms /= 2;
		f += (int) (dt * 1000 / (unsigned) ms);
		if (f >= 1000)
		{
			f = 1000;
			if (k.type != 8)
			{
				done++;
				score += fast ? 100 : 50;
				if (k.type == 7 && k.fillH == 1000 && k.fillV == 1000) { score += 500; sfx (1320, 80); }
				else sfx (400 + (done % 8) * 60, 30, SOUND_TRIANGLE, 80);
			}
			advance ();
		}
		redraw ();
	}

	bool cellAt (int px, int py, int &x, int &y)
	{
		if (px < BX || py < BY) return false;
		x = (px - BX) / CELL; y = (py - BY) / CELL;
		return x < GW && y < GH;
	}
	void press (int px, int py, bool right) override
	{
		if (right) return;
		if (state == 2) { if (gms () - stateT > 500) { level++; newRound (); } return; }
		if (state == 3) { if (gms () - stateT > 500) newGame (); return; }
		int x, y;
		if (cellAt (px, py, x, y)) { curX = x; curY = y; place (x, y); }
	}
	bool key (long k) override
	{
		switch (k)
		{
		case KEY_LEFT:  if (curX > 0) curX--; break;
		case KEY_RIGHT: if (curX < GW - 1) curX++; break;
		case KEY_UP:    if (curY > 0) curY--; break;
		case KEY_DOWN:  if (curY < GH - 1) curY++; break;
		case ' ': case KEY_ENTER:
			if (state == 2 || state == 3) press (-1, -1, false);
			else place (curX, curY);
			break;
		case 'f': case 'F': if (state == 0 || state == 1) { fast = true; } break;
		case 'p': case 'P':
			if (state == 4) state = prevState; else if (state <= 1) { prevState = state; state = 4; }
			break;
		default: return false;
		}
		redraw ();
		return true;
	}

	// ---- drawing ---------------------------------------------------------------------
	void pipeSeg (int cx, int cy, int d, int len, unsigned col, int thick)
	{
		// a segment from the centre towards side d, len px long
		int h = thick / 2;
		switch (d)
		{
		case N:  canvas.fillRect (cx - h, cy - len, thick, len, col); break;
		case S:  canvas.fillRect (cx - h, cy, thick, len, col); break;
		case E:  canvas.fillRect (cx, cy - h, len, thick, col); break;
		case Wd: canvas.fillRect (cx - len, cy - h, len, thick, col); break;
		}
	}
	// a straight run along side d between distances [from, to) of the centre, `thick` wide
	void run (int cx, int cy, int d, int from, int to, unsigned col, int thick)
	{
		if (to <= from) return;
		int h = thick / 2;
		switch (d)
		{
		case N:  canvas.fillRect (cx - h, cy - to, thick, to - from, col); break;
		case S:  canvas.fillRect (cx - h, cy + from, thick, to - from, col); break;
		case E:  canvas.fillRect (cx + from, cy - h, to - from, thick, col); break;
		case Wd: canvas.fillRect (cx - to, cy - h, to - from, thick, col); break;
		}
	}
	// Water from side `in` to the centre, then out towards `out`; f = 0..1000. The start
	// valve has no `in`: its water runs from the centre out.
	void water (int cx, int cy, int in, int out, int f)
	{
		const unsigned WC = 0x0040A0FF;
		int half = CELL / 2;
		if (in)
		{
			int a = f < 500 ? f * half / 500 : half;
			run (cx, cy, in, half - a, half, WC, 8);
			if (f > 500) { canvas.fillRect (cx - 4, cy - 4, 8, 8, WC); run (cx, cy, out, 0, (f - 500) * half / 500, WC, 8); }
		}
		else { canvas.fillRect (cx - 4, cy - 4, 8, 8, WC); run (cx, cy, out, 0, f * half / 1000, WC, 8); }
	}
	void drawPiece (int px, int py, int type, int size)
	{
		int cx = px + size / 2, cy = py + size / 2, half = size / 2;
		const unsigned PIPE = 0x00A8B0B8, EDGE = 0x00505860;
		for (int d = 1; d <= 8; d <<= 1) if (CONN[type] & d)
		{ pipeSeg (cx, cy, d, half, EDGE, 16); }
		for (int d = 1; d <= 8; d <<= 1) if (CONN[type] & d)
		{ pipeSeg (cx, cy, d, half, PIPE, 12); pipeSeg (cx, cy, d, half, 0x00282C30, 8); }
		if (CONN[type]) { canvas.fillRect (cx - 6, cy - 6, 12, 12, PIPE); canvas.fillRect (cx - 4, cy - 4, 8, 8, 0x00282C30); }
		if (type == 7) { canvas.fillRect (cx - 6, cy - 4, 12, 8, 0x00282C30); canvas.fillRect (cx - 4, cy - 6, 8, 12, 0x00282C30); }
	}
	void paint () override
	{
		Canvas &c = canvas;
		c.fillRect (0, 0, W, H, 0x00283038);
		char s[64];
		s[0] = 0; gcat (s, "Score "); gcatn (s, score); gtext (c, 10, 6, s, 0x00FFFFFF);
		s[0] = 0; gcat (s, "Round "); gcatn (s, level + 1); gtext (c, BX + 120, 6, s, 0x0080C0FF);
		s[0] = 0; gcat (s, "Pipes "); gcatn (s, done); gcat (s, " / "); gcatn (s, need);
		gtext (c, BX + 240, 6, s, done >= need ? 0x0080FF80 : 0x00FFD080);
		// queue
		c.fillRect (QX - 4, BY - 4, QW + 8, NQ * QW + 8, 0x00181C20);
		for (int i = 0; i < NQ; i++)
		{
			int y = BY + i * QW;
			c.fillRect (QX, y, QW, QW, i == NQ - 1 ? 0x00506070 : 0x00404850);
			c.frameRect (QX, y, QW, QW, 0x00202428);
			drawPiece (QX, y, queue[i], QW);
		}
		gtext (c, QX, BY + NQ * QW + 8, "next", 0x00C0C8D0);
		// countdown bar
		if (state == 0)
		{
			int total = 22000 - level * 1500; if (total < 8000) total = 8000;
			int hbar = (int) ((long) (GH * CELL) * countdown / total);
			c.fillRect (BX - 14, BY, 8, GH * CELL, 0x00181C20);
			c.fillRect (BX - 14, BY + GH * CELL - hbar, 8, hbar, 0x0040A0FF);
		}
		// board
		for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++)
		{
			int px = BX + x * CELL, py = BY + y * CELL;
			Tile &k = t[y][x];
			c.fillRect (px, py, CELL, CELL, ((x + y) & 1) ? 0x00687480 : 0x00606C78);
			c.frameRect (px, py, CELL, CELL, 0x00505A64);
			if (k.type == 9) { c.fillRect (px + 3, py + 3, CELL - 6, CELL - 6, 0x00705040); c.frameRect (px + 3, py + 3, CELL - 6, CELL - 6, 0x00402818); continue; }
			if (k.type == 8)
			{
				c.fillRect (px + 6, py + 6, CELL - 12, CELL - 12, 0x00384048);
				pipeSeg (px + CELL / 2, py + CELL / 2, k.startDir, CELL / 2, 0x00A8B0B8, 12);
				pipeSeg (px + CELL / 2, py + CELL / 2, k.startDir, CELL / 2, 0x00282C30, 8);
				c.fillRect (px + CELL / 2 - 8, py + CELL / 2 - 8, 16, 16, 0x00D04030);
				if (k.fillH) water (px + CELL / 2, py + CELL / 2, 0, k.startDir, k.fillH);
				continue;
			}
			if (k.type)
			{
				drawPiece (px, py, k.type, CELL);
				if (k.type == 7)
				{
					if (k.fillH) water (px + CELL / 2, py + CELL / 2, k.inSide, opposite (k.inSide), k.fillH);
					if (k.fillV) water (px + CELL / 2, py + CELL / 2, k.inSideV, opposite (k.inSideV), k.fillV);
				}
				else if (k.fillH) water (px + CELL / 2, py + CELL / 2, k.inSide, CONN[k.type] & ~k.inSide, k.fillH);
			}
		}
		if (bombT && gms () - bombT < 250)
			c.frameRect (BX + bombX * CELL + 2, BY + bombY * CELL + 2, CELL - 4, CELL - 4, 0x00FFB040);
		// cursor
		c.frameRect (BX + curX * CELL, BY + curY * CELL, CELL, CELL, 0x00FFFF60);
		c.frameRect (BX + curX * CELL + 1, BY + curY * CELL + 1, CELL - 2, CELL - 2, 0x00FFFF60);
		// footer
		const char *hint = state == 0 ? "Lay pipes! F = let the water flow now" : fast ? "Fast flow: double points" : "";
		gtext (c, BX, BY + GH * CELL + 10, hint, 0x00C0C8D0);
		if (state == 2) { gtext_c (c, BX + GW * CELL / 2, BY + GH * CELL / 2 - 30, "ROUND WON!", 0x0080FF80, 2); gtext_c (c, BX + GW * CELL / 2, BY + GH * CELL / 2 + 10, "Click for the next round", 0x00FFFFFF); }
		if (state == 3) { gtext_c (c, BX + GW * CELL / 2, BY + GH * CELL / 2 - 30, "IT SPILLED!", 0x00FF6060, 2); gtext_c (c, BX + GW * CELL / 2, BY + GH * CELL / 2 + 10, "Click for a new game", 0x00FFFFFF); }
		if (state == 4) gtext_c (c, BX + GW * CELL / 2, BY + GH * CELL / 2 - 16, "PAUSED", 0x00FFFF80, 2);
	}
};

static Pipes *g_game;
static void on_new (void) { g_game->newGame (); g_game->redraw (); }
static void on_pause (void) { g_game->key ('p'); }
static void on_sound (void) { sfx_set_mute (!g_sfx_mute); }

int main (void)
{
	GameRoot root (W, H, "Pipes");
	if (root.canvas.px == 0) return 1;
	g_game = new Pipes (0, 0, W, H);
	root.addChild (g_game);
	root.view = g_game;
	static Menu menu;
	menu.menu ("Game");
	menu.item ("New Game",       "^N", WK_CTRL ('N'), on_new);
	menu.item ("Pause",          "P",  0,             on_pause);
	menu.item ("Sound On / Off", "",   0,             on_sound);
	menu.publish ();
	g_game->setFocus ();
	root.run ();
	return 0;
}
