//
// circuitstest.cpp -- Circuits' engine on the PC (user/Apps/circuits/circuit.h): part A, the board on levels made
// here -- the stars, the wrong rows, an incomplete board refused, loops refused, one wire an input, the parts allowed,
// the step mode's depths, the circuit text and its errors, undo / redo, the truth table copied, the geometry (the
// fixed parts' rows, the pins, the routes, the hit tests); part B, the packs given on the command line -- every level
// read, its table equal to its objective, its reference solution won with three stars at the minimum gate count, the
// packs' errors with their lines, the progress written and read back, the unlocking, the lessons. Run by
// tools/tests/run_circuits_test.sh.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "circuits/circuit.h"
#include <stdio.h>

using namespace circuits;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

// ---- levels made here ---------------------------------------------------------------------------------------------------
typedef int (*Fn) (unsigned row, int out);
// A level: its inputs / outputs ("A B", "Out"), the gates allowed ("NOT AND"; "*": all), par, its objective
static void make (Level &L, const char *id, const char *ins, const char *outs, const char *parts, int par3, int par2, Fn f)
{
	snprintf (L.id, sizeof L.id, "%s", id);
	snprintf (L.title[0], sizeof L.title[0], "%s", id);
	char b[64];
	snprintf (b, sizeof b, "%s", ins); L.ninputs = 0;
	for (char *t = strtok (b, " "); t; t = strtok (0, " ")) snprintf (L.inName[L.ninputs++], IONAMEL, "%s", t);
	snprintf (b, sizeof b, "%s", outs); L.noutputs = 0;
	for (char *t = strtok (b, " "); t; t = strtok (0, " ")) snprintf (L.outName[L.noutputs++], IONAMEL, "%s", t);
	L.parts = 0;
	if (!strcmp (parts, "*")) for (int t = P_NOT; t < P_COUNT_; t++) L.parts |= 1u << t;
	else { snprintf (b, sizeof b, "%s", parts); for (char *t = strtok (b, " "); t; t = strtok (0, " ")) L.parts |= 1u << type_of (t); }
	L.par3 = par3; L.par2 = par2;
	for (int o = 0; o < L.noutputs; o++)
	{
		L.want[o] = 0;
		for (unsigned r = 0; r < (1u << L.ninputs); r++) if (f (r, o)) L.want[o] |= 1u << r;
	}
}
static int bit (unsigned row, int n, int k) { return (row >> (n - 1 - k)) & 1; }
static int f_xor (unsigned r, int) { return bit (r, 2, 0) ^ bit (r, 2, 1); }
static int f_and (unsigned r, int) { return bit (r, 2, 0) & bit (r, 2, 1); }
static int f_half (unsigned r, int o) { int a = bit (r, 2, 0), b = bit (r, 2, 1); return o == 0 ? a & b : a ^ b; }
static int f_full (unsigned r, int o) { int s = bit (r, 3, 0) + bit (r, 3, 1) + bit (r, 3, 2); return o == 0 ? s >= 2 : s & 1; }

static const char *text_of (const Circuit &c)
{
	static char b[4][TEXTCAP]; static int k;
	k = (k + 1) & 3;
	write_text (c, b[k], TEXTCAP);
	return b[k];
}
static Circuit from (const Level &L, const char *text)
{
	Circuit c; Err e; int line;
	bool ok = read_text (c, L, text, &e, &line);
	CHECK (ok, "read_text (%s) failed: %s at line %d", L.id, err_name (e), line);
	return c;
}

