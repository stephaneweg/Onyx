//
// faulttest -- fault on purpose in the app's own code, to check that a crashing app is killed
// and the system goes on. Every app runs at EL0 (protected mode, kern/el0.h): any synchronous
// exception but a system call kills the process (kernel/sys/el0.cpp) -- the terminal gets its
// prompt back, kmsg shows "el0: faulttest (pid N) killed: <the fault> at pc ... (address ...,
// ESR ...)" and the desktop an "Application error" notice. The fault expected is in brackets.
//
//   faulttest write | read     a store / a load at an unmapped app address, 32 GB (a data abort)
//   faulttest ro               a store into the kapi table, read-only at EL0 (a data abort)
//   faulttest kernel           a load from the kernel's image, 0x80000: the identity map is
//                              EL1-only (a data abort)
//   faulttest mmio             a load from a peripheral, the system timer at 0xFE003004: no device
//                              is reachable from EL0 (a data abort)
//   faulttest null             a store at address 0: page 0 is the kernel's, EL1-only (a data
//                              abort, whatever cmdline.txt nullguard says)
//   faulttest jump             a call to an unmapped app address (an instruction abort)
//   faulttest wild             a call to a wild address outside the app space (an instruction abort)
//   faulttest pcalign          a branch to a misaligned address (a misaligned PC)
//   faulttest udf | brk        an undefined instruction / a BRK, __builtin_trap (an undefined
//                              instruction / a breakpoint)
//   faulttest irqoff           an attempt to mask the IRQs, msr daifset: EL0 may not (a privileged
//                              system register) -- the store after it is never reached
//   faulttest sysreg           a read of an EL1 register, mrs sctlr_el1 (an undefined instruction)
//   faulttest thread           a second thread faults: the whole process must end (a data abort)
//   faulttest post             the fault in a call run by the event pump, user-side at EL0 (a
//                              data abort)
//   faulttest memcpy           the kapi table's memcpy given a bad pointer: at EL0 it is user code,
//                              in the page next to the table, KAPI_STUBS_VA (a data abort, pc there)
//   faulttest kapi             NOT a fault: bad pointers handed to kapis (kern/uaccess.h) -- each
//                              call must fail with its error value, the process lives on and
//                              prints a PASS / FAIL line per check
//
// (The functions named *_el0scan_expected hold the privileged instructions on purpose:
// tools/el0scan.sh does not report them.)
//
// MIT licence (Onyx).
//
#define KAPI_INLINE			// (a test of the kernel's table itself: read here, not through AppKit)
#include "appkit/appkit.h"

#define BAD_APP_VA	0x800000000UL		// 32 GB: in the app space, never mapped
#define WILD_VA		0xDEAD00000000UL	// beyond the 64 GB translation range
#define KERNEL_VA	0x80000UL		// the kernel's image (identity map, EL1-only)
#define MMIO_VA		0xFE003004UL		// the system timer's counter (identity map, EL1-only)

// (the address through a register: the compiler may not see a constant -- a store to 0 would
// become a trap of its own)
static unsigned long Hide (unsigned long ulAddr)
{
	asm volatile ("" : "+r" (ulAddr));
	return ulAddr;
}

static void Store (unsigned long ulAddr)
{
	*(volatile unsigned long *) Hide (ulAddr) = 0x0BADC0DEUL;
}

static unsigned long Load (unsigned long ulAddr)
{
	return *(volatile unsigned long *) Hide (ulAddr);
}

static void Call (unsigned long ulAddr)
{
	// blr (not a tail call): LR = the app's return address
	asm volatile ("blr %0" :: "r" (ulAddr) : "x30", "memory");
}

static void __attribute__ ((noinline)) IrqOff_el0scan_expected (void)
{
	asm volatile ("msr daifset, #3" ::: "memory");
	Store (BAD_APP_VA);				// (never reached: the msr kills the process)
	asm volatile ("msr daifclr, #3" ::: "memory");
}

static unsigned long __attribute__ ((noinline)) SysReg_el0scan_expected (void)
{
	unsigned long ul;
	asm volatile ("mrs %0, sctlr_el1" : "=r" (ul));
	return ul;
}

static int FaultThread (void *pArg)
{
	(void) pArg;
	kapi_msleep (300);
	Store (BAD_APP_VA);
	return 0;
}

static void FaultPost (void *pCtx, long lValue)
{
	(void) pCtx; (void) lValue;
	Store (BAD_APP_VA);
}

// ---- faulttest kapi: the kapis check the pointers they are given ----------------------------

static int s_nFail = 0;

static void Expect (const char *pWhat, int bOK)
{
	ax_puts (bOK ? "PASS  " : "FAIL  ");
	ax_putln (pWhat);
	if (!bOK) s_nFail++;
}

