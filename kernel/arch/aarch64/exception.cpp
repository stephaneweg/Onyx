//
// exception.cpp
//
// C side of our exception handling (assembly in vectors.S). Reuses Circle's
// ExceptionHandler() for register dumps on kernel faults, and Circle's
// InterruptHandler() for GIC dispatch (called directly from the IRQ stub).
//
#include <kern/trapframe.h>
#include <kern/appcore.h>
#include <kern/addrspace.h>		// the faulting app's pid (AppFaultExit)
#include <kern/crashlog.h>
#include <kern/thread.h>		// WordWaitTick (v68)
#include <circle/multicore.h>
#include <kern/layout.h>		// IS_USER_VA (preempt-gate classification)
#include <kern/gui/gimage.h>
#include <circle/sched/scheduler.h>
#include <circle/2dgraphics.h>
#include <circle/string.h>
#include <circle/exception.h>		// EXCEPTION_* codes
#include <circle/exceptionstub.h>	// TAbortFrame, ExceptionHandler()
#include <circle/logger.h>
#include <circle/startup.h>		// halt()
#include <circle/actled.h>		// panic SOS on the ACT LED (headless)
#include <circle/timer.h>		// SimpleMsDelay
#include <circle/bcmframebuffer.h>	// the panic screen, by the CPU
#include <circle/synchronize.h>
#include <circle/util.h>
#include <circle/types.h>

// Panic surface: the framebuffer actually shown on HDMI (the compositor's
// C2DGraphics). Once the compositor runs, the boot console (m_Screen) is no
// longer scanned out, so an exception dump there is invisible. We paint a
// visible red panic with the key registers onto the displayed buffer instead.
static C2DGraphics *s_pPanicGraphics = 0;

void SetPanicGraphics (C2DGraphics *p2D)
{
	s_pPanicGraphics = p2D;
}

// Text without the heap: an exception handler runs with the FIQs masked, and the heap's spin lock
// (IRQ level) asserts then -- the report was lost behind "synchronize64.cpp(64): assertion failed:
// nTargetLevel == FIQ_LEVEL || !(nFlags & 0x40)". A small appender into a caller's buffer instead.
struct TPanicText
{
	char *p; unsigned n, cap;
	TPanicText (char *pBuf, unsigned nCap) : p (pBuf), n (0), cap (nCap) { p[0] = 0; }
	TPanicText &s (const char *q) { while (q && *q && n + 1 < cap) p[n++] = *q++; p[n] = 0; return *this; }
	TPanicText &x (u64 v)			// 0x..., the digits it needs
	{
		char d[20]; int k = 0;
		do { d[k++] = "0123456789ABCDEF"[v & 15]; v >>= 4; } while (v);
		s ("0x");
		while (k && n + 1 < cap) p[n++] = d[--k];
		p[n] = 0; return *this;
	}
};

static void PanicToScreen (unsigned nEC, u64 ulELR, u64 ulFAR, u64 ulSPSR)
{
	if (s_pPanicGraphics == 0)
	{
		return;
	}

	int nW = (int) s_pPanicGraphics->GetWidth ();
	int nH = (int) s_pPanicGraphics->GetHeight ();
	GImage Img ((u32 *) s_pPanicGraphics->GetBuffer (), nW, nH);

	Img.Clear (0x00500000);						// dark red
	Img.DrawText (16, 16, "*** KERNEL PANIC (exception) ***", 0x00FFFFFF);

	static char Line[96];
	TPanicText (Line, sizeof Line).s ("EC=").x (nEC).s ("  ELR=").x (ulELR);
	Img.DrawText (16, 40, Line, 0x00FFFF00);
	TPanicText (Line, sizeof Line).s ("FAR=").x (ulFAR).s ("  SPSR=").x (ulSPSR);
	Img.DrawText (16, 56, Line, 0x00FFFF00);

	// Copied into the displayed frame buffer by the CPU, not by UpdateDisplay's DMA: the
	// compositor's display DMA may be in flight (its channel taken, its completion never
	// seen with the IRQs masked here), and waiting for it hung the panic before the screen
	// and the SOS -- a panic then looked like a silent hang.
	CBcmFrameBuffer *pFB = (CBcmFrameBuffer *) s_pPanicGraphics->GetDisplay ();
	u8 *pDst = pFB != 0 ? (u8 *) (uintptr) pFB->GetBuffer () : 0;
	if (pDst == 0 || pFB->GetDepth () != 32) return;
	unsigned nPitch = pFB->GetPitch ();
	const u8 *pSrc = (const u8 *) s_pPanicGraphics->GetBuffer ();
	for (int y = 0; y < nH; y++)
		memcpy (pDst + (unsigned) y * nPitch, pSrc + (unsigned) y * (unsigned) nW * 4, (unsigned) nW * 4);
	CleanDataCacheRange ((uintptr) pDst, (unsigned) nH * nPitch);
}

