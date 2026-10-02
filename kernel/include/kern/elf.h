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
};

struct TImgSource;			// kern/image.h: where the file's bytes come from

// The ELF header and the program headers read from pSrc and checked: an AArch64 ELF64 executable,
// its headers and every segment's bytes inside the file, every segment in the user VA range, no
// two segments in one 64 KB page. Segments without memory are left out. -> 0, or -KAPI_E* with
// *ppWhy a short reason for the log.
int ElfReadPlan (const TImgSource *pSrc, TElfPlan *pPlan, const char **ppWhy);

// Load all PT_LOAD segments of the ELF image at pImage (nSize bytes) into pAS.
// On success returns TRUE and writes the entry point to *pEntry. Segments must lie
// in the user VA range and (with 64 KB-aligned linking) not share a 64 KB page.
// (v77: the same checks and the same result through the image object, kern/image.h -- an image
// of its own, never shared, that pAS holds; the kernel's own loader reads the file instead.)
boolean LoadELF (const void *pImage, size_t nSize, CAddressSpace *pAS, u64 *pEntry);

#endif
