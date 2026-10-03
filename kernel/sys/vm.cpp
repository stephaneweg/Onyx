//
// vm.cpp -- an app's virtual memory (kern/vm.h): the regions, demand paging, the pins and the
// deferred zap, and the kapis vm_map / vm_unmap / vm_protect / vm_advise / vm_query / vm_stats
// (kapi v75 slots 199..204, docs/POSIX-PLAN.md §3.1). thread_create_ex / thread_info (205, 206)
// are in sys/thread.cpp, with the threads.
//
// Owner: WP-MEM.
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
#include <kern/vm.h>
#include <kern/addrspace.h>
#include <kern/kapi_abi.h>
#include <kern/layout.h>
#include <kern/trapframe.h>
#include <kern/uaccess.h>		// UserPut (vm_query, vm_stats)
#include <kern/lsock.h>			// the shared objects (v76: SHM regions)
#include <kern/handle.h>		// shm_map's handle
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/memory.h>		// the app pool's free counters
#include <circle/alloc.h>		// pfree
#include <circle/logger.h>
#include <circle/util.h>

static const char From[] = "vm";

static inline u64 Min64 (u64 a, u64 b)	{ return a < b ? a : b; }
static inline u64 Max64 (u64 a, u64 b)	{ return a > b ? a : b; }

static inline boolean IsLazy (unsigned nKind)
{
	return nKind == KAPI_VMK_ANON || nKind == KAPI_VMK_HEAP || nKind == KAPI_VMK_STACK || nKind == KAPI_VMK_SHM;
}

// ---- the region list (pure data: unit-tested on the host) ------------------------------------
// VMALIST-BEGIN

// The first region ending above ulVA (nVma: none).
static unsigned VmaLowerBound (const TVmSpace *pVm, u64 ulVA)
{
	unsigned lo = 0, hi = pVm->nVma;
	while (lo < hi)
	{
		unsigned mid = (lo + hi) / 2;
		if (pVm->pVma[mid].ulEnd <= ulVA) lo = mid + 1; else hi = mid;
	}
	return lo;
}

// The region holding ulVA, or -1.
static int VmaFind (const TVmSpace *pVm, u64 ulVA)
{
	unsigned i = VmaLowerBound (pVm, ulVA);
	return i < pVm->nVma && pVm->pVma[i].ulStart <= ulVA ? (int) i : -1;
}

// Room for nMore regions (the array grown by doubling, at most VM_MAX_VMAS). FALSE: none.
static boolean VmaRoom (TVmSpace *pVm, unsigned nMore)
{
	unsigned nWant = pVm->nVma + nMore;
	if (nWant > VM_MAX_VMAS) return FALSE;
	if (nWant <= pVm->nCap) return TRUE;
	unsigned nCap = pVm->nCap != 0 ? pVm->nCap : 16;
	while (nCap < nWant) nCap *= 2;
	if (nCap > VM_MAX_VMAS) nCap = VM_MAX_VMAS;
	TVma *p = new TVma[nCap];
	if (p == 0) return FALSE;
	for (unsigned i = 0; i < pVm->nVma; i++) p[i] = pVm->pVma[i];
	delete [] pVm->pVma;
	pVm->pVma = p;
	pVm->nCap = nCap;
	return TRUE;
}

static void VmaInsertAt (TVmSpace *pVm, unsigned i, const TVma &V)	// (VmaRoom first)
{
	for (unsigned k = pVm->nVma; k > i; k--) pVm->pVma[k] = pVm->pVma[k - 1];
	pVm->pVma[i] = V;
	pVm->nVma++;
}

static void VmaEraseAt (TVmSpace *pVm, unsigned i, unsigned n)
{
	if (n == 0) return;
	for (unsigned k = i; k + n < pVm->nVma; k++) pVm->pVma[k] = pVm->pVma[k + n];
	pVm->nVma -= n;
}

// Does any region overlap [s, e)?
static boolean VmaOverlaps (const TVmSpace *pVm, u64 s, u64 e)
{
	unsigned i = VmaLowerBound (pVm, s);
	return i < pVm->nVma && pVm->pVma[i].ulStart < e;
}

// Is [s, e) covered without a hole by regions whose kind is in nKindMask (bit 1 << kind)?
static boolean VmaCovered (const TVmSpace *pVm, u64 s, u64 e, unsigned nKindMask)
{
	u64 ulAt = s;
	for (unsigned i = VmaLowerBound (pVm, s); i < pVm->nVma; i++)
	{
		const TVma &V = pVm->pVma[i];
		if (V.ulStart > ulAt || !(nKindMask & (1u << V.nKind))) return FALSE;
		ulAt = V.ulEnd;
		if (ulAt >= e) return TRUE;
	}
	return FALSE;
}

// Would cutting [s, e) need one more region (one region across both edges)?
static boolean VmaCutSplits (const TVmSpace *pVm, u64 s, u64 e)
{
	int i = VmaFind (pVm, s);
	return i >= 0 && pVm->pVma[i].ulStart < s && pVm->pVma[i].ulEnd > e;
}

// [s, e) taken out of every region (those across its edges trimmed). FALSE: no room for the split
// (nothing changed).
static boolean VmaCut (TVmSpace *pVm, u64 s, u64 e)
{
	unsigned i = VmaLowerBound (pVm, s);
	if (i < pVm->nVma && pVm->pVma[i].ulStart < s && pVm->pVma[i].ulEnd > e)
	{
		if (!VmaRoom (pVm, 1)) return FALSE;
		TVma Hi = pVm->pVma[i];
		if (Hi.pObj != 0) Hi.ulObjOff += e - Hi.ulStart;	// (SHM: each piece keeps its offset)
		Hi.ulStart = e;
		pVm->pVma[i].ulEnd = s;
		VmaInsertAt (pVm, i + 1, Hi);
		return TRUE;
	}
	if (i < pVm->nVma && pVm->pVma[i].ulStart < s)
	{
		pVm->pVma[i].ulEnd = s;			// (its end is <= e: the case above)
		i++;
	}
	unsigned j = i;
	while (j < pVm->nVma && pVm->pVma[j].ulEnd <= e) j++;
	VmaEraseAt (pVm, i, j - i);
	if (i < pVm->nVma && pVm->pVma[i].ulStart < e)
	{
		if (pVm->pVma[i].pObj != 0) pVm->pVma[i].ulObjOff += e - pVm->pVma[i].ulStart;
		pVm->pVma[i].ulStart = e;
	}
	return TRUE;
}

// A boundary at ulVA (the region across it split). FALSE: no room.
static boolean VmaSplitAt (TVmSpace *pVm, u64 ulVA)
{
	int i = VmaFind (pVm, ulVA);
	if (i < 0 || pVm->pVma[i].ulStart == ulVA) return TRUE;
	if (!VmaRoom (pVm, 1)) return FALSE;
	TVma Hi = pVm->pVma[i];
	if (Hi.pObj != 0) Hi.ulObjOff += ulVA - Hi.ulStart;
	Hi.ulStart = ulVA;
	pVm->pVma[i].ulEnd = ulVA;
	VmaInsertAt (pVm, (unsigned) i + 1, Hi);
	return TRUE;
}

