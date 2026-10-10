//
// v3d/picatev_ref.h -- what a PICA200 combiner configuration computes, in plain integers: the reference the
// generated shader (picatev.cpp) is checked against on the PC, written as user/Emulators/n3ds's software
// renderer computes a fragment (n3ds_pica.cpp).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener (see docs/LICENSING.md)
//
#ifndef _v3d_picatev_ref_h
#define _v3d_picatev_ref_h
#include "v3d/picatev.h"

namespace picatev
{

// a fragment's inputs, 0..255 a channel (r g b a)
struct Inputs { int primary[4], litP[4], litS[4], tex[3][4], konst[6][4], buffer[4], alphaRef, blend[4]; };

inline int refClamp (int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
inline int refCombine (unsigned mode, int a, int b, int c)
{
	switch (mode)
	{
	case 1: return a * b / 255;
	case 2: return refClamp (a + b);
	case 3: return refClamp (a + b - 128);
	case 4: return (a * c + b * (255 - c)) / 255;
	case 5: return refClamp (a - b);
	case 8: return refClamp (a * b / 255 + c);
	case 9: return refClamp ((a + b) * c / 255);
	default: return a;
	}
}

// -> false: the alpha test refuses the fragment (out is what the combiner made all the same)
inline bool reference (const Config &c, const Inputs &in, int out[4])
{
	static const int NEUTRAL[4] = { 0, 0, 0, 255 };
	int prev[4] = { in.primary[0], in.primary[1], in.primary[2], in.primary[3] };
	int buffer[4] = { 0, 0, 0, 0 }, next[4] = { in.buffer[0], in.buffer[1], in.buffer[2], in.buffer[3] };
	for (int st = 0; st < 6; st++)
	{
		const Config::Stage &sg = c.stage[st];
		if (!sg.pass)
		{
			const int *tab[16] = { in.primary, c.lit ? in.litP : in.primary, c.lit ? in.litS : NEUTRAL,
					       (c.texOn & 1) ? in.tex[0] : NEUTRAL, (c.texOn & 2) ? in.tex[1] : NEUTRAL, (c.texOn & 4) ? in.tex[2] : NEUTRAL,
					       NEUTRAL, prev, prev, prev, prev, prev, prev, buffer, in.konst[st], prev };
			int col[3][3], a[3];
			for (int k = 0; k < 3; k++)
			{
				const int *s = tab[sg.src[k] & 15];
				switch (sg.op[k])
				{
				case 1: col[k][0] = 255 - s[0]; col[k][1] = 255 - s[1]; col[k][2] = 255 - s[2]; break;
				case 2: col[k][0] = col[k][1] = col[k][2] = s[3]; break;
				case 3: col[k][0] = col[k][1] = col[k][2] = 255 - s[3]; break;
				case 4: col[k][0] = col[k][1] = col[k][2] = s[0]; break;
				case 5: col[k][0] = col[k][1] = col[k][2] = 255 - s[0]; break;
				case 8: col[k][0] = col[k][1] = col[k][2] = s[1]; break;
				case 9: col[k][0] = col[k][1] = col[k][2] = 255 - s[1]; break;
				case 12: col[k][0] = col[k][1] = col[k][2] = s[2]; break;
				case 13: col[k][0] = col[k][1] = col[k][2] = 255 - s[2]; break;
				default: col[k][0] = s[0]; col[k][1] = s[1]; col[k][2] = s[2]; break;
				}
				s = tab[sg.srcA[k] & 15];
				const int v = s[sg.opA[k] < 2 ? 3 : sg.opA[k] < 4 ? 0 : sg.opA[k] < 6 ? 1 : 2];
				a[k] = (sg.opA[k] & 1) ? 255 - v : v;
			}
			int o[4];
			if (sg.mode == 6 || sg.mode == 7)
			{
				const int d = ((col[0][0] * 2 - 255) * (col[1][0] * 2 - 255) + (col[0][1] * 2 - 255) * (col[1][1] * 2 - 255) + (col[0][2] * 2 - 255) * (col[1][2] * 2 - 255)) / 255;
				o[0] = o[1] = o[2] = refClamp (d);
			}
			else for (int i = 0; i < 3; i++) o[i] = refCombine (sg.mode, col[0][i], col[1][i], col[2][i]);
			o[3] = sg.mode == 7 ? o[0] : refCombine (sg.modeA, a[0], a[1], a[2]);
			for (int i = 0; i < 3; i++) prev[i] = refClamp (o[i] * sg.scale);
			prev[3] = refClamp (o[3] * sg.scaleA);
		}
		for (int i = 0; i < 4; i++) buffer[i] = next[i];
		if (st < 4)
		{
			if (c.updateRgb >> st & 1) { next[0] = prev[0]; next[1] = prev[1]; next[2] = prev[2]; }
			if (c.updateA >> st & 1) next[3] = prev[3];
		}
	}
	for (int i = 0; i < 4; i++)
	{
		const int k = in.blend[c.outConst[i] & 3];
		out[i] = c.outMode[i] == 1 ? prev[i] * k / 255 : c.outMode[i] == 2 ? prev[i] * (255 - k) / 255 : c.outMode[i] == 3 ? k : c.outMode[i] == 4 ? 255 - k : prev[i];
	}
	if (!c.alphaTest) return true;
	const int x = prev[3], r = in.alphaRef;
	switch (c.alphaFunc & 7) { case 0: return false; case 1: return true; case 2: return x == r; case 3: return x != r; case 4: return x < r; case 5: return x <= r; case 6: return x > r; default: return x >= r; }
}

} // namespace picatev
#endif
