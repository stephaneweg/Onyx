//
// cpu.cpp -- the guest threads on Unicorn Engine (onyxrun.h): one engine a guest thread, on a host thread of its own;
// in a process one thread runs at a time (its lock, plock), the processes in parallel. A system call ("svc #0", the
// slot in x8) stops the engine; the thread runs the slot's host function with its process's lock let go (it may
// block: a sleep, a wait, a read of the console), takes the lock again, puts the result in x0 and goes on at the
// instruction after the svc. A tick (every 5 ms) stops a thread that has run 10 ms when another of its process waits.
//
// The memory: every engine of a process maps the process's regions (hostmem.cpp). A change is logged (Mem::log);
// each engine replays what it has not seen before it runs again -- an engine is only changed by its own thread.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <unicorn/unicorn.h>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../user/BinUtils/kapi_names.h"

struct Thread
{
	Proc *P;
	int tid;
	uc_engine *uc = 0;
	size_t applied = 0;		// P->mem.log's entries this engine has
	std::string name;
	bool done = false;
	std::atomic<bool> killed { false };
	int code = 0;			// its exit code (thread_join)
	bool coreJob = false;		// an app core's job (a fault stops it only)
};

static thread_local Thread *t_Self = 0;

// ONYXRUN_PROFILE=N: every N ms, where each running thread is (its pc, its lr) on stderr.
static std::atomic<bool> s_ProfileReq { false };
static int s_ProfileMs = 0;

Thread *cpu_self (void) { return t_Self; }
int cpu_tid (void) { return t_Self ? t_Self->tid : 0; }
Proc *cur (void) { return t_Self ? t_Self->P : 0; }

static u64 now_us (void)
{
	return (u64) std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
}

// ---- the process's lock --------------------------------------------------------------------------------------------
void plock (Proc *P)
{
	std::unique_lock<std::mutex> L (P->lockM);
	if (P->lockDepth > 0 && P->lockOwner == std::this_thread::get_id ()) { P->lockDepth++; return; }
	u64 t = P->lockNext++;
	P->lockWaiting++;
	while (P->lockServing != t) P->lockCV.wait (L);
	P->lockWaiting--;
	P->lockOwner = std::this_thread::get_id ();
	P->lockDepth = 1;
}

void punlock (Proc *P)
{
	std::lock_guard<std::mutex> L (P->lockM);
	if (--P->lockDepth > 0) return;
	P->lockOwner = std::thread::id ();
	P->lockServing++;
	P->lockCV.notify_all ();
}

// Let go of the lock whatever its depth -> the depth, to take it again with plock_depth.
static int punlock_all (Proc *P)
{
	std::lock_guard<std::mutex> L (P->lockM);
	int d = P->lockDepth;
	P->lockDepth = 0;
	P->lockOwner = std::thread::id ();
	P->lockServing++;
	P->lockCV.notify_all ();
	return d;
}
static void plock_depth (Proc *P, int d)
{
	plock (P);
	std::lock_guard<std::mutex> L (P->lockM);
	P->lockDepth = d;
}

// ---- the memory's changes, replayed by each engine ------------------------------------------------------------------
static void apply_ops (Thread *T)
{
	std::vector<MemOp> &log = T->P->mem.log;
	for (; T->applied < log.size (); T->applied++)
	{
		const MemOp &o = log[T->applied];
		uc_err e;
		if (o.kind == 0) e = uc_mem_map_ptr (T->uc, o.va, o.len, o.prot, o.host);
		else if (o.kind == 1) e = uc_mem_unmap (T->uc, o.va, o.len);
		else e = uc_mem_protect (T->uc, o.va, o.len, o.prot);
		if (e != UC_ERR_OK)
			rlog ("%s, thread %d: memory op %d at %llx (+%llx): %s", T->P->name.c_str (), T->tid, o.kind,
			      (unsigned long long) o.va, (unsigned long long) o.len, uc_strerror (e));
	}
}

void cpu_flush_code (Proc *P, u64 va, u64 len)
{
	// (P's lock held: none of its engines runs)
	for (Thread *T : P->threads)
		if (T && !T->done && T->uc) uc_ctl_remove_cache (T->uc, va, va + len);
}

void guest_counter (u64 *cnt, u64 *freq)
{
	*cnt = 0; *freq = 1;
	if (!t_Self || !t_Self->uc) return;
	uc_arm64_cp_reg r;
	memset (&r, 0, sizeof r);
	r.op0 = 3; r.op1 = 3; r.crn = 14; r.crm = 0; r.op2 = 1;		// CNTPCT_EL0
	if (uc_reg_read (t_Self->uc, UC_ARM64_REG_CP_REG, &r) == UC_ERR_OK) *cnt = r.val;
	r.op2 = 0; r.val = 0;							// CNTFRQ_EL0
	if (uc_reg_read (t_Self->uc, UC_ARM64_REG_CP_REG, &r) == UC_ERR_OK && r.val) *freq = r.val;
}

