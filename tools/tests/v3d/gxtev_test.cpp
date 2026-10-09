// gxtev_test -- the TEV shaders of user/Libs/v3d/gxtev.cpp on the PC: random TEV configurations (the
// stages' combiners with every bias / scale / compare mode / clamp / destination, the swap tables,
// the konst selections, the texture lookups, the rasterized colours, the alpha test, the EFB's
// formats, the destination alpha) are generated, checked against the V3D's instruction
// restrictions (tools/qpu/qpulib), run in the simulator (tools/qpu/qpusim) over random pixels and
// compared with a C++ reference: the TEV of pc/NintendoEMU/core/gxgl.cpp's GLSL, line for line.
//   gxtev_test [configurations] [seed]        (GXTEV_VER=71: built for V3D 7.1, the Pi 5 -- qpu.h's translation --,
//                                               checked and simulated as 7.1)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "v3d/gxtev.h"
#include "v3d/gxtev_ref.h"
extern "C" {
#include "qpulib.h"
}
#include "qpusim.h"
using namespace qpusim;
using gxtev::Config;
using gxtev::Dyn;
using gxtev::reference;

// the float a channel is written as (the shader's conversion), then the TLB's 8 bits
static int tlb8 (float v) { v = fromHalf (toHalf (v)); v = v != v ? 0 : v < 0 ? 0 : v > 1 ? 1 : v; return (int) lrintf (v * 255.0f); }
static void outBytes (const Config &cf, const Dyn &dy, const int in[4], int o[4])
{
	int fmt = cf.efbFmt <= 2 ? cf.efbFmt : 0;
	for (int ch = 0; ch < 4; ch++)
	{
		if (ch == 3 && (cf.dstAlpha || fmt == 2)) { o[3] = tlb8 (cf.dstAlpha ? dy.dstA : 1.0f); continue; }
		int shr = fmt == 1 ? 2 : fmt == 2 ? (ch == 1 ? 2 : 3) : 0;
		float div = fmt == 1 ? 63.0f : fmt == 2 ? (ch == 1 ? 63.0f : 31.0f) : 255.0f;
		o[ch] = tlb8 ((float) (in[ch] >> shr) * (1.0f / div));
	}
}
// the FS's float -> integer steps (x 128, doubled, - x, rounded)
static int to255 (float v) { float t = v * 128.0f; t = t + t; t = t - v; return (int) nearbyintf (t); }

// ---- random configurations -----------------------------------------------------------------------------------
static unsigned rnd32 () { return (unsigned) rand () << 16 ^ (unsigned) rand (); }
static int rr (int n) { return rand () % n; }
static void randomConfig (Config &c, int complexity)
{
	memset (&c, 0, sizeof c);
	c.nStages = 1 + rr (complexity ? 16 : 3);
	int looks = 0;
	for (int st = 0; st < c.nStages; st++)
	{
		c.cenv[st] = rnd32 () & 0xFFFFFF; c.aenv[st] = rnd32 () & 0xFFFFFF;
		unsigned tr = (unsigned) rr (8) | (unsigned) rr (8) << 3;
		if (rr (2) && looks < 8) { tr |= 64; looks++; }
		static const int chans[4] = { 0, 1, 7, 7 };
		tr |= (unsigned) chans[rr (4)] << 7;
		c.tref[st] = tr;
		c.ksel[st] = (unsigned) rr (32) | (unsigned) rr (32) << 5;
	}
	for (int k = 0; k < 16; k++) c.swap[k] = (unsigned char) rr (4);
	for (int t = 0; t < 4; t++) if (rr (3) == 0) for (int ch = 0; ch < 4; ch++) c.swap[t * 4 + ch] = (unsigned char) ch;
	c.alphaFunc[0] = (unsigned char) (rr (3) ? 7 : rr (8)); c.alphaFunc[1] = (unsigned char) (rr (3) ? 7 : rr (8));
	c.alphaLogic = (unsigned char) rr (4);
	c.efbFmt = (unsigned char) rr (3);
	c.dstAlpha = rr (4) == 0;
	c.projMask = (unsigned char) rr (256);
}

