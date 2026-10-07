/*
 * sys/mman.h -- memory mappings on Onyx (libonyxposix mman.c, over the kapi v75 vm_* calls).
 *
 * Anonymous mappings (private; MAP_SHARED anonymous is private too: there is no fork), PROT_NONE
 * reservations committed later with mprotect, MAP_FIXED inside the mmap arena, madvise
 * (DONTNEED: zero-filled at the next touch). A MAP_PRIVATE file is read into an anonymous mapping
 * (an eager copy); MAP_SHARED with PROT_WRITE on a file is ENOTSUP. PROT_EXEC on anonymous memory from kernel v78 (a JIT).
 * The page size is 64 KB (getpagesize, sysconf (_SC_PAGESIZE)).
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
#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROT_NONE		0
#define PROT_READ		1
#define PROT_WRITE		2
#define PROT_EXEC		4

#define MAP_SHARED		0x01
#define MAP_PRIVATE		0x02
#define MAP_FILE		0	/* (BSD: a file mapping, the default) */
#define MAP_TYPE		0x0F
#define MAP_FIXED		0x10
#define MAP_ANONYMOUS		0x20
#define MAP_ANON		MAP_ANONYMOUS
#define MAP_NORESERVE		0x4000
#define MAP_POPULATE		0x8000
#define MAP_STACK		0x20000
#define MAP_FIXED_NOREPLACE	0x100000
#define MAP_FAILED		((void *) -1)

#define MADV_NORMAL		0
#define MADV_RANDOM		1
#define MADV_SEQUENTIAL		2
#define MADV_WILLNEED		3
#define MADV_DONTNEED		4
#define MADV_FREE		8
#define MADV_DONTDUMP		16
#define MADV_DODUMP		17
#define POSIX_MADV_NORMAL	MADV_NORMAL
#define POSIX_MADV_RANDOM	MADV_RANDOM
#define POSIX_MADV_SEQUENTIAL	MADV_SEQUENTIAL
#define POSIX_MADV_WILLNEED	MADV_WILLNEED
#define POSIX_MADV_DONTNEED	MADV_DONTNEED

#define MS_ASYNC		1
#define MS_INVALIDATE		2
#define MS_SYNC			4

#define MCL_CURRENT		1
#define MCL_FUTURE		2

void *mmap (void *, size_t, int, int, int, off_t);
int munmap (void *, size_t);
int mprotect (void *, size_t, int);
int madvise (void *, size_t, int);
int posix_madvise (void *, size_t, int);
int msync (void *, size_t, int);
int mlock (const void *, size_t);
int munlock (const void *, size_t);
int mlockall (int);
int munlockall (void);
int mincore (void *, size_t, unsigned char *);
int shm_open (const char *, int, mode_t);
int shm_unlink (const char *);

/* (v76) an anonymous shared memory object as a descriptor (ftruncate, mmap MAP_SHARED, passed with
 * SCM_RIGHTS or posix_spawn; seals with fcntl F_ADD_SEALS) */
#define MFD_CLOEXEC		0x0001U
#define MFD_ALLOW_SEALING	0x0002U
int memfd_create (const char *, unsigned int);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MMAN_H */
