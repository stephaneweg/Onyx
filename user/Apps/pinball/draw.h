//
// draw.h -- Pinball's drawing of a table (the window's, included by main.cpp only): a table in table units drawn
// at a scale into a Canvas with UIKit's VPath (anti-aliased, 1/16 px) -- the static layer (the background, the
// artwork, the words, the ramps' tracks, the walls, the posts, the rubbers, the gates, the saucers' holes), drawn once
// per table and scale and kept, and the dynamic layer drawn over it each frame from the game (the lamps, the targets,
// the bumpers, the flippers at their angle, the plunger, the balls). 04-ux-design.md §5 is the style sheet.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _pinball_draw_h
#define _pinball_draw_h

#include <math.h>

static int g_lang = 0;				// 0 English, 1 French (the system's, through uk_lang)

// ---- the faces: DejaVu Sans at any size (made on first use) -------------------------------------------------------
static FtTextFace *g_face[64];
static TextFace *face (int px)
{
	if (px < 6) px = 6;
	if (px > 63) px = 63;
	if (!g_face[px]) { FtTextFace *f = new FtTextFace; if (!f->open ("DejaVu Sans", px)) { delete f; return 0; } g_face[px] = f; }
	return g_face[px];
}

// ---- numbers grouped by thousands: TRC ("digits", ",") -- a narrow no-break space in French ---------------------------
static void fmt (long v, char *o, int cap)
{
	char d[24]; snprintf (d, sizeof d, "%ld", v);
	const char *sep = TRC ("digits", ",");
	int n = (int) strlen (d), k = 0;
	for (int i = 0; i < n && k < cap - 8; i++)
	{
		o[k++] = d[i];
		int left = n - 1 - i;
		if (left > 0 && left % 3 == 0 && d[i] != '-') for (const char *s = sep; *s; s++) o[k++] = *s;
	}
	o[k] = 0;
}

// ---- the scale: table units -> px (ox + u * s), VPath's 1/16 px ----------------------------------------------------
struct View { double s, ox, oy; };
static View fit_view (const Table &t, int w, int h, int margin = 0)
{
	double s = fmin ((w - 2 * margin) / t.w, (h - 2 * margin) / t.h);
	View v = { s, (w - t.w * s) / 2, (h - t.h * s) / 2 };
	return v;
}
static int VX (const View &v, double u) { return (int) lround ((v.ox + u * v.s) * 16); }
static int VY (const View &v, double u) { return (int) lround ((v.oy + u * v.s) * 16); }
static int VL (const View &v, double u) { int r = (int) lround (u * v.s * 16); return r < 8 ? 8 : r; }	// a length (>= 0.5 px)

static void disc (Canvas &cv, const View &v, Vec c, double r, unsigned col, int alpha = 255)
{ VPath p; p.circle (VX (v, c.x), VY (v, c.y), VL (v, r)); p.fill (cv, col, alpha); }
static void ring (Canvas &cv, const View &v, Vec c, double r, double w, unsigned col, int alpha = 255)
{ VPath p; p.arc (VX (v, c.x), VY (v, c.y), VL (v, r), 0, 360, VL (v, w)); p.fill (cv, col, alpha); }
static void quad (Canvas &cv, const View &v, Vec a, Vec b, double th, unsigned col, int alpha = 255)	// a band a -> b, th thick
{
	double dx = b.x - a.x, dy = b.y - a.y, l = sqrt (dx * dx + dy * dy); if (l <= 0) return;
	double nx = -dy / l * th / 2, ny = dx / l * th / 2;
	int xy[8] = { VX (v, a.x + nx), VY (v, a.y + ny), VX (v, b.x + nx), VY (v, b.y + ny), VX (v, b.x - nx), VY (v, b.y - ny), VX (v, a.x - nx), VY (v, a.y - ny) };
	VPath p; p.poly (xy, 4); p.fill (cv, col, alpha);
}
enum { MAXDRAW = 128 };					// the points of one polyline drawn at a time
static void stroke (Canvas &cv, const View &v, const Vec *pp, int n, double w, unsigned col, bool closed = false, int alpha = 255)
{
	static int xy[2 * MAXDRAW]; if (n > MAXDRAW) n = MAXDRAW; if (n < 2) return;
	for (int i = 0; i < n; i++) { xy[2 * i] = VX (v, pp[i].x); xy[2 * i + 1] = VY (v, pp[i].y); }
	VPath p; p.polyline (xy, n, VL (v, w), closed); p.fill (cv, col, alpha);
}
static void fillpoly (Canvas &cv, const View &v, const Vec *pp, int n, unsigned col, int alpha = 255)
{
	static int xy[2 * MAXDRAW]; if (n > MAXDRAW) n = MAXDRAW; if (n < 3) return;
	for (int i = 0; i < n; i++) { xy[2 * i] = VX (v, pp[i].x); xy[2 * i + 1] = VY (v, pp[i].y); }
	VPath p; p.poly (xy, n); p.fill (cv, col, alpha);
}

