//
// picatev_test.cpp -- the PICA200 combiner's generated fragment shaders (user/Libs/v3d/picatev.cpp) against the
// reference (picatev_ref.h), on the PC: random configurations -- the sources, the operands, the modes, the
// scales, the buffer, the lookups, the lighting's colours, the alpha test -- built, checked against the QPU's
// instruction restrictions (qpulib), run by the simulator (qpusim) over random pixels.
//
//   picatev_test [configurations] [seed]      (PICATEV_VER=71: built for V3D 7.1; PICATEV_DUMP=1: the first program)
//
// MIT License -- Copyright (c) 2026 Stephane Wegener (see docs/LICENSING.md)
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "v3d/picatev.h"
#include "v3d/picatev_ref.h"
extern "C" {
#include "qpulib.h"
}
#include "qpusim.h"
using namespace qpusim;
using picatev::Config;
using picatev::Inputs;

static int tlb8 (float v) { v = fromHalf (toHalf (v)); v = v != v ? 0 : v < 0 ? 0 : v > 1 ? 1 : v; return (int) lrintf (v * 255.0f); }
// a colour varying as the shader takes it: x 255, rounded, brought into 0..255
static int to255 (float v) { int n = (int) nearbyintf (v * 255.0f); return n < 0 ? 0 : n > 255 ? 255 : n; }

