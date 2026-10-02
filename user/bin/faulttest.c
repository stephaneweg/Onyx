//
// faulttest -- fault on purpose in the app's own code, to check that a crashing app is killed
// and the system goes on (kernel/arch/aarch64/exception.cpp, AppFaultRedirect): the terminal
// gets its prompt back, `kmsg` shows an "appfault" report (the name, pid, the decoded fault and
// its registers). With cmdline.txt appfault=halt the machine halts instead (the old behaviour).
//
//   faulttest write | read     a store / a load at an unmapped app address (32 GB)
//   faulttest ro               a store into the kapi table (read-only)
//   faulttest jump             a call to an unmapped app address
//   faulttest wild             a call to a wild address outside the app space (LR is the app's)
//   faulttest pcalign          a branch to a misaligned address
//   faulttest udf | brk        an undefined instruction / a BRK (__builtin_trap)
//   faulttest irqoff           a store at an unmapped address with the IRQs masked
//   faulttest thread           a second thread faults: the whole process must end
//   faulttest post             the fault in a call run by the event pump (kernel frames below it)
//   faulttest memcpy           the kapi table's memcpy given a bad pointer (the app is killed)
//   faulttest null             a store at address 0: killed with cmdline.txt nullguard=1 only
//                              (else the kernel's identity map covers page 0 for an EL1 app)
//   faulttest kapi             NOT a fault: bad pointers handed to kapis (kern/uaccess.h) -- each
//                              call must fail with its error value, the process lives on and
//                              prints a PASS / FAIL line per check
//
// MIT licence (Onyx).
//
#include "kapi.h"
#include "applib.h"

#define BAD_APP_VA	0x800000000UL		// 32 GB: in the app space, never mapped
#define WILD_VA		0xDEAD00000000UL	// beyond the 64 GB translation range

static void Store (unsigned long ulAddr)
{
	*(volatile unsigned long *) ulAddr = 0x0BADC0DEUL;
}

static void Call (unsigned long ulAddr)
{
	// blr (not a tail call): LR = the app's return address
	asm volatile ("blr %0" :: "r" (ulAddr) : "x30", "memory");
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
	const char *pKernel = (const char *) 0x80000UL;	// the kernel's image (not the app's)
	char Local[64];						// a legacy app's stack: kernel memory

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
	else if (ax_streq (m, "read"))	(void) *(volatile unsigned long *) BAD_APP_VA;
	else if (ax_streq (m, "ro"))	Store ((unsigned long) KT);
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
	else if (ax_streq (m, "irqoff"))
	{
		asm volatile ("msr daifset, #3" ::: "memory");
		Store (BAD_APP_VA);
		asm volatile ("msr daifclr, #3" ::: "memory");
	}
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
	else if (ax_streq (m, "null"))	Store (0);
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

	static const char *const Modes[] = { "write", "read", "ro", "jump", "wild", "pcalign", "udf",
					     "brk", "irqoff", "thread", "post", "memcpy", "null" };
	int bKnown = 0;
	for (unsigned k = 0; k < sizeof Modes / sizeof Modes[0]; k++) bKnown |= ax_streq (m, Modes[k]);
	if (!bKnown)
	{
		ax_putln ("usage: faulttest write|read|ro|jump|wild|pcalign|udf|brk|irqoff|thread|post|memcpy|null");
		ax_putln ("       faulttest kapi   (bad pointers to kapis: each must fail cleanly)");
		ax_putln ("faults on purpose: this process should be killed (see kmsg), the system go on");
		return 1;
	}

	ax_puts ("faulttest: ");
	ax_puts (m);
	ax_putln (" -- this process should now be killed (kmsg: appfault)");
	kapi_msleep (300);				// (the line out to the terminal / telnet)
	Fault (m);
	ax_putln ("faulttest: BUG: still alive after the fault");
	return 2;
}
