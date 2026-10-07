//
// terrain.h -- Critters' pixel terrain: one material byte and one colour a logical pixel, the map's edges, digging,
// bursting, laying bricks, the changed rectangle (what the window redraws) and an incremental hash of the materials
// (the per-step checksum of the determinism test). No user interface, no file, no float, integers only.
//
// Edges (02 #3): the columns left of 0 and right of w-1 are steel (side walls), the row above 0 is steel (a ceiling),
// below h-1 is the void (a creature whose feet go there dies).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _critters_terrain_h
#define _critters_terrain_h

#include <stdint.h>

namespace critters {

enum Mat : uint8_t { M_EMPTY = 0, M_EARTH, M_STEEL, M_WATER, M_LAVA };

struct Rect { int x0, y0, x1, y1; };			// inclusive; empty when x1 < x0

// splitmix64's finaliser and a 32-bit position hash (the speckle texture): fixed integer functions, not a generator
uint64_t mix64 (uint64_t z);
uint32_t mix32 (int x, int y, int k);

struct Terrain
{
	int w, h;					// 320..1600 x 100..160 (a level's size)
	uint8_t  *m;					// w*h materials (Mat)
	uint32_t *col;					// w*h colours 0x00RRGGBB (the drawing's layer: Canvas's format)
	uint32_t bg, brickColour;			// the level's background, the bricks' colour
	uint64_t hash;					// XOR over the pixels of mix64 (i * 8 + m[i]), kept up to date by set ()
	Rect dirty;					// changed since the window last took it (take_dirty)

	Terrain () : w (0), h (0), m (0), col (0), bg (0), brickColour (0), hash (0) { dirty.x0 = dirty.y0 = 0; dirty.x1 = dirty.y1 = -1; }
	~Terrain () { free (); }
	bool alloc (int w, int h);			// all empty, colour bg (set bg first); false: no memory
	void free ();

	int  at (int x, int y) const			// outside: x<0 / x>=w / y<0 -> M_STEEL; y>=h -> M_EMPTY (the void)
	{
		if (x < 0 || x >= w || y < 0) return M_STEEL;
		if (y >= h) return M_EMPTY;
		return m[y * w + x];
	}
	bool solid (int x, int y) const { int k = at (x, y); return k == M_EARTH || k == M_STEEL; }
	bool hazard (int x, int y) const { int k = at (x, y); return k == M_WATER || k == M_LAVA; }

	void set (int x, int y, uint8_t mat, uint32_t colour);	// the one writer after the build: the hash, the dirty rectangle
	int  dig_rect (int x0, int y0, int x1, int y1);	// earth -> empty (bg) in the rectangle (clipped) -> earth pixels removed
	bool any_steel (int x0, int y0, int x1, int y1) const;	// a steel pixel in the rectangle (inside the map only)
	int  dig_disc (int cx, int cy, int r);		// earth -> empty where dx*dx + dy*dy <= r*r -> pixels removed
	// A builder's brick, its feet at (x, y) facing dir: columns x - dir ... x + 4*dir (6 px), rows y - 1 ... y (2 px);
	// only empty pixels are filled (earth, the bricks' colour, the top row lighter) -> pixels filled
	int  brick (int x, int y, int dir);
	void touch (int x0, int y0, int x1, int y1);	// the rectangle added to dirty (clipped)
	Rect take_dirty ();				// the changed rectangle, then dirty is emptied
	uint64_t full_hash () const;			// the same value as hash, recomputed (the test compares)
};

}
#endif
