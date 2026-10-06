//
// pinmock.cpp -- the UX Designer's mock-ups of Pinball (AutoDev round 4), drawn by UIKit on the PC (the desktop
// simulator: fakekapi.cpp, FreeType's DejaVu Sans). A THROWAWAY: not the app, not the Developer's code, not a
// documentation screenshot. It reads the draft tables (mktables.py: the real .table format, read through FileKit's
// fk_kv), draws them as 04-ux-design.md says, and lays out the windows with UIKit's widgets (LcdDisplay, Button,
// ToolButton, Textbox...) and a few drawn ones (the table list, the panel's rows, the key legend). No physics: each
// scene's state (balls, lamps, flippers, messages) is set by hand (MOCK_SCENE). Built and run by mockups.sh.
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

// ---- faces -----------------------------------------------------------------------------------------------------------
static FtTextFace *g_face[64];
static TextFace *face (int px) { if (px < 6) px = 6; if (px > 63) px = 63; if (!g_face[px]) { g_face[px] = new FtTextFace; g_face[px]->open ("DejaVu Sans", px); } return g_face[px]; }

// ---- numbers: grouped by TRC ("digits", ",") (FR: a narrow no-break space) -------------------------------------------
static void fmt (long v, char *o, int cap)
{
	char d[24]; snprintf (d, sizeof d, "%ld", v);
	const char *sep = TRC ("digits", ",");
	int n = (int) strlen (d), k = 0;
	for (int i = 0; i < n && k < cap - 8; i++)
	{
		o[k++] = d[i];
		int left = n - 1 - i;
		if (left > 0 && left % 3 == 0) for (const char *s = sep; *s; s++) o[k++] = *s;
	}
	o[k] = 0;
}

// ---- the table (the draft's elements, just what is drawn) -------------------------------------------------------------
struct P { double x, y; };
struct Poly { P p[48]; int n; bool closed; unsigned col; double width; };
struct Arc  { P c; double r, from, to; unsigned col; double width; };
struct Circ { P c; double r; unsigned col; char id[25]; };
struct Seg  { P a, b; unsigned col; char id[25]; bool drop; };
struct Lane { double x, y, w, h; unsigned col; char id[25], group[16]; };
struct Flip { int side; P pivot; double len, rest, up, r0, r1; unsigned col; };
struct Ramp { P a, b, pass; P path[32]; int n; unsigned col; double width; char id[25]; };
struct Lbl  { P at; char en[64], fr[64]; int size, angle; unsigned col; };
struct Tbl
{
	char name[2][64], goal[2][200], file[64]; double w, h; unsigned bg; bool ok; char err[96];
	Poly wall[64]; int nwall; Poly shape[96]; int nshape; Arc arc[8]; int narc; Circ post[16]; int npost;
	Circ bump[8]; int nbump; Circ sauc[4]; int nsauc; Seg sling[4]; int nsling; Seg tgt[24]; int ntgt;
	Lane lane[16]; int nlane; Seg gate[4]; int ngate; Flip flip[3]; int nflip; Ramp ramp[4]; int nramp;
	Lbl lbl[24]; int nlbl; P plunger; unsigned slingCol, lampCol;
	struct Rule { char msg[2][64]; int count; bool once; } rule[16]; int nrule;
	const char *nm () const { return g_fr && name[1][0] ? name[1] : name[0]; }
	const char *gl () const { return g_fr && goal[1][0] ? goal[1] : goal[0]; }
};

static unsigned colour (const char *s, unsigned def) { return s && s[0] == '#' ? (unsigned) strtoul (s + 1, 0, 16) : def; }
static int points (const char *s, P *o, int cap)
{
	int n = 0; char *e;
	while (s && *s && n < cap)
	{
		double x = strtod (s, &e); if (e == s) break; s = e; while (*s == ' ' || *s == ',') s++;
		double y = strtod (s, &e); if (e == s) break; s = e; while (*s == ' ' || *s == ',') s++;
		o[n].x = x; o[n].y = y; n++;
	}
	return n;
}
static P pt (const char *s) { P p = { 0, 0 }; points (s, &p, 1); return p; }

static fk_kv *g_kv;
static const char *get (int b, const char *key, const char *def = 0)
{
	for (int i = 0; i < fk_kv_count (g_kv); i++) if (fk_kv_block (g_kv, i) == b && !strcmp (fk_kv_key (g_kv, i), key)) return fk_kv_value (g_kv, i);
	return def;
}
static double num (int b, const char *key, double def) { const char *v = get (b, key); return v ? atof (v) : def; }
static void cpy (char *d, const char *s, int cap) { snprintf (d, cap, "%s", s ? s : ""); }

