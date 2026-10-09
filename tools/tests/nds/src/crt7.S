@ crt9.S -- the ARM7's start for the core's test programs: .bss cleared, main called, then a halt loop.
@ (The direct boot leaves the stacks in the ARM7 WRAM, system mode, interrupts off.)
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
	mov	lr, pc
	bx	r0
2:	swi	#0x060000
	b	2b
	.pool