// Insert a region over a free range (VmaRoom (1) first).
static void VmaInsert (TVmSpace *pVm, u64 s, u64 e, unsigned nProt, unsigned nKind)
{
	TVma V;
	V.ulStart = s; V.ulEnd = e;
	V.nProt = (u16) nProt; V.nKind = (u16) nKind; V.nFlags = 0;
	V.pObj = 0; V.ulObjOff = 0;
	VmaInsertAt (pVm, VmaLowerBound (pVm, s), V);
}

// (v76) Insert an SHM region (pObj from offset ulOff) over a free range (VmaRoom (1) first).
static void VmaInsertShm (TVmSpace *pVm, u64 s, u64 e, unsigned nProt, void *pObj, u64 ulOff, u32 nFlags)
{
	TVma V;
	V.ulStart = s; V.ulEnd = e;
	V.nProt = (u16) nProt; V.nKind = KAPI_VMK_SHM; V.nFlags = nFlags;
	V.pObj = pObj; V.ulObjOff = ulOff;
	VmaInsertAt (pVm, VmaLowerBound (pVm, s), V);
}

// Neighbours alike merged around [s, e) (the stacks never: each is its own).
static void VmaMerge (TVmSpace *pVm, u64 s, u64 e)
{
	unsigned i = VmaLowerBound (pVm, s);
	if (i > 0) i--;
	while (i + 1 < pVm->nVma && pVm->pVma[i].ulStart <= e)
	{
		TVma &A = pVm->pVma[i], &B = pVm->pVma[i + 1];
		if (   A.ulEnd == B.ulStart && A.nProt == B.nProt && A.nKind == B.nKind
		    && A.nFlags == B.nFlags && A.nKind != KAPI_VMK_STACK && A.pObj == B.pObj
		    && (A.pObj == 0 || A.ulObjOff + (A.ulEnd - A.ulStart) == B.ulObjOff))
		{
			A.ulEnd = B.ulEnd;
			VmaEraseAt (pVm, i + 1, 1);
		}
		else
		{
			i++;
		}
	}
}

// The lowest free range of nLen bytes in [lo, hi) -> its start, 0: none.
static u64 VmaFindGap (const TVmSpace *pVm, u64 nLen, u64 lo, u64 hi)
{
	u64 ulAt = lo;
	for (unsigned i = VmaLowerBound (pVm, lo); i < pVm->nVma && pVm->pVma[i].ulStart < hi; i++)
	{
		if (pVm->pVma[i].ulStart >= ulAt && pVm->pVma[i].ulStart - ulAt >= nLen) return ulAt;
		if (pVm->pVma[i].ulEnd > ulAt) ulAt = pVm->pVma[i].ulEnd;
	}
	return ulAt < hi && hi - ulAt >= nLen ? ulAt : 0;
}

// VMALIST-END
// ---- the space ---------------------------------------------------------------------------------

static CTask *CurrentTask (void)
{
	return CScheduler::IsActive () ? CScheduler::Get ()->GetCurrentTask () : 0;
}

static CAddressSpace *CurrentAS (void)
{
	CTask *pTask = CurrentTask ();
	return pTask != 0 ? (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER) : 0;
}

TVmSpace *VmOf (CAddressSpace *pAS, boolean bCreate)
{
	if (pAS == 0) return 0;
	TVmSpace *pVm = pAS->GetVm ();
	if (pVm == 0 && bCreate)
	{
		pVm = new TVmSpace;
		if (pVm == 0) return 0;
		memset (pVm, 0, sizeof *pVm);
		pAS->SetVm (pVm);
	}
	return pVm;
}

// The deferred ranges that could not get a node of their own (the kernel heap is out): one union
// per space, in a small side table keyed by the space.
struct TVmSweep { TVmSpace *pVm; u64 ulStart, ulEnd; };
#define VM_SWEEPS	8
static TVmSweep s_Sweep[VM_SWEEPS];

void VmTeardown (CAddressSpace *pAS)
{
	TVmSpace *pVm = pAS->GetVm ();
	if (pVm == 0) return;
	while (pVm->pZapPending != 0)
	{
		TVmZap *z = pVm->pZapPending;
		pVm->pZapPending = z->pNext;
		delete z;
	}
	for (unsigned i = 0; i < VM_SWEEPS; i++) if (s_Sweep[i].pVm == pVm) s_Sweep[i].pVm = 0;
	while (pVm->pShm != 0)				// (v76: no PTE of the space is used any more)
	{
		TVmShmRef *r = pVm->pShm;
		pVm->pShm = r->pNext;
		ShmMapDetach (r->pObj, TRUE);
		delete r;
	}
	delete [] pVm->pVma;
	pAS->SetVm (0);
	delete pVm;					// (the frames: freed by ~CAddressSpace's walk)
}

// ---- the app pool -------------------------------------------------------------------------------

u64 VmPoolFree (void)
{
	// The high zone (app frames) and the low pager (where palloc_high falls back, on a 1 GB Pi
	// always; it also holds the page tables): the reserve protects both.
	return   (u64) CMemorySystem::GetPagerHighFreeSpace () + CMemorySystem::GetPagerHighFreeListSpace ()
	       + (u64) CMemorySystem::GetPagerFreeSpace () + CMemorySystem::GetPagerFreeListSpace ();
}

boolean VmCommitOK (u64 nBytes)
{
	u64 nFree = VmPoolFree ();
	return nFree > VM_RESERVE && nBytes <= nFree - VM_RESERVE;
}

// ---- the page tables ------------------------------------------------------------------------------

#define PTE_IS_VALID(v)		(((v) & 3) == 3)
#define PTE_FRAME(v)		((v) & 0x0000FFFFFFFF0000ULL)
#define PTE_AP_SHIFT		6
#define PTE_AP_MASK		(3ULL << PTE_AP_SHIFT)
#define PTE_UXN			(1ULL << 54)		// (v78) not executable at EL0
#define PTE_PROT_MASK		(PTE_AP_MASK | PTE_UXN)

static inline volatile u64 *PteOf (CAddressSpace *pAS, u64 ulVA)
{
	return (volatile u64 *) pAS->PageDesc (ulVA);
}

static inline unsigned PteAP (u64 v)
{
	return (unsigned) ((v >> PTE_AP_SHIFT) & 3);
}

// The AP of a page of a region with nProt: EL0 RW / EL0 RO / NONE = EL1 read-only, EL0 nothing
// (the frame kept: mprotect back restores it; the kernel's probes use AT S1E0*, so they refuse it,
// and no kernel write reaches it).
static unsigned ApForProt (unsigned nProt)
{
	if (nProt & KAPI_PROT_WRITE) return ATTRIB_AP_RW_ALL;
	if (nProt & KAPI_PROT_READ) return ATTRIB_AP_RO_ALL;
	return ATTRIB_AP_RO_EL1;
}

