//
// table.cpp -- Pinball's table reader (table.h): the ".table" text through FileKit's fk_kv, every block checked in the
// file's order, then the cross-checks (the outline, the flippers, the plunger, the banks, the rules' references). The
// first error found refuses the file with its line and its reason; never a half-loaded table.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "table.h"
#include <stdio.h>

namespace pinball {

// ---- the errors' formats (table.h lists them for the translation) ------------------------------------------------------
static const char *const E_BLOCK = "unknown block [%s]";
static const char *const E_FIRST = "[table] must be the first block, once";
static const char *const E_NEEDS = "[%s] needs %s";
static const char *const E_VALUE = "bad value for %s";
static const char *const E_RANGE = "%s out of range";
static const char *const E_TWICE = "id %s used twice";
static const char *const E_OUTLINE = "the table needs one closed wall with id = outline";
static const char *const E_FLIPPERS = "the table needs 2 or 3 flippers, a left one and a right one";
static const char *const E_PLUNGER = "the table needs one [plunger]";
static const char *const E_UNKNOWN = "unknown %s";
static const char *const E_BANK = "bank %s needs 2 to 8 targets";
static const char *const E_MANY = "too many %s (max %s)";
static const char *const E_BIG = "the file is too big";
static const char *const E_READ = "cannot read the file";

// ---- small helpers ------------------------------------------------------------------------------------------------------
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; if (cap > 0) d[i] = 0; }
static bool sp (char c) { return c == ' ' || c == '\t'; }
static bool digit (char c) { return c >= '0' && c <= '9'; }
static int utf8_chars (const char *s) { int n = 0; for (; *s; s++) if (((unsigned char) *s & 0xC0) != 0x80) n++; return n; }
static double dmin (double a, double b) { return a < b ? a : b; }
static double dmax (double a, double b) { return a > b ? a : b; }

double pb_sin (double a)
{
	const double TWO_PI = 2 * PB_PI;
	if (a > 1000 || a < -1000) a -= TWO_PI * (double) (long) (a / TWO_PI);
	while (a > PB_PI) a -= TWO_PI;
	while (a < -PB_PI) a += TWO_PI;
	if (a > PB_PI / 2) a = PB_PI - a;
	else if (a < -PB_PI / 2) a = -PB_PI - a;
	double x2 = a * a;				// Taylor to x^11: < 1e-8 on [-pi/2, pi/2]
	return a * (1 + x2 * (-1.0 / 6 + x2 * (1.0 / 120 + x2 * (-1.0 / 5040 + x2 * (1.0 / 362880 - x2 / 39916800.0)))));
}
double pb_cos (double a) { return pb_sin (a + PB_PI / 2); }

// number := ["-"] digits ["." digits] (no exponent, no locale): s advanced past it -> true
static bool read_num (const char *&s, double &v)
{
	const char *p = s;
	bool neg = false;
	if (*p == '-') { neg = true; p++; }
	if (!digit (*p)) return false;
	double x = 0;
	while (digit (*p)) x = x * 10 + (*p++ - '0');
	if (*p == '.')
	{
		p++;
		if (!digit (*p)) return false;
		double f = 0.1;
		while (digit (*p)) { x += (*p++ - '0') * f; f *= 0.1; }
	}
	v = neg ? -x : x;
	s = p;
	return true;
}
// Numbers separated by spaces and/or one comma -> how many (at most cap), -1: does not read or more than cap
static int read_nums (const char *s, double *out, int cap)
{
	int n = 0;
	while (sp (*s)) s++;
	if (!*s) return 0;
	for (;;)
	{
		double v;
		if (!read_num (s, v)) return -1;
		if (n == cap) return -1;
		out[n++] = v;
		bool sep = false;
		while (sp (*s)) { s++; sep = true; }
		if (*s == ',') { s++; sep = true; while (sp (*s)) s++; if (!*s) return -1; }
		if (!*s) return n;
		if (!sep) return -1;
	}
}
static bool is_id (const char *s)
{
	int n = 0;
	for (; s[n]; n++) if (!((s[n] >= 'a' && s[n] <= 'z') || digit (s[n]) || s[n] == '_' || s[n] == '-')) return false;
	return n >= 1 && n < IDL;
}
static Vec dir_of (int k) { static const Vec D[4] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } }; return D[k]; }

// ---- the reader's state -------------------------------------------------------------------------------------------------
struct Ctx
{
	fk_kv *kv;
	Table &t;
	LoadError &e;
	int b;					// the block being read
	const char *bname;			// its name
	int bline;				// its header's line
	int i0, i1;				// its entries
	// what the cross-checks need
	int tableLine, rotateLine;
	char rotate[IDL];
	int nOutline, plungers;
	int bankLine[MAXBANK];
	char ruleWhen[MAXRULE][64]; int ruleLine[MAXRULE];
	char wallId[MAXWALL][IDL];
	Ctx (fk_kv *k, Table &tt, LoadError &ee) : kv (k), t (tt), e (ee), b (0), bname (""), bline (0), i0 (0), i1 (0),
		tableLine (0), rotateLine (0), nOutline (0), plungers (0) { rotate[0] = 0; }
};