// ---- part A: the engine -------------------------------------------------------------------------------------------------
static void test_geometry ()
{
	// the fixed parts' rows (04 §5.2)
	CHECK (fixed_row (0, 1) == 14, "1 of 1");
	CHECK (fixed_row (0, 2) == 6 && fixed_row (1, 2) == 21, "of 2");
	CHECK (fixed_row (0, 3) == 4 && fixed_row (1, 3) == 14 && fixed_row (2, 3) == 24, "of 3");
	CHECK (fixed_row (0, 4) == 2 && fixed_row (1, 4) == 10 && fixed_row (2, 4) == 17 && fixed_row (3, 4) == 25, "of 4");
	Level L; make (L, "geo", "A B Cin", "Cout S", "*", 5, 6, f_full);
	Circuit c; c.setup (L);
	CHECK (c.n == 5 && c.ni == 3 && c.no == 2, "setup: %d parts", c.n);
	CHECK (c.p[0].x == 0 && c.p[0].y == 4 && c.p[2].y == 24 && c.p[3].x == LAMP_X && c.p[3].y == 6 && c.p[4].y == 21, "fixed places");
	int x, y, w, h;
	c.pinOut (1, x, y); CHECK (x == 5 && y == 15, "switch B's pin (%d, %d)", x, y);
	c.pinIn (3, 0, x, y); CHECK (x == 34 && y == 7, "lamp Cout's pin (%d, %d)", x, y);
	c.box (0, x, y, w, h); CHECK (w == 5 && h == 2, "switch 5 x 2");
	c.box (4, x, y, w, h); CHECK (w == 6 && h == 2, "lamp 6 x 2");
	CHECK (c.pins (0) == 0 && c.pins (3) == 1, "switch / lamp pins");
	int ga = c.add (P_AND, 10, 3), gn = c.add (P_NOT, 20, 3);
	CHECK (ga == 5 && gn == 6 && !strcmp (c.p[ga].name, "g1") && !strcmp (c.p[gn].name, "g2"), "the names g1, g2");
	c.pinIn (ga, 0, x, y); CHECK (x == 10 && y == 4, "AND's pin 1 (%d, %d)", x, y);
	c.pinIn (ga, 1, x, y); CHECK (x == 10 && y == 6, "AND's pin 2 (%d, %d)", x, y);
	c.pinOut (ga, x, y); CHECK (x == 15 && y == 5, "AND's output (%d, %d)", x, y);
	c.pinIn (gn, 0, x, y); CHECK (x == 20 && y == 5, "NOT's pin (%d, %d)", x, y);
	c.box (gn, x, y, w, h); CHECK (w == 5 && h == 4, "a gate 5 x 4");
	CHECK (c.pins (ga) == 2 && c.pins (gn) == 1, "AND 2 pins, NOT 1");
	for (int t = P_OR; t < P_COUNT_; t++) { int g = c.add (t, 6 + 5 * (t - P_OR), 24); CHECK (g >= 0 && c.pins (g) == 2, "%s: 2 pins", type_name (t)); c.remove (g); }
	CHECK (c.at (12, 4) == ga && c.at (14, 6) == ga && c.at (15, 5) < 0 && c.at (1, 4) == 0 && c.at (39, 22) == 4 && c.at (30, 29) < 0, "at ()");
	// outside, overlapping
	Err e;
	CHECK (c.add (P_AND, 3, 10, &e) < 0 && e == E_OUTSIDE, "x = 3: %s", err_name (e));
	CHECK (c.add (P_AND, 30, 10, &e) < 0 && e == E_OUTSIDE, "x = 30: %s", err_name (e));
	CHECK (c.add (P_AND, 28, 10, &e) >= 0, "x = 28 fits (right edge 33): %s", err_name (e));
	CHECK (c.add (P_AND, 10, 27, &e) < 0 && e == E_OUTSIDE, "y = 27: %s", err_name (e));
	CHECK (c.add (P_AND, 12, 5, &e) < 0 && e == E_OVERLAP, "overlap: %s", err_name (e));
	CHECK (c.add (P_AND, 15, 3, &e) >= 0, "touching is no overlap: %s", err_name (e));
	CHECK (!c.fits (P_OR, 11, 4) && c.fits (P_OR, 10, 12), "fits ()");
	CHECK (c.move (0, 1, 1) == E_FIXED && c.move (3, 30, 1) == E_FIXED, "switches and lamps do not move");
	CHECK (c.move (ga, 16, 3) == E_OVERLAP && c.p[ga].x == 10, "a refused move leaves it");
	CHECK (c.remove (0) == E_FIXED && c.remove (3) == E_FIXED, "switches and lamps stay");
}
static void test_routes ()
{
	Level L; make (L, "rt", "A B", "Out", "*", 1, 1, f_and);
	Circuit c; c.setup (L);					// A (5, 7), B (5, 22)
	int g = c.add (P_AND, 20, 3);				// pins (20, 4), (20, 6); out (25, 5)
	int xy[16];
	CHECK (c.route (g, 0, xy, 16) == 0, "no wire: no route");
	c.connect (0, g, 0);
	int np = c.route (g, 0, xy, 16);
	CHECK (np == 4 && xy[0] == 5 && xy[1] == 7 && xy[2] == 12 && xy[3] == 7 && xy[4] == 12 && xy[5] == 4 && xy[6] == 20 && xy[7] == 4,
		"forward: 4 points, the vertical at the middle (%d: %d,%d %d,%d %d,%d %d,%d)", np, xy[0], xy[1], xy[2], xy[3], xy[4], xy[5], xy[6], xy[7]);
	c.connect (1, g, 1);					// B: the same middle column, overlapping A's vertical -> moved
	np = c.route (g, 1, xy, 16);
	CHECK (np == 4 && (xy[2] == 13 || xy[2] == 11) && xy[2] == xy[4] && xy[3] == 22 && xy[5] == 6, "another source's vertical: one column off (%d)", xy[2]);
	// the same source may share the column
	int g2 = c.add (P_OR, 20, 10);				// pins (20, 11), (20, 13)
	c.connect (0, g2, 0);
	np = c.route (g2, 0, xy, 16);
	CHECK (np == 4 && xy[2] == 12, "the same source: the same column (%d)", xy[2]);
	int jx[16]; int nj = c.junctions (jx, 16);
	CHECK (nj == 1 && jx[0] == 12 && jx[1] == 7, "a junction where A's two wires part (%d: %d,%d)", nj, jx[0], jx[1]);
	// a footprint in the way: a gate on the middle column between the rows
	Circuit d; d.setup (L);
	int t = d.add (P_NOT, 26, 12);				// pin (26, 14)
	d.add (P_AND, 14, 8);					// columns 14..19, rows 8..12: the middle (5 + 26) / 2 = 15 crossed
	d.connect (0, t, 0);
	np = d.route (t, 0, xy, 16);
	CHECK (np == 4 && (xy[2] <= 14 || xy[2] >= 19), "a vertical crosses no part (%d)", xy[2]);
	// backward: the target left of the source
	int b1 = d.add (P_AND, 22, 20), b2 = d.add (P_NOT, 8, 20);	// b1 out (27, 22); b2 pin (8, 22)
	d.connect (b1, b2, 0);
	np = d.route (b2, 0, xy, 16);
	CHECK (np == 6 && xy[0] == 27 && xy[2] == 28 && xy[5] == 25 && xy[7] == 25 && xy[6] == 7 && xy[10] == 8 && xy[11] == 22,
		"backward: 6 points under both parts (%d: y %d)", np, xy[5]);
	// the hit tests (1/16 cell)
	bool out; int pin;
	CHECK (c.pinAt (5 * 16, 7 * 16, &out, &pin) == 0 && out, "A's pin");
	CHECK (c.pinAt (5 * 16 + 9, 7 * 16, &out, &pin) == 0, "A's pin, 0.56 cell off");
	CHECK (c.pinAt (5 * 16 + 10, 7 * 16, &out, &pin) < 0, "0.62 cell off: none");
	CHECK (c.pinAt (20 * 16, 6 * 16 + 3, &out, &pin) == g && !out && pin == 1, "g1's pin 2");
	CHECK (c.pinAt (25 * 16, 5 * 16, &out, &pin) == g && out, "g1's output");
	CHECK (c.pinAt (34 * 16, 15 * 16, &out, &pin) == 2 && !out && pin == 0, "the lamp's pin");
	CHECK (c.wireAt (8 * 16, 7 * 16 + 4, &pin) == g && pin == 0, "on A's wire");
	CHECK (c.wireAt (16 * 16 + 5, 4 * 16, &pin) == g && pin == 0, "on its last segment");
	CHECK (c.wireAt (8 * 16, 7 * 16 + 6, &pin) < 0, "0.37 cell off: none");
	CHECK (c.wireAt (30 * 16, 28 * 16, &pin) < 0, "nowhere near a wire");
	// canConnect
	Err e;
	CHECK (c.canConnect (2, g, 0, &e) == false && e == E_PIN, "a lamp is no source");
	CHECK (c.canConnect (g, g, 0, &e) == false && e == E_LOOP, "into itself: a loop");
	CHECK (c.canConnect (1, g, 0, &e) && e == E_OK, "into a fed input: accepted (it replaces)");
	CHECK (c.canConnect (g, 0, 0, &e) == false && e == E_PIN, "a switch has no input");
	CHECK (c.canConnect (g, 2, 1, &e) == false && e == E_PIN, "a lamp has one input");
}
static void test_stars_and_rows ()
{
	// AC 4
	CHECK (stars_for (3, 3, 5, true) == 3 && stars_for (4, 3, 5, true) == 2 && stars_for (5, 3, 5, true) == 2 && stars_for (6, 3, 5, true) == 1
		&& stars_for (2, 3, 5, false) == 0, "stars for par = 3 5");
	Level X; make (X, "xor", "A B", "Out", "NOT AND OR NAND NOR", 3, 5, f_xor);
	Circuit c = from (X, "part g1 OR 10 3\npart g2 NAND 10 14\npart g3 AND 20 8\nwire A g1.1\nwire B g1.2\nwire A g2.1\nwire B g2.2\nwire g1 g3.1\nwire g2 g3.2\nwire g3 Out");
	CheckResult r = check (c, X);
	CHECK (r.err == E_OK && r.won && r.gates == 3 && r.stars == 3 && r.nwrong == 0 && r.firstWrong == -1, "xor in 3: won, 3 stars");
	c.add (P_NOT, 26, 22);					// unused gates count
	r = check (c, X); CHECK (r.err == E_GATE_OPEN, "an unused NOT has an open input");
	int g4 = c.find ("g4"); c.connect (0, g4, 0);
	r = check (c, X); CHECK (r.won && r.gates == 4 && r.stars == 2, "4 gates: 2 stars (%d)", r.stars);
	int g5 = c.add (P_NOT, 26, 2); c.connect (0, g5, 0);
	r = check (c, X); CHECK (r.won && r.gates == 5 && r.stars == 2, "5 gates: 2 stars");
	int g6 = c.add (P_NOT, 20, 22); c.connect (0, g6, 0);
	r = check (c, X); CHECK (r.won && r.gates == 6 && r.stars == 1, "6 gates: 1 star");
	// AC 5: OR on xor fails on row 11 only; on and, on rows 01 and 10
	c = from (X, "part g1 OR\nwire A g1.1\nwire B g1.2\nwire g1 Out");
	r = check (c, X);
	CHECK (r.err == E_OK && !r.won && r.stars == 0 && r.nwrong == 1 && r.firstWrong == 3 && r.wrong == 8 && r.got[0] == 0xE, "xor with OR: row 11 wrong (%d, %x)", r.nwrong, r.wrong);
	Level A; make (A, "and", "A B", "Out", "NOT AND OR", 1, 1, f_and);
	A.parts |= 1u << P_OR;
	c = from (A, "part g1 OR\nwire A g1.1\nwire B g1.2\nwire g1 Out");
	r = check (c, A);
	CHECK (!r.won && r.nwrong == 2 && r.wrong == 6 && r.firstWrong == 1, "and with OR: rows 01, 10 wrong (%x)", r.wrong);
	// AC 6: an incomplete board refused, naming the part, nothing won
	c = from (A, "part g1 AND\nwire A g1.1\nwire B g1.2");
	r = check (c, A);
	CHECK (r.err == E_LAMP_OPEN && r.errPart == 2 && !strcmp (c.p[r.errPart].name, "Out") && !r.won && r.stars == 0, "lamp Out not connected");
	c = from (A, "part g1 AND\nwire A g1.1\nwire g1 Out");
	r = check (c, A);
	CHECK (r.err == E_GATE_OPEN && !strcmp (c.p[r.errPart].name, "g1") && !r.won && r.stars == 0, "g1 has an open input");
}
static void test_wires ()
{
	Level X; make (X, "xor", "A B", "Out", "*", 3, 5, f_xor);
	// AC 7: loops refused, the circuit unchanged
	Circuit c = from (X, "part g1 AND 8 2\npart g2 OR 16 2\nwire A g1.1\nwire g1 g2.1");
	char before[TEXTCAP]; snprintf (before, sizeof before, "%s", text_of (c));
	int g1 = c.find ("g1"), g2 = c.find ("g2");
	CHECK (c.connect (g1, g1, 1) == E_LOOP, "g1 -> g1.2 refused");
	CHECK (c.connect (g2, g1, 1) == E_LOOP, "g1 -> g2 -> g1.2 refused");
	CHECK (c.connect (g2, g1, 0) == E_LOOP, "g1 -> g2 -> g1.1 refused (it replaces nothing)");
	CHECK (!strcmp (before, text_of (c)), "the circuit unchanged");
	CHECK (c.upstream (g2, g1) && !c.upstream (g1, g2) && c.upstream (g2, 0) && !c.upstream (g1, 1), "upstream ()");
	// AC 8: a second wire into an input replaces the first; an output feeds 3 inputs
	int w = c.wires ();
	CHECK (c.connect (1, g1, 0) == E_OK && c.wires () == w && c.p[g1].in[0] == 1, "B replaces A on g1.1, %d wires", c.wires ());
	int g3 = c.add (P_NOT, 8, 20);
	CHECK (c.connect (0, g1, 1) == E_OK && c.connect (0, g2, 1) == E_OK && c.connect (0, g3, 0) == E_OK && c.wires () == w + 3, "A into 3 inputs");
	c.disconnect (g3, 0); CHECK (c.wires () == w + 2 && c.p[g3].in[0] == -1, "disconnect");
	// removing a gate: its wires go, the others' indices follow
	CHECK (c.remove (g1) == E_OK && c.find ("g1") < 0 && c.wires () == 1, "g1 removed with its 3 wires (%d left)", c.wires ());
	g2 = c.find ("g2"); g3 = c.find ("g3");
	CHECK (c.p[g2].in[0] == -1 && c.p[g2].in[1] == 0, "g2's wires renumbered");
	c.clear (); CHECK (c.gates () == 0 && c.wires () == 0 && c.n == 3, "clear");
	// AC 9: the parts allowed, the pins, the 48 gates
	Level N; make (N, "not", "A", "Out", "NOT", 1, 1, f_and);
	Circuit d; d.setup (N); Err e;
	CHECK (d.add (P_AND, 10, 10, &e) < 0 && e == E_NOT_ALLOWED, "AND not allowed here");
	int n1 = d.add (P_NOT, 10, 10, &e);
	CHECK (n1 >= 0 && d.pins (n1) == 1 && d.connect (0, n1, 1) == E_PIN, "NOT: one pin");
	Circuit f; f.setup (X);
	int placed = 0;
	for (int y = 0; y + GATE_H <= BOARD_H; y += GATE_H) for (int x = GATE_MINX; x + GATE_W <= GATE_MAXX; x += GATE_W) if (f.add (P_AND, x, y) >= 0) placed++;
	CHECK (placed == 35 && f.add (P_AND, 6, 28, &e) < 0, "the board holds 35 gates side by side (%d)", placed);
	// (48 gates do not fit on 40 x 30 cells: the limit is shown on a board filled by hand)
	for (int k = f.gates (); k < MAXGATES; k++) { Part &q = f.p[f.n++]; snprintf (q.name, NAMEL, "h%d", k % 100); q.type = P_NOT; q.x = 0; q.y = 0; q.in[0] = q.in[1] = -1; }
	CHECK (f.gates () == 48 && f.add (P_AND, 6, 26, &e) < 0 && e == E_FULL && !f.fits (P_AND, 6, 26), "a 49th gate refused: %s", err_name (e));
}
static const char *FULL =
	"part g1 XOR 8 4\npart g2 XOR 16 8\npart g3 AND 8 12\npart g4 AND 16 16\npart g5 OR 24 14\n"
	"wire A g1.1\nwire B g1.2\nwire g1 g2.1\nwire Cin g2.2\nwire A g3.1\nwire B g3.2\nwire g1 g4.1\nwire Cin g4.2\nwire g3 g5.1\nwire g4 g5.2\n"
	"wire g5 Cout\nwire g2 S";
