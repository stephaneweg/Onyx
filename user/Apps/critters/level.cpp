//
// level.cpp -- Critters' level reader and terrain builder (level.h): the ".level" text through FileKit's fk_kv, every
// block checked in the file's order, then the cross-checks (1 to 4 hatches and exits, each one in an empty pixel). The
// first error found refuses the file with its line and its reason. The terrain: the shapes drawn in order (rectangles,
// polygons by scanlines with the even-odd rule at pixel centres, circles), textures by fixed integer rules.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "level.h"
#include <stdio.h>
#include <string.h>

namespace critters {

const char *const ROLE_WORD[NROLES] = { "climber", "floater", "blocker", "builder", "digger", "exploder", "basher", "miner" };

// ---- the errors' formats (level.h lists them for the translation) -------------------------------------------------------
static const char *const E_BLOCK = "unknown block [%s]";
static const char *const E_FIRST = "[level] must be the first block, once";
static const char *const E_NEEDS = "[%s] needs %s";
static const char *const E_SHAPE = "[shape] needs one of rect, points, circle";
static const char *const E_VALUE = "bad value for %s";
static const char *const E_RANGE = "%s out of range";
static const char *const E_COUNT = "the level needs 1 to 4 [%s]";
static const char *const E_INSIDE = "[%s] is inside the terrain";
static const char *const E_ROLE = "role %s is not available";
static const char *const E_MANY = "too many %s (max %s)";
static const char *const E_BIG = "the file is too big";
static const char *const E_READ = "cannot read the file";

enum { COORD = 10000 };				// a shape's coordinates: -10000 ... 10000 (clipped to the map)

// ---- small helpers ------------------------------------------------------------------------------------------------------
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; if (cap > 0) d[i] = 0; }
static bool sp (char c) { return c == ' ' || c == '\t'; }
static bool digit (char c) { return c >= '0' && c <= '9'; }
static int utf8_chars (const char *s) { int n = 0; for (; *s; s++) if (((unsigned char) *s & 0xC0) != 0x80) n++; return n; }

// int := ["-"] digits (at most 7): s advanced past it -> true
static bool read_int (const char *&s, int &v)
{
	const char *p = s;
	bool neg = false;
	if (*p == '-') { neg = true; p++; }
	if (!digit (*p)) return false;
	int x = 0, nd = 0;
	while (digit (*p)) { if (++nd > 7) return false; x = x * 10 + (*p++ - '0'); }
	v = neg ? -x : x;
	s = p;
	return true;
}
// Integers separated by spaces and/or one comma -> how many (at most cap), -1: does not read or more than cap
static int read_ints (const char *s, int *out, int cap)
{
	int n = 0;
	while (sp (*s)) s++;
	if (!*s) return 0;
	for (;;)
	{
		int v;
		if (!read_int (s, v)) return -1;
		if (n == cap) return -1;
		out[n++] = v;
		bool sep = false;
		while (sp (*s)) { s++; sep = true; }
		if (*s == ',') { s++; sep = true; while (sp (*s)) s++; if (!*s) return -1; }
		if (!*s) return n;
		if (!sep) return -1;
	}
}

// ---- the reader's state -------------------------------------------------------------------------------------------------
struct Ctx
{
	fk_kv *kv;
	Level &lv;
	LoadError &e;
	const char *bname;			// the block being read
	int bline;				// its header's line
	int i0, i1;				// its entries
	int levelLine;
	int hatchLine[MAXHATCH], exitLine[MAXEXIT];
	Ctx (fk_kv *k, Level &l, LoadError &ee) : kv (k), lv (l), e (ee), bname (""), bline (0), i0 (0), i1 (0), levelLine (0) {}
};

