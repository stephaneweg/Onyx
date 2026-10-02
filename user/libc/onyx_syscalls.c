//
// onyx_syscalls.c -- the newlib "system call" layer for Onyx userland apps.
//
// The aarch64-none-elf toolchain ships newlib (libc + libm). newlib is portable C
// that bottoms out in a handful of POSIX-ish syscall stubs (_sbrk, _read, _write,
// _open, ...). This file implements those stubs on top of the Onyx kapi ABI, so an
// app can be built against newlib and use the real <stdio.h>/<stdlib.h>/<string.h>/
// <math.h> instead of the freestanding helpers (applib.h / umm.h).
//
// Link this object together with crt0libc.S (which calls exit() after main, so
// stdio is flushed). Heap: _sbrk maps onto kapi_sbrk -- so newlib's malloc owns the
// per-process heap. Do NOT also link umm.h in a newlib app (one allocator only).
//
// Files: the kapi file API is sequential (open/read/fsize/close, no seek). To give
// newlib full FILE* semantics (fseek/ftell/fwrite) we back each open fd with an
// in-memory buffer: a read opens by slurping the whole file into RAM; a writable fd
// accumulates in RAM and is written out with kapi_save_file() on close. Resource
// files (CSS, certs, small assets) fit comfortably; this is the pragmatic shim until
// a kapi_lseek lands.
//
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <fcntl.h>
#include <unistd.h>		// SEEK_SET / SEEK_CUR / SEEK_END
#include <string.h>
#include <stdlib.h>

#include "kapi.h"

#undef errno
extern int errno;

// newlib references environ.
static char *s_env_empty[1] = { 0 };
char **environ = s_env_empty;

// crtbegin.o (a startfile we omit with -nostartfiles) normally defines this; some
// libc atexit/__cxa_atexit paths reference it.
void *__dso_handle = 0;

// ---- in-memory fd table -----------------------------------------------------
// fds 0/1/2 are the console (handled inline); real files start at FD_BASE.

#define FD_BASE   3
#define MAX_FILES 32

typedef struct
{
	int            used;
	int            writable;
	unsigned char *buf;
	long           size;	// valid bytes
	long           cap;	// allocated bytes
	long           pos;	// current offset
	char           path[256];
} TFile;

static TFile s_files[MAX_FILES];

static TFile *fd_get (int fd)
{
	int i = fd - FD_BASE;
	if (i < 0 || i >= MAX_FILES || !s_files[i].used)
		return 0;
	return &s_files[i];
}

static int fd_alloc (void)
{
	for (int i = 0; i < MAX_FILES; i++)
		if (!s_files[i].used)
			return FD_BASE + i;
	return -1;
}

// Grow f->buf so f->cap >= need. Returns 0 on success, -1 on OOM.
static int file_reserve (TFile *f, long need)
{
	if (need <= f->cap)
		return 0;
	long ncap = f->cap ? f->cap : 256;
	while (ncap < need)
		ncap *= 2;
	unsigned char *nb = (unsigned char *) realloc (f->buf, (size_t) ncap);
	if (!nb)
		return -1;
	f->buf = nb;
	f->cap = ncap;
	return 0;
}

// ---- app cores (kapi v51): syscalls made there run on the main thread ------------
//
// Code running on an app core (kapi_core_run) must make no kapi call. A newlib program
// whose code there still uses malloc, stdio or files (Doom's engine) turns on the RPC
// (onyx_rpc_enable): a syscall made on an app core then posts itself, and the main thread
// runs it in onyx_rpc_serve (which it calls often) while the app core waits. Off, or on the
// main core, the syscalls run directly as always. One app-core caller at a time.
static volatile int s_rpcOn = 0;
static long (*volatile s_rpcFn) (long, long, long);
static volatile long s_rpcA, s_rpcB, s_rpcC, s_rpcRet;
static volatile int s_rpcState = 0;		// 0 free, 1 asked, 2 done

static inline int on_app_core (void)
{
	return kapi__core () != 0;		// (TPIDRRO_EL0 from kapi v73: readable at EL0)
}

void onyx_rpc_enable (int on) { s_rpcOn = on; }
int onyx_rpc_on_app_core (void) { return s_rpcOn && on_app_core (); }

void onyx_rpc_serve (void)
{
	if (s_rpcState != 1)
		return;
	__asm__ volatile ("dmb ish" ::: "memory");
	s_rpcRet = s_rpcFn (s_rpcA, s_rpcB, s_rpcC);
	__asm__ volatile ("dmb ish" ::: "memory");
	s_rpcState = 2;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
}

