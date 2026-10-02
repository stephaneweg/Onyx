//
// el0.h -- protected mode: apps running at EL0, calling the kernel by system calls.
//
// A process is either LEGACY (EL1t, calls the kapi table's kernel functions directly: the model
// since the beginning) or PROTECTED (EL0t: it cannot touch the kernel's memory, the devices or the
// other processes' frames, and a fault kills it instead of halting the machine). Both coexist; a
// process opts in by its app.txt ("mode = protected"), by cmdline.txt (protected=<name>,... or
// appmode=protected for every process). docs/EL0-PROTECTED-MODE.md has the design.
//
// The binaries are the same: a protected process's KAPI_TABLE_VA page holds, instead of the
// kernel's functions, the addresses of EL0 code mapped next to it (KAPI_STUBS_VA, read-only,
// executable at EL0 only):
//   - one stub per table entry, "mov x8, #slot; svc #0; ret" (slot = the entry's index in
//     TKApiTable, counted in 8-byte words: version is slot 0);
//   - a user-side memcpy / memset / memmove (no system call for those);
//   - the user-side event pump (pump_events, wait_for_exit, pump_wait): it pops the window's
//     events and the posted calls with system calls (pop_event, pop_post, v73) and calls the
//     app's handlers itself, at EL0;
//   - the return paths of a thread (thread_exit), of the main entry (exit) and of an app core's
//     job.
//
// The kernel side of an EL0 exception (arch/aarch64/el0.S): the trap frame (kern/trapframe.h) is
// built on the running task's KERNEL stack (its CTask stack; TPIDR_EL1 holds the top of it while
// the core is at EL0), then the handler runs at EL1t on that stack -- a system call runs the
// kernel's kapi function exactly as a legacy app's direct call does, so the kapis that Yield keep
// working; an IRQ taken at EL0 may simply Yield there (the timer's preemption), no trampoline.
//
// MIT licence (Onyx). Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#ifndef _kern_el0_h
#define _kern_el0_h

// (included by el0blob.S too: no C here but under !__ASSEMBLER__)

// ---- the EL0 code page (stubs + the user-side routines), mapped just above the table ----------
#define KAPI_STUBS_VA		(14ULL * 0x40000000ULL + 0x10000ULL)	// KAPI_TABLE_VA + 64 KB
#define EL0_STUB_SIZE		16				// bytes a stub (3 instructions + pad)
#define EL0_STUB_SLOTS		512				// stubs [0, 8 KB)
#define EL0_BLOB_OFFSET		(EL0_STUB_SLOTS * EL0_STUB_SIZE)	// the routines from 8 KB

// The table's slots the user-side code calls (static_assert'ed against TKApiTable in sys/el0.cpp).
#define EL0_SYS_EXIT		13
#define EL0_SYS_SHOULD_EXIT	16
#define EL0_SYS_THREAD_EXIT	168
#define EL0_SYS_POP_EVENT	194		// (v73)
#define EL0_SYS_EVENT_MODS	195		// (v73)
#define EL0_SYS_POP_POST	196		// (v73)
#define EL0_SYS_PUMP_SLEEP	197		// (v73)
// Not a table slot: an app core's job returned (its return address is the blob's El0CoreReturn).
#define EL0_SYS_CORE_DONE	0xFFFF

#define EL0_GUI_EVENT_KEY	5		// GUI_EVENT_KEY (kern/gui/window.h, user/kapi.h)
#define EL0_POSTS_MAX		256		// POSTS_MAX (kern/thread.h): a pump runs one ring's worth

// ---- stacks -------------------------------------------------------------------------------------
// A protected task's kernel stack (its CTask stack: the trap frames, the kapis it calls).
#define EL0_KSTACK_SIZE		0x40000				// 256 KB
// The main task's user stack: below USER_STACK_TOP (kern/layout.h), app.txt's "stack" or 1 MB.
#define EL0_USTACK_MIN		0x100000			// 1 MB
#define EL0_USTACK_MAX		0x4000000			// 64 MB
// Threads' user stacks: one 32 MB slot per thread record (kern/thread.h THREAD_RECS), the stack
// at the top of its slot, the rest unmapped (a guard below every stack).
#define USER_THREAD_STACKS	(32ULL * 0x40000000ULL)		// 32 GB
#define USER_THREAD_SLOT	0x2000000ULL			// 32 MB (THREAD_STACK_MAX is 16 MB)

#ifndef __ASSEMBLER__

#include <circle/types.h>

struct TTrapFrame;

// Boot: build the EL0 table page and the EL0 code page (after KApiTableInit, before any process).
void El0Init (void);

// Every core, early: let EL0 read the counters (CNTKCTL_EL1), the cache type and do the JIT's cache
// maintenance (SCTLR_EL1.UCI / UCT / DZE), WFE / WFI without a trap (nTWE / nTWI), the PMU
// (PMUSERENR_EL0), and publish the core's number in TPIDRRO_EL0 (read-only at EL0: what
// "mrs mpidr_el1" gave an EL1 app). Changes nothing for EL1 code.
void El0CoreInit (unsigned nCore);

// The physical (= identity) addresses of the two pages a protected space maps.
u64 El0TablePhys (void);
u64 El0CodePhys (void);

// The user VAs of the blob's return paths (the LR a new EL0 context starts with).
u64 El0MainReturnVA (void);
u64 El0ThreadReturnVA (void);
u64 El0CoreReturnVA (void);

// The launch policy (cmdline.txt): appmode=protected|legacy (the default for every process) and
// protected=<name>[,<name>...] (programs run protected: an app's folder name, a tool's file name).
void El0Configure (const char *pAppMode, const char *pList);
// cmdline.txt el0pmu=1: the performance counters readable at EL0 (PMUSERENR_EL0, by El0CoreInit).
void El0ConfigurePmu (boolean bOn);
// nAppTxtMode: the app.txt's "mode" (1 protected, 0 legacy, -1 none). pName: the program's short
// name ("eyes", "threadtest"). Priority: app.txt, then the protected= list, then appmode=.
boolean El0ShouldProtect (int nAppTxtMode, const char *pName);

#ifdef __cplusplus
extern "C" {
#endif

// arch/aarch64/el0.S: start EL0 code on the current (kernel) stack -- never returns. The trap
// frame of the new context is built just below the current SP (the kernel stack's top for its
// later exceptions); x0 = ulArg, x30 = ulLR, SP_EL0 = ulUserSP, PC = ulEntry, PSTATE = EL0t with
// the interrupts enabled. Call it at EL1t, in the process's address space.
void El0Enter (u64 ulEntry, u64 ulUserSP, u64 ulArg, u64 ulLR) __attribute__ ((noreturn));

// The C side of the EL0 vectors (sys/el0.cpp): on the task's kernel stack, at EL1t.
void El0SyncHandler (TTrapFrame *pFrame);	// SVC -> the kapi; else the process is killed
void El0IrqExit (TTrapFrame *pFrame);		// after the IRQ's handler: preempt (core 0)

#ifdef __cplusplus
}
#endif

#endif // __ASSEMBLER__

#endif // _kern_el0_h
