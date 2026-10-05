/*
 * onyx_mucompat.c -- what MuPDF needs and newlib lacks (the Onyx build, mupdf.mk): timegm; fitz's directory
 * "archives" (source/fitz/directory.c, left out: newlib has no <dirent.h>, and a PDF never needs them).
 */
#include "mupdf/fitz.h"
#include <time.h>

time_t timegm (struct tm *tm)
{
	static const int before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	long y = tm->tm_year + 1900, m = tm->tm_mon;
	y += m / 12; m %= 12; if (m < 0) { m += 12; y--; }
	long days = (y - 1970) * 365 + ((y - 1969) / 4) - ((y - 1901) / 100) + ((y - 1601) / 400) + before[m] + tm->tm_mday - 1;
	if (m > 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) days++;
	return (time_t) (((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec);
}

int fz_is_directory (fz_context *ctx, const char *path) { (void) ctx; (void) path; return 0; }

fz_archive *fz_open_directory (fz_context *ctx, const char *path)
{
	fz_throw (ctx, FZ_ERROR_UNSUPPORTED, "'%s': folders are not archives on Onyx", path);
}

/* newlib's system calls that Onyx's libc layer (user/Runtime/libc/onyx_syscalls.c) leaves out and MuPDF reaches: a file
 * cut short (its writers: never used here), a file's facts (fitz asks for a time: none), random bytes (a new
 * document's id: the ticks stirred). */
#include <sys/stat.h>
#include <errno.h>
#include "appkit/appkit.h"
int ftruncate (int fd, off_t len) { (void) fd; (void) len; errno = ENOSYS; return -1; }
int _stat (const char *path, struct stat *st) { (void) path; (void) st; errno = ENOSYS; return -1; }
int _getentropy (void *buf, size_t n)
{
	static unsigned long long s;
	if (!s) s = 0x9E3779B97F4A7C15ULL ^ kapi_get_ticks ();
	unsigned char *b = (unsigned char *) buf;
	for (size_t i = 0; i < n; i++) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; s += kapi_get_ticks (); b[i] = (unsigned char) (s >> 24); }
	return 0;
}
