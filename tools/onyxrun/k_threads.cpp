//
// k_threads.cpp -- the threads and their synchronisation (onyxrun.h), as kernel/sys/thread.cpp does them: thread_create
// (_ex), thread_exit / join / self / info / priority, the mutexes, events and barriers (handles 1..256), wait_word /
// wake_word, and the calls posted to the process (post, pop_post, pump_sleep). A guest thread is a host thread with
// its own Unicorn engine (cpu.cpp); its stack is one of the 32 MB slots at USER_THREAD_STACKS, as on the Pi.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <condition_variable>
#include <map>
#include <deque>
#include <chrono>
#include <string.h>

bool guest_str (const char *p, std::string &out, size_t max);
u64 el0_thread_return (void);
int cpu_thread_state (int tid, int *code);
template <class T> static void put (T *p, T v) { if (p && hm_ok (p, sizeof (T), MEM_W)) *p = v; }

#define THREAD_MAX	32
#define STACK_SLOTS	((USER_MMAP_BASE - USER_THREAD_STACKS) / USER_THREAD_SLOT)

struct TInfo { int slot; u64 lo, hi; bool detached; bool joined; };
static std::mutex s_TM;
static std::condition_variable s_TCV;			// a thread ended
static std::map<int, TInfo> s_TInfo;			// by tid (>= 2)
static bool s_SlotBusy[STACK_SLOTS];

static int new_thread (u64 fn, u64 arg, u64 stack, u64 tls, const char *name, bool detached)
{
	if (stack == 0) stack = 256 * 1024;
	if (stack < 16 * 1024 || stack > 16 * 1024 * 1024) return -KAPI_EINVAL;
	stack = (stack + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
	int slot = -1;
	{
		std::lock_guard<std::mutex> L (s_TM);
		int live = 0, c;
		for (auto &t : s_TInfo) if (!t.second.joined && cpu_thread_state (t.first, &c) < 0) live++;
		if (live >= THREAD_MAX) return -KAPI_EAGAIN;
		for (int i = 0; i < (int) STACK_SLOTS; i++) if (!s_SlotBusy[i]) { slot = i; s_SlotBusy[i] = true; break; }
		if (slot < 0) return -KAPI_EAGAIN;
	}
	u64 hi = USER_THREAD_STACKS + (u64) (slot + 1) * USER_THREAD_SLOT, lo = hi - stack;
	{
		BigLockHold L;
		if (!hm_map (lo, stack, MEM_R | MEM_W, "stack of a thread"))
		{
			std::lock_guard<std::mutex> L2 (s_TM);
			s_SlotBusy[slot] = false;
			return -KAPI_ENOMEM;
		}
	}
	std::lock_guard<std::mutex> L (s_TM);		// (held across the start: the thread's record is there before it runs)
	int tid = cpu_thread_start (fn, arg, hi, el0_thread_return (), tls, name);
	s_TInfo[tid] = TInfo { slot, lo, hi, detached, false };
	return tid;
}

// cpu.cpp: a thread ended (the big lock NOT held) -- its joiners woken; a detached one's stack freed now.
void cpu_on_thread_end (int tid)
{
	u64 lo = 0, len = 0;
	int slot = -1;
	{
		std::lock_guard<std::mutex> L (s_TM);
		auto it = s_TInfo.find (tid);
		if (it != s_TInfo.end () && it->second.detached && !it->second.joined)
		{
			lo = it->second.lo; len = it->second.hi - it->second.lo; slot = it->second.slot;
			it->second.joined = true;
		}
		s_TCV.notify_all ();
	}
	if (slot >= 0)
	{
		{ BigLockHold B; hm_unmap (lo, len); }
		std::lock_guard<std::mutex> L (s_TM);
		s_SlotBusy[slot] = false;
	}
}

static int k_thread_create (u64 fn, u64 arg, unsigned stack, const char *name)
{
	std::string n;
	if (name) guest_str (name, n, 64);
	int r = new_thread (fn, arg, stack, 0, n.c_str (), false);
	return r >= 0 ? r : r == -KAPI_EAGAIN ? -2 : -1;	// (v67's: -1 no memory, -2 too many threads)
}

static int k_thread_create_ex (const struct kapi_thread_attr *a)
{
	if (!hm_ok (a, sizeof *a, MEM_R)) return -KAPI_EFAULT;
	std::string n;
	if (a->name) guest_str (a->name, n, 64);
	u64 stack = a->stack_size ? a->stack_size : 8ull << 20;
	if (stack > 16ull << 20) stack = 16ull << 20;
	return new_thread (a->fn, a->arg, stack, a->tls, n.c_str (), (a->flags & KAPI_THREAD_DETACHED) != 0);
}

static void k_thread_exit (int code)
{
	if (cpu_tid () <= 1) cpu_exit_process (code);
	cpu_exit_thread (code);
}

static int k_thread_join (int tid, unsigned timeout, int *code)
{
	if (tid == cpu_tid ()) return -3;
	std::unique_lock<std::mutex> L (s_TM);
	auto it = s_TInfo.find (tid);
	if (it == s_TInfo.end () || it->second.joined || it->second.detached) return -2;
	auto until = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout);
	int c = 0;
	while (cpu_thread_state (tid, &c) < 0)
	{
		if (timeout == KAPI_WAIT_FOREVER) s_TCV.wait (L);
		else if (s_TCV.wait_until (L, until) == std::cv_status::timeout && cpu_thread_state (tid, &c) < 0) return -1;
	}
	it = s_TInfo.find (tid);
	it->second.joined = true;
	u64 lo = it->second.lo, len = it->second.hi - it->second.lo;
	int slot = it->second.slot;
	L.unlock ();
	{ BigLockHold B; hm_unmap (lo, len); }
	{ std::lock_guard<std::mutex> L2 (s_TM); s_SlotBusy[slot] = false; }
	put (code, c);
	return 0;
}

