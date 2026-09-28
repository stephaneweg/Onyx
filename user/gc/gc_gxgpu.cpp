//
// gc/gc_gxgpu.cpp -- the GX for a GPU with shaders (Machine::gpu, a front end's): each primitive's
// vertices as the game gave them (model space, their matrix indices; gc_gxdraw.cpp decodes them),
// its triangles / lines / points as indices, and the state it is drawn with -- the XF's (the colour
// channels, the texgens, the projection) and the BP's (the TEV stages, the konst colours, the alpha
// test, the z texture, the fog, the depth / blending modes, the scissor, the textures of the maps
// the stages use) as GxState, rebuilt when a register changed. The EFB copies go to the GPU too: to
// the XFB (the picture), or to a texture -- kept by the GPU, not written to MEM1: a slot here remembers
// where it went and what MEM1 held there then, so that a texture read from that address (MEM1 not
// written since) is the copy (Dolphin's "EFB copies to texture only").
//
#include "gc/gc.h"
#include <stddef.h>

namespace gc {

static_assert (offsetof (GxState, zmode) == GX_UBO_BYTES, "the uniform block's size");

static inline float u2f (u32 u) { float f; __builtin_memcpy (&f, &u, 4); return f; }
static inline s32 s11 (u32 v) { s32 x = (s32) (v & 0x7FF); return x & 0x400 ? x - 0x800 : x; }
// the fog's a and c: sign 19, exponent 11-18, mantissa 0-10 (a float's top bits)
static inline float fogFloat (u32 v) { return u2f (((v >> 19) & 1) << 31 | ((v >> 11) & 0xFF) << 23 | (v & 0x7FF) << 12); }

// a texture format's bytes for w x h texels (its tiles: 32 bytes, RGBA8's 64)
static u32 texBytes (int w, int h, int fmt)
{
	static const int tw[16] = { 8, 8, 8, 4, 4, 4, 4, 0, 8, 8, 4, 0, 0, 0, 8, 0 };
	static const int th[16] = { 8, 4, 4, 4, 4, 4, 4, 0, 8, 4, 4, 0, 0, 0, 8, 0 };
	if (fmt < 0 || fmt > 14 || !tw[fmt]) return 0;
	return (u32) ((w + tw[fmt] - 1) / tw[fmt] * ((h + th[fmt] - 1) / th[fmt])) * (fmt == 6 ? 64 : 32);
}

// the texture format an EFB copy's format writes (EFBCopyFormat -> I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8)
static int copyTexFormat (u32 fmt)
{
	static const int t[16] = { 0, 1, 2, 3, 4, 5, 6, 1, 1, 1, 1, 3, 3, 3, 3, 6 };
	return t[fmt & 15];
}

// samples of MEM1 there (what a copy's slot remembers: MEM1 not written since, the copy is the texture)
static u64 memHash (const u8 *mem, u32 addr, u32 bytes)
{
	if (addr >= MEM1_SIZE) return 0;
	if (addr + bytes > MEM1_SIZE) bytes = MEM1_SIZE - addr;
	u64 h = 1469598103934665603ull ^ bytes;
	u32 step = bytes / 64 + 1;
	for (u32 i = 0; i < bytes; i += step) { h ^= mem[addr + i]; h *= 1099511628211ull; }
	return h;
}

// ---- the state -----------------------------------------------------------------------------------------------
void Machine::gxGpuState (u32 vcd)
{
	GxState &s = gxs;
	if (gxsDirty || (u32) s.vtx[0] != vcd)
	{
		gxsDirty = false;
		// the vertex stage
		s.vtx[0] = (s32) vcd; s.vtx[1] = (s32) (xfRegs[0x1009] & 3); s.vtx[2] = (s32) (xfRegs[0x103F] & 15); s.vtx[3] = (s32) (xfRegs[0x1012] & 1);
		for (int k = 0; k < 4; k++) s.chan[k] = (s32) xfRegs[0x100E + k];
		s.matAmb[0] = xfRegs[0x100C]; s.matAmb[1] = xfRegs[0x100D]; s.matAmb[2] = xfRegs[0x100A]; s.matAmb[3] = xfRegs[0x100B];
		for (int i = 0; i < 8; i++)
		{
			s.texgen[i][0] = (s32) xfRegs[0x1040 + i]; s.texgen[i][1] = (s32) xfRegs[0x1050 + i]; s.texgen[i][2] = s.texgen[i][3] = 0;
		}
		for (int k = 0; k < 6; k++) s.proj[k] = u2f (xfRegs[0x1020 + k]);
		s.proj[6] = (float) (xfRegs[0x1026] & 1); s.proj[7] = 0;
		// the viewport: the EFB's rectangle (the XF's has the scissor offset's 342 in it), the depth range
		float wd = u2f (xfRegs[0x101A]), ht = u2f (xfRegs[0x101B]), zr = u2f (xfRegs[0x101C]);
		float xo = u2f (xfRegs[0x101D]), yo = u2f (xfRegs[0x101E]), fz = u2f (xfRegs[0x101F]);
		u32 so = bpRegs[0x59];
		float xoff = (float) ((so & 0x3FF) * 2), yoff = (float) (((so >> 10) & 0x3FF) * 2);
		float aw = wd < 0 ? -wd : wd, ah = ht < 0 ? -ht : ht;
		s.viewport[0] = xo - aw - xoff; s.viewport[1] = yo - ah - yoff; s.viewport[2] = 2 * aw; s.viewport[3] = 2 * ah;
		s.viewport[4] = (fz - zr) / 16777216.f; s.viewport[5] = fz / 16777216.f;
		s.vp[0] = wd < 0 ? -1.f : 1.f; s.vp[1] = ht < 0 ? -1.f : 1.f; s.vp[2] = s.vp[3] = 0;
		// the TEV
		u32 gm = bpRegs[0x00];
		s.gen[0] = (s32) ((gm >> 10) & 15) + 1; s.gen[1] = (s32) ((gm >> 16) & 7); s.gen[2] = (s32) ((bpRegs[0xF3] >> 22) & 3); s.gen[3] = (s32) (bpRegs[0x43] & 7);
		for (int st = 0; st < 16; st++)
		{
			s.tevC[st] = (s32) bpRegs[0xC0 + st * 2]; s.tevA[st] = (s32) bpRegs[0xC1 + st * 2];
			s.tref[st] = (s32) ((bpRegs[0x28 + st / 2] >> ((st & 1) ? 12 : 0)) & 0x3FF);
			s.ksel[st] = (s32) ((bpRegs[0xF6 + st / 2] >> ((st & 1) ? 14 : 4)) & 0x3FF);
			s.ind[st] = (s32) bpRegs[0x10 + st];
		}
		for (int t = 0; t < 4; t++)
		{
			u32 a = bpRegs[0xF6 + t * 2], b = bpRegs[0xF7 + t * 2];
			s.swap[t * 4] = (s32) (a & 3); s.swap[t * 4 + 1] = (s32) ((a >> 2) & 3); s.swap[t * 4 + 2] = (s32) (b & 3); s.swap[t * 4 + 3] = (s32) ((b >> 2) & 3);
		}
		for (int r = 0; r < 4; r++)
		{
			u32 ra = bpRegs[0xE0 + r * 2], bg = bpRegs[0xE1 + r * 2];
			s.regs[r * 4] = s11 (ra); s.regs[r * 4 + 3] = s11 (ra >> 12); s.regs[r * 4 + 2] = s11 (bg); s.regs[r * 4 + 1] = s11 (bg >> 12);
			u32 ka = bpKonst[r * 2], kb = bpKonst[r * 2 + 1];
			s.konst[r * 4] = (s32) (ka & 0xFF); s.konst[r * 4 + 3] = (s32) ((ka >> 12) & 0xFF); s.konst[r * 4 + 2] = (s32) (kb & 0xFF); s.konst[r * 4 + 1] = (s32) ((kb >> 12) & 0xFF);
		}
		u32 ac = bpRegs[0xF3];
		s.alpha[0] = (s32) ((ac >> 16) & 7); s.alpha[1] = (s32) ((ac >> 19) & 7); s.alpha[2] = (s32) (ac & 0xFF); s.alpha[3] = (s32) ((ac >> 8) & 0xFF);
		s.zenv[0] = (s32) ((bpRegs[0xF5] >> 2) & 3); s.zenv[1] = (s32) (bpRegs[0xF5] & 3); s.zenv[2] = (s32) (bpRegs[0xF4] & 0xFFFFFF);
		s.zenv[3] = (bpRegs[0x42] & 0x100) ? (s32) (0x100 | (bpRegs[0x42] & 0xFF)) : 0;
		// the fog
		u32 f3 = bpRegs[0xF1];
		s.fogI[0] = (s32) ((f3 >> 21) & 7); s.fogI[1] = (s32) ((f3 >> 20) & 1); s.fogI[2] = (s32) (bpRegs[0xEF] & 0xFFFFFF); s.fogI[3] = (s32) (bpRegs[0xF0] & 0x1F);
		s.fogF[0] = fogFloat (bpRegs[0xEE]); s.fogF[1] = fogFloat (f3);
		{
			u32 fr = bpRegs[0xE8];
			float centre = (float) ((s32) (fr & 0x3FF) - 342);
			s.fogF[2] = aw > 0 ? centre / (2 * aw) * 2 - 1 : 0; s.fogF[3] = 2 * aw;
			for (int i = 0; i < 10; i++)
			{
				u32 k = bpRegs[0xE9 + i / 2];
				s.fogK[i] = (float) ((i & 1) ? (k & 0xFFF) : ((k >> 12) & 0xFFF)) / 256.f;
			}
			s.fogK[10] = (fr & 0x400) ? 1.f : 0.f; s.fogK[11] = 0;
		}
		u32 fc = bpRegs[0xF2];
		s.fogColor[0] = (float) ((fc >> 16) & 0xFF); s.fogColor[1] = (float) ((fc >> 8) & 0xFF); s.fogColor[2] = (float) (fc & 0xFF); s.fogColor[3] = 0;
		// the indirect texturing
		for (int k = 0; k < 9; k++) s.indMtx[k] = (s32) bpRegs[0x06 + k];
		s.indMtx[9] = s.indMtx[10] = s.indMtx[11] = 0;
		s.indRef[0] = (s32) bpRegs[0x27]; s.indRef[1] = (s32) bpRegs[0x25]; s.indRef[2] = (s32) bpRegs[0x26]; s.indRef[3] = 0;
		// the coordinates: the rasterizer's scale (its texels), projected (STQ)
		for (int i = 0; i < 8; i++)
		{
			s.tcScale[i][0] = (float) ((bpRegs[0x30 + i * 2] & 0xFFFF) + 1); s.tcScale[i][1] = (float) ((bpRegs[0x31 + i * 2] & 0xFFFF) + 1);
			s.tcScale[i][2] = (xfRegs[0x1040 + i] & 2) ? 1.f : 0.f; s.tcScale[i][3] = 0;
		}
		// the render state
		s.zmode = bpRegs[0x40]; s.cmode0 = bpRegs[0x41]; s.cmode1 = bpRegs[0x42]; s.peCtrl = bpRegs[0x43];
		s.cull = (gm >> 14) & 3; s.lpSize = bpRegs[0x22];
		u32 tl = bpRegs[0x20], br = bpRegs[0x21];
		s.scissor[0] = (s32) ((tl >> 12) & 0x7FF) - (s32) xoff; s.scissor[1] = (s32) (tl & 0x7FF) - (s32) yoff;
		s.scissor[2] = (s32) ((br >> 12) & 0x7FF) - (s32) xoff + 1; s.scissor[3] = (s32) (br & 0x7FF) - (s32) yoff + 1;
		for (int m = 0; m < 8; m++)
		{
			s.texMode[m][0] = bpRegs[(m < 4 ? 0x80 : 0xA0) + (m & 3)]; s.texMode[m][1] = bpRegs[(m < 4 ? 0x84 : 0xA4) + (m & 3)];
		}
		s.serial++;
	}
	// the textures of the maps the stages (and the indirect stages) read (twice when the texture
	// pool was emptied meanwhile: the maps before it again)
	u32 used = 0;
	for (int st = 0; st < s.gen[0]; st++) if (s.tref[st] & 0x40) used |= 1u << (s.tref[st] & 7);
	for (int k = 0; k < s.gen[1]; k++) used |= 1u << ((bpRegs[0x27] >> (6 * k)) & 7);
	for (int pass = 0; pass < 2; pass++)
	{
		u32 flushes = texFlushes;
		for (int m = 0; m < 8; m++)
		{
			int t = (used >> m) & 1 ? gpuTexture (m) : -1;
			if (t == s.tex[m]) continue;
			s.tex[m] = t;
			u32 img0 = bpRegs[(m < 4 ? 0x88 : 0xA8) + (m & 3)];		// (its size as the game gives it)
			s.texSize[m][0] = (float) ((img0 & 0x3FF) + 1); s.texSize[m][1] = (float) (((img0 >> 10) & 0x3FF) + 1);
			s.texSize[m][2] = s.texSize[m][3] = 0;
			s.serial++;
		}
		if (texFlushes == flushes) break;
	}
}

// the texture a map reads: an EFB copy's (MEM1 there as the copy left it), or decoded from MEM1
int Machine::gpuTexture (int map)
{
	u32 img3 = bpRegs[(map < 4 ? 0x94 : 0xB4) + (map & 3)];
	u32 addr = (img3 & 0x00FFFFFF) << 5;
	for (int i = 0; i < GX_COPIES; i++)
	{
		EfbCopy &c = efbCopies[i];
		if (!c.bytes || c.addr != addr) continue;
		if (memHash (mem1, c.addr, c.bytes) == c.hash) return GX_TEX_COPY + i;
		c.bytes = 0;							// (written since: a texture of the game's)
	}
	return gxTexture (map, true);
}

// ---- a primitive: its indices --------------------------------------------------------------------------------
void Machine::gxGpuPrimitive (int prim, int count, const GxVertex *vx, u32 vcd)
{
	gxGpuState (vcd);
	for (int st = 0; st < gxs.gen[0]; st++) if (gxs.ind[st]) { gxIndirect++; break; }
	static u32 idx[0x10000 * 6];
	int ni = 0, kind = GX_TRIANGLES;
	switch (prim)
	{
	case 0: case 1:								// quads
		for (int i = 0; i + 3 < count; i += 4)
		{
			idx[ni++] = (u32) i; idx[ni++] = (u32) i + 1; idx[ni++] = (u32) i + 2;
			idx[ni++] = (u32) i; idx[ni++] = (u32) i + 2; idx[ni++] = (u32) i + 3;
		}
		break;
	case 2: for (int i = 0; i + 2 < count; i += 3) { idx[ni++] = (u32) i; idx[ni++] = (u32) i + 1; idx[ni++] = (u32) i + 2; } break;
	case 3:									// a strip
		for (int i = 0; i + 2 < count; i++)
		{
			if (i & 1) { idx[ni++] = (u32) i + 1; idx[ni++] = (u32) i; }
			else { idx[ni++] = (u32) i; idx[ni++] = (u32) i + 1; }
			idx[ni++] = (u32) i + 2;
		}
		break;
	case 4: for (int i = 1; i + 1 < count; i++) { idx[ni++] = 0; idx[ni++] = (u32) i; idx[ni++] = (u32) i + 1; } break;	// a fan
	case 5: kind = GX_LINES; for (int i = 0; i + 1 < count; i += 2) { idx[ni++] = (u32) i; idx[ni++] = (u32) i + 1; } break;
	case 6: kind = GX_LINES; for (int i = 0; i + 1 < count; i++) { idx[ni++] = (u32) i; idx[ni++] = (u32) i + 1; } break;
	default: kind = GX_POINTS; for (int i = 0; i < count; i++) idx[ni++] = (u32) i; break;
	}
	if (ni) gpu->draw (*this, gxs, vx, count, idx, ni, kind);
}

// ---- an EFB copy (BP 0x52) -------------------------------------------------------------------------------------
void Machine::gxGpuCopy (u32 v)
{
	GxCopy c;
	u32 src = bpRegs[0x49], sz = bpRegs[0x4A];
	c.x = (s32) (src & 0x3FF); c.y = (s32) ((src >> 10) & 0x3FF);
	c.w = (s32) (sz & 0x3FF) + 1; c.h = (s32) ((sz >> 10) & 0x3FF) + 1;
	c.addr = (bpRegs[0x4B] & 0x00FFFFFF) << 5;
	c.toXfb = (v >> 14) & 1; c.half = (v >> 9) & 1; c.clear = (v >> 11) & 1; c.intensity = (v >> 15) & 1;
	u32 tpf = (v >> 3) & 15;
	c.fmt = tpf / 2 + (tpf & 1) * 8;
	c.efbFmt = bpRegs[0x43] & 7; c.depth = c.efbFmt == 3;
	c.clearColor = (bpRegs[0x4F] & 0xFFFF) << 16 | (bpRegs[0x50] & 0xFFFF);
	c.clearZ = bpRegs[0x51] & 0xFFFFFF;
	c.colorMask = (bpRegs[0x41] >> 3) & 1; c.alphaMask = (bpRegs[0x41] >> 4) & 1; c.zMask = (bpRegs[0x40] >> 4) & 1;
	c.slot = -1;
	if (!c.toXfb)
	{
		// its slot: the one at that address, else the oldest; the others it overwrites forgotten
		int w = c.half ? c.w / 2 : c.w, h = c.half ? c.h / 2 : c.h;
		if (w < 1) w = 1;
		if (h < 1) h = 1;
		u32 bytes = texBytes (w, h, copyTexFormat (c.fmt));
		static u32 useClock;
		int slot = -1, old = 0;
		for (int i = 0; i < GX_COPIES; i++)
		{
			EfbCopy &e = efbCopies[i];
			if (e.bytes && e.addr == c.addr) slot = i;
			else if (e.bytes && e.addr < c.addr + bytes && c.addr < e.addr + e.bytes) e.bytes = 0;
			if (efbCopies[i].use < efbCopies[old].use) old = i;
		}
		if (slot < 0) slot = old;
		EfbCopy &e = efbCopies[slot];
		e.addr = c.addr; e.bytes = bytes; e.w = (u16) w; e.h = (u16) h; e.fmt = (u8) c.fmt;
		e.hash = memHash (mem1, c.addr, bytes); e.use = ++useClock;
		c.slot = slot;
		gxsDirty = true;						// (a texture may be this copy now)
		for (int m = 0; m < 8; m++) gxs.tex[m] = -2;
	}
	gpu->copy (*this, c);
}

} // namespace gc
