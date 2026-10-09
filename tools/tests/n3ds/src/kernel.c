/*
 * kernel.c -- the 3DS core's first test program (phase T1): the processor in a real program (ARM, Thumb, VFP,
 * 64-bit helpers, LDREX / STREX, the TLS register) and the emulated kernel -- the heap, threads and their
 * priorities, mutexes, events, semaphores, waits with a time-out, sleeps, address arbiters, handles.
 * It checks itself: "<n> checks, 0 failed" at the end (tools/tests/n3ds/check.sh).
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include "sys.h"

#define TICKS_PER_MS	268111u
#define FOREVER		(-1ll)

static char s_log[64]; static int s_logLen;
static void logc (char c) { if (s_logLen < 63) { s_log[s_logLen++] = c; s_log[s_logLen] = 0; } }
static void logReset (void) { s_logLen = 0; s_log[0] = 0; }
static int logIs (const char *want)
{
	int i = 0;
	while (want[i] && s_log[i] == want[i]) i++;
	return want[i] == 0 && s_log[i] == 0;
}
static void checkLog (const char *what, const char *want)
{
	if (logIs (want)) { check (what, 1); return; }
	check (what, 0);
	print ("     the order was \""); print (s_log); print ("\", expected \""); print (want); print ("\"\n");
}

/* threads' stacks: pieces of the heap */
static u32 s_stackNext;
static Handle spawn (void (*fn) (void *), u32 arg, s32 priority)
{
	Handle h = 0;
	s_stackNext += 0x1000;
	Result r = svcCreateThread (&h, fn, arg, (u32 *) s_stackNext, priority, -2);
	checkEq ("CreateThread", r, 0);
	return h;
}
static void join (Handle h)
{
	checkEq ("wait for a thread's end", svcWaitSynchronization (h, FOREVER), 0);
	checkEq ("close a thread", svcCloseHandle (h), 0);
}

/* ---- the processor ---- */
static volatile float s_fa = 1.5f, s_fb = 2.25f;
static volatile double s_da = 1e10, s_db = 3.0;
static volatile u64 s_big = 3000000021ull;
static volatile s32 s_counter;

static void testCpu (void)
{
	section ("processor");
	checkEq ("a Thumb function", thumbSum (100), 5050);
	checkEq ("a 64-bit division", (u32) (s_big / 7), 428571431);
	checkEq ("a 64-bit remainder", (u32) (s_big % 1000), 21);
	checkEq ("float multiply", (u32) (s_fa * s_fb * 8.0f), 27);
	checkEq ("double divide", (u32) (s_da / s_db), 3333333333u);
	checkEq ("float to int, negative", (u32) (s32) (s_fa - s_fb * 4.0f), (u32) -7);
	s_counter = 5;
	checkEq ("an atomic add", (u32) atomicAdd ((s32 *) &s_counter, 37), 42);
	checkEq ("... stored", (u32) s_counter, 42);
	check ("the TLS register", getTls () != 0 && ((u32) getTls () & 0x1FF) == 0);
	getTls ()[4] = 0x12345678;
	checkEq ("the TLS is memory", getTls ()[4], 0x12345678);
}

