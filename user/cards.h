//
// cards.h -- playing cards for the card games (Solitaire, FreeCell): drawing (face with
// rank + suit pips, face cards, the back), and the classic "bouncing cards" victory
// animation. A card is 0..51: suit = c / 13 (0 clubs, 1 diamonds, 2 hearts, 3 spades),
// rank = c % 13 (0 = ace .. 12 = king). Needs game.h (gtext).
//
#ifndef _onyx_cards_h
#define _onyx_cards_h

#include "game.h"

#define CARD_W	64
#define CARD_H	88

static inline int  card_suit (int c) { return c / 13; }
static inline int  card_rank (int c) { return c % 13; }
static inline bool card_red (int c) { int s = c / 13; return s == 1 || s == 2; }
static inline int  card_make (int rank, int suit) { return suit * 13 + rank; }

// 9x9 suit pips: clubs, diamonds, hearts, spades.
static const char *const SUIT_PIP[4][9] = {
	{ "...###...", "..#####..", "..#####..", "##.###.##", "#########", "#########", "##..#..##", "....#....", "...###..." },
	{ "....#....", "...###...", "..#####..", ".#######.", "#########", ".#######.", "..#####..", "...###...", "....#...." },
	{ ".##...##.", "####.####", "#########", "#########", ".#######.", "..#####..", "...###...", "....#....", "........." },
	{ "....#....", "...###...", "..#####..", ".#######.", "#########", "#########", ".##.#.##.", "....#....", "...###..." },
};

static inline void card_pip (wtk::Canvas &c, int x, int y, int suit, unsigned col, int sc = 1)
{
	for (int r = 0; r < 9; r++) for (int i = 0; i < 9; i++)
		if (SUIT_PIP[suit][r][i] == '#') c.fillRect (x + i * sc, y + r * sc, sc, sc, col);
}

static inline void card_round_rect (wtk::Canvas &c, int x, int y, int w, int h, unsigned fill, unsigned edge)
{
	c.fillRect (x + 2, y, w - 4, h, fill);
	c.fillRect (x, y + 2, w, h - 4, fill);
	c.fillRect (x + 1, y + 1, w - 2, h - 2, fill);
	c.fillRect (x + 2, y, w - 4, 1, edge); c.fillRect (x + 2, y + h - 1, w - 4, 1, edge);
	c.fillRect (x, y + 2, 1, h - 4, edge); c.fillRect (x + w - 1, y + 2, 1, h - 4, edge);
	c.pixel (x + 1, y + 1, edge); c.pixel (x + w - 2, y + 1, edge);
	c.pixel (x + 1, y + h - 2, edge); c.pixel (x + w - 2, y + h - 2, edge);
}

// An empty slot (a dashed rounded outline), with an optional letter / suit hint.
static inline void card_slot (wtk::Canvas &c, int x, int y, int suitHint = -1, const char *label = 0)
{
	card_round_rect (c, x, y, CARD_W, CARD_H, 0x00206028, 0x0060A068);
	if (suitHint >= 0) card_pip (c, x + CARD_W / 2 - 9, y + CARD_H / 2 - 9, suitHint, 0x0040884A, 2);
	if (label) gtext_c (c, x + CARD_W / 2, y + CARD_H / 2 - 8, label, 0x0060A068, 1, 0);
}

static inline void card_back (wtk::Canvas &c, int x, int y)
{
	card_round_rect (c, x, y, CARD_W, CARD_H, 0x00FFFFFF, 0x00404040);
	c.fillRect (x + 4, y + 4, CARD_W - 8, CARD_H - 8, 0x002050B0);
	for (int yy = y + 6; yy < y + CARD_H - 6; yy += 4)
		for (int xx = x + 6 + ((yy - y) / 4 & 1) * 2; xx < x + CARD_W - 6; xx += 4)
			c.fillRect (xx, yy, 2, 2, 0x004078D8);
	c.frameRect (x + 6, y + 6, CARD_W - 12, CARD_H - 12, 0x0090B8F0);
}

