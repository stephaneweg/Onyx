/*
 * newlib_syscalls.c -- the system-call layer newlib bottoms out in, for programs built on
 * libonyxposix (docs/POSIX-PLAN.md §3.4). It replaces user/libc/onyx_syscalls.c for them (a
 * program links one or the other, never both; the existing apps keep onyx_syscalls.c).
 *
 * The stubs go through the descriptor table (fd.c): _read / _write / _lseek / _fstat / _close
 * on any descriptor -- v75 files read and written in place (no whole-file buffering), pipes,
 * sockets, /dev pseudo files, the console. _open is open (file.c); _stat / _unlink / _link
 * (stat.c), _gettimeofday / _times (time.c), _getpid / _wait / _fork / _execve (proc.c), _kill
 * (signal.c) and _isatty (misc.c) are with their families. Here too: _sbrk, _exit, newlib's
 * retargetable locks (on the futex mutex) and the app-core RPC of onyx_syscalls.c.
 *
 * The app-core RPC (kapi v51): code on an app core (kapi_core_run) makes no kapi call. When the
 * program turns the RPC on (onyx_rpc_enable), a stub called there posts itself and the main
 * thread runs it in onyx_rpc_serve, while the core waits. Off, or on the main core, the stubs run
 * directly.
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/lock.h>
#include "posix_internal.h"

/* crtbegin.o (not linked: crt0posix is the start) defines it; atexit / __cxa_atexit use it */
void *__dso_handle = 0;

/* ---- the app-core RPC ---- */
static volatile int s_rpcOn = 0;
static long (*volatile s_rpcFn) (long, long, long);
static volatile long s_rpcA, s_rpcB, s_rpcC, s_rpcRet;
static volatile int s_rpcErr;
static volatile int s_rpcState = 0;		/* 0 free, 1 asked, 2 done */
static volatile unsigned s_rpcLock;

void onyx_rpc_enable (int on) { s_rpcOn = on; }
int onyx_rpc_on_app_core (void) { return s_rpcOn && kapi__core () != 0; }

void onyx_rpc_serve (void)
{
	if (s_rpcState != 1)
		return;
	__asm__ volatile ("dmb ish" ::: "memory");
	int e = errno;
	errno = 0;
	s_rpcRet = s_rpcFn (s_rpcA, s_rpcB, s_rpcC);
	s_rpcErr = errno;
	errno = e;
	__asm__ volatile ("dmb ish" ::: "memory");
	s_rpcState = 2;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
}

long onyx_rpc3 (long (*fn) (long, long, long), long a, long b, long c)
{
	if (!s_rpcOn || kapi__core () == 0)
		return fn (a, b, c);
	while (__atomic_exchange_n (&s_rpcLock, 1, __ATOMIC_ACQUIRE))	/* (one app-core caller at a time) */
		kapi__pause ();
	s_rpcFn = fn; s_rpcA = a; s_rpcB = b; s_rpcC = c;
	__asm__ volatile ("dmb ish" ::: "memory");
	s_rpcState = 1;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
	while (s_rpcState != 2)
		__asm__ volatile ("wfe" ::: "memory");
	__asm__ volatile ("dmb ish" ::: "memory");
	long r = s_rpcRet;
	if (s_rpcErr)
		errno = s_rpcErr;
	s_rpcState = 0;
	__atomic_store_n (&s_rpcLock, 0, __ATOMIC_RELEASE);
	return r;
}

/* ---- memory ---- */
static long rpc_sbrk (long incr, long b, long c)
{
	(void) b; (void) c;
	void *prev = kapi_sbrk (incr);
	if (prev == (void *) -1)
	{
		errno = ENOMEM;
		return -1;
	}
	return (long) prev;
}

void *_sbrk (ptrdiff_t incr)
{
	return (void *) onyx_rpc3 (rpc_sbrk, (long) incr, 0, 0);
}

/* ---- descriptors ---- */
static long rpc_read (long fd, long buf, long n)
{
	struct __onyx_ofd *d = __onyx_fd_get ((int) fd);
	if (d == 0)
		return -1;
	ssize_t r = __onyx_read (d, (void *) buf, (size_t) n);
	__onyx_fd_put (d);
	return r;
}

static long rpc_write (long fd, long buf, long n)
{
	struct __onyx_ofd *d = __onyx_fd_get ((int) fd);
	if (d == 0)
		return -1;
	ssize_t r = __onyx_write (d, (const void *) buf, (size_t) n);
	__onyx_fd_put (d);
	return r;
}

