//
// gates.h -- Circuits' drawing pieces (the window's, included by main.cpp only): the colours of the board, the
// faces, the gates' symbols (the ANSI "distinctive shapes", their curves sampled with integer arithmetic into
// VPath polygons, 1/16 px), the stars and the padlock (Turtle Quest's), the wrapped text, and the icons of the
// palette and of the bench's buttons (a ToolIconFn).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _circuits_gates_h
#define _circuits_gates_h

// ---- the colours (04-ux-design §11): the board is theme-independent, as Turtle Quest's ----------------------------
static const unsigned BOARD_BG   = 0x00EEF3EF;	// the board: a pale green paper
static const unsigned BOARD_DOT  = 0x00C9D4CC;	// the grid's dots
static const unsigned STRIP_BG   = 0x00E2EAE4;	// the fixed columns (switches, lamps)
static const unsigned WIRE_1     = 0x0021A346;	// a wire at 1: lit green
static const unsigned WIRE_0     = 0x00334A5E;	// a wire at 0: dark slate
static const unsigned WIRE_X     = 0x00A9B3BC;	// not computed yet (step mode): grey, dashed
static const unsigned WIRE_OPEN  = 0x00808A94;	// a pin with no wire
static const unsigned GATE_INK   = 0x002B3440;	// the gates' outline and names
static const unsigned GATE_FACE  = 0x00FFFFFF;
static const unsigned GATE_FACE1 = 0x00E3F5E7;	// a gate whose output is 1
static const unsigned GATE_FACEX = 0x00F0F2F4;	// not computed yet (step mode)
static const unsigned LAMP_ON    = 0x00FFD23F, LAMP_OFF = 0x005B5648;
static const unsigned ERR_RED    = 0x00D0342C, OK_GREEN = 0x003E9B4F, STAR_GOLD = 0x00F2B705;

// ---- the faces: the UI's (FontKit's, 13 px), a title's 18, the small 10 (gates, palette), the count's 30, a tiny 9 ----
static FtTextFace *g_big, *g_small, *g_huge, *g_tiny;
static FtTextFace *open_face (int px) { FtTextFace *f = new FtTextFace; if (f->open ("DejaVu Sans", px)) return f; delete f; return 0; }
static int face_h (TextFace *f) { return f ? f->height () : uk_fh (); }

// ---- the gates' names on screen (the pack and the circuit text keep the English ones: the engine's) -----------------
static const char *const GATE_WORD[P_COUNT_] = { "", "", TRN ("NOT"), TRN ("AND"), TRN ("OR"), TRN ("XOR"), TRN ("NAND"), TRN ("NOR") };
static const char *gate_word (int t) { return t >= P_NOT && t < P_COUNT_ ? TR (GATE_WORD[t]) : ""; }

