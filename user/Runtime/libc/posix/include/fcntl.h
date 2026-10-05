/*
 * fcntl.h -- newlib's, plus Linux's file seals (fcntl F_ADD_SEALS / F_GET_SEALS on a memfd_create or
 * shm_open descriptor: kapi v76, docs/POSIX-PLAN.md §14; WebKit's IPC::Semaphore uses them).
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
#ifndef _ONYX_FCNTL_H
#define _ONYX_FCNTL_H

#include_next <fcntl.h>

#ifndef F_ADD_SEALS
#define F_ADD_SEALS	1033		/* = Linux */
#define F_GET_SEALS	1034
#define F_SEAL_SEAL	0x0001
#define F_SEAL_SHRINK	0x0002
#define F_SEAL_GROW	0x0004
#define F_SEAL_WRITE	0x0008
#endif

#endif /* _ONYX_FCNTL_H */
