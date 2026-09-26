//
// freecell/main.cpp -- FreeCell. All 52 cards are dealt face up in eight columns; four free
// cells (top left) hold one card each; build the four foundations (top right) up by suit
// from the ace. Drag cards: a column takes a card one lower in the other colour (any card
// on an empty column); a run of several cards moves at once when the free cells and empty
// columns allow it. A double click sends a card to its foundation, else to a free cell.
// Cards that are no longer needed go to the foundations by themselves. Undo with ^Z.
// The deals are numbered 1..32000 and are the same as Microsoft FreeCell's
// (Game > Select Game...).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "cards.h"

using namespace wtk;

#define GAP	10
#define W	(GAP + 8 * (CARD_W + GAP))
#define H	560
#define TOPY	10
#define TABY	(TOPY + CARD_H + 20)
#define UP_DY	22

struct Col { int n; signed char c[52]; };
struct FState { Col col[8]; signed char cell[4]; signed char found[4]; int moves; };	// found = top rank (-1 none)

// Microsoft's deal: its C runtime rand () seeded with the game number.
static void ms_deal (unsigned game, Col col[8])
{
	unsigned seed = game;
	int deck[52];
	for (int i = 0; i < 52; i++) deck[i] = 51 - i;			// MS numbering: rank * 4 + suit (C D H S)
	for (int i = 0; i < 51; i++)
	{
		seed = (seed * 214013u + 2531011u) & 0x7FFFFFFFu;
		int j = 51 - (int) ((seed >> 16) % (unsigned) (52 - i));
		int t = deck[i]; deck[i] = deck[j]; deck[j] = t;
	}
	for (int c = 0; c < 8; c++) col[c].n = 0;
	for (int i = 0; i < 52; i++)
	{
		int ms = deck[i];
		Col &k = col[i % 8];
		k.c[k.n++] = (signed char) card_make (ms / 4, ms % 4);	// our suits: C D H S too
	}
}

class FreeCell : public GameView
{
public:
	FState s;
	FState undo[128]; int nundo;
	Col *col;				// = s.col (the test harness reads it)
	unsigned game;
	bool dragging, moved; int dSrc, dIdx, dOffX, dOffY, pressX, pressY;	// dSrc: 0..7 column, 8..11 cell
	unsigned lastClickT; int lastClickSrc;
	bool won; WinAnim anim; bool animOn;
	char msg[64]; unsigned msgT;

	FreeCell (int l, int t, int w, int h) : GameView (l, t, w, h), col (s.col)
	{ rng_seed (kapi_get_ticks () * 2654435761u + 11); newGame (1 + rng_n (32000)); }

	void newGame (unsigned n)
	{
		game = n; ms_deal (n, s.col);
		for (int i = 0; i < 4; i++) { s.cell[i] = -1; s.found[i] = -1; }
		s.moves = 0; nundo = 0; dragging = false; won = false; animOn = false; lastClickT = 0; msg[0] = 0;
		autoPlay ();
		redraw ();
	}

	int colX (int c) const { return GAP + c * (CARD_W + GAP); }
	int cellX (int i) const { return GAP + i * (CARD_W + GAP); }
	int foundX (int f) const { return GAP + (4 + f) * (CARD_W + GAP); }
	int dy (int c) const
	{
		int d = UP_DY, n = s.col[c].n;
		while (d > 10 && n > 1 && TABY + (n - 1) * d + CARD_H > H - 24) d--;
		return d;
	}
	int cardY (int c, int i) const { return TABY + i * dy (c); }

	int freeCells () const { int n = 0; for (int i = 0; i < 4; i++) n += s.cell[i] < 0; return n; }
	int emptyCols (int except) const { int n = 0; for (int c = 0; c < 8; c++) n += c != except && s.col[c].n == 0; return n; }
	int maxRun (int dest) const { return (freeCells () + 1) << emptyCols (dest); }