static bool err (Ctx &c, int line, const char *fmt, const char *a0 = "", const char *a1 = "")
{
	c.e.line = line; c.e.fmt = fmt;
	scpy (c.e.arg[0], a0, sizeof c.e.arg[0]);
	scpy (c.e.arg[1], a1, sizeof c.e.arg[1]);
	snprintf (c.e.reason, sizeof c.e.reason, fmt, c.e.arg[0], c.e.arg[1]);
	return false;
}
static int find (Ctx &c, const char *key)
{
	for (int i = c.i0; i < c.i1; i++) if (!strcmp (fk_kv_key (c.kv, i), key)) return i;
	return -1;
}
static int line_of (Ctx &c, int i) { return fk_kv_line (c.kv, i); }
static bool needs (Ctx &c, const char *key) { return err (c, c.bline, E_NEEDS, c.bname, key); }

// A number in [lo, hi] (integer: a whole one); absent -> def, or the "needs" error when def is 0 (req)
static bool get_num (Ctx &c, const char *key, double &v, double lo, double hi, const double *def, bool integer = false)
{
	int i = find (c, key);
	if (i < 0) { if (!def) return needs (c, key); v = *def; return true; }
	double x[1];
	if (read_nums (fk_kv_value (c.kv, i), x, 1) != 1) return err (c, line_of (c, i), E_VALUE, key);
	if (integer && x[0] != (double) (long) x[0]) return err (c, line_of (c, i), E_VALUE, key);
	if (x[0] < lo || x[0] > hi) return err (c, line_of (c, i), E_RANGE, key);
	v = x[0];
	return true;
}
static bool get_int (Ctx &c, const char *key, int &v, int lo, int hi, int def, bool req = false)
{
	double d = def, x;
	if (!get_num (c, key, x, lo, hi, req ? 0 : &d, true)) return false;
	v = (int) x;
	return true;
}
static bool get_dbl (Ctx &c, const char *key, double &v, double lo, double hi, double def, bool req = false)
{
	return get_num (c, key, v, lo, hi, req ? 0 : &def);
}
// n numbers (a point, a size, a velocity); inside: every pair a point inside the table
static bool get_nums (Ctx &c, const char *key, double *v, int n, double lo, double hi, bool inside, bool req, int *line = 0)
{
	int i = find (c, key);
	if (i < 0) return req ? needs (c, key) : true;
	if (line) *line = line_of (c, i);
	if (read_nums (fk_kv_value (c.kv, i), v, n) != n) return err (c, line_of (c, i), E_VALUE, key);
	for (int k = 0; k < n; k++)
	{
		if (v[k] < lo || v[k] > hi) return err (c, line_of (c, i), E_RANGE, key);
		if (inside && (v[k] < 0 || v[k] > (k & 1 ? c.t.h : c.t.w))) return err (c, line_of (c, i), E_RANGE, key);
	}
	return true;
}
static bool get_point (Ctx &c, const char *key, Vec &p, bool req = true)
{
	double v[2] = { p.x, p.y };
	if (!get_nums (c, key, v, 2, -1e9, 1e9, true, req)) return false;
	p.x = v[0]; p.y = v[1];
	return true;
}
static bool get_vel (Ctx &c, const char *key, Vec &p, bool req = true)
{
	double v[2] = { p.x, p.y };
	if (!get_nums (c, key, v, 2, -4000, 4000, false, req)) return false;
	p.x = v[0]; p.y = v[1];
	return true;
}
// A list of points (at least minN, at most cap), each inside the table
static bool get_points (Ctx &c, const char *key, Vec *out, int cap, int &n, int minN)
{
	int i = find (c, key);
	if (i < 0) return needs (c, key);
	static double v[2 * (MAXSEG + 1) + 2];
	int k = read_nums (fk_kv_value (c.kv, i), v, 2 * (MAXSEG + 1) + 2);
	if (k < 0 || (k & 1)) return err (c, line_of (c, i), E_VALUE, key);
	n = k / 2;
	if (n < minN) return err (c, line_of (c, i), E_VALUE, key);
	if (n > cap)
	{
		char m[16]; snprintf (m, sizeof m, "%d", cap);
		return err (c, line_of (c, i), E_MANY, "points", m);
	}
	for (int j = 0; j < n; j++)
	{
		if (v[2 * j] < 0 || v[2 * j] > c.t.w || v[2 * j + 1] < 0 || v[2 * j + 1] > c.t.h) return err (c, line_of (c, i), E_RANGE, key);
		out[j].x = v[2 * j]; out[j].y = v[2 * j + 1];
	}
	return true;
}
static bool get_colour (Ctx &c, const char *key, unsigned &col, unsigned def, bool req = false)
{
	int i = find (c, key);
	if (i < 0) { if (req) return needs (c, key); col = def; return true; }
	const char *s = fk_kv_value (c.kv, i);
	if (s[0] != '#' || strlen (s) != 7) return err (c, line_of (c, i), E_VALUE, key);
	unsigned v = 0;
	for (int k = 1; k < 7; k++)
	{
		char h = s[k];
		int d = digit (h) ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
		if (d < 0) return err (c, line_of (c, i), E_VALUE, key);
		v = v << 4 | (unsigned) d;
	}
	col = v;
	return true;
}
static bool get_bool (Ctx &c, const char *key, bool &v, bool def)
{
	int i = find (c, key);
	if (i < 0) { v = def; return true; }
	const char *s = fk_kv_value (c.kv, i);
	if (strcmp (s, "0") && strcmp (s, "1")) return err (c, line_of (c, i), E_VALUE, key);
	v = s[0] == '1';
	return true;
}
// One of the words (its index); absent: -1 or the "needs" error
static bool get_word (Ctx &c, const char *key, const char *const *words, int nwords, int &v, bool req)
{
	int i = find (c, key);
	if (i < 0) { if (req) return needs (c, key); v = -1; return true; }
	const char *s = fk_kv_value (c.kv, i);
	for (int k = 0; k < nwords; k++) if (!strcmp (s, words[k])) { v = k; return true; }
	return err (c, line_of (c, i), E_VALUE, key);
}
// A text and its French (key.fr), each at most maxc characters
static bool get_text (Ctx &c, const char *key, Text &t, int maxc, bool req)
{
	t.en[0] = t.fr[0] = 0;
	char k2[32]; snprintf (k2, sizeof k2, "%s.fr", key);
	int i = find (c, key);
	if (i < 0) { if (req) return needs (c, key); }
	else
	{
		const char *s = fk_kv_value (c.kv, i);
		if (utf8_chars (s) > maxc || strlen (s) >= TEXTL) return err (c, line_of (c, i), E_RANGE, key);
		scpy (t.en, s, TEXTL);
	}
	int j = find (c, k2);
	if (j >= 0)
	{
		const char *s = fk_kv_value (c.kv, j);
		if (utf8_chars (s) > maxc || strlen (s) >= TEXTL) return err (c, line_of (c, j), E_RANGE, k2);
		scpy (t.fr, s, TEXTL);
	}
	return true;
}

