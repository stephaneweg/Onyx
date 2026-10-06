//
// board.h -- Circuits' board (the window's, included by main.cpp after the game's state): the grid with the level's
// switches and lamps in their strips, the gates in their shapes, the wires routed by the engine and coloured by their
// level, the step mode's badges, the overlays (the ghost of a gate being placed or moved, the wire being drawn, the
// empty board's hint); and the mouse: a press on an output pin draws a wire, on a fed input pin picks its wire up, on a
// gate selects it (a drag moves it), on a switch toggles it, on a wire selects it; a right click deletes. The palette's
// buttons (PaletteButton) arm a gate on the press, so a gate is placed by a click then a click, or by a drag from the
// palette (UIKit routes the held button's moves to the widget under the pointer: the board places it on the release).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _circuits_board_h
#define _circuits_board_h

class Board : public Widget
{
public:
	int c, ox, oy;					// the cell (px), the grid's origin in the board
	enum { D_NONE, D_WIRE, D_PRESS, D_MOVE };
	int drag;					// what the held button does
	int src;					// D_WIRE: the source part
	int pickDst, pickPin;				// D_WIRE from a picked-up wire: where it was plugged (-1: a new wire)
	int mvI, mvDX, mvDY, mvX, mvY;			// D_PRESS / D_MOVE: the gate, the pointer in its box (1/16 cell), its new cell
	int pressX, pressY;				// where the press was (px)
	int px, py;					// the pointer (px; -1: out)
	int tgt, tpin; bool tok;			// D_WIRE: the input pin under the pointer, accepted or not
	bool lbl, lbr;					// the buttons as last seen
	int hoverGate, hoverOut;			// the gate / the output pin under the pointer (-1)
	bool inside;					// the pointer over the board

	Board (int l, int t, int w, int h) : Widget (l, t, w, h), c (12), ox (0), oy (0), drag (D_NONE), src (-1), pickDst (-1), pickPin (0),
		mvI (-1), mvDX (0), mvDY (0), mvX (0), mvY (0), pressX (0), pressY (0), px (-1), py (-1), tgt (-1), tpin (0), tok (false),
		lbl (false), lbr (false), hoverGate (-1), hoverOut (-1), inside (false) {}