static int k_thread_self (void) { return cpu_tid (); }

static int k_thread_info (int tid, struct kapi_thread_info *out)
{
	if (!hm_ok (out, sizeof *out, MEM_W)) return -KAPI_EFAULT;
	if (tid == 0) tid = cpu_tid ();
	struct kapi_thread_info I;
	memset (&I, 0, sizeof I);
	I.tid = tid;
	if (tid == 1)
	{
		HmRegion r;
		if (hm_find (USER_STACK_TOP - 16, &r)) { I.stack_lo = r.va; I.stack_hi = USER_STACK_TOP; }
	}
	else
	{
		std::lock_guard<std::mutex> L (s_TM);
		auto it = s_TInfo.find (tid);
		if (it == s_TInfo.end () || it->second.joined) return -KAPI_ESRCH;
		I.stack_lo = it->second.lo; I.stack_hi = it->second.hi;
		int c;
		I.state = cpu_thread_state (tid, &c) == 0 ? 1 : 0;
	}
	I.guard = HM_GRAIN;
	*out = I;
	return 0;
}

static std::map<int, int> s_Prio;			// (s_TM) tid -> 0 normal / 1 "real time"

static int k_thread_priority (int tid, int prio)
{
	if (prio > 1) return -1;
	if (tid == 0) tid = cpu_tid ();
	std::lock_guard<std::mutex> L (s_TM);
	int c;
	if (tid != 1 && (!s_TInfo.count (tid) || cpu_thread_state (tid, &c) == 0)) return -2;
	int old = s_Prio.count (tid) ? s_Prio[tid] : 0;
	if (prio >= 0) s_Prio[tid] = prio;		// (the host's scheduler is not told: every thread shares the big lock)
	return old;
}

// ---- mutexes, events, barriers ------------------------------------------------------------------------------
struct Sync
{
	int kind;				// 1 mutex, 2 event, 3 barrier
	int owner = 0, depth = 0;		// mutex
	bool manual = false, set = false;	// event
	unsigned count = 0, in = 0, gen = 0;	// barrier
	bool closed = false;
};
static std::mutex s_SM;
static std::condition_variable s_SCV;
static std::map<int, Sync> s_Sync;
static int new_sync (const Sync &s)
{
	std::lock_guard<std::mutex> L (s_SM);
	for (int h = 1; h <= 256; h++) if (!s_Sync.count (h)) { s_Sync[h] = s; return h; }
	return -1;
}

static bool wait_until (std::unique_lock<std::mutex> &L, unsigned timeout, std::chrono::steady_clock::time_point until)
{
	if (timeout == KAPI_WAIT_FOREVER) { s_SCV.wait (L); return true; }
	return s_SCV.wait_until (L, until) != std::cv_status::timeout;
}

static int k_mutex_create (void) { Sync s; s.kind = 1; return new_sync (s); }

static int k_mutex_lock (int h, unsigned timeout)
{
	int me = cpu_tid ();
	auto until = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout == KAPI_WAIT_FOREVER ? 0 : timeout);
	std::unique_lock<std::mutex> L (s_SM);
	for (;;)
	{
		auto it = s_Sync.find (h);
		if (it == s_Sync.end () || it->second.kind != 1 || it->second.closed) return -2;
		Sync &m = it->second;
		int c;
		if (m.owner != 0 && m.owner != me && cpu_thread_state (m.owner, &c) == 0) { m.owner = 0; m.depth = 0; }	// (its owner ended)
		if (m.owner == 0 || m.owner == me) { m.owner = me; m.depth++; return 0; }
		if (timeout == 0) return -1;
		if (!wait_until (L, timeout, until) && timeout != KAPI_WAIT_FOREVER)
		{
			auto j = s_Sync.find (h);
			if (j != s_Sync.end () && (j->second.owner == 0 || j->second.owner == me)) continue;
			return -1;
		}
	}
}

