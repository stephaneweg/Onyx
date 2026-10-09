/*
 * t0_shim.c -- the code memory of the Dynarmic bring-up test (shim/sys/mman.h) done by Linux's calls, for a
 * program built with the bare-metal toolchain and run under qemu-aarch64 (with basic/a64/linux_shim.c, which
 * gives newlib its system calls).
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include <sys/mman.h>

static long sys6 (long n, long a, long b, long c, long d, long e, long f)
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

void *mmap (void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	long r = sys6 (222, (long) addr, (long) len, prot, flags, fd, (long) off);
	return r < 0 && r > -4096 ? MAP_FAILED : (void *) r;
}
int munmap (void *addr, size_t len) { return (int) sys6 (215, (long) addr, (long) len, 0, 0, 0, 0); }
int mprotect (void *addr, size_t len, int prot) { return (int) sys6 (226, (long) addr, (long) len, prot, 0, 0, 0); }
