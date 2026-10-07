/*
 * mman.c -- mmap / munmap / mprotect / madvise (libonyxposix, docs/POSIX-PLAN.md §3.4), on the
 * kernel's v75 vm_* calls (WP-MEM: lazy, zero-filled regions in the mmap arena, 34..60 GB).
 *
 *  - anonymous: vm_map (MAP_SHARED anonymous is private: there is no fork); PROT_NONE
 *    reservations, MAP_FIXED / MAP_FIXED_NOREPLACE inside the arena, MAP_NORESERVE, MAP_POPULATE;
 *  - a file, MAP_PRIVATE (or MAP_SHARED read-only): an anonymous read-write mapping filled with
 *    pread, then protected as asked (an eager copy; later writes to the file are not seen);
 *    MAP_SHARED with PROT_WRITE on a file -> ENOTSUP;
 *  - (v76) a shared memory object (memfd_create, shm_open: ipc.c): MAP_SHARED -> shm_map (the
 *    object's own pages, seen by every process mapping it); MAP_PRIVATE -> an anonymous copy;
 *  - PROT_EXEC (a JIT): what the kernel says -- v78 gives it to anonymous memory (and to a
 *    file's private copy), refuses it for shared objects; an older kernel: ENOTSUP;
 *  - munmap (partial: the kernel splits), mprotect, madvise (DONTNEED / FREE: zero at the next
 *    touch), mincore (from vm_query), msync / mlock: no-ops.
 * On a kernel without vm_map (ENOSYS) an anonymous mapping is 64 KB-aligned heap memory
 * (memalign, zeroed): no reservations of gigabytes, no MAP_FIXED, protections not enforced,
 * munmap frees only a whole mapping.
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
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include "posix_internal.h"

static inline unsigned long page_up (unsigned long n) { return (n + ONYX_PAGE - 1) & ~(ONYX_PAGE - 1); }

/* ---- the fallback (no vm_map): heap blocks ---- */
struct heapmap
{
	struct heapmap *next;
	unsigned long start, len;
};
static struct heapmap *s_heapmaps;
static volatile unsigned s_heapLock;
static int s_novm;				/* 1: the kernel said ENOSYS */

int __onyx_vm_available (void)
{
	if (s_novm)
		return 0;
	struct kapi_vm_region r;
	if (kapi_vm_query (0, &r) == -KAPI_ENOSYS)
		s_novm = 1;
	return !s_novm;
}

static void *heap_map (unsigned long len)
{
	struct heapmap *m = (struct heapmap *) malloc (sizeof *m);
	void *p = memalign (ONYX_PAGE, len);
	if (m == 0 || p == 0)
	{
		free (m);
		free (p);
		errno = ENOMEM;
		return MAP_FAILED;
	}
	memset (p, 0, len);
	m->start = (unsigned long) p;
	m->len = len;
	__onyx_lock (&s_heapLock);
	m->next = s_heapmaps;
	s_heapmaps = m;
	__onyx_unlock (&s_heapLock);
	return p;
}

static struct heapmap *heap_find (unsigned long a, int unlink)
{
	__onyx_lock (&s_heapLock);
	for (struct heapmap **pp = &s_heapmaps; *pp; pp = &(*pp)->next)
	{
		struct heapmap *m = *pp;
		if (a >= m->start && a < m->start + m->len)
		{
			if (unlink && a == m->start)
				*pp = m->next;
			__onyx_unlock (&s_heapLock);
			return m;
		}
	}
	__onyx_unlock (&s_heapLock);
	return 0;
}

