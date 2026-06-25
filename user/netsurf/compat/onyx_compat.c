/*
 * onyx_compat.c -- POSIX bits NetSurf expects that newlib bare-metal lacks (Onyx).
 *
 * Implements the prototypes from the compat/ shim headers. First bring-up: directory
 * enumeration is empty (no kapi dir-list yet) and uname() reports fixed Onyx values.
 * Part of the brick-9 platform layer; link this into the NetSurf app.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <unistd.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <regex.h>
#include <iconv.h>

#include "kapi.h"

/* ---- directory enumeration (stub: always empty) ---------------------- */
struct __onyx_dir { int dummy; };

DIR *opendir(const char *name)
{
	(void)name;
	return NULL;		/* no directory listing on Onyx yet */
}

struct dirent *readdir(DIR *dirp)
{
	(void)dirp;
	return NULL;		/* end of (empty) directory */
}

int closedir(DIR *dirp)
{
	(void)dirp;
	return 0;
}

void rewinddir(DIR *dirp)
{
	(void)dirp;
}

/* ---- uname ----------------------------------------------------------- */
int uname(struct utsname *buf)
{
	if (buf == NULL)
		return -1;
	strcpy(buf->sysname, "Onyx");
	strcpy(buf->nodename, "onyx");
	strcpy(buf->release, "1.0");
	strcpy(buf->version, "Onyx");
	strcpy(buf->machine, "aarch64");
	return 0;
}

/* ---- file-system bits newlib lacks ----------------------------------- */
/* stat()/link() bottom out in these newlib syscalls. We back stat() with the kapi: a
 * file that opens is a regular file of kapi_fsize() bytes; everything else is ENOENT. */
int _stat(const char *path, struct stat *st)
{
	void *h = kapi_open(path);
	if (h == NULL) { errno = ENOENT; return -1; }
	memset(st, 0, sizeof *st);
	st->st_mode = S_IFREG;
	st->st_size = (off_t) kapi_fsize(h);
	kapi_close(h);
	return 0;
}

int _link(const char *a, const char *b) { (void)a; (void)b; errno = EPERM; return -1; }
int rmdir(const char *path)              { (void)path; return 0; }
int unlinkat(int fd, const char *path, int flag) { (void)fd; (void)flag; return unlink(path); }
int socket(int d, int t, int p)          { (void)d; (void)t; (void)p; errno = EAFNOSUPPORT; return -1; }

/* scandir: no directory enumeration on Onyx yet -> report an empty directory. */
int scandir(const char *dir, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **))
{
	(void) dir; (void) filter; (void) compar;
	if (namelist) *namelist = NULL;
	return 0;
}

int access(const char *path, int mode)
{
	void *h;
	(void) mode;
	h = kapi_open(path);
	if (h == NULL) { errno = ENOENT; return -1; }
	kapi_close(h);
	return 0;
}

int mkdir(const char *path, mode_t mode) { (void)path; (void)mode; return 0; }	/* pretend ok */
int ftruncate(int fd, off_t length)      { (void)fd; (void)length; return 0; }
int dirfd(DIR *dirp)                      { (void)dirp; return -1; }
int fstatat(int fd, const char *path, struct stat *st, int flag)
{ (void)fd; (void)flag; return _stat(path, st); }

char *realpath(const char *path, char *resolved)
{
	if (path == NULL) return NULL;
	if (resolved == NULL) { resolved = (char *) malloc(strlen(path) + 1); if (!resolved) return NULL; }
	strcpy(resolved, path);		/* no symlinks on Onyx: identity */
	return resolved;
}

