//
// addrspace.cpp
//
#include <kern/addrspace.h>
#include <kern/appcore.h>
#include <kern/gui/window.h>		// CWindow + CWindowManager (process window)
#include <kern/gui/surface.h>		// CSurfaceManager (free a dead owner's surfaces)
#include <kern/ipc.h>			// CMailbox + IpcOnProcessGone (activity-shell IPC)
#include <kern/stream.h>		// CStream + CProcess (stdio teardown)
#include <kern/kapi_abi.h>		// KAPI_TABLE_VA (fixed VA of the app's kapi table)
#include <kern/applaunch.h>		// g_bVerbose (gated lifecycle logging)
#include <kern/net.h>			// NetCloseByPid (reclaim a dead process's sockets)
#include <kern/v3d.h>			// V3DReleaseAS (free a dead process's GPU textures)
#include <kern/thread.h>		// ThreadsFree (its threads' objects)
#include <kern/el0.h>			// the EL0 table + code pages, KAPI_STUBS_VA
#include <circle/logger.h>		// CLogger (verbose exit log)
#include <circle/sched/task.h>		// CTask, TASK_USER_DATA_USER, GetUserData
#include <circle/alloc.h>		// palloc / pfree (64 KB pages)
#include <circle/synchronize.h>		// DataSyncBarrier
#include <circle/util.h>		// memcpy / memset
#include <assert.h>

// Kernel TTBR0 base (L2 table, ASID 0), captured once by AddrSpaceInit().
static u64 s_ulKernelTTBR0 = 0;

// Software bit (in an L3 page descriptor's Ignored field) flagging a frame that
// this address space palloc'd and must pfree on teardown.
#define PAGE_SW_OWNED		1

// ---- ASID allocation (8-bit; ASID 0 reserved for the kernel/global pages) ----

static boolean s_bASIDUsed[ASID_COUNT];

static u8 AllocASID (void)
{
	for (unsigned i = ASID_USER_FIRST; i <= ASID_USER_LAST; i++)
	{
		if (!s_bASIDUsed[i])
		{
			s_bASIDUsed[i] = TRUE;
			return (u8) i;
		}
	}

	return 0;		// exhausted (should not happen for our small task counts)
}

static void FreeASID (u8 nASID)
{
	if (nASID != ASID_KERNEL)
	{
		s_bASIDUsed[nASID] = FALSE;
	}
}

// ---- CAddressSpace -----------------------------------------------------------

// Monotonic process-id source (1..). 0 means "kernel task" (no address space).
static unsigned s_nNextPid = 1;

// Total 64 KB frames owned by all user address spaces (see kern/addrspace.h).
unsigned g_nUserPages = 0;

CAddressSpace::CAddressSpace (void)
:	m_pL2 (0),
	m_nASID (0),
	m_nPid (s_nNextPid++),
	m_nParentPid (0),
	m_pWindow (0),
	m_pStdin (0),
	m_pStdout (0),
	m_pMailbox (0),
	m_pProcess (0),
	m_nExitStatus (0),
	m_nOwnedPages (0),
	m_ulHeapBrk (USER_HEAP_BASE),
	m_ulHeapEnd (USER_HEAP_BASE),
	m_ulSurfaceNext (USER_SURFACE_BASE),
	m_ulCodeNext (USER_CODE_BASE),
	m_pMainTask (0),
	m_nTasks (0),
	m_pThreads (0)
{
	m_Args[0] = '\0';
	memset (&m_Syscalls, 0, sizeof m_Syscalls);
	m_Cwd[0] = 'S'; m_Cwd[1] = 'D'; m_Cwd[2] = ':'; m_Cwd[3] = '/'; m_Cwd[4] = '\0';	// root

	assert (s_ulKernelTTBR0 != 0);		// AddrSpaceInit() must run first

	m_pL2 = (TARMV8MMU_LEVEL2_DESCRIPTOR *) palloc ();
	if (m_pL2 == 0)
	{
		return;
	}
	m_nOwnedPages++; g_nUserPages++;	// the L2 table

	// Share all kernel mappings: copy the kernel L2 table verbatim. Its valid
	// entries point at the kernel's L3 tables (global, identity, EL1-only) and are
	// now shared; the user-range entries it contains are invalid, ready for us.
	memcpy (m_pL2, (const void *) (s_ulKernelTTBR0 & ~(0xFFFFULL << TTBR0_ASID_SHIFT)),
		KPAGE_SIZE);

	m_nASID = AllocASID ();

	// The app's view of the kernel (kern/el0.h): the EL0 kapi table, read-only at the fixed
	// KAPI_TABLE_VA (its entries point at the EL0 code, never at the kernel), and the EL0 code
	// page (the system-call stubs, the user-side routines) next to it, executable at EL0. Both
	// are kernel globals shared by every process: mapped not-owned, never freed here.
	TKPageAttr TableAttr = KPAGE_ATTR_APP_RODATA;
	TKPageAttr CodeAttr = KPAGE_ATTR_APP_CODE;
	MapContig (KAPI_TABLE_VA, El0TablePhys (), 1, TableAttr);
	MapContig (KAPI_STUBS_VA, El0CodePhys (), 1, CodeAttr);

	DataSyncBarrier ();
}