// Words on the table: centred on (x, y) px, in a face of px; angle 90: read from the bottom up (drawn into a scratch
// canvas, its coverage blended turned a quarter)
static void words (Canvas &cv, int x, int y, const char *s, unsigned col, int px, int angle, int style = 2)
{
	TextFace *f = face (px); if (!f) return;
	UkFaceScope sc (f);
	int tw = uk_tw (s, style), th = uk_fh ();
	if (angle != 90) { uk_text (cv, x - tw / 2, y - th / 2, s, col, style); return; }
	Canvas t; if (!t.alloc (tw + 2, th)) return;
	t.clear (0);
	uk_text (t, 1, 0, s, 0xFFFFFF, style);
	for (int yy = 0; yy < th; yy++) for (int xx = 0; xx < tw + 2; xx++)
	{
		int a = (t.px[yy * t.stride + xx] >> 16) & 255;
		if (a) uk_blend_px (cv, x - th / 2 + yy, y + (tw + 2) / 2 - xx, col, a);
	}
}
// A [label]'s size 1..4 -> px at the scale (11 / 15 / 22 / 30 px at s = 0.654: the 600 x 680 window)
static int label_px (int size, double s) { static const int PX[5] = { 0, 11, 15, 22, 30 }; return (int) lround (PX[size < 1 ? 1 : size > 4 ? 4 : size] * s / 0.654); }

// A lamp (an insert): unlit a dim tint of its colour in the playfield, lit the colour with a glow and a highlight
static void lamp (Canvas &cv, const View &v, Vec c, double r, unsigned col, unsigned bg, bool lit)
{
	if (!lit) { disc (cv, v, c, r, uk_mix (bg, col, 72)); ring (cv, v, c, r - 0.6, 1.2, uk_mix (bg, col, 130), 200); return; }
	disc (cv, v, c, r * 1.9, col, 50); disc (cv, v, c, r * 1.35, col, 90);
	disc (cv, v, c, r, col); Vec h = { c.x - r * 0.3, c.y - r * 0.3 }; disc (cv, v, h, r * 0.42, 0xFFFFFF, 170);
}
// A pill-shaped insert with its word (the multiplier row, SHOOT AGAIN)
static void pill (Canvas &cv, const View &v, Vec c, double w, double h, unsigned col, unsigned bg, bool lit, const char *txt)
{
	int x = (int) lround (v.ox + (c.x - w / 2) * v.s), y = (int) lround (v.oy + (c.y - h / 2) * v.s), pw = (int) lround (w * v.s), ph = (int) lround (h * v.s);
	if (pw < 4 || ph < 3) return;
	if (lit) { uk_rbox (cv, x - 3, y - 3, pw + 6, ph + 6, ph / 2 + 3, col, col, 70); uk_rbox (cv, x, y, pw, ph, ph / 2, uk_mix (col, 0xFFFFFF, 90), col); }
	else { uk_rbox (cv, x, y, pw, ph, ph / 2, uk_mix (bg, col, 70), uk_mix (bg, col, 60)); uk_rline (cv, x, y, pw, ph, ph / 2, uk_mix (bg, col, 130), 200); }
	if (txt && ph >= 8)
	{
		int px = (int) (ph * 0.78);
		for (; px > 6; px--) { TextFace *f = face (px); if (!f) return; UkFaceScope sc (f); if (uk_tw (txt, 0) <= pw - 4) break; }
		words (cv, x + pw / 2, y + ph / 2, txt, lit ? uk_ink_on (col) : uk_mix (bg, col, 160), px, 0, 0);
	}
}

