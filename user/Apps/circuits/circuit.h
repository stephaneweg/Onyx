//
// circuit.h -- Circuits' engine: the board (switches, lamps, gates, wires) in grid cells, the evaluation (live, and
// depth by depth for the step mode), the Check against a level's truth table and its stars, the circuit text (a
// solution, the player's board saved, the clipboard), the undo history, the wires' routes and the hit tests the
// window needs. No user interface and no file here: the window (main.cpp) draws and loads, the host test
// (tools/tests/circuits/circuitstest.cpp) checks every level of the packs with it.
//
// The board is BOARD_W x BOARD_H cells. The level's switches stand in the left strip (x = 0, 5 x 2 cells, their
// output pin at (5, y + 1)), its lamps in the right one (x = 34, 6 x 2, their input pin at (34, y + 1)), input k of
// n at y = (2k + 1) * 30 / (2n) - 1; neither moves. A gate is 5 x 4 cells, between the strips (columns 6 to 33),
// overlapping nothing: its inputs at (x, y + 1) and (x, y + 3) -- NOT's one at (x, y + 2) --, its output at
// (x + 5, y + 2). An output feeds any number of inputs, an input takes one wire, and a wire that would make a loop
// is refused: signals only go forward (combinational logic).
//
// Circuit text, one statement a line ('#': a comment):
//   part <name> <NOT|AND|OR|XOR|NAND|NOR> [<x> <y>]   a gate; without x y it is placed by its depth
//   wire <from> <to>                                  from a switch or a gate, to a gate's pin (g1.1, g1.2) or a lamp
// Written canonically: the gates in their order, then the wires into the gates (pin 1, 2), then into the lamps.
//
// The packs and the player's progress: at the end (over FileKit's fk_kv documents, read and written by the window);
// the lesson cards: lessons.h.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _circuits_circuit_h
#define _circuits_circuit_h

#include <stdlib.h>
#include <string.h>

namespace circuits {

enum PartType { P_SWITCH, P_LAMP, P_NOT, P_AND, P_OR, P_XOR, P_NAND, P_NOR, P_COUNT_ };
enum { MAXIO = 4, MAXGATES = 48, MAXPARTS = 2 * MAXIO + MAXGATES, MAXROWS = 16, MAXWIRES = 2 * MAXGATES + MAXIO };
enum { BOARD_W = 40, BOARD_H = 30 };				// the grid, in cells
enum { SWITCH_W = 5, SWITCH_H = 2, LAMP_X = 34, LAMP_W = 6, LAMP_H = 2, GATE_W = 5, GATE_H = 4,
       GATE_MINX = 6, GATE_MAXX = 33 };			// a gate's footprint lies within columns GATE_MINX .. GATE_MAXX
enum { NAMEL = 9, IONAMEL = 5, TEXTCAP = 8192 };		// a gate's name (8 chars), an input's / output's (4); a circuit's text
enum Err { E_OK, E_NOT_ALLOWED, E_FULL, E_OVERLAP, E_OUTSIDE, E_FIXED, E_LOOP, E_PIN, E_SAME,
           E_LAMP_OPEN, E_GATE_OPEN, E_SYNTAX, E_NAME, E_TYPE, E_DOUBLE, E_COUNT_ };
enum Lv { L0 = 0, L1 = 1, LX = 2 };				// a signal: 0, 1, not computed yet (step mode)

const char *type_name (int type);				// "AND"; "" for a switch / lamp
int type_of (const char *name);					// "AND" -> P_AND; -1 not a gate's name
const char *err_name (Err e);					// "E_LOOP" (the window has the words)
static inline int gate_type (int t) { return t >= P_NOT && t < P_COUNT_; }
int fixed_row (int k, int n);					// the row of the switch / lamp k of n

// ---- a level (what the engine needs of it; levels.h reads it from a pack) ----
struct Level
{
	char id[24];
	char title[2][80], text[2][480], hint[2][480];		// [0] English, [1] French ("": the English one)
	char topic[16];						// its "concept": the lesson card shown the first time
	int ninputs, noutputs;
	char inName[MAXIO][IONAMEL], outName[MAXIO][IONAMEL];	// most significant first
	unsigned parts;						// the gates allowed: bit 1 << P_NOT ...
	int par3, par2;						// the largest gate counts for 3 and 2 stars
	unsigned want[MAXIO];					// output o: bit r = its value on row r (inputs = r, the first one the MSB)
	char *solution;						// a reference circuit (malloc'd; 0: none)
	int solutionLine;					// its first line in the pack (the errors' lines)
	Level () { memset (this, 0, sizeof *this); }
	~Level () { free (solution); }
	int rows () const { return 1 << ninputs; }
	const char *titleOf (int lang) const { return title[lang][0] ? title[lang] : title[0]; }
	const char *textOf (int lang) const { return text[lang][0] ? text[lang] : text[0]; }
	const char *hintOf (int lang) const { return hint[lang][0] ? hint[lang] : hint[0]; }
	bool allows (int type) const { return gate_type (type) && (parts & (1u << type)); }
private:
	Level (const Level &);
	Level &operator= (const Level &);
};

// ---- the board ----
struct Part
{
	char name[NAMEL];
	unsigned char type;
	short x, y;						// its left-top cell
	short in[2];						// in[k]: the part feeding input k, -1 none
};
struct Circuit
{
	Part p[MAXPARTS];
	int n, ni, no;						// parts [0, ni) the switches, [ni, ni + no) the lamps, then the gates
	unsigned allowed;					// the gates the level allows (bit 1 << type)

