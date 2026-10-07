// shaders_test -- the ready-made programs of user/Libs/v3d/shaders.cpp (kapi v61) on the PC: each one
// encodes, passes the V3D 4.2 instruction restrictions (tools/qpu/qpulib), and the fragment
// shaders give the expected colours in the simulator (tools/qpu/qpusim).
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "v3d/shaders.h"
extern "C" {
#include "qpulib.h"
}
#include "qpusim.h"
using namespace qpusim;

static int fails = 0;
static float rnd () { return (float) (rand () % 1001) / 1000.0f; }
static int q (float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return (int) lrintf (fromHalf (toHalf (v)) * 255.0f); }
static int close (uint32_t got, const int want[4], int tol)
{
	for (int k = 0; k < 4; k++) if (abs ((int) ((got >> (k * 8)) & 255) - want[k]) > tol) return 0;
	return 1;
}

static bool check (const char *name, const qpu::Prog &p, int kind)
{
	char err[256];
	if (!p.ok ()) { printf ("FAIL %s: instruction %d not encodable\n", name, p.badAt); fails++; return false; }
	if (qpu_check ((const uint64_t *) p.words (), p.count (), kind, err, sizeof err) < 0)
	{ printf ("FAIL %s: %s\n", name, err); fails++; return false; }
	printf ("ok    : %s (%d instructions)\n", name, p.count ());
	return true;
}

static void colourFS (const char *name, const qpu::Prog &p, int nVary, bool bUni)
{
	std::vector<Pixel> px (64);
	float U[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
	for (auto &x : px) { x.vary.resize (nVary); for (auto &v : x.vary) v = rnd (); }
	Run run;
	if (bUni) for (int k = 0; k < 4; k++) { union { float f; uint32_t u; } c; c.f = U[k]; run.uniforms.push_back (c.u); }
	if (!runFragment ((const uint64_t *) p.words (), p.count (), px, run)) { printf ("FAIL %s: %s\n", name, run.error.c_str ()); fails++; return; }
	int bad = 0;
	for (auto &x : px)
	{
		int want[4]; for (int k = 0; k < 4; k++) want[k] = q (bUni ? U[k] : x.vary[k]);
		if (!x.written || !close (x.rgba, want, 1)) bad++;
	}
	printf ("%s  : %s simulated, %d wrong\n", bad ? "FAIL" : "ok", name, bad);
	fails += bad != 0;
}

int main ()
{
	srand (1);
	for (int n = 4; n <= 64; n += 4)
	{
		qpu::Prog p; qpu::passVS (p, n);
		char name[32]; snprintf (name, sizeof name, "passVS (%d)", n);
		check (name, p, QPU_VERTEX);
	}
	{ qpu::Prog p; qpu::passCS (p); check ("passCS", p, QPU_COORD); }
	for (int f = 0; f < 2; f++)
	{
		qpu::Prog p; qpu::flatFS (p, f); const char *nm = f ? "flatFS (final)" : "flatFS";
		if (check (nm, p, QPU_FRAG)) colourFS (nm, p, 0, true);
		for (int n = 4; n <= 8; n += 4)
		{
			qpu::Prog v; qpu::varyFS (v, n, f); char name[40]; snprintf (name, sizeof name, "varyFS (%d%s)", n, f ? ", final" : "");
			if (check (name, v, QPU_FRAG)) colourFS (name, v, n, false);
		}
	}
	{
		qpu::Prog p; qpu::texFS (p);
		if (check ("texFS", p, QPU_FRAG))
		{
			Texture t; t.w = 4; t.h = 4; t.wrapS = t.wrapT = 0; t.linear = false;
			for (int i = 0; i < 16; i++) t.px.push_back ((uint32_t) (rand () & 0xFFFFFF) | ((uint32_t) (rand () & 255) << 24));
			Run run; run.uniforms.push_back (0x1000 | 3); run.uniforms.push_back (0x2000); run.textures[0x1000] = t;
			std::vector<Pixel> px (64);
			for (auto &x : px) { x.vary.resize (2); x.vary[0] = rnd (); x.vary[1] = rnd (); }
			if (!runFragment ((const uint64_t *) p.words (), p.count (), px, run)) { printf ("FAIL texFS: %s\n", run.error.c_str ()); fails++; }
			else
			{
				int bad = 0;
				for (auto &x : px)
				{
					int tx = ((int) floorf (x.vary[0] * 4)) & 3, ty = ((int) floorf (x.vary[1] * 4)) & 3;
					uint32_t c = t.px[(size_t) (ty * 4 + tx)];
					int want[4]; for (int k = 0; k < 4; k++) want[k] = q ((float) ((c >> (k * 8)) & 255) / 255.0f);
					if (!x.written || !close (x.rgba, want, 1)) bad++;
				}
				printf ("%s  : texFS simulated, %d wrong\n", bad ? "FAIL" : "ok", bad);
				fails += bad != 0;
			}
		}
	}
	return fails ? 1 : 0;
}
