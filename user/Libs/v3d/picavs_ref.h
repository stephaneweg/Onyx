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
// What write x gives for the outputs: a float, or (isInt) the screen's fixed-point integer.
inline float refWrite (const Write &x, const float out[16][4], const float view[V_COUNT], bool &isInt)
{
	const float *q = out[x.reg & 15];
	isInt = false;
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
