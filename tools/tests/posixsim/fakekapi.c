/*
 * fakekapi.c -- posixsim: a stand-in Onyx kernel to run libonyxposix programs on the PC, as aarch64
 * Linux processes under qemu-user (tools/tests/posixsim/run.sh; docs/03 §5.4).
 *
 * The program is linked as for Onyx (crt0posix, onyx-posix.ld, libonyxposix, newlib) plus this
 * file and start.S, whose entry maps a kapi table at KAPI_TABLE_VA (kern/kapi_abi.h) and fills it
 * with functions made of raw Linux system calls, then jumps to the program's _start. So the
 * library runs as on the Pi -- its start-up, TLS on TPIDR_EL0, the descriptor table, newlib's
 * stdio, the futex-based locks with real concurrency -- against:
 *   POSIXSIM_LEVEL=76 (default): the v75 calls (files, sockets, poll, threads with TLS, vm_*,
 *                     spawn / wait, clock, environment), as WP-MEM / WP-FILE/PROC / WP-NET
 *                     specify them (docs/POSIX-PLAN.md §3), and v76's IPC (§14: local sockets
 *                     over Linux AF_UNIX socketpairs, handles carried with SCM_RIGHTS -- their
 *                     kinds and tags in a header (packets) or a side channel (streams) --,
 *                     shared memory over memfd_create / files in $ROOT/.shm, spawn_ex2 through
 *                     inherited descriptors and POSIXSIM_HANDLES);
 *   POSIXSIM_LEVEL=75: no v76;
 *   POSIXSIM_LEVEL=74: none of them (the table says v74): libonyxposix's fallbacks on the old calls.
 * Onyx paths map to POSIXSIM_ROOT (default /tmp/posixsim): "SD:/x" -> $ROOT/SD/x, "RAM:/y" ->
 * $ROOT/RAM/y; the working directory starts at SD:/. A spawned program is run by the qemu binary
 * (POSIXSIM_QEMU) on the host file $ROOT/SD/... DNS: "localhost" and numeric addresses only.
 * Not emulated: app cores, windows, sound; the RAM volume's limits; FAT's case-insensitivity.
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
#include <string.h>
#include <kern/kapi_abi.h>
#include "sys.h"

#define KT_W	((struct TKApiTable *) KAPI_TABLE_VA)

/* ---- small helpers (no C library calls that could reach the kapi) ---- */
static char s_root[256] = "/tmp/posixsim";
static char s_qemu[256] = "/usr/bin/qemu-aarch64-static";
static char s_cwd[512] = "SD:/";
static long s_argc;
static char **s_argv, **s_envp;
static int s_level = 76;
static int s_tz = 60;
static int s_nonet = 1;
static unsigned long long s_bootCnt;

static int lerr (long e)		/* a Linux errno -> newlib's */
{
	switch (e)
	{
	case 36: return KAPI_ENAMETOOLONG;
	case 38: return KAPI_ENOSYS;
	case 39: return KAPI_ENOTEMPTY;
	case 88: return KAPI_ENOTSOCK;
	case 89: return KAPI_EDESTADDRREQ;
	case 90: return KAPI_EMSGSIZE;
	case 92: return KAPI_ENOPROTOOPT;
	case 93: return KAPI_EPROTONOSUPPORT;
	case 95: return KAPI_EOPNOTSUPP;
	case 97: return KAPI_EAFNOSUPPORT;
	case 98: return KAPI_EADDRINUSE;
	case 99: return KAPI_EADDRNOTAVAIL;
	case 100: return KAPI_ENETDOWN;
	case 101: return KAPI_ENETUNREACH;
	case 103: return KAPI_ECONNABORTED;
	case 104: return KAPI_ECONNRESET;
	case 105: return KAPI_ENOBUFS;
	case 106: return KAPI_EISCONN;
	case 107: return KAPI_ENOTCONN;
	case 110: return KAPI_ETIMEDOUT;
	case 111: return KAPI_ECONNREFUSED;
	case 113: return KAPI_EHOSTUNREACH;
	case 114: return KAPI_EALREADY;
	case 115: return KAPI_EINPROGRESS;
	default: return (int) e;		/* 1..34: the same values */
	}
}
static long kret (long r) { return r < 0 ? -lerr (-r) : r; }

static const char *getenv_ (const char *name)
{
	size_t n = strlen (name);
	for (char **e = s_envp; e && *e; e++)
		if (strncmp (*e, name, n) == 0 && (*e)[n] == '=')
			return *e + n + 1;
	return 0;
}

static int atoi_ (const char *s)
{
	int v = 0, neg = 0;
	if (*s == '-') { neg = 1; s++; }
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}

static void put (const char *s) { sc3 (L_write, 2, s, strlen (s)); }

static long l_open (const char *p, int flags, int mode) { return sc4 (L_openat, L_AT_FDCWD, p, flags, mode); }

/* ---- paths: the kernel's ResolvePath, then $ROOT/<VOL>/... ---- */
static unsigned volume_prefix (const char *p)
{
	unsigned i = 0;
	while (p[i] && p[i] != '/' && p[i] != ':' && i < 16)
		i++;
	return p[i] == ':' && i > 0 ? i + 1 : 0;
}

static void resolve (const char *in, char *out, unsigned cap)	/* -> "VOL:/a/b" */
{
	char raw[1024];
	unsigned r = 0, nv = volume_prefix (in);
	#define PUT(s, n) do { for (unsigned k_ = 0; k_ < (n) && (s)[k_] && r < sizeof raw - 1; k_++) raw[r++] = (s)[k_]; } while (0)
	if (nv)
	{
		for (unsigned k = 0; k < nv; k++)
			raw[r++] = in[k] >= 'a' && in[k] <= 'z' ? (char) (in[k] - 32) : in[k];
		PUT (in + nv, ~0u);
	}
	else if (in[0] == '/')
	{
		unsigned n = volume_prefix (s_cwd);
		PUT (s_cwd, n);
		PUT (in, ~0u);
	}
	else
	{
		PUT (s_cwd, ~0u);
		raw[r++] = '/';
		PUT (in, ~0u);
	}
	raw[r] = 0;
	nv = volume_prefix (raw);
	unsigned o = 0, starts[64];
	int depth = 0;
	for (unsigned k = 0; k < nv; k++)
		out[o++] = raw[k];
	if (nv == 4 && raw[0] == 'S' && raw[1] == 'D' && raw[2] == '0')
	{
		o = 0;
		out[o++] = 'S'; out[o++] = 'D'; out[o++] = ':';
	}
	unsigned base = o, i = nv;
	while (raw[i])
	{
		while (raw[i] == '/') i++;
		if (!raw[i]) break;
		unsigned j = i;
		while (raw[j] && raw[j] != '/') j++;
		unsigned len = j - i;
		if (len == 1 && raw[i] == '.')
			;
		else if (len == 2 && raw[i] == '.' && raw[i + 1] == '.')
		{
			if (depth > 0) o = starts[--depth];
		}
		else
		{
			if (depth < 64) starts[depth++] = o;
			if (o + len + 2 < cap)
			{
				out[o++] = '/';
				memcpy (out + o, raw + i, len);
				o += len;
			}
		}
		i = j;
	}
	if (o == base)
		out[o++] = '/';
	out[o] = 0;
}

static char *host (const char *p, char *buf)		/* an app path -> the host's (buf: 1024) */
{
	char abs[1024];
	resolve (p ? p : "", abs, sizeof abs);
	unsigned nv = volume_prefix (abs);
	unsigned o = 0;
	for (const char *s = s_root; *s; s++) buf[o++] = *s;
	buf[o++] = '/';
	for (unsigned k = 0; k + 1 < nv; k++) buf[o++] = abs[k];
	const char *rest = abs + nv;
	while (*rest && o < 1020) buf[o++] = *rest++;
	buf[o] = 0;
	return buf;
}

/* ---- handles ---- */
enum { H_FREE, H_FILE, H_DIR, H_STREAM, H_PROC, H_OFILE, H_LSOCK, H_SHM };
struct handle
{
	int type;
	int fd, fd2;				/* STREAM: read end, write end; LSOCK: data, side channel */
	int stype, acc;				/* LSOCK: the socket type; SHM: the access */
	int pid, done, status;			/* PROC */
	int dpos, dlen;				/* DIR */
	char dbuf[4096];
};
static struct handle s_h[256];
static volatile int s_hLock;

static void lock (volatile int *l) { while (__atomic_exchange_n (l, 1, __ATOMIC_ACQUIRE)) sc0 (L_sched_yield); }
static void unlock (volatile int *l) { __atomic_store_n (l, 0, __ATOMIC_RELEASE); }

static long hnew (int type)
{
	lock (&s_hLock);
	for (int i = 0; i < 256; i++)
		if (s_h[i].type == H_FREE)
		{
			memset (&s_h[i], 0, sizeof s_h[i]);
			s_h[i].type = type;
			s_h[i].fd = s_h[i].fd2 = -1;
			unlock (&s_hLock);
			return i + 1;
		}
	unlock (&s_hLock);
	return 0;
}
static struct handle *hget (long v, int type)
{
	if (v < 1 || v > 256 || s_h[v - 1].type != type)
		return 0;
	return &s_h[v - 1];
}
static void hfree (long v) { if (v >= 1 && v <= 256) s_h[v - 1].type = H_FREE; }

/* ---- time ---- */
static unsigned long long cntvct (void) { unsigned long long c; __asm__ volatile ("isb; mrs %0, cntvct_el0" : "=r" (c)); return c; }
static unsigned long long cntfrq (void) { unsigned long long f; __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f)); return f; }
static long long now_ns (int clock)
{
	struct l_timespec t;
	sc2 (L_clock_gettime, clock, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}
static void sleep_ns (long long ns)
{
	struct l_timespec t = { ns / 1000000000LL, ns % 1000000000LL };
	sc2 (L_nanosleep, &t, 0);
}

/* ---- the old calls ---- */
static unsigned *f_create_window (int w, int h, const char *t) { (void) w; (void) h; (void) t; return 0; }
static void f_yield (void) { sc0 (L_sched_yield); }
static void f_msleep (unsigned ms) { sleep_ns ((long long) ms * 1000000LL); }
static unsigned f_get_ticks (void) { return (unsigned) (now_ns (1) / 10000000LL); }
static void f_exit (int s) { sc1 (L_exit_group, s); for (;;) ; }
static int f_write (int fd, const void *b, unsigned n) { (void) fd; return (int) sc3 (L_write, 2, b, n); }
static int f_stdout_write (const void *b, unsigned n) { return (int) sc3 (L_write, 1, b, n); }
static int f_stdin_read (void *b, unsigned n) { long r = sc3 (L_read, 0, b, n); return r < 0 ? 0 : (int) r; }

static int f_get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *s)
{
	long long t = now_ns (0) / 1000000000LL + s_tz * 60;	/* (the local time) */
	long days = (long) (t / 86400), rem = (long) (t % 86400);
	/* civil from days (Hinnant) */
	long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
	long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long yy = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	long dd = doy - (153 * mp + 2) / 5 + 1, mm = mp + (mp < 10 ? 3 : -9);
	*y = (int) (yy + (mm <= 2)); *mo = (int) mm; *d = (int) dd;
	*h = (int) (rem / 3600); *mi = (int) (rem / 60 % 60); *s = (int) (rem % 60);
	return 1;
}

static void *f_sbrk (long inc)
{
	static unsigned long brk = 0x280000000UL;
	unsigned long old = brk;
	if (brk + inc > 0x280000000UL + (2UL << 30))
		return (void *) -1;
	brk += inc;
	return (void *) old;
}

static int f_meminfo (unsigned long *t, unsigned long *f, unsigned long *a, unsigned *p)
{
	if (t) *t = 4UL << 20;
	if (f) *f = 2UL << 20;
	if (a) *a = 1UL << 20;
	if (p) *p = 64;
	return 1;
}

