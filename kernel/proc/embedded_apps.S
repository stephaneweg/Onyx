/*
 * embedded_apps.S -- embed userland ELF images into the kernel via .incbin, so the
 * ELF loader can launch them without the SD card (SD loading comes later). Paths
 * are relative to the kernel/ build directory; the user ELFs must be built first
 * (see kernel/Makefile dependency on ../user).
 */
	.section .rodata
	.balign	16

	.globl	hello_elf_begin
hello_elf_begin:
	.incbin	"../user/hello.elf"
	.globl	hello_elf_end
hello_elf_end:

	.balign	16
	.globl	demoA_elf_begin
demoA_elf_begin:
	.incbin	"../user/demoA.elf"
	.globl	demoA_elf_end
demoA_elf_end:

	.balign	16
	.globl	demoB_elf_begin
demoB_elf_begin:
	.incbin	"../user/demoB.elf"
	.globl	demoB_elf_end
demoB_elf_end:
