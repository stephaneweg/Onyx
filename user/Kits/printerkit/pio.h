//
// printerkit/pio.h -- the print system's files and clock, the same calls on Onyx (the kapi) and on a PC (the host
// tests: -DPRINT_HOST, stdio). A job is written and read as a stream: never whole in memory.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef ONYX_PRINT_PIO_H
#define ONYX_PRINT_PIO_H

#ifdef PRINT_HOST
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
typedef FILE *pio_file;					// a file being written
typedef FILE *pio_in;					// a file being read
#define PIO_NONE ((pio_file) 0)
static inline pio_file pio_create (const char *p)	{ return fopen (p, "wb"); }
static inline pio_in pio_open (const char *p)		{ return fopen (p, "rb"); }
static inline bool pio_write (pio_file f, const void *b, unsigned n)	{ return fwrite (b, 1, n, f) == n; }
static inline int pio_read (pio_in f, void *b, unsigned n)		{ return (int) fread (b, 1, n, f); }
static inline void pio_close (pio_file f)		{ if (f) fclose (f); }
static inline void pio_close_in (pio_in f)		{ if (f) fclose (f); }
static inline void pio_remove (const char *p)		{ remove (p); }
static inline void pio_mkdir (const char *p)		{ mkdir (p, 0777); }
static inline bool pio_rename (const char *a, const char *b)	{ return rename (a, b) == 0; }
static inline unsigned pio_ticks (void)			{ return (unsigned) (clock () * 1000 / CLOCKS_PER_SEC); }
static inline void pio_sleep (unsigned ms)		{ usleep (ms * 1000); }
#else
#include "appkit/appkit.h"
typedef long long pio_file;				// a file being written: a v75 handle (written as it comes)
typedef void *pio_in;					// a file being read: the kapi's first file calls (every kernel has them)
#define PIO_NONE ((pio_file) 0)
static inline pio_file pio_create (const char *p)
{
	long long h = kapi_file_open (p, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC, 0644);
	return h > 0 ? h : PIO_NONE;
}
static inline pio_in pio_open (const char *p)		{ return kapi_open (p); }
static inline bool pio_write (pio_file f, const void *b, unsigned n)	{ return kapi_file_write (f, b, n, -1) == (long long) n; }
static inline int pio_read (pio_in f, void *b, unsigned n)
{
	unsigned got = 0;				// (a short read is not the end: read until it is)
	while (got < n)
	{
		int r = kapi_read (f, (char *) b + got, n - got);
		if (r <= 0) break;
		got += (unsigned) r;
	}
	return (int) got;
}
static inline void pio_close (pio_file f)		{ if (f != PIO_NONE) kapi_handle_close (f); }
static inline void pio_close_in (pio_in f)		{ if (f) kapi_close (f); }
static inline void pio_remove (const char *p)		{ kapi_remove (p); }
static inline void pio_mkdir (const char *p)		{ kapi_mkdir (p); }
static inline bool pio_rename (const char *a, const char *b)	{ return kapi_rename (a, b) == 0; }
static inline unsigned pio_ticks (void)			{ return kapi_get_ticks (); }
static inline void pio_sleep (unsigned ms)		{ kapi_msleep (ms); }
#endif

// a whole small file (a list of printers, a job's ticket) -> new[] bytes with a 0 after them, 0: none
static inline char *pio_load (const char *path, unsigned *len)
{
	pio_in f = pio_open (path);
	if (!f) return 0;
	unsigned cap = 4096, n = 0;
	char *b = new char[cap + 1];
	for (;;)
	{
		int r = pio_read (f, b + n, cap - n);
		if (r <= 0) break;
		n += (unsigned) r;
		if (n == cap) { char *t = new char[cap * 2 + 1]; for (unsigned i = 0; i < n; i++) t[i] = b[i]; delete[] b; b = t; cap *= 2; }
	}
	pio_close_in (f);
	b[n] = 0;
	if (len) *len = n;
	return b;
}
static inline bool pio_save (const char *path, const void *b, unsigned n)
{
	pio_file f = pio_create (path);
	if (f == PIO_NONE) return false;
	bool ok = pio_write (f, b, n);
	pio_close (f);
	return ok;
}

#endif
