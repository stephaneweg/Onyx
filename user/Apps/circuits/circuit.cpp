//
// circuit.cpp -- Circuits' engine (circuit.h): the board, the evaluation, the Check, the circuit text, the undo
// history, the routes and the hit tests; the packs and the progress.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "circuit.h"
#include <stdio.h>

namespace circuits {

static const char *const TYPE_NAMES[P_COUNT_] = { "", "", "NOT", "AND", "OR", "XOR", "NAND", "NOR" };
static const char *const ERR_NAMES[E_COUNT_] = { "E_OK", "E_NOT_ALLOWED", "E_FULL", "E_OVERLAP", "E_OUTSIDE", "E_FIXED",
	"E_LOOP", "E_PIN", "E_SAME", "E_LAMP_OPEN", "E_GATE_OPEN", "E_SYNTAX", "E_NAME", "E_TYPE", "E_DOUBLE" };

const char *type_name (int type) { return type >= 0 && type < P_COUNT_ ? TYPE_NAMES[type] : ""; }
int type_of (const char *name)
{
	for (int t = P_NOT; t < P_COUNT_; t++) if (!strcmp (TYPE_NAMES[t], name)) return t;
	return -1;
}
const char *err_name (Err e) { return e >= 0 && e < E_COUNT_ ? ERR_NAMES[e] : "?"; }
int fixed_row (int k, int n) { return n > 0 ? (2 * k + 1) * BOARD_H / (2 * n) - 1 : 0; }

static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; if (cap > 0) d[i] = 0; }
static int imin (int a, int b) { return a < b ? a : b; }
static int imax (int a, int b) { return a > b ? a : b; }
static int iabs (int a) { return a < 0 ? -a : a; }

