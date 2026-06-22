//
// addrspace.h
//
// Per-process address space: a private L2 translation table (64 KB granule) that
// shares the kernel's identity mappings (copied from the kernel L2 table, global
// pages) and adds private, ASID-tagged user mappings in [USER_VA_BASE, USER_VA_END).
//
// See ARCHITECTURE.md §4. The kernel half stays identity-mapped and EL1-only; the
// user half is per-process and EL0-accessible. Switching is just TTBR0_EL1 plus an
// ASID, so no TLB flush is needed on a normal context switch.
//
#ifndef _kern_addrspace_h
#define _kern_addrspace_h

#include <kern/layout.h>		// page-table structs (via armv8mmu.h) + attrs
#include <circle/types.h>

class CWindow;

class CAddressSpace
{
public:
	CAddressSpace (void);
	~CAddressSpace (void);

	boolean IsValid (void) const		{ return m_pL2 != 0; }

	// Map one 64 KB user page (ulVA, ulPA both 64 KB-aligned, ulVA in user range).
	boolean MapPage (uintptr ulVA, uintptr ulPA, const TKPageAttr &Attr);

	// Map nPages consecutive 64 KB pages [ulVA..] -> [ulPhys..] (e.g. a window canvas).
	void MapContig (u64 ulVA, u64 ulPhys, unsigned nPages, const TKPageAttr &Attr);

	// Allocate a fresh physical frame and map it at ulVA. Returns the frame's
	// kernel (identity) address so the caller can fill it, or 0 on failure.
	void *MapNewPage (uintptr ulVA, const TKPageAttr &Attr);

	// Load TTBR0_EL1 = L2-base | (ASID << 48); isb.
	void Activate (void);

	u8 GetASID (void) const			{ return m_nASID; }

	// The window owned by this process (if any); freed when the space is destroyed.
	void SetWindow (CWindow *pWindow)	{ m_pWindow = pWindow; }
	CWindow *GetWindow (void)		{ return m_pWindow; }

private:
	TARMV8MMU_LEVEL3_DESCRIPTOR *GetOrCreateL3 (unsigned nL2Index);

private:
	TARMV8MMU_LEVEL2_DESCRIPTOR *m_pL2;	// this process's L2 table (one 64 KB page)
	u8			     m_nASID;
	CWindow			    *m_pWindow;
};

// Capture the kernel's TTBR0 base (call once, after the MMU is up) so kernel-only
// tasks can be switched back to the kernel address space.
void AddrSpaceInit (void);
void ActivateKernelAddressSpace (void);

// Scheduler task-switch handler: activate the new task's address space, or the
// kernel address space if it has none. Matches TSchedulerTaskHandler; register it
// with CScheduler::RegisterTaskSwitchHandler(). The per-task CAddressSpace* lives
// in the task's TASK_USER_DATA_USER slot.
class CTask;
void AddressSpaceTaskSwitch (CTask *pTask);

// Scheduler task-termination handler: free the address space owned by a terminating
// process. Register with CScheduler::RegisterTaskTerminationHandler().
void AddressSpaceTaskTerminate (CTask *pTask);

#endif // _kern_addrspace_h
