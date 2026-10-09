@ asm9.S -- the ARMv5TE instructions the C compiler may not make: each check sets a bit of r4 when it
@ passes; v5tests (out) stores r4 (0x3FFF expected) and the Q flag test's CPSR.
	.text
	.arm
	.global v5tests
v5tests:
	push	{r4-r11, lr}
	mov	r11, r0
	mov	r4, #0
	@ 0: CLZ
	ldr	r0, =0x00010000
	clz	r1, r0
	cmp	r1, #15
	orreq	r4, r4, #1
	@ 1: CLZ 0
	mov	r0, #0
	clz	r1, r0
	cmp	r1, #32
	orreq	r4, r4, #2
	@ 2: QADD saturates, sets Q
	msr	cpsr_f, #0
	ldr	r0, =0x7FFFFFFF
	mov	r1, #1
	qadd	r2, r0, r1
	mrs	r3, cpsr
	cmp	r2, r0
	tsteq	r3, #0x08000000
	orrne	r4, r4, #4
	@ 3: QSUB
	ldr	r0, =0x80000000
	mov	r1, #1
	qsub	r2, r0, r1
	cmp	r2, r0
	orreq	r4, r4, #8
	@ 4: QDADD: 1 + sat (2 * 0x40000000)
	mov	r0, #1
	mov	r1, #0x40000000
	qdadd	r2, r0, r1
	ldr	r3, =0x7FFFFFFF
	cmp	r2, r3
	orreq	r4, r4, #16
	@ 5: SMULBB: -32768 * 2
	ldr	r0, =0x12348000
	mov	r1, #2
	smulbb	r2, r0, r1
	ldr	r3, =0xFFFF0000
	cmp	r2, r3
	orreq	r4, r4, #32
	@ 6: SMULTB: 0x1234 * 3
	smultb	r2, r0, r1
	mov	r1, #3
	smultb	r2, r0, r1
	ldr	r3, =(0x1234 * 3)
	cmp	r2, r3
	orreq	r4, r4, #64
	@ 7: SMLAWB: (0x00020000 * 0x4000) >> 16 + 5 = 0x8000 + 5
	ldr	r0, =0x00020000
	ldr	r1, =0x00004000
	mov	r2, #5
	smlawb	r3, r0, r1, r2
	ldr	r5, =0x8005
	cmp	r3, r5
	orreq	r4, r4, #128
	@ 8: SMLALBB: 64-bit += -2 * 3
	mov	r0, #0
	mov	r1, #1
	mvn	r2, #1
	mov	r3, #3
	smlalbb	r0, r1, r2, r3
	mvn	r5, #5
	cmp	r0, r5
	cmpeq	r1, #0
	orreq	r4, r4, #256
	@ 9: LDRD / STRD
	ldr	r6, =scratch
	ldr	r0, =0xCAFEBABE
	ldr	r1, =0x0BADF00D
	strd	r0, r1, [r6]
	mov	r0, #0
	mov	r1, #0
	ldrd	r2, r3, [r6]
	ldr	r5, =0xCAFEBABE
	cmp	r2, r5
	ldreq	r5, =0x0BADF00D
	cmpeq	r3, r5
	orreq	r4, r4, #512
	@ 10: BLX to Thumb (immediate), returns 42
	blx	thumb42
	cmp	r0, #42
	orreq	r4, r4, #1024
	@ 11: BLX register
	ldr	r5, =thumb43
	orr	r5, r5, #1
	blx	r5
	cmp	r0, #43
	orreq	r4, r4, #2048
	@ 12: LDR pc interworking, the return by POP {pc} from Thumb
	adr	lr, 1f
	ldr	pc, =thumbpop + 1
1:	cmp	r0, #44
	orreq	r4, r4, #4096
	@ 13: LDM with the base in the list (ARM9: written back if not the last)
	ldr	r6, =scratch
	mov	r0, #7
	str	r0, [r6]
	str	r0, [r6, #4]
	mov	r7, r6
	ldmia	r7!, {r0, r1}
	sub	r7, r7, r6
	cmp	r7, #8
	orreq	r4, r4, #8192
	str	r4, [r11]
	mrs	r0, cpsr
	str	r0, [r11, #4]
	pop	{r4-r11, pc}
	.pool

	.thumb
	.thumb_func
thumb42:
	mov	r0, #42
	bx	lr
	.thumb_func
thumb43:
	mov	r0, #43
	bx	lr
	.thumb_func
thumbpop:
	push	{lr}
	mov	r0, #44
	pop	{pc}

	.bss
	.align 3
scratch: .space 16
