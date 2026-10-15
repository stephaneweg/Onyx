//
// textures.h -- the game's few pictures, made at start-up (no file): a soft round glow (the sparkles), the speed
// pads' chevrons, a star. 0xAARRGGBB; numbered as the batches name them (TX_*).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_TEXTURES_H
#define CZ_TEXTURES_H

#include "gfx.h"

namespace cz {

enum { TX_NONE, TX_GLOW, TX_CHEVRON, TX_STAR, TX_SIGNS, TX_COUNT };

struct Textures
{
	TexStore store;
	unsigned glow[32 * 32], chevron[32 * 32], star[32 * 32];
	enum { SW = 256, SH = 256 };
	unsigned signs[SW * SH];		// the billboards' pictures: 2 x 4 cells of 128 x 64, white on black (tinted)
	void make ()
	{
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
			{
				float dx = ((float) x + 0.5f - 16) / 16, dy = ((float) y + 0.5f - 16) / 16, d = sqrtf (dx * dx + dy * dy);
				float g = clampf (1 - d, 0, 1); g = g * g * (3 - 2 * g);
				unsigned v = (unsigned) (g * 255);
				glow[y * 32 + x] = v << 24 | v << 16 | v << 8 | v;
				// chevrons: two arrows pointing up (t: along the pad)
				float u = fabsf ((float) x + 0.5f - 16) / 16, w = (float) y / 32;
				float ph = fmodf (w + u * 0.5f, 0.5f) / 0.5f;
				float c = ph < 0.45f ? 1.0f : 0.0f;
				unsigned cv = (unsigned) (c * 255);
				chevron[y * 32 + x] = 0xFF000000u | cv << 16 | cv << 8 | cv;
				// a five-pointed star, soft-edged
				float a = atan2f (dy, dx), r = 0.55f + 0.4f * cosf (5 * a) * 0.5f + 0.2f;
				float sv = clampf ((r - d) * 8, 0, 1);
				unsigned st = (unsigned) (sv * 255);
				star[y * 32 + x] = st << 24 | st << 16 | st << 8 | st;
			}
		makeSigns ();
		store.add (glow, 32, 32); store.add (chevron, 32, 32); store.add (star, 32, 32); store.add (signs, SW, SH);
	}
	// the signs' cells: a frame, and shapes (the game writes words over them: main.cpp's signWords)
	void cellRect (int cell, int *x, int *y) { *x = (cell % 2) * 128; *y = (cell / 2) * 64; }
	void put (int x, int y, unsigned v) { if (x >= 0 && y >= 0 && x < SW && y < SH) { unsigned o = signs[y * SW + x] & 255; if (v > o) signs[y * SW + x] = 0xFF000000u | v << 16 | v << 8 | v; } }
	void makeSigns ()
	{
		for (int i = 0; i < SW * SH; i++) signs[i] = 0xFF000000u;
		for (int cell = 0; cell < 8; cell++)
		{
			int x0, y0; cellRect (cell, &x0, &y0);
			// a bright frame
			for (int x = 3; x < 125; x++) for (int t = 0; t < 2; t++) { put (x0 + x, y0 + 3 + t, 255); put (x0 + x, y0 + 60 - t, 255); }
			for (int y = 3; y < 61; y++) for (int t = 0; t < 2; t++) { put (x0 + 3 + t, y0 + y, 255); put (x0 + 124 - t, y0 + y, 255); }
			// a shape: chevrons, a ring, a star, bars, a wave
			int k = cell % 4;
			for (int y = 8; y < 56; y++)
				for (int x = 8; x < 120; x++)
				{
					float fx = (float) (x - 64), fy = (float) (y - 32);
					unsigned v = 0;
					if (k == 0 && fmodf (fabsf (fx) * 0.0f + x - fabsf (fy) * 0.9f + 200, 26) < 9 && x > 70) v = 200;
					if (k == 1 && fabsf (sqrtf ((x - 100.0f) * (x - 100.0f) + fy * fy) - 16) < 3) v = 220;
					if (k == 2 && x > 92 && (y / 6) % 2 == 0 && (y < 30 || x > 104)) v = 170;
					if (k == 3 && fabsf (fy - 10 * sinf ((float) x * 0.15f)) < 2.5f && x > 80) v = 210;
					if (v) put (x0 + x, y0 + y, v);
				}
		}
	}
};

} // namespace cz

#endif