	bool canFound (int card) const { return card_rank (card) == s.found[card_suit (card)] + 1; }
	bool canStack (int card, int c) const
	{
		const Col &k = s.col[c];
		if (k.n == 0) return true;
		int t = k.c[k.n - 1];
		return card_red (t) != card_red (card) && card_rank (card) + 1 == card_rank (t);
	}
	bool isRun (int c, int from) const		// [from..top] descending, alternating colours
	{
		const Col &k = s.col[c];
		for (int i = from; i + 1 < k.n; i++)
			if (card_red (k.c[i]) == card_red (k.c[i + 1]) || card_rank (k.c[i]) != card_rank (k.c[i + 1]) + 1) return false;
		return true;
	}

	void pushUndo () { if (nundo == 128) { for (int i = 1; i < 128; i++) undo[i - 1] = undo[i]; nundo--; } undo[nundo++] = s; }
	void doUndo () { if (nundo && !won) { s = undo[--nundo]; sfx (300, 40, SOUND_TRIANGLE); redraw (); } }

	// Safe to put away: nothing left can need it any more.
	bool safe (int card) const
	{
		int r = card_rank (card);
		if (r <= 1) return true;
		bool red = card_red (card);
		for (int f = 0; f < 4; f++)
		{
			bool fr = f == 1 || f == 2;
			if (fr != red && s.found[f] < r - 1) return false;
		}
		return true;
	}
	void autoPlay ()
	{
		bool any = true;
		while (any)
		{
			any = false;
			for (int i = 0; i < 4; i++)
				if (s.cell[i] >= 0 && canFound (s.cell[i]) && safe (s.cell[i]))
				{ s.found[card_suit (s.cell[i])]++; s.cell[i] = -1; any = true; }
			for (int c = 0; c < 8; c++)
			{
				Col &k = s.col[c];
				if (k.n && canFound (k.c[k.n - 1]) && safe (k.c[k.n - 1]))
				{ s.found[card_suit (k.c[k.n - 1])]++; k.n--; any = true; }
			}
		}
		checkWin ();
	}
	void checkWin ()
	{
		for (int f = 0; f < 4; f++) if (s.found[f] != 12) return;
		if (won) return;
		won = true;
		int fx[4], fy[4];
		for (int f = 0; f < 4; f++) { fx[f] = foundX (f); fy[f] = TOPY; }
		win_start (anim, fx, fy); animOn = true;
		sfx_win ();
	}
	void after (bool toFound)
	{
		s.moves++;
		if (toFound) sfx (700, 50); else sfx (520, 25, SOUND_TRIANGLE, 100);
		autoPlay ();
		redraw ();
	}
	void say (const char *m) { int i = 0; for (; m[i] && i < 63; i++) msg[i] = m[i]; msg[i] = 0; msgT = kapi_get_ticks (); sfx (160, 80, SOUND_SQUARE, 60); }

	// The card(s) being moved: source 0..7 column (from index idx), 8..11 a free cell.
	int srcCard (int src, int idx) const { return src < 8 ? s.col[src].c[idx] : s.cell[src - 8]; }
	int srcCount (int src, int idx) const { return src < 8 ? s.col[src].n - idx : 1; }
	void take (int src, int idx) { if (src < 8) s.col[src].n = idx; else s.cell[src - 8] = -1; }

	bool dropOnFound (int src, int idx)
	{
		if (srcCount (src, idx) != 1) return false;
		int card = srcCard (src, idx);
		if (!canFound (card)) return false;
		pushUndo (); take (src, idx); s.found[card_suit (card)]++; after (true);
		return true;
	}
	bool dropOnCell (int src, int idx, int cell)
	{
		if (srcCount (src, idx) != 1 || s.cell[cell] >= 0 || src == 8 + cell) return false;
		int card = srcCard (src, idx);
		pushUndo (); take (src, idx); s.cell[cell] = (signed char) card; after (false);
		return true;
	}
	bool dropOnCol (int src, int idx, int c)
	{
		if (src == c) return false;
		int n = srcCount (src, idx);
		// onto an empty column a long run can be split: move the largest part that fits
		if (s.col[c].n == 0 && src < 8 && n > maxRun (c)) { idx = s.col[src].n - maxRun (c); n = maxRun (c); }
		if (!canStack (srcCard (src, idx), c)) return false;
		if (n > maxRun (c)) { say ("Not enough free cells to move that many cards."); return false; }
		pushUndo ();
		Col &d = s.col[c];
		for (int i = 0; i < n; i++) d.c[d.n++] = (signed char) srcCard (src, src < 8 ? idx + i : idx);
		take (src, idx);
		after (false);
		return true;
	}

