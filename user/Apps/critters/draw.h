//
// draw.h -- Critters' drawing (the window's, included by main.cpp only): the faces, the creatures, the hatch, the
// exit, the bursts, the cursor's brackets, the roles' pictures -- vector drawings (UIKit's VPath, anti-aliased) after
// 04-ux-design.md §5 (the mock-up crmock.cpp's pictures); the creatures pre-rendered once into frames with an opacity
// (drawn on black and on white: the opacity is the difference -- 05-validation note 1) and blended each frame; the
// terrain blitted x2 with its letterbox; the [label] words painted into the terrain's colour layer (D22).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _critters_draw_h
#define _critters_draw_h

#include <math.h>

static int g_lang = 0;				// 0 English, 1 French (the system's, through uk_lang)
static const unsigned AMBER = 0xB06A00, GREEN = 0x2E8A3E, RED = 0xC0302A;	// (the status line's message, enough saved, an error)

// ---- the faces: DejaVu Sans at any size (made on first use) -------------------------------------------------------
static FtTextFace *g_face[64];
static TextFace *face (int px)
{
	if (px < 6) px = 6;
	if (px > 63) px = 63;
	if (!g_face[px]) { FtTextFace *f = new FtTextFace; if (!f->open ("DejaVu Sans", px)) { delete f; return 0; } g_face[px] = f; }
	return g_face[px];
}
static void mmss (int s, char *b, int cap) { if (s < 0) s = 0; snprintf (b, cap, "%d:%02d", s / 60, s % 60); }

// ---- the roles' names (the slots' hover, the status line, the help card) ------------------------------------------
static const char *const ROLE_NAME[critters::NROLES] = { TRN ("Climber"), TRN ("Floater"), TRN ("Blocker"), TRN ("Builder"),
							 TRN ("Digger"), TRN ("Exploder"), TRN ("Basher"), TRN ("Miner") };
enum { NBUILT = 6 };				// the roles built (the basher and the miner: 02 SHOULD 1, not built)

// ---- the drawing's primitives (screen px as doubles; greyed when g_grey) -------------------------------------------
static bool g_grey;					// the pictures of an empty slot: every colour greyed (04 §5.4)
static unsigned C (unsigned c) { if (!g_grey) return c; int b = uk_bright (c); b = 110 + b / 3; return (unsigned) (b << 16 | b << 8 | b); }
static int P16 (double v) { return (int) lround (v * 16); }
static void el (Canvas &cv, double x, double y, double rx, double ry, unsigned c, int a = 255)
{ VPath p; p.ellipse (P16 (x), P16 (y), P16 (rx), P16 (ry)); p.fill (cv, C (c), a); }
static void ln (Canvas &cv, double x0, double y0, double x1, double y1, double w, unsigned c, int a = 255)
{ VPath p; p.line (P16 (x0), P16 (y0), P16 (x1), P16 (y1), P16 (w)); p.fill (cv, C (c), a); }
static void box (Canvas &cv, double x, double y, double w, double h, unsigned c, int a = 255)
{ VPath p; p.rect (P16 (x), P16 (y), P16 (w), P16 (h)); p.fill (cv, C (c), a); }
// A countdown digit: a dark outline, then the ink
static void digit (Canvas &cv, int cx, int cy, const char *s, int px, unsigned ink)
{
	UkFaceScope sc (face (px)); int w = uk_tw (s, 2), h = uk_fh ();
	for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (dx || dy) uk_text (cv, cx - w / 2 + dx, cy - h / 2 + dy, s, C (0x1A0E06), 2);
	uk_text (cv, cx - w / 2, cy - h / 2, s, C (ink), 2);
}

