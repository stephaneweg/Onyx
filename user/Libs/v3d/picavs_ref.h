//
// v3d/picavs_ref.h -- what a picavs program must give (the tests' reference): the list run on a vertex with plain
// float arithmetic, then the asked outputs. Not used on the Pi.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener (see docs/LICENSING.md)
//
#ifndef _v3d_picavs_ref_h
#define _v3d_picavs_ref_h
#include <math.h>
#include <string.h>
#include "v3d/picavs.h"

namespace picavs
{
// regs: the 32 registers (the inputs in 0..15), out: the 16 outputs (zeros at the start); consts[slot][4]
inline void refRun (const List &l, const float consts[][4], float regs[32][4], float out[16][4])
{
	bool flag[2] = { false, false }, gv[40];
	gv[0] = true;
	for (u32 n = 0; n < l.nOps; n++)
	{
		const Op &o = l.ops[n];
		if (o.op == OP_GUARD)
		{
			const bool x = flag[0] == ((o.reg[2] >> 1 & 1) != 0), y = flag[1] == ((o.reg[2] & 1) != 0);
			bool c = o.reg[1] == 0 ? x || y : o.reg[1] == 1 ? x && y : o.reg[1] == 2 ? x : y;
			if (o.shape) c = !c;
			gv[o.d] = gv[o.reg[0]] && c;
			continue;
		}
		if (o.op == OP_FLAG) { flag[o.d & 1] = o.reg[0] != 0; continue; }
		if (o.guard && !gv[o.guard]) continue;
		float in[3][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
		if (o.op != OP_PUT)
			for (int k = 0; k < 3; k++)
				for (int i = 0; i < 4; i++)
					in[k][i] = o.kind[k] ? consts[o.slot[k]][i] : (o.neg[k] ? -regs[o.reg[k]][o.sw[k][i] & 3] : regs[o.reg[k]][o.sw[k][i] & 3]);
		if (o.op == OP_CMP)
		{
			for (int i = 0; i < 2; i++)
			{
				const u32 c = i == 0 ? o.slot[2] >> 3 & 7 : o.slot[2] & 7;
				const float x = in[0][i], y = in[1][i];
				flag[i] = c == 0 ? x == y : c == 1 ? x != y : c == 2 ? x < y : c == 3 ? x <= y : c == 4 ? x > y : c == 5 ? x >= y : true;
			}
			continue;
		}
		float v[4] = { 0, 0, 0, 0 };
		if (o.op == OP_PUT) memcpy (v, consts[o.slot[0]], sizeof v);
		else if (o.shape == 2) for (int i = 0; i < 4; i++) v[i] = in[0][i] * in[1][i] + in[2][i];
		else switch (o.op)
		{
		case 0x00: for (int i = 0; i < 4; i++) v[i] = in[0][i] + in[1][i]; break;
		case 0x08: for (int i = 0; i < 4; i++) v[i] = in[0][i] * in[1][i]; break;
		case 0x0C: for (int i = 0; i < 4; i++) v[i] = fmaxf (in[0][i], in[1][i]); break;
		case 0x0D: for (int i = 0; i < 4; i++) v[i] = fminf (in[0][i], in[1][i]); break;
		case 0x0B: for (int i = 0; i < 4; i++) v[i] = floorf (in[0][i]); break;
		case 0x13: for (int i = 0; i < 4; i++) v[i] = in[0][i]; break;
		case 0x01: case 0x02: case 0x03: case 0x18:
		{
			float s = in[0][0] * in[1][0];
			s = s + in[0][1] * in[1][1];
			s = s + in[0][2] * in[1][2];
			if (o.op == 0x02) s = s + in[0][3] * in[1][3]; else if (o.op != 0x01) s = s + in[1][3];
			v[0] = v[1] = v[2] = v[3] = s;
			break;
		}
		case 0x05: v[0] = v[1] = v[2] = v[3] = exp2f (in[0][0]); break;
		case 0x06: v[0] = v[1] = v[2] = v[3] = log2f (in[0][0]); break;
		case 0x0E: v[0] = v[1] = v[2] = v[3] = 1.0f / in[0][0]; break;
		case 0x0F: v[0] = v[1] = v[2] = v[3] = 1.0f / sqrtf (in[0][0]); break;
		case 0x09: case 0x1A: for (int i = 0; i < 4; i++) v[i] = in[0][i] >= in[1][i] ? 1.0f : 0.0f; break;
		case 0x0A: case 0x1B: for (int i = 0; i < 4; i++) v[i] = in[0][i] < in[1][i] ? 1.0f : 0.0f; break;
		default: break;
		}
		float *dst = o.dOut ? out[o.d & 15] : regs[o.d & 31];
		for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) dst[i] = v[i];
	}
}
// The lighting at a vertex (picavs.h), from the outputs: the two colours.
inline float refTable (const float *lv, int k, float x, bool abs)
{
	if (abs) x = fmaxf (x, -x);
	const float *t = lv + tableAt (k);
	float y = t[0] + x * t[1];
	for (int j = 0; j < LUT_KNOTS; j++) y = y + fmaxf (x + -t[2 + 2 * j], 0.0f) * t[3 + 2 * j];
	return y;
}
inline void refLight (const Lighting &L, const float *lv, const float out[16][4], float litP[4], float litS[4])
{
	auto S = [&] (const Sem &q) { return q.reg == 0xFF ? 0.0f : out[q.reg & 15][q.comp & 3]; };
	auto rsq = [] (float square) { return 1.0f / sqrtf (fmaxf (square, 1e-30f)); };
	auto unit = [&] (float v[3]) { const float inv = rsq (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); for (int c = 0; c < 3; c++) v[c] = v[c] * inv; };
	auto dot = [] (const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
	float q[4];
	for (int c = 0; c < 4; c++) q[c] = S (L.quat[c]);
	{ const float inv = rsq ((q[0] * q[0] + q[1] * q[1]) + (q[2] * q[2] + q[3] * q[3])); for (int c = 0; c < 4; c++) q[c] = q[c] * inv; }
	float n[3], v[3], raw[3];
	{ const float t = q[0] * q[2] + q[3] * q[1]; n[0] = t + t; }
	{ const float t = q[1] * q[2] + -(q[3] * q[0]); n[1] = t + t; }
	{ const float t = q[0] * q[0] + q[1] * q[1]; n[2] = 1.0f + -(t + t); }
	for (int c = 0; c < 3; c++) { raw[c] = S (L.view[c]); v[c] = raw[c]; }
	unit (v);
	float dif[3], spe[3] = { 0, 0, 0 }, aP = 1.0f, aS = 1.0f;
	for (int c = 0; c < 3; c++) dif[c] = lv[c];
	for (int li = 0; li < L.count && li < LIGHTS_MAX; li++)
	{
		const float *g = lv + lightAt (li);
		float l[3];
		for (int c = 0; c < 3; c++) l[c] = L.light[li].directional ? g[c] : g[c] + raw[c];
		const float ld = dot (l, l), linv = rsq (ld);
		float att = 1.0f;
		if (L.light[li].distance)
		{
			float x = ld * linv * g[16] + g[15];
			x = fminf (fmaxf (x, 0.0f), 1.0f);
			att = refTable (lv, 6 + li, x, true);
		}
		for (int c = 0; c < 3; c++) l[c] = l[c] * linv;
		float h[3];
		for (int c = 0; c < 3; c++) h[c] = v[c] + l[c];
		unit (h);
		const float nl = dot (n, l), lightOn = L.light[li].twoSided ? fmaxf (nl, -nl) : fmaxf (nl, 0.0f);
		const float in[4] = { dot (n, h), dot (v, h), dot (n, v), nl };
		auto look = [&] (int k) { return !L.table[k].on ? 1.0f : refTable (lv, k, L.table[k].input < 4 ? in[L.table[k].input] : 0.0f, L.table[k].abs != 0); };
		const float d0 = look (0), d1 = look (1), rr = look (3), rg = L.table[4].on ? look (4) : rr, rb = L.table[5].on ? look (5) : rr;
		const float refl[3] = { rr, rg, rb };
		const float high = L.clampHighlights ? (nl > 0.0f ? 1.0f : 0.0f) : 1.0f;
		for (int c = 0; c < 3; c++)
		{
			dif[c] = dif[c] + (g[3 + c] * lightOn + g[6 + c]) * att;
			spe[c] = spe[c] + (g[9 + c] * d0 + g[12 + c] * d1 * refl[c]) * high * att;
		}
		if (li == L.count - 1 && L.table[2].on)
		{
			const float fr = look (2);
			if (L.fresnel & 1) aP = fr;
			if (L.fresnel & 2) aS = fr;
		}
	}
	for (int c = 0; c < 4; c++)
	{
		litP[c] = fminf (fmaxf (c < 3 ? dif[c] : aP, 0.0f), 1.0f);
		litS[c] = fminf (fmaxf (c < 3 ? spe[c] : aS, 0.0f), 1.0f);
	}
}
// What write x gives for the outputs: a float, or (isInt) the screen's fixed-point integer.
inline float refWrite (const Write &x, const float out[16][4], const float view[V_COUNT], bool &isInt, const float *litP = 0, const float *litS = 0)
{
	const float *q = out[x.reg & 15];
	isInt = false;
	if (x.kind == W_LITP) return litP ? litP[x.comp & 3] : 0.0f;
	if (x.kind == W_LITS) return litS ? litS[x.comp & 3] : 0.0f;
	if (x.kind == W_RAW) return q[x.comp & 3];
	if (x.kind == W_ONE_MINUS) return 1.0f + -q[x.comp & 3];
	const float xc = q[0] * view[V_KX] + q[3] * view[V_CX], yc = q[1] * view[V_KY] + q[3] * view[V_CY], zc = q[2] * view[V_KZ] + q[3] * view[V_CZ];
	const float rw = 1.0f / q[3];
	switch (x.kind)
	{
	case W_XC: return xc; case W_YC: return yc; case W_ZC: return zc; case W_WC: return q[3]; case W_RW: return rw;
	case W_XS: isInt = true; return nearbyintf (xc * rw * view[V_SX]);
	case W_YS: isInt = true; return nearbyintf (yc * rw * view[V_SY]);
	default: return zc * rw * view[V_SZ] + view[V_OZ];
	}
}
}

#endif
