//
// elf.h
//
// Minimal ELF64 (AArch64) loader. Parses PT_LOAD segments out of an in-memory ELF
// image and maps them into a process address space (CAddressSpace). The image
// source can be embedded bytes or a file read from the SD card (#6 later).
//
#ifndef _kern_elf_h
#define _kern_elf_h

#include <circle/types.h>

class CAddressSpace;

// --- ELF64 on-disk structures (little-endian AArch64) ---
typedef u64 Elf64_Addr;
typedef u64 Elf64_Off;
typedef u16 Elf64_Half;
typedef u32 Elf64_Word;
typedef u64 Elf64_Xword;

struct Elf64_Ehdr
{
	unsigned char	e_ident[16];
	Elf64_Half	e_type;
	Elf64_Half	e_machine;
	Elf64_Word	e_version;
	Elf64_Addr	e_entry;
	Elf64_Off	e_phoff;
	Elf64_Off	e_shoff;
	Elf64_Word	e_flags;
	Elf64_Half	e_ehsize;
	Elf64_Half	e_phentsize;
	Elf64_Half	e_phnum;
	Elf64_Half	e_shentsize;
	Elf64_Half	e_shnum;
	Elf64_Half	e_shstrndx;
};

struct Elf64_Phdr
{
	Elf64_Word	p_type;
	Elf64_Word	p_flags;
	Elf64_Off	p_offset;
	Elf64_Addr	p_vaddr;
	Elf64_Addr	p_paddr;
	Elf64_Xword	p_filesz;
	Elf64_Xword	p_memsz;
	Elf64_Xword	p_align;
};

#define ET_EXEC		2
#define ET_DYN		3
#define EM_AARCH64	183
#define PT_LOAD		1
#define PT_DYNAMIC	2
#define PF_X		1
#define PF_W		2
#define PF_R		4

// (v77) What a program file asks to be loaded: read from its headers alone (ElfReadPlan), before
// any segment -- the streaming loader (kern/image.h) then reads each segment straight into its
// frames.
#define ELF_MAX_SEGS	16		// PT_LOAD segments with memory (our programs have 1 or 2)

struct TElfSeg
{
	u64	ulVAddr;		// where it goes (anywhere in its first page)
	u64	ulMemSz;		// its size in memory (> 0)
	u64	ulFileSz;		// its bytes in the file (<= ulMemSz; the rest is zero: the bss)
	u64	ulOffset;		// where they are in the file
	u32	nFlags;			// PF_*
};

struct TElfPlan
{
	u64	 ulEntry;
	unsigned nSegs;
	TElfSeg	 Seg[ELF_MAX_SEGS];
	boolean	 bLib;			// (v83) a shared library: linked at 0, placed by the kernel
	u64	 ulDynVAddr;		// a library's dynamic section (in its writable segment's file bytes)
	u64	 ulDynSize;
};

// (v83) A shared library's dynamic section and relocations (docs/SHARED-LIBS-PLAN.md): the only
// relocation the kernel applies is R_AARCH64_RELATIVE (the library's base + the addend).
struct Elf64_Dyn
{
	u64	d_tag;
	u64	d_val;
};

struct Elf64_Rela
{
	u64	r_offset;
	u64	r_info;
	u64	r_addend;
};

#define DT_NULL			0
#define DT_RELA			7
#define DT_RELASZ		8
#define DT_RELAENT		9
#define DT_REL			17
#define DT_TEXTREL		22
#define DT_JMPREL		23
#define DT_FLAGS		30
#define DF_TEXTREL		4
#define ELF64_R_TYPE(i)		((u32) (i))
#define R_AARCH64_NONE		0
#define R_AARCH64_RELATIVE	1027

// What ElfReadPlan accepts
#define ELF_KIND_PROGRAM	0	// a program: every segment in the user range
#define ELF_KIND_LIB		1	// a shared library: an ET_DYN linked at 0, of lib.ld's shape
#define ELF_KIND_ANY		2	// either (a preload): pPlan->bLib says which
#define ELF_LIB_MAX_SPAN	0x40000000ULL	// a library's size in memory: 1 GB at most

struct TImgSource;			// kern/image.h: where the file's bytes come from

// The ELF header and the program headers read from pSrc and checked: an AArch64 ELF64 executable,
// its headers and every segment's bytes inside the file, every segment in the user VA range, no
// two segments in one 64 KB page. Segments without memory are left out. -> 0, or -KAPI_E* with
// *ppWhy a short reason for the log.
// (v83) nKind ELF_KIND_LIB: a shared library instead (user/lib.ld's shape): an ET_DYN linked at 0,
// exactly two segments -- the first one read-only (the code), the second one writable --, one
// PT_DYNAMIC inside the writable segment's file bytes, and the entry (the library's export table)
// there too. A program is refused as a library, a library as a program. ELF_KIND_ANY: whichever
// the file is (an ET_DYN whose first segment is below the user range is a library).
int ElfReadPlan (const TImgSource *pSrc, TElfPlan *pPlan, const char **ppWhy, unsigned nKind = ELF_KIND_PROGRAM);

// Load all PT_LOAD segments of the ELF image at pImage (nSize bytes) into pAS.
// On success returns TRUE and writes the entry point to *pEntry. Segments must lie
// in the user VA range and (with 64 KB-aligned linking) not share a 64 KB page.
// (v77: the same checks and the same result through the image object, kern/image.h -- an image
// of its own, never shared, that pAS holds; the kernel's own loader reads the file instead.)
boolean LoadELF (const void *pImage, size_t nSize, CAddressSpace *pAS, u64 *pEntry);

#endif
