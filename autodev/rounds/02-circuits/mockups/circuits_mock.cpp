//
// circuits_mock -- (AutoDev round 2, the UX Designer's mock-ups; the desktop simulator only, never on the card)
// Circuits' window drawn with UIKit itself from canned boards, so the pictures of 04-ux-design.md are the real
// toolkit's pixels. Not the app: the owner-drawn pieces (LevelList, Board, TruthTable, Card, MsgBar, the lesson
// and the result cards) are sketched here the way the GUI plan builds them. Built and run by mockups.sh beside
// it; MOCK=<scene> picks the picture, MOCK_LANG=fr the French words:
//   main     level 3.3 Full adder solved, live (A = 1, B = 0, Cin = 1), a gate selected
//   place    a gate being placed (AND armed in the palette, its ghost under the pointer)
//   wire     a wire being drawn (from X1's output to A2's input: the pin ringed)
//   step     step by step, step 2 of 3 (the OR gate and Cout still unknown)
//   check    level 2.2 checked with OR: row 1 1 wrong, marked; the switches set on it
//   won      level 2.2 won with 4 gates: the result card, 2 stars
//   refused  Check refused: lamp Cout not connected
//   lesson   level 1.3: the lesson card of a new gate (AND)
//   empty    the first start, level 1.1 (no gate in the palette), only 1.1 open
//   levels   the levels list alone, tall (the three worlds, locked / open / stars)
//   dialog   a malformed pack refused (MessageBox)
// MOCK_W / MOCK_H: the window's size (default 1000 x 620; the minimum 920 x 600)
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "uikit/vpaint.h"
#include "fontkit/uikitface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

using namespace uikit;

static const char *g_scene = "main";
static bool g_fr = false;
#define TR2(en, fr) (g_fr ? (fr) : (en))
static FtTextFace *g_big, *g_small, *g_huge, *g_tiny;

// ---- the palette of the board (04-ux-design.md section 9) ----------------------------------------------------
static const unsigned BOARD_BG   = 0x00EEF3EF;	// the board: a pale green paper
static const unsigned BOARD_DOT  = 0x00C9D4CC;	// the grid's dots
static const unsigned STRIP_BG   = 0x00E2EAE4;	// the fixed columns (switches, lamps)
static const unsigned WIRE_1     = 0x0021A346;	// a wire at 1: lit green
static const unsigned WIRE_0     = 0x00334A5E;	// a wire at 0: dark slate
static const unsigned WIRE_X     = 0x00A9B3BC;	// unknown (step mode) / not driven: grey, dashed
static const unsigned GATE_INK   = 0x002B3440;	// the gates' outline and names
static const unsigned GATE_FACE  = 0x00FFFFFF;
static const unsigned GATE_FACE1 = 0x00E3F5E7;	// a gate whose output is 1
static const unsigned GATE_FACEX = 0x00F0F2F4;	// not computed yet (step mode)
static const unsigned LAMP_ON    = 0x00FFD23F, LAMP_OFF = 0x005B5648;
static const unsigned ERR_RED    = 0x00D0342C, OK_GREEN = 0x003E9B4F, STAR_GOLD = 0x00F2B705;

// ---- the canned model -------------------------------------------------------------------------------------------
enum { T_SW, T_LAMP, T_NOT, T_AND, T_OR, T_XOR, T_NAND, T_NOR, T_N };
static const char *const TNAME[T_N] = { "", "", "NOT", "AND", "OR", "XOR", "NAND", "NOR" };
static const char *const TNAME_FR[T_N] = { "", "", "NON", "ET", "OU", "OUX", "NON-ET", "NON-OU" };
static const char *tname (int t) { return g_fr ? TNAME_FR[t] : TNAME[t]; }
struct Part { int type, x, y; const char *name; int v; bool known; int depth; bool sel, err; };
struct Wire { int src, dst, pin, xm; bool sel; };
enum { GW = 40, GH = 30 };				// the grid, in cells
static Part g_p[32]; static int g_np = 0;
static Wire g_w[48]; static int g_nw = 0;
static int g_step = -1;					// step mode: the depth computed so far (-1: live)
static int g_maxDepth = 0;

static int add_part (int t, int x, int y, const char *name = "", int v = 0)
{ Part &p = g_p[g_np]; memset (&p, 0, sizeof p); p.type = t; p.x = x; p.y = y; p.name = name; p.v = v; p.known = true; return g_np++; }
static void add_wire (int s, int d, int pin, int xm) { Wire &w = g_w[g_nw++]; w.src = s; w.dst = d; w.pin = pin; w.xm = xm; w.sel = false; }
// the level's switches and lamps: fixed, spread down the two edges (the engine's rule: y = (2k + 1) H / 2n - 1)
static int add_inputs (const char *const *names, int n, const int *vals)
{ int first = g_np; for (int k = 0; k < n; k++) add_part (T_SW, 0, (2 * k + 1) * GH / (2 * n) - 1, names[k], vals[k]); return first; }
static int add_outputs (const char *const *names, int n)
{ int first = g_np; for (int k = 0; k < n; k++) add_part (T_LAMP, 34, (2 * k + 1) * GH / (2 * n) - 1, names[k]); return first; }

static int pins_of (int t) { return t == T_SW ? 0 : t == T_LAMP || t == T_NOT ? 1 : 2; }
static void pin_out (const Part &p, int &x, int &y) { if (p.type == T_SW) { x = p.x + 5; y = p.y + 1; } else { x = p.x + 5; y = p.y + 2; } }
static void pin_in (const Part &p, int k, int &x, int &y)
{
	x = p.x;
	if (p.type == T_LAMP) y = p.y + 1; else if (p.type == T_NOT) y = p.y + 2; else y = p.y + 1 + 2 * k;
}
static int src_of (int d, int pin) { for (int i = 0; i < g_nw; i++) if (g_w[i].dst == d && g_w[i].pin == pin) return g_w[i].src; return -1; }

static void evaluate ()
{
	for (int i = 0; i < g_np; i++) if (g_p[i].type != T_SW) { g_p[i].depth = -1; }
	for (int pass = 0; pass < 32; pass++)
		for (int i = 0; i < g_np; i++)
		{
			Part &p = g_p[i]; if (p.type == T_SW) { p.depth = 0; continue; }
			int n = pins_of (p.type), in[2] = { 0, 0 }, d = 0; bool ok = true;
			for (int k = 0; k < n; k++)
			{
				int s = src_of (i, k); if (s < 0) { in[k] = 0; continue; }
				if (g_p[s].depth < 0) { ok = false; break; }
				in[k] = g_p[s].v; if (g_p[s].depth > d) d = g_p[s].depth;
			}
			if (!ok) continue;
			int v = 0;
			switch (p.type)
			{
			case T_LAMP: v = in[0]; break;
			case T_NOT: v = !in[0]; break;
			case T_AND: v = in[0] & in[1]; break;
			case T_OR: v = in[0] | in[1]; break;
			case T_XOR: v = in[0] ^ in[1]; break;
			case T_NAND: v = !(in[0] & in[1]); break;
			case T_NOR: v = !(in[0] | in[1]); break;
			}
			p.v = v; p.depth = p.type == T_LAMP ? d : d + 1;
		}
	g_maxDepth = 0;
	for (int i = 0; i < g_np; i++) if (g_p[i].type > T_LAMP && g_p[i].depth > g_maxDepth) g_maxDepth = g_p[i].depth;
	for (int i = 0; i < g_np; i++)
	{
		Part &p = g_p[i];
		if (g_step < 0 || p.type == T_SW) p.known = true;
		else if (p.type == T_LAMP) { int s = src_of (i, 0); p.known = s >= 0 && (g_p[s].type == T_SW || g_p[s].depth <= g_step); }
		else p.known = p.depth <= g_step;
	}
}
static int gate_count () { int n = 0; for (int i = 0; i < g_np; i++) if (g_p[i].type > T_LAMP) n++; return n; }

// ---- the levels (the canned progress) ----------------------------------------------------------------------------
struct Lv { const char *en, *fr; int stars; bool open; };
struct World { const char *en, *fr; Lv lv[8]; int n; };
static World g_worlds[3] = {
	{ "1. Gates", "1. Les portes", { { "First light", "Première lumière", 3, true }, { "Upside down", "À l'envers", 3, true },
	  { "Both at once", "Les deux à la fois", 3, true }, { "One or the other", "L'un ou l'autre", 3, true },
	  { "Three keys", "Trois clés", 3, true }, { "Not both", "Pas les deux", 2, true }, { "Neither one", "Ni l'un ni l'autre", 3, true },
	  { "NAND does it all", "NAND sait tout faire", 2, true } }, 8 },
	{ "2. Combining", "2. Combiner", { { "An OR made of NAND", "Un OU en NAND", 3, true }, { "The hallway light", "La lumière du couloir", 2, true },
	  { "XOR made of NAND", "Un OU exclusif en NAND", 3, true }, { "Same or not", "Pareil ou pas", 3, true },
	  { "Majority vote", "Vote à la majorité", 1, true }, { "Railway points", "L'aiguillage", 3, true } }, 6 },
	{ "3. Arithmetic", "3. Calculer", { { "Odd one out", "Nombre impair", 3, true }, { "Half adder", "Demi-additionneur", 3, true },
	  { "Full adder", "Additionneur complet", 0, true }, { "Two-bit adder", "Additionneur 2 bits", 0, false },
	  { "Decoder", "Décodeur", 0, false }, { "Comparator", "Comparateur", 0, false } }, 6 },
};
static int g_curW = 2, g_curL = 2;
static void progress_fresh (int openW, int openL)	// everything up to (openW, openL) open, none won after it
{
	for (int w = 0; w < 3; w++) for (int l = 0; l < g_worlds[w].n; l++)
	{
		bool before = w < openW || (w == openW && l < openL), at = w == openW && l == openL;
		g_worlds[w].lv[l].open = before || at;
		if (!before) g_worlds[w].lv[l].stars = 0;
	}
}

