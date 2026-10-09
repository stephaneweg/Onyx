//
// loader.cpp -- the program's ELF, AppKit and the other shared libraries mapped as the kernel maps them
// (kernel/proc/elf.cpp, proc/image.cpp, kernel.cpp AppKitLoad; docs/SHARED-LIBS-PLAN.md section 3):
//   - a program: an ET_EXEC AArch64 ELF whose PT_LOAD segments lie in the user range (8 GB+), mapped at their
//     addresses on 64 KB pages, the file's bytes copied, the rest zero;
//   - a library: an ET_DYN linked at 0 (user/Runtime/lib.ld: two PT_LOAD -- read + execute, read + write -- and a
//     PT_DYNAMIC), placed in the arena [USER_LIB_BASE, USER_LIB_END), its R_AARCH64_RELATIVE relocations applied
//     (any other type refuses it); its export table is at base + e_entry: { u32 version (= the entries), u32 size,
//     init, entries... };
//   - AppKit (SD:/lib/appkit.so): a library whose entries are copied into the kapi page at APPKIT_TABLE_VA, where
//     every program's AppKit stubs read them (no init called: the kernel calls none).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <map>
#include <stdio.h>
#include <string.h>

// ---- the ELF structures (as <elf.h>: the Windows host has none) ----
struct Ehdr { u8 ident[16]; u16 type, machine; u32 version; u64 entry, phoff, shoff; u32 flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; };
struct Phdr { u32 type, flags; u64 offset, vaddr, paddr, filesz, memsz, align; };
struct Dyn { s64 tag; u64 val; };
struct Rela { u64 offset, info; s64 addend; };
#define ET_EXEC		2
#define ET_DYN		3
#define EM_AARCH64	183
#define PT_LOAD		1
#define PT_DYNAMIC	2
#define PF_X		1
#define PF_W		2
#define PF_R		4
#define DT_NULL		0
#define DT_RELA		7
#define DT_RELASZ	8
#define DT_RELAENT	9
#define DT_TEXTREL	22
#define R_AARCH64_RELATIVE	1027

static bool read_file (const char *path, std::vector<u8> &out)
{
	FILE *f = fopen (path, "rb");
	if (!f) return false;
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	out.resize (n > 0 ? (size_t) n : 0);
	bool ok = n >= 0 && fread (out.data (), 1, out.size (), f) == out.size ();
	fclose (f);
	return ok;
}

static unsigned prot_of (u32 flags)
{
	return ((flags & PF_R) ? MEM_R : 0) | ((flags & PF_W) ? MEM_W : 0) | ((flags & PF_X) ? MEM_X : 0);
}

