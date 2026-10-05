//
// el0.cpp -- apps at EL0 (kern/el0.h): the EL0 table and code pages, the per-core EL0 setup, the C
// side of the EL0 exceptions (system calls and their statistics, the ID register emulation,
// faults, preemption).
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
#include <kern/uaccess.h>		// UAccessCopy (the faulting instruction), UserPut
#include <kern/vm.h>			// (v75) demand paging, the pins
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

// ---- the table's layout, as the blob (arch/aarch64/el0blob.S) and the stubs use it -------------

#define SLOT(f)		(__builtin_offsetof (TKApiTable, f) / sizeof (u64))
#define KAPI_SLOTS	KAPI_TABLE_SLOTS		// (kern/addrspace.h)

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
static_assert (sizeof (struct kapi_syscall_stats) == 104, "kern/kapi_abi.h: struct kapi_syscall_stats (ABI)");

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

static inline const u64 *KernelSlots (void)
{
	return (const u64 *) KApiKernelTable ();
}

// AppKit's table (kern/kapi_abi.h) where every program's stubs read it: APPKIT_TABLE_VA, in the page of
// the kernel's own table. Before any program runs (kernel.cpp: the first program's start).
static_assert (KAPI_SLOTS * 8 <= APPKIT_TABLE_VA - KAPI_TABLE_VA, "the kernel's table and AppKit's in one page");
static_assert (APPKIT_TABLE_VA - KAPI_TABLE_VA + APPKIT_TABLE_MAX * 8ULL <= KPAGE_SIZE, "AppKit's table in the page");
void El0InstallAppKit (const u64 *pEntries, unsigned nEntries)
{
	u64 *pDst = s_pTable + (APPKIT_TABLE_VA - KAPI_TABLE_VA) / 8;
	for (unsigned i = 0; i < APPKIT_TABLE_MAX; i++) pDst[i] = i < nEntries ? pEntries[i] : 0;
	CleanDataCacheRange ((u64) (uintptr) s_pTable, KPAGE_SIZE);
	asm volatile ("dsb ish; isb" ::: "memory");
}

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
	const u64 *pKernel = KernelSlots ();
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

	// Done at EL0, without the kernel (their kernel slots are 0): the memory primitives (docs/EL0
	// §4.5) and the event pump, whose handlers are the app's code (§4.2).
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
static u64 s_ulCntFrq = 1;			// CNTFRQ_EL0 (the system counter's Hz)

// The ID registers ID_AA64*_EL1 (op0 3, op1 0, CRn 0, CRm 4..7, op2 0..7) as core 0 read them at
// boot: the same on every core of the BCM2711 (four Cortex-A72). The unallocated encodings of that
// space are RAZ (ARMv8.0), so all 32 can be read.
static u64 s_IdRegs[8][8];

#define ID_READ(m, o)	asm volatile ("mrs %0, S3_0_C0_C" #m "_" #o : "=r" (s_IdRegs[m][o]))
#define ID_READ8(m)	do { ID_READ (m, 0); ID_READ (m, 1); ID_READ (m, 2); ID_READ (m, 3); \
			     ID_READ (m, 4); ID_READ (m, 5); ID_READ (m, 6); ID_READ (m, 7); } while (0)

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

	if (nCore == 0)
	{
		ID_READ8 (4);
		ID_READ8 (5);
		ID_READ8 (6);
		ID_READ8 (7);
		asm volatile ("mrs %0, cntfrq_el0" : "=r" (v));
		s_ulCntFrq = v != 0 ? v : 1;
	}
}

