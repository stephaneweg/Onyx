@ crt9.S -- the ARM9's start for the core's test programs: .bss cleared, main called, then a halt loop.
@ (The direct boot leaves the stacks in the DTCM at 0x03000000, system mode, interrupts off.)
	.section .crt0, "ax"
	.arm
	.global _start
_start:
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	mov	r2, #0
1:	cmp	r0, r1
	strlo	r2, [r0], #4
	blo	1b
	ldr	r0, =main
	blx	r0
2:	mcr	p15, 0, r0, c7, c0, 4
	b	2b
	.pool
