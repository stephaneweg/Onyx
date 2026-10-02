//
// elf.cpp -- minimal ELF64/AArch64 loader.
//
// (v77) The headers are read and checked on their own (ElfReadPlan); the segments are then read
// straight into their frames by the image object (proc/image.cpp, kern/image.h). LoadELF, the
// old entry point over a whole file in memory, goes through the same code.
//
#include <kern/elf.h>
#include <kern/image.h>
#include <kern/addrspace.h>
#include <kern/layout.h>
#include <kern/kapi_abi.h>		// KAPI_E*
#include <circle/synchronize.h>		// SyncDataAndInstructionCache
#include <circle/util.h>		// memcpy
#include <circle/logger.h>

static const char FromELF[] = "elf";

static int Refuse (const char **ppWhy, const char *pWhy, int nErr)
{
	if (ppWhy != 0) *ppWhy = pWhy;
	return nErr;
}

// nBytes at ulOffset, all of them (the range is inside the file: checked by the caller).
static boolean ReadAll (const TImgSource *pSrc, u64 ulOffset, void *pBuffer, unsigned nBytes)
{
	return pSrc->pRead (pSrc->pCtx, ulOffset, pBuffer, nBytes) == (int) nBytes;
}

int ElfReadPlan (const TImgSource *pSrc, TElfPlan *pPlan, const char **ppWhy)
{
	pPlan->ulEntry = 0;
	pPlan->nSegs = 0;
	if (pSrc == 0 || pSrc->pRead == 0 || pSrc->nSize < sizeof (Elf64_Ehdr))
	{
		return Refuse (ppWhy, "too short for an ELF header", -KAPI_EINVAL);
	}

	Elf64_Ehdr Ehdr;
	if (!ReadAll (pSrc, 0, &Ehdr, sizeof Ehdr))
	{
		return Refuse (ppWhy, "read failed (the ELF header)", -KAPI_EIO);
	}
	if (!(Ehdr.e_ident[0] == 0x7F && Ehdr.e_ident[1] == 'E'
	   && Ehdr.e_ident[2] == 'L'  && Ehdr.e_ident[3] == 'F'))
	{
		return Refuse (ppWhy, "bad ELF magic", -KAPI_EINVAL);
	}
	if (Ehdr.e_ident[4] != 2 /* ELFCLASS64 */ || Ehdr.e_machine != EM_AARCH64)
	{
		return Refuse (ppWhy, "not AArch64 ELF64", -KAPI_EINVAL);
	}
	if (Ehdr.e_type != ET_EXEC && Ehdr.e_type != ET_DYN)
	{
		return Refuse (ppWhy, "not an executable ELF", -KAPI_EINVAL);
	}
	// The program headers: inside the file (the loader of a whole image in memory never checked).
	if (Ehdr.e_phnum != 0
	    && (   Ehdr.e_phentsize < sizeof (Elf64_Phdr)
		|| Ehdr.e_phoff > pSrc->nSize
		|| (u64) Ehdr.e_phnum * Ehdr.e_phentsize > pSrc->nSize - Ehdr.e_phoff))
	{
		return Refuse (ppWhy, "program headers past end of image", -KAPI_EINVAL);
	}

	for (unsigned i = 0; i < Ehdr.e_phnum; i++)
	{
		Elf64_Phdr Phdr;
		if (!ReadAll (pSrc, Ehdr.e_phoff + (u64) i * Ehdr.e_phentsize, &Phdr, sizeof Phdr))
		{
			return Refuse (ppWhy, "read failed (the program headers)", -KAPI_EIO);
		}
		if (Phdr.p_type != PT_LOAD || Phdr.p_memsz == 0)
		{
			continue;
		}

		if (Phdr.p_offset > pSrc->nSize || Phdr.p_filesz > pSrc->nSize - Phdr.p_offset)
		{
			return Refuse (ppWhy, "segment past end of image", -KAPI_EINVAL);
		}
		if (   !IS_USER_VA (Phdr.p_vaddr)
		    || Phdr.p_vaddr + Phdr.p_memsz - 1 < Phdr.p_vaddr		// (wraps)
		    || !IS_USER_VA (Phdr.p_vaddr + Phdr.p_memsz - 1))
		{
			return Refuse (ppWhy, "segment out of user range", -KAPI_EINVAL);
		}
		if (pPlan->nSegs == ELF_MAX_SEGS)
		{
			return Refuse (ppWhy, "too many segments", -KAPI_EINVAL);
		}
		// Two segments in one 64 KB page: the second one's page replaced the first one's (its
		// frame lost, its bytes gone). Our programs are linked with 64 KB pages: refused.
		u64 ulStart = KPAGE_ALIGN_DOWN (Phdr.p_vaddr);
		u64 ulEnd   = KPAGE_ALIGN_UP (Phdr.p_vaddr + Phdr.p_memsz);
		for (unsigned k = 0; k < pPlan->nSegs; k++)
		{
			const TElfSeg &O = pPlan->Seg[k];
			if (   ulStart < KPAGE_ALIGN_UP (O.ulVAddr + O.ulMemSz)
			    && KPAGE_ALIGN_DOWN (O.ulVAddr) < ulEnd)
			{
				return Refuse (ppWhy, "two segments share a page", -KAPI_EINVAL);
			}
		}

		TElfSeg &S = pPlan->Seg[pPlan->nSegs++];
		S.ulVAddr  = Phdr.p_vaddr;
		S.ulMemSz  = Phdr.p_memsz;
		// (bytes beyond the memory size were never copied: as before)
		S.ulFileSz = Phdr.p_filesz < Phdr.p_memsz ? Phdr.p_filesz : Phdr.p_memsz;
		S.ulOffset = Phdr.p_offset;
		S.nFlags   = Phdr.p_flags;
	}

	pPlan->ulEntry = Ehdr.e_entry;
	return 0;
}

// ---- the old entry point: a whole file in memory -------------------------------------------------

struct TMemImage
{
	const u8 *pBytes;
	u64	  nSize;
};

static int MemRead (void *pCtx, u64 ulOffset, void *pBuffer, unsigned nBytes)
{
	const TMemImage *pMem = (const TMemImage *) pCtx;
	if (ulOffset >= pMem->nSize) return 0;
	if (nBytes > pMem->nSize - ulOffset) nBytes = (unsigned) (pMem->nSize - ulOffset);
	memcpy (pBuffer, pMem->pBytes + ulOffset, nBytes);
	return (int) nBytes;
}

boolean LoadELF (const void *pImage, size_t nSize, CAddressSpace *pAS, u64 *pEntry)
{
	if (pImage == 0 || nSize < sizeof (Elf64_Ehdr) || pAS == 0)
	{
		return FALSE;
	}

	TMemImage Mem = { (const u8 *) pImage, nSize };
	TImgSource Src = { MemRead, &Mem, nSize };
	TImage *pObj = 0;
	const char *pWhy = "";
	if (ImageOpen (0, 0, &Src, 0, &pObj, 0, &pWhy) < 0)	// (no path: an image of its own)
	{
		CLogger::Get ()->Write (FromELF, LogError, "%s", pWhy);
		return FALSE;
	}
	boolean bOK = ImageMap (pObj, pAS, pEntry);
	ImageRelease (pObj);				// (pAS holds it now)
	if (!bOK)
	{
		return FALSE;
	}

	// We wrote code via the identity mapping: make it executable at the user VA.
	SyncDataAndInstructionCache ();

	return TRUE;
}
