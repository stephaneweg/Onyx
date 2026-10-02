/*
 * misc.c -- the rest of the POSIX surface (libonyxposix, docs/POSIX-PLAN.md §3.4): sysconf,
 * pathconf, uname, gethostname, getrandom / getentropy, rlimits and rusage, the user database
 * (one user, "onyx"), ttys, ioctl, and the stubs code links against: dlopen, backtrace, syslog,
 * err / warn, network interfaces.
 *
 * sysconf: the page is 64 KB; ONE processor online -- all of a process's threads run on core 0
 * (the app cores are kapi_core_run's, not threads'); the physical pages from kapi meminfo.
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
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>
#include <termios.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <syslog.h>
#include <err.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include "posix_internal.h"

/* ---- sysconf / pathconf ---- */
long sysconf (int name)
{
	switch (name)
	{
	case _SC_PAGESIZE:		return (long) ONYX_PAGE;
	case _SC_NPROCESSORS_CONF:
	case _SC_NPROCESSORS_ONLN:	return 1;
	case _SC_PHYS_PAGES:
	case _SC_AVPHYS_PAGES:
	{
		unsigned long total = 0, freekb = 0, app = 0;
		unsigned page = 0;
		if (kapi__core () != 0 || !kapi_meminfo (&total, &freekb, &app, &page))
			return -1;
		return (long) ((name == _SC_PHYS_PAGES ? total : freekb) / (ONYX_PAGE / 1024));
	}
	case _SC_OPEN_MAX:		return ONYX_FD_MAX;
	case _SC_CLK_TCK:		return 100;
	case _SC_ARG_MAX:		return 65536;
	case _SC_CHILD_MAX:		return 64;
	case _SC_HOST_NAME_MAX:		return 64;
	case _SC_LOGIN_NAME_MAX:	return 32;
	case _SC_LINE_MAX:		return 2048;
	case _SC_GETPW_R_SIZE_MAX:
	case _SC_GETGR_R_SIZE_MAX:	return 1024;
	case _SC_THREAD_KEYS_MAX:	return PTHREAD_KEYS_MAX;
	case _SC_THREAD_STACK_MIN:	return PTHREAD_STACK_MIN;
	case _SC_THREAD_THREADS_MAX:	return PTHREAD_THREADS_MAX;
	case _SC_THREAD_DESTRUCTOR_ITERATIONS: return PTHREAD_DESTRUCTOR_ITERATIONS;
	case _SC_SEM_VALUE_MAX:		return 0x7FFFFFFF;
	case _SC_IOV_MAX:		return 1024;
	case _SC_TZNAME_MAX:		return 6;
	case _SC_VERSION:		return 200809L;
	case _SC_THREADS:
	case _SC_THREAD_SAFE_FUNCTIONS:
	case _SC_TIMEOUTS:
	case _SC_MONOTONIC_CLOCK:
	case _SC_CLOCK_SELECTION:
	case _SC_SEMAPHORES:
	case _SC_SPIN_LOCKS:
	case _SC_BARRIERS:
	case _SC_READER_WRITER_LOCKS:
	case _SC_MAPPED_FILES:
	case _SC_TIMERS:
	case _SC_SPAWN:
	case _SC_FSYNC:			return 200809L;
	case _SC_NGROUPS_MAX:		return 0;
	case _SC_RTSIG_MAX:		return 0;
	case _SC_SIGQUEUE_MAX:		return 0;
	case _SC_LEVEL1_DCACHE_LINESIZE: return 64;
	default:
		errno = EINVAL;
		return -1;
	}
}

long fpathconf (int fd, int name)
{
	(void) fd;
	switch (name)
	{
	case _PC_NAME_MAX:		return 255;
	case _PC_PATH_MAX:		return PATH_MAX;
	case _PC_PIPE_BUF:		return 512;
	case _PC_LINK_MAX:		return 1;
	case _PC_NO_TRUNC:		return 1;
	case _PC_CHOWN_RESTRICTED:	return 1;
	case _PC_VDISABLE:		return 0;
	case _PC_FILESIZEBITS:		return 64;
	case _PC_TIMESTAMP_RESOLUTION:	return 2000000000L;	/* FAT: 2 s */
	default:
		errno = EINVAL;
		return -1;
	}
}