// ---- the ids --------------------------------------------------------------------------------------------------------------
static bool id_used (Ctx &c, const char *id)
{
	const Table &t = c.t;
	for (int i = 0; i < t.ncirc; i++) if (!strcmp (t.circ[i].id, id)) return true;
	for (int i = 0; i < t.nsling; i++) if (!strcmp (t.sling[i].id, id)) return true;
	for (int i = 0; i < t.ntgt; i++) if (!strcmp (t.tgt[i].id, id)) return true;
	for (int i = 0; i < t.nlane; i++) if (!strcmp (t.lane[i].id, id)) return true;
	for (int i = 0; i < t.nramp; i++) if (!strcmp (t.ramp[i].id, id)) return true;
	for (int i = 0; i < t.nsaucer; i++) if (!strcmp (t.saucer[i].id, id)) return true;
	for (int i = 0; i < t.nwall; i++) if (!strcmp (c.wallId[i], id)) return true;
	return false;
}
// The block's id into out: req, the grammar, unique
static bool get_id (Ctx &c, char *out, bool req)
{
	out[0] = 0;
	int i = find (c, "id");
	if (i < 0) return req ? needs (c, "id") : true;
	const char *s = fk_kv_value (c.kv, i);
	if (!is_id (s)) return err (c, line_of (c, i), E_VALUE, "id");
	if (id_used (c, s)) return err (c, line_of (c, i), E_TWICE, s);
	scpy (out, s, IDL);
	return true;
}
static bool too_many (Ctx &c, int n, int max, const char *things)
{
	if (n < max) return false;
	char m[16]; snprintf (m, sizeof m, "%d", max);
	err (c, c.bline, E_MANY, things, m);
	return true;
}
// A collision segment -> its index (the caller checked the room)
static int add_seg (Table &t, Vec a, Vec b, double bounce, double friction, int kind, int elem, int wall)
{
	Seg &s = t.seg[t.nseg];
	s.a = a; s.b = b; s.bounce = bounce; s.friction = friction; s.kind = kind; s.elem = elem; s.wall = wall;
	s.lo.x = dmin (a.x, b.x); s.lo.y = dmin (a.y, b.y); s.hi.x = dmax (a.x, b.x); s.hi.y = dmax (a.y, b.y);
	return t.nseg++;
}

// ---- the blocks ---------------------------------------------------------------------------------------------------------
static const char *const WORDS_PASS[] = { "up", "down", "left", "right" };
static const char *const WORDS_SIDE[] = { "left", "right" };
static const char *const WORDS_KIND[] = { "drop", "standup" };

