//
// onyxrun.h -- the Onyx app runner (docs/APP-RUNNER-STUDY.md): Onyx programs' Pi binaries, unchanged, run on a PC
// (Windows or Linux, x86-64) -- their AArch64 code by Unicorn Engine, their system calls answered by this host
// program, which stands for the kernel. Several Onyx processes (a program, the graphics server Elegant, what they
// start) live in the one host process.
//
//   - a process's memory: its whole user range [USER_VA_BASE, USER_VA_CEILING) is one host reservation; a region it
//     gets (its ELF at 8 GB, a library at 16 GB+, the heap, a stack, the kapi page at 14 GB...) is committed there,
//     at host = reservation + (va - USER_VA_BASE), and mapped into the process's Unicorn engines at va. A buffer
//     shared with another process (a window's pixels, a shm object) is host memory of its own, mapped into both. A
//     pointer a program passes to a system call is translated (g2h, checked against the regions);
//   - the kapi page at KAPI_TABLE_VA as the kernel builds it (kernel/sys/el0.cpp): each entry points at a stub
//     "mov x8, #slot; svc #0; ret" at KAPI_STUBS_VA, or at the kernel's own EL0 blob (memcpy & co., the event pump:
//     el0blob.inc); AppKit's table copied at APPKIT_TABLE_VA;
//   - a system call: the svc stops the engine; the slot's host function runs with x0..x7 (as the kernel's Syscall:
//     every kapi takes at most 8 integer / pointer arguments) and the process's lock let go; its result into x0;
//   - the threads: one Unicorn engine and one host thread a guest thread; in a process one thread runs at a time (its
//     lock: Unicorn's exclusive loads / stores are not atomic across engines), the processes in parallel; a tick
//     stops a thread that has run 10 ms when another of its process waits.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#ifndef ONYXRUN_H
#define ONYXRUN_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <deque>
#include <functional>
#include <thread>

typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint16_t u16;
typedef uint8_t u8;

#include <kern/kapi_abi.h>
#include <kern/layout.h>
#include <kern/el0.h>

#define MEM_R	1
#define MEM_W	2
#define MEM_X	4
#define MEM_RWX	7
#define HM_GRAIN	0x10000ULL
#define ALIGN_UP(x)	(((x) + HM_GRAIN - 1) & ~(HM_GRAIN - 1))

struct Proc;
struct Thread;

// ---- host memory (hostmem.cpp) ---------------------------------------------------------------------------------
// A reservation of len bytes of host address space (nothing committed) -> its address, 0.
u8 *host_reserve (u64 len);
void host_release_all (u8 *p, u64 len);
bool host_commit (u8 *p, u64 len);			// zero-filled, read + write
void host_decommit (u8 *p, u64 len);
// Memory to share between processes (a window's pixels): len bytes zeroed, its own -> its address, 0.
u8 *host_shared_alloc (u64 len);
void host_shared_free (u8 *p, u64 len);

// ---- a process's memory (mem.cpp; the process's lock held unless said) -------------------------------------------
struct Region
{
	u64 va, len;
	unsigned prot;		// MEM_*
	std::string what;	// "program x", "library SD:/lib/uikit.so", "the heap", "stack of ...", "vm_map", ...
	u8 *host;		// where va is in the host
	bool shared;		// host memory of its own (not the reservation's): never decommitted by unmap
	int kind;		// KAPI_VMK_*
};
struct MemOp { int kind; u64 va, len; unsigned prot; u8 *host; };	// kind 0 map, 1 unmap, 2 protect

struct Mem
{
	u8 *base = 0;				// the reservation: guest USER_VA_BASE is here
	std::map<u64, Region> regions;		// by start
	std::vector<MemOp> log;			// replayed by each engine (cpu.cpp)
	bool init ();
	void fini ();
	bool map (u64 va, u64 len, unsigned prot, const char *what, int kind);
	bool map_shared (u64 va, u64 len, unsigned prot, const char *what, u8 *host, int kind);
	bool unmap (u64 va, u64 len);
	bool protect (u64 va, u64 len, unsigned prot);
	const Region *find (u64 va) const;
	const Region *next (u64 va) const;		// the first region at or above va
	u64 find_free (u64 lo, u64 hi, u64 len) const;
	// [va, va + n) inside mapped regions with prot, contiguous in the host too -> its host address, 0
	u8 *g2h (u64 va, u64 n, unsigned prot) const;
private:
	void split_at (u64 a);
	bool overlaps (u64 va, u64 len) const;
};