static void *f_open (const char *p)
{
	char hp[1024];
	long fd = l_open (host (p, hp), L_O_RDONLY | L_O_CLOEXEC, 0);
	if (fd < 0)
		return 0;
	struct l_stat st;
	sc2 (L_fstat, fd, &st);
	if ((st.st_mode & 0170000) == 0040000)
	{
		sc1 (L_close, fd);
		return 0;
	}
	long h = hnew (H_FILE);
	if (!h) { sc1 (L_close, fd); return 0; }
	s_h[h - 1].fd = (int) fd;
	return (void *) h;
}
static int f_read (void *h, void *b, unsigned n)
{
	struct handle *x = hget ((long) h, H_FILE);
	if (!x) return -1;
	long r = sc3 (L_read, x->fd, b, n);
	return r < 0 ? -1 : (int) r;
}
static unsigned long long f_fsize64 (void *h)
{
	struct handle *x = hget ((long) h, H_FILE);
	struct l_stat st;
	if (!x || sc2 (L_fstat, x->fd, &st) < 0) return 0;
	return (unsigned long long) st.st_size;
}
static unsigned f_fsize (void *h) { return (unsigned) f_fsize64 (h); }
static int f_seek (void *h, unsigned long long pos)
{
	struct handle *x = hget ((long) h, H_FILE);
	if (!x) return -1;
	return sc3 (L_lseek, x->fd, pos, 0) < 0 ? -1 : 0;
}
static void f_close (void *h)
{
	struct handle *x = hget ((long) h, H_FILE);
	if (x) { sc1 (L_close, x->fd); hfree ((long) h); }
}
static int f_save_file (const char *p, const void *b, unsigned n)
{
	char hp[1024];
	long fd = l_open (host (p, hp), L_O_WRONLY | L_O_CREAT | L_O_TRUNC | L_O_CLOEXEC, 0644);
	if (fd < 0) return -1;
	long w = n ? sc3 (L_write, fd, b, n) : 0;
	sc1 (L_close, fd);
	return w == (long) n ? (int) n : -1;
}
static int f_mkdir (const char *p) { char hp[1024]; return sc3 (L_mkdirat, L_AT_FDCWD, host (p, hp), 0755) < 0 ? -1 : 0; }
static int f_remove (const char *p)
{
	char hp[1024];
	host (p, hp);
	long r = sc3 (L_unlinkat, L_AT_FDCWD, hp, 0);
	if (r == -21)
		r = sc3 (L_unlinkat, L_AT_FDCWD, hp, 0x200);
	return r < 0 ? -1 : 0;
}
static int f_rename (const char *a, const char *b)
{
	char ha[1024], hb[1024];
	return sc4 (L_renameat, L_AT_FDCWD, host (a, ha), L_AT_FDCWD, host (b, hb)) < 0 ? -1 : 0;
}
static int f_chdir (const char *p)
{
	char hp[1024], abs[512];
	struct l_stat st;
	if (sc4 (L_newfstatat, L_AT_FDCWD, host (p, hp), &st, 0) < 0 || (st.st_mode & 0170000) != 0040000)
		return 0;
	resolve (p, abs, sizeof abs);
	strcpy (s_cwd, abs);
	return 1;
}
static int f_getcwd (char *b, unsigned n)
{
	unsigned l = (unsigned) strlen (s_cwd);
	if (!b || n == 0) return 0;
	if (l >= n) l = n - 1;
	memcpy (b, s_cwd, l);
	b[l] = 0;
	return (int) l;
}

/* directories */
static void *f_opendir (const char *p)
{
	char hp[1024];
	long fd = l_open (host (p, hp), L_O_RDONLY | L_O_DIRECTORY | L_O_CLOEXEC, 0);
	if (fd < 0) return 0;
	long h = hnew (H_DIR);
	if (!h) { sc1 (L_close, fd); return 0; }
	s_h[h - 1].fd = (int) fd;
	return (void *) h;
}
/* the next entry (not "." / "..") -> 1 and its name, type; 0 end */
static int next_ent (struct handle *x, char *name, int *isdir)
{
	for (;;)
	{
		if (x->dpos >= x->dlen)
		{
			long n = sc3 (L_getdents64, x->fd, x->dbuf, sizeof x->dbuf);
			if (n <= 0) return 0;
			x->dlen = (int) n;
			x->dpos = 0;
		}
		char *e = x->dbuf + x->dpos;
		unsigned short reclen = *(unsigned short *) (e + 16);
		unsigned char type = *(unsigned char *) (e + 18);
		char *nm = e + 19;
		x->dpos += reclen;
		if (!strcmp (nm, ".") || !strcmp (nm, ".."))
			continue;
		strncpy (name, nm, 255);
		name[255] = 0;
		*isdir = type == 4;
		return 1;
	}
}
static int f_readdir (void *d, struct kapi_dirent *out)
{
	struct handle *x = hget ((long) d, H_DIR);
	char name[256];
	int isdir;
	if (!x || !next_ent (x, name, &isdir)) return 0;
	memset (out, 0, sizeof *out);
	strncpy (out->name, name, sizeof out->name - 1);
	out->is_dir = isdir;
	struct l_stat st;
	if (!isdir && sc4 (L_newfstatat, x->fd, name, &st, 0) == 0)
		out->size = (unsigned) st.st_size;
	return 1;
}
static void f_closedir (void *d)
{
	struct handle *x = hget ((long) d, H_DIR);
	if (x) { sc1 (L_close, x->fd); hfree ((long) d); }
}

/* streams */
static void *f_pipe (void)
{
	int fds[2];
	if (sc2 (L_pipe2, fds, L_O_CLOEXEC) < 0) return 0;
	long h = hnew (H_STREAM);
	s_h[h - 1].fd = fds[0];
	s_h[h - 1].fd2 = fds[1];
	return (void *) h;
}
static void *f_file_in (const char *p)
{
	char hp[1024];
	long fd = l_open (host (p, hp), L_O_RDONLY | L_O_CLOEXEC, 0);
	if (fd < 0) return 0;
	long h = hnew (H_STREAM);
	s_h[h - 1].fd = (int) fd;
	return (void *) h;
}
static void *f_file_out (const char *p, int append)
{
	char hp[1024];
	long fd = l_open (host (p, hp), L_O_WRONLY | L_O_CREAT | L_O_CLOEXEC | (append ? L_O_APPEND : L_O_TRUNC), 0644);
	if (fd < 0) return 0;
	long h = hnew (H_STREAM);
	s_h[h - 1].fd2 = (int) fd;
	return (void *) h;
}
static int f_stream_read (void *h, void *b, unsigned n)
{
	struct handle *x = hget ((long) h, H_STREAM);
	if (!x || x->fd < 0) return 0;
	long r = sc3 (L_read, x->fd, b, n);
	return r < 0 ? 0 : (int) r;
}
static int f_stream_read_nb (void *h, void *b, unsigned n)
{
	struct handle *x = hget ((long) h, H_STREAM);
	if (!x || x->fd < 0) return 0;
	struct l_pollfd p = { x->fd, 1, 0 };
	struct l_timespec z = { 0, 0 };
	if (sc4 (L_ppoll, &p, 1, &z, 0) <= 0)
		return -1;
	long r = sc3 (L_read, x->fd, b, n);
	return r < 0 ? -1 : (int) r;
}
static int f_stream_write (void *h, const void *b, unsigned n)
{
	struct handle *x = hget ((long) h, H_STREAM);
	if (!x || x->fd2 < 0) return -1;
	long r = sc3 (L_write, x->fd2, b, n);
	return r < 0 ? -1 : (int) r;
}
static void f_stream_eof (void *h)
{
	struct handle *x = hget ((long) h, H_STREAM);
	if (x && x->fd2 >= 0 && x->fd2 > 2) { sc1 (L_close, x->fd2); x->fd2 = -1; }
}
static void f_stream_close (void *h)
{
	struct handle *x = hget ((long) h, H_STREAM);
	if (!x) return;
	if (x->fd > 2) sc1 (L_close, x->fd);
	if (x->fd2 > 2) sc1 (L_close, x->fd2);
	hfree ((long) h);
}
static void *f_stdin_stream (void) { long h = hnew (H_STREAM); s_h[h - 1].fd = 0; return (void *) h; }
static void *f_stdout_stream (void) { long h = hnew (H_STREAM); s_h[h - 1].fd2 = 1; return (void *) h; }

/* processes: the program is run by the qemu binary */
static long do_spawn2 (const char *path, char **argv, char **envp, const char *cwd, void *in, void *out,
		      const int *keep, int nkeep, const char *henv);
static long do_spawn (const char *path, char **argv, char **envp, const char *cwd, void *in, void *out)
{
	return do_spawn2 (path, argv, envp, cwd, in, out, 0, 0, 0);
}
static long do_spawn2 (const char *path, char **argv, char **envp, const char *cwd, void *in, void *out,
		      const int *keep, int nkeep, const char *henv)
{
	char hp[1024];
	host (path, hp);
	struct l_stat st;
	if (sc4 (L_newfstatat, L_AT_FDCWD, hp, &st, 0) < 0)
		return -KAPI_ENOENT;
	struct handle *hi = in ? hget ((long) in, H_STREAM) : 0;
	struct handle *ho = out ? hget ((long) out, H_STREAM) : 0;
	char hcwd[1024];
	host (cwd ? cwd : s_cwd, hcwd);
	char *av[260];
	int n = 0;
	av[n++] = s_qemu;
	av[n++] = hp;
	for (int i = 1; argv && argv[i] && n < 258; i++)
		av[n++] = argv[i];
	av[n] = 0;
	/* the child's own Onyx argv[0] and working directory */
	static char *ev[600];
	static char a0[1100], cw[600];
	int m = 0;
	strcpy (a0, "POSIXSIM_ARGV0=");
	strcat (a0, path);
	strcpy (cw, "POSIXSIM_CWD=");
	resolve (cwd ? cwd : s_cwd, cw + 13, sizeof cw - 13);
	for (int i = 0; envp && envp[i] && m < 590; i++)
		if (strncmp (envp[i], "POSIXSIM_ARGV0=", 15) && strncmp (envp[i], "POSIXSIM_CWD=", 13)
		    && strncmp (envp[i], "POSIXSIM_HANDLES=", 17))
			ev[m++] = envp[i];
	ev[m++] = a0;
	ev[m++] = cw;
	if (henv)
		ev[m++] = (char *) henv;
	ev[m] = 0;
	long pid = sc5 (L_clone, 17 /* SIGCHLD */, 0, 0, 0, 0);
	if (pid == 0)
	{
		if (hi && hi->fd >= 0) sc3 (24 /* dup3 */, hi->fd, 0, 0);
		if (ho && ho->fd2 >= 0) { sc3 (24, ho->fd2, 1, 0); sc3 (24, ho->fd2, 2, 0); }
		for (int i = 0; i < nkeep; i++)
			sc3 (L_fcntl, keep[i], 2 /* F_SETFD */, 0);	/* (v76: given to the child) */
		sc3 (L_execve, s_qemu, av, ev);
		sc1 (L_exit_group, 127);
	}
	if (pid < 0)
		return kret (pid);
	long h = hnew (H_PROC);
	s_h[h - 1].pid = (int) pid;
	return h;
}

static char **split_block (const char *b, char **v, int max)
{
	int n = 0;
	while (b && *b && n < max - 1)
	{
		v[n++] = (char *) b;
		b += strlen (b) + 1;
	}
	v[n] = 0;
	return v;
}

static void *f_spawn (const char *path, const char *args, void *in, void *out)
{
	static char line[4096];
	char *av[64];
	int n = 0;
	av[n++] = (char *) path;
	strncpy (line, args ? args : "", sizeof line - 1);
	char *p = line;
	while (*p && n < 62)
	{
		while (*p == ' ') p++;
		if (!*p) break;
		char *s = p;
		if (*p == '"') { s = ++p; while (*p && *p != '"') p++; }
		else while (*p && *p != ' ') p++;
		if (*p) *p++ = 0;
		av[n++] = s;
	}
	av[n] = 0;
	long h = do_spawn (path, av, s_envp, 0, in, out);
	return h > 0 ? (void *) h : 0;
}

static int reap (struct handle *x, int nohang)
{
	if (x->done) return 1;
	int st = 0;
	long r = sc4 (L_wait4, x->pid, &st, nohang ? 1 : 0, 0);
	if (r == x->pid) { x->done = 1; x->status = st; return 1; }
	return 0;
}
static int f_wait (void *proc)
{
	struct handle *x = hget ((long) proc, H_PROC);
	if (!x) return -1;
	reap (x, 0);
	int st = x->status;
	hfree ((long) proc);
	return (st & 0x7F) ? 0 : (st >> 8) & 0xFF;
}
static int f_proc_done (void *proc)
{
	struct handle *x = hget ((long) proc, H_PROC);
	return x ? reap (x, 1) : 1;
}
static int f_get_args (char *b, unsigned n)
{
	unsigned o = 0;
	for (long i = 1; i < s_argc && o + 1 < n; i++)
	{
		if (i > 1 && o + 1 < n) b[o++] = ' ';
		for (const char *s = s_argv[i]; *s && o + 1 < n; s++) b[o++] = *s;
	}
	if (n) b[o] = 0;
	return (int) o;
}
static int f_kill_pid (int pid, int force) { return sc2 (L_kill, pid, force ? 9 : 15) == 0 ? 1 : 0; }
static int f_random (void *b, unsigned n) { long r = sc3 (L_getrandom, b, n, 0); return r < 0 ? 0 : (int) r; }
static int f_vol_info (const char *p, struct kapi_vol_info *o) { (void) p; memset (o, 0, sizeof *o); o->total = 1ULL << 30; o->free = 1ULL << 29; o->used = o->total - o->free; strcpy (o->type, "FAT32"); return 0; }
static int f_proc_stats (int pid, struct kapi_syscall_stats *o) { (void) o; return sc2 (L_kill, pid ? pid : sc0 (L_getpid), 0) == 0 ? 0 : -1; }