static bool blk_table (Ctx &c)
{
	Table &t = c.t;
	double d;
	if (!get_num (c, "format", d, -1e9, 1e9, 0, true)) return false;
	if (d != 1) return err (c, line_of (c, find (c, "format")), E_RANGE, "format");
	if (!get_text (c, "name", t.name, 40, true) || !get_text (c, "goal", t.goal, 160, false)) return false;
	double sz[2];
	if (!get_nums (c, "size", sz, 2, 200, 2000, false, true)) return false;
	t.w = sz[0]; t.h = sz[1];
	if (!get_dbl (c, "gravity", t.gravity, 200, 5000, 1400) || !get_dbl (c, "ball", t.ball, 6, 30, 13)
	    || !get_int (c, "balls", t.balls, 1, 9, 3) || !get_dbl (c, "ballsave", t.ballsave, 0, 30, 8)
	    || !get_dbl (c, "drain", t.drain, 0, t.h, 0, true) || !get_colour (c, "background", t.background, 0x101828))
		return false;
	int i = find (c, "rotate");
	if (i >= 0)
	{
		if (!is_id (fk_kv_value (c.kv, i))) return err (c, line_of (c, i), E_VALUE, "rotate");
		scpy (c.rotate, fk_kv_value (c.kv, i), IDL);
		c.rotateLine = line_of (c, i);
	}
	return true;
}
static bool blk_wall (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.nwall, MAXWALL, "walls")) return false;
	Wall &w = t.wall[t.nwall];
	char *id = c.wallId[t.nwall];
	static Vec p[MAXSEG + 1];
	int n;
	if (!get_id (c, id, false) || !get_points (c, "points", p, MAXSEG + 1, n, 2)
	    || !get_bool (c, "closed", w.closed, false) || !get_dbl (c, "bounce", w.bounce, 0, 1.5, 0.5)
	    || !get_dbl (c, "friction", w.friction, 0, 1, 0.1) || !get_colour (c, "colour", w.colour, 0x8090A0)
	    || !get_int (c, "width", w.width, 1, 40, 4))
		return false;
	w.outline = !strcmp (id, "outline");
	if (w.outline)
	{
		if (++c.nOutline > 1) return err (c, line_of (c, find (c, "id")), E_OUTLINE);
		if (!w.closed || n < 3) return err (c, c.bline, E_OUTLINE);
		for (int k = 0; k < n; k++) t.outline[k] = p[k];
		t.noutline = n;
	}
	int segs = w.closed ? n : n - 1;
	if (t.nseg + segs > MAXSEG)
	{
		char m[16]; snprintf (m, sizeof m, "%d", (int) MAXSEG);
		return err (c, c.bline, E_MANY, "wall segments", m);
	}
	w.first = t.nseg; w.n = segs;
	for (int k = 0; k < segs; k++) add_seg (t, p[k], p[(k + 1) % n], w.bounce, w.friction, S_WALL, -1, t.nwall);
	t.nwall++;
	return true;
}
static bool blk_arc (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.narc, MAXARC, "arcs")) return false;
	Arc &a = t.arc[t.narc];
	if (!get_point (c, "centre", a.c) || !get_dbl (c, "radius", a.r, 1, 4000, 0, true)
	    || !get_dbl (c, "from", a.from, -360, 720, 0, true) || !get_dbl (c, "to", a.to, -360, 720, 0, true)
	    || !get_dbl (c, "bounce", a.bounce, 0, 1.5, 0.5) || !get_dbl (c, "friction", a.friction, 0, 1, 0.1)
	    || !get_colour (c, "colour", a.colour, 0x8090A0) || !get_int (c, "width", a.width, 1, 40, 4))
		return false;
	a.span = a.to - a.from;
	while (a.span <= 0) a.span += 360;
	if (a.span > 360) return err (c, line_of (c, find (c, "to")), E_RANGE, "to");
	a.to = a.from + a.span;
	double f = a.from * PB_PI / 180, g = a.to * PB_PI / 180;
	a.fu.x = pb_cos (f); a.fu.y = pb_sin (f);
	a.tu.x = pb_cos (g); a.tu.y = pb_sin (g);
	t.narc++;
	return true;
}
static bool circle_room (Ctx &c) { return !too_many (c, c.t.ncirc + c.t.nsaucer, MAXCIRC, "posts, bumpers and saucers"); }
static bool blk_post (Ctx &c)
{
	Table &t = c.t;
	if (!circle_room (c)) return false;
	Circle &o = t.circ[t.ncirc];
	o.id[0] = 0; o.kind = C_POST; o.kick = 0; o.score = 0;
	if (!get_point (c, "at", o.c) || !get_dbl (c, "radius", o.r, 1, 200, 0, true) || !get_dbl (c, "bounce", o.bounce, 0, 1.5, 0.6)
	    || !get_colour (c, "colour", o.colour, 0xF4F4F4))
		return false;
	t.ncirc++;
	return true;
}
static bool blk_bumper (Ctx &c)
{
	Table &t = c.t;
	if (!circle_room (c)) return false;
	Circle &o = t.circ[t.ncirc];
	o.kind = C_BUMPER; o.bounce = 0.6;
	if (!get_id (c, o.id, true) || !get_point (c, "at", o.c) || !get_dbl (c, "radius", o.r, 4, 200, 0, true)
	    || !get_dbl (c, "kick", o.kick, 0, 4000, 900) || !get_int (c, "score", o.score, 0, 1000000, 100)
	    || !get_colour (c, "colour", o.colour, 0xE04060))
		return false;
	t.ncirc++;
	return true;
}
static bool face (Ctx &c, Vec &a, Vec &b)
{
	if (!get_point (c, "a", a) || !get_point (c, "b", b)) return false;
	if (a.x == b.x && a.y == b.y) return err (c, line_of (c, find (c, "b")), E_VALUE, "b");
	if (c.t.nseg >= MAXSEG)
	{
		char m[16]; snprintf (m, sizeof m, "%d", (int) MAXSEG);
		return err (c, c.bline, E_MANY, "wall segments", m);
	}
	return true;
}
static bool blk_sling (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.nsling, MAXSLING, "slings")) return false;
	Sling &s = t.sling[t.nsling];
	Vec a, b;
	if (!get_id (c, s.id, true) || !face (c, a, b) || !get_dbl (c, "kick", s.kick, 0, 4000, 700)
	    || !get_int (c, "score", s.score, 0, 1000000, 10) || !get_colour (c, "colour", s.colour, 0xE0559A))
		return false;
	s.seg = add_seg (t, a, b, 0.5, 0, S_SLING, t.nsling, -1);
	t.nsling++;
	return true;
}
static int bank_of (Ctx &c, const char *id, int line)
{
	Table &t = c.t;
	for (int i = 0; i < t.nbank; i++) if (!strcmp (t.bank[i].id, id)) return i;
	if (t.nbank >= MAXBANK) return -1;
	scpy (t.bank[t.nbank].id, id, IDL);
	t.bank[t.nbank].n = 0;
	c.bankLine[t.nbank] = line;
	return t.nbank++;
}
static bool blk_target (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.ntgt, MAXTGT, "targets")) return false;
	Target &g = t.tgt[t.ntgt];
	Vec a, b;
	int kind;
	if (!get_id (c, g.id, true) || !face (c, a, b) || !get_word (c, "kind", WORDS_KIND, 2, kind, true)) return false;
	g.drop = kind == 0;
	g.bank = -1;
	if (!get_int (c, "score", g.score, 0, 1000000, g.drop ? 500 : 250) || !get_colour (c, "colour", g.colour, g.drop ? 0xF2B33D : 0x8CE05A))
		return false;
	if (g.drop)
	{
		int i = find (c, "bank");
		if (i < 0) return needs (c, "bank");
		const char *s = fk_kv_value (c.kv, i);
		if (!is_id (s)) return err (c, line_of (c, i), E_VALUE, "bank");
		int k = bank_of (c, s, line_of (c, i));
		if (k < 0) { char m[16]; snprintf (m, sizeof m, "%d", (int) MAXBANK); return err (c, line_of (c, i), E_MANY, "banks", m); }
		if (t.bank[k].n >= 8) return err (c, line_of (c, i), E_BANK, s);
		t.bank[k].tgt[t.bank[k].n++] = t.ntgt;
		g.bank = k;
	}
	g.seg = add_seg (t, a, b, 0.5, 0.1, S_TARGET, t.ntgt, -1);
	t.ntgt++;
	return true;
}
static bool blk_lane (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.nlane, MAXLANE, "lanes")) return false;
	Lane &l = t.lane[t.nlane];
	double r[4];
	int rl = 0;
	if (!get_id (c, l.id, true) || !get_nums (c, "rect", r, 4, 0, 4000, false, true, &rl)) return false;
	if (r[2] <= 0 || r[3] <= 0 || r[0] + r[2] > t.w || r[1] + r[3] > t.h) return err (c, rl, E_RANGE, "rect");
	l.x = r[0]; l.y = r[1]; l.w = r[2]; l.h = r[3];
	if (!get_int (c, "score", l.score, 0, 1000000, 50) || !get_colour (c, "colour", l.colour, 0xFFD34D)) return false;
	l.group = -1;
	int i = find (c, "group");
	if (i >= 0)
	{
		const char *s = fk_kv_value (c.kv, i);
		if (!is_id (s)) return err (c, line_of (c, i), E_VALUE, "group");
		for (int k = 0; k < t.ngroup; k++) if (!strcmp (t.group[k], s)) l.group = k;
		if (l.group < 0)
		{
			if (t.ngroup >= MAXGROUP) { char m[16]; snprintf (m, sizeof m, "%d", (int) MAXGROUP); return err (c, line_of (c, i), E_MANY, "lane groups", m); }
			scpy (t.group[t.ngroup], s, IDL);
			l.group = t.ngroup++;
		}
	}
	t.nlane++;
	return true;
}
// Can the segment a-b be crossed along pass? (not nearly parallel to it: within 60 degrees of its normal)
static bool crosses (Vec a, Vec b, Vec pass)
{
	double dx = b.x - a.x, dy = b.y - a.y, along = dx * pass.x + dy * pass.y;
	return along * along < 0.75 * (dx * dx + dy * dy);
}
static bool blk_gate (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.ngate, MAXGATE, "gates")) return false;
	Gate &g = t.gate[t.ngate];
	int k;
	if (!get_point (c, "a", g.a) || !get_point (c, "b", g.b) || !get_word (c, "pass", WORDS_PASS, 4, k, true)) return false;
	if (g.a.x == g.b.x && g.a.y == g.b.y) return err (c, line_of (c, find (c, "b")), E_VALUE, "b");
	g.pass = dir_of (k);
	if (!crosses (g.a, g.b, g.pass)) return err (c, line_of (c, find (c, "pass")), E_VALUE, "pass");
	t.ngate++;
	return true;
}
static bool blk_flipper (Ctx &c)
{
	Table &t = c.t;
	if (t.nflip >= MAXFLIP) return err (c, c.bline, E_FLIPPERS);
	Flip &f = t.flip[t.nflip];
	double r[2] = { 12, 6 }, sp2;
	if (!get_word (c, "side", WORDS_SIDE, 2, f.side, true) || !get_point (c, "pivot", f.pivot)
	    || !get_dbl (c, "length", f.len, 30, 200, 0, true) || !get_dbl (c, "rest", f.restDeg, -90, 90, 30)
	    || !get_dbl (c, "up", f.upDeg, -90, 90, -25) || !get_nums (c, "radius", r, 2, 2, 40, false, false)
	    || !get_dbl (c, "speed", sp2, 100, 5000, 1800) || !get_dbl (c, "bounce", f.bounce, 0, 1.5, 0.4)
	    || !get_colour (c, "colour", f.colour, 0xF2F4F8))
		return false;
	f.r0 = r[0]; f.r1 = r[1];
	f.speed = sp2 * PB_PI / 180;
	double rest = f.side == SIDE_LEFT ? f.restDeg : 180 - f.restDeg, up = f.side == SIDE_LEFT ? f.upDeg : 180 - f.upDeg;
	f.rest = rest * PB_PI / 180; f.up = up * PB_PI / 180;
	t.nflip++;
	return true;
}
static bool blk_plunger (Ctx &c)
{
	Table &t = c.t;
	if (++c.plungers > 1) return err (c, c.bline, E_PLUNGER);
	return get_point (c, "at", t.plunger) && get_dbl (c, "max", t.plungerMax, 100, 4000, 2600)
	       && get_dbl (c, "auto", t.plungerAuto, 100, 4000, 2300);
}
static bool blk_ramp (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.nramp, MAXRAMP, "ramps")) return false;
	Ramp &r = t.ramp[t.nramp];
	int k;
	if (!get_id (c, r.id, true) || !get_point (c, "a", r.a) || !get_point (c, "b", r.b)
	    || !get_word (c, "pass", WORDS_PASS, 4, k, true) || !get_points (c, "path", r.path, MAXPTS, r.npath, 2)
	    || !get_dbl (c, "time", r.time, 0.1, 10, 0.8) || !get_vel (c, "out", r.out)
	    || !get_int (c, "score", r.score, 0, 1000000, 1000) || !get_colour (c, "colour", r.colour, 0x4FA3FF)
	    || !get_int (c, "width", r.width, 4, 60, 24))
		return false;
	if (r.a.x == r.b.x && r.a.y == r.b.y) return err (c, line_of (c, find (c, "b")), E_VALUE, "b");
	r.pass = dir_of (k);
	if (!crosses (r.a, r.b, r.pass)) return err (c, line_of (c, find (c, "pass")), E_VALUE, "pass");
	t.nramp++;
	return true;
}
static bool blk_saucer (Ctx &c)
{
	Table &t = c.t;
	if (!circle_room (c)) return false;
	Saucer &s = t.saucer[t.nsaucer];
	if (!get_id (c, s.id, true) || !get_point (c, "at", s.c) || !get_dbl (c, "radius", s.r, 4, 100, 0, true)
	    || !get_dbl (c, "hold", s.hold, 0, 10, 1.5) || !get_vel (c, "out", s.out)
	    || !get_int (c, "score", s.score, 0, 1000000, 750) || !get_colour (c, "colour", s.colour, 0xFF8A3D))
		return false;
	t.nsaucer++;
	return true;
}
static bool blk_shape (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.nshape, MAXSHAPE, "shapes")) return false;
	Shape &s = t.shape[t.nshape];
	if (!get_points (c, "points", s.p, MAXPTS, s.n, 3) || !get_colour (c, "colour", s.colour, 0, true)) return false;
	t.nshape++;
	return true;
}
static bool blk_label (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.nlabel, MAXLABEL, "labels")) return false;
	Label &l = t.label[t.nlabel];
	if (!get_point (c, "at", l.at) || !get_text (c, "text", l.text, 60, true) || !get_int (c, "size", l.size, 1, 4, 1)
	    || !get_colour (c, "colour", l.colour, 0xFFFFFF) || !get_int (c, "angle", l.angle, 0, 90, 0))
		return false;
	if (l.angle != 0 && l.angle != 90) return err (c, line_of (c, find (c, "angle")), E_VALUE, "angle");
	t.nlabel++;
	return true;
}
// "score 1000; multiplier; multiball 2" -> the rule's actions
static bool parse_do (Ctx &c, Rule &r, const char *s, int line)
{
	r.nact = 0;
	char buf[256];
	scpy (buf, s, sizeof buf);
	char *p = buf;
	for (;;)
	{
		char *e = p;
		while (*e && *e != ';') e++;
		bool last = !*e;
		*e = 0;
		while (sp (*p)) p++;
		char *q = p + strlen (p);
		while (q > p && sp (q[-1])) *--q = 0;
		if (!*p) { if (last && r.nact) break; return err (c, line, E_VALUE, "do"); }
		if (r.nact >= MAXACT) { char m[16]; snprintf (m, sizeof m, "%d", (int) MAXACT); return err (c, line, E_MANY, "actions", m); }
		Action &a = r.act[r.nact];
		char w[16]; int n = 0;
		while (*p && !sp (*p) && n < 15) w[n++] = *p++;
		w[n] = 0;
		while (sp (*p)) p++;
		double v[1]; int k = *p ? read_nums (p, v, 1) : 0;
		if (k < 0 || (k == 1 && v[0] != (double) (long) v[0])) return err (c, line, E_VALUE, "do");
		if (!strcmp (w, "score") && k == 1 && v[0] >= 0 && v[0] <= 10000000) { a.kind = A_SCORE; a.n = (long) v[0]; }
		else if (!strcmp (w, "bonus") && k == 1 && v[0] >= 0 && v[0] <= 10000000) { a.kind = A_BONUS; a.n = (long) v[0]; }
		else if (!strcmp (w, "multiplier") && k == 0) { a.kind = A_MULT; a.n = 1; }
		else if (!strcmp (w, "multiball") && k == 1 && (v[0] == 2 || v[0] == 3)) { a.kind = A_MULTIBALL; a.n = (long) v[0]; }
		else if (!strcmp (w, "extraball") && k == 0) { a.kind = A_EXTRABALL; a.n = 1; }
		else if (!strcmp (w, "ballsave") && k == 1 && v[0] >= 1 && v[0] <= 30) { a.kind = A_BALLSAVE; a.n = (long) v[0]; }
		else return err (c, line, E_VALUE, "do");
		r.nact++;
		if (last) break;
		p = e + 1;
	}
	return true;
}
static bool blk_rule (Ctx &c)
{
	Table &t = c.t;
	if (too_many (c, t.nrule, MAXRULE, "rules")) return false;
	Rule &r = t.rule[t.nrule];
	int i = find (c, "when");
	if (i < 0) return needs (c, "when");
	scpy (c.ruleWhen[t.nrule], fk_kv_value (c.kv, i), sizeof c.ruleWhen[0]);
	c.ruleLine[t.nrule] = line_of (c, i);
	int d = find (c, "do");
	if (d < 0) return needs (c, "do");
	if (!parse_do (c, r, fk_kv_value (c.kv, d), line_of (c, d))) return false;
	if (!get_int (c, "count", r.count, 1, 99, 1) || !get_bool (c, "once", r.once, false) || !get_text (c, "message", r.message, 32, false))
		return false;
	t.nrule++;
	return true;
}

