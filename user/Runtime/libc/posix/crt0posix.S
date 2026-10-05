/*
 * crt0posix.S -- the entry of a program built on libonyxposix (docs/POSIX-PLAN.md §3.4).
 *
 * The kernel enters _start at EL0 with the stack set and no arguments in registers. The frame
 * chain is ended (x29 = 0, x30 = 0: backtrace stops there) and __onyx_start (start.c) does the
 * rest in C -- first of all the main thread's TPIDR_EL0 (tls.c), then the unwind tables, the
 * descriptors, the environment, argc / argv, the constructors, and exit (main (argc, argv, envp)).
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
	.section .text._start, "ax"
	.globl	_start
	.type	_start, %function
_start:
	mov	x29, #0
	mov	x30, #0
	mov	x0, sp
	and	x0, x0, #~15		/* (the kernel's SP is aligned; keep it so) */
	mov	sp, x0
	bl	__onyx_start		/* does not return */
1:	b	1b
	.size	_start, . - _start

/*
 * Empty _init / _fini: newlib's __libc_fini_array (exit) calls _fini, and there is no crti.o.
 * The constructors and destructors are the .init_array / .fini_array. Weak: a real one wins.
 */
	.section .text._init_fini, "ax"
	.weak	_init
	.weak	_fini
	.type	_init, %function
	.type	_fini, %function
_init:
_fini:
	ret