// (v78) A region's protection as PTE bits: its AP, and UXN unless it is executable (PROT_EXEC: a
// JIT's code; PXN stays set, the kernel never runs an app's code).
static inline u64 PteProt (unsigned nProt)
{
	return ((u64) ApForProt (nProt) << PTE_AP_SHIFT) | ((nProt & KAPI_PROT_EXEC) ? 0 : PTE_UXN);
}

static inline boolean ApAllows (unsigned nAP, boolean bWrite)
{
	return bWrite ? nAP == ATTRIB_AP_RW_ALL : (nAP == ATTRIB_AP_RW_ALL || nAP == ATTRIB_AP_RO_ALL);
}

// The next 512 MB slot (one L3 table each) above ulVA.
static inline u64 NextSlot (u64 ulVA)
{
	return (ulVA / L2_BLOCK_SIZE + 1) * L2_BLOCK_SIZE;
}

// PTEs changed (invalidated, or their protection changed): the TLBs of every core told, and only
// then the frames freed (with their word waiters woken first).
class CVmBatch
{
public:
	CVmBatch (CAddressSpace *pAS) : m_pAS (pAS), m_n (0) {}
	~CVmBatch (void) { Flush (); }

	void Add (u64 ulVA, u64 ulFrame)	// ulFrame: to free after the flush (0: none)
	{
		if (m_n == BATCH) Flush ();
		m_VA[m_n] = ulVA;
		m_Frame[m_n] = ulFrame;
		m_n++;
	}

	void Flush (void)
	{
		if (m_n == 0) return;
		u64 ulASID = (u64) m_pAS->GetASID () << TTBR0_ASID_SHIFT;
		asm volatile ("dsb ishst" ::: "memory");		// the PTEs before the TLBIs
		if (m_n > VM_TLBI_ASID_ABOVE)
		{
			asm volatile ("tlbi aside1is, %0" :: "r" (ulASID) : "memory");
		}
		else
		{
			for (unsigned i = 0; i < m_n; i++)
			{
				u64 ulArg = ((m_VA[i] >> 12) & 0xFFFFFFFFFFFULL) | ulASID;
				asm volatile ("tlbi vae1is, %0" :: "r" (ulArg) : "memory");
			}
		}
		asm volatile ("dsb ish; isb" ::: "memory");		// done on every core
		for (unsigned i = 0; i < m_n; i++)
		{
			if (m_Frame[i] != 0)
			{
				WordWaitsZap (m_Frame[i], KPAGE_SIZE);
				pfree ((void *) (uintptr) m_Frame[i]);
				m_pAS->PageReleased ();
			}
		}
		m_n = 0;
	}

private:
	enum { BATCH = 128 };
	CAddressSpace *m_pAS;
	unsigned m_n;
	u64 m_VA[BATCH];
	u64 m_Frame[BATCH];
};

// ---- pins ------------------------------------------------------------------------------------------

// Does a pin of a task other than pSelf (0: of any task) overlap [s, e)?
static boolean PinnedByOther (const TVmSpace *pVm, u64 s, u64 e, CTask *pSelf)
{
	if (pVm->nPins == 0) return FALSE;
	for (unsigned i = 0; i < VM_PINS; i++)
	{
		const TVmPin &P = pVm->Pin[i];
		if (P.pTask != 0 && P.pTask != pSelf && P.ulStart < e && s < P.ulEnd) return TRUE;
	}
	return FALSE;
}

void VmPin (CAddressSpace *pAS, u64 s, u64 e)
{
	TVmSpace *pVm = pAS != 0 ? pAS->GetVm () : 0;
	CTask *pTask = CurrentTask ();
	if (pVm == 0 || pTask == 0 || s >= e) return;
	TVmPin *pFree = 0;
	for (unsigned i = 0; i < VM_PINS; i++)
	{
		TVmPin &P = pVm->Pin[i];
		if (P.pTask == pTask)
		{
			P.ulStart = Min64 (P.ulStart, s);
			P.ulEnd = Max64 (P.ulEnd, e);
			return;
		}
		if (P.pTask == 0 && pFree == 0) pFree = &P;
	}
	if (pFree == 0)
	{
		static boolean s_bLogged = FALSE;	// (one per task with a kapi in progress: cannot be)
		if (!s_bLogged) CLogger::Get ()->Write (From, LogError, "pin table full");
		s_bLogged = TRUE;
		return;
	}
	pFree->pTask = pTask;
	pFree->ulStart = s;
	pFree->ulEnd = e;
	pVm->nPins++;
}

// A range with deferred pages (ZAP / SYNC bits set in their PTEs).
static void AddPending (TVmSpace *pVm, u64 s, u64 e)
{
	TVmZap *z = pVm->pZapPending;
	if (z != 0 && s <= z->ulEnd && z->ulStart <= e)		// (the usual case: the next page)
	{
		z->ulStart = Min64 (z->ulStart, s);
		z->ulEnd = Max64 (z->ulEnd, e);
		return;
	}
	z = new TVmZap;
	if (z != 0)
	{
		z->ulStart = s;
		z->ulEnd = e;
		z->pNext = pVm->pZapPending;
		pVm->pZapPending = z;
		return;
	}
	// (no kernel heap left: the space's sweep range grows instead)
	for (unsigned i = 0; i < VM_SWEEPS; i++)
	{
		if (s_Sweep[i].pVm == pVm)
		{
			s_Sweep[i].ulStart = Min64 (s_Sweep[i].ulStart, s);
			s_Sweep[i].ulEnd = Max64 (s_Sweep[i].ulEnd, e);
			return;
		}
	}
	for (unsigned i = 0; i < VM_SWEEPS; i++)
	{
		if (s_Sweep[i].pVm == 0)
		{
			s_Sweep[i].pVm = pVm; s_Sweep[i].ulStart = s; s_Sweep[i].ulEnd = e;
			return;
		}
	}
	// (the PTE bits stay: the pages are freed with the space)
}

// The deferred pages of [s, e), no pin over them any more: dropped (ZAP), or given their region's
// protection (SYNC; dropped if no lazy region holds them now).
static void Settle (CAddressSpace *pAS, TVmSpace *pVm, u64 s, u64 e, CVmBatch &Batch)
{
	for (u64 va = s; va < e; )
	{
		volatile u64 *p = PteOf (pAS, va);
		if (p == 0) { va = NextSlot (va); continue; }
		u64 v = *p;
		if (PTE_IS_VALID (v) && (v & (VM_PTE_SW_ZAP | VM_PTE_SW_SYNC)))
		{
			int i = (v & VM_PTE_SW_ZAP) ? -1 : VmaFind (pVm, va);
			if (i >= 0 && IsLazy (pVm->pVma[i].nKind))
			{
				*p = (v & ~(PTE_PROT_MASK | VM_PTE_SW_SYNC)) | PteProt (pVm->pVma[i].nProt);
				Batch.Add (va, 0);
			}
			else
			{
				*p = 0;
				Batch.Add (va, (v & VM_PTE_SW_OWNED) ? PTE_FRAME (v) : 0);
			}
		}
		va += KPAGE_SIZE;
	}
}

