//
// crmock.cpp -- the UX Designer's mock-ups of Critters (AutoDev round 5), drawn by UIKit on the PC (the desktop
// simulator: fakekapi.cpp, FreeType's DejaVu Sans). A THROWAWAY: not the app, not the Developer's code, not a
// documentation screenshot. It reads the draft levels (mklevels.py: the real .level format of 02 §7.1, read through
// FileKit's fk_kv), builds their terrain as 02 §7.1.3 says, draws it x2 with the creatures, the hatches and the exits of
// 04-ux-design.md §5, and lays out the windows with UIKit's widgets (LcdDisplay, ToolButton, Button) and the few drawn
// ones this design adds (the skill slots, the status line, the minimap, the level list, the preview). No simulation:
// each scene's creatures and terrain edits are set by hand (MOCK_SCENE). Built and run by mockups.sh.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "fontkit/uikitface.h"
#include "filekit/kvtext.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uikit;

static bool g_fr;
static const char *SC (void) { return getenv ("MOCK_SCENE") ? getenv ("MOCK_SCENE") : "picker"; }
static bool scene (const char *s) { return !strcmp (SC (), s); }
static void cpy (char *d, const char *s, int cap) { snprintf (d, cap, "%s", s ? s : ""); }

// ---- faces ------------------------------------------------------------------------------------------------------------
static FtTextFace *g_face[64];
static TextFace *face (int px) { if (px < 6) px = 6; if (px > 63) px = 63; if (!g_face[px]) { g_face[px] = new FtTextFace; g_face[px]->open ("DejaVu Sans", px); } return g_face[px]; }

// ---- the level (just what is drawn) -----------------------------------------------------------------------------------
enum { M_EMPTY, M_EARTH, M_STEEL, M_WATER, M_LAVA };
enum { R_CLIMBER, R_FLOATER, R_BLOCKER, R_BUILDER, R_DIGGER, R_EXPLODER, R_BASHER, R_MINER, NROLES };
static const char *const ROLE_WORD[NROLES] = { "climber", "floater", "blocker", "builder", "digger", "exploder", "basher", "miner" };
static const char *const ROLE_NAME[NROLES] = { TRN ("Climber"), TRN ("Floater"), TRN ("Blocker"), TRN ("Builder"), TRN ("Digger"), TRN ("Exploder"), TRN ("Basher"), TRN ("Miner") };
struct Pt { int x, y; };
struct Shape { int kind; int v[4]; Pt p[64]; int n; int mat; unsigned col, col2; int tex; };	// kind 0 rect, 1 poly, 2 circle; mat -1 erase
struct Lbl { Pt at; char en[48], fr[48]; unsigned col; };
struct Level
{
	char name[2][64], hint[2][200], file[64], err[96]; bool ok;
	int w, h, count, save, timeSec, rate, roles[NROLES]; unsigned bg, brick;
	Shape shape[64]; int nshape; Pt hatch[4]; int hdir[4]; int nhatch; Pt exit[4]; int nexit; Lbl lbl[8]; int nlbl;
	unsigned char *m; unsigned *col;			// the terrain (built on demand)
	const char *nm () const { return g_fr && name[1][0] ? name[1] : name[0]; }
	const char *ht () const { return g_fr && hint[1][0] ? hint[1] : hint[0]; }
	int at (int x, int y) const { if (x < 0 || x >= w || y < 0) return M_STEEL; if (y >= h) return M_EMPTY; return m[y * w + x]; }
	bool solid (int x, int y) const { int k = at (x, y); return k == M_EARTH || k == M_STEEL; }
};

static unsigned colour (const char *s, unsigned def) { return s && s[0] == '#' ? (unsigned) strtoul (s + 1, 0, 16) : def; }
static int ints (const char *s, int *o, int cap)
{
	int n = 0; char *e;
	while (s && *s && n < cap) { long v = strtol (s, &e, 10); if (e == s) break; o[n++] = (int) v; s = e; while (*s == ' ' || *s == ',') s++; }
	return n;
}
static fk_kv *g_kv;
static const char *get (int b, const char *key, const char *def = 0)
{
	for (int i = 0; i < fk_kv_count (g_kv); i++) if (fk_kv_block (g_kv, i) == b && !strcmp (fk_kv_key (g_kv, i), key)) return fk_kv_value (g_kv, i);
	return def;
}
static int num (int b, const char *key, int def) { const char *v = get (b, key); return v ? atoi (v) : def; }

static bool load (const char *path, Level &L)
{
	memset (&L, 0, sizeof L);
	const char *bn = strrchr (path, '/'); cpy (L.file, bn ? bn + 1 : path, sizeof L.file);
	void *f = kapi_open (path);
	if (!f) { cpy (L.err, TR ("cannot read the file"), sizeof L.err); return false; }
	unsigned n = kapi_fsize (f); char *text = (char *) malloc (n + 1); int got = kapi_read (f, text, n); kapi_close (f);
	text[got > 0 ? got : 0] = 0;
	g_kv = fk_kv_parse (text, 0); free (text);
	static const char *const KNOWN[] = { "level", "shape", "hatch", "exit", "label", 0 };
	for (int b = 1; b <= fk_kv_blocks (g_kv); b++)
	{
		const char *k = fk_kv_block_name (g_kv, b); bool known = false;
		for (int i = 0; KNOWN[i]; i++) if (!strcmp (k, KNOWN[i])) known = true;
		if (!known)
		{
			char r[64]; snprintf (r, sizeof r, TR ("unknown block [%s]"), k);
			snprintf (L.err, sizeof L.err, TR ("line %d: "), fk_kv_block_line (g_kv, b));
			strncat (L.err, r, sizeof L.err - strlen (L.err) - 1);
			fk_kv_free (g_kv); return false;
		}
		if (!strcmp (k, "level"))
		{
			cpy (L.name[0], get (b, "name"), 64); cpy (L.name[1], get (b, "name.fr"), 64);
			cpy (L.hint[0], get (b, "hint"), 200); cpy (L.hint[1], get (b, "hint.fr"), 200);
			int s[2] = { 320, 160 }; ints (get (b, "size", "320 160"), s, 2); L.w = s[0]; L.h = s[1];
			L.count = num (b, "count", 10); L.save = num (b, "save", 1); L.timeSec = num (b, "time", 180); L.rate = num (b, "rate", 50);
			for (int r = 0; r < NROLES; r++) L.roles[r] = num (b, ROLE_WORD[r], 0);
			L.bg = colour (get (b, "background"), 0x101830); L.brick = colour (get (b, "brick"), 0xC8A060);
		}
		else if (!strcmp (k, "shape"))
		{
			Shape &s = L.shape[L.nshape++]; memset (&s, 0, sizeof s);
			if (get (b, "rect")) { s.kind = 0; ints (get (b, "rect"), s.v, 4); }
			else if (get (b, "circle")) { s.kind = 2; ints (get (b, "circle"), s.v, 3); }
			else { s.kind = 1; int t[128]; int k2 = ints (get (b, "points"), t, 128); s.n = k2 / 2; for (int i = 0; i < s.n; i++) { s.p[i].x = t[2 * i]; s.p[i].y = t[2 * i + 1]; } }
			const char *m = get (b, "material", "earth");
			s.mat = !strcmp (m, "steel") ? M_STEEL : !strcmp (m, "water") ? M_WATER : !strcmp (m, "lava") ? M_LAVA : !strcmp (m, "erase") ? -1 : M_EARTH;
			unsigned def = s.mat == M_STEEL ? 0x8890A0 : s.mat == M_WATER ? 0x3070D0 : s.mat == M_LAVA ? 0xE05020 : 0x8A5A34;
			s.col = colour (get (b, "colour"), def);
			unsigned c = s.col; unsigned d = (((c >> 16 & 255) * 3 / 4) << 16) | (((c >> 8 & 255) * 3 / 4) << 8) | ((c & 255) * 3 / 4);
			s.col2 = colour (get (b, "colour2"), d);
			const char *t = get (b, "texture", "plain");
			s.tex = !strcmp (t, "speckle") ? 1 : !strcmp (t, "stripes") ? 2 : !strcmp (t, "bricks") ? 3 : 0;
		}
		else if (!strcmp (k, "hatch")) { int v[2] = { 0, 0 }; ints (get (b, "at"), v, 2); L.hatch[L.nhatch].x = v[0]; L.hatch[L.nhatch].y = v[1]; L.hdir[L.nhatch++] = strcmp (get (b, "dir", "right"), "left") ? 1 : -1; }
		else if (!strcmp (k, "exit")) { int v[2] = { 0, 0 }; ints (get (b, "at"), v, 2); L.exit[L.nexit].x = v[0]; L.exit[L.nexit++].y = v[1]; }
		else if (!strcmp (k, "label"))
		{ Lbl &l = L.lbl[L.nlbl++]; int v[2] = { 0, 0 }; ints (get (b, "at"), v, 2); l.at.x = v[0]; l.at.y = v[1]; cpy (l.en, get (b, "text"), 48); cpy (l.fr, get (b, "text.fr"), 48); l.col = colour (get (b, "colour"), 0xFFFFFF); }
	}
	fk_kv_free (g_kv);
	L.ok = true;
	return true;
}

