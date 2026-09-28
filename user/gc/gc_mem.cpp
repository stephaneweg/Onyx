//
// gc/gc_mem.cpp -- the Gekko's view of memory: the BAT translation (the games map their memory
// with the four instruction and four data BATs: 0x80000000 cached and 0xC0000000 uncached
// onto MEM1 and the hardware), kept as a map of 128 KB blocks rebuilt when a BAT or the MSR
// changes; then the physical map: MEM1 (24 MB, mirrored up to 32 MB... not: 0..0x017FFFFF),
// the hardware registers at 0x0C000000, the locked cache at 0xE0000000.
//
#include "gc/gc.h"

namespace gc {

static void zero (void *p, u32 n) { u8 *d = (u8 *) p; while (n--) *d++ = 0; }

Machine::Machine ()
{
	mem1 = new u8[MEM1_SIZE + 16];			// (+16: the JIT's unaligned accesses at the end)
	jit = 0; jitUntil = 0; jitFlush = false; jitBlocks = jitCompiles = 0; jitProfile = false;
	aram = new u8[ARAM_SIZE];
	disc = 0; discSize = 0; discRead = 0; discCtx = 0; pal = false; title[0] = 0; gameId[0] = 0;
	for (int i = 0; i < 2; i++) gfxFrame[i].v = 0, gfxFrame[i].b = 0;
	for (int i = 0; i < MAX_TEX; i++) tex[i].px = 0;
	texPool = new u32[TEX_POOL]; texPoolTop = 0; texFlushes = 0;
	timeFifo = timePrim = timeTex = 0;
	texEpoch = 1; for (int m = 0; m < 8; m++) texMemo[m].epoch = 0;
	tmem = 0;
	for (int i = 0; i < 4; i++) { padBtn[i] = 0; padSX[i] = padSY[i] = padCX[i] = padCY[i] = 0; padL[i] = padR[i] = 0; }
	for (int s = 0; s < 2; s++) { card[s].flash = 0; card[s].size = 0; }	// (the slots empty until the front end fills them)
	audioHostRate = 0;					// (no sound until the front end asks for it)
	gpu = 0;						// (the GX drawn for kapi gpu_render, until a front end has a GPU)
	reset ();
}

Machine::~Machine () { delete [] mem1; delete [] aram; delete [] texPool; }

void Machine::reset ()
{
	zero (mem1, MEM1_SIZE); zero (lcache, sizeof lcache);
	zero (gpr, sizeof gpr);
	for (int i = 0; i < 32; i++) ps[i][0] = ps[i][1] = 0.0;
	cr = lr = ctr = xer = fpscr = 0; fprfPending = false; fprfVal = 0.0;
	msr = 0x00002032;					// FP, IR, DR (as the IPL leaves it)
	pc = npc = 0x80003100;
	srr0 = srr1 = dar = dsisr = ear = dec = 0;
	for (int i = 0; i < 4; i++) sprg[i] = 0;
	pvr = 0x00083214;					// Gekko
	hid0 = 0x0011C464; hid1 = 0x80000000; hid2 = 0xE0000000; hid4 = 0;
	for (int i = 0; i < 8; i++) gqr[i] = 0;
	l2cr = wpar = dmaU = dmaL = mmcr0 = mmcr1 = ictc = 0;
	for (int i = 0; i < 4; i++) pmc[i] = 0;
	for (int i = 0; i < 3; i++) thrm[i] = 0;
	for (int i = 0; i < 16; i++) sr[i] = 0x80000000;
	sdr1 = 0;
	// the BATs the IPL sets: 0x80000000 -> 0 (256 MB, cached), 0xC0000000 -> 0 (uncached),
	// DBAT3 0xE0000000 -> the locked cache
	for (int i = 0; i < 8; i++) ibat[i] = dbat[i] = 0;
	ibat[0] = 0x80001FFF; ibat[1] = 0x00000002;
	ibat[2] = 0xC0001FFF; ibat[3] = 0x0000002A;
	dbat[0] = 0x80001FFF; dbat[1] = 0x00000002;
	dbat[2] = 0xC0001FFF; dbat[3] = 0x0000002A;
	dbat[6] = 0xE00001FE; dbat[7] = 0xE0000002;
	batRebuild ();
	tbBase = 0; cycles = 0; decAt = ~0ull; decPending = false; resv = false; resvAddr = 0; idleSkips = 0; curPc = pc; halted = false; haltMsg[0] = 0; extIrq = 0; memFault = false;
	jitFlush = true;
	hwReset ();
}

// BATxU: BEPI (bits 0-14: the effective 128 KB block), BL (bits 19-29: the size mask), Vs, Vp;
// BATxL: BRPN (bits 0-14: the physical block).
void Machine::batRebuild ()
{
	for (int m = 0; m < 2; m++)
	{
		u32 *map = m ? imap : dmap;
		const u32 *bat = m ? ibat : dbat;
		for (int i = 0; i < 0x8000; i++) map[i] = 0;
		for (int b = 3; b >= 0; b--)			// (BAT0 wins: applied last)
		{
			u32 up = bat[b * 2], lo = bat[b * 2 + 1];
			if (!(up & 2)) continue;			// Vs (the games run in supervisor mode)
			u32 bl = (up >> 2) & 0x7FF;			// 128 KB << n - 1
			u32 blocks = bl + 1, first = up >> 17, phys = lo >> 17;
			for (u32 k = 0; k < blocks && first + k < 0x8000; k++)
				map[(first & ~bl) + k] = ((phys & ~bl) + k) + 1;
		}
	}
	jitFlush = true;					// (the JIT's code assumed the old map)
}

bool Machine::translate (u32 ea, u32 &pa, bool data, bool)
{
	if (!(msr & (data ? 0x10 : 0x20))) { pa = ea; return true; }	// real mode
	u32 b = (data ? dmap : imap)[ea >> 17];
	if (!b) return false;
	pa = ((b - 1) << 17) | (ea & 0x1FFFF);
	return true;
}

u8 *Machine::ptr (u32 pa)
{
	if (pa < MEM1_SIZE) return mem1 + pa;
	if (pa >= 0xE0000000 && pa < 0xE0000000 + LCACHE_SIZE) return lcache + (pa - 0xE0000000);
	return 0;
}


// A failed translation raises a DSI (the games seldom do; one that does is stopped with a
// message until the page tables are there).
#define XLATE(write) \
	u32 pa; \
	if (!translate (ea, pa, true, write)) { memFault = true; dar = ea; dsisr = 0x40000000 | (write ? 0x02000000 : 0); exception (0x300, curPc); return 0; }

u8 Machine::read8 (u32 ea)
{
	XLATE (false)
	if (u8 *p = ptr (pa)) return *p;
	return (u8) hwRead (pa, 1);
}
u16 Machine::read16 (u32 ea)
{
	XLATE (false)
	if (u8 *p = ptr (pa)) return (u16) (p[0] << 8 | p[1]);
	return (u16) hwRead (pa, 2);
}
u32 Machine::read32 (u32 ea)
{
	XLATE (false)
	if (u8 *p = ptr (pa)) { if (!(pa & 3)) return bswap32 (*(const u32 *) p); return (u32) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
	return hwRead (pa, 4);
}
u64 Machine::read64 (u32 ea)
{
	u32 hi = read32 (ea);
	if (memFault) return 0;
	return (u64) hi << 32 | read32 (ea + 4);
}
#undef XLATE
#define XLATE(write) \
	u32 pa; \
	if (!translate (ea, pa, true, write)) { memFault = true; dar = ea; dsisr = 0x42000000; exception (0x300, curPc); return; }

void Machine::write8 (u32 ea, u8 v)
{
	XLATE (true)
	if (u8 *p = ptr (pa)) { *p = v; return; }
	hwWrite (pa, v, 1);
}
void Machine::write16 (u32 ea, u16 v)
{
	XLATE (true)
	if (u8 *p = ptr (pa)) { p[0] = (u8) (v >> 8); p[1] = (u8) v; return; }
	hwWrite (pa, v, 2);
}
void Machine::write32 (u32 ea, u32 v)
{
	XLATE (true)
	if (u8 *p = ptr (pa)) { if (!(pa & 3)) *(u32 *) p = bswap32 (v); else { p[0] = (u8) (v >> 24); p[1] = (u8) (v >> 16); p[2] = (u8) (v >> 8); p[3] = (u8) v; } return; }
	hwWrite (pa, v, 4);
}
void Machine::write64 (u32 ea, u64 v)
{
	write32 (ea, (u32) (v >> 32));
	if (!memFault) write32 (ea + 4, (u32) v);
}

u32 Machine::fetch (u32 ea)
{
	u32 pa;
	if (!translate (ea, pa, false, false)) { memFault = true; exception (0x400, curPc); return 0; }
	if (u8 *p = ptr (pa)) return bswap32 (*(const u32 *) p);
	memFault = true;
	int n = 0; const char *m = "fetch outside memory at ";
	while (m[n]) { haltMsg[n] = m[n]; n++; }
	for (int i = 7; i >= 0; i--) haltMsg[n++] = "0123456789ABCDEF"[(ea >> (i * 4)) & 15];
	haltMsg[n] = 0;
	halted = true;
	return 0;
}

} // namespace gc
