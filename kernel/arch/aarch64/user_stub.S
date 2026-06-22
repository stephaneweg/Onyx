/*
 * user_stub.S
 *
 * A minimal EL0 program used by milestone #5 to prove the user/kernel boundary:
 * page tables, the EL0 entry (enter_user), an SVC syscall, and the return to EL0.
 *
 * It is POSITION-INDEPENDENT (only PC-relative `adr`), because the kernel copies
 * these bytes [user_stub_begin, user_stub_end) into a fresh user frame mapped at
 * USER_VA_BASE and runs them there. Real programs arrive as ELF files in #6.
 */

#include <kern/syscall.h>

	.section .text
	.align	4
	.globl	user_stub_begin
	.globl	user_stub_end

user_stub_begin:
	/* write(fd=1, msg, len) */
	adr	x1, 1f				/* msg: PC-relative -> correct EL0 VA */
	mov	x0, #1				/* fd */
	mov	x2, #(2f - 1f)			/* len */
	mov	x8, #SYS_write
	svc	#0

	/* Loop forever in EL0. The timer preempts us, so the kernel's other
	   threads keep running; this proves EL0 is just another scheduled
	   context. (Clean exit + teardown comes with the process model in #6.) */
0:	b	0b

	.align	3
1:	.ascii	"hello from EL0 (user process)\n"
2:
	.align	4
user_stub_end:

/* End */