// ---- the system calls -----------------------------------------------------------------------------------------------
KapiFn g_Kapi[KAPI_SLOTS_HOST];
const char *g_KapiName[KAPI_SLOTS_HOST];

KapiReg::KapiReg (unsigned slot, const char *name, void *fn)
{
	if (slot < KAPI_SLOTS_HOST) { g_Kapi[slot] = (KapiFn) fn; g_KapiName[slot] = name; }
}

static const char *slot_name (unsigned n)
{
	return n < KAPI_NAMES_COUNT ? kapi_slot_names[n] : n < KAPI_SLOTS_HOST && g_KapiName[n] ? g_KapiName[n] : "?";
}

static std::atomic<bool> s_Missing[KAPI_SLOTS_HOST];
void kapi_report_missing (unsigned slot)
{
	if (slot < KAPI_SLOTS_HOST && !s_Missing[slot].exchange (true))
		rlog ("kapi slot %u (%s) is not in the runner yet", slot, slot_name (slot));
}

struct ThreadEnd { int code; };			// thrown out of a host function: the thread ends

void cpu_exit_thread (int code) { throw ThreadEnd { code }; }

static u64 xreg (uc_engine *uc, int r) { u64 v = 0; uc_reg_read (uc, r, &v); return v; }

// The pending svc (P's lock held on entry and on return).
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
		int d = punlock_all (T->P);
		try { r = g_Kapi[n] (x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7]); }
		catch (...) { plock_depth (T->P, d); throw; }
		plock_depth (T->P, d);
		if (g_Run.trace)
			rlog ("[%s %d] %s (%llx, %llx, %llx, %llx) -> %llx", T->P->name.c_str (), T->tid, slot_name ((unsigned) n),
			      (unsigned long long) x[0], (unsigned long long) x[1], (unsigned long long) x[2], (unsigned long long) x[3],
			      (unsigned long long) r);
	}
	uc_reg_write (T->uc, UC_ARM64_REG_X0, &r);
}

// ---- the engine's hooks -----------------------------------------------------------------------------------------
enum { STOP_NONE, STOP_SVC, STOP_FAULT };
struct EngineState { int stop; u32 intno; };
static thread_local EngineState t_State;

// QEMU's exception numbers (target/arm/cpu.h)
#define EXCP_UDEF	1
#define EXCP_SWI	2
#define EXCP_BKPT	7

static void hook_intr (uc_engine *uc, u32 intno, void *)
{
	if (intno == EXCP_SWI) t_State.stop = STOP_SVC;
	else { t_State.stop = STOP_FAULT; t_State.intno = intno; }
	uc_emu_stop (uc);
}

static bool hook_unmapped (uc_engine *, uc_mem_type type, u64, int, s64, void *)
{
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
	default:			return intno == EXCP_UDEF ? "an undefined instruction" : intno == EXCP_BKPT ? "a breakpoint" : "an exception";
	}
}

bool core_job_fault (Proc *P, int tid);		// (k_threads.cpp)

// A fault: an app core's job stops alone; any other thread's takes the process down (as on the Pi).
static void fault (Thread *T, uc_err e)
{
	u64 pc = xreg (T->uc, UC_ARM64_REG_PC), sp = xreg (T->uc, UC_ARM64_REG_SP), lr = xreg (T->uc, UC_ARM64_REG_X30);
	const Region *r = T->P->mem.find (pc);
	rlog ("%s%s: %s at pc %llx (%s), lr %llx, sp %llx (thread %d %s)", T->P->name.c_str (),
	      T->coreJob ? "'s app core job stopped" : " killed", fault_name (t_State.intno, e), (unsigned long long) pc,
	      r ? r->what.c_str () : "nowhere", (unsigned long long) lr, (unsigned long long) sp, T->tid, T->name.c_str ());
	if (T->coreJob && core_job_fault (T->P, T->tid)) return;
	if (!T->P->dying.exchange (true))
	{
		T->P->status = -11;
		T->P->reason = KAPI_PROC_FAULT;
	}
}

static uc_engine *new_engine (void)
{
	uc_engine *uc = 0;
	uc_err e = uc_open (UC_ARCH_ARM64, UC_MODE_ARM, &uc);
	if (e != UC_ERR_OK) { rlog ("Unicorn: %s", uc_strerror (e)); exit (2); }
	uc_ctl_set_cpu_model (uc, UC_CPU_ARM64_A72);
	uc_hook h1, h2;
	uc_hook_add (uc, &h1, UC_HOOK_INTR, (void *) hook_intr, 0, 1, 0);
	uc_hook_add (uc, &h2, UC_HOOK_MEM_INVALID, (void *) hook_unmapped, 0, 1, 0);
	u64 core = 0;
	uc_reg_write (uc, UC_ARM64_REG_TPIDRRO_EL0, &core);	// (the core's number at EL0: 0)
	return uc;
}

void proc_thread_gone (Proc *P, Thread *T);		// (proc.cpp) a thread ended: the process's end when it was the last