// ---- the board ----------------------------------------------------------------------------------------------------------
void Circuit::setup (const Level &L)
{
	ni = imin (imax (L.ninputs, 0), MAXIO); no = imin (imax (L.noutputs, 0), MAXIO); n = ni + no;
	allowed = L.parts;
	for (int k = 0; k < n; k++)
	{
		Part &q = p[k];
		bool sw = k < ni;
		scpy (q.name, sw ? L.inName[k] : L.outName[k - ni], sizeof q.name);
		q.type = sw ? P_SWITCH : P_LAMP;
		q.x = sw ? 0 : LAMP_X;
		q.y = (short) fixed_row (sw ? k : k - ni, sw ? ni : no);
		q.in[0] = q.in[1] = -1;
	}
}
int Circuit::find (const char *name) const
{
	for (int i = 0; i < n; i++) if (!strcmp (p[i].name, name)) return i;
	return -1;
}
int Circuit::pins (int i) const
{
	if (i < 0 || i >= n) return 0;
	int t = p[i].type;
	return t == P_SWITCH ? 0 : t == P_LAMP || t == P_NOT ? 1 : 2;
}
int Circuit::wires () const
{
	int w = 0;
	for (int i = 0; i < n; i++) for (int k = 0; k < pins (i); k++) if (p[i].in[k] >= 0) w++;
	return w;
}
bool Circuit::upstream (int a, int b) const
{
	// (a depth-first walk up a's inputs; no loop can exist, so it ends)
	int stack[MAXPARTS * 2], sp = 0;
	bool seen[MAXPARTS] = { false };
	if (a < 0 || a >= n) return false;
	stack[sp++] = a;
	while (sp)
	{
		int i = stack[--sp];
		for (int k = 0; k < pins (i); k++)
		{
			int s = p[i].in[k];
			if (s < 0 || seen[s]) continue;
			if (s == b) return true;
			seen[s] = true;
			if (sp < MAXPARTS * 2) stack[sp++] = s;
		}
	}
	return false;
}
void Circuit::box (int i, int &x, int &y, int &w, int &h) const
{
	x = p[i].x; y = p[i].y;
	if (p[i].type == P_SWITCH) { w = SWITCH_W; h = SWITCH_H; }
	else if (p[i].type == P_LAMP) { w = LAMP_W; h = LAMP_H; }
	else { w = GATE_W; h = GATE_H; }
}
void Circuit::pinIn (int i, int k, int &x, int &y) const
{
	x = p[i].x;
	if (p[i].type == P_LAMP) y = p[i].y + 1;
	else if (p[i].type == P_NOT) y = p[i].y + 2;
	else y = p[i].y + (k ? 3 : 1);
}
void Circuit::pinOut (int i, int &x, int &y) const
{
	if (p[i].type == P_SWITCH) { x = p[i].x + SWITCH_W; y = p[i].y + 1; }
	else { x = p[i].x + GATE_W; y = p[i].y + 2; }
}
int Circuit::at (int cx, int cy) const
{
	for (int i = n - 1; i >= 0; i--)
	{
		int x, y, w, h; box (i, x, y, w, h);
		if (cx >= x && cx < x + w && cy >= y && cy < y + h) return i;
	}
	return -1;
}
Err Circuit::placeErr (int type, int x, int y, int ignore) const
{
	if (!gate_type (type) || !(allowed & (1u << type))) return E_NOT_ALLOWED;
	if (ignore < 0 && gates () >= MAXGATES) return E_FULL;
	if (x < GATE_MINX || x + GATE_W > GATE_MAXX || y < 0 || y + GATE_H > BOARD_H) return E_OUTSIDE;
	for (int i = ni + no; i < n; i++)
	{
		if (i == ignore) continue;
		if (x < p[i].x + GATE_W && p[i].x < x + GATE_W && y < p[i].y + GATE_H && p[i].y < y + GATE_H) return E_OVERLAP;
	}
	return E_OK;
}
int Circuit::add (int type, int x, int y, Err *e)
{
	Err r = placeErr (type, x, y);
	if (e) *e = r;
	if (r != E_OK) return -1;
	char nm[NAMEL];
	for (int k = 1; ; k++) { snprintf (nm, sizeof nm, "g%d", k); if (find (nm) < 0) break; }
	Part &q = p[n];
	scpy (q.name, nm, sizeof q.name);
	q.type = (unsigned char) type; q.x = (short) x; q.y = (short) y; q.in[0] = q.in[1] = -1;
	return n++;
}
Err Circuit::move (int i, int x, int y)
{
	if (i < 0 || i >= n) return E_PIN;
	if (fixed (i)) return E_FIXED;
	Err r = placeErr (p[i].type, x, y, i);
	if (r != E_OK) return r;
	p[i].x = (short) x; p[i].y = (short) y;
	return E_OK;
}
bool Circuit::canConnect (int src, int dst, int pin, Err *e) const
{
	Err r = E_OK;
	if (src < 0 || src >= n || dst < 0 || dst >= n || p[src].type == P_LAMP || pin < 0 || pin >= pins (dst)) r = E_PIN;
	else if (src == dst || upstream (src, dst)) r = E_LOOP;
	if (e) *e = r;
	return r == E_OK;
}
Err Circuit::connect (int src, int dst, int pin)
{
	Err r;
	if (!canConnect (src, dst, pin, &r)) return r;
	p[dst].in[pin] = (short) src;
	return E_OK;
}
void Circuit::disconnect (int dst, int pin)
{
	if (dst >= 0 && dst < n && pin >= 0 && pin < 2) p[dst].in[pin] = -1;
}
Err Circuit::remove (int i)
{
	if (i < 0 || i >= n) return E_PIN;
	if (fixed (i)) return E_FIXED;
	for (int k = i; k + 1 < n; k++) p[k] = p[k + 1];
	n--;
	for (int k = 0; k < n; k++)
		for (int j = 0; j < 2; j++)
		{
			if (p[k].in[j] == i) p[k].in[j] = -1;
			else if (p[k].in[j] > i) p[k].in[j]--;
		}
	return E_OK;
}
void Circuit::clear ()
{
	n = ni + no;
	for (int k = 0; k < n; k++) p[k].in[0] = p[k].in[1] = -1;
}

