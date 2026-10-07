//
// level.h -- Critters' level: the model of a ".level" file (the level's facts, the shapes the terrain is drawn from,
// the hatches, the exits, the painted words) and its reader; the terrain built from the shapes. No user interface and
// no file here: the window reads the file's text and hands it to load_level; the host test
// (tools/tests/critters/critterstest.cpp) and crsim (tools/critters/crsim.cpp) do the same.
//
// The format (02-product-analysis §7.1, docs/04 "Critters"): a UTF-8 text of [block] headers and "key = value"
// lines read with FileKit's fk_kv (a block name may come many times: each header is one element). Units: logical
// pixels, origin top-left, x right, y down; times in seconds. A file that breaks a rule is refused as a whole with the
// line and the reason: "line 14: unknown block [shap]"; never a half-built level. Integers only.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _critters_level_h
#define _critters_level_h

#include <stdint.h>
#include "filekit/filekit.h"			// (fk_kv_*: the level's text)
#include "terrain.h"

namespace critters {

// The limits (02 §7.1.2); a file over one is refused ("too many shapes (max 256)").
enum {
	MAXW = 1600, MAXH = 160, MINW = 320, MINH = 100,
	MAXCRIT = 80,				// creatures a level
	MAXSHAPE = 256, MAXPTS = 64, MAXHATCH = 4, MAXEXIT = 4, MAXLABEL = 32,
	NROLES = 8,
	MAXFILE = 65536,			// a .level's size in bytes
	NAMEC = 32, HINTC = 160,		// a name's, a hint's (and a label's) characters
	HINTL = 644				// a text's bytes (160 UTF-8 characters + 0)
};
enum Role { R_CLIMBER, R_FLOATER, R_BLOCKER, R_BUILDER, R_DIGGER, R_EXPLODER, R_BASHER, R_MINER };
extern const char *const ROLE_WORD[NROLES];	// "climber" ... "miner": the file's and the .sol's words (never translated)
enum ShapeKind { SH_RECT, SH_POLY, SH_CIRCLE };
enum Texture { TX_PLAIN, TX_SPECKLE, TX_STRIPES, TX_BRICKS };
enum { MAT_ERASE = 255 };			// a shape's material "erase": back to empty

// A text in the two languages: get (1) is the French one when the file has it, else the English one (the window passes
// !strcmp (uk_lang (), "fr")).
struct Text
{
	char en[HINTL], fr[HINTL];
	const char *get (int lang) const { return lang == 1 && fr[0] ? fr : en; }
};
struct Shape
{
	uint8_t kind, mat, tex;			// ShapeKind; Mat or MAT_ERASE; Texture
	uint32_t colour, colour2;
	int16_t p[2 * MAXPTS]; int n;		// rect: x y w h (n = 4); circle: cx cy r (n = 3); polygon: n points (2n values)
	int x0, y0, x1, y1;			// its bounding box (inclusive, not clipped)
};
struct Point { int16_t x, y; };
struct Hatch { Point at; int8_t dir; };	// dir -1 left, +1 right
struct Label { Point at; Text text; uint32_t colour; };
struct Level
{
	Text name, hint;
	int w, h, count, save, timeSec, rate, roles[NROLES];
	int start;				// the view's left edge at the start, px; -1: centred on the first hatch (the window's)
	uint32_t bg, brick;			// the background's (the sky's) and the bricks' colours
	Shape shape[MAXSHAPE]; int nshape;
	Hatch hatch[MAXHATCH]; int nhatch;
	Point exit[MAXEXIT]; int nexit;
	Label label[MAXLABEL]; int nlabel;
};

// A refusal: the line (0: the file itself), the reason in English (what the tests compare), and its format with its
// two %s arguments (the window translates the format, TR (e.fmt), then fills it).
struct LoadError { int line; char reason[112]; const char *fmt; char arg[2][40]; };

// The text read into lv -> true; false: e says why and lv is cleared (no half-built level). withDiggers2: the basher
// and the miner are built (02 SHOULD 1) -- else a count > 0 of them is refused.
bool load_level (const char *text, Level &lv, LoadError &e, bool withDiggers2 = false);
void load_error_file (LoadError &e, bool tooBig);	// line 0: "the file is too big" / "cannot read the file"
int  material_at (const Level &lv, int x, int y);	// the shapes evaluated at one pixel (Mat), without the terrain
// The whole map from the shapes (02 §7.1.3): t allocated at the level's size, colours, the bricks' colour -> false: no memory
bool build_terrain (const Level &lv, Terrain &t);

// The load errors' formats (translated by the window; check.py reads these lines):
// TR: unknown block [%s]
// TR: [level] must be the first block, once
// TR: [%s] needs %s
// TR: [shape] needs one of rect, points, circle
// TR: bad value for %s
// TR: %s out of range
// TR: the level needs 1 to 4 [%s]
// TR: [%s] is inside the terrain
// TR: role %s is not available
// TR: too many %s (max %s)
// TR: the file is too big
// TR: cannot read the file
// The things counted in "too many %s" (the window translates the first argument too):
// TR: shapes
// TR: points
// TR: labels

}
#endif
