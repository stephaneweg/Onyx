/*
 * sys/mman.h -- what Dynarmic's assembler (oaknut's CodeBlock) asks of the system for its code memory, for a
 * build with the bare-metal toolchain (newlib has no <sys/mman.h>): mmap / munmap / mprotect. The test host
 * (t0_shim.c) does them with Linux's calls under qemu-aarch64; on Onyx they are AppKit's kapi_code_alloc
 * (docs/3DS-EMULATOR-STUDY.md, phase T0).
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#ifndef ONYX_N3DS_SHIM_MMAN_H
#define ONYX_N3DS_SHIM_MMAN_H
#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE	0
#define PROT_READ	1
#define PROT_WRITE	2
#define PROT_EXEC	4
#define MAP_SHARED	0x01
#define MAP_PRIVATE	0x02
#define MAP_FIXED	0x10
#define MAP_ANON	0x20
#define MAP_ANONYMOUS	0x20
#define MAP_FAILED	((void *) -1)

#ifdef __cplusplus
extern "C" {
#endif
void *mmap (void *addr, size_t len, int prot, int flags, int fd, off_t off);
int munmap (void *addr, size_t len);
int mprotect (void *addr, size_t len, int prot);
#ifdef __cplusplus
}
#endif
#endif