/* the network (the old calls) */
static int parse_ip (const char *s, unsigned *out)	/* network order */
{
	unsigned v[4], k = 0;
	if (!strcmp (s, "localhost")) s = "127.0.0.1";
	for (;;)
	{
		if (*s < '0' || *s > '9') return 0;
		unsigned x = 0;
		while (*s >= '0' && *s <= '9') x = x * 10 + (unsigned) (*s++ - '0');
		if (x > 255) return 0;
		v[k++] = x;
		if (k == 4) break;
		if (*s++ != '.') return 0;
	}
	if (*s) return 0;
	*out = v[0] | v[1] << 8 | v[2] << 16 | v[3] << 24;
	return 1;
}
static int f_net_status (char *ip, unsigned cap) { if (s_nonet) return 0; if (ip && cap > 9) strcpy (ip, "127.0.0.1"); return 1; }
static int f_net_resolve (const char *h, char *ip, unsigned cap)
{
	unsigned a;
	if (!parse_ip (h, &a) || cap < 16) return 0;
	unsigned char *b = (unsigned char *) &a;
	char t[16];
	int o = 0;
	for (int i = 0; i < 4; i++)
	{
		unsigned x = b[i];
		if (x >= 100) t[o++] = (char) ('0' + x / 100);
		if (x >= 10) t[o++] = (char) ('0' + x / 10 % 10);
		t[o++] = (char) ('0' + x % 10);
		if (i < 3) t[o++] = '.';
	}
	t[o] = 0;
	strcpy (ip, t);
	return 1;
}
static int f_tcp_connect (const char *h, unsigned port)
{
	unsigned a;
	if (!parse_ip (h, &a)) return -3;
	long s = sc3 (L_socket, 2, 1 | 02000000, 0);
	if (s < 0) return -2;
	struct l_sockaddr_in sin = { 2, (unsigned short) (port << 8 | port >> 8), a, { 0 } };
	if (sc3 (L_connect, s, &sin, sizeof sin) < 0) { sc1 (L_close, s); return -1; }
	return (int) s;
}
static int f_tcp_send (int s, const void *b, unsigned n) { long r = sc6 (L_sendto, s, (long) b, n, 0x4000, 0, 0); return r < 0 ? -1 : (int) r; }
static int f_tcp_recv (int s, void *b, unsigned n)
{
	long r = sc6 (L_recvfrom, s, (long) b, n, 0x40 /* DONTWAIT */, 0, 0);
	if (r == -11) return 0;
	return r <= 0 ? -1 : (int) r;
}
static void f_tcp_close (int s) { sc1 (L_close, s); }
static int f_tcp_listen (unsigned port)
{
	long s = sc3 (L_socket, 2, 1 | 02000000, 0);
	int one = 1;
	sc5 (L_setsockopt, s, 1, 2, &one, 4);
	struct l_sockaddr_in sin = { 2, (unsigned short) (port << 8 | port >> 8), 0, { 0 } };
	if (sc3 (L_bind, s, &sin, sizeof sin) < 0) { sc1 (L_close, s); return -6; }
	sc2 (L_listen, s, 8);
	return (int) s;
}
static int f_tcp_accept (int l, char *ip, unsigned cap)
{
	struct l_sockaddr_in sin;
	unsigned len = sizeof sin;
	long s = sc4 (L_accept4, l, &sin, &len, 02000000);
	if (s < 0) return -1;
	unsigned char *b = (unsigned char *) &sin.addr;
	if (ip && cap > 16) { char t[16]; unsigned a = b[0] | b[1] << 8 | b[2] << 16 | (unsigned) b[3] << 24; (void) a; f_net_resolve ("127.0.0.1", t, 16); strcpy (ip, t); }
	return (int) s;
}

/* ---- threads (v67 and v75) ---- */
extern long posixsim_clone (unsigned long flags, void *stack, int *ptid, unsigned long tls, int *ctid,
			    int (*fn) (void *), void *arg);
struct trec
{
	volatile int used;
	int tid;
	volatile int ltid;			/* CLONE_CHILD_CLEARTID: 0 once it ended */
	int code;
	int (*fn) (void *);
	void *arg;
	unsigned long stk, stklen;		/* the mapping */
	unsigned long lo, hi;			/* its usable stack */
};
static struct trec s_t[64];
static volatile int s_tLock;
static int s_nextTid = 2;
static int s_mainLtid;

static int tramp (void *p)
{
	struct trec *r = (struct trec *) p;
	r->code = r->fn (r->arg);
	sc1 (L_exit, 0);
	return 0;
}

static int create (int (*fn) (void *), void *arg, unsigned long ss, unsigned long tls)
{
	if (ss == 0) ss = 256 * 1024;
	if (ss < 16384) ss = 16384;
	ss = (ss + 0xFFFF) & ~0xFFFFUL;
	lock (&s_tLock);
	struct trec *r = 0;
	for (int i = 0; i < 64; i++)
		if (!s_t[i].used) { r = &s_t[i]; break; }
	if (!r) { unlock (&s_tLock); return -KAPI_EAGAIN; }
	memset (r, 0, sizeof *r);
	r->used = 1;
	r->tid = s_nextTid++;
	unlock (&s_tLock);
	/* a 64 KB-aligned top (as the kernel's thread slots), a guard page below */
	unsigned long len = ss + 0x20000;
	long m = sc6 (L_mmap, 0, (long) len, 3, 0x22 | 0x4000, -1, 0);
	if (m < 0) { r->used = 0; return -KAPI_ENOMEM; }
	r->stk = (unsigned long) m;
	r->stklen = len;
	r->hi = ((unsigned long) m + len) & ~0xFFFFUL;
	r->lo = r->hi - ss;
	r->fn = fn;
	r->arg = arg;
	r->ltid = 1;
	unsigned long flags = 0x100 | 0x200 | 0x400 | 0x800 | 0x10000 | 0x40000 | 0x80000 /* SETTLS */ |
			      0x100000 /* PARENT_SETTID */ | 0x200000 /* CHILD_CLEARTID */;
	long t = posixsim_clone (flags, (void *) r->hi, (int *) &r->ltid, tls, (int *) &r->ltid, tramp, r);
	if (t < 0) { sc2 (L_munmap, r->stk, r->stklen); r->used = 0; return -KAPI_EAGAIN; }
	return r->tid;
}

static int f_thread_create (int (*fn) (void *), void *arg, unsigned ss, const char *name)
{
	(void) name;
	int r = create (fn, arg, ss, 0);
	return r > 0 ? r : r == -KAPI_ENOMEM ? -1 : -2;
}
static int f_thread_create_ex (const struct kapi_thread_attr *a)
{
	if (!a || !a->fn) return -KAPI_EINVAL;
	return create ((int (*) (void *)) (unsigned long) a->fn, (void *) (unsigned long) a->arg,
		       a->stack_size ? a->stack_size : (8UL << 20), a->tls);
}
static struct trec *self_rec (void)
{
	long lt = sc0 (L_gettid);
	for (int i = 0; i < 64; i++)
		if (s_t[i].used && s_t[i].ltid == lt) return &s_t[i];
	return 0;
}
static void f_thread_exit (int code)
{
	if (sc0 (L_gettid) == s_mainLtid) { sc1 (L_exit_group, code); }
	struct trec *r = self_rec ();
	if (r) r->code = code;
	sc1 (L_exit, 0);
	for (;;) ;
}
static int f_thread_join (int tid, unsigned ms, int *code)
{
	struct trec *r = 0;
	for (int i = 0; i < 64; i++)
		if (s_t[i].used && s_t[i].tid == tid) r = &s_t[i];
	if (!r) return -2;
	long long end = now_ns (1) + (long long) ms * 1000000LL;
	for (;;)
	{
		int v = r->ltid;
		if (v == 0) break;
		if (ms == 0) return -1;
		struct l_timespec t = { 0, 10000000 };
		if (ms != KAPI_WAIT_FOREVER && now_ns (1) >= end) return -1;
		sc6 (L_futex, (long) &r->ltid, 0, v, (long) &t, 0, 0);
	}
	if (code) *code = r->code;
	sc2 (L_munmap, r->stk, r->stklen);
	r->used = 0;
	return 0;
}
static int f_thread_self (void)
{
	struct trec *r = self_rec ();
	return r ? r->tid : 1;
}
static int f_thread_info (int tid, struct kapi_thread_info *o)
{
	if (tid == 0) tid = f_thread_self ();
	memset (o, 0, sizeof *o);
	o->tid = tid;
	if (tid == 1)
	{
		/* the main stack: [stack] in /proc/self/maps */
		static char buf[65536];
		long fd = l_open ("/proc/self/maps", 0, 0);
		long n = fd >= 0 ? sc3 (L_read, fd, buf, sizeof buf - 1) : -1;
		if (fd >= 0) sc1 (L_close, fd);
		if (n <= 0) return -KAPI_ESRCH;
		buf[n] = 0;
		char *l = strstr (buf, "[stack]");
		if (!l) return -KAPI_ESRCH;
		while (l > buf && l[-1] != '\n') l--;
		unsigned long a = 0, b = 0;
		while (*l != '-') a = a * 16 + (unsigned long) (*l <= '9' ? *l - '0' : (*l | 32) - 'a' + 10), l++;
		l++;
		while (*l != ' ') b = b * 16 + (unsigned long) (*l <= '9' ? *l - '0' : (*l | 32) - 'a' + 10), l++;
		o->stack_lo = a;
		o->stack_hi = b;
		return 0;
	}
	for (int i = 0; i < 64; i++)
		if (s_t[i].used && s_t[i].tid == tid)
		{
			o->stack_lo = s_t[i].lo;
			o->stack_hi = s_t[i].hi;
			o->state = s_t[i].ltid == 0;
			o->guard = 0x10000;
			return 0;
		}
	return -KAPI_ESRCH;
}
static int f_thread_priority (int tid, int prio) { (void) tid; (void) prio; return 0; }
static int f_wait_word (volatile unsigned *a, unsigned expected, unsigned ms)
{
	if (((unsigned long) a & 3) != 0) return -1;
	/* As the kernel: woken by wake_word, or -- a word changed by an app core, which cannot call
	 * wake_word -- at the next 10 ms tick: the wait is made of 10 ms waits, the word looked at
	 * after each. (POSIXSIM_NOTICK=1: not looked at, as this bench was: what only an app core's
	 * unlock would wake sleeps for good.) */
	static int s_notick = -1;
	if (s_notick < 0) { const char *v = getenv_ ("POSIXSIM_NOTICK"); s_notick = v && atoi_ (v) != 0; }
	unsigned long long left = ms;
	for (;;)
	{
		unsigned slice = s_notick ? ms : ms == KAPI_WAIT_FOREVER || left > 10 ? 10 : (unsigned) left;
		struct l_timespec t = { slice / 1000, (long) (slice % 1000) * 1000000L };
		long r = sc6 (L_futex, (long) a, 0 /* FUTEX_WAIT */, expected, s_notick && ms == KAPI_WAIT_FOREVER ? 0 : (long) &t, 0, 0);
		if (r != -110)
			return 0;
		if (__atomic_load_n (a, __ATOMIC_SEQ_CST) != expected)
			return 0;
		if (ms != KAPI_WAIT_FOREVER && (left -= slice) == 0)
			return 1;
	}
}
static int f_wake_word (volatile unsigned *a) { return (int) sc6 (L_futex, (long) a, 1, 0x7FFFFFFF, 0, 0, 0); }