// Headless sign of a kernel panic: blink SOS (... --- ...) on the green ACT LED forever,
// so a panic is told apart from a hang (LED frozen) without a screen or serial cable.
static void PanicBlink (boolean bForever)
{
	CActLED *pLED = CActLED::Get ();
	if (pLED == 0)
	{
		if (bForever) halt ();
		return;
	}
	static const unsigned Pattern[] = { 150, 150, 150, 450, 450, 450, 150, 150, 150 };
	do
	{
		for (unsigned i = 0; i < sizeof Pattern / sizeof Pattern[0]; i++)
		{
			pLED->On ();  CTimer::SimpleMsDelay (Pattern[i]);
			pLED->Off (); CTimer::SimpleMsDelay (150);
		}
		CTimer::SimpleMsDelay (1200);
	}
	while (bForever);
}

// ESR_EL1 exception classes we care about
#define EC_SVC64		0x15	// SVC from AArch64
#define EC_IABORT_LOWER		0x20	// instruction abort from a lower EL (EL0)
#define EC_IABORT_SAME		0x21
#define EC_DABORT_LOWER		0x24	// data abort from a lower EL (EL0)
#define EC_DABORT_SAME		0x25
#define EC_PC_ALIGN		0x22	// PC alignment fault

static inline u64 ReadESR (void)
{
	u64 nValue;
	asm volatile ("mrs %0, esr_el1" : "=r" (nValue));
	return nValue;
}

static inline u64 ReadFAR (void)
{
	u64 nValue;
	asm volatile ("mrs %0, far_el1" : "=r" (nValue));
	return nValue;
}

// Build a Circle TAbortFrame from our trap frame and hand it to Circle's
// ExceptionHandler(), which logs all registers and halts. Does not return.
static void DumpAndHalt (unsigned nException, TTrapFrame *pFrame)
{
	TAbortFrame Frame;
	Frame.esr_el1  = ReadESR ();
	Frame.spsr_el1 = pFrame->spsr_el1;
	Frame.x30      = pFrame->x[30];
	Frame.elr_el1  = pFrame->elr_el1;
	Frame.sp_el0   = pFrame->sp_el0;
	Frame.sp_el1   = (u64) pFrame + TF_SIZE;	// kernel SP at the moment of trap
	Frame.far_el1  = ReadFAR ();
	Frame.unused   = 0;

	unsigned nEC = (unsigned) ((Frame.esr_el1 >> 26) & 0x3F);

	// The crash record first (read back after the watchdog reboot: SD:/etc/lastcrash.txt) --
	// without the heap (the FIQs are masked here: its lock would assert, the report lost).
	{
		static char Line[200];
		TPanicText (Line, sizeof Line).s ("an exception: EC=").x (nEC).s (" ELR=").x (pFrame->elr_el1)
			.s (" FAR=").x (Frame.far_el1).s (" SPSR=").x (pFrame->spsr_el1).s (" LR=").x (pFrame->x[30])
			.s (" SP=").x (Frame.sp_el1).s (" task ")
			.s (CScheduler::IsActive () && CScheduler::Get ()->GetCurrentTask ()
			    ? CScheduler::Get ()->GetCurrentTask ()->GetName () : "-");
		CrashLogPanic (Line);
		CrashLogStack (pFrame);		// (the return addresses on the faulting context's stack)
	}

	// Paint a visible red panic (EC/ELR/FAR) on the displayed framebuffer -- the
	// boot console is no longer scanned out once the compositor runs.
	PanicToScreen (nEC, pFrame->elr_el1, Frame.far_el1, pFrame->spsr_el1);

	// Log the essentials (the boot console shows it if the compositor has not taken the
	// display yet; kmsg is gone with the system), then blink SOS instead of Circle's
	// ExceptionHandler, whose register dump would go to the same invisible console and
	// then halt with a frozen LED -- indistinguishable from a hang when headless.
	// (No CLogger here: it formats on the heap -- the assertion above. The crash record has it all.)
	(void) nException;
	PanicBlink (FALSE);			// one SOS first
	CrashLogDumpNow ("an exception (kernel panic)");	// core 1: the report, then a restart
	PanicBlink (TRUE);				// (hangreboot=0: SOS for ever)
}