/* ---- inet address parsing (declared in compat/arpa/inet.h) ----------- */
int inet_aton(const char *cp, struct in_addr *inp)
{
	unsigned long parts[4] = {0,0,0,0};
	int i = 0;
	const char *p = cp;
	for (i = 0; i < 4; i++) {
		unsigned long v = 0; int any = 0;
		while (*p >= '0' && *p <= '9') { v = v*10 + (unsigned)(*p-'0'); p++; any = 1; }
		if (!any || v > 255) return 0;
		parts[i] = v;
		if (i < 3) { if (*p != '.') return 0; p++; }
	}
	if (*p != '\0') return 0;
	if (inp) inp->s_addr = htonl((uint32_t)((parts[0]<<24)|(parts[1]<<16)|(parts[2]<<8)|parts[3]));
	return 1;
}

int inet_pton(int af, const char *src, void *dst)
{
	if (af != AF_INET) return -1;
	return inet_aton(src, (struct in_addr *) dst) ? 1 : 0;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
	const unsigned char *b = (const unsigned char *) src;
	uint32_t a;
	if (af != AF_INET || dst == NULL) return NULL;
	a = ntohl(((const struct in_addr *) src)->s_addr); (void)b;
	if ((int)size < 16) return NULL;
	{
		unsigned o = 0; int i; uint32_t v = a;
		unsigned char q[4] = { (unsigned char)(v>>24), (unsigned char)(v>>16),
				       (unsigned char)(v>>8), (unsigned char)v };
		for (i = 0; i < 4; i++) {
			unsigned n = q[i], d = 100; int started = 0;
			if (i) dst[o++] = '.';
			while (d) { unsigned dig = (n/d)%10; if (dig || started || d==1) { dst[o++]=(char)('0'+dig); started=1; } d/=10; }
		}
		dst[o] = '\0';
	}
	return dst;
}

/* ---- iconv (passthrough) --------------------------------------------- */
/* A real charset converter is out of scope for the first bring-up; this passes bytes
 * through unchanged, which is correct for the UTF-8 / ASCII path that dominates the web.
 * Non-UTF-8 input is copied verbatim (may mojibake) rather than failing the fetch. */
iconv_t iconv_open(const char *to, const char *from) { (void)to; (void)from; return (iconv_t) 1; }
int     iconv_close(iconv_t cd) { (void)cd; return 0; }
size_t  iconv(iconv_t cd, char **inbuf, size_t *inleft, char **outbuf, size_t *outleft)
{
	(void) cd;
	if (inbuf == NULL || *inbuf == NULL) return 0;		/* reset/flush */
	while (*inleft > 0 && *outleft > 0) {
		**outbuf = **inbuf;
		(*inbuf)++; (*outbuf)++; (*inleft)--; (*outleft)--;
	}
	if (*inleft > 0) { errno = E2BIG; return (size_t) -1; }
	return 0;
}

/* ---- mmap (file mapping via malloc + pread) -------------------------- */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	void *p;
	(void) addr; (void) prot; (void) flags;
	if (length == 0) return MAP_FAILED;
	p = malloc(length);
	if (p == NULL) return MAP_FAILED;
	if (pread(fd, p, length, offset) < 0) { free(p); return MAP_FAILED; }
	return p;
}

int munmap(void *addr, size_t length) { (void) length; free(addr); return 0; }

/* ---- POSIX regex (stub: unavailable) --------------------------------- */
/* NetSurf uses regex in save_complete (rewriting saved pages) and a couple of utils;
 * none are needed to fetch + render. Report "no support" so callers degrade gracefully. */
int regcomp(regex_t *preg, const char *regex, int cflags)
{ (void)preg; (void)regex; (void)cflags; return REG_BADPAT; }
int regexec(const regex_t *preg, const char *str, size_t n, regmatch_t pmatch[], int eflags)
{ (void)preg; (void)str; (void)n; (void)pmatch; (void)eflags; return REG_NOMATCH; }
size_t regerror(int code, const regex_t *preg, char *errbuf, size_t errbuf_size)
{ (void)code; (void)preg; if (errbuf && errbuf_size) errbuf[0] = '\0'; return 0; }
void regfree(regex_t *preg) { (void)preg; }