// ---- the terrain (02 §7.1.3) ------------------------------------------------------------------------------------------
static unsigned hash3 (int x, int y, int k) { unsigned h = (unsigned) x * 374761393u + (unsigned) y * 668265263u + (unsigned) k * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
static bool inside (const Shape &s, int x, int y)
{
	if (s.kind == 0) return x >= s.v[0] && x < s.v[0] + s.v[2] && y >= s.v[1] && y < s.v[1] + s.v[3];
	if (s.kind == 2) { int dx = x - s.v[0], dy = y - s.v[1]; return dx * dx + dy * dy <= s.v[2] * s.v[2]; }
	bool in = false; double px = x + 0.5, py = y + 0.5;
	for (int i = 0, j = s.n - 1; i < s.n; j = i++)
	{
		double xi = s.p[i].x, yi = s.p[i].y, xj = s.p[j].x, yj = s.p[j].y;
		if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
	}
	return in;
}
static void paint_label (Level &L, const Lbl &l)
{
	const char *s = g_fr && l.fr[0] ? l.fr : l.en;
	UkFaceScope sc (face (9));
	int tw = uk_tw (s, 2) + 2, th = uk_fh ();
	Canvas t; if (!t.alloc (tw, th)) return; t.clear (0);
	uk_text (t, 1, 0, s, 0xFFFFFF, 2);
	for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++)
	{
		int a = (t.px[y * t.stride + x] >> 16) & 255, X = l.at.x - tw / 2 + x, Y = l.at.y - th / 2 + y;
		if (a < 90 || X < 0 || X >= L.w || Y < 0 || Y >= L.h || !L.solid (X, Y)) continue;
		L.col[Y * L.w + X] = uk_mix (L.col[Y * L.w + X], l.col, 150);
	}
}
static void build (Level &L)
{
	if (L.m) return;
	L.m = (unsigned char *) calloc (L.w * L.h, 1); L.col = (unsigned *) malloc (L.w * L.h * 4);
	for (int i = 0; i < L.w * L.h; i++) L.col[i] = L.bg;
	for (int k = 0; k < L.nshape; k++)
	{
		const Shape &s = L.shape[k];
		for (int y = 0; y < L.h; y++) for (int x = 0; x < L.w; x++)
		{
			if (!inside (s, x, y)) continue;
			int i = y * L.w + x;
			if (s.mat < 0) { L.m[i] = M_EMPTY; L.col[i] = L.bg; continue; }
			L.m[i] = (unsigned char) s.mat;
			bool two = s.tex == 1 ? hash3 (x, y, k) % 6 == 0 : s.tex == 2 ? y % 4 == 3 : s.tex == 3 ? (y % 4 == 3 || (x + (y / 4 % 2) * 4) % 8 == 7) : false;
			unsigned c = two ? s.col2 : s.col;
			if (s.mat == M_STEEL && s.tex == 3 && y % 4 == 0 && (x + (y / 4 % 2) * 4) % 8 == 0) c = uk_tone (s.col, 200);	// (a rivet: 04 §5.3)
			L.col[i] = c;
		}
	}
	for (int k = 0; k < L.nlbl; k++) paint_label (L, L.lbl[k]);
}
static void dig_rect (Level &L, int x0, int y0, int x1, int y1)
{ for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (x >= 0 && x < L.w && y >= 0 && y < L.h && L.m[y * L.w + x] == M_EARTH) { L.m[y * L.w + x] = M_EMPTY; L.col[y * L.w + x] = L.bg; } }
static void dig_disc (Level &L, int cx, int cy, int r)
{ for (int y = cy - r; y <= cy + r; y++) for (int x = cx - r; x <= cx + r; x++) if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) dig_rect (L, x, y, x, y); }
static void brick (Level &L, int x, int y, int dir)		// 03 §3.3: columns x - dir .. x + 4 dir, rows y - 1 .. y
{
	for (int i = -1; i <= 4; i++) for (int r = -1; r <= 0; r++)
	{
		int X = x + i * dir, Y = y + r; if (X < 0 || X >= L.w || Y < 0 || Y >= L.h || L.m[Y * L.w + X] != M_EMPTY) continue;
		L.m[Y * L.w + X] = M_EARTH; L.col[Y * L.w + X] = r < 0 ? uk_tone (L.brick, 170) : L.brick;
	}
}
static void stair (Level &L, int x, int y, int dir, int n) { for (int i = 0; i < n; i++) brick (L, x + 3 * i * dir, y - 2 * i, dir); }
static int ground (const Level &L, int x, int y0)		// the feet's row standing at x, searching down from y0
{ for (int y = y0; y < L.h; y++) if (!L.solid (x, y) && L.solid (x, y + 1)) return y; return L.h - 1; }

// ---- the creatures (04 §5.1) ------------------------------------------------------------------------------------------
enum { S_WALK, S_FALL, S_FLOAT, S_CLIMB, S_BLOCK, S_BUILD, S_DIG, S_SHRUG, S_SPLAT, S_EXIT, S_DROWN };
enum { F_CLIMBER = 1, F_FLOATER = 2 };
struct Critter { int x, y, dir, state, frame, flags, fuse; };	// fuse: the digit shown (0 none)

static bool g_grey;					// the icons of an empty slot: every colour greyed
static unsigned C (unsigned c) { if (!g_grey) return c; int b = uk_bright (c); b = 110 + b / 3; return (unsigned) (b << 16 | b << 8 | b); }
static const unsigned BODY = 0xFFC24A, BODY_DK = 0xE58E22, BELLY = 0xFFE7A6, LINE = 0x4A2A10, LEAF = 0x7ED957, LEAF_DK = 0x3E8A2E, FEET = 0x3A2412;

static void el (Canvas &cv, double x, double y, double rx, double ry, unsigned c, int a = 255)
{ VPath p; p.ellipse ((int) lround (x * 16), (int) lround (y * 16), (int) lround (rx * 16), (int) lround (ry * 16)); p.fill (cv, C (c), a); }
static void ln (Canvas &cv, double x0, double y0, double x1, double y1, double w, unsigned c, int a = 255)
{ VPath p; p.line ((int) lround (x0 * 16), (int) lround (y0 * 16), (int) lround (x1 * 16), (int) lround (y1 * 16), (int) lround (w * 16)); p.fill (cv, C (c), a); }
static void box (Canvas &cv, double x, double y, double w, double h, unsigned c, int a = 255)
{ VPath p; p.rect ((int) lround (x * 16), (int) lround (y * 16), (int) lround (w * 16), (int) lround (h * 16)); p.fill (cv, C (c), a); }
static void digit (Canvas &cv, int cx, int cy, const char *s, int px, unsigned ink)	// a countdown digit: dark outline, then the ink
{
	UkFaceScope sc (face (px)); int w = uk_tw (s, 2), h = uk_fh ();
	for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (dx || dy) uk_text (cv, cx - w / 2 + dx, cy - h / 2 + dy, s, C (0x1A0E06), 2);
	uk_text (cv, cx - w / 2, cy - h / 2, s, C (ink), 2);
}