static void RunPending (CAddressSpace *pAS, TVmSpace *pVm)
{
	CVmBatch Batch (pAS);
	for (TVmZap **pp = &pVm->pZapPending; *pp != 0; )
	{
		TVmZap *z = *pp;
		if (PinnedByOther (pVm, z->ulStart, z->ulEnd, 0))
		{
			pp = &z->pNext;
			continue;
		}
		*pp = z->pNext;
		Settle (pAS, pVm, z->ulStart, z->ulEnd, Batch);
		delete z;
	}
	for (unsigned i = 0; i < VM_SWEEPS; i++)
	{
		TVmSweep &S = s_Sweep[i];
		if (S.pVm == pVm && !PinnedByOther (pVm, S.ulStart, S.ulEnd, 0))
		{
			S.pVm = 0;
			Settle (pAS, pVm, S.ulStart, S.ulEnd, Batch);
		}
	}
}

static boolean HasSweep (const TVmSpace *pVm)
{
	for (unsigned i = 0; i < VM_SWEEPS; i++) if (s_Sweep[i].pVm == pVm) return TRUE;
	return FALSE;
}

// (v76) The shared objects no region names any more let go -- unless deferred zaps are pending in
// the space (a PTE marked ZAP may still name a frame of one: kept until they are settled).
static void VmShmGc (TVmSpace *pVm)
{
	if (pVm->pShm == 0 || pVm->pZapPending != 0 || HasSweep (pVm)) return;
	for (TVmShmRef **pp = &pVm->pShm; *pp != 0; )
	{
		TVmShmRef *r = *pp;
		boolean bUsed = FALSE;
		for (unsigned i = 0; i < pVm->nVma && !bUsed; i++) bUsed = pVm->pVma[i].pObj == r->pObj;
		if (bUsed)
		{
			pp = &r->pNext;
			continue;
		}
		*pp = r->pNext;
		ShmMapDetach (r->pObj, FALSE);
		delete r;
	}
}

// (v76) The space maps pObj from now on (its reference taken once). FALSE: out of memory.
static boolean VmShmAttach (TVmSpace *pVm, void *pObj)
{
	for (TVmShmRef *r = pVm->pShm; r != 0; r = r->pNext) if (r->pObj == pObj) return TRUE;
	TVmShmRef *r = new TVmShmRef;
	if (r == 0) return FALSE;
	r->pObj = pObj;
	r->pNext = pVm->pShm;
	pVm->pShm = r;
	ShmMapAttach (pObj);
	return TRUE;
}

static volatile u64 s_ulNetPC;			// the EL1 safety net caught kernel code at that PC
static volatile u64 s_ulNetVA;
static boolean s_bNetLogged;

void VmUnpinTask (CAddressSpace *pAS, CTask *pTask)
{
	if (s_ulNetPC != 0 && !s_bNetLogged)		// (logged here, out of the exception)
	{
		s_bNetLogged = TRUE;
		CLogger::Get ()->Write (From, LogWarning, "kernel touched an unpopulated user page at pc %lx (address %lx)",
					(unsigned long) s_ulNetPC, (unsigned long) s_ulNetVA);
	}
	TVmSpace *pVm = pAS != 0 ? pAS->GetVm () : 0;
	if (pVm == 0) return;
	if (pVm->nPins != 0)
	{
		for (unsigned i = 0; i < VM_PINS; i++)
		{
			if (pVm->Pin[i].pTask == pTask)
			{
				pVm->Pin[i].pTask = 0;
				pVm->nPins--;
				break;
			}
		}
	}
	if (pVm->pZapPending != 0 || HasSweep (pVm))
	{
		RunPending (pAS, pVm);
		VmShmGc (pVm);				// (v76: an object kept for a zap)
	}
}

// ---- the pages of a range ---------------------------------------------------------------------------

// The pages of [s, e) leave (an unmap, DONTNEED, a shrink): dropped now, or -- under another task's
// pin -- marked and dropped when it ends (their word waiters woken now: they return, unpin).
static void Release (CAddressSpace *pAS, TVmSpace *pVm, u64 s, u64 e, CTask *pSelf)
{
	CVmBatch Batch (pAS);
	for (u64 va = s; va < e; )
	{
		volatile u64 *p = PteOf (pAS, va);
		if (p == 0) { va = NextSlot (va); continue; }
		u64 v = *p;
		if (PTE_IS_VALID (v))
		{
			if (PinnedByOther (pVm, va, va + KPAGE_SIZE, pSelf))
			{
				*p = v | VM_PTE_SW_ZAP;
				WordWaitsZap (PTE_FRAME (v), KPAGE_SIZE);
				AddPending (pVm, va, va + KPAGE_SIZE);
			}
			else
			{
				*p = 0;
				Batch.Add (va, (v & VM_PTE_SW_OWNED) ? PTE_FRAME (v) : 0);
			}
		}
		va += KPAGE_SIZE;
	}
}

// The present pages of [s, e) given nProt. One that would lose its write access under another
// task's pin (a kapi writing there in place) keeps it until the pin ends.
static void Reprotect (CAddressSpace *pAS, TVmSpace *pVm, u64 s, u64 e, unsigned nProt, CTask *pSelf)
{
	CVmBatch Batch (pAS);
	unsigned nAP = ApForProt (nProt);
	u64 ulBits = PteProt (nProt);
	for (u64 va = s; va < e; )
	{
		volatile u64 *p = PteOf (pAS, va);
		if (p == 0) { va = NextSlot (va); continue; }
		u64 v = *p;
		if (PTE_IS_VALID (v) && ((v & PTE_PROT_MASK) != ulBits || (v & VM_PTE_SW_SYNC)))
		{
			boolean bLosesWrite = PteAP (v) == ATTRIB_AP_RW_ALL && nAP != ATTRIB_AP_RW_ALL;
			if (bLosesWrite && PinnedByOther (pVm, va, va + KPAGE_SIZE, pSelf))
			{
				*p = v | VM_PTE_SW_SYNC;
				AddPending (pVm, va, va + KPAGE_SIZE);
			}
			else
			{
				*p = (v & ~(PTE_PROT_MASK | VM_PTE_SW_SYNC)) | ulBits;
				Batch.Add (va, 0);
			}
		}
		va += KPAGE_SIZE;
	}
}

// A new region over [s, e): pages still there (left by a deferred zap) are its own now, zeroed.
static void Adopt (CAddressSpace *pAS, u64 s, u64 e, unsigned nProt)
{
	CVmBatch Batch (pAS);
	u64 ulBits = PteProt (nProt);
	for (u64 va = s; va < e; )
	{
		volatile u64 *p = PteOf (pAS, va);
		if (p == 0) { va = NextSlot (va); continue; }
		u64 v = *p;
		if (PTE_IS_VALID (v))
		{
			if (v & VM_PTE_SW_OWNED)
			{
				memset ((void *) (uintptr) PTE_FRAME (v), 0, KPAGE_SIZE);
				asm volatile ("dsb ishst" ::: "memory");
				*p = (v & ~(PTE_PROT_MASK | VM_PTE_SW_ZAP | VM_PTE_SW_SYNC)) | ulBits;
				Batch.Add (va, 0);
			}
			else
			{
				*p = 0;				// (not ours: cannot be, in a lazy range)
				Batch.Add (va, 0);
			}
		}
		va += KPAGE_SIZE;
	}
}

