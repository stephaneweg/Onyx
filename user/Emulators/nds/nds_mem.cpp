//
// nds/nds_mem.cpp -- the two processors' memory maps: the TCMs (ARM9), the main memory (4 MB),
// the shared WRAM split by WRAMCNT, the ARM7's WRAM, the VRAM banks A..I mapped by VRAMCNT (16 KB
// pages: what each region shows, the textures' and the extended palettes' slots), the palettes,
// the OAM, the BIOS; and the fast page tables the interpreter and the JIT read and write through.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

const u32 Machine::bankOff[9] = { 0x00000, 0x20000, 0x40000, 0x60000, 0x80000, 0x90000, 0x94000, 0x98000, 0xA0000 };
const u32 Machine::bankSize[9] = { 0x20000, 0x20000, 0x20000, 0x20000, 0x10000, 0x4000, 0x4000, 0x8000, 0x4000 };

// ---- the VRAM ----------------------------------------------------------------------------------------------
void Machine::vramMap ()
{
	for (int i = 0; i < 32; i++) { mapABG[i] = 0; ptrABG[i] = 0; mapTex[i & 7] = 0; ptrTexPage[i] = 0; }
	for (int i = 0; i < 16; i++) { mapAOBJ[i] = 0; ptrAOBJ[i] = 0; mapARM7[i] = 0; ptrARM7[i] = 0; }
	for (int i = 0; i < 8; i++) { mapBBG[i] = 0; ptrBBG[i] = 0; mapBOBJ[i] = 0; ptrBOBJ[i] = 0; ptrTexPal[i] = 0; }
	for (int i = 0; i < 41; i++) mapLCDC[i] = 0;
	for (int i = 0; i < 4; i++) { ptrABGExt[i] = 0; ptrBBGExt[i] = 0; }
	ptrAOBJExt = ptrBOBJExt = 0;
	vramstat = 0;
	u16 texPalMap[8] = { 0 };
	for (int b = 0; b < 9; b++)
	{
		u8 c = vramcnt[b];
		if (!(c & 0x80)) continue;
		int mst = c & ((b <= BANK_B || b >= BANK_H) ? 3 : 7), ofs = (c >> 3) & 3;
		u8 *base = vram + bankOff[b];
		int np = (int) (bankSize[b] >> 14); if (!np) np = 1;
		u16 bit = (u16) (1 << b);
		#define MAP(arr, parr, n, start) for (int p = 0; p < np; p++) { int ix = (start) + p; if (ix >= 0 && ix < (n)) { if (!arr[ix]) parr[ix] = base + p * 0x4000; arr[ix] |= bit; } }
		if (mst == 0) { int s = (int) (bankOff[b] >> 14); for (int p = 0; p < np; p++) mapLCDC[s + p] |= bit; continue; }
		switch (b)
		{
		case BANK_A: case BANK_B:
			if (mst == 1) { MAP (mapABG, ptrABG, 32, ofs * 8); }
			else if (mst == 2) { MAP (mapAOBJ, ptrAOBJ, 16, (ofs & 1) * 8); }
			else { for (int p = 0; p < np; p++) if (!ptrTexPage[ofs * 8 + p]) ptrTexPage[ofs * 8 + p] = base + p * 0x4000; }
			break;
		case BANK_C: case BANK_D:
			if (mst == 1) { MAP (mapABG, ptrABG, 32, ofs * 8); }
			else if (mst == 2) { MAP (mapARM7, ptrARM7, 16, (ofs & 1) * 8); vramstat |= (u8) (b == BANK_C ? 1 : 2); }
			else if (mst == 3) { for (int p = 0; p < np; p++) if (!ptrTexPage[ofs * 8 + p]) ptrTexPage[ofs * 8 + p] = base + p * 0x4000; }
			else if (mst == 4)
			{
				if (b == BANK_C) { MAP (mapBBG, ptrBBG, 8, 0); }
				else { MAP (mapBOBJ, ptrBOBJ, 8, 0); }
			}
			break;
		case BANK_E:
			if (mst == 1) { MAP (mapABG, ptrABG, 32, 0); }
			else if (mst == 2) { MAP (mapAOBJ, ptrAOBJ, 16, 0); }
			else if (mst == 3) { for (int p = 0; p < 4; p++) if (!texPalMap[p]) { texPalMap[p] = bit; ptrTexPal[p] = base + p * 0x4000; } }
			else if (mst == 4) { for (int s = 0; s < 4; s++) if (!ptrABGExt[s]) ptrABGExt[s] = base + s * 0x2000; }
			break;
		case BANK_F: case BANK_G:
		{
			int pg = (ofs & 1) + 4 * (ofs >> 1);
			if (mst == 1) { MAP (mapABG, ptrABG, 32, pg); }
			else if (mst == 2) { MAP (mapAOBJ, ptrAOBJ, 16, pg); }
			else if (mst == 3) { if (!texPalMap[pg]) { texPalMap[pg] = bit; ptrTexPal[pg] = base; } }
			else if (mst == 4) { for (int s = 0; s < 2; s++) if (!ptrABGExt[(ofs & 1) * 2 + s]) ptrABGExt[(ofs & 1) * 2 + s] = base + s * 0x2000; }
			else if (mst == 5) { if (!ptrAOBJExt) ptrAOBJExt = base; }
			break;
		}
		case BANK_H:
			if (mst == 1) { MAP (mapBBG, ptrBBG, 8, 0); }
			else if (mst == 2) { for (int s = 0; s < 4; s++) if (!ptrBBGExt[s]) ptrBBGExt[s] = base + s * 0x2000; }
			break;
		case BANK_I:
			if (mst == 1) { MAP (mapBBG, ptrBBG, 8, 2); }
			else if (mst == 2) { MAP (mapBOBJ, ptrBOBJ, 8, 0); }
			else if (mst == 3) { if (!ptrBOBJExt) ptrBOBJExt = base; }
			break;
		}
		#undef MAP
	}
	pagesDirty = true;
}

