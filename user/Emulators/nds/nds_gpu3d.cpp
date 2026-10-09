//
// nds/nds_gpu3d.cpp -- the 3D engine's geometry side: the commands (packed through GXFIFO or one
// per port), the four matrix stacks (projection, position, vector, texture; 20.12 fixed point,
// row vectors: v' = v x M, a command's matrix multiplies on the left), the lighting (four lights,
// the shininess table), the texture coordinates' transforms, the polygons (triangles, quads and
// their strips; culling, clipping to the view volume, the viewport, the depth) into the polygon RAM;
// the box / position / vector tests; the swap at the VBlank (the commands after a SWAP_BUFFERS wait
// for it). The drawing is nds_render3d.cpp's.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

typedef __int128 s128;

static const u8 NPARAMS[256] = {
	// 0x00
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 0x10
	1, 0, 1, 1, 1, 0, 16, 12, 16, 12, 9, 3, 3, 0, 0, 0,
	// 0x20
	1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
	// 0x30
	1, 1, 1, 1, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 0x40
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 0x50
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 0x60
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 0x70
	3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static void identity (s32 *m) { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 0x1000 : 0; }

void Gpu3D::reset (Machine *mm)
{
	m = mm;
	fifoHead = fifoTail = 0; parHead = parTail = 0; polyFront = true;
	packedCmds = 0; packedLeft = 0; paramsLeft = 0; curCmd = 0; nParam = 0;
	portCmd = -1; portCount = 0;
	swapPending = false; swapParam = 0; busy = false;
	identity (proj); identity (pos); identity (vec); identity (tex); identity (clip);
	for (int i = 0; i < 32; i++) { identity (posStack[i]); identity (vecStack[i]); }
	identity (projStack); identity (texStack);
	projSp = posSp = texSp = 0; mtxMode = 0; clipDirty = false; stackError = false;
	vx = vy = vz = 0; cr = cg = cb = 63; ts = tt = 0; tsOut = ttOut = 0; nx = ny = nz = 0;
	polyAttr = polyAttrPending = 0; texParam = 0; palBase = 0; difAmb = 0; speEmi = 0;
	for (int i = 0; i < 4; i++) { lightVec[i][0] = lightVec[i][1] = lightVec[i][2] = 0; lightColor[i] = 0; }
	for (int i = 0; i < 128; i++) shininess[i] = 0;
	useShininess = false;
	primType = 0; inBegin = false; vtxInPrim = 0; primCount = 0; stripOdd = false;
	vpX1 = 0; vpY1 = 0; vpX2 = 255; vpY2 = 191;
	for (int i = 0; i < 4; i++) posResult[i] = 0;
	for (int i = 0; i < 3; i++) vecResult[i] = 0;
	boxResult = false;
	nVerts[0] = nVerts[1] = nPolys[0] = nPolys[1] = 0;
	cur = 0; rdSet = 1; overflow = false;
	dispcnt3d = 0;
	for (int i = 0; i < 0x80; i++) regs[i] = rdRegs[i] = 0;
	rdDispcnt = 0; rdSwap = 0;
}

// ---- matrices ------------------------------------------------------------------------------------------------------
// d = s (rows x cols, the rest identity) x d
void Gpu3D::mtxMult (s32 *d, const s32 *s, int rows, int cols)
{
	s32 a[16];
	identity (a);
	for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) a[r * 4 + c] = s[r * cols + c];
	s32 o[16];
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
		{
			s64 v = 0;
			for (int k = 0; k < 4; k++) v += (s64) a[r * 4 + k] * d[k * 4 + c];
			o[r * 4 + c] = (s32) (v >> 12);
		}
	for (int i = 0; i < 16; i++) d[i] = o[i];
}

void Gpu3D::mtxLoad (s32 *d, const s32 *s) { for (int i = 0; i < 16; i++) d[i] = s[i]; }

void Gpu3D::updateClip ()
{
	if (!clipDirty) return;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
		{
			s64 v = 0;
			for (int k = 0; k < 4; k++) v += (s64) pos[r * 4 + k] * proj[k * 4 + c];
			clip[r * 4 + c] = (s32) (v >> 12);
		}
	clipDirty = false;
}