// ---- drawing helpers --------------------------------------------------------------------------------------------
static void star (Canvas &cv, int cx, int cy, int r, unsigned c, bool filled)	// Turtle Quest's star
{
	int pts[20];
	for (int i = 0; i < 10; i++)
	{
		int rr = i & 1 ? r * 2 / 5 : r, a = i * 36;
		pts[2 * i] = V (cx) + uk_sin (a) * rr * 16 / 16384;
		pts[2 * i + 1] = V (cy) - uk_cos (a) * rr * 16 / 16384;
	}
	VPath p; p.poly (pts, 10);
	if (filled) p.fill (cv, c);
	else { VPath o; o.polyline (pts, 10, V (1) + 8, true); o.fill (cv, c); }
}
static void padlock (Canvas &cv, int cx, int cy, unsigned c)
{
	VPath p; p.rrect (V (cx - 5), V (cy - 1), V (10), V (8), V (1) + 8); p.fill (cv, c);
	VPath s; s.arc (V (cx), V (cy - 2), V (3) + 8, 0, 180, V (1) + 8); s.line (V (cx - 3) - 8, V (cy - 2), V (cx - 3) - 8, V (cy), V (1) + 8);
	s.line (V (cx + 3) + 8, V (cy - 2), V (cx + 3) + 8, V (cy), V (1) + 8); s.fill (cv, c);
}
static void wrap_draw (Canvas &cv, int x, int y, int w, const char *s, unsigned c, int maxL, int lh, int style = 0)
{
	int st[12], ln[12]; int n = uk_text_wrap (s, (int) strlen (s), w, maxL, st, ln, 0, style);
	for (int i = 0; i < n; i++) { char l[300]; int m = ln[i] < 299 ? ln[i] : 299; memcpy (l, s + st[i], m); l[m] = 0; uk_text (cv, x, y + i * lh, l, c, style); }
}
static int wrap_lines (const char *s, int w, int maxL, int style = 0) { int st[12], ln[12]; return uk_text_wrap (s, (int) strlen (s), w, maxL, st, ln, 0, style); }

// The gate's outline, in 1/16 px: a closed polygon (fill it, then stroke it). Quadratic curves are sampled by
// hand (VPath has none): in the app the same points with integer arithmetic.
static int qbez (int *xy, int n, double x0, double y0, double x1, double y1, double x2, double y2, int steps, bool skipFirst)
{
	for (int i = skipFirst ? 1 : 0; i <= steps; i++)
	{
		double t = (double) i / steps, a = (1 - t) * (1 - t), b = 2 * (1 - t) * t, c = t * t;
		xy[2 * n] = (int) lround (a * x0 + b * x1 + c * x2); xy[2 * n + 1] = (int) lround (a * y0 + b * y1 + c * y2); n++;
	}
	return n;
}
// type at the body box (L, T)-(R, B) (1/16 px); the bubble's radius rb: the outline's points -> n
static int gate_outline (int type, double L, double T, double R, double B, double rb, int *xy)
{
	double cy = (T + B) / 2, h2 = (B - T) / 2, w = R - L; int n = 0;
	bool bub = type == T_NOT || type == T_NAND || type == T_NOR;
	double Rb = bub ? R - 2 * rb : R;
	switch (type)
	{
	case T_NOT:
		xy[0] = (int) L; xy[1] = (int) (cy - h2 * 0.78); xy[2] = (int) Rb; xy[3] = (int) cy; xy[4] = (int) L; xy[5] = (int) (cy + h2 * 0.78); n = 3; break;
	case T_AND: case T_NAND:
	{
		double r = h2, cx = Rb - r;
		xy[2 * n] = (int) L; xy[2 * n + 1] = (int) T; n++;
		for (int a = -90; a <= 90; a += 10) { xy[2 * n] = (int) lround (cx + r * cos (a * M_PI / 180)); xy[2 * n + 1] = (int) lround (cy + r * sin (a * M_PI / 180)); n++; }
		xy[2 * n] = (int) L; xy[2 * n + 1] = (int) B; n++;
		break;
	}
	default:	// OR, NOR, XOR: the shield
	{
		double l = type == T_XOR ? L + w * 0.12 : L;
		n = qbez (xy, n, l, T, l + (Rb - l) * 0.58, T, Rb, cy, 12, false);
		n = qbez (xy, n, Rb, cy, l + (Rb - l) * 0.58, B, l, B, 12, true);
		n = qbez (xy, n, l, B, l + (Rb - l) * 0.26, cy, l, T, 10, true);
		n--;	// (the last point is the first)
	}
	}
	return n;
}

// ---- the board ------------------------------------------------------------------------------------------------------
class Board;
static Board *g_board;
struct Ghost { bool on; int type, x, y; bool ok; };
static Ghost g_ghost = { false, 0, 0, 0, true };
struct Rubber { bool on; int src; double gx, gy; int tgt, tpin; bool ok; };
static Rubber g_rub = { false, 0, 0, 0, -1, 0, true };
static bool g_hintEmpty = false;