// ---- routes ---------------------------------------------------------------------------------------------------------------
// Every wire's route, in the canonical order (into the gates, pin 1 then 2; then into the lamps): a wire's column is
// chosen knowing the verticals of the wires routed before it.
namespace {
struct Route { int dst, pin, src, np; int xy[12]; };
}
static int route_order (const Circuit &c, int *dst, int *pin)
{
	int w = 0;
	for (int i = c.ni + c.no; i < c.n; i++) for (int k = 0; k < c.pins (i); k++) if (c.p[i].in[k] >= 0) { dst[w] = i; pin[w] = k; w++; }
	for (int i = c.ni; i < c.ni + c.no; i++) if (c.p[i].in[0] >= 0) { dst[w] = i; pin[w] = 0; w++; }
	return w;
}
// Is column x free for a vertical from y0 to y1 of source src (routes r[0, nr) already made)?
static bool column_free (const Circuit &c, const Route *r, int nr, int src, int x, int y0, int y1)
{
	int a = imin (y0, y1), b = imax (y0, y1);
	for (int k = 0; k < nr; k++)
	{
		if (r[k].src == src) continue;				// (the same signal may share it)
		for (int s = 0; s + 1 < r[k].np; s++)
		{
			int x0 = r[k].xy[2 * s], ya = r[k].xy[2 * s + 1], x1 = r[k].xy[2 * s + 2], yb = r[k].xy[2 * s + 3];
			if (x0 != x || x1 != x) continue;
			if (imin (ya, yb) <= b && a <= imax (ya, yb)) return false;
		}
	}
	for (int i = 0; i < c.n; i++)
	{
		int px, py, pw, ph; c.box (i, px, py, pw, ph);
		if (px < x && x < px + pw && imax (a, py) < imin (b, py + ph)) return false;
	}
	return true;
}
static void make_route (const Circuit &c, const Route *done, int nd, Route &r)
{
	int sx, sy, tx, ty, s = r.src;
	c.pinOut (s, sx, sy); c.pinIn (r.dst, r.pin, tx, ty);
	if (tx > sx)						// forward: out, a vertical, in
	{
		int xm = (sx + tx) / 2;
		if (tx - sx >= 2)
		{
			for (int d = 0; d <= tx - sx; d++)		// the middle, then +-1, +-2...
			{
				int cand[2] = { (sx + tx) / 2 + d, (sx + tx) / 2 - d }, found = 0;
				for (int j = 0; j < (d ? 2 : 1); j++)
					if (cand[j] > sx && cand[j] < tx && column_free (c, done, nd, s, cand[j], sy, ty)) { xm = cand[j]; found = 1; break; }
				if (found) break;
			}
		}
		else xm = tx;						// (one column apart: the vertical at the target's pin)
		int pts[8] = { sx, sy, xm, sy, xm, ty, tx, ty };
		memcpy (r.xy, pts, sizeof pts); r.np = 4;
	}
	else								// backward: right, down under both parts, left, up / down, in
	{
		int bx, by, bw, bh, ex, ey, ew, eh;
		c.box (s, bx, by, bw, bh); c.box (r.dst, ex, ey, ew, eh);
		int yb = imax (by + bh, ey + eh) + 1;
		if (yb > BOARD_H) yb = imin (by, ey) - 1;
		int pts[12] = { sx, sy, sx + 1, sy, sx + 1, yb, tx - 1, yb, tx - 1, ty, tx, ty };
		memcpy (r.xy, pts, sizeof pts); r.np = 6;
	}
}
static int all_routes (const Circuit &c, Route *r)
{
	int dst[MAXWIRES + MAXPARTS], pin[MAXWIRES + MAXPARTS];
	int w = route_order (c, dst, pin);
	for (int k = 0; k < w; k++)
	{
		r[k].dst = dst[k]; r[k].pin = pin[k]; r[k].src = c.p[dst[k]].in[pin[k]];
		make_route (c, r, k, r[k]);
	}
	return w;
}
int Circuit::route (int dst, int pin, int *xy, int cap) const
{
	if (dst < 0 || dst >= n || pin < 0 || pin >= pins (dst) || p[dst].in[pin] < 0) return 0;
	Route r[MAXWIRES + MAXPARTS];
	int w = all_routes (*this, r);
	for (int k = 0; k < w; k++)
		if (r[k].dst == dst && r[k].pin == pin)
		{
			int m = imin (r[k].np, cap / 2);
			for (int j = 0; j < 2 * m; j++) xy[j] = r[k].xy[j];
			return m;
		}
	return 0;
}
// Is (x, y) strictly inside one of the segments of route q?
static bool inside_segment (const Route &q, int x, int y)
{
	for (int s = 0; s + 1 < q.np; s++)
	{
		int x0 = q.xy[2 * s], y0 = q.xy[2 * s + 1], x1 = q.xy[2 * s + 2], y1 = q.xy[2 * s + 3];
		if (x0 == x1 && x == x0 && y > imin (y0, y1) && y < imax (y0, y1)) return true;
		if (y0 == y1 && y == y0 && x > imin (x0, x1) && x < imax (x0, x1)) return true;
	}
	return false;
}
// The direction route q leaves its point j by (0: none): 1 right, 2 left, 3 down, 4 up
static int leaves (const Route &q, int j)
{
	if (j + 1 >= q.np) return 0;
	int dx = q.xy[2 * j + 2] - q.xy[2 * j], dy = q.xy[2 * j + 3] - q.xy[2 * j + 1];
	return dx > 0 ? 1 : dx < 0 ? 2 : dy > 0 ? 3 : dy < 0 ? 4 : 0;
}
int Circuit::junctions (int *xy, int cap) const
{
	Route r[MAXWIRES + MAXPARTS];
	int w = all_routes (*this, r), m = 0;
	for (int a = 0; a < w; a++)
		for (int j = 1; j + 1 < r[a].np; j++)
		{
			int x = r[a].xy[2 * j], y = r[a].xy[2 * j + 1];
			bool dot = false;
			for (int b = 0; b < w && !dot; b++)
			{
				if (b == a || r[b].src != r[a].src) continue;
				if (inside_segment (r[b], x, y)) dot = true;
				else
					for (int k = 1; k + 1 < r[b].np && !dot; k++)
						if (r[b].xy[2 * k] == x && r[b].xy[2 * k + 1] == y && leaves (r[b], k) != leaves (r[a], j)) dot = true;
			}
			if (!dot) continue;
			bool dup = false;
			for (int k = 0; k < m && !dup; k++) dup = xy[2 * k] == x && xy[2 * k + 1] == y;
			if (dup || 2 * m + 2 > cap) continue;
			xy[2 * m] = x; xy[2 * m + 1] = y; m++;
		}
	return m;
}
int Circuit::pinAt (int gx16, int gy16, bool *isOut, int *pin) const
{
	int best = -1, bestD = 0, bestPin = 0; bool bestOut = false;
	const int R2 = 92;					// (0.6 cell, in 1/16: 9.6 squared)
	for (int i = 0; i < n; i++)
	{
		int x, y;
		if (p[i].type != P_LAMP)
		{
			pinOut (i, x, y);
			int dx = gx16 - x * 16, dy = gy16 - y * 16, d = dx * dx + dy * dy;
			if (d <= R2 && (best < 0 || d < bestD)) { best = i; bestD = d; bestOut = true; bestPin = 0; }
		}
		for (int k = 0; k < pins (i); k++)
		{
			pinIn (i, k, x, y);
			int dx = gx16 - x * 16, dy = gy16 - y * 16, d = dx * dx + dy * dy;
			if (d <= R2 && (best < 0 || d < bestD)) { best = i; bestD = d; bestOut = false; bestPin = k; }
		}
	}
	if (best >= 0) { if (isOut) *isOut = bestOut; if (pin) *pin = bestPin; }
	return best;
}
int Circuit::wireAt (int gx16, int gy16, int *pin) const
{
	Route r[MAXWIRES + MAXPARTS];
	int w = all_routes (*this, r), best = -1, bestD = 6;	// (1/3 cell: 5.33 in 1/16)
	for (int k = 0; k < w; k++)
		for (int s = 0; s + 1 < r[k].np; s++)
		{
			int x0 = r[k].xy[2 * s] * 16, y0 = r[k].xy[2 * s + 1] * 16, x1 = r[k].xy[2 * s + 2] * 16, y1 = r[k].xy[2 * s + 3] * 16, d;
			if (x0 == x1)
				d = gy16 < imin (y0, y1) ? iabs (gx16 - x0) + imin (y0, y1) - gy16 : gy16 > imax (y0, y1) ? iabs (gx16 - x0) + gy16 - imax (y0, y1) : iabs (gx16 - x0);
			else
				d = gx16 < imin (x0, x1) ? iabs (gy16 - y0) + imin (x0, x1) - gx16 : gx16 > imax (x0, x1) ? iabs (gy16 - y0) + gx16 - imax (x0, x1) : iabs (gy16 - y0);
			if (d < bestD) { bestD = d; best = k; }
		}
	if (best < 0) return -1;
	if (pin) *pin = r[best].pin;
	return r[best].dst;
}