// Present pages in [s, e).
static unsigned CountResident (CAddressSpace *pAS, u64 s, u64 e)
{
	unsigned n = 0;
	for (u64 va = s; va < e; )
	{
		volatile u64 *p = PteOf (pAS, va);
		if (p == 0) { va = NextSlot (va); continue; }
		if (PTE_IS_VALID (*p)) n++;
		va += KPAGE_SIZE;
	}
	return n;
}

// ---- demand paging -------------------------------------------------------------------------------

unsigned VmProtAt (CAddressSpace *pAS, u64 ulVA)
{
	TVmSpace *pVm = pAS != 0 ? pAS->GetVm () : 0;
	if (pVm == 0 || !IS_USER_VA (ulVA)) return 0;
	int i = VmaFind (pVm, ulVA & ~(u64) KPAGE_MASK);
	return i >= 0 && IsLazy (pVm->pVma[i].nKind) ? pVm->pVma[i].nProt : 0;
}

int VmFaultIn (CAddressSpace *pAS, u64 ulVA, boolean bWrite)
{
	TVmSpace *pVm = pAS != 0 ? pAS->GetVm () : 0;
	if (pVm == 0 || !IS_USER_VA (ulVA)) return -KAPI_EFAULT;
	ulVA &= ~(u64) KPAGE_MASK;
	int i = VmaFind (pVm, ulVA);
	if (i < 0 || !IsLazy (pVm->pVma[i].nKind)) return -KAPI_EFAULT;
	unsigned nProt = pVm->pVma[i].nProt;
	if (   (nProt & (KAPI_PROT_READ | KAPI_PROT_WRITE)) == 0
	    || (bWrite && !(nProt & KAPI_PROT_WRITE)))
	{
		return -KAPI_EACCES;
	}
	volatile u64 *p = PteOf (pAS, ulVA);
	if (p != 0 && PTE_IS_VALID (*p))
	{
		return ApAllows (PteAP (*p), bWrite) ? 0 : -KAPI_EACCES;	// (another thread filled it)
	}
	if (pVm->pVma[i].nKind == KAPI_VMK_SHM)			// (v76) the object's frame
	{
		const TVma &V = pVm->pVma[i];
		u64 ulPhys;
		int r = ShmFrame (V.pObj, (ulVA - V.ulStart + V.ulObjOff) / KPAGE_SIZE, &ulPhys);
		if (r < 0) return r;					// (-EFAULT: beyond its size)
		TKPageAttr Attr = KPAGE_ATTR_APP_DATA;
		Attr.AP = ApForProt (nProt);
		if (!pAS->MapPage (ulVA, ulPhys, Attr, FALSE)) return -KAPI_ENOMEM;	// (not owned)
		pVm->nFaults++;
		return 1;
	}
	if (VmPoolFree () < VM_RESERVE + 2 * KPAGE_SIZE)			// (the page, maybe an L3)
	{
		return -KAPI_ENOMEM;
	}
	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;
	Attr.AP = ApForProt (nProt);
	Attr.UXN = (nProt & KAPI_PROT_EXEC) ? 0 : 1;	// (v78: a JIT's code)
	if (pAS->MapNewPage (ulVA, Attr) == 0)		// zeroed, DSB before and after its PTE
	{
		return -KAPI_ENOMEM;
	}
	pVm->nFaults++;
	return 1;
}

int VmPopulate (CAddressSpace *pAS, u64 s, u64 e, boolean bYield)
{
	s &= ~(u64) KPAGE_MASK;
	unsigned nFilled = 0;
	for (u64 va = s; va < e; )
	{
		TVmSpace *pVm = pAS->GetVm ();
		if (pVm == 0) return 0;
		unsigned i = VmaLowerBound (pVm, va);
		if (i >= pVm->nVma || pVm->pVma[i].ulStart >= e) break;
		TVma V = pVm->pVma[i];				// (a copy: a yield may change the list)
		if (va < V.ulStart) va = V.ulStart;
		if (!IsLazy (V.nKind) || (V.nProt & (KAPI_PROT_READ | KAPI_PROT_WRITE)) == 0)
		{
			va = V.ulEnd;
			continue;
		}
		u64 ulEnd = Min64 (V.ulEnd, e);
		while (va < ulEnd)
		{
			int r = VmFaultIn (pAS, va, FALSE);
			if (r == -KAPI_ENOMEM) return r;
			va += KPAGE_SIZE;
			if (r > 0 && bYield && ++nFilled % VM_POPULATE_BATCH == 0)
			{
				CScheduler::Get ()->Yield ();	// (then the regions looked up again)
				break;
			}
		}
	}
	return 0;
}

const char *VmFaultWhy (CAddressSpace *pAS, u64 ulFAR)
{
	TVmSpace *pVm = pAS != 0 ? pAS->GetVm () : 0;
	if (pVm == 0) return 0;
	int i = VmaFind (pVm, ulFAR);
	if (i >= 0)
	{
		const TVma &V = pVm->pVma[i];
		if (   V.nKind == KAPI_VMK_SHM && V.nProt != KAPI_PROT_NONE
		    && ulFAR - V.ulStart + V.ulObjOff >= ShmSize (V.pObj))
		{
			return "beyond the shared object";
		}
		return V.nProt == KAPI_PROT_NONE ? "PROT_NONE access" : 0;
	}
	unsigned k = VmaLowerBound (pVm, ulFAR);
	if (   k < pVm->nVma && pVm->pVma[k].nKind == KAPI_VMK_STACK
	    && pVm->pVma[k].ulStart - ulFAR <= VM_STACK_GUARD_HINT)
	{
		return "stack overflow";
	}
	return 0;
}

#define EC_DABORT_SAME		0x25
#define ESR_FNV			(1ULL << 10)
#define ESR_WNR			(1ULL << 6)

boolean VmKernelFault (TTrapFrame *pFrame, u64 ulESR, u64 ulFAR)
{
	if (   ((ulESR >> 26) & 0x3F) != EC_DABORT_SAME
	    || (ulESR & ESR_FNV) != 0
	    || (ulESR & 0x3C) != 0x04			// a translation fault (levels 0..3)
	    || !IS_USER_VA (ulFAR)
	    || !CScheduler::IsActive ()
	    || CScheduler::ThisCore () != 0)
	{
		return FALSE;
	}
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0) return FALSE;
	asm volatile ("msr daifclr, #1" ::: "memory");	// (the allocator's lock wants the FIQ on)
	int r = VmFaultIn (pAS, ulFAR, (ulESR & ESR_WNR) != 0);
	asm volatile ("msr daifset, #1" ::: "memory");
	if (r < 0) return FALSE;
	if (s_ulNetPC == 0)
	{
		s_ulNetVA = ulFAR;
		s_ulNetPC = pFrame->elr_el1;		// (logged by the next VmUnpinTask)
	}
	return TRUE;
}

