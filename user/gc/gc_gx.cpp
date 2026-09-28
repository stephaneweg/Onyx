//
// gc/gc_gx.cpp -- the command processor and the pixel engine's registers: the GX FIFO (in
// memory, written by the CPU through the write-gather pipe) read command by command -- NOP, the
// CP registers (the vertex descriptor VCD, the attribute formats VAT, the array bases and
// strides), the XF registers and matrices (direct and indexed loads), the BP registers (the
// TEV, the textures, the "draw done" and the tokens, the EFB copies), display lists, the
// primitives with their vertices (their size from the VCD and the VAT) handed to the drawing
// (gxPrimitive) -- and the PE's interrupts (token, finish) that the games wait for.
//
#include "gc/gc.h"

namespace gc {

enum { PI_PE_TOKEN = 0x200, PI_PE_FINISH = 0x400 };

static inline u32 be32 (const u8 *p) { return (u32) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static inline u32 be16 (const u8 *p) { return (u32) p[0] << 8 | p[1]; }

// ---- the CP's registers (MMIO, 16-bit) --------------------------------------------------------------------------
// 0x00 status, 0x02 control, 0x04 clear, 0x20-0x3F the FIFO: base, end, high / low watermarks,
// read-write distance, write pointer, read pointer, breakpoint (lo, hi halves)
u32 Machine::cpRead (u32 off, int size)
{
	off &= 0x7F;
	if (size == 4) return cpRead (off, 2) << 16 | cpRead (off + 2, 2);
	gxFifoKick ();
	switch (off)
	{
	case 0x00: return 0x0014 | 0x0008;				// read idle, command idle (all done)
	case 0x20: return cpFifoBase & 0xFFFF;
	case 0x22: return cpFifoBase >> 16;
	case 0x24: return cpFifoEnd & 0xFFFF;
	case 0x26: return cpFifoEnd >> 16;
	case 0x30: case 0x32: return 0;				// the read-write distance: nothing waiting
	case 0x34: return cpFifoWptr & 0xFFFF;
	case 0x36: return cpFifoWptr >> 16;
	case 0x38: return cpFifoRptr & 0xFFFF;
	case 0x3A: return cpFifoRptr >> 16;
	}
	return cpReg16[off / 2];
}

void Machine::cpWrite (u32 off, u32 v, int size)
{
	off &= 0x7E;
	if (size == 4) { cpWrite (off, v >> 16, 2); cpWrite (off + 2, v & 0xFFFF, 2); return; }
	cpReg16[off / 2] = (u16) v;
	switch (off)
	{
	case 0x20: cpFifoBase = (cpFifoBase & 0xFFFF0000) | (v & 0xFFE0); break;
	case 0x22: cpFifoBase = (cpFifoBase & 0xFFFF) | (v & 0x3FF) << 16; break;
	case 0x24: cpFifoEnd = (cpFifoEnd & 0xFFFF0000) | (v & 0xFFE0); break;
	case 0x26: cpFifoEnd = (cpFifoEnd & 0xFFFF) | (v & 0x3FF) << 16; break;
	case 0x34: cpFifoWptr = (cpFifoWptr & 0xFFFF0000) | (v & 0xFFE0); break;
	case 0x36: cpFifoWptr = (cpFifoWptr & 0xFFFF) | (v & 0x3FF) << 16; break;
	case 0x38: cpFifoRptr = (cpFifoRptr & 0xFFFF0000) | (v & 0xFFE0); break;
	case 0x3A: cpFifoRptr = (cpFifoRptr & 0xFFFF) | (v & 0x3FF) << 16; break;
	}
}

// ---- the PE's registers: 0x0A the interrupts (TOKEN_ENABLE 1, FINISH_ENABLE 2, TOKEN 4, FINISH 8),
// 0x0E the last token -----------------------------------------------------------------------------------------
u32 Machine::peRead (u32 off, int size)
{
	off &= 0x7F;
	if (size == 4) return peRead (off, 2) << 16 | peRead (off + 2, 2);
	gxFifoKick ();
	return peReg16[off / 2];
}

void Machine::peWrite (u32 off, u32 v, int size)
{
	off &= 0x7E;
	if (size == 4) { peWrite (off, v >> 16, 2); peWrite (off + 2, v & 0xFFFF, 2); return; }
	if (off == 0x0A)
	{
		u16 &st = peReg16[0x0A / 2];
		st = (u16) ((st & ~(v & 0xC)) & 0xC) | (u16) (v & 3);
		if (!((st & 4) && (st & 1))) piLower (PI_PE_TOKEN);
		if (!((st & 8) && (st & 2))) piLower (PI_PE_FINISH);
		return;
	}
	peReg16[off / 2] = (u16) v;
}

// ---- the FIFO -------------------------------------------------------------------------------------------------
// Linked to the CPU (the games' normal mode), the GP reads what the CPU has written: from the CP's
// read pointer up to the PI's write pointer, wrapping at the FIFO's end. A command cut by the
// wrap (or not all written yet) waits for the rest.
void Machine::gxFifoKick ()
{
	GcTimed timed (timeFifo);
	if (!cpFifoEnd || !(cpReg16[0x02 / 2] & 1)) { cpFifoRptr = piFifoWptr & 0x03FFFFE0 & ~0x20000000u; return; }
	u32 w = piFifoWptr & 0x01FFFFFF, base = cpFifoBase & 0x01FFFFFF, end = cpFifoEnd & 0x01FFFFFF;
	if (end <= base || end > MEM1_SIZE) return;
	u32 r = cpFifoRptr & 0x01FFFFFF;
	if (r < base || r >= end + 32) r = base;
	static u8 tmp[64 * 1024 + 64];
	for (int guard = 0; guard < 1000000 && r != w; guard++)
	{
		if (r >= end + 32 || r >= end) r = base;		// (the GP wraps at the end)
		if (r == w) break;
		// the bytes available from r (up to w, or to the end then from the base)
		u32 avail = w >= r ? w - r : (end - r) + (w - base);
		if (avail > sizeof tmp - 64) avail = sizeof tmp - 64;
		const u8 *p;
		if (w >= r || end - r >= avail) p = mem1 + r;
		else
		{
			u32 first = end - r;
			for (u32 i = 0; i < first; i++) tmp[i] = mem1[r + i];
			for (u32 i = first; i < avail; i++) tmp[i] = mem1[base + i - first];
			p = tmp;
		}
		int n = gxCommand (p, (int) avail, false);
		if (n <= 0) break;					// (not all there yet)
		r += (u32) n;
		if (r >= end) r = base + (r - end);
	}
	cpFifoRptr = r;
	cpFifoWptr = w;
}

// the size of a vertex in the given VAT format (from the VCD)
int Machine::gxVertexSize (int vat)
{
	u32 lo = cpRegs[0x50], hi = cpRegs[0x60];
	u32 a = cpRegs[0x70 + vat], b = cpRegs[0x80 + vat], c = cpRegs[0x90 + vat];
	static const int csize[8] = { 1, 1, 2, 2, 4, 0, 0, 0 };
	static const int colsize[8] = { 2, 3, 4, 2, 3, 4, 0, 0 };
	int n = 0;
	if (lo & 1) n++;						// the position matrix index
	for (int i = 0; i < 8; i++) if (lo & (2 << i)) n++;		// the texture matrix indices
	int t = (int) (lo >> 9) & 3;					// the position
	if (t == 1) n += ((a & 1) ? 3 : 2) * csize[(a >> 1) & 7];
	else if (t) n += t == 2 ? 1 : 2;
	t = (int) (lo >> 11) & 3;					// the normal (NBT: 3 vectors)
	if (t)
	{
		bool nbt = (a >> 9) & 1, idx3 = (a >> 31) & 1;
		if (t == 1) n += (nbt ? 9 : 3) * csize[(a >> 10) & 7];
		else n += (t == 2 ? 1 : 2) * (nbt && idx3 ? 3 : 1);
	}
	for (int k = 0; k < 2; k++)					// the two colours
	{
		t = (int) (lo >> (13 + 2 * k)) & 3;
		if (t == 1) n += colsize[(a >> (14 + 4 * k)) & 7];
		else if (t) n += t == 2 ? 1 : 2;
	}
	// the eight texture coordinates: counts and formats spread over VAT A / B / C
	static const int cntBit[8] = { 21, 0, 9, 18, 27, 5, 14, 23 };
	static const int fmtBit[8] = { 22, 1, 10, 19, 28, 6, 15, 24 };
	static const int reg[8] = { 0, 1, 1, 1, 1, 2, 2, 2 };
	for (int i = 0; i < 8; i++)
	{
		t = (int) (hi >> (2 * i)) & 3;
		if (!t) continue;
		u32 v = reg[i] == 0 ? a : reg[i] == 1 ? b : c;
		if (t == 1) n += (((v >> cntBit[i]) & 1) ? 2 : 1) * csize[(v >> fmtBit[i]) & 7];
		else n += t == 2 ? 1 : 2;
	}
	return n;
}

// one command at p: its length in bytes (0: not all of it is there yet)
int Machine::gxCommand (const u8 *p, int avail, bool inDl)
{
	if (avail < 1) return 0;
	u8 op = p[0];
	gxCmds++;
	switch (op)
	{
	case 0x00: return 1;						// NOP
	case 0x48: return 1;						// invalidate the vertex cache
	case 0x08:							// a CP register: address, value
		if (avail < 6) return 0;
		{
			u32 r = p[1], v = be32 (p + 2);
			cpRegs[r] = v;
			if (r >= 0x50 && r < 0x58) cpRegs[0x50] = v;	// (the VCD is mirrored)
			if (r >= 0x60 && r < 0x68) cpRegs[0x60] = v;
		}
		return 6;
	case 0x10:							// XF registers / memory: count - 1, address, values
	{
		if (avail < 5) return 0;
		u32 h = be32 (p + 1);
		int n = (int) ((h >> 16) & 0xF) + 1;
		if (avail < 5 + n * 4) return 0;
		gxXf (h & 0xFFFF, n, p + 5);
		return 5 + n * 4;
	}
	case 0x20: case 0x28: case 0x30: case 0x38:			// indexed XF loads (matrices, lights)
	{
		if (avail < 5) return 0;
		u32 h = be32 (p + 1);
		int arr = 12 + ((op - 0x20) >> 3);			// the arrays 12..15
		u32 idx = h >> 16, n = ((h >> 12) & 0xF) + 1, addr = h & 0xFFF;
		u32 src = (cpRegs[0xA0 + arr] & 0x03FFFFFF) + idx * (cpRegs[0xB0 + arr] & 0xFF);
		u8 buf[64];
		for (u32 i = 0; i < n * 4 && i < 64; i++) buf[i] = src + i < MEM1_SIZE ? mem1[src + i] : 0;
		gxXf (addr, (int) n, buf);
		return 5;
	}
	case 0x40:							// call a display list
		if (avail < 9) return 0;
		if (!inDl) gxRunDl (be32 (p + 1), be32 (p + 5));
		return 9;
	case 0x44: return 1;						// (a metrics command)
	case 0x61:							// a BP register
		if (avail < 5) return 0;
		gxBp (be32 (p + 1));
		return 5;
	}
	if (op >= 0x80 && op < 0xC0)					// a primitive: its vertices
	{
		if (avail < 3) return 0;
		int vat = op & 7, prim = (op >> 3) & 7, count = (int) be16 (p + 1);
		int vs = gxVertexSize (vat), len = 3 + count * vs;
		if (avail < len) return 0;
		gxPrims++; gxVerts += (u32) count;
		gxPrimitive (prim, vat, count, p + 3);
		return len;
	}
	// an unknown command: skipped (1 byte), as the hardware would report an error
	return 1;
}

void Machine::gxRunDl (u32 addr, u32 size)
{
	addr &= 0x01FFFFFF;
	if (addr + size > MEM1_SIZE) return;
	const u8 *p = mem1 + addr;
	int left = (int) size;
	while (left > 0)
	{
		int n = gxCommand (p, left, true);
		if (n <= 0) break;
		p += n; left -= n;
	}
}

void Machine::gxXf (u32 addr, int n, const u8 *data)
{
	if (addr < 0x1000) xfSerial++;					// (a GPU's copies: the matrices, the lights,
	if (addr + (u32) n > 0x1000) gxsDirty = true;			// the registers' state)
	for (int i = 0; i < n; i++, addr++)
		if (addr < 0x1100) xfRegs[addr] = be32 (data + i * 4);
}

// the BP registers: the mask register 0xFE applies to the next write
void Machine::gxBp (u32 v)
{
	u32 r = v >> 24, val = v & 0xFFFFFF;
	gxsDirty = true;
	if (r >= 0xE0 && r <= 0xE7 && (val & 0x800000)) { bpKonst[r - 0xE0] = val; return; }	// (a konst colour)
	u32 m = bpRegs[0xFE] ? bpRegs[0xFE] : 0xFFFFFF;
	if (r != 0xFE) { val = (bpRegs[r] & ~m) | (val & m); bpRegs[0xFE] = 0xFFFFFF; }
	bpRegs[r] = val;
	switch (r)
	{
	case 0x45:							// PE_DONE: the drawing is finished
	{
		u16 &st = peReg16[0x0A / 2];
		st |= 8;
		if (st & 2) piRaise (PI_PE_FINISH);
		break;
	}
	case 0x47: peReg16[0x0E / 2] = (u16) val; break;		// PE_TOKEN
	case 0x48:							// PE_TOKEN_INT
	{
		peReg16[0x0E / 2] = (u16) val;
		u16 &st = peReg16[0x0A / 2];
		st |= 4;
		if (st & 1) piRaise (PI_PE_TOKEN);
		break;
	}
	case 0x52: gxCopy (val); break;					// an EFB copy
	case 0x65:							// load a TLUT: from memory (0x64) into the TMEM
	{
		u32 src = (bpRegs[0x64] & 0x1FFFFF) << 5, dst = (val & 0x3FF) << 9, n = (val & 0x1FFC00) >> 5;
		for (u32 i = 0; i < n && src + i < MEM1_SIZE && dst + i < 0x100000; i++) tmem[dst + i] = mem1[src + i];
		texEpoch++;						// (the textures with a palette: looked up again)
		break;
	}
	}
}

} // namespace gc