static bool load (const char *path, Tbl &t)
{
	memset (&t, 0, sizeof t);
	const char *bn = strrchr (path, '/'); cpy (t.file, bn ? bn + 1 : path, sizeof t.file);
	void *f = kapi_open (path);
	if (!f) { snprintf (t.err, sizeof t.err, "%s", TR ("cannot read the file")); return false; }
	unsigned n = kapi_fsize (f); char *text = (char *) malloc (n + 1); int got = kapi_read (f, text, n); kapi_close (f);
	text[got > 0 ? got : 0] = 0;
	g_kv = fk_kv_parse (text, 0); free (text);
	static const char *const KNOWN[] = { "table", "wall", "arc", "post", "bumper", "sling", "target", "lane", "gate", "flipper", "plunger", "ramp", "saucer", "shape", "label", "rule", 0 };
	t.slingCol = 0xE0559A; t.lampCol = 0xFFD34D;
	for (int b = 1; b <= fk_kv_blocks (g_kv); b++)
	{
		const char *k = fk_kv_block_name (g_kv, b); bool known = false;
		for (int i = 0; KNOWN[i]; i++) if (!strcmp (k, KNOWN[i])) known = true;
		if (!known)
		{
			char r[64]; snprintf (r, sizeof r, TR ("unknown block [%s]"), k);
			snprintf (t.err, sizeof t.err, TR ("line %d: "), fk_kv_block_line (g_kv, b));
			strncat (t.err, r, sizeof t.err - strlen (t.err) - 1);
			fk_kv_free (g_kv); return false;
		}
		if (!strcmp (k, "table"))
		{
			cpy (t.name[0], get (b, "name"), 64); cpy (t.name[1], get (b, "name.fr"), 64);
			cpy (t.goal[0], get (b, "goal"), 200); cpy (t.goal[1], get (b, "goal.fr"), 200);
			P s = pt (get (b, "size", "520 1040")); t.w = s.x; t.h = s.y; t.bg = colour (get (b, "background"), 0x101828);
		}
		else if (!strcmp (k, "wall") || !strcmp (k, "shape"))
		{
			bool sh = k[0] == 's'; Poly &p = sh ? t.shape[t.nshape++] : t.wall[t.nwall++];
			p.n = points (get (b, "points"), p.p, 48); p.closed = sh || num (b, "closed", 0) != 0;
			p.col = colour (get (b, "colour"), 0x8090A0); p.width = num (b, "width", 4);
			if (!sh && get (b, "id") && !strcmp (get (b, "id"), "outline")) { p.closed = false; p.p[p.n++] = p.p[0]; p.width = 6; }	// (the outline: the side rails)
		}
		else if (!strcmp (k, "arc")) { Arc &a = t.arc[t.narc++]; a.c = pt (get (b, "centre")); a.r = num (b, "radius", 100); a.from = num (b, "from", 0); a.to = num (b, "to", 0); a.col = colour (get (b, "colour"), 0x8090A0); a.width = num (b, "width", 4); }
		else if (!strcmp (k, "post")) { Circ &c = t.post[t.npost++]; c.c = pt (get (b, "at")); c.r = num (b, "radius", 6); c.col = colour (get (b, "colour"), 0xE8E8E8); }
		else if (!strcmp (k, "bumper") || !strcmp (k, "saucer"))
		{ Circ &c = k[0] == 'b' ? t.bump[t.nbump++] : t.sauc[t.nsauc++]; c.c = pt (get (b, "at")); c.r = num (b, "radius", 20); c.col = colour (get (b, "colour"), 0xE04060); cpy (c.id, get (b, "id"), 25); }
		else if (!strcmp (k, "sling") || !strcmp (k, "target") || !strcmp (k, "gate"))
		{
			Seg &s = k[0] == 's' ? t.sling[t.nsling++] : k[0] == 't' ? t.tgt[t.ntgt++] : t.gate[t.ngate++];
			s.a = pt (get (b, "a")); s.b = pt (get (b, "b")); s.col = colour (get (b, "colour"), 0xF0A040); cpy (s.id, get (b, "id"), 25);
			s.drop = get (b, "kind") && !strcmp (get (b, "kind"), "drop");
			if (k[0] == 's' && k[1] == 'l') t.slingCol = s.col;
		}
		else if (!strcmp (k, "lane"))
		{
			Lane &l = t.lane[t.nlane++]; P r[2]; points (get (b, "rect"), r, 2); l.x = r[0].x; l.y = r[0].y; l.w = r[1].x; l.h = r[1].y;
			l.col = colour (get (b, "colour"), 0xFFD34D); cpy (l.id, get (b, "id"), 25); cpy (l.group, get (b, "group"), 16);
			if (!strcmp (l.group, "top")) t.lampCol = l.col;
		}
		else if (!strcmp (k, "flipper"))
		{
			Flip &f = t.flip[t.nflip++]; f.side = !strcmp (get (b, "side", "left"), "right"); f.pivot = pt (get (b, "pivot")); f.len = num (b, "length", 70);
			f.rest = num (b, "rest", 30); f.up = num (b, "up", -25); f.r0 = 12; f.r1 = 6; f.col = colour (get (b, "colour"), 0xF2F4F8);
		}
		else if (!strcmp (k, "plunger")) t.plunger = pt (get (b, "at"));
		else if (!strcmp (k, "ramp"))
		{
			Ramp &r = t.ramp[t.nramp++]; r.a = pt (get (b, "a")); r.b = pt (get (b, "b")); r.n = points (get (b, "path"), r.path, 32);
			const char *ps = get (b, "pass", "up"); r.pass.x = !strcmp (ps, "left") ? -1 : !strcmp (ps, "right") ? 1 : 0; r.pass.y = !strcmp (ps, "up") ? -1 : !strcmp (ps, "down") ? 1 : 0;
			r.col = colour (get (b, "colour"), 0x4FA3FF); r.width = num (b, "width", 24); cpy (r.id, get (b, "id"), 25);
		}
		else if (!strcmp (k, "rule"))
		{ Tbl::Rule &r = t.rule[t.nrule++]; cpy (r.msg[0], get (b, "message"), 64); cpy (r.msg[1], get (b, "message.fr"), 64); r.count = (int) num (b, "count", 1); r.once = num (b, "once", 0) != 0; }
		else if (!strcmp (k, "label"))
		{
			Lbl &l = t.lbl[t.nlbl++]; l.at = pt (get (b, "at")); cpy (l.en, get (b, "text"), 64); cpy (l.fr, get (b, "text.fr"), 64);
			l.size = (int) num (b, "size", 1); l.angle = (int) num (b, "angle", 0); l.col = colour (get (b, "colour"), 0xFFFFFF);
		}
	}
	fk_kv_free (g_kv);
	t.ok = true;
	return true;
}

// ---- the drawing (04-ux-design.md §5) --------------------------------------------------------------------------------
struct View { double s, ox, oy; };				// table units -> px: ox + u * s
static int VX (const View &v, double u) { return (int) lround ((v.ox + u * v.s) * 16); }
static int VY (const View &v, double u) { return (int) lround ((v.oy + u * v.s) * 16); }
static int VL (const View &v, double u) { int r = (int) lround (u * v.s * 16); return r < 8 ? 8 : r; }	// a length (>= 0.5 px)

static void disc (Canvas &cv, const View &v, P c, double r, unsigned col, int alpha = 255)
{ VPath p; p.circle (VX (v, c.x), VY (v, c.y), VL (v, r)); p.fill (cv, col, alpha); }
static void ring (Canvas &cv, const View &v, P c, double r, double w, unsigned col, int alpha = 255)
{ VPath p; p.arc (VX (v, c.x), VY (v, c.y), VL (v, r), 0, 360, VL (v, w)); p.fill (cv, col, alpha); }
static void quad (Canvas &cv, const View &v, P a, P b, double th, unsigned col, int alpha = 255)	// a band a -> b, th thick
{
	double dx = b.x - a.x, dy = b.y - a.y, l = sqrt (dx * dx + dy * dy); if (l <= 0) return;
	double nx = -dy / l * th / 2, ny = dx / l * th / 2;
	int xy[8] = { VX (v, a.x + nx), VY (v, a.y + ny), VX (v, b.x + nx), VY (v, b.y + ny), VX (v, b.x - nx), VY (v, b.y - ny), VX (v, a.x - nx), VY (v, a.y - ny) };
	VPath p; p.poly (xy, 4); p.fill (cv, col, alpha);
}
static void stroke (Canvas &cv, const View &v, const P *pp, int n, double w, unsigned col, bool closed = false, int alpha = 255)
{
	int xy[2 * 64]; if (n > 64) n = 64;
	for (int i = 0; i < n; i++) { xy[2 * i] = VX (v, pp[i].x); xy[2 * i + 1] = VY (v, pp[i].y); }
	VPath p; p.polyline (xy, n, VL (v, w), closed); p.fill (cv, col, alpha);
}
static void fillpoly (Canvas &cv, const View &v, const P *pp, int n, unsigned col, int alpha = 255)
{
	int xy[2 * 64]; if (n > 64) n = 64;
	for (int i = 0; i < n; i++) { xy[2 * i] = VX (v, pp[i].x); xy[2 * i + 1] = VY (v, pp[i].y); }
	VPath p; p.poly (xy, n); p.fill (cv, col, alpha);
}
// words on the table: centred on (x, y) px, in a face of px; angle 90: read from the bottom up
static void words (Canvas &cv, int x, int y, const char *s, unsigned col, int px, int angle, int style = 2)
{
	UkFaceScope sc (face (px));
	int tw = uk_tw (s, style), th = uk_fh ();
	if (angle != 90) { uk_text (cv, x - tw / 2, y - th / 2, s, col, style); return; }
	Canvas t; if (!t.alloc (tw + 2, th)) return; t.clear (0);
	uk_text (t, 1, 0, s, 0xFFFFFF, style);
	for (int yy = 0; yy < th; yy++) for (int xx = 0; xx < tw + 2; xx++)
	{
		int a = (t.px[yy * t.stride + xx] >> 16) & 255;
		if (a) uk_blend_px (cv, x - th / 2 + yy, y + (tw + 2) / 2 - xx, col, a);
	}
}
static int label_px (int size, double s) { static const int PX[5] = { 0, 11, 15, 22, 30 }; return (int) lround (PX[size < 1 ? 1 : size > 4 ? 4 : size] * s / 0.654); }

