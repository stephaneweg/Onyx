//
// image.cpp -- program images (kern/image.h; kapi v77; docs/02 section 7 "Program images"): the
// streaming loader, the shared image object keyed by the program's canonical path, its references,
// the pin of a preload, the file layer's hook.
//
// (v83) A shared library (docs/SHARED-LIBS-PLAN.md) is an image too: position-independent, linked
// at 0, placed once for the whole system in the library arena (kern/layout.h) when it is loaded,
// its relocations applied once to the copy of its data -- so every process maps the same code
// frames at the same address and copies data that is already relocated.
//
// An image (TImage) is, per PT_LOAD segment of the program (kern/elf.h's plan):
//  - a read-only one (no PF_W): its 64 KB frames, allocated from the app pool and filled straight
//    from the file -- mapped in every process of the program, never owned by an address space, so
//    a process's teardown never frees them (mm/addrspace.cpp frees the frames flagged owned only);
//  - a writable one: a copy of its first bytes (the file's), from which every process gets its own
//    pages (the bss zero). 156 KB at most in today's programs.
// Nothing ever writes a shared frame after the load: the loader writes it through the kernel's
// identity mapping before any page table names it, and its pages are mapped AP = read-only for
// EL0 AND EL1 (KPAGE_ATTR_APP_CODE / _RODATA), so a kapi's copy-out there fails as it always did
// on a program's code.
//
// References: one per address space mapping it (ImageMap), one per task between ImageOpen and
// ImageRelease. Freed at zero unless pinned. Compiled on the PC too (tools/tests/run_image_test.sh).
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#include <kern/image.h>
#include <kern/elf.h>
#include <kern/addrspace.h>
#include <kern/layout.h>
#include <kern/vm.h>			// VmNoteRegion, VmCommitOK, WordWaitsZap
#include <kern/kapi_abi.h>
#include <circle/sched/scheduler.h>
#include <circle/alloc.h>		// palloc_high, pfree
#include <circle/new.h>
#include <circle/util.h>
#include <fatfs/ff.h>			// FF_VOLUME_STRS: the volumes' names

#define IMG_MAGIC	0x494D4147u		// "IMAG": a live TImage

enum { IMG_ST_LOADING = 0, IMG_ST_READY = 1, IMG_ST_FAILED = 2 };

struct TImgSeg
{
	u64	ulVAddr;
	u64	ulMemSz;
	u64	ulFileSz;
	u32	nFlags;				// PF_*
	boolean	bShared;			// read-only: frames of the image
	u64	nFirst;				// shared: its first frame in pFrame
	u64	nPages;				// the 64 KB pages it covers
	u8     *pInit;				// private: its ulFileSz first bytes (0: none)
};

struct TImage
{
	u32	 nMagic;
	int	 nRefs;
	unsigned nState;			// IMG_ST_*
	int	 nErr;				// FAILED: why (-KAPI_E*)
	boolean	 bNamed;			// found by its path (new processes map it)
	boolean	 bPinned;			// kept at zero references (only while named)
	u64	 ulEntry;
	unsigned nSegs;
	TImgSeg	 Seg[ELF_MAX_SEGS];
	u64	*pFrame;			// the shared segments' frames (physical addresses, 0: not yet)
	u64	 nFrames;			// its entries
	u64	 nInitBytes;			// the private segments' copies, together
	u64	 nFileSize;
	unsigned nStack;			// (ImageStack)
	// (v83) a shared library (docs/SHARED-LIBS-PLAN.md): placed once for the whole system
	boolean	 bLib;
	u64	 ulBase;			// where every process maps it (in the library arena)
	u64	 ulSpan;			// the bytes it takes there, from ulBase (0: not placed)
	unsigned nRelocs;			// the relocations applied to its data's copy at the load
	unsigned nVersion;			// its export table's version
	char	 Path[IMG_PATH_MAX];		// canonical ("": none)
	TImage	*pNext;
};

static TImage	*s_pImages;			// every live image, named or not
static unsigned	 s_nPages;			// frames held by all of them

static inline u64 Min64 (u64 a, u64 b) { return a < b ? a : b; }
static inline u64 Max64 (u64 a, u64 b) { return a > b ? a : b; }

static inline boolean Is (const TImage *o) { return o != 0 && o->nMagic == IMG_MAGIC; }

static int Refuse (const char **ppWhy, const char *pWhy, int nErr)
{
	if (ppWhy != 0) *ppWhy = pWhy;
	return nErr;
}

