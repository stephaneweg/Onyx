//
// n3ds/n3ds_loader.cpp -- what a program is loaded from: an ELF (32-bit ARM, linked at its place: our own test
// programs, tools/tests/n3ds/src) or a .3dsx (the homebrew's format: three segments -- code, constants, data --
// made to be put anywhere, with the words to adjust listed after them, and a RomFS at its end). A game's format
// (NCCH / NCSD) comes next.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

static u16 le16 (const u8 *p) { return (u16) (p[0] | p[1] << 8); }
static u32 le32 (const u8 *p) { return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16 | (u32) p[3] << 24; }
static void put32 (u8 *p, u32 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); p[2] = (u8) (v >> 16); p[3] = (u8) (v >> 24); }

bool Machine::load (const u8 *f, u32 size)
{
	if (size >= 4 && memcmp (f, "3DSX", 4) == 0) return load3dsx (f, size);
	if (size >= 4 && memcmp (f, "\177ELF", 4) == 0) return loadElf (f, size);
	if (size >= 0x104 && (memcmp (f + 0x100, "NCSD", 4) == 0 || memcmp (f + 0x100, "NCCH", 4) == 0))
	{
		fail ("a game's file (NCSD / NCCH): not loaded yet");
		return false;
	}
	fail ("not a 3DS program (.3dsx, .elf)");
	return false;
}

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

// A .3dsx: the header (its size, the relocation headers' size, the three segments' sizes -- the data's includes
// its zeroed part, whose size follows), then for a longer header where the icon and the RomFS are; a relocation
// header a segment (how many absolute and how many relative entries); the segments; then each segment's
// entries: "skip this many words, then adjust this many". A word to adjust holds an address as if the program
// were at 0, each segment on its own pages: absolute -> that address in the loaded program; relative -> its
// distance from the word itself.
bool Machine::load3dsx (const u8 *f, u32 size)
{
	if (size < 32 || memcmp (f, "3DSX", 4) != 0) { fail ("not a .3dsx program"); return false; }
	const u32 headerSize = le16 (f + 4), relocHeaderSize = le16 (f + 6);
	const u32 segSize[3] = { le32 (f + 16), le32 (f + 20), le32 (f + 24) }, bssSize = le32 (f + 28);
	if (headerSize < 32 || relocHeaderSize < 8 || bssSize > segSize[2]) { fail ("the .3dsx's header is wrong"); return false; }
	u32 pages[3], total = 0;
	for (int i = 0; i < 3; i++)
	{
		if (segSize[i] > 0x04000000) { fail ("the .3dsx's header is wrong"); return false; }
		pages[i] = (segSize[i] + PAGE_SIZE - 1) & ~(u32) (PAGE_SIZE - 1);
		total += pages[i];
	}
	const u32 relocHeaders = headerSize, segData = relocHeaders + 3 * relocHeaderSize;
	const u32 fileBytes[3] = { segSize[0], segSize[1], segSize[2] - bssSize };
	if (!total || segData > size || (u64) segData + fileBytes[0] + fileBytes[1] + fileBytes[2] > size) { fail ("the .3dsx is cut"); return false; }
	u8 *host = mem.allocTop (total);
	if (!host) { fail ("not enough memory for the program"); return false; }
	const u32 base = VA_CODE;
	u32 segOff[3] = { 0, pages[0], pages[0] + pages[1] };
	u32 pos = segData;
	for (int i = 0; i < 3; i++) { memcpy (host + segOff[i], f + pos, fileBytes[i]); pos += fileBytes[i]; }
	// the words to adjust
	for (int seg = 0; seg < 3; seg++)
	{
		const u8 *rh = f + relocHeaders + (u32) seg * relocHeaderSize;
		const u32 segEnd = segOff[seg] + pages[seg];
		for (u32 table = 0; table < relocHeaderSize / 4; table++)
		{
			u32 word = segOff[seg];					// (each table walks the segment from its start)
			const u32 count = le32 (rh + table * 4);
			if ((u64) pos + (u64) count * 4 > size) { fail ("the .3dsx's relocations are cut"); return false; }
			for (u32 e = 0; e < count; e++, pos += 4)
			{
				const u32 skip = le16 (f + pos), patch = le16 (f + pos + 2);
				word += skip * 4;
				for (u32 k = 0; k < patch && word + 4 <= segEnd; k++, word += 4)
				{
					if (table > 1) continue;				// (a kind we do not know: left)
					const u32 v = le32 (host + word), sub = v >> 28, addr = base + (v & 0x0FFFFFFF);
					if (table == 0) put32 (host + word, addr);
					else
					{
						const u32 rel = addr - (base + word);
						put32 (host + word, sub == 1 ? rel & 0x7FFFFFFF : rel);
					}
				}
			}
		}
	}
	mem.map (base + segOff[0], pages[0], host + segOff[0], PERM_RX);
	if (pages[1]) mem.map (base + segOff[1], pages[1], host + segOff[1], PERM_R);
	if (pages[2]) mem.map (base + segOff[2], pages[2], host + segOff[2], PERM_RW);
	// the RomFS, when the header says where it is
	if (headerSize >= 44)
	{
		const u32 fsOffset = le32 (f + 40);
		if (fsOffset && fsOffset < size) { romfs = f + fsOffset; romfsSize = size - fsOffset; }
	}
	return start (base, 0x40000);
}

}