// ---- the gates' outlines -------------------------------------------------------------------------------------------
// A quadratic curve (x0, y0) .. (x2, y2) pulled by (x1, y1), sampled at steps + 1 points (skipFirst: its first
// point is the previous curve's last) into xy from n -> the new n. Coordinates in 1/16 px.
static int qbez (int *xy, int n, int x0, int y0, int x1, int y1, int x2, int y2, int steps, bool skipFirst)
{
	int d = steps * steps;
	for (int i = skipFirst ? 1 : 0; i <= steps; i++)
	{
		int a = (steps - i) * (steps - i), b = 2 * (steps - i) * i, c = i * i;
		xy[2 * n] = (a * x0 + b * x1 + c * x2 + d / 2) / d;
		xy[2 * n + 1] = (a * y0 + b * y1 + c * y2 + d / 2) / d;
		n++;
	}
	return n;
}
// The body of a gate of `type` in the box (L, T)-(R, B), the bubble's radius rb (NOT, NAND, NOR: the body ends
// before it): its closed outline's points in xy (room for 40) -> how many.
static int gate_outline (int type, int L, int T, int R, int B, int rb, int *xy)
{
	int cy = (T + B) / 2, h2 = (B - T) / 2, n = 0;
	bool bub = type == P_NOT || type == P_NAND || type == P_NOR;
	int Rb = bub ? R - 2 * rb : R;
	switch (type)
	{
	case P_NOT:
		xy[0] = L; xy[1] = cy - h2 * 78 / 100; xy[2] = Rb; xy[3] = cy; xy[4] = L; xy[5] = cy + h2 * 78 / 100;
		return 3;
	case P_AND: case P_NAND:			// a flat back, a half disc in front
	{
		int r = h2, cx = Rb - r;
		xy[2 * n] = L; xy[2 * n + 1] = T; n++;
		for (int a = -90; a <= 90; a += 10)
		{
			xy[2 * n] = cx + (int) ((long long) r * uk_cos (a) / 16384);
			xy[2 * n + 1] = cy + (int) ((long long) r * uk_sin (a) / 16384);
			n++;
		}
		xy[2 * n] = L; xy[2 * n + 1] = B; n++;
		return n;
	}
	default:					// OR, NOR, XOR: the shield (XOR's moved 12 % right: its back curve before it)
	{
		int l = type == P_XOR ? L + (R - L) * 12 / 100 : L;
		n = qbez (xy, n, l, T, l + (Rb - l) * 58 / 100, T, Rb, cy, 10, false);
		n = qbez (xy, n, Rb, cy, l + (Rb - l) * 58 / 100, B, l, B, 10, true);
		n = qbez (xy, n, l, B, l + (Rb - l) * 26 / 100, cy, l, T, 8, true);
		return n - 1;				// (the last point is the first)
	}
	}
}
// A gate's symbol drawn in the box (L, T)-(R, B) (1/16 px): its body in `face`, its outline (sw wide) in `ink`, XOR's
// back curve, the bubble. alpha: the ghost's.
static void draw_gate_shape (Canvas &cv, int type, int L, int T, int R, int B, int rb, unsigned face, unsigned ink, int sw, int alpha = 255)
{
	int xy[80], m = gate_outline (type, L, T, R, B, rb, xy), cy = (T + B) / 2;
	{ VPath b; b.poly (xy, m); b.fill (cv, face, alpha); }
	{ VPath o; o.polyline (xy, m, sw, true); o.fill (cv, ink, alpha); }
	if (type == P_XOR)
	{
		int bx[40], k = qbez (bx, 0, L, B, L + (R - L) * 24 / 100, cy, L, T, 8, false);
		VPath o; o.polyline (bx, k, sw); o.fill (cv, ink, alpha);
	}
	if (type == P_NOT || type == P_NAND || type == P_NOR)
	{
		VPath bb; bb.circle (R - rb, cy, rb); bb.fill (cv, ink, alpha);
		VPath bi; bi.circle (R - rb, cy, rb - sw); bi.fill (cv, face, 255);
	}
}

// ---- stars, padlock (Turtle Quest's star) ----------------------------------------------------------------------------
static void star (Canvas &cv, int cx, int cy, int r, unsigned c, bool filled)
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
	VPath s; s.arc (V (cx), V (cy - 2), V (3) + 8, 0, 180, V (1) + 8);
	s.line (V (cx - 3) - 8, V (cy - 2), V (cx - 3) - 8, V (cy), V (1) + 8);
	s.line (V (cx + 3) + 8, V (cy - 2), V (cx + 3) + 8, V (cy), V (1) + 8); s.fill (cv, c);
}