// the ARM9's VRAM: the region's page -> its mask (0 none)
static inline const u16 *vregion (Machine *m, u32 a, int &ix)
{
	switch ((a >> 21) & 7)
	{
	case 0: ix = (int) (a >> 14) & 31; return m->mapABG;
	case 1: ix = (int) (a >> 14) & 7; return m->mapBBG;
	case 2: ix = (int) (a >> 14) & 15; return m->mapAOBJ;
	case 3: ix = (int) (a >> 14) & 7; return m->mapBOBJ;
	default: ix = (int) ((a & 0xFFFFF) >> 14); if (ix >= 41) return 0; return m->mapLCDC;
	}
}

u8 *Machine::vramPtr (u32 a)
{
	int ix; const u16 *map = vregion (this, a, ix);
	if (!map || !map[ix]) return 0;
	if (map == mapLCDC) return vram + (a & 0xFFFFF);
	u8 *const *ptrs = map == mapABG ? ptrABG : map == mapBBG ? ptrBBG : map == mapAOBJ ? ptrAOBJ : ptrBOBJ;
	return ptrs[ix] + (a & 0x3FFF);
}

u8 *Machine::vramPtr7 (u32 a)
{
	int ix = (int) (a >> 14) & 15;
	return mapARM7[ix] ? ptrARM7[ix] + (a & 0x3FFF) : 0;
}

// (several banks at one page: the first one only -- a game's mistake)
u32 Machine::vramRead16 (u32 a)
{
	u8 *p = vramPtr (a);
	return p ? rd16 (p) : 0;
}

void Machine::vramWrite16 (u32 a, u16 v)
{
	u8 *p = vramPtr (a);
	if (p) wr16 (p, v);
}

