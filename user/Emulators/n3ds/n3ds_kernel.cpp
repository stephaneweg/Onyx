//
// n3ds/n3ds_kernel.cpp -- the 3DS's kernel, emulated: the handle table, the threads and their scheduler (the
// application core: a thread runs until it waits or a thread of a higher priority is ready; one priority's
// threads take turns when they yield), events, mutexes, semaphores, address arbiters, waits with a time-out,
// timers, shared memory blocks, the heap (ControlMemory), and the system calls a program makes (SVC n: arguments
// in r0-r5, the result in r0, what it returns in r1...). A request to a service (SendSyncRequest) is answered at
// once by the service's function (n3ds_ipc.cpp).
//
// A thread that is not running has its registers in Thread::ctx; the running one in the Cpu (`current`). A
// thread that starts to wait is saved at once and `current` cleared: what wakes it later writes its results
// into ctx.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

static const u64 NEVER = ~0ull;

Machine::Machine ()
{
	memset (&mem, 0, sizeof mem);
	cpu = 0; now = 0; ticksLeft = 0;
	memset (handles, 0, sizeof handles);
	memset (threads, 0, sizeof threads); threadCount = 0;
	current = 0; nextThreadId = 1; nextSeq = 1;
	resched = false; exited = false; exitCode = 0; heapSize = 0; entry = 0;
	lastError[0] = 0; notes[0] = 0; debugOut = 0; debugUser = 0;
	memset (timers, 0, sizeof timers); timerCount = 0;
	memset (&gsp, 0, sizeof gsp);
	memset (&apt, 0, sizeof apt); apt.cpuLimit = 30;
	memset (&hid, 0, sizeof hid);
	memset (&source, 0, sizeof source); romfsBase = 0; romfsSize = 0; memFile = 0; memSize = 0; trace = false; traceGpu = false; gpuSkip = 0; pica = 0;
	title[0] = 0; productCode[0] = 0;
	parallelBegin = 0; parallelDone = 0; parallelEnd = 0; parallelUser = 0; helpers = 0; monoOnly = false;
	memset (&dsp, 0, sizeof dsp);
	storage = 0; storageDirty = false; memset (archives, 0, sizeof archives);
	memset (&user, 0, sizeof user); setUser ("Onyx", 1, 2);
	svcCount = switchCount = 0; unknownSvcs = 0;
}

Machine::~Machine ()
{
	delete cpu;
	picaFree (this);
	storageFree (this);
	release (gsp.irq); release (gsp.shared);
	release (apt.lock); release (apt.signal); release (apt.param); release (apt.font);
	release (dsp.interrupt); release (dsp.semaphore);
	release (hid.shared); for (int i = 0; i < 5; i++) release (hid.events[i]);
	for (int i = 0; i < HANDLE_MAX; i++) if (handles[i]) release (handles[i]);
	for (int i = 0; i < threadCount; i++) release (threads[i]);
	mem.quit ();
}

bool Machine::init ()
{
	if (!mem.init ()) return false;
	u8 *cfg = mem.allocTop (2 * PAGE_SIZE);				// the configuration and shared pages
	u8 *tls = mem.allocTop (TLS_MAX * TLS_SIZE);
	if (!cfg || !tls) return false;
	mem.map (VA_CONFIG, PAGE_SIZE, cfg, PERM_R);
	mem.map (VA_SHARED, PAGE_SIZE, cfg + PAGE_SIZE, PERM_R);
	mem.map (VA_TLS, TLS_MAX * TLS_SIZE, tls, PERM_RW);
	dsp.ram = mem.allocTop (DSP_RAM_SIZE);
	if (!dsp.ram) return false;
	mem.map (VA_DSP_RAM, DSP_RAM_SIZE, dsp.ram, PERM_RW);
	// the configuration page: the kernel's and the firmware's versions (11.x), a retail unit, the memory's shares
	cfg[0x02] = 57; cfg[0x03] = 2;					// kernel 2.57
	cfg[0x10] = 2;							// SYSCOREVER
	cfg[0x14] = 1;							// UNITINFO: retail
	const u32 appMem = 0x04000000, sysMem = 0x02C00000, baseMem = 0x01400000;
	memcpy (cfg + 0x40, &appMem, 4); memcpy (cfg + 0x44, &sysMem, 4); memcpy (cfg + 0x48, &baseMem, 4);
	cfg[0x62] = 57; cfg[0x63] = 2; cfg[0x64] = 2;			// FIRM 2.57, its SYSCOREVER
	const u32 sdk = 0x0000F297; memcpy (cfg + 0x68, &sdk, 4);
	// the shared page: a product unit, the date (ms since 1900: 2026-10-09), the 3D slider down
	u8 *sh = cfg + PAGE_SIZE;
	sh[0x04] = 1;
	const u64 date = 4000492800000ull; memcpy (sh + 0x20, &date, 8); memcpy (sh + 0x40, &date, 8);
	cpu = Cpu::create (this);
	return cpu != 0;
}

