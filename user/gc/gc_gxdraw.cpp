//
// gc/gc_gxdraw.cpp -- the GX drawing at a high level, for the GPU (kapi gpu_render): each
// primitive's vertices are decoded (their attributes from the VCD / VAT, direct or indexed in
// the arrays), transformed by the XF (the position / normal matrices, the projection, the
// viewport -> clip space over the EFB), lit (the colour channels: material, ambient, the lights
// with their attenuation), given texture coordinates (the texgens and their matrices), and
// coloured by the TEV evaluated per vertex twice (the texel as black, as white) -> texel x c1 +
// c2, as the N64's combiner. The texture is the first stage's, decoded from any GX format
// (I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, C4 / C8 / C14X2 with their TLUT, CMPR) into a cache.
// The depth mode, the blending, the alpha compare become the batch's state. A copy of the EFB to
// the XFB ends a frame (the next one starts with the copy's clear colour).
//
#include "gc/gc.h"

namespace gc {

static inline u32 be32 (const u8 *p) { return (u32) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static inline u32 be16 (const u8 *p) { return (u32) p[0] << 8 | p[1]; }
static inline float u2f (u32 u) { float f; __builtin_memcpy (&f, &u, 4); return f; }
static inline float clamp01 (float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
static void zero (void *p, u32 n) { u8 *d = (u8 *) p; while (n--) *d++ = 0; }

void Machine::gxInit ()
{
	for (int i = 0; i < 2; i++)
	{
		if (!gfxFrame[i].v) { gfxFrame[i].v = new GVertex[GFrame::MAXV]; gfxFrame[i].b = new GBatch[GFrame::MAXB]; }
		gfxFrame[i].nv = 0; gfxFrame[i].nb = 0; gfxFrame[i].clear = 0; gfxFrame[i].width = 640; gfxFrame[i].height = 480;
	}
	gfxBuild = 0; gfxReady = -1; gfxSerial = 0; texClock = 0; gxClearNext = 0;
	xfbCopyAddr[0] = xfbCopyAddr[1] = 0xFFFFFFFFu;
	texFlush ();
	zero (&gxs, sizeof gxs); for (int i = 0; i < 8; i++) gxs.tex[i] = -1;
	gxsDirty = true; xfSerial = 0;
	zero (efbCopies, sizeof efbCopies);
	if (!tmem) tmem = new u8[0x100000];
	zero (tmem, 0x100000);
}

// ---- the vertices ----------------------------------------------------------------------------------------------
// (decoded into GxVertex: the GPU's as they are, gc_gxgpu.cpp; transformed here for the frames)
static const int kCompSize[8] = { 1, 1, 2, 2, 4, 0, 0, 0 };

// one component of a format (u8, s8, u16, s16, f32), dequantized by 2^-shift
static inline float comp (const u8 *p, int fmt, float scale)
{
	switch (fmt)
	{
	case 0: return p[0] * scale;
	case 1: return (s8) p[0] * scale;
	case 2: return (float) be16 (p) * scale;
	case 3: return (s16) be16 (p) * scale;
	default: return u2f (be32 (p));
	}
}

// a colour (RGB565, RGB888, RGB888x, RGBA4444, RGBA6666, RGBA8888) -> RGBA8
static int colour (const u8 *p, int fmt, u8 *c)
{
	auto x5 = [] (u32 v) { return (u8) (v << 3 | v >> 2); };
	auto x6 = [] (u32 v) { return (u8) (v << 2 | v >> 4); };
	switch (fmt)
	{
	case 0: { u32 v = be16 (p); c[0] = x5 ((v >> 11) & 31); c[1] = x6 ((v >> 5) & 63); c[2] = x5 (v & 31); c[3] = 255; return 2; }
	case 1: c[0] = p[0]; c[1] = p[1]; c[2] = p[2]; c[3] = 255; return 3;
	case 2: c[0] = p[0]; c[1] = p[1]; c[2] = p[2]; c[3] = 255; return 4;
	case 3: { u32 v = be16 (p); c[0] = (u8) ((v >> 12) * 17); c[1] = (u8) (((v >> 8) & 15) * 17); c[2] = (u8) (((v >> 4) & 15) * 17); c[3] = (u8) ((v & 15) * 17); return 2; }
	case 4: { u32 v = (u32) p[0] << 16 | p[1] << 8 | p[2]; c[0] = x6 (v >> 18); c[1] = x6 ((v >> 12) & 63); c[2] = x6 ((v >> 6) & 63); c[3] = x6 (v & 63); return 3; }
	default: c[0] = p[0]; c[1] = p[1]; c[2] = p[2]; c[3] = p[3]; return 4;
	}
}

// ---- the TEV, per vertex ---------------------------------------------------------------------------------------
struct Tev
{
	float reg[4][4];				// PREV, C0, C1, C2
	float konst[4][4];
};

static float kfrac (int sel) { static const float f[8] = { 1.f, 7 / 8.f, 3 / 4.f, 5 / 8.f, 1 / 2.f, 3 / 8.f, 1 / 4.f, 1 / 8.f }; return f[sel & 7]; }

// ---- the textures ------------------------------------------------------------------------------------------------
static inline u32 rgba (u32 r, u32 g, u32 b, u32 a) { return a << 24 | r << 16 | g << 8 | b; }
static inline u32 c565 (u32 v, u32 a = 255) { u32 r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31; return rgba (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2, a); }
static inline u32 c5a3 (u32 v)
{
	if (v & 0x8000) { u32 r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31; return rgba (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255); }
	u32 a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
	return rgba (r * 17, g * 17, b * 17, a << 5 | a << 2 | a >> 1);
}
static inline u32 tlutColour (const u8 *tl, u32 idx, int fmt)
{
	u32 v = be16 (tl + idx * 2);
	if (fmt == 0) return rgba (v & 255, v & 255, v & 255, v >> 8);	// IA8
	if (fmt == 1) return c565 (v);
	return c5a3 (v);
}

// the formats' tiles (texels), their bytes
static const int kTileW[16] = { 8, 8, 8, 4, 4, 4, 4, 0, 8, 8, 4, 0, 0, 0, 8, 0 };
static const int kTileH[16] = { 8, 4, 4, 4, 4, 4, 4, 0, 8, 4, 4, 0, 0, 0, 8, 0 };
static u32 levelBytes (int w, int h, int fmt)
{
	if (fmt > 14 || !kTileW[fmt]) return 0;
	return (u32) ((w + kTileW[fmt] - 1) / kTileW[fmt] * ((h + kTileH[fmt] - 1) / kTileH[fmt])) * (fmt == 6 ? 64 : 32);
}

// decode w x h texels of format fmt at src (the tiles of the GX formats) into out (0xAARRGGBB)
static void decodeTexture (u32 *out, const u8 *src, const u8 *srcEnd, int w, int h, int fmt, const u8 *tlut, int tlfmt)
{
	const int *bw = kTileW, *bh = kTileH;
	if (fmt > 14 || !bw[fmt]) { for (int i = 0; i < w * h; i++) out[i] = 0xFFFF00FF; return; }
	int tw = bw[fmt], th = bh[fmt];
	const u8 *p = src;
	for (int by = 0; by < h; by += th)
		for (int bx = 0; bx < w; bx += tw)
		{
			if (p + 64 > srcEnd) return;
			if (fmt == 14)					// CMPR: 2 x 2 DXT1 blocks of 4 x 4
			{
				for (int sb = 0; sb < 4; sb++, p += 8)
				{
					u32 c0 = be16 (p), c1 = be16 (p + 2), pal[4];
					pal[0] = c565 (c0); pal[1] = c565 (c1);
					u32 r0 = (pal[0] >> 16) & 255, g0 = (pal[0] >> 8) & 255, b0 = pal[0] & 255;
					u32 r1 = (pal[1] >> 16) & 255, g1 = (pal[1] >> 8) & 255, b1 = pal[1] & 255;
					if (c0 > c1) { pal[2] = rgba ((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, 255); pal[3] = rgba ((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, 255); }
					else { pal[2] = rgba ((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255); pal[3] = 0; }
					int ox = bx + (sb & 1) * 4, oy = by + (sb >> 1) * 4;
					for (int y = 0; y < 4; y++)
					{
						u8 row = p[4 + y];
						for (int x = 0; x < 4; x++)
							if (ox + x < w && oy + y < h) out[(oy + y) * w + ox + x] = pal[(row >> (6 - x * 2)) & 3];
					}
				}
				continue;
			}
			if (fmt == 6)					// RGBA8: 2 32-byte halves (AR, then GB)
			{
				for (int y = 0; y < 4; y++)
					for (int x = 0; x < 4; x++)
					{
						int i = y * 4 + x;
						u32 a = p[i * 2], r = p[i * 2 + 1], g = p[32 + i * 2], b = p[32 + i * 2 + 1];
						if (bx + x < w && by + y < h) out[(by + y) * w + bx + x] = rgba (r, g, b, a);
					}
				p += 64;
				continue;
			}
			for (int y = 0; y < th; y++)
				for (int x = 0; x < tw; x++)
				{
					u32 c;
					switch (fmt)
					{
					case 0: { u32 v = (p[(y * tw + x) >> 1] >> ((x & 1) ? 0 : 4)) & 15; v *= 17; c = rgba (v, v, v, v); break; }	// I4
					case 1: { u32 v = p[y * tw + x]; c = rgba (v, v, v, v); break; }					// I8
					case 2: { u32 v = p[y * tw + x], i = (v & 15) * 17, a = (v >> 4) * 17; c = rgba (i, i, i, a); break; }	// IA4
					case 3: { const u8 *q = p + (y * tw + x) * 2; c = rgba (q[1], q[1], q[1], q[0]); break; }		// IA8
					case 4: c = c565 (be16 (p + (y * tw + x) * 2)); break;							// RGB565
					case 5: c = c5a3 (be16 (p + (y * tw + x) * 2)); break;							// RGB5A3
					case 8: { u32 v = (p[(y * tw + x) >> 1] >> ((x & 1) ? 0 : 4)) & 15; c = tlutColour (tlut, v, tlfmt); break; }	// C4
					case 9: c = tlutColour (tlut, p[y * tw + x], tlfmt); break;						// C8
					default: c = tlutColour (tlut, be16 (p + (y * tw + x) * 2) & 0x3FFF, tlfmt); break;			// C14X2
					}
					if (bx + x < w && by + y < h) out[(by + y) * w + bx + x] = c;
				}
			p += 32;
		}
}

// every texture forgotten, the pool empty (the reset; the pool full: the draws of the frame being
// made that read one before may then show another -- a frame, now and then)
void Machine::texFlush ()
{
	for (int i = 0; i < MAX_TEX; i++) { tex[i].px = 0; tex[i].cap = 0; tex[i].w = tex[i].h = 0; tex[i].levels = 1; tex[i].key = 0; tex[i].lastUse = 0; tex[i].dirty = false; }
	for (int i = 0; i < 1024; i++) texFind[i] = -1;
	texPoolTop = 0; texFlushes++;
}

// the texture of map n (0..7): TEXIMAGE0 (size, format), TEXIMAGE3 (address), TEXTLUT; with its
// mipmaps (a GPU's) as far as TX_SETMODE1's maximum LOD, when the minifying filter uses them
int Machine::gxTexture (int map, bool mips)
{
	u32 img0 = bpRegs[(map < 4 ? 0x88 : 0xA8) + (map & 3)], img3 = bpRegs[(map < 4 ? 0x94 : 0xB4) + (map & 3)];
	u32 tl = bpRegs[(map < 4 ? 0x98 : 0xB8) + (map & 3)];
	int w = (int) (img0 & 0x3FF) + 1, h = (int) ((img0 >> 10) & 0x3FF) + 1, fmt = (int) (img0 >> 20) & 15;
	u32 addr = (img3 & 0x00FFFFFF) << 5;
	if (w > 1024 || h > 1024 || addr >= MEM1_SIZE) return -1;
	int tlfmt = (int) (tl >> 10) & 3;
	u32 tloff = (tl & 0x3FF) << 9;
	int levels = 1;
	if (mips)
	{
		u32 m0 = bpRegs[(map < 4 ? 0x80 : 0xA0) + (map & 3)], m1 = bpRegs[(map < 4 ? 0x84 : 0xA4) + (map & 3)];
		int minf = (int) (m0 >> 5) & 7, maxLod = (int) ((m1 >> 8) & 0xFF) >> 4;
		if (minf != 0 && minf != 4)
			while (levels <= maxLod && ((w >> levels) > 0 || (h >> levels) > 0)) levels++;
	}
	// its size in memory, a hash of samples of it (the texture can change: its key follows)
	u32 bytes = 0, texels = 0;
	for (int l = 0; l < levels; l++)
	{
		int lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1;
		bytes += levelBytes (lw, lh, fmt); texels += (u32) (lw * lh);
	}
	if (addr + bytes > MEM1_SIZE) bytes = MEM1_SIZE - addr;
	u64 hsh = 1469598103934665603ull;
	u32 step = bytes / 64 + 1;
	for (u32 i = 0; i < bytes; i += step * 4 > 4 ? step : 1) { hsh ^= mem1[addr + i]; hsh *= 1099511628211ull; }
	if (fmt >= 8 && fmt <= 10) for (u32 i = 0; i < 64; i++) { hsh ^= tmem[(tloff + i * 8) & 0xFFFFF]; hsh *= 1099511628211ull; }
	u64 key = hsh ^ ((u64) addr << 32) ^ (u64) img0 * 0x9E3779B97F4A7C15ull ^ (u64) tl << 20 ^ (u64) levels << 58;
	texClock++;
	int hint = texFind[key & 1023];
	if (hint >= 0 && tex[hint].w && tex[hint].key == key) { tex[hint].lastUse = texClock; return hint; }
	int lru = 0;
	for (int i = 0; i < MAX_TEX; i++)
	{
		if (tex[i].w && tex[i].key == key) { tex[i].lastUse = texClock; texFind[key & 1023] = (s16) i; return i; }
		if (tex[i].lastUse < tex[lru].lastUse) lru = i;
	}
	GTexture &T = tex[lru];
	texDecodes++;
	if (T.cap < (int) texels)					// (its room from the pool: its size, not 4 MB each)
	{
		u32 room = texels < 64 * 64 ? 64 * 64 : texels;
		if (!texPool || room > (u32) TEX_POOL) return -1;
		if (texPoolTop + room > (u32) TEX_POOL) texFlush ();
		T.px = texPool + texPoolTop; T.cap = (int) room; texPoolTop += room;
	}
	texFind[key & 1023] = (s16) lru;
	T.w = w; T.h = h; T.levels = levels; T.key = key; T.lastUse = texClock; T.dirty = true;
	u32 *out = T.px; const u8 *src = mem1 + addr;
	for (int l = 0; l < levels; l++)
	{
		int lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1;
		decodeTexture (out, src, mem1 + MEM1_SIZE, lw, lh, fmt, tmem + (tloff & 0xFFFFF), tlfmt);
		out += lw * lh; src += levelBytes (lw, lh, fmt);
	}
	return lru;
}

// ---- a triangle into the frame being built ----------------------------------------------------------------------
void Machine::gxEmit (const GVertex *v3, int t, u32 flags)
{
	GFrame &F = gfxFrame[gfxBuild];
	if (F.nv + 3 > GFrame::MAXV) return;
	GBatch *last = F.nb ? &F.b[F.nb - 1] : 0;
	if (!last || last->tex != t || last->flags != flags || last->first + last->count != (u32) F.nv)
	{
		if (F.nb >= GFrame::MAXB) return;
		last = &F.b[F.nb++];
		last->first = (u32) F.nv; last->count = 0; last->tex = t; last->flags = flags;
		for (int k = 0; k < 16; k++) last->m[k] = (k % 5) ? 0.f : 1.f;
	}
	for (int i = 0; i < 3; i++) F.v[F.nv++] = v3[i];
	last->count += 3;
}

// ---- a primitive -------------------------------------------------------------------------------------------------
void Machine::gxPrimitive (int prim, int vat, int count, const u8 *data)
{
	if (count <= 0 || (prim >= 5 && !gpu)) return;			// (lines, points: a GPU's only)
	u32 lo = cpRegs[0x50], hi = cpRegs[0x60];
	u32 va = cpRegs[0x70 + vat], vb = cpRegs[0x80 + vat], vc = cpRegs[0x90 + vat];
	auto xfF = [&] (u32 a) { return u2f (xfRegs[a]); };
	u32 mat = xfRegs[0x1018], mat2 = xfRegs[0x1019];
	int vsz = gxVertexSize (vat);
	u32 vcd = (((lo >> 11) & 3) ? GX_VCD_NRM : 0) | (((lo >> 13) & 3) ? GX_VCD_C0 : 0) | (((lo >> 15) & 3) ? GX_VCD_C1 : 0);
	// ---- the vertices ----
	static GxVertex vx[0x10000];
	if (count > 0x10000) count = 0x10000;
	const u8 *p = data;
	for (int i = 0; i < count; i++, p += vsz)
	{
		GxVertex &v = vx[i];
		const u8 *q = p;
		v.mtx[0] = (u8) (mat & 63);
		for (int k = 0; k < 8; k++) v.mtx[1 + k] = (u8) (k < 4 ? (mat >> (6 + 6 * k)) & 63 : (mat2 >> (6 * (k - 4))) & 63);
		if (lo & 1) v.mtx[0] = *q++ & 63;
		for (int k = 0; k < 8; k++) if (lo & (2u << k)) v.mtx[1 + k] = *q++ & 63;
		auto fetch = [&] (int type, int arr, const u8 *&src) -> const u8 *
		{
			if (type == 1) return src;
			u32 idx = type == 2 ? *src : be16 (src);
			src += type == 2 ? 1 : 2;
			u32 a = ((cpRegs[0xA0 + arr] & 0x03FFFFFF) + idx * (cpRegs[0xB0 + arr] & 0xFF)) & 0x01FFFFFF;
			return mem1 + (a < MEM1_SIZE - 64 ? a : 0);
		};
		// the position
		int t = (int) (lo >> 9) & 3;
		v.pos[0] = v.pos[1] = v.pos[2] = 0;
		if (t)
		{
			int fmt = (int) (va >> 1) & 7, n = (va & 1) ? 3 : 2, cs = kCompSize[fmt];
			float sc = fmt == 4 ? 1.f : 1.f / (float) (1u << ((va >> 4) & 31));
			const u8 *src = fetch (t, 0, q);
			for (int c = 0; c < n; c++) v.pos[c] = comp (src + c * cs, fmt, sc);
			if (t == 1) q += n * cs;
		}
		// the normal (the binormal, the tangent: skipped)
		t = (int) (lo >> 11) & 3;
		v.nrm[0] = v.nrm[1] = v.nrm[2] = 0;
		if (t)
		{
			int fmt = (int) (va >> 10) & 7, cs = kCompSize[fmt];
			bool nbt = (va >> 9) & 1, idx3 = (va >> 31) & 1;
			float sc = fmt == 1 ? 1 / 64.f : fmt == 3 ? 1 / 16384.f : 1.f;
			const u8 *src = fetch (t, 1, q);
			for (int c = 0; c < 3; c++) v.nrm[c] = comp (src + c * cs, fmt, sc);
			if (t == 1) q += (nbt ? 9 : 3) * cs;
			else if (nbt && idx3) q += t == 2 ? 2 : 4;
		}
		// the colours
		for (int k = 0; k < 2; k++)
		{
			u8 *c = k ? v.c1 : v.c0;
			c[0] = c[1] = c[2] = c[3] = 255;
			t = (int) (lo >> (13 + 2 * k)) & 3;
			if (!t) continue;
			const u8 *src = fetch (t, 2 + k, q);
			int n = colour (src, (int) (va >> (14 + 4 * k)) & 7, c);
			if (t == 1) q += n;
		}
		// the texture coordinates
		static const int cntBit[8] = { 21, 0, 9, 18, 27, 5, 14, 23 };
		static const int fmtBit[8] = { 22, 1, 10, 19, 28, 6, 15, 24 };
		static const int shBit[8] = { 25, 4, 13, 22, 0, 9, 18, 27 };
		static const int reg[8] = { 0, 1, 1, 1, 2, 2, 2, 2 };
		static const int regFmt[8] = { 0, 1, 1, 1, 1, 2, 2, 2 };
		for (int k = 0; k < 8; k++)
		{
			t = (int) (hi >> (2 * k)) & 3;
			v.tc[k][0] = v.tc[k][1] = 0;
			if (!t) continue;
			u32 rc = regFmt[k] == 0 ? va : regFmt[k] == 1 ? vb : vc;
			u32 rs = reg[k] == 0 ? va : reg[k] == 1 ? vb : vc;
			int fmt = (int) (rc >> fmtBit[k]) & 7, n = ((rc >> cntBit[k]) & 1) ? 2 : 1, cs = kCompSize[fmt];
			float sc = fmt == 4 ? 1.f : 1.f / (float) (1u << ((rs >> shBit[k]) & 31));
			const u8 *src = fetch (t, 4 + k, q);
			for (int c = 0; c < n; c++) v.tc[k][c] = comp (src + c * cs, fmt, sc);
			if (t == 1) q += n * cs;
		}
	}
	if (gpu) { gxGpuPrimitive (prim, count, vx, vcd); return; }
	// ---- this draw's state ----
	u32 genmode = bpRegs[0x00];
	int nStages = (int) ((genmode >> 10) & 15) + 1;
	// the texture: the first stage that uses one
	int texMap = -1, texCoord = 0;
	for (int s = 0; s < nStages; s++)
	{
		u32 tref = bpRegs[0x28 + s / 2] >> ((s & 1) ? 12 : 0);
		if (tref & 0x40) { texMap = (int) (tref & 7); texCoord = (int) ((tref >> 3) & 7); break; }
	}
	int texIdx = texMap >= 0 ? gxTexture (texMap) : -1;
	u32 flags = GF_NOMATRIX;
	u32 zmode = bpRegs[0x40];
	if (!(zmode & 1)) flags |= GF_ZALWAYS | GF_NOZWRITE;
	else
	{
		static const u32 zmap[8] = { 7, 0, 2, 3, 4, 5, 6, 7 };	// (never: always, rare)
		flags |= zmap[(zmode >> 1) & 7];
		if (!(zmode & 0x10)) flags |= GF_NOZWRITE;
	}
	u32 bm = bpRegs[0x41];
	if (bm & 1)							// blending: the usual pairs
	{
		int src = (int) (bm >> 8) & 7, dst = (int) (bm >> 5) & 7, mode = 0;
		if (src == 4 && dst == 5) mode = 1;			// alpha
		else if ((src == 4 || src == 1) && dst == 1) mode = 2;	// add
		else if ((src == 0 && dst == 2) || (src == 2 && dst == 0)) mode = 3;	// multiply
		else if (src == 1 && dst == 5) mode = 4;		// premultiplied
		flags |= (u32) mode << GF_BLEND_SHIFT;
	}
	u32 ac = bpRegs[0xF3];						// the alpha compare
	{
		int c0 = (int) (ac >> 16) & 7, c1 = (int) (ac >> 19) & 7, ref0 = (int) ac & 255, ref1 = (int) (ac >> 8) & 255;
		int c = c0, ref = ref0;
		if (c0 == 7 && c1 != 7) { c = c1; ref = ref1; }
		if (c == 4 && ref < 255) flags |= GF_ALPHATEST | (u32) (ref + 1) << 19;	// greater
		else if (c == 6 && ref > 0) flags |= GF_ALPHATEST | (u32) ref << 19;	// greater or equal
	}
	if (texIdx >= 0)
	{
		u32 tm0 = bpRegs[(texMap < 4 ? 0x80 : 0xA0) + (texMap & 3)];
		static const u32 wrap[4] = { 1, 0, 2, 0 };		// GX: clamp, repeat, mirror -> ours
		flags |= wrap[tm0 & 3] << GF_WRAP_S_SHIFT | wrap[(tm0 >> 2) & 3] << GF_WRAP_T_SHIFT;
		if (tm0 & 0x10) flags |= GF_LINEAR;
	}
	// ---- the TEV's registers, the konst colours ----
	Tev tv;
	for (int r = 0; r < 4; r++)
	{
		u32 ra = bpRegs[0xE0 + r * 2], bg = bpRegs[0xE1 + r * 2];
		auto s11 = [] (u32 v) { int x = (int) (v & 0x7FF); if (x & 0x400) x -= 0x800; return x / 255.f; };
		tv.reg[r][0] = s11 (ra); tv.reg[r][3] = s11 (ra >> 12); tv.reg[r][2] = s11 (bg); tv.reg[r][1] = s11 (bg >> 12);

	}
	for (int r = 0; r < 4; r++)
	{
		u32 ra = bpKonst[r * 2], bg = bpKonst[r * 2 + 1];
		tv.konst[r][0] = (ra & 0xFF) / 255.f; tv.konst[r][3] = ((ra >> 12) & 0xFF) / 255.f; tv.konst[r][2] = (bg & 0xFF) / 255.f; tv.konst[r][1] = ((bg >> 12) & 0xFF) / 255.f;
	}
	// ---- per vertex: transform, light, texgen, TEV ----
	static GVertex out[0x10000];
	int efbW = (int) (bpRegs[0x4A] & 0x3FF) + 1, efbH = (int) ((bpRegs[0x4A] >> 10) & 0x3FF) + 1;
	if (efbW < 16) efbW = 640;
	if (efbH < 16) efbH = 480;
	float sx = xfF (0x101A), sy = xfF (0x101B), ox = xfF (0x101D) - 342.f, oy = xfF (0x101E) - 342.f;
	float pr[6]; for (int k = 0; k < 6; k++) pr[k] = xfF (0x1020 + (u32) k);
	bool ortho = xfRegs[0x1026] & 1;
	u32 numChans = xfRegs[0x1009] & 3;
	for (int i = 0; i < count; i++)
	{
		const GxVertex &v = vx[i];
		const bool hasN = vcd & GX_VCD_NRM, hasC[2] = { (vcd & GX_VCD_C0) != 0, (vcd & GX_VCD_C1) != 0 };
		float vcolour[2][4];
		for (int c = 0; c < 4; c++) { vcolour[0][c] = v.c0[c] / 255.f; vcolour[1][c] = v.c1[c] / 255.f; }
		// the position matrix (3 x 4 at pm * 4), the normal matrix (3 x 3 at 0x400 + pm % 32 * 3)
		u32 m = (u32) v.mtx[0] * 4;
		float e[3];
		for (int r = 0; r < 3; r++) e[r] = xfF (m + r * 4) * v.pos[0] + xfF (m + r * 4 + 1) * v.pos[1] + xfF (m + r * 4 + 2) * v.pos[2] + xfF (m + r * 4 + 3);
		float nrm[3] = { 0, 0, 1 };
		if (hasN)
		{
			u32 nm = 0x400 + ((u32) v.mtx[0] & 31) * 3;
			for (int r = 0; r < 3; r++) nrm[r] = xfF (nm + r * 3) * v.nrm[0] + xfF (nm + r * 3 + 1) * v.nrm[1] + xfF (nm + r * 3 + 2) * v.nrm[2];
			float l = __builtin_sqrtf (nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
			if (l > 0) { nrm[0] /= l; nrm[1] /= l; nrm[2] /= l; }
		}
		// the projection -> GX clip space (z in -w .. 0), then over the EFB -> the GPU's clip space
		float xc, yc, zc, wc;
		if (ortho) { xc = pr[0] * e[0] + pr[1]; yc = pr[2] * e[1] + pr[3]; zc = pr[4] * e[2] + pr[5]; wc = 1.f; }
		else { xc = pr[0] * e[0] + pr[1] * e[2]; yc = pr[2] * e[1] + pr[3] * e[2]; zc = pr[4] * e[2] + pr[5]; wc = -e[2]; }
		GVertex &o = out[i];
		o.x = (xc * sx + ox * wc) * 2.f / (float) efbW - wc;
		o.y = wc - (yc * sy + oy * wc) * 2.f / (float) efbH;
		o.z = 2.f * zc + wc;
		o.w = wc;
		// the colour channels: material x (ambient + the lights), or the vertex's colour
		float chan[2][4];
		for (int k = 0; k < 2; k++)
		{
			for (int a = 0; a < 2; a++)				// a: colour (rgb) / alpha control
			{
				u32 ctl = xfRegs[0x100E + (u32) k + (u32) a * 2];
				u32 mc = xfRegs[0x100C + (u32) k], am = xfRegs[0x100A + (u32) k];
				float mat4[4] = { (mc >> 24) / 255.f, ((mc >> 16) & 255) / 255.f, ((mc >> 8) & 255) / 255.f, (mc & 255) / 255.f };
				float amb4[4] = { (am >> 24) / 255.f, ((am >> 16) & 255) / 255.f, ((am >> 8) & 255) / 255.f, (am & 255) / 255.f };
				const float *vcol = hasC[k] ? vcolour[k] : (hasC[0] ? vcolour[0] : 0);
				float matv[4], ambv[4];
				for (int c = 0; c < 4; c++) { matv[c] = (ctl & 1) && vcol ? vcol[c] : mat4[c]; ambv[c] = (ctl & 0x40) && vcol ? vcol[c] : amb4[c]; }
				float res[4];
				if (ctl & 2)					// lighting on
				{
					float acc[4] = { ambv[0], ambv[1], ambv[2], ambv[3] };
					u32 mask = ((ctl >> 2) & 15) | ((ctl >> 7) & 0xF0);
					for (int L = 0; L < 8; L++)
					{
						if (!(mask & (1u << L))) continue;
						u32 lb = 0x600 + (u32) L * 16;
						u32 lc = xfRegs[lb + 3];
						float lcol[4] = { (lc >> 24) / 255.f, ((lc >> 16) & 255) / 255.f, ((lc >> 8) & 255) / 255.f, (lc & 255) / 255.f };
						float lp[3] = { xfF (lb + 10), xfF (lb + 11), xfF (lb + 12) };
						float ld[3] = { lp[0] - e[0], lp[1] - e[1], lp[2] - e[2] };
						float dist = __builtin_sqrtf (ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);
						if (dist > 0) { ld[0] /= dist; ld[1] /= dist; ld[2] /= dist; }
						float diff = nrm[0] * ld[0] + nrm[1] * ld[1] + nrm[2] * ld[2];
						u32 datt = (ctl >> 7) & 3;
						if (datt == 2) diff = diff < 0 ? 0 : diff;
						else if (datt == 0) diff = 1.f;
						float att = 1.f;
						if (ctl & 0x200)				// the attenuation
						{
							float a0 = xfF (lb + 4), a1 = xfF (lb + 5), a2 = xfF (lb + 6), k0 = xfF (lb + 7), k1 = xfF (lb + 8), k2 = xfF (lb + 9);
							if (ctl & 0x400)			// spot: the angle and the distance
							{
								float dir[3] = { xfF (lb + 13), xfF (lb + 14), xfF (lb + 15) };
								float cs = -(ld[0] * dir[0] + ld[1] * dir[1] + ld[2] * dir[2]);
								float aa = a0 + a1 * cs + a2 * cs * cs; if (aa < 0) aa = 0;
								float dd = k0 + k1 * dist + k2 * dist * dist;
								att = dd > 0 ? aa / dd : aa;
							}
							else					// specular: the half-angle
							{
								float cs = nrm[0] * xfF (lb + 13) + nrm[1] * xfF (lb + 14) + nrm[2] * xfF (lb + 15);
								if (cs < 0) cs = 0;
								float aa = a0 + a1 * cs + a2 * cs * cs, dd = k0 + k1 * cs + k2 * cs * cs;
								att = dd > 0 ? aa / dd : aa;
							}
						}
						for (int c = 0; c < 4; c++) acc[c] += lcol[c] * diff * att;
					}
					for (int c = 0; c < 4; c++) res[c] = matv[c] * clamp01 (acc[c]);
				}
				else for (int c = 0; c < 4; c++) res[c] = matv[c];
				if (a == 0) { chan[k][0] = res[0]; chan[k][1] = res[1]; chan[k][2] = res[2]; }
				else chan[k][3] = res[3];
			}
		}
		if (numChans == 0) for (int k = 0; k < 2; k++) for (int c = 0; c < 4; c++) chan[k][c] = 1.f;
		// the texture coordinate of the texture used: its texgen (a matrix x the source)
		float s = 0, tt = 0;
		if (texIdx >= 0)
		{
			u32 tg = xfRegs[0x1040 + (u32) texCoord];
			int src = (int) (tg >> 7) & 31, type = (int) (tg >> 4) & 7;
			float in[4] = { 0, 0, 1, 1 };
			if (src == 0) { in[0] = v.pos[0]; in[1] = v.pos[1]; in[2] = v.pos[2]; }
			else if (src == 1 && hasN) { in[0] = v.nrm[0]; in[1] = v.nrm[1]; in[2] = v.nrm[2]; }
			else if (src >= 5 && src <= 12) { in[0] = v.tc[src - 5][0]; in[1] = v.tc[src - 5][1]; in[2] = 1; }
			if (!((tg >> 2) & 1) && src >= 5) in[2] = 1.f;	// AB11
			if (type == 0)
			{
				u32 tm = (u32) v.mtx[1 + texCoord] * 4;
				bool stq = (tg >> 1) & 1;
				float r0 = xfF (tm) * in[0] + xfF (tm + 1) * in[1] + xfF (tm + 2) * in[2] + xfF (tm + 3) * in[3];
				float r1 = xfF (tm + 4) * in[0] + xfF (tm + 5) * in[1] + xfF (tm + 6) * in[2] + xfF (tm + 7) * in[3];
				float r2 = stq ? xfF (tm + 8) * in[0] + xfF (tm + 9) * in[1] + xfF (tm + 10) * in[2] + xfF (tm + 11) * in[3] : 1.f;
				if (r2 != 0.f && r2 != 1.f) { r0 /= r2; r1 /= r2; }
				s = r0; tt = r1;
			}
			else { s = in[0]; tt = in[1]; }
		}
		o.s = s; o.t = tt;
		// the TEV: its stages evaluated with the texel black (-> c2) and white (-> c1 + c2)
		float res[2][4];
		for (int pass = 0; pass < 2; pass++)
		{
			float tex4 = (float) pass;
			float R[4][4];
			for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) R[r][c] = tv.reg[r][c];
			for (int st = 0; st < nStages; st++)
			{
				u32 cenv = bpRegs[0xC0 + st * 2], aenv = bpRegs[0xC1 + st * 2];
				u32 tref = bpRegs[0x28 + st / 2] >> ((st & 1) ? 12 : 0);
				int chanSel = (int) (tref >> 7) & 7;
				const float *ras = chanSel == 0 ? chan[0] : chanSel == 1 ? chan[1] : 0;
				float rasc[4] = { 0, 0, 0, 0 };
				if (ras) for (int c = 0; c < 4; c++) rasc[c] = ras[c];
				bool hasTex = (tref & 0x40) && (int) (tref & 7) == texMap;
				float tx[4] = { hasTex ? tex4 : 1.f, hasTex ? tex4 : 1.f, hasTex ? tex4 : 1.f, hasTex ? tex4 : 1.f };
				u32 ksel = bpRegs[0xF6 + st / 2] >> ((st & 1) ? 14 : 4);
				int kc = (int) ksel & 31, ka = (int) (ksel >> 5) & 31;
				float k4[4];
				auto kpick = [&] (int sel, int comp) -> float
				{
					if (sel < 8) return kfrac (sel);
					if (sel >= 12 && sel < 16) return tv.konst[sel - 12][comp];
					if (sel >= 16) { int kk = (sel - 16) & 3, which = (sel - 16) >> 2; return tv.konst[kk][which < 3 ? which : 3]; }
					return 1.f;
				};
				for (int c = 0; c < 3; c++) k4[c] = kpick (kc, c);
				k4[3] = kpick (ka, 3);
				auto carg = [&] (int sel, float *o3)
				{
					switch (sel)
					{
					case 0: for (int c = 0; c < 3; c++) o3[c] = R[0][c]; break;
					case 1: for (int c = 0; c < 3; c++) o3[c] = R[0][3]; break;
					case 2: case 4: case 6: for (int c = 0; c < 3; c++) o3[c] = R[sel / 2][c]; break;
					case 3: case 5: case 7: for (int c = 0; c < 3; c++) o3[c] = R[sel / 2][3]; break;
					case 8: for (int c = 0; c < 3; c++) o3[c] = tx[c]; break;
					case 9: for (int c = 0; c < 3; c++) o3[c] = tx[3]; break;
					case 10: for (int c = 0; c < 3; c++) o3[c] = rasc[c]; break;
					case 11: for (int c = 0; c < 3; c++) o3[c] = rasc[3]; break;
					case 12: for (int c = 0; c < 3; c++) o3[c] = 1.f; break;
					case 13: for (int c = 0; c < 3; c++) o3[c] = 0.5f; break;
					case 14: for (int c = 0; c < 3; c++) o3[c] = k4[c]; break;
					default: for (int c = 0; c < 3; c++) o3[c] = 0.f; break;
					}
				};
				auto aarg = [&] (int sel) -> float
				{
					switch (sel)
					{
					case 0: return R[0][3];
					case 1: return R[1][3];
					case 2: return R[2][3];
					case 3: return R[3][3];
					case 4: return tx[3];
					case 5: return rasc[3];
					case 6: return k4[3];
					default: return 0.f;
					}
				};
				static const float scaleTab[4] = { 1.f, 2.f, 4.f, 0.5f };
				static const float biasTab[4] = { 0.f, 0.5f, -0.5f, 0.f };
				// the colour
				float A[3], B[3], C[3], D[3], rc[3];
				carg ((int) (cenv >> 12) & 15, A); carg ((int) (cenv >> 8) & 15, B); carg ((int) (cenv >> 4) & 15, C); carg ((int) cenv & 15, D);
				int bias = (int) (cenv >> 16) & 3;
				for (int c = 0; c < 3; c++)
				{
					float lerp = (1.f - C[c]) * A[c] + C[c] * B[c];
					float r = bias == 3 ? D[c] : (D[c] + ((cenv >> 18) & 1 ? -lerp : lerp) + biasTab[bias]) * scaleTab[(cenv >> 20) & 3];
					rc[c] = (cenv >> 19) & 1 ? clamp01 (r) : r;
				}
				int cd = (int) (cenv >> 22) & 3;
				// the alpha
				float aA = aarg ((int) (aenv >> 13) & 7), aB = aarg ((int) (aenv >> 10) & 7), aC = aarg ((int) (aenv >> 7) & 7), aD = aarg ((int) (aenv >> 4) & 7);
				int abias = (int) (aenv >> 16) & 3;
				float al = (1.f - aC) * aA + aC * aB;
				float ra = abias == 3 ? aD : (aD + ((aenv >> 18) & 1 ? -al : al) + biasTab[abias]) * scaleTab[(aenv >> 20) & 3];
				if ((aenv >> 19) & 1) ra = clamp01 (ra);
				int ad = (int) (aenv >> 22) & 3;
				for (int c = 0; c < 3; c++) R[cd][c] = rc[c];
				R[ad][3] = ra;
			}
			for (int c = 0; c < 4; c++) res[pass][c] = clamp01 (R[0][c]);
		}
		auto b8 = [] (float f) { int v = (int) (f * 255.f + 0.5f); return (u8) (v < 0 ? 0 : v > 255 ? 255 : v); };
		if (texIdx >= 0)
		{
			o.r = b8 (res[1][0] - res[0][0]); o.g = b8 (res[1][1] - res[0][1]); o.b = b8 (res[1][2] - res[0][2]); o.a = b8 (res[1][3] - res[0][3]);
			o.r2 = b8 (res[0][0]); o.g2 = b8 (res[0][1]); o.b2 = b8 (res[0][2]); o.a2 = b8 (res[0][3]);
		}
		else
		{
			o.r = b8 (res[0][0]); o.g = b8 (res[0][1]); o.b = b8 (res[0][2]); o.a = b8 (res[0][3]);
			o.r2 = o.g2 = o.b2 = o.a2 = 0;
		}
	}
	// ---- the primitive's triangles ----
	GVertex t3[3];
	auto tri = [&] (int a, int b, int c) { t3[0] = out[a]; t3[1] = out[b]; t3[2] = out[c]; gxEmit (t3, texIdx, flags); };
	switch (prim)
	{
	case 0: for (int i = 0; i + 3 < count; i += 4) { tri (i, i + 1, i + 2); tri (i, i + 2, i + 3); } break;	// quads
	case 1: for (int i = 0; i + 3 < count; i += 4) { tri (i, i + 1, i + 2); tri (i, i + 2, i + 3); } break;
	case 2: for (int i = 0; i + 2 < count; i += 3) tri (i, i + 1, i + 2); break;					// triangles
	case 3: for (int i = 0; i + 2 < count; i++) { if (i & 1) tri (i + 1, i, i + 2); else tri (i, i + 1, i + 2); } break;	// strip
	case 4: for (int i = 1; i + 1 < count; i++) tri (0, i, i + 1); break;						// fan
	}
}

// BP 0x52: an EFB copy. To the XFB (bit 14): the frame is finished; bit 11: the EFB is cleared
// after (the next frame starts with the clear colour, BP 0x4F / 0x50). A GPU does them all.
void Machine::gxCopy (u32 v)
{
	gxCopies++;
	if (gpu) gxGpuCopy (v);
	if (!(v & 0x4000)) return;					// (a copy to a texture: the GPU's only)
	u32 dst = (bpRegs[0x4B] & 0xFFFFFF) << 5;			// (the XFB it goes to: its picture is this frame's)
	if (dst != xfbCopyAddr[0]) { xfbCopyAddr[1] = xfbCopyAddr[0]; xfbCopyAddr[0] = dst; }
	if (gpu) { gfxReady = 0; gfxSerial++; return; }		// (the picture: the GPU's)
	GFrame &F = gfxFrame[gfxBuild];
	F.width = (int) (bpRegs[0x4A] & 0x3FF) + 1; F.height = (int) ((bpRegs[0x4A] >> 10) & 0x3FF) + 1;
	if (F.nv == 0 && gfxReady >= 0 && !(v & 0x800)) return;	// (an empty copy)
	gfxReady = gfxBuild; gfxSerial++;
	gfxBuild ^= 1;
	GFrame &N = gfxFrame[gfxBuild];
	N.nv = 0; N.nb = 0;
	if (v & 0x800)
	{
		u32 ar = bpRegs[0x4F], gb = bpRegs[0x50];
		gxClearNext = (ar & 0xFF) << 16 | ((gb >> 8) & 0xFF) << 8 | (gb & 0xFF);
	}
	N.clear = gxClearNext;
}

} // namespace gc
