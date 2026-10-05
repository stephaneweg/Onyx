/*
 * posix_internal.h -- libonyxposix's private declarations (user/libc/posix, docs/POSIX-PLAN.md
 * §3.4; docs/03 §5.4). Not installed into the sysroot.
 *
 * The pieces:
 *  - the thread control block (struct __onyx_thread) below each thread's TCB, found from
 *    TPIDR_EL0 (tls.c); errno per thread (errno.c in tls.c); the emutls override (emutls.c);
 *  - the descriptor table (fd.c): fds -> reference-counted open-file descriptions over the kapi
 *    handles (v75 files, the old file calls as a fallback, pipes, sockets, /dev pseudo files);
 *  - helpers: the v75 error convention (-KAPI_Exxx = -errno), Onyx paths (path.c), the clock
 *    (time.c), the app-core RPC of the newlib stubs (newlib_syscalls.c).
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
#ifndef _ONYX_POSIX_INTERNAL_H
#define _ONYX_POSIX_INTERNAL_H

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <newlib.h>
#include "appkit/appkit.h"

/* The toolchain in use, from newlib's configuration (no compiler macro tells them apart):
 * WP-TC's aarch64-onyx-elf has newlib built --enable-newlib-reent-thread-local (errno and the
 * _reent members are __thread variables) and GCC --enable-tls: native TLS, no emutls override,
 * newlib's own __errno. The interim aarch64-none-elf has neither (emutls.c, tls.c's __errno). */
#if defined(_WANT_REENT_THREAD_LOCAL)
#define ONYX_NATIVE_TLS		1
#endif

#define ONYX_PAGE		65536UL		/* the kernel's page (64 KB granule) */
#define ONYX_PATH_MAX		1024
#define ONYX_FD_MAX		1024		/* FD_SETSIZE, RLIMIT_NOFILE */
#define ONYX_KEYS_MAX		PTHREAD_KEYS_MAX
#define ONYX_STACK_DEFAULT	(8UL << 20)	/* a pthread's stack (v75: lazy) */
#define ONYX_STACK_DEFAULT_V67	(1UL << 20)	/* ... on a v67 kernel the stack is mapped at once */
#define ONYX_STACK_MAX		(16UL << 20)	/* the kernel's maximum */

/* ---- the v75 error convention ---------------------------------------------------------- */
/* A v75 kapi returns >= 0, or -KAPI_Exxx where KAPI_Exxx is newlib's errno value. */
static inline long long __onyx_sys (long long r)
{
	if (r < 0)
	{
		errno = (int) -r;
		return -1;
	}
	return r;
}
#define ONYX_NOSYS(r)		((r) == -KAPI_ENOSYS)
#define ONYX_ERR(e)		(errno = (e), -1)

/* ---- threads ----------------------------------------------------------------------------- */
#define ONYX_THREAD_MAGIC	0x4F4E5854u	/* "ONXT" */

struct __onyx_thread
{
	struct __onyx_thread *self;
	unsigned magic;
	int tid;				/* the kernel's thread id (1: the main thread) */
	int err;				/* errno (the worker threads'; see tls.c) */
	volatile int state;			/* ONYX_T_* (the futex word joiners sleep on) */
	int detached;
	int is_main;
	void *(*start) (void *);
	void *arg;
	void *result;
	void *specific[ONYX_KEYS_MAX];		/* pthread_setspecific */
	void **emutls;				/* __thread under the interim toolchain (emutls.c) */
	unsigned long emutls_n;
	unsigned long long stack_lo, stack_hi, guard;
	unsigned long stack_size;
	void *block;				/* the malloc'd block (this + TCB + TLS); 0: static */
	struct __onyx_thread *next;		/* every thread of the process (tls.c's list) */
	struct __onyx_cleanup *cleanup;		/* pthread_cleanup_push */
	int prio;
	int cancel_state;
	char name[32];
} __attribute__ ((aligned (16)));

#define ONYX_T_RUNNING		0
#define ONYX_T_FINISHED		1		/* ended: result stored, not joined yet */

extern struct __onyx_thread *__onyx_main_thread;