void Machine::fail (const char *fmt, ...)
{
	if (!lastError[0])
	{
		va_list ap; va_start (ap, fmt);
		vsnprintf (lastError, sizeof lastError, fmt, ap);
		va_end (ap);
	}
	exited = true; exitCode = 0xFFFFFFFF;
	if (cpu) cpu->halt ();
}

// Something the program asked for that is not emulated: kept (once each) for whoever looks at a game that fails.
void Machine::note (const char *fmt, ...)
{
	char one[96];
	va_list ap; va_start (ap, fmt);
	vsnprintf (one, sizeof one, fmt, ap);
	va_end (ap);
	if (strstr (notes, one)) return;
	size_t n = strlen (notes), k = strlen (one);
	if (n + k + 3 > sizeof notes) return;
	if (n) { memcpy (notes + n, "; ", 2); n += 2; }
	memcpy (notes + n, one, k + 1);
}

Session::~Session () { free (path); }

Timer::Timer (Machine *machine, int r) : Object (OBJ_TIMER), m (machine), reset (r), signaled (false), fireTick (~0ull), interval (0)
{
	if (m->timerCount < (int) TIMER_MAX) m->timers[m->timerCount++] = this;
}
Timer::~Timer ()
{
	for (int i = 0; i < m->timerCount; i++)
		if (m->timers[i] == this) { m->timers[i] = m->timers[--m->timerCount]; break; }
}

u64 Machine::nsToTicks (s64 ns) const
{
	u64 n = (u64) ns;
	return n / 1000000000ull * TICKS_PER_SECOND + n % 1000000000ull * TICKS_PER_SECOND / 1000000000ull;
}

// ---- handles ---------------------------------------------------------------------------------------------------------
enum { HANDLE_BASE = 0x10 };

u32 Machine::handleNew (Object *o)
{
	for (int i = 0; i < HANDLE_MAX; i++) if (!handles[i]) { handles[i] = o; return (u32) (HANDLE_BASE + i); }
	release (o);
	return 0;
}

Object *Machine::handleGet (u32 h, int type)
{
	Object *o = 0;
	if (h == HANDLE_CUR_THREAD) o = current;
	else if (h >= HANDLE_BASE && h < HANDLE_BASE + HANDLE_MAX) o = handles[h - HANDLE_BASE];
	if (o && type && o->type != type) return 0;
	return o;
}

bool Machine::handleClose (u32 h)
{
	if (h < HANDLE_BASE || h >= HANDLE_BASE + HANDLE_MAX || !handles[h - HANDLE_BASE]) return false;
	Object *o = handles[h - HANDLE_BASE];
	handles[h - HANDLE_BASE] = 0;
	release (o);
	return true;
}

void Machine::release (Object *o) { if (o && --o->refs == 0) delete o; }

// ---- threads ---------------------------------------------------------------------------------------------------------
Thread *Machine::threadNew (u32 entryPoint, u32 arg, u32 stackTop, s32 priority)
{
	if (threadCount >= (int) TLS_MAX) return 0;
	bool used[TLS_MAX]; memset (used, 0, sizeof used);
	for (int i = 0; i < threadCount; i++) if (threads[i]->tlsSlot >= 0) used[threads[i]->tlsSlot] = true;
	int slot = 0; while (slot < (int) TLS_MAX && used[slot]) slot++;
	if (slot == (int) TLS_MAX) return 0;
	Thread *t = new Thread;
	t->id = nextThreadId++; t->priority = priority; t->readySeq = nextSeq++; t->tlsSlot = slot;
	memset (&t->ctx, 0, sizeof t->ctx);
	t->ctx.r[0] = arg; t->ctx.r[13] = stackTop & ~7u; t->ctx.r[15] = entryPoint & ~1u;
	t->ctx.cpsr = 0x10 | ((entryPoint & 1) ? 0x20 : 0);		// user mode; Thumb when the address is odd
	t->ctx.fpscr = 0x03C00000;					// default NaN, flush to zero, round to zero: the console's
	t->ctx.tls = VA_TLS + (u32) slot * TLS_SIZE;
	mem.fill (t->ctx.tls, 0, TLS_SIZE);
	threads[threadCount++] = t;					// (the list's reference: the one `new` gave)
	return t;
}

bool Machine::start (u32 entryPoint, u32 stackSize)
{
	stackSize = (stackSize + PAGE_SIZE - 1) & ~(u32) (PAGE_SIZE - 1);
	u8 *stack = mem.allocTop (stackSize);
	if (!stack) return false;
	mem.map (VA_HEAP_END - stackSize, stackSize, stack, PERM_RW);
	entry = entryPoint;
	return threadNew (entryPoint, 0, VA_HEAP_END, 0x30) != 0;
}