// ---- the creatures (04 §5.1) -----------------------------------------------------------------------------------------
enum { D_WALK, D_FALL, D_FLOAT, D_CLIMB, D_BLOCK, D_BUILD, D_DIG, D_SHRUG, D_SPLAT, ND };
struct Look { int state, frame, dir, flags; bool hot; int fuse; };	// a drawing's pose (fuse: the digit, 0 none)
static const unsigned BODY = 0xFFC24A, BODY_DK = 0xE58E22, BELLY = 0xFFE7A6, LINE = 0x4A2A10, LEAF = 0x7ED957, LEAF_DK = 0x3E8A2E, FEET = 0x3A2412;
static unsigned g_brickCol = 0xC8A060;			// the level's bricks (a builder's brick in hand)

// One creature, its feet's bottom at (sx, sy) on the screen, k = the scale (1 = the x2 play view)
static void critter (Canvas &cv, double sx, double sy, const Look &c, double k = 1.0)
{
	int d = c.dir; bool front = c.state == D_BLOCK;
	if (c.state == D_SPLAT)
	{
		el (cv, sx, sy - 2.5 * k, 10.5 * k, 3.6 * k, LINE); el (cv, sx, sy - 2.6 * k, 9.5 * k, 2.8 * k, BODY);
		for (int e = -1; e <= 1; e += 2) { ln (cv, sx + e * 3 * k - 1.3 * k, sy - 4 * k, sx + e * 3 * k + 1.3 * k, sy - 1.6 * k, 0.9 * k, LINE); ln (cv, sx + e * 3 * k + 1.3 * k, sy - 4 * k, sx + e * 3 * k - 1.3 * k, sy - 1.6 * k, 0.9 * k, LINE); }
		return;
	}
	double bob = (c.state == D_WALK && (c.frame & 1)) ? -0.6 * k : 0, cy = sy - 11 * k + bob;
	if (c.state == D_DIG) cy += 2 * k;
	unsigned body = c.hot ? uk_mix (BODY, 0xFF4A30, 150) : BODY;
	if (c.state == D_FLOAT)					// the floater's leaf, open over it
	{
		ln (cv, sx - 7 * k, cy - 4 * k, sx - 1 * k, cy - 18 * k, 0.8 * k, LEAF_DK); ln (cv, sx + 7 * k, cy - 4 * k, sx + 1 * k, cy - 18 * k, 0.8 * k, LEAF_DK);
		el (cv, sx, cy - 19 * k, 12 * k, 4.6 * k, LEAF_DK); el (cv, sx, cy - 19.6 * k, 11 * k, 3.6 * k, LEAF);
		ln (cv, sx - 10 * k, cy - 19 * k, sx + 10 * k, cy - 19 * k, 0.7 * k, LEAF_DK);
	}
	double f = c.state == D_WALK ? (c.frame % 4 == 0 ? 1.6 : c.frame % 4 == 2 ? -1.6 : 0) : 0;	// the feet
	if (c.state == D_FALL || c.state == D_FLOAT) { el (cv, sx - 1.8 * k, sy - 1.6 * k, 2 * k, 1.5 * k, FEET); el (cv, sx + 1.8 * k, sy - 1.6 * k, 2 * k, 1.5 * k, FEET); }
	else if (c.state == D_CLIMB) { double s = (c.frame & 1) ? 1.2 : 0; el (cv, sx + d * 6 * k, sy - (2 + s) * k, 1.6 * k, 2.2 * k, FEET); el (cv, sx + d * 6 * k, sy - (6 - s) * k, 1.6 * k, 2.2 * k, FEET); }
	else { el (cv, sx - 3.2 * k + f * k, sy - 1.4 * k, 2.6 * k, 1.5 * k, FEET); el (cv, sx + 3.2 * k - f * k, sy - 1.4 * k, 2.6 * k, 1.5 * k, FEET); }
	auto arm = [&] (double ax, double ay) { el (cv, ax, ay, 2.6 * k, 2.2 * k, LINE); el (cv, ax, ay, 1.8 * k, 1.5 * k, BODY_DK); };
	if (c.state == D_BLOCK) { el (cv, sx - 10.5 * k, cy, 3.8 * k, 2.4 * k, LINE); el (cv, sx - 10.5 * k, cy, 3 * k, 1.6 * k, BODY_DK); el (cv, sx + 10.5 * k, cy, 3.8 * k, 2.4 * k, LINE); el (cv, sx + 10.5 * k, cy, 3 * k, 1.6 * k, BODY_DK); }
	if (c.state == D_SHRUG || c.state == D_FALL) { arm (sx - 8.5 * k, cy - 7 * k); arm (sx + 8.5 * k, cy - 7 * k); }
	// the body: an outline, the warm face, a pale belly, a highlight
	el (cv, sx, cy, 8.8 * k, 9.8 * k, LINE);
	el (cv, sx, cy, 7.9 * k, 8.9 * k, body);
	el (cv, sx, cy + 4.2 * k, 6.4 * k, 4.4 * k, BODY_DK, 70);
	el (cv, sx + (front ? 0 : d * 1.2 * k), cy + 2.6 * k, 4.6 * k, 4.4 * k, BELLY, 210);
	el (cv, sx - 3.2 * k, cy - 5 * k, 2.2 * k, 1.5 * k, 0xFFFFFF, 140);
	if (c.flags & critters::F_CLIMBER) { VPath p; p.arc (P16 (sx), P16 (cy), P16 (7.6 * k), 35, 145, P16 (2.2 * k)); p.fill (cv, C (0x2EC4E8)); }	// the band
	if (c.state != D_FLOAT)					// the sprout (the floater's: a second, paler leaf)
	{
		double tx = sx + (front ? 0 : -d * 0.6 * k), ty = cy - 9.6 * k;
		ln (cv, tx, ty, tx + 0.8 * k, ty - 3.6 * k, 1.1 * k, LEAF_DK);
		el (cv, tx + 3 * k, ty - 3.8 * k, 3 * k, 1.5 * k, LEAF_DK); el (cv, tx + 3 * k, ty - 3.9 * k, 2.3 * k, 1 * k, LEAF);
		if (c.flags & critters::F_FLOATER) { el (cv, tx - 2.4 * k, ty - 3.4 * k, 2.8 * k, 1.6 * k, LEAF_DK); el (cv, tx - 2.4 * k, ty - 3.5 * k, 2.1 * k, 1.1 * k, 0xB8F07A); }
	}
	double ex = front ? 0 : d * 2.4 * k, ey = cy - 2.6 * k, er = (c.state == D_FALL ? 2.9 : 2.4) * k;	// the eyes
	double px = front ? 0 : d * 0.9 * k, py = c.state == D_CLIMB || c.state == D_FALL ? -0.9 * k : c.state == D_DIG || c.state == D_BUILD ? 0.8 * k : 0;
	for (int e = -1; e <= 1; e += 2)
	{
		el (cv, sx + ex + e * 3 * k, ey, er, er * 1.1, 0xFFFFFF);
		el (cv, sx + ex + e * 3 * k + px, ey + py, 1.3 * k, 1.5 * k, 0x1A0E06);
	}
	if (c.state == D_BLOCK) { ln (cv, sx - 5.4 * k, ey - 3.6 * k, sx - 1.6 * k, ey - 2.6 * k, 1 * k, LINE); ln (cv, sx + 5.4 * k, ey - 3.6 * k, sx + 1.6 * k, ey - 2.6 * k, 1 * k, LINE); }
	if (c.state == D_FALL) el (cv, sx + ex, cy + 3.2 * k, 1.5 * k, 1.8 * k, LINE);
	if (c.state == D_CLIMB) { arm (sx + d * 8 * k, cy - 6 * k); arm (sx + d * 8.5 * k, cy + 1 * k); }
	if (c.state == D_BUILD)
	{
		double bx = sx + d * 8.5 * k, by = cy + 2 * k + ((c.frame & 3) == 3 ? 1.5 * k : 0);
		box (cv, bx - 4 * k, by - 2.2 * k, 8 * k, 4.4 * k, LINE); box (cv, bx - 3.3 * k, by - 1.5 * k, 6.6 * k, 3 * k, g_brickCol); box (cv, bx - 3.3 * k, by - 1.5 * k, 6.6 * k, 1 * k, uk_tone (g_brickCol, 170));
		arm (bx - d * 3.4 * k, by + 1 * k);
	}
	if (c.state == D_DIG)
	{
		arm (sx - 6.5 * k, sy - 2.5 * k); arm (sx + 6.5 * k, sy - 2.5 * k);
		static const double DUST[6][2] = { { -10, -3 }, { -12, -6 }, { -8, -8 }, { 10, -4 }, { 12, -7 }, { 9, -9 } };
		for (int i = 0; i < 6; i++) if ((i + c.frame) % 3) el (cv, sx + DUST[i][0] * k, sy + DUST[i][1] * k, 1.3 * k, 1.3 * k, 0xB08860, 220);
	}
	if (c.fuse > 0) { char b[4]; snprintf (b, sizeof b, "%d", c.fuse); digit (cv, (int) sx, (int) (cy - 19 * k), b, (int) (13 * k), c.fuse == 1 ? 0xFF6A50 : 0xFFFFFF); }
}