/* The calling thread. On an app core (kapi_core_run: no kapi call, TPIDR_EL0 is not ours
 * until the kernel's WP-MEM sets it) the code shares the main thread's block. */
static inline struct __onyx_thread *__onyx_self (void)
{
#if defined(__aarch64__)
	if (kapi__core () != 0)
		return __onyx_main_thread;
	unsigned long tp;
	__asm__ volatile ("mrs %0, tpidr_el0" : "=r" (tp));
	if (tp == 0)
		return __onyx_main_thread;
	return (struct __onyx_thread *) (tp - sizeof (struct __onyx_thread));
#else
	return __onyx_main_thread;
#endif
}

/* The id a mutex stores as its owner: the tid on core 0, the core (negated) on an app core. */
static inline int __onyx_owner_id (void)
{
	unsigned core = kapi__core ();
	if (core != 0)
		return -(int) core;
	struct __onyx_thread *t = __onyx_self ();
	return t ? t->tid : 1;
}

/* tls.c */
void __onyx_tls_init_main (void);			/* crt0posix: the main thread's TP */
struct __onyx_thread *__onyx_thread_alloc (void);	/* a block: struct + TCB + TLS image */
void __onyx_thread_free (struct __onyx_thread *);
unsigned long __onyx_thread_tp (struct __onyx_thread *);
void __onyx_thread_list_add (struct __onyx_thread *);
void __onyx_thread_list_remove (struct __onyx_thread *);
struct __onyx_thread *__onyx_thread_list_first (void);
void __onyx_thread_list_lock (void);
void __onyx_thread_list_unlock (void);
void __onyx_emutls_free (struct __onyx_thread *);	/* emutls.c */
void __onyx_keys_run_destructors (struct __onyx_thread *);	/* pthread.c */

/* ---- synchronisation helpers (pthread_sync.c) --------------------------------------------- */
/* Sleep on a word while it equals `expected` (the futex; a spin on an app core). ms:
 * KAPI_WAIT_FOREVER or a timeout. -> 0 woken / changed, 1 timeout. */
int __onyx_futex_wait (volatile unsigned *addr, unsigned expected, unsigned ms);
void __onyx_futex_wake (volatile unsigned *addr);
/* ms left until an absolute time on `clock` (rounded up); -1 = passed. */
long long __onyx_ms_until (clockid_t clock, const struct timespec *abs);
/* A small internal lock (a word: 0 free): the futex mutex without the pthread type. */
void __onyx_lock (volatile unsigned *l);
void __onyx_unlock (volatile unsigned *l);

/* ---- time (time.c) ----------------------------------------------------------------------- */
unsigned long long __onyx_mono_ns (void);		/* CLOCK_MONOTONIC */
long long __onyx_real_ns (void);			/* CLOCK_REALTIME */
int __onyx_tz_minutes (void);
void __onyx_sleep_ns (unsigned long long ns);

/* ---- paths (path.c) ---------------------------------------------------------------------- */
/* An application path made an Onyx one in buf: "/tmp/x" -> "RAM:/tmp/x" (created at the first
 * use); others as they are (the kernel resolves "x", "./x", "/x" against the cwd). Returns buf,
 * or 0 (ENAMETOOLONG). */
char *__onyx_path (const char *in, char *buf, size_t cap);
/* "/dev/null" & co -> an ONYX_FD_* pseudo type, else 0 */
int __onyx_dev_type (const char *path);
/* The absolute, normalised form ("SD:/a/b") of a path. 0 / -1 (errno). */
int __onyx_abspath (const char *in, char *out, size_t cap);
/* Join a directory descriptor's path (AT_FDCWD: none) and a relative name. */
char *__onyx_at_path (int dirfd, const char *name, char *buf, size_t cap);