// a lamp (an insert): unlit a dim tint of its colour in the playfield, lit the colour with a glow and a highlight
static void lamp (Canvas &cv, const View &v, P c, double r, unsigned col, unsigned bg, bool lit)
{
	if (!lit) { disc (cv, v, c, r, uk_mix (bg, col, 72)); ring (cv, v, c, r - 0.6, 1.2, uk_mix (bg, col, 130), 200); return; }
	disc (cv, v, c, r * 1.9, col, 50); disc (cv, v, c, r * 1.35, col, 90);
	disc (cv, v, c, r, col); P h = { c.x - r * 0.3, c.y - r * 0.3 }; disc (cv, v, h, r * 0.42, 0xFFFFFF, 170);
}
static void pill (Canvas &cv, const View &v, P c, double w, double h, unsigned col, unsigned bg, bool lit, const char *txt)
{
	int x = (int) lround (v.ox + (c.x - w / 2) * v.s), y = (int) lround (v.oy + (c.y - h / 2) * v.s), pw = (int) lround (w * v.s), ph = (int) lround (h * v.s);
	if (lit) { uk_rbox (cv, x - 3, y - 3, pw + 6, ph + 6, ph / 2 + 3, col, col, 70); uk_rbox (cv, x, y, pw, ph, ph / 2, uk_mix (col, 0xFFFFFF, 90), col); }
	else { uk_rbox (cv, x, y, pw, ph, ph / 2, uk_mix (bg, col, 70), uk_mix (bg, col, 60)); uk_rline (cv, x, y, pw, ph, ph / 2, uk_mix (bg, col, 130), 200); }
	if (txt && ph >= 8) words (cv, x + pw / 2, y + ph / 2, txt, lit ? uk_ink_on (col) : uk_mix (bg, col, 160), (int) (ph * 0.78), 0);
}

// the static layer: background, artwork, words, ramps' tracks, walls, posts, rubbers, gates, saucers' holes
static void draw_static (Canvas &cv, const Tbl &t, const View &v, bool small)
{
	int x0 = (int) lround (v.ox), y0 = (int) lround (v.oy), w = (int) lround (t.w * v.s), h = (int) lround (t.h * v.s);
	cv.fillRect (x0, y0, w, h, t.bg);
	for (int i = 0; i < t.nshape; i++) fillpoly (cv, v, t.shape[i].p, t.shape[i].n, t.shape[i].col);
	if (!small) for (int i = 0; i < t.nlbl; i++)
	{
		const Lbl &l = t.lbl[i]; int px = label_px (l.size, v.s);
		if (px >= 7) words (cv, VX (v, l.at.x) / 16, VY (v, l.at.y) / 16, g_fr && l.fr[0] ? l.fr : l.en, l.col, px, l.angle);
	}
	for (int i = 0; i < t.nramp; i++)					// a ramp: a see-through track and its two rails
	{
		const Ramp &r = t.ramp[i]; stroke (cv, v, r.path, r.n, r.width, r.col, false, 60);
		for (int side = -1; side <= 1; side += 2)
		{
			P o[32];
			for (int k = 0; k < r.n; k++)
			{
				P a = r.path[k > 0 ? k - 1 : k], b = r.path[k < r.n - 1 ? k + 1 : k];
				double dx = b.x - a.x, dy = b.y - a.y, l = sqrt (dx * dx + dy * dy); if (l <= 0) l = 1;
				o[k].x = r.path[k].x - dy / l * side * r.width / 2; o[k].y = r.path[k].y + dx / l * side * r.width / 2;
			}
			stroke (cv, v, o, r.n, small ? 2.5 : 1.6, uk_mix (r.col, 0xFFFFFF, 60), false, 230);
		}
	}
	for (int i = 0; i < t.nwall; i++)
	{
		const Poly &p = t.wall[i];
		if (p.closed) { fillpoly (cv, v, p.p, p.n, uk_mix (t.bg, p.col, 90)); stroke (cv, v, p.p, p.n, 2.5, p.col, true); }
		else stroke (cv, v, p.p, p.n, p.width, p.col);
	}
	for (int i = 0; i < t.narc; i++)
	{
		const Arc &a = t.arc[i];
		int a0 = (int) lround (360 - a.to), a1 = a0 + (int) lround (a.to - a.from);
		VPath p; p.arc (VX (v, a.c.x), VY (v, a.c.y), VL (v, a.r), a0, a1, VL (v, a.width)); p.fill (cv, a.col);
	}
	for (int i = 0; i < t.nsling; i++) { P pp[2] = { t.sling[i].a, t.sling[i].b }; stroke (cv, v, pp, 2, 5, 0xF4F4F4); }	// the rubber
	for (int i = 0; i < t.npost; i++) { disc (cv, v, t.post[i].c, t.post[i].r + 2.5, 0xF4F4F4); disc (cv, v, t.post[i].c, t.post[i].r - 1, uk_mix (t.post[i].col, 0x000000, 60)); }
	for (int i = 0; i < t.ngate; i++) { P pp[2] = { t.gate[i].a, t.gate[i].b }; stroke (cv, v, pp, 2, 2, 0xC8CCD4); disc (cv, v, t.gate[i].a, 3, 0xC8CCD4); }
	for (int i = 0; i < t.nsauc; i++)
	{ disc (cv, v, t.sauc[i].c, t.sauc[i].r + 4, uk_mix (t.bg, t.sauc[i].col, 110)); disc (cv, v, t.sauc[i].c, t.sauc[i].r, uk_mix (t.bg, 0x000000, 170)); }
	if (!small)							// the plunger's housing
	{
		P a = { t.plunger.x - 14, 1032 }, b = { t.plunger.x + 14, 1032 };
		quad (cv, v, a, b, 12, uk_mix (t.bg, 0x000000, 120));
	}
}

// ---- the state of a scene (the game's, set by hand here) ------------------------------------------------------------
struct State
{
	P ball[3]; int nball; int onRamp;			// a ball drawn on ramp 0's path at its point onRamp (-1: none)
	bool flipUp[3], dead; double pull;
	char lit[24][25]; int nlit; char down[8][25]; int ndown; char flash[25];
	int mult; bool shootAgain, ballSave;
};
static State g_st;
static bool is_lit (const char *id) { for (int i = 0; i < g_st.nlit; i++) if (!strcmp (g_st.lit[i], id)) return true; return false; }
static bool is_down (const char *id) { for (int i = 0; i < g_st.ndown; i++) if (!strcmp (g_st.down[i], id)) return true; return false; }