// ---- the command intake ---------------------------------------------------------------------------------------------
void Gpu3D::cmdPush (int cmd, const u32 *p, int n)
{
	if (swapPending || fifoHead != fifoTail)
	{
		if (fifoHead - fifoTail >= FIFO_SIZE || parHead - parTail + (u32) n > FIFO_SIZE * 2) return;	// (full: dropped)
		fifoCmd[fifoHead & (FIFO_SIZE - 1)] = (u32) cmd | ((u32) n << 8);
		for (int i = 0; i < n; i++) fifoPar[(parHead++) & (FIFO_SIZE * 2 - 1)] = p[i];
		fifoHead++;
		return;
	}
	exec (cmd, p);
}

void Gpu3D::drain ()
{
	u32 par[32];
	while (fifoHead != fifoTail && !swapPending)
	{
		u32 e = fifoCmd[fifoTail & (FIFO_SIZE - 1)];
		int cmd = (int) (e & 0xFF), n = (int) ((e >> 8) & 0xFF);
		for (int i = 0; i < n; i++) par[i] = fifoPar[(parTail++) & (FIFO_SIZE * 2 - 1)];
		fifoTail++;
		exec (cmd, par);
	}
}

void Gpu3D::write32 (u32 a, u32 v)
{
	u32 off = a & 0xFFF;
	if (off >= 0x400 && off < 0x440)				// GXFIFO: packed commands
	{
		if (!packedLeft)
		{
			packedCmds = v; nParam = 0;
			packedLeft = 1;
			// the commands with no parameter at the head run now
			while (packedCmds && !NPARAMS[packedCmds & 0xFF]) { cmdPush ((int) (packedCmds & 0xFF), 0, 0); packedCmds >>= 8; }
			if (!packedCmds) packedLeft = 0;
			return;
		}
		params[nParam++] = v;
		int c = (int) (packedCmds & 0xFF);
		if (nParam >= NPARAMS[c])
		{
			cmdPush (c, params, nParam);
			nParam = 0;
			packedCmds >>= 8;
			while (packedCmds && !NPARAMS[packedCmds & 0xFF]) { cmdPush ((int) (packedCmds & 0xFF), 0, 0); packedCmds >>= 8; }
			if (!packedCmds) packedLeft = 0;
		}
		return;
	}
	if (off >= 0x440 && off < 0x600)				// a command's own port
	{
		int c = (int) ((off - 0x400) >> 2);
		int np = NPARAMS[c];
		if (!np) { cmdPush (c, 0, 0); return; }
		if (portCmd != c) { portCmd = c; portCount = 0; }
		portParams[portCount++] = v;
		if (portCount >= np) { cmdPush (c, portParams, np); portCount = 0; portCmd = -1; }
		return;
	}
	if (off == 0x600)						// GXSTAT
	{
		if (v & 0x8000) { stackError = false; projSp = 0; }
		regs[0x7F] = (u8) (v >> 30);
		dispcnt3d = dispcnt3d;
		gxstat ();
		return;
	}
	if (off >= 0x330 && off < 0x3C0)
	{
		for (int i = 0; i < 4; i++) write8 (a + (u32) i, (u8) (v >> (i * 8)));
		return;
	}
}

void Gpu3D::write16 (u32 a, u16 v)
{
	u32 off = a & 0xFFF;
	if (off >= 0x400 && off < 0x600) return;			// (the FIFO takes words only)
	if (off == 0x600 || off == 0x602) { if (off == 0x602) write32 (a - 2, (u32) v << 16); return; }
	write8 (a, (u8) v); write8 (a + 1, (u8) (v >> 8));
}

void Gpu3D::write8 (u32 a, u8 v)
{
	u32 off = a & 0xFFF;
	if (off >= 0x330 && off < 0x3C0)
	{
		regs[off - 0x330] = v;
		u32 o = off - 0x330;
		if (o < 0x10) edgeColor[o >> 1] = rd16 (regs + (o & ~1u));
		else if (o == 0x10) alphaRef = v & 31;
		else if (o >= 0x20 && o < 0x24) clearColor = rd32 (regs + 0x20);
		else if (o >= 0x24 && o < 0x26) clearDepth = rd16 (regs + 0x24) & 0x7FFF;
		else if (o >= 0x26 && o < 0x28) clearOffset = rd16 (regs + 0x26);
		else if (o >= 0x28 && o < 0x2C) fogColor = rd32 (regs + 0x28);
		else if (o >= 0x2C && o < 0x2E) fogOffset = rd16 (regs + 0x2C) & 0x7FFF;
		else if (o >= 0x30 && o < 0x50) fogTable[o - 0x30] = v & 0x7F;
		else if (o >= 0x50 && o < 0x90) toonTable[(o - 0x50) >> 1] = rd16 (regs + (o & ~1u)) & 0x7FFF;
		return;
	}
	if (off == 0x603) { regs[0x7F] = (u8) (v >> 6); gxstat (); }
}