// The PT_LOAD segments of an image mapped at base + vaddr (64 KB pages, a page shared by two segments gets both
// protections), their bytes copied -> false (said why in *why).
static bool map_segments (const std::vector<u8> &F, u64 base, const char *what, const char **why)
{
	const Ehdr *eh = (const Ehdr *) F.data ();
	std::map<u64, unsigned> pages;					// page -> protection
	for (int i = 0; i < eh->phnum; i++)
	{
		const Phdr *ph = (const Phdr *) (F.data () + eh->phoff + (u64) i * eh->phentsize);
		if (ph->type != PT_LOAD || ph->memsz == 0) continue;
		if (ph->offset + ph->filesz > F.size () || ph->filesz > ph->memsz) { *why = "a segment past the file's end"; return false; }
		u64 a = (base + ph->vaddr) & ~(HM_GRAIN - 1), e = (base + ph->vaddr + ph->memsz + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
		if (a < USER_VA_BASE || e > USER_VA_END) { *why = "a segment outside the user range"; return false; }
		for (u64 p = a; p < e; p += HM_GRAIN) pages[p] |= prot_of (ph->flags);
	}
	// runs of pages with the same protection, each one region
	for (auto it = pages.begin (); it != pages.end (); )
	{
		u64 a = it->first; unsigned prot = it->second; u64 e = a + HM_GRAIN;
		++it;
		while (it != pages.end () && it->first == e && it->second == prot) { e += HM_GRAIN; ++it; }
		if (!hm_map (a, e - a, prot, what)) { *why = "its memory cannot be mapped (taken, or no memory)"; return false; }
	}
	for (int i = 0; i < eh->phnum; i++)
	{
		const Phdr *ph = (const Phdr *) (F.data () + eh->phoff + (u64) i * eh->phentsize);
		if (ph->type != PT_LOAD || ph->memsz == 0) continue;
		memcpy ((void *) (base + ph->vaddr), F.data () + ph->offset, ph->filesz);
	}
	return true;
}

static bool check_elf (const std::vector<u8> &F, u16 type, const char **why)
{
	const Ehdr *eh = (const Ehdr *) F.data ();
	if (F.size () < sizeof (Ehdr) || memcmp (eh->ident, "\177ELF", 4) != 0 || eh->ident[4] != 2 /* 64-bit */)
	{ *why = "not a 64-bit ELF"; return false; }
	if (eh->machine != EM_AARCH64) { *why = "not an AArch64 ELF"; return false; }
	if (eh->type != type) { *why = type == ET_EXEC ? "not a program (ET_EXEC)" : "not a library (ET_DYN)"; return false; }
	if (eh->phoff + (u64) eh->phnum * eh->phentsize > F.size () || eh->phentsize < sizeof (Phdr)) { *why = "bad program headers"; return false; }
	return true;
}

u64 load_program (const char *hostFile)
{
	std::vector<u8> F;
	const char *why = "";
	if (!read_file (hostFile, F)) { rlog ("cannot read %s", hostFile); return 0; }
	if (!check_elf (F, ET_EXEC, &why) || !map_segments (F, 0, ("program " + g_Proc.name).c_str (), &why))
	{
		rlog ("%s: %s", hostFile, why);
		return 0;
	}
	const Ehdr *eh = (const Ehdr *) F.data ();
	return eh->entry;
}

// ---- the libraries -----------------------------------------------------------------------------------------
struct Lib { std::string path; u64 base, table; };
static std::vector<Lib> s_Libs;				// the process's (16 at most, as the kernel's)
#define LIB_MAX		16

// A library image mapped and relocated -> its table's address, 0 (*err).
static u64 map_library (const std::string &onyxPath, int *err)
{
	std::string host = host_path (onyxPath.c_str ());
	std::vector<u8> F;
	const char *why = "";
	if (host.empty () || !read_file (host.c_str (), F)) { *err = -KAPI_ENOENT; return 0; }
	if (!check_elf (F, ET_DYN, &why)) { rlog ("%s: %s", onyxPath.c_str (), why); *err = -KAPI_EINVAL; return 0; }
	const Ehdr *eh = (const Ehdr *) F.data ();
	u64 span = 0, dynOff = 0, dynSize = 0;
	for (int i = 0; i < eh->phnum; i++)
	{
		const Phdr *ph = (const Phdr *) (F.data () + eh->phoff + (u64) i * eh->phentsize);
		if (ph->type == PT_LOAD && ph->vaddr + ph->memsz > span) span = ph->vaddr + ph->memsz;
		if (ph->type == PT_DYNAMIC) { dynOff = ph->offset; dynSize = ph->filesz; }
	}
	if (span == 0 || dynSize == 0 || dynOff + dynSize > F.size ()) { rlog ("%s: not of lib.ld's shape", onyxPath.c_str ()); *err = -KAPI_EINVAL; return 0; }
	u64 base = hm_find_free (USER_LIB_BASE, USER_LIB_END, span);
	if (base == 0) { *err = -KAPI_ENOMEM; return 0; }
	std::string what = "library " + onyxPath;
	if (!map_segments (F, base, what.c_str (), &why)) { rlog ("%s: %s", onyxPath.c_str (), why); *err = -KAPI_ENOMEM; return 0; }
	// the relocations (read from the file's copy of the dynamic table; written into the mapped data)
	u64 rela = 0, relasz = 0, relaent = sizeof (Rela);
	for (const Dyn *d = (const Dyn *) (F.data () + dynOff); (const u8 *) (d + 1) <= F.data () + dynOff + dynSize && d->tag != DT_NULL; d++)
	{
		if (d->tag == DT_RELA) rela = d->val;
		else if (d->tag == DT_RELASZ) relasz = d->val;
		else if (d->tag == DT_RELAENT) relaent = d->val;
		else if (d->tag == DT_TEXTREL) { rlog ("%s: DT_TEXTREL", onyxPath.c_str ()); *err = -KAPI_EINVAL; return 0; }
	}
	for (u64 o = 0; relaent >= sizeof (Rela) && o + sizeof (Rela) <= relasz; o += relaent)
	{
		const Rela *r = (const Rela *) (base + rela + o);	// (in the mapped text segment)
		if ((u32) r->info != R_AARCH64_RELATIVE || r->offset + 8 > span)
		{
			rlog ("%s: relocation type %u", onyxPath.c_str (), (unsigned) (u32) r->info);
			*err = -KAPI_EINVAL; return 0;
		}
		*(u64 *) (base + r->offset) = base + (u64) r->addend;
	}
	cpu_flush_code (base, span);
	if (g_Proc.trace) rlog ("lib %s at %llx", onyxPath.c_str (), (unsigned long long) base);
	return base + eh->entry;
}

// The library's canonical Onyx path: a bare name is SD:/lib/<name>.so, anything with '/', '\' or ':' a path.
static std::string lib_canon (const char *name)
{
	bool path = false;
	for (const char *q = name; *q; q++) if (*q == '/' || *q == '\\' || *q == ':') path = true;
	return onyx_abs (path ? name : ("SD:/lib/" + std::string (name) + ".so").c_str ());
}

u64 load_library (const char *name, unsigned minVersion, int *err)
{
	*err = 0;
	if (!name || !*name) { *err = -KAPI_EINVAL; return 0; }
	std::string canon = lib_canon (name);
	if (canon.empty ()) { *err = -KAPI_ENAMETOOLONG; return 0; }
	BigLockHold L;
	u64 table = 0;
	for (auto &l : s_Libs)
		if (strcasecmp (l.path.c_str (), canon.c_str ()) == 0) table = l.table;
	if (table == 0)
	{
		if (s_Libs.size () >= LIB_MAX) { *err = -KAPI_EMFILE; return 0; }
		table = map_library (canon, err);
		if (table == 0) return 0;
		s_Libs.push_back (Lib { canon, 0, table });
	}
	if (*(const u32 *) table < minVersion) { *err = -KAPI_ENOTSUP; return 0; }
	return table;
}

bool load_appkit (void)
{
	int err = 0;
	u64 t;
	{
		BigLockHold L;
		t = map_library ("SD:/lib/appkit.so", &err);
		if (t) s_Libs.push_back (Lib { "SD:/lib/appkit.so", 0, t });
	}
	if (t == 0) { rlog ("cannot load SD:/lib/appkit.so (%d): THE PROGRAM CANNOT CALL THE KERNEL", err); return false; }
	u32 n = *(const u32 *) t, size = *(const u32 *) (t + 4);
	if (n < 1 || n > APPKIT_TABLE_MAX || size < 16 + n * 8) { rlog ("SD:/lib/appkit.so is not AppKit (its table)"); return false; }
	u64 *dst = (u64 *) APPKIT_TABLE_VA;
	for (u32 i = 0; i < APPKIT_TABLE_MAX; i++) dst[i] = i < n ? ((const u64 *) (t + 16))[i] : 0;
	return true;
}
