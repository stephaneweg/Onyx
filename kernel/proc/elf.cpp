//
// elf.cpp -- minimal ELF64/AArch64 loader.
//
#include <kern/elf.h>
#include <kern/addrspace.h>
#include <kern/layout.h>
#include <circle/synchronize.h>		// SyncDataAndInstructionCache
#include <circle/util.h>		// memcpy
#include <circle/logger.h>

static const char FromELF[] = "elf";

static u64 Min64 (u64 a, u64 b) { return a < b ? a : b; }
static u64 Max64 (u64 a, u64 b) { return a > b ? a : b; }

// Map the pages covering [ulVAddr, ulVAddr+ulMemSz) and copy ulFileSz bytes of
// segment data into them; the remainder (bss) stays zero (MapNewPage zeroes).
static boolean LoadSegment (CAddressSpace *pAS, u64 ulVAddr, const u8 *pData,
			    u64 ulFileSz, u64 ulMemSz, const TKPageAttr &Attr)
{
	u64 ulStart = KPAGE_ALIGN_DOWN (ulVAddr);
	u64 ulEnd   = KPAGE_ALIGN_UP (ulVAddr + ulMemSz);

	for (u64 va = ulStart; va < ulEnd; va += KPAGE_SIZE)
	{
		u8 *pFrame = (u8 *) pAS->MapNewPage (va, Attr);	// zeroed, identity addr
		if (pFrame == 0)
		{
			return FALSE;
		}

		// Overlap of [ulVAddr, ulVAddr+ulFileSz) with this page [va, va+64K).
		u64 ulCopyStart = Max64 (va, ulVAddr);
		u64 ulCopyEnd   = Min64 (va + KPAGE_SIZE, ulVAddr + ulFileSz);
		if (ulCopyStart < ulCopyEnd)
		{
			memcpy (pFrame + (ulCopyStart - va),
				pData + (ulCopyStart - ulVAddr),
				(size_t) (ulCopyEnd - ulCopyStart));
		}
	}

	return TRUE;
}

boolean LoadELF (const void *pImage, size_t nSize, CAddressSpace *pAS, u64 *pEntry)
{
	if (pImage == 0 || nSize < sizeof (Elf64_Ehdr) || pAS == 0)
	{
		return FALSE;
	}

	const u8 *pBytes = (const u8 *) pImage;
	const Elf64_Ehdr *pEhdr = (const Elf64_Ehdr *) pImage;

	if (!(pEhdr->e_ident[0] == 0x7F && pEhdr->e_ident[1] == 'E'
	   && pEhdr->e_ident[2] == 'L'  && pEhdr->e_ident[3] == 'F'))
	{
		CLogger::Get ()->Write (FromELF, LogError, "bad ELF magic");
		return FALSE;
	}
	if (pEhdr->e_ident[4] != 2 /* ELFCLASS64 */ || pEhdr->e_machine != EM_AARCH64)
	{
		CLogger::Get ()->Write (FromELF, LogError, "not AArch64 ELF64");
		return FALSE;
	}
	if (pEhdr->e_type != ET_EXEC && pEhdr->e_type != ET_DYN)
	{
		CLogger::Get ()->Write (FromELF, LogError, "not an executable ELF");
		return FALSE;
	}

	for (unsigned i = 0; i < pEhdr->e_phnum; i++)
	{
		const Elf64_Phdr *pPhdr =
			(const Elf64_Phdr *) (pBytes + pEhdr->e_phoff + i * pEhdr->e_phentsize);

		if (pPhdr->p_type != PT_LOAD || pPhdr->p_memsz == 0)
		{
			continue;
		}

		if (pPhdr->p_offset + pPhdr->p_filesz > nSize)
		{
			CLogger::Get ()->Write (FromELF, LogError, "segment past end of image");
			return FALSE;
		}
		if (!IS_USER_VA (pPhdr->p_vaddr)
		    || !IS_USER_VA (pPhdr->p_vaddr + pPhdr->p_memsz - 1))
		{
			CLogger::Get ()->Write (FromELF, LogError,
						"segment vaddr %lp out of user range",
						(void *) pPhdr->p_vaddr);
			return FALSE;
		}

		TKPageAttr Code = KPAGE_ATTR_USER_CODE;
		TKPageAttr Data = KPAGE_ATTR_USER_DATA;
		const TKPageAttr &Attr = (pPhdr->p_flags & PF_X) ? Code : Data;

		if (!LoadSegment (pAS, pPhdr->p_vaddr, pBytes + pPhdr->p_offset,
				  pPhdr->p_filesz, pPhdr->p_memsz, Attr))
		{
			return FALSE;
		}
	}

	// We wrote code via the identity mapping: make it executable at the user VA.
	SyncDataAndInstructionCache ();

	*pEntry = pEhdr->e_entry;
	return TRUE;
}