// The thread's life: its engine run until it ends (its last return, thread_exit, the process's end).
static void run_thread (Thread *T, u64 pc)
{
	t_Self = T;
	Proc *P = T->P;
	plock (P);
	for (;;)
	{
		if (P->dying || T->killed) break;
		apply_ops (T);
		t_State.stop = STOP_NONE;
		P->runningSince = now_us ();
		P->running = T;
		uc_err e = uc_emu_start (T->uc, pc, 1 /* (never reached: odd) */, 0, 0);
		{ std::lock_guard<std::mutex> L (P->lockM); P->running = nullptr; }	// (the tick checks it under lockM)
		pc = xreg (T->uc, UC_ARM64_REG_PC);
		if (t_State.stop == STOP_SVC)
		{
			try { do_syscall (T); }
			catch (ThreadEnd &End) { T->code = End.code; break; }
			continue;
		}
		if (e != UC_ERR_OK || t_State.stop == STOP_FAULT)
		{
			fault (T, e);
			T->code = -11;
			break;
		}
		if (s_ProfileMs > 0 && s_ProfileReq.load ())
		{
			const Region *r = P->mem.find (pc);
			u64 lr = xreg (T->uc, UC_ARM64_REG_X30);
			rlog ("profile: %s thread %d at pc %llx (%s +%llx), lr %llx", P->name.c_str (), T->tid, (unsigned long long) pc,
			      r ? r->what.c_str () : "?", (unsigned long long) (r ? pc - r->va : 0), (unsigned long long) lr);
		}
		// stopped by the tick (or the process's end): let the others run, then go on
		int d = punlock_all (P);
		std::this_thread::yield ();
		plock_depth (P, d);
	}
	uc_close (T->uc);
	T->uc = 0;
	T->done = true;
	punlock_all (P);
	t_Self = 0;
	proc_thread_gone (P, T);
}

// The tick: in every process, the running engine stopped when another thread of it waits and it has run 10 ms.
static void ticker (void)
{
	u64 lastProfile = now_us ();
	for (;;)
	{
		std::this_thread::sleep_for (std::chrono::milliseconds (5));
		u64 now = now_us ();
		bool profile = s_ProfileMs > 0 && now - lastProfile >= (u64) s_ProfileMs * 1000;
		s_ProfileReq = profile;
		if (profile) lastProfile = now;
		for (Proc *P : proc_list ())
		{
			Thread *T = P->running.load ();
			if (T && (profile || (P->lockWaiting.load () > 0 && now - P->runningSince.load () >= 10000)))
			{
				std::lock_guard<std::mutex> L (P->lockM);	// (T's engine is not closed meanwhile)
				if (P->running.load () == T && T->uc) uc_emu_stop (T->uc);
			}
		}
	}
}

int cpu_thread_start (Proc *P, u64 pc, u64 arg, u64 sp, u64 lr, u64 tls, const char *name)
{
	static std::once_flag s_Tick;
	std::call_once (s_Tick, [] {
		if (getenv ("ONYXRUN_PROFILE")) s_ProfileMs = atoi (getenv ("ONYXRUN_PROFILE"));
		std::thread (ticker).detach ();
	});
	Thread *T = new Thread ();
	T->P = P;
	T->name = name ? name : "";
	T->coreJob = lr == el0_core_return ();
	{
		PLock L (P);
		P->threads.push_back (T);
		T->tid = (int) P->threads.size ();
		T->uc = new_engine ();
		apply_ops (T);
		uc_reg_write (T->uc, UC_ARM64_REG_X0, &arg);
		uc_reg_write (T->uc, UC_ARM64_REG_SP, &sp);
		uc_reg_write (T->uc, UC_ARM64_REG_X30, &lr);
		uc_reg_write (T->uc, UC_ARM64_REG_TPIDR_EL0, &tls);
	}
	std::thread (run_thread, T, pc).detach ();
	return T->tid;
}

int cpu_thread_state (Proc *P, int tid, int *code)
{
	std::lock_guard<std::mutex> L (P->lockM);
	if (tid < 1 || tid > (int) P->threads.size ()) return -2;
	Thread *T = P->threads[tid - 1];
	if (!T->done) return -1;
	if (code) *code = T->code;
	return 0;
}

bool cpu_thread_done (Thread *T) { return T->done; }
int cpu_thread_tid (Thread *T) { return T->tid; }
int cpu_thread_code (Thread *T) { return T->code; }

int cpu_thread_kill (Proc *P, int tid)
{
	if (tid < 1) return -1;
	std::lock_guard<std::mutex> L (P->lockM);		// (not plock: the thread may be running)
	if (tid > (int) P->threads.size ()) return -1;
	Thread *T = P->threads[tid - 1];
	T->killed = true;
	if (P->running.load () == T && T->uc) uc_emu_stop (T->uc);
	return 0;
}

void cpu_stop_all (Proc *P)
{
	std::lock_guard<std::mutex> L (P->lockM);
	Thread *T = P->running.load ();
	if (T && T->uc) uc_emu_stop (T->uc);
}