// ---- wrapped text (uikit's uk_text_wrap: the lines' starts and lengths) ------------------------------------------------
static int wrap_count (const char *s, int w, int maxL, int style = 0)
{ int st[16], ln[16]; return uk_text_wrap (s, (int) strlen (s), w, maxL < 16 ? maxL : 16, st, ln, 0, style); }
// The lines from y, lh apart; centred in w when centre -> how many
static int wrap_draw (Canvas &cv, int x, int y, int w, const char *s, unsigned c, int maxL, int lh, int style = 0, bool centre = false)
{
	int st[16], ln[16]; int n = uk_text_wrap (s, (int) strlen (s), w, maxL < 16 ? maxL : 16, st, ln, 0, style);
	for (int i = 0; i < n; i++)
	{
		char l[400]; int m = ln[i] < 399 ? ln[i] : 399; memcpy (l, s + st[i], m); l[m] = 0;
		if (centre) uk_text_c (cv, x, y + i * lh, w, lh, l, c, style); else uk_text (cv, x, y + i * lh, l, c, style);
	}
	return n;
}

// ---- the icons: the palette's (the select arrow, the gates) and the bench's (Check, Step, Reset) -------------------------
enum { IC_SELECT = 100, IC_CHECK, IC_STEP, IC_RESET };
static void arrow_pointer (Canvas &cv, int px, int py, unsigned c, bool outline)
{
	int pts[] = { V (px), V (py), V (px), V (py + 15), V (px + 4), V (py + 11), V (px + 7), V (py + 17), V (px + 9), V (py + 16),
		      V (px + 6), V (py + 10), V (px + 11), V (py + 10) };
	VPath a; a.poly (pts, 7); a.fill (cv, c);
	if (outline) { VPath o; o.polyline (pts, 7, V (1), true); o.fill (cv, 0x00FFFFFF); }
}
// A box of `size` px at (x, y): a size >= 30 has its name under the symbol (10 px; 9 when wider than the box)
static void circuits_icon (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool off)
{
	(void) off;
	if (id == IC_SELECT)
	{
		if (size >= 30) { UkFaceScope fs (g_small); uk_text_c (cv, x - 8, y + size - 12, size + 16, 12, TR ("Select"), ink, 0); x += size / 2 - 8; size = 22; }
		arrow_pointer (cv, x + size / 4, y + 1, ink, false);
		return;
	}
	if (id == IC_CHECK) { uk_glyph (cv, WKG_CHECK, x + size / 2, y + size / 2, size - 2, ink); return; }
	if (id == IC_STEP)				// a bar, then a triangle: one step
	{
		VPath p; int pts[] = { V (x + 5), V (y + 3), V (x + size - 4), V (y + size / 2), V (x + 5), V (y + size - 3) };
		p.poly (pts, 3); p.rect (V (x + 1), V (y + 3), V (3), V (size - 6)); p.fill (cv, ink); return;
	}
	if (id == IC_RESET) { uk_tool_glyph (cv, WKT_TO_START, x, y, size, ink); return; }
	if (id < P_NOT || id >= P_COUNT_) return;
	if (size >= 30)					// the name under the symbol
	{
		const char *nm = gate_word (id);
		UkFaceScope fs (g_small);
		if (uk_tw (nm) > size) { UkFaceScope ft (g_tiny); uk_text_c (cv, x - 8, y + size - 12, size + 16, 12, nm, ink, 0); }
		else uk_text_c (cv, x - 8, y + size - 12, size + 16, 12, nm, ink, 0);
		int s2 = size * 2 / 3; x += (size - s2) / 2; y += 1; size = s2;
	}
	int s = size * 16;
	int L = x * 16 + s * 22 / 100, R = x * 16 + s * 86 / 100, cy = y * 16 + s / 2, h2 = s * 30 / 100, rb = s * 7 / 100;
	VPath st;
	if (id == P_NOT) st.line (x * 16, cy, L + 8, cy, V (1) + 4);
	else { st.line (x * 16, cy - h2 * 55 / 100, L + 24, cy - h2 * 55 / 100, V (1) + 4); st.line (x * 16, cy + h2 * 55 / 100, L + 24, cy + h2 * 55 / 100, V (1) + 4); }
	st.line (R - 8, cy, (x + size) * 16, cy, V (1) + 4);
	st.fill (cv, ink);
	draw_gate_shape (cv, id, L, cy - h2, R, cy + h2, rb, 0x00FFFFFF, ink, V (1) + 6);
}

#endif
