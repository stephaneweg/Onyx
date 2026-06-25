/*
 * sys/mman.h -- minimal memory-mapping shim for the NetSurf port on Onyx (newlib).
 * content/fetchers/file/file.c mmap()s a local file to serve file:// URLs. Onyx has no
 * VM mmap; onyx_compat.c implements a file mapping as malloc + pread (good enough for the
 * read-only MAP_PRIVATE use here). Part of the brick-9 platform layer.
 */
#ifndef _ONYX_COMPAT_SYS_MMAN_H
#define _ONYX_COMPAT_SYS_MMAN_H 1

#include <sys/types.h>

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_FAILED  ((void *) -1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int   munmap(void *addr, size_t length);

#endif /* _ONYX_COMPAT_SYS_MMAN_H */