/* ---- v75: memory ---- */
static long long f_vm_map (unsigned long long addr, unsigned long long len, unsigned prot, unsigned flags)
{
	if (len == 0) return -KAPI_EINVAL;		/* (v78: PROT_EXEC given, as the kernel does) */
	len = (len + 0xFFFF) & ~0xFFFFULL;
	long lf = 0x22 | (flags & (KAPI_MAP_FIXED | KAPI_MAP_NORESERVE | KAPI_MAP_POPULATE | KAPI_MAP_FIXED_NOREPLACE));
	if (flags & (KAPI_MAP_FIXED | KAPI_MAP_FIXED_NOREPLACE))
	{
		if (addr & 0xFFFF) return -KAPI_EINVAL;
		long r = sc6 (L_mmap, (long) addr, (long) len, prot, lf, -1, 0);
		return r < 0 ? -lerr (-r) : r;
	}
	/* (the kernel's pages are 64 KB: an aligned place, the rest given back) */
	long r = sc6 (L_mmap, 0, (long) (len + 0x10000), prot, lf, -1, 0);
	if (r < 0) return -lerr (-r);
	unsigned long a = ((unsigned long) r + 0xFFFF) & ~0xFFFFUL;
	if (a > (unsigned long) r) sc2 (L_munmap, r, a - (unsigned long) r);
	unsigned long end = (unsigned long) r + len + 0x10000;
	if (end > a + len) sc2 (L_munmap, a + len, end - (a + len));
	return (long long) a;
}
static int f_vm_unmap (unsigned long long a, unsigned long long l) { return (int) kret (sc2 (L_munmap, a, l)); }
static int f_vm_protect (unsigned long long a, unsigned long long l, unsigned p)
{
	return (int) kret (sc3 (L_mprotect, a, l, p));
}
static int f_vm_advise (unsigned long long a, unsigned long long l, int adv) { return (int) kret (sc3 (L_madvise, a, l, adv)); }
static int f_vm_query (unsigned long long addr, struct kapi_vm_region *o)
{
	static char buf[65536];
	long fd = l_open ("/proc/self/maps", 0, 0);
	long n = fd >= 0 ? sc3 (L_read, fd, buf, sizeof buf - 1) : -1;
	if (fd >= 0) sc1 (L_close, fd);
	if (n <= 0) return -KAPI_EFAULT;
	buf[n] = 0;
	unsigned long long best = ~0ULL, bend = 0;
	unsigned bprot = 0;
	for (char *l = buf; *l; )
	{
		unsigned long long a = 0, b = 0;
		while (*l && *l != '-') a = a * 16 + (unsigned long long) (*l <= '9' ? *l - '0' : (*l | 32) - 'a' + 10), l++;
		if (*l) l++;
		while (*l && *l != ' ') b = b * 16 + (unsigned long long) (*l <= '9' ? *l - '0' : (*l | 32) - 'a' + 10), l++;
		unsigned prot = 0, kind = KAPI_VMK_ANON;
		if (*l == ' ') { l++; if (l[0] == 'r') prot |= 1; if (l[1] == 'w') prot |= 2; if (l[3] == 's') kind = KAPI_VMK_SHM; }
		while (*l && *l != '\n') l++;
		if (*l) l++;
		if (addr >= a && addr < b) { o->start = a; o->end = b; o->prot = prot; o->kind = kind; o->resident = 0; o->flags = KAPI_VMF_LAZY; return 0; }
		if (a > addr && a < best) { best = a; bend = b; bprot = prot; }
	}
	if (best == ~0ULL) return -KAPI_ENOMEM;
	o->start = best; o->end = bend; o->prot = bprot; o->kind = KAPI_VMK_ANON; o->resident = 0; o->flags = KAPI_VMF_LAZY;
	return 1;
}
static int f_vm_stats (int pid, struct kapi_vm_stats *o) { (void) pid; memset (o, 0, sizeof *o); return 0; }

/* ---- v75: files ---- */
static void to_kstat (const struct l_stat *s, struct kapi_stat *k)
{
	memset (k, 0, sizeof *k);
	k->size = (unsigned long long) s->st_size;
	k->mtime = s->st_mtime;
	k->ctime = s->st_ctime;
	k->ino = s->st_ino;
	k->mode = s->st_mode & 0170777;
	k->dev = (unsigned) s->st_dev;
	k->blksize = (unsigned) s->st_blksize;
	k->blocks = (unsigned long long) s->st_blocks;
}
static long long f_file_open (const char *p, unsigned flags, unsigned mode)
{
	char hp[1024];
	long fd = l_open (host (p, hp), (int) (flags & 0x6C3) | L_O_CLOEXEC, (int) mode);
	if (fd < 0) return -lerr (-fd);
	struct l_stat st;
	sc2 (L_fstat, fd, &st);
	if ((st.st_mode & 0170000) == 0040000) { sc1 (L_close, fd); return -KAPI_EISDIR; }
	long h = hnew (H_OFILE);
	if (!h) { sc1 (L_close, fd); return -KAPI_EMFILE; }
	s_h[h - 1].fd = (int) fd;
	return h;
}
#define OFILE(h) struct handle *x = hget ((long) (h), H_OFILE); if (!x) return -KAPI_EBADF
static long long f_file_read (long long h, void *b, unsigned long long n, long long off)
{
	OFILE (h);
	return kret (off < 0 ? sc3 (L_read, x->fd, b, n) : sc4 (L_pread64, x->fd, b, n, off));
}
static long long f_file_write (long long h, const void *b, unsigned long long n, long long off)
{
	OFILE (h);
	return kret (off < 0 ? sc3 (L_write, x->fd, b, n) : sc4 (L_pwrite64, x->fd, b, n, off));
}
static long long f_file_seek (long long h, long long off, int wh) { OFILE (h); return kret (sc3 (L_lseek, x->fd, off, wh)); }
static int f_file_truncate (long long h, long long sz) { OFILE (h); return (int) kret (sc2 (L_ftruncate, x->fd, sz)); }
static int f_file_sync (long long h) { OFILE (h); return (int) kret (sc1 (L_fsync, x->fd)); }
static int f_file_stat (long long h, struct kapi_stat *o)
{
	OFILE (h);
	struct l_stat st;
	long r = sc2 (L_fstat, x->fd, &st);
	if (r < 0) return -lerr (-r);
	to_kstat (&st, o);
	return 0;
}
static int f_file_close (long long h) { OFILE (h); sc1 (L_close, x->fd); hfree ((long) h); return 0; }
static int f_path_stat (const char *p, struct kapi_stat *o)
{
	char hp[1024];
	struct l_stat st;
	long r = sc4 (L_newfstatat, L_AT_FDCWD, host (p, hp), &st, 0);
	if (r < 0) return -lerr (-r);
	to_kstat (&st, o);
	return 0;
}
static int f_path_unlink (const char *p, unsigned flags)
{
	char hp[1024];
	return (int) kret (sc3 (L_unlinkat, L_AT_FDCWD, host (p, hp), (flags & KAPI_UNLINK_DIR) ? 0x200 : 0));
}
static int f_path_mkdir (const char *p, unsigned mode) { char hp[1024]; return (int) kret (sc3 (L_mkdirat, L_AT_FDCWD, host (p, hp), mode ? mode : 0755)); }
static int f_path_rename (const char *a, const char *b)
{
	char ha[1024], hb[1024], ra[512], rb[512];
	resolve (a, ra, sizeof ra);
	resolve (b, rb, sizeof rb);
	if (volume_prefix (ra) != volume_prefix (rb) || strncmp (ra, rb, volume_prefix (ra)))
		return -KAPI_EXDEV;
	return (int) kret (sc4 (L_renameat, L_AT_FDCWD, host (a, ha), L_AT_FDCWD, host (b, hb)));
}
static int f_path_utime (const char *p, long long mtime)
{
	char hp[1024];
	struct l_timespec t[2] = { { mtime, 0 }, { mtime, 0 } };
	return (int) kret (sc4 (L_utimensat, L_AT_FDCWD, host (p, hp), t, 0));
}
static int f_dir_read (void *d, struct kapi_dirent2 *o)
{
	struct handle *x = hget ((long) d, H_DIR);
	if (!x) return -KAPI_EBADF;
	char name[256];
	int isdir;
	if (!next_ent (x, name, &isdir)) return 0;
	memset (o, 0, sizeof *o);
	strcpy (o->name, name);
	struct l_stat st;
	if (sc4 (L_newfstatat, x->fd, name, &st, 0) == 0)
	{
		o->size = (unsigned long long) st.st_size;
		o->mtime = st.st_mtime;
		o->mode = st.st_mode & 0170777;
		o->ino = st.st_ino;
	}
	return 1;
}
static int f_stream_write_nb (void *h, const void *b, unsigned n)
{
	struct handle *x = hget ((long) h, H_STREAM);
	if (!x || x->fd2 < 0) return -KAPI_EBADF;
	if (n == 0) return 0;
	struct l_pollfd p = { x->fd2, 4, 0 };
	struct l_timespec z = { 0, 0 };
	if (sc4 (L_ppoll, &p, 1, &z, 0) <= 0) return -KAPI_EAGAIN;
	long fl = sc2 (L_fcntl, x->fd2, 3 /* F_GETFL */);
	sc3 (L_fcntl, x->fd2, 4, fl | L_O_NONBLOCK);
	long r = sc3 (L_write, x->fd2, b, n);
	sc3 (L_fcntl, x->fd2, 4, fl);
	if (r == -11) return -KAPI_EAGAIN;
	return (int) kret (r);
}

/* ---- v75: processes, environment, clock ---- */
static long long f_spawn_ex (const struct kapi_spawn_attr *a)
{
	char *av[260], *ev[600];
	split_block (a->argv, av, 260);
	char **e = a->envp ? split_block (a->envp, ev, 600) : s_envp;
	return do_spawn (a->path, av, e, a->cwd, a->in, a->out);
}
static int f_proc_wait (void *proc, unsigned flags, struct kapi_proc_status *o)
{
	struct handle *x = hget ((long) proc, H_PROC);
	if (!x) return -KAPI_EBADF;
	if (!reap (x, (flags & KAPI_WAIT_NOHANG) != 0))
	{
		if (o) { memset (o, 0, sizeof *o); o->pid = x->pid; }
		return 0;
	}
	if (o)
	{
		memset (o, 0, sizeof *o);
		o->pid = x->pid;
		int sig = x->status & 0x7F;
		if (sig == 0) { o->code = (x->status >> 8) & 0xFF; o->reason = KAPI_PROC_EXITED; }
		else if (sig == 9) { o->code = -9; o->reason = KAPI_PROC_KILLED; }
		else { o->code = -11; o->reason = KAPI_PROC_FAULT; }
	}
	if (!(flags & KAPI_WAIT_KEEP)) hfree ((long) proc);
	return 1;
}
static int block_out (char *b, unsigned cap, char **v, const char *first)
{
	unsigned o = 0;
	for (long i = 0; v && v[i]; i++)
	{
		const char *s = i == 0 && first ? first : v[i];
		unsigned l = (unsigned) strlen (s) + 1;
		if (o + l < cap) memcpy (b + o, s, l);
		o += l;
	}
	if (o < cap) b[o] = 0;
	return (int) o + 1;
}
static int f_get_argv (char *b, unsigned cap) { return block_out (b, cap, s_argv, getenv_ ("POSIXSIM_ARGV0")); }
static int f_get_env (char *b, unsigned cap) { return block_out (b, cap, s_envp, 0); }
static int f_getpid (int which) { return (int) (which ? sc0 (L_getppid) : sc0 (L_getpid)); }
static int f_clock_info (struct kapi_clock_info *o)
{
	memset (o, 0, sizeof *o);
	o->cnt = cntvct ();
	o->freq = cntfrq ();
	o->utc_us = now_ns (0) / 1000;
	o->tz_minutes = s_tz;
	o->flags = KAPI_CLOCK_REALTIME_VALID;
	o->boot_cnt = s_bootCnt;
	return 0;
}
static int f_sleep_us (unsigned long long us) { if (us) sleep_ns ((long long) us * 1000); else sc0 (L_sched_yield); return 0; }

/* ---- v76: local sockets, handles carried, shared memory (docs/POSIX-PLAN.md §14) ----
 * A local socket = a Linux AF_UNIX socket of the same type (its number: LBASE + the handle); a
 * STREAM one also has a SEQPACKET side channel. The handles of a message travel as Linux
 * descriptors (SCM_RIGHTS) with, for each, its kind, tag, which descriptors (mask) and socket
 * type: in a header at the start of the packet (SEQPACKET / DGRAM: every packet has one), or as one
 * record on the side channel for each send that carries some (STREAM: the descriptors ride on the
 * data, so Linux keeps them at their byte). */
#define LBASE		KAPI_SOCK_LOCAL_BASE
#define XMAX		KAPI_IPC_HANDLES_MAX
struct xmeta { int kind; unsigned tag; int mask; int stype; unsigned xflags; unsigned pad; };
struct xhdr { unsigned n, pad; struct xmeta m[XMAX]; };

static struct handle *lsock (long s) { return s >= LBASE ? hget (s - LBASE, H_LSOCK) : 0; }
static int is_packet (const struct handle *x) { return x->stype != KAPI_SOCK_STREAM; }
static int ltype (int kt) { return kt == KAPI_SOCK_STREAM ? 1 : kt == KAPI_SOCK_SEQPACKET ? 5 : 2; }

/* the Linux descriptors of one handle of this process -> how many (<= 2), 0: not one */
static int xfds (const struct kapi_handle_xfer *x, int *fd, struct xmeta *m)
{
	struct handle *h = 0;
	int n = 0;
	m->kind = x->kind;
	m->tag = x->tag;
	m->mask = 0;
	m->stype = 0;
	m->xflags = x->flags;
	m->pad = 0;
	switch (x->kind)
	{
	case KAPI_HK_OFILE: h = hget ((long) x->h, H_OFILE); break;
	case KAPI_HK_STREAM: h = hget ((long) x->h, H_STREAM); break;
	case KAPI_HK_SHM: h = hget ((long) x->h, H_SHM); break;
	case KAPI_HK_LSOCK: h = lsock ((long) x->h); break;
	case KAPI_HK_SOCKET:
		if (x->h < 0 || x->h >= LBASE) return 0;
		fd[n++] = (int) x->h;
		m->mask = 1;
		return n;
	default: return 0;
	}
	if (!h) return 0;
	int wantR = 1, wantW = 1;
	if (h->type == H_STREAM && h->fd >= 0 && h->fd2 >= 0)	/* (a pipe: the end asked for only) */
	{
		wantR = !(x->flags & KAPI_HXF_WRITER);
		wantW = !wantR;
	}
	if (h->fd >= 0 && wantR) { fd[n++] = h->fd; m->mask |= 1; }
	if (h->fd2 >= 0 && wantW) { fd[n++] = h->fd2; m->mask |= 2; }
	m->stype = h->type == H_LSOCK ? h->stype : h->acc;
	return n;
}

