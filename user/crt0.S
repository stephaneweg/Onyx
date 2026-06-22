/*
 * crt0.S -- userland entry. The kernel enters here in EL0 with SP_EL0 set to the
 * user stack top. Call main(), then exit() with its return value.
 */
	.section .text._start
	.globl	_start
_start:
	bl	main			/* x0 = return value */
	mov	x8, #3			/* SYS_exit */
	svc	#0
1:	b	1b			/* never reached */