static bool check (const gxtev::Shader &sh, char *err, unsigned cap)
{
	return qpu_check ((const uint64_t *) sh.prog.words (), sh.prog.count (), QPU_FRAG, err, cap) >= 0;
}

// one configuration over random pixels (from seed): 0 ok, 1 wrong (said when verbose), 2 not built
static gxtev::Shader sh;
static int testConfig (const Config &cf, unsigned seed, bool verbose, const char *name)
{
	srand (seed);
	if (!gxtev::build (cf, sh))
	{
		if (strstr (sh.err, "registers")) return 2;
		if (verbose) printf ("FAIL %s: %s\n", name, sh.err);
		return 1;
	}
	char err[256];
	if (!check (sh, err, sizeof err)) { if (verbose) printf ("FAIL %s (%d stages): %s\n", name, cf.nStages, err); return 1; }
	// its dynamic values, textures, uniforms
	Dyn dy;
	for (int k = 0; k < 16; k++) { dy.regs[k] = rr (2048) - 1024; dy.konst[k] = rr (256); }
	if (rr (2)) for (int k = 0; k < 16; k++) dy.regs[k] = rr (256);
	dy.aref[0] = rr (256); dy.aref[1] = rr (256); dy.dstA = (float) rr (256) / 255.0f;
	Run run;
	run.version = qpu::version ();
	Texture tx[8];
	for (int L = 0; L < sh.nLook; L++)
	{
		Texture &t = tx[L]; t.w = 1 << (1 + rr (3)); t.h = 1 << (1 + rr (3)); t.wrapS = t.wrapT = 0; t.linear = false;
		for (int i = 0; i < t.w * t.h; i++) t.px.push_back (rnd32 ());
		run.textures[0x10000u * (unsigned) (L + 1)] = t;
	}
	for (int i = 0; i < sh.nUni; i++)
	{
		const gxtev::Uni &u = sh.uni[i];
		unsigned v = 0;
		switch (u.kind)
		{
		case gxtev::U_CONST: v = u.value; break;
		case gxtev::U_REG: v = (unsigned) dy.regs[u.a * 4 + u.b]; break;
		case gxtev::U_KONST: v = (unsigned) dy.konst[u.a * 4 + u.b]; break;
		case gxtev::U_ALPHAREF: v = (unsigned) dy.aref[u.a]; break;
		case gxtev::U_TEXP0: v = 0x10000u * (unsigned) (u.a + 1) | 3; break;
		case gxtev::U_TEXP1: v = 0; break;
		case gxtev::U_DSTALPHA: { union { float f; unsigned u; } k; k.f = dy.dstA; v = k.u; } break;
		}
		run.uniforms.push_back (v);
	}
	for (int i = 0; i < sh.nClaim; i++)
	{
		int reg = sh.claim[i].reg;			// (0..5 r0..r5, 6 + n rf n: the program's; on V3D 7.1 where they are)
		if (qpu::version () >= 71) reg = 6 + qpu::physical (reg < 6 ? qpu::R { 0, reg, 0, 0 } : qpu::rf (reg - 6));
		run.watch.push_back ({ sh.claim[i].ip, reg, sh.claim[i].lo, sh.claim[i].hi });
	}
	// the pixels
	std::vector<Pixel> px (48);
	std::vector<std::vector<float>> c0 (px.size ()), c1 (px.size ()), st (px.size ());
	for (size_t p = 0; p < px.size (); p++)
	{
		c0[p].resize (4); c1[p].resize (4); st[p].resize (24);
		for (int k = 0; k < 4; k++) { c0[p][k] = (float) rr (1001) / 1000.0f; c1[p][k] = (float) rr (1001) / 1000.0f; }
		for (int k = 0; k < 24; k++) st[p][k] = (k % 3 == 2) ? 0.5f + (float) rr (1000) / 500.0f : (float) rr (2000) / 1000.0f - 0.5f;
		for (int v = 0; v < sh.nVary; v++)
		{
			const gxtev::Vary &vy = sh.vary[v];
			px[p].vary.push_back (vy.kind == gxtev::V_C0 ? c0[p][vy.a] : vy.kind == gxtev::V_C1 ? c1[p][vy.a] : st[p][vy.a * 3 + vy.b]);
		}
		px[p].nTlb = 0;
	}
	if (!runFragment ((const uint64_t *) sh.prog.words (), sh.prog.count (), px, run))
	{ if (verbose) printf ("FAIL %s: the simulator: %s\n", name, run.error.c_str ()); return 1; }
	for (size_t p = 0; p < px.size (); p++)
	{
		int ras0[4], ras1[4], texel[8][4];
		for (int k = 0; k < 4; k++) { ras0[k] = to255 (c0[p][k]); ras1[k] = to255 (c1[p][k]); }
		for (int L = 0; L < sh.nLook; L++)
		{
			const Texture &t = tx[L];
			float s = st[p][L * 3], tt = st[p][L * 3 + 1];
			if (sh.look[L].proj) { float r = 1.0f / st[p][L * 3 + 2]; s = s * r; tt = tt * r; }
			int x = (int) floorf (s * (float) t.w), y = (int) floorf (tt * (float) t.h);
			x %= t.w; if (x < 0) x += t.w; y %= t.h; if (y < 0) y += t.h;
			uint32_t c = t.px[(size_t) (y * t.w + x)];
			for (int k = 0; k < 4; k++) texel[L][k] = to255 (fromHalf (toHalf ((float) ((c >> (k * 8)) & 255) / 255.0f)));
		}
		int o[4], want[4];
		bool pass = reference (cf, dy, ras0, ras1, texel, o);
		outBytes (cf, dy, o, want);
		if (pass != px[p].written)
		{
			if (verbose) printf ("FAIL %s (%d stages, %d lookups): pixel %d %s, expected %s (alpha %d, refs %d %d, funcs %d %d logic %d)\n", name, cf.nStages, sh.nLook,
				(int) p, px[p].written ? "drawn" : "discarded", pass ? "drawn" : "discarded", o[3], dy.aref[0], dy.aref[1],
				cf.alphaFunc[0], cf.alphaFunc[1], cf.alphaLogic);
			return 1;
		}
		if (!pass) continue;
		for (int k = 0; k < 4; k++)
			if ((int) ((px[p].rgba >> (k * 8)) & 255) != want[k])
			{
				if (verbose) printf ("FAIL %s (%d stages, %d lookups, fmt %d): pixel %d channel %d = %d, expected %d (TEV %d %d %d %d; ras0 %d %d %d %d)\n", name, cf.nStages, sh.nLook, cf.efbFmt,
					(int) p, k, (int) ((px[p].rgba >> (k * 8)) & 255), want[k], o[0], o[1], o[2], o[3], ras0[0], ras0[1], ras0[2], ras0[3]);
				return 1;
			}
	}
	return 0;
}