/* a handle of this process for descriptors received -> its value (-1: the table full: closed) */
static long long xmake (const struct xmeta *m, const int *fd)
{
	int n = (m->mask & 1) + ((m->mask >> 1) & 1);
	if (m->kind == KAPI_HK_SOCKET)
		return fd[0];
	int type = m->kind == KAPI_HK_OFILE ? H_OFILE : m->kind == KAPI_HK_STREAM ? H_STREAM
		 : m->kind == KAPI_HK_SHM ? H_SHM : H_LSOCK;
	long h = hnew (type);
	if (h == 0)
	{
		for (int i = 0; i < n; i++) sc1 (L_close, fd[i]);
		return -1;
	}
	struct handle *x = &s_h[h - 1];
	int k = 0;
	if (m->mask & 1) x->fd = fd[k++];
	if (m->mask & 2) x->fd2 = fd[k++];
	if (type == H_LSOCK) { x->stype = m->stype; return LBASE + h; }
	x->acc = m->stype;
	return h;
}

/* send: the iovecs and nx handles from local socket x -> bytes / -errno */
static long long local_send (struct handle *x, const struct kapi_iovec *iov, unsigned niov,
			     const struct kapi_handle_xfer *hx, unsigned nx, unsigned flags)
{
	/* (on the stack: the program's threads really run at once here, and every packet carries H --
	 * shared, a send with handles and another thread's send spoiled each other's header, and the
	 * receiver lost the messages: the loads that never started on the software drawing path) */
	struct xhdr H;
	int fds[2 * XMAX];
	char cbuf[sizeof (struct l_cmsghdr) + sizeof fds + 8] __attribute__ ((aligned (8)));
	memset (&H, 0, sizeof H);
	if (nx > XMAX) return -KAPI_EINVAL;
	int nfd = 0;
	H.n = nx;
	for (unsigned i = 0; i < nx; i++)
	{
		int k = xfds (&hx[i], fds + nfd, &H.m[i]);
		if (k == 0) return -KAPI_EBADF;
		nfd += k;
	}
	struct l_iovec lv[KAPI_IPC_IOV_MAX + 1];
	unsigned nv = 0;
	unsigned hdr = 8 + nx * (unsigned) sizeof (struct xmeta);
	if (is_packet (x)) { lv[nv].base = &H; lv[nv].len = hdr; nv++; }
	else if (nx > 0)
	{
		long r = sc6 (L_sendto, x->fd2, (long) &H, hdr, 0x4000, 0, 0);	/* (the record first) */
		if (r < 0) return kret (r);
	}
	for (unsigned i = 0; i < niov && nv <= KAPI_IPC_IOV_MAX; i++)
	{
		lv[nv].base = (void *) (unsigned long) iov[i].base;
		lv[nv].len = iov[i].len;
		nv++;
	}
	struct l_msghdr m;
	memset (&m, 0, sizeof m);
	m.iov = lv;
	m.iovlen = nv;
	if (nfd > 0)
	{
		struct l_cmsghdr *c = (struct l_cmsghdr *) cbuf;
		c->len = sizeof *c + (unsigned) nfd * sizeof (int);
		c->level = 1;			/* SOL_SOCKET */
		c->type = 1;			/* SCM_RIGHTS */
		memcpy (c + 1, fds, (unsigned) nfd * sizeof (int));
		m.control = cbuf;
		m.controllen = (c->len + 7) & ~7UL;
	}
	long r = sc3 (L_sendmsg, x->fd, &m, (flags & KAPI_MSG_DONTWAIT) | 0x4000);
	if (r < 0) return kret (r);
	return is_packet (x) ? (r >= (long) hdr ? r - (long) hdr : 0) : r;
}

/* receive into the iovecs; up to cap handles to out (*pn, *pfl updated) -> bytes / -errno */
static long long local_recv (struct handle *x, const struct kapi_iovec *iov, unsigned niov,
			     struct kapi_handle_xfer *out, unsigned cap, unsigned *pn, unsigned *pfl, unsigned flags)
{
	struct xhdr H;				/* (on the stack: see local_send) */
	char cbuf[sizeof (struct l_cmsghdr) + 2 * XMAX * sizeof (int) + 8] __attribute__ ((aligned (8)));
	memset (&H, 0, sizeof H);
	unsigned long long total = 0;
	for (unsigned i = 0; i < niov; i++) total += iov[i].len;
	char *tmp = 0;
	struct l_iovec lv[KAPI_IPC_IOV_MAX];
	struct l_msghdr m;
	memset (&m, 0, sizeof m);
	if (is_packet (x))		/* the header + the data in one buffer, then copied out */
	{
		tmp = (char *) sc6 (L_mmap, 0, (long) (sizeof H + total + 1), 3, 0x22, -1, 0);
		if ((long) tmp < 0) return -KAPI_ENOMEM;
		lv[0].base = tmp;
		lv[0].len = sizeof H + total;
		m.iovlen = 1;
	}
	else
	{
		for (unsigned i = 0; i < niov; i++) { lv[i].base = (void *) (unsigned long) iov[i].base; lv[i].len = iov[i].len; }
		m.iovlen = niov;
	}
	m.iov = lv;
	m.control = cbuf;
	m.controllen = sizeof cbuf;
	long r = sc3 (L_recvmsg, x->fd, &m, flags & (KAPI_MSG_PEEK | KAPI_MSG_DONTWAIT | KAPI_MSG_WAITALL));
	*pn = 0;
	*pfl = 0;
	if (r < 0)
	{
		if (tmp) sc2 (L_munmap, tmp, sizeof H + total + 1);
		return kret (r);
	}
	if (m.flags & 0x20) *pfl |= KAPI_MSG_TRUNC;
	/* the descriptors that came */
	int nfd = 0, *fds = 0;
	if (m.controllen >= sizeof (struct l_cmsghdr))
	{
		struct l_cmsghdr *c = (struct l_cmsghdr *) cbuf;
		if (c->level == 1 && c->type == 1)
		{
			nfd = (int) ((c->len - sizeof *c) / sizeof (int));
			fds = (int *) (c + 1);
		}
	}
	long long got = r;
	unsigned nmeta = 0;
	if (is_packet (x))
	{
		if (r >= 8)
		{
			memcpy (&H, tmp, (unsigned long) r < sizeof H ? (unsigned long) r : sizeof H);
			nmeta = H.n;
			unsigned hdr = 8 + nmeta * (unsigned) sizeof (struct xmeta);
			got = r > (long) hdr ? r - (long) hdr : 0;
			if ((unsigned long long) got > total)	/* (cut to the caller's room) */
			{
				got = (long long) total;
				*pfl |= KAPI_MSG_TRUNC;
			}
			unsigned long long o = 0;
			for (unsigned i = 0; i < niov && o < (unsigned long long) got; i++)
			{
				unsigned long long k = iov[i].len < got - o ? iov[i].len : got - o;
				memcpy ((void *) (unsigned long) iov[i].base, tmp + hdr + o, k);
				o += k;
			}
		}
		else
			got = 0;
		sc2 (L_munmap, tmp, sizeof H + total + 1);
	}
	else if (nfd > 0)
	{
		long q = sc6 (L_recvfrom, x->fd2, (long) &H, sizeof H, 0, 0, 0);	/* (its record) */
		nmeta = q >= 8 ? H.n : 0;
	}
	if (flags & KAPI_MSG_PEEK)		/* (no handles with a peek: the duplicates closed) */
	{
		for (int i = 0; i < nfd; i++) sc1 (L_close, fds[i]);
		return got;
	}
	int k = 0;
	for (unsigned i = 0; i < nmeta && k < nfd; i++)
	{
		int n = (H.m[i].mask & 1) + ((H.m[i].mask >> 1) & 1);
		if (*pn >= cap)
		{
			for (int j = 0; j < n; j++) sc1 (L_close, fds[k + j]);
			*pfl |= KAPI_MSG_CTRUNC;
		}
		else
		{
			long long h = xmake (&H.m[i], fds + k);
			if (h < 0) *pfl |= KAPI_MSG_CTRUNC;
			else
			{
				memset (&out[*pn], 0, sizeof out[*pn]);
				out[*pn].h = h;
				out[*pn].kind = H.m[i].kind;
				out[*pn].tag = H.m[i].tag;
				out[*pn].flags = H.m[i].xflags;
				out[*pn].fd = -1;
				(*pn)++;
			}
		}
		k += n;
	}
	if (m.flags & 0x8) *pfl |= KAPI_MSG_CTRUNC;
	return got;
}

static int f_sock_pair (int type, unsigned flags, int *sv)
{
	if (type != KAPI_SOCK_STREAM && type != KAPI_SOCK_SEQPACKET && type != KAPI_SOCK_DGRAM) return -KAPI_EPROTONOSUPPORT;
	if (flags & ~KAPI_SOCKF_NONBLOCK) return -KAPI_EINVAL;
	int d[2], m[2] = { -1, -1 };
	long r = sc4 (L_socketpair, 1, ltype (type) | L_O_CLOEXEC | ((flags & KAPI_SOCKF_NONBLOCK) ? L_O_NONBLOCK : 0), 0, d);
	if (r < 0) return (int) kret (r);
	if (type == KAPI_SOCK_STREAM && (r = sc4 (L_socketpair, 1, 5 | L_O_CLOEXEC, 0, m)) < 0)
	{
		sc1 (L_close, d[0]); sc1 (L_close, d[1]);
		return (int) kret (r);
	}
	for (int i = 0; i < 2; i++)
	{
		long h = hnew (H_LSOCK);
		if (h == 0) return -KAPI_EMFILE;
		s_h[h - 1].fd = d[i];
		s_h[h - 1].fd2 = m[i];
		s_h[h - 1].stype = type;
		sv[i] = (int) (LBASE + h);
	}
	s_h[sv[0] - LBASE - 1].pid = sv[1];		/* (the peer, while both are ours: the cycle check) */
	s_h[sv[1] - LBASE - 1].pid = sv[0];
	return 0;
}

static long long f_sock_sendmsg (int s, const struct kapi_msghdr *m, unsigned flags)
{
	struct handle *x = lsock (s);
	if (!x) return s >= 0 && s < LBASE ? -KAPI_EOPNOTSUPP : -KAPI_EBADF;
	if (m->iovcnt > KAPI_IPC_IOV_MAX || m->nhandles > XMAX) return -KAPI_EINVAL;
	for (unsigned i = 0; i < m->nhandles; i++)
		if (m->handles[i].kind == KAPI_HK_LSOCK && x->pid != 0 && m->handles[i].h == x->pid && lsock (x->pid)
		    && lsock (x->pid)->pid == s)	/* (still its peer: the number is reused once the peer is closed) */
			return -KAPI_EINVAL;		/* (the receiving end over its own connection) */
	return local_send (x, m->iov, m->iovcnt, m->handles, m->nhandles, flags);
}

static long long f_sock_recvmsg (int s, struct kapi_msghdr *m, unsigned flags)
{
	struct handle *x = lsock (s);
	if (!x) return s >= 0 && s < LBASE ? -KAPI_EOPNOTSUPP : -KAPI_EBADF;
	if (m->iovcnt > KAPI_IPC_IOV_MAX || m->nhandles > XMAX) return -KAPI_EINVAL;
	unsigned n, fl;
	long long r = local_recv (x, m->iov, m->iovcnt, m->handles, m->handles ? m->nhandles : 0, &n, &fl, flags);
	if (r >= 0) { m->nhandles = n; m->flags = fl; }
	return r;
}

static long long f_shm_create (unsigned long long size, unsigned flags)
{
	if (flags & ~KAPI_SHM_ALLOW_SEALING) return -KAPI_EINVAL;
	long fd = sc2 (L_memfd_create, "posixsim", 1 | ((flags & KAPI_SHM_ALLOW_SEALING) ? 2 : 0));
	if (fd < 0) return kret (fd);
	if (size && sc2 (L_ftruncate, fd, size) < 0) { sc1 (L_close, fd); return -KAPI_ENOMEM; }
	long h = hnew (H_SHM);
	if (h == 0) { sc1 (L_close, fd); return -KAPI_EMFILE; }
	s_h[h - 1].fd = (int) fd;
	s_h[h - 1].acc = KAPI_O_RDWR;
	return h;
}

static int shm_path (const char *name, char *out)
{
	if (*name == '/') name++;
	if (!*name || strchr (name, '/')) return -KAPI_EINVAL;
	if (strlen (name) > KAPI_SHM_NAME_MAX) return -KAPI_ENAMETOOLONG;
	strcpy (out, s_root);
	strcat (out, "/.shm");
	sc3 (L_mkdirat, L_AT_FDCWD, out, 0755);
	strcat (out, "/");
	strcat (out, name);
	return 0;
}

