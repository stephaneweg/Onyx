//
// k_threads.cpp -- the threads and their synchronisation (onyxrun.h), as kernel/sys/thread.cpp does them: thread_create
// (_ex), thread_exit / join / self / info / priority, the mutexes, events and barriers (handles 1..256), wait_word /
// wake_word, the calls posted to the process (post, pop_post, pump_sleep), the app cores (core_*). A guest thread is a
// host thread with its own Unicorn engine (cpu.cpp); its stack one of the 32 MB slots at USER_THREAD_STACKS, as on
// the Pi. Every wait is cut in slices: the process's end ends it (check_dying).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <chrono>
#include <string.h>
#include <stdlib.h>

#define THREAD_MAX	32
#define STACK_SLOTS	((USER_MMAP_BASE - USER_THREAD_STACKS) / USER_THREAD_SLOT)

typedef std::chrono::steady_clock Clock;

struct TInfo { int slot; u64 lo, hi; bool detached; bool joined; int prio; };
struct Sync
{
	int kind;				// 1 mutex, 2 event, 3 barrier
	int owner = 0, depth = 0;		// mutex
	bool manual = false, set = false;	// event
	unsigned count = 0, in = 0, gen = 0;	// barrier
};
struct TState					// a process's (P->ext)
{
	std::mutex m;
	std::condition_variable cv;		// a thread ended, a sync object changed
	std::map<int, TInfo> threads;		// by tid (>= 2)
	bool slotBusy[STACK_SLOTS] = {};
	std::map<int, Sync> sync;
	int coreTid[4] = {}, prio1 = 0;
	bool coreOwned[4] = {}, coreFault[4] = {};
};

static TState &TS (Proc *P)
{
	static std::mutex s_M;
	std::lock_guard<std::mutex> L (s_M);
	if (!P->ext) { TState *t = new TState (); P->ext = t; }
	return *(TState *) P->ext;
}

// A wait on cv for at most ms (KAPI_WAIT_FOREVER: no end) in 50 ms slices -> false when the time is up.
static bool wait_slice (std::condition_variable &cv, std::unique_lock<std::mutex> &L, unsigned ms, Clock::time_point until)
{
	auto step = Clock::now () + std::chrono::milliseconds (50);
	if (ms != KAPI_WAIT_FOREVER && step > until) step = until;
	cv.wait_until (L, step);
	L.unlock ();
	check_dying ();
	L.lock ();
	return ms == KAPI_WAIT_FOREVER || Clock::now () < until;
}

// ---- threads ----------------------------------------------------------------------------------------------------
static int new_thread (Proc *P, u64 fn, u64 arg, u64 stack, u64 tls, const char *name, bool detached)
{
	TState &S = TS (P);
	if (stack == 0) stack = 256 * 1024;
	if (stack < 16 * 1024 || stack > 16 * 1024 * 1024) return -KAPI_EINVAL;
	stack = ALIGN_UP (stack);
	int slot = -1;
	{
		std::lock_guard<std::mutex> L (S.m);
		int live = 0, c;
		for (auto &t : S.threads) if (!t.second.joined && cpu_thread_state (P, t.first, &c) < 0) live++;
		if (live >= THREAD_MAX) return -KAPI_EAGAIN;
		for (int i = 0; i < (int) STACK_SLOTS; i++) if (!S.slotBusy[i]) { slot = i; S.slotBusy[i] = true; break; }
		if (slot < 0) return -KAPI_EAGAIN;
	}
	u64 hi = USER_THREAD_STACKS + (u64) (slot + 1) * USER_THREAD_SLOT, lo = hi - stack;
	bool ok;
	{
		PLock L (P);
		ok = P->mem.map (lo, stack, MEM_R | MEM_W, "stack of a thread", KAPI_VMK_STACK);
	}
	if (!ok)
	{
		std::lock_guard<std::mutex> L (S.m);
		S.slotBusy[slot] = false;
		return -KAPI_ENOMEM;
	}
	std::lock_guard<std::mutex> L (S.m);	// (held across the start: the record is there before the thread can end)
	int tid = cpu_thread_start (P, fn, arg, hi, el0_thread_return (), tls, name);
	S.threads[tid] = TInfo { slot, lo, hi, detached, false, 0 };
	return tid;
}

