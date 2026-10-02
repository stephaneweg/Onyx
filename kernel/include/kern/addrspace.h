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

extern unsigned g_nUserPages;		// (below)

class CWindow;
class CStream;
class CMailbox;
struct CProcess;
class CTask;
class CProcThreads;
struct TVmSpace;			// (v75) kern/vm.h (WP-MEM)
struct TProcInfo;			// (v75) kern/procx.h (WP-FILE/PROC)

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

	// (v75) A user stack (kern/el0.h) over [ulTop - nSize, ulTop): a LAZY region (kern/vm.h) --
	// its pages filled on first touch; nothing mapped now. FALSE: out of memory.
	boolean MapStack (u64 ulTop, u64 nSize);

	// Allocate a fresh physical frame and map it at ulVA. Returns the frame's
	// kernel (identity) address so the caller can fill it, or 0 on failure.
	void *MapNewPage (uintptr ulVA, const TKPageAttr &Attr);

	// Unix-style sbrk for the per-process heap at USER_HEAP_BASE: move the break by
	// nIncrement bytes. (v75) The heap is a lazy region (kern/vm.h) that grows with the break
	// (its pages filled on first touch; at once once the app holds an app core) and shrinks with
	// it (the pages above the new break dropped). Returns the PREVIOUS break (a user VA), or
	// (void*)-1 on failure (out of heap VA, or a growth beyond the app pool: the overcommit
	// check). The user allocators (user/umm.h, newlib's malloc) call this through kapi_sbrk.
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

	// (v75, kern/vm.h) The L3 descriptor of ulVA's page (a user VA), or 0 if its 512 MB slot has
	// no table yet. The VM code reads and writes it as one 64-bit word.
	TARMV8MMU_LEVEL3_PAGE_DESCRIPTOR *PageDesc (u64 ulVA);
	// A frame the VM code unmapped and freed: no longer counted.
	void PageReleased (void)		{ if (m_nOwnedPages > 0) m_nOwnedPages--; if (g_nUserPages > 0) g_nUserPages--; }
	// Its page tables (the L2 + its private L3s), in 64 KB pages.
	unsigned GetTablePages (void) const;

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
	CProcess *GetProcess (void)		{ return m_pProcess; }		// (v75)
	void SetExitStatus (int n)		{ m_nExitStatus = n; }
	int GetExitStatus (void) const		{ return m_nExitStatus; }	// (v75)
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

	// (v75) Its virtual memory (kern/vm.h: lazy regions, pins) and its POSIX side (kern/procx.h:
	// argv and environment blocks): 0 until their work package makes them; freed by the
	// teardown (VmTeardown, ProcInfoTeardown).
	TVmSpace *GetVm (void)			{ return m_pVm; }
	void SetVm (TVmSpace *p)		{ m_pVm = p; }
	TProcInfo *GetProcInfo (void)		{ return m_pProcInfo; }
	void SetProcInfo (TProcInfo *p)		{ m_pProcInfo = p; }

	// (v75) Why the process ended, as proc_wait reports it: KAPI_PROC_EXITED (the default: it
	// exited, its status is the code), KAPI_PROC_FAULT (-11), KAPI_PROC_KILLED (-9),
	// KAPI_PROC_OOM (-9). Set before the end (a fault, a kill, the OOM killer).
	void SetTermReason (int nReason, int nCode) { m_nTermReason = nReason; m_nTermCode = nCode; }
	int GetTermReason (void) const		{ return m_nTermReason; }
	int GetTermCode (void) const		{ return m_nTermCode; }

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
	TVmSpace		    *m_pVm;	// (v75) virtual memory (WP-MEM; 0: none yet)
	TProcInfo		    *m_pProcInfo; // (v75) argv / env blocks (WP-FILE/PROC; 0: none)
	int			     m_nTermReason; // (v75) KAPI_PROC_* (proc_wait)
	int			     m_nTermCode;	// (v75) its code (the exit status for EXITED)
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