static void *anon_map (void *addr, unsigned long len, int prot, int flags)
{
	unsigned kf = 0;
	if (flags & MAP_FIXED) kf |= KAPI_MAP_FIXED;
	if (flags & MAP_FIXED_NOREPLACE) kf |= KAPI_MAP_FIXED_NOREPLACE;
	if (flags & MAP_NORESERVE) kf |= KAPI_MAP_NORESERVE;
	if (flags & MAP_POPULATE) kf |= KAPI_MAP_POPULATE;
	if (!s_novm)
	{
		long long r = kapi_vm_map ((unsigned long long) (unsigned long) addr, len, (unsigned) prot, kf);
		if (r != -KAPI_ENOSYS)
		{
			if (r < 0)
			{
				errno = (int) -r;
				return MAP_FAILED;
			}
			return (void *) (unsigned long) r;
		}
		s_novm = 1;
	}
	if (prot & PROT_EXEC)				/* (heap memory never runs) */
	{
		errno = ENOTSUP;
		return MAP_FAILED;
	}
	if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))
	{
		errno = ENOMEM;
		return MAP_FAILED;
	}
	if (len > (256UL << 20))		/* (a reservation: the heap cannot hold it) */
	{
		errno = ENOMEM;
		return MAP_FAILED;
	}
	return heap_map (len);
}

/* (v76) a shared memory object's mapping: MAP_SHARED in place, MAP_PRIVATE a copy */
static void *shm_mmap (struct __onyx_ofd *d, void *addr, size_t len, unsigned long n, int prot, int flags, off_t off)
{
	unsigned kf = 0;
	if (flags & MAP_FIXED) kf |= KAPI_MAP_FIXED;
	if (flags & MAP_FIXED_NOREPLACE) kf |= KAPI_MAP_FIXED_NOREPLACE;
	if (flags & MAP_POPULATE) kf |= KAPI_MAP_POPULATE;
	if ((flags & MAP_TYPE) == MAP_SHARED)
	{
		if ((prot & PROT_WRITE) && (d->flags & O_ACCMODE) != O_RDWR)
		{
			errno = EACCES;
			return MAP_FAILED;
		}
		long long r = kapi_shm_map (d->h, (unsigned long long) (unsigned long) addr, n, (unsigned) prot, kf, (unsigned long long) off);
		if (r < 0)
		{
			errno = (int) -r;
			return MAP_FAILED;
		}
		return (void *) (unsigned long) r;
	}
	void *p = anon_map (addr, n, PROT_READ | PROT_WRITE, flags & ~MAP_NORESERVE);
	if (p == MAP_FAILED)
		return p;
	long long size = kapi_shm_ctl (d->h, KAPI_SHM_GET_SIZE, 0);
	long long src = size > (long long) off ? kapi_shm_map (d->h, 0, n, PROT_READ, 0, (unsigned long long) off) : -1;
	if (src > 0)
	{
		unsigned long long have = (unsigned long long) (size - off);
		memcpy (p, (void *) (unsigned long) src, have < len ? (size_t) have : len);
		kapi_vm_unmap ((unsigned long long) src, n);
	}
	if ((prot & (PROT_READ | PROT_WRITE)) != (PROT_READ | PROT_WRITE))
		kapi_vm_protect ((unsigned long) p, n, (unsigned) prot);
	return p;
}