void Machine::threadExit (Thread *t)
{
	t->status = THREAD_DEAD; t->tlsSlot = -1;
	for (int i = 0; i < t->waitCount; i++) release (t->waitOn[i]);
	t->waitCount = 0;
	// its mutexes are freed (their waiters go on)
	for (int i = 0; i < HANDLE_MAX; i++)
		if (handles[i] && handles[i]->type == OBJ_MUTEX && ((Mutex *) handles[i])->owner == t)
		{
			Mutex *mu = (Mutex *) handles[i]; mu->owner = 0; mu->count = 0;
			wakeWaiters (mu);
		}
	wakeWaiters (t);
	for (int i = 0; i < threadCount; i++)
		if (threads[i] == t) { threads[i] = threads[--threadCount]; threads[threadCount] = 0; break; }
	if (current == t) { current = 0; resched = true; }
	release (t);
	if (threadCount == 0) exited = true;				// (the last thread left: the program is over)
}

// The thread to run: the ready one of the highest priority; among equals the running one, else the one ready first.
Thread *Machine::pick ()
{
	Thread *best = 0;
	for (int i = 0; i < threadCount; i++)
	{
		Thread *t = threads[i];
		if (t->status != THREAD_READY) continue;
		if (!best || t->priority < best->priority) { best = t; continue; }
		if (t->priority > best->priority) continue;
		if (best == current) continue;
		if (t == current || t->readySeq < best->readySeq) best = t;
	}
	return best;
}

void Machine::switchTo (Thread *t)
{
	if (current) cpu->save (current->ctx);
	cpu->load (t->ctx);
	current = t;
	switchCount++;
}

// Does the wait of t end now? Then what it waited for is taken and its results are written -- into the Cpu
// when t is the thread making the call (inSvc), into its saved registers otherwise.
bool Machine::tryWait (Thread *t, bool inSvc)
{
	int index = -1;
	if (t->waitAll)
	{
		for (int i = 0; i < t->waitCount; i++) if (!t->waitOn[i]->available (t)) return false;
		for (int i = 0; i < t->waitCount; i++) t->waitOn[i]->acquire (t);
		index = 0;
	}
	else
	{
		for (int i = 0; i < t->waitCount && index < 0; i++) if (t->waitOn[i]->available (t)) index = i;
		if (index < 0) return false;
		t->waitOn[index]->acquire (t);
	}
	u32 *r = inSvc ? cpu->regs () : t->ctx.r;
	r[0] = RES_OK;
	if (t->waitOut) r[1] = (u32) index;
	return true;
}

// o changed: the threads waiting for it that can go on do, the highest priority first.
void Machine::wakeWaiters (Object *o)
{
	for (;;)
	{
		Thread *best = 0;
		for (int i = 0; i < threadCount; i++)
		{
			Thread *t = threads[i];
			if (t->status != THREAD_WAIT_SYNC) continue;
			bool has = false;
			for (int k = 0; k < t->waitCount; k++) if (t->waitOn[k] == o) has = true;
			if (!has) continue;
			bool can = t->waitAll;					// (all of them: every one must be free now)
			for (int k = 0; k < t->waitCount; k++)
				if (t->waitAll) { if (!t->waitOn[k]->available (t)) can = false; }
				else if (t->waitOn[k]->available (t)) can = true;
			if (!can) continue;
			if (!best || t->priority < best->priority || (t->priority == best->priority && t->readySeq < best->readySeq)) best = t;
		}
		if (!best || !tryWait (best, false)) return;
		for (int k = 0; k < best->waitCount; k++) release (best->waitOn[k]);
		best->waitCount = 0;
		best->status = THREAD_READY; best->readySeq = nextSeq++; best->wakeTick = NEVER;
		resched = true;
	}
}

