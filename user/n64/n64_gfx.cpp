//
// n64/n64_gfx.cpp -- the graphics tasks at a high level: what the RSP's F3DEX2 microcode (and
// OoT's F3DZEX2) does with a display list, and what the RDP then draws -- turned into the
// triangles of a GFrame for a renderer (the GPU on Onyx, software on the PC).
//
//   * the RSP: the segments, the matrix stack (model-view, projection; row vectors: clip =
//     v x MV x P), the vertices (transformed, lit by the directional lights and the ambient
//     one, the texture coordinates scaled), the triangles (TRI1 / TRI2 / QUAD), the display
//     list calls / branches / ends, MOVEWORD, MOVEMEM (viewport, lights), the geometry mode,
//     the other modes. The viewport is folded into the clip coordinates (the frame's own NDC).
//   * the RDP: the colour / depth / texture images, TMEM (4 KB, loaded by LOADBLOCK with its
//     dxt line counter, LOADTILE, LOADTLUT; odd lines swapped as the hardware does), the 8
//     tiles, the texture formats (RGBA 16 / 32, CI 4 / 8 with a RGBA16 or IA16 palette, IA 4 /
//     8 / 16, I 4 / 8) decoded into a cache of RGBA8 textures (keyed by a hash of what they
//     are made of), the colour combiner (evaluated for each vertex with a white and a black
//     texel: the texture is then multiplied by the result for white), the blender (alpha
//     blending recognised), the depth test / update, the fill and texture rectangles.
//
#include "n64/n64.h"
#ifdef N64_TRACE
#include <stdio.h>
#include <stdlib.h>
#endif

