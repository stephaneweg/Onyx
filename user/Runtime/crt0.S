/*
 * crt0.S -- userland entry (Option C: the app runs in EL1, called directly by the
 * kernel with SP set to its thread stack). Run the C++ global constructors (the
 * .init_array; empty for C apps), call main(), then return to the kernel, which
 * terminates the task (CUserProcessTask::Run). Apps that want to exit early call
 * kapi_exit() themselves (it dispatches through the kapi ABI table).
 */
	.section .text._start
	.globl	_start
_start:
	stp	x29, x30, [sp, #-32]!	/* save FP + return address to the kernel ... */
	stp	x19, x20, [sp, #16]	/* ... and the callee-saved regs the loop uses */

	/* Run global constructors: for (f in [__init_array_start, __init_array_end)) f() */
	adrp	x19, __init_array_start
	add	x19, x19, :lo12:__init_array_start
	adrp	x20, __init_array_end
	add	x20, x20, :lo12:__init_array_end
1:	cmp	x19, x20
	b.hs	2f
	ldr	x0, [x19], #8		/* next ctor pointer (post-increment) */
	blr	x0
	b	1b
2:
	bl	main

	ldp	x19, x20, [sp, #16]
	ldp	x29, x30, [sp], #32
	ret				/* back to the kernel -> task terminates */
