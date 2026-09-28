// qpusim_test -- the QPU simulator (tools/qpu/qpusim) against the kernel's own fragment shaders
// (kernel/sys/v3d_shaders.inc), whose results are known: FS_COLOR = min (1, colour + colour2),
// FS_COLOR_AT = the same, dropped below the alpha threshold, FS_TEX = min (1, texel x colour + colour2).
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "qpusim.h"
typedef unsigned long long u64;
#include "../../../kernel/sys/v3d_shaders.inc"
using namespace qpusim;

static float rnd () { return (float) (rand () % 1001) / 1000.0f; }
static int q (float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return (int) lrintf (v * 255.0f); }
static int close (uint32_t got, const int want[4], int tol)
{
	for (int k = 0; k < 4; k++) if (abs ((int) ((got >> (k * 8)) & 255) - want[k]) > tol) return 0;
	return 1;
}
static float h (float f) { return fromHalf (toHalf (f)); }	// (the TLB gets f16)

int main ()
{
	srand (1);
	int fails = 0;
	// FS_COLOR / FS_COLOR_AT
	for (int at = 0; at < 2; at++)
	{
		std::vector<Pixel> px (100);
		float thr = 0.5f;
		for (auto &p : px) { p.vary.resize (10); for (int k = 0; k < 10; k++) p.vary[k] = k < 2 ? rnd () : rnd () * 0.6f; }
		Run run; if (at) run.uniforms.push_back (0x3f000000);	// 0.5
		const u64 *w = at ? FS_COLOR_AT : FS_COLOR; int n = at ? (int) (sizeof FS_COLOR_AT / 8) : (int) (sizeof FS_COLOR / 8);
		if (!runFragment ((const uint64_t *) w, n, px, run)) { printf ("FAIL %s: %s\n", at ? "FS_COLOR_AT" : "FS_COLOR", run.error.c_str ()); return 1; }
		int bad = 0;
		for (auto &p : px)
		{
			float a = fminf (1, p.vary[5] + p.vary[9]);
			bool drop = at && a < thr;
			if (drop != !p.written) { bad++; continue; }
			if (drop) continue;
			int want[4]; for (int k = 0; k < 4; k++) want[k] = q (h (fminf (1, p.vary[2 + k] + p.vary[6 + k])));
			if (!close (p.rgba, want, 1)) bad++;
		}
		printf ("%s  : %s, %d pixels, %d wrong\n", bad ? "FAIL" : "ok", at ? "FS_COLOR_AT (alpha test)" : "FS_COLOR", (int) px.size (), bad);
		fails += bad != 0;
	}
	// FS_TEX: a 4 x 4 texture, nearest, repeat
	{
		Texture t; t.w = 4; t.h = 4; t.wrapS = t.wrapT = 0; t.linear = false;
		for (int i = 0; i < 16; i++) t.px.push_back ((uint32_t) (rand () & 0xFFFFFFFF) | 0xFF000000u);
		Run run; run.uniforms.push_back (0x1000 | 3); run.uniforms.push_back (0x2000); run.textures[0x1000] = t;
		std::vector<Pixel> px (100);
		for (auto &p : px) { p.vary.resize (10); for (int k = 0; k < 10; k++) p.vary[k] = k < 2 ? rnd () * 2.0f : rnd () * 0.5f; }
		if (!runFragment ((const uint64_t *) FS_TEX, (int) (sizeof FS_TEX / 8), px, run)) { printf ("FAIL FS_TEX: %s\n", run.error.c_str ()); return 1; }
		int bad = 0;
		for (auto &p : px)
		{
			int x = ((int) floorf (p.vary[0] * 4)) & 3, y = ((int) floorf (p.vary[1] * 4)) & 3;
			uint32_t c = t.px[(size_t) (y * 4 + x)];
			int want[4];
			for (int k = 0; k < 4; k++) { float tex = h ((float) ((c >> (k * 8)) & 255) / 255.0f); want[k] = q (h (fminf (1, tex * p.vary[2 + k] + p.vary[6 + k]))); }
			if (!p.written || !close (p.rgba, want, 1)) bad++;
		}
		printf ("%s  : FS_TEX (nearest, repeat), %d pixels, %d wrong\n", bad ? "FAIL" : "ok", (int) px.size (), bad);
		fails += bad != 0;
	}
	return fails ? 1 : 0;
}