// ---- the evaluation ------------------------------------------------------------------------------------------------------
static int gate_out (int type, int a, int b)
{
	switch (type)
	{
	case P_NOT:  return !a;
	case P_AND:  return a & b;
	case P_OR:   return a | b;
	case P_XOR:  return a ^ b;
	case P_NAND: return !(a & b);
	case P_NOR:  return !(a | b);
	}
	return 0;
}
// Depths, values and the determined flags; step < 0: live (everything computed)
static void eval_all (const Circuit &c, unsigned inputs, int step, Eval &e)
{
	bool done[MAXPARTS] = { false };
	e.maxDepth = 0;
	for (int i = 0; i < c.n; i++) { e.v[i] = L0; e.det[i] = true; e.depth[i] = 0; }
	for (int k = 0; k < c.ni; k++) { e.v[k] = (unsigned char) ((inputs >> (c.ni - 1 - k)) & 1); done[k] = true; }
	// (no loop on the board: each pass finishes at least one part whose inputs are all finished)
	for (int left = c.n - c.ni; left > 0; )
	{
		int before = left;
		for (int i = c.ni; i < c.n; i++)
		{
			if (done[i]) continue;
			int np = c.pins (i), ready = 1;
			for (int k = 0; k < np; k++) if (c.p[i].in[k] >= 0 && !done[c.p[i].in[k]]) ready = 0;
			if (!ready) continue;
			int val[2] = { 0, 0 }, d = 0; bool det = true, unknown = false;
			for (int k = 0; k < np; k++)
			{
				int s = c.p[i].in[k];
				if (s < 0) { det = false; continue; }
				val[k] = e.v[s] == L1; det = det && e.det[s]; unknown = unknown || e.v[s] == LX;
				d = imax (d, e.depth[s]);
			}
			if (c.p[i].type == P_LAMP)
			{
				e.depth[i] = d;
				e.v[i] = (unsigned char) (unknown ? (int) LX : val[0]);
			}
			else
			{
				e.depth[i] = d + 1;
				e.maxDepth = imax (e.maxDepth, d + 1);
				e.v[i] = (unsigned char) ((step >= 0 && d + 1 > step) || unknown ? (int) LX : gate_out (c.p[i].type, val[0], val[1]));
			}
			e.det[i] = det;
			done[i] = true; left--;
		}
		if (left == before) break;				// (cannot happen: a loop)
	}
}
void evaluate (const Circuit &c, unsigned inputs, Eval &e) { eval_all (c, inputs, -1, e); }
void evaluate_to (const Circuit &c, unsigned inputs, int step, Eval &e) { eval_all (c, inputs, step < 0 ? 0 : step, e); }