long pathconf (const char *path, int name) { (void) path; return fpathconf (-1, name); }

/* ---- the machine ---- */
int gethostname (char *buf, size_t len)
{
	const char *h = getenv ("HOSTNAME");
	if (h == 0 || *h == '\0')
		h = "onyx";
	if (strlen (h) >= len)
		return ONYX_ERR (ENAMETOOLONG);
	strcpy (buf, h);
	return 0;
}

int sethostname (const char *name, size_t len)
{
	char b[65];
	if (len >= sizeof b)
		return ONYX_ERR (EINVAL);
	memcpy (b, name, len);
	b[len] = '\0';
	return setenv ("HOSTNAME", b, 1);
}

int uname (struct utsname *u)
{
	memset (u, 0, sizeof *u);
	strcpy (u->sysname, "Onyx");
	gethostname (u->nodename, sizeof u->nodename);
	snprintf (u->release, sizeof u->release, "kapi%u", kapi__core () == 0 ? KT->version : 0);
	strcpy (u->version, "Onyx (Raspberry Pi 4, Circle)");
	strcpy (u->machine, "aarch64");
	strcpy (u->domainname, "(none)");
	return 0;
}

long gethostid (void) { return 0x4F4E5958; }

/* ---- randomness (kapi random, v30) ---- */
ssize_t getrandom (void *buf, size_t n, unsigned flags)
{
	(void) flags;
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	size_t done = 0;
	while (done < n)
	{
		unsigned c = n - done > 65536 ? 65536 : (unsigned) (n - done);
		int r = kapi_random ((char *) buf + done, c);
		if (r <= 0)
			return done ? (ssize_t) done : ONYX_ERR (EIO);
		done += (unsigned) r;
	}
	return (ssize_t) done;
}

int getentropy (void *buf, size_t n)
{
	if (n > 256)
		return ONYX_ERR (EIO);
	return getrandom (buf, n, 0) == (ssize_t) n ? 0 : -1;
}
int _getentropy (void *buf, size_t n) { return getentropy (buf, n); }

/* ---- limits and usage ---- */
static rlim_t s_nofile = ONYX_FD_MAX;

int getrlimit (int r, struct rlimit *l)
{
	switch (r)
	{
	case RLIMIT_STACK:
	{
		pthread_attr_t a;
		size_t sz = 1 << 20;
		if (pthread_getattr_np ((pthread_t) __onyx_main_thread, &a) == 0)
			pthread_attr_getstacksize (&a, &sz);
		l->rlim_cur = l->rlim_max = sz;
		return 0;
	}
	case RLIMIT_NOFILE:
		l->rlim_cur = s_nofile;
		l->rlim_max = ONYX_FD_MAX;
		return 0;
	case RLIMIT_NPROC:
		l->rlim_cur = l->rlim_max = 64;
		return 0;
	case RLIMIT_CORE:
		l->rlim_cur = l->rlim_max = 0;
		return 0;
	default:
		if (r < 0 || r >= RLIM_NLIMITS)
			return ONYX_ERR (EINVAL);
		l->rlim_cur = l->rlim_max = RLIM_INFINITY;
		return 0;
	}
}

int setrlimit (int r, const struct rlimit *l)
{
	struct rlimit cur;
	if (getrlimit (r, &cur) != 0)
		return -1;
	if (l->rlim_cur > cur.rlim_max || l->rlim_max > cur.rlim_max)
		return ONYX_ERR (EPERM);
	if (r == RLIMIT_NOFILE)
		s_nofile = l->rlim_cur;
	return 0;
}

int getrusage (int who, struct rusage *u)
{
	memset (u, 0, sizeof *u);
	if (who == RUSAGE_CHILDREN)
		return 0;
	unsigned long long ns = __onyx_mono_ns ();
	u->ru_utime.tv_sec = (time_t) (ns / 1000000000ULL);
	u->ru_utime.tv_usec = (suseconds_t) ((ns % 1000000000ULL) / 1000);
	struct kapi_vm_stats vs;
	if (kapi__core () == 0 && kapi_vm_stats (0, &vs) == 0)
		u->ru_maxrss = (long) (vs.resident / 1024);
	return 0;
}