static void free_stack (Proc *P, u64 lo, u64 len, int slot)
{
	TState &S = TS (P);
	{ PLock L (P); P->mem.unmap (lo, len); }
	std::lock_guard<std::mutex> L (S.m);
	S.slotBusy[slot] = false;
}

// (proc.cpp's proc_thread_gone tells it: a detached thread's stack freed at its end; joiners woken)
void thread_ended (Proc *P, int tid)
{
	TState &S = TS (P);
	u64 lo = 0, len = 0;
	int slot = -1;
	{
		std::lock_guard<std::mutex> L (S.m);
		auto it = S.threads.find (tid);
		if (it != S.threads.end () && it->second.detached && !it->second.joined)
		{
			lo = it->second.lo; len = it->second.hi - it->second.lo; slot = it->second.slot;
			it->second.joined = true;
		}
		S.cv.notify_all ();
	}
	if (slot >= 0 && !P->ended) free_stack (P, lo, len, slot);
}

static int k_thread_create (u64 fn, u64 arg, unsigned stack, u64 name)
{
	std::string n;
	if (name) gstr (name, n, 64);
	int r = new_thread (cur (), fn, arg, stack, 0, n.c_str (), false);
	return r >= 0 ? r : r == -KAPI_EAGAIN ? -2 : -1;	// (v67's: -1 no memory, -2 too many threads)
}

static int k_thread_create_ex (u64 attr)
{
	const struct kapi_thread_attr *a = G<const struct kapi_thread_attr> (attr, MEM_R);
	if (!a) return -KAPI_EFAULT;
	std::string n;
	if (a->name) gstr ((u64) a->name, n, 64);
	u64 stack = a->stack_size ? a->stack_size : 8ull << 20;
	if (stack > 16ull << 20) stack = 16ull << 20;
	return new_thread (cur (), a->fn, a->arg, stack, a->tls, n.c_str (), (a->flags & KAPI_THREAD_DETACHED) != 0);
}

static void k_thread_exit (int code)
{
	if (cpu_tid () <= 1) proc_exit (cur (), code, KAPI_PROC_EXITED);
	cpu_exit_thread (code);
}

static int k_thread_join (int tid, unsigned timeout, u64 code)
{
	Proc *P = cur ();
	TState &S = TS (P);
	if (tid == cpu_tid ()) return -3;
	std::unique_lock<std::mutex> L (S.m);
	auto it = S.threads.find (tid);
	if (it == S.threads.end () || it->second.joined || it->second.detached) return -2;
	auto until = Clock::now () + std::chrono::milliseconds (timeout == KAPI_WAIT_FOREVER ? 0 : timeout);
	int c = 0;
	while (cpu_thread_state (P, tid, &c) < 0)
	{
		if (timeout == 0) return -1;
		if (!wait_slice (S.cv, L, timeout, until) && cpu_thread_state (P, tid, &c) < 0) return -1;
	}
	it = S.threads.find (tid);
	it->second.joined = true;
	u64 lo = it->second.lo, len = it->second.hi - it->second.lo;
	int slot = it->second.slot;
	L.unlock ();
	free_stack (P, lo, len, slot);
	gput<int> (code, c);
	return 0;
}

static int k_thread_self (void) { return cpu_tid (); }

static int k_thread_info (int tid, u64 out)
{
	struct kapi_thread_info *o = G<struct kapi_thread_info> (out, MEM_W);
	if (!o) return -KAPI_EFAULT;
	Proc *P = cur ();
	TState &S = TS (P);
	if (tid == 0) tid = cpu_tid ();
	struct kapi_thread_info I;
	memset (&I, 0, sizeof I);
	I.tid = tid;
	if (tid == 1)
	{
		PLock L (P);
		const Region *r = P->mem.find (USER_STACK_TOP - 16);
		if (r) { I.stack_lo = r->va; I.stack_hi = USER_STACK_TOP; }
	}
	else
	{
		std::lock_guard<std::mutex> L (S.m);
		auto it = S.threads.find (tid);
		if (it == S.threads.end () || it->second.joined) return -KAPI_ESRCH;
		I.stack_lo = it->second.lo; I.stack_hi = it->second.hi;
		int c;
		I.state = cpu_thread_state (P, tid, &c) == 0 ? 1 : 0;
	}
	I.guard = HM_GRAIN;
	*o = I;
	return 0;
}