static int k_mutex_unlock (int h)
{
	std::lock_guard<std::mutex> L (s_SM);
	auto it = s_Sync.find (h);
	if (it == s_Sync.end () || it->second.kind != 1) return -2;
	Sync &m = it->second;
	if (m.owner != cpu_tid ()) return -1;
	if (--m.depth == 0) { m.owner = 0; s_SCV.notify_all (); }
	return 0;
}

static int k_event_create (int manual, int initial) { Sync s; s.kind = 2; s.manual = manual != 0; s.set = initial != 0; return new_sync (s); }

static int k_event_set (int h)
{
	std::lock_guard<std::mutex> L (s_SM);
	auto it = s_Sync.find (h);
	if (it == s_Sync.end () || it->second.kind != 2) return -2;
	it->second.set = true;
	s_SCV.notify_all ();
	return 0;
}

static int k_event_reset (int h)
{
	std::lock_guard<std::mutex> L (s_SM);
	auto it = s_Sync.find (h);
	if (it == s_Sync.end () || it->second.kind != 2) return -2;
	it->second.set = false;
	return 0;
}

static int k_event_wait (int h, unsigned timeout)
{
	auto until = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout == KAPI_WAIT_FOREVER ? 0 : timeout);
	std::unique_lock<std::mutex> L (s_SM);
	for (;;)
	{
		auto it = s_Sync.find (h);
		if (it == s_Sync.end () || it->second.kind != 2 || it->second.closed) return -2;
		if (it->second.set) { if (!it->second.manual) it->second.set = false; return 0; }
		if (timeout == 0) return -1;
		if (!wait_until (L, timeout, until))
		{
			auto j = s_Sync.find (h);
			if (j != s_Sync.end () && j->second.set) continue;
			return -1;
		}
	}
}

static int k_barrier_create (unsigned count) { if (count == 0) return -2; Sync s; s.kind = 3; s.count = count; return new_sync (s); }

static int k_barrier_wait (int h)
{
	std::unique_lock<std::mutex> L (s_SM);
	auto it = s_Sync.find (h);
	if (it == s_Sync.end () || it->second.kind != 3) return -2;
	Sync &b = it->second;
	unsigned gen = b.gen;
	if (++b.in == b.count) { b.in = 0; b.gen++; s_SCV.notify_all (); return 1; }
	while (true)
	{
		s_SCV.wait (L);
		auto j = s_Sync.find (h);
		if (j == s_Sync.end () || j->second.closed) return -2;
		if (j->second.gen != gen) return 0;
	}
}

static int k_sync_close (int h)
{
	std::lock_guard<std::mutex> L (s_SM);
	auto it = s_Sync.find (h);
	if (it == s_Sync.end ()) return -2;
	s_Sync.erase (it);
	s_SCV.notify_all ();
	return 0;
}

// ---- wait_word / wake_word ------------------------------------------------------------------------------------
static std::mutex s_WM;
static std::condition_variable s_WCV;

static u64 s_WakeGen = 0;				// (s_WM) bumped by every wake_word

static int k_wait_word (volatile unsigned *addr, unsigned expected, unsigned timeout)
{
	if (((u64) addr & 3) || !hm_ok ((const void *) addr, 4, MEM_R)) return -1;
	auto until = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout == KAPI_WAIT_FOREVER ? 0 : timeout);
	std::unique_lock<std::mutex> L (s_WM);
	u64 gen = s_WakeGen;
	while (*addr == expected)
	{
		if (timeout == 0) return 1;
		// (woken by wake_word, or every 10 ms: the kernel re-reads the sleeping words at each tick too)
		auto step = std::chrono::steady_clock::now () + std::chrono::milliseconds (10);
		if (timeout != KAPI_WAIT_FOREVER && step > until) step = until;
		s_WCV.wait_until (L, step);
		if (s_WakeGen != gen) return 0;			// (a wake: spurious ones allowed, the caller checks again)
		if (timeout != KAPI_WAIT_FOREVER && std::chrono::steady_clock::now () >= until) return *addr == expected ? 1 : 0;
	}
	return 0;
}

static int k_wake_word (volatile unsigned *addr)
{
	if (((u64) addr & 3) || !hm_ok ((const void *) addr, 4, MEM_R)) return -1;
	std::lock_guard<std::mutex> L (s_WM);
	s_WakeGen++;
	s_WCV.notify_all ();
	return 1;
}