// The pointer's brackets round a creature (D9): white = it would take the chosen role, red = it would refuse; the
// keyboard's focus adds the accent triangle above
static void brackets (Canvas &cv, double sx, double sy, unsigned c, bool focus)
{
	double x0 = sx - 11, x1 = sx + 11, y0 = sy - 27, y1 = sy + 2, L = 6, w = 1.6;
	ln (cv, x0, y0, x0 + L, y0, w, c); ln (cv, x0, y0, x0, y0 + L, w, c); ln (cv, x1, y0, x1 - L, y0, w, c); ln (cv, x1, y0, x1, y0 + L, w, c);
	ln (cv, x0, y1, x0 + L, y1, w, c); ln (cv, x0, y1, x0, y1 - L, w, c); ln (cv, x1, y1, x1 - L, y1, w, c); ln (cv, x1, y1, x1, y1 - L, w, c);
	if (focus) { VPath p; int t[6] = { V ((int) sx - 5), V ((int) y0 - 9), V ((int) sx + 5), V ((int) y0 - 9), V ((int) sx), V ((int) y0 - 3) }; p.poly (t, 3); p.fill (cv, C_ACCENT); }
}

// ---- the hatch and the exit (04 §5.2) --------------------------------------------------------------------------------
static void hatch (Canvas &cv, double hx, double hy, bool open, double k = 1)
{
	double cy = hy - 12 * k;						// the stone lump over the release point
	VPath p; p.rrect (P16 (hx - 23 * k), P16 (cy - 11 * k), P16 (46 * k), P16 (19 * k), P16 (9 * k)); p.fill (cv, 0x2A1E16);
	VPath q; q.rrect (P16 (hx - 22 * k), P16 (cy - 10 * k), P16 (44 * k), P16 (17 * k), P16 (8 * k)); q.fill (cv, 0x6A5644);
	el (cv, hx - 6 * k, cy - 6 * k, 12 * k, 3 * k, 0x8A7460, 200);
	el (cv, hx - 15 * k, cy - 10 * k, 5 * k, 2.4 * k, 0x5E8A32); el (cv, hx - 9 * k, cy - 11 * k, 4 * k, 2 * k, 0x7EB04A); el (cv, hx + 14 * k, cy - 9.5 * k, 4.5 * k, 2.2 * k, 0x5E8A32);
	if (!open)							// the round door on its underside
	{
		el (cv, hx, cy + 6 * k, 14 * k, 4.6 * k, 0x2A1A0C); el (cv, hx, cy + 5.6 * k, 13 * k, 3.8 * k, 0xA8783E);
		ln (cv, hx - 5 * k, cy + 2.4 * k, hx - 5 * k, cy + 9 * k, 0.8 * k, 0x6A4420); ln (cv, hx + 5 * k, cy + 2.4 * k, hx + 5 * k, cy + 9 * k, 0.8 * k, 0x6A4420);
		el (cv, hx + 8 * k, cy + 5.6 * k, 1.2 * k, 1.2 * k, 0xE8D090);
	}
	else
	{
		el (cv, hx, cy + 6 * k, 14 * k, 4.6 * k, 0x2A1A0C); el (cv, hx, cy + 6 * k, 12.4 * k, 3.6 * k, 0x0E0804);
		el (cv, hx - 15 * k, cy + 14 * k, 3.4 * k, 9.5 * k, 0x2A1A0C); el (cv, hx - 15 * k, cy + 14 * k, 2.6 * k, 8.6 * k, 0xA8783E);
		el (cv, hx, cy + 8 * k, 8 * k, 1.4 * k, 0xFFD890, 60);
	}
}
static void portal (Canvas &cv, double ex, double ey, int pulse, double k = 1)	// the glowing doorway; pulse 0..255
{
	double top = ey - 36 * k;
	el (cv, ex, ey - 18 * k, 30 * k, 26 * k, 0x60F0D8, 18 + pulse / 12);
	el (cv, ex, ey - 18 * k, 20 * k, 21 * k, 0x60F0D8, 30 + pulse / 8);
	box (cv, ex - 10 * k, top + 10 * k, 20 * k, 26 * k, 0x1C8C84); el (cv, ex, top + 10 * k, 10 * k, 10 * k, 0x1C8C84);
	box (cv, ex - 7 * k, top + 12 * k, 14 * k, 24 * k, 0x3ED8C4); el (cv, ex, top + 12 * k, 7 * k, 7 * k, 0x3ED8C4);
	el (cv, ex, ey - 13 * k, 4.5 * k, 12 * k, 0xD8FFF6, 150 + pulse / 3);
	VPath a; a.arc (P16 (ex), P16 (top + 10 * k), P16 (13 * k), 0, 180, P16 (6 * k)); a.fill (cv, 0x3A3028);	// the stone frame
	VPath b; b.arc (P16 (ex), P16 (top + 10 * k), P16 (13 * k), 0, 180, P16 (4.4 * k)); b.fill (cv, 0xA89A88);
	box (cv, ex - 16 * k, top + 10 * k, 6 * k, 26 * k, 0x3A3028); box (cv, ex + 10 * k, top + 10 * k, 6 * k, 26 * k, 0x3A3028);
	box (cv, ex - 15.2 * k, top + 10 * k, 4.4 * k, 26 * k, 0xA89A88); box (cv, ex + 10.8 * k, top + 10 * k, 4.4 * k, 26 * k, 0x8C7E6E);
	el (cv, ex, top - 3 * k, 2.4 * k, 2.4 * k, 0x7ED957);		// a leaf keystone
	box (cv, ex - 18 * k, ey - 1 * k, 36 * k, 3 * k, 0x6E6254);	// the threshold
	static const double SP[4][2] = { { -6, -26 }, { 7, -20 }, { -3, -10 }, { 5, -31 } };
	for (int i = 0; i < 4; i++) el (cv, ex + SP[i][0] * k, ey + SP[i][1] * k, 1.1 * k, 1.1 * k, 0xFFFFFF, 120 + (i * 40 + pulse) % 120);
}

