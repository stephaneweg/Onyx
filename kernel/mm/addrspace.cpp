//
// addrspace.cpp
//
#include <kern/addrspace.h>
#include <kern/gui/window.h>		// CWindow + CWindowManager (process window)
#include <kern/stream.h>		// CStream + CProcess (stdio teardown)
#include <kern/kapi_abi.h>		// KAPI_TABLE_VA (fixed VA for the kapi table)
#include <kern/kapitable.h>		// KApiTablePhys
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

CAddressSpace::CAddressSpace (void)
:	m_pL2 (0),
	m_nASID (0),
	m_nPid (s_nNextPid++),
	m_nParentPid (0),
	m_pWindow (0),
	m_pStdin (0),
	m_pStdout (0),
	m_pProcess (0),
	m_nExitStatus (0)
{
	m_Args[0] = '\0';
	m_Cwd[0] = 'S'; m_Cwd[1] = 'D'; m_Cwd[2] = ':'; m_Cwd[3] = '/'; m_Cwd[4] = '\0';	// root

	assert (s_ulKernelTTBR0 != 0);		// AddrSpaceInit() must run first

	m_pL2 = (TARMV8MMU_LEVEL2_DESCRIPTOR *) palloc ();
	if (m_pL2 == 0)
	{
		return;
	}

	// Share all kernel mappings: copy the kernel L2 table verbatim. Its valid
	// entries point at the kernel's L3 tables (global, identity, EL1-only) and are
	// now shared; the user-range entries it contains are invalid, ready for us.
	memcpy (m_pL2, (const void *) (s_ulKernelTTBR0 & ~(0xFFFFULL << TTBR0_ASID_SHIFT)),
		KPAGE_SIZE);

	m_nASID = AllocASID ();

	// Publish the kapi ABI table read-only at the fixed user VA, so apps call the
	// kernel through it (no linking against kernel addresses). The table page is a
	// kernel global (not owned by this space), so teardown frees the L3 we add here
	// but never the page itself.
	TKPageAttr ApiAttr = KPAGE_ATTR_APP_RODATA;
	MapContig (KAPI_TABLE_VA, KApiTablePhys (), 1, ApiAttr);

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
	// stdio teardown: signal EOF to whoever reads our stdout, drop our stream refs,
	// and mark the spawn handle done (so a waiter unblocks). Done first so the
	// terminal sees the child finish promptly.
	if (m_pStdout != 0)
	{
		m_pStdout->CloseWrite ();
		m_pStdout->Release ();
		m_pStdout = 0;
	}
	if (m_pStdin != 0)
	{
		m_pStdin->Release ();
		m_pStdin = 0;
	}
	if (m_pProcess != 0)
	{
		m_pProcess->nStatus = m_nExitStatus;
		m_pProcess->bDone = TRUE;
		m_pProcess = 0;
	}

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
				// Never free the shared kapi ABI table page: it is a kernel global
				// aliased read-only into every address space (mapped not-owned, so
				// this should already be skipped -- but guard explicitly: freeing it
				// would corrupt the table for every app).
				if (ulFrame == KApiTablePhys ())
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

void CAddressSpace::MapContig (u64 ulVA, u64 ulPhys, unsigned nPages, const TKPageAttr &Attr)
{
	for (unsigned i = 0; i < nPages; i++)
	{
		MapPage (ulVA + (u64) i * KPAGE_SIZE, ulPhys + (u64) i * KPAGE_SIZE, Attr);
	}
}

void *CAddressSpace::MapNewPage (uintptr ulVA, const TKPageAttr &Attr)
{
	void *pFrame = palloc ();		// identity-mapped: kernel VA == PA
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

	return pFrame;
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
		delete pAS;
	}
}
