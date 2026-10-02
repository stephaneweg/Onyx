//
// el0.cpp -- protected mode (kern/el0.h): the EL0 table and code pages, the per-core EL0 setup,
// the C side of the EL0 exceptions (system calls, faults, preemption), the launch policy.
//
// MIT licence (Onyx). Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include <kern/el0.h>
#include <kern/trapframe.h>
#include <kern/kapi_abi.h>
#include <kern/kapitable.h>
#include <kern/addrspace.h>
#include <kern/appcore.h>
#include <kern/crashlog.h>
#include <kern/ipc.h>			// IpcNotify (a killed process: a notice)
#include <kern/layout.h>
#include <circle/multicore.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/alloc.h>		// palloc
#include <circle/synchronize.h>		// CleanDataCacheRange
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/util.h>
#include <assert.h>

extern "C" void kapi_exit (int nStatus);
extern "C" void PreemptDoYield (void);		// arch/aarch64/exception.cpp

// ---- the table's layout, as the blob (arch/aarch64/el0blob.S) and the stubs use it -------------

#define SLOT(f)		(__builtin_offsetof (TKApiTable, f) / sizeof (u64))
#define KAPI_SLOTS	(sizeof (TKApiTable) / sizeof (u64))

static_assert (KAPI_STUBS_VA == KAPI_TABLE_VA + 0x10000ULL, "kern/el0.h: KAPI_STUBS_VA");
static_assert (__builtin_offsetof (TKApiTable, create_window) == 8, "slot 0 is version (+ padding)");
static_assert (sizeof (TKApiTable) % sizeof (u64) == 0, "the table is made of 8-byte slots");
static_assert (SLOT (exit) == EL0_SYS_EXIT, "kern/el0.h: EL0_SYS_EXIT");
static_assert (SLOT (should_exit) == EL0_SYS_SHOULD_EXIT, "kern/el0.h: EL0_SYS_SHOULD_EXIT");
static_assert (SLOT (thread_exit) == EL0_SYS_THREAD_EXIT, "kern/el0.h: EL0_SYS_THREAD_EXIT");
static_assert (SLOT (pop_event) == EL0_SYS_POP_EVENT, "kern/el0.h: EL0_SYS_POP_EVENT");
static_assert (SLOT (event_mods) == EL0_SYS_EVENT_MODS, "kern/el0.h: EL0_SYS_EVENT_MODS");
static_assert (SLOT (pop_post) == EL0_SYS_POP_POST, "kern/el0.h: EL0_SYS_POP_POST");
static_assert (SLOT (pump_sleep) == EL0_SYS_PUMP_SLEEP, "kern/el0.h: EL0_SYS_PUMP_SLEEP");
static_assert (KAPI_SLOTS <= EL0_STUB_SLOTS, "kern/el0.h: more stubs than EL0_STUB_SLOTS");
static_assert (KAPI_SLOTS < EL0_SYS_CORE_DONE, "EL0_SYS_CORE_DONE must not be a slot");
static_assert (sizeof (struct kapi_event) == 32 && __builtin_offsetof (struct kapi_event, mods) == 28,
	       "el0blob.S: struct kapi_event's layout");
static_assert (sizeof (struct kapi_posted) == 24 && __builtin_offsetof (struct kapi_posted, value) == 16,
	       "el0blob.S: struct kapi_posted's layout");

// The blob (arch/aarch64/el0blob.S): EL0 code, copied into the code page.
extern "C" const u8 El0BlobStart[], El0BlobEnd[];
extern "C" const u8 El0Memcpy[], El0Memmove[], El0Memset[];
extern "C" const u8 El0PumpEvents[], El0WaitForExit[], El0PumpWait[];
extern "C" const u8 El0ThreadReturn[], El0MainReturn[], El0CoreReturn[];

static u64 *s_pTable = 0;		// the EL0 table page (palloc: identity, PA == VA)
static u32 *s_pCode = 0;		// the EL0 code page: the stubs, then the blob
static boolean s_bUserSide[KAPI_SLOTS];	// slots done at EL0: never a system call

