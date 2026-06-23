/*
 * crt0.S -- userland entry (Option C: the app runs in EL1, called directly by the
 * kernel with SP set to its thread stack). Call main(), then return to the kernel,
 * which terminates the task (CUserProcessTask::Run). Apps that want to exit early
 * call kapi_exit() themselves (it dispatches through the kapi ABI table).
 */
	.section .text._start
	.globl	_start
_start:
	stp	x29, x30, [sp, #-16]!	/* save frame ptr + return address to the kernel */
	bl	main
	ldp	x29, x30, [sp], #16
	ret				/* back to the kernel -> task terminates */