void El0ConfigurePmu (boolean bOn)
{
	s_bPmu = bOn;
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

static inline u64 Cntpct (void)
{
	u64 v;
	asm volatile ("mrs %0, cntpct_el0" : "=r" (v));
	return v;
}

// The calling process (core 0 only: an app core's job has no task there).
static inline CAddressSpace *CurrentAS (void)
{
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	return pTask != 0 ? (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER) : 0;
}

// One system call counted (core 0, IRQs masked: no lock). The rate: calls per second over a
// window of at least 1 s of CNTPCT, closed by the first call after its end.
static inline void CountSyscall (CAddressSpace *pAS, u64 n)
{
	TSyscallStats &S = *pAS->GetSyscallStats ();
	S.nTotal++;
	if (n < KAPI_SLOTS && S.nSlot[n] != 0xFFFFFFFFu)
	{
		S.nSlot[n]++;
	}
	S.nWindowCount++;
	u64 ulNow = Cntpct ();
	if (S.ulWindowStart == 0)
	{
		S.ulWindowStart = ulNow;
	}
	else if (ulNow - S.ulWindowStart >= s_ulCntFrq)
	{
		S.nRate = (u32) ((u64) S.nWindowCount * s_ulCntFrq / (ulNow - S.ulWindowStart));
		S.ulWindowStart = ulNow;
		S.nWindowCount = 0;
	}
}

// A system call: the table's slot x8, its arguments x0-x7 (every kapi takes at most 8, all
// integers or pointers: none on the stack, none in the FP registers), its result in x0. Run in the
// calling task, on its kernel stack with the IRQs on: a kapi that Yields (a wait, a chunked read)
// parks the task as usual.
static void Syscall (TTrapFrame *pFrame)
{
	u64 n = pFrame->x[8];
	CAddressSpace *pAS = CurrentAS ();
	if (pAS != 0)
	{
		CountSyscall (pAS, n);
	}

	const u64 *pKernel = KernelSlots ();
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

	// (v75) The ranges its probes pinned are free again; the zaps they deferred are done.
	if (pAS != 0)
	{
		VmUnpinTask (pAS, CScheduler::Get ()->GetCurrentTask ());
	}
}

// ---- the MRS emulation (kern/el0.h) ----
//
// Emulated: op0 3, op1 0, CRn 0 -- the ID register space -- with
//   CRm 0: op2 0 MIDR_EL1 (the real value), 5 MPIDR_EL1 (the real value of the core the app runs
//          on: Aff0 = the core's number -- what an app built before v73 read in kapi__core),
//          6 REVIDR_EL1 (real); the other op2 are not emulated (the app is killed);
//   CRm 1..3 (the AArch32 ID registers): 0 (no AArch32 at EL0 on Onyx);
//   CRm 4..7 (ID_AA64*_EL1): the boot snapshot, sanitised --
//     ID_AA64PFR0_EL1 (4, 0): EL0, EL1, FP, AdvSIMD, SVE, DIT, CSV2, CSV3 kept; EL2, EL3, GIC, RAS,
//                             SEL2, MPAM, AMU, RME... 0 (nothing an app can use);
//     ID_AA64DFR0_EL1 (5, 0): DebugVer only (the rest is the self-hosted debug / PMU / trace
//                             hardware: an app cannot reach it);
//     ID_AA64DFR1_EL1 (5, 1), ID_AA64AFR0_EL1 (5, 4), ID_AA64AFR1_EL1 (5, 5): 0;
//     everything else (PFR1, ZFR0, ISAR0..2, MMFR0..2, the RAZ ones): the hardware's value.
// Either encoding of the trap: EC 0x00 (ARMv8.0, the A72: the instruction is read from the app's
// code) or EC 0x18 (FEAT_IDST: decoded from the ISS).

#define PFR0_KEEP	0xFF0F000F00FF00FFULL	// CSV3 CSV2 . DIT . . SVE . . . AdvSIMD FP . . EL1 EL0
#define DFR0_KEEP	0x000000000000000FULL	// DebugVer

// The value of the register (op1 = 0, CRn = 0 assumed): TRUE if it is emulated.
static boolean IdRegValue (unsigned nCRm, unsigned nOp2, u64 *pValue)
{
	u64 v = 0;
	if (nCRm == 0)
	{
		switch (nOp2)
		{
		case 0:	asm volatile ("mrs %0, midr_el1" : "=r" (v)); break;
		case 5:	asm volatile ("mrs %0, mpidr_el1" : "=r" (v)); break;
		case 6:	asm volatile ("mrs %0, revidr_el1" : "=r" (v)); break;
		default: return FALSE;
		}
	}
	else if (nCRm >= 4 && nCRm <= 7)
	{
		v = s_IdRegs[nCRm][nOp2];
		if (nCRm == 4 && nOp2 == 0)		v &= PFR0_KEEP;
		else if (nCRm == 5 && nOp2 == 0)	v &= DFR0_KEEP;
		else if (nCRm == 5 && (nOp2 == 1 || nOp2 == 4 || nOp2 == 5)) v = 0;
	}
	else if (nCRm > 7)
	{
		return FALSE;
	}
	// (CRm 1..3: 0)
	*pValue = v;
	return TRUE;
}

// An EL0 exception that may be an MRS of an ID register: emulated (the target register written,
// ELR past the instruction) -> TRUE. IRQs masked; touches no lock (an app core's too).
static boolean EmulateMrs (TTrapFrame *pFrame, unsigned nEC, u64 ulESR)
{
	unsigned nOp0, nOp1, nCRn, nCRm, nOp2, nRt;
	if (nEC == EC_SYSREG)
	{
		u32 nISS = (u32) ulESR & 0x1FFFFFF;
		if ((nISS & 1) == 0)
		{
			return FALSE;				// an MSR (a write): never
		}
		nOp0 = (nISS >> 20) & 3;
		nOp2 = (nISS >> 17) & 7;
		nOp1 = (nISS >> 14) & 7;
		nCRn = (nISS >> 10) & 15;
		nRt  = (nISS >> 5) & 31;
		nCRm = (nISS >> 1) & 15;
	}
	else if (nEC == EC_UNKNOWN)
	{
		u64 ulPC = pFrame->elr_el1;
		u32 nInsn;
		if (   (ulPC & 3) != 0
		    || !IS_USER_VA (ulPC) || !IS_USER_VA (ulPC + 3)
		    || UAccessCopy (&nInsn, (const void *) (uintptr) ulPC, 4) != 0)
		{
			return FALSE;
		}
		if ((nInsn & 0xFFF00000u) != 0xD5300000u)	// MRS Xt, S<op0>_<op1>_C<n>_C<m>_<op2>
		{
			return FALSE;
		}
		nOp0 = 2 | ((nInsn >> 19) & 1);
		nOp1 = (nInsn >> 16) & 7;
		nCRn = (nInsn >> 12) & 15;
		nCRm = (nInsn >> 8) & 15;
		nOp2 = (nInsn >> 5) & 7;
		nRt  = nInsn & 31;
	}
	else
	{
		return FALSE;
	}

	u64 ulValue;
	if (nOp0 != 3 || nOp1 != 0 || nCRn != 0 || !IdRegValue (nCRm, nOp2, &ulValue))
	{
		return FALSE;
	}
	if (nRt != 31)					// (31: XZR, the value dropped)
	{
		pFrame->x[nRt] = ulValue;
	}
	pFrame->elr_el1 += 4;
	return TRUE;
}

// A fault at EL0 (any synchronous exception but a system call or an emulated MRS): the process is
// killed, never the machine. Its kmsg line names it (v75: with why, when the regions tell -- "stack
// overflow", "PROT_NONE access"); the desktop shows a notice.
static void __attribute__ ((noreturn)) Fault (TTrapFrame *pFrame, unsigned nEC, u64 ulESR, u64 ulFAR)
{
	asm volatile ("msr daifclr, #3" ::: "memory");

	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	const char *pWhy = (nEC == EC_DABORT_LOW || nEC == EC_IABORT_LOW) ? VmFaultWhy (pAS, ulFAR) : 0;
	CLogger::Get ()->Write ("el0", LogError,
				"%s (pid %u) killed: %s%s%s%s at pc %lx (address %lx, ESR %lx, sp %lx, lr %lx)",
				pTask->GetName (), pAS != 0 ? pAS->GetPid () : 0, FaultName (nEC),
				pWhy != 0 ? " (" : "", pWhy != 0 ? pWhy : "", pWhy != 0 ? ")" : "",
				(unsigned long) pFrame->elr_el1, (unsigned long) ulFAR,
				(unsigned long) ulESR, (unsigned long) pFrame->sp_el0,
				(unsigned long) pFrame->x[30]);

	// The return addresses along the frame-pointer chain (x29 -> {next x29, lr}): the program's
	// callers, to symbolise with addr2line. Read with the fault-safe copy; the chain must climb.
	{
		CString Trace, Item;
		u64 ulFP = pFrame->x[29];
		for (unsigned i = 0; i < 16; i++)
		{
			u64 Pair[2];
			if (   (ulFP & 7) != 0 || !IS_USER_VA (ulFP) || !IS_USER_VA (ulFP + 15)
			    || UAccessCopy (Pair, (const void *) (uintptr) ulFP, sizeof Pair) != 0
			    || Pair[1] == 0)
			{
				break;
			}
			Item.Format (" %lx", (unsigned long) Pair[1]);
			Trace.Append (Item);
			if (Pair[0] <= ulFP) break;
			ulFP = Pair[0];
		}
		if (Trace.GetLength () != 0)
		{
			CLogger::Get ()->Write ("el0", LogError, "%s backtrace:%s", pTask->GetName (), (const char *) Trace);
		}
	}

	CString Text;
	Text.Format ("%s stopped: %s at %lx", pTask->GetName (), FaultName (nEC),
		     (unsigned long) pFrame->elr_el1);
	IpcNotify ("Application error", Text);

	if (pAS != 0) pAS->SetTermReason (KAPI_PROC_FAULT, EL0_FAULT_STATUS);	// (v75: proc_wait)
	kapi_exit (EL0_FAULT_STATUS);		// (the whole process: its other threads too)
	for (;;) { }
}

#define EL0_OOM_STATUS		(-9)		// (v75) the exit status of a process the OOM path kills

// (v75) A page could not be filled: the app pool is under its reserve (kern/vm.h). The process is
// killed, as for a fault -- the system and the other apps go on.
static void __attribute__ ((noreturn)) OutOfMemory (u64 ulFAR)
{
	asm volatile ("msr daifclr, #3" ::: "memory");

	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	CLogger::Get ()->Write ("vm", LogError,
				"%s (pid %u) killed: out of memory (page fault at %lx, %u KB resident)",
				pTask->GetName (), pAS != 0 ? pAS->GetPid () : 0, (unsigned long) ulFAR,
				pAS != 0 ? pAS->GetPages () * (unsigned) (KPAGE_SIZE / 1024) : 0);

	CString Text;
	Text.Format ("%s ran out of memory", pTask->GetName ());
	IpcNotify ("Application error", Text);

	if (pAS != 0) pAS->SetTermReason (KAPI_PROC_OOM, EL0_OOM_STATUS);
	kapi_exit (EL0_OOM_STATUS);
	for (;;) { }
}

#define DFSC_KIND(esr)		((esr) & 0x3C)	// the fault status code, without its level
#define DFSC_TRANSLATION	0x04
#define DFSC_PERMISSION		0x0C
#define ESR_FNV			(1ULL << 10)	// FAR not valid
#define ESR_WNR			(1ULL << 6)	// a write

// (v75) A data abort from EL0 in a lazy region (kern/vm.h): a translation fault fills the page, a
// permission fault the region now allows (a page another thread just filled, an mprotect up) is
// spurious -- the access is retried either way (TRUE). Out of memory kills the process. FALSE: a
// real fault. Core 0 (an app core's job: sys/appcore.cpp).
static boolean PageFault (unsigned nEC, u64 ulESR, u64 ulFAR)
{
	if (nEC == EC_IABORT_LOW)		// (v78) code not filled yet in an executable lazy region
	{
		if (DFSC_KIND (ulESR) != DFSC_TRANSLATION || (ulESR & ESR_FNV) != 0) return FALSE;
		CAddressSpace *pAS = CurrentAS ();
		if (pAS == 0 || (VmProtAt (pAS, ulFAR) & KAPI_PROT_EXEC) == 0) return FALSE;
		asm volatile ("msr daifclr, #3" ::: "memory");
		int r = VmFaultIn (pAS, ulFAR, FALSE);
		if (r == -KAPI_ENOMEM) OutOfMemory (ulFAR);
		return r >= 0;
	}
	if (   nEC != EC_DABORT_LOW || (ulESR & ESR_FNV) != 0
	    || (DFSC_KIND (ulESR) != DFSC_TRANSLATION && DFSC_KIND (ulESR) != DFSC_PERMISSION))
	{
		return FALSE;
	}
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0) return FALSE;
	asm volatile ("msr daifclr, #3" ::: "memory");		// (as a system call: the allocator)
	int r = VmFaultIn (pAS, ulFAR, (ulESR & ESR_WNR) != 0);
	if (r == 0)
	{
		// There and allowed: a stale TLB entry here (a protection raised) -- dropped.
		u64 ulArg = (((ulFAR & ~(u64) KPAGE_MASK) >> 12) & 0xFFFFFFFFFFFULL)
			  | ((u64) pAS->GetASID () << TTBR0_ASID_SHIFT);
		asm volatile ("tlbi vae1, %0; dsb nsh; isb" :: "r" (ulArg) : "memory");
	}
	if (r == -KAPI_ENOMEM) OutOfMemory (ulFAR);
	return r >= 0;
}