static void test_steps ()
{
	// AC 10: the full adder, depth 3
	Level F; make (F, "full", "A B Cin", "Cout S", "*", 5, 6, f_full);
	Circuit c = from (F, FULL);
	CheckResult r = check (c, F);
	CHECK (r.won && r.stars == 3, "the full adder wins");
	for (unsigned row = 0; row < 8; row++)
	{
		Eval live, e1, e3, e0;
		evaluate (c, row, live);
		CHECK (live.maxDepth == 3, "3 depths (%d)", live.maxDepth);
		evaluate_to (c, row, 0, e0);
		evaluate_to (c, row, 1, e1);
		evaluate_to (c, row, 3, e3);
		bool ok1 = true, ok3 = true, ok0 = true;
		for (int i = 0; i < c.n; i++)
		{
			if (i >= c.ni + c.no) ok1 = ok1 && ((live.depth[i] <= 1) == (e1.v[i] != LX)) && (e1.v[i] == LX || e1.v[i] == live.v[i]);
			if (i >= c.ni + c.no) ok0 = ok0 && e0.v[i] == LX;
			ok3 = ok3 && e3.v[i] == live.v[i];
		}
		CHECK (ok0 && e0.v[c.ni] == LX, "row %u, step 0: every gate and lamp unknown", row);
		CHECK (ok1 && e1.v[c.ni] == LX && e1.v[c.ni + 1] == LX, "row %u, step 1: only depth 1 known", row);
		CHECK (ok3, "row %u, step 3: everything as live", row);
		CHECK (live.depth[c.find ("g1")] == 1 && live.depth[c.find ("g2")] == 2 && live.depth[c.find ("g5")] == 3 && live.depth[c.ni] == 3, "depths");
	}
	// an open input: counted 0 live, the parts after it not determined
	Circuit d = from (F, "part g1 AND 10 10\nwire A g1.1\nwire g1 S");
	Eval e; evaluate (d, 7, e);
	CHECK (e.v[d.find ("g1")] == L0 && !e.det[d.find ("g1")] && !e.det[d.find ("S")] && e.det[0] && !e.det[d.find ("Cout")], "open: 0 and undetermined");
}
static void test_text ()
{
	Level F; make (F, "full", "A B Cin", "Cout S", "*", 5, 6, f_full);
	// AC 11: written, read, written: the same text
	Circuit c = from (F, FULL);
	char t1[TEXTCAP]; int n1 = write_text (c, t1, sizeof t1);
	CHECK (n1 == (int) strlen (t1) && !strcmp (t1, FULL), "canonical text:\n%s", t1);
	Circuit d = from (F, t1);
	CHECK (!strcmp (text_of (d), t1), "read back, written again: the same");
	// comments, blank lines, \r\n, spaces
	d = from (F, "# a comment\r\n\r\n  part   g1  XOR 8 4 \r\nwire A g1.1\r\n");
	CHECK (!strcmp (text_of (d), "part g1 XOR 8 4\nwire A g1.1"), "comments and \\r\\n: %s", text_of (d));
	Circuit e0; e0.setup (F); CHECK (!strcmp (text_of (e0), ""), "an empty board: an empty text");
	d = from (F, "wire A S"); CHECK (!strcmp (text_of (d), "wire A S"), "a lamp wired straight");
	// the errors, with their lines; the circuit unchanged
	struct { const char *text; Err err; int line; } bad[] = {
		{ "part g1 AND 8 4\npart g2 FOO 8 12", E_TYPE, 2 },
		{ "part g1 AND 8 4\nwire A g1", E_PIN, 2 },
		{ "part g1 NOT 8 4\nwire A g1.2", E_PIN, 2 },
		{ "part g1 AND 8 4\nwire A g1.3", E_PIN, 2 },
		{ "part g1 AND 8 4\nwire A g1.1\n# x\nwire B g1.1", E_DOUBLE, 4 },
		{ "part g1 AND 8 4\npart g2 OR 16 4\nwire g1 g2.1\nwire g2 g1.1", E_LOOP, 4 },
		{ "part g1 AND 8 4\nwire g1 g1.2", E_LOOP, 2 },
		{ "part g1 AND 8 4\nwire X g1.1", E_NAME, 2 },
		{ "part g1 AND 8 4\nwire A S.1", E_PIN, 2 },
		{ "part g1 AND 8 4\nwire S g1.1", E_PIN, 2 },
		{ "part g1 AND 8 4\nwire A B", E_PIN, 2 },
		{ "part g1 AND 8 4\nwire A g2.1", E_NAME, 2 },
		{ "\npart g1 AND 2 4", E_OUTSIDE, 2 },
		{ "part g1 AND 8 4\npart g2 AND 9 5", E_OVERLAP, 2 },
		{ "part g1 AND 8 4\npart g1 OR 16 4", E_NAME, 2 },
		{ "part G1 AND 8 4", E_NAME, 1 },
		{ "part g1 AND 8", E_SYNTAX, 1 },
		{ "part g1 AND x 4", E_SYNTAX, 1 },
		{ "parts g1 AND", E_SYNTAX, 1 },
		{ "wire A S extra", E_SYNTAX, 1 },
	};
	Circuit keep = from (F, FULL);
	for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++)
	{
		Err err = E_OK; int line = 0;
		bool ok = read_text (keep, F, bad[k].text, &err, &line);
		CHECK (!ok && err == bad[k].err && line == bad[k].line, "bad text %u: %s at %d, expected %s at %d", k, err_name (err), line, err_name (bad[k].err), bad[k].line);
	}
	CHECK (!strcmp (text_of (keep), FULL), "a refused text leaves the circuit");
	Level N; make (N, "nand", "A B", "Out", "NOT AND OR", 2, 3, f_and);
	Err err; int line;
	CHECK (!read_text (keep, N, "part g1 NAND 8 4", &err, &line) && err == E_NOT_ALLOWED && line == 1, "a part the level does not allow");
	// without places: by depth, nothing outside, nothing overlapping
	d = from (F, "part g1 XOR\npart g2 XOR\npart g3 AND\npart g4 AND\npart g5 OR\nwire A g1.1\nwire B g1.2\nwire g1 g2.1\nwire Cin g2.2\nwire A g3.1\n"
		"wire B g3.2\nwire g1 g4.1\nwire Cin g4.2\nwire g3 g5.1\nwire g4 g5.2\nwire g5 Cout\nwire g2 S");
	CHECK (d.p[d.find ("g1")].x == 7 && d.p[d.find ("g2")].x == 13 && d.p[d.find ("g5")].x == 19 && d.p[d.find ("g3")].x == 7, "columns by depth");
	CHECK (check (d, F).won, "auto-placed: still the full adder");
}
static void test_history ()
{
	// AC 15
	Level F; make (F, "full", "A B Cin", "Cout S", "*", 5, 6, f_full);
	Circuit c; c.setup (F);
	History *h = new History;					// (as the window: not on the stack)
	h->start (c);
	CHECK (!h->canUndo () && !h->canRedo (), "nothing to undo at the start");
	char st[6][TEXTCAP]; int ns = 0;
	snprintf (st[ns++], TEXTCAP, "%s", text_of (c));
	int g = c.add (P_AND, 10, 4); h->push (c); snprintf (st[ns++], TEXTCAP, "%s", text_of (c));		// place
	c.connect (0, g, 0); h->push (c); snprintf (st[ns++], TEXTCAP, "%s", text_of (c));			// wire
	c.move (g, 12, 6); h->push (c); snprintf (st[ns++], TEXTCAP, "%s", text_of (c));			// move
	c.add (P_OR, 20, 20); c.remove (g); h->push (c); snprintf (st[ns++], TEXTCAP, "%s", text_of (c));	// delete
	c.clear (); h->push (c); snprintf (st[ns++], TEXTCAP, "%s", text_of (c));				// clear
	h->push (c);
	CHECK (h->n == 6, "an unchanged board pushes nothing (%d)", h->n);
	for (int k = ns - 2; k >= 0; k--) { CHECK (h->undo (c, F) && !strcmp (text_of (c), st[k]), "undo to state %d", k); }
	CHECK (!h->canUndo () && !h->undo (c, F), "nothing more to undo");
	for (int k = 1; k < ns; k++) { CHECK (h->redo (c, F) && !strcmp (text_of (c), st[k]), "redo to state %d", k); }
	CHECK (!h->canRedo (), "nothing more to redo");
	h->undo (c, F); h->undo (c, F);
	c.add (P_NOT, 26, 26); h->push (c);
	CHECK (!h->canRedo () && h->canUndo (), "a change drops the redo steps");
	// 64 steps kept, the 65th drops the oldest
	c.setup (F); h->start (c);
	char first[TEXTCAP]; int gx = -1;
	for (int k = 0; k < 70; k++)
	{
		if (k == 0) { gx = c.add (P_AND, 6, 0); h->push (c); snprintf (first, sizeof first, "%s", text_of (c)); continue; }
		c.move (gx, 6 + (k % 20), (k / 20) * 5); h->push (c);
	}
	int undone = 0; while (h->undo (c, F)) undone++;
	CHECK (undone == HIST_STEPS, "64 undo steps (%d)", undone);
	CHECK (strcmp (text_of (c), first) && c.gates () == 1, "the oldest states dropped");
	delete h;
}
static void test_table ()
{
	// AC 16
	Level H; make (H, "half", "A B", "C S", "*", 2, 3, f_half);
	char t[512];
	int n = truth_table_text (H, 0, t, sizeof t);
	CHECK (n == (int) strlen (t) && !strcmp (t, "A\tB\tC\tS\n0\t0\t0\t0\n0\t1\t0\t1\n1\t0\t0\t1\n1\t1\t1\t0\n"), "half's table:\n%s", t);
	Circuit c = from (H, "part g1 AND 10 4\npart g2 XOR 10 16\nwire A g1.1\nwire B g1.2\nwire A g2.1\nwire B g2.2\nwire g1 C\nwire g2 S");
	CheckResult r = check (c, H);
	CHECK (r.won && r.stars == 3, "half adder in 2");
	truth_table_text (H, &r, t, sizeof t);
	CHECK (!strcmp (t, "A\tB\tC\tS\tC'\tS'\n0\t0\t0\t0\t0\t0\n0\t1\t0\t1\t0\t1\n1\t0\t0\t1\t0\t1\n1\t1\t1\t0\t1\t0\n"), "after a Check:\n%s", t);
	c = from (H, "part g1 OR 10 4\nwire A g1.1\nwire B g1.2\nwire g1 C\nwire g1 S");
	r = check (c, H);
	truth_table_text (H, &r, t, sizeof t);
	CHECK (!r.won && r.nwrong == 3 && !strcmp (t, "A\tB\tC\tS\tC'\tS'\n0\t0\t0\t0\t0\t0\n0\t1\t0\t1\t1\t1\n1\t0\t0\t1\t1\t1\n1\t1\t1\t0\t1\t1\n"), "a wrong board's columns:\n%s", t);
	char small[8]; n = truth_table_text (H, 0, small, sizeof small);
	CHECK (n == 40 && strlen (small) == 7, "cut to its buffer, its length told (%d)", n);
}

int main (int argc, char **argv)
{
	test_geometry ();
	test_routes ();
	test_stars_and_rows ();
	test_wires ();
	test_steps ();
	test_text ();
	test_history ();
	test_table ();
	(void) argc; (void) argv;
	if (fails) { printf ("FAIL circuits (%d of %d checks)\n", fails, checks); return 1; }
	printf ("ok   circuits (%d checks: the engine)\n", checks);
	return 0;
}
