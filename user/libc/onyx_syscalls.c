//
// onyx_syscalls.c -- the newlib "system call" layer for Onyx userland apps.
//
// The aarch64-none-elf toolchain ships newlib (libc + libm). newlib is portable C
// that bottoms out in a handful of POSIX-ish syscall stubs (_sbrk, _read, _write,
// _open, ...). This file implements those stubs on top of the Onyx kapi ABI, so an
// app can be built against newlib and use the real <stdio.h>/<stdlib.h>/<string.h>/
// <math.h> instead of the freestanding helpers (applib.h / umm.h).
//
// Link this object together with crt0libc.S (which calls exit() after main, so
// stdio is flushed). Heap: _sbrk maps onto kapi_sbrk -- so newlib's malloc owns the
// per-process heap. Do NOT also link umm.h in a newlib app (one allocator only).
//
// Files: the kapi file API is sequential (open/read/fsize/close, no seek). To give
// newlib full FILE* semantics (fseek/ftell/fwrite) we back each open fd with an
// in-memory buffer: a read opens by slurping the whole file into RAM; a writable fd
// accumulates in RAM and is written out with kapi_save_file() on close. Resource
// files (CSS, certs, small assets) fit comfortably; this is the pragmatic shim until
// a kapi_lseek lands.
//
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <fcntl.h>
#include <unistd.h>		// SEEK_SET / SEEK_CUR / SEEK_END
#include <string.h>
#include <stdlib.h>

#include "kapi.h"

#undef errno
extern int errno;

// newlib references environ.
static char *s_env_empty[1] = { 0 };
char **environ = s_env_empty;

// crtbegin.o (a startfile we omit with -nostartfiles) normally defines this; some
// libc atexit/__cxa_atexit paths reference it.
void *__dso_handle = 0;

// ---- in-memory fd table -----------------------------------------------------
// fds 0/1/2 are the console (handled inline); real files start at FD_BASE.

#define FD_BASE   3
#define MAX_FILES 32

typedef struct
{
	int            used;
	int            writable;
	unsigned char *buf;
	long           size;	// valid bytes
	long           cap;	// allocated bytes
	long           pos;	// current offset
	char           path[256];
} TFile;

static TFile s_files[MAX_FILES];

static TFile *fd_get (int fd)
{
	int i = fd - FD_BASE;
	if (i < 0 || i >= MAX_FILES || !s_files[i].used)
		return 0;
	return &s_files[i];
}

static int fd_alloc (void)
{
	for (int i = 0; i < MAX_FILES; i++)
		if (!s_files[i].used)
			return FD_BASE + i;
	return -1;
}

// Grow f->buf so f->cap >= need. Returns 0 on success, -1 on OOM.
static int file_reserve (TFile *f, long need)
{
	if (need <= f->cap)
		return 0;
	long ncap = f->cap ? f->cap : 256;
	while (ncap < need)
		ncap *= 2;
	unsigned char *nb = (unsigned char *) realloc (f->buf, (size_t) ncap);
	if (!nb)
		return -1;
	f->buf = nb;
	f->cap = ncap;
	return 0;
}

// ---- memory -----------------------------------------------------------------

void *_sbrk (ptrdiff_t incr)
{
	void *prev = kapi_sbrk ((long) incr);
	if (prev == (void *) -1)
	{
		errno = ENOMEM;
		return (void *) -1;
	}
	return prev;
}

// ---- console + files --------------------------------------------------------

int _write (int fd, const void *vbuf, size_t len)
{
	const unsigned char *buf = (const unsigned char *) vbuf;

	// stdout/stderr go to THIS TASK'S stdout stream (what the terminal reads), not
	// kapi_write() -- that one ignores the fd and dumps to the kernel log (kmsg).
	if (fd == 1 || fd == 2)
		return kapi_stdout_write (buf, (unsigned) len);

	TFile *f = fd_get (fd);
	if (!f || !f->writable)
	{
		errno = EBADF;
		return -1;
	}
	if (file_reserve (f, f->pos + (long) len) != 0)
	{
		errno = ENOMEM;
		return -1;
	}
	memcpy (f->buf + f->pos, buf, len);
	f->pos += (long) len;
	if (f->pos > f->size)
		f->size = f->pos;
	return (int) len;
}

int _read (int fd, void *vbuf, size_t len)
{
	if (fd == 0)				// stdin
		return kapi_stdin_read (vbuf, (unsigned) len);

	TFile *f = fd_get (fd);
	if (!f)
	{
		errno = EBADF;
		return -1;
	}
	long avail = f->size - f->pos;
	if (avail <= 0)
		return 0;			// EOF
	long n = (long) len < avail ? (long) len : avail;
	memcpy (vbuf, f->buf + f->pos, (size_t) n);
	f->pos += n;
	return (int) n;
}

