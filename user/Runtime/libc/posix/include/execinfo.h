/*
 * execinfo.h -- backtraces: backtrace walks the frame-pointer chain (when the code keeps one),
 * backtrace_symbols prints addresses (no symbol table at run time). libonyxposix misc.c.
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
#ifndef _EXECINFO_H
#define _EXECINFO_H

#ifdef __cplusplus
extern "C" {
#endif

int backtrace (void **, int);
char **backtrace_symbols (void *const *, int);
void backtrace_symbols_fd (void *const *, int, int);

#ifdef __cplusplus
}
#endif

#endif /* _EXECINFO_H */
