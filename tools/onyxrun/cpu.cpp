//
// cpu.cpp -- the guest's threads on Unicorn Engine (onyxrun.h): one engine a guest thread, on a host thread of its
// own, one running at a time (the big lock). A system call ("svc #0", the slot in x8) stops the engine; the thread
// then runs the slot's host function with the lock let go (it may block: a sleep, a wait, a read of the console),
// takes the lock again, puts the result in x0 and goes on at the instruction after the svc. A tick (every 10 ms)
// stops the running engine when another thread waits for the lock: the threads share the guest's time.
//
// The memory: every engine maps the same host memory at the same addresses (hostmem.cpp). A change (a region
// mapped, unmapped, protected) is logged; each engine replays what it has not seen before it runs again -- an
// engine is only changed by its own thread, never while another thread is inside it.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <unicorn/unicorn.h>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ---- the big lock: a ticket lock (fair: a thread that lets it go and takes it again queues behind the others) ----
static std::mutex s_BigM;
static std::condition_variable s_BigCV;
static u64 s_Next = 0, s_Serving = 0;
static std::atomic<int> s_Waiting (0);

void big_lock (void)
{
	std::unique_lock<std::mutex> L (s_BigM);
	u64 t = s_Next++;
	s_Waiting++;
	while (s_Serving != t) s_BigCV.wait (L);
	s_Waiting--;
}

void big_unlock (void)
{
	std::lock_guard<std::mutex> L (s_BigM);
	s_Serving++;
	s_BigCV.notify_all ();
}

// ---- the memory's change log, replayed by each engine ---------------------------------------------------------
struct MemOp { int kind; u64 va, len; unsigned prot; void *host; };	// kind 0 map, 1 unmap, 2 protect
static std::vector<MemOp> s_MemLog;					// (the big lock)

struct Thread
{
	int tid;
	uc_engine *uc;
	size_t applied;			// s_MemLog's entries this engine has
	std::string name;
	bool done;
	std::atomic<bool> killed;	// core_release: stop it at once
	int code;			// its exit code (thread_join)
	std::thread host;
};

static std::mutex s_ThreadsM;
static std::vector<Thread *> s_Threads;		// by tid - 1 (a slot reused once joined: kept simple, never reused)
static thread_local Thread *t_Self = 0;
static std::atomic<Thread *> s_Running (nullptr);	// the thread inside its engine now (the tick stops it)
static std::atomic<u64> s_RunningSince (0);

static u64 now_us (void)
{
	return (u64) std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
}

static void apply_ops (Thread *T)
{
	for (; T->applied < s_MemLog.size (); T->applied++)
	{
		const MemOp &o = s_MemLog[T->applied];
		uc_err e = UC_ERR_OK;
		if (o.kind == 0) e = uc_mem_map_ptr (T->uc, o.va, o.len, o.prot, o.host);
		else if (o.kind == 1) e = uc_mem_unmap (T->uc, o.va, o.len);
		else e = uc_mem_protect (T->uc, o.va, o.len, o.prot);
		if (e != UC_ERR_OK)
			rlog ("engine %d: memory op %d at %llx (+%llx): %s", T->tid, o.kind, (unsigned long long) o.va,
			      (unsigned long long) o.len, uc_strerror (e));
	}
}

void cpu_map_all (u64 va, u64 len, unsigned prot, void *host)	{ s_MemLog.push_back (MemOp { 0, va, len, prot, host }); }
void cpu_unmap_all (u64 va, u64 len)				{ s_MemLog.push_back (MemOp { 1, va, len, 0, 0 }); }
void cpu_protect_all (u64 va, u64 len, unsigned prot)		{ s_MemLog.push_back (MemOp { 2, va, len, prot, 0 }); }

void cpu_flush_code (u64 va, u64 len)
{
	// (the big lock held: no engine runs) each engine drops its translations of [va, va + len)
	std::lock_guard<std::mutex> L (s_ThreadsM);
	for (Thread *T : s_Threads)
		if (T && !T->done && T->uc) uc_ctl_remove_cache (T->uc, va, va + len);
}