	void fit ()
	{
		c = (width - 20) / BOARD_W; int ch = (height - 20) / BOARD_H; if (ch < c) c = ch; if (c < 10) c = 10;
		ox = (width - BOARD_W * c) / 2; oy = (height - BOARD_H * c) / 2;
	}
	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); fit (); }
	// cells -> px; 1/16 cell -> 1/16 px; px -> 1/16 cell
	int X (int gx) { return ox + gx * c; }
	int Y (int gy) { return oy + gy * c; }
	int X16 (int g16) { return ox * 16 + g16 * c; }
	int Y16 (int g16) { return oy * 16 + g16 * c; }
	int G16x (int mx) { return (mx - ox) * 16 / c; }
	int G16y (int my) { return (my - oy) * 16 / c; }
	static int fdiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

	// ---- drawing ------------------------------------------------------------------------------------------------
	unsigned level_col (int part) { int v = g_ev.v[part]; return v == LX ? WIRE_X : v ? WIRE_1 : WIRE_0; }
	int level_w (unsigned col) { return col == WIRE_1 ? V (3) : col == WIRE_X ? V (2) : V (2) + 8; }
	void seg (Canvas &cv, int x0, int y0, int x1, int y1, unsigned col, int w, int alpha = 255)	// (1/16 px)
	{ VPath p; p.line (x0, y0, x1, y1, w); p.fill (cv, col, alpha); }
	void dashed (int x0, int y0, int x1, int y1, unsigned col, int w)				// (1/16 px)
	{
		int len = x1 > x0 ? x1 - x0 : x0 - x1; len += y1 > y0 ? y1 - y0 : y0 - y1;
		if (len <= 0) return;
		int dash = c * 16 * 45 / 100, gap = c * 16 * 30 / 100; if (dash < 48) dash = 48; if (gap < 32) gap = 32;
		for (int s = 0; s < len; s += dash + gap)
		{
			int e = s + dash < len ? s + dash : len;
			seg (canvas, x0 + (int) ((long long) (x1 - x0) * s / len), y0 + (int) ((long long) (y1 - y0) * s / len),
			     x0 + (int) ((long long) (x1 - x0) * e / len), y0 + (int) ((long long) (y1 - y0) * e / len), col, w);
		}
	}
	void pin_dot (int gx16, int gy16, unsigned col) { VPath d; d.circle (X16 (gx16), Y16 (gy16), V (2) + 8); d.fill (canvas, col); }
	void ring (int cx, int cy, int r0, int r1, unsigned col, int alpha = 255)	// (1/16 px; radii in px)
	{ VPath r; r.circle (cx, cy, V (r1)); r.hole (cx, cy, V (r0)); r.fill (canvas, col, alpha); }
	void draw_wire (int dst, int pin)
	{
		int xy[12], n = g_c.route (dst, pin, xy, 12);
		if (n < 2) return;
		int s = g_c.p[dst].in[pin];
		unsigned col = level_col (s);
		bool sel = dst == g_selDst && pin == g_selPin;
		if (sel) { VPath h; for (int k = 0; k + 1 < n; k++) h.line (X16 (xy[2 * k] * 16), Y16 (xy[2 * k + 1] * 16), X16 (xy[2 * k + 2] * 16), Y16 (xy[2 * k + 3] * 16), V (8)); h.fill (canvas, C_ACCENT, 90); }
		if (col == WIRE_X)
		{
			for (int k = 0; k + 1 < n; k++) dashed (X16 (xy[2 * k] * 16), Y16 (xy[2 * k + 1] * 16), X16 (xy[2 * k + 2] * 16), Y16 (xy[2 * k + 3] * 16), col, V (2));
			return;
		}
		VPath p;
		for (int k = 0; k + 1 < n; k++) p.line (X16 (xy[2 * k] * 16), Y16 (xy[2 * k + 1] * 16), X16 (xy[2 * k + 2] * 16), Y16 (xy[2 * k + 3] * 16), level_w (col));
		p.fill (canvas, col);
	}
	// A gate (i: the board's; -1: the ghost of `type` at (gx, gy), ok: it fits)
	void draw_gate (int i, int type, int gx, int gy, bool ok)
	{
		bool ghost = i < 0;
		const Part *q = ghost ? 0 : &g_c.p[i];
		int n = type == P_NOT ? 1 : 2, alpha = ghost ? 200 : 255;
		for (int k = 0; k < n; k++)			// the input stubs (the body covers their ends)
		{
			int y = type == P_NOT ? gy + 2 : gy + (k ? 3 : 1), s = ghost ? -1 : q->in[k];
			unsigned col = s >= 0 ? level_col (s) : WIRE_OPEN;
			seg (canvas, X16 (gx * 16), Y16 (y * 16), X16 (gx * 16 + 32), Y16 (y * 16), col, s >= 0 && col == WIRE_1 ? V (3) : V (2), ghost ? 120 : 255);
		}
		unsigned oc = ghost ? WIRE_OPEN : level_col (i);
		seg (canvas, X16 (gx * 16 + 56), Y16 (gy * 16 + 32), X16 (gx * 16 + 80), Y16 (gy * 16 + 32), oc, !ghost && oc == WIRE_1 ? V (3) : V (2), ghost ? 120 : 255);
		if (!ghost && (i == g_sel || i == hoverGate))
		{
			int x0 = X16 (gx * 16 + 9) / 16, y0 = Y16 (gy * 16 + 3) / 16, x1 = X16 (gx * 16 + 76) / 16, y1 = Y16 (gy * 16 + 61) / 16;
			if (i == g_sel) uk_rbox (canvas, x0, y0, x1 - x0, y1 - y0, 6, uk_mix (BOARD_BG, C_ACCENT, 50), uk_mix (BOARD_BG, C_ACCENT, 50));
			uk_rline (canvas, x0, y0, x1 - x0, y1 - y0, 6, C_ACCENT, i == g_sel ? 255 : 60);
		}
		int L = X16 (gx * 16 + 16), R = X16 (gx * 16 + 66), cy = Y16 (gy * 16 + 32), h2 = c * 16 * 145 / 100, rb = c * 16 * 3 / 10;
		unsigned face = ghost ? (ok ? 0x00E6F4EA : 0x00FBE0DC) : g_ev.v[i] == LX ? GATE_FACEX : g_ev.v[i] ? GATE_FACE1 : GATE_FACE;
		unsigned ink = ghost ? (ok ? OK_GREEN : ERR_RED) : i == g_errPart ? ERR_RED : GATE_INK;
		draw_gate_shape (canvas, type, L, cy - h2, R, cy + h2, rb, face, ink, V (1) + 10, alpha);
		{						// its name, small, inside
			UkFaceScope fs (g_small);
			int tx = type == P_NOT ? 18 : type == P_XOR || type == P_OR || type == P_NOR ? 30 : 22;
			int tw = type == P_NOT ? 26 : type == P_AND || type == P_NAND ? 34 : 32;
			int x0 = X16 (gx * 16 + tx) / 16, w0 = X16 (gx * 16 + tx + tw) / 16 - x0;
			const char *nm = gate_word (type);
			if (uk_tw (nm) > w0 + 6) { UkFaceScope ft (g_tiny); uk_text_c (canvas, x0 - 4, cy / 16 - 8, w0 + 8, 16, nm, ghost ? ink : uk_mix (face, GATE_INK, 190), 0); }
			else uk_text_c (canvas, x0 - 4, cy / 16 - 8, w0 + 8, 16, nm, ghost ? ink : uk_mix (face, GATE_INK, 190), 0);
		}
		if (g_step >= 0 && !ghost)			// step mode: the gate's depth in a disc above it
		{
			int bx = X (gx) + c * 5 / 2, by = Y (gy) - 2;
			VPath d; d.circle (V (bx), V (by), V (7)); d.fill (canvas, g_ev.v[i] == LX ? 0x00B5BDC4 : C_ACCENT);
			char t[8]; snprintf (t, sizeof t, "%d", g_ev.depth[i]);
			UkFaceScope fs (g_small); uk_text_c (canvas, bx - 7, by - 7, 14, 14, t, 0x00FFFFFF, 2);
		}
		for (int k = 0; k < n; k++) { int y = type == P_NOT ? gy + 2 : gy + (k ? 3 : 1); pin_dot (gx * 16, y * 16, ghost ? WIRE_OPEN : 0x005A6570); }
		pin_dot (gx * 16 + 80, gy * 16 + 32, oc);
	}
	void draw_switch (int i)				// a key showing its digit, its name at its left; green when 1
	{
		const Part &q = g_c.p[i];
		bool on = g_ev.v[i] == L1;
		int kx = X16 (q.x * 16 + 38) / 16, ky = Y16 (q.y * 16 + 1) / 16, ks = Y16 (q.y * 16 + 31) / 16 - ky, kw = X16 (q.x * 16 + 72) / 16 - kx;
		seg (canvas, X16 (q.x * 16 + 69), Y16 (q.y * 16 + 16), X16 (q.x * 16 + 80), Y16 (q.y * 16 + 16), on ? WIRE_1 : WIRE_0, on ? V (3) : V (2) + 8);
		uk_text_l (canvas, kx - 5 - uk_tw (q.name, 2), ky, ks, q.name, GATE_INK, 2);
		if (on) { uk_rbox (canvas, kx, ky, kw, ks, 5, uk_tone (WIRE_1, 150), WIRE_1); uk_rline (canvas, kx, ky, kw, ks, 5, uk_tone (WIRE_1, 80)); }
		else uk_raised (canvas, kx, ky, kw, ks, 5, C_FACE);
		uk_text_c (canvas, kx, ky, kw, ks, on ? "1" : "0", on ? 0x00FFFFFF : C_TEXT, 2);
		pin_dot (q.x * 16 + 80, q.y * 16 + 16, on ? WIRE_1 : WIRE_0);
	}
	void draw_lamp (int i)
	{
		const Part &q = g_c.p[i];
		int s = q.in[0];
		unsigned col = s >= 0 ? level_col (s) : WIRE_OPEN;
		seg (canvas, X16 (q.x * 16), Y16 (q.y * 16 + 16), X16 (q.x * 16 + 16), Y16 (q.y * 16 + 16), col, s >= 0 && col == WIRE_1 ? V (3) : V (2));
		int cx = X16 (q.x * 16 + 28) / 16, cy = Y (q.y + 1), r = c * 9 / 10 + 1;
		bool unknown = g_ev.v[i] == LX, on = !unknown && g_ev.v[i] == L1 && s >= 0;
		if (i == g_errPart) ring (V (cx), V (cy), r + 3, r + 6, ERR_RED);
		if (on)
		{
			VPath g1; g1.circle (V (cx), V (cy), V (r + 7)); g1.fill (canvas, LAMP_ON, 50);
			VPath g2; g2.circle (V (cx), V (cy), V (r + 3)); g2.fill (canvas, LAMP_ON, 90);
		}
		VPath b; b.circle (V (cx), V (cy), V (r)); b.fill (canvas, on ? 0x00C99A12 : unknown ? 0x009AA3AB : 0x00403B30);
		VPath bi; bi.circle (V (cx), V (cy), V (r - 2)); bi.fill (canvas, on ? LAMP_ON : unknown ? 0x00C3CAD0 : LAMP_OFF);
		VPath hl; hl.circle (V (cx - r / 3), V (cy - r / 3), V (r / 3)); hl.fill (canvas, 0x00FFFFFF, on ? 170 : 60);
		if (unknown) uk_text_c (canvas, cx - r, cy - r, 2 * r, 2 * r, "?", 0x00FFFFFF, 2);
		uk_text_l (canvas, cx + r + 6, cy - 10, 20, q.name, GATE_INK, 2);
		pin_dot (q.x * 16, q.y * 16 + 16, col);
	}
	// The wire being drawn: its future route, dashed, from the source's pin to the pointer (or the input pin under it)
	void draw_rubber ()
	{
		int x0, y0; g_c.pinOut (src, x0, y0);
		int sx = X16 (x0 * 16), sy = Y16 (y0 * 16), ex = V (px), ey = V (py), t16 = G16x (px);
		if (tgt >= 0) { int tx, ty; g_c.pinIn (tgt, tpin, tx, ty); ex = X16 (tx * 16); ey = Y16 (ty * 16); t16 = tx * 16; }
		int xm16 = t16 > x0 * 16 + 16 ? (x0 * 16 + t16) / 2 / 16 * 16 : x0 * 16 + 16, xm = X16 (xm16);
		dashed (sx, sy, xm, sy, C_ACCENT, V (2) + 8); dashed (xm, sy, xm, ey, C_ACCENT, V (2) + 8); dashed (xm, ey, ex, ey, C_ACCENT, V (2) + 8);
		if (tgt >= 0) ring (ex, ey, 5, 7, tok ? OK_GREEN : ERR_RED);
		ring (sx, sy, 4, 6, C_ACCENT);
	}
	void draw_hint ()
	{
		bool first = g_L && g_L->parts == 0;
		const char *t = first ? TR ("Press on the switch's pin, drag to the lamp's pin, let go.")
				      : TR ("Take a gate from the palette and put it on the board, then drag from a pin to a pin to wire it.");
		int bx = X (9), by = Y (6), bw = X (31) - bx, n = wrap_count (t, bw - 32, 4), bh = 44 + n * 18 + 12;
		uk_rbox (canvas, bx, by, bw, bh, 10, 0x00FFFFFF, 0x00FFFFFF, 170);
		for (int x = bx + 8; x < bx + bw - 8; x += 10) { canvas.fillRect (x, by, 5, 1, 0x009DB0A4); canvas.fillRect (x, by + bh - 1, 5, 1, 0x009DB0A4); }
		for (int y = by + 8; y < by + bh - 8; y += 10) { canvas.fillRect (bx, y, 1, 5, 0x009DB0A4); canvas.fillRect (bx + bw - 1, y, 1, 5, 0x009DB0A4); }
		{ UkFaceScope fs (g_big); uk_text_c (canvas, bx, by + 12, bw, 24, first ? TR ("Lay a wire") : TR ("Build your circuit"), 0x00405048, 2); }
		wrap_draw (canvas, bx + 16, by + 44, bw - 32, t, 0x005A6A60, 4, 18, 0, true);
	}
	void onDraw () override
	{
		fit ();
		canvas.clear (bgColor ());
		uk_rbox (canvas, 0, 0, width, height, 10, BOARD_BG, BOARD_BG);
		uk_rbox (canvas, 1, 1, X (GATE_MINX) - 1, height - 2, 10, STRIP_BG, STRIP_BG, 255, UK_TL | UK_BL);
		uk_rbox (canvas, X (GATE_MAXX), 1, width - X (GATE_MAXX) - 1, height - 2, 10, STRIP_BG, STRIP_BG, 255, UK_TR | UK_BR);
		for (int gy = 0; gy <= BOARD_H; gy++) for (int gx = GATE_MINX + 1; gx < GATE_MAXX; gx++) canvas.fillRect (X (gx), Y (gy), 2, 1, BOARD_DOT);
		{
			UkFaceScope fs (g_small); unsigned cap = uk_mix (STRIP_BG, GATE_INK, 120);
			uk_text_c (canvas, 0, 4, X (GATE_MINX), 14, TR ("INPUTS"), cap, 2);
			uk_text_c (canvas, X (GATE_MAXX), 4, width - X (GATE_MAXX), 14, TR ("OUTPUTS"), cap, 2);
		}
		uk_rline (canvas, 0, 0, width, height, 10, uk_mix (BOARD_BG, GATE_INK, 60));
		if (!g_L) return;
		// the wires (the one selected last, over the others), their junctions
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < g_c.n; i++)
				for (int k = 0; k < g_c.pins (i); k++)
					if (g_c.p[i].in[k] >= 0 && (pass == 1) == (i == g_selDst && k == g_selPin)) draw_wire (i, k);
		{
			int xy[2 * 128], m = g_c.junctions (xy, 2 * 128);
			for (int j = 0; j < m; j++)
			{
				int pin = 0, d = g_c.wireAt (xy[2 * j] * 16, xy[2 * j + 1] * 16, &pin);
				unsigned col = d >= 0 && g_c.p[d].in[pin] >= 0 ? level_col (g_c.p[d].in[pin]) : WIRE_0;
				VPath dot; dot.circle (X16 (xy[2 * j] * 16), Y16 (xy[2 * j + 1] * 16), V (3) + 8); dot.fill (canvas, col);
			}
		}
		for (int i = 0; i < g_c.n; i++)
		{
			const Part &q = g_c.p[i];
			if (q.type == P_SWITCH) draw_switch (i);
			else if (q.type == P_LAMP) draw_lamp (i);
			else if (!(drag == D_MOVE && i == mvI)) draw_gate (i, q.type, q.x, q.y, true);
		}
		if (g_c.gates () == 0 && g_c.wires () == 0 && drag == D_NONE && g_armed < 0) draw_hint ();
		if (drag == D_MOVE) { draw_gate (mvI, g_c.p[mvI].type, mvX, mvY, true); draw_gate (-1, g_c.p[mvI].type, mvX, mvY, g_c.placeErr (g_c.p[mvI].type, mvX, mvY, mvI) == E_OK); }
		else if (g_armed >= 0 && inside) { int gx, gy; ghost_cell (gx, gy); draw_gate (-1, g_armed, gx, gy, g_c.fits (g_armed, gx, gy)); }
		if (drag == D_WIRE) draw_rubber ();
		else if (hoverOut >= 0 && g_armed < 0) { int x, y; g_c.pinOut (hoverOut, x, y); ring (X16 (x * 16), Y16 (y * 16), 4, 6, C_ACCENT, 110); }
	}

	// ---- the mouse ------------------------------------------------------------------------------------------------------
	void ghost_cell (int &gx, int &gy)			// the cell of a gate centred under the pointer
	{ gx = fdiv (G16x (px) - 40 + 8, 16); gy = fdiv (G16y (py) - 32 + 8, 16); }
	// The pin under (mx, my) within 0.6 cell (7 px at least): its part, *isOut, *pin -> -1 none
	int pin_at (int mx, int my, bool *isOut, int *pin)
	{
		int r = c * 6 / 10; if (r < 7) r = 7;
		int best = -1, bestD = r * r + 1;
		for (int i = 0; i < g_c.n; i++)
		{
			int x, y;
			if (g_c.p[i].type != P_LAMP)
			{
				g_c.pinOut (i, x, y); int dx = mx - X (x), dy = my - Y (y), d = dx * dx + dy * dy;
				if (d < bestD) { bestD = d; best = i; *isOut = true; *pin = 0; }
			}
			for (int k = 0; k < g_c.pins (i); k++)
			{
				g_c.pinIn (i, k, x, y); int dx = mx - X (x), dy = my - Y (y), d = dx * dx + dy * dy;
				if (d < bestD) { bestD = d; best = i; *isOut = false; *pin = k; }
			}
		}
		return best;
	}
	int gate_at (int mx, int my)
	{
		int i = g_c.at (fdiv (mx - ox, c), fdiv (my - oy, c));
		return i;
	}
	void cancel ()						// Esc, a release outside: the drag undone
	{
		if (drag == D_WIRE && pickDst >= 0) g_c.connect (src, pickDst, pickPin);	// (the wire picked up: back)
		drag = D_NONE; catchOutside = false; tgt = -1;
		refresh ();
	}
	void update_target ()
	{
		tgt = -1; tok = false;
		bool out = false; int pin = 0, i = pin_at (px, py, &out, &pin);
		if (i >= 0 && !out) { Err e; tgt = i; tpin = pin; tok = g_c.canConnect (src, i, pin, &e); }
	}
	void press (int mx, int my)
	{
		pressX = mx; pressY = my;
		if (g_armed >= 0) return;			// (placed on the release)
		bool out = false; int pin = 0, i = pin_at (mx, my, &out, &pin);
		if (i >= 0 && out) { start_wire (i, -1, 0); return; }
		if (i >= 0 && !out && g_c.p[i].in[pin] >= 0)	// a fed input: its wire picked up
		{
			int s = g_c.p[i].in[pin];
			g_c.disconnect (i, pin);
			start_wire (s, i, pin);
			return;
		}
		int g = gate_at (mx, my);
		if (g >= 0 && g_c.p[g].type == P_SWITCH) { toggle_switch (g); return; }
		if (g >= 0 && gate_type (g_c.p[g].type))
		{
			select_gate (g);
			drag = D_PRESS; mvI = g; mvDX = G16x (mx) - g_c.p[g].x * 16; mvDY = G16y (my) - g_c.p[g].y * 16;
			mvX = g_c.p[g].x; mvY = g_c.p[g].y; catchOutside = true;
			return;
		}
		int wp = 0, d = g_c.wireAt (G16x (mx), G16y (my), &wp);
		if (d >= 0) { select_wire (d, wp); return; }
		select_gate (-1);
	}
	void start_wire (int s, int dst, int pin)
	{
		drag = D_WIRE; src = s; pickDst = dst; pickPin = pin; catchOutside = true;
		select_gate (-1);
		set_message (M_INFO, TR ("Let go on an input pin to lay the wire (Esc: cancel)."));
		update_target ();
		if (dst >= 0) refresh ();
	}
	void release (int mx, int my)
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (g_armed >= 0 && drag == D_NONE)
		{
			if (!in) return;
			int gx, gy; ghost_cell (gx, gy);
			Err e; int i = g_c.add (g_armed, gx, gy, &e);
			if (i < 0) { set_message (M_ERR, TR (err_word (e))); invalidate (true); return; }
			arm (-1); select_gate (i); board_changed ();
			return;
		}
		int d = drag; drag = D_NONE; catchOutside = false;
		if (d == D_WIRE)
		{
			if (!in) { drag = D_WIRE; cancel (); prompt (); return; }
			update_target ();
			int t = tgt; tgt = -1;
			if (t >= 0)
			{
				Err e = g_c.connect (src, t, tpin);
				if (e != E_OK)
				{
					if (pickDst >= 0) g_c.connect (src, pickDst, pickPin);
					set_message (M_ERR, TR (err_word (e))); refresh (); return;
				}
			}
			else if (pickDst < 0) { prompt (); refresh (); return; }	// (released elsewhere: nothing made)
			prompt (); board_changed ();
			return;
		}
		if (d == D_MOVE)
		{
			if (!in) { refresh (); return; }
			Err e = g_c.move (mvI, mvX, mvY);
			if (e != E_OK) { set_message (M_ERR, TR (err_word (e))); refresh (); return; }
			board_changed ();
			return;
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		bool leave = mx == -1 && my == -1 && !bl && !br;
		if (leave)
		{
			px = py = -1; inside = false; lbl = lbr = false;
			if (hoverGate >= 0 || hoverOut >= 0 || g_armed >= 0) { hoverGate = hoverOut = -1; invalidate (true); }
			return false;
		}
		px = mx; py = my;
		inside = mx >= 0 && my >= 0 && mx < width && my < height;
		bool down = bl && !lbl, up = !bl && lbl, rdown = br && !lbr;
		lbl = bl != 0; lbr = br != 0;
		if (rdown && drag == D_NONE)			// a right click: disarms, or deletes the gate / the wire
		{
			if (g_armed >= 0) arm (-1);
			else
			{
				bool out = false; int pin = 0;
				int g = gate_at (mx, my);
				if (g >= 0 && gate_type (g_c.p[g].type) && pin_at (mx, my, &out, &pin) < 0) { g_c.remove (g); select_gate (-1); board_changed (); }
				else { int wp = 0, d = g_c.wireAt (G16x (mx), G16y (my), &wp); if (d >= 0) { g_c.disconnect (d, wp); select_wire (-1, 0); board_changed (); } }
			}
			return inside;
		}
		if (down && inside) press (mx, my);
		else if (up) release (mx, my);
		else if (bl && drag == D_WIRE) update_target ();
		else if (bl && (drag == D_PRESS || drag == D_MOVE))
		{
			int dx = mx - pressX, dy = my - pressY;
			if (drag == D_PRESS && dx * dx + dy * dy >= (c / 2) * (c / 2)) drag = D_MOVE;
			if (drag == D_MOVE) { mvX = fdiv (G16x (mx) - mvDX + 8, 16); mvY = fdiv (G16y (my) - mvDY + 8, 16); }
		}
		// the hover
		int hg = -1, ho = -1;
		if (!bl && inside && g_armed < 0)
		{
			bool out = false; int pin = 0, i = pin_at (mx, my, &out, &pin);
			if (i >= 0 && out) ho = i;
			else { int g = gate_at (mx, my); if (g >= 0 && gate_type (g_c.p[g].type)) hg = g; }
		}
		hoverGate = hg; hoverOut = ho;
		invalidate (true);
		return inside || drag != D_NONE;
	}
};

// ---- the palette's buttons: armed on the press (a drag from the palette onto the board places the gate) -------------------
class PaletteButton : public ToolButton
{
public:
	int type;					// a gate's type; -1: Select
	char tipText[96];
	PaletteButton (int t) : ToolButton (40, 40, 0), type (t)
	{
		iconSize = 36; tipText[0] = 0; tip = tipText;
		setIcon (circuits_icon, t < 0 ? (int) IC_SELECT : t); setToggle (true, t < 0);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		ToolButton::onMouse (mx, my, 0, 0, 0, 0);	// (its hover)
		if (bl && !pressed && in) { pressed = true; arm (type < 0 || g_armed == type ? -1 : type); }
		if (!bl) pressed = false;
		return in;
	}
};

#endif