u32 Gpu3D::read32 (u32 a)
{
	u32 off = a & 0xFFF;
	if (off == 0x600) return gxstat ();
	if (off == 0x604) return (u32) nPolys[cur] | ((u32) (nVerts[cur] > 6144 ? 6144 : nVerts[cur]) << 16);
	if (off == 0x320) return 46;					// RDLINES_COUNT
	if (off >= 0x620 && off < 0x630) return posResult[(off - 0x620) >> 2];
	if (off >= 0x630 && off < 0x638) { int i = (int) (off - 0x630) >> 1; u32 lo = vecResult[i] & 0xFFFF, hi = i < 2 ? vecResult[i + 1] & 0xFFFF : 0; return lo | (hi << 16); }
	if (off >= 0x640 && off < 0x680) { updateClip (); return (u32) clip[(off - 0x640) >> 2]; }
	if (off >= 0x680 && off < 0x6A4) { int i = (int) (off - 0x680) >> 2; return (u32) vec[(i / 3) * 4 + i % 3]; }
	return 0;
}

u32 Gpu3D::gxstat ()
{
	int cnt = fifoCount (); if (cnt > 256) cnt = 256;
	u32 v = 0;
	if (boxResult) v |= 2;
	v |= (u32) (posSp & 31) << 8;
	v |= (u32) (projSp & 1) << 13;
	if (stackError) v |= 0x8000;
	v |= (u32) cnt << 16;
	if (cnt < 128) v |= 1u << 25;
	if (!cnt) v |= 1u << 26;
	if (cnt || swapPending) v |= 1u << 27;
	int irqMode = regs[0x7F] & 3;
	v |= (u32) irqMode << 30;
	if ((irqMode == 1 && cnt < 128) || (irqMode == 2 && !cnt)) m->raise (0, 21);
	return v;
}

void Gpu3D::vblank ()
{
	if (swapPending)
	{
		// the set filled becomes the one drawn
		rdSet = cur; rdSwap = swapParam; rdDispcnt = dispcnt3d;
		for (int i = 0; i < 0x80; i++) rdRegs[i] = regs[i];
		cur ^= 1;
		nVerts[cur] = 0; nPolys[cur] = 0;
		swapPending = false;
		if (m->render3dKick) m->render3dKick (m->render3dCtx);
		else m->render3dNow ();
		drain ();
	}
	gxstat ();
}

// ---- the commands -------------------------------------------------------------------------------------------------------
static inline s32 s10 (u32 v) { return (s32) (v << 22) >> 22; }