void CAddressSpace::SetArgs (const char *pArgs)
{
	unsigned i = 0;
	if (pArgs != 0)
	{
		for (; pArgs[i] != '\0' && i < sizeof (m_Args) - 1; i++)
		{
			m_Args[i] = pArgs[i];
		}
	}
	m_Args[i] = '\0';
}

void CAddressSpace::SetCwd (const char *pCwd)
{
	unsigned i = 0;
	if (pCwd != 0)
	{
		for (; pCwd[i] != '\0' && i < sizeof (m_Cwd) - 1; i++)
		{
			m_Cwd[i] = pCwd[i];
		}
	}
	m_Cwd[i] = '\0';
	if (m_Cwd[0] == '\0')		// never empty: default to root
	{
		m_Cwd[0] = 'S'; m_Cwd[1] = 'D'; m_Cwd[2] = ':'; m_Cwd[3] = '/'; m_Cwd[4] = '\0';
	}
}

CAddressSpace::~CAddressSpace (void)
{
	// Its threads' objects first (their waiters -- killed tasks -- unlinked), and the
	// window's wake-up pointer to them cleared.
	ThreadsFree (this);

	// stdio teardown: signal EOF to whoever reads our stdout, drop our stream refs,
	// and mark the spawn record done (so a waiter unblocks). Done first so the
	// terminal sees the child finish promptly. A stream's LAST ref is released by the
	// reaper task a moment later (kern/handle.h): a file stream's last release writes
	// to the card, not to be done here (the reaper's teardown runs with IRQs masked).
	if (m_pStdout != 0)
	{
		m_pStdout->CloseWrite ();
		HandlesDeferRelease (m_pStdout);
		m_pStdout = 0;
	}
	if (m_pStdin != 0)
	{
		HandlesDeferRelease (m_pStdin);
		m_pStdin = 0;
	}
	if (m_pProcess != 0)
	{
		m_pProcess->nStatus = m_nExitStatus;
		m_pProcess->bDone = TRUE;
		ProcessRelease (m_pProcess);		// (the spawner's handle keeps it, if any)
		m_pProcess = 0;
	}

	// Its handles still open (kern/handle.h): files and directories closed, streams and
	// spawn records released -- they leaked before when an app was killed or forgot them.
	m_Handles.CloseAll (TRUE);
	if (m_pMailbox != 0)
	{
		delete m_pMailbox;
		m_pMailbox = 0;
	}

	// Its app cores (kern/appcore.h) first: a job still running there uses this space.
	// A core that did not stop (its code masked the interrupts) may still touch the
	// window and the frames: then leak them all rather than free memory in use.
	if (!AppCoreReleaseAS (this))
	{
		CLogger::Get ()->Write ("proc", LogError, "pid %u: an app core did not stop, memory kept", m_nPid);
		m_pWindow = 0;
		return;
	}

	V3DReleaseAS (this);			// its GPU textures (no frame of it is in flight now)

	// This runs in the janitor/reaper context (ReapTerminatedTasks), not inside the
	// scheduler core: IRQs are enabled and the task is already quiescent, so it is
	// safe to do the full teardown (free frames, free the window, TLB ops).
	//
	// The window was already removed from the compositor at kapi_exit (in the app's
	// own context, >=100 ms ago), so no in-flight composite references it; we can
	// now free the CWindow + its canvas. Remove() again is a harmless no-op.
	if (m_pWindow != 0)
	{
		if (CWindowManager::Get () != 0)
		{
			CWindowManager::Get ()->Remove (m_pWindow);
		}
		delete m_pWindow;		// frees the canvas buffer too (~CWindow)
		m_pWindow = 0;
	}

	if (m_pL2 == 0)
	{
		return;
	}

	// Invalidate the TLB + walk-cache for this ASID BEFORE freeing its page tables.
	// These pages were LIVE page tables, so the CPU cached their translation walks;
	// freeing and recycling them while a cached walk still references them breaks
	// later translations -- a fresh, never-walked page frees fine, which was the
	// tell. So invalidate first, while the tables are still intact (the old code
	// did this AFTER the frees -- too late).
	u64 ulArg = (u64) m_nASID << TTBR0_ASID_SHIFT;
	asm volatile ("tlbi aside1is, %0; dsb ish; isb" :: "r" (ulArg) : "memory");

	// Kernel L2 (this AS was built by memcpy-ing it): used to detect L3 tables that
	// are SHARED with the kernel (same descriptor) so we never free a kernel table.
	const TARMV8MMU_LEVEL2_DESCRIPTOR *pKernelL2 =
		(const TARMV8MMU_LEVEL2_DESCRIPTOR *) s_ulKernelTTBR0;

	// Now free every frame we own (palloc'd via MapNewPage), then each L3 table,
	// then the L2. Frames not flagged PAGE_SW_OWNED are owned elsewhere (the window
	// canvas) and must NOT be freed here.
	unsigned nFirst = L2_INDEX (USER_VA_BASE);
	unsigned nLast  = L2_INDEX (USER_VA_END - 1);
	for (unsigned i = nFirst; i <= nLast; i++)
	{
		if (m_pL2[i].Table.Value11 != 3)
		{
			continue;
		}

		// Skip L3 tables shared with the kernel (copied from the kernel L2 at
		// construction): freeing them would corrupt the kernel's page tables and
		// break translation for everyone -- including the IRQ page walk.
		if (pKernelL2[i].Table.Value11 == 3
		    && (u64) pKernelL2[i].Table.TableAddress
			 == (u64) m_pL2[i].Table.TableAddress)
		{
			continue;
		}

		TARMV8MMU_LEVEL3_DESCRIPTOR *pL3 = (TARMV8MMU_LEVEL3_DESCRIPTOR *)
			ARMV8MMUL2TABLEPTR ((u64) m_pL2[i].Table.TableAddress);

		for (unsigned j = 0; j < L3_ENTRIES; j++)
		{
			TARMV8MMU_LEVEL3_PAGE_DESCRIPTOR *pPage = &pL3[j].Page;
			if (pPage->Value11 == 3 && (pPage->Ignored & PAGE_SW_OWNED))
			{
				u64 ulFrame = (u64) ARMV8MMUL3PAGEPTR ((u64) pPage->OutputAddress);
				// Never free the shared EL0 table and code pages: kernel globals
				// aliased into every address space (mapped not-owned, so this should
				// already be skipped -- but guard explicitly: freeing them would
				// corrupt every app's view of the kernel).
				if (ulFrame == El0TablePhys () || ulFrame == El0CodePhys ())
				{
					continue;
				}
				pfree ((void *) ulFrame);
			}
		}

		// Free this AS's private L3 (the table page it pointed to is NOT freed: it is
		// either a kernel-shared frame, the window canvas, or the kapi table -- all
		// owned elsewhere).
		pfree (pL3);
	}

	pfree (m_pL2);
	m_pL2 = 0;

	if (g_nUserPages >= m_nOwnedPages) g_nUserPages -= m_nOwnedPages;
	m_nOwnedPages = 0;

	FreeASID (m_nASID);
}

