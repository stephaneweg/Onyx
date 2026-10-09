//
// n3ds/n3ds_loader.cpp -- what a program is loaded from. This slice: an ELF (32-bit ARM, linked at its place:
// our own test programs, tools/tests/n3ds/src). The console's formats (.3dsx, NCCH / NCSD) come next.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

static u16 le16 (const u8 *p) { return (u16) (p[0] | p[1] << 8); }
static u32 le32 (const u8 *p) { return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16 | (u32) p[3] << 24; }

bool Machine::loadElf (const u8 *f, u32 size)
{
	if (size < 52 || memcmp (f, "\177ELF", 4) != 0 || f[4] != 1 || f[5] != 1 || le16 (f + 16) != 2 || le16 (f + 18) != 40)
	{
		fail ("not a 32-bit ARM ELF program");
		return false;
	}
	const u32 phoff = le32 (f + 28), phentsize = le16 (f + 42), phnum = le16 (f + 44);
	if (phentsize < 32 || phoff > size || (u64) phoff + (u64) phnum * phentsize > size) { fail ("the ELF's program headers are cut"); return false; }
	bool any = false;
	for (u32 i = 0; i < phnum; i++)
	{
		const u8 *ph = f + phoff + i * phentsize;
		if (le32 (ph) != 1) continue;					// PT_LOAD
		const u32 off = le32 (ph + 4), va = le32 (ph + 8), filesz = le32 (ph + 16), memsz = le32 (ph + 20), flags = le32 (ph + 24);
		if (!memsz) continue;
		if (filesz > memsz || off > size || filesz > size - off) { fail ("an ELF segment is cut"); return false; }
		const u32 base = va & ~(u32) (PAGE_SIZE - 1), span = va - base + memsz;
		if (base < VA_CODE || base + span > VA_HEAP || base + span < base) { fail ("an ELF segment at %08x is outside the program's space", (unsigned) va); return false; }
		if (mem.pages[base >> PAGE_BITS]) { fail ("two ELF segments share the page %08x", (unsigned) base); return false; }
		u8 *host = mem.allocTop (span);
		if (!host) { fail ("not enough memory for the program"); return false; }
		memcpy (host + (va - base), f + off, filesz);
		int perm = ((flags & 4) ? PERM_R : 0) | ((flags & 2) ? PERM_W : 0) | ((flags & 1) ? PERM_X : 0);
		mem.map (base, span, host, perm);
		any = true;
	}
	if (!any) { fail ("the ELF has nothing to load"); return false; }
	return start (le32 (f + 24), 0x10000);
}

}