void Gpu3D::exec (int cmd, const u32 *p)
{
	switch (cmd)
	{
	case 0x10: mtxMode = (int) (p[0] & 3); break;
	case 0x11:							// MTX_PUSH
		if (mtxMode == 0) { if (projSp > 0) stackError = true; mtxLoad (projStack, proj); projSp = 1; }
		else if (mtxMode == 3) { mtxLoad (texStack, tex); texSp = 1; }
		else
		{
			if (posSp >= 31) stackError = true;
			mtxLoad (posStack[posSp & 31], pos); mtxLoad (vecStack[posSp & 31], vec);
			posSp = (posSp + 1) & 63;
		}
		break;
	case 0x12:							// MTX_POP
		if (mtxMode == 0) { if (projSp < 1) stackError = true; projSp = 0; mtxLoad (proj, projStack); clipDirty = true; }
		else if (mtxMode == 3) { texSp = 0; mtxLoad (tex, texStack); }
		else
		{
			int n = (int) ((s32) (p[0] << 26) >> 26);
			posSp = (posSp - n) & 63;
			if (posSp >= 31) stackError = true;
			mtxLoad (pos, posStack[posSp & 31]); mtxLoad (vec, vecStack[posSp & 31]);
			clipDirty = true;
		}
		break;
	case 0x13:							// MTX_STORE
		if (mtxMode == 0) mtxLoad (projStack, proj);
		else if (mtxMode == 3) mtxLoad (texStack, tex);
		else { int n = (int) (p[0] & 31); if (n == 31) stackError = true; mtxLoad (posStack[n], pos); mtxLoad (vecStack[n], vec); }
		break;
	case 0x14:							// MTX_RESTORE
		if (mtxMode == 0) { mtxLoad (proj, projStack); clipDirty = true; }
		else if (mtxMode == 3) mtxLoad (tex, texStack);
		else { int n = (int) (p[0] & 31); if (n == 31) stackError = true; mtxLoad (pos, posStack[n]); mtxLoad (vec, vecStack[n]); clipDirty = true; }
		break;
	case 0x15:							// MTX_IDENTITY
		if (mtxMode == 0) identity (proj);
		else if (mtxMode == 3) identity (tex);
		else { identity (pos); if (mtxMode == 2) identity (vec); }
		clipDirty = true;
		break;
	case 0x16: case 0x17:						// MTX_LOAD_4x4 / 4x3
	{
		s32 mm[16]; identity (mm);
		if (cmd == 0x16) for (int i = 0; i < 16; i++) mm[i] = (s32) p[i];
		else for (int r = 0; r < 4; r++) for (int c = 0; c < 3; c++) mm[r * 4 + c] = (s32) p[r * 3 + c];
		if (mtxMode == 0) mtxLoad (proj, mm);
		else if (mtxMode == 3) mtxLoad (tex, mm);
		else { mtxLoad (pos, mm); if (mtxMode == 2) mtxLoad (vec, mm); }
		clipDirty = true;
		break;
	}
	case 0x18: case 0x19: case 0x1A:				// MTX_MULT_4x4 / 4x3 / 3x3
	{
		int rows = cmd == 0x18 ? 4 : cmd == 0x19 ? 4 : 3, cols = cmd == 0x18 ? 4 : 3;
		const s32 *s = (const s32 *) p;
		if (mtxMode == 0) mtxMult (proj, s, rows, cols);
		else if (mtxMode == 3) mtxMult (tex, s, rows, cols);
		else { mtxMult (pos, s, rows, cols); if (mtxMode == 2) mtxMult (vec, s, rows, cols); }
		clipDirty = true;
		break;
	}
	case 0x1B:							// MTX_SCALE (the vector matrix kept)
	{
		s32 *d = mtxMode == 0 ? proj : mtxMode == 3 ? tex : pos;
		for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) d[r * 4 + c] = (s32) (((s64) d[r * 4 + c] * (s32) p[r]) >> 12);
		clipDirty = true;
		break;
	}
	case 0x1C:							// MTX_TRANS
	{
		s32 *ds[2] = { mtxMode == 0 ? proj : mtxMode == 3 ? tex : pos, mtxMode == 2 ? vec : 0 };
		for (int k = 0; k < 2; k++)
		{
			s32 *d = ds[k]; if (!d) continue;
			for (int c = 0; c < 4; c++)
				d[12 + c] = (s32) (((s64) (s32) p[0] * d[c] + (s64) (s32) p[1] * d[4 + c] + (s64) (s32) p[2] * d[8 + c] + ((s64) d[12 + c] << 12)) >> 12);
		}
		clipDirty = true;
		break;
	}
	case 0x20:							// COLOR
	{
		u32 c = p[0];
		u32 r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
		cr = (s32) ((r << 1) | (r ? 1 : 0)); cg = (s32) ((g << 1) | (g ? 1 : 0)); cb = (s32) ((b << 1) | (b ? 1 : 0));
		break;
	}
	case 0x21:							// NORMAL
		nx = s10 (p[0]); ny = s10 (p[0] >> 10); nz = s10 (p[0] >> 20);
		if (((texParam >> 30) & 3) == 2) texCoordTransform (2);
		lighting ();
		break;
	case 0x22:							// TEXCOORD
		ts = (s16) (p[0] & 0xFFFF); tt = (s16) (p[0] >> 16);
		if (((texParam >> 30) & 3) == 1) texCoordTransform (1);
		else { tsOut = ts; ttOut = tt; }
		break;
	case 0x23: addVertex ((s16) (p[0] & 0xFFFF), (s16) (p[0] >> 16), (s16) (p[1] & 0xFFFF)); break;
	case 0x24: addVertex (s10 (p[0]) << 6, s10 (p[0] >> 10) << 6, s10 (p[0] >> 20) << 6); break;
	case 0x25: addVertex ((s16) (p[0] & 0xFFFF), (s16) (p[0] >> 16), vz); break;
	case 0x26: addVertex ((s16) (p[0] & 0xFFFF), vy, (s16) (p[0] >> 16)); break;
	case 0x27: addVertex (vx, (s16) (p[0] & 0xFFFF), (s16) (p[0] >> 16)); break;
	case 0x28: addVertex ((s16) (vx + s10 (p[0])), (s16) (vy + s10 (p[0] >> 10)), (s16) (vz + s10 (p[0] >> 20))); break;
	case 0x29: polyAttrPending = p[0]; break;
	case 0x2A: texParam = p[0]; break;
	case 0x2B: palBase = p[0] & 0x1FFF; break;
	case 0x30:							// DIF_AMB
		difAmb = p[0];
		if (p[0] & 0x8000)
		{
			u32 r = p[0] & 31, g = (p[0] >> 5) & 31, b = (p[0] >> 10) & 31;
			cr = (s32) ((r << 1) | (r ? 1 : 0)); cg = (s32) ((g << 1) | (g ? 1 : 0)); cb = (s32) ((b << 1) | (b ? 1 : 0));
		}
		break;
	case 0x31: speEmi = p[0]; useShininess = p[0] & 0x8000; break;
	case 0x32:							// LIGHT_VECTOR (by the vector matrix)
	{
		int l = (int) (p[0] >> 30);
		s32 x = s10 (p[0]), y = s10 (p[0] >> 10), z = s10 (p[0] >> 20);
		for (int i = 0; i < 3; i++) lightVec[l][i] = (s32) (((s64) x * vec[i] + (s64) y * vec[4 + i] + (s64) z * vec[8 + i]) >> 12);
		break;
	}
	case 0x33: lightColor[p[0] >> 30] = p[0] & 0x7FFF; break;
	case 0x34: for (int i = 0; i < 32; i++) for (int k = 0; k < 4; k++) shininess[i * 4 + k] = (u8) (p[i] >> (k * 8)); break;
	case 0x40:							// BEGIN_VTXS
		primType = (int) (p[0] & 3); inBegin = true; vtxInPrim = 0; primCount = 0; stripOdd = false;
		polyAttr = polyAttrPending;
		break;
	case 0x41: inBegin = false; break;				// END_VTXS
	case 0x50: swapPending = true; swapParam = p[0] & 3; break;	// SWAP_BUFFERS
	case 0x60:							// VIEWPORT
		vpX1 = (s32) (p[0] & 0xFF); vpY1 = (s32) ((p[0] >> 8) & 0xFF);
		vpX2 = (s32) ((p[0] >> 16) & 0xFF); vpY2 = (s32) ((p[0] >> 24) & 0xFF);
		break;
	case 0x70: boxTest (p); break;
	case 0x71:							// POS_TEST
	{
		vx = (s16) (p[0] & 0xFFFF); vy = (s16) (p[0] >> 16); vz = (s16) (p[1] & 0xFFFF);
		updateClip ();
		for (int c = 0; c < 4; c++) posResult[c] = (u32) (s32) (((s64) vx * clip[c] + (s64) vy * clip[4 + c] + (s64) vz * clip[8 + c] + ((s64) clip[12 + c] << 12)) >> 12);
		break;
	}
	case 0x72:							// VEC_TEST
	{
		s32 x = s10 (p[0]), y = s10 (p[0] >> 10), z = s10 (p[0] >> 20);
		for (int c = 0; c < 3; c++)
		{
			s32 v = (s32) (((s64) x * vec[c] + (s64) y * vec[4 + c] + (s64) z * vec[8 + c]) >> 9);
			vecResult[c] = (u32) (s32) (s16) (v & 0xFFFF);
		}
		break;
	}
	default: break;
	}
}