TARMV8MMU_LEVEL3_DESCRIPTOR *CAddressSpace::GetOrCreateL3 (unsigned nL2Index)
{
	TARMV8MMU_LEVEL2_TABLE_DESCRIPTOR *pDesc = &m_pL2[nL2Index].Table;

	if (pDesc->Value11 == 3)		// already a table descriptor
	{
		return (TARMV8MMU_LEVEL3_DESCRIPTOR *)
			ARMV8MMUL2TABLEPTR ((u64) pDesc->TableAddress);
	}

	TARMV8MMU_LEVEL3_DESCRIPTOR *pL3 = (TARMV8MMU_LEVEL3_DESCRIPTOR *) palloc ();
	if (pL3 == 0)
	{
		return 0;
	}
	m_nOwnedPages++; g_nUserPages++;	// an L3 table
	memset (pL3, 0, KPAGE_SIZE);

	pDesc->Value11	    = 3;
	pDesc->Ignored1	    = 0;
	pDesc->TableAddress = ARMV8MMUL2TABLEADDR ((u64) pL3);
	pDesc->Reserved0    = 0;
	pDesc->Ignored2	    = 0;
	pDesc->PXNTable	    = 0;
	pDesc->UXNTable	    = 0;
	pDesc->APTable	    = AP_TABLE_ALL_ACCESS;
	pDesc->NSTable	    = 0;

	DataSyncBarrier ();

	return pL3;
}