static long long f_shm_open (const char *name, unsigned oflags, unsigned mode)
{
	char p[512];
	int r = shm_path (name, p);
	if (r < 0) return r;
	unsigned acc = oflags & KAPI_O_ACCMODE;
	if (acc != KAPI_O_RDONLY && acc != KAPI_O_RDWR) return -KAPI_EINVAL;
	long fd = l_open (p, (acc == KAPI_O_RDWR ? L_O_RDWR : L_O_RDONLY) | L_O_CLOEXEC | ((oflags & KAPI_O_CREAT) ? L_O_CREAT : 0)
			  | ((oflags & KAPI_O_EXCL) ? L_O_EXCL : 0) | ((oflags & KAPI_O_TRUNC) ? L_O_TRUNC : 0), mode ? mode : 0600);
	if (fd < 0) return kret (fd);
	long h = hnew (H_SHM);
	if (h == 0) { sc1 (L_close, fd); return -KAPI_EMFILE; }
	s_h[h - 1].fd = (int) fd;
	s_h[h - 1].acc = (int) acc;
	return h;
}

static int f_shm_unlink (const char *name)
{
	char p[512];
	int r = shm_path (name, p);
	return r < 0 ? r : (int) kret (sc3 (L_unlinkat, L_AT_FDCWD, p, 0));
}

static long long f_shm_ctl (long long h, int op, unsigned long long arg)
{
	struct handle *x = hget ((long) h, H_SHM);
	if (!x) return -KAPI_EBADF;
	struct l_stat st;
	switch (op)
	{
	case KAPI_SHM_GET_SIZE: return sc2 (L_fstat, x->fd, &st) < 0 ? -KAPI_EIO : st.st_size;
	case KAPI_SHM_SET_SIZE:
		if (x->acc != KAPI_O_RDWR) return -KAPI_EINVAL;
		return kret (sc2 (L_ftruncate, x->fd, arg));
	case KAPI_SHM_ADD_SEALS:
		if (x->acc != KAPI_O_RDWR) return -KAPI_EPERM;
		return kret (sc3 (L_fcntl, x->fd, 1033, arg));
	case KAPI_SHM_GET_SEALS: { long r = sc3 (L_fcntl, x->fd, 1034, 0); return r < 0 ? 0 : r; }
	case KAPI_SHM_GET_ID: return sc2 (L_fstat, x->fd, &st) < 0 ? -KAPI_EIO : (long long) st.st_ino;
	case KAPI_SHM_GET_ACCESS: return x->acc;
	default: return -KAPI_EINVAL;
	}
}

static long long f_shm_map (long long h, unsigned long long addr, unsigned long long len, unsigned prot,
			    unsigned flags, unsigned long long off)
{
	struct handle *x = hget ((long) h, H_SHM);
	if (!x) return -KAPI_EBADF;
	if (prot & KAPI_PROT_EXEC) return -KAPI_ENOTSUP;
	if ((prot & KAPI_PROT_WRITE) && x->acc != KAPI_O_RDWR) return -KAPI_EACCES;
	if (off & 0xFFFF) return -KAPI_EINVAL;
	len = (len + 0xFFFF) & ~0xFFFFULL;
	long lf = 1 /* MAP_SHARED */ | ((flags & KAPI_MAP_FIXED) ? 0x10 : 0) | ((flags & KAPI_MAP_FIXED_NOREPLACE) ? 0x100000 : 0)
		| ((flags & KAPI_MAP_POPULATE) ? 0x8000 : 0);
	return kret (sc6 (L_mmap, (long) addr, (long) len, prot, lf, x->fd, (long) off));
}

static int f_handle_close (long long h)
{
	struct handle *x = h >= LBASE ? lsock ((long) h) : (h >= 1 && h <= 256 ? &s_h[h - 1] : 0);
	if (!x || (x->type != H_SHM && x->type != H_LSOCK && x->type != H_OFILE && x->type != H_STREAM)) return -KAPI_EBADF;
	if (x->fd > 2) sc1 (L_close, x->fd);
	if (x->fd2 > 2) sc1 (L_close, x->fd2);
	x->type = H_FREE;
	return 0;
}

/* spawn_ex2: the Linux descriptors stay open in the child; POSIXSIM_HANDLES tells it what they are:
 * "fd,kind,tag,mask,stype,lfd,lfd;..." */
static long long f_spawn_ex2 (const struct kapi_spawn_attr *a, const struct kapi_handle_xfer *hx, unsigned n)
{
	static char env[16384];
	static int keep[2 * XMAX];
	if (n > XMAX) return -KAPI_EINVAL;
	int nk = 0;
	unsigned o = 0;
	strcpy (env, "POSIXSIM_HANDLES=");
	o = 17;
	for (unsigned i = 0; i < n; i++)
	{
		int fd[2] = { -1, -1 };
		struct xmeta m;
		int k = xfds (&hx[i], fd, &m);
		if (k == 0 || hx[i].fd < 0) return -KAPI_EBADF;
		char line[160];
		int v[8] = { hx[i].fd, m.kind, (int) m.tag, m.mask, m.stype, fd[0], fd[1], (int) m.xflags };
		unsigned l = 0;
		for (int j = 0; j < 8; j++)
		{
			char num[16];
			int q = 0;
			unsigned u = (unsigned) v[j];
			int neg = v[j] < 0 && j != 2;
			if (neg) u = (unsigned) -v[j];
			do { num[q++] = (char) ('0' + u % 10); u /= 10; } while (u);
			if (neg) line[l++] = '-';
			while (q) line[l++] = num[--q];
			line[l++] = j < 7 ? ',' : ';';
		}
		if (o + l + 1 >= sizeof env) return -KAPI_ENOMEM;
		memcpy (env + o, line, l);
		o += l;
		for (int j = 0; j < k; j++) keep[nk++] = fd[j];
	}
	env[o] = 0;
	char *av[260], *ev[600];
	split_block (a->argv, av, 260);
	char **e = a->envp ? split_block (a->envp, ev, 600) : s_envp;
	return do_spawn2 (a->path, av, e, a->cwd, a->in, a->out, keep, nk, env);
}

static int f_get_handles (struct kapi_handle_xfer *out, unsigned cap)
{
	static struct kapi_handle_xfer got[XMAX];
	static int n = -1;
	if (n < 0)
	{
		n = 0;
		const char *p = getenv_ ("POSIXSIM_HANDLES");
		while (p && *p && n < XMAX)
		{
			long v[8];
			for (int j = 0; j < 8; j++)
			{
				int neg = *p == '-';
				if (neg) p++;
				unsigned long u = 0;
				while (*p >= '0' && *p <= '9') u = u * 10 + (unsigned long) (*p++ - '0');
				v[j] = neg ? -(long) u : (long) u;
				if (*p == ',' || *p == ';') p++;
			}
			struct xmeta m = { (int) v[1], (unsigned) v[2], (int) v[3], (int) v[4], (unsigned) v[7], 0 };
			int fd[2] = { (int) v[5], (int) v[6] };
			for (int j = 0; j < 2; j++)
				if (fd[j] >= 0) sc3 (L_fcntl, fd[j], 2, 1);	/* (FD_CLOEXEC again) */
			memset (&got[n], 0, sizeof got[n]);
			got[n].h = xmake (&m, fd);
			got[n].kind = got[n].h < 0 ? KAPI_HK_NONE : m.kind;
			got[n].tag = m.tag;
			got[n].flags = m.xflags;
			got[n].fd = (int) v[0];
			n++;
		}
	}
	for (unsigned i = 0; i < cap && i < (unsigned) n; i++) out[i] = got[i];
	return n;
}