/* ---- descriptors (fd.c) ------------------------------------------------------------------ */
enum
{
	ONYX_FD_NONE = 0,
	ONYX_FD_FILE,			/* v75 file_open handle */
	ONYX_FD_LFILE,			/* the old file calls (a kernel without v75 files) */
	ONYX_FD_DIR,			/* a directory opened as a descriptor (its path) */
	ONYX_FD_PIPE_R,			/* the ends of a kernel pipe stream */
	ONYX_FD_PIPE_W,
	ONYX_FD_STREAM,			/* another kernel stream (stdin_stream...) */
	ONYX_FD_SOCKET,			/* v75 socket number */
	ONYX_FD_LSOCKET,		/* the old tcp_* sockets (a kernel without v75 sockets) */
	ONYX_FD_NULL,			/* /dev/null */
	ONYX_FD_ZERO,			/* /dev/zero */
	ONYX_FD_RANDOM,			/* /dev/urandom, /dev/random */
	ONYX_FD_CONSOLE,		/* 0, 1, 2: the task's stdin / stdout */
	ONYX_FD_SHM			/* (v76) a shared memory object: memfd_create, shm_open */
};

struct __onyx_pipe
{
	void *h;			/* the kernel stream */
	volatile int readers, writers;
	int eof_sent;
	int remote;			/* (v76) an end received from another process: its own stream handle */
	int child_reader;		/* the read end given to a child as its stdin (posix_spawn): a
					   reader elsewhere, so a write is not EPIPE when ours are closed */
	int child_writer;		/* the write end given to a child as its stdout: closing ours
					   sends no end of file (the child's exit ends its stream) */
};

struct __onyx_ofd
{
	int type;
	volatile int refs;
	int flags;			/* newlib's O_ACCMODE | O_APPEND | O_NONBLOCK bits */
	long long h;			/* FILE: handle; SOCKET / LSOCKET: socket number */
	int kappend;			/* FILE: the kernel handle was opened with KAPI_O_APPEND */
	void *stream;			/* STREAM: handle; CONSOLE: the stdin stream (for poll) */
	struct __onyx_pipe *pipe;	/* PIPE_R / PIPE_W */
	char *path;			/* FILE / LFILE / DIR: the Onyx path (fstat, fchdir, dirfd) */
	volatile unsigned lock;		/* serialises LFILE and carry-buffer operations */
	int console;			/* CONSOLE: 0 stdin, 1 stdout, 2 stderr */
	/* LFILE: the old calls. A read-only description reads through a kapi_open handle with
	 * kapi_seek; a writable one holds the whole file (written back at fsync / close). */
	void *lh;
	unsigned char *buf;
	long long size, cap, pos, kpos;
	int dirty, whole, unlinked;
	/* a carry buffer: bytes read ahead by poll's user-space emulation (pipes, the old
	 * sockets) or kept by MSG_PEEK on an old socket */
	unsigned char *carry;
	unsigned carry_n, carry_off;
	int eof;			/* the peer ended (old sockets, pipes seen by poll) */
	/* sockets */
	int sotype;			/* SOCK_STREAM / SOCK_DGRAM */
	int connected, listening, so_error;
	unsigned char peer[16];		/* struct sockaddr_in of the peer (old sockets) */
	unsigned rcvtimeo, sndtimeo;	/* ms, 0 = none (old sockets) */
};

/* The description of a descriptor, with a reference (put it back with __onyx_fd_put), or 0
 * and errno = EBADF. */
struct __onyx_ofd *__onyx_fd_get (int fd);
void __onyx_fd_put (struct __onyx_ofd *d);
struct __onyx_ofd *__onyx_ofd_new (int type, int flags);
/* Install a new description (its reference moves to the table) at the lowest free descriptor
 * >= min -> the descriptor, or -1 (EMFILE; the description is released). */
int __onyx_fd_install (struct __onyx_ofd *d, int min, int cloexec);
int __onyx_fd_close (int fd);
void __onyx_fd_init (void);
int __onyx_fd_cloexec (int fd, int set);	/* set -1: ask */
int __onyx_fd_place (struct __onyx_ofd *d, int fd, int cloexec);	/* (v76) at fd exactly */
void __onyx_ofd_lock (struct __onyx_ofd *d);
void __onyx_ofd_unlock (struct __onyx_ofd *d);

