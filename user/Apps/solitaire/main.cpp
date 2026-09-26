//
// solitaire/main.cpp -- Klondike, the classic patience. Drag cards with the mouse: build
// the seven columns down in alternating colours (a king on an empty column), and the four
// foundations up by suit from the ace. Click the stock to turn cards (one, or three with
// Game > Draw Three); when it is empty, click it to turn the waste over again. A double
// click sends a card to its foundation; a right click sends every card that can go.
// Hidden cards turn over by themselves. Undo with ^Z. Windows-style scoring and a timer;
// the cards bounce when you win.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "cards.h"

using namespace wtk;

#define GAP	12
#define W	(GAP + 7 * (CARD_W + GAP))
#define H	540
#define TOPY	10
#define TABY	(TOPY + CARD_H + 18)
#define DOWN_DY	6
#define UP_DY	20

enum { STOCK = 0, WASTE = 1, FOUND = 2, TAB = 6, NPILE = 13 };

struct Pile { int n; signed char c[52]; bool up[52]; };
struct State { Pile p[NPILE]; int score; };

class Klondike : public GameView
{
public:
	State s;
	State undo[64]; int nundo;
	bool draw3;
	unsigned t0, elapsed; bool running;
	// dragging
	bool dragging; int dPile, dIdx, dOffX, dOffY, pressX, pressY; bool moved;
	unsigned lastClickT; int lastClickPile, lastClickIdx;
	bool won; WinAnim anim; bool animOn;

	Klondike (int l, int t, int w, int h) : GameView (l, t, w, h), draw3 (false)
	{ rng_seed (gms () * 2654435761u + 3); deal (); }

	int pileX (int p) const
	{
		if (p == STOCK) return GAP;
		if (p == WASTE) return GAP + (CARD_W + GAP);
		if (p >= FOUND && p < TAB) return GAP + (3 + p - FOUND) * (CARD_W + GAP);
		return GAP + (p - TAB) * (CARD_W + GAP);
	}
	int upDy (int p) const
	{
		// squeeze a tall column so it fits the window
		const Pile &q = s.p[p];
		int down = 0, up = 0;
		for (int i = 0; i < q.n; i++) (q.up[i] ? up : down)++;
		int dy = UP_DY;
		while (dy > 8 && TABY + down * DOWN_DY + (up > 0 ? up - 1 : 0) * dy + CARD_H > H - 24) dy--;
		return dy;
	}
	// position of card i of pile p
	void cardPos (int p, int i, int &x, int &y) const
	{
		x = pileX (p); y = p < TAB ? TOPY : TABY;
		if (p >= TAB)
		{
			int dy = upDy (p);
			for (int k = 0; k < i; k++) y += s.p[p].up[k] ? dy : DOWN_DY;
		}
		else if (p == WASTE && draw3)
		{
			int n = s.p[WASTE].n, first = n - 3 < 0 ? 0 : n - 3;
			if (i >= first) x += (i - first) * 14;
		}
	}