// ---- the roles' pictures (the slots, the cards, the picker, the status line): the creature in its role ---------------
static void role_icon (Canvas &cv, int role, int cx, int cy, int size, bool off)
{
	using namespace critters;
	g_grey = off;
	double k = size / 34.0, sx = cx, sy = cy + 13 * k;
	Look c = { D_WALK, 1, 1, 0, false, 0 };
	switch (role)
	{
	case R_CLIMBER: c.state = D_CLIMB; c.flags = F_CLIMBER; c.frame = 0; sx -= 4 * k; box (cv, cx + 9 * k, cy - 16 * k, 6 * k, 32 * k, 0x8890A0); box (cv, cx + 9 * k, cy - 16 * k, 1.5 * k, 32 * k, 0xB8C0CC); break;
	case R_FLOATER: c.state = D_FLOAT; c.flags = F_FLOATER; sy += 2 * k; break;
	case R_BLOCKER: c.state = D_BLOCK; break;
	case R_BUILDER: c.state = D_BUILD; c.frame = 0; sx -= 4 * k;
		for (int i = 0; i < 3; i++) { box (cv, cx + (3 + i * 5) * k, sy - (2 + i * 3.5) * k, 7 * k, 3 * k, 0x7A5A2A); box (cv, cx + (3.5 + i * 5) * k, sy - (1.6 + i * 3.5) * k, 6 * k, 2.2 * k, 0xC8A060); } break;
	case R_DIGGER: c.state = D_DIG; c.frame = 1; box (cv, cx - 15 * k, sy, 30 * k, 4 * k, 0x8A5A34); box (cv, cx - 5 * k, sy, 10 * k, 4 * k, 0x3A2412); break;
	case R_EXPLODER: c.fuse = 5; sy += 4 * k; break;
	case R_BASHER: case R_MINER:
	{
		sx -= 5 * k;
		VPath p; int dg = role == R_BASHER ? 0 : 1;
		p.line (V (cx + 1), P16 (cy - 2 * k), P16 (cx + 13 * k), P16 (cy + (dg ? 10 : -2) * k), P16 (2.4 * k));
		p.arrowHead (P16 (cx + 15 * k), P16 (cy + (dg ? 12 : -2) * k), dg ? 315 : 0, P16 (6 * k), P16 (4 * k)); p.fill (cv, C (0xE8E0D0));
		break;
	}
	}
	unsigned keep = g_brickCol; g_brickCol = 0xC8A060;
	critter (cv, sx, sy, c, k);
	g_brickCol = keep;
	g_grey = false;
}
static void nuke_icon (Canvas &cv, int cx, int cy, int size, bool off)	// All explode: a 12-pointed burst
{
	g_grey = off; double r = size / 2.0; int xy[48];
	for (int i = 0; i < 24; i++) { double a = i * M_PI / 12, rr = (i & 1) ? r * 0.55 : r; xy[2 * i] = P16 (cx + cos (a) * rr); xy[2 * i + 1] = P16 (cy + sin (a) * rr); }
	VPath p; p.poly (xy, 24); p.fill (cv, C (0xE84A2A));
	el (cv, cx, cy, r * 0.45, r * 0.45, 0xFFD24A); el (cv, cx, cy, r * 0.22, r * 0.22, 0xFFFFFF, 200);
	g_grey = false;
}