void Machine::vramWrite32 (u32 a, u32 v) { vramWrite16 (a, (u16) v); vramWrite16 (a + 2, (u16) (v >> 16)); }
void Machine::vramWrite8 (u32 a, u8 v)
{
	u8 *p = vramPtr (a);
	if (p) { u32 h = rd16 (p - (a & 1)); h = (a & 1) ? (h & 0xFF) | ((u32) v << 8) : (h & 0xFF00) | v; vramWrite16 (a & ~1u, (u16) h); }
}

// ---- the fast pages ------------------------------------------------------------------------------------------
static inline u8 *sharedWram9 (Machine *m, u32 a)
{
	switch (m->wramcnt & 3)
	{
	case 0: return m->wram + (a & 0x7FFF);
	case 1: return m->wram + 0x4000 + (a & 0x3FFF);
	case 2: return m->wram + (a & 0x3FFF);
	default: return 0;
	}
}
static inline u8 *sharedWram7 (Machine *m, u32 a)
{
	switch (m->wramcnt & 3)
	{
	case 0: return m->wram7 + (a & 0xFFFF);
	case 1: return m->wram + (a & 0x3FFF);
	case 2: return m->wram + 0x4000 + (a & 0x3FFF);
	default: return m->wram + (a & 0x7FFF);
	}
}

extern bool jitPageHasCode (Machine *m, const u8 *host);

void Machine::pagesUpdate ()
{
	pagesDirty = false;
	for (int cpu = 0; cpu < 2; cpu++)
	{
		u8 **rp = rdPage[cpu], **wp = wrPage[cpu];
		for (int i = 0; i < NPAGES; i++) rp[i] = wp[i] = 0;
		for (u32 pg = 0x02000000 >> PAGE_BITS; pg < (0x03000000u >> PAGE_BITS); pg++) rp[pg] = wp[pg] = mainRam + ((pg << PAGE_BITS) & 0x3FFFFF);
		for (u32 pg = 0x03000000 >> PAGE_BITS; pg < (0x04000000u >> PAGE_BITS); pg++)
		{
			u32 a = pg << PAGE_BITS;
			u8 *p = cpu ? (a >= 0x03800000 ? wram7 + (a & 0xFFFF) : sharedWram7 (this, a)) : sharedWram9 (this, a);
			rp[pg] = wp[pg] = p;
		}
		if (cpu == 0)
		{
			for (u32 pg = 0x06000000 >> PAGE_BITS; pg < (0x07000000u >> PAGE_BITS); pg++)
			{
				u32 a = pg << PAGE_BITS;
				int ix; const u16 *map = vregion (this, a, ix);
				if (map && map[ix] && !(map[ix] & (map[ix] - 1))) rp[pg] = wp[pg] = vramPtr (a);
			}
			// the TCMs over the rest (the DTCM first, the ITCM wins)
			if (arm9.dtcmSize >= 0x4000)
				for (u32 a = arm9.dtcmBase; a - arm9.dtcmBase < arm9.dtcmSize; a += 0x4000) { rp[a >> PAGE_BITS] = wp[a >> PAGE_BITS] = dtcm; if (a + 0x4000 < a) break; }
			else if (arm9.dtcmSize) rp[arm9.dtcmBase >> PAGE_BITS] = wp[arm9.dtcmBase >> PAGE_BITS] = 0;
			for (u32 a = 0; a < arm9.itcmSize; a += 0x4000) rp[a >> PAGE_BITS] = wp[a >> PAGE_BITS] = itcm + (a & 0x7FFF);
		}
		else
		{
			rp[0] = bios7;					// (the BIOS: read only)
			for (u32 pg = 0x06000000 >> PAGE_BITS; pg < (0x07000000u >> PAGE_BITS); pg++)
			{
				u32 a = pg << PAGE_BITS;
				int ix = (int) (a >> 14) & 15;
				if (mapARM7[ix] && !(mapARM7[ix] & (mapARM7[ix] - 1))) rp[pg] = wp[pg] = ptrARM7[ix];
			}
		}
		if (jit)						// (pages with translated code: written through the functions)
			for (int i = 0; i < NPAGES; i++) if (wp[i] && jitPageHasCode (this, wp[i])) wp[i] = 0;
	}
}

