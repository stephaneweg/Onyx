//
// table.h -- Pinball's table: the model of a ".table" file (the walls, the toys, the flippers, the rules, the artwork)
// and its reader. No user interface and no file here: the window reads the file's text and hands it to load_table;
// the host test (tools/tests/pinball/pinballtest.cpp) loads every table with it.
//
// The format (docs/04 "Pinball", 02-product-analysis §7.1): a UTF-8 text of [block] headers and "key = value" lines,
// read with FileKit's fk_kv (a block name may come many times: each header is one element). Units: table units,
// origin top-left, x right, y down (towards the player); angles in degrees (0 = +x, 90 = +y, clockwise on screen).
// A file that breaks a rule is refused as a whole, with the line and the reason: "line 12: unknown block [bumber]".
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _pinball_table_h
#define _pinball_table_h

#include <stdlib.h>
#include <string.h>
#include "filekit/filekit.h"			// (fk_kv_*: the table's text)

namespace pinball {

struct Vec { double x, y; };

// The limits of a table (02 §7.1.2); a file over one is refused ("too many lanes (max 32)").
enum {
	MAXSEG = 2000,				// collision segments (walls, slings, targets)
	MAXWALL = 256,				// [wall] blocks
	MAXCIRC = 64,				// posts + bumpers + saucers
	MAXARC = 32, MAXTGT = 64, MAXBANK = 32, MAXLANE = 32, MAXGROUP = 32, MAXGATE = 16, MAXRAMP = 4,
	MAXSLING = 16, MAXFLIP = 3, MAXRULE = 32, MAXACT = 8, MAXSHAPE = 128, MAXLABEL = 64,
	MAXPTS = 64,				// the points of a ramp's path, of a [shape]
	IDL = 25,				// an id: 24 characters + 0
	TEXTL = 164,				// a text (a goal: 160 characters)
	MAXFILE = 65536				// a .table's size in bytes
};

// A text in the two languages: get (1) is the French one when the file has it, else the English one.
struct Text
{
	char en[TEXTL], fr[TEXTL];
	const char *get (int lang) const { return lang == 1 && fr[0] ? fr : en; }
};

enum SegKind { S_WALL, S_SLING, S_TARGET };	// a collision segment's kind (gates and ramp entries are apart)
enum CircKind { C_POST, C_BUMPER };
enum Side { SIDE_LEFT, SIDE_RIGHT };

struct Seg					// a collision segment a -> b
{
	Vec a, b;
	double bounce, friction;
	int kind;				// SegKind
	int elem;				// the sling / target it is the face of (-1: a wall)
	int wall;				// the [wall] it belongs to (-1: a sling's or a target's face)
	Vec lo, hi;				// its bounding box
};
struct Wall					// a [wall]: its segments seg[first .. first + n - 1], drawn as a polyline
{
	int first, n;
	bool closed, outline;
	double bounce, friction;
	unsigned colour; int width;
};
struct Arc					// a [arc]: the part of a circle from `from` to `to` (clockwise on screen)
{
	Vec c; double r;
	double from, to;			// degrees, as written (to > from after loading: to - from in (0, 360])
	Vec fu, tu;				// the unit vectors at from and to (the range test, without atan2)
	double span;				// to - from, degrees
	double bounce, friction;
	unsigned colour; int width;
};
struct Circle					// a [post] or a [bumper]
{
	char id[IDL];				// "" for a post
	Vec c; double r;
	double bounce, kick;			// kick: a bumper's least outgoing speed
	int kind;				// CircKind
	int score;
	unsigned colour;
};
struct Sling { char id[IDL]; int seg, score; double kick; unsigned colour; };
struct Target { char id[IDL]; int seg, bank /* -1: a standup */, score; bool drop; unsigned colour; };
struct Bank { char id[IDL]; int tgt[8], n; };
struct Lane { char id[IDL]; double x, y, w, h; int group /* -1: none */, score; unsigned colour; };
struct Gate { Vec a, b, pass /* the unit vector of the allowed crossing */; };
struct Flip
{
	int side;				// Side: which key moves it, which way it points
	Vec pivot; double len;
	double rest, up;			// world angles in radians (y down; a right flipper's mirrored: pi - angle)
	double restDeg, upDeg;			// as written
	double r0, r1;				// the radius at the pivot, at the tip
	double speed;				// rad/s
	double bounce;
	unsigned colour;
};
struct Ramp
{
	char id[IDL];
	Vec a, b, pass;				// the entry segment, crossed along pass
	Vec path[MAXPTS]; int npath;		// the ball drawn along it; the last point: where it comes back
	double time;				// seconds on the ramp
	Vec out;				// the velocity it comes back with
	int score;
	unsigned colour; int width;
};
struct Saucer { char id[IDL]; Vec c; double r, hold; Vec out; int score; unsigned colour; };
struct Shape { Vec p[MAXPTS]; int n; unsigned colour; };
struct Label { Vec at; Text text; int size, angle; unsigned colour; };

enum EvKind { EV_BANK, EV_LANES, EV_RAMP, EV_SAUCER, EV_HIT };	// a [rule]'s "when"
enum HitKind { H_LANE = 1, H_TARGET, H_BUMPER, H_SLING };	// "hit <id>": Rule::ref = kind << 16 | index
enum ActKind { A_SCORE, A_BONUS, A_MULT, A_MULTIBALL, A_EXTRABALL, A_BALLSAVE };
struct Action { int kind; long n; };
struct Rule
{
	int when;				// EvKind
	int ref;				// the bank / lane group / ramp / saucer index, or (HitKind << 16 | index) for EV_HIT
	int count;				// events that fire it
	bool once;				// at most once a game
	Action act[MAXACT]; int nact;
	Text message;				// "" when none
};

struct Table
{
	Text name, goal;
	double w, h;				// size
	double gravity, ball, ballsave, drain;
	int balls;
	int rotate;				// the lane group whose lamps shift with the flippers (-1: none)
	unsigned background;

