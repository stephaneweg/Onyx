//
// onyxrun.h -- the Onyx app runner (docs/APP-RUNNER-STUDY.md): an Onyx program's Pi binary, unchanged, run on a PC
// (Windows or Linux, x86-64) -- its AArch64 code by Unicorn Engine, its system calls answered by this host program.
//
// How it stands for the kernel:
//   - the memory: the guest's addresses ARE the host's -- every region the program has (its ELF at 8 GB, the
//     libraries at 16 GB+, the heap, the stack, the kapi page at 14 GB...) is host memory reserved at the same
//     address (hostmem.cpp) and mapped into Unicorn there; so a pointer the program passes to a system call is
//     used as it is, with no translation;
//   - the kapi table: the page at KAPI_TABLE_VA as the kernel builds it (kernel/sys/el0.cpp): each entry points at
//     a stub "mov x8, #slot; svc #0; ret" at KAPI_STUBS_VA, or at the kernel's own EL0 blob (memcpy & co., the event
//     pump: el0blob.inc); AppKit's table copied at APPKIT_TABLE_VA;
//   - a system call: Unicorn's interrupt hook (cpu.cpp) calls the host function of slot x8 with x0..x7 (as the
//     kernel's Syscall does: every kapi takes at most 8 integer / pointer arguments), its result into x0;
//   - the threads: one Unicorn engine and one host thread a guest thread, ONE running at a time (the big lock:
//     Unicorn's exclusive loads / stores are not atomic across engines); a system call lets the lock go while the
//     host function runs (it may block), a tick stops a thread that runs too long when another waits.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors. (See LICENSE-MIT in the repository.)
//
#ifndef ONYXRUN_H
#define ONYXRUN_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <mutex>

typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint16_t u16;
typedef uint8_t u8;

#include <kern/kapi_abi.h>
#include <kern/layout.h>
#include <kern/el0.h>

// ---- the host's memory at the guest's addresses (hostmem.cpp) ---------------------------------------------------
#define MEM_R	1
#define MEM_W	2
#define MEM_X	4
#define MEM_RWX	7

// Reserve [USER_VA_BASE, USER_VA_CEILING) at the start: no host library lands there afterwards.
bool hm_init (void);
// Make [va, va + len) usable (64 KB aligned): host memory there, zero-filled, then mapped into every guest engine.
bool hm_map (u64 va, u64 len, unsigned prot, const char *what);
bool hm_unmap (u64 va, u64 len);
bool hm_protect (u64 va, u64 len, unsigned prot);
// The region holding va (its start, length, protection, name) -> false: none.
struct HmRegion { u64 va, len; unsigned prot; std::string what; };
bool hm_find (u64 va, HmRegion *out);
bool hm_next (u64 va, HmRegion *out);		// the first region at or above va
std::vector<HmRegion> hm_regions (void);
// Is [p, p + n) mapped with prot (all of it)?
bool hm_ok (const void *p, u64 n, unsigned prot);
// A free range of len bytes in [lo, hi) (64 KB aligned), first fit -> 0: none.
u64 hm_find_free (u64 lo, u64 hi, u64 len);
#define HM_GRAIN	0x10000ULL

// ---- the guest's CPUs (cpu.cpp) ------------------------------------------------------------------------------
struct Thread;
// A new guest thread starting at pc (x0 = arg, sp, x30 = the return path, TPIDR_EL0 = tls) on a host thread of its
// own -> its tid (>= 1). tid 1 is the main thread: cpu_run_main runs it on the calling host thread.
int cpu_thread_start (u64 pc, u64 arg, u64 sp, u64 lr, u64 tls, const char *name);
[[noreturn]] void cpu_run_main (u64 pc, u64 sp, u64 lr);
// The calling guest thread's tid (in a system call), 0 outside.
int cpu_tid (void);
// The process ends (every thread stopped) with this status: never returns.
[[noreturn]] void cpu_exit_process (int status);
// The calling thread ends (its code kept for thread_join): never returns to its guest code.
[[noreturn]] void cpu_exit_thread (int code);
// Every engine sees a mapping change (hostmem.cpp calls these, the big lock held).
void cpu_map_all (u64 va, u64 len, unsigned prot, void *host);
void cpu_unmap_all (u64 va, u64 len);
void cpu_protect_all (u64 va, u64 len, unsigned prot);
// The big lock: held while guest code runs or the engines change. A host function that blocks never holds it.
void big_lock (void);
void big_unlock (void);
struct BigLockHold { BigLockHold () { big_lock (); } ~BigLockHold () { big_unlock (); } };
// The code in [va, va + len) changed (a JIT, the loader): every engine forgets its translations there.
void cpu_flush_code (u64 va, u64 len);

// ---- the kapi (k_*.cpp) -------------------------------------------------------------------------------------
#define KAPI_SLOTS_HOST	(sizeof (struct TKApiTable) / 8)
#define SLOT(f)		(offsetof (struct TKApiTable, f) / 8)
typedef u64 (*KapiFn) (u64, u64, u64, u64, u64, u64, u64, u64);
extern KapiFn g_Kapi[KAPI_SLOTS_HOST];
extern const char *g_KapiName[KAPI_SLOTS_HOST];
// Registered by each k_*.cpp at its static start: KAPI (slot_name, host_function). The host function takes the
// slot's arguments with host types of the same width (pointers are guest = host addresses; `long` is written
// long long: a Windows host's long is 32 bits).
struct KapiReg { KapiReg (unsigned slot, const char *name, void *fn); };
#define KAPI(f, fn) static KapiReg s_reg_##f (SLOT (f), #f, (void *) (fn))
// The slots done in the guest (the EL0 blob), never a system call.
bool kapi_user_side (unsigned slot);
void kapi_report_missing (unsigned slot);	// "not in the runner" once per slot

// ---- the process (main.cpp, k_base.cpp) ----------------------------------------------------------------------
struct Process
{
	std::string root;		// the host folder standing for SD: (the repository's sdcard/)
	std::string ramRoot;		// the host folder for RAM:
	std::string path;		// the program's Onyx path ("SD:/bin/echo")
	std::string name;		// its name ("echo", "clock")
	std::string cwd;		// the current folder, an Onyx path ("SD:/")
	std::vector<char> argv;		// get_argv's block: NUL-separated
	std::vector<char> env;		// get_env's block: "K=V" NUL-separated
	u64 heapBase, heapBrk, heapTop;	// sbrk: [heapBase, heapBrk) used, mapped up to heapTop
	int pid;
	bool trace;			// ONYXRUN_TRACE=1: every system call on stderr
	std::mutex lock;		// the fields that change (cwd, heap)
};
extern Process g_Proc;

// Onyx path -> host path ("SD:/apps/x" -> <root>/apps/x; relative: the cwd's), "" when it is not a volume the
// runner has. Case-insensitive as FAT is (the existing name's case found on the host).
std::string host_path (const char *onyx);
// An Onyx path made absolute ("SD:/a/b"), "" if bad.
std::string onyx_abs (const char *onyx);

// Load the program and AppKit (loader.cpp) -> its entry, 0 on failure (said on stderr).
u64 load_program (const char *hostFile);
bool load_appkit (void);
// A library (kapi_lib_open): its table's address, or 0 and *err (-KAPI_E*).
u64 load_library (const char *onyxPath, unsigned minVersion, int *err);

void kapi_init_table (void);		// the page at KAPI_TABLE_VA (k_base.cpp)

// Logging: what the runner says (stderr), and the kernel log's lines (kapi write, klog).
void rlog (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

#endif