static int k_thread_priority (int tid, int prio)
{
	Proc *P = cur ();
	TState &S = TS (P);
	if (prio > 1) return -1;
	if (tid == 0) tid = cpu_tid ();
	std::lock_guard<std::mutex> L (S.m);
	int c;
	if (tid == 1) { int old = S.prio1; if (prio >= 0) S.prio1 = prio; return old; }
	auto it = S.threads.find (tid);
	if (it == S.threads.end () || cpu_thread_state (P, tid, &c) == 0) return -2;
	int old = it->second.prio;
	if (prio >= 0) it->second.prio = prio;		// (the host's scheduler is not told: a process's threads share its lock)
	return old;
}

// ---- mutexes, events, barriers ------------------------------------------------------------------------------
static int new_sync (const Sync &s)
{
	TState &S = TS (cur ());
	std::lock_guard<std::mutex> L (S.m);
	for (int h = 1; h <= 256; h++) if (!S.sync.count (h)) { S.sync[h] = s; return h; }
	return -1;
}

static int k_mutex_create (void) { Sync s; s.kind = 1; return new_sync (s); }

static int k_mutex_lock (int h, unsigned timeout)
{
	Proc *P = cur ();
	TState &S = TS (P);
	int me = cpu_tid ();
	auto until = Clock::now () + std::chrono::milliseconds (timeout == KAPI_WAIT_FOREVER ? 0 : timeout);
	std::unique_lock<std::mutex> L (S.m);
	for (;;)
	{
		auto it = S.sync.find (h);
		if (it == S.sync.end () || it->second.kind != 1) return -2;
		Sync &m = it->second;
		int c;
		if (m.owner != 0 && m.owner != me && cpu_thread_state (P, m.owner, &c) == 0) { m.owner = 0; m.depth = 0; }	// (its owner ended)
		if (m.owner == 0 || m.owner == me) { m.owner = me; m.depth++; return 0; }
		if (timeout == 0) return -1;
		if (!wait_slice (S.cv, L, timeout, until))
		{
			auto j = S.sync.find (h);
			if (j != S.sync.end () && (j->second.owner == 0 || j->second.owner == me)) continue;
			return -1;
		}
	}
}

static int k_mutex_unlock (int h)
{
	TState &S = TS (cur ());
	std::lock_guard<std::mutex> L (S.m);
	auto it = S.sync.find (h);
	if (it == S.sync.end () || it->second.kind != 1) return -2;
	Sync &m = it->second;
	if (m.owner != cpu_tid ()) return -1;
	if (--m.depth == 0) { m.owner = 0; S.cv.notify_all (); }
	return 0;
}

static int k_event_create (int manual, int initial) { Sync s; s.kind = 2; s.manual = manual != 0; s.set = initial != 0; return new_sync (s); }

static int event_change (int h, bool set)
{
	TState &S = TS (cur ());
	std::lock_guard<std::mutex> L (S.m);
	auto it = S.sync.find (h);
	if (it == S.sync.end () || it->second.kind != 2) return -2;
	it->second.set = set;
	if (set) S.cv.notify_all ();
	return 0;
}
static int k_event_set (int h)		{ return event_change (h, true); }
static int k_event_reset (int h)	{ return event_change (h, false); }

static int k_event_wait (int h, unsigned timeout)
{
	TState &S = TS (cur ());
	auto until = Clock::now () + std::chrono::milliseconds (timeout == KAPI_WAIT_FOREVER ? 0 : timeout);
	std::unique_lock<std::mutex> L (S.m);
	for (;;)
	{
		auto it = S.sync.find (h);
		if (it == S.sync.end () || it->second.kind != 2) return -2;
		if (it->second.set) { if (!it->second.manual) it->second.set = false; return 0; }
		if (timeout == 0) return -1;
		if (!wait_slice (S.cv, L, timeout, until))
		{
			auto j = S.sync.find (h);
			if (j != S.sync.end () && j->second.set) continue;
			return -1;
		}
	}
}

static int k_barrier_create (unsigned count) { if (count == 0) return -2; Sync s; s.kind = 3; s.count = count; return new_sync (s); }