// One creature, its feet's bottom at (sx, sy) on the screen, k = the scale (1 = the x2 play view)
static void critter (Canvas &cv, double sx, double sy, const Critter &c, double k = 1.0)
{
	int d = c.dir; bool front = c.state == S_BLOCK;
	if (c.state == S_SPLAT)
	{
		el (cv, sx, sy - 2.5 * k, 10.5 * k, 3.6 * k, LINE); el (cv, sx, sy - 2.6 * k, 9.5 * k, 2.8 * k, BODY);
		for (int e = -1; e <= 1; e += 2) { ln (cv, sx + e * 3 * k - 1.3 * k, sy - 4 * k, sx + e * 3 * k + 1.3 * k, sy - 1.6 * k, 0.9 * k, LINE); ln (cv, sx + e * 3 * k + 1.3 * k, sy - 4 * k, sx + e * 3 * k - 1.3 * k, sy - 1.6 * k, 0.9 * k, LINE); }
		return;
	}
	double bob = (c.state == S_WALK && (c.frame & 1)) ? -0.6 * k : 0, cy = sy - 11 * k + bob;
	if (c.state == S_DIG) cy += 2 * k;
	unsigned body = BODY;
	if (c.fuse == 1) body = uk_mix (BODY, 0xFF4A30, 150);
	// the floater's leaf, open over it
	if (c.state == S_FLOAT)
	{
		ln (cv, sx - 7 * k, cy - 4 * k, sx - 1 * k, cy - 18 * k, 0.8 * k, LEAF_DK); ln (cv, sx + 7 * k, cy - 4 * k, sx + 1 * k, cy - 18 * k, 0.8 * k, LEAF_DK);
		el (cv, sx, cy - 19 * k, 12 * k, 4.6 * k, LEAF_DK); el (cv, sx, cy - 19.6 * k, 11 * k, 3.6 * k, LEAF);
		ln (cv, sx - 10 * k, cy - 19 * k, sx + 10 * k, cy - 19 * k, 0.7 * k, LEAF_DK);
	}
	// feet
	double f = c.state == S_WALK ? (c.frame % 4 == 0 ? 1.6 : c.frame % 4 == 2 ? -1.6 : 0) : 0;
	if (c.state == S_FALL || c.state == S_FLOAT) { el (cv, sx - 1.8 * k, sy - 1.6 * k, 2 * k, 1.5 * k, FEET); el (cv, sx + 1.8 * k, sy - 1.6 * k, 2 * k, 1.5 * k, FEET); }
	else if (c.state == S_CLIMB) { el (cv, sx + d * 6 * k, sy - 2 * k, 1.6 * k, 2.2 * k, FEET); el (cv, sx + d * 6 * k, sy - 6 * k, 1.6 * k, 2.2 * k, FEET); }
	else { el (cv, sx - 3.2 * k + f * k, sy - 1.4 * k, 2.6 * k, 1.5 * k, FEET); el (cv, sx + 3.2 * k - f * k, sy - 1.4 * k, 2.6 * k, 1.5 * k, FEET); }
	// arms (behind the body's outline: little stubs)
	auto arm = [&] (double ax, double ay) { el (cv, ax, ay, 2.6 * k, 2.2 * k, LINE); el (cv, ax, ay, 1.8 * k, 1.5 * k, BODY_DK); };
	if (c.state == S_BLOCK) { el (cv, sx - 10.5 * k, cy, 3.8 * k, 2.4 * k, LINE); el (cv, sx - 10.5 * k, cy, 3 * k, 1.6 * k, BODY_DK); el (cv, sx + 10.5 * k, cy, 3.8 * k, 2.4 * k, LINE); el (cv, sx + 10.5 * k, cy, 3 * k, 1.6 * k, BODY_DK); }
	if (c.state == S_SHRUG || c.state == S_FALL) { arm (sx - 8.5 * k, cy - 7 * k); arm (sx + 8.5 * k, cy - 7 * k); }
	// the body: an outline, the warm face, a pale belly, a highlight
	el (cv, sx, cy, 8.8 * k, 9.8 * k, LINE);
	el (cv, sx, cy, 7.9 * k, 8.9 * k, body);
	el (cv, sx, cy + 4.2 * k, 6.4 * k, 4.4 * k, BODY_DK, 70);
	el (cv, sx + (front ? 0 : d * 1.2 * k), cy + 2.6 * k, 4.6 * k, 4.4 * k, BELLY, 210);
	el (cv, sx - 3.2 * k, cy - 5 * k, 2.2 * k, 1.5 * k, 0xFFFFFF, 140);
	// the climber's band
	if (c.flags & F_CLIMBER) { VPath p; p.arc ((int) lround (sx * 16), (int) lround (cy * 16), (int) lround (7.6 * k * 16), 35, 145, (int) lround (2.2 * k * 16)); p.fill (cv, C (0x2EC4E8)); }
	// the sprout on top (the floater's: a closed leaf bud, two leaves)
	if (c.state != S_FLOAT)
	{
		double tx = sx + (front ? 0 : -d * 0.6 * k), ty = cy - 9.6 * k;
		ln (cv, tx, ty, tx + 0.8 * k, ty - 3.6 * k, 1.1 * k, LEAF_DK);
		el (cv, tx + 3 * k, ty - 3.8 * k, 3 * k, 1.5 * k, LEAF_DK); el (cv, tx + 3 * k, ty - 3.9 * k, 2.3 * k, 1 * k, LEAF);
		if (c.flags & F_FLOATER) { el (cv, tx - 2.4 * k, ty - 3.4 * k, 2.8 * k, 1.6 * k, LEAF_DK); el (cv, tx - 2.4 * k, ty - 3.5 * k, 2.1 * k, 1.1 * k, 0xB8F07A); }
	}
	// eyes
	double ex = front ? 0 : d * 2.4 * k, ey = cy - 2.6 * k, er = (c.state == S_FALL ? 2.9 : 2.4) * k;
	double px = front ? 0 : d * 0.9 * k, py = c.state == S_CLIMB || c.state == S_FALL ? -0.9 * k : c.state == S_DIG || c.state == S_BUILD ? 0.8 * k : 0;
	for (int e = -1; e <= 1; e += 2)
	{
		el (cv, sx + ex + e * 3 * k, ey, er, er * 1.1, 0xFFFFFF);
		el (cv, sx + ex + e * 3 * k + px, ey + py, 1.3 * k, 1.5 * k, 0x1A0E06);
	}
	if (c.state == S_BLOCK) { ln (cv, sx - 5.4 * k, ey - 3.6 * k, sx - 1.6 * k, ey - 2.6 * k, 1 * k, LINE); ln (cv, sx + 5.4 * k, ey - 3.6 * k, sx + 1.6 * k, ey - 2.6 * k, 1 * k, LINE); }
	if (c.state == S_FALL) el (cv, sx + ex, cy + 3.2 * k, 1.5 * k, 1.8 * k, LINE);
	// hands at work
	if (c.state == S_CLIMB) { arm (sx + d * 8 * k, cy - 6 * k); arm (sx + d * 8.5 * k, cy + 1 * k); }
	if (c.state == S_BUILD)
	{
		double bx = sx + d * 8.5 * k, by = cy + 2 * k;
		box (cv, bx - 4 * k, by - 2.2 * k, 8 * k, 4.4 * k, LINE); box (cv, bx - 3.3 * k, by - 1.5 * k, 6.6 * k, 3 * k, 0xC8A060); box (cv, bx - 3.3 * k, by - 1.5 * k, 6.6 * k, 1 * k, 0xE8CC90);
		arm (bx - d * 3.4 * k, by + 1 * k);
	}
	if (c.state == S_DIG)
	{
		arm (sx - 6.5 * k, sy - 2.5 * k); arm (sx + 6.5 * k, sy - 2.5 * k);
		static const double DUST[6][2] = { { -10, -3 }, { -12, -6 }, { -8, -8 }, { 10, -4 }, { 12, -7 }, { 9, -9 } };
		for (int i = 0; i < 6; i++) if ((i + c.frame) % 3) el (cv, sx + DUST[i][0] * k, sy + DUST[i][1] * k, 1.3 * k, 1.3 * k, 0xB08860, 220);
	}
	if (c.fuse > 0) { char b[4]; snprintf (b, sizeof b, "%d", c.fuse); digit (cv, (int) sx, (int) (cy - 19 * k), b, (int) (13 * k), c.fuse == 1 ? 0xFF6A50 : 0xFFFFFF); }
}

// the pointer's brackets round a creature (04 §5.4): white = it can take the chosen role, red = it would refuse
static void brackets (Canvas &cv, double sx, double sy, unsigned c, bool focus)
{
	double x0 = sx - 11, x1 = sx + 11, y0 = sy - 27, y1 = sy + 2, L = 6, w = 1.6;
	ln (cv, x0, y0, x0 + L, y0, w, c); ln (cv, x0, y0, x0, y0 + L, w, c); ln (cv, x1, y0, x1 - L, y0, w, c); ln (cv, x1, y0, x1, y0 + L, w, c);
	ln (cv, x0, y1, x0 + L, y1, w, c); ln (cv, x0, y1, x0, y1 - L, w, c); ln (cv, x1, y1, x1 - L, y1, w, c); ln (cv, x1, y1, x1, y1 - L, w, c);
	if (focus) { VPath p; int t[6] = { V ((int) sx - 5), V ((int) y0 - 9), V ((int) sx + 5), V ((int) y0 - 9), V ((int) sx), V ((int) y0 - 3) }; p.poly (t, 3); p.fill (cv, C_ACCENT); }
}

// ---- the hatch and the exit (04 §5.2) ---------------------------------------------------------------------------------
static void hatch (Canvas &cv, double hx, double hy, bool open, double k = 1)
{
	double cy = hy - 12 * k;						// the stone lump over the release point
	VPath p; p.rrect ((int) ((hx - 23 * k) * 16), (int) ((cy - 11 * k) * 16), (int) (46 * k * 16), (int) (19 * k * 16), (int) (9 * k * 16)); p.fill (cv, 0x2A1E16);
	VPath q; q.rrect ((int) ((hx - 22 * k) * 16), (int) ((cy - 10 * k) * 16), (int) (44 * k * 16), (int) (17 * k * 16), (int) (8 * k * 16)); q.fill (cv, 0x6A5644);
	el (cv, hx - 6 * k, cy - 6 * k, 12 * k, 3 * k, 0x8A7460, 200);
	el (cv, hx - 15 * k, cy - 10 * k, 5 * k, 2.4 * k, 0x5E8A32); el (cv, hx - 9 * k, cy - 11 * k, 4 * k, 2 * k, 0x7EB04A); el (cv, hx + 14 * k, cy - 9.5 * k, 4.5 * k, 2.2 * k, 0x5E8A32);
	// the round door on its underside
	if (!open)
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
	// the light inside the arch
	box (cv, ex - 10 * k, top + 10 * k, 20 * k, 26 * k, 0x1C8C84); el (cv, ex, top + 10 * k, 10 * k, 10 * k, 0x1C8C84);
	box (cv, ex - 7 * k, top + 12 * k, 14 * k, 24 * k, 0x3ED8C4); el (cv, ex, top + 12 * k, 7 * k, 7 * k, 0x3ED8C4);
	el (cv, ex, ey - 13 * k, 4.5 * k, 12 * k, 0xD8FFF6, 150 + pulse / 3);
	// the stone frame: two posts and an arch
	VPath a; a.arc ((int) (ex * 16), (int) ((top + 10 * k) * 16), (int) (13 * k * 16), 0, 180, (int) (6 * k * 16)); a.fill (cv, 0x3A3028);
	VPath b; b.arc ((int) (ex * 16), (int) ((top + 10 * k) * 16), (int) (13 * k * 16), 0, 180, (int) (4.4 * k * 16)); b.fill (cv, 0xA89A88);
	box (cv, ex - 16 * k, top + 10 * k, 6 * k, 26 * k, 0x3A3028); box (cv, ex + 10 * k, top + 10 * k, 6 * k, 26 * k, 0x3A3028);
	box (cv, ex - 15.2 * k, top + 10 * k, 4.4 * k, 26 * k, 0xA89A88); box (cv, ex + 10.8 * k, top + 10 * k, 4.4 * k, 26 * k, 0x8C7E6E);
	box (cv, ex - 13 * k, top - 3.6 * k, 26 * k, 0, 0);
	el (cv, ex, top - 3 * k, 2.4 * k, 2.4 * k, 0x7ED957);		// a leaf keystone
	box (cv, ex - 18 * k, ey - 1 * k, 36 * k, 3 * k, 0x6E6254);	// the threshold
	static const double SP[4][2] = { { -6, -26 }, { 7, -20 }, { -3, -10 }, { 5, -31 } };
	for (int i = 0; i < 4; i++) el (cv, ex + SP[i][0] * k, ey + SP[i][1] * k, 1.1 * k, 1.1 * k, 0xFFFFFF, 120 + (i * 40 + pulse) % 120);
}
static void burst (Canvas &cv, double cx, double cy, unsigned earth)
{
	for (int i = 0; i < 22; i++)
	{
		double a = i * 2.39996, r = 6 + (i * 7) % 17;
		double x = cx + cos (a) * r * 1.4, y = cy + sin (a) * r - (i % 5);
		if (i % 3) box (cv, x - 1.5, y - 1.5, 3, 3, uk_tone (earth, 100 + (i * 13) % 60)); else el (cv, x, y, 1.6, 1.6, i % 2 ? 0xFFE060 : 0xFF8A30);
	}
	el (cv, cx, cy, 9, 8, 0xFFF0B0, 120);
}

