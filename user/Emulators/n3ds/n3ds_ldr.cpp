//
// n3ds/n3ds_ldr.cpp -- ldr:ro, the loader of a game's dynamic modules ("CRO": Pokemon, Smash Bros and other big
// games keep most of their code in such files and load them as they go). The game reads a module into its own
// memory and asks the service to make it runnable at an address it chose: the module's tables hold offsets
// (made addresses here), the places in its own code and data that depend on where it sits (internal
// relocations), and what it takes from the other modules -- by name, by number or by place -- and gives them.
// The game's fixed program takes part through a module without code, the "CRS" (its exports only), given first.
//
// Written from the format's public description (3dbrew, "CRO0"); nothing of an emulator's sources.
//
// What is left out: the modules' signatures (a CRR is accepted as it is), the "fix levels" (a module keeps its
// whole size), and the difference between the automatically and the manually linked lists (one list).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

namespace {

// The header's fields (after 0x80 bytes of hashes). From H_CODE on: pairs of (offset, size or count).
enum
{
	H_MAGIC = 0x80, H_NAME = 0x84, H_NEXT = 0x88, H_PREV = 0x8C, H_SIZE = 0x90, H_BSS = 0x94,
	H_ONLOAD = 0xA4, H_ONEXIT = 0xA8, H_ONUNRESOLVED = 0xAC,
	H_CODE = 0xB0, H_DATA = 0xB8, H_MODNAME = 0xC0, H_SEGS = 0xC8, H_NEXP = 0xD0, H_IEXP = 0xD8, H_ESTR = 0xE0, H_ETREE = 0xE8,
	H_IMOD = 0xF0, H_EXTREL = 0xF8, H_NIMP = 0x100, H_IIMP = 0x108, H_AIMP = 0x110, H_ISTR = 0x118, H_SANON = 0x120,
	H_INTREL = 0x128, H_STREL = 0x130, H_END = 0x138,
};
enum { SEG_TEXT = 0, SEG_RODATA = 1, SEG_DATA = 2, SEG_BSS = 3 };

struct Export { u32 hash, name, addr; };			// (name: the string's address in the game's memory)

struct Module
{
	u32 at, size, buffer;					// where it runs, and the game's buffer behind it
	bool crs;
	u32 dataNew, dataOld, dataSize;				// the data segment: where it lives, and its picture in the file
	Export *exports; u32 exportCount;
	Module *next;
};

struct Ldr { Module *first; Module *crs; };

u32 hashOf (const char *s) { u32 h = 2166136261u; for (; *s; s++) h = (h ^ (u8) *s) * 16777619u; return h; }
int byHash (const void *a, const void *b)
{
	const u32 x = ((const Export *) a)->hash, y = ((const Export *) b)->hash;
	return x < y ? -1 : x > y;
}

struct Linker
{
	Machine *m;
	Ldr *l;
	Memory &mem;
	Linker (Machine *mm, Ldr *ll) : m (mm), l (ll), mem (mm->mem) {}