// ---- the regions the kernel makes ----------------------------------------------------------------

void VmNoteRegion (CAddressSpace *pAS, u64 s, u64 e, unsigned nProt, unsigned nKind)
{
	TVmSpace *pVm = VmOf (pAS, TRUE);
	s = KPAGE_ALIGN_DOWN (s);
	e = KPAGE_ALIGN_UP (e);
	if (pVm == 0 || s >= e) return;
	for (unsigned i = VmaLowerBound (pVm, s); i < pVm->nVma && pVm->pVma[i].ulStart < e; i++)
	{
		if (IsLazy (pVm->pVma[i].nKind)) return;	// (cannot be: never over a lazy one)
	}
	if (!VmaRoom (pVm, 2) || !VmaCut (pVm, s, e)) return;
	VmaInsert (pVm, s, e, nProt, nKind);
	VmaMerge (pVm, s, e);
}

int VmMapStack (CAddressSpace *pAS, u64 ulTop, u64 nSize, u64 nSlot)
{
	TVmSpace *pVm = VmOf (pAS, TRUE);
	if (pVm == 0) return -KAPI_ENOMEM;
	if (nSlot < nSize) nSlot = nSize;
	u64 s = KPAGE_ALIGN_DOWN (ulTop - nSize), ulSlot = ulTop - nSlot;
	int i = VmaFind (pVm, s);
	if (   i >= 0 && pVm->pVma[i].nKind == KAPI_VMK_STACK
	    && pVm->pVma[i].ulStart == s && pVm->pVma[i].ulEnd == ulTop)
	{
		return 0;					// (a slot reused at its size: kept)
	}
	for (unsigned k = VmaLowerBound (pVm, ulSlot); k < pVm->nVma && pVm->pVma[k].ulStart < ulTop; k++)
	{
		if (pVm->pVma[k].nKind != KAPI_VMK_STACK) return -KAPI_EINVAL;
	}
	if (!VmaRoom (pVm, 2)) return -KAPI_ENOMEM;
	VmaCut (pVm, ulSlot, ulTop);
	Release (pAS, pVm, ulSlot, ulTop, CurrentTask ());
	VmaInsert (pVm, s, ulTop, KAPI_PROT_READ | KAPI_PROT_WRITE, KAPI_VMK_STACK);
	Adopt (pAS, s, ulTop, KAPI_PROT_READ | KAPI_PROT_WRITE);
	return 0;
}

void VmDiscard (CAddressSpace *pAS, u64 s, u64 e)
{
	TVmSpace *pVm = pAS != 0 ? pAS->GetVm () : 0;
	if (pVm != 0 && s < e) Release (pAS, pVm, s, e, CurrentTask ());
}

int VmHeapResize (CAddressSpace *pAS, u64 ulOld, u64 ulNew)
{
	TVmSpace *pVm = VmOf (pAS, TRUE);
	if (pVm == 0) return -KAPI_ENOMEM;
	if (ulNew < ulOld)
	{
		VmaCut (pVm, ulNew, ulOld);			// (its tail: no split)
		Release (pAS, pVm, ulNew, ulOld, CurrentTask ());
		return 0;
	}
	if (ulNew == ulOld) return 0;
	if (!VmCommitOK (ulNew - ulOld) || VmaOverlaps (pVm, ulOld, ulNew) || !VmaRoom (pVm, 1))
	{
		return -KAPI_ENOMEM;
	}
	VmaInsert (pVm, ulOld, ulNew, KAPI_PROT_READ | KAPI_PROT_WRITE, KAPI_VMK_HEAP);
	VmaMerge (pVm, ulOld, ulNew);			// (one heap region)
	Adopt (pAS, ulOld, ulNew, KAPI_PROT_READ | KAPI_PROT_WRITE);
	if (pVm->bEagerHeap && VmPopulate (pAS, ulOld, ulNew, FALSE) != 0)
	{
		VmaCut (pVm, ulOld, ulNew);			// (as before: out of memory, nothing grown)
		Release (pAS, pVm, ulOld, ulNew, CurrentTask ());
		return -KAPI_ENOMEM;
	}
	return 0;
}

void VmEagerHeap (CAddressSpace *pAS)
{
	TVmSpace *pVm = VmOf (pAS, TRUE);
	if (pVm == 0 || pVm->bEagerHeap) return;
	pVm->bEagerHeap = TRUE;
	int i = VmaFind (pVm, USER_HEAP_BASE);
	if (i >= 0 && pVm->pVma[i].nKind == KAPI_VMK_HEAP)
	{
		VmPopulate (pAS, pVm->pVma[i].ulStart, pVm->pVma[i].ulEnd, TRUE);
	}
}

boolean VmRegionAt (CAddressSpace *pAS, u64 ulVA, TVma *pOut)
{
	TVmSpace *pVm = pAS != 0 ? pAS->GetVm () : 0;
	int i = pVm != 0 ? VmaFind (pVm, ulVA) : -1;
	if (i < 0) return FALSE;
	*pOut = pVm->pVma[i];
	return TRUE;
}

u64 VmRegionEndBelow (CAddressSpace *pAS, u64 ulVA)
{
	TVmSpace *pVm = pAS != 0 ? pAS->GetVm () : 0;
	if (pVm == 0) return USER_VA_BASE;
	unsigned i = VmaLowerBound (pVm, ulVA);		// (the first ending above ulVA)
	while (i > 0 && pVm->pVma[i - 1].ulEnd > ulVA) i--;
	return i > 0 ? pVm->pVma[i - 1].ulEnd : USER_VA_BASE;
}

// ---- the kapis ---------------------------------------------------------------------------------

#define KIND_BIT(k)		(1u << (k))
#define KINDS_LAZY		(KIND_BIT (KAPI_VMK_ANON) | KIND_BIT (KAPI_VMK_HEAP) | KIND_BIT (KAPI_VMK_STACK) \
				 | KIND_BIT (KAPI_VMK_SHM))
#define KINDS_ARENA		(KIND_BIT (KAPI_VMK_ANON) | KIND_BIT (KAPI_VMK_SHM))
#define PROT_ALL		(KAPI_PROT_READ | KAPI_PROT_WRITE | KAPI_PROT_EXEC)

// [addr, addr + len) rounded to pages; FALSE: unaligned, empty or wrapping.
static boolean PageRange (u64 ulAddr, u64 ulLen, u64 *pEnd)
{
	if ((ulAddr & KPAGE_MASK) != 0 || ulLen == 0 || ulLen > USER_VA_END) return FALSE;
	u64 ulEnd = ulAddr + ((ulLen + KPAGE_MASK) & ~(u64) KPAGE_MASK);
	if (ulEnd <= ulAddr) return FALSE;
	*pEnd = ulEnd;
	return TRUE;
}

static inline boolean InArena (u64 s, u64 e)
{
	return s >= USER_MMAP_BASE && e <= USER_MMAP_END && s < e;
}

