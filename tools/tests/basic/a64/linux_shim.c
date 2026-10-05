/*
 * linux_shim.c -- lets a program built with the bare-metal toolchain (aarch64-none-elf, newlib) run as a
 * Linux process under qemu-aarch64: its own _start and newlib's system calls done by Linux ones. So the
 * BASIC host test (host_main.cpp) runs in AArch64 -- the native translator (user/basic/basjit.h) with it
 * -- without a Linux cross compiler (tools/tests/run_basic_native_test.sh).
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#undef errno
extern int errno;

static long sys (long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__ ("x8") = n;
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x3 __asm__ ("x3") = d;
	register long x4 __asm__ ("x4") = e;
	register long x5 __asm__ ("x5") = f;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x8), "r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x5) : "memory");
	return x0;
}
static long ret (long r) { if (r < 0 && r > -4096) { errno = (int) -r; return -1; } return r; }

char **environ;
void *__dso_handle = 0;			/* (no crtbegin: -nostartfiles) */
extern int main (int, char **);
extern void __libc_init_array (void);
extern void exit (int);
void _init (void) {}
void _fini (void) {}

void shim_start (long *sp)
{
	int argc = (int) sp[0];
	char **argv = (char **) (sp + 1);
	environ = argv + argc + 1;
	__libc_init_array ();
	exit (main (argc, argv));
}
__asm__ (".global _start\n_start:\n\tmov x29, #0\n\tmov x30, #0\n\tmov x0, sp\n\tand sp, x0, #-16\n\tb shim_start\n");

int _write (int fd, const char *b, int n) { return (int) ret (sys (64, fd, (long) b, n, 0, 0, 0)); }
int _read (int fd, char *b, int n) { return (int) ret (sys (63, fd, (long) b, n, 0, 0, 0)); }
int _open (const char *path, int flags, int mode)
{
	/* newlib's O_ flags -> Linux's */
	int lf = flags & 3;
	if (flags & 0x0008) lf |= 0x400;		/* O_APPEND */
	if (flags & 0x0200) lf |= 0x40;			/* O_CREAT */
	if (flags & 0x0400) lf |= 0x200;		/* O_TRUNC */
	if (flags & 0x0800) lf |= 0x80;			/* O_EXCL */
	return (int) ret (sys (56, -100, (long) path, lf, mode, 0, 0));
}
int _close (int fd) { return (int) ret (sys (57, fd, 0, 0, 0, 0, 0)); }
off_t _lseek (int fd, off_t off, int whence) { return (off_t) ret (sys (62, fd, off, whence, 0, 0, 0)); }
int _fstat (int fd, struct stat *st) { st->st_mode = fd < 3 ? S_IFCHR : S_IFREG; st->st_blksize = 4096; return 0; }
int _stat (const char *path, struct stat *st) { int fd = _open (path, 0, 0); if (fd < 0) return -1; _close (fd); st->st_mode = S_IFREG; return 0; }
int _isatty (int fd) { (void) fd; return 0; }
int _unlink (const char *path) { return (int) ret (sys (35, -100, (long) path, 0, 0, 0, 0)); }
int _rename (const char *a, const char *b) { return (int) ret (sys (38, -100, (long) a, -100, (long) b, 0, 0)); }
int rename (const char *a, const char *b) { return _rename (a, b); }
int chdir (const char *path) { return (int) ret (sys (49, (long) path, 0, 0, 0, 0, 0)); }
int _getpid (void) { return 1; }
int _kill (int pid, int sig) { (void) pid; (void) sig; errno = EINVAL; return -1; }
void _exit (int code) { sys (94, code, 0, 0, 0, 0, 0); for (;;) ; }
int _gettimeofday (struct timeval *tv, void *tz) { (void) tz; return (int) ret (sys (169, (long) tv, 0, 0, 0, 0, 0)); }
unsigned long shim_clock_us (void) { long ts[2] = { 0, 0 }; sys (113, 1, (long) ts, 0, 0, 0, 0); return (unsigned long) (ts[0] * 1000000 + ts[1] / 1000); }
clock_t _times (void *buf) { (void) buf; return (clock_t) shim_clock_us (); }
void *_sbrk (long incr)
{
	static long cur;
	if (!cur) cur = sys (214, 0, 0, 0, 0, 0, 0);
	long old = cur, want = cur + incr;
	if (sys (214, want, 0, 0, 0, 0, 0) != want) { errno = ENOMEM; return (void *) -1; }
	cur = want;
	return (void *) old;
}
/* memory to write machine code to and run it from */
void *shim_code_alloc (unsigned long size)
{
	long r = sys (222, 0, (long) ((size + 4095) & ~4095ul), 7, 0x22, -1, 0);
	return r < 0 && r > -4096 ? 0 : (void *) r;
}