// ---- the Check -------------------------------------------------------------------------------------------------------------
int stars_for (int gates, int par3, int par2, bool won)
{
	if (!won) return 0;
	return gates <= par3 ? 3 : gates <= par2 ? 2 : 1;
}
CheckResult check (const Circuit &c, const Level &L)
{
	CheckResult r;
	memset (&r, 0, sizeof r);
	r.err = E_OK; r.errPart = -1; r.firstWrong = -1; r.gates = c.gates ();
	for (int i = c.ni; i < c.ni + c.no; i++) if (c.p[i].in[0] < 0) { r.err = E_LAMP_OPEN; r.errPart = i; return r; }
	for (int i = c.ni + c.no; i < c.n; i++)
		for (int k = 0; k < c.pins (i); k++) if (c.p[i].in[k] < 0) { r.err = E_GATE_OPEN; r.errPart = i; return r; }
	Eval e;
	for (int row = 0; row < L.rows () && row < MAXROWS; row++)
	{
		evaluate (c, (unsigned) row, e);
		bool bad = false;
		for (int o = 0; o < c.no; o++)
		{
			int got = e.v[c.ni + o] == L1;
			if (got) r.got[o] |= 1u << row;
			if (got != (int) ((L.want[o] >> row) & 1)) bad = true;
		}
		if (bad) { r.wrong |= 1u << row; r.nwrong++; if (r.firstWrong < 0) r.firstWrong = row; }
	}
	r.won = r.nwrong == 0;
	r.stars = stars_for (r.gates, L.par3, L.par2, r.won);
	return r;
}