extern "C" {

long long kapi_vm_map (unsigned long long ulAddr, unsigned long long ulLen, unsigned nProt, unsigned nFlags)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0 || ulLen == 0 || (nProt & ~PROT_ALL) != 0) return -KAPI_EINVAL;
	if (ulLen > USER_MMAP_END - USER_MMAP_BASE) return -KAPI_ENOMEM;
	u64 nLen = (ulLen + KPAGE_MASK) & ~(u64) KPAGE_MASK;
	TVmSpace *pVm = VmOf (pAS, TRUE);
	if (pVm == 0) return -KAPI_ENOMEM;
	if ((nProt & KAPI_PROT_WRITE) && !(nFlags & KAPI_MAP_NORESERVE) && !VmCommitOK (nLen))
	{
		return -KAPI_ENOMEM;			// (the heuristic overcommit)
	}
	if (!VmaRoom (pVm, 2)) return -KAPI_ENOMEM;	// (a FIXED cut may split one, the insert)

	u64 s;
	if (nFlags & (KAPI_MAP_FIXED | KAPI_MAP_FIXED_NOREPLACE))
	{
		s = ulAddr;
		if ((s & KPAGE_MASK) != 0 || !InArena (s, s + nLen) || s + nLen < s) return -KAPI_EINVAL;
		if (VmaOverlaps (pVm, s, s + nLen))
		{
			if (!(nFlags & KAPI_MAP_FIXED)) return -KAPI_EEXIST;	// (NOREPLACE alone)
			if (nFlags & KAPI_MAP_FIXED_NOREPLACE) return -KAPI_EEXIST;
			VmaCut (pVm, s, s + nLen);		// (only ANON / SHM regions in the arena)
			Release (pAS, pVm, s, s + nLen, CurrentTask ());
			VmShmGc (pVm);
		}
	}
	else
	{
		s = ulAddr;
		if (   (s & KPAGE_MASK) != 0 || s + nLen < s || !InArena (s, s + nLen)
		    || VmaOverlaps (pVm, s, s + nLen))
		{
			s = VmaFindGap (pVm, nLen, USER_MMAP_BASE, USER_MMAP_END);
			if (s == 0) return -KAPI_ENOMEM;
		}
	}
	VmaInsert (pVm, s, s + nLen, nProt, KAPI_VMK_ANON);
	Adopt (pAS, s, s + nLen, nProt);
	VmaMerge (pVm, s, s + nLen);
	if (nFlags & KAPI_MAP_POPULATE)
	{
		VmPopulate (pAS, s, s + nLen, TRUE);		// (as Linux: a failure is not reported)
	}
	return (long long) s;
}

// (v76) [off, off + len) of shared object h mapped MAP_SHARED: an SHM region (lazy: its pages are the
// object's frames, filled at the first touch). Placed as vm_map places an anonymous mapping.
long long kapi_shm_map (long long h, unsigned long long ulAddr, unsigned long long ulLen, unsigned nProt,
			unsigned nFlags, unsigned long long ulOff)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0 || ulLen == 0 || (nProt & ~PROT_ALL) != 0) return -KAPI_EINVAL;
	if (nFlags & ~(KAPI_MAP_FIXED | KAPI_MAP_FIXED_NOREPLACE | KAPI_MAP_POPULATE | KAPI_MAP_NORESERVE)) return -KAPI_EINVAL;
	if (nProt & KAPI_PROT_EXEC) return -KAPI_ENOTSUP;
	if ((ulOff & KPAGE_MASK) != 0 || ulOff > IPC_SHM_MAX) return -KAPI_EINVAL;
	if (ulLen > USER_MMAP_END - USER_MMAP_BASE) return -KAPI_ENOMEM;
	CHandleTable *pTable = HandlesCurrent ();
	unsigned nAccess = 0;
	void *pObj = pTable != 0 && h > 0 && h <= 0xFFFFFF
		   ? pTable->Get ((void *) (uintptr) (unsigned) h, HANDLE_SHM, &nAccess) : 0;
	if (!ShmIs (pObj)) return -KAPI_EBADF;
	if (nProt & KAPI_PROT_WRITE)
	{
		if (nAccess != KAPI_O_RDWR) return -KAPI_EACCES;
		if (ShmSeals (pObj) & KAPI_SEAL_WRITE) return -KAPI_EPERM;
	}
	u64 nLen = (ulLen + KPAGE_MASK) & ~(u64) KPAGE_MASK;
	TVmSpace *pVm = VmOf (pAS, TRUE);
	if (pVm == 0 || !VmaRoom (pVm, 2)) return -KAPI_ENOMEM;
	CTask *pSelf = CurrentTask ();

	u64 s;
	boolean bCut = FALSE;
	if (nFlags & (KAPI_MAP_FIXED | KAPI_MAP_FIXED_NOREPLACE))
	{
		s = ulAddr;
		if ((s & KPAGE_MASK) != 0 || s + nLen < s || !InArena (s, s + nLen)) return -KAPI_EINVAL;
		if (VmaOverlaps (pVm, s, s + nLen) && !(nFlags & KAPI_MAP_FIXED)) return -KAPI_EEXIST;
		if (VmaOverlaps (pVm, s, s + nLen) && (nFlags & KAPI_MAP_FIXED_NOREPLACE)) return -KAPI_EEXIST;
		if (PinnedByOther (pVm, s, s + nLen, pSelf)) return -KAPI_EBUSY;	// (a kapi works there)
		bCut = VmaOverlaps (pVm, s, s + nLen);
	}
	else
	{
		s = ulAddr;
		if (   (s & KPAGE_MASK) != 0 || s + nLen < s || !InArena (s, s + nLen)
		    || VmaOverlaps (pVm, s, s + nLen) || PinnedByOther (pVm, s, s + nLen, pSelf))
		{
			u64 lo = USER_MMAP_BASE;
			for (unsigned nTry = 0; ; nTry++)	// (a gap still zapped under a pin: the next)
			{
				s = VmaFindGap (pVm, nLen, lo, USER_MMAP_END);
				if (s == 0 || nTry >= 16) return -KAPI_ENOMEM;
				if (!PinnedByOther (pVm, s, s + nLen, pSelf)) break;
				lo = s + nLen;
			}
		}
	}
	if (!VmShmAttach (pVm, pObj)) return -KAPI_ENOMEM;
	if (bCut)
	{
		VmaCut (pVm, s, s + nLen);
		Release (pAS, pVm, s, s + nLen, pSelf);
	}
	VmaInsertShm (pVm, s, s + nLen, nProt, pObj, ulOff, nAccess != KAPI_O_RDWR ? VMA_F_SHM_RDONLY : 0);
	VmaMerge (pVm, s, s + nLen);
	VmShmGc (pVm);					// (what the cut let go)
	if (nFlags & KAPI_MAP_POPULATE)
	{
		VmPopulate (pAS, s, s + nLen, TRUE);		// (as vm_map: a failure is not reported)
	}
	return (long long) s;
}

