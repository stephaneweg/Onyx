/*
 * fakekapi.c -- posixsim: a stand-in Onyx kernel to run libonyxposix programs on the PC, as aarch64
 * Linux processes under qemu-user (tools/tests/posixsim/run.sh; docs/03 §5.4).
 *
 * The program is linked as for Onyx (crt0posix, onyx-posix.ld, libonyxposix, newlib) plus this
 * file and start.S, whose entry maps a kapi table at KAPI_TABLE_VA (kern/kapi_abi.h) and fills it
 * with functions made of raw Linux system calls, then jumps to the program's _start. So the
 * library runs as on the Pi -- its start-up, TLS on TPIDR_EL0, the descriptor table, newlib's
 * stdio, the futex-based locks with real concurrency -- against:
 *   POSIXSIM_LEVEL=75 (default): the v75 calls (files, sockets, poll, threads with TLS, vm_*,
 *                     spawn / wait, clock, environment), as WP-MEM / WP-FILE/PROC / WP-NET
 *                     specify them (docs/POSIX-PLAN.md §3);
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
static int s_level = 75;
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
enum { H_FREE, H_FILE, H_DIR, H_STREAM, H_PROC, H_OFILE };
struct handle
{
	int type;
	int fd, fd2;				/* STREAM: read end, write end */
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
static long do_spawn (const char *path, char **argv, char **envp, const char *cwd, void *in, void *out)
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
		if (strncmp (envp[i], "POSIXSIM_ARGV0=", 15) && strncmp (envp[i], "POSIXSIM_CWD=", 13))
			ev[m++] = envp[i];
	ev[m++] = a0;
	ev[m++] = cw;
	ev[m] = 0;
	long pid = sc5 (L_clone, 17 /* SIGCHLD */, 0, 0, 0, 0);
	if (pid == 0)
	{
		if (hi && hi->fd >= 0) sc3 (24 /* dup3 */, hi->fd, 0, 0);
		if (ho && ho->fd2 >= 0) { sc3 (24, ho->fd2, 1, 0); sc3 (24, ho->fd2, 2, 0); }
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
	struct l_timespec t = { ms / 1000, (long) (ms % 1000) * 1000000L };
	long r = sc6 (L_futex, (long) a, 0 /* FUTEX_WAIT */, expected, ms == KAPI_WAIT_FOREVER ? 0 : (long) &t, 0, 0);
	return r == -110 ? 1 : 0;
}
static int f_wake_word (volatile unsigned *a) { return (int) sc6 (L_futex, (long) a, 1, 0x7FFFFFFF, 0, 0, 0); }

/* ---- v75: memory ---- */
static long long f_vm_map (unsigned long long addr, unsigned long long len, unsigned prot, unsigned flags)
{
	if (prot & KAPI_PROT_EXEC) return -KAPI_ENOTSUP;
	if (len == 0) return -KAPI_EINVAL;
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
	if (p & KAPI_PROT_EXEC) return -KAPI_ENOTSUP;
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
		unsigned prot = 0;
		if (*l == ' ') { l++; if (l[0] == 'r') prot |= 1; if (l[1] == 'w') prot |= 2; }
		while (*l && *l != '\n') l++;
		if (*l) l++;
		if (addr >= a && addr < b) { o->start = a; o->end = b; o->prot = prot; o->kind = KAPI_VMK_ANON; o->resident = 0; o->flags = KAPI_VMF_LAZY; return 0; }
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
static int f_sock_connect (int s, const struct kapi_sockaddr *to) { struct l_sockaddr_in a; to_sin (to, &a); return (int) kret (sc3 (L_connect, s, &a, sizeof a)); }
static int f_sock_bind (int s, const struct kapi_sockaddr *to)
{
	struct l_sockaddr_in a;
	int one = 1;
	to_sin (to, &a);
	sc5 (L_setsockopt, s, 1, 2, &one, 4);
	return (int) kret (sc3 (L_bind, s, &a, sizeof a));
}
static int f_sock_listen (int s, int b) { return (int) kret (sc2 (L_listen, s, b < 1 ? 1 : b > 32 ? 32 : b)); }
static int f_sock_accept (int s, struct kapi_sockaddr *peer, unsigned flags)
{
	struct l_sockaddr_in a;
	unsigned len = sizeof a;
	long r = sc4 (L_accept4, s, &a, &len, 02000000 | ((flags & KAPI_SOCKF_NONBLOCK) ? L_O_NONBLOCK : 0));
	if (r >= 0 && peer) from_sin (&a, peer);
	return (int) kret (r);
}
static long long f_sock_send (int s, const void *b, unsigned long long n, unsigned flags, const struct kapi_sockaddr *to)
{
	struct l_sockaddr_in a;
	if (to) to_sin (to, &a);
	return kret (sc6 (L_sendto, s, (long) b, (long) n, (long) (flags | 0x4000), to ? (long) &a : 0, to ? (long) sizeof a : 0));
}
static long long f_sock_recv (int s, void *b, unsigned long long n, unsigned flags, struct kapi_sockaddr *from)
{
	struct l_sockaddr_in a;
	unsigned len = sizeof a;
	long r = sc6 (L_recvfrom, s, (long) b, (long) n, (long) flags, from ? (long) &a : 0, from ? (long) &len : 0);
	if (r >= 0 && from) from_sin (&a, from);
	return kret (r);
}
static int f_sock_shutdown (int s, int how) { return (int) kret (sc2 (L_shutdown, s, how)); }
static int f_sock_close (int s) { return (int) kret (sc1 (L_close, s)); }
static int f_sock_getopt (int s, int opt, int *v)
{
	int x = 0;
	unsigned len = 4;
	long r = 0;
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
	default: return -KAPI_ENOPROTOOPT;
	}
	if (r < 0) return -lerr (-r);
	*v = x;
	return 0;
}
static int f_sock_setopt (int s, int opt, int v)
{
	long r;
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
		if (fds[i].kind == KAPI_PK_SOCKET)
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
	T->version = s_level >= 75 ? 75 : 74;
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
}