long onyx_rpc3 (long (*fn) (long, long, long), long a, long b, long c)
{
	if (!s_rpcOn || !on_app_core ())
		return fn (a, b, c);
	s_rpcFn = fn; s_rpcA = a; s_rpcB = b; s_rpcC = c;
	__asm__ volatile ("dmb ish" ::: "memory");
	s_rpcState = 1;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
	while (s_rpcState != 2)
		__asm__ volatile ("wfe" ::: "memory");
	__asm__ volatile ("dmb ish" ::: "memory");
	long r = s_rpcRet;
	s_rpcState = 0;
	__asm__ volatile ("dmb ish" ::: "memory");
	return r;
}

// ---- memory -----------------------------------------------------------------

static long rpc_sbrk (long incr, long b, long c);
void *_sbrk (ptrdiff_t incr)
{
	return (void *) onyx_rpc3 (rpc_sbrk, (long) incr, 0, 0);
}
static long rpc_sbrk (long incr_, long b, long c)
{
	(void) b; (void) c;
	ptrdiff_t incr = (ptrdiff_t) incr_;
	void *prev = kapi_sbrk ((long) incr);
	if (prev == (void *) -1)
	{
		errno = ENOMEM;
		return -1;
	}
	return (long) prev;
}

// ---- console + files --------------------------------------------------------

static long rpc_stdout (long buf, long len, long c)
{
	(void) c;
	return kapi_stdout_write ((const void *) buf, (unsigned) len);
}

int _write (int fd, const void *vbuf, size_t len)
{
	const unsigned char *buf = (const unsigned char *) vbuf;

	// stdout/stderr go to THIS TASK'S stdout stream (what the terminal reads), not
	// kapi_write() -- that one ignores the fd and dumps to the kernel log (kmsg).
	if (fd == 1 || fd == 2)
		return (int) onyx_rpc3 (rpc_stdout, (long) buf, (long) len, 0);

	TFile *f = fd_get (fd);
	if (!f || !f->writable)
	{
		errno = EBADF;
		return -1;
	}
	if (file_reserve (f, f->pos + (long) len) != 0)
	{
		errno = ENOMEM;
		return -1;
	}
	memcpy (f->buf + f->pos, buf, len);
	f->pos += (long) len;
	if (f->pos > f->size)
		f->size = f->pos;
	return (int) len;
}

static long rpc_stdin (long buf, long len, long c)
{
	(void) c;
	return kapi_stdin_read ((void *) buf, (unsigned) len);
}

int _read (int fd, void *vbuf, size_t len)
{
	if (fd == 0)				// stdin
		return (int) onyx_rpc3 (rpc_stdin, (long) vbuf, (long) len, 0);

	TFile *f = fd_get (fd);
	if (!f)
	{
		errno = EBADF;
		return -1;
	}
	long avail = f->size - f->pos;
	if (avail <= 0)
		return 0;			// EOF
	long n = (long) len < avail ? (long) len : avail;
	memcpy (vbuf, f->buf + f->pos, (size_t) n);
	f->pos += n;
	return (int) n;
}

static int x_open (const char *name, int flags, int mode);
static long rpc_open (long name, long flags, long mode) { return x_open ((const char *) name, (int) flags, (int) mode); }
int _open (const char *name, int flags, int mode)
{
	return (int) onyx_rpc3 (rpc_open, (long) name, flags, mode);
}
static int x_open (const char *name, int flags, int mode)
{
	(void) mode;

	int fd = fd_alloc ();
	if (fd < 0)
	{
		errno = EMFILE;
		return -1;
	}
	TFile *f = &s_files[fd - FD_BASE];
	memset (f, 0, sizeof *f);

	int acc = flags & O_ACCMODE;		// O_RDONLY / O_WRONLY / O_RDWR
	f->writable = (acc == O_WRONLY || acc == O_RDWR);

	// Slurp any existing content unless we are truncating.
	if (!(flags & O_TRUNC))
	{
		void *h = kapi_open (name);
		if (h)
		{
			unsigned sz = kapi_fsize (h);
			if (sz > 0 && file_reserve (f, (long) sz) == 0)
			{
				long got = 0;
				while (got < (long) sz)
				{
					int r = kapi_read (h, f->buf + got, sz - (unsigned) got);
					if (r <= 0)
						break;
					got += r;
				}
				f->size = got;
			}
			kapi_close (h);
		}
		else if (!(flags & O_CREAT))
		{
			f->used = 0;		// reading a missing file
			errno = ENOENT;
			return -1;
		}
	}

	f->used = 1;
	f->pos  = (flags & O_APPEND) ? f->size : 0;
	strncpy (f->path, name, sizeof f->path - 1);
	return fd;
}