int kapi_vm_unmap (unsigned long long ulAddr, unsigned long long ulLen)
{
	CAddressSpace *pAS = CurrentAS ();
	TVmSpace *pVm = VmOf (pAS, TRUE);
	u64 e;
	if (pVm == 0 || !PageRange (ulAddr, ulLen, &e) || !InArena (ulAddr, e)) return -KAPI_EINVAL;
	if (VmaCutSplits (pVm, ulAddr, e) && !VmaRoom (pVm, 1)) return -KAPI_ENOMEM;
	VmaCut (pVm, ulAddr, e);				// (holes allowed, as munmap)
	Release (pAS, pVm, ulAddr, e, CurrentTask ());
	VmShmGc (pVm);
	return 0;
}

int kapi_vm_protect (unsigned long long ulAddr, unsigned long long ulLen, unsigned nProt)
{
	CAddressSpace *pAS = CurrentAS ();
	TVmSpace *pVm = VmOf (pAS, TRUE);
	u64 e;
	if (pVm == 0 || (nProt & ~PROT_ALL) != 0 || !PageRange (ulAddr, ulLen, &e)) return -KAPI_EINVAL;
	if (!VmaCovered (pVm, ulAddr, e, KINDS_ARENA)) return -KAPI_EINVAL;
	if ((nProt & KAPI_PROT_EXEC) && !VmaCovered (pVm, ulAddr, e, KIND_BIT (KAPI_VMK_ANON)))
	{
		return -KAPI_ENOTSUP;				// (v78: a shared object's pages never run)
	}
	if (nProt & KAPI_PROT_WRITE)				// (v76: a shared object's write rights)
	{
		for (unsigned i = VmaLowerBound (pVm, ulAddr); i < pVm->nVma && pVm->pVma[i].ulStart < e; i++)
		{
			const TVma &V = pVm->pVma[i];
			if (V.nKind != KAPI_VMK_SHM) continue;
			if (V.nFlags & VMA_F_SHM_RDONLY) return -KAPI_EACCES;
			if (ShmSeals (V.pObj) & KAPI_SEAL_WRITE) return -KAPI_EPERM;
		}
	}
	if (!VmaRoom (pVm, 2)) return -KAPI_ENOMEM;		// (the split cap)
	VmaSplitAt (pVm, ulAddr);
	VmaSplitAt (pVm, e);
	for (unsigned i = VmaLowerBound (pVm, ulAddr); i < pVm->nVma && pVm->pVma[i].ulStart < e; i++)
	{
		pVm->pVma[i].nProt = (u16) nProt;
	}
	Reprotect (pAS, pVm, ulAddr, e, nProt, CurrentTask ());
	VmaMerge (pVm, ulAddr, e);
	return 0;
}

int kapi_vm_advise (unsigned long long ulAddr, unsigned long long ulLen, int nAdvice)
{
	CAddressSpace *pAS = CurrentAS ();
	TVmSpace *pVm = VmOf (pAS, TRUE);
	if (pVm == 0 || (ulAddr & KPAGE_MASK) != 0) return -KAPI_EINVAL;
	if (ulLen == 0) return 0;
	u64 e;
	if (!PageRange (ulAddr, ulLen, &e) || !VmaCovered (pVm, ulAddr, e, KINDS_LAZY)) return -KAPI_EINVAL;
	switch (nAdvice)
	{
	case KAPI_MADV_NORMAL:
	case KAPI_MADV_RANDOM:
	case KAPI_MADV_SEQUENTIAL:
		return 0;

	case KAPI_MADV_WILLNEED:
		return VmPopulate (pAS, ulAddr, e, TRUE);

	case KAPI_MADV_DONTNEED:
	case KAPI_MADV_FREE:
		if (!VmaCovered (pVm, ulAddr, e, KIND_BIT (KAPI_VMK_ANON) | KIND_BIT (KAPI_VMK_HEAP) | KIND_BIT (KAPI_VMK_SHM)))
		{
			return -KAPI_EINVAL;
		}
		Release (pAS, pVm, ulAddr, e, CurrentTask ());	// (zeros on the next touch; SHM: the object's data kept)
		return 0;

	default:
		return -KAPI_EINVAL;
	}
}

int kapi_vm_query (unsigned long long ulAddr, struct kapi_vm_region *pOut)
{
	CAddressSpace *pAS = CurrentAS ();
	TVmSpace *pVm = VmOf (pAS, FALSE);
	if (pVm == 0) return -KAPI_ENOMEM;
	unsigned i = VmaLowerBound (pVm, ulAddr);
	if (i >= pVm->nVma) return -KAPI_ENOMEM;
	const TVma &V = pVm->pVma[i];
	struct kapi_vm_region Out;
	memset (&Out, 0, sizeof Out);
	Out.start = V.ulStart;
	Out.end = V.ulEnd;
	Out.prot = V.nProt;
	Out.kind = V.nKind;
	Out.resident = CountResident (pAS, V.ulStart, V.ulEnd);
	Out.flags = IsLazy (V.nKind) ? KAPI_VMF_LAZY : 0;
	int r = V.ulStart <= ulAddr ? 0 : 1;
	return UserPut (pOut, Out) ? r : -KAPI_EFAULT;
}

struct TFindVmPid
{
	unsigned       nPid;
	CAddressSpace *pFound;
};

static boolean FindVmPid (CTask *pTask, const char *pName, TTaskState State, TTaskFlags Flags, void *pParam)
{
	(void) pName; (void) Flags;
	TFindVmPid *pFind = (TFindVmPid *) pParam;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (State != TaskStateTerminated && pAS != 0 && pAS->GetPid () == pFind->nPid)
	{
		pFind->pFound = pAS;
		return FALSE;
	}
	return TRUE;
}

int kapi_vm_stats (int nPid, struct kapi_vm_stats *pOut)
{
	CAddressSpace *pAS = 0;
	if (nPid == 0)
	{
		pAS = CurrentAS ();
	}
	else if (nPid > 0 && CScheduler::IsActive ())
	{
		TFindVmPid Find = { (unsigned) nPid, 0 };
		CScheduler::Get ()->EnumerateTasks (FindVmPid, &Find);
		pAS = Find.pFound;
	}
	if (pAS == 0) return -KAPI_ESRCH;

	// Made here, copied out whole; no Yield in between: the space stays.
	struct kapi_vm_stats Out;
	memset (&Out, 0, sizeof Out);
	Out.resident = (unsigned long long) pAS->GetPages () * KPAGE_SIZE;
	Out.pt_bytes = (unsigned long long) pAS->GetTablePages () * KPAGE_SIZE;
	TVmSpace *pVm = pAS->GetVm ();
	if (pVm != 0)
	{
		for (unsigned i = 0; i < pVm->nVma; i++)
		{
			const TVma &V = pVm->pVma[i];
			if (IsLazy (V.nKind)) Out.lazy += V.ulEnd - V.ulStart;
			if (V.nProt & KAPI_PROT_WRITE) Out.writable += V.ulEnd - V.ulStart;
		}
		Out.faults = pVm->nFaults;
	}
	Out.limit = 0;
	return UserPut (pOut, Out) ? 0 : -KAPI_EFAULT;
}

}
