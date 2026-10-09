// kshaders_test -- the kernel's QPU shaders on the PC: kernel/sys/v3d_shaders.inc (V3D 4.2, the Pi 4) and
// v3d_shaders71.inc (V3D 7.1, the Pi 5) are the same programs for two instruction sets. Each passes its
// QPU's instruction restrictions (tools/qpu/qpulib), then both run in the simulator (tools/qpu/qpusim)
// on the same random vertices / pixels: their outputs must be the same, bit for bit, and match a
// reference computed here (the vertex shaders: clip = M * v, the screen coordinates; the fragment
// shaders: colour + colour2, texel * colour + colour2, the alpha test).
//
// MIT licence (docs/LICENSING.md).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
extern "C" {
#include "qpulib.h"
}
#include "qpusim.h"
using namespace qpusim;

typedef uint64_t u64;
namespace k42 {
#include "../../../kernel/sys/v3d_shaders.inc"
}
namespace k71 {
#include "../../../kernel/sys/v3d_shaders71.inc"
}

static int fails = 0;
static float rnd (float lo, float hi) { return lo + (hi - lo) * (float) (rand () % 10001) / 10000.0f; }
static uint32_t U (float f) { uint32_t u; memcpy (&u, &f, 4); return u; }
static float F (uint32_t u) { float f; memcpy (&f, &u, 4); return f; }

struct Shader { const char *name; int kind; const u64 *w42; int n42; const u64 *w71; int n71; };
#define SH(N, K) { #N, K, k42::N, (int) (sizeof k42::N / 8), k71::N, (int) (sizeof k71::N / 8) }
static const Shader shaders[] = {
	SH (VS_CLIP, QPU_VERTEX), SH (CS_CLIP, QPU_COORD), SH (FS_COLOR, QPU_FRAG), SH (FS_COLOR_AT, QPU_FRAG),
	SH (FS_TEX, QPU_FRAG), SH (FS_TEX_AT, QPU_FRAG),
};

static void restrictions ()
{
	char err[512];
	for (const Shader &s : shaders)
		for (int v = 42; v <= 71; v += 29)
		{
			qpu_set_version (v);
			int r = qpu_check (v == 42 ? s.w42 : s.w71, v == 42 ? s.n42 : s.n71, s.kind, err, sizeof err);
			printf ("%s  : %s, V3D %d.%d: %s\n", r ? "FAIL" : "ok", s.name, v / 10, v % 10, r ? err : "the restrictions hold");
			fails += r != 0;
		}
	qpu_set_version (42);
}

// ---- the vertex shaders: the same VPM outputs, and the reference -------------------------------------------
static void vertex (const Shader &s, bool render)
{
	const int nIn = render ? 14 : 4;
	std::vector<Pixel> a (48);
	Run r42, r71; r71.version = 71;
	float M[16]; for (float &m : M) m = rnd (-2, 2);
	float sc[4] = { rnd (100, 300), rnd (-300, -100), rnd (0.2f, 0.5f), rnd (0.3f, 0.6f) };
	for (float m : M) { r42.uniforms.push_back (U (m)); }
	for (int k = 0; k < (render ? 4 : 2); k++) r42.uniforms.push_back (U (sc[k]));
	r71.uniforms = r42.uniforms;
	for (Pixel &p : a)
	{
		for (int k = 0; k < nIn; k++) p.vpmIn.push_back (U (k == 3 ? rnd (0.5f, 3) : rnd (-1, 1)));
		p.vpmOut.assign (render ? 14 : 6, 0);
	}
	std::vector<Pixel> b = a;
	if (!runVertex (s.w42, s.n42, a, r42)) { printf ("FAIL %s 4.2: %s\n", s.name, r42.error.c_str ()); fails++; return; }
	if (!runVertex (s.w71, s.n71, b, r71)) { printf ("FAIL %s 7.1: %s\n", s.name, r71.error.c_str ()); fails++; return; }
	int diff = 0, bad = 0;
	for (size_t i = 0; i < a.size (); i++)
	{
		if (a[i].vpmOut != b[i].vpmOut) diff++;
		float in[4]; for (int k = 0; k < 4; k++) in[k] = F (a[i].vpmIn[k]);
		float c[4]; for (int r = 0; r < 4; r++) { c[r] = 0; for (int k = 0; k < 4; k++) c[r] += M[r * 4 + k] * in[k]; }
		float iw = 1.0f / c[3];
		std::vector<uint32_t> want;
		if (render)
		{
			want.push_back ((uint32_t) (int32_t) nearbyintf (c[0] * iw * sc[0]));
			want.push_back ((uint32_t) (int32_t) nearbyintf (c[1] * iw * sc[1]));
			want.push_back (U (c[2] * iw * sc[2] + sc[3]));
			want.push_back (U (iw));
			for (int k = 4; k < 14; k++) want.push_back (a[i].vpmIn[k]);
		}
		else
		{
			for (int k = 0; k < 4; k++) want.push_back (U (c[k]));
			want.push_back ((uint32_t) (int32_t) nearbyintf (c[0] * iw * sc[0]));
			want.push_back ((uint32_t) (int32_t) nearbyintf (c[1] * iw * sc[1]));
		}
		for (size_t k = 0; k < want.size (); k++)
		{
			bool isInt = render ? k < 2 : k >= 4;
			bool ok = isInt ? abs ((int32_t) b[i].vpmOut[k] - (int32_t) want[k]) <= 1
					: fabsf (F (b[i].vpmOut[k]) - F (want[k])) <= 1e-4f * (1 + fabsf (F (want[k])));
			if (!ok) { bad++; break; }
		}
	}
	printf ("%s  : %s: 4.2 and 7.1 %s, %d of %zu vertices off the reference\n", diff || bad ? "FAIL" : "ok", s.name,
		diff ? "DIFFER" : "the same", bad, a.size ());
	fails += diff != 0 || bad != 0;
}