typedef bool (*BlockFn) (Ctx &);
static const struct { const char *name; BlockFn fn; } BLOCKS[] = {
	{ "wall", blk_wall }, { "arc", blk_arc }, { "post", blk_post }, { "bumper", blk_bumper }, { "sling", blk_sling },
	{ "target", blk_target }, { "lane", blk_lane }, { "gate", blk_gate }, { "flipper", blk_flipper },
	{ "plunger", blk_plunger }, { "ramp", blk_ramp }, { "saucer", blk_saucer }, { "shape", blk_shape },
	{ "label", blk_label }, { "rule", blk_rule } };

// ---- the cross-checks ---------------------------------------------------------------------------------------------------
static int find_named (const char *id, const char (*names)[IDL], int n)
{
	for (int i = 0; i < n; i++) if (!strcmp (names[i], id)) return i;
	return -1;
}
static bool resolve_rule (Ctx &c, int k)
{
	Table &t = c.t;
	Rule &r = t.rule[k];
	int line = c.ruleLine[k];
	char w[64]; scpy (w, c.ruleWhen[k], sizeof w);
	char *p = w;
	while (*p && !sp (*p)) p++;
	if (!*p) return err (c, line, E_VALUE, "when");
	*p++ = 0;
	while (sp (*p)) p++;
	const char *id = p;
	if (!is_id (id)) return err (c, line, E_VALUE, "when");
	int i = -1;
	if (!strcmp (w, "bank")) { r.when = EV_BANK; for (int j = 0; j < t.nbank; j++) if (!strcmp (t.bank[j].id, id)) i = j; }
	else if (!strcmp (w, "lanes")) { r.when = EV_LANES; i = find_named (id, t.group, t.ngroup); }
	else if (!strcmp (w, "ramp")) { r.when = EV_RAMP; for (int j = 0; j < t.nramp; j++) if (!strcmp (t.ramp[j].id, id)) i = j; }
	else if (!strcmp (w, "saucer")) { r.when = EV_SAUCER; for (int j = 0; j < t.nsaucer; j++) if (!strcmp (t.saucer[j].id, id)) i = j; }
	else if (!strcmp (w, "hit"))
	{
		r.when = EV_HIT;
		for (int j = 0; j < t.nlane && i < 0; j++) if (!strcmp (t.lane[j].id, id)) i = H_LANE << 16 | j;
		for (int j = 0; j < t.ntgt && i < 0; j++) if (!strcmp (t.tgt[j].id, id)) i = H_TARGET << 16 | j;
		for (int j = 0; j < t.ncirc && i < 0; j++) if (t.circ[j].kind == C_BUMPER && !strcmp (t.circ[j].id, id)) i = H_BUMPER << 16 | j;
		for (int j = 0; j < t.nsling && i < 0; j++) if (!strcmp (t.sling[j].id, id)) i = H_SLING << 16 | j;
	}
	else return err (c, line, E_VALUE, "when");
	if (i < 0) return err (c, line, E_UNKNOWN, id);
	r.ref = i;
	return true;
}
static bool cross_checks (Ctx &c)
{
	Table &t = c.t;
	if (c.nOutline != 1) return err (c, c.tableLine, E_OUTLINE);
	int left = 0, right = 0;
	for (int i = 0; i < t.nflip; i++) (t.flip[i].side == SIDE_LEFT ? left : right)++;
	if (t.nflip < 2 || !left || !right) return err (c, c.tableLine, E_FLIPPERS);
	if (c.plungers != 1) return err (c, c.tableLine, E_PLUNGER);
	for (int i = 0; i < t.nbank; i++) if (t.bank[i].n < 2) return err (c, c.bankLine[i], E_BANK, t.bank[i].id);
	t.rotate = -1;
	if (c.rotate[0])
	{
		t.rotate = find_named (c.rotate, t.group, t.ngroup);
		if (t.rotate < 0) return err (c, c.rotateLine, E_UNKNOWN, c.rotate);
	}
	for (int k = 0; k < t.nrule; k++) if (!resolve_rule (c, k)) return false;
	return true;
}