class Board : public Widget
{
public:
	int c, ox, oy;					// the cell, px; the grid's origin
	Board (int l, int t, int w, int h) : Widget (l, t, w, h), c (12), ox (0), oy (0) {}
	int X (double gx) { return ox + (int) lround (gx * c); }
	int Y (double gy) { return oy + (int) lround (gy * c); }
	double VX (double gx) { return (ox + gx * c) * 16; }
	double VY (double gy) { return (oy + gy * c) * 16; }
	void fit ()
	{
		int m = 10;
		c = (width - 2 * m) / GW; int ch = (height - 2 * m) / GH; if (ch < c) c = ch; if (c < 10) c = 10;
		ox = (width - GW * c) / 2; oy = (height - GH * c) / 2;
	}
	unsigned wireCol (int src) { const Part &s = g_p[src]; if (!s.known) return WIRE_X; return s.v ? WIRE_1 : WIRE_0; }
	void seg (VPath &p, double x0, double y0, double x1, double y1, int w) { p.line ((int) VX (x0), (int) VY (y0), (int) VX (x1), (int) VY (y1), w); }
	void dashed (Canvas &cv, double x0, double y0, double x1, double y1, unsigned col, int w)
	{
		double len = fabs (x1 - x0) + fabs (y1 - y0); if (len <= 0) return;
		double dash = 0.45, gap = 0.3;
		for (double s = 0; s < len; s += dash + gap)
		{
			double e = s + dash < len ? s + dash : len;
			VPath p; seg (p, x0 + (x1 - x0) * s / len, y0 + (y1 - y0) * s / len, x0 + (x1 - x0) * e / len, y0 + (y1 - y0) * e / len, w); p.fill (cv, col);
		}
	}
	void route (int i, double *pts)		// the wire's 4 points (grid units)
	{
		const Wire &w = g_w[i]; int x0, y0, x1, y1; pin_out (g_p[w.src], x0, y0); pin_in (g_p[w.dst], w.pin, x1, y1);
		pts[0] = x0; pts[1] = y0; pts[2] = w.xm; pts[3] = y0; pts[4] = w.xm; pts[5] = y1; pts[6] = x1; pts[7] = y1;
	}
	void drawWire (int i)
	{
		double q[8]; route (i, q);
		const Wire &w = g_w[i];
		unsigned col = wireCol (w.src);
		int ww = col == WIRE_1 ? V (3) : V (2) + 8;
		if (w.sel) { VPath h; for (int k = 0; k < 3; k++) seg (h, q[2 * k], q[2 * k + 1], q[2 * k + 2], q[2 * k + 3], V (8)); h.fill (canvas, C_ACCENT, 90); }
		if (col == WIRE_X) { for (int k = 0; k < 3; k++) dashed (canvas, q[2 * k], q[2 * k + 1], q[2 * k + 2], q[2 * k + 3], col, V (2)); return; }
		VPath p; for (int k = 0; k < 3; k++) seg (p, q[2 * k], q[2 * k + 1], q[2 * k + 2], q[2 * k + 3], ww); p.fill (canvas, col);
	}
	void junctions ()				// a dot where a wire leaves another of the same source
	{
		for (int i = 0; i < g_nw; i++)
		{
			double a[8]; route (i, a);
			for (int j = 0; j < g_nw; j++)
			{
				if (i == j || g_w[j].src != g_w[i].src) continue;
				double b[8]; route (j, b);
				// i turns at (xm, y0); j goes on past it along the same row: a junction
				if (b[2] > a[2] && b[1] == a[1] && a[2] != a[0])
				{ VPath d; d.circle ((int) VX (a[2]), (int) VY (a[3]), V (3) + 8); d.fill (canvas, wireCol (g_w[i].src)); }
			}
		}
	}
	void pinDot (double gx, double gy, unsigned col) { VPath d; d.circle ((int) VX (gx), (int) VY (gy), V (2) + 8); d.fill (canvas, col); }
	void drawGate (const Part &p, bool ghost = false, bool ghostOk = true)
	{
		int n = pins_of (p.type);
		// the stubs: from the pins into the body (the body covers their ends)
		for (int k = 0; k < n; k++)
		{
			int x, y; pin_in (p, k, x, y); int s = ghost ? -1 : src_of ((int) (&p - g_p), k);
			unsigned col = s >= 0 ? wireCol (s) : 0x00808A94;
			VPath st; seg (st, x, y, x + 2, y, s >= 0 && col == WIRE_1 ? V (3) : V (2)); st.fill (canvas, col, ghost ? 120 : 255);
		}
		unsigned oc = ghost ? 0x00808A94 : !p.known ? WIRE_X : p.v ? WIRE_1 : WIRE_0;
		{ VPath st; seg (st, p.x + 3.5, p.y + 2, p.x + 5, p.y + 2, !ghost && p.known && p.v ? V (3) : V (2)); st.fill (canvas, oc, ghost ? 120 : 255); }
		double L = VX (p.x + 1), R = VX (p.x + 4.1), cy = VY (p.y + 2), h2 = c * 16 * 1.45, rb = c * 16 * 0.3;
		int xy[200];
		int m = gate_outline (p.type, L, cy - h2, R, cy + h2, rb, xy);
		unsigned face = ghost ? (ghostOk ? 0x00E6F4EA : 0x00FBE0DC) : !p.known ? GATE_FACEX : p.v ? GATE_FACE1 : GATE_FACE;
		unsigned ink = ghost ? (ghostOk ? OK_GREEN : ERR_RED) : p.err ? ERR_RED : GATE_INK;
		if (p.sel)
		{
			uk_rbox (canvas, X (p.x + 0.55), Y (p.y + 0.2), X (p.x + 4.75) - X (p.x + 0.55), Y (p.y + 3.8) - Y (p.y + 0.2), 6, uk_mix (BOARD_BG, C_ACCENT, 50), uk_mix (BOARD_BG, C_ACCENT, 50));
			uk_rline (canvas, X (p.x + 0.55), Y (p.y + 0.2), X (p.x + 4.75) - X (p.x + 0.55), Y (p.y + 3.8) - Y (p.y + 0.2), 6, C_ACCENT);
		}
		{ VPath b; b.poly (xy, m); b.fill (canvas, face, ghost ? 200 : 255); }
		{ VPath o; o.polyline (xy, m, V (1) + 10, true); o.fill (canvas, ink, ghost ? 200 : 255); }
		if (p.type == T_XOR)
		{
			int bx[40]; int k = qbez (bx, 0, L - c * 16 * 0.0, cy + h2, L + (R - L) * 0.24, cy, L, cy - h2, 10, false);
			VPath o; o.polyline (bx, k, V (1) + 10, false); o.fill (canvas, ink, ghost ? 200 : 255);
		}
		if (p.type == T_NOT || p.type == T_NAND || p.type == T_NOR)
		{
			VPath bb; bb.circle ((int) (R - rb), (int) cy, (int) rb); bb.fill (canvas, ink, ghost ? 200 : 255);
			VPath bi; bi.circle ((int) (R - rb), (int) cy, (int) (rb - 24)); bi.fill (canvas, face, 255);
		}
		// its name, small, inside
		{
			UkFaceScope fs (g_small);
			const char *nm = tname (p.type);
			double tx = p.type == T_NOT ? p.x + 1.15 : p.type == T_XOR || p.type == T_OR || p.type == T_NOR ? p.x + 1.9 : p.x + 1.35;
			double tw = p.type == T_NOT ? 1.6 : 2.0;
			if (p.type == T_AND || p.type == T_NAND) tw = 2.1;
			int x0 = X (tx), w0 = X (tx + tw) - x0;
			uk_text_c (canvas, x0, Y (p.y + 2) - 8, w0, 16, nm, ghost ? ink : uk_mix (face, GATE_INK, 190), 0);
		}
		// step mode: the depth in a small disc above the gate
		if (g_step >= 0 && !ghost)
		{
			int cx = X (p.x + 2.5), cyy = Y (p.y + 0.05) - 2;
			unsigned bc = p.known ? C_ACCENT : 0x00B5BDC4;
			VPath d; d.circle (V (cx), V (cyy), V (7)); d.fill (canvas, bc);
			char t[4]; snprintf (t, sizeof t, "%d", p.depth);
			UkFaceScope fs (g_small); uk_text_c (canvas, cx - 7, cyy - 7, 14, 14, t, 0x00FFFFFF, 2);
		}
		for (int k = 0; k < n; k++) { int x, y; pin_in (p, k, x, y); pinDot (x, y, ghost ? 0x00808A94 : 0x005A6570); }
		{ int x, y; pin_out (p, x, y); pinDot (x, y, ghost ? 0x00808A94 : oc); }
	}
	void drawSwitch (const Part &p)		// a key showing its digit, its name at its left; green when 1
	{
		int kx = X (p.x + 2.4), ky = Y (p.y + 0.05), ks = Y (p.y + 1.95) - ky, kw = X (p.x + 4.5) - kx;
		{ VPath st; seg (st, p.x + 4.3, p.y + 1, p.x + 5, p.y + 1, p.v ? V (3) : V (2) + 8); st.fill (canvas, p.v ? WIRE_1 : WIRE_0); }
		int nw = uk_tw (p.name, 2);
		uk_text_l (canvas, kx - 5 - nw, ky, ks, p.name, C_FIELD_TEXT, 2);
		if (p.v) { uk_rbox (canvas, kx, ky, kw, ks, 5, uk_tone (WIRE_1, 150), WIRE_1); uk_rline (canvas, kx, ky, kw, ks, 5, uk_tone (WIRE_1, 80)); }
		else uk_raised (canvas, kx, ky, kw, ks, 5, C_FACE);
		uk_text_c (canvas, kx, ky, kw, ks, p.v ? "1" : "0", p.v ? 0x00FFFFFF : C_TEXT, 2);
		pinDot (p.x + 5, p.y + 1, p.v ? WIRE_1 : WIRE_0);
	}
	void drawLamp (const Part &p, int idx)
	{
		int s = src_of (idx, 0);
		unsigned col = s >= 0 ? wireCol (s) : 0x00808A94;
		{ VPath st; seg (st, p.x, p.y + 1, p.x + 1, p.y + 1, s >= 0 && col == WIRE_1 ? V (3) : V (2)); st.fill (canvas, col); }
		int cx = X (p.x + 1.75), cy = Y (p.y + 1), r = c * 9 / 10 + 1;
		bool on = p.known && p.v && s >= 0;
		if (p.err) { VPath e; e.circle (V (cx), V (cy), V (r + 6)); e.hole (V (cx), V (cy), V (r + 3)); e.fill (canvas, ERR_RED); }
		if (on)
		{
			VPath g1; g1.circle (V (cx), V (cy), V (r + 7)); g1.fill (canvas, LAMP_ON, 50);
			VPath g2; g2.circle (V (cx), V (cy), V (r + 3)); g2.fill (canvas, LAMP_ON, 90);
		}
		VPath b; b.circle (V (cx), V (cy), V (r)); b.fill (canvas, on ? 0x00C99A12 : !p.known ? 0x009AA3AB : 0x00403B30);
		VPath bi; bi.circle (V (cx), V (cy), V (r - 2)); bi.fill (canvas, on ? LAMP_ON : !p.known ? 0x00C3CAD0 : LAMP_OFF);
		VPath hl; hl.circle (V (cx - r / 3), V (cy - r / 3), V (r / 3)); hl.fill (canvas, 0x00FFFFFF, on ? 170 : 60);
		if (!p.known) uk_text_c (canvas, cx - r, cy - r, 2 * r, 2 * r, "?", 0x00FFFFFF, 2);
		uk_text_l (canvas, cx + r + 6, cy - 10, 20, p.name, C_FIELD_TEXT, 2);
		pinDot (p.x, p.y + 1, col);
	}
	void onDraw () override
	{
		fit ();
		canvas.clear (bgColor ());
		uk_rbox (canvas, 0, 0, width, height, 10, BOARD_BG, BOARD_BG);
		// the fixed columns: the switches' and the lamps'
		uk_rbox (canvas, 1, 1, X (6) - 1, height - 2, 10, STRIP_BG, STRIP_BG, 255, UK_TL | UK_BL);
		uk_rbox (canvas, X (33), 1, width - X (33) - 1, height - 2, 10, STRIP_BG, STRIP_BG, 255, UK_TR | UK_BR);
		for (int gy = 0; gy <= GH; gy++) for (int gx = 0; gx <= GW; gx++)
			if (gx > 6 && gx < 33) canvas.fillRect (X (gx), Y (gy), 1, 1, BOARD_DOT), canvas.fillRect (X (gx), Y (gy), 2, 1, BOARD_DOT);
		{ UkFaceScope fs (g_small); unsigned cap = uk_mix (STRIP_BG, GATE_INK, 120);
		  uk_text_c (canvas, 0, 4, X (6), 14, TR2 ("INPUTS", "ENTRÉES"), cap, 2); uk_text_c (canvas, X (33), 4, width - X (33), 14, TR2 ("OUTPUTS", "SORTIES"), cap, 2); }
		uk_rline (canvas, 0, 0, width, height, 10, uk_mix (BOARD_BG, GATE_INK, 60));
		for (int i = 0; i < g_nw; i++) if (!g_w[i].sel) drawWire (i);
		for (int i = 0; i < g_nw; i++) if (g_w[i].sel) drawWire (i);
		junctions ();
		for (int i = 0; i < g_np; i++)
		{
			const Part &p = g_p[i];
			if (p.type == T_SW) drawSwitch (p); else if (p.type == T_LAMP) drawLamp (p, i); else drawGate (p);
		}
		if (g_hintEmpty)
		{
			int bx = X (9), by = Y (6), bw = X (31) - bx, bh = Y (13) - by;
			uk_rbox (canvas, bx, by, bw, bh, 10, 0x00FFFFFF, 0x00FFFFFF, 170);
			for (int x = bx + 8; x < bx + bw - 8; x += 10) { canvas.fillRect (x, by, 5, 1, 0x009DB0A4); canvas.fillRect (x, by + bh - 1, 5, 1, 0x009DB0A4); }
			for (int y = by + 8; y < by + bh - 8; y += 10) { canvas.fillRect (bx, y, 1, 5, 0x009DB0A4); canvas.fillRect (bx + bw - 1, y, 1, 5, 0x009DB0A4); }
			{ UkFaceScope fs (g_big); uk_text_c (canvas, bx, by + 12, bw, 24, TR2 ("Lay a wire", "Pose un fil"), 0x00405048, 2); }
			const char *t = TR2 ("Press on the switch's pin, drag to the lamp's pin, let go.",
					     "Appuie sur la borne de l'interrupteur, glisse jusqu'à celle de la lampe, lâche.");
			int st[4], ln[4]; int k = uk_text_wrap (t, (int) strlen (t), bw - 40, 3, st, ln);
			for (int i = 0; i < k; i++) { char l[200]; memcpy (l, t + st[i], ln[i]); l[ln[i]] = 0; uk_text_c (canvas, bx, by + 44 + i * 18, bw, 18, l, 0x005A6A60); }
		}
		if (g_ghost.on) { Part gp; memset (&gp, 0, sizeof gp); gp.type = g_ghost.type; gp.x = g_ghost.x; gp.y = g_ghost.y; drawGate (gp, true, g_ghost.ok); }
		if (g_rub.on)
		{
			int x0, y0; pin_out (g_p[g_rub.src], x0, y0);
			double ex = g_rub.gx, ey = g_rub.gy;
			if (g_rub.tgt >= 0) { int tx, ty; pin_in (g_p[g_rub.tgt], g_rub.tpin, tx, ty); ex = tx; ey = ty; }
			double xm = floor ((x0 + ex) / 2);
			dashed (canvas, x0, y0, xm, y0, C_ACCENT, V (2) + 8); dashed (canvas, xm, y0, xm, ey, C_ACCENT, V (2) + 8); dashed (canvas, xm, ey, ex, ey, C_ACCENT, V (2) + 8);
			if (g_rub.tgt >= 0)
			{
				int tx, ty; pin_in (g_p[g_rub.tgt], g_rub.tpin, tx, ty);
				VPath r; r.circle ((int) VX (tx), (int) VY (ty), V (7)); r.hole ((int) VX (tx), (int) VY (ty), V (5)); r.fill (canvas, g_rub.ok ? OK_GREEN : ERR_RED);
			}
			VPath o; o.circle ((int) VX (x0), (int) VY (y0), V (6)); o.hole ((int) VX (x0), (int) VY (y0), V (4)); o.fill (canvas, C_ACCENT);
			// the pointer
			int px = X (g_rub.gx), py = Y (g_rub.gy), pts[] = { V (px), V (py), V (px), V (py + 15), V (px + 4), V (py + 11), V (px + 7), V (py + 17), V (px + 9), V (py + 16), V (px + 6), V (py + 10), V (px + 11), V (py + 10) };
			VPath a; a.poly (pts, 7); a.fill (canvas, 0x00000000); VPath a2; a2.polyline (pts, 7, V (1), true); a2.fill (canvas, 0x00FFFFFF);
		}
		if (g_ghost.on)
		{
			int px = X (g_ghost.x + 2.6), py = Y (g_ghost.y + 2.4);
			int pts[] = { V (px), V (py), V (px), V (py + 15), V (px + 4), V (py + 11), V (px + 7), V (py + 17), V (px + 9), V (py + 16), V (px + 6), V (py + 10), V (px + 11), V (py + 10) };
			VPath a; a.poly (pts, 7); a.fill (canvas, 0x00000000); VPath a2; a2.polyline (pts, 7, V (1), true); a2.fill (canvas, 0x00FFFFFF);
		}
	}
};