	bool hitSrc (int x, int y, int &src, int &idx)
	{
		if (y >= TOPY && y < TOPY + CARD_H)
			for (int i = 0; i < 4; i++) if (x >= cellX (i) && x < cellX (i) + CARD_W && s.cell[i] >= 0) { src = 8 + i; idx = 0; return true; }
		for (int c = 0; c < 8; c++)
		{
			if (x < colX (c) || x >= colX (c) + CARD_W) continue;
			for (int i = s.col[c].n - 1; i >= 0; i--)
				if (y >= cardY (c, i) && y < cardY (c, i) + CARD_H) { src = c; idx = i; return true; }
		}
		return false;
	}

	void press (int x, int y, bool right) override
	{
		if (animOn || won) { if (!animOn) newGame (1 + rng_n (32000)); return; }
		if (right) return;
		int src, idx;
		if (!hitSrc (x, y, src, idx)) return;
		unsigned now = kapi_get_ticks ();
		bool top = src >= 8 || idx == s.col[src].n - 1;
		if (top && now - lastClickT < 450 && lastClickSrc == src)
		{
			lastClickT = 0;
			if (dropOnFound (src, idx)) return;
			if (src < 8) for (int i = 0; i < 4; i++) if (dropOnCell (src, idx, i)) return;
			return;
		}
		lastClickT = now; lastClickSrc = src;
		if (src < 8 && !isRun (src, idx)) return;
		int cx = src < 8 ? colX (src) : cellX (src - 8), cy = src < 8 ? cardY (src, idx) : TOPY;
		dragging = true; moved = false; dSrc = src; dIdx = idx; dOffX = x - cx; dOffY = y - cy; pressX = x; pressY = y;
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
		int cx = x - dOffX + CARD_W / 2, cy = y - dOffY + CARD_H / 2;
		if (cy < TOPY + CARD_H + 10)
		{
			for (int f = 0; f < 4; f++) if (cx >= foundX (f) - 8 && cx < foundX (f) + CARD_W + 8) { dropOnFound (dSrc, dIdx); redraw (); return; }
			for (int i = 0; i < 4; i++) if (cx >= cellX (i) - 8 && cx < cellX (i) + CARD_W + 8) { dropOnCell (dSrc, dIdx, i); redraw (); return; }
		}
		for (int c = 0; c < 8; c++) if (cx >= colX (c) - GAP / 2 && cx < colX (c) + CARD_W + GAP / 2) { dropOnCol (dSrc, dIdx, c); break; }
		redraw ();
	}
	bool key (long k) override
	{
		if (k == WK_CTRL ('Z')) { doUndo (); return true; }
		return false;
	}
	void tick (unsigned) override
	{
		if (animOn) redraw ();
		if (msg[0] && kapi_get_ticks () - msgT > 2500) { msg[0] = 0; redraw (); }
	}