int getpriority (int which, id_t who) { (void) which; (void) who; errno = 0; return 0; }
int setpriority (int which, id_t who, int prio) { (void) which; (void) who; (void) prio; return 0; }
int nice (int inc) { (void) inc; return 0; }

/* ---- users: one, "onyx" (uid 0), its home from HOME ---- */
uid_t getuid (void) { return 0; }
uid_t geteuid (void) { return 0; }
gid_t getgid (void) { return 0; }
gid_t getegid (void) { return 0; }
int setuid (uid_t u) { return u == 0 ? 0 : ONYX_ERR (EPERM); }
int setgid (gid_t g) { return g == 0 ? 0 : ONYX_ERR (EPERM); }
int seteuid (uid_t u) { return setuid (u); }
int setegid (gid_t g) { return setgid (g); }
int getgroups (int n, gid_t *list) { (void) n; (void) list; return 0; }
int issetugid (void) { return 0; }

static int fill_pw (struct passwd *pw, char *buf, size_t len)
{
	const char *home = getenv ("HOME");
	if (home == 0)
		home = "SD:/home";
	size_t need = strlen (home) + 32;
	if (len < need)
		return ERANGE;
	char *p = buf;
	pw->pw_name = strcpy (p, "onyx"); p += 5;
	pw->pw_passwd = strcpy (p, "x"); p += 2;
	pw->pw_uid = 0;
	pw->pw_gid = 0;
	pw->pw_comment = strcpy (p, ""); p += 1;
	pw->pw_gecos = pw->pw_comment;
	pw->pw_dir = strcpy (p, home); p += strlen (home) + 1;
	pw->pw_shell = strcpy (p, "SD:/bin/cmd");
	return 0;
}

int getpwuid_r (uid_t uid, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
	*res = 0;
	if (uid != 0)
		return 0;
	int r = fill_pw (pw, buf, len);
	if (r == 0)
		*res = pw;
	return r;
}

int getpwnam_r (const char *name, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
	*res = 0;
	if (strcmp (name, "onyx") != 0 && strcmp (name, "root") != 0)
		return 0;
	return getpwuid_r (0, pw, buf, len, res);
}

struct passwd *getpwuid (uid_t uid)
{
	static struct passwd pw;
	static char buf[512];
	struct passwd *r;
	getpwuid_r (uid, &pw, buf, sizeof buf, &r);
	return r;
}

struct passwd *getpwnam (const char *name)
{
	static struct passwd pw;
	static char buf[512];
	struct passwd *r;
	getpwnam_r (name, &pw, buf, sizeof buf, &r);
	return r;
}

static int s_pwent;
struct passwd *getpwent (void) { return s_pwent++ == 0 ? getpwuid (0) : 0; }
void setpwent (void) { s_pwent = 0; }
void endpwent (void) { s_pwent = 0; }

char *getlogin (void) { return "onyx"; }
int getlogin_r (char *b, size_t n)
{
	if (n < 5)
		return ERANGE;
	strcpy (b, "onyx");
	return 0;
}

/* ---- ttys: the console descriptors are ttys; nothing can be set ---- */
static int is_console (int fd)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	int r = d->type == ONYX_FD_CONSOLE;
	__onyx_fd_put (d);
	return r;
}

int _isatty (int fd)
{
	int c = is_console (fd);
	if (c < 0)
		return 0;
	if (!c)
		errno = ENOTTY;
	return c;
}
int isatty (int fd) { return _isatty (fd); }

char *ttyname (int fd) { return is_console (fd) == 1 ? "/dev/tty" : (errno = ENOTTY, (char *) 0); }
int ttyname_r (int fd, char *b, size_t n)
{
	if (is_console (fd) != 1)
		return ENOTTY;
	if (n < 9)
		return ERANGE;
	strcpy (b, "/dev/tty");
	return 0;
}