// ---- the levels list --------------------------------------------------------------------------------------------
class LevelList : public Widget
{
public:
	int top;
	enum { HEAD = 30, ROW = 28 };
	LevelList (int l, int t, int w, int h) : Widget (l, t, w, h), top (0) {}
	int total () { int n = 0; for (int w = 0; w < 3; w++) n += HEAD + g_worlds[w].n * ROW; return n; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		Canvas clip; clip.adopt (canvas.px + 2 * canvas.stride + 2, width - 4, height - 4, canvas.stride);
		int y = -top, scroll = total () > height - 4;
		int rw = clip.w - (scroll ? UK_SBW + 2 : 0);
		for (int w = 0; w < 3; w++)
		{
			World &W = g_worlds[w];
			int got = 0; bool any = false; for (int l = 0; l < W.n; l++) { got += W.lv[l].stars; if (W.lv[l].open) any = true; }
			unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 130);
			if (y > -HEAD && y < clip.h)
			{
				clip.fillRect (0, y, rw, HEAD, uk_mix (C_FIELD, C_FIELD_TEXT, 14));
				uk_text_l (clip, 10, y, HEAD, g_fr ? W.fr : W.en, any ? C_FIELD_TEXT : dim, 2);
				char t[16]; snprintf (t, sizeof t, "%d/%d", got, 3 * W.n); int tw = uk_tw (t);
				if (any) { star (clip, rw - tw - 22, y + HEAD / 2, 6, STAR_GOLD, true); uk_text_l (clip, rw - tw - 12, y, HEAD, t, dim); }
				else padlock (clip, rw - 18, y + HEAD / 2, dim);
				clip.fillRect (0, y + HEAD - 1, rw, 1, uk_mix (C_FIELD, C_FIELD_TEXT, 36));
			}
			y += HEAD;
			for (int l = 0; l < W.n; l++, y += ROW)
			{
				if (y <= -ROW || y >= clip.h) continue;
				Lv &L = W.lv[l];
				bool sel = w == g_curW && l == g_curL;
				unsigned ink = !L.open ? C_DIS : C_FIELD_TEXT;
				if (sel) { uk_hilite (clip, 3, y + 2, rw - 6, ROW - 4, 5); ink = uk_hilite_ink (); }
				int cx = 20, cy = y + ROW / 2;
				if (!L.open) { VPath d; d.circle (V (cx), V (cy), V (10)); d.fill (clip, uk_mix (C_FIELD, C_FIELD_TEXT, 26)); padlock (clip, cx, cy, uk_mix (C_FIELD, C_FIELD_TEXT, 110)); }
				else
				{
					VPath d; d.circle (V (cx), V (cy), V (10)); d.fill (clip, L.stars ? OK_GREEN : sel ? 0x00FFFFFF : uk_mix (C_FIELD, C_FIELD_TEXT, 70));
					char num[8]; snprintf (num, sizeof num, "%d", l + 1);
					UkFaceScope fs (g_small); uk_text_c (clip, cx - 10, cy - 10, 20, 20, num, L.stars ? 0x00FFFFFF : sel ? C_ACCENT : 0x00FFFFFF, 2);
				}
				char t[80], f[80]; snprintf (t, sizeof t, "%s", g_fr ? L.fr : L.en);
				uk_text_fit (t, rw - 38 - 54, f, sizeof f, sel ? 2 : 0);
				uk_text_l (clip, 36, y, ROW, f, ink, sel ? 2 : 0);
				if (L.open)
					for (int k = 0; k < 3; k++) star (clip, rw - 44 + k * 14, cy, 6, k < L.stars ? STAR_GOLD : uk_mix (sel ? C_ACCENT : C_FIELD, ink, 80), k < L.stars);
			}
		}
		if (scroll)
		{
			UkThumb th = uk_thumb (total (), height - 4, top, height - 4);
			uk_draw_vscroll (canvas, width - UK_SBW - 2, 2, UK_SBW, height - 4, th, C_FIELD);
		}
	}
};

// ---- the level's card (Turtle Quest's) -------------------------------------------------------------------------------
static const char *g_title = "3.3  Full adder";
static const char *g_text = "Add three bits: A + B + Cin. S is the sum's units bit, Cout the carry.";
static bool g_showHint = false;
static const char *g_hint = "";
class Card : public Widget
{
public:
	Card (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_rbox (canvas, 0, 0, width, height, 10, uk_mix (C_FIELD, 0xFFF6D8, 120), uk_mix (C_FIELD, 0xFFEFC2, 120));
		uk_rline (canvas, 0, 0, width, height, 10, 0xE0C77A);
		{ UkFaceScope fs (g_big); char f[120]; uk_text_fit (g_title, width - 28 - 180, f, sizeof f, 2); uk_text (canvas, 14, 7, f, 0x3A2E10, 2); }
		int y = 7 + g_big->height () + 3;
		wrap_draw (canvas, 14, y, width - 28 - 180, g_text, 0x3A3A3A, 3, uk_fh () + 2);
		if (g_showHint)
		{
			char h[400]; snprintf (h, sizeof h, "%s %s", TR2 ("Hint:", "Indice :"), g_hint);
			int n = wrap_lines (g_text, width - 28 - 180, 2);
			wrap_draw (canvas, 14, y + n * (uk_fh () + 2) + 2, width - 28, h, 0x8A5A00, 2, uk_fh () + 2);
		}
	}
};