// ---- the app cores (kapi v51): a job is a guest thread (cpu.cpp); 2 cores, 2 and 3 --------------------------------
static std::mutex s_CM;
static int s_CoreTid[4];				// the job's tid, 0 none
static bool s_CoreOwned[4], s_CoreFault[4];
u64 el0_core_return (void);
int cpu_thread_kill (int tid);

bool core_job_fault (int tid)
{
	std::lock_guard<std::mutex> L (s_CM);
	for (int c = 2; c <= 3; c++) if (s_CoreTid[c] == tid && tid != 0) { s_CoreFault[c] = true; return true; }
	return false;
}

static int k_core_acquire (void)
{
	std::lock_guard<std::mutex> L (s_CM);
	for (int c = 2; c <= 3; c++) if (!s_CoreOwned[c]) { s_CoreOwned[c] = true; s_CoreTid[c] = 0; s_CoreFault[c] = false; return c; }
	return -1;
}

static int core_running (int c)
{
	int code;
	return s_CoreTid[c] != 0 && cpu_thread_state (s_CoreTid[c], &code) < 0;
}

static int k_core_run (int c, u64 fn, u64 arg, u64 stack_top)
{
	std::lock_guard<std::mutex> L (s_CM);
	if (c < 2 || c > 3 || !s_CoreOwned[c] || core_running (c)) return -1;
	if ((stack_top & 15) || !hm_ok ((const void *) (stack_top - 16), 16, MEM_W) || !hm_ok ((const void *) fn, 4, MEM_X)) return -1;
	s_CoreFault[c] = false;
	s_CoreTid[c] = cpu_thread_start (fn, arg, stack_top, el0_core_return (), 0, c == 2 ? "core 2" : "core 3");
	return 0;
}

static int k_core_state (int c)
{
	std::lock_guard<std::mutex> L (s_CM);
	if (c < 2 || c > 3 || !s_CoreOwned[c]) return KAPI_CORE_NOTYOURS;
	if (s_CoreFault[c]) return KAPI_CORE_FAULT;
	return core_running (c) ? KAPI_CORE_RUNNING : KAPI_CORE_IDLE;
}

static void k_core_release (int c)
{
	std::lock_guard<std::mutex> L (s_CM);
	if (c < 2 || c > 3 || !s_CoreOwned[c]) return;
	if (core_running (c)) cpu_thread_kill (s_CoreTid[c]);
	s_CoreOwned[c] = false;
	s_CoreTid[c] = 0;
}

// ---- the posted calls ----------------------------------------------------------------------------------------------
static std::mutex s_PM;
static std::condition_variable s_PCV;
static std::deque<struct kapi_posted> s_Posts;

static int k_post (u64 fn, u64 ctx, long long value)
{
	std::lock_guard<std::mutex> L (s_PM);
	if (s_Posts.size () >= 256) return -1;
	struct kapi_posted p;
	memset (&p, 0, sizeof p);
	p.fn = fn; p.ctx = ctx; p.value = value;
	s_Posts.push_back (p);
	s_PCV.notify_all ();
	return 0;
}

static int k_pop_post (struct kapi_posted *out)
{
	if (!hm_ok (out, sizeof *out, MEM_W)) return 0;
	std::lock_guard<std::mutex> L (s_PM);
	if (s_Posts.empty ()) return 0;
	*out = s_Posts.front ();
	s_Posts.pop_front ();
	return 1;
}

// The window's events (k_window.cpp when there is a window): pending count, and the wait.
int win_events_pending (void) __attribute__ ((weak));
int win_events_pending (void) { return 0; }

static int k_pump_sleep (unsigned timeout)
{
	std::unique_lock<std::mutex> L (s_PM);
	auto until = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout);
	while (s_Posts.empty () && win_events_pending () == 0)
	{
		if (timeout == 0) break;
		auto step = std::chrono::steady_clock::now () + std::chrono::milliseconds (5);
		if (step > until) step = until;
		s_PCV.wait_until (L, step);
		if (std::chrono::steady_clock::now () >= until) break;
	}
	return (int) s_Posts.size () + win_events_pending ();
}

void post_wake (void) { std::lock_guard<std::mutex> L (s_PM); s_PCV.notify_all (); }

static unsigned s_EvMods = 0xFFFFFFFFu;
static unsigned k_event_mods (unsigned m) { unsigned p = s_EvMods; s_EvMods = m; return p; }
unsigned event_mods_now (void) { return s_EvMods; }

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
KAPI (pump_sleep, k_pump_sleep);
KAPI (event_mods, k_event_mods);
KAPI (core_acquire, k_core_acquire);
KAPI (core_run, k_core_run);
KAPI (core_state, k_core_state);
KAPI (core_release, k_core_release);