	void paint () override
	{
		Canvas &c = canvas;
		if (animOn)
		{
			if (!win_step (anim, c, W, H)) { animOn = false; gtext_c (c, W / 2, H / 2 - 20, "You win! Click for a new game", 0x00FFFF80); }
			return;
		}
		c.fillRect (0, 0, W, H, 0x00207830);
		for (int i = 0; i < 4; i++)
		{
			card_slot (c, cellX (i), TOPY);
			if (s.cell[i] >= 0 && !(dragging && moved && dSrc == 8 + i)) card_face (c, cellX (i), TOPY, s.cell[i]);
		}
		for (int f = 0; f < 4; f++)
		{
			if (s.found[f] >= 0) card_face (c, foundX (f), TOPY, card_make (s.found[f], f));
			else card_slot (c, foundX (f), TOPY, f);
		}
		for (int k = 0; k < 8; k++)
		{
			const Col &q = s.col[k];
			if (q.n == 0) card_slot (c, colX (k), TABY);
			for (int i = 0; i < q.n; i++)
			{
				if (dragging && moved && dSrc == k && i >= dIdx) break;
				card_face (c, colX (k), cardY (k, i), q.c[i]);
			}
		}
		if (dragging && moved)
		{
			int n = srcCount (dSrc, dIdx);
			for (int i = 0; i < n; i++) card_face (c, mx - dOffX, my - dOffY + i * UP_DY, srcCard (dSrc, dSrc < 8 ? dIdx + i : dIdx));
		}
		char t[96]; t[0] = 0;
		gcat (t, "Game #"); gcatn (t, game);
		gcat (t, "    Moves: "); gcatn (t, s.moves);
		gcat (t, "    Free cells: "); gcatn (t, freeCells ());
		gtext (c, GAP, H - 20, t, 0x00E0F0E0);
		if (msg[0]) gtext_c (c, W / 2, H - 44, msg, 0x00FFE080, 1, 0);
		if (won && !animOn) gtext_c (c, W / 2, H / 2 - 20, "You win! Click for a new game", 0x00FFFF80);
	}
};

// ---- Game > Select Game... ------------------------------------------------------------------
static void sel_btn (Widget &w) { ((Modal *) w.parent)->close (w.tag); }
class SelectDialog : public Modal
{
public:
	Textbox *tb;
	SelectDialog (unsigned cur) : Modal (300, 130)
	{
		left = (W - width) / 2; top = (H - height) / 2;
		char n[16]; gitoa (cur, n);
		tb = new Textbox (150, 40, 120, 26, n); addChild (tb);
		Button *b;
		b = new Button (width - 184, height - 38, 82, 28, "OK", sel_btn); b->tag = 1; addChild (b);
		b = new Button (width - 94, height - 38, 82, 28, "Cancel", sel_btn); b->tag = 0; addChild (b);
		tb->setFocus ();
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { close (1); return true; }
		return false;
	}
	void onDraw () override
	{
		canvas.clear (C_FACE_DN); canvas.frameRect (0, 0, width, height, C_ACCENT);
		canvas.text (10, 6, "Select game", C_TEXT);
		canvas.text (14, 44, "Game number:", C_TEXT);
		canvas.text (14, 70, "(1 to 32000)", C_DIS);
	}
};

static FreeCell *g_game;
static void on_new (void) { g_game->newGame (1 + rng_n (32000)); }
static void on_restart (void) { g_game->newGame (g_game->game); }
static void on_undo (void) { g_game->doUndo (); }
static void on_select (void)
{
	SelectDialog d (g_game->game);
	if (d.run ())
	{
		long n = 0;
		for (int i = 0; d.tb->text[i] >= '0' && d.tb->text[i] <= '9' && n < 100000; i++) n = n * 10 + (d.tb->text[i] - '0');
		if (n >= 1 && n <= 32000) g_game->newGame ((unsigned) n);
	}
	g_game->setFocus ();
}
static void on_sound (void) { sfx_set_mute (!g_sfx_mute); }

int main (void)
{
	GameRoot root (W, H, "FreeCell");
	if (root.canvas.px == 0) return 1;
	g_game = new FreeCell (0, 0, W, H);
	root.addChild (g_game);
	root.view = g_game;
	static Menu menu;
	menu.menu ("Game");
	menu.item ("New Game",        "^N", WK_CTRL ('N'), on_new);
	menu.item ("Select Game...",  "",   0,             on_select);
	menu.item ("Restart Game",    "",   0,             on_restart);
	menu.item ("Undo",            "^Z", WK_CTRL ('Z'), on_undo);
	menu.separator ();
	menu.item ("Sound On / Off",  "",   0,             on_sound);
	menu.publish ();
	char args[16];
	if (kapi_get_args (args, sizeof args) > 0 && args[0] >= '1' && args[0] <= '9')
	{ long n = 0; for (int i = 0; args[i] >= '0' && args[i] <= '9'; i++) n = n * 10 + (args[i] - '0'); if (n >= 1 && n <= 32000) g_game->newGame ((unsigned) n); }
	g_game->setFocus ();
	root.run ();
	return 0;
}