static bool err (LoadError &e, int line, const char *fmt, const char *a0 = "", const char *a1 = "")
{
	e.line = line; e.fmt = fmt;
	scpy (e.arg[0], a0, sizeof e.arg[0]);
	scpy (e.arg[1], a1, sizeof e.arg[1]);
	snprintf (e.reason, sizeof e.reason, fmt, e.arg[0], e.arg[1]);
	return false;
}
static bool err (Ctx &c, int line, const char *fmt, const char *a0 = "", const char *a1 = "") { return err (c.e, line, fmt, a0, a1); }
static int find (Ctx &c, const char *key)
{
	for (int i = c.i0; i < c.i1; i++) if (!strcmp (fk_kv_key (c.kv, i), key)) return i;
	return -1;
}
static int line_of (Ctx &c, int i) { return fk_kv_line (c.kv, i); }
static bool needs (Ctx &c, const char *key) { return err (c, c.bline, E_NEEDS, c.bname, key); }

// One integer in [lo, hi]; absent -> def, or the "needs" error when req
static bool get_int (Ctx &c, const char *key, int &v, int lo, int hi, int def, bool req = false)
{
	int i = find (c, key);
	if (i < 0) { if (req) return needs (c, key); v = def; return true; }
	int x[1];
	if (read_ints (fk_kv_value (c.kv, i), x, 1) != 1) return err (c, line_of (c, i), E_VALUE, key);
	if (x[0] < lo || x[0] > hi) return err (c, line_of (c, i), E_RANGE, key);
	v = x[0];
	return true;
}
// Exactly n integers, each in [lo, hi] -> true (absent: the "needs" error when req, else true and *found false)
static bool get_ints (Ctx &c, const char *key, int *v, int n, int lo, int hi, bool req, bool *found = 0, int *line = 0)
{
	int i = find (c, key);
	if (found) *found = i >= 0;
	if (i < 0) return req ? needs (c, key) : true;
	if (line) *line = line_of (c, i);
	if (read_ints (fk_kv_value (c.kv, i), v, n) != n) return err (c, line_of (c, i), E_VALUE, key);
	for (int k = 0; k < n; k++) if (v[k] < lo || v[k] > hi) return err (c, line_of (c, i), E_RANGE, key);
	return true;
}
// A point inside the map (a hatch, an exit, a label)
static bool get_point (Ctx &c, const char *key, Point &p, int *line)
{
	int v[2];
	if (!get_ints (c, key, v, 2, -COORD, COORD, true, 0, line)) return false;
	if (v[0] < 0 || v[0] >= c.lv.w || v[1] < 0 || v[1] >= c.lv.h) return err (c, *line, E_RANGE, key);
	p.x = (int16_t) v[0]; p.y = (int16_t) v[1];
	return true;
}
static bool get_colour (Ctx &c, const char *key, uint32_t &col, uint32_t def)
{
	int i = find (c, key);
	if (i < 0) { col = def; return true; }
	const char *s = fk_kv_value (c.kv, i);
	if (s[0] != '#' || strlen (s) != 7) return err (c, line_of (c, i), E_VALUE, key);
	uint32_t v = 0;
	for (int k = 1; k < 7; k++)
	{
		char h = s[k];
		int d = digit (h) ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
		if (d < 0) return err (c, line_of (c, i), E_VALUE, key);
		v = v << 4 | (uint32_t) d;
	}
	col = v;
	return true;
}
// One of the words (its index); absent: def
static bool get_word (Ctx &c, const char *key, const char *const *words, int nwords, int &v, int def)
{
	int i = find (c, key);
	if (i < 0) { v = def; return true; }
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
		if (utf8_chars (s) > maxc || strlen (s) >= HINTL) return err (c, line_of (c, i), E_RANGE, key);
		if (req && !s[0]) return err (c, line_of (c, i), E_VALUE, key);
		scpy (t.en, s, HINTL);
	}
	int j = find (c, k2);
	if (j >= 0)
	{
		const char *s = fk_kv_value (c.kv, j);
		if (utf8_chars (s) > maxc || strlen (s) >= HINTL) return err (c, line_of (c, j), E_RANGE, k2);
		scpy (t.fr, s, HINTL);
	}
	return true;
}
static bool too_many (Ctx &c, int n, int max, const char *things)
{
	if (n < max) return false;
	char m[16]; snprintf (m, sizeof m, "%d", max);
	err (c, c.bline, E_MANY, things, m);
	return true;
}

static uint32_t darker (uint32_t c)
{
	uint32_t r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
	return (r - r / 4) << 16 | (g - g / 4) << 8 | (b - b / 4);
}