void *mmap (void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	if (len == 0 || (off & (ONYX_PAGE - 1)) != 0 || off < 0)
	{
		errno = EINVAL;
		return MAP_FAILED;
	}
	/* PROT_EXEC: the kernel's answer (v78: anonymous memory only -- a JIT; before: ENOTSUP) */
	if (kapi__core () != 0)
	{
		errno = ENOSYS;
		return MAP_FAILED;
	}
	unsigned long n = page_up (len);
	int type = flags & MAP_TYPE;
	if (type != MAP_SHARED && type != MAP_PRIVATE)
	{
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (flags & MAP_ANONYMOUS)
		return anon_map (addr, n, prot, flags);

	/* a file */
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return MAP_FAILED;
	void *p = MAP_FAILED;
	if (d->type == ONYX_FD_SHM)			/* (v76) a shared memory object */
	{
		p = shm_mmap (d, addr, len, n, prot, flags, off);
		__onyx_fd_put (d);
		return p;
	}
	if (type == MAP_SHARED && (prot & PROT_WRITE))
		errno = ENOTSUP;
	else if (d->type != ONYX_FD_FILE && d->type != ONYX_FD_LFILE)
		errno = ENODEV;
	else if ((d->flags & O_ACCMODE) == O_WRONLY)
		errno = EACCES;
	else
	{
		p = anon_map (addr, n, PROT_READ | PROT_WRITE, flags & ~MAP_NORESERVE);
		if (p != MAP_FAILED)
		{
			size_t got = 0;
			while (got < len)
			{
				ssize_t r = __onyx_pread (d, (char *) p + got, len - got, off + (off_t) got);
				if (r <= 0)
					break;
				got += (size_t) r;
			}
			if ((prot & (PROT_READ | PROT_WRITE)) != (PROT_READ | PROT_WRITE) && !s_novm)
				kapi_vm_protect ((unsigned long) p, n, (unsigned) prot);
		}
	}
	__onyx_fd_put (d);
	return p;
}

int munmap (void *addr, size_t len)
{
	unsigned long a = (unsigned long) addr;
	if ((a & (ONYX_PAGE - 1)) != 0 || len == 0)
		return ONYX_ERR (EINVAL);
	if (!s_novm)
	{
		int r = kapi_vm_unmap (a, page_up (len));
		if (r != -KAPI_ENOSYS)
			return r < 0 ? ONYX_ERR (-r) : 0;
		s_novm = 1;
	}
	struct heapmap *m = heap_find (a, 1);
	if (m && m->start == a)
	{
		free ((void *) m->start);
		free (m);
	}
	return 0;					/* (a part of a mapping: kept) */
}

int mprotect (void *addr, size_t len, int prot)
{
	unsigned long a = (unsigned long) addr;
	if ((a & (ONYX_PAGE - 1)) != 0)
		return ONYX_ERR (EINVAL);
	if ((prot & PROT_EXEC) && s_novm)
		return ONYX_ERR (ENOTSUP);
	if (!s_novm)
	{
		int r = kapi_vm_protect (a, page_up (len), (unsigned) prot);
		if (r != -KAPI_ENOSYS)
			return r < 0 ? ONYX_ERR (-r) : 0;
		s_novm = 1;
	}
	return 0;					/* (not enforced without the kernel) */
}

int madvise (void *addr, size_t len, int advice)
{
	unsigned long a = (unsigned long) addr;
	if ((a & (ONYX_PAGE - 1)) != 0)
		return ONYX_ERR (EINVAL);
	if (advice == MADV_DONTDUMP || advice == MADV_DODUMP)
		return 0;
	if (!s_novm)
	{
		int r = kapi_vm_advise (a, page_up (len), advice);
		if (r != -KAPI_ENOSYS)
			return r < 0 ? ONYX_ERR (-r) : 0;
		s_novm = 1;
	}
	if (advice == MADV_DONTNEED && heap_find (a, 0))
		memset (addr, 0, len);
	return 0;
}

int posix_madvise (void *addr, size_t len, int advice)
{
	return madvise (addr, len, advice) == 0 ? 0 : errno;
}

int mincore (void *addr, size_t len, unsigned char *vec)
{
	unsigned long a = (unsigned long) addr;
	if ((a & (ONYX_PAGE - 1)) != 0)
		return ONYX_ERR (EINVAL);
	unsigned long pages = page_up (len) / ONYX_PAGE;
	struct kapi_vm_region r;
	int q = kapi_vm_query (a, &r);
	if (q == 0 || q == -KAPI_ENOSYS)
	{
		/* (the kernel counts a region's pages, not which: "resident" when any is) */
		unsigned char v = q == -KAPI_ENOSYS || r.resident > 0 || !(r.flags & KAPI_VMF_LAZY);
		memset (vec, v, pages);
		return 0;
	}
	return ONYX_ERR (ENOMEM);
}

int msync (void *addr, size_t len, int flags) { (void) addr; (void) len; (void) flags; return 0; }
int mlock (const void *addr, size_t len) { (void) addr; (void) len; return 0; }
int munlock (const void *addr, size_t len) { (void) addr; (void) len; return 0; }
int mlockall (int flags) { (void) flags; return 0; }
int munlockall (void) { return 0; }

int getpagesize (void) { return (int) ONYX_PAGE; }

/* newlib has memalign, aligned_alloc and malloc_usable_size; POSIX's own: */
int posix_memalign (void **out, size_t align, size_t size)
{
	if (align < sizeof (void *) || (align & (align - 1)) != 0)
		return EINVAL;
	void *p = memalign (align, size ? size : 1);
	if (p == 0)
		return ENOMEM;
	*out = p;
	return 0;
}