// ---- the message bar (Turtle Quest's) ------------------------------------------------------------------------------
enum { M_NONE, M_INFO, M_OK, M_ERR };
static int g_msgKind = M_INFO, g_msgStars = 0;
static const char *g_msg = "";
static bool g_next = false;
class MsgBar : public Widget
{
public:
	MsgBar (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (g_msgKind == M_NONE) return;
		unsigned face = g_msgKind == M_OK ? 0xDFF3DA : g_msgKind == M_ERR ? 0xFBE0DC : 0xE4ECF7, edge = g_msgKind == M_OK ? 0x4E9A57 : g_msgKind == M_ERR ? 0xC8463B : 0x5B7FB5;
		uk_rbox (canvas, 0, 0, width, height, 10, face, face);
		uk_rline (canvas, 0, 0, width, height, 10, edge);
		int x = 14;
		if (g_msgKind == M_OK && g_msgStars) for (int k = 0; k < 3; k++) { star (canvas, x + 10, height / 2, 10, k < g_msgStars ? STAR_GOLD : 0xC9D6C5, true); x += 23; }
		else { VPath d; d.circle (V (x + 10), V (height / 2), V (10)); d.fill (canvas, edge); uk_text_c (canvas, x, height / 2 - 10, 20, 20, g_msgKind == M_ERR ? "!" : "i", 0xFFFFFF, 2); x += 26; }
		int room = width - x - 16 - (g_next ? 140 : 0);
		int st[3], ln[3]; int n = uk_text_wrap (g_msg, (int) strlen (g_msg), room, 2, st, ln);
		int lh = uk_fh () + 1, y = (height - n * lh) / 2;
		for (int i = 0; i < n; i++) { char l[300]; memcpy (l, g_msg + st[i], ln[i]); l[ln[i]] = 0; uk_text (canvas, x + 6, y + i * lh, l, uk_mix (edge, 0, 140)); }
	}
};

// ---- the truth table (the objective) --------------------------------------------------------------------------------
struct TT { int ni, no; const char *in[4], *out[4]; unsigned want[4]; bool checked; unsigned got[4]; int cur; int focusRow; };
static TT g_tt;
class TruthTable : public Widget
{
public:
	TruthTable (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	int need () { return 46 + (1 << g_tt.ni) * 21 + 8; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		int rows = 1 << g_tt.ni, nc = g_tt.ni + g_tt.no * (g_tt.checked ? 2 : 1);
		int mark = 26, cw = (width - 16 - mark) / nc; if (cw > 44) cw = 44;
		int tw = nc * cw + mark, x0 = (width - tw) / 2 + 4, RH = 21, y0 = 46;
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 130), grid = uk_mix (C_FIELD, C_FIELD_TEXT, 30);
		// the heads: the groups, then the names
		int xo = x0 + g_tt.ni * cw, xg = xo + g_tt.no * cw;
		{ UkFaceScope fs (g_small);
		  uk_text_c (canvas, x0, 6, g_tt.ni * cw, 14, g_tt.ni > 1 ? TR2 ("IN", "ENTRÉES") : TR2 ("IN", "ENTRÉE"), dim, 2);
		  uk_text_c (canvas, xo, 6, g_tt.no * cw, 14, TR2 ("GOAL", "BUT"), dim, 2);
		  if (g_tt.checked) uk_text_c (canvas, xg, 6, g_tt.no * cw, 14, TR2 ("YOURS", "OBTENU"), dim, 2); }
		for (int k = 0; k < g_tt.ni; k++) uk_text_c (canvas, x0 + k * cw, 22, cw, 20, g_tt.in[k], C_FIELD_TEXT, 2);
		for (int k = 0; k < g_tt.no; k++) uk_text_c (canvas, xo + k * cw, 22, cw, 20, g_tt.out[k], C_FIELD_TEXT, 2);
		if (g_tt.checked) for (int k = 0; k < g_tt.no; k++) uk_text_c (canvas, xg + k * cw, 22, cw, 20, g_tt.out[k], C_FIELD_TEXT, 2);
		canvas.fillRect (x0 - 4, y0 - 2, tw + 2, 1, uk_mix (C_FIELD, C_FIELD_TEXT, 70));
		for (int r = 0; r < rows; r++)
		{
			int y = y0 + r * RH;
			bool wrong = false; if (g_tt.checked) for (int k = 0; k < g_tt.no; k++) if (((g_tt.want[k] >> r) & 1) != ((g_tt.got[k] >> r) & 1)) wrong = true;
			if (g_tt.checked && wrong) uk_rbox (canvas, x0 - 4, y + 1, tw + 2, RH - 2, 4, 0x00FBE0DC, 0x00FBE0DC);
			else if (r & 1) canvas.fillRect (x0 - 4, y + 1, tw + 2, RH - 2, uk_mix (C_FIELD, C_FIELD_TEXT, 8));
			if (r == g_tt.cur) uk_rline (canvas, x0 - 4, y + 1, tw + 2, RH - 2, 4, wrong && g_tt.checked ? ERR_RED : C_ACCENT, 255);
			for (int k = 0; k < g_tt.ni; k++)
			{ int b = (r >> (g_tt.ni - 1 - k)) & 1; uk_text_c (canvas, x0 + k * cw, y, cw, RH, b ? "1" : "0", b ? C_FIELD_TEXT : dim); }
			for (int k = 0; k < g_tt.no; k++)
			{ int b = (g_tt.want[k] >> r) & 1; uk_text_c (canvas, xo + k * cw, y, cw, RH, b ? "1" : "0", b ? C_FIELD_TEXT : dim, 2); }
			if (g_tt.checked)
			{
				for (int k = 0; k < g_tt.no; k++)
				{
					int b = (g_tt.got[k] >> r) & 1, wb = (g_tt.want[k] >> r) & 1;
					uk_text_c (canvas, xg + k * cw, y, cw, RH, b ? "1" : "0", b != wb ? ERR_RED : b ? C_FIELD_TEXT : dim, 2);
				}
				int mx = x0 + nc * cw + mark / 2 - 2, my = y + RH / 2;
				if (wrong) { VPath d; d.circle (V (mx), V (my), V (7)); d.fill (canvas, ERR_RED); uk_glyph (canvas, WKG_CLOSE, mx, my, 8, 0x00FFFFFF); }
				else uk_glyph (canvas, WKG_CHECK, mx, my, 11, OK_GREEN);
			}
			else if (r == g_tt.cur) uk_glyph (canvas, WKG_LEFT, x0 + nc * cw + mark / 2 - 2, y + RH / 2, 9, C_ACCENT);
		}
		canvas.fillRect (xo - 2, 22, 1, rows * RH + 24, grid);
		if (g_tt.checked) canvas.fillRect (xg, 22, 1, rows * RH + 24, grid);
	}
};

// ---- the bench under the table: gate count, stars needed --------------------------------------------------------
static int g_par3 = 5, g_par2 = 6;
class Count : public Widget
{
public:
	Count (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int n = gate_count ();
		char t[16]; snprintf (t, sizeof t, "%d", n);
		{ UkFaceScope fs (g_huge); uk_text (canvas, 4, 0, t, n <= g_par3 ? OK_GREEN : C_TEXT, 2); }
		int x = 4 + 6 + [&] { UkFaceScope fs (g_huge); return uk_tw (t, 2); } ();
		uk_text (canvas, x, 4, n == 1 ? TR2 ("gate", "porte") : TR2 ("gates", "portes"), C_TEXT, 2);
		char a[40], b[40]; snprintf (a, sizeof a, "\xE2\x89\xA4 %d", g_par3); snprintf (b, sizeof b, "\xE2\x89\xA4 %d", g_par2);
		int y = 22; unsigned dim = uk_mix (C_BG, C_TEXT, 170);
		for (int k = 0; k < 3; k++) star (canvas, x + 6 + k * 12, y + 8, 5, STAR_GOLD, true);
		uk_text (canvas, x + 42, y, a, dim);
		int x2 = x + 42 + uk_tw (a) + 14;
		for (int k = 0; k < 2; k++) star (canvas, x2 + 6 + k * 12, y + 8, 5, STAR_GOLD, true);
		uk_text (canvas, x2 + 30, y, b, dim);
		char d[80]; snprintf (d, sizeof d, TR2 ("Depth %d \xC2\xB7 Step (F8) to watch it", "Profondeur %d \xC2\xB7 Pas à pas (F8)"), g_maxDepth);
		if (n) uk_text (canvas, 4, 46, d, dim);
	}
};