int tcgetattr (int fd, struct termios *t) { (void) fd; (void) t; return ONYX_ERR (ENOTTY); }
int tcsetattr (int fd, int a, const struct termios *t) { (void) fd; (void) a; (void) t; return ONYX_ERR (ENOTTY); }
int tcflush (int fd, int q) { (void) fd; (void) q; return 0; }
int tcdrain (int fd) { (void) fd; return 0; }
speed_t cfgetispeed (const struct termios *t) { return t->c_ispeed; }
speed_t cfgetospeed (const struct termios *t) { return t->c_ospeed; }
int cfsetispeed (struct termios *t, speed_t s) { t->c_ispeed = s; return 0; }
int cfsetospeed (struct termios *t, speed_t s) { t->c_ospeed = s; return 0; }
void cfmakeraw (struct termios *t)
{
	t->c_iflag &= ~(unsigned) (IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	t->c_oflag &= ~(unsigned) OPOST;
	t->c_lflag &= ~(unsigned) (ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	t->c_cflag = (t->c_cflag & ~(unsigned) (CSIZE | PARENB)) | CS8;
}

int ioctl (int fd, unsigned long req, ...)
{
	va_list ap;
	va_start (ap, req);
	void *arg = va_arg (ap, void *);
	va_end (ap);
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	switch (req)
	{
	case FIONBIO:
	{
		int on = arg ? *(int *) arg : 0;
		if (d->type == ONYX_FD_SOCKET || d->type == ONYX_FD_LSOCKET)
			__onyx_sock_set_nonblock (d, on);
		if (on)
			d->flags |= O_NONBLOCK;
		else
			d->flags &= ~O_NONBLOCK;
		break;
	}
	case FIONREAD:
	{
		int n = 0;
		if (d->type == ONYX_FD_SOCKET)
			kapi_sock_getopt ((int) d->h, KAPI_SO_NREAD, &n);
		else if (d->carry_n > d->carry_off)
			n = (int) (d->carry_n - d->carry_off);
		else if (d->type == ONYX_FD_FILE || d->type == ONYX_FD_LFILE)
		{
			struct stat st;
			off_t pos = __onyx_lseek (d, 0, SEEK_CUR);
			if (__onyx_fstat (d, &st) == 0 && pos >= 0 && st.st_size > pos)
				n = (int) (st.st_size - pos);
		}
		if (arg)
			*(int *) arg = n;
		break;
	}
	case FIOCLEX:
	case FIONCLEX:
		__onyx_fd_cloexec (fd, req == FIOCLEX);
		break;
	case TIOCGWINSZ:
		if (d->type != ONYX_FD_CONSOLE)
			r = ONYX_ERR (ENOTTY);
		else if (arg)
		{
			struct winsize *w = (struct winsize *) arg;
			memset (w, 0, sizeof *w);
			const char *c = getenv ("COLUMNS"), *l = getenv ("LINES");
			w->ws_col = (unsigned short) (c ? atoi (c) : 80);
			w->ws_row = (unsigned short) (l ? atoi (l) : 25);
		}
		break;
	default:
		r = ONYX_ERR (ENOTTY);
	}
	__onyx_fd_put (d);
	return r;
}

/* ---- dynamic loading: none (static programs) ---- */
static const char *s_dlerr;
void *dlopen (const char *file, int mode)
{
	(void) mode;
	if (file == 0)
		return (void *) 1;			/* "the program itself": dlsym finds nothing in it */
	s_dlerr = "dlopen: Onyx programs are static (no shared objects)";
	return 0;
}
void *dlsym (void *h, const char *name) { (void) h; (void) name; s_dlerr = "dlsym: no symbol table at run time"; return 0; }
int dlclose (void *h) { (void) h; return 0; }
char *dlerror (void) { const char *e = s_dlerr; s_dlerr = 0; return (char *) e; }
int dladdr (const void *a, Dl_info *i) { (void) a; memset (i, 0, sizeof *i); return 0; }

/* ---- backtraces: the frame-pointer chain ---- */
int backtrace (void **buf, int n)
{
	int k = 0;
	void **fp = (void **) __builtin_frame_address (0);
	while (fp && k < n)
	{
		void *lr = fp[1];
		if (lr == 0)
			break;
		buf[k++] = lr;
		void **next = (void **) fp[0];
		if (next <= fp || (unsigned long) next - (unsigned long) fp > (16UL << 20))
			break;
		fp = next;
	}
	return k;
}

char **backtrace_symbols (void *const *buf, int n)
{
	char **v = (char **) malloc ((size_t) n * (sizeof (char *) + 24));
	if (v == 0)
		return 0;
	char *s = (char *) (v + n);
	for (int i = 0; i < n; i++)
	{
		v[i] = s;
		snprintf (s, 24, "[%p]", buf[i]);
		s += 24;
	}
	return v;
}

void backtrace_symbols_fd (void *const *buf, int n, int fd)
{
	for (int i = 0; i < n; i++)
	{
		char line[32];
		int l = snprintf (line, sizeof line, "[%p]\n", buf[i]);
		write (fd, line, (size_t) l);
	}
}

/* ---- syslog: the kernel log (kapi write's fd is ignored: it goes to kmsg) ---- */
static const char *s_ident;
static int s_logmask = 0xFF;
void openlog (const char *ident, int opt, int fac) { (void) opt; (void) fac; s_ident = ident; }
void closelog (void) { }
int setlogmask (int m) { int old = s_logmask; if (m) s_logmask = m; return old; }
void vsyslog (int prio, const char *fmt, va_list ap)
{
	if (!(s_logmask & LOG_MASK (prio & 7)) || kapi__core () != 0)
		return;
	char line[512];
	int n = snprintf (line, sizeof line, "%s: ", s_ident ? s_ident : program_invocation_short_name);
	if (n < 0 || n >= (int) sizeof line)
		n = 0;
	vsnprintf (line + n, sizeof line - (size_t) n, fmt, ap);
	kapi_write (2, line, (unsigned) strlen (line));
}
void syslog (int prio, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vsyslog (prio, fmt, ap);
	va_end (ap);
}

/* ---- err / warn ---- */
void vwarnx (const char *fmt, va_list ap)
{
	fprintf (stderr, "%s: ", program_invocation_short_name);
	if (fmt)
		vfprintf (stderr, fmt, ap);
	fputc ('\n', stderr);
}
void vwarn (const char *fmt, va_list ap)
{
	int e = errno;
	fprintf (stderr, "%s: ", program_invocation_short_name);
	if (fmt)
	{
		vfprintf (stderr, fmt, ap);
		fputs (": ", stderr);
	}
	fprintf (stderr, "%s\n", strerror (e));
}
void warn (const char *fmt, ...) { va_list ap; va_start (ap, fmt); vwarn (fmt, ap); va_end (ap); }
void warnx (const char *fmt, ...) { va_list ap; va_start (ap, fmt); vwarnx (fmt, ap); va_end (ap); }
void verr (int code, const char *fmt, va_list ap) { vwarn (fmt, ap); exit (code); }
void verrx (int code, const char *fmt, va_list ap) { vwarnx (fmt, ap); exit (code); }
void err (int code, const char *fmt, ...) { va_list ap; va_start (ap, fmt); verr (code, fmt, ap); }
void errx (int code, const char *fmt, ...) { va_list ap; va_start (ap, fmt); verrx (code, fmt, ap); }

/* ---- network interfaces: one, "wlan0" ---- */
unsigned if_nametoindex (const char *n) { return n && strcmp (n, "wlan0") == 0 ? 1 : 0; }
char *if_indextoname (unsigned i, char *buf)
{
	if (i != 1)
	{
		errno = ENXIO;
		return 0;
	}
	strcpy (buf, "wlan0");
	return buf;
}
struct if_nameindex *if_nameindex (void)
{
	struct if_nameindex *v = (struct if_nameindex *) calloc (2, sizeof *v);
	if (v)
	{
		v[0].if_index = 1;
		v[0].if_name = "wlan0";
	}
	return v;
}
void if_freenameindex (struct if_nameindex *v) { free (v); }
int getifaddrs (struct ifaddrs **ifa) { *ifa = 0; return ONYX_ERR (ENOSYS); }
void freeifaddrs (struct ifaddrs *ifa) { (void) ifa; }

/* ---- iconv: newlib's is configured out; code that probes it gets a clean failure ---- */
#include <iconv.h>
iconv_t iconv_open (const char *to, const char *from) { (void) to; (void) from; errno = EINVAL; return (iconv_t) -1; }
size_t iconv (iconv_t cd, char **in, size_t *inl, char **out, size_t *outl)
{
	(void) cd; (void) in; (void) inl; (void) out; (void) outl;
	errno = EBADF;
	return (size_t) -1;
}
int iconv_close (iconv_t cd) { (void) cd; return 0; }