	u32 f (const Module *M, u32 field) const { return mem.r32 (M->at + field); }
	void str (u32 va, char *out, u32 max) const
	{
		u32 n = 0;
		for (; n + 1 < max; n++) { const u8 c = mem.r8 (va + n); if (!c) break; out[n] = (char) c; }
		out[n] = 0;
	}
	// A "segment offset": the segment's number in the low four bits, the offset inside it above.
	u32 segAddr (const Module *M, u32 tag) const
	{
		const u32 seg = tag & 15, off = tag >> 4;
		if (seg >= f (M, H_SEGS + 4)) return 0;
		const u32 e = f (M, H_SEGS) + seg * 12;
		if (off > mem.r32 (e + 4)) return 0;
		return mem.r32 (e) + off;
	}
	// A word of the module: the data segment is written at both of its places (the game copies one onto the
	// other, before or after the loading).
	void put (const Module *M, u32 target, u32 v)
	{
		mem.w32 (target, v);
		if (M->dataOld != M->dataNew && target >= M->dataNew && target - M->dataNew < M->dataSize) mem.w32 (target - M->dataNew + M->dataOld, v);
	}
	void apply (const Module *M, u32 target, u32 type, u32 addend, u32 sym)
	{
		switch (type)
		{
		case 2: case 38: put (M, target, sym + addend); break;					// an address
		case 3: put (M, target, sym + addend - target); break;					// ... relative to the place
		case 42: put (M, target, (mem.r32 (target) & 0x80000000u) | ((sym + addend - target) & 0x7FFFFFFFu)); break;
		case 28: case 29:										// an ARM branch
		{
			const u32 off = sym + addend - target, insn = mem.r32 (target);
			if (type == 28 && (sym & 1)) put (M, target, 0xFA000000u | ((off >> 1) & 1) << 24 | ((off >> 2) & 0xFFFFFF));
			else put (M, target, (insn & 0xFF000000u) | ((off >> 2) & 0xFFFFFF));
			break;
		}
		case 10:											// a Thumb call
		{
			const u32 off = (sym & ~1u) + addend - target;
			put (M, target, (0xF000u | ((off >> 12) & 0x7FF)) | (u32) (((sym & 1) ? 0xF800u : 0xE800u) | ((off >> 1) & 0x7FF)) << 16);
			break;
		}
		default: break;
		}
	}
	// A batch of external relocations: entries of 12 bytes up to the one flagged last; the first one says whether
	// the batch found its symbol.
	void applyBatch (const Module *M, u32 batch, u32 sym, bool resolved)
	{
		for (u32 i = 0; i < 0x100000; i++)
		{
			const u32 e = batch + i * 12;
			const u32 target = segAddr (M, mem.r32 (e));
			if (target) apply (M, target, mem.r8 (e + 4), mem.r32 (e + 8), sym);
			if (mem.r8 (e + 5)) break;
		}
		mem.w8 (batch + 6, resolved ? 1 : 0);
	}

	u32 findExport (const Module *M, const char *name) const
	{
		const u32 h = hashOf (name);
		u32 lo = 0, hi = M->exportCount;
		while (lo < hi) { const u32 mid = (lo + hi) / 2; if (M->exports[mid].hash < h) lo = mid + 1; else hi = mid; }
		for (; lo < M->exportCount && M->exports[lo].hash == h; lo++)
		{
			char other[256]; str (M->exports[lo].name, other, sizeof other);
			if (strcmp (other, name) == 0) return M->exports[lo].addr;
		}
		return 0;
	}
	Module *findModule (const char *name) const
	{
		for (Module *B = l->first; B; B = B->next)
		{
			char other[128]; str (f (B, H_NAME), other, sizeof other);
			if (strcmp (other, name) == 0) return B;
		}
		return 0;
	}