void Gpu3D::texCoordTransform (int mode)
{
	if (mode == 1)
	{
		tsOut = (s32) (((s64) ts * tex[0] + (s64) tt * tex[4] + tex[8] + tex[12]) >> 12);
		ttOut = (s32) (((s64) ts * tex[1] + (s64) tt * tex[5] + tex[9] + tex[13]) >> 12);
	}
	else if (mode == 2)
	{
		tsOut = (s32) (((s64) nx * tex[0] + (s64) ny * tex[4] + (s64) nz * tex[8]) >> 21) + ts;
		ttOut = (s32) (((s64) nx * tex[1] + (s64) ny * tex[5] + (s64) nz * tex[9]) >> 21) + tt;
	}
}

void Gpu3D::lighting ()
{
	if (!(polyAttr & 15)) return;
	s32 n[3];
	for (int i = 0; i < 3; i++) n[i] = (s32) (((s64) nx * vec[i] + (s64) ny * vec[4 + i] + (s64) nz * vec[8 + i]) >> 12);
	s32 dif[3] = { (s32) (difAmb & 31), (s32) ((difAmb >> 5) & 31), (s32) ((difAmb >> 10) & 31) };
	s32 amb[3] = { (s32) ((difAmb >> 16) & 31), (s32) ((difAmb >> 21) & 31), (s32) ((difAmb >> 26) & 31) };
	s32 spe[3] = { (s32) (speEmi & 31), (s32) ((speEmi >> 5) & 31), (s32) ((speEmi >> 10) & 31) };
	s32 emi[3] = { (s32) ((speEmi >> 16) & 31), (s32) ((speEmi >> 21) & 31), (s32) ((speEmi >> 26) & 31) };
	s32 acc[3] = { 0, 0, 0 };
	for (int l = 0; l < 4; l++)
	{
		if (!(polyAttr & (1u << l))) continue;
		const s32 *L = lightVec[l];
		s32 diff = -(L[0] * n[0] + L[1] * n[1] + L[2] * n[2]) >> 10;
		if (diff < 0) diff = 0; else if (diff > 255) diff = 255;
		s32 shine = -(((L[0] >> 1) * n[0] + (L[1] >> 1) * n[1] + ((L[2] - 0x200) >> 1) * n[2]) >> 10);
		if (shine < 0) shine = 0; else if (shine > 255) shine = (0x100 - shine) & 0xFF;
		shine = ((shine * shine) >> 7) - 0x100;
		if (shine < 0) shine = 0;
		if (useShininess) shine = shininess[(shine >> 1) & 127];
		u32 lc = lightColor[l];
		s32 lcol[3] = { (s32) (lc & 31), (s32) ((lc >> 5) & 31), (s32) ((lc >> 10) & 31) };
		for (int i = 0; i < 3; i++)
			acc[i] += ((spe[i] * lcol[i] * shine) >> 13) + ((dif[i] * lcol[i] * diff) >> 13) + ((amb[i] * lcol[i]) >> 5);
	}
	s32 out[3];
	for (int i = 0; i < 3; i++) { s32 v = emi[i] + acc[i]; if (v > 31) v = 31; out[i] = (v << 1) | (v ? 1 : 0); }
	cr = out[0]; cg = out[1]; cb = out[2];
}