// ---- the circuit text ------------------------------------------------------------------------------------------------------
namespace {
struct Out
{
	char *d; int cap, n;
	Out (char *b, int c) : d (b), cap (c), n (0) { if (cap > 0) d[0] = 0; }
	void put (const char *s) { for (; *s; s++) { if (n + 1 < cap) { d[n] = *s; d[n + 1] = 0; } n++; } }
	void num (int v) { char b[16]; snprintf (b, sizeof b, "%d", v); put (b); }
	void nl () { if (n) put ("\n"); }
};
}
int write_text (const Circuit &c, char *out, int cap)
{
	Out o (out, cap);
	for (int i = c.ni + c.no; i < c.n; i++)
	{
		o.nl (); o.put ("part "); o.put (c.p[i].name); o.put (" "); o.put (type_name (c.p[i].type));
		o.put (" "); o.num (c.p[i].x); o.put (" "); o.num (c.p[i].y);
	}
	for (int pass = 0; pass < 2; pass++)
		for (int i = pass ? c.ni : c.ni + c.no; i < (pass ? c.ni + c.no : c.n); i++)
			for (int k = 0; k < c.pins (i); k++)
			{
				int s = c.p[i].in[k];
				if (s < 0) continue;
				o.nl (); o.put ("wire "); o.put (c.p[s].name); o.put (" "); o.put (c.p[i].name);
				if (!pass) { o.put (k ? ".2" : ".1"); }
			}
	return o.n;
}
// The words of a line (up to 6; spaces and tabs between them)
static int words (char *line, char **w, int max)
{
	int n = 0;
	for (char *p = line; *p; )
	{
		while (*p == ' ' || *p == '\t') *p++ = 0;
		if (!*p) break;
		if (n == max) return max + 1;
		w[n++] = p;
		while (*p && *p != ' ' && *p != '\t') p++;
	}
	return n;
}
static bool number (const char *s, int *v)
{
	int x = 0, k = 0, neg = 0;
	if (s[0] == '-') { neg = 1; k = 1; }
	if (!s[k]) return false;
	for (; s[k]; k++) { if (s[k] < '0' || s[k] > '9' || x > 100000) return false; x = x * 10 + s[k] - '0'; }
	*v = neg ? -x : x;
	return true;
}
static bool gate_name_ok (const char *s)
{
	int n = (int) strlen (s);
	if (n < 1 || n > NAMEL - 1 || s[0] < 'a' || s[0] > 'z') return false;
	for (int i = 1; i < n; i++) if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9'))) return false;
	return true;
}
bool read_text (Circuit &c, const Level &L, const char *text, Err *e, int *line)
{
	Circuit t;
	bool hasPos[MAXPARTS] = { false };
	int ln = 0;
	Err err = E_OK;
	t.setup (L);
	if (e) *e = E_OK;
	if (line) *line = 0;
	// pass 1: the parts (their names, types, places); pass 2: the wires
	for (int pass = 0; pass < 2 && err == E_OK; pass++)
	{
		ln = 0;
		for (const char *p = text ? text : ""; *p && err == E_OK; )
		{
			const char *q = p; while (*q && *q != '\n') q++;
			char buf[200]; int len = (int) (q - p);
			ln++;
			if (len >= (int) sizeof buf) { err = E_SYNTAX; break; }
			memcpy (buf, p, (size_t) len); buf[len] = 0;
			if (len && buf[len - 1] == '\r') buf[len - 1] = 0;
			p = *q ? q + 1 : q;
			char *w[6]; int nw = words (buf, w, 5);
			if (!nw || w[0][0] == '#') continue;
			if (!strcmp (w[0], "part"))
			{
				if (pass) continue;
				if (nw != 3 && nw != 5) { err = E_SYNTAX; break; }
				int type = type_of (w[2]);
				if (type < 0) { err = E_TYPE; break; }
				if (!gate_name_ok (w[1]) || t.find (w[1]) >= 0) { err = E_NAME; break; }
				if (!L.allows (type)) { err = E_NOT_ALLOWED; break; }
				if (t.gates () >= MAXGATES) { err = E_FULL; break; }
				int x = 0, y = 0;
				if (nw == 5)
				{
					if (!number (w[3], &x) || !number (w[4], &y)) { err = E_SYNTAX; break; }
					Err r = t.placeErr (type, x, y);
					if (r != E_OK) { err = r; break; }
					hasPos[t.n] = true;
				}
				Part &g = t.p[t.n++];
				scpy (g.name, w[1], sizeof g.name);
				g.type = (unsigned char) type; g.x = (short) x; g.y = (short) y; g.in[0] = g.in[1] = -1;
			}
			else if (!strcmp (w[0], "wire"))
			{
				if (!pass) continue;
				if (nw != 3) { err = E_SYNTAX; break; }
				int src = t.find (w[1]);
				if (src < 0) { err = strchr (w[1], '.') ? E_PIN : E_NAME; break; }
				if (t.p[src].type == P_LAMP) { err = E_PIN; break; }
				char to[NAMEL + 4]; scpy (to, w[2], sizeof to);
				int pin = 0;
				char *dot = strchr (to, '.');
				if (dot) { *dot = 0; if (!strcmp (dot + 1, "1")) pin = 0; else if (!strcmp (dot + 1, "2")) pin = 1; else { err = E_PIN; break; } }
				int dst = t.find (to);
				if (dst < 0) { err = E_NAME; break; }
				if (t.p[dst].type == P_SWITCH || (t.p[dst].type == P_LAMP) == (dot != 0) || pin >= t.pins (dst)) { err = E_PIN; break; }
				if (t.p[dst].in[pin] >= 0) { err = E_DOUBLE; break; }
				err = t.connect (src, dst, pin);
			}
			else err = E_SYNTAX;
		}
	}
	if (err != E_OK)
	{
		if (e) *e = err;
		if (line) *line = ln;
		return false;
	}
	// the gates without a place: in columns by their depth, spread as the fixed parts
	Eval ev; evaluate (t, 0, ev);
	int perDepth[MAXGATES + 2] = { 0 }, seen[MAXGATES + 2] = { 0 };
	for (int i = t.ni + t.no; i < t.n; i++) if (!hasPos[i]) perDepth[imin (ev.depth[i], MAXGATES + 1)]++;
	for (int i = t.ni + t.no; i < t.n; i++)
	{
		if (hasPos[i]) continue;
		int d = imin (ev.depth[i], MAXGATES + 1), k = seen[d]++, m = perDepth[d];
		int x = GATE_MINX + 1 + 6 * (d - 1), y = (2 * k + 1) * BOARD_H / (2 * m) - 2;
		if (t.placeErr (t.p[i].type, x, y, i) != E_OK)		// (no room there: the first free place, column by column)
		{
			bool found = false;
			for (int cx = GATE_MINX; cx + GATE_W <= GATE_MAXX && !found; cx++)
				for (int cy = 0; cy + GATE_H <= BOARD_H && !found; cy++)
					if (t.placeErr (t.p[i].type, cx, cy, i) == E_OK) { x = cx; y = cy; found = true; }
			if (!found) { if (e) *e = E_FULL; if (line) *line = 0; return false; }
		}
		t.p[i].x = (short) x; t.p[i].y = (short) y;
		hasPos[i] = true;
	}
	c = t;
	return true;
}
int truth_table_text (const Level &L, const CheckResult *r, char *out, int cap)
{
	Out o (out, cap);
	bool got = r && r->err == E_OK;
	for (int k = 0; k < L.ninputs; k++) { if (k) o.put ("\t"); o.put (L.inName[k]); }
	for (int k = 0; k < L.noutputs; k++) { o.put ("\t"); o.put (L.outName[k]); }
	if (got) for (int k = 0; k < L.noutputs; k++) { o.put ("\t"); o.put (L.outName[k]); o.put ("'"); }
	o.put ("\n");
	for (int row = 0; row < L.rows (); row++)
	{
		for (int k = 0; k < L.ninputs; k++) { if (k) o.put ("\t"); o.put ((row >> (L.ninputs - 1 - k)) & 1 ? "1" : "0"); }
		for (int k = 0; k < L.noutputs; k++) { o.put ("\t"); o.put ((L.want[k] >> row) & 1 ? "1" : "0"); }
		if (got) for (int k = 0; k < L.noutputs; k++) { o.put ("\t"); o.put ((r->got[k] >> row) & 1 ? "1" : "0"); }
		o.put ("\n");
	}
	return o.n;
}