static int k_barrier_wait (int h)
{
	TState &S = TS (cur ());
	std::unique_lock<std::mutex> L (S.m);
	auto it = S.sync.find (h);
	if (it == S.sync.end () || it->second.kind != 3) return -2;
	Sync &b = it->second;
	unsigned gen = b.gen;
	if (++b.in == b.count) { b.in = 0; b.gen++; S.cv.notify_all (); return 1; }
	for (;;)
	{
		wait_slice (S.cv, L, KAPI_WAIT_FOREVER, Clock::now ());
		auto j = S.sync.find (h);
		if (j == S.sync.end ()) return -2;
		if (j->second.gen != gen) return 0;
	}
}

static int k_sync_close (int h)
{
	TState &S = TS (cur ());
	std::lock_guard<std::mutex> L (S.m);
	auto it = S.sync.find (h);
	if (it == S.sync.end ()) return -2;
	S.sync.erase (it);
	S.cv.notify_all ();
	return 0;
}

// ---- wait_word / wake_word (system-wide: a word of a shared buffer wakes across processes) -------------------------
static std::mutex s_WM;
static std::condition_variable s_WCV;
static u64 s_WakeGen = 0;

static int k_wait_word (u64 addr, unsigned expected, unsigned timeout)
{
	volatile unsigned *w = (addr & 3) ? 0 : G<volatile unsigned> (addr, MEM_R);
	if (!w) return -1;
	auto until = Clock::now () + std::chrono::milliseconds (timeout == KAPI_WAIT_FOREVER ? 0 : timeout);
	std::unique_lock<std::mutex> L (s_WM);
	u64 gen = s_WakeGen;
	while (*w == expected)
	{
		if (timeout == 0) return 1;
		// (woken by wake_word, or every 10 ms: the kernel re-reads the sleeping words at each tick too)
		auto step = Clock::now () + std::chrono::milliseconds (10);
		if (timeout != KAPI_WAIT_FOREVER && step > until) step = until;
		s_WCV.wait_until (L, step);
		L.unlock (); check_dying (); L.lock ();
		if (s_WakeGen != gen) return 0;			// (a wake: spurious ones allowed, the caller checks again)
		if (timeout != KAPI_WAIT_FOREVER && Clock::now () >= until) return *w == expected ? 1 : 0;
	}
	return 0;
}

static int k_wake_word (u64 addr)
{
	if ((addr & 3) || !G<unsigned> (addr, MEM_R)) return -1;
	std::lock_guard<std::mutex> L (s_WM);
	s_WakeGen++;
	s_WCV.notify_all ();
	return 1;
}

// ---- the posted calls, the window's events (queued by k_ws.cpp) ----------------------------------------------------
static int k_post (u64 fn, u64 ctx, long long value)
{
	Proc *P = cur ();
	std::lock_guard<std::mutex> L (P->m);
	if (P->posts.size () >= 256) return -1;
	struct kapi_posted p;
	memset (&p, 0, sizeof p);
	p.fn = fn; p.ctx = ctx; p.value = value;
	P->posts.push_back (p);
	P->pumpCV.notify_all ();
	return 0;
}

static int k_pop_post (u64 out)
{
	struct kapi_posted *o = G<struct kapi_posted> (out, MEM_W);
	if (!o) return 0;
	Proc *P = cur ();
	std::lock_guard<std::mutex> L (P->m);
	if (P->posts.empty ()) return 0;
	*o = P->posts.front ();
	P->posts.pop_front ();
	return 1;
}

static int k_pop_event (u64 out)
{
	struct kapi_event *o = G<struct kapi_event> (out, MEM_W);
	if (!o) return 0;
	Proc *P = cur ();
	std::lock_guard<std::mutex> L (P->m);
	if (P->events.empty ()) return 0;
	*o = P->events.front ();
	P->events.pop_front ();
	return 1;
}

static int k_pump_sleep (unsigned timeout)
{
	Proc *P = cur ();
	std::unique_lock<std::mutex> L (P->m);
	auto until = Clock::now () + std::chrono::milliseconds (timeout);
	while (P->posts.empty () && P->events.empty () && !P->exitAsked)
	{
		if (timeout == 0) break;
		auto step = Clock::now () + std::chrono::milliseconds (50);
		if (step > until) step = until;
		P->pumpCV.wait_until (L, step);
		L.unlock (); check_dying (); L.lock ();
		if (Clock::now () >= until) break;
	}
	return (int) (P->posts.size () + P->events.size ()) + (P->exitAsked ? 1 : 0);
}