// ---- the role icons (the skill bar's, the picker's, the cards'): the creature in its role -----------------------------
static void role_icon (Canvas &cv, int role, int cx, int cy, int size, bool off)
{
	g_grey = off;
	double k = size / 34.0, sx = cx, sy = cy + 13 * k;
	Critter c = { 0, 0, 1, S_WALK, 1, 0, 0 };
	switch (role)
	{
	case R_CLIMBER: c.state = S_CLIMB; c.flags = F_CLIMBER; sx -= 4 * k; box (cv, cx + 9 * k, cy - 16 * k, 6 * k, 32 * k, 0x8890A0); box (cv, cx + 9 * k, cy - 16 * k, 1.5 * k, 32 * k, 0xB8C0CC); break;
	case R_FLOATER: c.state = S_FLOAT; c.flags = F_FLOATER; sy += 2 * k; break;
	case R_BLOCKER: c.state = S_BLOCK; break;
	case R_BUILDER: c.state = S_BUILD; sx -= 4 * k;
		for (int i = 0; i < 3; i++) { box (cv, cx + (3 + i * 5) * k, sy - (2 + i * 3.5) * k, 7 * k, 3 * k, 0x7A5A2A); box (cv, cx + (3.5 + i * 5) * k, sy - (1.6 + i * 3.5) * k, 6 * k, 2.2 * k, 0xC8A060); } break;
	case R_DIGGER: c.state = S_DIG; c.frame = 1; box (cv, cx - 15 * k, sy, 30 * k, 4 * k, 0x8A5A34); box (cv, cx - 5 * k, sy, 10 * k, 4 * k, 0x3A2412); sy -= 0; break;
	case R_EXPLODER: c.fuse = 5; sy += 4 * k; break;
	case R_BASHER: case R_MINER:
	{
		sx -= 5 * k;
		VPath p; int dx = role == R_BASHER ? 0 : 1;
		p.line (V (cx + 1), (int) ((cy - 2 * k) * 16), (int) ((cx + 13 * k) * 16), (int) ((cy + (dx ? 10 : -2) * k) * 16), (int) (2.4 * k * 16));
		p.arrowHead ((int) ((cx + 15 * k) * 16), (int) ((cy + (dx ? 12 : -2) * k) * 16), dx ? 315 : 0, (int) (6 * k * 16), (int) (4 * k * 16)); p.fill (cv, C (0xE8E0D0));
		break;
	}
	}
	critter (cv, sx, sy, c, k);
	g_grey = false;
}
static void nuke_icon (Canvas &cv, int cx, int cy, int size, bool off)
{
	g_grey = off; double r = size / 2.0; int xy[48];
	for (int i = 0; i < 24; i++) { double a = i * M_PI / 12, rr = (i & 1) ? r * 0.55 : r; xy[2 * i] = (int) lround ((cx + cos (a) * rr) * 16); xy[2 * i + 1] = (int) lround ((cy + sin (a) * rr) * 16); }
	VPath p; p.poly (xy, 24); p.fill (cv, C (0xE84A2A));
	el (cv, cx, cy, r * 0.45, r * 0.45, 0xFFD24A); el (cv, cx, cy, r * 0.22, r * 0.22, 0xFFFFFF, 200);
	g_grey = false;
}

// ---- the mock's state ---------------------------------------------------------------------------------------------------
static Level *g_lv[16]; static int g_nlv;
static const char *const FILES[] = {
	"SD:/apps/critters.app/levels/training-01-straight-down.level", "SD:/apps/critters.app/levels/training-02-mind-the-gap.level",
	"SD:/apps/critters.app/levels/training-03-hold-the-line.level", "SD:/apps/critters.app/levels/training-04-up-the-wall.level",
	"SD:/apps/critters.app/levels/training-05-soft-landing.level", "SD:/apps/critters.app/levels/training-06-blast-through.level",
	"SD:/apps/critters.app/levels/expedition-01-two-ways.level", "SD:/apps/critters.app/levels/expedition-02-steel-floor.level",
	"SD:/apps/critters.app/levels/expedition-03-the-climb.level", "SD:/apps/critters.app/levels/expedition-04-lava-lake.level",
	"SD:/apps/critters.app/levels/expedition-05-the-maze.level", "SD:/apps/critters.app/levels/expedition-06-grand-tour.level",
	"SD:/docs/critters/my-first-level.level", "SD:/docs/critters/cliffs.level", 0 };
// the progress fixture (the picker's ticks): solved, best saved, best time (s); -1 = locked
static const int PROG[14][3] = { { 1, 10, 72 }, { 1, 9, 100 }, { 1, 8, 125 }, { 0, 0, 0 }, { -1 }, { -1 }, { -1 }, { -1 }, { -1 }, { -1 }, { -1 }, { -1 }, { 1, 8, 131 }, { 0 } };
static int g_sel;
static void mmss (int s, char *b, int cap) { snprintf (b, cap, "%d:%02d", s / 60, s % 60); }

enum { W = 800, H = 448, PLAYH = 320, STATUSY = 320, STATUSH = 24, BARY = 344 };

// ---- the play area: a GameView in the app (games/game.h); here a plain Widget ---------------------------------------------
enum { O_NONE, O_CARD, O_MENU, O_NUKE, O_WON, O_LOST, O_HELP };
class Field : public Widget
{
public:
	Level *L; int vx; Critter c[40]; int nc; int hi; bool refuse, focus; bool hatchOpen; int overlay; bool paused, fast;
	int bursts[4][2]; int nb;
	Field (int l, int t, int w, int h) : Widget (l, t, w, h), L (0), vx (0), nc (0), hi (-1), refuse (false), focus (false), hatchOpen (true), overlay (0), paused (false), fast (false), nb (0) {}
	int ox () const { return L->w * 2 < width ? (width - L->w * 2) / 2 : 0; }
	double SX (int x) const { return ox () + (x - vx) * 2 + 1; }
	double SY (int y) const { return (y + 1) * 2; }
	void card (int w, int h, const char *title, int &x, int &y)
	{
		x = (width - w) / 2; y = (height - h) / 2;
		int th = uk_fh () + 10;
		uk_rbox (canvas, x, y, w, h, 8, C_FACE, C_FACE);
		uk_title_strip (canvas, x + 1, y + 1, w - 2, th, title, 7);
		uk_rline (canvas, x, y, w, h, 8, UK_OUTLINE == 2 ? 0 : uk_tone (C_FRAME_ACTIVE, 44), 255);
		y += th;
	}
	void onDraw () override
	{
		canvas.clear (uk_tone (L->bg, 70));
		int o = ox ();
		for (int sy = 0; sy < height && sy / 2 < L->h; sy++)
		{
			unsigned *row = canvas.px + sy * canvas.stride; int ly = sy / 2;
			for (int sx = o; sx < width; sx++) { int lx = vx + (sx - o) / 2; if (lx >= L->w) break; row[sx] = L->col[ly * L->w + lx]; }
		}
		for (int i = 0; i < L->nexit; i++) portal (canvas, SX (L->exit[i].x), SY (L->exit[i].y), 200);
		for (int i = 0; i < L->nhatch; i++) hatch (canvas, SX (L->hatch[i].x), SY (L->hatch[i].y) - 4, hatchOpen);
		for (int i = 0; i < nc; i++) critter (canvas, SX (c[i].x), SY (c[i].y), c[i]);
		for (int i = 0; i < nb; i++) burst (canvas, SX (bursts[i][0]), SY (bursts[i][1]), 0xC89A5C);
		if (hi >= 0) brackets (canvas, SX (c[hi].x), SY (c[hi].y), refuse ? 0xFF5A4A : 0xFFFFFF, focus);
		if (paused && !overlay)
		{
			VPath p; p.rect (0, 0, V (width), V (height)); p.fill (canvas, 0x000000, 70);
			const char *t = TR ("Paused"), *s = TR ("P or Space: resume");
			UkFaceScope sc (face (13)); int w = uk_tw (t, 2) + uk_tw (s) + 46;
			uk_rbox (canvas, (width - w) / 2, 10, w, 28, 14, uk_tone (C_FACE, 150), uk_tone (C_FACE, 120), 235);
			uk_tool_glyph (canvas, WKT_PAUSE, (width - w) / 2 + 10, 16, 16, C_TEXT);
			uk_text_l (canvas, (width - w) / 2 + 32, 10, 28, t, C_TEXT, 2);
			uk_text_l (canvas, (width - w) / 2 + 40 + uk_tw (t, 2), 10, 28, s, C_DIS);
		}
		if (fast && !overlay)
		{
			uk_rbox (canvas, width - 76, 10, 66, 24, 12, uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 120), 230);
			uk_tool_glyph (canvas, WKT_FORWARD, width - 68, 14, 16, uk_ink_on (C_ACCENT)); uk_text_l (canvas, width - 46, 10, 24, "\xC3\x97" "3", uk_ink_on (C_ACCENT), 2);
		}
		if (overlay && overlay != O_CARD) { VPath p; p.rect (0, 0, V (width), V (height)); p.fill (canvas, 0x000000, 120); }
		char b[160], t[32];
		int x, y;
		if (overlay == O_CARD)
		{
			int cw = 440, chh = 214; card (cw, chh, L->nm (), x, y);
			mmss (L->timeSec, t, sizeof t);
			snprintf (b, sizeof b, TR ("Save %d of %d"), L->save, L->count);
			{ UkFaceScope sc (face (16)); uk_text_c (canvas, x, y + 8, cw / 2 + 40, 24, b, C_TEXT, 2); }
			uk_glyph (canvas, WKG_HISTORY, x + cw / 2 + 58, y + 20, 16, C_DIS); uk_text_l (canvas, x + cw / 2 + 72, y + 8, 24, t, C_TEXT, 2);
			int rx = x + 24, n = 0; for (int r = 0; r < NROLES; r++) if (L->roles[r]) n++;
			rx = x + (cw - n * 52) / 2;
			for (int r = 0; r < NROLES; r++) if (L->roles[r])
			{
				uk_sunken (canvas, rx + 4, y + 40, 44, 44, 6, uk_tone (C_BG, 120)); role_icon (canvas, r, rx + 26, y + 61, 34, false);
				snprintf (b, sizeof b, "\xC3\x97%d", L->roles[r]); uk_text_c (canvas, rx, y + 86, 52, 18, b, C_TEXT, 2); rx += 52;
			}
			int st[6], lnn[6]; const char *h = L->ht (); int nl = uk_text_wrap (h, (int) strlen (h), cw - 48, 4, st, lnn);
			for (int i = 0; i < nl; i++) { snprintf (b, sizeof b, "%.*s", lnn[i], h + st[i]); uk_text_c (canvas, x + 24, y + 110 + i * uk_fh (), cw - 48, uk_fh (), b, C_TEXT); }
			UkFaceScope sc (face (11)); uk_text_c (canvas, x, y + chh - 48, cw, 18, TR ("Click or press a key to start"), C_DIS);
		}
		if (overlay == O_MENU) { card (260, 176, TR ("Paused"), x, y); uk_text_c (canvas, x, y + 124, 260, 20, TR ("Esc: resume"), C_DIS); }
		if (overlay == O_NUKE)
		{
			int cw = 400; card (cw, 172, TR ("All explode?"), x, y);
			nuke_icon (canvas, x + 40, y + 38, 40, false);
			const char *s = TR ("Every critter still out bursts after a 5-second countdown, and no more come out. The level then ends.");
			int st[5], lnn[5]; int nl = uk_text_wrap (s, (int) strlen (s), cw - 96, 4, st, lnn);
			for (int i = 0; i < nl; i++) { snprintf (b, sizeof b, "%.*s", lnn[i], s + st[i]); uk_text (canvas, x + 76, y + 14 + i * uk_fh (), b, C_TEXT); }
			UkFaceScope sc (face (11)); uk_text_c (canvas, x, y + 116, cw, 18, TR ("N or Enter: all explode \xC2\xB7 Esc: cancel"), C_DIS);
		}
		if (overlay == O_WON || overlay == O_LOST)
		{
			bool won = overlay == O_WON; int cw = 380, chh = 232;
			card (cw, chh, won ? TR ("Level complete!") : TR ("Not enough critters saved"), x, y);
			int saved = won ? 9 : 5;
			snprintf (b, sizeof b, "%d / %d", saved, L->count);
			{ UkFaceScope sc (face (30)); uk_text_c (canvas, x, y + 8, cw, 40, b, won ? C_TEXT : 0xC0302A, 2); }
			snprintf (b, sizeof b, TR ("saved (%d %%)"), saved * 100 / L->count);
			uk_text_c (canvas, x, y + 48, cw, 18, b, C_DIS);
			// the two figures in a sunken strip
			uk_sunken (canvas, x + 24, y + 74, cw - 48, 34, 6, uk_tone (C_BG, 140));
			uk_etch_v (canvas, x + cw / 2, y + 80, 22, uk_tone (C_BG, 140));
			snprintf (b, sizeof b, "%s  %d", TR ("Needed"), L->save); uk_text_c (canvas, x + 24, y + 74, cw / 2 - 24, 34, b, C_TEXT, 2);
			mmss (won ? 74 : 180, t, sizeof t); snprintf (b, sizeof b, "%s  %s", TR ("Time"), t); uk_text_c (canvas, x + cw / 2, y + 74, cw / 2 - 24, 34, b, C_TEXT, 2);
			if (won)
			{
				const char *nb2 = TR ("New best!"); UkFaceScope sc (face (12)); int w = uk_tw (nb2, 2) + 24;
				uk_rbox (canvas, x + (cw - w) / 2, y + 116, w, 22, 11, uk_tone (C_ACCENT, 150), C_ACCENT); uk_text_c (canvas, x + (cw - w) / 2, y + 116, w, 22, nb2, uk_ink_on (C_ACCENT), 2);
			}
			else { UkFaceScope sc (face (12)); uk_text_c (canvas, x, y + 116, cw, 22, TR ("Try another role, or give it sooner."), C_DIS); }
		}
		if (overlay == O_HELP)
		{
			int cw = 620, chh = 296; card (cw, chh, TR ("How to play"), x, y);
			const char *s = TR ("Critters come out of the hatch and walk on. Click a role, then a critter, to give it a job. Lead enough of them to the glowing exit before the time runs out.");
			int st[4], lnn[4]; int nl = uk_text_wrap (s, (int) strlen (s), cw - 40, 3, st, lnn);
			for (int i = 0; i < nl; i++) { snprintf (b, sizeof b, "%.*s", lnn[i], s + st[i]); uk_text (canvas, x + 20, y + 8 + i * uk_fh (), b, C_TEXT); }
			static const char *const DESC[6] = { TRN ("climbs walls instead of turning"), TRN ("survives any fall under its leaf"), TRN ("stands still: the others turn back"),
				TRN ("lays a stair of 12 bricks"), TRN ("digs straight down through earth"), TRN ("bursts after 5 seconds, with the earth") };
			for (int r = 0; r < 6; r++)
			{
				int col = r / 3, row = r % 3, rx = x + 16 + col * (cw / 2 - 8), ry = y + 68 + row * 56;
				uk_sunken (canvas, rx, ry, 48, 48, 6, uk_tone (C_BG, 120)); role_icon (canvas, r, rx + 24, ry + 24, 38, false);
				snprintf (b, sizeof b, "%d  %s", r + 1, TR (ROLE_NAME[r])); uk_text_l (canvas, rx + 58, ry + 2, 22, b, C_TEXT, 2);
				const char *ds = TR (DESC[r]); int st2[3], ln2[3]; int n2 = uk_text_wrap (ds, (int) strlen (ds), cw / 2 - 90, 2, st2, ln2);
				for (int i = 0; i < n2; i++) { snprintf (b, sizeof b, "%.*s", ln2[i], ds + st2[i]); uk_text (canvas, rx + 58, ry + 24 + i * (uk_fh () - 1), b, C_DIS); }
			}
			UkFaceScope sc (face (11)); uk_text_c (canvas, x, y + chh - 54, cw, 18, TR ("Esc or F1: close"), C_DIS);
		}
	}
};

