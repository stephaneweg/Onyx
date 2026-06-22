/*
 * vectors.S
 *
 * Our EL1 exception vector table (KVectorTable) and entry/exit stubs. Installed
 * over Circle's VectorTable after CExceptionHandler runs (see install_vectors).
 *
 * Design (see ARCHITECTURE.md §6):
 *   - IRQ (from EL1h and EL0): save full trap frame on the CURRENT stack (which is
 *     the running thread's kernel stack), call Circle's InterruptHandler (GIC
 *     dispatch + EOI + the registered periodic tick handler), then KernelIRQExit
 *     (reschedules if the time slice expired -> preemption), then restore + eret.
 *   - Sync from EL1: SVC -> syscall (test path); any other -> kernel panic dump.
 *   - Sync from EL0: SVC -> syscall; abort -> page-fault handler.
 *   - FIQ: branch to Circle's FIQStub (unchanged) so its FIQ path keeps working.
 *   - Everything else (EL1t, AArch32 lower EL, SError): bad-mode dump.
 *
 * The trap frame layout matches kern/trapframe.h (TTrapFrame).
 */

#include <kern/trapframe.h>

/* Save x0..x30, sp_el0, elr_el1, spsr_el1 onto the current stack. */
	.macro	SAVE_TRAP
	sub	sp, sp, #TF_SIZE
	stp	x0,  x1,  [sp, #TF_X(0)]
	stp	x2,  x3,  [sp, #TF_X(2)]
	stp	x4,  x5,  [sp, #TF_X(4)]
	stp	x6,  x7,  [sp, #TF_X(6)]
	stp	x8,  x9,  [sp, #TF_X(8)]
	stp	x10, x11, [sp, #TF_X(10)]
	stp	x12, x13, [sp, #TF_X(12)]
	stp	x14, x15, [sp, #TF_X(14)]
	stp	x16, x17, [sp, #TF_X(16)]
	stp	x18, x19, [sp, #TF_X(18)]
	stp	x20, x21, [sp, #TF_X(20)]
	stp	x22, x23, [sp, #TF_X(22)]
	stp	x24, x25, [sp, #TF_X(24)]
	stp	x26, x27, [sp, #TF_X(26)]
	stp	x28, x29, [sp, #TF_X(28)]
	str	x30,      [sp, #TF_X(30)]
	mrs	x9,  sp_el0
	mrs	x10, elr_el1
	mrs	x11, spsr_el1
	str	x9,  [sp, #TF_SP_EL0]
	str	x10, [sp, #TF_ELR]
	str	x11, [sp, #TF_SPSR]
	.endm

/* Restore everything and return from the exception. */
	.macro	RESTORE_TRAP_ERET
	ldr	x9,  [sp, #TF_SP_EL0]
	ldr	x10, [sp, #TF_ELR]
	ldr	x11, [sp, #TF_SPSR]
	msr	sp_el0,   x9
	msr	elr_el1,  x10
	msr	spsr_el1, x11
	ldp	x0,  x1,  [sp, #TF_X(0)]
	ldp	x2,  x3,  [sp, #TF_X(2)]
	ldp	x4,  x5,  [sp, #TF_X(4)]
	ldp	x6,  x7,  [sp, #TF_X(6)]
	ldp	x8,  x9,  [sp, #TF_X(8)]
	ldp	x10, x11, [sp, #TF_X(10)]
	ldp	x12, x13, [sp, #TF_X(12)]
	ldp	x14, x15, [sp, #TF_X(14)]
	ldp	x16, x17, [sp, #TF_X(16)]
	ldp	x18, x19, [sp, #TF_X(18)]
	ldp	x20, x21, [sp, #TF_X(20)]
	ldp	x22, x23, [sp, #TF_X(22)]
	ldp	x24, x25, [sp, #TF_X(24)]
	ldp	x26, x27, [sp, #TF_X(26)]
	ldp	x28, x29, [sp, #TF_X(28)]
	ldr	x30,      [sp, #TF_X(30)]
	add	sp, sp, #TF_SIZE
	eret
	.endm

/* One vector slot: aligned to 0x80, branches to a handler. */
	.macro	VEC handler
	.align	7
	b	\handler
	.endm

	.text
	.align	11			/* VBAR must be 2KB aligned */
	.globl	KVectorTable
KVectorTable:
	/* Current EL with SP0 (EL1t) -- Circle runs the main thread + all tasks HERE
	   (they use SP_EL0), so normal IRQs/sync come through this group. Must use the
	   real handlers, NOT BadMode. */
	VEC	SyncEL1Entry
	VEC	IrqEntry
	VEC	FIQStub
	VEC	BadModeEntry		/* SError */

	/* Current EL with SPx (EL1h) -- kernel threads */
	VEC	SyncEL1Entry
	VEC	IrqEntry
	VEC	FIQStub			/* reuse Circle's FIQ path */
	VEC	BadModeEntry		/* SError */

	/* Lower EL using AArch64 (EL0) -- user processes */
	VEC	SyncEL0Entry
	VEC	IrqEntry
	VEC	FIQStub
	VEC	BadModeEntry		/* SError */

	/* Lower EL using AArch32 -- unsupported */
	VEC	BadModeEntry
	VEC	BadModeEntry
	VEC	BadModeEntry
	VEC	BadModeEntry

/*
 * IRQ (shared by EL1t/EL1h/EL0). We MUST enable FIQ during handling: Circle's
 * InterruptHandler (and registered IRQ handlers) call EnterCritical(IRQ_LEVEL),
 * which asserts FIQ is not masked (synchronize64.cpp:111). Circle's own IRQStub
 * does the same DAIFClr/DAIFSet around the handler. Re-mask FIQ before eret.
 */
IrqEntry:
	SAVE_TRAP
	msr	DAIFClr, #1		/* enable FIQ (required by EnterCritical) */
	bl	InterruptHandler	/* Circle: GIC dispatch + EOI + periodic tick */
	bl	KernelIRQExit
	msr	DAIFSet, #1		/* disable FIQ again before returning */
	RESTORE_TRAP_ERET

/* Synchronous from EL1 (kernel): SVC test path or kernel panic. */
SyncEL1Entry:
	SAVE_TRAP
	mov	x0, sp
	bl	SyncHandlerEL1
	RESTORE_TRAP_ERET

/* Synchronous from EL0 (user): syscall or page fault. */
SyncEL0Entry:
	SAVE_TRAP
	mov	x0, sp
	bl	SyncHandlerEL0
	RESTORE_TRAP_ERET

/* Unexpected vector / SError / AArch32: dump and halt. */
BadModeEntry:
	SAVE_TRAP
	mov	x0, sp
	bl	BadModeHandler
	RESTORE_TRAP_ERET		/* BadModeHandler does not return, but keep it valid */

/*
 * install_vectors(): point VBAR_EL1 at our table.
 */
	.globl	install_vectors
install_vectors:
	ldr	x0, =KVectorTable
	msr	vbar_el1, x0
	isb
	ret

/*
 * enter_user(entry=x0, user_sp=x1, kparam=x2): drop to EL0 (used from #6).
 * SP_EL1 keeps its current value as the kernel stack used on the next trap.
 */
	.globl	enter_user
enter_user:
	msr	elr_el1, x0		/* user entry PC */
	msr	sp_el0,  x1		/* user stack */
	mov	x0, #SPSR_MODE_EL0t	/* EL0t, DAIF clear */
	msr	spsr_el1, x0
	mov	x0, x2			/* pass kparam in x0 to the user entry */
	/* scrub the rest to avoid leaking kernel state into EL0 */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	eret

/* End */
