/*
 * dirent.h -- POSIX directory listing for the SuperTuxKart port (newlib's <dirent.h> is "not
 * supported" on this target). On the kapi's FatFs listing (kapi_opendir / kapi_readdir):
 * onyx_posix.c. d_type is DT_DIR or DT_REG; no "." / ".." entries.
 */
#ifndef _ONYX_DIRENT_H
#define _ONYX_DIRENT_H
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DT_UNKNOWN	0
#define DT_DIR		4
#define DT_REG		8

struct dirent
{
	ino_t		d_ino;
	unsigned char	d_type;
	char		d_name[256];
};

typedef struct __onyx_dir DIR;

DIR *opendir (const char *path);
struct dirent *readdir (DIR *d);
int closedir (DIR *d);
void rewinddir (DIR *d);
long telldir (DIR *d);
void seekdir (DIR *d, long pos);

#ifdef __cplusplus
}
#endif
#endif