// ---- the scheduler ---------------------------------------------------------------------------------------------------
void Machine::run (u64 ticks)
{
	const u64 end = now + ticks;
	while (!exited && now < end)
	{
		if (gsp.listPending && picaDone (this)) gpuSync ();		// (the GPU drew a list aside: its end is told)
		// the sound processor's frames, timers whose time has come
		u64 nextWake = NEVER;
		if (dsp.on)
		{
			if (dsp.nextTick <= now) { dspFrame (this); dsp.nextTick += 1310720; if (dsp.nextTick <= now) dsp.nextTick = now + 1310720; }
			nextWake = dsp.nextTick;
		}
		for (int i = 0; i < timerCount; i++)
		{
			Timer *tm = timers[i];
			if (tm->fireTick == NEVER) continue;
			if (tm->fireTick <= now)
			{
				tm->signaled = true;
				tm->fireTick = tm->interval ? tm->fireTick + tm->interval : NEVER;
				if (tm->fireTick != NEVER && tm->fireTick <= now) tm->fireTick = now + tm->interval;	// (late: no burst)
				wakeWaiters (tm);
				if (tm->reset == RESET_PULSE) tm->signaled = false;
			}
			if (tm->fireTick < nextWake) nextWake = tm->fireTick;
		}
		// sleeps and timed waits that are over
		for (int i = 0; i < threadCount; i++)
		{
			Thread *t = threads[i];
			if (t->status == THREAD_READY || t->wakeTick == NEVER) continue;
			if (t->wakeTick > now) { if (t->wakeTick < nextWake) nextWake = t->wakeTick; continue; }
			if (t->status != THREAD_WAIT_SLEEP) t->ctx.r[0] = RES_TIMEOUT;
			for (int k = 0; k < t->waitCount; k++) release (t->waitOn[k]);
			t->waitCount = 0;
			t->status = THREAD_READY; t->readySeq = nextSeq++; t->wakeTick = NEVER;
		}
		Thread *t = pick ();
		if (!t)								// everyone waits: the time passes
		{
			if (gsp.listPending) { gpuSync (); continue; }		// (... for the GPU, maybe: this thread helps it, no time passes)
			if (current) { cpu->save (current->ctx); current = 0; }
			now = nextWake < end ? nextWake : end;
			continue;
		}
		if (t != current) switchTo (t);
		u64 limit = nextWake < end ? nextWake : end;
		if (gsp.listPending && limit > now + 67000) limit = now + 67000;	// (a quarter of a millisecond: the GPU's end is looked for often)
		ticksLeft = (s64) (limit - now);
		if (ticksLeft < 1) ticksLeft = 1;
		resched = false;
		cpu->run ();
	}
}

// ---- memory ----------------------------------------------------------------------------------------------------------
u32 Machine::svcControlMemory (u32 op, u32 addr0, u32 addr1, u32 size, u32 perm, u32 *out)
{
	*out = addr0;
	if ((addr0 | addr1 | size) & (PAGE_SIZE - 1)) return RES_INVALID_ARG;
	if (!size) return RES_INVALID_ARG;
	const bool linear = (op & 0x10000) != 0;
	const int p = (int) (perm & 3);
	switch (op & 0xFF)
	{
	case 3:									// commit
		if (linear)
		{
			if (size > FCRAM_SIZE - mem.linearUsed - mem.topUsed || mem.linearUsed + size > APP_LINEAR_MAX) return RES_OUT_OF_MEMORY;
			u32 va = VA_LINEAR + mem.linearUsed;
			if (addr0 && addr0 != va) return RES_INVALID_ADDRESS;	// (the linear heap only grows, in order)
			memset (mem.fcram + mem.linearUsed, 0, size);
			mem.map (va, size, mem.fcram + mem.linearUsed, p ? p : PERM_RW);
			mem.linearUsed += size;
			*out = va;
			return RES_OK;
		}
		else
		{
			if (!addr0) addr0 = VA_HEAP + heapSize;
			if (addr0 < VA_HEAP || addr0 + size > VA_HEAP_END || addr0 + size < addr0) return RES_INVALID_ADDRESS;
			if (mem.pages[addr0 >> PAGE_BITS]) return RES_INVALID_ADDRESS;	// (there already)
			u8 *host = mem.allocTop (size);
			if (!host) return RES_OUT_OF_MEMORY;
			mem.map (addr0, size, host, p ? p : PERM_RW);
			if (addr0 + size - VA_HEAP > heapSize) heapSize = addr0 + size - VA_HEAP;
			*out = addr0;
			return RES_OK;
		}
	case 1:									// free (the pages are not taken back yet)
		if (!mem.mapped (addr0, size)) return RES_INVALID_ADDRESS;
		mem.unmap (addr0, size);
		cpu->invalidate (addr0, size);
		return RES_OK;
	case 4:									// map: addr0 shows what addr1 holds
		if (!mem.mapped (addr1, size)) return RES_INVALID_ADDRESS;
		for (u32 o = 0; o < size; o += PAGE_SIZE) mem.map (addr0 + o, PAGE_SIZE, mem.pages[(addr1 + o) >> PAGE_BITS], p ? p : PERM_RW);
		return RES_OK;
	case 5:									// unmap
		mem.unmap (addr0, size);
		return RES_OK;
	case 6:									// protect
		if (!mem.mapped (addr0, size)) return RES_INVALID_ADDRESS;
		for (u32 o = 0; o < size; o += PAGE_SIZE) mem.perms[(addr0 + o) >> PAGE_BITS] = (u8) (perm & 7);
		return RES_OK;
	}
	return RES_INVALID_ARG;
}

