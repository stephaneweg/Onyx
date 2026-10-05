/*
 * sys/file.h -- newlib's, plus flock (libonyxposix file.c: always granted -- one process per
 * file in practice; there is no lock manager).
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
#ifndef _ONYX_SYS_FILE_H
#define _ONYX_SYS_FILE_H

#include_next <sys/file.h>

#ifndef LOCK_SH
#define LOCK_SH		1
#define LOCK_EX		2
#define LOCK_NB		4
#define LOCK_UN		8
#endif

#ifdef __cplusplus
extern "C" {
#endif
int flock (int, int);
#ifdef __cplusplus
}
#endif

#endif /* _ONYX_SYS_FILE_H */