// ---- the status line (04 §2.3): the chosen role, what is under the pointer; Out, Saved, Time ---------------------------
class StatusLine : public Widget
{
public:
	int role, left, out, saved, needed, timeLeft; const char *under; int nunder; bool onlyBlockers;
	StatusLine (int l, int t, int w, int h) : Widget (l, t, w, h), role (-1), left (0), out (0), saved (0), needed (0), timeLeft (0), under (0), nunder (0), onlyBlockers (false) {}
	int seg (int xr, const char *cap, const char *val, unsigned ink)	// right-aligned "Caption value", returns its left
	{
		int vw = uk_text_w (val, 2), cw = uk_text_w (cap);
		uk_text_l (canvas, xr - vw, 0, height, val, ink, 2); uk_text_l (canvas, xr - vw - 6 - cw, 0, height, cap, C_DIS);
		return xr - vw - 6 - cw;
	}
	void onDraw () override
	{
		canvas.clear (uk_tone (C_BG, 120));
		uk_etch_h (canvas, 0, height - 2, width, uk_tone (C_BG, 120));
		char b[64], t[16];
		mmss (timeLeft, t, sizeof t);
		int x = seg (width - 12, TR ("Time"), t, timeLeft < 30 ? 0xD03A2A : C_TEXT);
		uk_etch_v (canvas, x - 14, 5, height - 10, uk_tone (C_BG, 120));
		snprintf (b, sizeof b, "%d / %d", saved, needed);
		x = seg (x - 28, TR ("Saved"), b, saved >= needed ? 0x2E8A3E : C_TEXT);
		uk_etch_v (canvas, x - 14, 5, height - 10, uk_tone (C_BG, 120));
		snprintf (b, sizeof b, "%d", out); x = seg (x - 28, TR ("Out"), b, onlyBlockers ? 0xC07800 : C_TEXT);
		int lx = 10;
		if (onlyBlockers)
		{
			nuke_icon (canvas, lx + 8, height / 2, 16, false);
			char fit[160]; uk_text_fit (TR ("Only blockers are left: N (All explode) ends the level."), x - 30 - lx - 22, fit, sizeof fit, 2);
			uk_text_l (canvas, lx + 22, 0, height, fit, 0xB06A00, 2);
			return;
		}
		if (role >= 0)
		{
			role_icon (canvas, role, lx + 9, height / 2, 22, false);
			uk_text_l (canvas, lx + 24, 0, height, TR (ROLE_NAME[role]), C_TEXT, 2); lx += 24 + uk_text_w (TR (ROLE_NAME[role]), 2) + 6;
			snprintf (b, sizeof b, TR ("%d left"), left); uk_text_l (canvas, lx, 0, height, b, C_DIS); lx += uk_text_w (b) + 14;
		}
		if (under)
		{
			uk_etch_v (canvas, lx - 6, 5, height - 10, uk_tone (C_BG, 120)); lx += 6;
			uk_glyph (canvas, WKG_RING, lx + 4, height / 2, 10, C_DIS); lx += 14;
			uk_text_l (canvas, lx, 0, height, under, C_TEXT); lx += uk_text_w (under) + 6;
			if (nunder > 1) { snprintf (b, sizeof b, TR ("(%d here)"), nunder); uk_text_l (canvas, lx, 0, height, b, C_DIS); }
		}
	}
};

// ---- a slot of the skill bar (04 §2.4): a role (its key, its count, its picture) -- or Pause, Fast forward, All explode --
enum { K_PAUSE = 8, K_FAST, K_NUKE };
class SkillSlot : public Widget
{
public:
	int kind, count; bool sel, lit, pulse, absent;
	SkillSlot (int l, int t, int w, int h, int k) : Widget (l, t, w, h), kind (k), count (0), sel (false), lit (false), pulse (false), absent (false) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		bool role = kind < 8, off = role && (count == 0 || absent);
		if (pulse) uk_rbox (canvas, 0, 0, width, height, 8, uk_tone (0xFF9A30, 170), 0xFF9A30);
		if (sel || lit) { uk_raised (canvas, 2, 2, width - 4, height - 4, 6, uk_tone (C_ACCENT, 150), UK_PRESSED); uk_rline (canvas, 1, 1, width - 2, height - 2, 7, C_ACCENT); uk_rline (canvas, 2, 2, width - 4, height - 4, 6, C_ACCENT); }
		else uk_raised (canvas, 2, 2, width - 4, height - 4, 6, off ? uk_tone (C_BG, 120) : uk_tone (C_BG, 150));
		unsigned ink = sel || lit ? uk_ink_for (uk_tone (C_ACCENT, 150)) : C_TEXT;
		const char *key = kind < 8 ? 0 : kind == K_PAUSE ? "P" : kind == K_FAST ? "F" : "N";
		char kb[4]; if (role) { snprintf (kb, sizeof kb, "%d", kind + 1); key = kb; }
		{ UkFaceScope sc (face (10)); uk_text (canvas, 7, 5, key, sel || lit ? ink : off ? uk_mix (C_BG, C_DIS, 160) : C_DIS, 2); }
		if (role)
		{
			char b[8]; if (absent) cpy (b, "\xE2\x80\x93", 8); else snprintf (b, sizeof b, "%d", count);
			UkFaceScope sc (face (17)); uk_text_c (canvas, 0, 6, width, 24, b, off ? uk_mix (C_BG, C_DIS, 170) : ink, 2);
			role_icon (canvas, kind, width / 2, 62, 34, off);
		}
		else if (kind == K_NUKE) nuke_icon (canvas, width / 2, 50, 26, false);
		else uk_tool_glyph (canvas, kind == K_PAUSE ? WKT_PAUSE : WKT_FORWARD, width / 2 - 11, 39, 22, ink);
	}
};