// ---- vertices and polygons -------------------------------------------------------------------------------------------
void Gpu3D::addVertex (s32 x, s32 y, s32 z)
{
	vx = (s16) x; vy = (s16) y; vz = (s16) z;
	if (!inBegin) return;
	updateClip ();
	if (((texParam >> 30) & 3) == 3)
	{
		tsOut = (s32) (((s64) vx * tex[0] + (s64) vy * tex[4] + (s64) vz * tex[8]) >> 24) + ts;
		ttOut = (s32) (((s64) vx * tex[1] + (s64) vy * tex[5] + (s64) vz * tex[9]) >> 24) + tt;
	}
	Vertex v;
	s64 X = (s64) vx * clip[0] + (s64) vy * clip[4] + (s64) vz * clip[8] + ((s64) clip[12] << 12);
	s64 Y = (s64) vx * clip[1] + (s64) vy * clip[5] + (s64) vz * clip[9] + ((s64) clip[13] << 12);
	s64 Z = (s64) vx * clip[2] + (s64) vy * clip[6] + (s64) vz * clip[10] + ((s64) clip[14] << 12);
	s64 Wv = (s64) vx * clip[3] + (s64) vy * clip[7] + (s64) vz * clip[11] + ((s64) clip[15] << 12);
	v.x = (s32) (X >> 12); v.y = (s32) (Y >> 12); v.z = (s32) (Z >> 12); v.w = (s32) (Wv >> 12);
	v.cr = cr; v.cg = cg; v.cb = cb;
	v.s = tsOut; v.t = ttOut;
	v.clipped = false;
	v.sx = v.sy = v.sz = v.sw = 0;
	// the primitive's assembly
	switch (primType)
	{
	case 0: primV[primCount++] = v; if (primCount == 3) { emitPolygon (); primCount = 0; } break;
	case 1: primV[primCount++] = v; if (primCount == 4) { emitPolygon (); primCount = 0; } break;
	case 2:								// triangle strip
		primV[primCount++] = v;
		if (primCount == 3)
		{
			if (stripOdd) { Vertex t = primV[0]; primV[0] = primV[1]; primV[1] = t; }
			emitPolygon ();
			if (stripOdd) { Vertex t = primV[0]; primV[0] = primV[1]; primV[1] = t; }
			primV[0] = primV[1]; primV[1] = primV[2]; primCount = 2;
			stripOdd = !stripOdd;
		}
		break;
	default:							// quad strip: 0 1 3 2
		primV[primCount++] = v;
		if (primCount == 4)
		{
			Vertex a = primV[2]; primV[2] = primV[3]; primV[3] = a;
			emitPolygon ();
			a = primV[2]; primV[2] = primV[3]; primV[3] = a;
			primV[0] = primV[2]; primV[1] = primV[3]; primCount = 2;
		}
		break;
	}
}