// ---- the processes (proc.cpp) ----------------------------------------------------------------------------------
struct Obj;						// a handle's object (k_files.cpp, k_proc.cpp)
struct Proc
{
	int pid, ppid;
	std::string path;		// the program's Onyx path ("SD:/bin/echo")
	std::string name;		// its name ("echo", "clock")
	std::string cwd;		// the current folder ("SD:/")
	std::vector<char> argv, env;	// get_argv's and get_env's blocks
	u64 heapBase = 0, heapBrk = 0, heapTop = 0;
	Mem mem;
	// the process's big lock (plock): its engines, its memory -- one of its threads runs at a time. A fair ticket lock,
	// recursive (a host function that holds it may call another that takes it).
	std::mutex lockM;
	std::condition_variable lockCV;
	std::thread::id lockOwner;
	int lockDepth = 0;
	u64 lockNext = 0, lockServing = 0;
	std::atomic<int> lockWaiting { 0 };
	std::mutex m;			// the small fields (cwd, heap, handles, posts, the window's queue)
	std::vector<Thread *> threads;	// by tid - 1
	std::atomic<Thread *> running { nullptr };	// inside its engine now
	std::atomic<u64> runningSince { 0 };
	std::map<u64, std::shared_ptr<Obj>> handles;	// the process's handle table (a number >= 0x1000)
	u64 nextHandle = 0x1000;
	std::vector<std::pair<std::string, u64>> libs;	// the libraries mapped: canonical path, table
	std::shared_ptr<Obj> stdinStream, stdoutStream;	// its console (a pipe, a file, the host's console)
	std::atomic<bool> dying { false };	// exit called / killed: every thread ends at its next stop
	std::atomic<bool> ended { false };	// every thread gone
	int status = 0;			// its exit status
	int reason = 0;			// KAPI_EXIT_* (proc_wait)
	std::condition_variable endCV;	// (m) its end
	// the posts and the window's events (k_threads.cpp, k_ws.cpp)
	std::deque<struct kapi_posted> posts;
	std::deque<struct kapi_event> events;
	std::condition_variable pumpCV;	// (m) a post or an event came
	bool exitAsked = false;		// should_exit (the server asked it to close)
	unsigned evMods = 0xFFFFFFFFu;
	void *ext = 0;			// what other parts keep per process (sync objects...)
	std::vector<std::function<void ()>> atEnd;	// run once when it ends (its buffers, its sockets...)
};

// The process of the calling guest thread (in a system call), 0 outside one.
Proc *cur (void);
Proc *proc_find (int pid);				// a live process (or ended, not reaped)
std::vector<Proc *> proc_list (void);
extern std::mutex g_ProcsM;				// the process table

struct SpawnOpts
{
	std::string path;				// Onyx path of the program
	std::vector<std::string> argv;			// argv[0] = its path
	std::vector<std::string> env;			// empty: the parent's (or the runner's)
	std::string cwd;				// empty: the parent's
	std::string name;				// empty: from the path
	std::shared_ptr<Obj> in, out;			// its console streams (0: the host's console)
	int ppid = 0;
};
// A process made and started -> it (its pid), 0 and *why on failure.
Proc *proc_spawn (const SpawnOpts &o, std::string *why);
// The process ends (status): every thread stopped, the process's resources freed; never returns when it is the
// caller's own.
void proc_exit (Proc *P, int status, int reason);
int proc_wait_end (Proc *P, int timeoutMs);		// -1 still running after the timeout, else its status

// The process's lock (a fair ticket lock: a thread that lets it go and takes it again queues behind the others).
void plock (Proc *P);
void punlock (Proc *P);
struct PLock { Proc *P; PLock (Proc *p) : P (p) { plock (P); } ~PLock () { punlock (P); } };