/* ---- memory ---- */
static void testMemory (void)
{
	u32 a = 0;
	section ("memory");
	checkEq ("a heap", svcControlMemory (&a, 0x08000000, 0, 0x20000, MEMOP_ALLOC, MEMPERM_RW), 0);
	checkEq ("... where asked", a, 0x08000000);
	u32 *p = (u32 *) a;
	for (u32 i = 0; i < 0x8000; i++) p[i] = i * 2654435761u;
	u32 bad = 0;
	for (u32 i = 0; i < 0x8000; i++) if (p[i] != i * 2654435761u) bad++;
	checkEq ("... written and read back", bad, 0);
	s_stackNext = a;						/* the first 0x10000: the threads' stacks */
	checkEq ("more heap, anywhere", svcControlMemory (&a, 0, 0, 0x1000, MEMOP_ALLOC, MEMPERM_RW), 0);
	checkEq ("... after the first", a, 0x08020000);
	checkEq ("... zeroed", *(u32 *) a, 0);
	check ("a heap over another is refused", svcControlMemory (&a, 0x08000000, 0, 0x1000, MEMOP_ALLOC, MEMPERM_RW) != 0);
	check ("a size that is not pages is refused", svcControlMemory (&a, 0, 0, 0x1234, MEMOP_ALLOC, MEMPERM_RW) != 0);
	checkEq ("linear memory", svcControlMemory (&a, 0, 0, 0x4000, MEMOP_ALLOC | MEMOP_LINEAR, MEMPERM_RW), 0);
	checkEq ("... at the linear heap's start", a, 0x14000000);
	*(u32 *) (a + 0x3FFC) = 0xCAFEF00D;
	checkEq ("... written", *(u32 *) (a + 0x3FFC), 0xCAFEF00D);
	checkEq ("more linear memory", svcControlMemory (&a, 0, 0, 0x1000, MEMOP_ALLOC | MEMOP_LINEAR, MEMPERM_RW), 0);
	checkEq ("... right after", a, 0x14004000);
}

/* ---- threads ---- */
static Handle s_mutex;
static u32 s_tls[4]; static u32 s_ids[4];

static void worker (void *arg)
{
	u32 n = (u32) arg;
	svcWaitSynchronization (s_mutex, FOREVER);
	logc ((char) ('a' + n));
	s_tls[n] = (u32) getTls ();
	svcGetThreadId (&s_ids[n], CUR_THREAD);
	svcReleaseMutex (s_mutex);
	svcExitThread ();
}
static void shout (void *arg) { logc ((char) (u32) arg); svcExitThread (); }
static void yielder (void *arg)
{
	for (int i = 0; i < 3; i++) { logc ((char) (u32) arg); svcSleepThread (0); }
	svcExitThread ();
}

static void testThreads (void)
{
	Handle h[3]; s32 prio = 0; u32 id = 0;
	section ("threads");
	checkEq ("a mutex", svcCreateMutex (&s_mutex, 0), 0);
	checkEq ("the main thread's priority", svcGetThreadPriority (&prio, CUR_THREAD), 0);
	checkEq ("... 0x30", (u32) prio, 0x30);
	checkEq ("the main thread's id", svcGetThreadId (&id, CUR_THREAD), 0);
	s_tls[3] = (u32) getTls (); s_ids[3] = id;
	logReset ();
	for (u32 i = 0; i < 3; i++) h[i] = spawn (worker, i, 0x31);	/* lower than the main thread: they wait for it */
	checkLog ("lower priorities do not run before the main thread waits", "");
	for (int i = 0; i < 3; i++) join (h[i]);
	checkLog ("one priority's threads run in their order", "abc");
	int distinct = 1;
	for (int i = 0; i < 4; i++) for (int k = i + 1; k < 4; k++) if (s_tls[i] == s_tls[k] || s_ids[i] == s_ids[k] || !s_tls[i]) distinct = 0;
	check ("each thread has its TLS and its id", distinct);

	logReset ();
	Handle hi = spawn (shout, '!', 0x2F);				/* higher: it runs at once */
	checkLog ("a higher priority runs at its creation", "!");
	join (hi);

	logReset ();
	Handle y1 = spawn (yielder, 'x', 0x31), y2 = spawn (yielder, 'y', 0x31);
	join (y1); join (y2);
	checkLog ("threads that yield take turns", "xyxyxy");

	check ("waiting on a closed handle is refused", svcWaitSynchronization (hi, 0) == RES_INVALID_HANDLE);
	check ("closing a handle twice is refused", svcCloseHandle (hi) == RES_INVALID_HANDLE);
}

/* ---- mutexes ---- */
static void taker (void *arg)
{
	logc ('1');
	svcWaitSynchronization (s_mutex, FOREVER);
	logc ((char) (u32) arg);
	svcReleaseMutex (s_mutex);
	svcExitThread ();
}
static Result s_stolen;
static void thief (void *arg) { (void) arg; s_stolen = svcReleaseMutex (s_mutex); svcExitThread (); }

