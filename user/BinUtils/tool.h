//
// tool.h -- the small runtime of the text tools of /bin (head, tail, sed, ed, sort, grep...):
// argv, a buffered stdout, files and stdin read by lines, memory, a few string helpers. A tool
// defines  int tool_main (int argc, char **argv)  and includes this header once; main is here.
//
// Two back ends behind the same calls: kapi (the card: freestanding, no libc) and, with
// -DTOOL_HOST, the PC's libc -- the tools' test (tools/tests/run_tools_test.sh) builds the very
// same sources on the PC.
//
// Conventions: "-" or no file = stdin; a line is returned without its '\n'; messages go to
// stdout (a console program has no other stream); the exit code is tool_main's.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
// hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
// do so, subject to the following conditions: The above copyright notice and this permission
// notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
// IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _bin_tool_h
#define _bin_tool_h

#ifdef TOOL_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#else
#include "appkit/appkit.h"
#include "umm.h"
#endif

int tool_main (int argc, char **argv);

// ---- strings ------------------------------------------------------------------

static inline int t_strlen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static inline int t_strcmp (const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return (unsigned char) *a - (unsigned char) *b;
}
static inline int t_streq (const char *a, const char *b) { return t_strcmp (a, b) == 0; }
static inline int t_lower (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static inline int t_isdigit (int c) { return c >= '0' && c <= '9'; }
static inline int t_isblank (int c) { return c == ' ' || c == '\t'; }
static inline int t_strcasecmp (const char *a, const char *b)
{
	while (*a && t_lower ((unsigned char) *a) == t_lower ((unsigned char) *b)) { a++; b++; }
	return t_lower ((unsigned char) *a) - t_lower ((unsigned char) *b);
}
static inline void t_memcpy (void *d, const void *s, long n)
{
	char *dd = (char *) d; const char *ss = (const char *) s;
	if (dd < ss) for (long i = 0; i < n; i++) dd[i] = ss[i];
	else for (long i = n - 1; i >= 0; i--) dd[i] = ss[i];		// (overlap: a memmove)
}
// a decimal number at *p (an optional sign) -> 1 and *v, *p after it; 0: none there
static inline int t_number (const char **p, long *v)
{
	const char *s = *p; int neg = 0; long r = 0;
	if (*s == '-' || *s == '+') { neg = *s == '-'; s++; }
	if (!t_isdigit (*s)) return 0;
	while (t_isdigit (*s)) r = r * 10 + (*s++ - '0');
	*v = neg ? -r : r; *p = s;
	return 1;
}
// the whole string as a number -> 1, else 0
static inline int t_atol (const char *s, long *v) { return t_number (&s, v) && *s == '\0'; }

// ---- stdout (buffered; captured into memory for a tool that rewrites a file) --------

static char  t__ob[4096];
static int   t__on;
static char *t__cap; static long t__capn, t__capcap; static int t__capon;

static inline void t__write (const char *b, int n)
{
#ifdef TOOL_HOST
	fwrite (b, 1, (size_t) n, stdout); fflush (stdout);
#else
	kapi_stdout_write (b, (unsigned) n);
#endif
}
static inline void t_flush (void) { if (t__on > 0) t__write (t__ob, t__on); t__on = 0; }

static inline void t_fatal (const char *msg);
static inline void *t_malloc (long n)
{
#ifdef TOOL_HOST
	void *p = malloc ((size_t) (n > 0 ? n : 1));
#else
	void *p = umm_malloc ((unsigned long) (n > 0 ? n : 1));
#endif
	if (p == 0) t_fatal ("out of memory");
	return p;
}
static inline void *t_realloc (void *o, long n)
{
#ifdef TOOL_HOST
	void *p = realloc (o, (size_t) (n > 0 ? n : 1));
#else
	void *p = umm_realloc (o, (unsigned long) (n > 0 ? n : 1));
#endif
	if (p == 0) t_fatal ("out of memory");
	return p;
}
static inline void t_free (void *p)
{
#ifdef TOOL_HOST
	free (p);
#else
	umm_free (p);
#endif
}
static inline char *t_strndup (const char *s, long n)
{
	char *d = (char *) t_malloc (n + 1);
	t_memcpy (d, s, n); d[n] = '\0';
	return d;
}
static inline char *t_strdup (const char *s) { return t_strndup (s, t_strlen (s)); }

static inline void t_put (const char *b, long n)
{
	if (t__capon)
	{
		if (t__capn + n + 1 > t__capcap)
		{
			t__capcap = (t__capn + n + 1) * 2 + 4096;
			t__cap = (char *) t_realloc (t__cap, t__capcap);
		}
		t_memcpy (t__cap + t__capn, b, n); t__capn += n;
		return;
	}
	for (long i = 0; i < n; i++)
	{
		if (t__on >= (int) sizeof t__ob) t_flush ();
		t__ob[t__on++] = b[i];
	}
}
static inline void t_putc (char c) { t_put (&c, 1); }
static inline void t_puts (const char *s) { t_put (s, t_strlen (s)); }
static inline void t_putnum (long long v)
{
	char t[24]; int n = 0;
	unsigned long long u = v < 0 ? 0ULL - (unsigned long long) v : (unsigned long long) v;
	if (u == 0) t[n++] = '0';
	while (u) { t[n++] = (char) ('0' + u % 10); u /= 10; }
	if (v < 0) t_putc ('-');
	while (n) t_putc (t[--n]);
}
// the number right-aligned in w columns
static inline void t_putnumw (long long v, int w)
{
	int d = v < 0 ? 2 : 1;
	for (long long x = v < 0 ? -v : v; x >= 10; x /= 10) d++;
	for (; d < w; d++) t_putc (' ');
	t_putnum (v);
}
// what follows is kept in memory instead of written ...
static inline void t_capture (void) { t_flush (); t__capon = 1; t__capn = 0; }
// ... until here: the bytes (the tool's to free) and their count
static inline char *t_captured (long *len)
{
	char *p = t__cap ? t__cap : (char *) t_malloc (1);
	*len = t__capn;
	t__cap = 0; t__capn = t__capcap = 0; t__capon = 0;
	return p;
}

static const char *t__name = "tool";
// "<tool>: a b\n" (b may be 0)
static inline void t_err (const char *a, const char *b)
{
	int cap = t__capon; t__capon = 0;
	t_puts (t__name); t_puts (": "); t_puts (a);
	if (b) { t_putc (' '); t_puts (b); }
	t_putc ('\n'); t_flush ();
	t__capon = cap;
}
static inline void t_exit (int code)
{
	t__capon = 0; t_flush ();
#ifdef TOOL_HOST
	exit (code);
#else
	kapi_exit (code);
	for (;;) { }
#endif
}
static inline void t_fatal (const char *msg) { t_err (msg, 0); t_exit (2); }

// ---- input: a file, or stdin ----------------------------------------------------

struct t_file
{
	int   is_stdin;
#ifdef TOOL_HOST
	FILE *fp;
#else
	void *h;
#endif
	int   n, pos, eof;
	char  buf[4096];
	char *line; long linecap;	// t_getline's buffer
	int   nl;			// the last line read ended with a '\n'
};

static inline struct t_file *t_open (const char *path)
{
	struct t_file *f = (struct t_file *) t_malloc ((long) sizeof *f);
	f->n = f->pos = f->eof = 0; f->line = 0; f->linecap = 0; f->nl = 0;
	f->is_stdin = path == 0 || t_streq (path, "-");
#ifdef TOOL_HOST
	f->fp = f->is_stdin ? stdin : fopen (path, "rb");
	if (f->fp == 0) { t_free (f); return 0; }
#else
	f->h = f->is_stdin ? 0 : kapi_open (path);
	if (!f->is_stdin && f->h == 0) { t_free (f); return 0; }
#endif
	return f;
}
static inline void t_close (struct t_file *f)
{
	if (f == 0) return;
#ifdef TOOL_HOST
	if (!f->is_stdin) fclose (f->fp);
#else
	if (!f->is_stdin) kapi_close (f->h);
#endif
	t_free (f->line); t_free (f);
}
static inline int t__fill (struct t_file *f)
{
	if (f->eof) return 0;
	if (f->is_stdin) t_flush ();			// (a prompt, before waiting for the keyboard)
#ifdef TOOL_HOST
	int n = (int) fread (f->buf, 1, f->is_stdin ? 1 : sizeof f->buf, f->fp);
#else
	int n = f->is_stdin ? kapi_stdin_read (f->buf, sizeof f->buf)
			    : kapi_read (f->h, f->buf, sizeof f->buf);
#endif
	if (n <= 0) { f->eof = 1; f->n = f->pos = 0; return 0; }
	f->n = n; f->pos = 0;
	return 1;
}
// the next byte, or -1 at the end
static inline int t_getc (struct t_file *f)
{
	if (f->pos >= f->n && !t__fill (f)) return -1;
	return (unsigned char) f->buf[f->pos++];
}
// up to n bytes -> how many (0 at the end)
static inline long t_read (struct t_file *f, char *b, long n)
{
	if (f->pos >= f->n && !t__fill (f)) return 0;
	long k = f->n - f->pos; if (k > n) k = n;
	t_memcpy (b, f->buf + f->pos, k); f->pos += (int) k;
	return k;
}
// the next line without its '\n' (and without a '\r' before it), valid until the next call;
// 0 at the end. *len: its length (may hold NUL bytes); f->nl: it ended with a '\n'.
static inline char *t_getline (struct t_file *f, long *len)
{
	long n = 0; int c, any = 0;
	f->nl = 0;
	while ((c = t_getc (f)) >= 0)
	{
		any = 1;
		if (c == '\n') { f->nl = 1; break; }
		if (n + 2 > f->linecap) { f->linecap = f->linecap * 2 + 256; f->line = (char *) t_realloc (f->line, f->linecap); }
		f->line[n++] = (char) c;
	}
	if (!any) return 0;
	if (f->line == 0) { f->linecap = 256; f->line = (char *) t_malloc (f->linecap); }
	if (f->nl && n > 0 && f->line[n - 1] == '\r') n--;
	f->line[n] = '\0';
	if (len) *len = n;
	return f->line;
}
// a whole file (or stdin) in memory, a '\0' after it -> the bytes (to free), or 0
static inline char *t_slurp (const char *path, long *len)
{
	struct t_file *f = t_open (path);
	if (f == 0) return 0;
	long cap = 8192, n = 0, k;
	char *b = (char *) t_malloc (cap);
	while ((k = t_read (f, b + n, cap - n - 1)) > 0)
	{
		n += k;
		if (cap - n < 2048) { cap *= 2; b = (char *) t_realloc (b, cap); }
	}
	t_close (f);
	b[n] = '\0'; *len = n;
	return b;
}
// a buffer cut into lines in place ('\n' -> '\0', a '\r' before it dropped) -> the lines (to
// free) and their count; *lastnl: the last one ended with a '\n'
static inline char **t_lines (char *b, long len, long *count, int *lastnl)
{
	long cap = 256, n = 0, i = 0;
	char **v = (char **) t_malloc (cap * (long) sizeof *v);
	if (lastnl) *lastnl = len == 0 || b[len - 1] == '\n';
	while (i < len)
	{
		if (n >= cap) { cap *= 2; v = (char **) t_realloc (v, cap * (long) sizeof *v); }
		v[n++] = b + i;
		while (i < len && b[i] != '\n') i++;
		if (i > 0 && b[i - 1] == '\r' && i < len) b[i - 1] = '\0';
		b[i++] = '\0';			// (the slurp's own '\0' when there is no '\n')
	}
	*count = n;
	return v;
}

// ---- output files ----------------------------------------------------------------

// the whole file written (created or replaced) -> 1, 0 on a failure
static inline int t_save (const char *path, const char *b, long n)
{
#ifdef TOOL_HOST
	FILE *fp = fopen (path, "wb");
	if (fp == 0) return 0;
	int ok = fwrite (b, 1, (size_t) n, fp) == (size_t) n;
	return fclose (fp) == 0 && ok;
#else
	return kapi_save_file (path, b, (unsigned) n) == (int) n;
#endif
}
// a file written piece by piece (append: after what it holds) -> its handle, or 0
static inline void *t_create (const char *path, int append)
{
#ifdef TOOL_HOST
	return fopen (path, append ? "ab" : "wb");
#else
	return kapi_file_out (path, append);
#endif
}
static inline void t_fwrite (void *h, const char *b, long n)
{
#ifdef TOOL_HOST
	fwrite (b, 1, (size_t) n, (FILE *) h); fflush ((FILE *) h);
#else
	kapi_stream_write (h, b, (unsigned) n);
#endif
}
static inline void t_fclose (void *h)
{
#ifdef TOOL_HOST
	fclose ((FILE *) h);
#else
	kapi_stream_close (h);
#endif
}

// ---- the file system ---------------------------------------------------------------

// -> 0 nothing there, 1 a file, 2 a directory; *size: a file's bytes (may be 0)
static inline int t_stat (const char *path, long long *size)
{
#ifdef TOOL_HOST
	struct stat st;
	if (stat (path, &st) != 0) return 0;
	if (size) *size = (long long) st.st_size;
	return S_ISDIR (st.st_mode) ? 2 : 1;
#else
	struct kapi_stat st;
	int r = kapi_path_stat (path, &st);
	if (r == 0)
	{
		if (size) *size = (long long) st.size;
		return (st.mode & KAPI_S_IFDIR) ? 2 : 1;
	}
	if (r != -KAPI_ENOSYS) return 0;
	void *d = kapi_opendir (path);				// (a kernel before v75)
	if (d) { kapi_closedir (d); if (size) *size = 0; return 2; }
	void *f = kapi_open (path);
	if (f == 0) return 0;
	if (size) *size = kapi_fsize (f);
	kapi_close (f);
	return 1;
#endif
}

struct t_dirent { char name[256]; int is_dir; long long size; };

static inline void *t_opendir (const char *path)
{
#ifdef TOOL_HOST
	return opendir (path);
#else
	return kapi_opendir (path);
#endif
}
// the next entry ("." and ".." skipped) -> 1, 0 at the end; dir: the folder's path (the PC's
// stat needs it)
static inline int t_readdir (void *d, const char *dir, struct t_dirent *e)
{
#ifdef TOOL_HOST
	struct dirent *de;
	while ((de = readdir ((DIR *) d)) != 0)
	{
		if (t_streq (de->d_name, ".") || t_streq (de->d_name, "..")) continue;
		char p[1024]; struct stat st;
		snprintf (p, sizeof p, "%s/%s", dir, de->d_name);
		snprintf (e->name, sizeof e->name, "%s", de->d_name);
		e->is_dir = 0; e->size = 0;
		if (stat (p, &st) == 0) { e->is_dir = S_ISDIR (st.st_mode); e->size = (long long) st.st_size; }
		return 1;
	}
	return 0;
#else
	(void) dir;
	struct kapi_dirent2 e2;
	for (;;)
	{
		int r = kapi_dir_read (d, &e2);
		if (r == -KAPI_ENOSYS)				// (a kernel before v75: names cut at 127)
		{
			struct kapi_dirent e1;
			if (!kapi_readdir (d, &e1)) return 0;
			int k = 0; for (; e1.name[k]; k++) e->name[k] = e1.name[k];
			e->name[k] = '\0'; e->is_dir = e1.is_dir; e->size = e1.size;
		}
		else
		{
			if (r <= 0) return 0;
			int k = 0; for (; e2.name[k] && k < 255; k++) e->name[k] = e2.name[k];
			e->name[k] = '\0'; e->is_dir = (e2.mode & KAPI_S_IFDIR) != 0; e->size = (long long) e2.size;
		}
		if (t_streq (e->name, ".") || t_streq (e->name, "..")) continue;
		return 1;
	}
#endif
}
static inline void t_closedir (void *d)
{
#ifdef TOOL_HOST
	closedir ((DIR *) d);
#else
	kapi_closedir (d);
#endif
}

// ---- time ----------------------------------------------------------------------------

static inline void t_sleep_ms (unsigned ms)
{
	t_flush ();
#ifdef TOOL_HOST
	usleep (ms * 1000);
#else
	kapi_msleep (ms);
#endif
}
// the local date and time: v[0..5] = year, month, day, hour, minute, second
static inline void t_now (int v[6])
{
#ifdef TOOL_HOST
	time_t t = time (0); struct tm *m = localtime (&t);
	v[0] = m->tm_year + 1900; v[1] = m->tm_mon + 1; v[2] = m->tm_mday;
	v[3] = m->tm_hour; v[4] = m->tm_min; v[5] = m->tm_sec;
#else
	kapi_get_datetime (&v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
#endif
}

// ---- main ----------------------------------------------------------------------------

// a path's last component
static inline const char *t_basename (const char *p)
{
	const char *b = p;
	for (; *p; p++) if (*p == '/' || *p == ':' || *p == '\\') b = p + 1;
	return b;
}

#ifdef TOOL_HOST
int main (int argc, char **argv)
{
	t__name = t_basename (TOOL_NAME);
	int r = tool_main (argc, argv);
	t_exit (r);
	return r;
}
#else
int main (void)
{
	static char blk[4096 + 512];
	static char *argv[512];
	int argc = 0;
	int n = kapi_get_argv (blk, sizeof blk - 2);
	if (n > 0)						// "path\0arg1\0...\0\0"
	{
		if (n > (int) sizeof blk - 2) n = (int) sizeof blk - 2;
		blk[n] = blk[n + 1] = '\0';
		for (char *p = blk; *p && argc < 511; p += t_strlen (p) + 1) argv[argc++] = p;
	}
	else							// (a kernel before v75: the words of the line)
	{
		static char name[] = TOOL_NAME;
		argv[argc++] = name;
		kapi_get_args (blk, sizeof blk - 1);
		char *p = blk;
		while (*p && argc < 511)
		{
			while (t_isblank (*p)) p++;
			if (*p == '\0') break;
			if (*p == '"') { argv[argc++] = ++p; while (*p && *p != '"') p++; }
			else { argv[argc++] = p; while (*p && !t_isblank (*p)) p++; }
			if (*p) *p++ = '\0';
		}
	}
	if (argc == 0) { static char name[] = TOOL_NAME; argv[argc++] = name; }
	argv[argc] = 0;
	t__name = TOOL_NAME;
	int r = tool_main (argc, argv);
	t_exit (r);
	return r;
}
#endif

#endif
