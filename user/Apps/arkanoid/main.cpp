//
// arkanoid/main.cpp -- break the bricks with the ball. The paddle follows the mouse, or
// the Left / Right arrows (held); Space or a click launches the ball (and releases it
// when "Catch" holds it); P pauses. Coloured bricks break in one hit, silver ones take
// several, gold ones never break. Broken bricks sometimes drop a capsule -- catch it with
// the paddle: E expand, S slow, D disruption (3 balls), C catch, L extra life.
// Clear the breakable bricks to reach the next of the 8 rounds. Sound effects on the synth.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "game.h"

using namespace wtk;

#define COLS	13
#define ROWS	18
#define BW	32			// brick size
#define BH	14
#define WALL	12
#define HUD	26
#define W	(WALL * 2 + COLS * BW)
#define H	470
#define TOPY	(HUD + WALL + 26)	// first brick row
#define PADY	(H - 34)
#define PADH	10
#define BALL	8
#define FP	256			// fixed point

// Bricks: '.' none; w o c g r b m y = colours (1 hit); s = silver; G = gold (unbreakable).
static const char *const LEVELS[] = {
	// 1
	"............."
	"............."
	"sssssssssssss"
	"rrrrrrrrrrrrr"
	"yyyyyyyyyyyyy"
	"bbbbbbbbbbbbb"
	"mmmmmmmmmmmmm"
	"ggggggggggggg",
	// 2
	"w............"
	"wo..........."
	"woc.........."
	"wocg........."
	"wocgr........"
	"wocgrb......."
	"wocgrbm......"
	"wocgrbmy....."
	"wocgrbmyw...."
	"wocgrbmywo..."
	"wocgrbmywoc.."
	"wocgrbmywocg."
	"ssssssssssssr",
	// 3
	"............."
	"ggggggggggggg"
	"............."
	"wwwGGGGGGGGGG"
	"............."
	"rrrrrrrrrrrrr"
	"............."
	"GGGGGGGGGGwww"
	"............."
	"bbbbbbbbbbbbb"
	"............."
	"cccGGGGGGGGGG",
	// 4
	"............."
	".oc.gyc.bgr.."
	".cg.ycb.gro.."
	".gy.cbg.rob.."
	".yc.bgr.obg.."
	".cb.gro.bgy.."
	".bg.rob.gyc.."
	".gr.obg.ycb.."
	".ro.bgy.cbg.."
	".ob.gyc.bgr.."
	".bg.ycb.gro.."
	".gy.cbg.rob..",
	// 5
	"............."
	"...y.....y..."
	"...y.....y..."
	"....y...y...."
	"....y...y...."
	"...sssssss..."
	"...sssssss..."
	"..ssrsssrss.."
	"..ssrsssrss.."
	".sssssssssss."
	".sssssssssss."
	".s.sssssss.s."
	".s.s.....s.s."
	"....ss.ss....",
	// 6
	"............."
	"b.g.b.g.b.g.b"
	"b.g.b.g.b.g.b"
	"b.g.b.g.b.g.b"
	"b.g.b.g.b.g.b"
	"G.G.G.G.G.G.G"
	"b.g.b.g.b.g.b"
	"b.g.b.g.b.g.b"
	"b.g.b.g.b.g.b"
	"b.g.b.g.b.g.b"
	"o.m.o.m.o.m.o",
	// 7
	"............."
	".....sss....."
	"....syyys...."
	"...syyyyys..."
	"..syyrrryys.."
	"..syrrbrrys.."
	"..syyrrryys.."
	"...syyyyys..."
	"....syyys...."
	".....sss....."
	"............."
	"GGGGG...GGGGG",
	// 8
	"............."
	"sGsGsGsGsGsGs"
	"............."
	"rrrrrrrrrrrrr"
	"ooooooooooooo"
	"yyyyyyyyyyyyy"
	"ggggggggggggg"
	"ccccccccccccc"
	"bbbbbbbbbbbbb"
	"mmmmmmmmmmmmm"
	"............."
	"GsGsGsGsGsGsG",
};
#define NLEVELS ((int) (sizeof LEVELS / sizeof LEVELS[0]))