int _open (const char *name, int flags, int mode)
{
	(void) mode;

	int fd = fd_alloc ();
	if (fd < 0)
	{
		errno = EMFILE;
		return -1;
	}
	TFile *f = &s_files[fd - FD_BASE];
	memset (f, 0, sizeof *f);

	int acc = flags & O_ACCMODE;		// O_RDONLY / O_WRONLY / O_RDWR
	f->writable = (acc == O_WRONLY || acc == O_RDWR);

	// Slurp any existing content unless we are truncating.
	if (!(flags & O_TRUNC))
	{
		void *h = kapi_open (name);
		if (h)
		{
			unsigned sz = kapi_fsize (h);
			if (sz > 0 && file_reserve (f, (long) sz) == 0)
			{
				long got = 0;
				while (got < (long) sz)
				{
					int r = kapi_read (h, f->buf + got, sz - (unsigned) got);
					if (r <= 0)
						break;
					got += r;
				}
				f->size = got;
			}
			kapi_close (h);
		}
		else if (!(flags & O_CREAT))
		{
			f->used = 0;		// reading a missing file
			errno = ENOENT;
			return -1;
		}
	}

	f->used = 1;
	f->pos  = (flags & O_APPEND) ? f->size : 0;
	strncpy (f->path, name, sizeof f->path - 1);
	return fd;
}

off_t _lseek (int fd, off_t off, int whence)
{
	if (fd >= 0 && fd <= 2)
		return 0;			// console: not seekable, report 0

	TFile *f = fd_get (fd);
	if (!f)
	{
		errno = EBADF;
		return (off_t) -1;
	}
	long base = (whence == SEEK_CUR) ? f->pos
		  : (whence == SEEK_END) ? f->size
		  :                        0;	// SEEK_SET
	long np = base + (long) off;
	if (np < 0)
	{
		errno = EINVAL;
		return (off_t) -1;
	}
	f->pos = np;
	return (off_t) np;
}

int _close (int fd)
{
	TFile *f = fd_get (fd);
	if (!f)
	{
		if (fd >= 0 && fd <= 2)
			return 0;
		errno = EBADF;
		return -1;
	}
	int rc = 0;
	if (f->writable)			// flush the in-memory image to the SD card
		if (kapi_save_file (f->path, f->buf ? f->buf : (unsigned char *) "",
				    (unsigned) f->size) < 0)
			rc = -1;
	free (f->buf);
	memset (f, 0, sizeof *f);
	return rc;
}

int _fstat (int fd, struct stat *st)
{
	memset (st, 0, sizeof *st);
	if (fd >= 0 && fd <= 2)
	{
		st->st_mode = S_IFCHR;		// a tty: newlib keeps stdout line-buffered
		return 0;
	}
	TFile *f = fd_get (fd);
	if (!f)
	{
		errno = EBADF;
		return -1;
	}
	st->st_mode = S_IFREG;
	st->st_size = f->size;
	return 0;
}

int _isatty (int fd)
{
	return (fd >= 0 && fd <= 2) ? 1 : 0;
}

int _unlink (const char *name)
{
	if (kapi_remove (name) == 0)
		return 0;
	errno = ENOENT;
	return -1;
}

// pread/pwrite: POSIX positioned I/O. Our fds carry no per-call offset, so do it as
// save-offset / seek / read|write / restore-offset. NOT reentrant -- fine for Onyx's
// cooperative, single-threaded apps. Used by libnsutils (the NetSurf core libs).
ssize_t pread (int fd, void *buf, size_t n, off_t off)
{
	off_t cur = _lseek (fd, 0, SEEK_CUR);
	if (cur == (off_t) -1 || _lseek (fd, off, SEEK_SET) == (off_t) -1)
		return -1;
	int r = _read (fd, buf, n);
	_lseek (fd, cur, SEEK_SET);
	return r;
}

ssize_t pwrite (int fd, const void *buf, size_t n, off_t off)
{
	off_t cur = _lseek (fd, 0, SEEK_CUR);
	if (cur == (off_t) -1 || _lseek (fd, off, SEEK_SET) == (off_t) -1)
		return -1;
	int r = _write (fd, buf, n);
	_lseek (fd, cur, SEEK_SET);
	return r;
}

// ---- time -------------------------------------------------------------------

// Days from 1970-01-01 to y-m-d (proleptic Gregorian). Hinnant's algorithm.
static long days_from_civil (int y, int m, int d)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

int _gettimeofday (struct timeval *tv, void *tz)
{
	(void) tz;
	if (!tv)
		return 0;

	int y, mo, d, h, mi, s;
	if (kapi_get_datetime (&y, &mo, &d, &h, &mi, &s))
	{
		long days = days_from_civil (y, mo, d);
		tv->tv_sec  = ((days * 24 + h) * 60 + mi) * 60 + s;
		tv->tv_usec = 0;
	}
	else
	{
		unsigned ms = kapi_get_ticks ();	// monotonic, ms since boot
		tv->tv_sec  = ms / 1000;
		tv->tv_usec = (ms % 1000) * 1000;
	}
	return 0;
}

// ---- process ----------------------------------------------------------------

void _exit (int code)
{
	kapi_exit (code);
	for (;;)
		;				// kapi_exit does not return
}

int _kill (int pid, int sig)
{
	(void) pid;
	(void) sig;
	errno = EINVAL;
	return -1;
}

int _getpid (void)
{
	return 1;
}
