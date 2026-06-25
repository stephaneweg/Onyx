/*
 * onyx_compat.c -- POSIX bits NetSurf expects that newlib bare-metal lacks (Onyx).
 *
 * Implements the prototypes from the compat/ shim headers. First bring-up: directory
 * enumeration is empty (no kapi dir-list yet) and uname() reports fixed Onyx values.
 * Part of the brick-9 platform layer; link this into the NetSurf app.
 */
#include <stddef.h>
#include <string.h>

#include <dirent.h>
#include <sys/utsname.h>

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