// a failing configuration made smaller (stages dropped, lookups / tests turned off) while it still fails
static void minimize (Config &cf, unsigned seed)
{
	for (bool again = true; again; )
	{
		again = false;
		for (int k = 0; k < cf.nStages && cf.nStages > 1; k++)
		{
			Config t = cf;
			for (int j = k; j + 1 < t.nStages; j++) { t.cenv[j] = t.cenv[j + 1]; t.aenv[j] = t.aenv[j + 1]; t.tref[j] = t.tref[j + 1]; t.ksel[j] = t.ksel[j + 1]; }
			t.nStages--;
			if (testConfig (t, seed, false, "") == 1) { cf = t; again = true; break; }
		}
		Config t = cf; t.alphaFunc[0] = t.alphaFunc[1] = 7; t.alphaLogic = 0;
		if (memcmp (&t, &cf, sizeof t) && testConfig (t, seed, false, "") == 1) { cf = t; again = true; }
		t = cf; t.efbFmt = 0; t.dstAlpha = false;
		if (memcmp (&t, &cf, sizeof t) && testConfig (t, seed, false, "") == 1) { cf = t; again = true; }
	}
}

int main (int argc, char **argv)
{
	int nConf = argc > 1 ? atoi (argv[1]) : 2000;
	if (getenv ("GXTEV_VER") && atoi (getenv ("GXTEV_VER")) == 71) { qpu::setVersion (71, 32); qpu_set_version (71); printf ("(V3D 7.1)\n"); }
	unsigned seed0 = argc > 2 ? (unsigned) atoi (argv[2]) : 1;
	int fails = 0, built = 0, tooBig = 0;
	long instr = 0; int maxInstr = 0, maxRegs = 0;
	for (int n = 0; n < nConf && fails < 3; n++)
	{
		srand (seed0 * 7919u + (unsigned) n);
		Config cf; randomConfig (cf, n % 3 != 0);
		unsigned seed = seed0 * 104729u + (unsigned) n;
		char name[32]; snprintf (name, sizeof name, "config %d", n);
		int r = testConfig (cf, seed, true, name);
		if (r == 2) { tooBig++; continue; }
		if (r == 0)
		{
			built++;
			instr += sh.prog.count (); if (sh.prog.count () > maxInstr) maxInstr = sh.prog.count ();
			if (sh.nRegs > maxRegs) maxRegs = sh.nRegs;
			continue;
		}
		fails++;
		minimize (cf, seed);
		printf ("  smallest failing: %d stages, alpha %d %d / %d, fmt %d, dstAlpha %d, proj %02X, swap", cf.nStages, cf.alphaFunc[0], cf.alphaFunc[1], cf.alphaLogic, cf.efbFmt, cf.dstAlpha, cf.projMask);
		for (int k = 0; k < 16; k++) printf (" %d", cf.swap[k]);
		printf ("\n");
		for (int st = 0; st < cf.nStages; st++)
			printf ("    stage %d: cenv %06X aenv %06X tref %03X ksel %03X\n", st, cf.cenv[st], cf.aenv[st], cf.tref[st], cf.ksel[st]);
		testConfig (cf, seed, true, "  (it)");
		if (getenv ("GXTEV_DUMP"))
			for (int i = 0; i < sh.prog.count (); i++) printf ("  %3d  %s\n", i, qpu_disassemble (sh.prog.words ()[i]));
	}
	// a typical material: one stage, texture x rasterized colour (the GX's MODULATE), alpha the same
	{
		Config cf; memset (&cf, 0, sizeof cf);
		cf.nStages = 1;
		cf.cenv[0] = 0xF << 12 | 8 << 8 | 10 << 4 | 0xF | 1u << 19;		// (0, texc, rasc, 0), clamped
		cf.aenv[0] = 7u << 13 | 4u << 10 | 5u << 7 | 7u << 4 | 1u << 19;	// (0, texa, rasa, 0)
		cf.tref[0] = 64 | 0 << 7;
		for (int k = 0; k < 16; k++) cf.swap[k] = (unsigned char) (k & 3);
		cf.alphaFunc[0] = cf.alphaFunc[1] = 7;
		if (getenv ("GXTEV_DUMP") && gxtev::build (cf, sh))
			for (int i = 0; i < sh.prog.count (); i++) printf ("  %3d  %s\n", i, qpu_disassemble (sh.prog.words ()[i]));
		if (gxtev::build (cf, sh)) printf ("info: MODULATE (1 stage, 1 texture): %d instructions, %d uniforms, %d varyings, %d registers\n", sh.prog.count (), sh.nUni, sh.nVary, sh.nRegs);
		cf.alphaFunc[0] = 4; cf.alphaLogic = 0;
		if (gxtev::build (cf, sh)) printf ("info: the same with an alpha test: %d instructions\n", sh.prog.count ());
	}
	printf ("%s  : %d TEV configurations right (%d pixels each), %d beyond 32 registers; %ld instructions on average, %d at most; %d registers at most\n",
		fails ? "FAIL" : "ok", built, 48, tooBig, built ? instr / built : 0, maxInstr, maxRegs);
	return fails ? 1 : 0;
}