/* ---- v75: sockets and poll ---- */
static void to_sin (const struct kapi_sockaddr *k, struct l_sockaddr_in *s)
{
	memset (s, 0, sizeof *s);
	s->family = 2;
	s->port = (unsigned short) (k->port << 8 | k->port >> 8);
	memcpy (&s->addr, k->addr, 4);
}
static void from_sin (const struct l_sockaddr_in *s, struct kapi_sockaddr *k)
{
	memset (k, 0, sizeof *k);
	k->family = KAPI_AF_INET;
	k->port = (unsigned short) (s->port << 8 | s->port >> 8);
	memcpy (k->addr, &s->addr, 4);
}
static int f_sock_open (int type, unsigned flags)
{
	if (type != KAPI_SOCK_STREAM && type != KAPI_SOCK_DGRAM) return -KAPI_EPROTONOSUPPORT;
	return (int) kret (sc3 (L_socket, 2, (type == KAPI_SOCK_STREAM ? 1 : 2) | 02000000 | ((flags & KAPI_SOCKF_NONBLOCK) ? L_O_NONBLOCK : 0), 0));
}
static int f_sock_connect (int s, const struct kapi_sockaddr *to)
{
	if (s >= LBASE) return -KAPI_EOPNOTSUPP;
	struct l_sockaddr_in a; to_sin (to, &a); return (int) kret (sc3 (L_connect, s, &a, sizeof a));
}
static int f_sock_bind (int s, const struct kapi_sockaddr *to)
{
	if (s >= LBASE) return -KAPI_EOPNOTSUPP;
	struct l_sockaddr_in a;
	int one = 1;
	to_sin (to, &a);
	sc5 (L_setsockopt, s, 1, 2, &one, 4);
	return (int) kret (sc3 (L_bind, s, &a, sizeof a));
}
static int f_sock_listen (int s, int b) { return s >= LBASE ? -KAPI_EOPNOTSUPP : (int) kret (sc2 (L_listen, s, b < 1 ? 1 : b > 32 ? 32 : b)); }
static int f_sock_accept (int s, struct kapi_sockaddr *peer, unsigned flags)
{
	if (s >= LBASE) return -KAPI_EOPNOTSUPP;
	struct l_sockaddr_in a;
	unsigned len = sizeof a;
	long r = sc4 (L_accept4, s, &a, &len, 02000000 | ((flags & KAPI_SOCKF_NONBLOCK) ? L_O_NONBLOCK : 0));
	if (r >= 0 && peer) from_sin (&a, peer);
	return (int) kret (r);
}
static long long f_sock_send (int s, const void *b, unsigned long long n, unsigned flags, const struct kapi_sockaddr *to)
{
	if (s >= LBASE)
	{
		struct handle *x = lsock (s);
		struct kapi_iovec v = { (unsigned long long) (unsigned long) b, n };
		return x ? local_send (x, &v, 1, 0, 0, flags) : -KAPI_EBADF;
	}
	struct l_sockaddr_in a;
	if (to) to_sin (to, &a);
	return kret (sc6 (L_sendto, s, (long) b, (long) n, (long) (flags | 0x4000), to ? (long) &a : 0, to ? (long) sizeof a : 0));
}
static long long f_sock_recv (int s, void *b, unsigned long long n, unsigned flags, struct kapi_sockaddr *from)
{
	if (s >= LBASE)
	{
		struct handle *x = lsock (s);
		struct kapi_iovec v = { (unsigned long long) (unsigned long) b, n };
		unsigned nh, fl;
		long long r = x ? local_recv (x, &v, 1, 0, 0, &nh, &fl, flags) : -KAPI_EBADF;
		if (r >= 0 && from) { memset (from, 0, sizeof *from); from->family = KAPI_AF_UNIX; }
		return r;
	}
	struct l_sockaddr_in a;
	unsigned len = sizeof a;
	long r = sc6 (L_recvfrom, s, (long) b, (long) n, (long) flags, from ? (long) &a : 0, from ? (long) &len : 0);
	if (r >= 0 && from) from_sin (&a, from);
	return kret (r);
}
static int f_sock_shutdown (int s, int how)
{
	if (s >= LBASE) { struct handle *x = lsock (s); if (!x) return -KAPI_EBADF; s = x->fd; }
	return (int) kret (sc2 (L_shutdown, s, how));
}
static int f_sock_close (int s) { return s >= LBASE ? f_handle_close (s) : (int) kret (sc1 (L_close, s)); }
static int f_sock_getopt (int s, int opt, int *v)
{
	int x = 0;
	unsigned len = 4;
	long r = 0;
	struct handle *ls = s >= LBASE ? lsock (s) : 0;
	if (s >= LBASE)
	{
		if (!ls) return -KAPI_EBADF;
		s = ls->fd;
		switch (opt)
		{
		case KAPI_SO_TYPE: *v = ls->stype; return 0;
		case KAPI_SO_DOMAIN: *v = KAPI_AF_UNIX; return 0;
		case KAPI_SO_RCVBUF: r = sc5 (L_getsockopt, s, 1, 8, &x, &len); *v = x / 2; return (int) kret (r < 0 ? r : 0);
		case KAPI_SO_NREAD: r = sc3 (L_ioctl, s, 0x541B, &x); *v = x; return (int) kret (r < 0 ? r : 0);
		case KAPI_SO_SNDBUF: r = sc5 (L_getsockopt, s, 1, 7, &x, &len); *v = x / 2; return (int) kret (r < 0 ? r : 0);
		case KAPI_SO_PEERPID:
		{
			int cred[3];
			len = sizeof cred;
			r = sc5 (L_getsockopt, s, 1, 17, cred, &len);
			if (r < 0) return (int) kret (r);
			*v = cred[0];
			return 0;
		}
		default: break;
		}
	}
	switch (opt)
	{
	case KAPI_SO_ERROR: r = sc5 (L_getsockopt, s, 1, 4, &x, &len); x = x ? lerr (x) : 0; break;
	case KAPI_SO_NONBLOCK: r = sc2 (L_fcntl, s, 3); x = r >= 0 && (r & L_O_NONBLOCK); r = r < 0 ? r : 0; break;
	case KAPI_SO_RCVTIMEO_MS:
	case KAPI_SO_SNDTIMEO_MS:
	{
		struct l_timespec tv;	/* (struct timeval: two longs) */
		len = sizeof tv;
		r = sc5 (L_getsockopt, s, 1, opt == KAPI_SO_RCVTIMEO_MS ? 20 : 21, &tv, &len);
		x = (int) (tv.tv_sec * 1000 + tv.tv_nsec / 1000);
		break;
	}
	case KAPI_SO_BROADCAST: r = sc5 (L_getsockopt, s, 1, 6, &x, &len); break;
	case KAPI_SO_NREAD: r = sc3 (L_ioctl, s, 0x541B, &x); break;
	case KAPI_SO_TYPE: r = sc5 (L_getsockopt, s, 1, 3, &x, &len); x = x == 1 ? KAPI_SOCK_STREAM : KAPI_SOCK_DGRAM; break;
	case KAPI_SO_ACCEPTCONN: r = sc5 (L_getsockopt, s, 1, 30, &x, &len); break;
	case KAPI_SO_DOMAIN: x = KAPI_AF_INET; break;
	default: return -KAPI_ENOPROTOOPT;
	}
	if (r < 0) return -lerr (-r);
	*v = x;
	return 0;
}
static int f_sock_setopt (int s, int opt, int v)
{
	long r;
	if (s >= LBASE)
	{
		struct handle *ls = lsock (s);
		if (!ls) return -KAPI_EBADF;
		s = ls->fd;
		if (opt == KAPI_SO_RCVBUF || opt == KAPI_SO_SNDBUF)	/* (the FORCE options past wmem_max, as root) */
		{
			if (sc5 (L_setsockopt, s, 1, opt == KAPI_SO_RCVBUF ? 33 : 32, &v, 4) == 0) return 0;
			return (int) kret (sc5 (L_setsockopt, s, 1, opt == KAPI_SO_RCVBUF ? 8 : 7, &v, 4) < 0 ? -22 : 0);
		}
	}
	switch (opt)
	{
	case KAPI_SO_NONBLOCK:
	{
		long fl = sc2 (L_fcntl, s, 3);
		r = sc3 (L_fcntl, s, 4, v ? fl | L_O_NONBLOCK : fl & ~L_O_NONBLOCK);
		break;
	}
	case KAPI_SO_RCVTIMEO_MS:
	case KAPI_SO_SNDTIMEO_MS:
	{
		struct l_timespec tv = { v / 1000, (long) (v % 1000) * 1000 };
		r = sc5 (L_setsockopt, s, 1, opt == KAPI_SO_RCVTIMEO_MS ? 20 : 21, &tv, sizeof tv);
		break;
	}
	case KAPI_SO_BROADCAST: r = sc5 (L_setsockopt, s, 1, 6, &v, 4); break;
	default: return -KAPI_ENOPROTOOPT;
	}
	return (int) kret (r < 0 ? r : 0);
}
static int f_sock_name (int s, int peer, struct kapi_sockaddr *o)
{
	if (s >= LBASE)
	{
		struct handle *x = lsock (s);
		if (!x) return -KAPI_EBADF;
		memset (o, 0, sizeof *o);
		o->family = KAPI_AF_UNIX;
		return 0;
	}
	struct l_sockaddr_in a;
	unsigned len = sizeof a;
	long r = sc3 (peer ? L_getpeername : L_getsockname, s, &a, &len);
	if (r < 0) return -lerr (-r);
	from_sin (&a, o);
	return 0;
}
static int f_poll (struct kapi_pollfd *fds, unsigned n, int ms)
{
	struct l_pollfd p[1024];
	if (n > 1024) return -KAPI_EINVAL;
	for (unsigned i = 0; i < n; i++)
	{
		p[i].fd = -1;
		p[i].events = fds[i].events;
		p[i].revents = 0;
		fds[i].revents = 0;
		if (fds[i].kind == KAPI_PK_SOCKET && fds[i].h >= LBASE)
		{
			struct handle *x = lsock (fds[i].h);
			if (!x) { fds[i].revents = KAPI_POLLNVAL; continue; }
			p[i].fd = x->fd;
		}
		else if (fds[i].kind == KAPI_PK_SOCKET)
			p[i].fd = fds[i].h;
		else if (fds[i].kind == KAPI_PK_STREAM)
		{
			struct handle *x = hget (fds[i].h, H_STREAM);
			if (!x) { fds[i].revents = KAPI_POLLNVAL; continue; }
			p[i].fd = (fds[i].events & KAPI_POLLIN) && x->fd >= 0 ? x->fd : x->fd2;
		}
		else if (fds[i].kind == KAPI_PK_FILE)
		{
			struct handle *x = hget (fds[i].h, H_OFILE);
			if (!x) { fds[i].revents = KAPI_POLLNVAL; continue; }
			p[i].fd = x->fd;
		}
	}
	struct l_timespec t = { ms / 1000, (long) (ms % 1000) * 1000000L };
	long r = sc4 (L_ppoll, p, n, ms < 0 ? 0 : (long) &t, 0);
	if (r < 0) return -lerr (-r);
	int c = 0;
	for (unsigned i = 0; i < n; i++)
	{
		if (p[i].fd >= 0)
			fds[i].revents = p[i].revents;
		if (fds[i].revents)
			c++;
	}
	return c;
}

/* ---- the screen, shared surfaces, no GPU (WebKit's compositor: tools/webkit/test-webkit.sh GPU=1) ----
 * A surface is a file $ROOT/.shm/surface-<id>: a header (w, h) in its first 64 KB, then the pixels,
 * mapped shared by every process that asks; its id is made of the owner's pid, so ids are unique
 * across the bench's processes. The GPU is "not there": user/Libs/gpucomp composites on the CPU. */
#define SURF_HDR	0x10000L
static int s_surfN;

static void f_screen_size (int *w, int *h) { if (w) *w = 1280; if (h) *h = 800; }
static int f_gpu_info (char *b, unsigned cap) { if (b && cap > 8) strcpy (b, "posixsim"); else if (b && cap) b[0] = 0; return 0; }

static int surf_path (int id, char *out)
{
	char name[32] = "surface-";
	int n = 8;
	char d[12]; int k = 0;
	if (id <= 0) return -1;
	while (id) { d[k++] = (char) ('0' + id % 10); id /= 10; }
	while (k) name[n++] = d[--k];
	name[n] = 0;
	return shm_path (name, out);
}

static int f_surface_create (int w, int h)
{
	if (w <= 0 || h <= 0) return 0;
	if (w > 1280) w = 1280;				/* (capped to the screen, as the kernel's) */
	if (h > 800) h = 800;
	int id = (int) ((sc0 (L_getpid) & 0x3FFFFF) << 8) | (++s_surfN & 0xFF);
	char p[512];
	if (surf_path (id, p) < 0) return 0;
	long fd = l_open (p, L_O_RDWR | L_O_CREAT | L_O_TRUNC | L_O_CLOEXEC, 0600);
	if (fd < 0) return 0;
	int hdr[2] = { w, h };
	if (sc2 (L_ftruncate, fd, SURF_HDR + (long) w * h * 4) < 0 || sc4 (L_pwrite64, fd, hdr, sizeof hdr, 0) != sizeof hdr)
		id = 0;
	sc1 (L_close, fd);
	return id;
}

static int f_surface_size (int id, int *w, int *h)
{
	char p[512];
	int hdr[2] = { 0, 0 };
	if (surf_path (id, p) < 0) return 0;
	long fd = l_open (p, L_O_RDONLY | L_O_CLOEXEC, 0);
	if (fd < 0) return 0;
	long r = sc4 (L_pread64, fd, hdr, sizeof hdr, 0);
	sc1 (L_close, fd);
	if (r != sizeof hdr || hdr[0] <= 0 || hdr[1] <= 0) return 0;
	if (w) *w = hdr[0];
	if (h) *h = hdr[1];
	return 1;
}

static unsigned *f_surface_map (int id)
{
	char p[512];
	int w, h;
	if (!f_surface_size (id, &w, &h) || surf_path (id, p) < 0) return 0;
	long fd = l_open (p, L_O_RDWR | L_O_CLOEXEC, 0);
	if (fd < 0) return 0;
	long m = sc6 (L_mmap, 0, (long) w * h * 4, 3, 0x01, fd, SURF_HDR);	/* MAP_SHARED */
	sc1 (L_close, fd);
	return m < 0 && m > -4096 ? 0 : (unsigned *) m;
}

static void f_surface_present (int id) { (void) id; sc0 (L_sched_yield); }

static int f_surface_destroy (int id)
{
	char p[512];
	if (surf_path (id, p) < 0) return 0;
	return sc3 (L_unlinkat, L_AT_FDCWD, p, 0) == 0;
}

/* ---- app cores (v51): each a host thread, so that code given to kapi_core_run really runs beside
 * the main thread (WebKit's tile rasterisation: tools/webkit/onyxcores.c). POSIXSIM_CORES: how many
 * are free (default 2: cores 2 and 3; 0: none, as when an emulator holds them).
 *
 * As on the Pi, the code of an app core knows where it runs and may make no kernel call:
 * - kapi__core () reads TPIDRRO_EL0, which only a kernel can set. run.sh makes the program read
 *   TPIDR2_EL0 instead (corereg.py: every "mrs xN, tpidrro_el0" of its code), a register qemu-user
 *   lets the program write: 0 on the threads, the core's number on a fake core (core_tramp).
 * - every entry of the kapi table goes through a guard (guard_install): called on a fake core, it
 *   says which slot, from where (the return addresses, for addr2line), and ends the program with
 *   status 97 -- or, POSIXSIM_CORE_FAULT=1, does what the Pi's kernel does: the job is stopped,
 *   the core's state is KAPI_CORE_FAULT, the program goes on (what it does of it is the test). ---- */
struct fcore
{
	volatile int used, running, fault;
	void (*fn) (void *);
	void *arg;
};
static struct fcore s_core[2];
static volatile int s_coreLock;

static inline unsigned long this_core (void)
{
	unsigned long c;
	__asm__ volatile ("mrs %0, s3_3_c13_c0_5" : "=r" (c));		/* TPIDR2_EL0 */
	return c;
}

#define GUARD_SLOTS	512
void *posixsim_orig[GUARD_SLOTS];		/* the entries themselves, by slot */
extern char posixsim_thunks[];
/* slot n's thunk: x16 = n, then the guard: on a thread the entry itself, on a core posixsim_core_call */
__asm__ (
"	.text\n"
"	.balign	8\n"
"	.globl	posixsim_thunks\n"
"posixsim_thunks:\n"
"	.set	posixsim_n, 0\n"
"	.rept	512\n"
"	mov	x16, #posixsim_n\n"
"	b	posixsim_guard\n"
"	.set	posixsim_n, posixsim_n + 1\n"
"	.endr\n"
"posixsim_guard:\n"
"	mrs	x17, s3_3_c13_c0_5\n"
"	cbnz	x17, 1f\n"
"	adrp	x17, posixsim_orig\n"
"	add	x17, x17, :lo12:posixsim_orig\n"
"	ldr	x17, [x17, x16, lsl #3]\n"
"	br	x17\n"
"1:	mov	x0, x16\n"
"	mov	x1, x30\n"
"	mov	x2, x29\n"
"	b	posixsim_core_call\n");

