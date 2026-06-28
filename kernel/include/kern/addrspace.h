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
class CStream;
class CMailbox;
struct CProcess;

class CAddressSpace
{
public:
	CAddressSpace (void);
	~CAddressSpace (void);

	boolean IsValid (void) const		{ return m_pL2 != 0; }

	// Map one 64 KB user page (ulVA, ulPA both 64 KB-aligned, ulVA in user range).
	// bOwned marks the frame as kernel-allocated (palloc'd) for this space, so it
	// is pfree'd when the space is destroyed. Leave FALSE for frames owned
	// elsewhere (e.g. a window canvas owned by its CWindow).
	boolean MapPage (uintptr ulVA, uintptr ulPA, const TKPageAttr &Attr,
			 boolean bOwned = FALSE);

	// Map nPages consecutive 64 KB pages [ulVA..] -> [ulPhys..] (e.g. a window canvas).
	void MapContig (u64 ulVA, u64 ulPhys, unsigned nPages, const TKPageAttr &Attr);

	// Map a shell surface (its physical frames) into this space at a fresh VA in the
	// per-process surface arena [USER_SURFACE_BASE, USER_SURFACE_END). Returns the VA
	// (a user pointer) or 0 if the arena is exhausted. The frames are owned by the
	// CSurface, not this space, so teardown drops the mapping but never frees them.
	void *MapSurface (u64 ulPhys, unsigned nPages);

	// Allocate a fresh physical frame and map it at ulVA. Returns the frame's
	// kernel (identity) address so the caller can fill it, or 0 on failure.
	void *MapNewPage (uintptr ulVA, const TKPageAttr &Attr);

	// Unix-style sbrk for the per-process heap at USER_HEAP_BASE: move the break by
	// nIncrement bytes, mapping fresh 64 KB pages (EL1 RW) as it grows. Returns the
	// PREVIOUS break (a user VA), or (void*)-1 on failure (out of heap VA / OOM).
	// The user allocator (user/umm.h) calls this through kapi_sbrk.
	void *Sbrk (long nIncrement);

	// Load TTBR0_EL1 = L2-base | (ASID << 48); isb.
	void Activate (void);

	u8 GetASID (void) const			{ return m_nASID; }

	// Physical 64 KB pages this address space owns (palloc'd: its L2 + L3 tables +
	// every MapNewPage frame). For ps / the memory monitor. Excludes shared mappings
	// like a window canvas (owned by its CWindow).
	unsigned GetPages (void) const		{ return m_nOwnedPages; }

	// Process id: a small monotonic number assigned at creation, for ps/kill.
	unsigned GetPid (void) const		{ return m_nPid; }

	// Parent pid: the spawner's pid (0 = no parent / launched from the drawer). When
	// the parent dies, the reaper terminates its still-running children (cascade).
	void SetParentPid (unsigned n)		{ m_nParentPid = n; }
	unsigned GetParentPid (void) const	{ return m_nParentPid; }

	// The window owned by this process (if any); freed when the space is destroyed.
	void SetWindow (CWindow *pWindow)	{ m_pWindow = pWindow; }
	CWindow *GetWindow (void)		{ return m_pWindow; }

	// IPC mailbox for the activity-shell message router (kapi_mailbox_*). Lazily
	// created on first use; freed with the address space. Returns 0 only on OOM.
	CMailbox *GetOrCreateMailbox (void);

	// stdio: streams owned by this process (released on teardown; stdout gets a
	// CloseWrite so its reader sees EOF). A spawned process also has a CProcess
	// handle (its done/status set on teardown) and an argv string.
	void SetStdin (CStream *p)		{ m_pStdin = p; }
	CStream *GetStdin (void)		{ return m_pStdin; }
	void SetStdout (CStream *p)		{ m_pStdout = p; }
	CStream *GetStdout (void)		{ return m_pStdout; }
	void SetProcess (CProcess *p)		{ m_pProcess = p; }
	void SetExitStatus (int n)		{ m_nExitStatus = n; }
	void SetArgs (const char *pArgs);
	const char *GetArgs (void)		{ return m_Args; }

	// Current working directory (FatFs absolute path, e.g. "SD:/foo"; "SD:/" = root).
	// Inherited from the spawner; relative paths in file kapis resolve against it.
	void SetCwd (const char *pCwd);
	const char *GetCwd (void)		{ return m_Cwd; }

private:
	TARMV8MMU_LEVEL3_DESCRIPTOR *GetOrCreateL3 (unsigned nL2Index);

private:
	TARMV8MMU_LEVEL2_DESCRIPTOR *m_pL2;	// this process's L2 table (one 64 KB page)
	u8			     m_nASID;
	unsigned		     m_nPid;	// process id (monotonic, for ps/kill)
	unsigned		     m_nParentPid;	// spawner's pid (0 = none); cascade on death
	CWindow			    *m_pWindow;

	CStream			    *m_pStdin;	// stdio streams (refs released on teardown)
	CStream			    *m_pStdout;
	CMailbox		    *m_pMailbox; // IPC mailbox (lazy; freed on teardown)
	CProcess		    *m_pProcess; // spawn handle (done/status set on teardown)
	int			     m_nExitStatus;
	char			     m_Args[256]; // argv string for the child (kapi_get_args)
	char			     m_Cwd[256]; // current working directory (FatFs abs path)
	unsigned		     m_nOwnedPages; // palloc'd 64 KB frames owned (for ps/meminfo)
	u64			     m_ulHeapBrk; // logical heap break (kapi_sbrk), >= USER_HEAP_BASE
	u64			     m_ulHeapEnd; // page-aligned top of the mapped heap region
	u64			     m_ulSurfaceNext; // next free VA in the surface arena (bump)
};

// Total 64 KB physical pages currently owned by all user address spaces (sum of
// every CAddressSpace::GetPages()). Maintained as spaces are built/torn down; read
// by the meminfo kapi / memory monitor.
extern unsigned g_nUserPages;

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