off_t _lseek (int fd, off_t off, int whence)
{
	if (fd >= 0 && fd <= 2)
		return 0;			// console: not seekable, report 0

	TFile *f = fd_get (fd);
	if (!f)
	{
		errno = EBADF;
		return (off_t) -1;
	}
	long base = (whence == SEEK_CUR) ? f->pos
		  : (whence == SEEK_END) ? f->size
		  :                        0;	// SEEK_SET
	long np = base + (long) off;
	if (np < 0)
	{
		errno = EINVAL;
		return (off_t) -1;
	}
	f->pos = np;
	return (off_t) np;
}

static int x_close (int fd);
static long rpc_close (long fd, long b, long c) { (void) b; (void) c; return x_close ((int) fd); }
int _close (int fd)
{
	return (int) onyx_rpc3 (rpc_close, fd, 0, 0);
}
static int x_close (int fd)
{
	TFile *f = fd_get (fd);
	if (!f)
	{
		if (fd >= 0 && fd <= 2)
			return 0;
		errno = EBADF;
		return -1;
	}
	int rc = 0;
	if (f->writable)			// flush the in-memory image to the SD card
		if (kapi_save_file (f->path, f->buf ? f->buf : (unsigned char *) "",
				    (unsigned) f->size) < 0)
			rc = -1;
	free (f->buf);
	memset (f, 0, sizeof *f);
	return rc;
}

int _fstat (int fd, struct stat *st)
{
	memset (st, 0, sizeof *st);
	if (fd >= 0 && fd <= 2)
	{
		st->st_mode = S_IFCHR;		// a tty: newlib keeps stdout line-buffered
		return 0;
	}
	TFile *f = fd_get (fd);
	if (!f)
	{
		errno = EBADF;
		return -1;
	}
	st->st_mode = S_IFREG;
	st->st_size = f->size;
	return 0;
}

int _isatty (int fd)
{
	return (fd >= 0 && fd <= 2) ? 1 : 0;
}

static int x_unlink (const char *name);
static long rpc_unlink (long name, long b, long c) { (void) b; (void) c; return x_unlink ((const char *) name); }
int _unlink (const char *name)
{
	return (int) onyx_rpc3 (rpc_unlink, (long) name, 0, 0);
}
static int x_unlink (const char *name)
{
	if (kapi_remove (name) == 0)
		return 0;
	errno = ENOENT;
	return -1;
}

// pread/pwrite: POSIX positioned I/O. Our fds carry no per-call offset, so do it as
// save-offset / seek / read|write / restore-offset. NOT reentrant -- fine for Onyx's
// cooperative, single-threaded apps. Used by libnsutils (the NetSurf core libs).
ssize_t pread (int fd, void *buf, size_t n, off_t off)
{
	off_t cur = _lseek (fd, 0, SEEK_CUR);
	if (cur == (off_t) -1 || _lseek (fd, off, SEEK_SET) == (off_t) -1)
		return -1;
	int r = _read (fd, buf, n);
	_lseek (fd, cur, SEEK_SET);
	return r;
}

ssize_t pwrite (int fd, const void *buf, size_t n, off_t off)
{
	off_t cur = _lseek (fd, 0, SEEK_CUR);
	if (cur == (off_t) -1 || _lseek (fd, off, SEEK_SET) == (off_t) -1)
		return -1;
	int r = _write (fd, buf, n);
	_lseek (fd, cur, SEEK_SET);
	return r;
}

// ---- time -------------------------------------------------------------------

// Days from 1970-01-01 to y-m-d (proleptic Gregorian). Hinnant's algorithm.
static long days_from_civil (int y, int m, int d)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static int x_gettimeofday (struct timeval *tv, void *tz);
static long rpc_gettimeofday (long tv, long tz, long c) { (void) c; return x_gettimeofday ((struct timeval *) tv, (void *) tz); }
int _gettimeofday (struct timeval *tv, void *tz)
{
	return (int) onyx_rpc3 (rpc_gettimeofday, (long) tv, (long) tz, 0);
}
static int x_gettimeofday (struct timeval *tv, void *tz)
{
	(void) tz;
	if (!tv)
		return 0;

	// Build the time from a monotonic clock with microseconds, offset ONCE to the RTC
	// wall-clock epoch. The RTC alone has only 1-second resolution (tv_usec would be 0),
	// which throttles every millisecond-scheduled callback to the next whole second -- that
	// cripples schedulers like NetSurf's (it adds ms deltas to gettimeofday and compares),
	// making incremental parsing/layout/redraw crawl. base_sec keeps tv_sec close to real
	// wall-clock seconds.
	// The clock: the ARM generic timer (CNTPCT_EL0 / CNTFRQ_EL0, readable at EL0 -- as
	// kapi_clock_us reads it), in 64 bits. (It was kapi_get_ticks taken as milliseconds:
	// the kernel's ticks are 100 Hz (Circle's HZ), so the time ran ten times slower than
	// real -- every timer of an app (NetSurf's scheduler, a page's setTimeout, Date.now)
	// fired ten times late.)
	static long base_sec = -1;	// wall-clock second at clock 0 (computed once per process)
	unsigned long long us;
#ifdef __aarch64__
	{
		unsigned long c, f;
		__asm__ volatile ("isb\n\tmrs %0, cntpct_el0\n\tmrs %1, cntfrq_el0" : "=r" (c), "=r" (f));
		us = f != 0 ? (unsigned long long) (c / f) * 1000000ull +
			(unsigned long long) (c % f) * 1000000ull / f : 0;
	}
#else
	us = (unsigned long long) kapi_get_ticks () * 10000ull;	// (100 Hz ticks)
#endif

	if (base_sec < 0)
	{
		int y, mo, d, h, mi, s;
		if (kapi_get_datetime (&y, &mo, &d, &h, &mi, &s))
		{
			long days = days_from_civil (y, mo, d);
			long now  = ((days * 24 + h) * 60 + mi) * 60 + s;
			base_sec  = now - (long) (us / 1000000ull);
		}
		else
			base_sec = 0;		// no RTC -> seconds since boot
	}

	tv->tv_sec  = base_sec + (long) (us / 1000000ull);
	tv->tv_usec = (long) (us % 1000000ull);			// microsecond resolution, monotonic
	return 0;
}

