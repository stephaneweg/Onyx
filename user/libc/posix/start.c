/*
 * start.c -- the C half of crt0posix (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * In this order:
 *  1. the main thread's thread pointer (TPIDR_EL0 -> its TCB and static TLS block, tls.c):
 *     before anything that may touch TLS or errno;
 *  2. the unwind tables (.eh_frame, kept by onyx-posix.ld) registered with libgcc's unwinder,
 *     when the program has one (C++ exceptions) -- a weak reference: a C program pulls nothing;
 *  3. the descriptors 0, 1, 2 (the console);
 *  4. the environment (kapi get_env, TZ from the kernel's time zone) and argc / argv (get_argv;
 *     the old get_args line on an older kernel);
 *  5. the constructors (.init_array, in priority order: onyx-posix.ld sorts them);
 *  6. exit (main (argc, argv, environ)): atexit handlers, stdio flushed, _exit -> kapi_exit.
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
#include "posix_internal.h"

extern int main (int argc, char **argv, char **envp);
extern char **environ;

extern void (*__init_array_start[]) (void) __attribute__ ((weak));
extern void (*__init_array_end[]) (void) __attribute__ ((weak));
extern char __onyx_eh_frame_start[] __attribute__ ((weak));
extern void __register_frame_info (const void *begin, void *object) __attribute__ ((weak));

/* libgcc's `struct object' (unwind-dw2-fde.h): 6 pointer-sized words; a little more is harmless */
static void *s_ehObject[8];

void __onyx_start (void) __attribute__ ((noreturn));
void __onyx_start (void)
{
	__onyx_tls_init_main ();

	if (__register_frame_info != 0 && __onyx_eh_frame_start != 0)
		__register_frame_info (__onyx_eh_frame_start, s_ehObject);

	__onyx_fd_init ();
	__onyx_env_init ();
	char **argv;
	int argc = __onyx_args_init (&argv);

	if (__init_array_start != 0)
		for (void (**f) (void) = __init_array_start; f < __init_array_end; f++)
			(*f) ();

	exit (main (argc, argv, environ));
}