static unsigned k_event_mods (unsigned m)
{
	Proc *P = cur ();
	std::lock_guard<std::mutex> L (P->m);
	unsigned p = P->evMods;
	P->evMods = m;
	return p;
}

// ---- the app cores (kapi v51): a job is a guest thread of the process; two cores, 2 and 3 -------------------------
bool core_job_fault (Proc *P, int tid)
{
	TState &S = TS (P);
	std::lock_guard<std::mutex> L (S.m);
	for (int c = 2; c <= 3; c++) if (S.coreTid[c] == tid && tid != 0) { S.coreFault[c] = true; return true; }
	return false;
}

static int k_core_acquire (void)
{
	if (getenv ("ONYXRUN_NOCORES")) return -1;	// (to test a program without its app cores)
	TState &S = TS (cur ());
	std::lock_guard<std::mutex> L (S.m);
	for (int c = 2; c <= 3; c++) if (!S.coreOwned[c]) { S.coreOwned[c] = true; S.coreTid[c] = 0; S.coreFault[c] = false; return c; }
	return -1;
}

static bool core_running (Proc *P, TState &S, int c)
{
	int code;
	return S.coreTid[c] != 0 && cpu_thread_state (P, S.coreTid[c], &code) < 0;
}

static int k_core_run (int c, u64 fn, u64 arg, u64 stack_top)
{
	Proc *P = cur ();
	TState &S = TS (P);
	std::lock_guard<std::mutex> L (S.m);
	if (c < 2 || c > 3 || !S.coreOwned[c] || core_running (P, S, c)) return -1;
	if ((stack_top & 15) || !GB (stack_top - 16, 16, MEM_W) || !GB (fn, 4, MEM_X)) return -1;
	S.coreFault[c] = false;
	S.coreTid[c] = cpu_thread_start (P, fn, arg, stack_top, el0_core_return (), 0, c == 2 ? "core 2" : "core 3");
	return 0;
}

static int k_core_state (int c)
{
	Proc *P = cur ();
	TState &S = TS (P);
	std::lock_guard<std::mutex> L (S.m);
	if (c < 2 || c > 3 || !S.coreOwned[c]) return KAPI_CORE_NOTYOURS;
	if (S.coreFault[c]) return KAPI_CORE_FAULT;
	return core_running (P, S, c) ? KAPI_CORE_RUNNING : KAPI_CORE_IDLE;
}

static void k_core_release (int c)
{
	Proc *P = cur ();
	TState &S = TS (P);
	std::lock_guard<std::mutex> L (S.m);
	if (c < 2 || c > 3 || !S.coreOwned[c]) return;
	if (core_running (P, S, c)) cpu_thread_kill (P, S.coreTid[c]);
	S.coreOwned[c] = false;
	S.coreTid[c] = 0;
}

KAPI (thread_create, k_thread_create);
KAPI (thread_create_ex, k_thread_create_ex);
KAPI (thread_exit, k_thread_exit);
KAPI (thread_join, k_thread_join);
KAPI (thread_self, k_thread_self);
KAPI (thread_info, k_thread_info);
KAPI (thread_priority, k_thread_priority);
KAPI (mutex_create, k_mutex_create);
KAPI (mutex_lock, k_mutex_lock);
KAPI (mutex_unlock, k_mutex_unlock);
KAPI (event_create, k_event_create);
KAPI (event_set, k_event_set);
KAPI (event_reset, k_event_reset);
KAPI (event_wait, k_event_wait);
KAPI (barrier_create, k_barrier_create);
KAPI (barrier_wait, k_barrier_wait);
KAPI (sync_close, k_sync_close);
KAPI (wait_word, k_wait_word);
KAPI (wake_word, k_wake_word);
KAPI (post, k_post);
KAPI (pop_post, k_pop_post);
KAPI (pop_event, k_pop_event);
KAPI (pump_sleep, k_pump_sleep);
KAPI (event_mods, k_event_mods);
KAPI (core_acquire, k_core_acquire);
KAPI (core_run, k_core_run);
KAPI (core_state, k_core_state);
KAPI (core_release, k_core_release);