// ---- the guest's CPUs (cpu.cpp) ----------------------------------------------------------------------------------
// A new guest thread of P at pc (x0 = arg, sp, x30 = lr, TPIDR_EL0 = tls) on a host thread of its own -> its tid.
int cpu_thread_start (Proc *P, u64 pc, u64 arg, u64 sp, u64 lr, u64 tls, const char *name);
int cpu_tid (void);					// the calling guest thread's tid, 0 outside
Thread *cpu_self (void);
[[noreturn]] void cpu_exit_thread (int code);		// the calling thread ends (from a system call)
int cpu_thread_state (Proc *P, int tid, int *code);	// -2 no such thread, -1 running, 0 ended (*code)
int cpu_thread_kill (Proc *P, int tid);
bool cpu_thread_done (Thread *T);
int cpu_thread_tid (Thread *T);
int cpu_thread_code (Thread *T);
// A host wait of a system call checks this between its slices: the process is ending -> the thread ends (throws).
void check_dying (void);
void cpu_stop_all (Proc *P);				// every engine of P stopped (P->dying set first)
void cpu_flush_code (Proc *P, u64 va, u64 len);
void guest_counter (u64 *cnt, u64 *freq);		// CNTPCT_EL0 and CNTFRQ_EL0 as the guest reads them

// ---- the kapi (k_*.cpp) -----------------------------------------------------------------------------------------
#define KAPI_SLOTS_HOST	(sizeof (struct TKApiTable) / 8)
#define SLOT(f)		(offsetof (struct TKApiTable, f) / 8)
typedef u64 (*KapiFn) (u64, u64, u64, u64, u64, u64, u64, u64);
extern KapiFn g_Kapi[KAPI_SLOTS_HOST];
extern const char *g_KapiName[KAPI_SLOTS_HOST];
// KAPI (slot_name, host_function): the host function takes the slot's arguments as host integers of the same width
// -- a guest pointer is a u64 (translated with G / GB / gstr), `long` is written long long (a Windows host's long
// has 32 bits).
struct KapiReg { KapiReg (unsigned slot, const char *name, void *fn); };
#define KAPI(f, fn) static KapiReg s_reg_##f (SLOT (f), #f, (void *) (fn))
bool kapi_user_side (unsigned slot);
void kapi_report_missing (unsigned slot);
void kapi_init_table (Mem &M);				// the kapi page and the stubs page in a process
u64 el0_main_return (void);
u64 el0_thread_return (void);
u64 el0_core_return (void);

// The caller's memory (the current process): a T, n bytes, a string -- 0 / false when it is not mapped with prot.
template <class T> static inline T *G (u64 va, unsigned prot = MEM_R | MEM_W)
{ Proc *P = cur (); return P ? (T *) P->mem.g2h (va, sizeof (T), prot) : 0; }
static inline u8 *GB (u64 va, u64 n, unsigned prot)
{ Proc *P = cur (); return P ? P->mem.g2h (va, n, prot) : 0; }
bool gstr (u64 va, std::string &out, size_t max = 4096);
int gstr_out (u64 buf, unsigned cap, const std::string &s);	// s into the guest's buf[cap], cut, NUL -> its length
template <class T> static inline void gput (u64 va, T v) { T *p = G<T> (va, MEM_W); if (p) *p = v; }

// ---- the runner ------------------------------------------------------------------------------------------------
struct Runner
{
	std::string root;		// the host folder standing for SD:
	std::string ramRoot;		// ... for RAM:
	bool trace = false;		// every system call on stderr
	bool headless = false;		// no window on the PC
};
extern Runner g_Run;

std::string host_path (const char *onyx, const std::string &cwd);	// "" when not one of the runner's volumes
std::string onyx_abs (const char *onyx, const std::string &cwd);	// "SD:/a/b", "" when bad

u64 load_program (Proc *P, const std::string &hostFile, std::string *why);
bool load_appkit (Proc *P, std::string *why);
u64 load_library (Proc *P, const char *name, unsigned minVersion, int *err);

void rlog (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

#endif