boolean CAddressSpace::MapPage (uintptr ulVA, uintptr ulPA, const TKPageAttr &Attr,
				boolean bOwned)
{
	assert ((ulVA & KPAGE_MASK) == 0);
	assert ((ulPA & KPAGE_MASK) == 0);
	assert (IS_USER_VA (ulVA));		// never touch kernel L2 entries

	TARMV8MMU_LEVEL3_DESCRIPTOR *pL3 = GetOrCreateL3 (L2_INDEX (ulVA));
	if (pL3 == 0)
	{
		return FALSE;
	}

	TARMV8MMU_LEVEL3_PAGE_DESCRIPTOR *pPage = &pL3[L3_INDEX (ulVA)].Page;

	pPage->Value11	     = 3;
	pPage->AttrIndx	     = Attr.AttrIndx;
	pPage->NS	     = 0;
	pPage->AP	     = Attr.AP;
	pPage->SH	     = Attr.SH;
	pPage->AF	     = 1;
	pPage->nG	     = Attr.nG;
	pPage->Reserved0_1   = 0;
	pPage->OutputAddress = ARMV8MMUL3PAGEADDR (ulPA);
	pPage->Reserved0_2   = 0;
	pPage->Continous     = 0;
	pPage->PXN	     = Attr.PXN;
	pPage->UXN	     = Attr.UXN;
	pPage->Ignored	     = bOwned ? PAGE_SW_OWNED : 0;

	DataSyncBarrier ();

	return TRUE;
}

void CAddressSpace::FlushTLB (void)
{
	u64 ulArg = (u64) m_nASID << TTBR0_ASID_SHIFT;
	asm volatile ("dsb ishst; tlbi aside1is, %0; dsb ish; isb" :: "r" (ulArg) : "memory");
}

void CAddressSpace::MapContig (u64 ulVA, u64 ulPhys, unsigned nPages, const TKPageAttr &Attr)
{
	for (unsigned i = 0; i < nPages; i++)
	{
		MapPage (ulVA + (u64) i * KPAGE_SIZE, ulPhys + (u64) i * KPAGE_SIZE, Attr);
	}
}

void *CAddressSpace::MapSurface (u64 ulPhys, unsigned nPages)
{
	if (nPages == 0)
	{
		return 0;
	}
	u64 ulVA   = m_ulSurfaceNext;
	u64 ulNext = ulVA + (u64) nPages * KPAGE_SIZE;
	if (ulNext > USER_SURFACE_END)
	{
		return 0;				// arena exhausted
	}
	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;		// EL0 RW (frames owned by the CSurface)
	MapContig (ulVA, ulPhys, nPages, Attr);
	m_ulSurfaceNext = ulNext;
	DataSyncBarrier ();
	return (void *) ulVA;
}

CMailbox *CAddressSpace::GetOrCreateMailbox (void)
{
	if (m_pMailbox == 0)
	{
		m_pMailbox = new CMailbox;
	}
	return m_pMailbox;
}

boolean CAddressSpace::IsMapped (uintptr ulVA)
{
	if (!IS_USER_VA (ulVA)) return FALSE;
	const TARMV8MMU_LEVEL2_TABLE_DESCRIPTOR *pDesc = &m_pL2[L2_INDEX (ulVA)].Table;
	if (pDesc->Value11 != 3) return FALSE;
	const TARMV8MMU_LEVEL3_DESCRIPTOR *pL3 = (const TARMV8MMU_LEVEL3_DESCRIPTOR *)
		ARMV8MMUL2TABLEPTR ((u64) pDesc->TableAddress);
	return pL3[L3_INDEX (ulVA)].Page.Value11 == 3;
}

boolean CAddressSpace::MapStack (u64 ulTop, u64 nSize)
{
	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;
	for (u64 va = KPAGE_ALIGN_DOWN (ulTop - nSize); va < ulTop; va += KPAGE_SIZE)
	{
		if (!IsMapped (va) && MapNewPage (va, Attr) == 0)
		{
			return FALSE;			// (what is mapped stays: freed at teardown)
		}
	}
	return TRUE;
}

void *CAddressSpace::MapNewPage (uintptr ulVA, const TKPageAttr &Attr)
{
	// App data frames (ELF segments + heap) come from the HIGH zone (1-3GB): they are
	// pure CPU memory (no DMA -- SD I/O is PIO, net buffers are copied kernel-side), and
	// HIGH gives ~2GB instead of the small low pager. The frame stays identity-mapped
	// (kernel zeroes/ELF-loads it by identity) and is mapped into the app at ulVA. The
	// per-AS page tables themselves stay in the low pager (palloc, see GetOrCreateL3).
	void *pFrame = palloc_high ();		// identity-mapped: kernel VA == PA
	if (pFrame == 0)
	{
		return 0;
	}
	memset (pFrame, 0, KPAGE_SIZE);

	if (!MapPage (ulVA, (uintptr) pFrame, Attr, TRUE))	// TRUE: we own this frame
	{
		pfree (pFrame);
		return 0;
	}
	m_nOwnedPages++; g_nUserPages++;	// a user data frame (MapPage may also add an L3)

	return pFrame;
}