// ---- the entry points ---------------------------------------------------------------------------------------------------
void load_error_file (LoadError &e, bool big)
{
	e.line = 0; e.fmt = big ? E_BIG : E_READ;
	e.arg[0][0] = e.arg[1][0] = 0;
	scpy (e.reason, e.fmt, sizeof e.reason);
}

static bool read_blocks (Ctx &c)
{
	fk_kv *kv = c.kv;
	int n = fk_kv_count (kv), nb = fk_kv_blocks (kv);
	if (n > 0 && fk_kv_block (kv, 0) == 0) return err (c, fk_kv_line (kv, 0), E_FIRST);	// a key before any block
	int i = 0;
	for (int b = 1; b <= nb; b++)
	{
		c.b = b; c.bname = fk_kv_block_name (kv, b); c.bline = fk_kv_block_line (kv, b);
		c.i0 = i;
		while (i < n && fk_kv_block (kv, i) == b) i++;
		c.i1 = i;
		bool isTable = !strcmp (c.bname, "table");
		if ((b == 1) != isTable) return err (c, c.bline, E_FIRST);
		if (isTable) { c.tableLine = c.bline; if (!blk_table (c)) return false; continue; }
		BlockFn fn = 0;
		for (unsigned k = 0; k < sizeof BLOCKS / sizeof BLOCKS[0]; k++) if (!strcmp (BLOCKS[k].name, c.bname)) fn = BLOCKS[k].fn;
		if (!fn) return err (c, c.bline, E_BLOCK, c.bname);
		if (!fn (c)) return false;
	}
	if (nb == 0) return err (c, 0, E_FIRST);
	return cross_checks (c);
}