// ---- the overlay cards: a lesson, a result -----------------------------------------------------------------------------
static int g_lessonGate = T_AND;
class LessonCard : public Widget
{
public:
	Button *ok;
	LessonCard (int l, int t, int w, int h) : Widget (l, t, w, h)
	{ ok = new Button (w - 120, h - 46, 104, 30, "OK"); addChild (ok); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_rbox (canvas, 0, 0, width, height, 14, 0xFFFFFF, 0xF6F4FB);
		uk_rline (canvas, 0, 0, width, height, 14, 0x8E6CC8);
		uk_rbox (canvas, 0, 0, width, 44, 14, 0x8E6CC8, 0x7A58B8, 255, UK_TL | UK_TR);
		char t[120]; snprintf (t, sizeof t, "%s  %s", TR2 ("New gate:", "Nouvelle porte :"), tname (g_lessonGate));
		{ UkFaceScope fs (g_big); uk_text_l (canvas, 18, 0, 44, t, 0xFFFFFF, 2); }
		// the symbol, big, with its two inputs and its output
		{
			int L = 46, T = 70, sc = 18;
			double l = (L + sc) * 16, r = (L + sc * 4.1) * 16, cy = (T + 2 * sc) * 16, h2 = sc * 16 * 1.45;
			int xy[200]; int m = gate_outline (g_lessonGate, l, cy - h2, r, cy + h2, sc * 16 * 0.3, xy);
			VPath s1; s1.line (V (L - 8), V (T + sc), V (L + sc + 4), V (T + sc), V (2) + 8); s1.line (V (L - 8), V (T + 3 * sc), V (L + sc + 4), V (T + 3 * sc), V (2) + 8);
			s1.line ((int) r - V (2), (int) cy, V (L + sc * 5 + 10), (int) cy, V (2) + 8); s1.fill (canvas, WIRE_0);
			VPath f; f.poly (xy, m); f.fill (canvas, GATE_FACE); VPath o; o.polyline (xy, m, V (2), true); o.fill (canvas, GATE_INK);
			uk_text_l (canvas, L - 24, T + sc - 10, 20, "A", C_TEXT, 2); uk_text_l (canvas, L - 24, T + 3 * sc - 10, 20, "B", C_TEXT, 2);
			uk_text_l (canvas, L + sc * 5 + 16, (int) (cy / 16) - 10, 20, TR2 ("Out", "S"), C_TEXT, 2);
			{ UkFaceScope fs (g_small); uk_text_c (canvas, L + sc + 2, (int) (cy / 16) - 8, sc * 2, 16, tname (g_lessonGate), GATE_INK, 2); }
		}
		// its truth table
		int tx = 236, ty = 60, cw = 34, RH = 21;
		uk_rbox (canvas, tx - 8, ty - 4, 3 * cw + 16, 5 * RH + 10, 6, 0x00F1ECFA, 0x00F1ECFA);
		const char *hd[3] = { "A", "B", TR2 ("Out", "S") };
		for (int k = 0; k < 3; k++) uk_text_c (canvas, tx + k * cw, ty, cw, RH, hd[k], 0x004B2E83, 2);
		canvas.fillRect (tx, ty + RH, 3 * cw, 1, 0x00C9B8E6);
		for (int r = 0; r < 4; r++)
		{
			int a = r >> 1, bb = r & 1, o = a & bb;
			char s[3][2] = { { (char) ('0' + a), 0 }, { (char) ('0' + bb), 0 }, { (char) ('0' + o), 0 } };
			for (int k = 0; k < 3; k++) uk_text_c (canvas, tx + k * cw, ty + RH + 2 + r * RH, cw, RH, s[k], k == 2 && o ? WIRE_1 : 0x00303030, k == 2 ? 2 : 0);
		}
		const char *txt = TR2 ("An AND gate gives 1 only when both its inputs are 1 \xE2\x80\x94 like a lamp behind two switches in a row: both must be on.\n"
				       "Take one from the palette, wire A and B to its inputs, its output to the lamp.",
				       "Une porte ET donne 1 seulement quand ses deux entrées valent 1 \xE2\x80\x94 comme une lampe derrière deux interrupteurs à la suite : il faut les deux.\n"
				       "Pose-la depuis la palette, relie A et B à ses entrées, sa sortie à la lampe.");
		wrap_draw (canvas, 20, 196, width - 40, txt, 0x00303030, 5, uk_fh () + 3);
	}
};

class ResultCard : public Widget
{
public:
	Button *again, *next;
	ResultCard (int l, int t, int w, int h) : Widget (l, t, w, h)
	{
		again = new Button (18, h - 46, 170, 30, TR2 ("Try for \xE2\x98\x85\xE2\x98\x85\xE2\x98\x85", "Viser \xE2\x98\x85\xE2\x98\x85\xE2\x98\x85")); addChild (again);
		next = new Button (w - 158, h - 46, 140, 30, TR2 ("Next level", "Niveau suivant")); addChild (next);
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_rbox (canvas, 0, 0, width, height, 14, 0xFFFFFF, 0xF3FAF3);
		uk_rline (canvas, 0, 0, width, height, 14, 0x4E9A57);
		uk_rbox (canvas, 0, 0, width, 44, 14, 0x55A85E, 0x3E8E48, 255, UK_TL | UK_TR);
		{ UkFaceScope fs (g_big); uk_text_l (canvas, 18, 0, 44, TR2 ("Level complete!", "Niveau réussi !"), 0xFFFFFF, 2); }
		int cx = width / 2;
		for (int k = 0; k < 3; k++) star (canvas, cx - 52 + k * 52, 86, k == 1 ? 26 : 22, k < 2 ? STAR_GOLD : 0x00D5DDD3, true);
		{ UkFaceScope fs (g_big); uk_text_c (canvas, 0, 120, width, 26, TR2 ("4 gates \xE2\x80\x94 every row right", "4 portes \xE2\x80\x94 toutes les lignes justes"), 0x00264F2C, 2); }
		const char *t = TR2 ("Three stars need 3 gates or fewer. The best known circuit is smaller: can you find it?",
				     "Trois étoiles : 3 portes ou moins. Le meilleur circuit connu est plus petit : sauras-tu le trouver ?");
		int st[3], ln[3]; int n = uk_text_wrap (t, (int) strlen (t), width - 50, 3, st, ln);
		for (int i = 0; i < n; i++) { char l[200]; memcpy (l, t + st[i], ln[i]); l[ln[i]] = 0; uk_text_c (canvas, 0, 152 + i * 19, width, 19, l, 0x00405040); }
	}
};

// ---- the palette's icons (an app ToolIconFn): the select arrow, the gates' symbols ---------------------------------
enum { IC_SELECT = 100, IC_CHECK, IC_STEP, IC_RESET };
static void tool_icon (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool off)
{
	(void) off;
	if (id == IC_SELECT)
	{
		if (size >= 30) { UkFaceScope fs (g_small); uk_text_c (cv, x - 8, y + size - 12, size + 16, 12, TR2 ("Select", "Choisir"), ink, 0); x += size / 2 - 8; size = 22; }
		int px = x + size / 4, py = y + 1;
		int pts[] = { V (px), V (py), V (px), V (py + 15), V (px + 4), V (py + 11), V (px + 7), V (py + 17), V (px + 9), V (py + 16), V (px + 6), V (py + 10), V (px + 11), V (py + 10) };
		VPath a; a.poly (pts, 7); a.fill (cv, ink);
		return;
	}
	if (id == IC_CHECK) { uk_glyph (cv, WKG_CHECK, x + size / 2, y + size / 2, size - 2, ink); return; }
	if (id == IC_STEP)	// a bar then a triangle: one step
	{
		VPath p; int pts[] = { V (x + 5), V (y + 3), V (x + size - 4), V (y + size / 2), V (x + 5), V (y + size - 3) };
		p.poly (pts, 3); p.rect (V (x + 1), V (y + 3), V (3), V (size - 6)); p.fill (cv, ink); return;
	}
	if (id == IC_RESET) { uk_tool_glyph (cv, WKT_TO_START, x, y, size, ink); return; }
	// a gate: its symbol in the box's top, its name under it
	if (size >= 30)
	{
		{ UkFaceScope fs (g_small); const char *nm = tname (id);	// (a name wider than the button: the 9-px face)
		  if (uk_tw (nm) > size) { UkFaceScope ft (g_tiny); uk_text_c (cv, x - 8, y + size - 12, size + 16, 12, nm, ink, 0); }
		  else uk_text_c (cv, x - 8, y + size - 12, size + 16, 12, nm, ink, 0); }
		int s2 = size * 2 / 3; x += (size - s2) / 2; y += 1; size = s2;
	}
	double s = size * 16.0;
	double L = x * 16 + s * 0.22, R = x * 16 + s * 0.86, cy = y * 16 + s / 2, h2 = s * 0.30;
	int xy[200]; int m = gate_outline (id, L, cy - h2, R, cy + h2, s * 0.07, xy);
	VPath st;
	if (id == T_NOT) st.line (x * 16, (int) cy, (int) L + 8, (int) cy, V (1) + 4);
	else { st.line (x * 16, (int) (cy - h2 * 0.55), (int) L + 24, (int) (cy - h2 * 0.55), V (1) + 4); st.line (x * 16, (int) (cy + h2 * 0.55), (int) L + 24, (int) (cy + h2 * 0.55), V (1) + 4); }
	st.line ((int) R - 8, (int) cy, (x + size) * 16, (int) cy, V (1) + 4);
	st.fill (cv, ink);
	VPath f; f.poly (xy, m); f.fill (cv, 0x00FFFFFF);
	VPath o; o.polyline (xy, m, V (1) + 6, true); o.fill (cv, ink);
	if (id == T_XOR) { int bx[40]; int k = qbez (bx, 0, L - s * 0.0, cy + h2, L + (R - L) * 0.22, cy, L, cy - h2, 8, false); VPath b; b.polyline (bx, k, V (1) + 6); b.fill (cv, ink); }
	if (id == T_NOT || id == T_NAND || id == T_NOR) { VPath b; b.circle ((int) (R - s * 0.07), (int) cy, (int) (s * 0.07)); b.fill (cv, ink); VPath bi; bi.circle ((int) (R - s * 0.07), (int) cy, (int) (s * 0.07) - 16); bi.fill (cv, 0x00FFFFFF); }
}