// ---- A fault in an app's own code kills the app, not the machine -----------------------
//
// (Step 0 of protected mode, docs/EL0-PROTECTED-MODE.md §6.) Apps run at EL1t on their task's
// stack (SP_EL0); a synchronous exception arrives here on SP_EL1 (the core's exception stack),
// IRQ+FIQ masked. The task cannot be ended from here (no logging: the heap's lock asserts with
// the FIQs masked; no Yield on the exception stack), so the trap frame is rewritten instead: the
// eret lands in AppFaultExit, at EL1t, in the faulting task's own context, on the top of its
// CTask stack (the app's SP may be what went wrong), with the fault's registers as arguments.
// There the task logs the fault and ends its process through kapi_exit -- the app's own exit
// path: its window off the screen at once, its other threads terminated (TerminateGroup), its
// sockets closed; the reaper frees the rest (the address space: frames, window, surfaces, app
// cores, GPU objects, streams -- a waiting terminal sees the child end).
//
// Only a fault taken at EL1t in the app's own code qualifies: the faulting PC in the user VA
// range (IS_USER_VA -- the same test as the preemption gate in KernelIRQExit), or an instruction
// abort / PC alignment fault at a wild PC reached from app code (LR in the user VA range: a
// call through a bad pointer). A fault whose PC is in the kernel (a kapi call on behalf of an
// app included) keeps the post-mortem console. Kernel locks are never held while app code runs
// (the kapis that call back into an app -- the event pump, posts, a thread's entry -- hold
// nothing across the call); the no-kill count, the one such state the scheduler tracks, is
// checked: an app fault inside one is treated as a kernel fault.
//
// cmdline.txt appfault=halt: the old behaviour (halt, the crash record), for debugging.

boolean g_bAppFaultKill = TRUE;			// (kernel.cpp: appfault=)

#define APP_FAULT_STATUS	(-11)		// the exit status a waiter sees (as SIGSEGV)

extern "C" void kapi_exit (int nStatus);	// (sys/kapi.cpp)
extern "C" void AppFaultExit (u64 ulESR, u64 ulFAR, u64 ulPC, u64 ulSPSR, u64 ulLR, u64 ulSP)
	__attribute__ ((noreturn));

#define SPSR_KEEP_BITS	((1ULL << 22) | (1ULL << 12) | (1ULL << 9) | (1ULL << 8))	// PAN SSBS D A
#define SPSR_IF		((1ULL << 7) | (1ULL << 6))
#define SPSR_EL1T	0x4ULL