	Circuit () { n = ni = no = 0; allowed = 0; }
	void setup (const Level &L);				// the level's switches and lamps, no gate, no wire
	int  find (const char *name) const;			// a part by its name -> index, -1
	int  pins (int i) const;				// its inputs: switch 0, lamp 1, NOT 1, the others 2
	bool fixed (int i) const { return i < ni + no; }	// a switch or a lamp
	int  gates () const { return n - ni - no; }		// the gates on the board (an unused one counts)
	int  wires () const;					// the wires on the board
	bool upstream (int a, int b) const;			// does part a depend on part b (b feeds a, through any path)

	// changes (each one checked; nothing changed when refused)
	Err  placeErr (int type, int x, int y, int ignore = -1) const;	// could a gate go there (ignore: a gate being moved)
	bool fits (int type, int x, int y) const { return placeErr (type, x, y) == E_OK; }
	int  add (int type, int x, int y, Err *e = 0);		// -> the new gate's index (named the lowest free g<n>), -1
	Err  move (int i, int x, int y);			// E_FIXED (a switch / lamp), E_OUTSIDE, E_OVERLAP
	bool canConnect (int src, int dst, int pin, Err *e = 0) const;	// connect's verdict, nothing done
	Err  connect (int src, int dst, int pin);		// replaces the wire already there; E_LOOP, E_PIN
	void disconnect (int dst, int pin);
	Err  remove (int i);					// a gate and its wires (E_FIXED for a switch / lamp)
	void clear ();						// every gate and every wire

	// geometry, in cells
	void box (int i, int &x, int &y, int &w, int &h) const;
	void pinIn (int i, int k, int &x, int &y) const;
	void pinOut (int i, int &x, int &y) const;
	int  at (int cx, int cy) const;				// the part whose footprint holds the cell (cx, cy), -1
	// The route of the wire into dst's input pin: its polyline's points (x, y pairs in xy) -> how many (0: no
	// wire). Forward: 4 points, the vertical in a free column near the middle (no vertical of another source
	// overlapping, no footprint crossed); backward (the target not right of the source): 6 points under both parts.
	int  route (int dst, int pin, int *xy, int cap) const;
	int  junctions (int *xy, int cap) const;		// the dots where a wire leaves another of the same source
	// Hit tests, in 1/16 cell: the part with a pin within 0.6 cell -> its index (*isOut, *pin: which one), -1; the
	// wire within 1/3 cell of a segment -> the part it feeds (*pin: which input), -1.
	int  pinAt (int gx16, int gy16, bool *isOut, int *pin) const;
	int  wireAt (int gx16, int gy16, int *pin) const;
};

// ---- the evaluation ----
struct Eval
{
	unsigned char v[MAXPARTS];				// each part's output (a lamp: what it shows): L0, L1, LX
	bool det[MAXPARTS];					// false: an open input on its way (live mode counts it 0)
	int depth[MAXPARTS];					// switches 0, a gate 1 + its deepest input's, a lamp its source's
	int maxDepth;						// the deepest gate (0: none)
};
// Live: inputs = the row (switch k = bit ni - 1 - k), every part computed, an open input counting 0.
void evaluate (const Circuit &c, unsigned inputs, Eval &e);
// Step mode: the gates of depth <= step computed, the deeper ones (and the lamps they feed) LX.
void evaluate_to (const Circuit &c, unsigned inputs, int step, Eval &e);

// ---- the Check ----
struct CheckResult
{
	Err err;						// E_OK, or E_LAMP_OPEN / E_GATE_OPEN (refused: nothing evaluated)
	int errPart;						// the lamp / gate refused
	unsigned got[MAXIO];					// output o: bit r = its value on row r
	unsigned wrong;						// bit r: row r wrong
	int nwrong, firstWrong;					// how many, the first (-1: none)
	int gates, stars;
	bool won;
};
CheckResult check (const Circuit &c, const Level &L);		// every row; refuses an open lamp, then an open gate input
int stars_for (int gates, int par3, int par2, bool won);	// 0 not won; 3 <= par3; 2 <= par2; else 1

// ---- the circuit text ----
int  write_text (const Circuit &c, char *out, int cap);	// -> its length (no new line at the end; "" no gate no wire)
// The text on a board set up for L -> true; false: *e and *line (1-based) say why, c unchanged.
bool read_text (Circuit &c, const Level &L, const char *text, Err *e = 0, int *line = 0);
// Copy Truth Table: a header of the names, then a line a row, tab-separated; with r (a Check) the obtained outputs
// after the expected ones (their names with a ' after them) -> its length.
int  truth_table_text (const Level &L, const CheckResult *r, char *out, int cap);

// ---- undo / redo: the board's texts, the current one and up to HIST_STEPS before it ----
enum { HIST_STEPS = 64 };
struct History
{
	char *s[HIST_STEPS + 1];				// (malloc'd texts)
	int n, cur;
	History () { n = cur = 0; for (int i = 0; i <= HIST_STEPS; i++) s[i] = 0; }
	~History () { reset (); }
	void reset ();						// nothing kept
	void start (const Circuit &c);				// the level's first state (no undo before it)
	void push (const Circuit &c);				// after a change: the redo steps dropped, the oldest past 64
	bool canUndo () const { return cur > 0; }
	bool canRedo () const { return cur + 1 < n; }
	bool undo (Circuit &c, const Level &L);
	bool redo (Circuit &c, const Level &L);
private:
	History (const History &);
	History &operator= (const History &);
};

} // namespace circuits

#endif
