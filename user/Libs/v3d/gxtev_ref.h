//
// v3d/gxtev_ref.h -- the TEV of a configuration computed on the CPU, for checking the generated
// shaders (tools/tests/v3d/gxtev_test.cpp in the simulator, /bin/v3dprog on the GPU): the GLSL of
// pc/NintendoEMU/core/gxgl.cpp (FS_STAGE, FS_END), line for line, for the features gxtev.cpp
// generates. Header only, freestanding.
//
#ifndef _v3d_gxtev_ref_h
#define _v3d_gxtev_ref_h
#include "v3d/gxtev.h"

namespace gxtev
{

// the values a draw gives through the uniforms: the TEV registers (s11), the konst colours, the
// alpha references, the destination alpha (0..1)
struct Dyn { int regs[16]; int konst[16]; int aref[2]; float dstA; };


inline int shl (int v, int n) { return (int) ((unsigned) v << n); }
inline void tevReg (const int a[3], const int b[3], const int c[3], const int d[3], int bias, int sub, int scale, int n, int *r)
{
	int sh = scale == 1 ? 1 : scale == 2 ? 2 : 0;
	int rnd = sub == 0 ? (scale == 3 ? 128 : 0) : (scale == 3 ? 0 : 127);
	for (int i = 0; i < n; i++)
	{
		int l = (shl (shl (a[i], 8) + (b[i] - a[i]) * (c[i] + (c[i] >> 7)), sh) + rnd) >> 8;
		int x = shl (d[i] + (bias == 1 ? 128 : bias == 2 ? -128 : 0), sh) + (sub != 0 ? -l : l);
		r[i] = scale == 3 ? x >> 1 : x;
	}
}
inline int cmpKey (const int v[3], int m) { return m == 0 ? v[0] : m == 1 ? (v[1] << 8) | v[0] : (v[2] << 16) | (v[1] << 8) | v[0]; }
static const int kfracT[8] = { 255, 223, 191, 159, 128, 96, 64, 32 };

// one pixel: its rasterized colours (0..255), its lookups' texels (R G B A) -> RGBA 0..255 as the
// EFB gets it (before the EFB format and the TLB conversion), false: discarded
inline bool reference (const Config &cf, const Dyn &dy, const int ras0[4], const int ras1[4], const int texel[8][4], int out[4])
{
	int R[4][4];
	for (int r = 0; r < 4; r++) for (int ch = 0; ch < 4; ch++) R[r][ch] = dy.regs[r * 4 + ch];
	int look = 0;
	for (int st = 0; st < cf.nStages; st++)
	{
		unsigned CENV = cf.cenv[st], AENV = cf.aenv[st], TREF = cf.tref[st], KSEL = cf.ksel[st];
		int texC[4] = { 255, 255, 255, 255 };
		if (TREF & 64)
		{
			int t = (AENV >> 2) & 3;
			for (int ch = 0; ch < 4; ch++) texC[ch] = texel[look][cf.swap[t * 4 + ch]];
			look++;
		}
		int chn = (TREF >> 7) & 7;
		int ras[4] = { 0, 0, 0, 0 };
		if (chn == 0 || chn == 1) { const int *src = chn ? ras1 : ras0; for (int ch = 0; ch < 4; ch++) ras[ch] = src[cf.swap[(AENV & 3) * 4 + ch]]; }
		int ksc = KSEL & 31, ksa = (KSEL >> 5) & 31, kC[4];
		for (int ch = 0; ch < 3; ch++)
			kC[ch] = ksc < 8 ? kfracT[ksc] : ksc >= 12 && ksc < 16 ? dy.konst[(ksc - 12) * 4 + ch] : ksc >= 16 ? dy.konst[((ksc - 16) & 3) * 4 + ((ksc - 16) >> 2)] : 0;
		kC[3] = ksa < 8 ? kfracT[ksa] : ksa >= 16 ? dy.konst[((ksa - 16) & 3) * 4 + ((ksa - 16) >> 2)] : 0;
		auto cin = [&] (int s, int ch) -> int {
			switch (s)
			{
			case 0: case 2: case 4: case 6: return R[s >> 1][ch];
			case 1: case 3: case 5: case 7: return R[s >> 1][3];
			case 8: return texC[ch]; case 9: return texC[3]; case 10: return ras[ch]; case 11: return ras[3];
			case 12: return 255; case 13: return 128; case 14: return kC[ch];
			}
			return 0;
		};
		auto ain = [&] (int s) -> int {
			switch (s) { case 0: case 1: case 2: case 3: return R[s][3]; case 4: return texC[3]; case 5: return ras[3]; case 6: return kC[3]; }
			return 0;
		};
		int a[3], b[3], c[3], d[3];
		for (int ch = 0; ch < 3; ch++)
		{
			a[ch] = cin ((CENV >> 12) & 15, ch) & 255; b[ch] = cin ((CENV >> 8) & 15, ch) & 255;
			c[ch] = cin ((CENV >> 4) & 15, ch) & 255; d[ch] = cin (CENV & 15, ch);
		}
		int aa = ain ((AENV >> 13) & 7) & 255, ab = ain ((AENV >> 10) & 7) & 255, ac = ain ((AENV >> 7) & 7) & 255, ad = ain ((AENV >> 4) & 7);
		int cb = (CENV >> 16) & 3, co = (CENV >> 18) & 1, cs = (CENV >> 20) & 3;
		int rc[3];
		if (cb != 3) tevReg (a, b, c, d, cb, co, cs, 3, rc);
		else
		{
			int mode = (cs << 1) | co; bool eq = (mode & 1) != 0; int m = mode >> 1;
			if (m == 3) for (int i = 0; i < 3; i++) rc[i] = d[i] + ((eq ? a[i] == b[i] : a[i] > b[i]) ? c[i] : 0);
			else { int ka = cmpKey (a, m), kb = cmpKey (b, m); bool p = eq ? ka == kb : ka > kb; for (int i = 0; i < 3; i++) rc[i] = d[i] + (p ? c[i] : 0); }
		}
		for (int i = 0; i < 3; i++) rc[i] = (CENV & 0x80000) ? (rc[i] < 0 ? 0 : rc[i] > 255 ? 255 : rc[i]) : (rc[i] < -1024 ? -1024 : rc[i] > 1023 ? 1023 : rc[i]);
		int xb = (AENV >> 16) & 3, xo = (AENV >> 18) & 1, xs = (AENV >> 20) & 3;
		int ra;
		if (xb != 3) { int A3[3] = { aa }, B3[3] = { ab }, C3[3] = { ac }, D3[3] = { ad }; tevReg (A3, B3, C3, D3, xb, xo, xs, 1, &ra); }
		else
		{
			int mode = (xs << 1) | xo; bool eq = (mode & 1) != 0; int m = mode >> 1;
			if (m == 3) ra = ad + ((eq ? aa == ab : aa > ab) ? ac : 0);
			else { int ka = cmpKey (a, m), kb = cmpKey (b, m); ra = ad + ((eq ? ka == kb : ka > kb) ? ac : 0); }
		}
		ra = (AENV & 0x80000) ? (ra < 0 ? 0 : ra > 255 ? 255 : ra) : (ra < -1024 ? -1024 : ra > 1023 ? 1023 : ra);
		if (st == cf.nStages - 1) { for (int i = 0; i < 3; i++) R[0][i] = rc[i]; R[0][3] = ra; }
		else
		{
			int cd = (CENV >> 22) & 3, xd = (AENV >> 22) & 3;
			for (int i = 0; i < 3; i++) R[cd][i] = rc[i];
			R[xd][3] = ra;
		}
	}
	for (int ch = 0; ch < 4; ch++) out[ch] = R[0][ch] & 255;
	auto atest = [] (int f, int a, int r) {
		switch (f) { case 0: return false; case 1: return a < r; case 2: return a == r; case 3: return a <= r; case 4: return a > r; case 5: return a != r; case 6: return a >= r; }
		return true;
	};
	bool p0 = atest (cf.alphaFunc[0], out[3], dy.aref[0]), p1 = atest (cf.alphaFunc[1], out[3], dy.aref[1]);
	int lg = cf.alphaLogic;
	return lg == 0 ? (p0 && p1) : lg == 1 ? (p0 || p1) : lg == 2 ? (p0 != p1) : (p0 == p1);
}

} // namespace gxtev
#endif