// ---- the window --------------------------------------------------------------------------------------------------
static unsigned g_parts = (1 << T_NOT) | (1 << T_AND) | (1 << T_OR) | (1 << T_XOR) | (1 << T_NAND) | (1 << T_NOR);
static int g_armed = -1;				// the palette's tool lit: -1 Select
static bool g_stepOn = false;
static int g_lesson = 0;				// 1: the lesson card shown; 2: the result card

static int window ()
{
	const int W = getenv ("MOCK_W") ? atoi (getenv ("MOCK_W")) : 1000, H = getenv ("MOCK_H") ? atoi (getenv ("MOCK_H")) : 620, PAD = 10, LISTW = 214, RIGHTW = 238, TBH = 46, MSGH = 50; int CARDH = 66;
	evaluate ();
	Root root (W, H, "Circuits");
	root.setResizable (true);
	root.setMinSize (920, 560);
	int mid = PAD + LISTW + PAD, rx = W - PAD - RIGHTW, mw = rx - PAD - mid;
	LevelList *list = new LevelList (PAD, PAD, LISTW, H - 2 * PAD); root.addChild (list);
	{ int maxTop = list->total () - (H - 2 * PAD - 4); if (maxTop < 0) maxTop = 0; list->top = g_curW ? maxTop : 0; }
	// the card
	{ int n = wrap_lines (g_text, mw - 28 - 180, 3); int need = 7 + g_big->height () + 3 + n * (uk_fh () + 2) + 8; if (need > CARDH) CARDH = need; }
	Card *card = new Card (mid, PAD, mw, CARDH); root.addChild (card);
	Button *hint = new Button (mid + mw - 176, PAD + 10, 80, 28, TR2 ("Hint", "Indice")); root.addChild (hint);
	Button *lesson = new Button (mid + mw - 90, PAD + 10, 80, 28, TR2 ("Lesson", "Leçon")); root.addChild (lesson);
	((Widget *) hint)->tip = "F2"; ((Widget *) lesson)->tip = "F1";
	// the palette
	int ty = PAD + CARDH + 6;
	ToolBar *tb = new ToolBar (mid, ty, mw, TBH); root.addChild (tb);
	ToolButton *sel = (new ToolButton (40, 40, TR2 ("Select and move (Esc)", "Choisir et déplacer (Échap)")))->setIcon (tool_icon, IC_SELECT)->setToggle (true, g_armed < 0);
	sel->iconSize = 36; tb->add (sel, 0);
	tb->sep ();
	int ng = 0;
	for (int t = T_NOT; t <= T_NOR; t++)
	{
		if (!(g_parts & (1u << t))) continue;
		static char tips[8][80]; snprintf (tips[t], sizeof tips[t], "%s %s (%d)", TR2 ("Put a gate:", "Poser une porte"), tname (t), ++ng);
		ToolButton *b = (new ToolButton (40, 40, tips[t]))->setIcon (tool_icon, t)->setToggle (true, g_armed == t);
		b->iconSize = 36; tb->add (b, 1);
	}
	if (!ng) { Label *l = new Label (0, 0, 290, 30, TR2 ("No gate in this level: a wire is enough.", "Pas de porte ici : un fil suffit."), C_DIS); tb->add (l, 8); }
	ToolButton *redo = (new ToolButton (34, 34, TR2 ("Redo (Ctrl+Y)", "Rétablir (Ctrl+Y)")))->setGlyph (WKT_REDO); tb->addRight (redo, 0);
	ToolButton *undo = (new ToolButton (34, 34, TR2 ("Undo (Ctrl+Z)", "Annuler (Ctrl+Z)")))->setGlyph (WKT_UNDO); tb->addRight (undo, 0);
	ToolButton *del = (new ToolButton (34, 34, TR2 ("Delete (Del)", "Supprimer (Suppr)")))->setGlyph (WKT_TRASH); tb->addRight (del, 4);
	if (!strcmp (g_scene, "empty")) { undo->setDisabled (true); redo->setDisabled (true); del->setDisabled (true); }
	else redo->setDisabled (true);
	// the board, the message
	int by = ty + TBH + 6, bh = H - PAD - MSGH - 8 - by;
	g_board = new Board (mid, by, mw, bh); root.addChild (g_board);
	MsgBar *msg = new MsgBar (mid, H - PAD - MSGH, mw, MSGH); root.addChild (msg);
	if (g_next) { Button *nx = new Button (mid + mw - 140, H - PAD - MSGH + 10, 126, 30, TR2 ("Next level", "Niveau suivant")); root.addChild (nx); }
	// the right column: the objective, the count, the simulation
	Label *tl = new Label (rx, PAD, RIGHTW, 22, TR2 ("Truth table", "Table de vérité"), C_TEXT); root.addChild (tl);
	TruthTable *tt = new TruthTable (rx, PAD + 24, RIGHTW, 10);
	int tth = tt->need (); tt->resizeTo (RIGHTW, tth); root.addChild (tt);
	int cy = PAD + 24 + tth + 10;
	Count *cnt = new Count (rx, cy, RIGHTW, 66); root.addChild (cnt);
	int sy = H - PAD - 36 - 8 - 32;
	ToolButton *step = (new ToolButton ((RIGHTW - 6) / 2, 32, TR2 ("One gate depth more (F8)", "Une profondeur de plus (F8)")))->setIcon (tool_icon, IC_STEP)->setText (TR2 ("Step", "Pas à pas"))->setToggle (true, g_stepOn);
	step->raised = true; step->left = rx; step->top = sy; root.addChild (step);
	ToolButton *reset = (new ToolButton ((RIGHTW - 6) / 2, 32, TR2 ("Back to step 0 (F9)", "Retour au pas 0 (F9)")))->setIcon (tool_icon, IC_RESET)->setText (TR2 ("Reset", "Au départ"));
	reset->raised = true; reset->left = rx + (RIGHTW + 6) / 2; reset->top = sy; root.addChild (reset);
	if (!g_stepOn) reset->setDisabled (true);
	ToolButton *check = (new ToolButton (RIGHTW, 36, TR2 ("Check every row (F5)", "Vérifier toutes les lignes (F5)")))->setIcon (tool_icon, IC_CHECK)->setText (TR2 ("Check", "Vérifier"));
	check->filled = true; check->setOn (true); check->left = rx; check->top = H - PAD - 36; root.addChild (check);
	{ static char st[80]; snprintf (st, sizeof st, TR2 ("Step %d of %d \xC2\xB7 F7: back to live", "Pas %d sur %d \xC2\xB7 F7 : en direct"), g_step, g_maxDepth);
	  Label *k = new Label (rx, sy - 22, RIGHTW, 18, g_stepOn ? st : TR2 ("Live: click a switch or a row.", "En direct : clique une entrée."), g_stepOn ? C_ACCENT : C_DIS); root.addChild (k); }
	// the overlays
	if (g_lesson == 1)
	{
		int lw = 500, lh = 330; LessonCard *lc = new LessonCard (mid + (mw - lw) / 2, by + (bh - lh) / 2, lw, lh); root.addChild (lc);
	}
	if (g_lesson == 2)
	{
		int lw = 420, lh = 250; ResultCard *rc = new ResultCard (mid + (mw - lw) / 2, by + (bh - lh) / 2, lw, lh); root.addChild (rc);
	}
	evaluate ();
	if (!strcmp (g_scene, "dialog"))
		ft_messagebox (TR2 ("Open Level Pack", "Ouvrir un recueil"),
			TR2 ("\xE2\x80\x9C" "extra.circuits\xE2\x80\x9D cannot be opened:\nline 42: the table has 3 rows, 4 expected.\n\nNo level was added.",
			     "\xE2\x80\x9C" "extra.circuits\xE2\x80\x9D ne peut pas être ouvert :\nligne 42 : la table a 3 lignes, 4 attendues.\n\nAucun niveau n'a été ajouté."), MB_OK);
	root.run ();
	return 0;
}