static void ball (Canvas &cv, const View &v, P c, double r)
{
	P sh = { c.x + 2.5, c.y + 3.5 }; disc (cv, v, sh, r, 0x000000, 80);
	disc (cv, v, c, r, 0x7C838E); P m = { c.x - r * 0.12, c.y - r * 0.12 }; disc (cv, v, m, r * 0.82, 0xC4CAD2);
	P m2 = { c.x - r * 0.22, c.y - r * 0.22 }; disc (cv, v, m2, r * 0.55, 0xDDE1E6);
	P h = { c.x - r * 0.38, c.y - r * 0.4 }; disc (cv, v, h, r * 0.26, 0xFFFFFF, 235);
}
static void flipper (Canvas &cv, const View &v, const Flip &f, bool up, unsigned rubber, bool dead, unsigned bg)
{
	double deg = up ? f.up : f.rest, th = (f.side ? 180 - deg : deg) * M_PI / 180;
	double dx = cos (th), dy = sin (th), nx = -dy, ny = dx;
	P tip = { f.pivot.x + dx * f.len, f.pivot.y + dy * f.len };
	for (int pass = 0; pass < 2; pass++)
	{
		double k = pass ? 2.6 : 0; P pp[40]; int n = 0;
		for (int i = 0; i <= 16; i++) { double a = M_PI / 2 + M_PI * i / 16; double cx = cos (a), cy = sin (a);	// the pivot's round end
			pp[n].x = f.pivot.x + (dx * cx + nx * cy) * (f.r0 - k); pp[n].y = f.pivot.y + (dy * cx + ny * cy) * (f.r0 - k); n++; }
		for (int i = 0; i <= 16; i++) { double a = -M_PI / 2 + M_PI * i / 16; double cx = cos (a), cy = sin (a);	// the tip's
			pp[n].x = tip.x + (dx * cx + nx * cy) * (f.r1 - k); pp[n].y = tip.y + (dy * cx + ny * cy) * (f.r1 - k); n++; }
		unsigned c = pass ? f.col : rubber;
		if (dead) c = uk_mix (c, bg, 150);
		fillpoly (cv, v, pp, n, c);
	}
	disc (cv, v, f.pivot, 3.2, 0x8A9099);
}