namespace n64 {

enum
{
	G_ZBUFFER = 0x1, G_SHADE = 0x4, G_CULL_FRONT = 0x200, G_CULL_BACK = 0x400, G_FOG = 0x10000, G_LIGHTING = 0x20000,
	G_TEXTURE_GEN = 0x40000, G_SHADING_SMOOTH = 0x200000
};
// othermode L
enum { OM_ZCMP = 0x10, OM_ZUPD = 0x20, OM_IMRD = 0x40, OM_ZMODE = 0xC00, OM_FORCEBL = 0x4000 };

static inline float fclamp (float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static void zeroMem (void *p, u32 n) { u8 *d = (u8 *) p; while (n--) *d++ = 0; }

void Machine::gfxInit ()
{
	for (int i = 0; i < 2; i++)
	{
		if (!gfxFrame[i].v) gfxFrame[i].v = new GVertex[GFrame::MAXV];
		if (!gfxFrame[i].b) gfxFrame[i].b = new GBatch[GFrame::MAXB];
		gfxFrame[i].nv = gfxFrame[i].nb = 0; gfxFrame[i].clear = 0;
		gfxFrame[i].width = 320; gfxFrame[i].height = 240; gfxFrame[i].cimg = 0;
	}
	gfxBuild = 0; gfxReady = -1; gfxSerial = 0; texClock = 0;
	for (int i = 0; i < MAX_TEX; i++)
	{
		if (!tex[i].px) tex[i].px = new u32[TEX_PIXELS];
		tex[i].w = tex[i].h = 0; tex[i].key = 0; tex[i].lastUse = 0; tex[i].dirty = false;
	}
	zeroMem (segment, sizeof segment);
	for (int i = 0; i < 16; i++) proj[i] = mtxStack[0][i] = (i % 5) == 0 ? 1.0f : 0.0f;
	mtxSp = 0; mvpDirty = true;
	zeroMem (vtx, sizeof vtx);
	geom = 0; texScaleS = texScaleT = 1; texTile = 0; texOn = 0;
	zeroMem (lights, sizeof lights); numLights = 0;
	vpScale[0] = 640; vpScale[1] = 480; vpScale[2] = 511; vpScale[3] = 0;
	vpTrans[0] = 640; vpTrans[1] = 480; vpTrans[2] = 511; vpTrans[3] = 0;
	rdpHalf1 = rdpHalf2 = 0; fogMul = fogOff = 0;
	omH = omL = 0; combH = combL = 0;
	for (int i = 0; i < 4; i++) primC[i] = envC[i] = blendC[i] = fogC[i] = 0;
	fillColor = 0; primDepth = 0;
	cimg = zimg = timg = 0; cimgW = 320; timgW = 0; timgSiz = timgFmt = 0;
	zeroMem (tiles, sizeof tiles); zeroMem (tmem, sizeof tmem); tmemSerial = 0;
	scissor[0] = scissor[1] = 0; scissor[2] = 320; scissor[3] = 240;
	cimgSiz = 2; rectTile = 0; drawMain = false; dlEnd = false;
}

// ---- the task ---------------------------------------------------------------------------------------------------
void Machine::gfxTask ()
{
	if (!gfxFrame[0].v) gfxInit ();
	u32 dl = dmem[0xFF0 / 4] & 0x7FFFFF;				// the OSTask's data_ptr
	gfxDl (dl);
}

void Machine::gfxDl (u32 addr)
{
	u32 stack[18]; int sp = 0;
	u32 pcDl = addr;
	for (int n = 0; n < 2000000; n++)
	{
		u32 w0 = rdW (pcDl), w1 = rdW (pcDl + 4);
		pcDl += 8;
		u32 op = w0 >> 24;
		if (op == 0xDF)						// G_ENDDL
		{
			if (sp == 0) return;
			pcDl = stack[--sp];
			continue;
		}
		if (op == 0xDE)						// G_DL
		{
			if (((w0 >> 16) & 0xFF) == 0 && sp < 18) stack[sp++] = pcDl;
			pcDl = seg (w1);
			continue;
		}
		if (op == 0xE9) { gfxCmd (w0, w1, pcDl, sp, stack); return; }	// full sync: the end
		dlEnd = false;
		gfxCmd (w0, w1, pcDl, sp, stack);
		if (dlEnd)							// (as G_ENDDL)
		{
			if (sp == 0) return;
			pcDl = stack[--sp];
		}
	}
}

// ---- the matrices ------------------------------------------------------------------------------------------------
static void mtxMul (const float *a, const float *b, float *out)	// out = a x b (4x4, row-major)
{
	float r[16];
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
	for (int i = 0; i < 16; i++) out[i] = r[i];
}

// ---- the vertices ------------------------------------------------------------------------------------------------
void Machine::gfxVtx (u32 addr, int n, int v0)
{
	float *mv = mtxStack[mtxSp];
	if (mvpDirty) { mtxMul (mv, proj, mvp); mvpDirty = false; }
	for (int i = 0; i < n; i++)
	{
		int k = v0 + i;
		if (k < 0 || k >= 64) break;
		u32 a = addr + (u32) i * 16;
		float x = (float) (s16) rdH (a), y = (float) (s16) rdH (a + 2), z = (float) (s16) rdH (a + 4);
		Vtx &v = vtx[k];
		v.x = x * mvp[0] + y * mvp[4] + z * mvp[8] + mvp[12];
		v.y = x * mvp[1] + y * mvp[5] + z * mvp[9] + mvp[13];
		v.z = x * mvp[2] + y * mvp[6] + z * mvp[10] + mvp[14];
		v.w = x * mvp[3] + y * mvp[7] + z * mvp[11] + mvp[15];
		v.s = (float) (s16) rdH (a + 8) * texScaleS;
		v.t = (float) (s16) rdH (a + 10) * texScaleT;
		u8 c0 = rdB (a + 12), c1 = rdB (a + 13), c2 = rdB (a + 14), c3 = rdB (a + 15);
		if (geom & G_LIGHTING)
		{
			float nx = (float) (s8) c0, ny = (float) (s8) c1, nz = (float) (s8) c2;
			float tx = nx * mv[0] + ny * mv[4] + nz * mv[8];
			float ty = nx * mv[1] + ny * mv[5] + nz * mv[9];
			float tz = nx * mv[2] + ny * mv[6] + nz * mv[10];
			float l = __builtin_sqrtf (tx * tx + ty * ty + tz * tz);
			if (l > 0) { tx /= l; ty /= l; tz /= l; }
			const Light &amb = lights[numLights < 8 ? numLights : 7];
			float r = amb.r, g = amb.g, b = amb.b;
			for (int j = 0; j < numLights && j < 7; j++)
			{
				const Light &L = lights[j];
				float d = tx * L.x + ty * L.y + tz * L.z;
				if (d > 0) { r += L.r * d; g += L.g * d; b += L.b * d; }
			}
			v.r = fclamp (r); v.g = fclamp (g); v.b = fclamp (b);
			if (geom & G_TEXTURE_GEN)			// sphere mapping from the normal
			{
				v.s = (tx + 1) * 0.5f * 1024 * texScaleS;
				v.t = (ty + 1) * 0.5f * 1024 * texScaleT;
			}
		}
		else { v.r = c0 / 255.0f; v.g = c1 / 255.0f; v.b = c2 / 255.0f; }
		v.a = c3 / 255.0f;
		u32 clip = 0;
		if (v.x < -v.w) clip |= 1;
		if (v.x > v.w) clip |= 2;
		if (v.y < -v.w) clip |= 4;
		if (v.y > v.w) clip |= 8;
		if (v.w < 0.01f) clip |= 16;
		v.clip = clip;
	}
}

// ---- the colour combiner ------------------------------------------------------------------------------------------
// One cycle: (A - B) * C + D for the colour and the alpha, the inputs of the RDP.
static void combineCycle (int ca, int cb, int cc, int cd, int aa, int ab, int ac, int ad,
			  const float *comb, const float *t0, const float *shade, const float *prim, const float *env, float *out)
{
	static const float ONE[4] = { 1, 1, 1, 1 }, ZERO[4] = { 0, 0, 0, 0 };
	auto colA = [&] (int i) -> const float * {
		switch (i) { case 0: return comb; case 1: case 2: return t0; case 3: return prim; case 4: return shade; case 5: return env; case 6: return ONE; default: return ZERO; } };
	auto colB = [&] (int i) -> const float * {
		switch (i) { case 0: return comb; case 1: case 2: return t0; case 3: return prim; case 4: return shade; case 5: return env; default: return ZERO; } };
	auto colD = [&] (int i) -> const float * {
		switch (i) { case 0: return comb; case 1: case 2: return t0; case 3: return prim; case 4: return shade; case 5: return env; case 6: return ONE; default: return ZERO; } };
	float C[3];
	switch (cc)
	{
	case 0: C[0] = comb[0]; C[1] = comb[1]; C[2] = comb[2]; break;
	case 1: case 2: C[0] = t0[0]; C[1] = t0[1]; C[2] = t0[2]; break;
	case 3: C[0] = prim[0]; C[1] = prim[1]; C[2] = prim[2]; break;
	case 4: C[0] = shade[0]; C[1] = shade[1]; C[2] = shade[2]; break;
	case 5: C[0] = env[0]; C[1] = env[1]; C[2] = env[2]; break;
	case 6: C[0] = C[1] = C[2] = 1; break;			// (scale)
	case 7: C[0] = C[1] = C[2] = comb[3]; break;
	case 8: case 9: C[0] = C[1] = C[2] = t0[3]; break;
	case 10: C[0] = C[1] = C[2] = prim[3]; break;
	case 11: C[0] = C[1] = C[2] = shade[3]; break;
	case 12: C[0] = C[1] = C[2] = env[3]; break;
	case 13: case 14: C[0] = C[1] = C[2] = 0.5f; break;	// (LOD fractions)
	default: C[0] = C[1] = C[2] = 0; break;
	}
	const float *A = colA (ca), *B = colB (cb), *D = colD (cd);
	for (int k = 0; k < 3; k++) out[k] = (A[k] - B[k]) * C[k] + D[k];
	auto al = [&] (int i) -> float {
		switch (i) { case 0: return comb[3]; case 1: case 2: return t0[3]; case 3: return prim[3]; case 4: return shade[3]; case 5: return env[3]; case 6: return 1; default: return 0; } };
	float Cc;
	switch (ac) { case 0: Cc = 0.5f; break; case 1: case 2: Cc = t0[3]; break; case 3: Cc = prim[3]; break; case 4: Cc = shade[3]; break;
		      case 5: Cc = env[3]; break; case 6: Cc = 0.5f; break; default: Cc = 0; break; }
	out[3] = (al (aa) - al (ab)) * Cc + al (ad);
}

void Machine::gfxCombine (const float *shade, const float *texel, float *out)
{
	int a0 = (combH >> 20) & 15, c0 = (combH >> 15) & 31, Aa0 = (combH >> 12) & 7, Ac0 = (combH >> 9) & 7;
	int a1 = (combH >> 5) & 15, c1 = combH & 31;
	int b0 = (combL >> 28) & 15, b1 = (combL >> 24) & 15, Aa1 = (combL >> 21) & 7, Ac1 = (combL >> 18) & 7;
	int d0 = (combL >> 15) & 7, Ab0 = (combL >> 12) & 7, Ad0 = (combL >> 9) & 7, d1 = (combL >> 6) & 7, Ab1 = (combL >> 3) & 7, Ad1 = combL & 7;
	float c[4] = { 0, 0, 0, 0 }, r[4];
	combineCycle (a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0, c, texel, shade, primC, envC, r);
	if (((omH >> 20) & 3) == 1)					// two cycles: the second one on the first's result
	{
		for (int k = 0; k < 4; k++) c[k] = fclamp (r[k]);
		combineCycle (a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1, c, texel, shade, primC, envC, r);
	}
	for (int k = 0; k < 4; k++) out[k] = fclamp (r[k]);
}

// the combiner reads a texel (in a cycle that is used)
static bool usesTexel (u32 h, u32 l, bool two)
{
	auto t = [] (int v) { return v == 1 || v == 2; };
	auto tc = [] (int v) { return v == 1 || v == 2 || v == 8 || v == 9; };
	bool c0 = t ((h >> 20) & 15) || t ((l >> 28) & 15) || tc ((h >> 15) & 31) || t ((l >> 15) & 7)
		|| t ((h >> 12) & 7) || t ((l >> 12) & 7) || t ((h >> 9) & 7) || t ((l >> 9) & 7);
	bool c1 = t ((h >> 5) & 15) || t ((l >> 24) & 15) || tc (h & 31) || t ((l >> 6) & 7)
		|| t ((l >> 21) & 7) || t ((l >> 3) & 7) || t ((l >> 18) & 7) || t (l & 7);
	return two ? (c0 || c1) : c0;
}

// ---- the render state -> the batch's flags ----------------------------------------------------------------------------
u32 Machine::gfxFlags ()
{
	u32 f = 0;
	bool z = (geom & G_ZBUFFER) != 0;
	if (z && (omL & OM_ZCMP)) f |= GF_ZLEQUAL; else f |= GF_ZALWAYS;
	if (!(z && (omL & OM_ZUPD)) || (omL & OM_ZMODE) == 0xC00) f |= GF_NOZWRITE;
	if (geom & G_CULL_BACK) f |= GF_CULL_BACK;
	if (geom & G_CULL_FRONT) f |= GF_CULL_FRONT;
	// the blender: IN * IN_ALPHA + MEM * (1 - A) in the cycle that counts, or a translucent z mode
	bool two = ((omH >> 20) & 3) == 1;
	u32 p = (omL >> (two ? 28 : 30)) & 3, a = (omL >> (two ? 24 : 26)) & 3, m = (omL >> (two ? 20 : 22)) & 3, b = (omL >> (two ? 16 : 18)) & 3;
	bool blend = ((omL & OM_FORCEBL) && p == 0 && a == 0 && m == 1 && b == 0) || (omL & OM_ZMODE) == 0x800;
	if (blend) f |= GF_BLEND_ALPHA;
	if ((omL & 3) == 1)						// alpha compare against the blend colour's alpha
	{
		u32 t = (u32) (blendC[3] * 255.0f + 0.5f);
		f |= GF_ALPHATEST | (t ? t : 1) << 19;
	}
	if (((omH >> 12) & 3) == 2) f |= GF_LINEAR;			// bilinear filter
	return f;
}

// ---- the textures ---------------------------------------------------------------------------------------------------------
static inline u32 rgba16 (u32 c)
{
	u32 r = (c >> 11) & 31, g = (c >> 6) & 31, b = (c >> 1) & 31;
	return (c & 1 ? 0xFF000000u : 0) | ((r << 19 | r << 14) & 0xFF0000) | ((g << 11 | g << 6) & 0xFF00) | (b << 3 | b >> 2);
}
static inline u32 ia (u32 i, u32 a) { return a << 24 | i << 16 | i << 8 | i; }

int Machine::gfxTexture (int t)
{
	Tile &T = tiles[t & 7];
	int w = T.masks ? 1 << T.masks : ((T.sh - T.sl) >> 2) + 1;
	int h = T.maskt ? 1 << T.maskt : ((T.th - T.tl) >> 2) + 1;
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	while (w * h > TEX_PIXELS) { if (h > 1) h >>= 1; else w >>= 1; }
	int tlut = (int) ((omH >> 14) & 3);
	// the key: what the texture is made of
	u64 key = 1469598103934665603ull;
	auto mix = [&] (u32 v) { key = (key ^ v) * 1099511628211ull; };
	mix ((u32) T.fmt); mix ((u32) T.siz); mix ((u32) T.line); mix ((u32) T.tmem); mix ((u32) T.pal); mix ((u32) w); mix ((u32) h); mix ((u32) tlut);
	u32 lineBytes = (u32) T.line * 8, base = (u32) T.tmem * 8;
	u32 span = lineBytes * (u32) h; if (span > 4096 || span == 0) span = 4096;
	for (u32 i = 0; i < span; i += 4)
	{
		u32 a = (base + i) & 0xFFC;
		mix ((u32) tmem[a] << 24 | (u32) tmem[a + 1] << 16 | (u32) tmem[a + 2] << 8 | tmem[a + 3]);
	}
	if (T.siz == 3) for (u32 i = 0; i < span; i += 4) { u32 a = (base + i + 0x800) & 0xFFC; mix ((u32) tmem[a] << 24 | tmem[a + 1] << 16 | tmem[a + 2] << 8 | tmem[a + 3]); }
	if (T.fmt == 2) for (u32 i = 0x800; i < 0x1000; i += 8) mix ((u32) tmem[i] << 8 | tmem[i + 1]);
	texClock++;
	int victim = 0; u32 oldest = 0xFFFFFFFF;
	for (int i = 0; i < MAX_TEX; i++)
	{
		if (tex[i].key == key && tex[i].w == w && tex[i].h == h) { tex[i].lastUse = gfxSerial; return i; }
		if (tex[i].w == 0) { victim = i; oldest = 0; }
		else if (oldest && tex[i].lastUse < oldest) { oldest = tex[i].lastUse; victim = i; }
	}
	// decode it from TMEM
#ifdef N64_TRACE
	if (getenv ("N64_TEXLOG")) printf ("tex %d: fmt %d siz %d %dx%d line %d tmem %d pal %d tlut %d  timg %08X siz %u w %u\n", victim, T.fmt, T.siz, w, h, T.line, T.tmem, T.pal, tlut, timg, timgSiz, timgW);
#endif
	GTexture &X = tex[victim];
	X.key = key; X.w = w; X.h = h; X.lastUse = gfxSerial; X.dirty = true;
	for (int y = 0; y < h; y++)
	{
		u32 row = base + (u32) y * lineBytes;
		u32 sw = (y & 1) ? 4 : 0;					// odd lines: the 32-bit words swapped
		for (int x = 0; x < w; x++)
		{
			u32 c = 0;
			switch (T.siz)
			{
			case 0:							// 4 bits
			{
				u8 b = tmem[((row + (u32) x / 2) ^ sw) & 0xFFF];
				u32 n = (x & 1) ? (b & 15u) : (u32) (b >> 4);
				if (T.fmt == 2)
				{
					u32 pa = (0x800 + (((u32) T.pal << 4) | n) * 8) & 0xFFF;
					u32 e = (u32) tmem[pa] << 8 | tmem[pa + 1];
					c = tlut == 3 ? ia (e >> 8, e & 0xFF) : rgba16 (e);
				}
				else if (T.fmt == 3) { u32 i3 = n >> 1; c = ia (i3 * 36, (n & 1) ? 255 : 0); }
				else c = ia (n * 17, n * 17);
				break;
			}
			case 1:							// 8 bits
			{
				u8 b = tmem[((row + (u32) x) ^ sw) & 0xFFF];
				if (T.fmt == 2)
				{
					u32 pa = (0x800 + (u32) b * 8) & 0xFFF;
					u32 e = (u32) tmem[pa] << 8 | tmem[pa + 1];
					c = tlut == 3 ? ia (e >> 8, e & 0xFF) : rgba16 (e);
				}
				else if (T.fmt == 3) c = ia ((u32) (b >> 4) * 17, (u32) (b & 15) * 17);
				else c = ia (b, b);
				break;
			}
			case 2:							// 16 bits
			{
				u32 a = ((row + (u32) x * 2) ^ sw) & 0xFFE;
				u32 e = (u32) tmem[a] << 8 | tmem[a + 1];
				if (T.fmt == 3) c = ia (e >> 8, e & 0xFF);
				else c = rgba16 (e);
				break;
			}
			default:						// 32 bits: RG in the low half, BA in the high one
			{
				u32 a = ((row + (u32) x * 2) ^ sw) & 0x7FE;
				c = (u32) tmem[a + 0x801] << 24 | (u32) tmem[a] << 16 | (u32) tmem[a + 1] << 8 | tmem[a + 0x800];
				break;
			}
			}
			X.px[y * w + x] = c;
		}
	}
	return victim;
}

// TMEM loads: 0 LOADBLOCK, 1 LOADTILE, 2 LOADTLUT
void Machine::gfxLoad (u32 w0, u32 w1, int kind)
{
	Tile &T = tiles[(w1 >> 24) & 7];
	int uls = (int) ((w0 >> 12) & 0xFFF), ult = (int) (w0 & 0xFFF), lrs = (int) ((w1 >> 12) & 0xFFF), lrt = (int) (w1 & 0xFFF);
	u32 dst = (u32) T.tmem * 8;
	tmemSerial++;
	if (kind == 2)								// the palette: each entry 4 times
	{
		int n = ((lrs >> 2) - (uls >> 2)) + 1;
		u32 src = timg + ((u32) (ult >> 2) * timgW + (u32) (uls >> 2)) * 2;
		for (int i = 0; i < n && i < 256; i++)
		{
			u16 e = rdH (src + (u32) i * 2);
			for (int k = 0; k < 4; k++) { u32 a = (dst + (u32) i * 8 + (u32) k * 2) & 0xFFF; tmem[a] = (u8) (e >> 8); tmem[a + 1] = (u8) e; }
		}
		return;
	}
	if (kind == 0)								// a block: lrs + 1 texels, dxt = the line counter's step
	{
		u32 dxt = (u32) lrt;
		u32 bytes = timgSiz == 0 ? (u32) (lrs - uls + 1) / 2 : (u32) (lrs - uls + 1) << (timgSiz - 1);
		u32 src = timg + (timgSiz == 0 ? ((u32) ult * timgW + (u32) uls) / 2 : ((u32) ult * timgW + (u32) uls) << (timgSiz - 1));
		u32 words = (bytes + 7) / 8;
		for (u32 i = 0; i < words; i++)
		{
			u32 odd = ((i * dxt) >> 11) & 1;
			u8 b[8];
			for (int k = 0; k < 8; k++) b[k] = rdB (src + i * 8 + (u32) k);
			if (timgSiz == 3)						// 32 bits: RG low, BA high
			{
				for (int k = 0; k < 2; k++)
				{
					u32 a = ((dst + i * 4 + (u32) k * 2) ^ (odd ? 4u : 0u)) & 0x7FE;
					tmem[a] = b[k * 4]; tmem[a + 1] = b[k * 4 + 1];
					tmem[a + 0x800] = b[k * 4 + 2]; tmem[a + 0x801] = b[k * 4 + 3];
				}
				continue;
			}
			for (int k = 0; k < 8; k++)
			{
				u32 a = (dst + i * 8 + (u32) k) & 0xFFF;
				if (odd) a ^= 4;
				tmem[a] = b[k];
			}
		}
		return;
	}
	// a tile: the rectangle's lines, one TMEM line each
	int s0 = uls >> 2, t0 = ult >> 2, s1 = lrs >> 2, t1 = lrt >> 2;
	u32 lineBytes = (u32) T.line * 8;
	for (int y = t0; y <= t1; y++)
	{
		u32 row = dst + (u32) (y - t0) * lineBytes;
		u32 sw = ((y - t0) & 1) ? 4 : 0;
		if (timgSiz == 3)
		{
			for (int x = s0; x <= s1; x++)
			{
				u32 src = timg + ((u32) y * timgW + (u32) x) * 4;
				u32 a = ((row + (u32) (x - s0) * 2) ^ sw) & 0x7FE;
				tmem[a] = rdB (src); tmem[a + 1] = rdB (src + 1); tmem[a + 0x800] = rdB (src + 2); tmem[a + 0x801] = rdB (src + 3);
			}
			continue;
		}
		u32 bytesRow = timgSiz == 0 ? (u32) (s1 - s0 + 1) / 2 : (u32) (s1 - s0 + 1) << (timgSiz - 1);
		u32 src = timg + (timgSiz == 0 ? ((u32) y * timgW + (u32) s0) / 2 : ((u32) y * timgW + (u32) s0) << (timgSiz - 1));
		for (u32 i = 0; i < bytesRow; i++) tmem[((row + i) ^ sw) & 0xFFF] = rdB (src + i);
	}
}

// ---- output ------------------------------------------------------------------------------------------------------------
void Machine::gfxEmit (const GVertex *v3, int texId, u32 flags)
{
	GFrame &F = gfxFrame[gfxBuild];
	if (F.nv + 3 > GFrame::MAXV) return;
	u32 f = flags | GF_NOMATRIX;
	if (F.nb == 0 || F.b[F.nb - 1].tex != texId || F.b[F.nb - 1].flags != f || F.b[F.nb - 1].first + F.b[F.nb - 1].count != (u32) F.nv)
	{
		if (F.nb >= GFrame::MAXB) return;
		GBatch &B = F.b[F.nb++];
		B.first = (u32) F.nv; B.count = 0; B.tex = texId; B.flags = f;
		for (int i = 0; i < 16; i++) B.m[i] = (i % 5) == 0 ? 1.0f : 0.0f;
	}
	for (int k = 0; k < 3; k++) F.v[F.nv++] = v3[k];
	F.b[F.nb - 1].count += 3;
}

static inline u8 c8 (float v) { return (u8) (v <= 0 ? 0 : v >= 1 ? 255 : (int) (v * 255.0f + 0.5f)); }

void Machine::gfxTri (int a, int b, int c)
{
	if (!drawMain) return;
	const Vtx *V[3] = { &vtx[a & 63], &vtx[b & 63], &vtx[c & 63] };
	if (V[0]->clip & V[1]->clip & V[2]->clip & 15) return;		// all outside the same side
	GFrame &F = gfxFrame[gfxBuild];
	float W = (float) F.width, H = (float) F.height;
	float sx = vpScale[0] / 4.0f, sy = vpScale[1] / 4.0f, tx = vpTrans[0] / 4.0f, ty = vpTrans[1] / 4.0f;
	bool two = ((omH >> 20) & 3) == 1;
	bool textured = texOn && usesTexel (combH, combL, two);
	int texId = -1; float tw = 1, th = 1; const Tile *T = 0;
	u32 flags = gfxFlags ();
	if (textured)
	{
		texId = gfxTexture (texTile);
		T = &tiles[texTile & 7];
		tw = (float) tex[texId].w; th = (float) tex[texId].h;
		int ws = T->cms & 2 ? 1 : (T->cms & 1 ? 2 : 0), wt = T->cmt & 2 ? 1 : (T->cmt & 1 ? 2 : 0);
		if (!T->masks) ws = 1;
		if (!T->maskt) wt = 1;
		flags |= (u32) ws << GF_WRAP_S_SHIFT | (u32) wt << GF_WRAP_T_SHIFT;
	}
	GVertex o[3];
	static const float WHITE[4] = { 1, 1, 1, 1 }, BLACK[4] = { 0, 0, 0, 0 };
	// the decal z mode (paths, shadows, grass patches on the ground): the RDP lets them pass
	// within the depth slope of the surface below; here they are drawn a little nearer (in
	// NDC depth) so that the "less or equal" test does not make them flicker (z-fighting)
	float zBias = (omL & OM_ZMODE) == 0xC00 ? 2e-4f : 0.0f;
	for (int k = 0; k < 3; k++)
	{
		const Vtx &v = *V[k];
		GVertex &g = o[k];
		// the viewport folded in: the N64 screen -> this frame's NDC, y up
		g.x = v.x * (sx / (W / 2)) + v.w * (tx / (W / 2) - 1);
		g.y = v.y * (sy / (H / 2)) + v.w * (1 - ty / (H / 2));
		g.z = v.z - zBias * v.w; g.w = v.w;
		float shade[4] = { v.r, v.g, v.b, v.a };
		if (!(geom & G_SHADE)) shade[0] = shade[1] = shade[2] = shade[3] = 1;
		// the combiner is affine in the texel for almost every mode: its result for a black
		// texel is the added colour, the difference with a white one the texel's factor
		float c0[4], c1[4];
		gfxCombine (shade, BLACK, c0);
		if (textured)
		{
			gfxCombine (shade, WHITE, c1);
			for (int j = 0; j < 4; j++) { c1[j] -= c0[j]; if (c1[j] < 0) c1[j] = 0; }
			g.r = c8 (c1[0]); g.g = c8 (c1[1]); g.b = c8 (c1[2]); g.a = c8 (c1[3]);
			g.r2 = c8 (c0[0]); g.g2 = c8 (c0[1]); g.b2 = c8 (c0[2]); g.a2 = c8 (c0[3]);
		}
		else
		{
			g.r = c8 (c0[0]); g.g = c8 (c0[1]); g.b = c8 (c0[2]); g.a = c8 (c0[3]);
			g.r2 = g.g2 = g.b2 = g.a2 = 0;
		}
		if (textured)
		{
			float u = v.s / 32.0f, w = v.t / 32.0f;
			if (T->shifts) u = T->shifts <= 10 ? u / (float) (1 << T->shifts) : u * (float) (1 << (16 - T->shifts));
			if (T->shiftt) w = T->shiftt <= 10 ? w / (float) (1 << T->shiftt) : w * (float) (1 << (16 - T->shiftt));
			u -= T->sl / 4.0f; w -= T->tl / 4.0f;
			g.s = u / tw; g.t = w / th;
		}
		else g.s = g.t = 0;
	}
	gfxEmit (o, texId, flags);
}

// A rectangle in the N64 screen's pixels (fill: a solid colour; tex: s, t in texels of the tile)
void Machine::gfxRect (float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1, bool textured, bool fill)
{
	if (!drawMain) return;
	GFrame &F = gfxFrame[gfxBuild];
	float W = (float) F.width, H = (float) F.height;
	int texId = -1; float tw = 1, th = 1;
	u32 flags = GF_ZALWAYS | GF_NOZWRITE;
	u32 cyc = (omH >> 20) & 3;
	float col[4] = { 1, 1, 1, 1 }, add[4] = { 0, 0, 0, 0 };
	if (fill)
	{
		u32 c = cimgSiz == 3 ? fillColor : fillColor >> 16;
		if (cimgSiz == 3) { col[0] = (c >> 24) / 255.0f; col[1] = ((c >> 16) & 255) / 255.0f; col[2] = ((c >> 8) & 255) / 255.0f; }
		else { col[0] = ((c >> 11) & 31) / 31.0f; col[1] = ((c >> 6) & 31) / 31.0f; col[2] = ((c >> 1) & 31) / 31.0f; }
	}
	else
	{
		if (cyc != 2)							// (copy mode: the texels as they are)
		{
			static const float WHITE[4] = { 1, 1, 1, 1 }, BLACK[4] = { 0, 0, 0, 0 };
			gfxCombine (WHITE, BLACK, add);
			if (textured)
			{
				gfxCombine (WHITE, WHITE, col);
				for (int j = 0; j < 4; j++) { col[j] -= add[j]; if (col[j] < 0) col[j] = 0; }
			}
			else { for (int j = 0; j < 4; j++) col[j] = add[j]; add[0] = add[1] = add[2] = add[3] = 0; }
			if ((geom & G_ZBUFFER) && (omL & OM_ZCMP)) flags = GF_ZLEQUAL | GF_NOZWRITE;
			u32 f = gfxFlags ();
			flags |= f & (GF_BLEND_ALPHA | GF_LINEAR | GF_ALPHATEST | 0xFF80000u);
		}
		else if ((omL & 3) == 1) flags |= GF_ALPHATEST | 1u << 19;	// (copy with alpha compare: alpha 0 not drawn)
		if (textured)
		{
			texId = gfxTexture (rectTile);
			tw = (float) tex[texId].w; th = (float) tex[texId].h;
			const Tile &T = tiles[rectTile & 7];
			s0 -= T.sl / 4.0f; s1 -= T.sl / 4.0f; t0 -= T.tl / 4.0f; t1 -= T.tl / 4.0f;
			int ws = T.cms & 2 ? 1 : (T.cms & 1 ? 2 : 0), wt = T.cmt & 2 ? 1 : (T.cmt & 1 ? 2 : 0);
			if (!T.masks) ws = 1;
			if (!T.maskt) wt = 1;
			flags |= (u32) ws << GF_WRAP_S_SHIFT | (u32) wt << GF_WRAP_T_SHIFT;
		}
	}
	float X0 = x0 / (W / 2) - 1, X1 = x1 / (W / 2) - 1, Y0 = 1 - y0 / (H / 2), Y1 = 1 - y1 / (H / 2);
	GVertex q[4];
	float P[4][4] = { { X0, Y0, s0, t0 }, { X1, Y0, s1, t0 }, { X1, Y1, s1, t1 }, { X0, Y1, s0, t1 } };
	for (int k = 0; k < 4; k++)
	{
		q[k].x = P[k][0]; q[k].y = P[k][1]; q[k].z = 0; q[k].w = 1;
		q[k].s = P[k][2] / tw; q[k].t = P[k][3] / th;
		q[k].r = c8 (col[0]); q[k].g = c8 (col[1]); q[k].b = c8 (col[2]); q[k].a = fill ? 255 : c8 (col[3]);
		q[k].r2 = c8 (add[0]); q[k].g2 = c8 (add[1]); q[k].b2 = c8 (add[2]); q[k].a2 = c8 (add[3]);
	}
	GVertex t1v[3] = { q[0], q[2], q[1] }, t2v[3] = { q[0], q[3], q[2] };
	gfxEmit (t1v, texId, flags);
	gfxEmit (t2v, texId, flags);
}

// ---- one command ----------------------------------------------------------------------------------------------------
void Machine::gfxCmd (u32 w0, u32 w1, u32 &pcDl, int &sp, u32 *stack)
{
	(void) sp; (void) stack;
	switch (w0 >> 24)
	{
	case 0x01:								// G_VTX
	{
		int n = (int) ((w0 >> 12) & 0xFF), end = (int) ((w0 >> 1) & 0x7F);
		gfxVtx (seg (w1), n, end - n);
		break;
	}
	case 0x02:								// G_MODIFYVTX
	{
		int where = (int) ((w0 >> 16) & 0xFF), v = (int) ((w0 & 0xFFFF) / 2) & 63;
		if (where == 0x10) { vtx[v].r = (w1 >> 24) / 255.0f; vtx[v].g = ((w1 >> 16) & 255) / 255.0f; vtx[v].b = ((w1 >> 8) & 255) / 255.0f; vtx[v].a = (w1 & 255) / 255.0f; }
		else if (where == 0x14) { vtx[v].s = (float) (s16) (w1 >> 16); vtx[v].t = (float) (s16) w1; }
		break;
	}
	case 0x03:								// G_CULLDL
	{
		int v0 = (int) ((w0 & 0xFFFF) / 2) & 63, v1 = (int) ((w1 & 0xFFFF) / 2) & 63;
		u32 all = 31;
		for (int i = v0; i <= v1; i++) all &= vtx[i].clip;
		if (all & 15) dlEnd = true;					// (the rest of this list is skipped)
		break;
	}
	case 0x04: pcDl = seg (rdpHalf1); break;				// G_BRANCH_Z: (the nearer level of detail)
	case 0x05: gfxTri ((int) ((w0 >> 16) & 0xFF) / 2, (int) ((w0 >> 8) & 0xFF) / 2, (int) (w0 & 0xFF) / 2); break;
	case 0x06:								// G_TRI2
	case 0x07:								// G_QUAD
		gfxTri ((int) ((w0 >> 16) & 0xFF) / 2, (int) ((w0 >> 8) & 0xFF) / 2, (int) (w0 & 0xFF) / 2);
		gfxTri ((int) ((w1 >> 16) & 0xFF) / 2, (int) ((w1 >> 8) & 0xFF) / 2, (int) (w1 & 0xFF) / 2);
		break;
	case 0xD7:								// G_TEXTURE
		texScaleS = (float) (w1 >> 16) / 65536.0f; texScaleT = (float) (w1 & 0xFFFF) / 65536.0f;
		if ((w1 >> 16) == 0xFFFF) texScaleS = 1;
		if ((w1 & 0xFFFF) == 0xFFFF) texScaleT = 1;
		texTile = (int) ((w0 >> 8) & 7);
		texOn = (int) ((w0 >> 1) & 0x7F);
		break;
	case 0xD8:								// G_POPMTX
	{
		int n = (int) (w1 / 64);
		mtxSp -= n; if (mtxSp < 0) mtxSp = 0;
		mvpDirty = true;
		break;
	}
	case 0xD9: geom = (geom & (w0 & 0xFFFFFF)) | w1; break;		// G_GEOMETRYMODE
	case 0xDA:								// G_MTX
	{
		u32 a = seg (w1), param = (w0 & 0xFF) ^ 1;			// (F3DEX2: the push bit inverted)
		float m[16];
		for (int i = 0; i < 16; i++)
			m[i] = (float) (s16) rdH (a + (u32) i * 2) + (float) rdH (a + 32 + (u32) i * 2) / 65536.0f;
		if (param & 4)						// projection
		{
			if (param & 2) for (int i = 0; i < 16; i++) proj[i] = m[i];
			else mtxMul (m, proj, proj);
		}
		else
		{
			if ((param & 1) && mtxSp < 9) { for (int i = 0; i < 16; i++) mtxStack[mtxSp + 1][i] = mtxStack[mtxSp][i]; mtxSp++; }
			if (param & 2) for (int i = 0; i < 16; i++) mtxStack[mtxSp][i] = m[i];
			else mtxMul (m, mtxStack[mtxSp], mtxStack[mtxSp]);
		}
		mvpDirty = true;
		break;
	}
	case 0xDB:								// G_MOVEWORD
	{
		u32 idx = (w0 >> 16) & 0xFF, off = w0 & 0xFFFF;
		if (idx == 6) segment[(off / 4) & 15] = w1 & 0xFFFFFF;
		else if (idx == 2) numLights = (int) (w1 / 24);
		else if (idx == 8) { fogMul = (float) (s16) (w1 >> 16); fogOff = (float) (s16) w1; }
		else if (idx == 10)						// a light's colour
		{
			int l = (int) (off / 24);
			if (l < 8) { lights[l].r = (w1 >> 24) / 255.0f; lights[l].g = ((w1 >> 16) & 255) / 255.0f; lights[l].b = ((w1 >> 8) & 255) / 255.0f; }
		}
		break;
	}
	case 0xDC:								// G_MOVEMEM
	{
		u32 idx = w0 & 0xFF, off = ((w0 >> 8) & 0xFF) * 8, a = seg (w1);
		if (idx == 8)						// the viewport
		{
			for (int i = 0; i < 4; i++) { vpScale[i] = (s16) rdH (a + (u32) i * 2); vpTrans[i] = (s16) rdH (a + 8 + (u32) i * 2); }
		}
		else if (idx == 10 && off >= 48)				// a light (after the two lookat ones)
		{
			int l = (int) ((off - 48) / 24);
			if (l < 8)
			{
				Light &L = lights[l];
				L.r = rdB (a) / 255.0f; L.g = rdB (a + 1) / 255.0f; L.b = rdB (a + 2) / 255.0f;
				float x = (float) (s8) rdB (a + 8), y = (float) (s8) rdB (a + 9), z = (float) (s8) rdB (a + 10);
				float n = __builtin_sqrtf (x * x + y * y + z * z);
				if (n > 0) { x /= n; y /= n; z /= n; }
				L.x = x; L.y = y; L.z = z;
			}
		}
		break;
	}
	case 0xE1: rdpHalf1 = w1; break;
	case 0xF1: rdpHalf2 = w1; break;
	case 0xE2:								// G_SETOTHERMODE_L
	case 0xE3:								// G_SETOTHERMODE_H
	{
		u32 len = (w0 & 0xFF) + 1, sft = 32 - ((w0 >> 8) & 0xFF) - len;
		u32 mask = (len >= 32 ? 0xFFFFFFFFu : ((1u << len) - 1)) << sft;
		u32 &om = (w0 >> 24) == 0xE2 ? omL : omH;
		om = (om & ~mask) | (w1 & mask);
		break;
	}
	case 0xE4:								// TEXRECT (+ RDPHALF_1 / _2 after it)
	case 0xE5:
	{
		u32 h1 = rdW (pcDl + 4), h2 = rdW (pcDl + 12);
		pcDl += 16;
		float x1 = ((w0 >> 12) & 0xFFF) / 4.0f, y1 = (w0 & 0xFFF) / 4.0f;
		float x0 = ((w1 >> 12) & 0xFFF) / 4.0f, y0 = (w1 & 0xFFF) / 4.0f;
		rectTile = (int) ((w1 >> 24) & 7);
		float s = (float) (s16) (h1 >> 16) / 32.0f, t = (float) (s16) h1 / 32.0f;
		float dsdx = (float) (s16) (h2 >> 16) / 1024.0f, dtdy = (float) (s16) h2 / 1024.0f;
		u32 cyc = (omH >> 20) & 3;
		if (cyc == 2) { dsdx /= 4; x1 += 1; y1 += 1; }			// copy mode: 4 texels a step, inclusive
		if ((w0 >> 24) == 0xE5)						// flipped: s down, t across
		{
			gfxRect (x0, y0, x1, y1, s, t, s + (y1 - y0) * dsdx, t + (x1 - x0) * dtdy, true, false);
			break;
		}
		gfxRect (x0, y0, x1, y1, s, t, s + (x1 - x0) * dsdx, t + (y1 - y0) * dtdy, true, false);
		break;
	}
	default: gfxRdp (w0, w1); break;
	}
}

// ---- the RDP commands -----------------------------------------------------------------------------------------------
void Machine::gfxRdp (u32 w0, u32 w1)
{
	auto col = [] (u32 c, float *o) { o[0] = (c >> 24) / 255.0f; o[1] = ((c >> 16) & 255) / 255.0f; o[2] = ((c >> 8) & 255) / 255.0f; o[3] = (c & 255) / 255.0f; };
	switch (w0 >> 24)
	{
	case 0xFF:								// SETCIMG
		cimg = seg (w1); cimgW = (w0 & 0xFFF) + 1; cimgSiz = (w0 >> 19) & 3;
		drawMain = cimg != zimg && cimgW == viW ();
		if (drawMain) gfxFrame[gfxBuild].cimg = cimg;
		break;
	case 0xFE: zimg = seg (w1); drawMain = cimg != zimg && cimgW == viW (); break;	// SETZIMG
	case 0xFD: timg = seg (w1); timgW = (w0 & 0xFFF) + 1; timgSiz = (w0 >> 19) & 3; timgFmt = (w0 >> 21) & 7; break;
	case 0xFC: combH = w0 & 0xFFFFFF; combL = w1; break;
	case 0xFB: col (w1, envC); break;
	case 0xFA: col (w1, primC); break;
	case 0xF9: col (w1, blendC); break;
	case 0xF8: col (w1, fogC); break;
	case 0xF7: fillColor = w1; break;
	case 0xF6:								// FILLRECT
	{
		float x1 = ((w0 >> 12) & 0xFFF) / 4.0f, y1 = (w0 & 0xFFF) / 4.0f, x0 = ((w1 >> 12) & 0xFFF) / 4.0f, y0 = (w1 & 0xFFF) / 4.0f;
		u32 cyc = (omH >> 20) & 3;
		if (cyc == 3) { x1 += 1; y1 += 1; }				// fill mode: inclusive
		if (!drawMain) break;
		GFrame &F = gfxFrame[gfxBuild];
		if (cyc == 3 && x0 <= 0 && y0 <= 0 && x1 >= (float) F.width - 1 && y1 >= (float) F.height - 1)
		{
			// the whole screen: a clear (what was drawn before is covered)
			u32 c = cimgSiz == 3 ? fillColor : fillColor >> 16;
			u32 rgb = cimgSiz == 3 ? c >> 8 : (((c >> 11) & 31) * 255 / 31) << 16 | (((c >> 6) & 31) * 255 / 31) << 8 | ((c >> 1) & 31) * 255 / 31;
			F.clear = rgb; F.nv = 0; F.nb = 0;
			break;
		}
		gfxRect (x0, y0, x1, y1, 0, 0, 0, 0, false, cyc == 3);
		break;
	}
	case 0xF5:								// SETTILE
	{
		Tile &T = tiles[(w1 >> 24) & 7];
		T.fmt = (int) ((w0 >> 21) & 7); T.siz = (int) ((w0 >> 19) & 3); T.line = (int) ((w0 >> 9) & 0x1FF); T.tmem = (int) (w0 & 0x1FF);
		T.pal = (int) ((w1 >> 20) & 15); T.cmt = (int) ((w1 >> 18) & 3); T.maskt = (int) ((w1 >> 14) & 15); T.shiftt = (int) ((w1 >> 10) & 15);
		T.cms = (int) ((w1 >> 8) & 3); T.masks = (int) ((w1 >> 4) & 15); T.shifts = (int) (w1 & 15);
		break;
	}
	case 0xF4: gfxLoad (w0, w1, 1); break;				// LOADTILE
	case 0xF3: gfxLoad (w0, w1, 0); break;				// LOADBLOCK
	case 0xF0: gfxLoad (w0, w1, 2); break;				// LOADTLUT
	case 0xF2:								// SETTILESIZE
	{
		Tile &T = tiles[(w1 >> 24) & 7];
		T.sl = (int) ((w0 >> 12) & 0xFFF); T.tl = (int) (w0 & 0xFFF); T.sh = (int) ((w1 >> 12) & 0xFFF); T.th = (int) (w1 & 0xFFF);
		break;
	}
	case 0xEF: omH = w0 & 0xFFFFFF; omL = w1; break;			// SETOTHERMODE
	case 0xEE: primDepth = (float) (w1 >> 16); break;
	case 0xED:								// SETSCISSOR
		scissor[0] = (int) ((w0 >> 12) & 0xFFF) / 4; scissor[1] = (int) (w0 & 0xFFF) / 4;
		scissor[2] = (int) ((w1 >> 12) & 0xFFF) / 4; scissor[3] = (int) (w1 & 0xFFF) / 4;
		break;
	case 0xE9:								// FULLSYNC: this frame is done
		if (gfxFrame[gfxBuild].nv > 0 || gfxFrame[gfxBuild].clear)
		{
			gfxReady = gfxBuild;
			gfxBuild ^= 1;
			gfxSerial++;
			GFrame &N = gfxFrame[gfxBuild];
			N.nv = N.nb = 0; N.clear = gfxFrame[gfxReady].clear;
			N.width = viW () ? viW () : 320; N.height = N.width * 3 / 4;
		}
		break;
	default: break;
	}
}

} // namespace n64