static unsigned brick_color (char c)
{
	switch (c)
	{
	case 'w': return 0x00F0F0F0; case 'o': return 0x00FF9020; case 'c': return 0x0040E0F0;
	case 'g': return 0x0040D040; case 'r': return 0x00E83030; case 'b': return 0x003060F0;
	case 'm': return 0x00E040E0; case 'y': return 0x00F0E030; case 's': return 0x00A8A8B0;
	case 'G': return 0x00D0A020;
	}
	return 0;
}
static int brick_points (char c)
{
	switch (c) { case 'w': return 50; case 'o': return 60; case 'c': return 70; case 'g': return 80;
		     case 'r': return 90; case 'b': return 100; case 'm': return 110; case 'y': return 120; }
	return 50;
}

struct Ball { int x, y, vx, vy; bool live, stuck; int stuckOff; };
struct Capsule { int x, y; char kind; bool live; };

class Arkanoid : public GameView
{
public:
	char brick[ROWS][COLS];
	unsigned char hits[ROWS][COLS];
	Ball ball[3];
	Capsule cap[4];
	int  level, lives, score, hiscore;
	int  padX, padW;			// paddle (left, width) in px
	int  speed;				// ball speed (px/s * FP / 60)
	int  slowT, catchOn;
	int  state;				// 0 play, 1 paused, 2 level clear, 3 game over, 4 won all
	unsigned stateT;
	int  lastMouseX;

	Arkanoid (int l, int t, int w, int h) : GameView (l, t, w, h), hiscore (0), lastMouseX (-1)
	{ rng_seed (gms () * 2654435761u); newGame (); }