// ---- the canonical path (pure: unit-tested on the host) --------------------------------------------

static const char *const s_Volumes[] = { FF_VOLUME_STRS };	// "SD", "SD1"... (ffconf.h)
#define NVOLUMES	(sizeof s_Volumes / sizeof s_Volumes[0])

static inline char Lower (char c)	{ return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static inline boolean IsSep (char c)	{ return c == '/' || c == '\\'; }
static inline boolean IsAlpha (char c)	{ return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static inline boolean IsDigit (char c)	{ return c >= '0' && c <= '9'; }

// The volume prefix of p: its length with the ':' (0: none) -- letters then letters / digits (as
// ResolvePath's), or one digit (FatFs's numeric form).
static unsigned VolumeLen (const char *p)
{
	if (IsDigit (p[0])) return p[1] == ':' ? 2 : 0;
	if (!IsAlpha (p[0])) return 0;
	unsigned i = 1;
	while (IsAlpha (p[i]) || IsDigit (p[i])) i++;
	return p[i] == ':' ? i + 1 : 0;
}

// The volume named by the n characters at p, written canonical ("sd:") at pOut -> its length, 0:
// no room. One spelling per volume: lower case; "0:".."8:" are FF_VOLUME_STRS's names; "SD0" is "SD".
static unsigned PutVolume (const char *p, unsigned n, char *pOut)
{
	const char *pName = p;
	if (n == 1 && IsDigit (p[0]) && (unsigned) (p[0] - '0') < NVOLUMES)
	{
		pName = s_Volumes[p[0] - '0'];
		n = strlen (pName);
	}
	else if (n == 3 && Lower (p[0]) == 's' && Lower (p[1]) == 'd' && p[2] == '0')
	{
		n = 2;
	}
	if (n + 2 > IMG_PATH_MAX) return 0;
	for (unsigned i = 0; i < n; i++) pOut[i] = Lower (pName[i]);
	pOut[n] = ':';
	return n + 1;
}

boolean ImageCanonPath (const char *pIn, const char *pCwd, char *pOut)
{
	pOut[0] = '\0';
	if (pIn == 0 || pIn[0] == '\0') return FALSE;
	if (pCwd == 0 || VolumeLen (pCwd) == 0) pCwd = "SD:/";

	const char *Part[2] = { 0, pIn };		// the names: pCwd's (a relative path), then pIn's
	const char *pVol = pIn;
	unsigned nVol = VolumeLen (pIn);
	if (nVol != 0)
	{
		Part[1] = pIn + nVol;
	}
	else
	{
		pVol = pCwd;
		nVol = VolumeLen (pCwd);
		if (!IsSep (pIn[0])) Part[0] = pCwd + nVol;
	}
	unsigned o = PutVolume (pVol, nVol - 1, pOut);
	if (o == 0) return FALSE;
	unsigned nRoot = o;
	unsigned Start[64];				// where each name's '/' is (for "..")
	int nDepth = 0;
	for (unsigned k = 0; k < 2; k++)
	{
		const char *p = Part[k];
		if (p == 0) continue;
		while (*p != '\0')
		{
			while (IsSep (*p)) p++;
			if (*p == '\0') break;
			unsigned n = 0;
			while (p[n] != '\0' && !IsSep (p[n])) n++;
			const char *pName = p;
			p += n;
			if (n == 1 && pName[0] == '.') continue;
			if (n == 2 && pName[0] == '.' && pName[1] == '.')
			{
				if (nDepth > 0) o = Start[--nDepth];
				continue;
			}
			while (n > 0 && (pName[n - 1] == '.' || pName[n - 1] == ' ')) n--;	// (as FatFs)
			if (n == 0) continue;
			if (nDepth == 64 || o + 1 + n >= IMG_PATH_MAX)
			{
				pOut[0] = '\0';
				return FALSE;
			}
			Start[nDepth++] = o;
			pOut[o++] = '/';
			for (unsigned i = 0; i < n; i++) pOut[o++] = Lower (pName[i]);
		}
	}
	if (o == nRoot) pOut[o++] = '/';		// the volume's root: "sd:/"
	pOut[o] = '\0';
	return TRUE;
}

// ---- the objects -------------------------------------------------------------------------------------

static TImage *FindNamed (const char *pKey)
{
	for (TImage *o = s_pImages; o != 0; o = o->pNext)
	{
		if (o->bNamed && strcmp (o->Path, pKey) == 0) return o;
	}
	return 0;
}

// What it holds given back (nothing maps its frames).
static void FreeMemory (TImage *o)
{
	for (u64 i = 0; i < o->nFrames; i++)
	{
		if (o->pFrame[i] != 0)
		{
			WordWaitsZap (o->pFrame[i], KPAGE_SIZE);
			pfree ((void *) (uintptr) o->pFrame[i]);
			s_nPages--;
		}
	}
	delete [] o->pFrame;
	o->pFrame = 0;
	o->nFrames = 0;
	for (unsigned i = 0; i < o->nSegs; i++)
	{
		delete [] o->Seg[i].pInit;
		o->Seg[i].pInit = 0;
	}
	o->nInitBytes = 0;
}

static void Free (TImage *o)
{
	for (TImage **pp = &s_pImages; *pp != 0; pp = &(*pp)->pNext)
	{
		if (*pp == o) { *pp = o->pNext; break; }
	}
	FreeMemory (o);
	o->nMagic = 0;
	delete o;
}

// No new process maps it from now on; not kept either. Freed now if nothing holds it.
static void Unname (TImage *o)
{
	o->bNamed = FALSE;
	o->bPinned = FALSE;
	if (o->nRefs == 0) Free (o);
}

void ImageRelease (TImage *o)
{
	if (!Is (o) || --o->nRefs > 0) return;
	if (o->bPinned && o->nState == IMG_ST_READY) return;	// (a preload: kept)
	Free (o);
}

// ---- the streaming load --------------------------------------------------------------------------------

// nBytes of the file read to pDst, all of them; the other tasks run every IMG_CHUNK bytes.
static int ReadRun (const TImgSource *pSrc, u64 ulOffset, u8 *pDst, unsigned nBytes, unsigned *pSince)
{
	if (pSrc->pRead (pSrc->pCtx, ulOffset, pDst, nBytes) != (int) nBytes) return -KAPI_EIO;
	*pSince += nBytes;
	if (*pSince >= IMG_CHUNK)
	{
		*pSince = 0;
		CScheduler::Get ()->Yield ();		// let the UI / compositor run
	}
	return 0;
}

// A read-only segment: a zeroed frame per page, the file's bytes read straight into them -- one
// read for the frames that follow each other in memory, IMG_CHUNK bytes at most.
static int LoadShared (TImage *o, const TImgSeg &S, u64 ulOffset, const TImgSource *pSrc, unsigned *pSince)
{
	u64 ulStart = KPAGE_ALIGN_DOWN (S.ulVAddr);
	u64 ulFileEnd = S.ulVAddr + S.ulFileSz;
	u8 *pRun = 0;					// the read being gathered
	u64 ulRunOff = 0;
	unsigned nRun = 0;
	for (u64 i = 0; i < S.nPages; i++)
	{
		u8 *pFrame = (u8 *) palloc_high ();	// identity-mapped: kernel VA == PA
		if (pFrame == 0) return -KAPI_ENOMEM;
		memset (pFrame, 0, KPAGE_SIZE);		// (the bss, the rest of the last page)
		o->pFrame[S.nFirst + i] = (u64) (uintptr) pFrame;
		s_nPages++;

		// Overlap of [ulVAddr, ulVAddr + ulFileSz) with this page.
		u64 va = ulStart + i * KPAGE_SIZE;
		u64 ulCopyStart = Max64 (va, S.ulVAddr);
		u64 ulCopyEnd   = Min64 (va + KPAGE_SIZE, ulFileEnd);
		if (ulCopyStart >= ulCopyEnd) continue;
		u8 *pDst = pFrame + (ulCopyStart - va);
		unsigned nLen = (unsigned) (ulCopyEnd - ulCopyStart);
		u64 ulOff = ulOffset + (ulCopyStart - S.ulVAddr);
		if (nRun != 0 && pRun + nRun == pDst && ulRunOff + nRun == ulOff && nRun + nLen <= IMG_CHUNK)
		{
			nRun += nLen;
			continue;
		}
		if (nRun != 0)
		{
			int r = ReadRun (pSrc, ulRunOff, pRun, nRun, pSince);
			if (r < 0) return r;
		}
		pRun = pDst; ulRunOff = ulOff; nRun = nLen;
	}
	return nRun != 0 ? ReadRun (pSrc, ulRunOff, pRun, nRun, pSince) : 0;
}

// A writable segment: its file bytes kept in the kernel heap (each process copies them).
static int LoadPrivate (TImage *o, TImgSeg &S, u64 ulOffset, const TImgSource *pSrc, unsigned *pSince)
{
	if (S.ulFileSz == 0) return 0;
	S.pInit = new u8[S.ulFileSz];
	if (S.pInit == 0) return -KAPI_ENOMEM;
	o->nInitBytes += S.ulFileSz;
	for (u64 nDone = 0; nDone < S.ulFileSz; )
	{
		unsigned nLen = (unsigned) Min64 (S.ulFileSz - nDone, IMG_CHUNK);
		int r = ReadRun (pSrc, ulOffset + nDone, S.pInit + nDone, nLen, pSince);
		if (r < 0) return r;
		nDone += nLen;
	}
	return 0;
}

// ---- a shared library: its place, its relocations (v83) ---------------------------------------------

// A range of the library arena for o (ulSpan set): the lowest one that no other library holds, a
// page left free between two. The live images are the allocator's state: a library being loaded
// or still mapped by a process running an old build keeps its range. No yield.
static boolean LibPlace (TImage *o)
{
	u64 ulBase = USER_LIB_BASE;
	for (const TImage *p = s_pImages; p != 0; )
	{
		if (   p != o && p->bLib && p->ulSpan != 0
		    && ulBase < p->ulBase + p->ulSpan + KPAGE_SIZE && p->ulBase < ulBase + o->ulSpan + KPAGE_SIZE)
		{
			ulBase = p->ulBase + p->ulSpan + KPAGE_SIZE;	// (past it: look at them all again)
			p = s_pImages;
			continue;
		}
		p = p->pNext;
	}
	if (ulBase >= USER_LIB_END || o->ulSpan > USER_LIB_END - ulBase) return FALSE;
	o->ulBase = ulBase;
	return TRUE;
}

// n bytes of the loaded library at its link address ulVAddr (its frames, or its data's copy; the
// bss reads zero) -> FALSE: outside its segments.
static boolean LibPeek (const TImage *o, u64 ulVAddr, void *pOut, u64 n)
{
	u8 *pDst = (u8 *) pOut;
	while (n != 0)
	{
		const TImgSeg *pSeg = 0;
		for (unsigned i = 0; i < o->nSegs; i++)
		{
			if (ulVAddr >= o->Seg[i].ulVAddr && ulVAddr - o->Seg[i].ulVAddr < o->Seg[i].ulMemSz) pSeg = &o->Seg[i];
		}
		if (pSeg == 0) return FALSE;
		u64 nRun = Min64 (n, pSeg->ulMemSz - (ulVAddr - pSeg->ulVAddr));
		if (pSeg->bShared)
		{
			u64 ulStart = KPAGE_ALIGN_DOWN (pSeg->ulVAddr);
			u64 nPage = (ulVAddr - ulStart) / KPAGE_SIZE, nOff = (ulVAddr - ulStart) % KPAGE_SIZE;
			nRun = Min64 (nRun, KPAGE_SIZE - nOff);
			memcpy (pDst, (const u8 *) (uintptr) o->pFrame[pSeg->nFirst + nPage] + nOff, (size_t) nRun);
		}
		else
		{
			u64 nOff = ulVAddr - pSeg->ulVAddr;
			if (nOff < pSeg->ulFileSz)
			{
				nRun = Min64 (nRun, pSeg->ulFileSz - nOff);
				memcpy (pDst, pSeg->pInit + nOff, (size_t) nRun);
			}
			else
			{
				memset (pDst, 0, (size_t) nRun);
			}
		}
		pDst += nRun; ulVAddr += nRun; n -= nRun;
	}
	return TRUE;
}

static char s_RelocWhy[64];			// "relocation type N ..." (the log's reason)

// The library's relocations applied to its data's copy -- once: every process then copies bytes
// that already name the library's place. Only R_AARCH64_RELATIVE, only in the writable segment's
// file bytes (the code is shared as it is in the file: never patched).
static int LibRelocate (TImage *o, const TElfPlan &Plan, const char **ppWhy)
{
	TImgSeg &D = o->Seg[1];
	u64 ulRela = 0, nRelaSz = 0, nRelaEnt = sizeof (Elf64_Rela);
	for (u64 i = 0; i + sizeof (Elf64_Dyn) <= Plan.ulDynSize; i += sizeof (Elf64_Dyn))
	{
		Elf64_Dyn Dyn;
		memcpy (&Dyn, D.pInit + (Plan.ulDynVAddr - D.ulVAddr) + i, sizeof Dyn);
		if (Dyn.d_tag == DT_NULL) break;
		switch (Dyn.d_tag)
		{
		case DT_RELA:	 ulRela = Dyn.d_val;	break;
		case DT_RELASZ:	 nRelaSz = Dyn.d_val;	break;
		case DT_RELAENT: nRelaEnt = Dyn.d_val;	break;
		case DT_TEXTREL: return Refuse (ppWhy, "the library has text relocations", -KAPI_EINVAL);
		case DT_FLAGS:
			if (Dyn.d_val & DF_TEXTREL) return Refuse (ppWhy, "the library has text relocations", -KAPI_EINVAL);
			break;
		case DT_REL:
		case DT_JMPREL:	 return Refuse (ppWhy, "the library has relocations the kernel does not apply (REL / PLT)", -KAPI_EINVAL);
		}
	}
	if (nRelaSz == 0) return 0;
	if (nRelaEnt != sizeof (Elf64_Rela) || nRelaSz % nRelaEnt != 0)
	{
		return Refuse (ppWhy, "bad relocation table", -KAPI_EINVAL);
	}
	for (u64 i = 0; i < nRelaSz; i += nRelaEnt)
	{
		Elf64_Rela Rela;
		if (ulRela + i < ulRela || !LibPeek (o, ulRela + i, &Rela, sizeof Rela))
		{
			return Refuse (ppWhy, "relocation table outside the library", -KAPI_EINVAL);
		}
		u32 nType = ELF64_R_TYPE (Rela.r_info);
		if (nType == R_AARCH64_NONE) continue;
		if (nType != R_AARCH64_RELATIVE)
		{
			static const char Msg[] = "relocation type ";
			unsigned k = sizeof Msg - 1;
			memcpy (s_RelocWhy, Msg, k);
			char Digits[12]; unsigned nD = 0;
			do { Digits[nD++] = (char) ('0' + nType % 10); nType /= 10; } while (nType != 0);
			while (nD != 0) s_RelocWhy[k++] = Digits[--nD];
			static const char Tail[] = " (only RELATIVE is applied)";
			memcpy (s_RelocWhy + k, Tail, sizeof Tail);
			return Refuse (ppWhy, s_RelocWhy, -KAPI_EINVAL);
		}
		if (D.ulFileSz < 8 || Rela.r_offset < D.ulVAddr || Rela.r_offset - D.ulVAddr > D.ulFileSz - 8)
		{
			return Refuse (ppWhy, "relocation outside the library's data", -KAPI_EINVAL);
		}
		u64 ulValue = o->ulBase + Rela.r_addend;
		memcpy (D.pInit + (Rela.r_offset - D.ulVAddr), &ulValue, sizeof ulValue);
		o->nRelocs++;
	}
	return 0;
}

// The file streamed into o -> 0 / -errno (what was allocated stays in o: the caller frees it).
static int Load (TImage *o, const TImgSource *pSrc, unsigned nKind, const char **ppWhy)
{
	TElfPlan Plan;
	int r = ElfReadPlan (pSrc, &Plan, ppWhy, nKind);
	if (r < 0) return r;
	o->bLib = Plan.bLib;

	u64 nShared = 0;
	for (unsigned i = 0; i < Plan.nSegs; i++)
	{
		const TElfSeg &E = Plan.Seg[i];
		TImgSeg &S = o->Seg[i];
		S.ulVAddr  = E.ulVAddr;
		S.ulMemSz  = E.ulMemSz;
		S.ulFileSz = E.ulFileSz;
		S.nFlags   = E.nFlags;
		S.bShared  = (E.nFlags & PF_W) == 0;	// (never a writable one)
		S.nPages   = (KPAGE_ALIGN_UP (E.ulVAddr + E.ulMemSz) - KPAGE_ALIGN_DOWN (E.ulVAddr)) / KPAGE_SIZE;
		S.nFirst   = nShared;
		S.pInit    = 0;
		if (S.bShared) nShared += S.nPages;
	}
	o->nSegs = Plan.nSegs;
	o->ulEntry = Plan.ulEntry;
	o->nFileSize = pSrc->nSize;
	if (o->bLib)				// its place, before the first yield: no other load takes it
	{
		o->ulSpan = KPAGE_ALIGN_UP (Plan.Seg[1].ulVAddr + Plan.Seg[1].ulMemSz);
		if (!LibPlace (o))
		{
			o->ulSpan = 0;
			return Refuse (ppWhy, "no room in the library arena", -KAPI_ENOMEM);
		}
	}

	// A preload must not take the app pool's reserve (a process's own start is as before v77: it
	// takes what there is).
	if (o->bPinned && !VmCommitOK (nShared * KPAGE_SIZE))
	{
		return Refuse (ppWhy, "not enough free memory to keep it", -KAPI_ENOMEM);
	}
	if (nShared != 0)
	{
		o->pFrame = new u64[nShared];
		if (o->pFrame == 0) return Refuse (ppWhy, "out of memory", -KAPI_ENOMEM);
		memset (o->pFrame, 0, nShared * sizeof (u64));
		o->nFrames = nShared;
	}

	unsigned nSince = 0;
	for (unsigned i = 0; i < Plan.nSegs && r == 0; i++)
	{
		r = o->Seg[i].bShared ? LoadShared (o, o->Seg[i], Plan.Seg[i].ulOffset, pSrc, &nSince)
				      : LoadPrivate (o, o->Seg[i], Plan.Seg[i].ulOffset, pSrc, &nSince);
	}
	if (r < 0) return Refuse (ppWhy, r == -KAPI_ENOMEM ? "out of memory" : "read failed", r);
	if (o->bLib)
	{
		r = LibRelocate (o, Plan, ppWhy);
		if (r < 0) return r;
		const TImgSeg &D = o->Seg[1];	// (the table is in its file bytes: ElfReadPlan)
		memcpy (&o->nVersion, D.pInit + (o->ulEntry - D.ulVAddr), sizeof o->nVersion);
	}
	return 0;
}

int ImageOpen (const char *pPath, const char *pCwd, const TImgSource *pSrc, unsigned nFlags,
	       TImage **ppImage, unsigned *pHow, const char **ppWhy)
{
	*ppImage = 0;
	char Key[IMG_PATH_MAX];
	Key[0] = '\0';
	if (pPath != 0) ImageCanonPath (pPath, pCwd, Key);	// (too long: an image of its own)

	TImage *o = Key[0] != '\0' ? FindNamed (Key) : 0;
	if (o != 0)					// in memory: the card is not touched
	{
		o->nRefs++;
		if (nFlags & IMG_OPEN_PIN) o->bPinned = TRUE;
		unsigned nHow = IMG_HOW_SHARED;
		while (o->nState == IMG_ST_LOADING)	// another task reads it: wait (it yields)
		{
			nHow = IMG_HOW_WAITED;
			CScheduler::Get ()->MsSleep (IMG_WAIT_MS);
		}
		if (o->nState != IMG_ST_READY)
		{
			int nErr = o->nErr;
			ImageRelease (o);
			return Refuse (ppWhy, "its load by another task failed", nErr);
		}
		if (!(nFlags & IMG_OPEN_ANY) && o->bLib != ((nFlags & IMG_OPEN_LIB) != 0))
		{
			boolean bLib = o->bLib;
			ImageRelease (o);
			return Refuse (ppWhy, bLib ? "a shared library, not a program" : "a program, not a shared library",
				       -KAPI_EINVAL);
		}
		*ppImage = o;
		if (pHow != 0) *pHow = nHow;
		return 0;
	}
	if (pSrc == 0) return Refuse (ppWhy, "not in memory", -KAPI_ENOENT);

	o = new TImage;
	if (o == 0) return Refuse (ppWhy, "out of memory", -KAPI_ENOMEM);
	memset (o, 0, sizeof *o);
	o->nMagic = IMG_MAGIC;
	o->nRefs = 1;
	o->nState = IMG_ST_LOADING;
	o->bNamed = Key[0] != '\0';
	o->bPinned = o->bNamed && (nFlags & IMG_OPEN_PIN) != 0;
	strcpy (o->Path, Key);
	o->pNext = s_pImages;
	s_pImages = o;

	int r = Load (o, pSrc, nFlags & IMG_OPEN_ANY ? ELF_KIND_ANY : nFlags & IMG_OPEN_LIB ? ELF_KIND_LIB : ELF_KIND_PROGRAM,
		      ppWhy);				// (yields: found, waited for, unnamed meanwhile)
	if (r < 0)
	{
		FreeMemory (o);
		o->ulSpan = 0;				// (its place in the arena given back)
		o->nState = IMG_ST_FAILED;		// (its waiters see it, and let it go)
		o->nErr = r;
		o->bNamed = FALSE;
		o->bPinned = FALSE;
		ImageRelease (o);
		return r;
	}
	o->nState = IMG_ST_READY;
	*ppImage = o;
	if (pHow != 0) *pHow = IMG_HOW_LOADED;
	return 0;
}

// ---- a process's mapping ----------------------------------------------------------------------------

// o's segments mapped in pAS, ulBase added to their addresses (a program: 0).
static boolean MapSegs (TImage *o, CAddressSpace *pAS, u64 ulBase)
{
	TKPageAttr Code = KPAGE_ATTR_APP_CODE;		// EL0 RX (apps run at EL0)
	TKPageAttr Data = KPAGE_ATTR_APP_DATA;		// EL0 RW
	TKPageAttr RoData = KPAGE_ATTR_APP_RODATA;	// EL0 R
	for (unsigned i = 0; i < o->nSegs; i++)
	{
		const TImgSeg &S = o->Seg[i];
		u64 ulStart = KPAGE_ALIGN_DOWN (S.ulVAddr);
		boolean bExec = (S.nFlags & PF_X) != 0;
		if (S.bShared)
		{
			// The image's frames: not owned (the teardown leaves them), never writable.
			const TKPageAttr &Attr = bExec ? Code : RoData;
			for (u64 k = 0; k < S.nPages; k++)
			{
				if (!pAS->MapPage (ulBase + ulStart + k * KPAGE_SIZE, o->pFrame[S.nFirst + k], Attr, FALSE))
				{
					return FALSE;
				}
			}
		}
		else
		{
			// Its own pages (as before v77: PF_X decides the attributes), the first bytes copied.
			const TKPageAttr &Attr = bExec ? Code : Data;
			u64 ulFileEnd = S.ulVAddr + S.ulFileSz;
			for (u64 k = 0; k < S.nPages; k++)
			{
				u64 va = ulStart + k * KPAGE_SIZE;
				u8 *pFrame = (u8 *) pAS->MapNewPage (ulBase + va, Attr);	// zeroed, identity addr
				if (pFrame == 0) return FALSE;
				u64 ulCopyStart = Max64 (va, S.ulVAddr);
				u64 ulCopyEnd   = Min64 (va + KPAGE_SIZE, ulFileEnd);
				if (ulCopyStart < ulCopyEnd)
				{
					memcpy (pFrame + (ulCopyStart - va), S.pInit + (ulCopyStart - S.ulVAddr),
						(size_t) (ulCopyEnd - ulCopyStart));
				}
			}
		}
		// (v75) An eager region of its own (kern/vm.h): never filled on demand.
		VmNoteRegion (pAS, ulBase + S.ulVAddr, ulBase + S.ulVAddr + S.ulMemSz,
			      bExec ? KAPI_PROT_READ | KAPI_PROT_EXEC
				    : S.bShared ? KAPI_PROT_READ : KAPI_PROT_READ | KAPI_PROT_WRITE,
			      KAPI_VMK_IMAGE);
	}
	return TRUE;
}

boolean ImageMap (TImage *o, CAddressSpace *pAS, u64 *pEntry)
{
	if (!Is (o) || o->nState != IMG_ST_READY || o->bLib || pAS == 0 || pAS->GetImage () != 0) return FALSE;
	o->nRefs++;					// pAS's own (dropped by its destructor)
	pAS->SetImage (o);
	if (!MapSegs (o, pAS, 0)) return FALSE;
	*pEntry = o->ulEntry;
	return TRUE;
}

int ImageMapLib (TImage *o, CAddressSpace *pAS, u64 *pTable)
{
	if (!Is (o) || o->nState != IMG_ST_READY || !o->bLib || pAS == 0) return -KAPI_EINVAL;
	int n = pAS->FindLib (o);
	if (n >= 0)					// mapped already: the same table
	{
		if (!pAS->LibReady (n)) return -KAPI_ENOMEM;	// (its mapping failed half-way: not again)
		*pTable = o->ulBase + o->ulEntry;
		return 0;
	}
	if (!pAS->AddLib (o)) return -KAPI_EMFILE;	// (AS_LIB_MAX libraries in one process)
	o->nRefs++;					// pAS's own (dropped by its destructor)
	if (!MapSegs (o, pAS, o->ulBase)) return -KAPI_ENOMEM;
	pAS->SetLibReady (o);
	*pTable = o->ulBase + o->ulEntry;
	return 1;
}

// ---- unload, the file layer's hook, the list ---------------------------------------------------------

int ImageUnload (const char *pPath, const char *pCwd)
{
	char Key[IMG_PATH_MAX];
	if (s_pImages == 0 || !ImageCanonPath (pPath, pCwd, Key)) return -KAPI_ENOENT;
	TImage *o = FindNamed (Key);
	if (o == 0) return -KAPI_ENOENT;
	Unname (o);
	return 0;
}

void ImageFileChanged (const char *pAbsPath)
{
	if (s_pImages == 0) return;			// (the usual case at boot; cheap otherwise too)
	char Key[IMG_PATH_MAX];
	if (!ImageCanonPath (pAbsPath, 0, Key)) return;	// (longer than any key)
	unsigned n = strlen (Key);
	// "<folder>/app.txt": where the stack size of "<folder>/main" is (ImageStack)
	boolean bAppTxt = n > 8 && strcmp (Key + n - 8, "/app.txt") == 0;
	for (TImage *o = s_pImages, *pNext; o != 0; o = pNext)
	{
		pNext = o->pNext;
		if (!o->bNamed) continue;
		if (   strcmp (o->Path, Key) == 0
		    || (strncmp (o->Path, Key, n) == 0 && (o->Path[n] == '/' || Key[n - 1] == '/')))
		{
			Unname (o);
		}
		else if (bAppTxt && strncmp (o->Path, Key, n - 7) == 0 && strcmp (o->Path + n - 7, "main") == 0)
		{
			o->nStack = 0;			// (the image stays: its next start reads app.txt again)
		}
	}
}

static void Describe (const TImage *o, struct kapi_image_info *pOut)
{
	memset (pOut, 0, sizeof *pOut);
	pOut->size = o->nFrames * KPAGE_SIZE + o->nInitBytes;
	pOut->file_size = o->nFileSize;
	pOut->refs = (unsigned) o->nRefs;
	pOut->flags = (o->bPinned ? KAPI_IMG_KEPT : 0) | (o->nState == IMG_ST_LOADING ? KAPI_IMG_LOADING : 0)
		    | (o->bNamed ? 0 : KAPI_IMG_UNNAMED) | (o->bLib ? KAPI_IMG_LIB : 0);
	strcpy (pOut->path, o->Path);
}

unsigned ImageList (const char *pPath, const char *pCwd, struct kapi_image_info *pOut, unsigned nCap)
{
	if (pPath != 0)
	{
		char Key[IMG_PATH_MAX];
		TImage *o = s_pImages != 0 && ImageCanonPath (pPath, pCwd, Key) ? FindNamed (Key) : 0;
		if (o == 0) return 0;
		if (nCap > 0) Describe (o, pOut);
		return 1;
	}
	unsigned n = 0;
	for (const TImage *o = s_pImages; o != 0; o = o->pNext)
	{
		if (o->nState == IMG_ST_FAILED) continue;	// (only its waiters hold it still)
		if (n < nCap) Describe (o, &pOut[n]);
		n++;
	}
	return n;
}

unsigned ImagePagesTotal (void)
{
	return s_nPages;
}

void ImageSizes (const TImage *o, u64 *pShared, u64 *pPrivate)
{
	u64 nPrivate = 0;
	for (unsigned i = 0; i < o->nSegs; i++)
	{
		if (!o->Seg[i].bShared) nPrivate += o->Seg[i].nPages * KPAGE_SIZE;
	}
	*pShared = o->nFrames * KPAGE_SIZE;
	*pPrivate = nPrivate;
}

boolean ImageLibInfo (const TImage *o, u64 *pBase, unsigned *pRelocs, unsigned *pVersion)
{
	if (!Is (o) || !o->bLib) return FALSE;
	if (pBase != 0) *pBase = o->ulBase;
	if (pRelocs != 0) *pRelocs = o->nRelocs;
	if (pVersion != 0) *pVersion = o->nVersion;
	return TRUE;
}

unsigned ImageStack (const TImage *o)
{
	return Is (o) ? o->nStack : 0;
}

void ImageSetStack (TImage *o, unsigned nStack)
{
	if (Is (o)) o->nStack = nStack;
}