static int KapiChecks (void)
{
	const char *pBad = (const char *) BAD_APP_VA;		// in the app range, not mapped
	const char *pKernel = (const char *) KERNEL_VA;		// the kernel's image (not the app's)
	char Local[64];						// on the app's own (EL0) stack

	Expect ("open (unmapped path) -> 0", KT->open (pBad) == 0);
	Expect ("open (kernel path) -> 0", KT->open (pKernel) == 0);
	Expect ("exec (unmapped path) -> 0", KT->exec (pBad, 0) == 0);
	Expect ("getcwd (unmapped buffer) -> 0", KT->getcwd ((char *) pBad, 64) == 0);
	Expect ("getcwd (kernel buffer) -> 0", KT->getcwd ((char *) pKernel, 64) == 0);
	Expect ("getcwd (a local buffer) works", KT->getcwd (Local, sizeof Local) > 0 && Local[0] != '\0');
	Expect ("random (into the read-only kapi table) -> 0", KT->random ((void *) KT, 16) == 0);
	Expect ("random (a local buffer) works", KT->random (Local, 16) == 16);
	Expect ("save_file (unmapped data) -> -1", KT->save_file ("SD:/faulttest.tmp", pBad, 16) == -1);
	Expect ("clipboard_set (unmapped data) -> 0", KT->clipboard_set (1, pBad, 16) == 0);
	Expect ("stdout_write (unmapped data) -> -1", KT->stdout_write (pBad, 16) == -1);
	Expect ("tcp_send (kernel data) -> -1", KT->tcp_send (0, pKernel, 16) == -1);
	Expect ("wait_word (kernel word) -> -1", KT->wait_word ((volatile unsigned *) pKernel, 0, 0) == -1);
	Expect ("get_chrome (unmapped) -> 0", KT->get_chrome ((struct kapi_chrome *) pBad) == 0);
	KT->screen_size ((int *) pBad, (int *) pKernel);	// (void: skipped, no crash)
	KT->get_datetime ((int *) pBad, 0, 0, 0, 0, (int *) pKernel);
	void *h = KT->open ("SD:/cmdline.txt");
	if (h != 0)
	{
		Expect ("read (into an unmapped buffer) -> -1", KT->read (h, (void *) pBad, 64) == -1);
		Expect ("read (into the kernel) -> -1", KT->read (h, (void *) pKernel, 64) == -1);
		Expect ("read (a local buffer) works", KT->read (h, Local, sizeof Local) > 0);
		KT->close (h);
	}
	ax_putln (s_nFail == 0 ? "faulttest kapi: all checks passed" : "faulttest kapi: SOME CHECKS FAILED");
	return s_nFail == 0 ? 0 : 3;
}

static void Fault (const char *m)
{
	if (ax_streq (m, "write"))	Store (BAD_APP_VA);
	else if (ax_streq (m, "read"))	(void) Load (BAD_APP_VA);
	else if (ax_streq (m, "ro"))	Store ((unsigned long) KT);
	else if (ax_streq (m, "kernel")) (void) Load (KERNEL_VA);
	else if (ax_streq (m, "mmio"))	(void) Load (MMIO_VA);
	else if (ax_streq (m, "null"))	Store (0);
	else if (ax_streq (m, "jump"))	Call (BAD_APP_VA);
	else if (ax_streq (m, "wild"))	Call (WILD_VA);
	else if (ax_streq (m, "pcalign"))
	{
		unsigned long ulTarget;
		asm volatile ("adr %0, 1f\n" "1:" : "=r" (ulTarget));
		Call (ulTarget + 2);
	}
	else if (ax_streq (m, "udf"))	asm volatile ("udf #0" ::: "memory");
	else if (ax_streq (m, "brk"))	__builtin_trap ();
	else if (ax_streq (m, "irqoff")) IrqOff_el0scan_expected ();
	else if (ax_streq (m, "sysreg")) (void) SysReg_el0scan_expected ();
	else if (ax_streq (m, "thread"))
	{
		if (kapi_thread_create (FaultThread, 0, 0, "faulter") < 2)
		{
			ax_putln ("faulttest: no thread");
			return;
		}
		for (int i = 0; i < 30; i++)		// 3 s: the thread faults after 0.3 s
		{
			ax_putln ("main thread still running...");
			kapi_msleep (100);
		}
	}
	else if (ax_streq (m, "post"))
	{
		kapi_post (FaultPost, 0, 0);
		kapi_pump_events ();
	}
	else if (ax_streq (m, "memcpy"))
	{
		static char Src[64];
		KT->memcpy ((void *) BAD_APP_VA, Src, sizeof Src);
	}
}

int main (void)
{
	char a[64]; kapi_get_args (a, sizeof a);
	int i = 0; while (a[i] == ' ') i++;
	char m[16]; int n = 0;
	while (a[i] > ' ' && n < (int) sizeof m - 1) m[n++] = a[i++];
	m[n] = '\0';

	if (ax_streq (m, "kapi"))
	{
		return KapiChecks ();
	}

	static const char *const Modes[] = { "write", "read", "ro", "kernel", "mmio", "null", "jump", "wild",
					     "pcalign", "udf", "brk", "irqoff", "sysreg", "thread", "post", "memcpy" };
	int bKnown = 0;
	for (unsigned k = 0; k < sizeof Modes / sizeof Modes[0]; k++) bKnown |= ax_streq (m, Modes[k]);
	if (!bKnown)
	{
		ax_putln ("usage: faulttest write|read|ro|kernel|mmio|null|jump|wild|pcalign|udf|brk|irqoff|sysreg|thread|post|memcpy");
		ax_putln ("       faulttest kapi   (bad pointers to kapis: each must fail cleanly)");
		ax_putln ("faults on purpose: this process should be killed (kmsg: el0 ... killed), the system go on");
		return 1;
	}

	ax_puts ("faulttest: ");
	ax_puts (m);
	ax_putln (" -- this process should now be killed (kmsg: el0: faulttest ... killed)");
	kapi_msleep (300);				// (the line out to the terminal / telnet)
	Fault (m);
	ax_putln ("faulttest: BUG: still alive after the fault");
	return 2;
}