// clip a polygon against one plane: coordinate k (0 x, 1 y, 2 z), side +1 (v <= w) or -1 (-w <= v)
static int clipPlane (Vertex *in, int n, Vertex *out, int k, int side)
{
	int o = 0;
	for (int i = 0; i < n; i++)
	{
		Vertex &a = in[i], &b = in[(i + 1) % n];
		s64 ca = (s64) a.w - side * (s64) (&a.x)[k], cb = (s64) b.w - side * (s64) (&b.x)[k];	// >= 0: inside
		if (ca >= 0) out[o++] = a;
		if ((ca >= 0) != (cb >= 0))
		{
			// the crossing: t = ca / (ca - cb), 16 bits of fraction
			s64 den = ca - cb;
			s64 t = den ? (ca << 16) / den : 0;
			Vertex v;
			#define LERP(f) v.f = (s32) (a.f + (((s64) (b.f - a.f) * t) >> 16))
			LERP (x); LERP (y); LERP (z); LERP (w); LERP (cr); LERP (cg); LERP (cb); LERP (s); LERP (t);
			#undef LERP
			v.clipped = true; v.sx = v.sy = v.sz = v.sw = 0;
			out[o++] = v;
		}
	}
	return o;
}

void Gpu3D::emitPolygon ()
{
	int nv = primType & 1 ? 4 : 3;
	Vertex poly[2][12];
	for (int i = 0; i < nv; i++) poly[0][i] = primV[i];
	// the facing (in clip space: x, y, w)
	{
		const Vertex &v0 = poly[0][0], &v1 = poly[0][1], &v2 = poly[0][2];
		s128 ax = (s128) v0.x - v1.x, ay = (s128) v0.y - v1.y, aw = (s128) v0.w - v1.w;
		s128 bx = (s128) v2.x - v1.x, by = (s128) v2.y - v1.y, bw = (s128) v2.w - v1.w;
		s128 nX = ay * bw - aw * by, nY = aw * bx - ax * bw, nW = ax * by - ay * bx;
		s128 dot = (s128) v1.x * nX + (s128) v1.y * nY + (s128) v1.w * nW;
		bool front = dot < 0;
		if (dot == 0 && nv == 4)
		{
			const Vertex &v3 = poly[0][3];
			ax = (s128) v3.x - v1.x; ay = (s128) v3.y - v1.y; aw = (s128) v3.w - v1.w;
			(void) ax;
		}
		if (front && !(polyAttr & 0x80)) return;
		if (!front && dot != 0 && !(polyAttr & 0x40)) return;
		polyFront = front;
	}
	// clipping: the far plane first (a polygon crossing it is dropped unless bit 12)
	int n = nv, src = 0;
	{
		bool cross = false;
		for (int i = 0; i < n; i++) if (poly[0][i].z > poly[0][i].w) cross = true;
		if (cross)
		{
			if (!(polyAttr & 0x1000)) return;
			n = clipPlane (poly[src], n, poly[src ^ 1], 2, 1); src ^= 1;
			if (n < 3) return;
		}
	}
	static const int PL[5][2] = { { 2, -1 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 } };
	for (int p = 0; p < 5; p++)
	{
		bool out = false;
		for (int i = 0; i < n; i++) { const Vertex &v = poly[src][i]; s32 c = (&v.x)[PL[p][0]]; if (PL[p][1] > 0 ? c > v.w : -c > v.w) out = true; }
		if (!out) continue;
		n = clipPlane (poly[src], n, poly[src ^ 1], PL[p][0], PL[p][1]); src ^= 1;
		if (n < 3) return;
		if (n > 10) n = 10;
	}
	// into the polygon RAM
	if (nPolys[cur] >= MAX_POLYS || nVerts[cur] + n > MAX_VERTS) { overflow = true; return; }
	Polygon &P = pram[cur][nPolys[cur]];
	s32 vw = (vpX2 - vpX1 + 1) & 0x1FF, vh = (vpY2 - vpY1 + 1) & 0xFF;
	s32 vy0 = (191 - vpY2) & 0xFF;
	bool wbuf = false;
	P.nv = n;
	P.ymin = 255; P.ymax = -1;
	for (int i = 0; i < n; i++)
	{
		Vertex *v = &vram[cur][nVerts[cur]++];
		*v = poly[src][i];
		s64 w = v->w;
		if (w == 0) { v->sx = 0; v->sy = 0; }
		else
		{
			v->sx = (s32) ((((s64) v->x + w) * vw) / (w * 2) + vpX1);
			v->sy = (s32) ((((s64) -v->y + w) * vh) / (w * 2) + vy0);
		}
		if (v->sx < 0) v->sx = 0; if (v->sx > 256) v->sx = 256;
		if (v->sy < 0) v->sy = 0; if (v->sy > 192) v->sy = 192;
		if (w) v->sz = (s32) ((((s64) v->z * 0x4000) / w + 0x3FFF) * 0x200);
		else v->sz = 0x7FFE00;
		if (v->sz < 0) v->sz = 0; if (v->sz > 0xFFFFFF) v->sz = 0xFFFFFF;
		v->sw = v->w;
		P.v[i] = v;
		if (v->sy < P.ymin) P.ymin = v->sy;
		if (v->sy > P.ymax) P.ymax = v->sy;
	}
	P.attr = polyAttr; P.texParam = texParam; P.palBase = palBase;
	u32 alpha = (polyAttr >> 16) & 31;
	u32 fmt = (texParam >> 26) & 7;
	P.translucent = (alpha && alpha < 31) || (fmt == 1 || fmt == 6) || (((polyAttr >> 4) & 3) == 3 && alpha < 31);
	P.facingFront = polyFront;
	P.wBuffer = wbuf;
	P.index = nPolys[cur];
	nPolys[cur]++;
}

