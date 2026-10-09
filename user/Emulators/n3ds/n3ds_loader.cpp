//
// n3ds/n3ds_loader.cpp -- what a program is loaded from: an ELF (32-bit ARM, linked at its place: our own test
// programs, tools/tests/n3ds/src) or a .3dsx (the homebrew's format: three segments -- code, constants, data --
// made to be put anywhere, with the words to adjust listed after them, and a RomFS at its end). A game's format
// (NCCH / NCSD) comes next.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

static u16 le16 (const u8 *p) { return (u16) (p[0] | p[1] << 8); }
static u32 le32 (const u8 *p) { return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16 | (u32) p[3] << 24; }
static void put32 (u8 *p, u32 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); p[2] = (u8) (v >> 16); p[3] = (u8) (v >> 24); }

static bool memRead (void *user, u64 offset, void *dst, u32 n)
{
	const Machine *m = (const Machine *) user;
	if (offset > m->memSize || n > m->memSize - offset) return false;
	memcpy (dst, m->memFile + offset, n);
	return true;
}

bool Machine::load (const u8 *f, u32 size)
{
	memFile = f; memSize = size;
	Source src = { this, size, memRead };
	return loadFrom (src);
}

bool Machine::loadFrom (const Source &src)
{
	source = src;
	u8 head[0x200];
	if (source.size < 0x34 || !source.read (source.user, 0, head, source.size < sizeof head ? (u32) source.size : (u32) sizeof head)) { fail ("the program cannot be read"); return false; }
	if (source.size >= 0x200 && memcmp (head + 0x100, "NCSD", 4) == 0)		// a cartridge's image: its first partition is the game
	{
		const u64 at = (u64) le32 (head + 0x120) * 0x200;
		if (!at || at >= source.size) { fail ("the game's image has no program"); return false; }
		return loadNcch (at);
	}
	if (source.size >= 0x200 && memcmp (head + 0x100, "NCCH", 4) == 0) return loadNcch (0);
	const bool x3 = memcmp (head, "3DSX", 4) == 0, elf = memcmp (head, "\177ELF", 4) == 0;
	if (!x3 && !elf) { fail ("not a 3DS program (.3ds, .cci, .cxi, .3dsx, .elf)"); return false; }
	if (source.size > 0x04000000) { fail ("the program is too big"); return false; }
	// a homebrew's file is small: read whole (its code is copied into the machine; its RomFS is read from the source)
	u8 *f = (u8 *) malloc ((size_t) source.size);
	if (!f || !source.read (source.user, 0, f, (u32) source.size)) { free (f); fail ("the program cannot be read"); return false; }
	const bool ok = x3 ? load3dsx (f, (u32) source.size) : loadElf (f, (u32) source.size);
	free (f);
	return ok;
}

// The code of a game is packed backwards ("BLZ"): from the file's end, a control byte then 8 items, each a
// byte to copy or a reference (12 bits of distance, 4 of length) to what is already unpacked after it. The
// last 8 bytes say where the packed part ends and how much bigger the result is.
static bool unpackCode (const u8 *in, u32 inSize, u8 *out, u32 outSize)
{
	if (inSize < 8) return false;
	const u32 ends = le32 (in + inSize - 8);
	u32 index = inSize - (ends >> 24), stop = inSize - (ends & 0xFFFFFF), o = outSize;
	if ((ends >> 24) > inSize || (ends & 0xFFFFFF) > inSize) return false;
	memset (out, 0, outSize);
	memcpy (out, in, inSize);
	while (index > stop)
	{
		u8 control = in[--index];
		for (int i = 0; i < 8; i++, control = (u8) (control << 1))
		{
			if (index <= stop || !index || !o) break;
			if (control & 0x80)
			{
				if (index < 2) return false;
				index -= 2;
				u32 ref = (u32) (in[index] | in[index + 1] << 8);
				const u32 n = (ref >> 12) + 3;
				ref = (ref & 0xFFF) + 2;
				if (o < n) return false;
				for (u32 k = 0; k < n; k++) { if (o + ref >= outSize) return false; const u8 b = out[o + ref]; out[--o] = b; }
			}
			else out[--o] = in[--index];
		}
	}
	return true;
}