// ---- the fragment shaders ------------------------------------------------------------------------------------
static int q8 (float v) { v = v != v ? 0 : v < 0 ? 0 : v > 1 ? 1 : v; return (int) lrintf (fromHalf (toHalf (v)) * 255.0f); }

static void fragment (const Shader &s, bool tex, bool at)
{
	std::vector<Pixel> a (64);
	Run r42, r71; r71.version = 71;
	const uint32_t P0 = 0x10000 | 3, P1 = 0;
	Texture t; t.w = 8; t.h = 8; t.wrapS = t.wrapT = 0; t.linear = false;
	for (int k = 0; k < 64; k++) t.px.push_back ((uint32_t) rand () * 2654435761u);
	float thr = 0.4f;
	if (tex) { r42.uniforms.push_back (P0); r42.uniforms.push_back (P1); r42.textures[P0 & ~15u] = t; }
	if (at) r42.uniforms.push_back (U (thr));
	r71.uniforms = r42.uniforms; r71.textures = r42.textures;
	for (Pixel &p : a)
	{
		p.w = rnd (0.5f, 2.0f);
		for (int k = 0; k < 10; k++) { p.vary.push_back (rnd (0, 0.5f)); p.varyC.push_back (rnd (-0.1f, 0.2f)); }
	}
	std::vector<Pixel> b = a;
	if (!runFragment (s.w42, s.n42, a, r42)) { printf ("FAIL %s 4.2: %s\n", s.name, r42.error.c_str ()); fails++; return; }
	if (!runFragment (s.w71, s.n71, b, r71)) { printf ("FAIL %s 7.1: %s\n", s.name, r71.error.c_str ()); fails++; return; }
	int diff = 0, bad = 0, dropped = 0;
	for (size_t i = 0; i < a.size (); i++)
	{
		if (a[i].written != b[i].written || (a[i].written && a[i].rgba != b[i].rgba)) diff++;
		float v[10]; for (int k = 0; k < 10; k++) v[k] = a[i].vary[k] * a[i].w + a[i].varyC[k];
		float c[4];
		if (tex)
		{
			float sx = v[0], ty = v[1];
			int x = (int) floorf (sx * 8), y = (int) floorf (ty * 8); x &= 7; y &= 7;
			uint32_t px = t.px[(size_t) (y * 8 + x)];
			for (int k = 0; k < 4; k++)
			{
				float tx = fromHalf (toHalf ((float) ((px >> (k * 8)) & 255) / 255.0f));
				c[k] = tx * v[2 + k] + v[6 + k];
			}
		}
		else for (int k = 0; k < 4; k++) c[k] = v[2 + k] + v[6 + k];
		bool keep = !at || fminf (c[3], 1.0f) >= thr;
		if (!keep) { dropped++; if (b[i].written) bad++; continue; }
		if (!b[i].written) { bad++; continue; }
		for (int k = 0; k < 4; k++) if (abs ((int) ((b[i].rgba >> (k * 8)) & 255) - q8 (fminf (c[k], 1.0f))) > 1) { bad++; break; }
	}
	printf ("%s  : %s: 4.2 and 7.1 %s, %d of %zu pixels off the reference (%d dropped by the alpha test)\n",
		diff || bad ? "FAIL" : "ok", s.name, diff ? "DIFFER" : "the same", bad, a.size (), dropped);
	fails += diff != 0 || bad != 0;
}

int main ()
{
	srand (7);
	restrictions ();
	for (int rep = 0; rep < 3; rep++)
	{
		vertex (shaders[0], true); vertex (shaders[1], false);
		fragment (shaders[2], false, false); fragment (shaders[3], false, true);
		fragment (shaders[4], true, false); fragment (shaders[5], true, true);
	}
	printf ("kshaders: %d failure(s)\n", fails);
	return fails ? 1 : 0;
}
