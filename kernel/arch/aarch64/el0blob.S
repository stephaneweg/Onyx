/*
 * el0blob.S -- the user-side part of the kapi table (kern/el0.h), run AT EL0 by every process.
 * Not kernel code: these bytes are copied at boot (El0Init) into the EL0 code page, mapped
 * read-only + EL0-executable at KAPI_STUBS_VA + EL0_BLOB_OFFSET in every process, and the EL0
 * table points at them. So: position-independent (relative branches and
 * immediates only, no literal pool, no data), nothing but the AAPCS64, system calls by
 * "mov x8, #slot; svc #0" (the kernel returns x0 and preserves every other register).
 *
 * MIT licence (Onyx). Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
 */

#include <kern/el0.h>

	.section .rodata.el0blob, "a"
	.balign	16
	.globl	El0BlobStart
El0BlobStart:

/*
 * void *memcpy (void *dst, const void *src, unsigned long n) -- forward, 64 bytes a turn, then
 * 16, then bytes. Every block is loaded before it is stored, so it is also a correct forward
 * memmove (dst below src).
 */
	.globl	El0Memcpy
El0Memcpy:
	mov	x3, x0
	cmp	x2, #64
	b.lo	2f
1:	ldp	x4,  x5,  [x1]
	ldp	x6,  x7,  [x1, #16]
	ldp	x8,  x9,  [x1, #32]
	ldp	x10, x11, [x1, #48]
	add	x1, x1, #64
	stp	x4,  x5,  [x3]
	stp	x6,  x7,  [x3, #16]
	stp	x8,  x9,  [x3, #32]
	stp	x10, x11, [x3, #48]
	add	x3, x3, #64
	sub	x2, x2, #64
	cmp	x2, #64
	b.hs	1b
2:	cmp	x2, #16
	b.lo	4f
3:	ldp	x4, x5, [x1], #16
	stp	x4, x5, [x3], #16
	sub	x2, x2, #16
	cmp	x2, #16
	b.hs	3b
4:	cbz	x2, 6f
5:	ldrb	w4, [x1], #1
	strb	w4, [x3], #1
	subs	x2, x2, #1
	b.ne	5b
6:	ret

/*
 * void *memmove (void *dst, const void *src, unsigned long n) -- forward (El0Memcpy) unless dst
 * lies inside (src, src + n): then backward, 16 bytes a turn (each block loaded before stored).
 */
	.globl	El0Memmove
El0Memmove:
	sub	x3, x0, x1		/* dst - src: >= n (unsigned) when no backward copy is needed */
	cmp	x3, x2
	b.hs	El0Memcpy
	add	x1, x1, x2
	add	x3, x0, x2
1:	cmp	x2, #16
	b.lo	2f
	ldp	x4, x5, [x1, #-16]!
	stp	x4, x5, [x3, #-16]!
	sub	x2, x2, #16
	b	1b
2:	cbz	x2, 3f
	ldrb	w4, [x1, #-1]!
	strb	w4, [x3, #-1]!
	sub	x2, x2, #1
	b	2b
3:	ret

/* void *memset (void *dst, int c, unsigned long n) */
	.globl	El0Memset
El0Memset:
	mov	x3, x0
	and	w1, w1, #0xff
	orr	w1, w1, w1, lsl #8
	orr	w1, w1, w1, lsl #16
	orr	x1, x1, x1, lsl #32
1:	cmp	x2, #16
	b.lo	2f
	stp	x1, x1, [x3], #16
	sub	x2, x2, #16
	b	1b
2:	cbz	x2, 3f
	strb	w1, [x3], #1
	sub	x2, x2, #1
	b	2b
3:	ret

/*
 * void pump_events (void) -- kapi_pump_events, user-side: first the calls posted to the process
 * (at most one ring's worth: a call that posts again runs at the next pump), then the window's
 * events, each handler called here at EL0. A key event's modifiers are the ones
 * kapi_get_modifiers reports while its handler runs (event_mods sets them, then puts back the
 * previous value), as the kernel's pump did.
 *
 * Frame: [sp] x29 x30, [16] x19 x20, [32] x21, [48..80) the struct kapi_event / kapi_posted
 * (kern/kapi_abi.h): handler/fn +0, sender/ctx +8, value +16, event +24, mods +28.
 */
	.globl	El0PumpEvents
El0PumpEvents:
	stp	x29, x30, [sp, #-80]!
	mov	x29, sp
	stp	x19, x20, [sp, #16]
	str	x21, [sp, #32]
	mov	x19, #EL0_POSTS_MAX
1:	add	x0, sp, #48
	mov	x8, #EL0_SYS_POP_POST
	svc	#0
	cbz	w0, 2f
	ldr	x16, [sp, #48]		/* fn (ctx, value) */
	ldr	x0, [sp, #56]
	ldr	x1, [sp, #64]
	blr	x16
	subs	x19, x19, #1
	b.ne	1b
2:	add	x0, sp, #48
	mov	x8, #EL0_SYS_POP_EVENT
	svc	#0
	cbz	w0, 9f
	ldr	x16, [sp, #48]		/* the handler (0: none, dropped) */
	cbz	x16, 2b
	mov	w21, #0
	ldr	w9, [sp, #72]
	cmp	w9, #EL0_GUI_EVENT_KEY
	b.ne	3f
	ldr	w0, [sp, #76]		/* a key: its modifiers while the handler runs */
	mov	x8, #EL0_SYS_EVENT_MODS
	svc	#0
	mov	w20, w0			/* the previous value, put back after */
	mov	w21, #1
3:	ldr	x16, [sp, #48]
	ldr	x0, [sp, #56]		/* handler (sender, event, value) */
	ldr	w1, [sp, #72]
	ldr	x2, [sp, #64]
	blr	x16
	cbz	w21, 2b
	mov	w0, w20
	mov	x8, #EL0_SYS_EVENT_MODS
	svc	#0
	b	2b
9:	ldr	x21, [sp, #32]
	ldp	x19, x20, [sp, #16]
	ldp	x29, x30, [sp], #80
	ret

/*
 * void wait_for_exit (void) -- pump until the window's close box is clicked: between two pumps,
 * sleep until an event or a post comes (16 ms at most, as the kernel's loop did).
 */
	.globl	El0WaitForExit
El0WaitForExit:
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
1:	bl	El0PumpEvents
	mov	x8, #EL0_SYS_SHOULD_EXIT
	svc	#0
	cbnz	w0, 2f
	mov	w0, #16
	mov	x8, #EL0_SYS_PUMP_SLEEP
	svc	#0
	b	1b
2:	ldp	x29, x30, [sp], #16
	ret

/* int pump_wait (unsigned timeout_ms) -- sleep (the kernel's pump_sleep), then pump. */
	.globl	El0PumpWait
El0PumpWait:
	stp	x29, x30, [sp, #-32]!
	mov	x29, sp
	str	x19, [sp, #16]
	mov	x8, #EL0_SYS_PUMP_SLEEP
	svc	#0
	mov	w19, w0			/* events + posts pending (-1: no process) */
	tbnz	w19, #31, 1f
	bl	El0PumpEvents
1:	mov	w0, w19
	ldr	x19, [sp, #16]
	ldp	x29, x30, [sp], #32
	ret

/* The return address of a thread's function: thread_exit (its result). */
	.globl	El0ThreadReturn
El0ThreadReturn:
	mov	x8, #EL0_SYS_THREAD_EXIT
	svc	#0
	b	El0ThreadReturn

/* The return address of the main entry point (crt0 returned): exit (0). */
	.globl	El0MainReturn
El0MainReturn:
	mov	x0, #0
	mov	x8, #EL0_SYS_EXIT
	svc	#0
	b	El0MainReturn

/* The return address of an app core's job: back to the core's loop. */
	.globl	El0CoreReturn
El0CoreReturn:
	mov	x8, #EL0_SYS_CORE_DONE
	svc	#0
	b	El0CoreReturn

	.globl	El0BlobEnd
El0BlobEnd:

/* End */
