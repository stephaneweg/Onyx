/*
 * crt0libc.S -- userland entry for apps linked against newlib (see user/libc/).
 *
 * Same model as crt0.S (run the .init_array, then main), but instead of returning
 * to the kernel it calls exit(main(...)). That runs newlib's atexit handlers and
 * flushes/closes stdio (so buffered stdout reaches the console) before the task
 * terminates via _exit -> kapi_exit. Pair with onyx_syscalls.c.
 */
	.section .text._start
	.globl	_start
_start:
	stp	x29, x30, [sp, #-32]!	/* keep the frame 16-byte aligned */
	stp	x19, x20, [sp, #16]	/* callee-saved regs used by the ctor loop */

	/* Run global constructors: for (f in [__init_array_start, __init_array_end)) f() */
	adrp	x19, __init_array_start
	add	x19, x19, :lo12:__init_array_start
	adrp	x20, __init_array_end
	add	x20, x20, :lo12:__init_array_end
1:	cmp	x19, x20
	b.hs	2f
	ldr	x0, [x19], #8
	blr	x0
	b	1b
2:
	bl	main			/* int main(void) -> w0 */
	bl	exit			/* exit(w0): flush stdio + atexit, then _exit */

	/* exit() does not return; if it ever did, fall back to a clean stop. */
	ldp	x19, x20, [sp, #16]
	ldp	x29, x30, [sp], #32
	ret