static void testMutex (void)
{
	section ("mutexes");
	checkEq ("take a free mutex", svcWaitSynchronization (s_mutex, 0), 0);
	checkEq ("... again, by its owner", svcWaitSynchronization (s_mutex, 0), 0);
	logReset ();
	Handle t = spawn (taker, '2', 0x2F);				/* runs at once, then waits for the mutex */
	checkLog ("a thread waits for a held mutex", "1");
	checkEq ("release once", svcReleaseMutex (s_mutex), 0);
	checkLog ("... still held (taken twice)", "1");
	checkEq ("release twice", svcReleaseMutex (s_mutex), 0);
	checkLog ("... the waiter got it", "12");
	join (t);
	check ("releasing a mutex one does not hold is refused", svcReleaseMutex (s_mutex) == RES_NOT_OWNER);
	svcWaitSynchronization (s_mutex, 0);
	Handle th = spawn (thief, 0, 0x2F);
	join (th);
	check ("... nor another thread's", s_stolen == RES_NOT_OWNER);
	svcReleaseMutex (s_mutex);
}

/* ---- events, time ---- */
static Handle s_event;
static void waiter (void *arg)
{
	Result r = svcWaitSynchronization (s_event, FOREVER);
	logc (r == 0 ? (char) (u32) arg : '?');
	svcExitThread ();
}
static void sleeper (void *arg)
{
	svcSleepThread ((s64) (u32) arg * NS_MS);
	logc ('s');
	svcSignalEvent (s_event);
	svcExitThread ();
}

static void testEvents (void)
{
	section ("events and time");
	checkEq ("a one-shot event", svcCreateEvent (&s_event, 0), 0);
	check ("not signalled: a wait of no time ends at once", svcWaitSynchronization (s_event, 0) == RES_TIMEOUT);
	logReset ();
	Handle a = spawn (waiter, 'a', 0x2F), b = spawn (waiter, 'b', 0x2F);
	checkLog ("two threads wait for it", "");
	svcSignalEvent (s_event);
	checkLog ("a signal wakes one", "a");
	svcSignalEvent (s_event);
	checkLog ("... the next one the other", "ab");
	join (a); join (b);
	check ("... and the event is taken", svcWaitSynchronization (s_event, 0) == RES_TIMEOUT);
	svcSignalEvent (s_event);
	checkEq ("signalled before the wait", svcWaitSynchronization (s_event, 0), 0);
	svcCloseHandle (s_event);

	checkEq ("a sticky event", svcCreateEvent (&s_event, 1), 0);
	logReset ();
	a = spawn (waiter, 'a', 0x2F); b = spawn (waiter, 'b', 0x2F);
	svcSignalEvent (s_event);
	checkLog ("a signal wakes all", "ab");
	join (a); join (b);
	checkEq ("... and it stays signalled", svcWaitSynchronization (s_event, 0), 0);
	svcClearEvent (s_event);
	check ("... until cleared", svcWaitSynchronization (s_event, 0) == RES_TIMEOUT);
	svcCloseHandle (s_event);

	svcCreateEvent (&s_event, 0);
	u64 t0 = svcGetSystemTick ();
	check ("a wait of 5 ms on nothing times out", svcWaitSynchronization (s_event, 5 * NS_MS) == RES_TIMEOUT);
	u32 dt = (u32) (svcGetSystemTick () - t0);
	check ("... after 5 ms", dt >= 5 * TICKS_PER_MS && dt < 8 * TICKS_PER_MS);
	t0 = svcGetSystemTick ();
	svcSleepThread (20 * NS_MS);
	dt = (u32) (svcGetSystemTick () - t0);
	check ("a sleep of 20 ms", dt >= 20 * TICKS_PER_MS && dt < 23 * TICKS_PER_MS);

	logReset ();
	Handle s = spawn (sleeper, 10, 0x31);				/* signals the event in 10 ms */
	t0 = svcGetSystemTick ();
	checkEq ("a wait ended by a thread that slept", svcWaitSynchronization (s_event, 500 * NS_MS), 0);
	dt = (u32) (svcGetSystemTick () - t0);
	check ("... after its 10 ms, not the wait's 500", dt >= 10 * TICKS_PER_MS && dt < 13 * TICKS_PER_MS);
	checkLog ("... which ran", "s");
	join (s);
	svcCloseHandle (s_event);
}

