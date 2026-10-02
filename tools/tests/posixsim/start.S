/*
 * start.S -- posixsim's entry and its thread start (fakekapi.c): Linux enters with argc / argv /
 * envp on the stack; posixsim_init maps and fills the kapi table, then the program's own entry
 * (crt0posix's _start) runs as on Onyx.
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
	.text
	.globl	posixsim_start
	.type	posixsim_start, %function
posixsim_start:
	mov	x29, #0
	mov	x0, sp
	bl	posixsim_init
	b	_start

/* long posixsim_clone (flags, stack, ptid, tls, ctid, fn, arg): Linux clone; the child runs
   fn (arg) on the new stack, then exits (the thread only) */
	.globl	posixsim_clone
	.type	posixsim_clone, %function
posixsim_clone:
	stp	x5, x6, [x1, #-16]!
	mov	x8, #220
	svc	#0
	cbz	x0, 1f
	ret
1:	ldp	x1, x0, [sp], #16
	mov	x29, #0
	mov	x30, #0
	blr	x1
	mov	x8, #93
	svc	#0
	b	.