// IRQ+FIQ masked, on the exception stack: classify, and if it is the app's fault, redirect the
// eret. TRUE: redirected (the caller returns, the eret runs AppFaultExit).
static boolean AppFaultRedirect (TTrapFrame *pFrame, u64 ulESR)
{
	if (!g_bAppFaultKill || !CScheduler::IsActive ())
	{
		return FALSE;
	}
	if ((pFrame->spsr_el1 & 0xF) != SPSR_EL1T)	// EL1h: an IRQ handler / the kernel's own stack
	{
		return FALSE;
	}

	unsigned nEC = (unsigned) (ulESR >> 26) & 0x3F;
	boolean bApp = IS_USER_VA (pFrame->elr_el1);
	if (   !bApp
	    && (nEC == EC_IABORT_SAME || nEC == EC_PC_ALIGN)
	    && IS_USER_VA (pFrame->x[30]))
	{
		bApp = TRUE;				// a branch from app code to a wild address
	}
	if (!bApp)
	{
		return FALSE;
	}

	CScheduler *pSched = CScheduler::Get ();
	CTask *pTask = pSched->GetCurrentTask ();
	if (   pTask == 0
	    || pTask->GetUserData (TASK_USER_DATA_USER) == 0	// a kernel task: never in app code
	    || pSched->InNoKill ())				// holds a kernel resource: a kernel bug
	{
		return FALSE;
	}
	TStackInfo Stack = pTask->GetStack ();
	if (Stack.Size < 0x2000 || Stack.Top >= KERNEL_IDENTITY_END)	// (not a CTask stack?)
	{
		return FALSE;
	}

	// The fault, as AppFaultExit's arguments (x0..x5).
	pFrame->x[0] = ulESR;
	pFrame->x[1] = ReadFAR ();
	pFrame->x[2] = pFrame->elr_el1;
	pFrame->x[3] = pFrame->spsr_el1;
	pFrame->x[4] = pFrame->x[30];
	pFrame->x[5] = pFrame->sp_el0;
	pFrame->x[29] = 0;				// (a clean frame chain: nothing to return to)
	pFrame->x[30] = 0;

	// The rest of the task's life on the top of its own stack: whatever the app left below is
	// dead (the task never goes back to it), and nothing else points into it while it runs.
	pFrame->sp_el0   = Stack.Top & ~(u64) 15;
	pFrame->elr_el1  = (u64) (uintptr) &AppFaultExit;
	// EL1t, IRQ+FIQ masked until AppFaultExit unmasks them (as a new task starts); the app's
	// NZCV, IL, SS, BTYPE dropped.
	pFrame->spsr_el1 = (pFrame->spsr_el1 & SPSR_KEEP_BITS) | SPSR_IF | SPSR_EL1T;
	pFrame->fpsr = 0;				// the kernel's FP state, not the app's modes
	pFrame->fpcr = 0;

	return TRUE;
}

// A short decode of a fault status code (ESR ISS[5:0]: data / instruction aborts).
static const char *FaultStatus (unsigned nFSC, unsigned *pLevel)
{
	*pLevel = nFSC & 3;
	switch (nFSC & 0x3C)
	{
	case 0x00:	return "address size fault, level";
	case 0x04:	return "translation fault (nothing mapped there), level";
	case 0x08:	return "access flag fault, level";
	case 0x0C:	return "permission fault (read-only / not executable), level";
	}
	*pLevel = 4;					// (no level)
	switch (nFSC)
	{
	case 0x10:	return "synchronous external abort";
	case 0x21:	return "alignment fault";
	case 0x30:	return "TLB conflict";
	}
	return "fault status";
}

