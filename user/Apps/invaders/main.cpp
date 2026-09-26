//
// invaders/main.cpp -- Space Invaders. Left / Right arrows (held) or the mouse move the
// cannon, Space or a click fires (one shot at a time, like the arcade). The fleet marches
// faster as it thins out; the shields crumble under fire from both sides; a saucer
// crosses the top now and then (mystery points). An alien reaching the ground ends the
// game. P pauses. The four-note march and the effects play on the synth.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "game.h"

using namespace wtk;

#define W	448
#define H	480
#define HUD	26
#define S	2			// sprite pixel scale
#define ACOLS	11
#define AROWS	5
#define ASTEPX	32			// fleet spacing
#define ASTEPY	26
#define GROUND	(H - 30)
#define SHIPY	(GROUND - 22)
#define NSHIELD	4
#define SHW	22			// shield size in (unscaled) pixels
#define SHH	16

// Sprites: '#' = pixel. Two frames each (the march).
static const char *const SQUID[2][8] = {
	{ "...##...", "..####..", ".######.", "##.##.##", "########", "..#..#..", ".#.##.#.", "#.#..#.#" },
	{ "...##...", "..####..", ".######.", "##.##.##", "########", ".#.##.#.", "#......#", ".#....#." } };
static const char *const CRAB[2][8] = {
	{ "..#.....#..", "...#...#...", "..#######..", ".##.###.##.", "###########", "#.#######.#", "#.#.....#.#", "...##.##..." },
	{ "..#.....#..", "#..#...#..#", "#.#######.#", "###.###.###", "###########", ".#########.", "..#.....#..", ".#.......#." } };
static const char *const OCTO[2][8] = {
	{ "....####....", ".##########.", "############", "###..##..###", "############", "...##..##...", "..##.##.##..", "##........##" },
	{ "....####....", ".##########.", "############", "###..##..###", "############", "..###..###..", ".##..##..##.", "..##....##.." } };
static const char *const SHIP[8] = {
	"......#......", ".....###.....", ".....###.....", ".###########.", "#############", "#############", "#############", "#############" };
static const char *const UFO[7] = {
	".....######.....", "...##########...", "..############..", ".##.##.##.##.##.", "################", "..###..##..###..", "...#........#..." };
static const char *const BOOM[8] = {
	"#...#..#...#", ".#...##...#.", "..#......#..", "##........##", "..#......#..", ".#..#..#..#.", "#...#..#...#", "............" };

static void sprite (Canvas &c, int x, int y, const char *const *rows, int n, unsigned col, int sc = S)
{
	for (int r = 0; r < n; r++)
		for (int i = 0; rows[r][i]; i++)
			if (rows[r][i] == '#') c.fillRect (x + i * sc, y + r * sc, sc, sc, col);
}
static int spr_w (const char *const *rows) { return wk_len (rows[0]); }

struct Shot { int x, y, vy; bool live; };

class Invaders : public GameView
{
public:
	bool alive[AROWS][ACOLS];
	int  fx, fy, fdir, frame;		// fleet origin, direction, march frame
	unsigned stepT, stepEvery;
	int  marchNote;
	int  shipX, lives, score, hiscore, wave;
	Shot shot, bomb[3];
	unsigned char shield[NSHIELD][SHH][SHW];
	int  ufoX, ufoDir; bool ufoLive; unsigned ufoNext; int ufoScore; unsigned ufoShowT; int ufoShowX;
	int  state;				// 0 play, 1 paused, 2 ship hit, 3 game over, 4 wave clear
	unsigned stateT;
	int  boomA_r, boomA_c; unsigned boomAT;	// last alien explosion
	int  lastMouseX;

	Invaders (int l, int t, int w, int h) : GameView (l, t, w, h), hiscore (0), lastMouseX (-1)
	{ rng_seed (kapi_get_ticks () * 2246822519u); newGame (); }

	void newGame () { score = 0; lives = 3; wave = 0; newWave (); buildShields (); }
	void newWave ()
	{
		for (int r = 0; r < AROWS; r++) for (int c = 0; c < ACOLS; c++) alive[r][c] = true;
		fx = 40; fy = HUD + 60 + (wave % 6) * 12; fdir = 1; frame = 0;
		stepT = kapi_get_ticks (); marchNote = 0;
		shipX = W / 2 - 13; shot.live = false;
		for (int i = 0; i < 3; i++) bomb[i].live = false;
		ufoLive = false; ufoNext = kapi_get_ticks () + 15000 + rng_n (10000); ufoShowT = 0;
		boomAT = 0;
		state = 0;
	}
	void buildShields ()
	{
		static const char *const SH[SHH] = {		// the left half; the right one mirrors it
			"....#######", "...########", "..#########", ".##########", "###########", "###########",
			"###########", "###########", "###########", "###########", "###########", "#######....",
			"######.....", "#####......", "#####......", "#####......" };
		for (int s = 0; s < NSHIELD; s++) for (int y = 0; y < SHH; y++) for (int x = 0; x < SHW; x++)
		{
			int xx = x < SHW / 2 ? x : SHW - 1 - x;
			shield[s][y][x] = SH[y][xx] == '#';
		}
	}
	int shieldX (int s) const { return 44 + s * ((W - 88 - SHW * S) / (NSHIELD - 1)); }
	int shieldY () const { return SHIPY - 30 - SHH * S; }