	Seg seg[MAXSEG]; int nseg;
	Wall wall[MAXWALL]; int nwall;
	Arc arc[MAXARC]; int narc;
	Circle circ[MAXCIRC]; int ncirc;	// posts and bumpers (saucers apart, counted in the same limit)
	Sling sling[MAXSLING]; int nsling;
	Target tgt[MAXTGT]; int ntgt;
	Bank bank[MAXBANK]; int nbank;
	Lane lane[MAXLANE]; int nlane;
	char group[MAXGROUP][IDL]; int ngroup;	// the lane groups' names
	Gate gate[MAXGATE]; int ngate;
	Flip flip[MAXFLIP]; int nflip;
	Ramp ramp[MAXRAMP]; int nramp;
	Saucer saucer[MAXCIRC]; int nsaucer;
	Shape shape[MAXSHAPE]; int nshape;
	Label label[MAXLABEL]; int nlabel;
	Rule rule[MAXRULE]; int nrule;
	Vec plunger; double plungerMax, plungerAuto;
	Vec outline[MAXSEG]; int noutline;	// the outline polygon: every ball stays inside it
};

// Why a table was refused. reason: the English sentence (the tests compare it); fmt / arg: the same as a format and its
// arguments (every %s), for the window to translate the format before filling it: TR (e.fmt), e.arg[0], e.arg[1].
// line: the offending key's line, or its block's header line for a missing key (0: the file itself).
struct LoadError
{
	int line;
	char reason[128];
	const char *fmt;
	char arg[2][48];
};

// Reads a table from its text -> true; false: t is cleared (nothing usable) and e says why. The text is the whole
// file (the caller refuses a file over MAXFILE bytes with load_error_file, or passes it here: refused too).
bool load_table (const char *text, Table &t, LoadError &e);
// A file that could not be read (big = over MAXFILE bytes): the error at line 0.
void load_error_file (LoadError &e, bool big);
// Is p inside the table's outline polygon? (even-odd rule)
bool inside_outline (const Table &t, Vec p);
// The id of a "hit" reference (Rule::ref of an EV_HIT rule) -> the element's id ("" if none).
const char *hit_id (const Table &t, int ref);

// The core's own sine and cosine (radians): the same bits on every machine (no libm in the physics).
double pb_sin (double a);
double pb_cos (double a);
static const double PB_PI = 3.14159265358979323846;

// The load errors' formats (translated by the window; check.py reads these lines):
// TR: unknown block [%s]
// TR: [table] must be the first block, once
// TR: [%s] needs %s
// TR: bad value for %s
// TR: %s out of range
// TR: id %s used twice
// TR: the table needs one closed wall with id = outline
// TR: the table needs 2 or 3 flippers, a left one and a right one
// TR: the table needs one [plunger]
// TR: unknown %s
// TR: bank %s needs 2 to 8 targets
// TR: too many %s (max %s)
// TR: the file is too big
// TR: cannot read the file
// The things counted in "too many %s" (the window translates the first argument too):
// TR: wall segments
// TR: walls
// TR: arcs
// TR: posts, bumpers and saucers
// TR: slings
// TR: targets
// TR: banks
// TR: lanes
// TR: lane groups
// TR: gates
// TR: ramps
// TR: rules
// TR: actions
// TR: shapes
// TR: labels
// TR: points

}

#endif