// ---- the scenes -------------------------------------------------------------------------------------------------
static void full_adder (int a, int b, int cin, bool wireCout = true)
{
	const char *in[3] = { "A", "B", "Cin" }; int v[3] = { a, b, cin };
	const char *out[2] = { "Cout", "S" };
	int sA = add_inputs (in, 3, v), sB = sA + 1, sC = sA + 2;
	int lC = add_outputs (out, 2), lS = lC + 1;
	int A1 = add_part (T_AND, 10, 4), X1 = add_part (T_XOR, 10, 14), O1 = add_part (T_OR, 28, 5), A2 = add_part (T_AND, 20, 15), X2 = add_part (T_XOR, 20, 24);
	add_wire (sA, A1, 0, 7); add_wire (sB, A1, 1, 8); add_wire (sB, X1, 0, 8); add_wire (sA, X1, 1, 7);
	add_wire (X1, A2, 0, 18); add_wire (X1, X2, 1, 18); add_wire (sC, X2, 0, 17); add_wire (sC, A2, 1, 17);
	add_wire (A1, O1, 0, 26); add_wire (A2, O1, 1, 26);
	if (wireCout) add_wire (O1, lC, 0, 33);
	add_wire (X2, lS, 0, 30);
	(void) A1; (void) lS;
	g_tt.ni = 3; g_tt.no = 2; g_tt.in[0] = "A"; g_tt.in[1] = "B"; g_tt.in[2] = "Cin"; g_tt.out[0] = "Cout"; g_tt.out[1] = "S";
	g_tt.want[0] = 0xE8; g_tt.want[1] = 0x96; g_tt.cur = a * 4 + b * 2 + cin;
	g_par3 = 5; g_par2 = 6;
	g_title = TR2 ("3.3  Full adder", "3.3  Additionneur complet");
	g_text = TR2 ("Add three bits: A + B + Cin. S is the units bit of the sum, Cout its carry.",
		      "Additionne trois bits : A + B + Cin. S est le chiffre des unités, Cout la retenue.");
}
static void xor_level (bool four, int a, int b)		// 2.2 The hallway light: the OR mistake, or a 4-gate answer
{
	const char *in[2] = { "A", "B" }; int v[2] = { a, b }; const char *out[1] = { TR2 ("Out", "S") };
	int sA = add_inputs (in, 2, v), sB = sA + 1, l = add_outputs (out, 1);
	if (!four)
	{
		int o = add_part (T_OR, 18, 13);
		add_wire (sA, o, 0, 12); add_wire (sB, o, 1, 12); add_wire (o, l, 0, 28);
	}
	else
	{
		int o = add_part (T_OR, 13, 6), n1 = add_part (T_AND, 13, 19), n2 = add_part (T_NOT, 20, 19), g = add_part (T_AND, 27, 13);
		add_wire (sA, o, 0, 9); add_wire (sB, o, 1, 10); add_wire (sA, n1, 0, 9); add_wire (sB, n1, 1, 10);
		add_wire (n1, n2, 0, 19); add_wire (o, g, 0, 23); add_wire (n2, g, 1, 26); add_wire (g, l, 0, 33);
	}
	g_parts = (1 << T_NOT) | (1 << T_AND) | (1 << T_OR) | (1 << T_NAND) | (1 << T_NOR);
	g_tt.ni = 2; g_tt.no = 1; g_tt.in[0] = "A"; g_tt.in[1] = "B"; g_tt.out[0] = TR2 ("Out", "S"); g_tt.want[0] = 0x6; g_tt.cur = a * 2 + b;
	g_par3 = 3; g_par2 = 5;
	g_curW = 1; g_curL = 1;
	g_title = TR2 ("2.2  The hallway light", "2.2  La lumière du couloir");
	g_text = TR2 ("A switch at each end of the hallway: the lamp is on when exactly one switch is on.",
		      "Un interrupteur à chaque bout du couloir : la lampe s'allume quand exactement un des deux est en marche.");
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	g_big = new FtTextFace; if (!g_big->open ("DejaVu Sans", 18)) g_big = 0;
	g_small = new FtTextFace; if (!g_small->open ("DejaVu Sans", 10)) g_small = 0;
	g_huge = new FtTextFace; if (!g_huge->open ("DejaVu Sans", 30)) g_huge = 0;
	g_tiny = new FtTextFace; if (!g_tiny->open ("DejaVu Sans", 9)) g_tiny = 0;
	const char *m = getenv ("MOCK"); if (m) g_scene = m;
	const char *lg = getenv ("MOCK_LANG"); g_fr = lg && !strcmp (lg, "fr");
	const char *s = g_scene;
	progress_fresh (2, 2);
	g_msgKind = M_INFO;
	if (!strcmp (s, "main") || !strcmp (s, "dialog"))
	{
		full_adder (1, 0, 1);
		g_p[g_np - 2].sel = true;		// A2 selected
		g_msg = TR2 ("Every lamp follows the switches. When you think it is right: Check (F5).",
			     "Les lampes suivent les interrupteurs. Quand tu crois que c'est juste : Vérifier (F5).");
	}
	else if (!strcmp (s, "place"))
	{
		full_adder (1, 0, 1);
		// remove A2, O1, X2 and their wires: the board half built
		g_np -= 3; g_nw = 4;		// A1 and X1 placed and wired; O1, A2, X2 not yet
		g_ghost.on = true; g_ghost.type = T_AND; g_ghost.x = 20; g_ghost.y = 15; g_ghost.ok = true;
		g_armed = T_AND;
		g_msg = TR2 ("AND: click a free place on the board to put it (Esc: cancel).", "ET : clique une place libre du plateau pour la poser (Échap : annuler).");
	}
	else if (!strcmp (s, "wire"))
	{
		full_adder (1, 0, 1);
		g_np -= 1;			// X2 not yet; X1 -> A2.1 being drawn
		{ Wire keep[] = { g_w[0], g_w[1], g_w[2], g_w[3], g_w[7], g_w[8], g_w[9], g_w[10] }; g_nw = 8; memcpy (g_w, keep, sizeof keep); }
		g_rub.on = true; g_rub.src = 6; g_rub.gx = 19.6; g_rub.gy = 16.3; g_rub.tgt = 8; g_rub.tpin = 0; g_rub.ok = true;
		g_msg = TR2 ("Let go on an input pin to lay the wire (Esc: cancel).", "Lâche sur une borne d'entrée pour poser le fil (Échap : annuler).");
	}
	else if (!strcmp (s, "step"))
	{
		full_adder (1, 0, 1);
		g_step = 2; g_stepOn = true;
		g_msg = TR2 ("Step 2 of 3: the gates of depth 2 are computed. F8: the next depth \xC2\xB7 F9: back to the start \xC2\xB7 F7: live.",
			     "Pas 2 sur 3 : les portes de profondeur 2 sont calculées. F8 : la suivante \xC2\xB7 F9 : au départ \xC2\xB7 F7 : en direct.");
	}
	else if (!strcmp (s, "refused"))
	{
		full_adder (1, 0, 1, false);
		g_p[3].err = true;
		g_msgKind = M_ERR;
		g_msg = TR2 ("Lamp Cout is not connected: wire a gate's output to it, then Check again.",
			     "La lampe Cout n'est pas reliée : relie-lui la sortie d'une porte, puis vérifie encore.");
	}
	else if (!strcmp (s, "check"))
	{
		xor_level (false, 1, 1);
		g_tt.checked = true; g_tt.got[0] = 0xE;
		g_msgKind = M_ERR;
		g_msg = TR2 ("1 row is wrong: with A = 1 and B = 1 the lamp must stay off. The switches are set on that row.",
			     "1 ligne est fausse : avec A = 1 et B = 1 la lampe doit rester éteinte. Les interrupteurs sont mis sur cette ligne.");
	}
	else if (!strcmp (s, "won"))
	{
		xor_level (true, 1, 0);
		g_tt.checked = true; g_tt.got[0] = 0x6;
		g_worlds[1].lv[1].stars = 2;
		g_msgKind = M_OK; g_msgStars = 2; g_next = true;
		g_msg = TR2 ("Well done! 4 gates. Three stars need 3.", "Bravo ! 4 portes. Trois étoiles : 3.");
		g_lesson = 2;
	}
	else if (!strcmp (s, "lesson"))
	{
		const char *in[2] = { "A", "B" }; int v[2] = { 0, 0 }; const char *out[1] = { TR2 ("Out", "S") };
		add_inputs (in, 2, v); add_outputs (out, 1);
		g_parts = (1 << T_NOT) | (1 << T_AND);
		g_tt.ni = 2; g_tt.no = 1; g_tt.in[0] = "A"; g_tt.in[1] = "B"; g_tt.out[0] = TR2 ("Out", "S"); g_tt.want[0] = 0x8; g_tt.cur = 0;
		g_par3 = 1; g_par2 = 1; progress_fresh (0, 2); g_curW = 0; g_curL = 2;
		g_title = TR2 ("1.3  Both at once", "1.3  Les deux à la fois");
		g_text = TR2 ("The lamp must light only when both switches are on.", "La lampe doit s'allumer seulement quand les deux interrupteurs sont en marche.");
		g_lesson = 1;
		g_msg = TR2 ("New gate: AND. Read the card, then build.", "Nouvelle porte : ET. Lis la carte, puis construis.");
	}
	else if (!strcmp (s, "empty"))
	{
		const char *in[1] = { "A" }; int v[1] = { 0 }; const char *out[1] = { TR2 ("Out", "S") };
		add_inputs (in, 1, v); add_outputs (out, 1);
		g_parts = 0; g_hintEmpty = true;
		g_tt.ni = 1; g_tt.no = 1; g_tt.in[0] = "A"; g_tt.out[0] = TR2 ("Out", "S"); g_tt.want[0] = 0x2; g_tt.cur = 0;
		g_par3 = 0; g_par2 = 0; progress_fresh (0, 0); g_curW = 0; g_curL = 0;
		g_title = TR2 ("1.1  First light", "1.1  Première lumière");
		g_text = TR2 ("Make the lamp show what the switch says: on when A is on.", "La lampe doit montrer ce que dit l'interrupteur : allumée quand A est en marche.");
		g_msg = TR2 ("Welcome to Circuits! Wire the switch A to the lamp, then Check (F5).", "Bienvenue dans Circuits ! Relie l'interrupteur A à la lampe, puis Vérifier (F5).");
	}
	else if (!strcmp (s, "levels"))
	{
		progress_fresh (1, 3);
		g_worlds[0].lv[5].stars = 2; g_worlds[0].lv[7].stars = 2; g_worlds[1].lv[1].stars = 2;
		g_curW = 1; g_curL = 3;
		Root root (244, 640, TR2 ("Levels", "Niveaux"));
		LevelList *l = new LevelList (10, 10, 224, 620); root.addChild (l);
		root.run ();
		return 0;
	}
	return window ();
}