// The guest's counter as it reads it (CNTPCT_EL0) and its frequency (CNTFRQ_EL0): the calling thread's engine.
void guest_counter (u64 *cnt, u64 *freq)
{
	*cnt = 0; *freq = 1;
	if (!t_Self || !t_Self->uc) return;
	uc_arm64_cp_reg r;
	memset (&r, 0, sizeof r);
	r.op0 = 3; r.op1 = 3; r.crn = 14; r.crm = 0; r.op2 = 1;
	if (uc_reg_read (t_Self->uc, UC_ARM64_REG_CP_REG, &r) == UC_ERR_OK) *cnt = r.val;
	r.op2 = 0; r.val = 0;
	if (uc_reg_read (t_Self->uc, UC_ARM64_REG_CP_REG, &r) == UC_ERR_OK && r.val) *freq = r.val;
}

int cpu_tid (void)
{
	return t_Self ? t_Self->tid : 0;
}

// ---- the system calls ------------------------------------------------------------------------------------------
KapiFn g_Kapi[KAPI_SLOTS_HOST];
const char *g_KapiName[KAPI_SLOTS_HOST];

KapiReg::KapiReg (unsigned slot, const char *name, void *fn)
{
	if (slot < KAPI_SLOTS_HOST) { g_Kapi[slot] = (KapiFn) fn; g_KapiName[slot] = name; }
}

#include "../../user/BinUtils/kapi_names.h"
static std::atomic<bool> s_Missing[KAPI_SLOTS_HOST];
void kapi_report_missing (unsigned slot)
{
	if (slot < KAPI_SLOTS_HOST && !s_Missing[slot].exchange (true))
		rlog ("kapi slot %u (%s) is not in the runner yet", slot,
		      g_KapiName[slot] ? g_KapiName[slot] : slot < KAPI_NAMES_COUNT ? kapi_slot_names[slot] : "?");
}

struct ThreadEnd { int code; };
void cpu_on_thread_end (int tid);		// (k_threads.cpp)			// thrown by cpu_exit_thread out of the host function

static u64 xreg (uc_engine *uc, int r) { u64 v = 0; uc_reg_read (uc, r, &v); return v; }

// The pending svc's call, the lock let go around it.
static void do_syscall (Thread *T)
{
	u64 x[8];
	for (int i = 0; i < 8; i++) x[i] = xreg (T->uc, UC_ARM64_REG_X0 + i);
	u64 n = xreg (T->uc, UC_ARM64_REG_X8), r = 0;
	if (n == EL0_SYS_CORE_DONE) throw ThreadEnd { 0 };	// (an app core's job returned: its thread ends)
	if (n == 0 || n >= KAPI_SLOTS_HOST || kapi_user_side ((unsigned) n)) r = 0;
	else if (g_Kapi[n] == 0)
	{
		kapi_report_missing ((unsigned) n);
		// (the v75 calls and later answer -errno; the older ones 0, as an empty slot of the kernel's)
		r = n >= SLOT (vm_map) ? (u64) (s64) -KAPI_ENOSYS : 0;
	}
	else
	{
		big_unlock ();
		r = g_Kapi[n] (x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7]);
		big_lock ();
		if (g_Proc.trace)
			rlog ("[%d] %s (%llx, %llx, %llx, %llx) -> %llx", T->tid, n < KAPI_NAMES_COUNT ? kapi_slot_names[n] : g_KapiName[n], (unsigned long long) x[0],
			      (unsigned long long) x[1], (unsigned long long) x[2], (unsigned long long) x[3], (unsigned long long) r);
	}
	uc_reg_write (T->uc, UC_ARM64_REG_X0, &r);
}

// ---- the engine's hooks -----------------------------------------------------------------------------------------
enum { STOP_NONE, STOP_SVC, STOP_FAULT, STOP_TICK };
struct EngineState { int stop; u32 intno; };
static thread_local EngineState t_State;

// QEMU's exception numbers (target/arm/cpu.h): 1 UDEF, 2 SWI (svc), 7 BKPT...
#define EXCP_UDEF	1
#define EXCP_SWI	2
#define EXCP_BKPT	7

static void hook_intr (uc_engine *uc, u32 intno, void *)
{
	if (intno == EXCP_SWI) t_State.stop = STOP_SVC;
	else { t_State.stop = STOP_FAULT; t_State.intno = intno; }
	uc_emu_stop (uc);
}