// The report (kmsg), in the faulting task (task level: the heap may be used).
static void AppFaultLog (u64 ulESR, u64 ulFAR, u64 ulPC, u64 ulSPSR, u64 ulLR, u64 ulSP)
{
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	CAddressSpace *pAS = (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER);
	unsigned nEC = (unsigned) (ulESR >> 26) & 0x3F;
	unsigned nFSC = (unsigned) ulESR & 0x3F;

	CString What;
	unsigned nLevel = 4;
	const char *pStatus = 0;
	boolean bAddress = FALSE;			// FAR is valid (aborts, PC alignment)
	switch (nEC)
	{
	case 0x00:		What = "undefined instruction"; break;
	case 0x0E:		What = "illegal execution state"; break;
	case EC_IABORT_SAME:	pStatus = FaultStatus (nFSC, &nLevel); bAddress = TRUE;
				What = "instruction abort"; break;
	case EC_PC_ALIGN:	What = "PC alignment fault"; bAddress = TRUE; break;
	case EC_DABORT_SAME:	pStatus = FaultStatus (nFSC, &nLevel); bAddress = TRUE;
				What = (ulESR & (1 << 6)) != 0 ? "data abort on a write" : "data abort on a read"; break;
	case 0x26:		What = "SP alignment fault"; break;
	case 0x2C:		What = "floating-point exception"; break;
	case 0x31: case 0x33:
	case 0x35:		What = "debug exception"; break;
	case 0x3C:		What = "BRK (a trap: abort, __builtin_trap)"; break;
	default:		What.Format ("exception class %#x", nEC); break;
	}
	if (pStatus != 0)
	{
		CString Status;
		if (nLevel < 4) Status.Format (", %s %u", pStatus, nLevel);
		else		Status.Format (", %s %#x", pStatus, nFSC);
		What.Append ((const char *) Status);
	}
	CString Address;
	if (bAddress)
	{
		Address.Format (", address %lx (%s)", (unsigned long) ulFAR,
				IS_USER_VA (ulFAR) ? "app space"
				: ulFAR < KERNEL_IDENTITY_END ? "kernel space" : "outside both");
	}

	CLogger::Get ()->Write ("appfault", LogError, "%s (pid %u): %s at pc %lx%s -- process killed",
				pTask->GetName (), pAS != 0 ? pAS->GetPid () : 0, (const char *) What,
				(unsigned long) ulPC, (const char *) Address);
	CLogger::Get ()->Write ("appfault", LogError, "ESR %lx ELR %lx FAR %lx LR %lx SP %lx SPSR %lx",
				(unsigned long) ulESR, (unsigned long) ulPC, (unsigned long) ulFAR,
				(unsigned long) ulLR, (unsigned long) ulSP, (unsigned long) ulSPSR);
}

// Runs in the faulting task (EL1t, its stack top, its address space still active), entered by
// the eret from SyncHandlerEL1 -- never called. Logs the fault, then ends the process.
void AppFaultExit (u64 ulESR, u64 ulFAR, u64 ulPC, u64 ulSPSR, u64 ulLR, u64 ulSP)
{
	asm volatile ("msr daifclr, #3" ::: "memory");	// task level: IRQ+FIQ on (as TaskEntry)

	AppFaultLog (ulESR, ulFAR, ulPC, ulSPSR, ulLR, ulSP);	// (its strings freed on return)

	kapi_exit (APP_FAULT_STATUS);			// the process ends, with its threads
	for (;;) { }					// (not reached)
}

void SyncHandlerEL1 (TTrapFrame *pFrame)
{
#ifdef ARM_ALLOW_MULTI_CORE
	// A fault in an app's job on core 2-3: stop that job, not the machine.
	if (CMultiCoreSupport::ThisCore () != 0 && AppCoreOnFault (pFrame))
	{
		return;
	}
#endif

	u64 ulESR = ReadESR ();
	unsigned nEC = (unsigned) (ulESR >> 26) & 0x3F;

	if (nEC == EC_SVC64)
	{
		// SVC from EL1: used to exercise the syscall path before EL0 exists.
		SyscallEntry (pFrame);
		return;
	}

	// A fault in an app's own code: that app is killed, the system goes on.
	if (AppFaultRedirect (pFrame, ulESR))
	{
		return;
	}

	// Any other synchronous exception at EL1 is a kernel bug.
	DumpAndHalt (EXCEPTION_SYNCHRONOUS, pFrame);
}

void SyncHandlerEL0 (TTrapFrame *pFrame)
{
	unsigned nEC = (unsigned) (ReadESR () >> 26) & 0x3F;

	switch (nEC)
	{
	case EC_SVC64:
		SyscallEntry (pFrame);
		return;

	case EC_IABORT_LOWER:
	case EC_DABORT_LOWER:
		// User fault. No process model yet (#5/#6): we will later deliver a
		// signal / terminate the offending process. For now, dump and halt.
		CLogger::Get ()->Write ("exc", LogError,
					"EL0 abort EC=%#x ELR=%lp FAR=%lp",
					nEC, (void *) pFrame->elr_el1, (void *) ReadFAR ());
		DumpAndHalt (EXCEPTION_SYNCHRONOUS, pFrame);
		return;

	default:
		DumpAndHalt (EXCEPTION_SYNCHRONOUS, pFrame);
		return;
	}
}

