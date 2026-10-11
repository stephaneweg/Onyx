//
// picavs_test.cpp -- the vertex shaders generated from a specialized PICA200 shader's list (user/Libs/v3d/picavs.cpp)
// against the reference (picavs_ref.h), on the PC: random lists -- arithmetic on the inputs, the temporaries and
// constants, shuffled and negated operands, comparisons, guards one inside another, guarded writes --, built as a
// vertex shader and as a coordinate shader, checked against the QPU's instruction restrictions (qpulib), run by
// the simulator (qpusim) over random vertices.
//
//   picavs_test [lists] [seed]      (PICAVS_VER=71: built for V3D 7.1; PICAVS_DUMP=1: the first program)
//
// MIT License -- Copyright (c) 2026 Stephane Wegener (see docs/LICENSING.md)
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include "v3d/picavs.h"
#include "v3d/picavs_ref.h"
extern "C" {
#include "qpulib.h"
}
#include "qpusim.h"
using namespace qpusim;
using namespace picavs;

static unsigned rs = 1;
static unsigned rnd32 () { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static int rr (int n) { return (int) (rnd32 () % (unsigned) n); }
static float rf01 () { return (float) (rnd32 () & 0xFFFFFF) / 16777216.0f; }

struct Case
{
	std::vector<Op> ops;
	std::vector<float> consts;				// four a slot
	int slot (bool positive)
	{
		for (int i = 0; i < 4; i++) consts.push_back (positive ? 0.5f + rf01 () * 1.5f : rf01 () * 4.0f - 2.0f);
		return (int) (consts.size () / 4 - 1);
	}
};

// A random list. Inputs 0..5 are the vertex's; temporaries 16..21 and outputs 0..3 are written as it goes
// (`defined`: the components written so far -- an operand reads only those).
static void randomCase (Case &c)
{
	static const unsigned char OPS[] = { 0x00, 0x00, 0x08, 0x08, 0x0C, 0x0D, 0x01, 0x02, 0x03, 0x13, 0x13, 0x0B, 0x09, 0x0A, 0x0E, 0x0F, 0x30 };
	unsigned char defined[48];
	memset (defined, 0, sizeof defined);
	for (int r = 0; r < 6; r++) defined[r] = 0xF;
	int guards = 0, open[4], nOpen = 0;
	bool flags = false;
	const int count = 6 + rr (40);
	auto operand = [&] (Op &o, int k, unsigned need, bool positive)
	{
		// a constant, or a register whose needed components are there (an input else)
		if (positive ? rr (2) == 0 : rr (4) == 0) { o.kind[k] = 1; o.slot[k] = (unsigned short) c.slot (positive); for (int i = 0; i < 4; i++) o.sw[k][i] = (unsigned char) i; return; }
		for (int tries = 0; tries < 20; tries++)
		{
			const int reg = positive ? rr (6) : rr (4) == 0 ? rr (6) : 16 + rr (6);
			unsigned char sw[4]; bool ok = true;
			for (int i = 0; i < 4; i++) sw[i] = (unsigned char) rr (4);
			for (int i = 0; i < 4; i++) if ((need & (8u >> i)) && !(defined[reg] & (8u >> sw[i]))) ok = false;
			if (!ok) continue;
			o.kind[k] = 0; o.reg[k] = (unsigned char) reg; memcpy (o.sw[k], sw, 4); o.neg[k] = positive ? 0 : (unsigned char) (rr (4) == 0);
			return;
		}
		o.kind[k] = 0; o.reg[k] = (unsigned char) rr (6); for (int i = 0; i < 4; i++) o.sw[k][i] = (unsigned char) rr (4); o.neg[k] = 0;
	};
	for (int n = 0; n < count; n++)
	{
		Op o;
		memset (&o, 0, sizeof o);
		const int what = rr (16);
		if (what == 0)								// a comparison
		{
			o.op = OP_CMP; o.guard = nOpen ? (unsigned char) open[nOpen - 1] : 0; o.slot[2] = (unsigned short) (rr (6) << 3 | rr (6));
			operand (o, 0, 0xC, false); operand (o, 1, 0xC, false);
			c.ops.push_back (o); flags = true;
			continue;
		}
		if (what == 1 && flags && guards < 30 && nOpen < 4)			// a guard opened (inside the one open)
		{
			o.op = OP_GUARD; o.d = (unsigned char) ++guards; o.reg[0] = nOpen ? (unsigned char) open[nOpen - 1] : 0;
			o.reg[1] = (unsigned char) rr (4); o.reg[2] = (unsigned char) rr (4); o.shape = (unsigned char) rr (2);
			c.ops.push_back (o); open[nOpen++] = guards;
			continue;
		}
		if (what == 2 && nOpen) { nOpen--; continue; }				// ... closed
		if (what == 3 && !flags) { o.op = OP_FLAG; o.d = (unsigned char) rr (2); o.reg[0] = (unsigned char) rr (2); c.ops.push_back (o); continue; }
		o.op = OPS[rr ((int) sizeof OPS)];
		if (o.op == 0x30) { o.op = 0x38; o.shape = 2; } else o.shape = 1;
		o.dOut = (unsigned char) (rr (5) == 0); o.d = o.dOut ? (unsigned char) rr (4) : (unsigned char) (16 + rr (6));
		o.writes = (unsigned char) (1 + rr (15));
		o.guard = nOpen ? (unsigned char) open[nOpen - 1] : 0;
		const int dreg = o.dOut ? 32 + o.d : o.d;
		if (o.guard && (defined[dreg] & o.writes) != o.writes)			// (a guarded write keeps what was there: put there first)
		{
			Op m;
			memset (&m, 0, sizeof m);
			m.op = OP_PUT; m.d = o.d; m.dOut = o.dOut; m.writes = (unsigned char) (o.writes & ~defined[dreg]); m.slot[0] = (unsigned short) c.slot (false);
			c.ops.push_back (m); defined[dreg] |= m.writes;
		}
		unsigned need0 = o.writes, need1 = o.writes;
		bool positive = false;
		switch (o.op)
		{
		case 0x01: need0 = need1 = 0xE; break;
		case 0x02: need0 = need1 = 0xF; break;
		case 0x03: need0 = 0xE; need1 = 0xF; break;
		case 0x0E: case 0x0F: need0 = 8; need1 = 0; positive = true; break;	// (of an input or a constant: positive, away from 0)
		case 0x0B: case 0x13: need1 = 0; break;
		default: break;
		}
		operand (o, 0, need0, positive);
		if (need1) operand (o, 1, need1, false);
		if (o.shape == 2) operand (o, 2, o.writes, false);
		c.ops.push_back (o); defined[dreg] |= o.writes;
	}
}

static int fails, built, refused;

static bool check (const char *name, const Case &c, const std::vector<Write> &w, bool coord, bool dump, const Lighting *lit = 0)
{
	qpu::Prog p;
	static Uni uni[2048]; static In in[64];
	Result r;
	const List l = { c.ops.data (), (u32) c.ops.size () };
	if (!build (p, l, w.data (), (u32) w.size (), uni, 2048, in, 64, r, 0, lit)) { refused++; return true; }	// (too many values at once: a list the processor keeps)
	built++;
	char err[200];
	if (qpu_check ((const uint64_t *) p.words (), p.count (), coord ? QPU_COORD : QPU_VERTEX, err, sizeof err) < 0) { printf ("FAIL %s: %s\n", name, err); fails++; return false; }
	if (dump) for (int i = 0; i < p.count (); i++) printf ("  %3d  %s\n", i, qpu_disassemble (p.words ()[i]));
	float view[V_COUNT];
	for (int i = 0; i < V_COUNT; i++) view[i] = 0.5f + rf01 ();
	view[V_CX] = rf01 () * 0.2f - 0.1f; view[V_CY] = rf01 () * 0.2f - 0.1f; view[V_CZ] = rf01 () * 0.2f - 0.1f;
	view[V_SX] = 300.0f * 256.0f; view[V_SY] = -200.0f * 256.0f;
	// the lighting's values: colours, directions, the tables' broken lines (rising knots in 0..1)
	static float lv[LIGHT_FLOATS];
	for (int i = 0; i < LIGHT_FLOATS; i++) lv[i] = rf01 ();
	for (int i = 0; i < LIGHTS_MAX; i++) { for (int k = 0; k < 3; k++) lv[lightAt (i) + k] = rf01 () * 2.0f - 1.0f; lv[lightAt (i) + 15] = rf01 () * 0.3f; lv[lightAt (i) + 16] = rf01 () * 0.3f; }
	for (int k = 0; k < TABLES; k++) { float *t = lv + tableAt (k); t[0] = rf01 () * 0.5f; t[1] = rf01 () - 0.3f; for (int j = 0; j < LUT_KNOTS; j++) { t[2 + 2 * j] = ((float) j + rf01 ()) / (float) LUT_KNOTS; t[3 + 2 * j] = rf01 () * 2.0f - 1.0f; } }
	Run run; run.version = qpu::version ();
	for (u32 i = 0; i < r.nUni; i++)
	{
		union { float f; uint32_t u; } v;
		v.f = uni[i].kind == U_CONST ? c.consts[(size_t) uni[i].slot * 4 + uni[i].comp] : uni[i].kind == U_LIGHT ? lv[uni[i].slot] : uni[i].kind == U_TINY ? 1e-30f : view[uni[i].slot];
		run.uniforms.push_back (v.u);
	}
	std::vector<Pixel> vx (32);
	std::vector<std::vector<float> > inputs (vx.size (), std::vector<float> (16 * 4));
	for (size_t v = 0; v < vx.size (); v++)
	{
		for (int k = 0; k < 64; k++) inputs[v][k] = 0.5f + rf01 () * 1.5f;	// (positive: an input may be a reciprocal's operand)
		for (u32 k = 0; k < r.nIn; k++) { union { float f; uint32_t u; } q; q.f = inputs[v][in[k].reg * 4 + in[k].comp]; vx[v].vpmIn.push_back (q.u); }
	}
	if (!runVertex ((const uint64_t *) p.words (), p.count (), vx, run)) { printf ("FAIL %s: %s\n", name, run.error.c_str ()); fails++; return false; }
	int bad = 0;
	for (size_t v = 0; v < vx.size (); v++)
	{
		float regs[32][4], out[16][4];
		memset (regs, 0, sizeof regs); memset (out, 0, sizeof out);
		memcpy (regs, inputs[v].data (), sizeof (float) * 64);
		refRun (l, (const float (*)[4]) c.consts.data (), regs, out);
		float litP[4] = { 0, 0, 0, 0 }, litS[4] = { 0, 0, 0, 0 };
		if (lit) refLight (*lit, lv, out, litP, litS);
		for (size_t k = 0; k < w.size (); k++)
		{
			bool isInt;
			const float want = refWrite (w[k], out, view, isInt, litP, litS);
			if (want != want || fabsf (want) > 1e12f) continue;		// (an overflow somewhere: nothing to compare)
			if (k >= vx[v].vpmOut.size ()) { bad++; continue; }
			if (isInt) { if (fabsf (want) < 1e9f && labs ((long) (int) vx[v].vpmOut[k] - (long) want) > 2) bad++; continue; }
			union { float f; uint32_t u; } got; got.u = vx[v].vpmOut[k];
			if (!(fabsf (got.f - want) <= 2e-3f * (1.0f + fabsf (want)))) { if (!bad && dump) printf ("  vertex %zu write %zu: %g, wanted %g\n", v, k, (double) got.f, (double) want); bad++; }
		}
	}
	// (a comparison of two values a rounding apart may go the other way in the simulator: a vertex or two of the 32)
	if (bad > 3) { printf ("FAIL %s: %d outputs differ (%u operations, %d instructions)\n", name, bad, (unsigned) c.ops.size (), p.count ()); fails++; return false; }
	return true;
}

// PICAVS_FILE=<file>: the lists a game's shaders gave (n3dstest's N3DS_SPECDUMP: each with some vertices and the
// sixteen outputs the core's interpreter gives for them) -- built with every output written raw, run, compared.
static int fileRun (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f) { printf ("FAIL: cannot open %s\n", path); return 1; }
	int lists = 0, ok = 0, refusedHere = 0, bad = 0, peak = 0; long instrs = 0, ops = 0, posInstrs = 0;
	unsigned head[4];
	while (fread (head, 4, 4, f) == 4 && head[0] == 0x4C535650)
	{
		std::vector<Op> o (head[1]); std::vector<float> consts ((size_t) head[2] * 4), vv ((size_t) head[3] * 128);
		if (fread (o.data (), sizeof (Op), head[1], f) != head[1] || fread (consts.data (), 16, head[2], f) != head[2] || fread (vv.data (), 512, head[3], f) != head[3]) break;
		lists++; ops += head[1];
		std::vector<Write> w;
		for (int r = 0; r < 16; r++) for (int k = 0; k < 4; k++) { Write x = { W_RAW, (unsigned char) r, (unsigned char) k }; w.push_back (x); }
		qpu::Prog p;
		static Uni uni[4096]; static In in[64];
		Result r;
		const List l = { o.data (), head[1] };
		if (!build (p, l, w.data (), (u32) w.size (), uni, 4096, in, 64, r)) { refusedHere++; if (refusedHere <= 3) printf ("  list %d refused: %s (%d values)\n", lists, r.why, r.values); continue; }
		char err[200];
		if (qpu_check ((const uint64_t *) p.words (), p.count (), QPU_VERTEX, err, sizeof err) < 0) { printf ("FAIL list %d: %s\n", lists, err); bad++; continue; }
		instrs += p.count (); if (r.regsPeak > peak) peak = r.regsPeak;
		{ qpu::Prog q; Result r2; std::vector<Write> w4; for (int k = 0; k < 4; k++) { Write x = { W_RAW, 0, (unsigned char) k }; w4.push_back (x); } static Uni u2[4096]; static In i2[64]; if (build (q, l, w4.data (), 4, u2, 4096, i2, 64, r2)) posInstrs += q.count (); }
		Run run; run.version = qpu::version ();
		for (u32 i = 0; i < r.nUni; i++) { union { float f; uint32_t u; } v; v.f = consts[(size_t) uni[i].slot * 4 + uni[i].comp]; run.uniforms.push_back (v.u); }
		std::vector<Pixel> vx (head[3]);
		for (unsigned v = 0; v < head[3]; v++)
			for (u32 k = 0; k < r.nIn; k++) { union { float f; uint32_t u; } q; q.f = vv[(size_t) v * 128 + in[k].reg * 4 + in[k].comp]; vx[v].vpmIn.push_back (q.u); }
		if (!runVertex ((const uint64_t *) p.words (), p.count (), vx, run)) { printf ("FAIL list %d: %s\n", lists, run.error.c_str ()); bad++; continue; }
		int differ = 0;
		for (unsigned v = 0; v < head[3]; v++)
			for (int k = 0; k < 64; k++)
			{
				const float want = vv[(size_t) v * 128 + 64 + k];
				union { float f; uint32_t u; } got; got.u = (size_t) k < vx[v].vpmOut.size () ? vx[v].vpmOut[k] : 0;
				if (want != want) continue;
				if (!(fabsf (got.f - want) <= 2e-3f * (1.0f + fabsf (want)))) { if (!differ) printf ("  list %d vertex %u output %d.%d: %g, the interpreter %g\n", lists, v, k / 4, k % 4, (double) got.f, (double) want); differ++; }
			}
		if (differ) bad++; else ok++;
	}
	fclose (f);
	printf ("%s: %s: %d lists (%ld operations each), %d built and right, %d refused, %d wrong; %ld instructions a program (%ld for one output register alone), %d registers at most\n",
		bad ? "FAIL" : "ok  ", path, lists, lists ? ops / lists : 0, ok, refusedHere, bad, ok ? instrs / ok : 0, ok ? posInstrs / ok : 0, peak);
	return bad ? 1 : 0;
}