/* ---- semaphores, several objects, arbiters ---- */
static Handle s_sem, s_arbiter;
static volatile s32 s_word;
static void semWaiter (void *arg) { svcWaitSynchronization (s_sem, FOREVER); logc ((char) (u32) arg); svcExitThread (); }
static void arbWaiter (void *arg)
{
	Result r = svcArbitrateAddress (s_arbiter, (u32) &s_word, 1, 1, 0);	/* wait while the word is less than 1 */
	logc (r == 0 ? (char) (u32) arg : '?');
	svcExitThread ();
}

static void testOthers (void)
{
	s32 before = -1, index = -1;
	section ("semaphores, several objects, arbiters");
	checkEq ("a semaphore", svcCreateSemaphore (&s_sem, 0, 2), 0);
	logReset ();
	Handle a = spawn (semWaiter, 'a', 0x2F), b = spawn (semWaiter, 'b', 0x2F), c = spawn (semWaiter, 'c', 0x2F);
	checkEq ("release two", svcReleaseSemaphore (&before, s_sem, 2), 0);
	checkEq ("... it was at 0", (u32) before, 0);
	checkLog ("... two of three waiters go", "ab");
	check ("releasing over its maximum is refused", svcReleaseSemaphore (&before, s_sem, 3) != 0);
	svcReleaseSemaphore (&before, s_sem, 1);
	checkLog ("... the third", "abc");
	join (a); join (b); join (c);
	svcCloseHandle (s_sem);

	Handle ev[2];
	svcCreateEvent (&ev[0], 0); svcCreateEvent (&ev[1], 0);
	check ("any of two events, none signalled", svcWaitSynchronizationN (&index, ev, 2, 0, 0) == RES_TIMEOUT);
	svcSignalEvent (ev[1]);
	checkEq ("any of two events", svcWaitSynchronizationN (&index, ev, 2, 0, 0), 0);
	checkEq ("... the second", (u32) index, 1);
	svcSignalEvent (ev[0]);
	check ("both events, one signalled", svcWaitSynchronizationN (&index, ev, 2, 1, 0) == RES_TIMEOUT);
	svcSignalEvent (ev[1]);
	checkEq ("both events", svcWaitSynchronizationN (&index, ev, 2, 1, 0), 0);
	check ("... both taken", svcWaitSynchronizationN (&index, ev, 2, 0, 0) == RES_TIMEOUT);
	svcCloseHandle (ev[0]); svcCloseHandle (ev[1]);

	checkEq ("an address arbiter", svcCreateAddressArbiter (&s_arbiter), 0);
	s_word = 0;
	logReset ();
	a = spawn (arbWaiter, 'a', 0x2F); b = spawn (arbWaiter, 'b', 0x2F);
	checkLog ("two threads wait on a word", "");
	s_word = 1;
	checkEq ("signal one", svcArbitrateAddress (s_arbiter, (u32) &s_word, 0, 1, 0), 0);
	checkLog ("... one goes", "a");
	checkEq ("signal all", svcArbitrateAddress (s_arbiter, (u32) &s_word, 0, -1, 0), 0);
	checkLog ("... the other", "ab");
	join (a); join (b);
	a = spawn (arbWaiter, 'c', 0x2F);
	checkLog ("the word is not less: no wait", "abc");
	join (a);
	s_word = 0;
	check ("a timed wait on a word", svcArbitrateAddress (s_arbiter, (u32) &s_word, 3, 1, 2 * NS_MS) == RES_TIMEOUT);
	svcCloseHandle (s_arbiter);
}

int main (void)
{
	print ("3DS core: kernel test\n");
	testCpu ();
	testMemory ();
	testThreads ();
	testMutex ();
	testEvents ();
	testOthers ();
	summary ();
	return 0;
}