	void deal ()
	{
		signed char deck[52];
		for (int i = 0; i < 52; i++) deck[i] = (signed char) i;
		for (int i = 51; i > 0; i--) { int j = rng_n (i + 1); signed char t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
		for (int i = 0; i < NPILE; i++) s.p[i].n = 0;
		int k = 0;
		for (int col = 0; col < 7; col++)
			for (int r = 0; r <= col; r++) { Pile &q = s.p[TAB + col]; q.c[q.n] = deck[k++]; q.up[q.n] = r == col; q.n++; }
		while (k < 52) { Pile &q = s.p[STOCK]; q.c[q.n] = deck[k++]; q.up[q.n] = false; q.n++; }
		s.score = 0; nundo = 0;
		t0 = gms (); elapsed = 0; running = false;
		dragging = false; won = false; animOn = false; lastClickT = 0;
		redraw ();
	}

	void push_undo () { if (nundo == 64) { for (int i = 1; i < 64; i++) undo[i - 1] = undo[i]; nundo--; } undo[nundo++] = s; }
	void do_undo () { if (nundo && !won) { s = undo[--nundo]; s.score -= 2; sfx (300, 40, SOUND_TRIANGLE); redraw (); } }
	void start_clock () { if (!running && !won) { running = true; t0 = gms (); } }

	int top (int p) const { return s.p[p].n ? s.p[p].c[s.p[p].n - 1] : -1; }

	bool canFound (int card, int f) const
	{
		int t = top (f);
		if (t < 0) return card_rank (card) == 0;
		return card_suit (t) == card_suit (card) && card_rank (card) == card_rank (t) + 1;
	}
	bool canTab (int card, int p) const
	{
		int t = top (p);
		if (t < 0) return card_rank (card) == 12;
		if (!s.p[p].up[s.p[p].n - 1]) return false;
		return card_red (t) != card_red (card) && card_rank (card) + 1 == card_rank (t);
	}

	// Move cards [idx..] of pile `from` onto `to` (rules already checked).
	void move (int from, int idx, int to)
	{
		push_undo ();
		Pile &a = s.p[from], &b = s.p[to];
		for (int i = idx; i < a.n; i++) { b.c[b.n] = a.c[i]; b.up[b.n] = true; b.n++; }
		a.n = idx;
		// Windows scoring
		bool toF = to >= FOUND && to < TAB, fromF = from >= FOUND && from < TAB;
		if (from == WASTE && to >= TAB) s.score += 5;
		if (toF && !fromF) s.score += 10;
		if (fromF && to >= TAB) s.score -= 15;
		if (from >= TAB && a.n && !a.up[a.n - 1]) { a.up[a.n - 1] = true; s.score += 5; }	// turn the hidden card
		if (s.score < 0) s.score = 0;
		start_clock ();
		if (toF) sfx (660 + card_rank (b.c[b.n - 1]) * 40, 50); else sfx (520, 25, SOUND_TRIANGLE, 100);
		checkWin ();
	}

	void checkWin ()
	{
		int n = 0; for (int f = FOUND; f < TAB; f++) n += s.p[f].n;
		if (n < 52) return;
		won = true; running = false;
		elapsed = (gms () - t0) / 1000;
		if (elapsed > 30) s.score += (int) (700000 / elapsed) / 10;	// time bonus
		int fx[4], fy[4];
		for (int f = 0; f < 4; f++) { fx[f] = pileX (FOUND + f); fy[f] = TOPY; }
		win_start (anim, fx, fy); animOn = true;
		sfx_win ();
	}

	void turnStock ()
	{
		Pile &st = s.p[STOCK], &wa = s.p[WASTE];
		if (st.n == 0)
		{
			if (wa.n == 0) return;
			push_undo ();
			for (int i = wa.n - 1; i >= 0; i--) { st.c[st.n] = wa.c[i]; st.up[st.n] = false; st.n++; }
			wa.n = 0;
			s.score -= draw3 ? 20 : 100; if (s.score < 0) s.score = 0;
			sfx (200, 60, SOUND_NOISE, 60);
		}
		else
		{
			push_undo ();
			for (int k = 0; k < (draw3 ? 3 : 1) && st.n; k++) { wa.c[wa.n] = st.c[--st.n]; wa.up[wa.n] = true; wa.n++; }
			sfx (900, 20, SOUND_NOISE, 50);
		}
		start_clock ();
		redraw ();
	}

	bool toFoundation (int p)
	{
		int c = top (p);
		if (c < 0 || !s.p[p].up[s.p[p].n - 1] || (p >= FOUND && p < TAB)) return false;
		for (int f = FOUND; f < TAB; f++) if (canFound (c, f)) { move (p, s.p[p].n - 1, f); return true; }
		return false;
	}
	void autoAll ()
	{
		bool any = true;
		while (any && !won)
		{
			any = false;
			if (toFoundation (WASTE)) any = true;
			for (int p = TAB; p < NPILE; p++) if (toFoundation (p)) any = true;
		}
		redraw ();
	}

	// Which card (pile, index) is under (x, y)? index -1 = the empty pile itself.
	bool hit (int x, int y, int &pile, int &idx)
	{
		for (int p = 0; p < NPILE; p++)
		{
			const Pile &q = s.p[p];
			for (int i = q.n - 1; i >= 0; i--)
			{
				int cx, cy; cardPos (p, i, cx, cy);
				if (x >= cx && x < cx + CARD_W && y >= cy && y < cy + CARD_H) { pile = p; idx = i; return true; }
			}
			int px = pileX (p), py = p < TAB ? TOPY : TABY;
			if (x >= px && x < px + CARD_W && y >= py && y < py + CARD_H) { pile = p; idx = -1; return true; }
		}
		return false;
	}

	void press (int x, int y, bool right) override
	{
		if (animOn || won) { if (!animOn) deal (); return; }
		if (right) { autoAll (); return; }
		int p, i;
		if (!hit (x, y, p, i)) return;
		if (p == STOCK) { turnStock (); return; }
		if (i < 0) return;
		Pile &q = s.p[p];
		// double click -> foundation
		unsigned now = gms ();
		if (now - lastClickT < 450 && lastClickPile == p && lastClickIdx == i && i == q.n - 1)
		{
			lastClickT = 0;
			if (toFoundation (p)) { redraw (); return; }
		}
		lastClickT = now; lastClickPile = p; lastClickIdx = i;
		// what can be picked up: the waste's / a foundation's top, a face-up run of a column
		if (p == WASTE || (p >= FOUND && p < TAB)) i = q.n - 1;
		if (!q.up[i]) { if (i == q.n - 1 && p >= TAB) { push_undo (); q.up[i] = true; s.score += 5; redraw (); } return; }
		int cx, cy; cardPos (p, i, cx, cy);
		dragging = true; dPile = p; dIdx = i; dOffX = x - cx; dOffY = y - cy; pressX = x; pressY = y; moved = false;
	}
	void move (int x, int y) override
	{
		if (!dragging) return;
		if (x - pressX > 3 || pressX - x > 3 || y - pressY > 3 || pressY - y > 3) moved = true;
		redraw ();
	}
	void release (int x, int y, bool right) override
	{
		if (right || !dragging) return;
		dragging = false;
		if (!moved) { redraw (); return; }
		// the target: the pile whose area contains the dragged card's centre
		int cx = x - dOffX + CARD_W / 2, cy = y - dOffY + CARD_H / 2;
		int card = s.p[dPile].c[dIdx], n = s.p[dPile].n - dIdx;
		for (int p = FOUND; p < NPILE; p++)
		{
			if (p == dPile) continue;
			int px = pileX (p), py0 = p < TAB ? TOPY : TABY, py1 = py0 + CARD_H;
			if (p >= TAB && s.p[p].n) { int tx, ty; cardPos (p, s.p[p].n - 1, tx, ty); py1 = ty + CARD_H; }
			if (cx < px - 8 || cx >= px + CARD_W + 8 || cy < py0 - 8 || cy >= py1 + 8) continue;
			if (p < TAB) { if (n == 1 && canFound (card, p)) { move (dPile, dIdx, p); break; } }
			else if (canTab (card, p)) { move (dPile, dIdx, p); break; }
		}
		redraw ();
	}
	bool key (long k) override
	{
		if (k == WK_CTRL ('Z')) { do_undo (); return true; }
		if (k == ' ') { turnStock (); return true; }
		return false;
	}
	void tick (unsigned) override
	{
		if (animOn) { redraw (); return; }
		if (running) { unsigned e = (gms () - t0) / 1000; if (e != elapsed) { elapsed = e; redraw (); } }
	}

	void paint () override
	{
		Canvas &c = canvas;
		if (animOn)
		{
			if (!win_step (anim, c, W, H)) { animOn = false; gtext_c (c, W / 2, H / 2 - 20, "You win! Click to play again", 0x00FFFF80, 1); }
			return;
		}
		c.fillRect (0, 0, W, H, 0x00207830);
		// top row
		if (s.p[STOCK].n) { card_back (c, pileX (STOCK), TOPY); if (s.p[STOCK].n > 1) c.frameRect (pileX (STOCK) + 2, TOPY - 2, CARD_W - 4, 1, 0x00F0F0F0); }
		else card_slot (c, pileX (STOCK), TOPY, -1, s.p[WASTE].n ? "O" : "");
		card_slot (c, pileX (WASTE), TOPY);
		for (int f = 0; f < 4; f++) card_slot (c, pileX (FOUND + f), TOPY, f);
		for (int t = 0; t < 7; t++) card_slot (c, pileX (TAB + t), TABY);
		for (int p = WASTE; p < NPILE; p++)
		{
			const Pile &q = s.p[p];
			int from = 0;
			if (p == WASTE) from = draw3 ? (q.n - 3 < 0 ? 0 : q.n - 3) : (q.n - 1 < 0 ? 0 : q.n - 1);
			if (p >= FOUND && p < TAB) from = q.n - 2 < 0 ? 0 : q.n - 2;
			for (int i = from; i < q.n; i++)
			{
				if (dragging && moved && p == dPile && i >= dIdx) break;
				int x, y; cardPos (p, i, x, y);
				card_draw (c, x, y, q.c[i], q.up[i]);
			}
		}
		// the dragged run
		if (dragging && moved)
		{
			const Pile &q = s.p[dPile];
			for (int i = dIdx; i < q.n; i++) card_face (c, mx - dOffX, my - dOffY + (i - dIdx) * UP_DY, q.c[i]);
		}
		// status line
		char t[80]; t[0] = 0;
		gcat (t, "Score: "); gcatn (t, s.score);
		gcat (t, "    Time: "); gcatn (t, (long) elapsed);
		gcat (t, draw3 ? "    Draw three" : "    Draw one");
		gtext (c, GAP, H - 20, t, 0x00E0F0E0);
		if (won && !animOn) gtext_c (c, W / 2, H / 2 - 20, "You win! Click to play again", 0x00FFFF80, 1);
	}
};

static Klondike *g_game;
static void on_new (void) { g_game->deal (); }
static void on_undo (void) { g_game->do_undo (); }
static void on_auto (void) { g_game->autoAll (); }
static void on_draw1 (void) { g_game->draw3 = false; g_game->deal (); }
static void on_draw3 (void) { g_game->draw3 = true; g_game->deal (); }
static void on_sound (void) { sfx_set_mute (!g_sfx_mute); }

int main (void)
{
	GameRoot root (W, H, "Solitaire");
	if (root.canvas.px == 0) return 1;
	g_game = new Klondike (0, 0, W, H);
	root.addChild (g_game);
	root.view = g_game;
	static Menu menu;
	menu.menu ("Game");
	menu.item ("Deal",                "^N", WK_CTRL ('N'), on_new);
	menu.item ("Undo",                "^Z", WK_CTRL ('Z'), on_undo);
	menu.item ("Cards to Foundations", "",  0,             on_auto);
	menu.separator ();
	menu.item ("Draw One",            "",   0,             on_draw1);
	menu.item ("Draw Three",          "",   0,             on_draw3);
	menu.item ("Sound On / Off",      "",   0,             on_sound);
	menu.publish ();
	g_game->setFocus ();
	root.run ();
	return 0;
}
