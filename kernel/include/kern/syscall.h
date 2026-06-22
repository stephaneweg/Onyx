//
// syscall.h
//
// Syscall ABI and numbers. ABI (AArch64, Linux-ish):
//   x8        = syscall number
//   x0..x5    = arguments
//   x0        = return value (negative = -errno)
//   svc #0    = trap
//
#ifndef _kern_syscall_h
#define _kern_syscall_h

#include <circle/types.h>

// Syscall numbers
#define SYS_write	1	// ssize_t write(int fd, const void *buf, size_t len)
#define SYS_yield	2	// void yield(void)
#define SYS_exit	3	// void exit(int status)
#define SYS_getpid	4	// int getpid(void)

#define SYS_MAX		5

// Minimal errno-style returns
#define SYS_OK		0
#define SYS_ENOSYS	(-38)	// function not implemented
#define SYS_EFAULT	(-14)	// bad address

#ifndef __ASSEMBLER__		// the numbers above are also used from user_stub.S

#ifdef __cplusplus
extern "C" {
#endif

// User-memory access. Use unprivileged loads/stores (LDTR/STTR) so accesses honor
// EL0 permissions while running in EL1. Return 0 on success, SYS_EFAULT on a bad
// user pointer. (Fault-trapping for bad pointers is wired with the process model
// in #6; for now these assume a valid mapped user range.)
int copy_from_user (void *pDst, const void *pUserSrc, size_t nLen);
int copy_to_user (void *pUserDst, const void *pSrc, size_t nLen);

#ifdef __cplusplus
}
#endif

#endif // __ASSEMBLER__

#endif // _kern_syscall_h
