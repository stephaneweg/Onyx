/*
 * sys/uio.h -- vectored I/O on Onyx (libonyxposix: readv / writev loop over read / write).
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
#ifndef _SYS_UIO_H
#define _SYS_UIO_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iovec
{
	void *iov_base;
	size_t iov_len;
};

#define IOV_MAX		1024
#define UIO_MAXIOV	IOV_MAX

ssize_t readv (int, const struct iovec *, int);
ssize_t writev (int, const struct iovec *, int);
ssize_t preadv (int, const struct iovec *, int, off_t);
ssize_t pwritev (int, const struct iovec *, int, off_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_UIO_H */
