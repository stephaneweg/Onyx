/*
 * crt0.S -- userland entry (Option C: the app runs in EL1, called directly by the
 * kernel with SP set to its thread stack). Call main(), then kapi_exit() with its
 * return value. kapi_exit is a kernel symbol resolved at link (kernel_syms.ld).
 */
	.section .text._start
	.globl	_start
_start:
	bl	main			/* x0 = return value */
	bl	kapi_exit		/* exit(status); does not return */
1:	b	1b
