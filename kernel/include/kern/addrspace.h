//
// addrspace.h
//
// Per-process address space: a private L2 translation table (64 KB granule) that
// shares the kernel's identity mappings (copied from the kernel L2 table, global
// pages) and adds private, ASID-tagged user mappings in [USER_VA_BASE, USER_VA_END).
//
// The kernel half stays identity-mapped and EL1-only; the user half is per-process and
// EL0-accessible (every process runs at EL0, kern/el0.h; the presets in kern/layout.h).
// Switching is just TTBR0_EL1 plus an ASID, so no TLB flush is needed on a normal
// context switch.
//
#ifndef _kern_addrspace_h
#define _kern_addrspace_h

#include <kern/layout.h>		// page-table structs (via armv8mmu.h) + attrs
#include <kern/handle.h>		// CHandleTable (the process's opaque handles)
#include <kern/kapi_abi.h>		// TKApiTable (its slots: the system calls)
#include <circle/types.h>

// The table's slots, 8 bytes each (slot 0: the version) -- the system calls' numbers.
#define KAPI_TABLE_SLOTS	(sizeof (TKApiTable) / sizeof (u64))

// A process's system calls (sys/el0.cpp counts them, kapi_proc_stats reports them). Written by
// core 0 only (the system calls are dispatched there, IRQs masked): no lock.
struct TSyscallStats
{
	u64	nTotal;				// system calls since the process started
	u64	nEmulated;			// ID register reads emulated (kern/el0.h)
	u64	ulWindowStart;			// CNTPCT at the start of the current window (0: none)
	u32	nWindowCount;			// system calls in the current window
	u32	nRate;				// per second, over the last full window (>= 1 s)
	u32	nSlot[KAPI_TABLE_SLOTS];	// per table slot (saturating at 0xFFFFFFFF)
};

class CWindow;
class CStream;
class CMailbox;
struct CProcess;
class CTask;
class CProcThreads;

class CAddressSpace
{
public:
	// The kernel's mappings shared, KAPI_TABLE_VA holding the EL0 kapi table and
	// KAPI_STUBS_VA the EL0 code (kern/el0.h); nothing else mapped yet.
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

	// Is the 64 KB page at ulVA mapped?
	boolean IsMapped (uintptr ulVA);

	// Map fresh zeroed pages (EL0 RW, owned) over [ulTop - nSize, ulTop)
	// where nothing is mapped yet: a user stack (kern/el0.h). FALSE: out of memory.
	boolean MapStack (u64 ulTop, u64 nSize);

	// Allocate a fresh physical frame and map it at ulVA. Returns the frame's
	// kernel (identity) address so the caller can fill it, or 0 on failure.
	void *MapNewPage (uintptr ulVA, const TKPageAttr &Attr);

	// Unix-style sbrk for the per-process heap at USER_HEAP_BASE: move the break by
	// nIncrement bytes, mapping fresh 64 KB pages (EL0 RW) as it grows. Returns the
	// PREVIOUS break (a user VA), or (void*)-1 on failure (out of heap VA / OOM).
	// The user allocator (user/umm.h) calls this through kapi_sbrk.
	void *Sbrk (long nIncrement);

	// Fresh zeroed pages, EL0 read/write/execute, in the code arena (a JIT) -> VA, 0 full.
	void *CodeAlloc (u64 ulSize);

	// Load TTBR0_EL1 = L2-base | (ASID << 48); isb.
	void Activate (void);

	// Forget this space's cached translations on every core (after remapping pages already
	// mapped: MapContig over a live range, e.g. a window's canvas that grew).
	void FlushTLB (void);

	u8 GetASID (void) const			{ return m_nASID; }

	// The TTBR0_EL1 value that selects this space (an app core loads it: kern/appcore.h).
	u64 GetTTBR0 (void) const		{ return MAKE_TTBR0 ((u64) m_pL2, m_nASID); }

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

	// stdio: streams owned by this process (a ref each, released on teardown; stdout gets
	// a CloseWrite so its reader sees EOF). A spawned process also has a CProcess record
	// (a ref: its done/status set and the ref dropped on teardown) and an argv string.
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

	// The tasks running in this space: the app's main task (the first one) and its threads
	// (kapi_thread_create). AddressSpaceTaskTerminate frees the space with the last one.
	void AddTask (CTask *pTask)		{ if (m_pMainTask == 0) m_pMainTask = pTask; m_nTasks++; }
	unsigned DropTask (void)		{ return m_nTasks > 0 ? --m_nTasks : 0; }
	// (only compared: once the main task has ended the pointer may be stale)
	CTask *GetMainTask (void) const		{ return m_pMainTask; }

	// The process's threads, synchronisation objects and posted calls (kern/thread.h):
	// made on first use, freed with the space.
	CProcThreads *GetThreads (void)		{ return m_pThreads; }
	void SetThreads (CProcThreads *p)	{ m_pThreads = p; }

	// The process's handles (files, directories, streams, spawned processes: kern/handle.h),
	// shared by its threads; the ones still open are closed by the teardown.
	CHandleTable *GetHandles (void)		{ return &m_Handles; }

	// Its system-call statistics (kern/el0.h).
	TSyscallStats *GetSyscallStats (void)	{ return &m_Syscalls; }

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
	char			     m_Args[1024]; // argv string for the child (kapi_get_args)
	char			     m_Cwd[256]; // current working directory (FatFs abs path)
	unsigned		     m_nOwnedPages; // palloc'd 64 KB frames owned (for ps/meminfo)
	u64			     m_ulHeapBrk; // logical heap break (kapi_sbrk), >= USER_HEAP_BASE
	u64			     m_ulHeapEnd; // page-aligned top of the mapped heap region
	u64			     m_ulSurfaceNext; // next free VA in the surface arena (bump)
	u64			     m_ulCodeNext; // next free VA in the code arena (bump)
	CTask			    *m_pMainTask; // the app's first task (see AddTask)
	unsigned		     m_nTasks;	// tasks running in this space (main + threads)
	CProcThreads		    *m_pThreads; // threads / sync objects / posts (lazy)
	CHandleTable		     m_Handles;	// opaque handles (closed on teardown)
	TSyscallStats		     m_Syscalls; // system calls counted (sys/el0.cpp)
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