void *CAddressSpace::Sbrk (long nIncrement)
{
	u64 ulOld = m_ulHeapBrk;
	if (nIncrement == 0) return (void *) ulOld;

	if (nIncrement < 0)			// shrink: lower the break, keep pages mapped
	{					// (reclaimed wholesale at teardown anyway)
		u64 ulDec = (u64) (-nIncrement);
		m_ulHeapBrk = (ulDec >= ulOld - USER_HEAP_BASE) ? USER_HEAP_BASE : ulOld - ulDec;
		return (void *) ulOld;
	}

	u64 ulWant = ulOld + (u64) nIncrement;
	if (ulWant > USER_HEAP_MAX) return (void *) -1;		// out of heap VA

	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;			// EL0 RW
	while (m_ulHeapEnd < ulWant)				// map fresh pages to cover [old,want)
	{
		if (MapNewPage (m_ulHeapEnd, Attr) == 0) return (void *) -1;	// OOM
		m_ulHeapEnd += KPAGE_SIZE;
	}
	m_ulHeapBrk = ulWant;
	return (void *) ulOld;
}

void *CAddressSpace::CodeAlloc (u64 ulSize)
{
	u64 ulPages = (ulSize + KPAGE_SIZE - 1) / KPAGE_SIZE;
	if (ulPages == 0 || m_ulCodeNext + ulPages * KPAGE_SIZE > USER_CODE_END) return 0;
	u64 ulVA = m_ulCodeNext;
	TKPageAttr Attr = KPAGE_ATTR_APP_RWX;
	for (u64 i = 0; i < ulPages; i++)
	{
		if (MapNewPage (ulVA + i * KPAGE_SIZE, Attr) == 0)
		{
			m_ulCodeNext = ulVA + i * KPAGE_SIZE;	// (what is mapped stays, freed at teardown)
			return 0;
		}
	}
	m_ulCodeNext = ulVA + ulPages * KPAGE_SIZE;
	DataSyncBarrier ();
	return (void *) ulVA;
}

void CAddressSpace::Activate (void)
{
	u64 ulTTBR0 = MAKE_TTBR0 ((u64) m_pL2, m_nASID);
	asm volatile ("msr ttbr0_el1, %0; isb" :: "r" (ulTTBR0) : "memory");
}

// ---- kernel address space + scheduler hook -----------------------------------

void AddrSpaceInit (void)
{
	u64 ulTTBR0;
	asm volatile ("mrs %0, ttbr0_el1" : "=r" (ulTTBR0));
	s_ulKernelTTBR0 = ulTTBR0 & ~(0xFFFFULL << TTBR0_ASID_SHIFT);	// strip ASID
}

void ActivateKernelAddressSpace (void)
{
	asm volatile ("msr ttbr0_el1, %0; isb" :: "r" (s_ulKernelTTBR0) : "memory");
}

void AddressSpaceTaskSwitch (CTask *pTask)
{
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS != 0)
	{
		pAS->Activate ();
	}
	else
	{
		ActivateKernelAddressSpace ();
	}
}

void AddressSpaceTaskTerminate (CTask *pTask)
{
	// Called when a terminated task is reaped (it is not the current task and its
	// address space is not active). Free the address space, if any.
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (pAS != 0)
	{
		pTask->SetUserData (0, TASK_USER_DATA_USER);
		if (pAS->DropTask () > 0)
		{
			return;			// a thread: the process goes on (or its other
						// tasks are reaped in this same batch)
		}
		if (g_bVerbose)
			CLogger::Get ()->Write ("proc", LogNotice, "exit: %s (pid %u)",
						pTask->GetName (), pAS->GetPid ());
		NetCloseByPid (pAS->GetPid ());		// close any TCP sockets it leaked
		IpcOnProcessGone (pAS->GetPid ());	// forget it in the IPC router (clears shell)
		if (CSurfaceManager::Get () != 0)
			CSurfaceManager::Get ()->DestroyByOwner (pAS->GetPid ());	// free its surfaces
		pTask->SetUserData (0, TASK_USER_DATA_USER);
		delete pAS;
	}
}