void El0SyncHandler (TTrapFrame *pFrame)
{
	u64 ulESR, ulFAR;
	asm volatile ("mrs %0, esr_el1" : "=r" (ulESR));
	asm volatile ("mrs %0, far_el1" : "=r" (ulFAR));
	unsigned nEC = (unsigned) (ulESR >> 26) & 0x3F;

	// An ID register read: emulated, on any core (an app core's job too).
	if ((nEC == EC_UNKNOWN || nEC == EC_SYSREG) && EmulateMrs (pFrame, nEC, ulESR))
	{
#ifdef ARM_ALLOW_MULTI_CORE
		if (CMultiCoreSupport::ThisCore () == 0)
#endif
		{
			CAddressSpace *pAS = CurrentAS ();
			if (pAS != 0) pAS->GetSyscallStats ()->nEmulated++;
		}
		return;
	}

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

	if (PageFault (nEC, ulESR, ulFAR))	// (v75) demand paging: filled, retried
	{
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

	// The time slice is over (or a "real time" task is ready): the app is in its own code here
	// (EL0) -- switch now, on this task's kernel stack at EL1t, IRQ + FIQ masked: the task is parked
	// exactly as a voluntary yielder (one resume path: Yield returns, El0Return erets). The only
	// preemption there is: the kernel itself is not preempted (an IRQ taken at EL1 never switches,
	// arch/aarch64/exception.cpp KernelIRQExit).
	if (!CScheduler::Get ()->IsReschedPending ())
	{
		return;
	}
	CScheduler::Get ()->ClearResched ();
	CScheduler::Get ()->OnPreempt ();	// (the tasks that yield voluntarily go first)
	CScheduler::Get ()->Yield ();
}

// ---- the statistics (v74) ---------------------------------------------------------------------

struct TFindPid
{
	unsigned       nPid;
	CAddressSpace *pFound;
};

static boolean FindPidCallback (CTask *pTask, const char *pName, TTaskState State,
				TTaskFlags Flags, void *pParam)
{
	(void) pName; (void) Flags;
	TFindPid *pFind = (TFindPid *) pParam;
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	if (State != TaskStateTerminated && pAS != 0 && pAS->GetPid () == pFind->nPid)
	{
		pFind->pFound = pAS;
		return FALSE;				// (found: stop)
	}
	return TRUE;
}

int kapi_proc_stats (int nPid, struct kapi_syscall_stats *pOut)
{
	if (!CScheduler::IsActive ())
	{
		return -1;
	}
	CAddressSpace *pAS = 0;
	if (nPid == 0)
	{
		pAS = CurrentAS ();
	}
	else if (nPid > 0)
	{
		TFindPid Find = { (unsigned) nPid, 0 };
		CScheduler::Get ()->EnumerateTasks (FindPidCallback, &Find);
		pAS = Find.pFound;
	}
	if (pAS == 0)
	{
		return -1;
	}

	// Made here, copied out whole (kern/uaccess.h); no Yield in between: the space stays.
	const TSyscallStats &S = *pAS->GetSyscallStats ();
	struct kapi_syscall_stats Out;
	memset (&Out, 0, sizeof Out);
	Out.syscalls = S.nTotal;
	Out.emulated = S.nEmulated;
	Out.slots = KAPI_SLOTS;
	Out.rate = S.nRate;
	u64 ulElapsed = S.ulWindowStart != 0 ? Cntpct () - S.ulWindowStart : 0;
	if (ulElapsed >= s_ulCntFrq)			// the current window is over 1 s (idle: 0)
	{
		Out.rate = (unsigned) ((u64) S.nWindowCount * s_ulCntFrq / ulElapsed);
	}

	// The top slots, the most first (a selection: KAPI_SYSCALL_STATS_TOP passes over ~200 slots).
	u32 nPrev = 0xFFFFFFFFu;
	unsigned nPrevSlot = 0;
	for (unsigned k = 0; k < KAPI_SYSCALL_STATS_TOP; k++)
	{
		unsigned nBest = 0;
		u32 nBestCount = 0;
		for (unsigned n = 1; n < KAPI_SLOTS; n++)
		{
			u32 c = S.nSlot[n];
			// strictly after the previous one in the order (count descending, slot ascending)
			boolean bAfter = c < nPrev || (c == nPrev && n > nPrevSlot);
			if (bAfter && c > nBestCount)	// (the first of the largest: its smallest slot)
			{
				nBest = n;
				nBestCount = c;
			}
		}
		if (nBest == 0)
		{
			break;
		}
		Out.top_slot[k] = nBest;
		Out.top_count[k] = nBestCount;
		nPrev = nBestCount;
		nPrevSlot = nBest;
	}

	return UserPut (pOut, Out) ? 0 : -2;
}
