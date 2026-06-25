/*
 * dirent.h -- minimal <dirent.h> shim for the NetSurf port on Onyx (newlib).
 *
 * newlib (aarch64-none-elf) ships a <sys/dirent.h> that is just  #error "<dirent.h> not
 * supported"  -- there is no directory-enumeration syscall layer. NetSurf's utils
 * (file.c / filepath.c / utils.c) use opendir/readdir/closedir for file:// directory
 * listings. This provides the types + prototypes; onyx_compat.c implements them as empty
 * (opendir returns NULL) for a first bring-up -- file:// to a FILE still works via fopen,
 * directory INDEX pages just come back empty. Wire to a real kapi dir-list later.
 *
 * Part of the brick-9 platform layer; provided via -I so the libraries stay unpatched.
 */
#ifndef _ONYX_COMPAT_DIRENT_H
#define _ONYX_COMPAT_DIRENT_H 1

#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
	ino_t         d_ino;
	unsigned char d_type;
	char          d_name[256];
};

typedef struct __onyx_dir DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);

#endif /* _ONYX_COMPAT_DIRENT_H */