static long rpc_lseek (long fd, long off, long whence)
{
	struct __onyx_ofd *d = __onyx_fd_get ((int) fd);
	if (d == 0)
		return -1;
	off_t r = __onyx_lseek (d, (off_t) off, (int) whence);
	__onyx_fd_put (d);
	return (long) r;
}

static long rpc_fstat (long fd, long st, long c)
{
	(void) c;
	struct __onyx_ofd *d = __onyx_fd_get ((int) fd);
	if (d == 0)
		return -1;
	int r = __onyx_fstat (d, (struct stat *) st);
	__onyx_fd_put (d);
	return r;
}

static long rpc_close (long fd, long b, long c) { (void) b; (void) c; return __onyx_fd_close ((int) fd); }
static long rpc_open (long name, long flags, long mode) { return __onyx_open ((const char *) name, (int) flags, (int) mode); }

int _read (int fd, void *buf, size_t n) { return (int) onyx_rpc3 (rpc_read, fd, (long) buf, (long) n); }
int _write (int fd, const void *buf, size_t n) { return (int) onyx_rpc3 (rpc_write, fd, (long) buf, (long) n); }
off_t _lseek (int fd, off_t off, int whence) { return (off_t) onyx_rpc3 (rpc_lseek, fd, (long) off, whence); }
int _fstat (int fd, struct stat *st) { return (int) onyx_rpc3 (rpc_fstat, fd, (long) st, 0); }
int _close (int fd) { return (int) onyx_rpc3 (rpc_close, fd, 0, 0); }
int _open (const char *name, int flags, int mode) { return (int) onyx_rpc3 (rpc_open, (long) name, flags, mode); }

/* ---- the end ---- */
static long rpc_exit (long code, long b, long c) { (void) b; (void) c; kapi_exit ((int) code); return 0; }
void _exit (int code)
{
	onyx_rpc3 (rpc_exit, code, 0, 0);
	for (;;)
		;
}
void _Exit (int code) { _exit (code); }

/* ---- newlib's locks (retargetable locking): the futex mutex ---- */
struct __lock { volatile unsigned lk; volatile int owner; int count; };

struct __lock __lock___sfp_recursive_mutex;
struct __lock __lock___atexit_recursive_mutex;
struct __lock __lock___at_quick_exit_mutex;
struct __lock __lock___malloc_recursive_mutex;
struct __lock __lock___env_recursive_mutex;
struct __lock __lock___tz_mutex;
struct __lock __lock___dd_hash_mutex;
struct __lock __lock___arc4random_mutex;

void __retarget_lock_init (_LOCK_T *lock) { *lock = (_LOCK_T) calloc (1, sizeof (struct __lock)); }
void __retarget_lock_init_recursive (_LOCK_T *lock) { __retarget_lock_init (lock); }
void __retarget_lock_close (_LOCK_T lock) { free (lock); }
void __retarget_lock_close_recursive (_LOCK_T lock) { free (lock); }

void __retarget_lock_acquire (_LOCK_T lock) { if (lock) __onyx_lock (&lock->lk); }
void __retarget_lock_release (_LOCK_T lock) { if (lock) __onyx_unlock (&lock->lk); }
int __retarget_lock_try_acquire (_LOCK_T lock)
{
	unsigned z = 0;
	return lock == 0 || __atomic_compare_exchange_n (&lock->lk, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void __retarget_lock_acquire_recursive (_LOCK_T lock)
{
	if (lock == 0)
		return;
	int me = __onyx_owner_id ();
	if (lock->count > 0 && lock->owner == me)
	{
		lock->count++;
		return;
	}
	__onyx_lock (&lock->lk);
	lock->owner = me;
	lock->count = 1;
}

int __retarget_lock_try_acquire_recursive (_LOCK_T lock)
{
	if (lock == 0)
		return 1;
	int me = __onyx_owner_id ();
	if (lock->count > 0 && lock->owner == me)
	{
		lock->count++;
		return 1;
	}
	if (!__retarget_lock_try_acquire (lock))
		return 0;
	lock->owner = me;
	lock->count = 1;
	return 1;
}

void __retarget_lock_release_recursive (_LOCK_T lock)
{
	if (lock == 0 || lock->count == 0)
		return;
	if (--lock->count == 0)
	{
		lock->owner = 0;
		__onyx_unlock (&lock->lk);
	}
}