static bool hook_unmapped (uc_engine *uc, uc_mem_type type, u64 addr, int size, s64, void *)
{
	(void) uc; (void) size;
	t_State.stop = STOP_FAULT;
	t_State.intno = 1000 + (u32) type;
	return false;
}

static const char *fault_name (u32 intno, uc_err e)
{
	switch (e)
	{
	case UC_ERR_READ_UNMAPPED:	return "a data abort (a read where nothing is mapped)";
	case UC_ERR_WRITE_UNMAPPED:	return "a data abort (a write where nothing is mapped)";
	case UC_ERR_FETCH_UNMAPPED:	return "an instruction abort (no code there)";
	case UC_ERR_WRITE_PROT:		return "a data abort (a write to read-only memory)";
	case UC_ERR_READ_PROT:		return "a data abort (a read of protected memory)";
	case UC_ERR_FETCH_PROT:		return "an instruction abort (not executable)";
	case UC_ERR_READ_UNALIGNED:
	case UC_ERR_WRITE_UNALIGNED:
	case UC_ERR_FETCH_UNALIGNED:	return "an alignment fault";
	case UC_ERR_INSN_INVALID:	return "an undefined instruction";
	case UC_ERR_EXCEPTION:		return intno == EXCP_BKPT ? "a breakpoint" : intno == EXCP_UDEF ? "an undefined instruction" : "an exception";
	default:			return intno == EXCP_UDEF ? "an undefined instruction" : intno == EXCP_BKPT ? "a breakpoint" : "an exception";
	}
}

bool core_job_fault (int tid);		// (k_threads.cpp) an app core's job: only it stops

static void fault (Thread *T, uc_err e)
{
	u64 pc = xreg (T->uc, UC_ARM64_REG_PC), sp = xreg (T->uc, UC_ARM64_REG_SP), lr = xreg (T->uc, UC_ARM64_REG_X30);
	HmRegion r;
	std::string where = hm_find (pc, &r) ? r.what : "nowhere";
	rlog ("%s%s: %s at pc %llx (%s), lr %llx, sp %llx (thread %d %s)", g_Proc.name.c_str (), T->name.compare (0, 5, "core ") == 0 ? "'s app core job stopped" : " killed", fault_name (t_State.intno, e),
	      (unsigned long long) pc, where.c_str (), (unsigned long long) lr, (unsigned long long) sp, T->tid, T->name.c_str ());
	if (core_job_fault (T->tid)) return;	// (the job stopped, KAPI_CORE_FAULT; the process goes on)
	cpu_exit_process (-11);			// (EL0_FAULT_STATUS)
}

static uc_engine *new_engine (Thread *T)
{
	uc_engine *uc = 0;
	uc_err e = uc_open (UC_ARCH_ARM64, UC_MODE_ARM, &uc);
	if (e != UC_ERR_OK) { rlog ("Unicorn: %s", uc_strerror (e)); exit (2); }
	uc_ctl_set_cpu_model (uc, UC_CPU_ARM64_A72);
	uc_hook h1, h2;
	uc_hook_add (uc, &h1, UC_HOOK_INTR, (void *) hook_intr, T, 1, 0);
	uc_hook_add (uc, &h2, UC_HOOK_MEM_INVALID, (void *) hook_unmapped, T, 1, 0);
	u64 core = 0;
	uc_reg_write (uc, UC_ARM64_REG_TPIDRRO_EL0, &core);	// (the core's number at EL0: 0)
	return uc;
}

// The thread's life: run its engine until it ends (its last return, thread_exit, the process's end).
static void run_thread (Thread *T, u64 pc)
{
	t_Self = T;
	big_lock ();
	if (!T->uc) { T->uc = new_engine (T); T->applied = 0; }
	for (;;)
	{
		apply_ops (T);
		t_State.stop = STOP_NONE;
		s_RunningSince = now_us ();
		s_Running = T;
		uc_err e = uc_emu_start (T->uc, pc, 1 /* (never reached: odd) */, 0, 0);
		s_Running = nullptr;
		pc = xreg (T->uc, UC_ARM64_REG_PC);
		if (t_State.stop == STOP_SVC)
		{
			try { do_syscall (T); }
			catch (ThreadEnd &End)
			{
				if (xreg (T->uc, UC_ARM64_REG_X8) != EL0_SYS_CORE_DONE)
					big_lock ();		// (thrown by the host function: the lock was let go)
				std::lock_guard<std::mutex> L (s_ThreadsM);
				T->code = End.code;
				T->done = true;
				break;
			}
			continue;
		}
		if (T->killed) { std::lock_guard<std::mutex> L (s_ThreadsM); T->code = 0; T->done = true; break; }
		if (e != UC_ERR_OK || t_State.stop == STOP_FAULT)
		{
			fault (T, e);			// (returns for an app core's job only)
			std::lock_guard<std::mutex> L (s_ThreadsM);
			T->code = -11; T->done = true;
			break;
		}
		// stopped by the tick: let the others run, then go on
		big_unlock ();
		std::this_thread::yield ();
		big_lock ();
	}
	uc_close (T->uc);
	T->uc = 0;
	big_unlock ();
	cpu_on_thread_end (T->tid);
}