static u64 BlobVA (const u8 *pLabel)
{
	return KAPI_STUBS_VA + EL0_BLOB_OFFSET + (u64) (pLabel - El0BlobStart);
}

u64 El0MainReturnVA (void)	{ return BlobVA (El0MainReturn); }
u64 El0ThreadReturnVA (void)	{ return BlobVA (El0ThreadReturn); }
u64 El0CoreReturnVA (void)	{ return BlobVA (El0CoreReturn); }
u64 El0TablePhys (void)		{ return (u64) (uintptr) s_pTable; }
u64 El0CodePhys (void)		{ return (u64) (uintptr) s_pCode; }

void El0Init (void)
{
	s_pTable = (u64 *) palloc ();
	s_pCode = (u32 *) palloc ();
	assert (s_pTable != 0 && s_pCode != 0);
	memset (s_pTable, 0, KPAGE_SIZE);
	memset (s_pCode, 0, KPAGE_SIZE);	// (0 = UDF: a jump into a hole is a fault, not a slide)

	u64 nBlob = (u64) (El0BlobEnd - El0BlobStart);
	assert (EL0_BLOB_OFFSET + nBlob <= KPAGE_SIZE);
	memcpy ((u8 *) s_pCode + EL0_BLOB_OFFSET, El0BlobStart, nBlob);

	// Slot 0 is not a pointer (version + padding): the same value. Every other slot: its stub, or 0
	// where the kernel's table has 0 (an app tests some entries before calling them).
	const u64 *pKernel = (const u64 *) KApiTablePhys ();
	s_pTable[0] = pKernel[0];
	for (unsigned n = 1; n < KAPI_SLOTS; n++)
	{
		u32 *p = s_pCode + n * (EL0_STUB_SIZE / 4);
		p[0] = 0xD2800008u | (n << 5);		// movz x8, #n
		p[1] = 0xD4000001u;			// svc  #0
		p[2] = 0xD65F03C0u;			// ret
		p[3] = 0xD503201Fu;			// nop
		s_pTable[n] = pKernel[n] != 0 ? KAPI_STUBS_VA + (u64) n * EL0_STUB_SIZE : 0;
	}

	// Done at EL0, without the kernel: the memory primitives (docs/EL0 §4.5) and the event pump,
	// whose handlers are the app's code (§4.2).
	struct { unsigned nSlot; const u8 *pCode; } User[] =
	{
		{ SLOT (memcpy),	El0Memcpy },
		{ SLOT (memmove),	El0Memmove },
		{ SLOT (memset),	El0Memset },
		{ SLOT (pump_events),	El0PumpEvents },
		{ SLOT (wait_for_exit),	El0WaitForExit },
		{ SLOT (pump_wait),	El0PumpWait },
	};
	for (unsigned i = 0; i < sizeof User / sizeof User[0]; i++)
	{
		s_pTable[User[i].nSlot] = BlobVA (User[i].pCode);
		s_bUserSide[User[i].nSlot] = TRUE;
	}

	// To the point of coherency (the pages are mapped cacheable at another VA, read and fetched
	// by every core), then no stale instruction anywhere (PIPT I-caches: the inner shareable
	// invalidate covers the cores already started).
	CleanDataCacheRange ((u64) (uintptr) s_pTable, KPAGE_SIZE);
	CleanDataCacheRange ((u64) (uintptr) s_pCode, KPAGE_SIZE);
	asm volatile ("dsb ish; ic ialluis; dsb ish; isb" ::: "memory");
}

// ---- every core ------------------------------------------------------------------------------

static boolean s_bPmu = FALSE;			// cmdline.txt el0pmu=1: the PMU at EL0

