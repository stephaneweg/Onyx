// shaders_test -- the ready-made programs of user/Libs/v3d/shaders.cpp (kapi v61) on the PC: each one
// encodes, passes the V3D 4.2 instruction restrictions (tools/qpu/qpulib), and the fragment
// shaders give the expected colours in the simulator (tools/qpu/qpusim), the vertex shaders the
// screen coordinates. QPU_VER=71: the same, built for V3D 7.1 (qpu.h's translation) and run as 7.1.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	Run run; run.version = qpu::version ();
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

// passVS / passCS in the simulator: x y z w + the varyings in, Xs Ys (the version's sub-pixels) Zs 1/Wc (+ the
// varyings) or Xc Yc Zc Wc Xs Ys out, against the reference
static void vertexRun (const char *name, const qpu::Prog &p, int nIn, bool coord)
{
	unsigned vu[4]; qpu::viewUniforms (640, 480, vu);
	float sub = qpu::version () >= 71 ? 64.0f : 256.0f;
	std::vector<Pixel> vx (32);
	Run run; run.version = qpu::version ();
	for (int k = 0; k < (coord ? 2 : 4); k++) run.uniforms.push_back (vu[k]);
	for (auto &v : vx)
		for (int k = 0; k < nIn; k++) { union { float f; uint32_t u; } c; c.f = k == 3 ? 0.5f + rnd () : rnd () * 2 - 1; v.vpmIn.push_back (c.u); }
	if (!runVertex ((const uint64_t *) p.words (), p.count (), vx, run)) { printf ("FAIL %s: %s\n", name, run.error.c_str ()); fails++; return; }
	int bad = 0;
	for (auto &v : vx)
	{
		float in[4]; for (int k = 0; k < 4; k++) memcpy (&in[k], &v.vpmIn[k], 4);
		float iw = 1.0f / in[3];
		int xs = (int) nearbyintf (in[0] * iw * 320 * sub), ys = (int) nearbyintf (in[1] * iw * -240 * sub);
		int o = coord ? 4 : 0;
		if ((int) v.vpmOut[o] != xs && abs ((int) v.vpmOut[o] - xs) > 1) bad++;
		else if ((int) v.vpmOut[o + 1] != ys && abs ((int) v.vpmOut[o + 1] - ys) > 1) bad++;
		else if (!coord) for (int k = 4; k < nIn; k++) if (v.vpmOut[k] != v.vpmIn[k]) { bad++; break; }
	}
	printf ("%s  : %s simulated, %d wrong\n", bad ? "FAIL" : "ok", name, bad);
	fails += bad != 0;
}

int main ()
{
	srand (1);
	if (getenv ("QPU_VER") && atoi (getenv ("QPU_VER")) == 71) { qpu::setVersion (71, 32); qpu_set_version (71); printf ("(V3D 7.1)\n"); }
	for (int n = 4; n <= 64; n += 4)
	{
		qpu::Prog p; qpu::passVS (p, n);
		char name[32]; snprintf (name, sizeof name, "passVS (%d)", n);
		if (check (name, p, QPU_VERTEX) && (n == 8 || n == 13)) vertexRun (name, p, n, false);
	}
	{ qpu::Prog p; qpu::passCS (p); if (check ("passCS", p, QPU_COORD)) vertexRun ("passCS", p, 4, true); }
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
			Run run; run.version = qpu::version (); run.uniforms.push_back (0x1000 | 3); run.uniforms.push_back (0x2000); run.textures[0x1000] = t;
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