// A game (NCCH): its header (where its extended header, its ExeFS and its RomFS are, whether it is encrypted),
// the extended header (the program's name, the three segments' addresses and sizes, the stack), the ExeFS's
// ".code" file -- the three segments one after the other, packed or not.
bool Machine::loadNcch (u64 at)
{
	u8 h[0x200], ex[0x400], efs[0x200];
	if (!source.read (source.user, at, h, sizeof h) || memcmp (h + 0x100, "NCCH", 4) != 0) { fail ("not a game's program (NCCH)"); return false; }
	if (!(h[0x188 + 7] & 4))
	{
		fail ("this game is encrypted: it needs a decrypted dump of your own cartridge (no key is in Onyx)");
		return false;
	}
	memcpy (productCode, h + 0x150, 16); productCode[16] = 0;
	const u64 exefsAt = at + (u64) le32 (h + 0x1A0) * 0x200, romfsAt = at + (u64) le32 (h + 0x1B0) * 0x200;
	const u64 romfsLen = (u64) le32 (h + 0x1B4) * 0x200;
	if (!le32 (h + 0x180) || !le32 (h + 0x1A4)) { fail ("this file has no program (data only)"); return false; }
	if (!source.read (source.user, at + 0x200, ex, sizeof ex) || !source.read (source.user, exefsAt, efs, sizeof efs)) { fail ("the game's headers cannot be read"); return false; }
	memcpy (title, ex, 8); title[8] = 0;
	const bool packed = (ex[0x0D] & 1) != 0;
	const u32 textAddr = le32 (ex + 0x10), textPages = le32 (ex + 0x14), stackSize = le32 (ex + 0x1C);
	const u32 roAddr = le32 (ex + 0x20), roPages = le32 (ex + 0x24);
	const u32 dataAddr = le32 (ex + 0x30), dataPages = le32 (ex + 0x34), dataSize = le32 (ex + 0x38), bssSize = le32 (ex + 0x3C);
	u32 codeOff = 0, codeSize = 0;
	for (int i = 0; i < 10; i++) if (memcmp (efs + i * 16, ".code\0\0\0", 8) == 0) { codeOff = le32 (efs + i * 16 + 8); codeSize = le32 (efs + i * 16 + 12); }
	if (!codeSize || codeSize > 0x04000000) { fail ("the game has no code (or it is not readable: an encrypted dump?)"); return false; }
	const u32 total = (textPages + roPages + dataPages) * PAGE_SIZE;
	if (textAddr != VA_CODE || !textPages || total > 0x04000000 || roAddr != textAddr + textPages * PAGE_SIZE || dataAddr != roAddr + roPages * PAGE_SIZE)
	{
		fail ("the game's segments are not where they are expected (text %08x, ro %08x, data %08x)", (unsigned) textAddr, (unsigned) roAddr, (unsigned) dataAddr);
		return false;
	}
	u8 *file = (u8 *) malloc (codeSize);
	if (!file || !source.read (source.user, exefsAt + 0x200 + codeOff, file, codeSize)) { free (file); fail ("the game's code cannot be read"); return false; }
	const u32 bssPages = ((dataSize + bssSize + PAGE_SIZE - 1) >> PAGE_BITS) > dataPages ? ((dataSize + bssSize + PAGE_SIZE - 1) >> PAGE_BITS) - dataPages : 0;
	u8 *host = mem.allocTop (total + bssPages * PAGE_SIZE);
	if (!host) { free (file); fail ("not enough memory for the game"); return false; }
	bool ok = true;
	if (packed)
	{
		const u32 outSize = codeSize + le32 (file + codeSize - 4);
		if (outSize > total) ok = false; else ok = unpackCode (file, codeSize, host, outSize);
	}
	else memcpy (host, file, codeSize < total ? codeSize : total);
	free (file);
	if (!ok) { fail ("the game's code cannot be unpacked"); return false; }
	mem.map (textAddr, textPages * PAGE_SIZE, host, PERM_RX);
	if (roPages) mem.map (roAddr, roPages * PAGE_SIZE, host + textPages * PAGE_SIZE, PERM_R);
	if (dataPages + bssPages) mem.map (dataAddr, (dataPages + bssPages) * PAGE_SIZE, host + (textPages + roPages) * PAGE_SIZE, PERM_RW);
	// its RomFS: the program is given it from its third level on (after the 0x1000 bytes of its hash header)
	if (romfsLen > 0x1000 && romfsAt + romfsLen <= source.size) { romfsBase = romfsAt + 0x1000; romfsSize = romfsLen - 0x1000; }
	return start (textAddr, stackSize ? stackSize : 0x4000);
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
		if (fsOffset && fsOffset < size) { romfsBase = fsOffset; romfsSize = size - fsOffset; }
	}
	return start (base, 0x40000);
}

}