// ---- the system calls ------------------------------------------------------------------------------------------------
void Machine::svc (u32 n)
{
	u32 *r = cpu->regs ();
	Thread *t = current;
	svcCount++;
	if (!t) return;
	if (trace && n != 0x32 && n != 0x24 && n != 0x25 && n != 0x22 && n != 0x28 && n != 0x3D)
		fprintf (stderr, "svc %02x (%08x %08x %08x %08x) thread %u from %08x\n", (unsigned) n, (unsigned) r[0], (unsigned) r[1], (unsigned) r[2], (unsigned) r[3], (unsigned) t->id, (unsigned) r[14]);
	switch (n)
	{
	case 0x01:								// ControlMemory (op, addr0, addr1, size, perm) -> addr
	{
		u32 out = 0;
		r[0] = svcControlMemory (r[0], r[1], r[2], r[3], r[4], &out);
		r[1] = out;
		break;
	}
	case 0x02:								// QueryMemory (-, -, addr) -> base, size, perm, state
	{
		u32 a = r[2] & ~(u32) (PAGE_SIZE - 1), pg = a >> PAGE_BITS;
		u8 pm = mem.perms[pg]; bool there = mem.pages[pg] != 0;
		u32 lo = pg, hi = pg + 1;
		while (lo > 0 && (mem.pages[lo - 1] != 0) == there && mem.perms[lo - 1] == pm) lo--;
		while (hi < PAGE_COUNT && (mem.pages[hi] != 0) == there && mem.perms[hi] == pm) hi++;
		r[0] = RES_OK; r[1] = lo << PAGE_BITS; r[2] = (hi - lo) << PAGE_BITS; r[3] = pm; r[4] = there ? 5 : 0; r[5] = 0;	// (5: private)
		break;
	}
	case 0x03:								// ExitProcess
		exited = true; exitCode = 0; resched = true;
		break;
	case 0x08:								// CreateThread (priority, entry, arg, stack top, processor) -> handle
	{
		s32 prio = (s32) r[0];
		if (prio < 0 || prio > 0x3F) { r[0] = RES_OUT_OF_RANGE; break; }
		Thread *nt = threadNew (r[1], r[2], r[3], prio);
		if (!nt) { r[0] = RES_OUT_OF_HANDLES; break; }
		nt->refs++;
		u32 h = handleNew (nt);
		if (!h) { threadExit (nt); r[0] = RES_OUT_OF_HANDLES; break; }
		r[0] = RES_OK; r[1] = h;
		if (prio < t->priority) resched = true;
		break;
	}
	case 0x09:								// ExitThread
		threadExit (t);
		break;
	case 0x0A:								// SleepThread (ns)
	{
		s64 ns = (s64) ((u64) r[1] << 32 | r[0]);
		r[0] = RES_OK;
		if (ns <= 0) { t->readySeq = nextSeq++; cpu->save (t->ctx); current = 0; resched = true; break; }	// (a yield: the others of its priority first)
		t->status = THREAD_WAIT_SLEEP; t->wakeTick = now + nsToTicks (ns);
		cpu->save (t->ctx); current = 0; resched = true;
		break;
	}
	case 0x0B:								// GetThreadPriority (-, handle) -> priority
	{
		Thread *o = (Thread *) handleGet (r[1], OBJ_THREAD);
		if (!o) { r[0] = RES_INVALID_HANDLE; break; }
		r[0] = RES_OK; r[1] = (u32) o->priority;
		break;
	}
	case 0x0C:								// SetThreadPriority (handle, priority)
	{
		Thread *o = (Thread *) handleGet (r[0], OBJ_THREAD);
		if (!o) { r[0] = RES_INVALID_HANDLE; break; }
		if ((s32) r[1] < 0 || (s32) r[1] > 0x3F) { r[0] = RES_OUT_OF_RANGE; break; }
		o->priority = (s32) r[1]; r[0] = RES_OK; resched = true;
		break;
	}
	case 0x13:								// CreateMutex (-, initially locked) -> handle
	{
		Mutex *mu = new Mutex;
		if (r[1]) mu->acquire (t);
		u32 h = handleNew (mu);
		r[0] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; r[1] = h;
		break;
	}
	case 0x14:								// ReleaseMutex (handle)
	{
		Mutex *mu = (Mutex *) handleGet (r[0], OBJ_MUTEX);
		if (!mu) { r[0] = RES_INVALID_HANDLE; break; }
		if (mu->owner != t) { r[0] = RES_NOT_OWNER; break; }
		r[0] = RES_OK;
		if (--mu->count == 0) { mu->owner = 0; wakeWaiters (mu); }
		break;
	}
	case 0x15:								// CreateSemaphore (-, initial, max) -> handle
	{
		if ((s32) r[1] < 0 || (s32) r[2] < (s32) r[1]) { r[0] = RES_INVALID_ARG; break; }
		u32 h = handleNew (new Semaphore ((s32) r[1], (s32) r[2]));
		r[0] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; r[1] = h;
		break;
	}
	case 0x16:								// ReleaseSemaphore (-, handle, count) -> the count before
	{
		Semaphore *s = (Semaphore *) handleGet (r[1], OBJ_SEMAPHORE);
		if (!s) { r[0] = RES_INVALID_HANDLE; break; }
		if ((s32) r[2] < 0 || s->count + (s32) r[2] > s->max) { r[0] = RES_OUT_OF_RANGE; break; }
		r[0] = RES_OK; r[1] = (u32) s->count;
		s->count += (s32) r[2];
		wakeWaiters (s);
		break;
	}
	case 0x17:								// CreateEvent (-, reset type) -> handle
	{
		u32 h = handleNew (new Event ((int) r[1]));
		r[0] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; r[1] = h;
		break;
	}
	case 0x18:								// SignalEvent (handle)
	{
		Event *e = (Event *) handleGet (r[0], OBJ_EVENT);
		if (!e) { r[0] = RES_INVALID_HANDLE; break; }
		r[0] = RES_OK;
		e->signaled = true;
		wakeWaiters (e);
		if (e->reset == RESET_PULSE) e->signaled = false;
		break;
	}
	case 0x19:								// ClearEvent (handle)
	{
		Event *e = (Event *) handleGet (r[0], OBJ_EVENT);
		if (!e) { r[0] = RES_INVALID_HANDLE; break; }
		e->signaled = false; r[0] = RES_OK;
		break;
	}
	case 0x1A:								// CreateTimer (-, reset type) -> handle
	{
		if (timerCount >= (int) TIMER_MAX) { r[0] = RES_OUT_OF_HANDLES; break; }
		u32 h = handleNew (new Timer (this, (int) r[1]));
		r[0] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; r[1] = h;
		break;
	}
	case 0x1B:								// SetTimer (handle, interval low, initial, interval high)
	{
		Timer *tm = (Timer *) handleGet (r[0], OBJ_TIMER);
		if (!tm) { r[0] = RES_INVALID_HANDLE; break; }
		const s64 initial = (s64) ((u64) r[3] << 32 | r[2]), interval = (s64) ((u64) r[4] << 32 | r[1]);
		if (initial < 0 || interval < 0) { r[0] = RES_OUT_OF_RANGE; break; }
		tm->fireTick = now + nsToTicks (initial); tm->interval = nsToTicks (interval);
		r[0] = RES_OK; resched = true;					// (the scheduler takes its time into account)
		break;
	}
	case 0x1C:								// CancelTimer (handle)
	case 0x1D:								// ClearTimer (handle)
	{
		Timer *tm = (Timer *) handleGet (r[0], OBJ_TIMER);
		if (!tm) { r[0] = RES_INVALID_HANDLE; break; }
		if (n == 0x1C) tm->fireTick = NEVER; else tm->signaled = false;
		r[0] = RES_OK;
		break;
	}
	case 0x1E:								// CreateMemoryBlock (other's rights, addr, size, my rights) -> handle
	{
		const u32 addr = r[1], size = r[2];
		if (!size || (size & (PAGE_SIZE - 1)) || (addr & (PAGE_SIZE - 1))) { r[0] = RES_INVALID_ARG; break; }
		u8 *host;
		if (addr)							// the program's own pages, shared (one piece in the host)
		{
			if (!mem.mapped (addr, size)) { r[0] = RES_INVALID_ADDRESS; break; }
			host = mem.ptr (addr);
			bool whole = true;
			for (u32 o = 0; o < size; o += PAGE_SIZE) if (mem.pages[(addr + o) >> PAGE_BITS] != host + o) whole = false;
			if (!whole) { r[0] = RES_INVALID_ADDRESS; break; }
		}
		else if (!(host = mem.allocTop (size))) { r[0] = RES_OUT_OF_MEMORY; break; }
		u32 h = handleNew (new SharedMem (host, size));
		r[0] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; r[1] = h;
		break;
	}
	case 0x1F:								// MapMemoryBlock (handle, addr, my rights, other's rights)
	{
		SharedMem *sm = (SharedMem *) handleGet (r[0], OBJ_SHMEM);
		if (!sm) { r[0] = RES_INVALID_HANDLE; break; }
		const u32 at = r[1] ? r[1] : sm->va;				// (at 0: the block's own address -- the shared font)
		if (!at || (at & (PAGE_SIZE - 1)) || mem.pages[at >> PAGE_BITS]) { r[0] = RES_INVALID_ADDRESS; break; }
		mem.map (at, sm->size, sm->host, (r[2] & 3) ? (int) (r[2] & 3) : PERM_RW);
		r[0] = RES_OK;
		break;
	}
	case 0x20:								// UnmapMemoryBlock (handle, addr)
	{
		SharedMem *sm = (SharedMem *) handleGet (r[0], OBJ_SHMEM);
		if (!sm) { r[0] = RES_INVALID_HANDLE; break; }
		const u32 at = r[1] ? r[1] : sm->va;
		if (!at || mem.ptr (at) != sm->host) { r[0] = RES_INVALID_ADDRESS; break; }
		mem.unmap (at, sm->size);
		r[0] = RES_OK;
		break;
	}
	case 0x21:								// CreateAddressArbiter -> handle
	{
		u32 h = handleNew (new Arbiter);
		r[0] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; r[1] = h;
		break;
	}
	case 0x22:								// ArbitrateAddress (arbiter, addr, type, value, ns)
	{
		if (!handleGet (r[0], OBJ_ARBITER)) { r[0] = RES_INVALID_HANDLE; break; }
		const u32 addr = r[1], type = r[2]; const s32 value = (s32) r[3];
		const s64 ns = (s64) ((u64) r[5] << 32 | r[4]);
		r[0] = RES_OK;
		if (type == 0)							// signal: wake `value` waiters on addr (negative: all)
		{
			s32 left = value;
			for (;;)
			{
				if (value >= 0 && left <= 0) break;
				Thread *best = 0;
				for (int i = 0; i < threadCount; i++)
				{
					Thread *w = threads[i];
					if (w->status != THREAD_WAIT_ARBITER || w->arbiterAddr != addr) continue;
					if (!best || w->priority < best->priority || (w->priority == best->priority && w->readySeq < best->readySeq)) best = w;
				}
				if (!best) break;
				best->status = THREAD_READY; best->readySeq = nextSeq++; best->wakeTick = NEVER; best->ctx.r[0] = RES_OK;
				left--; resched = true;
			}
			break;
		}
		if (type > 4) { r[0] = RES_INVALID_ARG; break; }
		if (!mem.mapped (addr, 4)) { r[0] = RES_INVALID_ADDRESS; break; }
		s32 v = (s32) mem.r32 (addr);
		if (v >= value) break;						// (not less: no wait)
		if (type == 2 || type == 4) mem.w32 (addr, (u32) (v - 1));
		const bool timed = type == 3 || type == 4;
		if (timed && ns == 0) { r[0] = RES_TIMEOUT; break; }
		t->status = THREAD_WAIT_ARBITER; t->arbiterAddr = addr;
		t->wakeTick = timed && ns > 0 ? now + nsToTicks (ns) : NEVER;
		cpu->save (t->ctx); current = 0; resched = true;
		break;
	}
	case 0x23:								// CloseHandle (handle)
		r[0] = handleClose (r[0]) ? (u32) RES_OK : (u32) RES_INVALID_HANDLE;
		break;
	case 0x24:								// WaitSynchronization1 (handle, -, ns)
	case 0x25:								// WaitSynchronizationN (ns low, handles, count, all, ns high) -> index
	{
		const bool many = n == 0x25;
		const s64 ns = many ? (s64) ((u64) r[4] << 32 | r[0]) : (s64) ((u64) r[3] << 32 | r[2]);
		const s32 count = many ? (s32) r[2] : 1;
		if (count < 0 || count > WAIT_MAX) { r[0] = RES_OUT_OF_RANGE; break; }
		if (many && count && !mem.mapped (r[1], (u32) count * 4)) { r[0] = RES_INVALID_ADDRESS; break; }
		Object *objs[WAIT_MAX]; bool ok = true;
		for (s32 i = 0; i < count && ok; i++)
		{
			objs[i] = handleGet (many ? mem.r32 (r[1] + (u32) i * 4) : r[0]);
			if (!objs[i] || !objs[i]->waitable ()) ok = false;
		}
		if (!ok) { r[0] = RES_INVALID_HANDLE; break; }
		for (s32 i = 0; i < count; i++) t->waitOn[i] = objs[i];
		t->waitCount = count; t->waitAll = many && r[3] != 0; t->waitOut = many;
		if (count && tryWait (t, true)) { t->waitCount = 0; break; }
		if (ns == 0) { t->waitCount = 0; r[0] = RES_TIMEOUT; break; }
		for (s32 i = 0; i < count; i++) objs[i]->refs++;		// (kept while waited for)
		t->status = THREAD_WAIT_SYNC; t->wakeTick = ns > 0 ? now + nsToTicks (ns) : NEVER;
		cpu->save (t->ctx); current = 0; resched = true;
		break;
	}
	case 0x27:								// DuplicateHandle (-, handle) -> handle
	{
		Object *o = handleGet (r[1]);
		if (!o) { r[0] = RES_INVALID_HANDLE; break; }
		o->refs++;
		u32 h = handleNew (o);
		r[0] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; r[1] = h;
		break;
	}
	case 0x28:								// GetSystemTick -> ticks
		r[0] = (u32) now; r[1] = (u32) (now >> 32);
		break;
	case 0x0D: case 0x0F:							// GetThreadAffinityMask, GetThreadIdealProcessor
		r[0] = RES_OK; r[1] = 0;
		break;
	case 0x0E: case 0x10:							// SetThreadAffinityMask, SetThreadIdealProcessor
		r[0] = RES_OK;
		break;
	case 0x11:								// GetCurrentProcessorNumber
		r[0] = 0;
		break;
	case 0x2A:								// GetSystemInfo (-, type, parameter) -> a 64-bit value
	case 0x2B:								// GetProcessInfo (-, process, type) -> a 64-bit value
	{
		const u32 type = n == 0x2A ? r[1] : r[2];
		u64 v = 0;
		if (n == 0x2A && type == 0) v = mem.linearUsed + mem.topUsed;			// memory used (any region)
		else if (n == 0x2B && (type == 0 || type == 2)) v = mem.linearUsed + mem.topUsed;	// the process's memory
		else if (n == 0x2B && type == 20) v = (u64) PA_FCRAM - VA_LINEAR;		// linear address -> physical
		else note ("%s type %u", n == 0x2A ? "GetSystemInfo" : "GetProcessInfo", (unsigned) type);
		r[0] = RES_OK; r[1] = (u32) v; r[2] = (u32) (v >> 32);
		break;
	}
	case 0x38:								// GetResourceLimit (-, process) -> handle
	{
		u32 h = handleNew (new Process);
		r[0] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; r[1] = h;
		break;
	}
	case 0x39:								// GetResourceLimitLimitValues (values, handle, names, count)
	case 0x3A:								// GetResourceLimitCurrentValues
	{
		for (u32 i = 0; i < r[3] && i < 16; i++)
		{
			const u32 name = mem.r32 (r[2] + i * 4);
			u64 v = 0;
			if (name == 1) v = n == 0x39 ? 0x04000000 : (u64) mem.linearUsed + mem.topUsed;	// committed memory
			else if (name == 0) v = n == 0x39 ? 0x18 : 0x30;				// the highest priority allowed
			else if (name == 2) v = n == 0x39 ? (u64) TLS_MAX : (u64) threadCount;		// threads
			mem.w64 (r[0] + i * 8, v);
		}
		r[0] = RES_OK;
		break;
	}
	case 0x2D:								// ConnectToPort (-, name) -> handle
	{
		char name[12]; memset (name, 0, sizeof name);
		mem.read (r[1], name, 11);
		Session *s = strcmp (name, "srv:") == 0 ? serviceOpen ("srv:") : 0;
		if (!s) { note ("port %s", name); r[0] = RES_PORT_NOT_FOUND; break; }
		u32 h = handleNew (s);
		r[0] = h ? (u32) RES_OK : (u32) RES_OUT_OF_HANDLES; r[1] = h;
		break;
	}
	case 0x32:								// SendSyncRequest (session): the command buffer is in the TLS
	{
		Session *s = (Session *) handleGet (r[0], OBJ_SESSION);
		if (!s) { r[0] = RES_INVALID_HANDLE; break; }
		u32 *cmd = (u32 *) mem.ptr (t->ctx.tls + 0x80);
		r[0] = RES_OK;
		const u32 header = cmd ? cmd[0] : 0;
		if (cmd) s->fn (this, s, cmd);
		if (trace && cmd) fprintf (stderr, "  %s %08x (%08x %08x %08x) -> %08x, %08x %08x %08x\n", s->name, (unsigned) header,
					   (unsigned) cmd[1], (unsigned) cmd[2], (unsigned) cmd[3], (unsigned) cmd[1], (unsigned) cmd[2], (unsigned) cmd[3], (unsigned) cmd[4]);
		r = cpu->regs ();
		break;
	}
	case 0x35:								// GetProcessId (-, handle) -> id
		r[0] = RES_OK; r[1] = 1;
		break;
	case 0x37:								// GetThreadId (-, handle) -> id
	{
		Thread *o = (Thread *) handleGet (r[1], OBJ_THREAD);
		if (!o) { r[0] = RES_INVALID_HANDLE; break; }
		r[0] = RES_OK; r[1] = o->id;
		break;
	}
	case 0x3C:								// Break (reason)
		fail ("the program stopped itself (svcBreak %u) at %08x", (unsigned) r[0], (unsigned) r[14]);
		break;
	case 0x3D:								// OutputDebugString (text, length)
	{
		char buf[1024];
		u32 len = r[1] < sizeof buf ? r[1] : (u32) sizeof buf;
		if (len && mem.read (r[0], buf, len) && debugOut) debugOut (debugUser, buf, len);
		break;
	}
	default:
		unknownSvcs++;
		note ("system call %02x", (unsigned) n);
		r[0] = RES_NOT_IMPLEMENTED;
		break;
	}
	if (trace && current == t && (s32) cpu->regs ()[0] < 0 && n != 0x32) fprintf (stderr, "    -> %08x\n", (unsigned) cpu->regs ()[0]);
	if (resched || exited) cpu->halt ();
}

}