// The tick: stops the running engine when another thread waits for the lock and it has run 10 ms.
static void ticker (void)
{
	for (;;)
	{
		std::this_thread::sleep_for (std::chrono::milliseconds (5));
		Thread *T = s_Running.load ();
		if (T && s_Waiting.load () > 0 && now_us () - s_RunningSince.load () >= 10000)
		{
			t_State.stop = STOP_TICK;		// (not the running thread's t_State: harmless; it reads STOP_NONE)
			uc_emu_stop (T->uc);
		}
	}
}

static Thread *new_thread (const char *name)
{
	Thread *T = new Thread ();
	T->uc = 0; T->applied = 0; T->done = false; T->code = 0; T->killed = false;
	T->name = name ? name : "";
	std::lock_guard<std::mutex> L (s_ThreadsM);
	s_Threads.push_back (T);
	T->tid = (int) s_Threads.size ();
	return T;
}

static void set_entry (Thread *T, u64 pc, u64 arg, u64 sp, u64 lr, u64 tls)
{
	(void) pc;
	uc_reg_write (T->uc, UC_ARM64_REG_X0, &arg);
	uc_reg_write (T->uc, UC_ARM64_REG_SP, &sp);
	uc_reg_write (T->uc, UC_ARM64_REG_X30, &lr);
	uc_reg_write (T->uc, UC_ARM64_REG_TPIDR_EL0, &tls);
}

int cpu_thread_start (u64 pc, u64 arg, u64 sp, u64 lr, u64 tls, const char *name)
{
	Thread *T = new_thread (name);
	{
		BigLockHold L;
		T->uc = new_engine (T);
		T->applied = 0;
		apply_ops (T);
		set_entry (T, pc, arg, sp, lr, tls);
	}
	T->host = std::thread (run_thread, T, pc);
	T->host.detach ();
	return T->tid;
}

void cpu_run_main (u64 pc, u64 sp, u64 lr)
{
	static std::thread s_Tick (ticker);
	s_Tick.detach ();
	Thread *T = new_thread ("main");
	{
		BigLockHold L;
		T->uc = new_engine (T);
		T->applied = 0;
		apply_ops (T);
		set_entry (T, pc, 0, sp, lr, 0);
	}
	run_thread (T, pc);
	// (the main thread's return path is exit: never here)
	cpu_exit_process (0);
}

void cpu_exit_thread (int code)
{
	throw ThreadEnd { code };
}

// core_release: the thread stopped where it is (it ends at its next stop) -> 0, -1 no such thread.
int cpu_thread_kill (int tid)
{
	std::lock_guard<std::mutex> L (s_ThreadsM);
	if (tid < 1 || tid > (int) s_Threads.size ()) return -1;
	Thread *T = s_Threads[tid - 1];
	T->killed = true;
	if (s_Running.load () == T && T->uc) uc_emu_stop (T->uc);
	return 0;
}

// thread_join's view (k_threads.cpp): -1 running, else its code.
int cpu_thread_state (int tid, int *code)
{
	std::lock_guard<std::mutex> L (s_ThreadsM);
	if (tid < 1 || tid > (int) s_Threads.size ()) return -2;
	Thread *T = s_Threads[tid - 1];
	if (!T->done) return -1;
	if (code) *code = T->code;
	return 0;
}

void cpu_exit_process (int status)
{
	fflush (stdout);
	fflush (stderr);
#ifdef _WIN32
	ExitProcess ((unsigned) status);
#endif
	_exit (status & 0xFF);
}