static inline void card_face (wtk::Canvas &c, int x, int y, int card, bool selected = false)
{
	static const char *const RANK[13] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };
	int s = card_suit (card), r = card_rank (card);
	unsigned col = card_red (card) ? 0x00D01818 : 0x00101010;
	card_round_rect (c, x, y, CARD_W, CARD_H, selected ? 0x00C8DCFF : 0x00FFFFFF, 0x00404040);
	// corners
	gtext (c, x + 4, y + 2, RANK[r], col, 1, 2);
	card_pip (c, x + 4, y + 20, s, col);
	gtext (c, x + CARD_W - 4 - gtext_w (RANK[r]), y + CARD_H - 34, RANK[r], col, 1, 2);
	card_pip (c, x + CARD_W - 13, y + CARD_H - 13, s, col);
	// centre
	if (r >= 10)						// J Q K: a framed picture
	{
		unsigned fr = card_red (card) ? 0x00F0C8C8 : 0x00C8D0F0;
		c.fillRect (x + 16, y + 16, CARD_W - 32, CARD_H - 32, fr);
		c.frameRect (x + 16, y + 16, CARD_W - 32, CARD_H - 32, col);
		gtext_c (c, x + CARD_W / 2, y + CARD_H / 2 - 22, RANK[r], col, 2, 2);
		card_pip (c, x + CARD_W / 2 - 9, y + CARD_H / 2 + 8, s, col, 2);
	}
	else if (r == 0) card_pip (c, x + CARD_W / 2 - 13, y + CARD_H / 2 - 13, s, col, 3);
	else
	{
		// pips in two / three columns
		int n = r + 1;
		static const signed char LAY[10][10][2] = {		// (column 0..2, row 0..6) per count
			{ { 1, 3 } }, { { 1, 0 }, { 1, 6 } }, { { 1, 0 }, { 1, 3 }, { 1, 6 } },
			{ { 0, 0 }, { 2, 0 }, { 0, 6 }, { 2, 6 } },
			{ { 0, 0 }, { 2, 0 }, { 1, 3 }, { 0, 6 }, { 2, 6 } },
			{ { 0, 0 }, { 2, 0 }, { 0, 3 }, { 2, 3 }, { 0, 6 }, { 2, 6 } },
			{ { 0, 0 }, { 2, 0 }, { 1, 1 }, { 0, 3 }, { 2, 3 }, { 0, 6 }, { 2, 6 } },
			{ { 0, 0 }, { 2, 0 }, { 1, 1 }, { 0, 3 }, { 2, 3 }, { 1, 5 }, { 0, 6 }, { 2, 6 } },
			{ { 0, 0 }, { 2, 0 }, { 0, 2 }, { 2, 2 }, { 1, 3 }, { 0, 4 }, { 2, 4 }, { 0, 6 }, { 2, 6 } },
			{ { 0, 0 }, { 2, 0 }, { 1, 1 }, { 0, 2 }, { 2, 2 }, { 0, 4 }, { 2, 4 }, { 1, 5 }, { 0, 6 }, { 2, 6 } } };
		for (int i = 0; i < n; i++)
		{
			int px = x + 14 + LAY[n - 1][i][0] * 13, py = y + 14 + LAY[n - 1][i][1] * 9;
			card_pip (c, px, py, s, col);
		}
	}
}

static inline void card_draw (wtk::Canvas &c, int x, int y, int card, bool up, bool selected = false)
{
	if (up) card_face (c, x, y, card, selected); else card_back (c, x, y);
}

// ---- the victory animation: cards bounce off the bottom, leaving trails ----------------
// Paint over the canvas WITHOUT clearing it (the trails are the fun). Call win_start with
// the four foundation positions, then win_step every tick; it returns false when done.
struct WinAnim
{
	int fx[4], fy[4];			// foundation positions
	int next;				// cards launched so far (0..52)
	bool on; int x, y, vx, vy, card;	// the card in flight (fixed point /16)
};
static inline void win_start (WinAnim &a, const int fx[4], const int fy[4])
{
	for (int i = 0; i < 4; i++) { a.fx[i] = fx[i]; a.fy[i] = fy[i]; }
	a.next = 0; a.on = false;
}
static inline bool win_step (WinAnim &a, wtk::Canvas &c, int W, int H)
{
	for (int k = 0; k < 3; k++)
	{
		if (!a.on)
		{
			if (a.next >= 52) return false;
			int f = a.next % 4, rank = 12 - a.next / 4;
			a.card = card_make (rank, f);
			a.x = a.fx[f] * 16; a.y = a.fy[f] * 16;
			a.vx = (rng_n (2) ? 1 : -1) * (40 + rng_n (60)); a.vy = -(rng_n (80));
			a.on = true; a.next++;
		}
		a.x += a.vx; a.y += a.vy; a.vy += 12;
		if (a.y / 16 + CARD_H > H) { a.y = (H - CARD_H) * 16; a.vy = -a.vy * 3 / 4; if (a.vy > -60) a.vy = -60 - rng_n (40); }
		card_face (c, a.x / 16, a.y / 16, a.card);
		if (a.x / 16 + CARD_W < 0 || a.x / 16 > W) a.on = false;
	}
	return true;
}

#endif