// ---- the minimap (04 §2.5): the whole level, the creatures as dots, the visible frame -------------------------------------
class MiniMap : public Widget
{
public:
	Field *f;
	MiniMap (int l, int t, int w, int h) : Widget (l, t, w, h), f (0) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, 0x101418);
		Level &L = *f->L; int iw = width - 8, ih = height - 8;
		double sx = (double) iw / L.w, sy = fmin ((double) ih / L.h, 2 * sx); if (sx > 1) sx = 1;
		int mw = (int) (L.w * sx), mh = (int) (L.h * sy), x0 = 4 + (iw - mw) / 2, y0 = 4 + (ih - mh) / 2;
		for (int y = 0; y < mh; y++) for (int x = 0; x < mw; x++)
		{
			int lx = (int) (x / sx), ly = (int) (y / sy), best = -1;			// the block's first non-empty pixel's colour
			for (int dy = 0; dy < (int) (1 / sy) + 1 && best < 0; dy++) for (int dx = 0; dx < (int) (1 / sx) + 1; dx++)
			{ int X = lx + dx, Y = ly + dy; if (X < L.w && Y < L.h && L.m[Y * L.w + X]) { best = Y * L.w + X; break; } }
			canvas.px[(y0 + y) * canvas.stride + x0 + x] = best >= 0 ? L.col[best] : uk_mix (L.bg, 0, 60);
		}
		for (int i = 0; i < L.nexit; i++) { VPath p; p.circle (V (x0) + (int) (L.exit[i].x * sx * 16), V (y0) + (int) ((L.exit[i].y - 6) * sy * 16), V (3)); p.fill (canvas, 0x60F0D8); }
		for (int i = 0; i < L.nhatch; i++) uk_rbox (canvas, x0 + (int) (L.hatch[i].x * sx) - 3, y0 + (int) ((L.hatch[i].y - 8) * sy) - 2, 7, 4, 2, 0xA8783E, 0x6A5644);
		for (int i = 0; i < f->nc; i++) if (f->c[i].state != S_SPLAT) canvas.fillRect (x0 + (int) (f->c[i].x * sx) - 1, y0 + (int) ((f->c[i].y - 4) * sy) - 1, 2, 2, 0xFFD24A);
		int vw = (int) (f->width / 2 * sx); if (vw > mw) vw = mw;
		uk_rline (canvas, x0 + (int) (f->vx * sx), y0 - 2, vw, mh + 4, 2, 0xFFFFFF, 230);
	}
};

// ---- the picker (04 §3) ---------------------------------------------------------------------------------------------------
static int wrapped (const char *s, int w) { int st[8], ln[8]; return uk_text_wrap (s, (int) strlen (s), w, 8, st, ln); }
class Legend : public Widget
{
public:
	const char *key[6], *act[6]; int n;
	Legend (int l, int t, int w, int h) : Widget (l, t, w, h), n (0) {}
	static int cap (Canvas &cv, int x, int y, const char *k)
	{
		UkFaceScope sc (face (11)); int w = uk_tw (k, 2) + 12; if (w < 22) w = 22;
		uk_raised (cv, x, y, w, 20, 4, uk_tone (C_BG, 150)); uk_text_c (cv, x, y, w, 19, k, C_TEXT, 2); return w;
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int x = 0;
		for (int i = 0; i < n; i++)
		{
			int cx = x;
			for (const char *k = key[i]; *k; )
			{ char one[16]; int j = 0; while (k[j] && k[j] != '|' && j < 15) { one[j] = k[j]; j++; } one[j] = 0; cx += cap (canvas, cx, 1, one) + 3; k += j; if (*k == '|') k++; }
			uk_text_l (canvas, cx + 3, 0, 22, act[i], C_DIS); x = cx + 3 + uk_text_w (act[i]) + 12;
		}
	}
};
static bool locked (int i) { return i < 12 && PROG[i][0] < 0; }
class LevelList : public Widget
{
public:
	LevelList (int l, int t, int w, int h) : Widget (l, t, w, h) { canFocus = true; }
	void heading (int &y, const char *s, const char *sub)
	{
		UkFaceScope sc (face (11)); uk_text_l (canvas, 12, y, 20, s, C_DIS, 2);
		if (sub) uk_text_l (canvas, 16 + uk_tw (s, 2), y, 20, sub, uk_mix (C_FIELD, C_DIS, 170));
		y += 20;
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD, true);
		int tot = 4, selB = 0;						// (scrolled so that the chosen row shows)
		for (int i = 0; i < g_nlv; i++) { if (i == 0) tot += 20; if (i == 6 || i == 12) tot += 26; tot += (g_lv[i]->ok ? 25 : 40) + 1; if (i == g_sel) selB = tot; }
		int off = selB > height - 6 ? selB - (height - 6) : 0;
		int y = 4 - off;
		for (int i = 0; i < g_nlv; i++)
		{
			if (i == 0) heading (y, TR ("Training"), 0);
			if (i == 6) { y += 3; uk_etch_h (canvas, 10, y, width - 28, C_FIELD); y += 3; heading (y, TR ("Expedition"), 0); }
			if (i == 12) { y += 3; uk_etch_h (canvas, 10, y, width - 28, C_FIELD); y += 3; heading (y, TR ("My levels"), "SD:/docs/critters"); }
			Level &L = *g_lv[i]; int rh = L.ok ? 25 : 40; bool sel = i == g_sel, lk = locked (i);
			if (sel) uk_hilite (canvas, 4, y, width - 22, rh, 6, true);
			unsigned ink = sel ? uk_hilite_ink (true) : C_FIELD_TEXT, dim = sel ? uk_mix (uk_hilite_ink (true), C_ACCENT, 70) : C_DIS;
			if (lk || !L.ok) ink = sel ? uk_mix (C_ACCENT, ink, 200) : uk_mix (C_FIELD, C_FIELD_TEXT, 130);
			char b[96];
			// the number badge (the player's levels: a small file mark)
			if (i < 12) { snprintf (b, sizeof b, "%d", i % 6 + 1); uk_rbox (canvas, 10, y + 4, 20, 17, 5, sel ? uk_tone (C_ACCENT, 100) : uk_tone (C_FIELD, 112), sel ? uk_tone (C_ACCENT, 90) : uk_tone (C_FIELD, 104)); UkFaceScope sc (face (11)); uk_text_c (canvas, 10, y + 4, 20, 17, b, sel ? 0xFFFFFF : C_DIS, 2); }
			else if (L.ok) uk_glyph (canvas, WKG_DOT, 20, y + 12, 8, dim);
			else { VPath p; int t[6] = { V (20), V (y + 4), V (29), V (y + 20), V (11), V (y + 20) }; p.poly (t, 3); p.fill (canvas, 0xE0A020); uk_text_c (canvas, 11, y + 6, 18, 14, "!", 0x000000, 2); }
			uk_text_l (canvas, 38, y + (L.ok ? 0 : 1), 24, L.ok ? L.nm () : L.file, ink, sel && L.ok ? 2 : 0);
			if (!L.ok) { char fit[96]; uk_text_fit (L.err, width - 60, fit, sizeof fit); uk_text_l (canvas, 38, y + 18, 20, fit, sel ? ink : 0xC0302A); }
			else if (lk) uk_glyph (canvas, WKG_LOCK, width - 34, y + 12, 12, dim);
			else
			{
				int p = i < 12 ? i : 12;
				if (PROG[p][0] == 1)
				{
					snprintf (b, sizeof b, "%d/%d", PROG[p][1], L.count);
					UkFaceScope sc (face (11)); uk_text_l (canvas, width - 40 - uk_tw (b), y, 24, b, dim);
					uk_glyph (canvas, WKG_CHECK, width - 30, y + 12, 12, sel ? ink : 0x2E9A44);
				}
				else { UkFaceScope sc (face (10)); const char *nw = TR ("new"); int w = uk_tw (nw, 2) + 10; uk_rbox (canvas, width - 26 - w, y + 6, w, 14, 7, 0xFF9A30, 0xF07A10); uk_text_c (canvas, width - 26 - w, y + 6, w, 14, nw, 0xFFFFFF, 2); }
			}
			y += rh + 1;
		}
		{ int th = height - 8; uk_scroll_bar (canvas, width - 14, 4, 10, th, true, off * th / tot, th * th / tot, C_FIELD); }
		uk_rline (canvas, 0, 0, width, height, 6, C_ACCENT);
	}
};
class Preview : public Widget
{
public:
	Preview (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		Level &L = *g_lv[g_sel];
		if (!L.ok)
		{
			uk_sunken (canvas, 0, 0, width, height, 8, uk_tone (C_BG, 120));
			VPath p; int cx = 52, ty = 30; int tri[6] = { V (cx), V (ty), V (cx + 30), V (ty + 52), V (cx - 30), V (ty + 52) }; p.poly (tri, 3); p.fill (canvas, 0xE0A020);
			{ UkFaceScope sc (face (30)); uk_text_c (canvas, cx - 20, ty + 14, 40, 40, "!", 0x000000, 2); }
			const char *lines[3] = { TR ("This level cannot be played."), L.err, TR ("Correct the file in a text editor (Tinypad): the list is read again when you come back to it.") };
			int y = 22;
			for (int k = 0; k < 3; k++)
			{
				int st[6], lnn[6]; int n = uk_text_wrap (lines[k], (int) strlen (lines[k]), width - 120, 5, st, lnn, 0, k == 0 ? 2 : 0);
				for (int i = 0; i < n; i++) { char b[200]; snprintf (b, sizeof b, "%.*s", lnn[i], lines[k] + st[i]); uk_text (canvas, 104, y, b, k == 1 ? 0xC0302A : k == 0 ? C_TEXT : C_DIS, k == 0 ? 2 : 0); y += uk_fh (); }
				y += 8;
			}
			return;
		}
		build (L);
		double s = fmin ((width - 8.0) / L.w, (height - 8.0) / L.h);
		int pw = (int) (L.w * s), ph = (int) (L.h * s), x0 = (width - pw) / 2, y0 = (height - ph) / 2;
		uk_rbox (canvas, x0 - 4, y0 - 4, pw + 8, ph + 8, 6, uk_tone (C_BG, 90), uk_tone (C_BG, 70));
		for (int y = 0; y < ph; y++) for (int x = 0; x < pw; x++) canvas.px[(y0 + y) * canvas.stride + x0 + x] = L.col[(int) (y / s) * L.w + (int) (x / s)];
		double k = s / 2 * 1.4; if (k > 0.9) k = 0.9;
		for (int i = 0; i < L.nexit; i++) portal (canvas, x0 + L.exit[i].x * s, y0 + (L.exit[i].y + 1) * s, 200, k);
		for (int i = 0; i < L.nhatch; i++) hatch (canvas, x0 + L.hatch[i].x * s, y0 + (L.hatch[i].y + 1) * s - 2, false, k);
		if (locked (g_sel))
		{
			VPath p; p.rect (V (x0), V (y0), V (pw), V (ph)); p.fill (canvas, 0x000000, 120);
			uk_rbox (canvas, width / 2 - 22, height / 2 - 22, 44, 44, 22, uk_tone (C_FACE, 150), uk_tone (C_FACE, 110), 230);
			uk_glyph (canvas, WKG_LOCK, width / 2, height / 2, 22, C_TEXT);
		}
	}
};
class LevelInfo : public Widget
{
public:
	LevelInfo (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		Level &L = *g_lv[g_sel]; if (!L.ok) return;
		char b[200], t[16];
		{ UkFaceScope sc (face (17)); uk_text (canvas, 0, 0, L.nm (), C_TEXT, 2); }
		if (g_sel < 12) snprintf (b, sizeof b, TR ("%s \xC2\xB7 level %d of 6"), g_sel < 6 ? TR ("Training") : TR ("Expedition"), g_sel % 6 + 1);
		else snprintf (b, sizeof b, "%s \xC2\xB7 %s", TR ("My levels"), L.file);
		uk_text (canvas, 0, 24, b, C_DIS);
		// the facts in a sunken strip: save, time, rate
		uk_sunken (canvas, 0, 46, width, 30, 6, uk_tone (C_BG, 140));
		mmss (L.timeSec, t, sizeof t);
		const char *cap[3] = { TR ("Save"), TR ("Time"), TR ("Rate") }; char val[3][24];
		snprintf (val[0], 24, TR ("%d of %d"), L.save, L.count); cpy (val[1], t, 24); snprintf (val[2], 24, "%d", L.rate);
		for (int i = 0; i < 3; i++)
		{
			int cx = i * width / 3; if (i) uk_etch_v (canvas, cx, 51, 20, uk_tone (C_BG, 140));
			int w = uk_text_w (cap[i]) + 6 + uk_text_w (val[i], 2), x = cx + (width / 3 - w) / 2;
			uk_text_l (canvas, x, 46, 30, cap[i], C_DIS); uk_text_l (canvas, x + uk_text_w (cap[i]) + 6, 46, 30, val[i], C_TEXT, 2);
		}
		// the roles it gives
		int x = 0;
		for (int r = 0; r < 6; r++)
		{
			bool off = L.roles[r] == 0;
			role_icon (canvas, r, x + 18, 102, 30, off);
			if (off) cpy (b, "\xE2\x80\x93", 8); else snprintf (b, sizeof b, "\xC3\x97%d", L.roles[r]);
			uk_text_l (canvas, x + 36, 88, 30, b, off ? uk_mix (C_BG, C_DIS, 150) : C_TEXT, 2);
			x += width / 6;
		}
		// the hint, then the best result (or why it is locked)
		const char *h = L.ht(); int st[4], lnn[4]; int nl = uk_text_wrap (h, (int) strlen (h), width, 3, st, lnn);
		for (int i = 0; i < nl; i++) { snprintf (b, sizeof b, "%.*s", lnn[i], h + st[i]); uk_text (canvas, 0, 128 + i * uk_fh (), b, C_TEXT); }
		int by = 132 + nl * uk_fh () + 6; int p = g_sel < 12 ? g_sel : 12;
		if (locked (g_sel))
		{
			uk_glyph (canvas, WKG_LOCK, 8, by + 10, 12, C_DIS);
			snprintf (b, sizeof b, TR ("Solve \xE2\x80\x9C%s\xE2\x80\x9D to open this level."), g_lv[g_sel - 1]->nm ()); uk_text_l (canvas, 22, by, 20, b, C_DIS);
		}
		else if (PROG[p][0] == 1)
		{
			uk_glyph (canvas, WKG_CHECK, 8, by + 10, 12, 0x2E9A44); mmss (PROG[p][2], t, sizeof t);
			snprintf (b, sizeof b, TR ("Best: %d saved \xC2\xB7 %s"), PROG[p][1], t); uk_text_l (canvas, 22, by, 20, b, C_TEXT, 2);
		}
		else uk_text_l (canvas, 0, by, 20, TR ("Not solved yet."), C_DIS);
	}
};