static void draw_dynamic (Canvas &cv, const Tbl &t, const View &v)
{
	unsigned bg = t.bg; bool dead = g_st.dead;
	for (int i = 0; i < t.nlane; i++)					// the lanes' lamps
	{
		const Lane &l = t.lane[i]; P c = { l.x + l.w / 2, l.y + l.h / 2 };
		lamp (cv, v, c, (l.w < l.h ? l.w : l.h) * 0.3, l.col, bg, !dead && is_lit (l.id));
		P a = { l.x + 3, l.y + 4 }, b = { l.x + l.w - 3, l.y + 4 }; P ab[2] = { a, b }; stroke (cv, v, ab, 2, 1.4, 0xB8BEC8, false, 200);	// the rollover's wire
	}
	for (int i = 0; i < t.ntgt; i++)					// drop / standup targets
	{
		const Seg &s = t.tgt[i];
		double dx = s.b.x - s.a.x, dy = s.b.y - s.a.y, l = sqrt (dx * dx + dy * dy); double nx = -dy / l, ny = dx / l;
		P m = { (s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2 };
		if ((m.x - t.w / 2) * nx + (m.y - t.h / 2) * ny < 0) { nx = -nx; ny = -ny; }	// n: away from the table's centre
		if (s.drop)
		{
			if (is_down (s.id)) { quad (cv, v, s.a, s.b, 3, uk_mix (bg, 0x000000, 150)); continue; }
			P a = { s.a.x + nx * 4, s.a.y + ny * 4 }, b = { s.b.x + nx * 4, s.b.y + ny * 4 };
			quad (cv, v, a, b, 9, dead ? uk_mix (s.col, bg, 140) : s.col);
			P a2 = { s.a.x + dy / l * 3, s.a.y - dx / l * 3 }; (void) a2;
			quad (cv, v, s.a, s.b, 2, 0xFFFFFF, 150);
		}
		else
		{
			P lc = { m.x + nx * 16, m.y + ny * 16 };
			lamp (cv, v, lc, 6, s.col, bg, !dead && is_lit (s.id));
			P a = { s.a.x + nx * 3, s.a.y + ny * 3 }, b = { s.b.x + nx * 3, s.b.y + ny * 3 };
			quad (cv, v, a, b, 7, 0xF0F0F0); quad (cv, v, s.a, s.b, 2.5, s.col);
		}
	}
	for (int i = 0; i < t.nbump; i++)					// pop bumpers: a skirt, a cap, a lit top
	{
		const Circ &b = t.bump[i]; bool fl = !dead && !strcmp (g_st.flash, b.id);
		if (fl) { disc (cv, v, b.c, b.r * 1.45, b.col, 70); disc (cv, v, b.c, b.r * 1.2, b.col, 110); }
		disc (cv, v, b.c, b.r, uk_mix (b.col, 0x000000, 140));
		disc (cv, v, b.c, b.r * 0.84, fl ? uk_mix (b.col, 0xFFFFFF, 120) : dead ? uk_mix (b.col, bg, 120) : b.col);
		ring (cv, v, b.c, b.r * 0.84, 1.2, uk_mix (b.col, 0x000000, 90), 160);
		disc (cv, v, b.c, b.r * 0.5, fl ? 0xFFFFFF : uk_mix (b.col, 0xFFFFFF, 150));
		P h = { b.c.x - b.r * 0.18, b.c.y - b.r * 0.2 }; disc (cv, v, h, b.r * 0.16, 0xFFFFFF, 200);
	}
	for (int i = 0; i < t.nsauc; i++)					// a saucer's lamp: its ring lit
	{
		const Circ &s = t.sauc[i];
		if (!dead && is_lit (s.id)) { ring (cv, v, s.c, s.r + 6, 5, s.col, 90); ring (cv, v, s.c, s.r + 2, 3, s.col); }
		else ring (cv, v, s.c, s.r + 2, 2, uk_mix (bg, s.col, 120), 220);
	}
	for (int i = 0; i < t.nramp; i++)					// a ramp's arrow, before its entry
	{
		const Ramp &r = t.ramp[i]; P m = { (r.a.x + r.b.x) / 2 - r.pass.x * 34, (r.a.y + r.b.y) / 2 - r.pass.y * 34 };
		double px = r.pass.x, py = r.pass.y, qx = -py, qy = px;
		P tri[3] = { { m.x + px * 11, m.y + py * 11 }, { m.x - px * 8 + qx * 9, m.y - py * 8 + qy * 9 }, { m.x - px * 8 - qx * 9, m.y - py * 8 - qy * 9 } };
		bool lit = !dead && is_lit (r.id);
		if (lit) { disc (cv, v, m, 15, r.col, 60); fillpoly (cv, v, tri, 3, r.col); }
		else fillpoly (cv, v, tri, 3, uk_mix (bg, r.col, 80));
	}
	// the game's own inserts: the bonus multiplier (2x ... 5x) above the flippers, "Shoot again" below them
	double fx = (t.flip[0].pivot.x + t.flip[1].pivot.x) / 2, fy = t.flip[0].pivot.y;
	for (int i = 0; i < 4; i++)
	{
		char s[8]; snprintf (s, sizeof s, "%d\xC3\x97", i + 2); P c = { fx + (i - 1.5) * 38, fy - 115 };
		pill (cv, v, c, 32, 18, t.lampCol, bg, !dead && g_st.mult >= i + 2, s);
	}
	P sa = { fx, fy + 74 }; pill (cv, v, sa, 112, 17, 0xF04848, bg, g_st.shootAgain || g_st.ballSave, TR ("SHOOT AGAIN"));
	for (int i = 0; i < t.nflip; i++) flipper (cv, v, t.flip[i], g_st.flipUp[i], t.slingCol, dead, bg);
	{									// the plunger: a knob on a spring
		double y = t.plunger.y + 15 + g_st.pull * 36; P a = { t.plunger.x - 12, y }, b = { t.plunger.x + 12, y };
		P z[12]; int n = 0; for (double yy = y + 6; yy < 1030 && n < 12; yy += (1030 - y - 6) / 9, n++) { z[n].x = t.plunger.x + (n & 1 ? 7 : -7); z[n].y = yy; }
		stroke (cv, v, z, n, 1.8, 0x9AA0A8);
		quad (cv, v, a, b, 8, 0xC8CCD4); quad (cv, v, a, b, 2, 0xFFFFFF, 120);
	}
	for (int i = 0; i < g_st.nball; i++) ball (cv, v, g_st.ball[i], 13);
	if (g_st.onRamp >= 0 && t.nramp) ball (cv, v, t.ramp[0].path[g_st.onRamp], 14);
}

// ---- the window's widgets ------------------------------------------------------------------------------------------
static Tbl *g_tbl[6]; static int g_ntbl; static int g_sel;
static const char *const FILES[] = { "SD:/apps/pinball.app/tables/1-space-station.table", "SD:/apps/pinball.app/tables/2-haunted-manor.table",
				     "SD:/apps/pinball.app/tables/3-volcano.table", "SD:/docs/pinball/my-first-table.table", "SD:/docs/pinball/moon-base.table" };
enum { PANELW = 260 };

// The playfield: a GameView in the app (games/game.h); here a plain Widget. The table scaled to fit, letterboxed.
class Field : public Widget
{
public:
	Tbl *t; int overlay;				// 0 none, 1 paused, 2 game over + name, 3 the top 5, 4 the bonus count
	Field (int l, int tp, int w, int h) : Widget (l, tp, w, h), t (0), overlay (0) {}
	View view () const { double s = fmin (width / t->w, height / t->h); View v = { s, (width - t->w * s) / 2, (height - t->h * s) / 2 }; return v; }
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
		canvas.clear (uk_tone (t->bg, 70));			// the letterbox's bars: the table's background, darker
		View v = view ();
		draw_static (canvas, *t, v, false);
		draw_dynamic (canvas, *t, v);
		char b[64], n[32];
		if (overlay) { VPath p; p.rect (0, 0, V (width), V (height)); p.fill (canvas, 0x000000, overlay == 4 ? 60 : 130); }
		if (overlay == 1)
		{
			int x, y; card (240, 150, TR ("Paused"), x, y);
			uk_text_c (canvas, x, y + 92, 240, 28, TR ("P or Start: resume"), C_DIS);
		}
		if (overlay == 2)
		{
			int x, y; card (300, 196, TR ("Game over"), x, y);
			fmt (1312450, n, sizeof n);
			{ UkFaceScope sc (face (24)); uk_text_c (canvas, x, y + 6, 300, 34, n, C_TEXT, 2); }
			snprintf (b, sizeof b, TR ("New high score: %s place!"), TR ("2nd"));
			uk_text_c (canvas, x, y + 40, 300, 20, b, uk_tone (C_ACCENT, 90), 2);
			uk_text_l (canvas, x + 20, y + 66, 20, TR ("Your name:"), C_TEXT);
		}
		if (overlay == 3)
		{
			int x, y; card (300, 236, TR ("Best scores"), x, y);
			{ UkFaceScope sc (face (12)); uk_text_c (canvas, x, y + 4, 300, 18, t->nm (), C_DIS, 2); }
			static const long SCO[5] = { 1543200, 1312450, 1250340, 830120, 412000 };
			static const char *const NAM[5] = { "Léa", "Steph", "Steph", "Léa", "Steph" };
			for (int i = 0; i < 5; i++)
			{
				int ry = y + 28 + i * 26;
				if (i == 1) uk_hilite (canvas, x + 12, ry, 276, 24, 5, true);
				unsigned ink = i == 1 ? uk_hilite_ink (true) : C_TEXT;
				snprintf (b, sizeof b, "%d", i + 1); uk_text_l (canvas, x + 22, ry, 24, b, i == 1 ? ink : C_DIS, 2);
				uk_text_l (canvas, x + 44, ry, 24, NAM[i], ink);
				fmt (SCO[i], n, sizeof n); uk_text_l (canvas, x + 278 - uk_text_w (n, 2), ry, 24, n, ink, 2);
			}
			uk_text_c (canvas, x, y + 164, 300, 22, TR ("Enter or A: back to the tables"), C_DIS);
		}
		if (overlay == 4)
		{
			int w = 280, h = 92, x = (width - w) / 2, y = height - h - 150;
			uk_rbox (canvas, x, y, w, h, 10, uk_tone (C_FACE, 170), uk_tone (C_FACE, 126), 235);
			uk_rline (canvas, x, y, w, h, 10, uk_tone (C_FACE, 70), 220);
			uk_text_c (canvas, x, y + 8, w, 20, TR ("Bonus"), C_DIS, 2);
			char a[32], c[32]; fmt (4200, a, sizeof a); fmt (12600, c, sizeof c);
			snprintf (b, sizeof b, "%s \xC3\x97 3  =  %s", a, c);
			UkFaceScope sc (face (20)); uk_text_c (canvas, x, y + 32, w, 30, b, C_TEXT, 2);
			UkFaceScope sc2 (face (11)); uk_text_c (canvas, x, y + 64, w, 20, TR ("any key: skip"), C_DIS);
		}
	}
};

// The panel's rows (caption at the left, value in bold at the right) -- the score panel under the LCDs
class Stats : public Widget
{
public:
	const char *cap[4]; char val[4][40]; int n; int tilt;
	Stats (int l, int t, int w, int h) : Widget (l, t, w, h), n (0), tilt (0) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, uk_tone (C_BG, 140));
		int rh = (height - 8) / n;
		for (int i = 0; i < n; i++)
		{
			int y = 4 + i * rh;
			if (i) uk_etch_h (canvas, 10, y, width - 20, uk_tone (C_BG, 140));
			uk_text_l (canvas, 12, y, rh, cap[i], C_DIS);
			if (cap[i] == TR ("Tilt"))
				for (int k = 0; k < 3; k++)
				{ VPath p; p.circle (V (width - 18 - k * 16), V (y + rh / 2), V (5)); p.fill (canvas, 2 - k < tilt ? 0xE0453A : uk_tone (C_BG, 110)); }
			else uk_text_l (canvas, width - 12 - uk_text_w (val[i], 2), y, rh, val[i], C_TEXT, 2);
		}
	}
};