	// Everything in the tables that is an offset from the file's start becomes an address.
	void rebase (Module *M, u32 dataAddr, u32 dataSize, u32 bssAddr, u32 bssSize)
	{
		const u32 at = M->at;
		mem.w32 (at + H_NAME, f (M, H_NAME) + at);
		for (u32 h = H_CODE; h < H_END; h += 8) mem.w32 (at + h, f (M, h) + at);
		M->dataOld = M->dataNew = 0; M->dataSize = 0;
		const u32 segs = f (M, H_SEGS), segCount = f (M, H_SEGS + 4);
		for (u32 i = 0; i < segCount && i < 16; i++)
		{
			const u32 e = segs + i * 12, off = mem.r32 (e), size = mem.r32 (e + 4), id = mem.r32 (e + 8);
			if (M->crs) continue;						// (the fixed program's segments: addresses already)
			if (id == SEG_DATA && size)
			{
				M->dataOld = off + at; M->dataSize = size;
				M->dataNew = dataAddr && size <= dataSize ? dataAddr : M->dataOld;
				mem.w32 (e, M->dataNew);
			}
			else if (id == SEG_BSS) { if (size && size <= bssSize) mem.w32 (e, bssAddr); }
			else if (off) mem.w32 (e, off + at);
		}
		if (M->dataOld != M->dataNew)						// the data starts as the file has it
			for (u32 i = 0; i < M->dataSize; i += 4) mem.w32 (M->dataNew + i, mem.r32 (M->dataOld + i));
		u32 t = f (M, H_NEXP), n = f (M, H_NEXP + 4);
		for (u32 i = 0; i < n; i++) mem.w32 (t + i * 8, mem.r32 (t + i * 8) + at);
		t = f (M, H_IMOD); n = f (M, H_IMOD + 4);
		for (u32 i = 0; i < n; i++)
		{
			const u32 e = t + i * 20;
			mem.w32 (e, mem.r32 (e) + at); mem.w32 (e + 4, mem.r32 (e + 4) + at); mem.w32 (e + 12, mem.r32 (e + 12) + at);
		}
		t = f (M, H_NIMP); n = f (M, H_NIMP + 4);
		for (u32 i = 0; i < n; i++) { mem.w32 (t + i * 8, mem.r32 (t + i * 8) + at); mem.w32 (t + i * 8 + 4, mem.r32 (t + i * 8 + 4) + at); }
		t = f (M, H_IIMP); n = f (M, H_IIMP + 4);
		for (u32 i = 0; i < n; i++) mem.w32 (t + i * 8 + 4, mem.r32 (t + i * 8 + 4) + at);
		t = f (M, H_AIMP); n = f (M, H_AIMP + 4);
		for (u32 i = 0; i < n; i++) mem.w32 (t + i * 8 + 4, mem.r32 (t + i * 8 + 4) + at);
		t = f (M, H_SANON); n = f (M, H_SANON + 4);
		for (u32 i = 0; i < n; i++) mem.w32 (t + i * 8 + 4, mem.r32 (t + i * 8 + 4) + at);
	}
	void readExports (Module *M)
	{
		const u32 t = f (M, H_NEXP), n = f (M, H_NEXP + 4);
		M->exports = n && n < 0x100000 ? (Export *) malloc (n * sizeof (Export)) : 0;
		M->exportCount = M->exports ? n : 0;
		for (u32 i = 0; i < M->exportCount; i++)
		{
			char name[256];
			Export &x = M->exports[i];
			x.name = mem.r32 (t + i * 8); x.addr = segAddr (M, mem.r32 (t + i * 8 + 4));
			str (x.name, name, sizeof name); x.hash = hashOf (name);
		}
		if (M->exportCount) qsort (M->exports, M->exportCount, sizeof (Export), byHash);
	}
	void internalRelocations (Module *M)
	{
		const u32 t = f (M, H_INTREL), n = f (M, H_INTREL + 4);
		for (u32 i = 0; i < n; i++)
		{
			const u32 e = t + i * 12, target = segAddr (M, mem.r32 (e)), seg = mem.r8 (e + 5);
			if (!target || seg >= f (M, H_SEGS + 4)) continue;
			apply (M, target, mem.r8 (e + 4), mem.r32 (e + 8), mem.r32 (f (M, H_SEGS) + seg * 12));
		}
	}
	// What M takes from the others. A name nobody gives goes to the module's "unresolved" routine.
	void imports (Module *M)
	{
		const u32 unresolved = M->crs ? 0 : segAddr (M, f (M, H_ONUNRESOLVED));
		u32 t = f (M, H_NIMP), n = f (M, H_NIMP + 4);
		for (u32 i = 0; i < n; i++)
		{
			const u32 batch = mem.r32 (t + i * 8 + 4);
			if (mem.r8 (batch + 6)) continue;
			char name[256]; str (mem.r32 (t + i * 8), name, sizeof name);
			u32 sym = 0;
			if (strcmp (name, "__aeabi_atexit") == 0)
			{
				for (Module *B = l->first; B && !sym; B = B->next) sym = findExport (B, "nnroAeabiAtexit_");
			}
			else for (Module *B = l->first; B && !sym; B = B->next) if (B != M) sym = findExport (B, name);
			if (sym) applyBatch (M, batch, sym, true);
			else if (unresolved) applyBatch (M, batch, unresolved, false);
		}
		t = f (M, H_IMOD); n = f (M, H_IMOD + 4);
		for (u32 i = 0; i < n; i++)
		{
			const u32 e = t + i * 20;
			char name[128]; str (mem.r32 (e), name, sizeof name);
			Module *B = findModule (name);
			if (!B || B == M) continue;
			const u32 idx = mem.r32 (e + 4), idxCount = mem.r32 (e + 8), anon = mem.r32 (e + 12), anonCount = mem.r32 (e + 16);
			for (u32 j = 0; j < idxCount; j++)
			{
				const u32 number = mem.r32 (idx + j * 8), batch = mem.r32 (idx + j * 8 + 4);
				if (mem.r8 (batch + 6) || number >= f (B, H_IEXP + 4)) continue;
				applyBatch (M, batch, segAddr (B, mem.r32 (f (B, H_IEXP) + number * 4)), true);
			}
			for (u32 j = 0; j < anonCount; j++)
			{
				const u32 batch = mem.r32 (anon + j * 8 + 4);
				if (mem.r8 (batch + 6)) continue;
				applyBatch (M, batch, segAddr (B, mem.r32 (anon + j * 8)), true);
			}
		}
	}
	// The places of the fixed program that point into this module (its "static anonymous symbols").
	void staticSymbols (Module *M)
	{
		if (!l->crs || M->crs) return;
		const u32 t = f (M, H_SANON), n = f (M, H_SANON + 4);
		for (u32 i = 0; i < n; i++)
		{
			const u32 batch = mem.r32 (t + i * 8 + 4);
			if (mem.r8 (batch + 6)) continue;
			applyBatch (l->crs, batch, segAddr (M, mem.r32 (t + i * 8)), true);
		}
	}
	// Every batch of M back to "not found" (a module left).
	void forget (Module *M)
	{
		u32 t = f (M, H_NIMP), n = f (M, H_NIMP + 4);
		for (u32 i = 0; i < n; i++) mem.w8 (mem.r32 (t + i * 8 + 4) + 6, 0);
		t = f (M, H_IMOD); n = f (M, H_IMOD + 4);
		for (u32 i = 0; i < n; i++)
		{
			const u32 e = t + i * 20, idx = mem.r32 (e + 4), idxCount = mem.r32 (e + 8), anon = mem.r32 (e + 12), anonCount = mem.r32 (e + 16);
			for (u32 j = 0; j < idxCount; j++) mem.w8 (mem.r32 (idx + j * 8 + 4) + 6, 0);
			for (u32 j = 0; j < anonCount; j++) mem.w8 (mem.r32 (anon + j * 8 + 4) + 6, 0);
		}
	}
	void linkAll () { for (Module *M = l->first; M; M = M->next) imports (M); }
	// The modules' chain as the game's library may read it: next, and previous (the first one's: the last).
	void chain ()
	{
		Module *last = l->first;
		while (last && last->next) last = last->next;
		Module *prev = 0;
		for (Module *M = l->first; M; prev = M, M = M->next)
		{
			mem.w32 (M->at + H_NEXT, M->next ? M->next->at : 0);
			mem.w32 (M->at + H_PREV, prev ? prev->at : (last != M ? last->at : 0));
		}
	}
};

// The game's buffer seen at the module's address too (the same pages), code allowed.
bool alias (Machine *m, u32 buffer, u32 at, u32 size)
{
	if ((buffer | at) & (PAGE_SIZE - 1)) return false;
	size = (size + PAGE_SIZE - 1) & ~(u32) (PAGE_SIZE - 1);
	if (!m->mem.mapped (buffer, size)) return false;
	if (buffer != at)
		for (u32 off = 0; off < size; off += PAGE_SIZE)
			m->mem.map (at + off, PAGE_SIZE, m->mem.ptr (buffer + off), PERM_R | PERM_W | PERM_X);
	return true;
}

Module *load (Machine *m, Ldr *l, bool crs, u32 buffer, u32 at, u32 size, u32 dataAddr, u32 dataSize, u32 bssAddr, u32 bssSize)
{
	if (size < H_END || !alias (m, buffer, at, size)) return 0;
	if (m->mem.r32 (at + H_MAGIC) != 0x304F5243) return 0;				// "CRO0"
	Module *M = (Module *) calloc (1, sizeof (Module));
	if (!M) return 0;
	M->at = at; M->size = size; M->buffer = buffer; M->crs = crs;
	Linker k (m, l);
	k.rebase (M, dataAddr, dataSize, bssAddr, bssSize);
	k.readExports (M);
	k.internalRelocations (M);
	Module **end = &l->first;
	while (*end) end = &(*end)->next;
	*end = M;
	if (crs) l->crs = M;
	k.staticSymbols (M);
	k.linkAll ();
	k.chain ();
	m->cpu->invalidate (at, size);
	return M;
}

void unload (Machine *m, Ldr *l, u32 at)
{
	Linker k (m, l);
	for (Module **p = &l->first; *p; p = &(*p)->next)
	{
		Module *M = *p;
		if (M->at != at || M->crs) continue;
		*p = M->next;
		if (M->buffer != M->at) m->mem.unmap (M->at, (M->size + PAGE_SIZE - 1) & ~(u32) (PAGE_SIZE - 1));
		m->cpu->invalidate (M->at, M->size);
		free (M->exports); free (M);
		break;
	}
	for (Module *M = l->first; M; M = M->next) k.forget (M);
	k.linkAll ();
	k.chain ();
}

}

