/*
 * el0.S -- apps at EL0 (kern/el0.h): the exceptions taken from EL0, the return to EL0, and
 * the first entry into EL0 of a task, a thread or an app core's job.
 *
 * The "Lower EL using AArch64" group of KVectorTable (vectors.S) branches here; the exceptions
 * taken at EL1 (the kernel's own) keep vectors.S's paths.
 *
 * The stacks.
 *   - While a core runs EL0 code, TPIDR_EL1 holds the top of the running task's KERNEL stack (its
 *     CTask stack: free from there down). El0Return sets it right before the eret, from the
 *     frame it returns from, so it is always the right one for the context that eret enters --
 *     whichever task the scheduler resumed.
 *   - An exception from EL0 arrives at EL1h on SP_EL1, the core's exception stack (Circle's), with
 *     DAIF all masked. EL0_ENTRY borrows 16 bytes of it for two scratch registers, builds the
 *     whole trap frame (kern/trapframe.h: x0-x30, the user's SP_EL0, ELR, SPSR, q0-q31, FPSR,
 *     FPCR) at TPIDR_EL1 - TF_SIZE, gives SP_EL1 its 16 bytes back, points SP_EL0 at the frame
 *     and selects it (SPSel = 0): the handler runs at EL1t on the task's kernel stack -- as the
 *     kernel's own tasks run --, where Yield / TaskSwitch (which saves and restores SP_EL0) work.
 *   - El0Return: IRQs masked, back to SPSel = 1 (SP_EL0 is then free to hold the user's SP again:
 *     MSR SP_EL0 is UNDEFINED at EL1 with SPSel = 0), TPIDR_EL1 = frame + TF_SIZE, everything
 *     restored, eret. SP_EL1 is exactly as the exception found it.
 *
 * MIT licence (Onyx). Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
 */