// The table's name and goal (a heading, the goal wrapped under it)
class Heading : public Widget
{
public:
	const char *title, *text; int titlePx; bool swatch; const Tbl *t;
	Heading (int l, int tp, int w, int h) : Widget (l, tp, w, h), title (""), text (""), titlePx (16), swatch (false), t (0) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int x = 0;
		if (swatch && t)
		{
			uk_rbox (canvas, 0, 3, 8, titlePx + 4, 3, t->lampCol, t->slingCol);
			x = 14;
		}
		{ UkFaceScope sc (face (titlePx)); uk_text (canvas, x, 0, title, C_TEXT, 2); }
		int st[8], ln[8]; int n = uk_text_wrap (text, (int) strlen (text), width - 2, 6, st, ln);
		for (int i = 0; i < n; i++) { char b[200]; snprintf (b, sizeof b, "%.*s", ln[i], text + st[i]); uk_text (canvas, 0, titlePx + 10 + i * uk_fh (), b, C_DIS); }
	}
};

// The table's rules as goals: a row per [rule] with a message -- its count as dots (filled: counted this game), the message;
// a "once" rule done: a check, greyed
class RuleList : public Widget
{
public:
	const Tbl *t; int done[16]; bool doneOnce[16];
	RuleList (int l, int tp, int w, int h) : Widget (l, tp, w, h), t (0) { memset (done, 0, sizeof done); memset (doneOnce, 0, sizeof doneOnce); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int y = 0;
		for (int i = 0; i < t->nrule && y + 20 <= height; i++)
		{
			const Tbl::Rule &r = t->rule[i]; const char *m = g_fr && r.msg[1][0] ? r.msg[1] : r.msg[0];
			if (!m[0]) continue;
			int x = 2;
			if (doneOnce[i]) { uk_glyph (canvas, WKG_CHECK, x + 6, y + 10, 12, 0x3A9A4A); x += 18; }
			else if (r.count <= 6)
				for (int k = 0; k < r.count; k++) { VPath p; p.circle (V (x + 5), V (y + 10), V (4)); p.fill (canvas, k < done[i] ? t->lampCol == 0xFFD34D ? 0xE0A800 : t->lampCol : uk_tone (C_BG, 105)); x += 11; }
			else { char b[16]; snprintf (b, sizeof b, "%d/%d", done[i], r.count); uk_text_l (canvas, x, y, 20, b, C_TEXT, 2); x += uk_text_w (b, 2); }
			char fit[80]; uk_text_fit (m, width - x - 8, fit, sizeof fit);
			uk_text_l (canvas, x + 6, y, 20, fit, doneOnce[i] ? C_DIS : C_TEXT);
			y += 21;
		}
	}
};

static int wrapped (const char *s, int w) { int st[8], ln[8]; return uk_text_wrap (s, (int) strlen (s), w, 8, st, ln); }

// The keys' legend: a keycap (or a pad's button) then what it does; vertical (the panel) or in a row (the picker)
class Legend : public Widget
{
public:
	const char *key[8], *act[8]; int n; bool row;
	Legend (int l, int t, int w, int h, bool row_) : Widget (l, t, w, h), n (0), row (row_) {}
	static int cap (Canvas &cv, int x, int y, const char *k)
	{
		UkFaceScope sc (face (11));
		int w = uk_tw (k, 2) + 12; if (w < 22) w = 22;
		uk_raised (cv, x, y, w, 20, 4, uk_tone (C_BG, 150));
		uk_text_c (cv, x, y, w, 19, k, C_TEXT, 2);
		return w;
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int x = 0, y = 0;
		for (int i = 0; i < n; i++)
		{
			int cx = x, w = 0;
			for (const char *k = key[i]; *k; )				// "Z|←": several caps
			{
				char one[16]; int j = 0; while (k[j] && k[j] != '|' && j < 15) { one[j] = k[j]; j++; } one[j] = 0;
				w = cap (canvas, cx, y, one); cx += w + 3; k += j; if (*k == '|') k++;
			}
			if (row) { uk_text_l (canvas, cx + 3, y, 20, act[i], C_DIS); x = cx + 3 + uk_text_w (act[i]) + 18; }
			else { uk_text_l (canvas, 74, y, 20, act[i], C_TEXT); y += 25; }
		}
	}
};

// The picker's list: shipped tables, then the player's (SD:/docs/pinball) -- two lines a row; a broken one greyed, its error
class TableList : public Widget
{
public:
	TableList (int l, int t, int w, int h) : Widget (l, t, w, h) { canFocus = true; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD, true);
		int y = 6;
		for (int i = 0; i < g_ntbl; i++)
		{
			if (i == 3)						// the player's tables: a caption, an etched line
			{
				y += 4; uk_etch_h (canvas, 10, y, width - 20, C_FIELD); y += 4;
				UkFaceScope sc (face (11)); uk_text_l (canvas, 12, y, 18, TR ("Your tables (SD:/docs/pinball)"), C_DIS, 2); y += 20;
			}
			Tbl &t = *g_tbl[i]; int rh = 52;
			bool sel = i == g_sel;
			if (sel) uk_hilite (canvas, 4, y, width - 8, rh, 6, true);
			unsigned ink = sel ? uk_hilite_ink (true) : C_FIELD_TEXT, dim = sel ? uk_mix (uk_hilite_ink (true), C_ACCENT, 70) : C_DIS;
			// a small picture of the table (its static layer, 22 x 44)
			int tx = 12, ty = y + 4;
			if (t.ok) { View v = { 44.0 / t.h, (double) tx, (double) ty }; draw_static (canvas, t, v, true); uk_rline (canvas, tx - 1, ty - 1, (int) (t.w * v.s) + 2, 46, 2, uk_tone (C_FIELD, 80), 120); }
			else
			{
				uk_rbox (canvas, tx, ty, 22, 44, 3, uk_tone (C_FIELD, 118), uk_tone (C_FIELD, 108));
				VPath p; int tri[6] = { V (tx + 11), V (ty + 13), V (tx + 20), V (ty + 30), V (tx + 2), V (ty + 30) }; p.poly (tri, 3); p.fill (canvas, 0xE0A020);
				uk_text_c (canvas, tx, ty + 15, 22, 16, "!", 0x000000, 2);
			}
			int x = 46;
			uk_text_l (canvas, x, y + 4, 22, t.ok ? t.nm () : t.file, t.ok ? ink : sel ? uk_mix (C_ACCENT, ink, 190) : uk_mix (C_FIELD, C_FIELD_TEXT, 140), 2);
			char b[96], n[32];
			if (!t.ok) uk_text_l (canvas, x, y + 26, 20, t.err, sel ? ink : 0xC0302A);
			else
			{
				static const long BEST[5] = { 1543200, 412000, 0, 95210, 0 };
				if (BEST[i]) { fmt (BEST[i], n, sizeof n); snprintf (b, sizeof b, "%s  %s", TR ("Best"), n); }
				else snprintf (b, sizeof b, "%s", TR ("No score yet"));
				uk_text_l (canvas, x, y + 26, 20, b, dim);
			}
			y += rh + 2;
		}
	}
};

