/*
 * t0_onyx.c -- the code memory of the Dynarmic bring-up test (shim/sys/mman.h) on Onyx itself: AppKit's
 * kapi_code_alloc (pages a program may write and run). The region is never given back (the kernel frees it
 * with the process), as Onyx's own JITs do.
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include <sys/mman.h>
#include "appkit/appkit.h"

void *mmap (void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	(void) addr; (void) prot; (void) flags; (void) fd; (void) off;
	void *p = kapi_code_alloc ((unsigned long) len);
	return p ? p : MAP_FAILED;
}
int munmap (void *addr, size_t len) { (void) addr; (void) len; return 0; }
int mprotect (void *addr, size_t len, int prot) { (void) addr; (void) len; (void) prot; return 0; }