	void newGame () { level = 0; lives = 3; score = 0; loadLevel (); }
	void loadLevel ()
	{
		const char *s = LEVELS[level % NLEVELS];
		int n = wk_len (s);
		for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++)
		{
			int i = r * COLS + c;
			char ch = i < n ? s[i] : '.';
			brick[r][c] = ch == '.' ? 0 : ch;
			hits[r][c] = ch == 's' ? (unsigned char) (2 + level / 4) : 1;
		}
		for (int i = 0; i < 4; i++) cap[i].live = false;
		resetPaddle ();
		state = 0;
	}
	void resetPaddle ()
	{
		padW = 56; padX = (W - padW) / 2; slowT = 0; catchOn = 0;
		speed = (300 + level * 25) * FP / 60;
		for (int i = 0; i < 3; i++) ball[i].live = false;
		Ball &b = ball[0];
		b.live = true; b.stuck = true; b.stuckOff = padW / 2 - BALL / 2 + 6;
		b.x = (padX + b.stuckOff) * FP; b.y = (PADY - BALL) * FP;
		b.vx = speed / 2; b.vy = -speed;
	}
	int breakable () const
	{
		int n = 0;
		for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (brick[r][c] && brick[r][c] != 'G') n++;
		return n;
	}

	// ---- physics ---------------------------------------------------------------------
	void setSpeed (Ball &b, int s)
	{
		// keep the direction, rescale |v| to s (integer approx of the norm)
		int ax = b.vx < 0 ? -b.vx : b.vx, ay = b.vy < 0 ? -b.vy : b.vy;
		int n = ax > ay ? ax + ay / 2 - ay / 8 : ay + ax / 2 - ax / 8;	// ~ sqrt(ax^2+ay^2)
		if (n <= 0) return;
		b.vx = (int) ((long) b.vx * s / n); b.vy = (int) ((long) b.vy * s / n);
		if (b.vy > -FP / 2 && b.vy < FP / 2) b.vy = b.vy < 0 ? -FP / 2 : FP / 2;		// never flat
	}
	bool hitBricks (Ball &b, bool xaxis)
	{
		int x0 = b.x / FP, y0 = b.y / FP;
		int c0 = (x0 - WALL) / BW, c1 = (x0 + BALL - 1 - WALL) / BW;
		int r0 = (y0 - TOPY) / BH, r1 = (y0 + BALL - 1 - TOPY) / BH;
		if (y0 + BALL - 1 < TOPY) return false;
		if (y0 < TOPY) r0 = 0;
		bool hit = false;
		for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++)
		{
			if (r < 0 || r >= ROWS || c < 0 || c >= COLS || !brick[r][c]) continue;
			hit = true;
			char k = brick[r][c];
			if (k == 'G') { sfx (1400, 25, SOUND_TRIANGLE, 110); continue; }
			if (--hits[r][c] > 0) { sfx (1200, 30, SOUND_SQUARE, 70); continue; }
			brick[r][c] = 0;
			score += k == 's' ? 50 * (level + 1) : brick_points (k);
			sfx (500 + (ROWS - r) * 40, 40);
			if (rng_n (100) < 12) dropCapsule (WALL + c * BW + BW / 2, TOPY + r * BH);
		}
		(void) xaxis;
		return hit;
	}
	void dropCapsule (int x, int y)
	{
		for (int i = 0; i < 4; i++) if (!cap[i].live)
		{
			static const char kinds[] = "EESSDDCL";
			cap[i].live = true; cap[i].x = x - 12; cap[i].y = y; cap[i].kind = kinds[rng_n (8)];
			return;
		}
	}
	void stepBall (Ball &b)
	{
		if (!b.live) return;
		if (b.stuck) { b.x = (padX + b.stuckOff) * FP; b.y = (PADY - BALL) * FP; return; }
		int steps = 1 + ((b.vx < 0 ? -b.vx : b.vx) + (b.vy < 0 ? -b.vy : b.vy)) / (2 * FP);
		for (int s = 0; s < steps && b.live; s++)
		{
			// X
			b.x += b.vx / steps;
			if (b.x < WALL * FP) { b.x = WALL * FP; b.vx = -b.vx; sfx (330, 20); }
			if (b.x > (W - WALL - BALL) * FP) { b.x = (W - WALL - BALL) * FP; b.vx = -b.vx; sfx (330, 20); }
			if (hitBricks (b, true)) { b.x -= b.vx / steps; b.vx = -b.vx; }
			// Y
			b.y += b.vy / steps;
			if (b.y < (HUD + WALL) * FP) { b.y = (HUD + WALL) * FP; b.vy = -b.vy; sfx (330, 20); }
			if (hitBricks (b, false)) { b.y -= b.vy / steps; b.vy = -b.vy; }
			// paddle
			int bx = b.x / FP, by = b.y / FP;
			if (b.vy > 0 && by + BALL >= PADY && by + BALL <= PADY + PADH && bx + BALL > padX && bx < padX + padW)
			{
				// angle from the hit position: -60..+60 degrees
				int rel = (bx + BALL / 2) - (padX + padW / 2);		// -padW/2 .. padW/2
				int sp = slowT > 0 ? speed * 2 / 3 : speed;
				b.vx = (int) ((long) sp * rel * 17 / (padW * 10));	// tan-ish
				b.vy = -sp;
				setSpeed (b, sp);
				b.y = (PADY - BALL) * FP;
				sfx (440, 30, SOUND_SQUARE, 80);
				if (catchOn) { b.stuck = true; b.stuckOff = bx - padX; }
			}
			if (by > H) { b.live = false; }
		}
	}

	void loseBall ()
	{
		lives--;
		sfx_lose ();
		if (lives <= 0) { state = 3; stateT = gms (); if (score > hiscore) hiscore = score; }
		else resetPaddle ();
	}

	void launch ()
	{
		for (int i = 0; i < 3; i++) if (ball[i].live && ball[i].stuck)
		{
			ball[i].stuck = false;
			if (ball[i].vy > 0) ball[i].vy = -ball[i].vy;
			sfx (660, 30);
		}
	}

	void collect (char k)
	{
		score += 1000;
		sfx (988, 60); sfx_later (70, 1319, 90);
		switch (k)
		{
		case 'E': padW = 88; if (padX + padW > W - WALL) padX = W - WALL - padW; catchOn = 0; break;
		case 'S': slowT = 600; for (int i = 0; i < 3; i++) if (ball[i].live) setSpeed (ball[i], speed * 2 / 3); break;
		case 'C': catchOn = 1; padW = 56; break;
		case 'L': lives++; break;
		case 'D':
		{
			int src = -1;
			for (int i = 0; i < 3; i++) if (ball[i].live) { src = i; break; }
			if (src < 0) break;
			for (int i = 0, k2 = 0; i < 3; i++) if (!ball[i].live)
			{
				ball[i] = ball[src]; ball[i].stuck = false;
				ball[i].vx = (k2++ ? 1 : -1) * speed / 2; ball[i].vy = -speed;
				setSpeed (ball[i], speed);
			}
			break;
		}
		}
	}

	void tick (unsigned dt) override
	{
		(void) dt;
		if (state == 1) return;
		if (state == 2 || state == 3 || state == 4) { redraw (); return; }
		// paddle: held arrows, else the mouse
		int d = 0;
		if (kapi_key_held (KEY_LEFT)) d -= 7;
		if (kapi_key_held (KEY_RIGHT)) d += 7;
		if (d) padX += d;
		else if (mx != lastMouseX && mx >= 0) { padX = mx - padW / 2; }
		lastMouseX = mx;
		if (padX < WALL) padX = WALL;
		if (padX > W - WALL - padW) padX = W - WALL - padW;
		if (kapi_key_held (' ')) launch ();

		if (slowT > 0 && --slowT == 0) for (int i = 0; i < 3; i++) if (ball[i].live) setSpeed (ball[i], speed);
		for (int i = 0; i < 3; i++) stepBall (ball[i]);
		bool any = false;
		for (int i = 0; i < 3; i++) any |= ball[i].live;
		if (!any) { loseBall (); redraw (); return; }

		for (int i = 0; i < 4; i++) if (cap[i].live)
		{
			cap[i].y += 2;
			if (cap[i].y + 10 >= PADY && cap[i].y <= PADY + PADH && cap[i].x + 24 > padX && cap[i].x < padX + padW)
			{ cap[i].live = false; collect (cap[i].kind); }
			else if (cap[i].y > H) cap[i].live = false;
		}
		if (breakable () == 0)
		{
			sfx_win ();
			state = level + 1 >= NLEVELS ? 4 : 2; stateT = gms ();
			if (score > hiscore) hiscore = score;
		}
		redraw ();
	}

	void advance ()
	{
		if (state == 2 && gms () - stateT > 600) { level++; loadLevel (); }
		else if ((state == 3 || state == 4) && gms () - stateT > 600) newGame ();
	}
	void press (int, int, bool right) override
	{
		if (right) return;
		if (state == 0) launch (); else advance ();
	}
	bool key (long k) override
	{
		if (k == 'p' || k == 'P') { if (state == 0) state = 1; else if (state == 1) state = 0; redraw (); return true; }
		if (k == ' ' || k == KEY_ENTER) { if (state == 0) launch (); else if (state == 1) state = 0; else advance (); return true; }
		return false;
	}

	// ---- drawing ---------------------------------------------------------------------
	void bevel (int x, int y, int w, int h, unsigned c)
	{
		unsigned hi = ((c >> 1) & 0x7F7F7F) + 0x808080, lo = (c >> 1) & 0x7F7F7F;
		canvas.fillRect (x, y, w, h, c);
		canvas.fillRect (x, y, w, 1, hi); canvas.fillRect (x, y, 1, h, hi);
		canvas.fillRect (x, y + h - 1, w, 1, lo); canvas.fillRect (x + w - 1, y, 1, h, lo);
	}
	void paint () override
	{
		Canvas &c = canvas;
		// background: a dark diagonal pattern
		for (int y = HUD; y < H; y++)
		{
			unsigned col = ((y / 16) & 1) ? 0x00101A38 : 0x000C1430;
			c.fillRect (0, y, W, 1, col);
		}
		for (int y = HUD + WALL; y < H; y += 32) for (int x = WALL + ((y / 32) & 1) * 16; x < W - WALL; x += 32)
			c.fillRect (x, y, 2, 2, 0x00243060);
		// walls
		c.fillRect (0, HUD, W, WALL, 0x00707888); c.fillRect (0, HUD, WALL, H - HUD, 0x00707888);
		c.fillRect (W - WALL, HUD, WALL, H - HUD, 0x00707888);
		c.fillRect (0, HUD + WALL - 2, W, 2, 0x00404858);
		// HUD
		c.fillRect (0, 0, W, HUD, 0x00000000);
		char t[64];
		t[0] = 0; gcat (t, "SCORE "); gcatn (t, score); gtext (c, 8, 5, t, 0x00FFFFFF);
		t[0] = 0; gcat (t, "HI "); gcatn (t, hiscore); gtext_c (c, W / 2, 5, t, 0x00FF6060, 1, 0);
		t[0] = 0; gcat (t, "ROUND "); gcatn (t, level + 1); gtext (c, W - 150, 5, t, 0x0080C0FF);
		for (int i = 0; i < lives - 1 && i < 6; i++) c.fillRect (W - 60 + i * 9, 10, 7, 4, 0x00E0E0E0);
		// bricks
		for (int r = 0; r < ROWS; r++) for (int col = 0; col < COLS; col++) if (brick[r][col])
		{
			int x = WALL + col * BW, y = TOPY + r * BH;
			bevel (x + 1, y + 1, BW - 2, BH - 2, brick_color (brick[r][col]));
			if (brick[r][col] == 's' && hits[r][col] > 1) c.fillRect (x + BW / 2 - 2, y + 5, 4, 3, 0x00707078);
		}
		// capsules
		for (int i = 0; i < 4; i++) if (cap[i].live)
		{
			unsigned col = cap[i].kind == 'E' ? 0x003060F0 : cap[i].kind == 'S' ? 0x00FF9020 :
				       cap[i].kind == 'D' ? 0x0040E0F0 : cap[i].kind == 'C' ? 0x0040D040 : 0x00A0A0A8;
			bevel (cap[i].x, cap[i].y, 24, 11, col);
			char s[2] = { cap[i].kind, 0 };
			gtext (c, cap[i].x + 8, cap[i].y - 3, s, 0x00FFFFFF, 1, 2);
		}
		// paddle (Vaus): silver body with red caps
		bevel (padX + 6, PADY, padW - 12, PADH, catchOn ? 0x0060C060 : 0x00B8B8C8);
		bevel (padX, PADY, 8, PADH, 0x00E03030);
		bevel (padX + padW - 8, PADY, 8, PADH, 0x00E03030);
		// balls
		for (int i = 0; i < 3; i++) if (ball[i].live)
		{
			int x = ball[i].x / FP, y = ball[i].y / FP;
			c.fillRect (x + 2, y, BALL - 4, BALL, 0x00F0F0F0);
			c.fillRect (x, y + 2, BALL, BALL - 4, 0x00F0F0F0);
			c.fillRect (x + 2, y + 2, 2, 2, 0x00FFFFFF);
		}
		// messages
		if (state == 0 && ball[0].live && ball[0].stuck && level == 0 && score == 0)
			gtext_c (c, W / 2, H / 2, "Space or click to launch", 0x00FFFFFF);
		if (state == 1) gtext_c (c, W / 2, H / 2 - 16, "PAUSED", 0x00FFFF80, 2);
		if (state == 2) { char m[32] = "ROUND "; gcatn (m, level + 1); gcat (m, " CLEAR"); gtext_c (c, W / 2, H / 2 - 16, m, 0x0080FF80, 2); }
		if (state == 3) { gtext_c (c, W / 2, H / 2 - 16, "GAME OVER", 0x00FF6060, 2); gtext_c (c, W / 2, H / 2 + 24, "Space: new game", 0x00FFFFFF); }
		if (state == 4) { gtext_c (c, W / 2, H / 2 - 16, "YOU WIN!", 0x00FFE060, 2); gtext_c (c, W / 2, H / 2 + 24, "Space: new game", 0x00FFFFFF); }
	}
};

static Arkanoid *g_game;
static void on_new (void) { g_game->newGame (); g_game->redraw (); }
static void on_pause (void) { g_game->key ('p'); }
static void on_sound (void) { sfx_set_mute (!g_sfx_mute); }

int main (void)
{
	GameRoot root (W, H, "Arkanoid");
	if (root.canvas.px == 0) return 1;
	g_game = new Arkanoid (0, 0, W, H);
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