// The picker's preview: the chosen table drawn small (the play layer, lamps off) -- or, broken, why it cannot be played
class Thumb : public Widget
{
public:
	Thumb (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		Tbl &t = *g_tbl[g_sel];
		if (t.ok)
		{
			double s = fmin ((width - 4) / t.w, (height - 4) / t.h); View v = { s, (width - t.w * s) / 2, (height - t.h * s) / 2 };
			uk_rbox (canvas, (int) v.ox - 3, (int) v.oy - 3, (int) (t.w * s) + 6, (int) (t.h * s) + 6, 6, uk_tone (C_BG, 90), uk_tone (C_BG, 70));
			draw_static (canvas, t, v, false); State keep = g_st; memset (&g_st, 0, sizeof g_st); g_st.onRamp = -1;
			draw_dynamic (canvas, t, v); g_st = keep;
			return;
		}
		uk_sunken (canvas, 0, 0, width, height, 8, uk_tone (C_BG, 120));
		VPath p; int cx = width / 2, ty = 70; int tri[6] = { V (cx), V (ty), V (cx + 30), V (ty + 52), V (cx - 30), V (ty + 52) }; p.poly (tri, 3); p.fill (canvas, 0xE0A020);
		{ UkFaceScope sc (face (30)); uk_text_c (canvas, cx - 20, ty + 14, 40, 40, "!", 0x000000, 2); }
		const char *lines[3] = { TR ("This table cannot be played."), t.err, TR ("Correct the file in a text editor (Tinypad): the list is read again when you come back to it.") };
		int y = ty + 72;
		for (int k = 0; k < 3; k++)
		{
			int st[6], ln[6]; int n = uk_text_wrap (lines[k], (int) strlen (lines[k]), width - 28, 5, st, ln, 0, k == 0 ? 2 : 0);
			for (int i = 0; i < n; i++) { char b[200]; snprintf (b, sizeof b, "%.*s", ln[i], lines[k] + st[i]);
				uk_text (canvas, 14, y, b, k == 1 ? 0xC0302A : k == 0 ? C_TEXT : C_DIS, k == 0 ? 2 : 0); y += uk_fh (); }
			y += 8;
		}
	}
};

// The picker's top 5
class ScoreList : public Widget
{
public:
	ScoreList (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_text_l (canvas, 0, 0, 20, TR ("Best scores"), C_TEXT, 2);
		uk_etch_h (canvas, 0, 22, width, C_BG);
		static const long SCO[5] = { 1543200, 1250340, 830120, 412000, 0 };
		static const char *const NAM[5] = { "Léa", "Steph", "Léa", "Steph", "" };
		Tbl &t = *g_tbl[g_sel];
		for (int i = 0; i < 5; i++)
		{
			int y = 26 + i * 21; char b[8], n[32];
			snprintf (b, sizeof b, "%d", i + 1); uk_text_l (canvas, 2, y, 20, b, C_DIS, 2);
			bool has = t.ok && g_sel == 0 && SCO[i];
			if (g_sel == 3 && i == 0) { uk_text_l (canvas, 22, y, 20, "Léa", C_TEXT); fmt (95210, n, sizeof n); uk_text_l (canvas, width - 2 - uk_text_w (n, 2), y, 20, n, C_TEXT, 2); continue; }
			if (!has) { uk_text_l (canvas, 22, y, 20, "\xE2\x80\x94", uk_mix (C_BG, C_DIS, 150)); continue; }
			uk_text_l (canvas, 22, y, 20, NAM[i], C_TEXT);
			fmt (SCO[i], n, sizeof n); uk_text_l (canvas, width - 2 - uk_text_w (n, 2), y, 20, n, C_TEXT, 2);
		}
	}
};