// ---- the creatures' frames, pre-rendered (D21; 05 note 1) -------------------------------------------------------------
// A frame: SPW x SPH px with the feet's bottom at (SPX, SPY); per pixel the colour already multiplied by its opacity
// (the drawing on black) and the opacity (255 minus the difference between the drawings on white and on black).
enum { SPW = 36, SPH = 48, SPX = 18, SPY = 42, NSHRINK = 8 };
struct Frame { uint32_t col[SPW * SPH]; uint8_t a[SPW * SPH]; };
static Frame *g_frames[ND * 4 * 2 * 4 * 2 * NSHRINK];	// state x frame x dir x flags x hot x shrink: made on first use
static unsigned g_framesBrick;					// (the bricks' colour they were made with)
static void forget_frames () { for (Frame *&f : g_frames) { delete f; f = 0; } }
static const Frame *frame_of (const Look &lk, int shrink)
{
	if (g_framesBrick != g_brickCol) { forget_frames (); g_framesBrick = g_brickCol; }
	int key = ((((lk.state * 4 + (lk.frame & 3)) * 2 + (lk.dir > 0)) * 4 + (lk.flags & 3)) * 2 + (lk.hot ? 1 : 0)) * NSHRINK + shrink;
	if (g_frames[key]) return g_frames[key];
	static Canvas b, w;
	if (!b.px && (!b.alloc (SPW, SPH) || !w.alloc (SPW, SPH))) return 0;
	Look l = lk; l.fuse = 0;
	double k = 1.0 - shrink / (double) NSHRINK;
	b.clear (0x000000); critter (b, SPX, SPY, l, k);
	w.clear (0xFFFFFF); critter (w, SPX, SPY, l, k);
	Frame *f = new Frame;
	for (int y = 0; y < SPH; y++) for (int x = 0; x < SPW; x++)
	{
		uint32_t B = b.px[y * b.stride + x], W = w.px[y * w.stride + x];
		int a = 255 - ((int) ((W >> 8) & 255) - (int) ((B >> 8) & 255));
		if (a < 0) a = 0;
		if (a > 255) a = 255;
		f->col[y * SPW + x] = B & 0xFFFFFF; f->a[y * SPW + x] = (uint8_t) a;
	}
	g_frames[key] = f;
	return f;
}
// A frame blended into cv, its feet's bottom at (sx, sy); sink: the rows below sy - (SPY - sink) cut (a drowning one)
static void blend_frame (Canvas &cv, const Frame *f, int sx, int sy, int sink = 0)
{
	if (!f) return;
	int x0 = sx - SPX, y0 = sy - SPY + sink;
	for (int y = 0; y < SPH - sink; y++)
	{
		int Y = y0 + y; if (Y < 0 || Y >= cv.h) continue;
		uint32_t *row = cv.px + Y * cv.stride;
		const uint32_t *src = f->col + y * SPW; const uint8_t *al = f->a + y * SPW;
		for (int x = 0; x < SPW; x++)
		{
			int a = al[x]; if (!a) continue;
			int X = x0 + x; if (X < 0 || X >= cv.w) continue;
			if (a == 255) { row[X] = src[x]; continue; }
			uint32_t d = row[X], s = src[x]; int na = 255 - a;
			unsigned r = ((s >> 16) & 255) + ((d >> 16) & 255) * na / 255, g = ((s >> 8) & 255) + ((d >> 8) & 255) * na / 255, bl = (s & 255) + (d & 255) * na / 255;
			row[X] = (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (bl > 255 ? 255 : bl);
		}
	}
}

// ---- the terrain -------------------------------------------------------------------------------------------------------
// The visible part blitted x2 from the view's left edge vx (logical px), a narrower level centred (ox), the bars and what
// is below a lower level in the background's tone (D2)
static void blit_terrain (Canvas &cv, const critters::Terrain &t, int vx, int ox)
{
	cv.clear (uk_tone (t.bg, 70));
	int cols = (cv.w - ox) / 2; if (cols > t.w - vx) cols = t.w - vx;
	if (cols <= 0) return;
	for (int ly = 0; ly < t.h && ly * 2 + 1 < cv.h; ly++)
	{
		uint32_t *d0 = cv.px + (ly * 2) * cv.stride + ox;
		const uint32_t *s = t.col + ly * t.w + vx;
		for (int i = 0; i < cols; i++) d0[2 * i] = d0[2 * i + 1] = s[i];
		memcpy (cv.px + (ly * 2 + 1) * cv.stride + ox, d0, (size_t) cols * 2 * 4);
	}
}
// The level's words painted into the terrain's colour layer, over earth and steel only, mixed 150/256 (D22)
static void paint_labels (const critters::Level &lv, critters::Terrain &t)
{
	for (int k = 0; k < lv.nlabel; k++)
	{
		const critters::Label &l = lv.label[k];
		const char *s = l.text.get (g_lang);
		if (!*s) continue;
		UkFaceScope sc (face (9));
		int tw = uk_tw (s, 2) + 2, th = uk_fh ();
		Canvas c; if (!c.alloc (tw, th)) continue;
		c.clear (0); uk_text (c, 1, 0, s, 0xFFFFFF, 2);
		for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++)
		{
			int a = (c.px[y * c.stride + x] >> 16) & 255, X = l.at.x - tw / 2 + x, Y = l.at.y - th / 2 + y;
			if (a < 90 || X < 0 || X >= t.w || Y < 0 || Y >= t.h || !t.solid (X, Y)) continue;
			t.col[Y * t.w + X] = uk_mix (t.col[Y * t.w + X], l.colour, 150);
		}
	}
}

#endif