void Gpu3D::boxTest (const u32 *p)
{
	s32 x = (s16) (p[0] & 0xFFFF), y = (s16) (p[0] >> 16), z = (s16) (p[1] & 0xFFFF);
	s32 w = (s16) (p[1] >> 16), h = (s16) (p[2] & 0xFFFF), d = (s16) (p[2] >> 16);
	updateClip ();
	s64 cx[8], cy[8], cz[8], cw[8];
	for (int i = 0; i < 8; i++)
	{
		s64 px = x + ((i & 1) ? w : 0), py = y + ((i & 2) ? h : 0), pz = z + ((i & 4) ? d : 0);
		cx[i] = (px * clip[0] + py * clip[4] + pz * clip[8] + ((s64) clip[12] << 12)) >> 12;
		cy[i] = (px * clip[1] + py * clip[5] + pz * clip[9] + ((s64) clip[13] << 12)) >> 12;
		cz[i] = (px * clip[2] + py * clip[6] + pz * clip[10] + ((s64) clip[14] << 12)) >> 12;
		cw[i] = (px * clip[3] + py * clip[7] + pz * clip[11] + ((s64) clip[15] << 12)) >> 12;
	}
	// outside if every corner is beyond the same plane
	boxResult = true;
	for (int pl = 0; pl < 6; pl++)
	{
		bool allOut = true;
		for (int i = 0; i < 8 && allOut; i++)
		{
			s64 v = pl < 2 ? cx[i] : pl < 4 ? cy[i] : cz[i];
			bool out = (pl & 1) ? v < -cw[i] : v > cw[i];
			if (!out) allOut = false;
		}
		if (allOut) { boxResult = false; break; }
	}
}

} // namespace nds
