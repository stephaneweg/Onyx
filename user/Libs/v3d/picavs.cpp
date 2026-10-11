//
// v3d/picavs.cpp -- see picavs.h. Three passes: the list is opened into scalar values (a node a value: an input's
// component, a uniform, an operation on earlier values; a register's component is simply "its current value", so
// a MOV costs nothing and a guarded write is a choice between the new value and the old); what the asked outputs
// do not need is dropped; the rest is written out in order, each value in a register of the file from where it is
// made to its last use.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener (see docs/LICENSING.md)
//
#include "v3d/picavs.h"

using namespace qpu;

namespace picavs
{

namespace
{
enum { K_IN, K_UNI, K_ZERO, K_ONES, K_ONE, K_ADD, K_MUL, K_MAX, K_MIN, K_FLR, K_NEG, K_RCP, K_RSQ, K_EX2, K_LG2,
       K_CMP, K_AND, K_OR, K_NOT, K_SEL, K_M2F, K_FTOIN, K_OUT };
enum { C_EQ, C_NE, C_LT, C_GE };
enum { NODES_MAX = 6144, REGS = 64 };

struct Node
{
	u8 k, code;				// (K_CMP: C_*; K_UNI: U_*; K_IN: the component)
	short a, b, c;				// the operands (nodes), -1: none
	u16 u0;					// K_UNI: the slot (or V_*); K_IN: the register; K_OUT: the VPM index
	u8 u1;					// K_UNI: the component
	bool need;
	int last;				// the last node that reads it
	short reg;
};

struct Gen
{
	Node n[NODES_MAX]; int nn;
	bool fail; const char *why;
	short cur[48][4], flag[2], guard[40];	// each register's components (0..31, then the outputs), the comparison's results, the guards: their values
	short clip[4], rw; int clipOf;		// (the position's clip coordinates and 1 / w, made once)