// ---- process ----------------------------------------------------------------

static long rpc_exit (long code, long b, long c) { (void) b; (void) c; kapi_exit ((int) code); return 0; }
void _exit (int code)
{
	onyx_rpc3 (rpc_exit, code, 0, 0);
	for (;;)
		;				// kapi_exit does not return
}

int _kill (int pid, int sig)
{
	(void) pid;
	(void) sig;
	errno = EINVAL;
	return -1;
}

int _getpid (void)
{
	return 1;
}

// ---- locks (kapi v67 threads) -------------------------------------------------
//
// newlib is built with retargetable locking: malloc, stdio's FILEs, atexit, the
// environment... take these locks, no-ops until now. Defining them all here (the
// functions and the static locks) keeps newlib's own lock.o out. A lock is a kapi_lock
// (a swap, a yield while another thread holds it) plus an owner for the recursive ones:
// the thread's tid on core 0, the core (negated) on an app core, where no kapi call is
// made. errno and the other reentrancy state (_impure_ptr) stay shared by the threads.
#include <sys/lock.h>

struct __lock { volatile int lk; int owner; int count; };

struct __lock __lock___sfp_recursive_mutex;
struct __lock __lock___atexit_recursive_mutex;
struct __lock __lock___at_quick_exit_mutex;
struct __lock __lock___malloc_recursive_mutex;
struct __lock __lock___env_recursive_mutex;
struct __lock __lock___tz_mutex;
struct __lock __lock___dd_hash_mutex;
struct __lock __lock___arc4random_mutex;

static int lock_me (void)
{
	unsigned core = kapi__core ();
	return core == 0 ? kapi_thread_self () : -(int) core;
}

void __retarget_lock_init (_LOCK_T *lock)
{
	*lock = (_LOCK_T) calloc (1, sizeof (struct __lock));
}
void __retarget_lock_init_recursive (_LOCK_T *lock) { __retarget_lock_init (lock); }
void __retarget_lock_close (_LOCK_T lock) { free (lock); }
void __retarget_lock_close_recursive (_LOCK_T lock) { free (lock); }

void __retarget_lock_acquire (_LOCK_T lock)
{
	if (lock != 0) kapi_lock (&lock->lk);
}
int __retarget_lock_try_acquire (_LOCK_T lock)
{
	return lock == 0 || kapi__xchg (&lock->lk, 1) == 0;
}
void __retarget_lock_release (_LOCK_T lock)
{
	if (lock != 0) kapi_unlock (&lock->lk);
}

void __retarget_lock_acquire_recursive (_LOCK_T lock)
{
	if (lock == 0) return;
	int me = lock_me ();
	if (lock->count > 0 && lock->owner == me) { lock->count++; return; }
	kapi_lock (&lock->lk);
	lock->owner = me;
	lock->count = 1;
}
int __retarget_lock_try_acquire_recursive (_LOCK_T lock)
{
	if (lock == 0) return 1;
	int me = lock_me ();
	if (lock->count > 0 && lock->owner == me) { lock->count++; return 1; }
	if (kapi__xchg (&lock->lk, 1) != 0) return 0;
	lock->owner = me;
	lock->count = 1;
	return 1;
}
void __retarget_lock_release_recursive (_LOCK_T lock)
{
	if (lock == 0 || lock->count == 0) return;
	if (--lock->count == 0)
	{
		lock->owner = 0;
		kapi_unlock (&lock->lk);
	}
}
