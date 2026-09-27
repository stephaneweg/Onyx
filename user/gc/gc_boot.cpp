//
// gc/gc_boot.cpp -- starting a program as the IPL does: the low memory it fills (the disc's ID,
// the boot magic, the memory size, the console type, the video mode, the clocks), the MSR and
// BATs; a .dol's sections loaded; a disc image booted through its apploader -- the apploader's
// own code run by the CPU (its init, then main until it has loaded everything, reading the disc
// for it, then close, which gives the game's entry point).
//
#include "gc/gc.h"

namespace gc {

static inline u32 be32 (const u8 *p) { return (u32) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

enum { MAGIC_RET = 0x80001800 };				// a function called from here returns there

void Machine::hleBootState ()
{
	auto w32 = [&] (u32 a, u32 v) { write32 (a, v); };
	msr = 0x00002032;					// FP, IR, DR, RI
	w32 (0x80000020, 0x0D15EA5E);				// a normal boot
	w32 (0x80000024, 0x00000001);
	w32 (0x80000028, MEM1_SIZE);				// the memory
	w32 (0x8000002C, 0x00000003);				// a retail console
	w32 (0x80000030, 0x00000000);				// ArenaLo (set by the OS)
	w32 (0x80000034, 0x81800000);				// ArenaHi
	w32 (0x800000CC, pal ? 1 : 0);				// the video mode
	w32 (0x800000D0, ARAM_SIZE);
	w32 (0x800000EC, 0x81800000);
	w32 (0x800000F0, MEM1_SIZE);				// the simulated memory size
	w32 (0x800000F8, BUS_HZ);
	w32 (0x800000FC, CPU_HZ);
	write16 (0x800030E0, 6);				// (the pads: which are plugged -- the IPL's word)
	w32 (0x800030E4, 0x00008201);
	// the exception vectors: an "rfi" each until the OS installs its own
	for (u32 v = 0x100; v < 0x1400; v += 0x100) w32 (0x80000000 + v, 0x4C000064);
	// a "blr" to return to (MAGIC_RET) and the apploader's report function (a blr too)
	w32 (MAGIC_RET, 0x4E800020);
	w32 (MAGIC_RET + 4, 0x4E800020);
	gpr[1] = 0x816FFFF0;					// a stack
}

// a .dol: 7 text + 11 data sections (offsets 0x00, addresses 0x48, sizes 0x90), the bss (0xD8,
// 0xDC), the entry (0xE0)
bool Machine::loadDol (const u8 *d, u32 size)
{
	if (size < 0x100) return false;
	reset ();
	hleBootState ();
	for (int i = 0; i < 18; i++)
	{
		u32 off = be32 (d + i * 4), addr = be32 (d + 0x48 + i * 4), len = be32 (d + 0x90 + i * 4);
		if (!len) continue;
		if (off + len > size || (addr & 0x01FFFFFF) + len > MEM1_SIZE) return false;
		for (u32 k = 0; k < len; k++) mem1[(addr & 0x01FFFFFF) + k] = d[off + k];
	}
	u32 bss = be32 (d + 0xD8), bssLen = be32 (d + 0xDC);
	for (u32 k = 0; k < bssLen && (bss & 0x01FFFFFF) + k < MEM1_SIZE; k++) mem1[(bss & 0x01FFFFFF) + k] = 0;
	pc = npc = be32 (d + 0xE0);
	return true;
}

// Run the guest function at addr (r3.. set by the caller) until it returns; false if it did not.
static bool callGuest (Machine &m, u32 addr)
{
	m.lr = MAGIC_RET; m.pc = addr;
	for (u64 guard = 0; guard < 200000000ull && m.pc != MAGIC_RET && !m.halted; guard++) m.step ();
	return m.pc == MAGIC_RET;
}

// a disc image (GCM): the header (the game's ID, its name), the apploader at 0x2440
bool Machine::loadDisc (const u8 *iso, u32 size)
{
	disc = iso; discSize = size;
	return loadDiscImage (size);
}

bool Machine::loadDiscImage (u32 size)
{
	static u8 hdr[0x2460];
	if (size < 0x2460) return false;
	if (disc) for (u32 i = 0; i < 0x2460; i++) hdr[i] = disc[i];
	else if (!discRead || !discRead (discCtx, 0, 0x2460, hdr)) return false;
	const u8 *iso = hdr;
	if (be32 (iso + 0x1C) != 0xC2339F3D) return false;
	discSize = size;
	for (int i = 0; i < 6; i++) gameId[i] = (char) iso[i];
	gameId[6] = 0;
	int n = 0;
	for (; n < 63 && iso[0x20 + n]; n++) title[n] = (char) iso[0x20 + n];
	title[n] = 0;
	pal = iso[3] == 'P' || iso[3] == 'D' || iso[3] == 'F' || iso[3] == 'S' || iso[3] == 'I' || iso[3] == 'X' || iso[3] == 'Y';
	reset ();
	hleBootState ();
	for (int i = 0; i < 0x20; i++) write8 (0x80000000 + (u32) i, iso[i]);	// the disc's ID
	// the apploader: its header (date, entry, size, trailer), its code at 0x81200000
	u32 entry = be32 (iso + 0x2450), len = be32 (iso + 0x2454) + be32 (iso + 0x2458);
	if (0x2460 + len > size) return false;
	readDisc (0x2460, len, 0x81200000);
	// entry (r3, r4, r5 = where it writes its init, main, close functions)
	gpr[3] = 0x80001810; gpr[4] = 0x80001814; gpr[5] = 0x80001818;
	if (!callGuest (*this, entry)) return false;
	u32 fInit = read32 (0x80001810), fMain = read32 (0x80001814), fClose = read32 (0x80001818);
	gpr[3] = MAGIC_RET + 4;					// init (the report function: a blr)
	if (!callGuest (*this, fInit)) return false;
	for (int k = 0; k < 1000; k++)				// main: each call asks for a read (dst, len, offset)
	{
		gpr[3] = 0x80001820; gpr[4] = 0x80001824; gpr[5] = 0x80001828;
		if (!callGuest (*this, fMain)) return false;
		u32 more = gpr[3];
		u32 dst = read32 (0x80001820), rlen = read32 (0x80001824), off = read32 (0x80001828);
		if (rlen) readDisc (off, rlen, dst);
		if (!more) break;
	}
	if (!callGuest (*this, fClose)) return false;
	pc = npc = gpr[3];					// the game's entry
	gpr[1] = 0x816FFFF0;
	cycles = 0; decAt = ~0ull; decPending = false;
	viNextLine = viCyclesLine;
	return true;
}

} // namespace gc