static char *put_hex (char *p, unsigned long v)
{
	char d[16]; int n = 0;
	do { d[n++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
	*p++ = '0'; *p++ = 'x';
	while (n > 0) *p++ = d[--n];
	return p;
}
static char *put_str (char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *put_dec (char *p, unsigned long v)
{
	char d[24]; int n = 0;
	do { d[n++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (n > 0) *p++ = d[--n];
	return p;
}

void posixsim_core_call (unsigned long slot, unsigned long lr, unsigned long *fp);
void posixsim_core_call (unsigned long slot, unsigned long lr, unsigned long *fp)
{
	unsigned long core = this_core ();
	const char *v = getenv_ ("POSIXSIM_CORE_FAULT");
	int faults = v && atoi_ (v) != 0;
	char line[700], *p = line;
	p = put_str (p, "posixsim: appcore: core ");
	p = put_dec (p, core);
	p = put_str (p, " made a kernel call: kapi slot ");
	p = put_dec (p, slot);
	p = put_str (p, " (table offset ");
	p = put_dec (p, slot * 8);
	p = put_str (p, faults ? "), job stopped; from " : "), the program is ended; from ");
	p = put_hex (p, lr);
	/* the callers' return addresses (the frame records: x29 -> { the caller's x29, its x30 }) */
	unsigned long *f = fp;
	for (int i = 0; i < 24 && f != 0 && ((unsigned long) f & 15) == 0; i++)
	{
		unsigned long *next = (unsigned long *) f[0];
		if (f[1] == 0)
			break;
		p = put_str (p, " < ");
		p = put_hex (p, f[1]);
		if (next <= f || (unsigned long) next - (unsigned long) f > (4UL << 20))
			break;
		f = next;
	}
	*p++ = '\n';
	sc3 (L_write, 2, line, p - line);
	if (!faults)
		sc1 (L_exit_group, 97);
	if (core >= 2 && core <= 3)
		__atomic_store_n (&s_core[core - 2].fault, 1, __ATOMIC_SEQ_CST);
	for (;;)
		sc1 (L_exit, 0);			/* (the host thread only: the core is stopped) */
}

/* Where a thread is: "kill -USR1 <the thread's Linux id>" (ls /proc/<pid>/task) makes it print its
 * core, its pc and its callers' return addresses (addr2line -e <the program>) -- a program that hangs
 * on the bench says where. */
static void where_handler (int sig, void *info, void *uc)
{
	(void) sig; (void) info;
	unsigned long *regs = (unsigned long *) ((char *) uc + 184);	/* (mcontext's x0 .. x30, sp, pc) */
	char line[700], *p = line;
	p = put_str (p, "posixsim: thread ");
	p = put_dec (p, (unsigned long) sc0 (L_gettid));
	p = put_str (p, " (core ");
	p = put_dec (p, this_core ());
	p = put_str (p, ") is at ");
	p = put_hex (p, regs[32]);
	p = put_str (p, " < ");
	p = put_hex (p, regs[30]);
	unsigned long *f = (unsigned long *) regs[29];
	for (int i = 0; i < 24 && f != 0 && ((unsigned long) f & 15) == 0; i++)
	{
		unsigned long *next = (unsigned long *) f[0];
		if (f[1] == 0)
			break;
		p = put_str (p, " < ");
		p = put_hex (p, f[1]);
		if (next <= f || (unsigned long) next - (unsigned long) f > (4UL << 20))
			break;
		f = next;
	}
	*p++ = '\n';
	sc3 (L_write, 2, line, p - line);
}

static void where_install (void)
{
	struct { void *handler; unsigned long flags; unsigned long mask; } sa = { (void *) where_handler, 4 /* SA_SIGINFO */ | 0x10000000 /* SA_RESTART */, 0 };
	sc4 (134 /* rt_sigaction */, 10 /* SIGUSR1 */, (long) &sa, 0, 8);
}

/* Every entry of the table (a pointer into the program) is replaced by its thunk. */
static void guard_install (void)
{
	void **t = (void **) KAPI_TABLE_VA;
	unsigned n = sizeof (struct TKApiTable) / 8;
	for (unsigned i = 1; i < n && i < GUARD_SLOTS; i++)
	{
		unsigned long e = (unsigned long) t[i];
		if (e < 0x200000000UL || e >= 0x280000000UL)
			continue;
		posixsim_orig[i] = t[i];
		t[i] = posixsim_thunks + 8 * i;
	}
}

static int core_tramp (void *p)
{
	struct fcore *c = (struct fcore *) p;
	__asm__ volatile ("msr s3_3_c13_c0_5, %0" :: "r" ((unsigned long) (2 + (c - s_core))));
	c->fn (c->arg);
	__atomic_store_n (&c->running, 0, __ATOMIC_SEQ_CST);
	sc1 (L_exit, 0);
	return 0;
}

static int f_core_acquire (void)
{
	const char *v = getenv_ ("POSIXSIM_CORES");
	int n = v ? atoi_ (v) : 2;
	int got = -1;
	lock (&s_coreLock);
	for (int i = 0; i < 2 && i < n; i++)
		if (!s_core[i].used) { s_core[i].used = 1; got = 2 + i; break; }
	unlock (&s_coreLock);
	return got;
}

static int f_core_run (int core, void (*fn) (void *), void *arg, void *stack_top)
{
	if (core < 2 || core > 3 || !s_core[core - 2].used || s_core[core - 2].running || !fn || !stack_top)
		return -1;
	struct fcore *c = &s_core[core - 2];
	c->fn = fn;
	c->arg = arg;
	c->running = 1;
	c->fault = 0;
	unsigned long tls;		/* (the job starts with its caller's TPIDR_EL0, as v75's) */
	__asm__ volatile ("mrs %0, tpidr_el0" : "=r" (tls));
	unsigned long flags = 0x100 | 0x200 | 0x400 | 0x800 | 0x10000 | 0x40000 | 0x80000 /* SETTLS */;
	long t = posixsim_clone (flags, (void *) ((unsigned long) stack_top & ~15UL), 0, tls, 0, core_tramp, c);
	if (t < 0) { c->running = 0; return -1; }
	return 0;
}

static int f_core_state (int core)
{
	if (core < 2 || core > 3 || !s_core[core - 2].used)
		return KAPI_CORE_NOTYOURS;
	if (s_core[core - 2].fault)
		return KAPI_CORE_FAULT;
	return s_core[core - 2].running ? KAPI_CORE_RUNNING : KAPI_CORE_IDLE;
}

static void f_core_release (int core)
{
	if (core < 2 || core > 3)
		return;
	/* (a job that still runs is left to run: its host thread cannot be stopped from here) */
	if (s_core[core - 2].fault)
		s_core[core - 2].running = s_core[core - 2].fault = 0;
	if (!s_core[core - 2].running)
		s_core[core - 2].used = 0;
}

/* ---- the table ---- */
void posixsim_init (long *sp);
void posixsim_init (long *sp)
{
	s_argc = sp[0];
	s_argv = (char **) (sp + 1);
	s_envp = s_argv + s_argc + 1;
	const char *v;
	if ((v = getenv_ ("POSIXSIM_ROOT"))) strncpy (s_root, v, sizeof s_root - 1);
	if ((v = getenv_ ("POSIXSIM_QEMU"))) strncpy (s_qemu, v, sizeof s_qemu - 1);
	if ((v = getenv_ ("POSIXSIM_LEVEL"))) s_level = atoi_ (v);
	if ((v = getenv_ ("POSIXSIM_TZ"))) s_tz = atoi_ (v);
	if ((v = getenv_ ("POSIXSIM_NONET"))) s_nonet = atoi_ (v);
	if ((v = getenv_ ("POSIXSIM_CWD"))) strncpy (s_cwd, v, sizeof s_cwd - 1);
	s_bootCnt = cntvct () - cntfrq () * 100;	/* ("booted" 100 s ago) */
	s_mainLtid = (int) sc0 (L_gettid);
	char d[300];
	strcpy (d, s_root); sc3 (L_mkdirat, L_AT_FDCWD, d, 0755);
	strcpy (d, s_root); strcat (d, "/SD"); sc3 (L_mkdirat, L_AT_FDCWD, d, 0755);
	strcpy (d, s_root); strcat (d, "/RAM"); sc3 (L_mkdirat, L_AT_FDCWD, d, 0755);

	/* the heap (sbrk) and the table */
	sc6 (L_mmap, 0x280000000L, 2L << 30, 3, 0x22 | 0x10 | 0x4000, -1, 0);
	long t = sc6 (L_mmap, (long) KAPI_TABLE_VA, 0x20000, 3, 0x22 | 0x10, -1, 0);
	if (t != (long) KAPI_TABLE_VA)
	{
		put ("posixsim: cannot map the kapi table\n");
		sc1 (L_exit_group, 99);
	}
	struct TKApiTable *T = KT_W;
	T->version = s_level >= 76 ? 76 : s_level >= 75 ? 75 : 74;
	T->create_window = f_create_window;
	T->yield = f_yield;
	T->msleep = f_msleep;
	T->get_ticks = f_get_ticks;
	T->exit = f_exit;
	T->write = f_write;
	T->get_datetime = f_get_datetime;
	T->sbrk = f_sbrk;
	T->meminfo = f_meminfo;
	T->open = f_open;
	T->read = f_read;
	T->fsize = f_fsize;
	T->fsize64 = f_fsize64;
	T->seek = f_seek;
	T->close = f_close;
	T->save_file = f_save_file;
	T->mkdir = f_mkdir;
	T->remove = f_remove;
	T->rename = f_rename;
	T->chdir = f_chdir;
	T->getcwd = f_getcwd;
	T->opendir = f_opendir;
	T->readdir = f_readdir;
	T->closedir = f_closedir;
	T->pipe = f_pipe;
	T->file_in = f_file_in;
	T->file_out = f_file_out;
	T->stream_read = f_stream_read;
	T->stream_read_nb = f_stream_read_nb;
	T->stream_write = f_stream_write;
	T->stream_eof = f_stream_eof;
	T->stream_close = f_stream_close;
	T->stdin_stream = f_stdin_stream;
	T->stdout_stream = f_stdout_stream;
	T->stdin_read = f_stdin_read;
	T->stdout_write = f_stdout_write;
	T->spawn = f_spawn;
	T->wait = f_wait;
	T->proc_done = f_proc_done;
	T->get_args = f_get_args;
	T->kill_pid = f_kill_pid;
	T->random = f_random;
	T->vol_info = f_vol_info;
	T->proc_stats = f_proc_stats;
	T->net_status = f_net_status;
	T->net_resolve = f_net_resolve;
	T->tcp_connect = f_tcp_connect;
	T->tcp_send = f_tcp_send;
	T->tcp_recv = f_tcp_recv;
	T->tcp_close = f_tcp_close;
	T->tcp_listen = f_tcp_listen;
	T->tcp_accept = f_tcp_accept;
	T->thread_create = f_thread_create;
	T->thread_exit = f_thread_exit;
	T->thread_join = f_thread_join;
	T->thread_self = f_thread_self;
	T->thread_priority = f_thread_priority;
	T->wait_word = f_wait_word;
	T->wake_word = f_wake_word;
	T->core_acquire = f_core_acquire;
	T->core_run = f_core_run;
	T->core_state = f_core_state;
	T->core_release = f_core_release;
	T->screen_size = f_screen_size;
	T->gpu_info = f_gpu_info;
	T->surface_create = f_surface_create;
	T->surface_map = f_surface_map;
	T->surface_size = f_surface_size;
	T->surface_present = f_surface_present;
	T->surface_destroy = f_surface_destroy;
	if (s_level >= 75)
	{
		T->vm_map = f_vm_map;
		T->vm_unmap = f_vm_unmap;
		T->vm_protect = f_vm_protect;
		T->vm_advise = f_vm_advise;
		T->vm_query = f_vm_query;
		T->vm_stats = f_vm_stats;
		T->thread_create_ex = f_thread_create_ex;
		T->thread_info = f_thread_info;
		T->file_open = f_file_open;
		T->file_read = f_file_read;
		T->file_write = f_file_write;
		T->file_seek = f_file_seek;
		T->file_truncate = f_file_truncate;
		T->file_sync = f_file_sync;
		T->file_stat = f_file_stat;
		T->file_close = f_file_close;
		T->path_stat = f_path_stat;
		T->path_unlink = f_path_unlink;
		T->path_mkdir = f_path_mkdir;
		T->path_rename = f_path_rename;
		T->path_utime = f_path_utime;
		T->dir_read = f_dir_read;
		T->stream_write_nb = f_stream_write_nb;
		T->spawn_ex = f_spawn_ex;
		T->proc_wait = f_proc_wait;
		T->get_argv = f_get_argv;
		T->get_env = f_get_env;
		T->getpid = f_getpid;
		T->clock_info = f_clock_info;
		T->sleep_us = f_sleep_us;
		T->sock_open = f_sock_open;
		T->sock_connect = f_sock_connect;
		T->sock_bind = f_sock_bind;
		T->sock_listen = f_sock_listen;
		T->sock_accept = f_sock_accept;
		T->sock_send = f_sock_send;
		T->sock_recv = f_sock_recv;
		T->sock_shutdown = f_sock_shutdown;
		T->sock_close = f_sock_close;
		T->sock_getopt = f_sock_getopt;
		T->sock_setopt = f_sock_setopt;
		T->sock_name = f_sock_name;
		T->poll = f_poll;
	}
	if (s_level >= 76)
	{
		T->sock_pair = f_sock_pair;
		T->sock_sendmsg = f_sock_sendmsg;
		T->sock_recvmsg = f_sock_recvmsg;
		T->shm_create = f_shm_create;
		T->shm_open = f_shm_open;
		T->shm_unlink = f_shm_unlink;
		T->shm_ctl = f_shm_ctl;
		T->shm_map = f_shm_map;
		T->handle_close = f_handle_close;
		T->spawn_ex2 = f_spawn_ex2;
		T->get_handles = f_get_handles;
	}
	guard_install ();
	where_install ();
}