// ---- the ARM9 ---------------------------------------------------------------------------------------------------
#define TCM9R(a, rd)	if ((a) < arm9.itcmSize) return rd (itcm + ((a) & 0x7FFF)); \
			if ((a) - arm9.dtcmBase < arm9.dtcmSize) return rd (dtcm + ((a) & 0x3FFF));
static inline u32 rd8 (const u8 *p) { return *p; }

u32 Machine::a9Read32 (u32 a) { a &= ~3u; TCM9R (a, rd32) return bus9Read32 (a); }
u32 Machine::a9Read16 (u32 a) { a &= ~1u; TCM9R (a, rd16) return bus9Read16 (a); }
u32 Machine::a9Read8 (u32 a) { TCM9R (a, rd8) return bus9Read8 (a); }

void Machine::a9Write32 (u32 a, u32 v)
{
	a &= ~3u;
	if (a < arm9.itcmSize) { wr32 (itcm + (a & 0x7FFF), v); codeWritten (a, 0); return; }
	if (a - arm9.dtcmBase < arm9.dtcmSize) { wr32 (dtcm + (a & 0x3FFF), v); codeWritten (a, 0); return; }
	bus9Write32 (a, v);
}
void Machine::a9Write16 (u32 a, u32 v)
{
	a &= ~1u;
	if (a < arm9.itcmSize) { wr16 (itcm + (a & 0x7FFF), (u16) v); codeWritten (a, 0); return; }
	if (a - arm9.dtcmBase < arm9.dtcmSize) { wr16 (dtcm + (a & 0x3FFF), (u16) v); codeWritten (a, 0); return; }
	bus9Write16 (a, v);
}
void Machine::a9Write8 (u32 a, u32 v)
{
	if (a < arm9.itcmSize) { itcm[a & 0x7FFF] = (u8) v; codeWritten (a, 0); return; }
	if (a - arm9.dtcmBase < arm9.dtcmSize) { dtcm[a & 0x3FFF] = (u8) v; codeWritten (a, 0); return; }
	bus9Write8 (a, v);
}

u32 Machine::bus9Read32 (u32 a)
{
	a &= ~3u;
	switch (a >> 24)
	{
	case 0x02: return rd32 (mainRam + (a & 0x3FFFFF));
	case 0x03: { u8 *p = sharedWram9 (this, a); return p ? rd32 (p) : 0; }
	case 0x04: return io9Read32 (a);
	case 0x05: return rd32 (pal + (a & 0x7FF));
	case 0x06: { u8 *p = vramPtr (a); return p ? rd32 (p) : 0; }
	case 0x07: return rd32 (oam + (a & 0x7FF));
	case 0x08: case 0x09: case 0x0A: return 0xFFFFFFFF;	// (the GBA slot: empty)
	case 0xFF: if (a >= 0xFFFF0000) return rd32 (bios9 + (a & 0xFFF)); return 0;
	}
	return 0;
}
u32 Machine::bus9Read16 (u32 a)
{
	a &= ~1u;
	switch (a >> 24)
	{
	case 0x02: return rd16 (mainRam + (a & 0x3FFFFF));
	case 0x03: { u8 *p = sharedWram9 (this, a); return p ? rd16 (p) : 0; }
	case 0x04: return io9Read16 (a);
	case 0x05: return rd16 (pal + (a & 0x7FF));
	case 0x06: { u8 *p = vramPtr (a); return p ? rd16 (p) : 0; }
	case 0x07: return rd16 (oam + (a & 0x7FF));
	case 0x08: case 0x09: case 0x0A: return 0xFFFF;
	case 0xFF: if (a >= 0xFFFF0000) return rd16 (bios9 + (a & 0xFFF)); return 0;
	}
	return 0;
}
u32 Machine::bus9Read8 (u32 a)
{
	switch (a >> 24)
	{
	case 0x02: return mainRam[a & 0x3FFFFF];
	case 0x03: { u8 *p = sharedWram9 (this, a); return p ? *p : 0; }
	case 0x04: return io9Read8 (a);
	case 0x05: return pal[a & 0x7FF];
	case 0x06: { u8 *p = vramPtr (a); return p ? *p : 0; }
	case 0x07: return oam[a & 0x7FF];
	case 0x08: case 0x09: case 0x0A: return 0xFF;
	case 0xFF: if (a >= 0xFFFF0000) return bios9[a & 0xFFF]; return 0;
	}
	return 0;
}