// ---- the scenes ------------------------------------------------------------------------------------------------------
static void lit (const char *ids) { char b[256]; cpy (b, ids, sizeof b); for (char *s = strtok (b, ","); s; s = strtok (0, ",")) cpy (g_st.lit[g_st.nlit++], s, 25); }
static void down (const char *ids) { char b[256]; cpy (b, ids, sizeof b); for (char *s = strtok (b, ","); s; s = strtok (0, ",")) cpy (g_st.down[g_st.ndown++], s, 25); }

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();
	g_fr = !strcmp (uk_lang (), "fr");
	for (int i = 0; i < 5; i++) { g_tbl[i] = new Tbl; load (FILES[i], *g_tbl[i]); }
	g_ntbl = 5;
	g_sel = getenv ("MOCK_SEL") ? atoi (getenv ("MOCK_SEL")) : 0;
	int W = getenv ("MOCK_W") ? atoi (getenv ("MOCK_W")) : 600, H = getenv ("MOCK_H") ? atoi (getenv ("MOCK_H")) : 680;
	memset (&g_st, 0, sizeof g_st); g_st.onRamp = -1; g_st.mult = 1;

	if (scene ("trio"))								// the three tables side by side (the style sheet)
	{
		Root root (W, H, "Pinball -- the three tables");
		class Trio : public Widget { public: Trio (int w, int h) : Widget (0, 0, w, h) {}
			void onDraw () override { canvas.clear (uk_tone (C_BG, 60)); for (int i = 0; i < 3; i++) { Tbl &t = *g_tbl[i]; double s = (height - 16) / t.h;
				View v = { s, 8 + i * (t.w * s + 8), 8 }; draw_static (canvas, t, v, false); draw_dynamic (canvas, t, v); } } };
		g_st.mult = 3; lit ("l1,l3,st1,dock,crypt,orbit,lava,s2,g1,v2"); down ("t2,g3,v1,v4"); cpy (g_st.flash, "b2", 25);
		g_st.flipUp[0] = true; g_st.ball[0] = (P) { 240, 600 }; g_st.nball = 1; g_st.shootAgain = true;
		root.addChild (new Trio (W, H));
		root.run (); return 0;
	}

	Root root (W, H, "Pinball");
	if (root.canvas.px == 0) return 1;
	bool picker = scene ("picker");
	if (picker)
	{
		int L = 300;
		Heading *hd = new Heading (16, 12, L - 16, 30); hd->title = TR ("Choose a table"); hd->titlePx = 18; root.addChild (hd);
		TableList *tl = new TableList (12, 46, L - 12, 572); root.addChild (tl); tl->setFocus ();
		Thumb *th = new Thumb (L + 14, 12, W - L - 26, g_tbl[g_sel]->ok ? 330 : 576); root.addChild (th);
		Tbl &t = *g_tbl[g_sel];
		int glH = 26 + 16 * wrapped (t.ok ? t.gl () : "", W - L - 32);
		Heading *gl = new Heading (L + 16, 350, W - L - 30, glH); gl->title = t.ok ? t.nm () : t.file; gl->text = t.ok ? t.gl () : ""; gl->titlePx = 15; gl->swatch = t.ok; gl->t = &t; root.addChild (gl);
		ScoreList *sl = new ScoreList (L + 16, 350 + glH + 6, W - L - 30, 136); root.addChild (sl);
		if (!t.ok) { ((Widget *) sl)->hidden = true; ((Widget *) gl)->hidden = true; }
		ToolButton *play = (new ToolButton (W - L - 30, 36, TR ("Play this table (Enter)")))->setGlyph (WKT_PLAY)->setText (TR ("Play"));
		play->filled = true; play->raised = true; play->setOn (t.ok); play->setDisabled (!t.ok); play->left = L + 16; play->top = 600; root.addChild (play);
		Legend *lg = new Legend (14, H - 32, W - 28, 22, true);
		lg->key[0] = "\xE2\x86\x91|\xE2\x86\x93"; lg->act[0] = TR ("choose");
		lg->key[1] = TR ("Enter"); lg->act[1] = TR ("play");
		lg->key[2] = TR ("Esc"); lg->act[2] = TR ("quit"); lg->n = 3; root.addChild (lg);
		root.run (); return 0;
	}

	// the play screen: the playfield at the left, the panel at the right
	Tbl &t = *g_tbl[g_sel];
	Field *f = new Field (0, 0, W - PANELW, H); f->t = &t; root.addChild (f);
	int px = W - PANELW + 12, pw = PANELW - 24;
	Heading *hd = new Heading (px, 10, pw, 26); hd->title = t.nm (); hd->titlePx = 16; hd->swatch = true; hd->t = &t; root.addChild (hd);
	LcdDisplay *score = new LcdDisplay (px, 40, pw, 58, "0", TR ("BALL")); score->face = face (26); score->smallFace = face (11); root.addChild (score);
	LcdDisplay *msg = new LcdDisplay (px, 104, pw, 34, ""); msg->face = face (16); msg->centred = true; root.addChild (msg);
	Stats *st = new Stats (px, 146, pw, 132); root.addChild (st);
	int goalH = 26 + 16 * wrapped (t.gl (), pw - 2);
	Heading *goal = new Heading (px, 290, pw, goalH); goal->title = TR ("Goal"); goal->titlePx = 13; goal->text = t.gl (); root.addChild (goal);
	RuleList *rl = new RuleList (px, 290 + goalH + 4, pw, H - 6 - 5 * 25 - 8 - (290 + goalH + 4)); rl->t = &t; root.addChild (rl);
	rl->done[0] = 0; rl->done[1] = 0; rl->done[2] = 2; rl->done[3] = 1;
	Legend *lg = new Legend (px, H - 6 - 5 * 25, pw, 5 * 25, false);
	lg->key[0] = "Z|\xE2\x86\x90"; lg->act[0] = TR ("Left flipper");
	lg->key[1] = "M|\xE2\x86\x92"; lg->act[1] = TR ("Right flipper");
	lg->key[2] = TR ("Space"); lg->act[2] = TR ("Plunger (hold)");
	lg->key[3] = "N|\xE2\x86\x91"; lg->act[3] = TR ("Nudge");
	lg->key[4] = "P"; lg->act[4] = TR ("Pause"); lg->n = 5;
	if (getenv ("MOCK_PAD"))
	{ lg->key[0] = "L"; lg->key[1] = "R"; lg->key[2] = "A"; lg->key[3] = "Y"; lg->key[4] = TR ("Start"); }
	root.addChild (lg);

	long sc = 1250340, bonus = 4200, best = 1543200; int ballN = 2; const char *m = "";
	g_st.mult = 3; lit ("l1,l3,st1,orbit"); down ("t2"); cpy (g_st.flash, "b2", 25);
	g_st.ball[0] = (P) { 196, 868 }; g_st.nball = 1; g_st.flipUp[0] = true; m = TR ("Bonus x up!");
	if (scene ("multiball"))
	{
		rl->done[3] = 0; rl->done[2] = 3; rl->doneOnce[2] = true;
		g_st.nball = 1; g_st.ball[0] = (P) { 498, 520 }; g_st.onRamp = 9; g_st.flipUp[0] = false;
		lit ("dock,l2"); g_st.ballSave = true; m = g_fr ? "MULTIBALL !" : "MULTIBALL!"; sc = 1287900; cpy (g_st.flash, "b1", 25);
	}
	if (scene ("tilt")) { g_st.dead = true; g_st.flipUp[0] = false; g_st.ball[0] = (P) { 300, 760 }; m = "TILT"; msg->ink = 0xE0453A; st->tilt = 3; }
	if (scene ("launch")) { g_st.nball = 1; g_st.pull = 0.7; g_st.ball[0] = (P) { t.plunger.x, t.plunger.y + 15 + 0.7 * 36 - 4 - 13 }; g_st.flipUp[0] = false; m = TR ("Ball 2: launch it!"); sc = 512300; bonus = 0; g_st.mult = 1; g_st.nlit = 0; g_st.ndown = 0; g_st.flash[0] = 0; g_st.ballSave = true; }
	if (scene ("pause")) f->overlay = 1;
	if (scene ("name") || scene ("scores")) { f->overlay = scene ("name") ? 2 : 3; ballN = 3; sc = 1312450; g_st.nball = 0; g_st.flipUp[0] = false; m = TR ("Game over"); bonus = 0; }
	if (scene ("bonus")) { f->overlay = 4; g_st.nball = 0; g_st.flipUp[0] = false; m = TR ("Ball lost"); }
	if (scene ("broken")) { }
	char n[32], b[32]; fmt (sc, n, sizeof n); score->setText (n); snprintf (b, sizeof b, "%d / 3", ballN); score->setSub (b);
	msg->setText (m);
	st->n = 4; st->cap[0] = TR ("Bonus"); fmt (bonus, st->val[0], 40); st->cap[1] = TR ("Multiplier"); snprintf (st->val[1], 40, "\xC3\x97%d", g_st.mult);
	st->cap[2] = TR ("Best"); fmt (best, st->val[2], 40); st->cap[3] = TR ("Tilt"); if (!st->tilt) st->tilt = scene ("play") ? 1 : 0;
	if (f->overlay == 1)
	{
		int cw = 240, cx = (W - PANELW - cw) / 2, cy = (H - 150) / 2 + uk_fh () + 10;
		Button *r = new Button (cx + 24, cy + 10, cw - 48, 30, TR ("Resume")); root.addChild (r); r->setFocus ();
		Button *q = new Button (cx + 24, cy + 48, cw - 48, 30, TR ("Back to the tables")); root.addChild (q);
	}
	if (f->overlay == 2)
	{
		int cw = 300, cx = (W - PANELW - cw) / 2, cy = (H - 196) / 2 + uk_fh () + 10;
		Textbox *tb = new Textbox (cx + 20, cy + 90, cw - 40, 26, "Steph"); tb->maxLen = 16; root.addChild (tb); tb->setFocus ();
		Button *ok = new Button (cx + cw - 20 - 90, cy + 128, 90, 28, TR ("OK")); root.addChild (ok);
	}
	root.run ();
	return 0;
}