// ---- the blocks ---------------------------------------------------------------------------------------------------------
static const char *const WORDS_MAT[] = { "earth", "steel", "water", "lava", "erase" };
static const uint8_t MAT_OF[] = { M_EARTH, M_STEEL, M_WATER, M_LAVA, MAT_ERASE };
static const uint32_t COLOUR_OF[] = { 0x8A5A34, 0x8890A0, 0x3070D0, 0xE05020, 0 };
static const char *const WORDS_TEX[] = { "plain", "speckle", "stripes", "bricks" };
static const char *const WORDS_DIR[] = { "left", "right" };

static bool blk_level (Ctx &c, bool withDiggers2)
{
	Level &lv = c.lv;
	int format;
	if (!get_int (c, "format", format, -COORD, COORD, 0, true)) return false;
	if (format != 1) return err (c, line_of (c, find (c, "format")), E_RANGE, "format");
	if (!get_text (c, "name", lv.name, NAMEC, true) || !get_text (c, "hint", lv.hint, HINTC, false)) return false;
	int sz[2], szLine = 0;
	if (!get_ints (c, "size", sz, 2, -COORD, COORD, true, 0, &szLine)) return false;
	if (sz[0] < MINW || sz[0] > MAXW || sz[1] < MINH || sz[1] > MAXH) return err (c, szLine, E_RANGE, "size");
	lv.w = sz[0]; lv.h = sz[1];
	if (!get_int (c, "count", lv.count, 1, MAXCRIT, 0, true)) return false;
	if (!get_int (c, "save", lv.save, -COORD, COORD, 0, true)) return false;
	if (lv.save < 1 || lv.save > lv.count) return err (c, line_of (c, find (c, "save")), E_RANGE, "save");
	if (!get_int (c, "time", lv.timeSec, 30, 1200, 0, true) || !get_int (c, "rate", lv.rate, 1, 99, 50)) return false;
	for (int r = 0; r < NROLES; r++)
	{
		if (!get_int (c, ROLE_WORD[r], lv.roles[r], 0, 99, 0)) return false;
		if (r >= R_BASHER && lv.roles[r] > 0 && !withDiggers2)
			return err (c, line_of (c, find (c, ROLE_WORD[r])), E_ROLE, ROLE_WORD[r]);
	}
	if (!get_int (c, "start", lv.start, 0, lv.w - 1, -1)) return false;
	if (!get_colour (c, "background", lv.bg, 0x101830) || !get_colour (c, "brick", lv.brick, 0xC8A060)) return false;
	return true;
}