// ---- the scenes --------------------------------------------------------------------------------------------------------
static int g_n;
static Critter *add (Field *f, int x, int state, int dir = 1, int frame = 0, int flags = 0, int fuse = 0, int y = -1)
{
	Critter &c = f->c[f->nc++]; c.x = x; c.y = y >= 0 ? y : ground (*f->L, x, 0); c.dir = dir; c.state = state; c.frame = frame; c.flags = flags; c.fuse = fuse; g_n++;
	return &c;
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();
	g_fr = !strcmp (uk_lang (), "fr");
	for (int i = 0; FILES[i]; i++) { g_lv[i] = new Level; load (FILES[i], *g_lv[i]); g_nlv++; }
	g_sel = getenv ("MOCK_SEL") ? atoi (getenv ("MOCK_SEL")) : 3;

	if (scene ("sheet"))							// the style sheet: every state at x3, the hatch, the exit
	{
		Root root (760, 300, "Critters -- the style sheet");
		class Sheet : public Widget { public: Sheet () : Widget (0, 0, 760, 300) {}
			void onDraw () override
			{
				canvas.clear (0x101830);
				static const int ST[12][4] = { { S_WALK, 0, 0, 0 }, { S_WALK, 2, 0, 0 }, { S_FALL, 0, 0, 0 }, { S_FLOAT, 0, F_FLOATER, 0 }, { S_CLIMB, 0, F_CLIMBER, 0 }, { S_BLOCK, 0, 0, 0 },
							       { S_BUILD, 0, 0, 0 }, { S_DIG, 1, 0, 0 }, { S_SHRUG, 0, 0, 0 }, { S_WALK, 1, 0, 3 }, { S_WALK, 1, F_CLIMBER | F_FLOATER, 1 }, { S_SPLAT, 0, 0, 0 } };
				static const char *const NM[12] = { "walk 1", "walk 3", "fall", "float", "climb", "block", "build", "dig", "shrug", "exploder 3", "both, fuse 1", "splat" };
				for (int i = 0; i < 12; i++)
				{
					int cx = 40 + (i % 6) * 64, cy = i < 6 ? 110 : 230;
					Critter c = { 0, 0, 1, ST[i][0], ST[i][1], ST[i][2], ST[i][3] };
					if (c.state == S_CLIMB) { box (canvas, cx + 22, cy - 90, 14, 90, 0x8890A0); }
					box (canvas, cx - 30, cy, 60, 6, 0x8A5A34);
					critter (canvas, cx, cy, c, 3.0 * 0.62);
					UkFaceScope sc (face (10)); uk_text_c (canvas, cx - 32, cy + 8, 64, 14, NM[i], 0xC8D0E0);
				}
				box (canvas, 420, 0, 340, 300, 0x0C1426);
				hatch (canvas, 500, 120, false, 2); hatch (canvas, 500, 250, true, 2);
				portal (canvas, 670, 250, 220, 2.6);
				UkFaceScope sc (face (10)); uk_text_c (canvas, 440, 128, 120, 14, "hatch, closed", 0xC8D0E0); uk_text_c (canvas, 440, 270, 120, 14, "hatch, open", 0xC8D0E0); uk_text_c (canvas, 610, 270, 120, 14, "exit (pulsing)", 0xC8D0E0);
			} };
		root.addChild (new Sheet ());
		root.run (); return 0;
	}

	Root root (W, H, TR ("Critters"));
	if (root.canvas.px == 0) return 1;

	if (scene ("picker"))
	{
		int Lw = 300;
		LevelList *ll = new LevelList (12, 12, Lw, H - 24); root.addChild (ll); ll->setFocus ();
		Preview *pv = new Preview (Lw + 24, 12, W - Lw - 36, 150); root.addChild (pv);
		LevelInfo *li = new LevelInfo (Lw + 26, 174, W - Lw - 40, 216); root.addChild (li);
		Level &L = *g_lv[g_sel]; bool can = L.ok && !locked (g_sel);
		if (!L.ok) { pv->height = 0; delete pv; pv = new Preview (Lw + 24, 12, W - Lw - 36, 200); root.addChild (pv); ((Widget *) li)->hidden = true; }
		ToolButton *play = (new ToolButton (140, 36, TR ("Play this level (Enter)")))->setGlyph (WKT_PLAY)->setText (TR ("Play"));
		play->filled = true; play->raised = true; play->setOn (can); play->setDisabled (!can); play->left = W - 12 - 140; play->top = H - 12 - 36; root.addChild (play);
		Legend *lg = new Legend (Lw + 26, H - 12 - 29, W - Lw - 26 - 12 - 140 - 8, 22);
		lg->key[0] = "\xE2\x86\x91|\xE2\x86\x93"; lg->act[0] = TR ("choose"); lg->key[1] = TR ("Enter"); lg->act[1] = TR ("play"); lg->key[2] = TR ("Esc"); lg->act[2] = TR ("quit"); lg->n = 3;
		root.addChild (lg);
		root.run (); return 0;
	}

	// the play screen: the view, the status line, the skill bar
	int lv = scene ("build") ? 7 : scene ("card") || scene ("won") || scene ("lost") ? 1 : scene ("help") ? 0 : 6;
	if (getenv ("MOCK_LEVEL")) lv = atoi (getenv ("MOCK_LEVEL"));
	Level &L = *g_lv[lv]; build (L);
	Field *f = new Field (0, 0, W, PLAYH); f->L = &L; root.addChild (f);
	StatusLine *sl = new StatusLine (0, STATUSY, W, STATUSH); root.addChild (sl);
	class BarBg : public Widget { public: BarBg () : Widget (0, BARY, W, H - BARY) {} void onDraw () override { canvas.clear (C_BG);
		uk_etch_v (canvas, 362, 8, height - 16, C_BG); uk_etch_v (canvas, 460, 8, height - 16, C_BG); uk_etch_v (canvas, 602, 8, height - 16, C_BG); } };
	root.addChild (new BarBg ());
	SkillSlot *slot[11];
	for (int i = 0; i < 8; i++) { slot[i] = new SkillSlot (8 + i * 44, BARY + 6, 44, 92, i); slot[i]->count = L.roles[i]; slot[i]->absent = i >= 6; root.addChild (slot[i]); }
	LcdDisplay *rate = new LcdDisplay (370, BARY + 6, 84, 40, "50", TR ("RATE")); rate->face = face (20); rate->smallFace = face (10); root.addChild (rate);
	char rb[8]; snprintf (rb, sizeof rb, "%d", L.rate); rate->setText (rb); snprintf (rb, sizeof rb, "\xE2\x89\xA5%d", L.rate); rate->setSub (rb);
	ToolButton *minus = (new ToolButton (41, 44, TR ("Slower release (-)")))->setGlyph (WKT_MINUS); minus->raised = true; minus->left = 370; minus->top = BARY + 52; root.addChild (minus);
	ToolButton *plus = (new ToolButton (41, 44, TR ("Faster release (+)")))->setGlyph (WKT_PLUS); plus->raised = true; plus->left = 413; plus->top = BARY + 52; root.addChild (plus);
	for (int i = 0; i < 3; i++) { slot[8 + i] = new SkillSlot (466 + i * 44, BARY + 6, 44, 92, 8 + i); root.addChild (slot[8 + i]); }
	MiniMap *mm = new MiniMap (610, BARY + 6, W - 618, 92); mm->f = f; root.addChild (mm);

	sl->needed = L.save; sl->timeLeft = L.timeSec;
	int sel = -1;
	if (lv == 6)								// Two Ways, about a minute in
	{
		f->vx = 0;
		add (f, 102, S_WALK, 1, 0); add (f, 140, S_WALK, 1, 2); add (f, 64, S_FALL, 1, 0, 0, 0, 60); add (f, 180, S_WALK, -1, 1);
		dig_rect (L, 210, 74, 218, 79); add (f, 214, S_DIG, 1, 1, 0, 0, 79);
		stair (L, 318, ground (L, 312, 90), 1, 4); add (f, 312 + 3 * 4, S_BUILD, 1, 0, 0, 0, ground (L, 312, 90) - 8);
		add (f, 296, S_FALL, 1, 0, 0, 0, 104); add (f, 250, S_WALK, -1, 1); add (f, 236, S_WALK, 1, 3); add (f, 196, S_WALK, 1, 1, 0, 0, 125);
		f->c[f->nc - 1].y = ground (L, 196, 90);
		add (f, 160, S_WALK, 1, 2, 0, 0, ground (L, 160, 90));
		f->hi = 0; sel = R_DIGGER;
		L.roles[R_BUILDER] = 3; L.roles[R_DIGGER] = 1;
		sl->out = 11; sl->saved = 3; sl->timeLeft = 3 * 60 + 2; sl->under = TR ("Walker"); sl->nunder = 1;
	}
	if (lv == 7)								// Steel Floor: a digger's shaft stopped on steel, a builder's stair, an exploder counting
	{
		f->vx = 260;
		dig_rect (L, 341, 81, 349, 111); add (f, 345, S_WALK, 1, 2, 0, 0, 111);
		dig_rect (L, 431, 81, 439, 100); add (f, 435, S_DIG, 1, 1, 0, 0, 100);
		int gy = ground (L, 470, 60); stair (L, 476, gy, 1, 9); add (f, 470 + 27, S_BUILD, 1, 0, 0, 0, gy - 18);
		add (f, 400, S_WALK, 1, 1, 0, 3, ground (L, 400, 60)); add (f, 300, S_WALK, 1, 0, 0, 0, ground (L, 300, 60)); add (f, 330, S_WALK, -1, 2, 0, 0, ground (L, 330, 60));
		add (f, 560, S_WALK, 1, 3, 0, 0, ground (L, 560, 60)); add (f, 590, S_WALK, -1, 1, 0, 0, ground (L, 590, 60));
		dig_disc (L, 610, ground (L, 610, 60) - 4, 12); f->bursts[f->nb][0] = 610; f->bursts[f->nb++][1] = ground (L, 610, 60) - 12;
		L.roles[R_DIGGER] = 1; L.roles[R_BUILDER] = 2; L.roles[R_EXPLODER] = 1;
		f->hi = 5; f->focus = true; sel = R_BUILDER; f->refuse = false;
		sl->out = 14; sl->saved = 0; sl->timeLeft = 2 * 60 + 47; sl->under = TR ("Walker"); sl->nunder = 1;
	}
	if (lv == 1)								// Mind the Gap
	{
		f->vx = 0; f->hatchOpen = !scene ("card");
		if (scene ("card")) { f->overlay = O_CARD; f->paused = true; sl->out = 0; }
		else
		{
			stair (L, 186, ground (L, 180, 60), 1, 12);
			for (int i = 0; i < 4; i++) add (f, 280 + i * 34, S_WALK, i & 1 ? -1 : 1, i);
			f->overlay = scene ("won") ? O_WON : O_LOST; sl->out = 0; sl->saved = scene ("won") ? 9 : 5; sl->timeLeft = scene ("won") ? 106 : 0;
		}
	}
	if (lv == 0)
	{
		f->overlay = O_HELP; f->hatchOpen = true; add (f, 120, S_WALK, 1, 0, 0, 0, 69); add (f, 160, S_WALK, -1, 2, 0, 0, 69); sl->out = 2;
	}
	if (scene ("pause")) { f->paused = true; slot[K_PAUSE]->lit = true; }
	if (scene ("fast")) { f->fast = true; slot[K_FAST]->lit = true; }
	if (scene ("menu")) f->overlay = O_MENU;
	if (scene ("nuke")) f->overlay = O_NUKE;
	if (scene ("refuse")) { f->hi = 2; f->refuse = true; sl->under = TR ("Falling \xE2\x80\x94 cannot dig now"); }
	if (scene ("blockers"))
	{
		f->nc = 0; f->hi = -1; sel = -1;
		add (f, 230, S_BLOCK, 1, 0, 0, 0, ground (L, 230, 90)); add (f, 150, S_BLOCK, 1, 0, 0, 0, 73);
		sl->out = 2; sl->saved = 17; sl->timeLeft = 61; sl->onlyBlockers = true; slot[K_NUKE]->pulse = true;
		L.roles[R_BLOCKER] = 0; L.roles[R_BUILDER] = 0; L.roles[R_DIGGER] = 0;
	}
	for (int i = 0; i < 8; i++) { slot[i]->count = L.roles[i]; slot[i]->sel = i == sel; }
	sl->role = sel; if (sel >= 0) sl->left = L.roles[sel];
	if (f->overlay == O_MENU)
	{
		int cw = 260, cx = (W - cw) / 2, cy = (PLAYH - 176) / 2 + uk_fh () + 10;
		Button *r = new Button (cx + 30, cy + 12, cw - 60, 30, TR ("Resume")); root.addChild (r); r->setFocus ();
		root.addChild (new Button (cx + 30, cy + 48, cw - 60, 30, TR ("Restart Level")));
		root.addChild (new Button (cx + 30, cy + 84, cw - 60, 30, TR ("Back to the levels")));
	}
	if (f->overlay == O_NUKE)
	{
		int cw = 400, cx = (W - cw) / 2, cy = (PLAYH - 172) / 2 + uk_fh () + 10;
		Button *y = new Button (cx + cw - 20 - 150 - 10 - 110, cy + 82, 150, 30, TR ("All explode")); root.addChild (y); y->setFocus ();
		root.addChild (new Button (cx + cw - 20 - 110, cy + 82, 110, 30, TR ("Cancel")));
	}
	if (f->overlay == O_WON || f->overlay == O_LOST)
	{
		int cw = 380, cx = (W - cw) / 2, cy = (PLAYH - 232) / 2 + uk_fh () + 10, bw = 104, gap = 12, n = f->overlay == O_WON ? 3 : 2;
		int bx = cx + (cw - n * bw - (n - 1) * gap) / 2;
		Button *rt = new Button (bx, cy + 150, bw, 30, TR ("Retry")); root.addChild (rt); bx += bw + gap;
		if (n == 3) { Button *nx = new Button (bx, cy + 150, bw, 30, TR ("Next")); root.addChild (nx); nx->setFocus (); bx += bw + gap; } else rt->setFocus ();
		root.addChild (new Button (bx, cy + 150, bw, 30, TR ("Levels")));
	}
	if (f->overlay == O_CARD) { slot[K_PAUSE]->lit = true; }
	root.run ();
	return 0;
}