static unsigned rs = 1;
static unsigned rnd32 () { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static int rr (int n) { return (int) (rnd32 () % (unsigned) n); }

static void randomConfig (Config &c)
{
	static const unsigned char SRC[] = { 0, 1, 2, 3, 4, 5, 6, 13, 14, 15, 15, 15, 3, 0, 14 };
	static const unsigned char OPC[] = { 0, 0, 0, 1, 2, 3, 4, 5, 8, 9, 12, 13 };
	static const unsigned char MODE[] = { 0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	memset (&c, 0, sizeof c);
	const int active = 1 + rr (6);
	for (int st = 0; st < 6; st++)
	{
		Config::Stage &g = c.stage[st];
		g.pass = st >= active || rr (8) == 0;
		for (int k = 0; k < 3; k++)
		{
			g.src[k] = SRC[rr (sizeof SRC)]; g.srcA[k] = SRC[rr (sizeof SRC)];
			g.op[k] = OPC[rr (sizeof OPC)]; g.opA[k] = (unsigned char) rr (8);
		}
		g.mode = MODE[rr (sizeof MODE)]; g.modeA = MODE[rr (sizeof MODE)];
		if (g.modeA == 6 || g.modeA == 7) g.modeA = 1;			// (the dot product is the colour's)
		g.scale = (unsigned char) (rr (4) == 0 ? 1 << rr (3) : 1); g.scaleA = (unsigned char) (rr (4) == 0 ? 1 << rr (3) : 1);
	}
	c.updateRgb = (unsigned char) rr (16); c.updateA = (unsigned char) rr (16);
	c.alphaTest = rr (2) != 0; c.alphaFunc = (unsigned char) rr (8);
	c.texOn = (unsigned char) rr (8);
	c.lit = rr (2) != 0;
}

static picatev::Shader sh;
// one configuration over random pixels: 0 ok, 1 wrong (said when verbose), 2 not built (too many registers)
static int testConfig (const Config &cf, unsigned seed, bool verbose, const char *name)
{
	rs = seed | 1;
	if (!picatev::build (cf, sh))
	{
		if (strstr (sh.err, "registers")) return 2;
		if (verbose) printf ("FAIL %s: %s\n", name, sh.err);
		return 1;
	}
	char err[256];
	if (qpu_check ((const uint64_t *) sh.prog.words (), sh.prog.count (), QPU_FRAG, err, sizeof err) < 0) { if (verbose) printf ("FAIL %s: %s\n", name, err); return 1; }
	Inputs in;
	for (int st = 0; st < 6; st++) for (int k = 0; k < 4; k++) in.konst[st][k] = rr (256);
	for (int k = 0; k < 4; k++) in.buffer[k] = rr (256);
	in.alphaRef = rr (256);
	Run run;
	run.version = qpu::version ();
	Texture tx[3];
	for (int L = 0; L < sh.nLook; L++)
	{
		Texture &t = tx[L]; t.w = 1 << (1 + rr (3)); t.h = 1 << (1 + rr (3)); t.wrapS = t.wrapT = 0; t.linear = false;
		for (int i = 0; i < t.w * t.h; i++) t.px.push_back (rnd32 ());
		run.textures[0x10000u * (unsigned) (L + 1)] = t;
	}
	for (int i = 0; i < sh.nUni; i++)
	{
		const picatev::Uni &u = sh.uni[i];
		unsigned v = 0;
		switch (u.kind)
		{
		case picatev::U_CONST: v = u.value; break;
		case picatev::U_KONST: v = (unsigned) in.konst[u.a][u.b]; break;
		case picatev::U_BUFFER: v = (unsigned) in.buffer[u.b]; break;
		case picatev::U_ALPHAREF: v = (unsigned) in.alphaRef; break;
		case picatev::U_TEXP0: v = 0x10000u * (unsigned) (u.a + 1) | 3; break;
		default: v = 0; break;
		}
		run.uniforms.push_back (v);
	}
	for (int i = 0; i < sh.nClaim; i++)
	{
		int reg = sh.claim[i].reg;
		if (qpu::version () >= 71) reg = 6 + qpu::physical (reg < 6 ? qpu::R { 0, reg, 0, 0 } : qpu::rf (reg - 6));
		run.watch.push_back ({ sh.claim[i].ip, reg, sh.claim[i].lo, sh.claim[i].hi });
	}
	std::vector<Pixel> px (48);
	std::vector<std::vector<float>> col (px.size ()), st (px.size ());
	for (size_t p = 0; p < px.size (); p++)
	{
		col[p].resize (12); st[p].resize (6);
		for (int k = 0; k < 12; k++) col[p][k] = rr (8) == 0 ? (float) rr (1401) / 1000.0f - 0.2f : (float) rr (1001) / 1000.0f;
		for (int k = 0; k < 6; k++) st[p][k] = (float) rr (2000) / 1000.0f - 0.5f;
		for (int v = 0; v < sh.nVary; v++)
		{
			const picatev::Vary &vy = sh.vary[v];
			px[p].vary.push_back (vy.kind == picatev::V_COORD ? st[p][vy.a * 2 + vy.b] : col[p][(vy.kind == picatev::V_PRIMARY ? 0 : vy.kind == picatev::V_LITP ? 4 : 8) + vy.a]);
		}
		px[p].nTlb = 0;
	}
	if (!runFragment ((const uint64_t *) sh.prog.words (), sh.prog.count (), px, run))
	{ if (verbose) printf ("FAIL %s: the simulator: %s\n", name, run.error.c_str ()); return 1; }
	for (size_t p = 0; p < px.size (); p++)
	{
		for (int k = 0; k < 4; k++) { in.primary[k] = to255 (col[p][k]); in.litP[k] = to255 (col[p][4 + k]); in.litS[k] = to255 (col[p][8 + k]); }
		for (int u = 0; u < 3; u++) for (int k = 0; k < 4; k++) in.tex[u][k] = 0;
		for (int L = 0; L < sh.nLook; L++)
		{
			const Texture &t = tx[L];
			int x = (int) floorf (st[p][L * 2] * (float) t.w), y = (int) floorf (st[p][L * 2 + 1] * (float) t.h);
			x %= t.w; if (x < 0) x += t.w; y %= t.h; if (y < 0) y += t.h;
			const uint32_t c = t.px[(size_t) (y * t.w + x)];
			for (int k = 0; k < 4; k++) in.tex[sh.look[L].unit][k] = (int) nearbyintf (fromHalf (toHalf ((float) ((c >> (k * 8)) & 255) / 255.0f)) * 255.0f);
		}
		int o[4];
		const bool pass = picatev::reference (cf, in, o);
		if (pass != px[p].written)
		{
			if (verbose) printf ("FAIL %s: pixel %d %s, expected %s (alpha %d, reference %d, function %d)\n", name, (int) p, px[p].written ? "drawn" : "discarded", pass ? "drawn" : "discarded", o[3], in.alphaRef, cf.alphaFunc);
			return 1;
		}
		if (!pass) continue;
		for (int k = 0; k < 4; k++)
		{
			const int got = (int) ((px[p].rgba >> (k * 8)) & 255), want = tlb8 ((float) o[k] * (1.0f / 255.0f));
			if (got != want)
			{
				if (verbose) printf ("FAIL %s (%d lookups): pixel %d channel %d = %d, expected %d (combiner %d %d %d %d; primary %d %d %d %d)\n", name, sh.nLook, (int) p, k, got, want, o[0], o[1], o[2], o[3], in.primary[0], in.primary[1], in.primary[2], in.primary[3]);
				return 1;
			}
		}
	}
	return 0;
}

static void say (const Config &c)
{
	for (int st = 0; st < 6; st++)
	{
		const Config::Stage &g = c.stage[st];
		if (g.pass) continue;
		printf ("  stage %d: colour src %u %u %u op %u %u %u mode %u x%u | alpha src %u %u %u op %u %u %u mode %u x%u\n", st, g.src[0], g.src[1], g.src[2], g.op[0], g.op[1], g.op[2], g.mode, g.scale,
			g.srcA[0], g.srcA[1], g.srcA[2], g.opA[0], g.opA[1], g.opA[2], g.modeA, g.scaleA);
	}
	printf ("  buffer updates %x %x, alpha test %d function %u, textures %x, lit %d\n", c.updateRgb, c.updateA, c.alphaTest, c.alphaFunc, c.texOn, c.lit);
}

// a configuration from the registers' words as a trace prints them (source, operand, combine+scale a stage)
static void fromWords (Config &c, const unsigned w[6][3], unsigned texOn, bool lit, bool alphaTest, unsigned alphaFunc, unsigned update)
{
	memset (&c, 0, sizeof c);
	for (int st = 0; st < 6; st++)
	{
		Config::Stage &g = c.stage[st];
		for (int k = 0; k < 3; k++)
		{
			g.src[k] = (unsigned char) (w[st][0] >> (4 * k) & 15); g.srcA[k] = (unsigned char) (w[st][0] >> (16 + 4 * k) & 15);
			g.op[k] = (unsigned char) (w[st][1] >> (4 * k) & 15); g.opA[k] = (unsigned char) (w[st][1] >> (12 + 4 * k) & 7);
		}
		g.mode = (unsigned char) (w[st][2] & 15); g.modeA = (unsigned char) (w[st][2] >> 16 & 15);
		g.scale = 1; g.scaleA = 1;
		// (a stage that hands the stage before on unchanged)
		g.pass = g.mode == 0 && g.modeA == 0 && g.src[0] == 15 && g.srcA[0] == 15 && g.op[0] == 0 && g.opA[0] == 0;
	}
	c.updateRgb = (unsigned char) (update >> 8 & 15); c.updateA = (unsigned char) (update >> 12 & 15);
	c.texOn = (unsigned char) texOn; c.lit = lit; c.alphaTest = alphaTest; c.alphaFunc = (unsigned char) alphaFunc;
}

int main (int argc, char **argv)
{
	const int count = argc > 1 ? atoi (argv[1]) : 2000;
	unsigned seed = argc > 2 ? (unsigned) strtoul (argv[2], 0, 0) : 12345u;
	if (getenv ("PICATEV_VER") && atoi (getenv ("PICATEV_VER")) == 71) { qpu::setVersion (71, 32); qpu_set_version (71); printf ("(V3D 7.1)\n"); }
	int ok = 0, tooBig = 0, most = 0; long instr = 0;
	for (int n = 0; n < count; n++)
	{
		rs = (seed + (unsigned) n * 2654435761u) | 1;
		Config cf;
		randomConfig (cf);
		const unsigned pseed = rnd32 ();
		const int r = testConfig (cf, pseed, false, "");
		if (r == 2) { tooBig++; continue; }
		if (r == 1)
		{
			char name[64]; snprintf (name, sizeof name, "configuration %d", n);
			testConfig (cf, pseed, true, name);
			say (cf);
			if (getenv ("PICATEV_DUMP")) for (int i = 0; i < sh.prog.count (); i++) printf ("  %3d  %s%c", i, qpu_disassemble (sh.prog.words ()[i]), 10);
			return 1;
		}
		ok++; instr += sh.prog.count ();
		if (sh.nRegs > most) most = sh.nRegs;
	}
	printf ("ok  : %d random configurations as the reference (%d needed too many registers; %ld instructions on average, %d registers at most)\n", ok, tooBig, ok ? instr / ok : 0, most);
	// the game's own (A Link Between Worlds' title scene, a GPU trace's): the lit ground, the water, a one-colour layer
	static const unsigned GROUND[6][3] = { { 0x033001e2, 0x00002002, 0x00010008 }, { 0x0ee30ef2, 0x00000020, 0x00010001 }, { 0x03ef03df, 0, 0x00000001 }, { 0x03ff033d, 0, 0x00000001 }, { 0x033f0d0f, 0, 0x00000008 }, { 0x033f0def, 0, 0x00000003 } };
	static const unsigned WATER[6][3] = { { 0x0ee30e33, 0, 0 }, { 0x0ee30ef2, 0x00000020, 0x00010001 }, { 0x03ef03df, 0, 0x00000001 }, { 0x0eee0eee, 0, 0 }, { 0x0def0def, 0, 0x00040004 }, { 0x0ff00ff0, 0, 0x00010001 } };
	static const unsigned LAYER[6][3] = { { 0x0e3e0efe, 0, 0 }, { 0x0e1f0e1f, 0, 0 }, { 0x0e1f0e1f, 0, 0 }, { 0x0e1f0e1f, 0, 0 }, { 0x0e1f0e1f, 0, 0 }, { 0x0e1f0e1f, 0, 0 } };
	struct { const char *name; const unsigned (*w)[3]; unsigned texOn; bool lit, alphaTest; unsigned func; } GAME[] = {
		{ "the lit ground (6 stages, 2 textures)", GROUND, 3, true, true, 6 }, { "the water (1 texture)", WATER, 1, false, false, 0 }, { "a one-colour layer", LAYER, 0, false, false, 0 } };
	int bad = 0;
	for (auto &g : GAME)
	{
		Config cf;
		fromWords (cf, g.w, g.texOn, g.lit, g.alphaTest, g.func, 0);
		const int r = testConfig (cf, 777, true, g.name);
		if (r == 0) printf ("ok  : %s: %d instructions, %d uniforms, %d varyings, %d lookups, %d registers\n", g.name, sh.prog.count (), sh.nUni, sh.nVary, sh.nLook, sh.nRegs);
		else { printf ("FAIL: %s%s\n", g.name, r == 2 ? ": too many registers" : ""); say (cf); bad = 1; }
	}
	return bad;
}