void El0CoreInit (unsigned nCore)
{
	u64 v;

	// CNTKCTL_EL1.EL0PCTEN (0) / EL0VCTEN (1): cntpct_el0 / cntvct_el0 (and cntfrq_el0) at EL0.
	// (The event-stream bits CrashLogCoreInit sets are kept.)
	asm volatile ("mrs %0, cntkctl_el1" : "=r" (v));
	v |= 3;
	asm volatile ("msr cntkctl_el1, %0" :: "r" (v));

	// SCTLR_EL1, the EL0-only controls: UCI (26, dc cvau / ic ivau: a JIT's cache maintenance),
	// nTWE (18) / nTWI (16) (wfe / wfi do not trap: the spin waits of user/kapi.h, emucore),
	// UCT (15, ctr_el0), DZE (14, dc zva). Nothing changes for EL1 code.
	asm volatile ("mrs %0, sctlr_el1" : "=r" (v));
	v |= (1ul << 26) | (1ul << 18) | (1ul << 16) | (1ul << 15) | (1ul << 14);
	asm volatile ("msr sctlr_el1, %0" :: "r" (v));

	// PMUSERENR_EL0.EN: the performance counters at EL0 (user/gc/gc.h) -- only on request: an
	// MDCR_EL2.TPM left set by the boot stub would trap this write to EL2.
	if (s_bPmu)
	{
		asm volatile ("msr pmuserenr_el0, %0" :: "r" ((u64) 1));
	}

	// The core's number, read-only at EL0 (user/kapi.h kapi__core, libc's on_app_core).
	asm volatile ("msr tpidrro_el0, %0" :: "r" ((u64) nCore));
	asm volatile ("msr tpidr_el1, xzr");		// (El0Return sets it before any EL0 code)
	asm volatile ("isb" ::: "memory");
}

// ---- the launch policy ----------------------------------------------------------------------

static boolean s_bDefaultProtected = FALSE;
static char s_ProtectedList[256];

void El0Configure (const char *pAppMode, const char *pList)
{
	s_bDefaultProtected = pAppMode != 0 && strcmp (pAppMode, "protected") == 0;
	unsigned i = 0;
	if (pList != 0)
		for (; pList[i] != '\0' && i < sizeof s_ProtectedList - 1; i++) s_ProtectedList[i] = pList[i];
	s_ProtectedList[i] = '\0';
}

void El0ConfigurePmu (boolean bOn)
{
	s_bPmu = bOn;
}

static boolean InList (const char *pName)
{
	if (pName == 0 || pName[0] == '\0') return FALSE;
	unsigned nLen = strlen (pName);
	for (const char *p = s_ProtectedList; *p != '\0'; )
	{
		const char *q = p;
		while (*q != '\0' && *q != ',') q++;
		if ((unsigned) (q - p) == nLen && strncmp (p, pName, nLen) == 0) return TRUE;
		p = *q == ',' ? q + 1 : q;
	}
	return FALSE;
}

boolean El0ShouldProtect (int nAppTxtMode, const char *pName)
{
	if (nAppTxtMode >= 0) return nAppTxtMode != 0;
	if (InList (pName)) return TRUE;
	return s_bDefaultProtected;
}

// ---- the exceptions from EL0 ----------------------------------------------------------------

// ESR_EL1.EC values
#define EC_UNKNOWN	0x00
#define EC_SVC64	0x15
#define EC_SYSREG	0x18
#define EC_IABORT_LOW	0x20
#define EC_PC_ALIGN	0x22
#define EC_DABORT_LOW	0x24
#define EC_SP_ALIGN	0x26
#define EC_FP		0x2C
#define EC_BRK		0x3C

#define EL0_FAULT_STATUS	(-11)		// the exit status of a process killed by a fault

static const char *FaultName (unsigned nEC)
{
	switch (nEC)
	{
	case EC_UNKNOWN:	return "an undefined instruction";
	case EC_SYSREG:		return "a privileged system register";
	case EC_IABORT_LOW:	return "an instruction abort";
	case EC_PC_ALIGN:	return "a misaligned PC";
	case EC_DABORT_LOW:	return "a data abort";
	case EC_SP_ALIGN:	return "a misaligned SP";
	case EC_FP:		return "a floating-point exception";
	case EC_BRK:		return "a breakpoint";
	default:		return "an exception";
	}
}

