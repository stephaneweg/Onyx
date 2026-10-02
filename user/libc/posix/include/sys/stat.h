/*
 * sys/stat.h -- newlib's, plus what it keeps for Cygwin / RTEMS and libonyxposix provides: lstat
 * (= stat: there are no links), mknod, UTIME_NOW / UTIME_OMIT for utimensat / futimens.
 * The struct stat layout is newlib's (st_ino and st_dev are 16-bit: see stat.c).
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
#ifndef _ONYX_SYS_STAT_H
#define _ONYX_SYS_STAT_H

#include_next <sys/stat.h>

#ifndef UTIME_NOW
#define UTIME_NOW	-2L
#define UTIME_OMIT	-1L
#endif

#ifdef __cplusplus
extern "C" {
#endif
int lstat (const char *__restrict __path, struct stat *__restrict __buf);
int mknod (const char *__path, mode_t __mode, dev_t __dev);
#ifdef __cplusplus
}
#endif

#endif /* _ONYX_SYS_STAT_H */