// ---- what a table's drawing needs besides the table (found once per table) ------------------------------------------
struct Looks
{
	unsigned slingCol, lampCol;		// the flippers' rubber, the inserts' colour
	int lowL, lowR;				// the lower left and right flippers (the inserts sit between them)
	int slingBody[pinball::MAXSLING];	// the closed [wall] behind each sling (-1: none) -- lit when it kicks
};
static void looks_of (const Table &t, Looks &k)
{
	k.slingCol = t.nsling ? t.sling[0].colour : 0xE0559A;
	k.lampCol = 0xFFD34D;
	for (int i = 0; i < t.nlane; i++) if (t.rotate >= 0 && t.lane[i].group == t.rotate) { k.lampCol = t.lane[i].colour; break; }
	k.lowL = k.lowR = -1;
	for (int f = 0; f < t.nflip; f++)
	{
		int &low = t.flip[f].side == pinball::SIDE_LEFT ? k.lowL : k.lowR;
		if (low < 0 || t.flip[f].pivot.y > t.flip[low].pivot.y) low = f;
	}
	for (int s = 0; s < t.nsling; s++)
	{
		k.slingBody[s] = -1;
		Vec a = t.seg[t.sling[s].seg].a;
		for (int w = 0; w < t.nwall && k.slingBody[s] < 0; w++)
		{
			const pinball::Wall &W = t.wall[w];
			if (!W.closed || W.outline) continue;
			for (int i = 0; i < W.n; i++)
			{
				Vec p = t.seg[W.first + i].a;
				if (fabs (p.x - a.x) < 0.01 && fabs (p.y - a.y) < 0.01) { k.slingBody[s] = w; break; }
			}
		}
	}
}
// A wall's points (its segments' starts, then the last end when open) -> how many
static int wall_points (const Table &t, const pinball::Wall &W, Vec *out, int cap)
{
	int n = 0;
	for (int i = 0; i < W.n && n < cap; i++) out[n++] = t.seg[W.first + i].a;
	if (!W.closed && n < cap && W.n) out[n++] = t.seg[W.first + W.n - 1].b;
	return n;
}

// ---- the static layer ---------------------------------------------------------------------------------------------
// small: a list's picture (no words, thin rails); the plunger's housing only at full size
static void draw_static (Canvas &cv, const Table &t, const View &v, bool small)
{
	int x0 = (int) lround (v.ox), y0 = (int) lround (v.oy), w = (int) lround (t.w * v.s), h = (int) lround (t.h * v.s);
	cv.fillRect (x0, y0, w, h, t.background);
	unsigned bg = t.background;
	for (int i = 0; i < t.nshape; i++) fillpoly (cv, v, t.shape[i].p, t.shape[i].n, t.shape[i].colour);
	if (!small) for (int i = 0; i < t.nlabel; i++)
	{
		const pinball::Label &l = t.label[i]; int px = label_px (l.size, v.s);
		if (px >= 7) words (cv, VX (v, l.at.x) / 16, VY (v, l.at.y) / 16, l.text.get (g_lang), l.colour, px, l.angle);
	}
	for (int i = 0; i < t.nramp; i++)					// a ramp: a see-through track and its two rails
	{
		const pinball::Ramp &r = t.ramp[i]; stroke (cv, v, r.path, r.npath, r.width, r.colour, false, 60);
		for (int side = -1; side <= 1; side += 2)
		{
			Vec o[pinball::MAXPTS];
			for (int k = 0; k < r.npath; k++)
			{
				Vec a = r.path[k > 0 ? k - 1 : k], b = r.path[k < r.npath - 1 ? k + 1 : k];
				double dx = b.x - a.x, dy = b.y - a.y, l = sqrt (dx * dx + dy * dy); if (l <= 0) l = 1;
				o[k].x = r.path[k].x - dy / l * side * r.width / 2; o[k].y = r.path[k].y + dx / l * side * r.width / 2;
			}
			stroke (cv, v, o, r.npath, small ? 2.5 : 1.6, uk_mix (r.colour, 0xFFFFFF, 60), false, 230);
		}
	}
	static Vec pts[MAXDRAW];
	for (int i = 0; i < t.nwall; i++)
	{
		const pinball::Wall &W = t.wall[i];
		int n = wall_points (t, W, pts, MAXDRAW);
		if (W.outline) stroke (cv, v, pts, n, 6, W.colour, true);		// the outline: the side rails
		else if (W.closed) { fillpoly (cv, v, pts, n, uk_mix (bg, W.colour, 90)); stroke (cv, v, pts, n, 2.5, W.colour, true); }
		else stroke (cv, v, pts, n, W.width, W.colour);
	}
	for (int i = 0; i < t.narc; i++)
	{
		const pinball::Arc &a = t.arc[i];
		int a0 = (int) lround (360 - a.to), a1 = a0 + (int) lround (a.to - a.from);
		VPath p; p.arc (VX (v, a.c.x), VY (v, a.c.y), VL (v, a.r), a0, a1, VL (v, a.width)); p.fill (cv, a.colour);
	}
	for (int i = 0; i < t.nsling; i++) { const pinball::Seg &s = t.seg[t.sling[i].seg]; Vec pp[2] = { s.a, s.b }; stroke (cv, v, pp, 2, 5, 0xF4F4F4); }	// the rubber
	for (int i = 0; i < t.ncirc; i++)
		if (t.circ[i].kind == pinball::C_POST) { disc (cv, v, t.circ[i].c, t.circ[i].r + 2.5, 0xF4F4F4); disc (cv, v, t.circ[i].c, t.circ[i].r - 1, uk_mix (t.circ[i].colour, 0x000000, 60)); }
	for (int i = 0; i < t.ngate; i++) { Vec pp[2] = { t.gate[i].a, t.gate[i].b }; stroke (cv, v, pp, 2, 2, 0xC8CCD4); disc (cv, v, t.gate[i].a, 3, 0xC8CCD4); }
	for (int i = 0; i < t.nsaucer; i++)
	{ disc (cv, v, t.saucer[i].c, t.saucer[i].r + 4, uk_mix (bg, t.saucer[i].colour, 110)); disc (cv, v, t.saucer[i].c, t.saucer[i].r, uk_mix (bg, 0x000000, 170)); }
	if (!small)							// the plunger's housing
	{
		Vec a = { t.plunger.x - 14, t.h - 8 }, b = { t.plunger.x + 14, t.h - 8 };
		quad (cv, v, a, b, 12, uk_mix (bg, 0x000000, 120));
	}
}