void Machine::bus9Write32 (u32 a, u32 v)
{
	a &= ~3u;
	switch (a >> 24)
	{
	case 0x02: wr32 (mainRam + (a & 0x3FFFFF), v); codeWritten (a, 2); return;
	case 0x03: { u8 *p = sharedWram9 (this, a); if (p) { wr32 (p, v); codeWritten (a, 0); } return; }
	case 0x04: io9Write32 (a, v); return;
	case 0x05: wr32 (pal + (a & 0x7FF), v); return;
	case 0x06: vramWrite32 (a, v); codeWritten (a, 0); return;
	case 0x07: wr32 (oam + (a & 0x7FF), v); return;
	}
}
void Machine::bus9Write16 (u32 a, u32 v)
{
	a &= ~1u;
	switch (a >> 24)
	{
	case 0x02: wr16 (mainRam + (a & 0x3FFFFF), (u16) v); codeWritten (a, 2); return;
	case 0x03: { u8 *p = sharedWram9 (this, a); if (p) { wr16 (p, (u16) v); codeWritten (a, 0); } return; }
	case 0x04: io9Write16 (a, v); return;
	case 0x05: wr16 (pal + (a & 0x7FF), (u16) v); return;
	case 0x06: vramWrite16 (a, (u16) v); codeWritten (a, 0); return;
	case 0x07: wr16 (oam + (a & 0x7FF), (u16) v); return;
	}
}
void Machine::bus9Write8 (u32 a, u32 v)
{
	switch (a >> 24)
	{
	case 0x02: mainRam[a & 0x3FFFFF] = (u8) v; codeWritten (a, 2); return;
	case 0x03: { u8 *p = sharedWram9 (this, a); if (p) { *p = (u8) v; codeWritten (a, 0); } return; }
	case 0x04: io9Write8 (a, v); return;
	case 0x06: vramWrite8 (a, (u8) v); codeWritten (a, 0); return;
	}							// (8-bit writes to the palettes and the OAM: ignored)
}

// ---- the ARM7 -----------------------------------------------------------------------------------------------------
u32 Machine::a7Read32 (u32 a)
{
	a &= ~3u;
	switch (a >> 24)
	{
	case 0x00: if (a < 0x4000) return rd32 (bios7 + a); return 0;
	case 0x02: return rd32 (mainRam + (a & 0x3FFFFF));
	case 0x03: return rd32 (a >= 0x03800000 ? wram7 + (a & 0xFFFF) : sharedWram7 (this, a));
	case 0x04:
		if (a >= 0x04800000) { if ((a & 0xF000) >= 0x4000 && (a & 0xF000) < 0x6000) return rd32 (wifiRam + (a & 0x1FFF)); return rd32 (wifiReg + (a & 0xFFC)); }
		return io7Read32 (a);
	case 0x06: { u8 *p = vramPtr7 (a); return p ? rd32 (p) : 0; }
	case 0x08: case 0x09: case 0x0A: return 0xFFFFFFFF;
	}
	return 0;
}
u32 Machine::a7Read16 (u32 a)
{
	a &= ~1u;
	switch (a >> 24)
	{
	case 0x00: if (a < 0x4000) return rd16 (bios7 + a); return 0;
	case 0x02: return rd16 (mainRam + (a & 0x3FFFFF));
	case 0x03: return rd16 (a >= 0x03800000 ? wram7 + (a & 0xFFFF) : sharedWram7 (this, a));
	case 0x04:
		if (a >= 0x04800000) { if ((a & 0xF000) >= 0x4000 && (a & 0xF000) < 0x6000) return rd16 (wifiRam + (a & 0x1FFF)); return rd16 (wifiReg + (a & 0xFFE)); }
		return io7Read16 (a);
	case 0x06: { u8 *p = vramPtr7 (a); return p ? rd16 (p) : 0; }
	case 0x08: case 0x09: case 0x0A: return 0xFFFF;
	}
	return 0;
}
u32 Machine::a7Read8 (u32 a)
{
	switch (a >> 24)
	{
	case 0x00: if (a < 0x4000) return bios7[a]; return 0;
	case 0x02: return mainRam[a & 0x3FFFFF];
	case 0x03: return *(a >= 0x03800000 ? wram7 + (a & 0xFFFF) : sharedWram7 (this, a));
	case 0x04:
		if (a >= 0x04800000) { if ((a & 0xF000) >= 0x4000 && (a & 0xF000) < 0x6000) return wifiRam[a & 0x1FFF]; return wifiReg[a & 0xFFF]; }
		return io7Read8 (a);
	case 0x06: { u8 *p = vramPtr7 (a); return p ? *p : 0; }
	case 0x08: case 0x09: case 0x0A: return 0xFF;
	}
	return 0;
}

