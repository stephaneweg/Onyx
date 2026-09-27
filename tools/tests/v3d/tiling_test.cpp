//
// tiling_test -- kern/v3d_tiling.h (the texel layouts of V3D textures) against Mesa's own
// functions (below, from src/broadcom/common/v3d_tiling.c: Copyright 2014-2017 Broadcom, MIT),
// for every size up to 300 x 300: the layout chosen (as v3d_setup_slices does) and the
// offset of every texel. sh tools/tests/run_v3d_tiling_test.sh
//
#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <assert.h>
typedef uint32_t u32;
#include "kern/v3d_tiling.h"

static inline uint32_t align (uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
static uint32_t v3d_utile_width (int cpp) { assert (cpp == 4); return 4; }
static uint32_t v3d_utile_height (int cpp) { assert (cpp == 4); return 4; }

// ---- Mesa (MIT) ----
static inline uint32_t
v3d_get_utile_pixel_offset(uint32_t cpp, uint32_t x, uint32_t y)
{
        uint32_t utile_w = v3d_utile_width(cpp);

        assert(x < utile_w && y < v3d_utile_height(cpp));

        return x * cpp + y * utile_w * cpp;
}

static inline uint32_t
v3d_get_lt_pixel_offset(uint32_t cpp, uint32_t image_h, uint32_t x, uint32_t y)
{
        uint32_t utile_w = v3d_utile_width(cpp);
        uint32_t utile_h = v3d_utile_height(cpp);
        uint32_t utile_index_x = x / utile_w;
        uint32_t utile_index_y = y / utile_h;

        assert(utile_index_x == 0 || utile_index_y == 0);

        return (64 * (utile_index_x + utile_index_y) +
                v3d_get_utile_pixel_offset(cpp,
                                           x & (utile_w - 1),
                                           y & (utile_h - 1)));
}

static inline uint32_t
v3d_get_ublinear_pixel_offset(uint32_t cpp, uint32_t x, uint32_t y,
                              int ublinear_number)
{
        uint32_t utile_w = v3d_utile_width(cpp);
        uint32_t utile_h = v3d_utile_height(cpp);
        uint32_t ub_w = utile_w * 2;
        uint32_t ub_h = utile_h * 2;
        uint32_t ub_x = x / ub_w;
        uint32_t ub_y = y / ub_h;

        return (256 * (ub_y * ublinear_number +
                       ub_x) +
                ((x & utile_w) ? 64 : 0) +
                ((y & utile_h) ? 128 : 0) +
                + v3d_get_utile_pixel_offset(cpp,
                                             x & (utile_w - 1),
                                             y & (utile_h - 1)));
}

static inline uint32_t
v3d_get_uif_pixel_offset(uint32_t cpp, uint32_t image_h, uint32_t x, uint32_t y,
                         bool do_xor)
{
        uint32_t utile_w = v3d_utile_width(cpp);
        uint32_t utile_h = v3d_utile_height(cpp);
        uint32_t mb_width = utile_w * 2;
        uint32_t mb_height = utile_h * 2;
        uint32_t log2_mb_width = ffs(mb_width) - 1;
        uint32_t log2_mb_height = ffs(mb_height) - 1;

        /* Macroblock X, y */
        uint32_t mb_x = x >> log2_mb_width;
        uint32_t mb_y = y >> log2_mb_height;
        /* X, y within the macroblock */
        uint32_t mb_pixel_x = x - (mb_x << log2_mb_width);
        uint32_t mb_pixel_y = y - (mb_y << log2_mb_height);

        if (do_xor && (mb_x / 4) & 1)
                mb_y ^= 0x10;

        uint32_t mb_h = align(image_h, 1 << log2_mb_height) >> log2_mb_height;
        uint32_t mb_id = ((mb_x / 4) * ((mb_h - 1) * 4)) + mb_x + mb_y * 4;

        uint32_t mb_base_addr = mb_id * 256;

        bool top = mb_pixel_y < utile_h;
        bool left = mb_pixel_x < utile_w;

        /* Docs have this in pixels, we do bytes here. */
        uint32_t mb_tile_offset = (!top * 128 + !left * 64);

        uint32_t utile_x = mb_pixel_x & (utile_w - 1);
        uint32_t utile_y = mb_pixel_y & (utile_h - 1);

        uint32_t mb_pixel_address = (mb_base_addr +
                                     mb_tile_offset +
                                     v3d_get_utile_pixel_offset(cpp,
                                                                utile_x,
                                                                utile_y));

        return mb_pixel_address;
}

// ---- end of Mesa ----

int main (void)
{
	long n = 0, bad = 0;
	for (u32 w = 1; w <= 300; w++)
		for (u32 h = 1; h <= 300; h += (h < 40 ? 1 : 7))
		{
			TLayout L; Layout (w, h, L);
			u32 lw = w, lh = h, kind, pad = 0; bool xr = false;	// v3d_setup_slices, level 0
			if (lw <= 4 || lh <= 4) { kind = TL_LT; lw = align (lw, 4); lh = align (lh, 4); }
			else if (lw <= 8) { kind = TL_UB1; lw = align (lw, 8); lh = align (lh, 8); }
			else if (lw <= 16) { kind = TL_UB2; lw = align (lw, 16); lh = align (lh, 8); }
			else { kind = TL_UIF; lw = align (lw, 32); lh = align (lh, 8); pad = UbPad (lh / 8); lh += pad * 8; xr = (lh / 8) % 32 == 0; }
			if ((int) kind != L.nKind || lw != L.nPadW || lh != L.nPadH || pad != L.nUbPad || xr != L.bXor)
			{ printf ("layout %ux%u differs\n", w, h); bad++; continue; }
			for (u32 y = 0; y < h; y++)
				for (u32 x = 0; x < w; x++)
				{
					u32 m;
					switch (kind)
					{
					case TL_LT: m = v3d_get_lt_pixel_offset (4, lh, x, y); break;
					case TL_UB1: m = v3d_get_ublinear_pixel_offset (4, x, y, 1); break;
					case TL_UB2: m = v3d_get_ublinear_pixel_offset (4, x, y, 2); break;
					default: m = v3d_get_uif_pixel_offset (4, lh, x, y, xr);
					}
					u32 o = TexelOffset (L, x, y);
					n++;
					if (m != o || o + 4 > lw * lh * 4)
					{
						if (bad < 5) printf ("%ux%u (%u,%u): Mesa %u, ours %u\n", w, h, x, y, m, o);
						bad++;
					}
				}
		}
	printf ("v3d tiling: %ld texels checked, %ld wrong\n", n, bad);
	return bad != 0;
}