// ---- the dynamic layer ---------------------------------------------------------------------------------------------
static void ball (Canvas &cv, const View &v, Vec c, double r)
{
	Vec sh = { c.x + 2.5, c.y + 3.5 }; disc (cv, v, sh, r, 0x000000, 80);
	disc (cv, v, c, r, 0x7C838E); Vec m = { c.x - r * 0.12, c.y - r * 0.12 }; disc (cv, v, m, r * 0.82, 0xC4CAD2);
	Vec m2 = { c.x - r * 0.22, c.y - r * 0.22 }; disc (cv, v, m2, r * 0.55, 0xDDE1E6);
	Vec h = { c.x - r * 0.38, c.y - r * 0.4 }; disc (cv, v, h, r * 0.26, 0xFFFFFF, 235);
}
// A flipper at its world angle (radians, y down): a capsule of the rubber's colour, its ivory body inside, the pivot's screw
static void flipper (Canvas &cv, const View &v, const pinball::Flip &f, double ang, unsigned rubber, bool dead, unsigned bg)
{
	double dx = cos (ang), dy = sin (ang), nx = -dy, ny = dx;
	Vec tip = { f.pivot.x + dx * f.len, f.pivot.y + dy * f.len };
	for (int pass = 0; pass < 2; pass++)
	{
		double k = pass ? 2.6 : 0; Vec pp[40]; int n = 0;
		for (int i = 0; i <= 16; i++) { double a = M_PI / 2 + M_PI * i / 16; double cx = cos (a), cy = sin (a);	// the pivot's round end
			pp[n].x = f.pivot.x + (dx * cx + nx * cy) * (f.r0 - k); pp[n].y = f.pivot.y + (dy * cx + ny * cy) * (f.r0 - k); n++; }
		for (int i = 0; i <= 16; i++) { double a = -M_PI / 2 + M_PI * i / 16; double cx = cos (a), cy = sin (a);	// the tip's
			pp[n].x = tip.x + (dx * cx + nx * cy) * (f.r1 - k); pp[n].y = tip.y + (dy * cx + ny * cy) * (f.r1 - k); n++; }
		unsigned c = pass ? 0xF2F4F8 : rubber;
		if (dead) c = uk_mix (c, bg, 150);
		fillpoly (cv, v, pp, n, c);
	}
	disc (cv, v, f.pivot, 3.2, 0x8A9099);
}