// A system call: the table's slot x8, its arguments x0-x7 (every kapi takes at most 8, all
// integers or pointers: none on the stack, none in the FP registers), its result in x0. Run as an
// EL1 app's call runs it, on this task's kernel stack with the IRQs on: a kapi that Yields (a
// wait, a chunked read) parks the task as usual.
static void Syscall (TTrapFrame *pFrame)
{
	u64 n = pFrame->x[8];
	const u64 *pKernel = (const u64 *) KApiTablePhys ();
	if (n == 0 || n >= KAPI_SLOTS || s_bUserSide[n] || pKernel[n] == 0)
	{
		pFrame->x[0] = 0;		// (not a system call: done at EL0, or no such entry)
		return;
	}

	asm volatile ("msr daifclr, #3" ::: "memory");		// IRQ + FIQ on, as in any task

	typedef u64 TKapiFn (u64, u64, u64, u64, u64, u64, u64, u64);
	TKapiFn *pFn = (TKapiFn *) pKernel[n];
	pFrame->x[0] = (*pFn) (pFrame->x[0], pFrame->x[1], pFrame->x[2], pFrame->x[3],
			       pFrame->x[4], pFrame->x[5], pFrame->x[6], pFrame->x[7]);
}

// A fault at EL0 (any synchronous exception but a system call): the process is killed, never the
// machine. Its kmsg line names it; the desktop shows a notice.
static void __attribute__ ((noreturn)) Fault (TTrapFrame *pFrame, unsigned nEC, u64 ulESR, u64 ulFAR)
{
	asm volatile ("msr daifclr, #3" ::: "memory");

	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	CLogger::Get ()->Write ("el0", LogError,
				"%s (pid %u) killed: %s at pc %lx (address %lx, ESR %lx, sp %lx, lr %lx)",
				pTask->GetName (), pAS != 0 ? pAS->GetPid () : 0, FaultName (nEC),
				(unsigned long) pFrame->elr_el1, (unsigned long) ulFAR,
				(unsigned long) ulESR, (unsigned long) pFrame->sp_el0,
				(unsigned long) pFrame->x[30]);

	CString Text;
	Text.Format ("%s stopped: %s at %lx", pTask->GetName (), FaultName (nEC),
		     (unsigned long) pFrame->elr_el1);
	IpcNotify ("Application error", Text);

	kapi_exit (EL0_FAULT_STATUS);		// (the whole process: its other threads too)
	for (;;) { }
}

void El0SyncHandler (TTrapFrame *pFrame)
{
	u64 ulESR, ulFAR;
	asm volatile ("mrs %0, esr_el1" : "=r" (ulESR));
	asm volatile ("mrs %0, far_el1" : "=r" (ulFAR));
	unsigned nEC = (unsigned) (ulESR >> 26) & 0x3F;

#ifdef ARM_ALLOW_MULTI_CORE
	// An app core's job (kern/appcore.h): its end (El0CoreReturn) or a fault -- the job is
	// dropped either way. A system call is not allowed there (the kernel runs on core 0 only).
	if (CMultiCoreSupport::ThisCore () != 0)
	{
		AppCoreOnEl0Sync (pFrame, nEC == EC_SVC64 && pFrame->x[8] == EL0_SYS_CORE_DONE);
		return;
	}
#endif

	if (nEC == EC_SVC64)
	{
		Syscall (pFrame);
		return;
	}

	Fault (pFrame, nEC, ulESR, ulFAR);
}

void El0IrqExit (TTrapFrame *pFrame)
{
#ifdef ARM_ALLOW_MULTI_CORE
	if (CMultiCoreSupport::ThisCore () != 0)	// an app core (its stop IPI): no scheduling
	{
		AppCoreOnIRQExit (pFrame);
		return;
	}
#endif
	if (!CScheduler::IsActive ())
	{
		return;
	}

	CrashLogSample (pFrame);
	CScheduler::Get ()->StallSample (pFrame->elr_el1, pFrame->x[30]);

	// The time slice is over: the app is always in its own code here (EL0) -- switch now, on this
	// task's kernel stack at EL1t, IRQ + FIQ masked: the task is parked exactly as
	// PreemptTrampoline parks a legacy app (one resume path: Yield returns, El0Return erets).
	if (!CScheduler::Get ()->IsReschedPending ())
	{
		return;
	}
	CScheduler::Get ()->ClearResched ();
	PreemptDoYield ();
}