static bool blk_shape (Ctx &c)
{
	Level &lv = c.lv;
	if (too_many (c, lv.nshape, MAXSHAPE, "shapes")) return false;
	Shape &s = lv.shape[lv.nshape];
	memset (&s, 0, sizeof s);
	int has = (find (c, "rect") >= 0) + (find (c, "points") >= 0) + (find (c, "circle") >= 0);
	if (has != 1) return err (c, c.bline, E_SHAPE);
	int v[2 * MAXPTS + 2];
	if (find (c, "rect") >= 0)
	{
		int line = 0;
		if (!get_ints (c, "rect", v, 4, -COORD, COORD, true, 0, &line)) return false;
		if (v[2] < 1 || v[3] < 1) return err (c, line, E_RANGE, "rect");
		s.kind = SH_RECT; s.n = 4;
		for (int k = 0; k < 4; k++) s.p[k] = (int16_t) v[k];
		s.x0 = v[0]; s.y0 = v[1]; s.x1 = v[0] + v[2] - 1; s.y1 = v[1] + v[3] - 1;
	}
	else if (find (c, "circle") >= 0)
	{
		int line = 0;
		if (!get_ints (c, "circle", v, 3, -COORD, COORD, true, 0, &line)) return false;
		if (v[2] < 1 || v[2] > 400) return err (c, line, E_RANGE, "circle");
		s.kind = SH_CIRCLE; s.n = 3;
		for (int k = 0; k < 3; k++) s.p[k] = (int16_t) v[k];
		s.x0 = v[0] - v[2]; s.y0 = v[1] - v[2]; s.x1 = v[0] + v[2]; s.y1 = v[1] + v[2];
	}
	else
	{
		int i = find (c, "points");
		int k = read_ints (fk_kv_value (c.kv, i), v, 2 * MAXPTS + 2);
		if (k < 0)
		{
			// more numbers than the room: too many points if they read, else a bad value
			const char *p = fk_kv_value (c.kv, i);
			int cnt = 0, x;
			bool ok = true;
			while (*p) { while (sp (*p) || *p == ',') p++; if (!*p) break; if (!read_int (p, x)) { ok = false; break; } cnt++; }
			if (ok && cnt > 2 * MAXPTS && !(cnt & 1))
			{
				char m[16]; snprintf (m, sizeof m, "%d", (int) MAXPTS);
				return err (c, line_of (c, i), E_MANY, "points", m);
			}
			return err (c, line_of (c, i), E_VALUE, "points");
		}
		if ((k & 1) || k < 6) return err (c, line_of (c, i), E_VALUE, "points");
		if (k > 2 * MAXPTS)
		{
			char m[16]; snprintf (m, sizeof m, "%d", (int) MAXPTS);
			return err (c, line_of (c, i), E_MANY, "points", m);
		}
		s.kind = SH_POLY; s.n = k / 2;
		s.x0 = s.y0 = COORD; s.x1 = s.y1 = -COORD;
		for (int j = 0; j < k; j++)
		{
			if (v[j] < -COORD || v[j] > COORD) return err (c, line_of (c, i), E_RANGE, "points");
			s.p[j] = (int16_t) v[j];
		}
		for (int j = 0; j < s.n; j++)
		{
			int x = s.p[2 * j], y = s.p[2 * j + 1];
			if (x < s.x0) s.x0 = x;
			if (x > s.x1) s.x1 = x;
			if (y < s.y0) s.y0 = y;
			if (y > s.y1) s.y1 = y;
		}
	}
	int mat, tex;
	if (!get_word (c, "material", WORDS_MAT, 5, mat, 0) || !get_word (c, "texture", WORDS_TEX, 4, tex, TX_PLAIN)) return false;
	s.mat = MAT_OF[mat]; s.tex = (uint8_t) tex;
	if (!get_colour (c, "colour", s.colour, COLOUR_OF[mat])) return false;
	if (!get_colour (c, "colour2", s.colour2, darker (s.colour))) return false;
	lv.nshape++;
	return true;
}

static bool blk_hatch (Ctx &c)
{
	Level &lv = c.lv;
	if (lv.nhatch >= MAXHATCH) return err (c, c.levelLine, E_COUNT, "hatch");
	Hatch &h = lv.hatch[lv.nhatch];
	int line = 0, d;
	if (!get_point (c, "at", h.at, &line) || !get_word (c, "dir", WORDS_DIR, 2, d, 1)) return false;
	h.dir = d ? 1 : -1;
	c.hatchLine[lv.nhatch++] = line;
	return true;
}

static bool blk_exit (Ctx &c)
{
	Level &lv = c.lv;
	if (lv.nexit >= MAXEXIT) return err (c, c.levelLine, E_COUNT, "exit");
	int line = 0;
	if (!get_point (c, "at", lv.exit[lv.nexit], &line)) return false;
	c.exitLine[lv.nexit++] = line;
	return true;
}

static bool blk_label (Ctx &c)
{
	Level &lv = c.lv;
	if (too_many (c, lv.nlabel, MAXLABEL, "labels")) return false;
	Label &l = lv.label[lv.nlabel];
	int line = 0;
	if (!get_point (c, "at", l.at, &line) || !get_text (c, "text", l.text, HINTC, true)
	    || !get_colour (c, "colour", l.colour, 0xFFFFFF)) return false;
	lv.nlabel++;
	return true;
}

