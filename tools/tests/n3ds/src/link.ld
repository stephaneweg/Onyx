/*
 * link.ld -- the 3DS core's test programs: an ELF at the console's program address (0x00100000), its code, its
 * constants and its data each on their own pages (the loader maps a segment's pages with its rights).
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS (5);		/* R X */
	rodata PT_LOAD FLAGS (4);	/* R */
	data PT_LOAD FLAGS (6);		/* R W */
}

SECTIONS
{
	. = 0x00100000;
	.text : { *(.text.start) *(.text*) *(.glue_7*) *(.vfp11_veneer) *(.v4_bx) } :text
	. = ALIGN (0x1000);
	.rodata : { *(.rodata*) } :rodata
	. = ALIGN (0x1000);
	.data : { *(.data*) } :data
	.bss : { *(.bss*) *(COMMON) } :data
	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.ARM.attributes) *(.comment) *(.note*) }
}