#include <kern/trapframe.h>

	.macro	EL0_ENTRY
	stp	x0, x1, [sp, #-16]!	/* SP_EL1: two scratch registers */
	mrs	x0, tpidr_el1		/* the task's kernel stack top (El0Return) */
	sub	x0, x0, #TF_SIZE	/* x0 = the trap frame, on the kernel stack */
	stp	x2,  x3,  [x0, #TF_X(2)]
	stp	x4,  x5,  [x0, #TF_X(4)]
	stp	x6,  x7,  [x0, #TF_X(6)]
	stp	x8,  x9,  [x0, #TF_X(8)]
	stp	x10, x11, [x0, #TF_X(10)]
	stp	x12, x13, [x0, #TF_X(12)]
	stp	x14, x15, [x0, #TF_X(14)]
	stp	x16, x17, [x0, #TF_X(16)]
	stp	x18, x19, [x0, #TF_X(18)]
	stp	x20, x21, [x0, #TF_X(20)]
	stp	x22, x23, [x0, #TF_X(22)]
	stp	x24, x25, [x0, #TF_X(24)]
	stp	x26, x27, [x0, #TF_X(26)]
	stp	x28, x29, [x0, #TF_X(28)]
	str	x30,      [x0, #TF_X(30)]
	ldp	x2, x3, [sp], #16	/* the user's x0, x1 -- SP_EL1 as it was */
	stp	x2, x3, [x0, #TF_X(0)]
	mrs	x2, sp_el0		/* (SPSel = 1 here: SP_EL0 is readable) */
	mrs	x3, elr_el1
	str	x2, [x0, #TF_SP_EL0]
	str	x3, [x0, #TF_ELR]
	mrs	x2, spsr_el1
	str	x2, [x0, #TF_SPSR]
	stp	q0,  q1,  [x0, #TF_Q(0)]
	stp	q2,  q3,  [x0, #TF_Q(2)]
	stp	q4,  q5,  [x0, #TF_Q(4)]
	stp	q6,  q7,  [x0, #TF_Q(6)]
	stp	q8,  q9,  [x0, #TF_Q(8)]
	stp	q10, q11, [x0, #TF_Q(10)]
	stp	q12, q13, [x0, #TF_Q(12)]
	stp	q14, q15, [x0, #TF_Q(14)]
	stp	q16, q17, [x0, #TF_Q(16)]
	stp	q18, q19, [x0, #TF_Q(18)]
	stp	q20, q21, [x0, #TF_Q(20)]
	stp	q22, q23, [x0, #TF_Q(22)]
	stp	q24, q25, [x0, #TF_Q(24)]
	stp	q26, q27, [x0, #TF_Q(26)]
	stp	q28, q29, [x0, #TF_Q(28)]
	stp	q30, q31, [x0, #TF_Q(30)]
	mrs	x2, fpsr
	mrs	x3, fpcr
	str	x2, [x0, #TF_FPSR]
	str	x3, [x0, #TF_FPCR]
	msr	sp_el0, x0		/* the kernel stack, the frame on its top ... */
	msr	spsel, #0		/* ... selected: EL1t from here (DAIF still all masked) */
	isb
	mov	x29, xzr		/* (no frame chain into the user's stack) */
	.endm

	.text

/*
 * Synchronous exception from EL0: a system call (svc), or a fault that kills the process (an
 * app core's job: the job is dropped). El0SyncHandler enables the IRQs itself for a system call.
 */
	.globl	El0SyncEntry
	.type	El0SyncEntry, %function
El0SyncEntry:
	EL0_ENTRY
	mov	x0, sp
	bl	El0SyncHandler
	b	El0Return

/*
 * IRQ from EL0: as IrqEntry does (FIQ enabled around Circle's handler, which EnterCritical
 * requires), but on the task's kernel stack, then El0IrqExit -- which may simply Yield there when
 * the time slice is over (the switch happens at EL1t, on this task's kernel stack).
 */
	.globl	El0IrqEntry
	.type	El0IrqEntry, %function
El0IrqEntry:
	EL0_ENTRY
	msr	DAIFClr, #1		/* enable FIQ (EnterCritical asserts it) */
	bl	InterruptHandler	/* Circle: GIC dispatch + EOI + the periodic tick */
	msr	DAIFSet, #1		/* IRQ + FIQ masked again (El0IrqExit may Yield so) */
	mov	x0, sp
	bl	El0IrqExit
	b	El0Return

/*
 * Return to the context of the trap frame at SP (EL1t, on a kernel stack): normally EL0 -- or,
 * for an app core's dropped job, the EL1t context the frame was rewritten to (AppCoreRestart).
 */
	.globl	El0Return
	.type	El0Return, %function
El0Return:
	msr	DAIFSet, #0xf		/* nothing may take ELR / SPSR / SP_EL0 from here */
	mov	x0, sp			/* x0 = the frame */
	msr	spsel, #1		/* back on SP_EL1 (untouched): SP_EL0 is free */
	isb
	add	x1, x0, #TF_SIZE
	msr	tpidr_el1, x1		/* the kernel stack top for this context's next exception */
	ldr	x2, [x0, #TF_FPSR]
	ldr	x3, [x0, #TF_FPCR]
	msr	fpsr, x2
	msr	fpcr, x3		/* (the eret synchronises it) */
	ldp	q0,  q1,  [x0, #TF_Q(0)]
	ldp	q2,  q3,  [x0, #TF_Q(2)]
	ldp	q4,  q5,  [x0, #TF_Q(4)]
	ldp	q6,  q7,  [x0, #TF_Q(6)]
	ldp	q8,  q9,  [x0, #TF_Q(8)]
	ldp	q10, q11, [x0, #TF_Q(10)]
	ldp	q12, q13, [x0, #TF_Q(12)]
	ldp	q14, q15, [x0, #TF_Q(14)]
	ldp	q16, q17, [x0, #TF_Q(16)]
	ldp	q18, q19, [x0, #TF_Q(18)]
	ldp	q20, q21, [x0, #TF_Q(20)]
	ldp	q22, q23, [x0, #TF_Q(22)]
	ldp	q24, q25, [x0, #TF_Q(24)]
	ldp	q26, q27, [x0, #TF_Q(26)]
	ldp	q28, q29, [x0, #TF_Q(28)]
	ldp	q30, q31, [x0, #TF_Q(30)]
	ldr	x2, [x0, #TF_SP_EL0]
	ldr	x3, [x0, #TF_ELR]
	msr	sp_el0, x2		/* the user's SP (SPSel = 1: allowed) */
	msr	elr_el1, x3
	ldr	x2, [x0, #TF_SPSR]
	msr	spsr_el1, x2
	ldp	x2,  x3,  [x0, #TF_X(2)]
	ldp	x4,  x5,  [x0, #TF_X(4)]
	ldp	x6,  x7,  [x0, #TF_X(6)]
	ldp	x8,  x9,  [x0, #TF_X(8)]
	ldp	x10, x11, [x0, #TF_X(10)]
	ldp	x12, x13, [x0, #TF_X(12)]
	ldp	x14, x15, [x0, #TF_X(14)]
	ldp	x16, x17, [x0, #TF_X(16)]
	ldp	x18, x19, [x0, #TF_X(18)]
	ldp	x20, x21, [x0, #TF_X(20)]
	ldp	x22, x23, [x0, #TF_X(22)]
	ldp	x24, x25, [x0, #TF_X(24)]
	ldp	x26, x27, [x0, #TF_X(26)]
	ldp	x28, x29, [x0, #TF_X(28)]
	ldr	x30,      [x0, #TF_X(30)]
	ldr	x1, [x0, #TF_X(1)]
	ldr	x0, [x0, #TF_X(0)]	/* (the base register last) */
	eret
	dsb	nsh			/* no straight-line speculation past the eret */
	isb

/*
 * El0Enter (entry x0, user SP x1, arg x2, LR x3): a new EL0 context on the current kernel stack.
 * At EL1t (a task, an app core's loop), in the process's address space. Does not return: the
 * caller's frames above stay as they are (never used again: the context ends in the kernel by
 * exit / thread_exit / the core's restart, on a fresh frame).
 */
	.globl	El0Enter
	.type	El0Enter, %function
El0Enter:
	msr	DAIFSet, #0xf
	mov	x9, sp
	and	x9, x9, #~15
	sub	x9, x9, #TF_SIZE
	mov	sp, x9			/* (EL1t: SP_EL0) the frame */
	mov	x10, x9
	mov	x11, #(TF_SIZE / 16)
1:	stp	xzr, xzr, [x10], #16	/* every register 0: no kernel value leaks into EL0 */
	subs	x11, x11, #1
	b.ne	1b
	str	x2, [sp, #TF_X(0)]
	str	x3, [sp, #TF_X(30)]
	str	x1, [sp, #TF_SP_EL0]
	str	x0, [sp, #TF_ELR]
	/* SPSR = EL0t (M[4:0] = 0b00000: AArch64, EL0, SP_EL0), NZCV 0, IRQ + FIQ enabled, SError
	   and debug masked (D, A = bits 9, 8) -- as the kernel's tasks run: a pending SError is
	   not taken in the app's code. FPSR / FPCR 0. */
	mov	x4, #0x300
	str	x4, [sp, #TF_SPSR]
	b	El0Return

/* End */
