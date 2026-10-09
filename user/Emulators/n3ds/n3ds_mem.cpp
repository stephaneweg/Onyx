//
// n3ds/n3ds_mem.cpp -- the process's memory: one host pointer a 4 KB guest page (the table Dynarmic's code reads
// too), the 128 MB of FCRAM and the VRAM behind it. FCRAM is taken from both ends: the linear heap from its start
// (a linear address is its FCRAM offset: the GPU is given such addresses), everything else from its end.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

bool Memory::init ()
{
	pages = (u8 **) calloc (PAGE_COUNT, sizeof (u8 *));
	perms = (u8 *) calloc (PAGE_COUNT, 1);
	fcram = (u8 *) calloc (FCRAM_SIZE, 1);
	vram = (u8 *) calloc (VRAM_SIZE, 1);
	linearUsed = topUsed = 0; faults = 0; faultAddr = 0;
	if (!pages || !perms || !fcram || !vram) { quit (); return false; }
	map (VA_VRAM, VRAM_SIZE, vram, PERM_RW);
	return true;
}

void Memory::quit ()
{
	free (pages); free (perms); free (fcram); free (vram);
	pages = 0; perms = 0; fcram = 0; vram = 0;
}

u8 *Memory::allocTop (u32 size)
{
	size = (size + PAGE_SIZE - 1) & ~(u32) (PAGE_SIZE - 1);
	if (size > FCRAM_SIZE - linearUsed - topUsed) return 0;
	if (FCRAM_SIZE - topUsed - size < FONT_FCRAM + FONT_SIZE) return 0;		// (the shared font's place stays free)
	topUsed += size;
	u8 *p = fcram + FCRAM_SIZE - topUsed;
	memset (p, 0, size);
	return p;
}

bool Memory::map (u32 va, u32 size, u8 *host, int perm)
{
	if ((va & (PAGE_SIZE - 1)) || !host) return false;
	u32 n = (size + PAGE_SIZE - 1) >> PAGE_BITS, p = va >> PAGE_BITS;
	if (p + n > PAGE_COUNT) return false;
	for (u32 i = 0; i < n; i++) { pages[p + i] = host + (size_t) i * PAGE_SIZE; perms[p + i] = (u8) perm; }
	return true;
}

void Memory::unmap (u32 va, u32 size)
{
	u32 n = (size + PAGE_SIZE - 1) >> PAGE_BITS, p = va >> PAGE_BITS;
	for (u32 i = 0; i < n && p + i < PAGE_COUNT; i++) { pages[p + i] = 0; perms[p + i] = 0; }
}

bool Memory::mapped (u32 va, u32 size) const
{
	if (!size) return true;
	u32 p0 = va >> PAGE_BITS, p1 = (va + size - 1) >> PAGE_BITS;
	if (va + size - 1 < va) return false;
	for (u32 p = p0; p <= p1; p++) if (!pages[p]) return false;
	return true;
}

// (a value may straddle two pages that are not neighbours in the host: byte by byte then)
bool Memory::read (u32 va, void *dst, u32 n) const
{
	u8 *d = (u8 *) dst;
	while (n)
	{
		u8 *p = ptr (va);
		u32 k = PAGE_SIZE - (va & (PAGE_SIZE - 1)); if (k > n) k = n;
		if (!p) { memset (d, 0, n); const_cast<Memory *> (this)->faults++; const_cast<Memory *> (this)->faultAddr = va; return false; }
		memcpy (d, p, k); d += k; va += k; n -= k;
	}
	return true;
}

bool Memory::write (u32 va, const void *src, u32 n)
{
	const u8 *s = (const u8 *) src;
	while (n)
	{
		u8 *p = ptr (va);
		u32 k = PAGE_SIZE - (va & (PAGE_SIZE - 1)); if (k > n) k = n;
		if (!p) { faults++; faultAddr = va; return false; }
		memcpy (p, s, k); s += k; va += k; n -= k;
	}
	return true;
}

bool Memory::fill (u32 va, u8 v, u32 n)
{
	while (n)
	{
		u8 *p = ptr (va);
		u32 k = PAGE_SIZE - (va & (PAGE_SIZE - 1)); if (k > n) k = n;
		if (!p) { faults++; faultAddr = va; return false; }
		memset (p, v, k); va += k; n -= k;
	}
	return true;
}

u8 Memory::r8 (u32 va) const { u8 v = 0; read (va, &v, 1); return v; }
u16 Memory::r16 (u32 va) const { u16 v = 0; read (va, &v, 2); return v; }
u32 Memory::r32 (u32 va) const { u32 v = 0; read (va, &v, 4); return v; }
u64 Memory::r64 (u32 va) const { u64 v = 0; read (va, &v, 8); return v; }
void Memory::w8 (u32 va, u8 v) { write (va, &v, 1); }
void Memory::w16 (u32 va, u16 v) { write (va, &v, 2); }
void Memory::w32 (u32 va, u32 v) { write (va, &v, 4); }
void Memory::w64 (u32 va, u64 v) { write (va, &v, 8); }

}