void ldrFree (Machine *m)
{
	Ldr *l = (Ldr *) m->ldr;
	if (!l) return;
	for (Module *M = l->first; M; ) { Module *n = M->next; free (M->exports); free (M); M = n; }
	free (l);
	m->ldr = 0;
}

void ldrRequest (Machine *m, Session *, u32 *cmd)
{
	const u32 id = cmd[0] >> 16;
	if (!m->ldr) m->ldr = calloc (1, sizeof (Ldr));
	Ldr *l = (Ldr *) m->ldr;
	u32 result = RES_OK, value = 0;
	int words = 1;
	switch (id)
	{
	case 0x01:								// Initialize (the CRS: its buffer, its size, its address)
		if (!l || l->crs || !load (m, l, true, cmd[1], cmd[3], cmd[2], 0, 0, 0, 0)) { result = RES_INVALID_ARG; m->note ("ldr:ro: the game's module table (CRS) was not taken"); }
		break;
	case 0x02:								// LoadCRR (the modules' signatures: not checked)
	case 0x03:								// UnloadCRR
		break;
	case 0x04:								// LoadCRO (buffer, address, size, data address, 0, data size,
	case 0x09:								//          bss address, bss size, auto link, fix level, crr)
	{
		Module *M = l ? load (m, l, false, cmd[1], cmd[2], cmd[3], cmd[4], cmd[6], cmd[7], cmd[8]) : 0;
		if (!M) { result = RES_INVALID_ARG; m->note ("ldr:ro: a module was not taken (%08x, %u bytes)", (unsigned) cmd[1], (unsigned) cmd[3]); }
		else value = (M->size + PAGE_SIZE - 1) & ~(u32) (PAGE_SIZE - 1);
		words = 2;
		break;
	}
	case 0x05:								// UnloadCRO (address, 0, buffer)
		if (l) unload (m, l, cmd[1]);
		break;
	case 0x06:								// LinkCRO (address): done at the loading
	case 0x07:								// UnlinkCRO
		break;
	case 0x08:								// Shutdown (the CRS's buffer)
		ldrFree (m);
		break;
	default:
		ipcStub (m, "ldr:ro", cmd);
		return;
	}
	cmd[0] = ipcHeader (id, words, 0); cmd[1] = result; cmd[2] = value;
}

}