static bool load (fk_kv *kv, Level &lv, LoadError &e, bool withDiggers2)
{
	Ctx c (kv, lv, e);
	int n = fk_kv_count (kv), nb = fk_kv_blocks (kv);
	if (n > 0 && fk_kv_block (kv, 0) == 0) return err (c, fk_kv_line (kv, 0), E_FIRST);	// a key before any block
	if (nb == 0) return err (c, 1, E_FIRST);
	int i = 0;
	for (int b = 1; b <= nb; b++)
	{
		c.bname = fk_kv_block_name (kv, b); c.bline = fk_kv_block_line (kv, b);
		while (i < n && fk_kv_block (kv, i) < b) i++;
		c.i0 = i;
		while (i < n && fk_kv_block (kv, i) == b) i++;
		c.i1 = i;
		const char *nm = c.bname;
		bool isLevel = !strcmp (nm, "level");
		if (!isLevel && strcmp (nm, "shape") && strcmp (nm, "hatch") && strcmp (nm, "exit") && strcmp (nm, "label"))
			return err (c, c.bline, E_BLOCK, nm);
		if (isLevel != (b == 1)) return err (c, c.bline, E_FIRST);
		bool ok = isLevel ? (c.levelLine = c.bline, blk_level (c, withDiggers2))
			: !strcmp (nm, "shape") ? blk_shape (c)
			: !strcmp (nm, "hatch") ? blk_hatch (c)
			: !strcmp (nm, "exit") ? blk_exit (c) : blk_label (c);
		if (!ok) return false;
	}
	if (lv.nhatch < 1) return err (c, c.levelLine, E_COUNT, "hatch");
	if (lv.nexit < 1) return err (c, c.levelLine, E_COUNT, "exit");
	for (int k = 0; k < lv.nhatch; k++)
		if (material_at (lv, lv.hatch[k].at.x, lv.hatch[k].at.y) != M_EMPTY) return err (c, c.hatchLine[k], E_INSIDE, "hatch");
	for (int k = 0; k < lv.nexit; k++)
		if (material_at (lv, lv.exit[k].x, lv.exit[k].y) != M_EMPTY) return err (c, c.exitLine[k], E_INSIDE, "exit");
	return true;
}

bool load_level (const char *text, Level &lv, LoadError &e, bool withDiggers2)
{
	memset (&lv, 0, sizeof lv);
	memset (&e, 0, sizeof e);
	e.fmt = "";
	if (!text) { load_error_file (e, false); return false; }
	if (strlen (text) > MAXFILE) { load_error_file (e, true); return false; }
	fk_kv *kv = fk_kv_parse (text, 0);
	if (!kv) { load_error_file (e, false); return false; }
	bool ok = load (kv, lv, e, withDiggers2);
	fk_kv_free (kv);
	if (!ok) memset (&lv, 0, sizeof lv);
	return ok;
}

void load_error_file (LoadError &e, bool tooBig)
{
	err (e, 0, tooBig ? E_BIG : E_READ);
}

// ---- the shapes' pixels ---------------------------------------------------------------------------------------------------
// A polygon's crossings with the line through the centres of row py, in doubled coordinates (the centres at 2x+1,
// 2y+1; the vertices at even values, so a vertex never lies on the line): for each edge crossing it, the first pixel
// column whose centre is right of the crossing. A pixel px is inside (even-odd) when an odd number of them are <= px.
static int poly_cross (const Shape &s, int py, int *out)
{
	int nc = 0;
	int64_t Y = 2 * (int64_t) py + 1;
	for (int j = 0; j < s.n; j++)
	{
		int k = j + 1 == s.n ? 0 : j + 1;
		int64_t X1 = 2 * (int64_t) s.p[2 * j], Y1 = 2 * (int64_t) s.p[2 * j + 1];
		int64_t X2 = 2 * (int64_t) s.p[2 * k], Y2 = 2 * (int64_t) s.p[2 * k + 1];
		if (!((Y1 < Y && Y < Y2) || (Y2 < Y && Y < Y1))) continue;
		// the crossing Xc = num / den (den > 0); the first px with 2px+1 > Xc: px = floor ((num - den) / (2 den)) + 1
		int64_t den = Y2 - Y1, num = X1 * den + (Y - Y1) * (X2 - X1);
		if (den < 0) { den = -den; num = -num; }
		int64_t a = num - den, d = 2 * den;
		int64_t q = a >= 0 ? a / d : -((-a + d - 1) / d);
		out[nc++] = (int) (q + 1);
	}
	for (int a = 1; a < nc; a++)			// (insertion sort: at most 64)
	{
		int v = out[a], b = a - 1;
		while (b >= 0 && out[b] > v) { out[b + 1] = out[b]; b--; }
		out[b + 1] = v;
	}
	return nc;
}