	int add (int k, int a = -1, int b = -1, int c = -1)
	{
		if (nn >= NODES_MAX) { fail = true; why = "the list is too long"; return 0; }
		Node &x = n[nn];
		x.k = (u8) k; x.code = 0; x.a = (short) a; x.b = (short) b; x.c = (short) c; x.u0 = 0; x.u1 = 0; x.need = false; x.last = -1; x.reg = -1;
		return nn++;
	}
	int uni (int kind, int slot, int comp) { const int i = add (K_UNI); n[i].code = (u8) kind; n[i].u0 = (u16) slot; n[i].u1 = (u8) comp; return i; }
	int val (int reg, int comp)		// a register's component now (an input not read yet: read; nothing written: 0)
	{
		if (cur[reg][comp] < 0)
		{
			if (reg < 16) { const int i = add (K_IN); n[i].u0 = (u16) reg; n[i].code = (u8) comp; cur[reg][comp] = (short) i; }
			else cur[reg][comp] = (short) add (K_ZERO);
		}
		return cur[reg][comp];
	}
	int operand (const Op &o, int k, int i)
	{
		if (o.kind[k]) return uni (U_CONST, o.slot[k], i);
		const int v = val (o.reg[k], o.sw[k][i] & 3);
		return o.neg[k] ? add (K_NEG, v) : v;
	}
	int cmp (u32 code, int a, int b)	// a comparison -> a mask (all ones: true)
	{
		int x = a, y = b, c;
		switch (code)
		{
		case 0: c = C_EQ; break; case 1: c = C_NE; break; case 2: c = C_LT; break; case 5: c = C_GE; break;
		case 3: c = C_GE; x = b; y = a; break;		// a <= b: not (b < a)
		case 4: c = C_LT; x = b; y = a; break;		// a > b: b < a
		default: return add (K_ONES);
		}
		const int i = add (K_CMP, x, y);
		n[i].code = (u8) c;
		return i;
	}
	int flagNow (int i) { if (flag[i] < 0) flag[i] = (short) add (K_ZERO); return flag[i]; }
};

static Gen G;
}

bool build (Prog &p, const List &l, const Write *w, u32 nw, Uni *uni, u32 uniMax, In *in, u32 inMax, Result &r, const Sem *pos, const Lighting *lit, u32 inPre)
{
	Gen &g = G;
	g.nn = 0; g.fail = false; g.why = 0; g.clipOf = -1; g.rw = -1;
	for (int i = 0; i < 48; i++) for (int c = 0; c < 4; c++) g.cur[i][c] = -1;
	g.flag[0] = g.flag[1] = -1;
	for (int i = 0; i < 40; i++) g.guard[i] = -1;
	r.nUni = 0; r.nIn = inPre < inMax ? inPre : inMax; r.ok = false; r.why = 0; r.values = 0; r.regsPeak = 0;

	// ---- 1. the list as scalar values
	for (u32 k = 0; k < l.nOps && !g.fail; k++)
	{
		const Op &o = l.ops[k];
		if (o.op == OP_FLAG) { g.flag[o.d & 1] = (short) g.add (o.reg[0] ? K_ONES : K_ZERO); continue; }
		if (o.op == OP_GUARD)
		{
			if (o.d >= 40 || o.reg[0] >= 40) { g.fail = true; g.why = "too many guards"; break; }
			const int x = (o.reg[2] >> 1 & 1) ? g.flagNow (0) : g.add (K_NOT, g.flagNow (0));
			const int y = (o.reg[2] & 1) ? g.flagNow (1) : g.add (K_NOT, g.flagNow (1));
			int c = o.reg[1] == 0 ? g.add (K_OR, x, y) : o.reg[1] == 1 ? g.add (K_AND, x, y) : o.reg[1] == 2 ? x : y;
			if (o.shape) c = g.add (K_NOT, c);
			if (o.reg[0]) c = g.add (K_AND, g.guard[o.reg[0]], c);
			g.guard[o.d] = (short) c;
			continue;
		}
		const int gd = o.guard ? g.guard[o.guard < 40 ? o.guard : 0] : -1;
		if (o.guard && gd < 0) { g.fail = true; g.why = "a guard used before it is made"; break; }
		if (o.op == OP_CMP)
		{
			for (int i = 0; i < 2; i++)
			{
				const u32 code = i == 0 ? o.slot[2] >> 3 & 7 : o.slot[2] & 7;
				int v = g.cmp (code, g.operand (o, 0, i), g.operand (o, 1, i));
				if (gd >= 0) v = g.add (K_SEL, gd, v, g.flagNow (i));
				g.flag[i] = (short) v;
			}
			continue;
		}
		const int dreg = o.dOut ? 32 + (o.d & 15) : (o.d & 31);
		int res[4] = { -1, -1, -1, -1 };
		if (o.op == OP_PUT) { for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) g.cur[dreg][i] = (short) g.uni (U_CONST, o.slot[0], i); continue; }	// (never guarded)
		if (o.shape == 2) { for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) res[i] = g.add (K_ADD, g.add (K_MUL, g.operand (o, 0, i), g.operand (o, 1, i)), g.operand (o, 2, i)); }
		else switch (o.op)
		{
		case 0x00: case 0x08: case 0x0C: case 0x0D:
		{
			const int k2 = o.op == 0x00 ? K_ADD : o.op == 0x08 ? K_MUL : o.op == 0x0C ? K_MAX : K_MIN;
			for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) res[i] = g.add (k2, g.operand (o, 0, i), g.operand (o, 1, i));
			break;
		}
		case 0x0B: for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) res[i] = g.add (K_FLR, g.operand (o, 0, i)); break;
		case 0x13: for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) res[i] = g.operand (o, 0, i); break;
		case 0x01: case 0x02: case 0x03: case 0x18:
		{
			int s = g.add (K_MUL, g.operand (o, 0, 0), g.operand (o, 1, 0));
			s = g.add (K_ADD, s, g.add (K_MUL, g.operand (o, 0, 1), g.operand (o, 1, 1)));
			s = g.add (K_ADD, s, g.add (K_MUL, g.operand (o, 0, 2), g.operand (o, 1, 2)));
			if (o.op == 0x02) s = g.add (K_ADD, s, g.add (K_MUL, g.operand (o, 0, 3), g.operand (o, 1, 3)));
			else if (o.op != 0x01) s = g.add (K_ADD, s, g.operand (o, 1, 3));
			for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) res[i] = s;
			break;
		}
		case 0x05: case 0x06: case 0x0E: case 0x0F:
		{
			const int s = g.add (o.op == 0x05 ? K_EX2 : o.op == 0x06 ? K_LG2 : o.op == 0x0E ? K_RCP : K_RSQ, g.operand (o, 0, 0));
			for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) res[i] = s;
			break;
		}
		case 0x09: case 0x1A: case 0x0A: case 0x1B:
			for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) res[i] = g.add (K_M2F, g.cmp (o.op == 0x09 || o.op == 0x1A ? 5 : 2, g.operand (o, 0, i), g.operand (o, 1, i)));
			break;
		default: g.fail = true; g.why = "an instruction the GPU's shader does not do"; break;
		}
		if (g.fail) break;
		for (int i = 0; i < 4; i++)
			if (o.writes & (8u >> i))
				g.cur[dreg][i] = (short) (gd >= 0 ? g.add (K_SEL, gd, res[i], g.val (dreg, i)) : res[i]);
	}

	// ---- 1b. the lighting at the vertex (see picavs.h; the 3DS core's lightRows, value by value)
	int litP[4] = { -1, -1, -1, -1 }, litS[4] = { -1, -1, -1, -1 };
	if (lit && !g.fail)
	{
		auto S = [&] (const Sem &q) { return q.reg == 0xFF ? g.add (K_ZERO) : g.val (32 + (q.reg & 15), q.comp & 3); };
		auto U = [&] (int slot) { return g.uni (U_LIGHT, slot, 0); };
		auto mul = [&] (int a, int b) { return g.add (K_MUL, a, b); };
		auto sum = [&] (int a, int b) { return g.add (K_ADD, a, b); };
		auto dot = [&] (const int a[3], const int b[3]) { return sum (sum (mul (a[0], b[0]), mul (a[1], b[1])), mul (a[2], b[2])); };
		// (a vector of length 0 stays 0, as on the processor: the square is kept above 1e-30)
		auto rsq = [&] (int square) { return g.add (K_RSQ, g.add (K_MAX, square, g.uni (U_TINY, 0, 0))); };
		auto unit = [&] (int v[3]) { const int inv = rsq (dot (v, v)); for (int c = 0; c < 3; c++) v[c] = mul (v[c], inv); };
		auto abs_ = [&] (int a) { return g.add (K_MAX, a, g.add (K_NEG, a)); };
		// a table as its broken line
		auto table = [&] (int k, int x, bool abs)
		{
			if (abs) x = abs_ (x);
			int y = sum (U (tableAt (k)), mul (x, U (tableAt (k) + 1)));
			for (int j = 0; j < LUT_KNOTS; j++)
				y = sum (y, mul (g.add (K_MAX, sum (x, g.add (K_NEG, U (tableAt (k) + 2 + 2 * j))), g.add (K_ZERO)), U (tableAt (k) + 3 + 2 * j)));
			return y;
		};
		int q[4];
		for (int c = 0; c < 4; c++) q[c] = S (lit->quat[c]);
		{ const int inv = rsq (sum (sum (mul (q[0], q[0]), mul (q[1], q[1])), sum (mul (q[2], q[2]), mul (q[3], q[3])))); for (int c = 0; c < 4; c++) q[c] = mul (q[c], inv); }
		int n[3], v[3], raw[3];
		{ const int t = sum (mul (q[0], q[2]), mul (q[3], q[1])); n[0] = sum (t, t); }
		{ const int t = sum (mul (q[1], q[2]), g.add (K_NEG, mul (q[3], q[0]))); n[1] = sum (t, t); }
		{ const int t = sum (mul (q[0], q[0]), mul (q[1], q[1])); n[2] = sum (g.add (K_ONE), g.add (K_NEG, sum (t, t))); }
		for (int c = 0; c < 3; c++) { raw[c] = S (lit->view[c]); v[c] = raw[c]; }
		unit (v);
		int dif[3], spe[3];
		for (int c = 0; c < 3; c++) { dif[c] = U (c); spe[c] = -1; }
		int in[4] = { -1, -1, -1, -1 };
		for (int li = 0; li < lit->count && li < LIGHTS_MAX && !g.fail; li++)
		{
			const int at = lightAt (li);
			int lv[3];
			for (int c = 0; c < 3; c++) { lv[c] = U (at + c); if (!lit->light[li].directional) lv[c] = sum (lv[c], raw[c]); }
			const int ld = dot (lv, lv), linv = rsq (ld);
			int att = -1;
			if (lit->light[li].distance)				// the table at clamp (scale x distance + bias, 0, 1)
			{
				int x = sum (mul (mul (ld, linv), U (at + 16)), U (at + 15));
				x = g.add (K_MIN, g.add (K_MAX, x, g.add (K_ZERO)), g.add (K_ONE));
				att = table (6 + li, x, true);
			}
			for (int c = 0; c < 3; c++) lv[c] = mul (lv[c], linv);
			int h[3];
			for (int c = 0; c < 3; c++) h[c] = sum (v[c], lv[c]);
			unit (h);
			const int nl = dot (n, lv);
			const int lightOn = lit->light[li].twoSided ? abs_ (nl) : g.add (K_MAX, nl, g.add (K_ZERO));
			in[0] = dot (n, h); in[1] = dot (v, h); in[2] = dot (n, v); in[3] = nl;
			auto look = [&] (int k) -> int				// -1: the table is off (1)
			{
				if (!lit->table[k].on) return -1;
				const int x = lit->table[k].input < 4 ? in[lit->table[k].input] : g.add (K_ZERO);
				return table (k, x, lit->table[k].abs != 0);
			};
			const int d0 = look (0), d1 = look (1), rr = look (3);
			const int rg = lit->table[4].on ? look (4) : rr, rb = lit->table[5].on ? look (5) : rr;
			const int refl[3] = { rr, rg, rb };
			const int high = lit->clampHighlights ? g.add (K_M2F, g.cmp (4, nl, g.add (K_ZERO))) : -1;	// (0 where the light is behind)
			for (int c = 0; c < 3; c++)
			{
				int a = sum (mul (U (at + 3 + c), lightOn), U (at + 6 + c));
				if (att >= 0) a = mul (a, att);
				dif[c] = sum (dif[c], a);
				int b0 = U (at + 9 + c), b1 = U (at + 12 + c);
				if (d0 >= 0) b0 = mul (b0, d0);
				if (d1 >= 0) b1 = mul (b1, d1);
				if (refl[c] >= 0) b1 = mul (b1, refl[c]);
				int b = sum (b0, b1);
				if (high >= 0) b = mul (b, high);
				if (att >= 0) b = mul (b, att);
				spe[c] = spe[c] < 0 ? b : sum (spe[c], b);
			}
			if (li == lit->count - 1 && lit->table[2].on)
			{
				const int fr = look (2);
				if (lit->fresnel & 1) litP[3] = fr;
				if (lit->fresnel & 2) litS[3] = fr;
			}
		}
		for (int c = 0; c < 4; c++)				// each colour brought into 0..1
		{
			int a = c < 3 ? dif[c] : litP[3], b = c < 3 ? spe[c] : litS[3];
			if (a < 0) a = g.add (K_ONE);
			if (b < 0) b = g.add (c < 3 ? K_ZERO : K_ONE);
			litP[c] = g.add (K_MIN, g.add (K_MAX, a, g.add (K_ZERO)), g.add (K_ONE));
			litS[c] = g.add (K_MIN, g.add (K_MAX, b, g.add (K_ZERO)), g.add (K_ONE));
		}
	}

	// ---- 2. what is written
	for (u32 k = 0; k < nw && !g.fail; k++)
	{
		const Write &x = w[k];
		const int oreg = 32 + (x.reg & 15);
		int v;
		if (x.kind == W_RAW) v = g.val (oreg, x.comp & 3);
		else if (x.kind == W_ZERO) v = g.add (K_ZERO);
		else if (x.kind == W_ONE) v = g.add (K_ONE);
		else if (x.kind == W_LITP || x.kind == W_LITS) { v = (x.kind == W_LITP ? litP : litS)[x.comp & 3]; if (v < 0) { g.fail = true; g.why = "the lighting's colour asked without the lighting"; v = 0; } }
		else if (x.kind == W_ONE_MINUS) { const int one = g.add (K_M2F, g.add (K_ONES)); v = g.add (K_ADD, one, g.add (K_NEG, g.val (oreg, x.comp & 3))); }
		else
		{
			if (g.clipOf != oreg)				// the clip position: (x KX + w CX, y KY + w CY, z KZ + w CZ, w)
			{
				auto P = [&] (int c) { return !pos ? g.val (oreg, c) : pos[c].reg == 0xFF ? g.add (c == 3 ? K_ONE : K_ZERO) : g.val (32 + (pos[c].reg & 15), pos[c].comp & 3); };
				const int pw = P (3);
				for (int c = 0; c < 3; c++)
					g.clip[c] = (short) g.add (K_ADD, g.add (K_MUL, P (c), g.uni (U_VIEW, V_KX + 2 * c, 0)), g.add (K_MUL, pw, g.uni (U_VIEW, V_CX + 2 * c, 0)));
				g.clip[3] = (short) pw; g.rw = (short) g.add (K_RCP, pw); g.clipOf = oreg;
			}
			switch (x.kind)
			{
			case W_XC: v = g.clip[0]; break; case W_YC: v = g.clip[1]; break; case W_ZC: v = g.clip[2]; break; case W_WC: v = g.clip[3]; break;
			case W_RW: v = g.rw; break;
			case W_XS: v = g.add (K_FTOIN, g.add (K_MUL, g.add (K_MUL, g.clip[0], g.rw), g.uni (U_VIEW, V_SX, 0))); break;
			case W_YS: v = g.add (K_FTOIN, g.add (K_MUL, g.add (K_MUL, g.clip[1], g.rw), g.uni (U_VIEW, V_SY, 0))); break;
			case W_ZS: v = g.add (K_ADD, g.add (K_MUL, g.add (K_MUL, g.clip[2], g.rw), g.uni (U_VIEW, V_SZ, 0)), g.uni (U_VIEW, V_OZ, 0)); break;
			default: g.fail = true; g.why = "an output of an unknown kind"; v = 0; break;
			}
		}
		const int o = g.add (K_OUT, v);
		g.n[o].u0 = (u16) k; g.n[o].need = true;
	}
	if (g.fail) { r.why = g.why; return false; }

	// ---- 3. what is needed, and till when
	for (int i = g.nn - 1; i >= 0; i--)
	{
		Node &x = g.n[i];
		if (!x.need) continue;
		if (x.a >= 0) g.n[x.a].need = true;
		if (x.b >= 0) g.n[x.b].need = true;
		if (x.c >= 0) g.n[x.c].need = true;
	}
	for (int i = 0; i < g.nn; i++)
	{
		const Node &x = g.n[i];
		if (!x.need) continue;
		r.values++;
		if (x.k == K_OUT) continue;				// (written as soon as its value is made: see below)
		if (x.a >= 0) g.n[x.a].last = i;
		if (x.b >= 0) g.n[x.b].last = i;
		if (x.c >= 0) g.n[x.c].last = i;
	}

	// (the outputs each value is: a chain per value)
	static short firstOut[NODES_MAX], nextOut[NODES_MAX];
	for (int i = 0; i < g.nn; i++) firstOut[i] = nextOut[i] = -1;
	for (int i = g.nn - 1; i >= 0; i--) if (g.n[i].k == K_OUT && g.n[i].a >= 0) { nextOut[i] = firstOut[g.n[i].a]; firstOut[g.n[i].a] = (short) i; }
	// ---- 4. the program
	bool used[REGS]; int ready[REGS], live = 0;
	// (a vertex or coordinate shader is run one thread a QPU -- KAPI_GPU_P_VS_1WAY --: the whole register file is its own)
	struct Whole { int was; Whole () : was (regs ()) { if (version () >= 71) setVersion (71, 64); } ~Whole () { if (version () >= 71) setVersion (71, was); } } whole;
	const int limit = version () >= 71 ? (rfLimit () < REGS ? rfLimit () : REGS) : REGS;
	for (int i = 0; i < REGS; i++) { used[i] = false; ready[i] = 0; }
	// a register read: not before what was put in it is there (a VPM read lands three instructions later)
	auto wait = [&] (int a, int b = -1, int c = -1)
	{
		const int v[3] = { a, b, c };
		for (int k = 0; k < 3; k++) if (v[k] >= 0) while (p.count () < ready[g.n[v[k]].reg]) p << I ();
	};
	auto R_ = [&] (int node) { return rf (g.n[node].reg); };
	auto index = [&] (u32 i) -> R				// a VPM index as an operand (more than 15: counted up in r3)
	{
		if (i <= 15) return imm (i);
		p << I ().m (V3D_QPU_M_MOV, r3, imm (15));
		for (u32 left = i - 15; left; ) { const u32 s = left > 15 ? 15 : left; p << I ().m (V3D_QPU_M_ADD, r3, r3, imm (s)); left -= s; }
		return r3;
	};
	for (int i = 0; i < g.nn; i++)
	{
		Node &x = g.n[i];
		if (!x.need || x.k == K_OUT) continue;
		{
			int d = -1;
			for (int k = 1; k < limit && d < 0; k++) if (!used[k]) d = k;
			if (d < 0) { r.why = "more values alive at once than the QPU has registers"; return false; }
			used[d] = true; x.reg = (short) d; live++;
			if (live > r.regsPeak) r.regsPeak = live;
			const R D = rf (d);
			wait (x.a, x.b, x.c);
			switch (x.k)
			{
			case K_IN:
			{
				u32 where = r.nIn;				// (one of the given first inputs, or the next)
				for (u32 q = 0; q < inPre && q < r.nIn; q++) if (in[q].reg == (u8) x.u0 && in[q].comp == x.code) { where = q; break; }
				if (where == r.nIn)
				{
					if (r.nIn >= inMax) { r.why = "more inputs than there is room for"; return false; }
					in[r.nIn].reg = (u8) x.u0; in[r.nIn].comp = x.code; r.nIn++;
				}
				const R at = index (where);
				p << I ().a (V3D_QPU_A_LDVPMV_IN, D, at);
				ready[d] = p.count () + 2;
				break;
			}
			case K_UNI:
				if (r.nUni >= uniMax) { r.why = "more uniforms than there is room for"; return false; }
				uni[r.nUni].kind = x.code; uni[r.nUni].slot = x.u0; uni[r.nUni].comp = x.u1; r.nUni++;
				p << I ().ldunifrf (D);
				break;
			case K_ZERO: p << I ().m (V3D_QPU_M_MOV, D, imm (0)); break;
			case K_ONES: p << I ().m (V3D_QPU_M_MOV, D, imm ((u32) -1)); break;
			case K_ONE: p << I ().m (V3D_QPU_M_MOV, D, imm (0x3f800000)); break;
			case K_ADD: p << I ().a (V3D_QPU_A_FADD, D, R_ (x.a), R_ (x.b)); break;
			case K_MUL: p << I ().m (V3D_QPU_M_FMUL, D, R_ (x.a), R_ (x.b)); break;
			case K_MAX: p << I ().a (V3D_QPU_A_FMAX, D, R_ (x.a), R_ (x.b)); break;
			case K_MIN: p << I ().a (V3D_QPU_A_FMIN, D, R_ (x.a), R_ (x.b)); break;
			case K_FLR: p << I ().a (V3D_QPU_A_FFLOOR, D, R_ (x.a)); break;
			case K_FTOIN: p << I ().a (V3D_QPU_A_FTOIN, D, R_ (x.a)); break;
			case K_NEG: p << I ().m (V3D_QPU_M_MOV, r0, imm (0)); p << I ().a (V3D_QPU_A_FSUB, D, r0, R_ (x.a)); break;
			case K_RCP: case K_RSQ: case K_EX2: case K_LG2:		// the SFU: its result in r4 three instructions on
				p << I ().m (V3D_QPU_M_MOV, x.k == K_RCP ? recip : x.k == K_RSQ ? rsqrt : x.k == K_EX2 ? exp2 : log2, R_ (x.a));
				p << I (); p << I ();
				p << I ().m (V3D_QPU_M_MOV, D, r4);
				break;
			case K_CMP:						// a - b: its sign, or its being zero, as a mask
				p << I ().a (V3D_QPU_A_FCMP, nop, R_ (x.a), R_ (x.b)).apf (x.code == C_EQ || x.code == C_NE ? V3D_QPU_PF_PUSHZ : V3D_QPU_PF_PUSHN);
				p << I ().m (V3D_QPU_M_MOV, D, imm (0));
				p << I ().m (V3D_QPU_M_MOV, D, imm ((u32) -1)).mc (x.code == C_EQ || x.code == C_LT ? V3D_QPU_COND_IFA : V3D_QPU_COND_IFNA);
				break;
			case K_AND: p << I ().a (V3D_QPU_A_AND, D, R_ (x.a), R_ (x.b)); break;
			case K_OR: p << I ().a (V3D_QPU_A_OR, D, R_ (x.a), R_ (x.b)); break;
			case K_NOT: p << I ().a (V3D_QPU_A_NOT, D, R_ (x.a)); break;
			case K_SEL:						// a (the guard): b where it says yes, else c
				p << I ().m (V3D_QPU_M_MOV, D, R_ (x.c));
				p << I ().a (V3D_QPU_A_OR, nop, R_ (x.a), R_ (x.a)).apf (V3D_QPU_PF_PUSHZ);
				p << I ().m (V3D_QPU_M_MOV, D, R_ (x.b)).mc (V3D_QPU_COND_IFNA);
				break;
			case K_M2F:						// a mask as 1.0 / 0.0
				p << I ().a (V3D_QPU_A_OR, nop, R_ (x.a), R_ (x.a)).apf (V3D_QPU_PF_PUSHZ);
				p << I ().m (V3D_QPU_M_MOV, D, imm (0));
				p << I ().m (V3D_QPU_M_MOV, D, imm (0x3f800000)).mc (V3D_QPU_COND_IFNA);
				break;
			default: r.why = "a value of an unknown kind"; return false;
			}
			if (ready[d] < p.count () + 1) ready[d] = p.count () + 1;	// (a register of the file is not read by the instruction right after its write)
			// what is written of it goes to the VPM now: an output's value is not kept to the program's end
			for (int o = firstOut[i]; o >= 0; o = nextOut[o])
			{
				wait (i);
				const R at = index (g.n[o].u0);
				p << I ().a (V3D_QPU_A_STVPMV, at, D);
			}
			if (x.last < 0) { used[d] = false; live--; }			// (read by nothing else)
		}
		const int ops[3] = { x.a, x.b, x.c };
		for (int k = 0; k < 3; k++)
			if (ops[k] >= 0 && g.n[ops[k]].last == i && g.n[ops[k]].reg >= 0 && used[g.n[ops[k]].reg])
			{
				bool again = false;					// (the same value as two operands: freed once)
				for (int q = 0; q < k; q++) if (ops[q] == ops[k]) again = true;
				if (!again) { used[g.n[ops[k]].reg] = false; live--; }
			}
	}
	p << I ().a (V3D_QPU_A_VPMWT, nop);
	p << I ().thrsw ();
	p << I ();
	p << I ();
	if (!p.ok ()) { r.why = "an instruction could not be encoded"; return false; }
	r.ok = true;
	return true;
}

}