	int remaining () const { int n = 0; for (int r = 0; r < AROWS; r++) for (int c = 0; c < ACOLS; c++) n += alive[r][c]; return n; }
	const char *const *alienSprite (int r, int f) const { return r == 0 ? SQUID[f] : r < 3 ? CRAB[f] : OCTO[f]; }
	int alienPoints (int r) const { return r == 0 ? 30 : r < 3 ? 20 : 10; }
	int alienX (int r, int c) const { int w = spr_w (alienSprite (r, 0)) * S; return fx + c * ASTEPX + (24 - w) / 2; }
	int alienY (int r) const { return fy + r * ASTEPY; }

	// Erode a shield where a shot hits (a small blast); true if it hit.
	bool hitShield (int x, int y, int down)
	{
		for (int s = 0; s < NSHIELD; s++)
		{
			int sx = (x - shieldX (s)) / S, sy = (y - shieldY ()) / S;
			if (sx < 0 || sx >= SHW || sy < 0 || sy >= SHH || !shield[s][sy][sx]) continue;
			for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++)
			{
				int yy = sy + dy + down, xx = sx + dx;
				if (yy >= 0 && yy < SHH && xx >= 0 && xx < SHW && (dx * dx + dy * dy <= 4) && rng_n (4)) shield[s][yy][xx] = 0;
			}
			return true;
		}
		return false;
	}

	void fire ()
	{
		if (state != 0 || shot.live) return;
		shot.live = true; shot.x = shipX + 12; shot.y = SHIPY - 8; shot.vy = -9;
		sfx (1600, 40, SOUND_SAW, 60); sfx_later (40, 1100, 40, SOUND_SAW, 50);
	}

	void march ()
	{
		int n = remaining ();
		stepEvery = 30 + n * 14 - wave * 20;
		if (stepEvery < 30) stepEvery = 30;
		unsigned now = kapi_get_ticks ();
		if (now - stepT < stepEvery) return;
		stepT = now;
		// edges of the living fleet
		int minx = W, maxx = 0, maxy = 0;
		for (int r = 0; r < AROWS; r++) for (int c = 0; c < ACOLS; c++) if (alive[r][c])
		{
			int x = alienX (r, c), w = spr_w (alienSprite (r, 0)) * S;
			if (x < minx) minx = x;
			if (x + w > maxx) maxx = x + w;
			if (alienY (r) + 16 > maxy) maxy = alienY (r) + 16;
		}
		if ((fdir > 0 && maxx + 8 > W - 8) || (fdir < 0 && minx - 8 < 8)) { fdir = -fdir; fy += 12; }
		else fx += 8 * fdir;
		frame ^= 1;
		static const unsigned notes[4] = { 98, 87, 78, 73 };		// the famous descending bass
		sfx (notes[marchNote], 70, SOUND_SQUARE, 110);
		marchNote = (marchNote + 1) & 3;
		// the fleet eats the shields it walks through, and wins on the ground
		for (int s = 0; s < NSHIELD; s++) for (int y = 0; y < SHH; y++)
			if (shieldY () + y * S < maxy) for (int x = 0; x < SHW; x++)
			{
				int px = shieldX (s) + x * S;
				for (int r = 0; r < AROWS; r++) for (int c = 0; c < ACOLS; c++) if (alive[r][c])
				{
					int ax = alienX (r, c), ay = alienY (r);
					if (px >= ax && px < ax + 24 && shieldY () + y * S >= ay && shieldY () + y * S < ay + 16) shield[s][y][x] = 0;
				}
			}
		if (maxy >= SHIPY) { lives = 0; shipHit (); }
	}

	void alienFire ()
	{
		int chance = 2 + wave;			// per tick, out of 100
		if (rng_n (100) >= chance) return;
		int c = rng_n (ACOLS);
		for (int r = AROWS - 1; r >= 0; r--) if (alive[r][c])
		{
			for (int i = 0; i < 3; i++) if (!bomb[i].live)
			{
				bomb[i].live = true; bomb[i].x = alienX (r, c) + 10; bomb[i].y = alienY (r) + 16;
				bomb[i].vy = 3 + (wave > 3 ? 1 : 0) + rng_n (2);
				return;
			}
			return;
		}
	}

	void shipHit ()
	{
		state = 2; stateT = kapi_get_ticks ();
		sfx (120, 400, SOUND_NOISE, 160);
		if (--lives <= 0) { lives = 0; state = 3; if (score > hiscore) hiscore = score; sfx_lose (); }
		shot.live = false;
		for (int i = 0; i < 3; i++) bomb[i].live = false;
	}

	void tick (unsigned dt) override
	{
		(void) dt;
		unsigned now = kapi_get_ticks ();
		if (state == 1) return;
		if (state == 2) { if (now - stateT > 1200) state = 0; redraw (); return; }
		if (state == 3) { redraw (); return; }
		if (state == 4) { if (now - stateT > 1500) { wave++; newWave (); } redraw (); return; }

		// cannon
		int d = 0;
		if (kapi_key_held (KEY_LEFT)) d -= 4;
		if (kapi_key_held (KEY_RIGHT)) d += 4;
		if (d) shipX += d;
		else if (mx != lastMouseX && mx >= 0) shipX = mx - 13;
		lastMouseX = mx;
		if (shipX < 8) shipX = 8;
		if (shipX > W - 8 - 26) shipX = W - 8 - 26;
		if (kapi_key_held (' ')) fire ();

		march ();
		if (state != 0) { redraw (); return; }
		alienFire ();

		// our shot
		if (shot.live)
		{
			shot.y += shot.vy;
			if (shot.y < HUD + 4) shot.live = false;
			else if (hitShield (shot.x, shot.y, -1)) shot.live = false;
			else
			{
				for (int r = 0; r < AROWS && shot.live; r++) for (int c = 0; c < ACOLS; c++) if (alive[r][c])
				{
					int ax = alienX (r, c), ay = alienY (r), w = spr_w (alienSprite (r, 0)) * S;
					if (shot.x >= ax && shot.x < ax + w && shot.y >= ay && shot.y < ay + 16)
					{
						alive[r][c] = false; shot.live = false; score += alienPoints (r);
						boomA_r = ay; boomA_c = ax; boomAT = now;
						sfx (220, 90, SOUND_NOISE, 120);
						break;
					}
				}
				for (int i = 0; i < 3 && shot.live; i++)		// shots can collide
					if (bomb[i].live && shot.x >= bomb[i].x - 3 && shot.x <= bomb[i].x + 3 && shot.y <= bomb[i].y + 10 && shot.y >= bomb[i].y - 4)
					{ bomb[i].live = false; shot.live = false; }
				if (shot.live && ufoLive && shot.x >= ufoX && shot.x < ufoX + 32 && shot.y >= HUD + 10 && shot.y < HUD + 24)
				{
					static const int pts[] = { 50, 100, 150, 300 };
					ufoScore = pts[rng_n (4)]; score += ufoScore;
					ufoLive = false; shot.live = false; ufoShowT = now; ufoShowX = ufoX;
					sfx (880, 80); sfx_later (90, 660, 80); sfx_later (180, 440, 120);
				}
			}
		}
		// bombs
		for (int i = 0; i < 3; i++) if (bomb[i].live)
		{
			bomb[i].y += bomb[i].vy;
			if (bomb[i].y > GROUND) bomb[i].live = false;
			else if (hitShield (bomb[i].x, bomb[i].y + 8, 1)) bomb[i].live = false;
			else if (bomb[i].x >= shipX && bomb[i].x < shipX + 26 && bomb[i].y + 8 >= SHIPY && bomb[i].y < SHIPY + 16)
			{ bomb[i].live = false; shipHit (); break; }
		}
		// the saucer
		if (!ufoLive && now > ufoNext && remaining () > 6)
		{
			ufoLive = true; ufoDir = rng_n (2) ? 1 : -1; ufoX = ufoDir > 0 ? -32 : W;
			ufoNext = now + 20000 + rng_n (15000);
		}
		if (ufoLive)
		{
			ufoX += ufoDir * 2;
			if ((now / 120) & 1) sfx (ufoDir > 0 ? 740 : 700, 60, SOUND_TRIANGLE, 50);
			if (ufoX < -40 || ufoX > W + 8) ufoLive = false;
		}
		if (remaining () == 0) { state = 4; stateT = now; sfx_win (); if (score > hiscore) hiscore = score; }
		redraw ();
	}

	void press (int, int, bool right) override
	{
		if (right) return;
		if (state == 3) { newGame (); return; }
		fire ();
	}
	bool key (long k) override
	{
		if (k == 'p' || k == 'P') { if (state == 0) state = 1; else if (state == 1) state = 0; redraw (); return true; }
		if (k == ' ' || k == KEY_ENTER) { if (state == 3) newGame (); else if (state == 1) state = 0; else fire (); return true; }
		return false;
	}

	void paint () override
	{
		Canvas &c = canvas;
		unsigned now = kapi_get_ticks ();
		c.fillRect (0, 0, W, H, 0x00000008);
		// a few stars
		for (int i = 0; i < 40; i++) c.pixel ((i * 97 + 13) % W, HUD + (i * 57 + 29) % (GROUND - HUD), 0x00404060);
		// HUD
		char t[48];
		t[0] = 0; gcat (t, "SCORE "); gcatn (t, score); gtext (c, 8, 5, t, 0x00FFFFFF);
		t[0] = 0; gcat (t, "HI "); gcatn (t, hiscore); gtext_c (c, W / 2, 5, t, 0x00FFFFFF, 1, 0);
		t[0] = 0; gcat (t, "WAVE "); gcatn (t, wave + 1); gtext (c, W - 90, 5, t, 0x0080C0FF);
		c.fillRect (0, HUD - 2, W, 1, 0x00303050);
		// fleet
		for (int r = 0; r < AROWS; r++) for (int col = 0; col < ACOLS; col++) if (alive[r][col])
		{
			unsigned colr = r == 0 ? 0x00F060F0 : r < 3 ? 0x0060E0F0 : 0x0060F060;
			sprite (c, alienX (r, col), alienY (r), alienSprite (r, frame), 8, colr);
		}
		if (boomAT && now - boomAT < 200) sprite (c, boomA_c, boomA_r, BOOM, 8, 0x00FFFFFF);
		// saucer
		if (ufoLive) sprite (c, ufoX, HUD + 10, UFO, 7, 0x00FF4040);
		if (ufoShowT && now - ufoShowT < 1000) { char s[8]; gitoa (ufoScore, s); gtext (c, ufoShowX, HUD + 8, s, 0x00FF4040); }
		// shields
		for (int s = 0; s < NSHIELD; s++) for (int y = 0; y < SHH; y++) for (int x = 0; x < SHW; x++)
			if (shield[s][y][x]) c.fillRect (shieldX (s) + x * S, shieldY () + y * S, S, S, 0x0040E040);
		// cannon
		if (state == 2 && ((now / 100) & 1)) sprite (c, shipX, SHIPY, BOOM, 8, 0x0040FF40);
		else if (state != 3) sprite (c, shipX, SHIPY, SHIP, 8, 0x0040FF40);
		// shots
		if (shot.live) c.fillRect (shot.x - 1, shot.y, 2, 8, 0x00FFFFFF);
		for (int i = 0; i < 3; i++) if (bomb[i].live)
			for (int k = 0; k < 4; k++) c.fillRect (bomb[i].x - 1 + (((bomb[i].y / 4 + k) & 1) ? 2 : 0), bomb[i].y + k * 2, 2, 2, 0x00FFE080);
		// ground + lives
		c.fillRect (0, GROUND, W, 2, 0x0040E040);
		t[0] = 0; gcatn (t, lives); gtext (c, 8, GROUND + 6, t, 0x00FFFFFF);
		for (int i = 0; i < lives - 1 && i < 5; i++) sprite (c, 28 + i * 32, GROUND + 8, SHIP, 8, 0x0040FF40, 1);
		if (state == 1) gtext_c (c, W / 2, H / 2 - 16, "PAUSED", 0x00FFFF80, 2);
		if (state == 3) { gtext_c (c, W / 2, H / 2 - 30, "GAME OVER", 0x00FF6060, 2); gtext_c (c, W / 2, H / 2 + 10, "Space: new game", 0x00FFFFFF); }
		if (state == 4) { char m[24] = "WAVE "; gcatn (m, wave + 1); gcat (m, " CLEARED"); gtext_c (c, W / 2, H / 2 - 16, m, 0x0080FF80, 2); }
	}
};

static Invaders *g_game;
static void on_new (void) { g_game->newGame (); g_game->redraw (); }
static void on_pause (void) { g_game->key ('p'); }
static void on_sound (void) { sfx_set_mute (!g_sfx_mute); }

int main (void)
{
	GameRoot root (W, H, "Invaders");
	if (root.canvas.px == 0) return 1;
	g_game = new Invaders (0, 0, W, H);
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