static bool shape_has (const Shape &s, int x, int y)
{
	if (x < s.x0 || x > s.x1 || y < s.y0 || y > s.y1) return false;
	if (s.kind != SH_POLY)
	{
		if (s.kind == SH_RECT) return true;
		int dx = x - s.p[0], dy = y - s.p[1];
		return dx * dx + dy * dy <= s.p[2] * s.p[2];
	}
	int cr[MAXPTS];
	int nc = poly_cross (s, y, cr), in = 0;
	for (int k = 0; k < nc; k++) if (cr[k] <= x) in ^= 1;
	return in != 0;
}

int material_at (const Level &lv, int x, int y)
{
	int m = M_EMPTY;
	for (int i = 0; i < lv.nshape; i++)
		if (shape_has (lv.shape[i], x, y)) m = lv.shape[i].mat == MAT_ERASE ? (int) M_EMPTY : (int) lv.shape[i].mat;
	return m;
}

static uint32_t lighten (uint32_t c)
{
	uint32_t r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
	return (r + (255 - r) / 2) << 16 | (g + (255 - g) / 2) << 8 | (b + (255 - b) / 2);
}

// One pixel of shape i (its material and its colour by the texture)
static void paint (const Level &lv, Terrain &t, int i, int x, int y)
{
	const Shape &s = lv.shape[i];
	int k = y * t.w + x;
	if (s.mat == MAT_ERASE) { t.m[k] = M_EMPTY; t.col[k] = lv.bg; return; }
	uint32_t c = s.colour;
	switch (s.tex)
	{
	case TX_SPECKLE: if (mix32 (x, y, i) % 6 == 0) c = s.colour2; break;
	case TX_STRIPES: if (y % 4 == 3) c = s.colour2; break;
	case TX_BRICKS:
	{
		int bx = (x + (y / 4 % 2) * 4) % 8;
		if (y % 4 == 3 || bx == 7) c = s.colour2;
		else if (s.mat == M_STEEL && y % 4 == 0 && bx == 0) c = lighten (s.colour);	// the steel plates' rivets
		break;
	}
	default: break;
	}
	t.m[k] = s.mat; t.col[k] = c;
}

bool build_terrain (const Level &lv, Terrain &t)
{
	t.bg = lv.bg; t.brickColour = lv.brick;
	if (!t.alloc (lv.w, lv.h)) return false;
	int cr[MAXPTS];
	for (int i = 0; i < lv.nshape; i++)
	{
		const Shape &s = lv.shape[i];
		int x0 = s.x0 < 0 ? 0 : s.x0, x1 = s.x1 > t.w - 1 ? t.w - 1 : s.x1;
		int y0 = s.y0 < 0 ? 0 : s.y0, y1 = s.y1 > t.h - 1 ? t.h - 1 : s.y1;
		for (int y = y0; y <= y1; y++)
		{
			if (s.kind == SH_POLY)
			{
				int nc = poly_cross (s, y, cr);
				for (int k = 0; k + 1 < nc; k += 2)
				{
					int a = cr[k] < x0 ? x0 : cr[k], b = cr[k + 1] - 1 > x1 ? x1 : cr[k + 1] - 1;
					for (int x = a; x <= b; x++) paint (lv, t, i, x, y);
				}
			}
			else if (s.kind == SH_RECT)
				for (int x = x0; x <= x1; x++) paint (lv, t, i, x, y);
			else
			{
				int dy = y - s.p[1], r2 = s.p[2] * s.p[2];
				for (int x = x0; x <= x1; x++) { int dx = x - s.p[0]; if (dx * dx + dy * dy <= r2) paint (lv, t, i, x, y); }
			}
		}
	}
	t.hash = t.full_hash ();
	t.dirty.x0 = 0; t.dirty.y0 = 0; t.dirty.x1 = t.w - 1; t.dirty.y1 = t.h - 1;
	return true;
}

}
