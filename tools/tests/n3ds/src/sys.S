@
@ sys.S -- the start-up and the system calls of the 3DS core's test programs (tools/tests/n3ds/src): the console's
@ SVC conventions (arguments in r0-r5, the result in r0, what a call returns in r1...), wrapped for C as the
@ homebrew libraries do. Our own code: no Nintendo SDK, no libctru.
@
@ MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
@
	.syntax unified
	.arch armv6k
	.fpu vfp
	.arm
	.text

	.section .text.start, "ax"
	.global _start
_start:
	bl	main
	svc	0x03				@ ExitProcess
1:	b	1b

	.text
	.macro FN name
	.global \name
	.type \name, %function
\name:
	.endm

@ Result svcControlMemory (u32 *out, u32 addr0, u32 addr1, u32 size, u32 op, u32 perm)
FN svcControlMemory
	push	{r0, r4}
	ldr	r0, [sp, #8]
	ldr	r4, [sp, #12]
	svc	0x01
	ldr	r2, [sp], #4
	str	r1, [r2]
	ldr	r4, [sp], #4
	bx	lr

FN svcExitProcess
	svc	0x03
	bx	lr

@ Result svcCreateThread (Handle *out, void (*entry) (void *), u32 arg, u32 *stackTop, s32 priority, s32 processor)
FN svcCreateThread
	push	{r0, r4}
	ldr	r0, [sp, #8]
	ldr	r4, [sp, #12]
	svc	0x08
	ldr	r2, [sp], #4
	str	r1, [r2]
	ldr	r4, [sp], #4
	bx	lr

FN svcExitThread
	svc	0x09
	bx	lr

@ void svcSleepThread (s64 ns)
FN svcSleepThread
	svc	0x0A
	bx	lr

@ Result svcGetThreadPriority (s32 *out, Handle h)
FN svcGetThreadPriority
	str	r0, [sp, #-4]!
	svc	0x0B
	ldr	r3, [sp], #4
	str	r1, [r3]
	bx	lr

FN svcSetThreadPriority
	svc	0x0C
	bx	lr

@ Result svcCreateMutex (Handle *out, int locked)
FN svcCreateMutex
	str	r0, [sp, #-4]!
	svc	0x13
	ldr	r3, [sp], #4
	str	r1, [r3]
	bx	lr

FN svcReleaseMutex
	svc	0x14
	bx	lr

@ Result svcCreateSemaphore (Handle *out, s32 initial, s32 max)
FN svcCreateSemaphore
	str	r0, [sp, #-4]!
	svc	0x15
	ldr	r3, [sp], #4
	str	r1, [r3]
	bx	lr

@ Result svcReleaseSemaphore (s32 *before, Handle h, s32 count)
FN svcReleaseSemaphore
	str	r0, [sp, #-4]!
	svc	0x16
	ldr	r3, [sp], #4
	str	r1, [r3]
	bx	lr

@ Result svcCreateEvent (Handle *out, u32 resetType)
FN svcCreateEvent
	str	r0, [sp, #-4]!
	svc	0x17
	ldr	r3, [sp], #4
	str	r1, [r3]
	bx	lr

FN svcSignalEvent
	svc	0x18
	bx	lr

FN svcClearEvent
	svc	0x19
	bx	lr

@ Result svcCreateAddressArbiter (Handle *out)
FN svcCreateAddressArbiter
	str	r0, [sp, #-4]!
	svc	0x21
	ldr	r3, [sp], #4
	str	r1, [r3]
	bx	lr

@ Result svcArbitrateAddress (Handle arbiter, u32 addr, u32 type, s32 value, s64 ns)
FN svcArbitrateAddress
	push	{r4, r5}
	ldr	r4, [sp, #8]
	ldr	r5, [sp, #12]
	svc	0x22
	pop	{r4, r5}
	bx	lr

FN svcCloseHandle
	svc	0x23
	bx	lr

@ Result svcWaitSynchronization (Handle h, s64 ns)	(ns in r2:r3)
FN svcWaitSynchronization
	svc	0x24
	bx	lr

@ Result svcWaitSynchronizationN (s32 *out, Handle *handles, s32 count, int all, s64 ns)
FN svcWaitSynchronizationN
	str	r5, [sp, #-4]!
	str	r4, [sp, #-4]!
	mov	r5, r0
	ldr	r0, [sp, #8]
	ldr	r4, [sp, #12]
	svc	0x25
	str	r1, [r5]
	ldr	r4, [sp], #4
	ldr	r5, [sp], #4
	bx	lr

@ Result svcDuplicateHandle (Handle *out, Handle h)
FN svcDuplicateHandle
	str	r0, [sp, #-4]!
	svc	0x27
	ldr	r3, [sp], #4
	str	r1, [r3]
	bx	lr

@ u64 svcGetSystemTick (void)
FN svcGetSystemTick
	svc	0x28
	bx	lr

@ Result svcGetThreadId (u32 *out, Handle h)
FN svcGetThreadId
	str	r0, [sp, #-4]!
	svc	0x37
	ldr	r3, [sp], #4
	str	r1, [r3]
	bx	lr

FN svcBreak
	svc	0x3C
	bx	lr

@ void svcOutputDebugString (const char *text, int length)
FN svcOutputDebugString
	svc	0x3D
	bx	lr

@ Result svcUnknown (void): a call the kernel does not have
FN svcUnknown
	svc	0x7F
	bx	lr

@ u32 *getTls (void): the thread's local storage (CP15: the thread ID register the user reads)
FN getTls
	mrc	p15, 0, r0, c13, c0, 3
	bx	lr

@ s32 atomicAdd (s32 *p, s32 n): the new value (LDREX / STREX)
FN atomicAdd
1:	ldrex	r2, [r0]
	add	r2, r2, r1
	strex	r3, r2, [r0]
	cmp	r3, #0
	bne	1b
	mov	r0, r2
	bx	lr

@ u32 thumbSum (u32 n): 1 + ... + n, in Thumb
	.thumb
	.thumb_func
	.global thumbSum
	.type thumbSum, %function
thumbSum:
	movs	r1, #0
1:	adds	r1, r1, r0
	subs	r0, #1
	bne	1b
	movs	r0, r1
	bx	lr