int main (int argc, char **argv)
{
	if (getenv ("PICAVS_FILE")) { const int ver = getenv ("PICAVS_VER") ? atoi (getenv ("PICAVS_VER")) : 42; qpu::setVersion (ver); qpu_set_version (ver); return fileRun (getenv ("PICAVS_FILE")); }
	const int count = argc > 1 ? atoi (argv[1]) : 400;
	if (argc > 2) rs = (unsigned) atoi (argv[2]) | 1;
	const int ver = getenv ("PICAVS_VER") ? atoi (getenv ("PICAVS_VER")) : 42;
	qpu::setVersion (ver); qpu_set_version (ver);
	long instr = 0;
	for (int n = 0; n < count; n++)
	{
		Case c;
		randomCase (c);
		std::vector<Write> raw, vs, cs;
		for (int o = 0; o < 4; o++) for (int k = 0; k < 4; k++) { Write x = { W_RAW, (unsigned char) o, (unsigned char) k }; raw.push_back (x); }
		{ const unsigned char K[] = { W_XS, W_YS, W_ZS, W_RW }; for (int k = 0; k < 4; k++) { Write x = { K[k], 0, 0 }; vs.push_back (x); } }
		{ Write a = { W_RAW, 1, 0 }, b = { W_ONE_MINUS, 1, 1 }; vs.push_back (a); vs.push_back (b); }
		{ const unsigned char K[] = { W_XC, W_YC, W_ZC, W_WC, W_XS, W_YS }; for (int k = 0; k < 6; k++) { Write x = { K[k], 0, 0 }; cs.push_back (x); } }
		char name[64];
		snprintf (name, sizeof name, "list %d (raw outputs)", n); check (name, c, raw, false, n == 0 && getenv ("PICAVS_DUMP"));
		snprintf (name, sizeof name, "list %d (a vertex shader)", n); check (name, c, vs, false, false);
		snprintf (name, sizeof name, "list %d (a coordinate shader)", n); check (name, c, cs, true, false);
		// the lighting: the quaternion in output 2, the view vector in output 3 (both given a vertex's values first)
		{
			Case cl = c;
			Op m[2];
			memset (m, 0, sizeof m);
			for (int q = 0; q < 2; q++) { m[q].op = 0x13; m[q].shape = 1; m[q].dOut = 1; m[q].d = (unsigned char) (2 + q); m[q].writes = 0xF; m[q].reg[0] = (unsigned char) (4 + q); for (int i = 0; i < 4; i++) m[q].sw[0][i] = (unsigned char) i; }
			cl.ops.insert (cl.ops.begin (), m, m + 2);
			Lighting L;
			memset (&L, 0, sizeof L);
			L.count = (unsigned char) (1 + rr (2));
			for (int i = 0; i < 4; i++) { L.quat[i].reg = 2; L.quat[i].comp = (unsigned char) i; }
			for (int i = 0; i < 3; i++) { L.view[i].reg = 3; L.view[i].comp = (unsigned char) i; }
			for (int i = 0; i < L.count; i++) { L.light[i].directional = (unsigned char) rr (2); L.light[i].twoSided = (unsigned char) rr (2); L.light[i].distance = (unsigned char) (rr (3) == 0); }
			for (int k = 0; k < 6; k++) { L.table[k].on = (unsigned char) (rr (3) != 0); L.table[k].abs = (unsigned char) rr (2); L.table[k].input = (unsigned char) rr (5); }
			L.fresnel = (unsigned char) rr (4); L.clampHighlights = (unsigned char) rr (2);
			std::vector<Write> wl;
			for (int k = 0; k < 4; k++) { Write x = { W_LITP, 0, (unsigned char) k }, y = { W_LITS, 0, (unsigned char) k }; wl.push_back (x); wl.push_back (y); }
			{ Write x = { W_RAW, 0, 0 }; wl.push_back (x); }
			snprintf (name, sizeof name, "list %d (the lighting)", n); check (name, cl, wl, false, false, &L);
		}
	}
	printf ("%s: picavs, V3D %d.%d: %d programs built from %d lists and run (%d refused: too many values at once), %d failed\n", fails ? "FAIL" : "ok  ", ver / 10, ver % 10, built, count, refused, fails);
	return fails ? 1 : 0;
}