bool load_table (const char *text, Table &t, LoadError &e)
{
	memset (&t, 0, sizeof t);
	memset (&e, 0, sizeof e);
	e.fmt = "";
	if (!text) { load_error_file (e, false); return false; }
	if (strlen (text) > MAXFILE) { load_error_file (e, true); return false; }
	fk_kv *kv = fk_kv_parse (text, 0);
	if (!kv) { load_error_file (e, false); return false; }
	Ctx *c = new Ctx (kv, t, e);
	bool ok = read_blocks (*c);
	delete c;
	fk_kv_free (kv);
	if (!ok) memset (&t, 0, sizeof t);
	return ok;
}

bool inside_outline (const Table &t, Vec p)
{
	bool in = false;
	for (int i = 0, j = t.noutline - 1; i < t.noutline; j = i++)
	{
		const Vec &a = t.outline[i], &b = t.outline[j];
		if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
	}
	return in;
}

const char *hit_id (const Table &t, int ref)
{
	int k = ref >> 16, i = ref & 0xFFFF;
	switch (k)
	{
	case H_LANE: return i < t.nlane ? t.lane[i].id : "";
	case H_TARGET: return i < t.ntgt ? t.tgt[i].id : "";
	case H_BUMPER: return i < t.ncirc ? t.circ[i].id : "";
	case H_SLING: return i < t.nsling ? t.sling[i].id : "";
	}
	return "";
}

}
