/*
 * dlfcn.h -- dynamic loading: Onyx programs are static, so dlopen fails (dlerror says why) and
 * dlsym finds nothing; dladdr knows nothing (libonyxposix misc.c).
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
#ifndef _DLFCN_H
#define _DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

#define RTLD_LAZY	0x0001
#define RTLD_NOW	0x0002
#define RTLD_NOLOAD	0x0004
#define RTLD_GLOBAL	0x0100
#define RTLD_LOCAL	0
#define RTLD_DEFAULT	((void *) 0)
#define RTLD_NEXT	((void *) -1)

typedef struct
{
	const char *dli_fname;
	void *dli_fbase;
	const char *dli_sname;
	void *dli_saddr;
} Dl_info;

void *dlopen (const char *, int);
void *dlsym (void *__restrict, const char *__restrict);
int dlclose (void *);
char *dlerror (void);
int dladdr (const void *, Dl_info *);

#ifdef __cplusplus
}
#endif

#endif /* _DLFCN_H */