void BadModeHandler (TTrapFrame *pFrame)
{
	DumpAndHalt (EXCEPTION_UNEXPECTED, pFrame);
}

// ---- Track A preemption: the timer-IRQ exit redirect ------------------------
//
// Handed to the trampoline (vectors.S) when we preempt an app: its interrupted PC
// and PSTATE, to be restored when it is rescheduled. C linkage so the asm sees them;
// the single-core, IRQ-masked write->read handoff makes plain globals safe (nothing
// else runs between KernelIRQExit writing them and the trampoline reading them).
extern "C" {
	void PreemptTrampoline (void);		// vectors.S
	u64  g_PreemptELR  = 0;
	u64  g_PreemptSPSR = 0;
}

// The trampoline bl's this: the ordinary cooperative switch, but reached at EL1t on
// the preempted app's own SP_EL0 stack -- where Yield's SP_EL0/TTBR0 swap is correct.
extern "C" void PreemptDoYield (void)
{
	CScheduler::Get ()->OnPreempt ();	// the tasks that yield voluntarily go first
	CScheduler::Get ()->Yield ();
}

void KernelIRQExit (TTrapFrame *pFrame)
{
#ifdef ARM_ALLOW_MULTI_CORE
	// Cores 2-3 (app cores) take their IRQs (the stop IPI) through our vectors too; no
	// scheduling there.
	if (CMultiCoreSupport::ThisCore () != 0)
	{
		AppCoreOnIRQExit (pFrame);
		return;
	}
#endif

	// Track-A preemptive reschedule, run at the end of every IRQ. Switch ONLY when a
	// time slice has expired AND the interrupted context was an app running its OWN
	// code -- EL1t (SPSR.M == 0b0100) with a user-VA return PC. The user-VA test IS
	// the kernel lock: an app inside a kapi_* call has a kernel-VA ELR and is left
	// alone (it may hold a kernel resource; those kapis yield cooperatively anyway).
	// Kernel threads (EL1h, or EL1t with a kernel-VA PC) are never preempted.
	if (!CScheduler::IsActive ())
	{
		return;
	}

	CrashLogSample (pFrame);			// (the crash record: where core 0 was)

	// Stall watchdog: where is the current task, if it has not yielded for too long?
	CScheduler::Get ()->StallSample (pFrame->elr_el1, pFrame->x[30]);

	if (!CScheduler::Get ()->IsReschedPending ())
	{
		return;
	}

	if (   (pFrame->spsr_el1 & 0xF) != 0x4		// not EL1t
	    || !IS_USER_VA (pFrame->elr_el1))		// not in the app's own code
	{
		return;
	}

	// Redirect the IRQ return to the trampoline, which performs the real switch at
	// EL1t on the app's stack. Stash the app's PC/PSTATE for it to restore on resume,
	// and mask IRQ+FIQ so the trampoline can't be re-preempted before it parks.
	CScheduler::Get ()->ClearResched ();
	g_PreemptELR     = pFrame->elr_el1;
	g_PreemptSPSR    = pFrame->spsr_el1;
	pFrame->elr_el1  = (u64) &PreemptTrampoline;
	pFrame->spsr_el1 = pFrame->spsr_el1 | (1u << 7) | (1u << 6);	// set I + F

	// IMPORTANT: do NOT log (CLogger::Write) here. It writes to the screen and
	// formats via the heap -- not safe from IRQ context, and doubly unsafe right
	// before this self-induced context switch (a throttled trace here hung the
	// machine on the switch that immediately followed a logged preemption). For
	// observability, bump a plain counter and read it later from a normal task.
}

void PeriodicTick (void)
{
	// Runs inside the timer IRQ (within InterruptHandler), 100 times per second.
	if (CScheduler::IsActive ())
	{
		WordWaitTick ();			// (v68) words changed without a wake (app cores)
		CScheduler::Get ()->OnTimerTick ();	// (after: a "real time" task it woke preempts)
	}
}