// ---- undo / redo -----------------------------------------------------------------------------------------------------------
static char *snapshot (const Circuit &c)
{
	static char buf[TEXTCAP];
	int n = write_text (c, buf, sizeof buf);
	if (n >= (int) sizeof buf) n = (int) sizeof buf - 1;
	char *s = (char *) malloc ((size_t) n + 1);
	if (s) { memcpy (s, buf, (size_t) n); s[n] = 0; }
	return s;
}
void History::reset ()
{
	for (int i = 0; i < n; i++) { free (s[i]); s[i] = 0; }
	n = cur = 0;
}
void History::start (const Circuit &c)
{
	reset ();
	s[0] = snapshot (c); n = s[0] ? 1 : 0; cur = 0;
}
void History::push (const Circuit &c)
{
	char *t = snapshot (c);
	if (!t) return;
	if (n && !strcmp (s[cur], t)) { free (t); return; }	// (nothing changed)
	for (int i = cur + 1; i < n; i++) { free (s[i]); s[i] = 0; }
	n = n ? cur + 1 : 0;
	if (n == HIST_STEPS + 1)				// (the oldest dropped)
	{
		free (s[0]);
		for (int i = 0; i + 1 < n; i++) s[i] = s[i + 1];
		n--;
	}
	s[n++] = t; cur = n - 1;
}
bool History::undo (Circuit &c, const Level &L)
{
	if (!canUndo () || !read_text (c, L, s[cur - 1])) return false;
	cur--;
	return true;
}
bool History::redo (Circuit &c, const Level &L)
{
	if (!canRedo () || !read_text (c, L, s[cur + 1])) return false;
	cur++;
	return true;
}

} // namespace circuits
