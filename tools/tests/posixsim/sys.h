/*
 * sys.h -- raw Linux system calls for the posixsim stand-in kernel (fakekapi.c), which runs as an
 * aarch64 Linux process under qemu-user: no C library of its own (the program's newlib bottoms out
 * in libonyxposix, which calls the kapi table this file fills).
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#ifndef _POSIXSIM_SYS_H
#define _POSIXSIM_SYS_H

static inline long sc6 (long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__ ("x8") = n;
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x2 __asm__ ("x2") = c;
	register long x3 __asm__ ("x3") = d;
	register long x4 __asm__ ("x4") = e;
	register long x5 __asm__ ("x5") = f;
	__asm__ volatile ("svc #0" : "+r" (x0) : "r" (x8), "r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x5) : "memory");
	return x0;
}
#define sc0(n)			sc6 (n, 0, 0, 0, 0, 0, 0)
#define sc1(n, a)		sc6 (n, (long) (a), 0, 0, 0, 0, 0)
#define sc2(n, a, b)		sc6 (n, (long) (a), (long) (b), 0, 0, 0, 0)
#define sc3(n, a, b, c)		sc6 (n, (long) (a), (long) (b), (long) (c), 0, 0, 0)
#define sc4(n, a, b, c, d)	sc6 (n, (long) (a), (long) (b), (long) (c), (long) (d), 0, 0)
#define sc5(n, a, b, c, d, e)	sc6 (n, (long) (a), (long) (b), (long) (c), (long) (d), (long) (e), 0)

/* aarch64 Linux system call numbers */
#define L_getcwd	17
#define L_fcntl		25
#define L_ioctl		29
#define L_mkdirat	34
#define L_unlinkat	35
#define L_renameat	38
#define L_ftruncate	46
#define L_chdir		49
#define L_openat	56
#define L_close		57
#define L_pipe2		59
#define L_getdents64	61
#define L_lseek		62
#define L_read		63
#define L_write		64
#define L_pread64	67
#define L_pwrite64	68
#define L_ppoll		73
#define L_newfstatat	79
#define L_fstat		80
#define L_fsync		82
#define L_utimensat	88
#define L_exit		93
#define L_exit_group	94
#define L_futex		98
#define L_nanosleep	101
#define L_clock_gettime	113
#define L_sched_yield	124
#define L_kill		129
#define L_getpid	172
#define L_getppid	173
#define L_gettid	178
#define L_socket	198
#define L_bind		200
#define L_listen	201
#define L_connect	203
#define L_getsockname	204
#define L_getpeername	205
#define L_sendto	206
#define L_recvfrom	207
#define L_setsockopt	208
#define L_getsockopt	209
#define L_shutdown	210
#define L_munmap	215
#define L_clone		220
#define L_execve	221
#define L_mmap		222
#define L_mprotect	226
#define L_madvise	233
#define L_accept4	242
#define L_wait4		260
#define L_getrandom	278

#define L_AT_FDCWD	-100
#define L_O_RDONLY	0
#define L_O_WRONLY	1
#define L_O_RDWR	2
#define L_O_CREAT	0100
#define L_O_EXCL	0200
#define L_O_TRUNC	01000
#define L_O_APPEND	02000
#define L_O_NONBLOCK	04000
#define L_O_DIRECTORY	040000
#define L_O_CLOEXEC	02000000

struct l_stat				/* aarch64 struct stat (asm-generic) */
{
	unsigned long st_dev, st_ino;
	unsigned st_mode, st_nlink, st_uid, st_gid;
	unsigned long st_rdev, __pad1;
	long st_size;
	int st_blksize, __pad2;
	long st_blocks;
	long st_atime, st_atime_nsec, st_mtime, st_mtime_nsec, st_ctime, st_ctime_nsec;
	unsigned __unused4, __unused5;
};

struct l_timespec { long tv_sec, tv_nsec; };
struct l_pollfd { int fd; short events, revents; };
struct l_sockaddr_in { unsigned short family, port; unsigned addr; unsigned char zero[8]; };

#endif