/* I/O on a description (file.c) */
ssize_t __onyx_read (struct __onyx_ofd *d, void *buf, size_t n);
ssize_t __onyx_write (struct __onyx_ofd *d, const void *buf, size_t n);
ssize_t __onyx_pread (struct __onyx_ofd *d, void *buf, size_t n, off_t off);
ssize_t __onyx_pwrite (struct __onyx_ofd *d, const void *buf, size_t n, off_t off);
off_t __onyx_lseek (struct __onyx_ofd *d, off_t off, int whence);
int __onyx_fstat (struct __onyx_ofd *d, struct stat *st);
int __onyx_ofd_release (struct __onyx_ofd *d);	/* the last reference: close the kernel object */
int __onyx_open (const char *path, int flags, int mode);
int __onyx_lfile_flush (struct __onyx_ofd *d);
int __onyx_lfile_unlinked (const char *onyxpath);	/* fd.c: the LFILEs of a path unlinked -> how many */

/* stat (stat.c) */
void __onyx_stat_from_kapi (const struct kapi_stat *ks, struct stat *st);
int __onyx_path_stat (const char *onyxpath, struct stat *st);	/* an Onyx path; -1 + errno */
int __onyx_path_is_dir (const char *onyxpath);

/* sockets (socket.c) */
ssize_t __onyx_sock_recv (struct __onyx_ofd *d, void *buf, size_t n, int flags, void *from);
ssize_t __onyx_sock_send (struct __onyx_ofd *d, const void *buf, size_t n, int flags, const void *to);
int __onyx_sock_close (struct __onyx_ofd *d);
int __onyx_sock_set_nonblock (struct __onyx_ofd *d, int on);
/* poll's helpers for the old sockets and the pipes (user-space readiness) */
int __onyx_carry_fill (struct __onyx_ofd *d);	/* -> 1 data, 0 none, -1 end / error */
size_t __onyx_carry_take (struct __onyx_ofd *d, void *buf, size_t n, int peek);

/* IPC (ipc.c, v76): descriptors carried by a local socket (SCM_RIGHTS) or given to a child */
#define ONYX_IS_LOCAL_SOCK(d)	((d)->type == ONYX_FD_SOCKET && (d)->h >= KAPI_SOCK_LOCAL_BASE)
struct msghdr;
/* The kernel handle behind a description, for sendmsg / posix_spawn -> 0, or -1 (EOPNOTSUPP: a
 * description that cannot be passed: a directory, /dev/null, an old-call file or socket...). */
int __onyx_ofd_xfer (struct __onyx_ofd *d, struct kapi_handle_xfer *x);
/* A new description for a handle received (its reference moves in) -> it, or 0 (errno). */
struct __onyx_ofd *__onyx_ofd_from_xfer (const struct kapi_handle_xfer *x);
/* The handles the spawner gave (get_handles), installed at their descriptors (start-up). */
void __onyx_fd_inherit (void);
/* posix_spawn: the descriptors a child inherits (no FD_CLOEXEC, passable, >= 3), not in skip[]
 * (a bitmap of ONYX_FD_MAX bits) -> how many written to out (at most max). */
int __onyx_fd_collect (struct kapi_handle_xfer *out, int max, const unsigned char *skip);
ssize_t __onyx_local_sendmsg (struct __onyx_ofd *d, const struct msghdr *m, int flags);
ssize_t __onyx_local_recvmsg (struct __onyx_ofd *d, struct msghdr *m, int flags);

/* ---- environment, arguments, start-up (env.c, crt0posix) --------------------------------- */
void __onyx_env_init (void);
int __onyx_args_init (char ***argv);		/* -> argc */
extern char *__onyx_progname;
extern char *program_invocation_name;
extern char *program_invocation_short_name;

/* ---- the app-core RPC (newlib_syscalls.c) ------------------------------------------------ */
long onyx_rpc3 (long (*fn) (long, long, long), long a, long b, long c);

/* ---- misc ---------------------------------------------------------------------------------- */
int __onyx_vm_available (void);			/* the kernel has vm_map (mman.c) */
int __onyx_signal_default (int sig);		/* signal.c: run a signal's action */

#endif /* _ONYX_POSIX_INTERNAL_H */