void Machine::a7Write32 (u32 a, u32 v)
{
	a &= ~3u;
	switch (a >> 24)
	{
	case 0x02: wr32 (mainRam + (a & 0x3FFFFF), v); codeWritten (a, 2); return;
	case 0x03: wr32 (a >= 0x03800000 ? wram7 + (a & 0xFFFF) : sharedWram7 (this, a), v); codeWritten (a, 1); return;
	case 0x04:
		if (a >= 0x04800000) { if ((a & 0xF000) >= 0x4000 && (a & 0xF000) < 0x6000) wr32 (wifiRam + (a & 0x1FFF), v); else wr32 (wifiReg + (a & 0xFFC), v); return; }
		io7Write32 (a, v); return;
	case 0x06: { u8 *p = vramPtr7 (a); if (p) wr32 (p, v); codeWritten (a, 1); return; }
	}
}
void Machine::a7Write16 (u32 a, u32 v)
{
	a &= ~1u;
	switch (a >> 24)
	{
	case 0x02: wr16 (mainRam + (a & 0x3FFFFF), (u16) v); codeWritten (a, 2); return;
	case 0x03: wr16 (a >= 0x03800000 ? wram7 + (a & 0xFFFF) : sharedWram7 (this, a), (u16) v); codeWritten (a, 1); return;
	case 0x04:
		if (a >= 0x04800000) { if ((a & 0xF000) >= 0x4000 && (a & 0xF000) < 0x6000) wr16 (wifiRam + (a & 0x1FFF), (u16) v); else wr16 (wifiReg + (a & 0xFFE), (u16) v); return; }
		io7Write16 (a, v); return;
	case 0x06: { u8 *p = vramPtr7 (a); if (p) wr16 (p, (u16) v); codeWritten (a, 1); return; }
	}
}
void Machine::a7Write8 (u32 a, u32 v)
{
	switch (a >> 24)
	{
	case 0x02: mainRam[a & 0x3FFFFF] = (u8) v; codeWritten (a, 2); return;
	case 0x03: *(a >= 0x03800000 ? wram7 + (a & 0xFFFF) : sharedWram7 (this, a)) = (u8) v; codeWritten (a, 1); return;
	case 0x04:
		if (a >= 0x04800000) { if ((a & 0xF000) >= 0x4000 && (a & 0xF000) < 0x6000) wifiRam[a & 0x1FFF] = (u8) v; else wifiReg[a & 0xFFF] = (u8) v; return; }
		io7Write8 (a, v); return;
	case 0x06: { u8 *p = vramPtr7 (a); if (p) *p = (u8) v; codeWritten (a, 1); return; }
	}
}

} // namespace nds