// The moving parts from the game g (0: the table at rest -- every lamp off, no ball: the picker's preview)
static void draw_dynamic (Canvas &cv, const Table &t, const Looks &k, const View &v, const Game *g)
{
	unsigned bg = t.background; bool dead = g && g->tilted;
	const World *w = g ? &g->w : 0;
	if (w) for (int s = 0; s < t.nsling; s++)			// a sling's body lit while it kicks
		if (w->slingFlash[s] > 0 && k.slingBody[s] >= 0)
		{
			static Vec pts[MAXDRAW]; const pinball::Wall &W = t.wall[k.slingBody[s]];
			int n = wall_points (t, W, pts, MAXDRAW);
			fillpoly (cv, v, pts, n, uk_mix (uk_mix (bg, W.colour, 90), 0xFFFFFF, 120)); stroke (cv, v, pts, n, 2.5, uk_mix (W.colour, 0xFFFFFF, 120), true);
			const pinball::Seg &sg = t.seg[t.sling[s].seg]; Vec pp[2] = { sg.a, sg.b }; stroke (cv, v, pp, 2, 5, 0xFFFFFF);
		}
	for (int i = 0; i < t.nlane; i++)					// the lanes' lamps
	{
		const pinball::Lane &l = t.lane[i]; Vec c = { l.x + l.w / 2, l.y + l.h / 2 };
		lamp (cv, v, c, (l.w < l.h ? l.w : l.h) * 0.3, l.colour, bg, w && !dead && w->laneLit[i]);
		Vec ab[2] = { { l.x + 3, l.y + 4 }, { l.x + l.w - 3, l.y + 4 } }; stroke (cv, v, ab, 2, 1.4, 0xB8BEC8, false, 200);	// the rollover's wire
	}
	for (int i = 0; i < t.ntgt; i++)					// drop / standup targets
	{
		const pinball::Target &tg = t.tgt[i]; const pinball::Seg &s = t.seg[tg.seg];
		double dx = s.b.x - s.a.x, dy = s.b.y - s.a.y, l = sqrt (dx * dx + dy * dy); if (l <= 0) continue;
		double nx = -dy / l, ny = dx / l;
		Vec m = { (s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2 };
		if ((m.x - t.w / 2) * nx + (m.y - t.h / 2) * ny < 0) { nx = -nx; ny = -ny; }	// n: away from the table's centre
		if (tg.drop)
		{
			if (w && w->tgtDown[i]) { quad (cv, v, s.a, s.b, 3, uk_mix (bg, 0x000000, 150)); continue; }
			Vec a = { s.a.x + nx * 4, s.a.y + ny * 4 }, b = { s.b.x + nx * 4, s.b.y + ny * 4 };
			quad (cv, v, a, b, 9, dead ? uk_mix (tg.colour, bg, 140) : tg.colour);
			quad (cv, v, s.a, s.b, 2, 0xFFFFFF, 150);
		}
		else
		{
			Vec lc = { m.x + nx * 16, m.y + ny * 16 };
			lamp (cv, v, lc, 6, tg.colour, bg, w && !dead && w->tgtLit[i]);
			Vec a = { s.a.x + nx * 3, s.a.y + ny * 3 }, b = { s.b.x + nx * 3, s.b.y + ny * 3 };
			quad (cv, v, a, b, 7, 0xF0F0F0); quad (cv, v, s.a, s.b, 2.5, tg.colour);
		}
	}
	for (int i = 0; i < t.ncirc; i++)					// pop bumpers: a skirt, a cap, a lit top
	{
		const pinball::Circle &b = t.circ[i]; if (b.kind != pinball::C_BUMPER) continue;
		bool fl = w && !dead && w->bumperFlash[i] > 0;
		if (fl) { disc (cv, v, b.c, b.r * 1.45, b.colour, 70); disc (cv, v, b.c, b.r * 1.2, b.colour, 110); }
		disc (cv, v, b.c, b.r, uk_mix (b.colour, 0x000000, 140));
		disc (cv, v, b.c, b.r * 0.84, fl ? uk_mix (b.colour, 0xFFFFFF, 120) : dead ? uk_mix (b.colour, bg, 120) : b.colour);
		ring (cv, v, b.c, b.r * 0.84, 1.2, uk_mix (b.colour, 0x000000, 90), 160);
		disc (cv, v, b.c, b.r * 0.5, fl ? 0xFFFFFF : uk_mix (b.colour, 0xFFFFFF, 150));
		Vec h = { b.c.x - b.r * 0.18, b.c.y - b.r * 0.2 }; disc (cv, v, h, b.r * 0.16, 0xFFFFFF, 200);
	}
	for (int i = 0; i < t.nsaucer; i++)					// a saucer's ring: lit while it holds a ball
	{
		const pinball::Saucer &s = t.saucer[i]; bool lit = false;
		if (w && !dead) for (int b = 0; b < pinball::MAXBALL; b++) if (w->ball[b].state == pinball::B_SAUCER && w->ball[b].saucer == i) lit = true;
		if (lit) { ring (cv, v, s.c, s.r + 6, 5, s.colour, 90); ring (cv, v, s.c, s.r + 2, 3, s.colour); }
		else ring (cv, v, s.c, s.r + 2, 2, uk_mix (bg, s.colour, 120), 220);
	}
	for (int i = 0; i < t.nramp; i++)					// a ramp's arrow, before its entry: lit when entered
	{
		const pinball::Ramp &r = t.ramp[i]; Vec m = { (r.a.x + r.b.x) / 2 - r.pass.x * 34, (r.a.y + r.b.y) / 2 - r.pass.y * 34 };
		double px = r.pass.x, py = r.pass.y, qx = -py, qy = px;
		Vec tri[3] = { { m.x + px * 11, m.y + py * 11 }, { m.x - px * 8 + qx * 9, m.y - py * 8 + qy * 9 }, { m.x - px * 8 - qx * 9, m.y - py * 8 - qy * 9 } };
		bool lit = w && !dead && w->rampFlash[i] > 0;
		if (lit) { disc (cv, v, m, 15, r.colour, 60); fillpoly (cv, v, tri, 3, r.colour); }
		else fillpoly (cv, v, tri, 3, uk_mix (bg, r.colour, 80));
	}
	// the game's own inserts (04 D17): the bonus multiplier (2x ... 5x) above the flippers, SHOOT AGAIN below them
	if (k.lowL >= 0 && k.lowR >= 0)
	{
		double fx = (t.flip[k.lowL].pivot.x + t.flip[k.lowR].pivot.x) / 2, fy = t.flip[k.lowL].pivot.y;
		for (int i = 0; i < 4; i++)
		{
			char s[8]; snprintf (s, sizeof s, "%d\xC3\x97", i + 2); Vec c = { fx + (i - 1.5) * 38, fy - 115 };
			pill (cv, v, c, 32, 18, k.lampCol, bg, g && !dead && g->mult >= i + 2, s);
		}
		bool sa = g && (g->extraBalls > 0 || (g->ballSaveActive () && (g->frameNo / 15) % 2 == 0));
		Vec c = { fx, fy + 74 }; pill (cv, v, c, 112, 17, 0xF04848, bg, sa, TR ("SHOOT AGAIN"));
	}
	for (int i = 0; i < t.nflip; i++) flipper (cv, v, t.flip[i], w ? w->flipAng[i] : t.flip[i].rest, k.slingCol, dead, bg);
	double pull = w ? w->pull : 0;
	{									// the plunger: a knob on a spring
		double y = t.plunger.y + 15 + pull * 36, bottom = t.h - 10; Vec a = { t.plunger.x - 12, y }, b = { t.plunger.x + 12, y };
		Vec z[12]; int n = 0;
		for (double yy = y + 6; yy < bottom && n < 12; yy += (bottom - y - 6) / 9, n++) { z[n].x = t.plunger.x + (n & 1 ? 7 : -7); z[n].y = yy; }
		stroke (cv, v, z, n, 1.8, 0x9AA0A8);
		quad (cv, v, a, b, 8, 0xC8CCD4); quad (cv, v, a, b, 2, 0xFFFFFF, 120);
	}
	if (!w) return;
	for (int i = 0; i < pinball::MAXBALL; i++)
	{
		const pinball::Ball &B = w->ball[i];
		if (B.state == pinball::B_PLAY || B.state == pinball::B_SAUCER) ball (cv, v, B.p, t.ball);
		else if (B.state == pinball::B_PLUNGER) { Vec p = { B.p.x, B.p.y + pull * 36 }; ball (cv, v, p, t.ball); }
	}
	for (int i = 0; i < pinball::MAXBALL; i++)				// a ball on a ramp: above the table
		if (w->ball[i].state == pinball::B_RAMP) ball (cv, v, w->rampPos (i), t.ball + 1);
}

#endif
