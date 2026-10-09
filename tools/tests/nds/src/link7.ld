OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
	. = 0x037F8000;
	.text : { *(.crt0) *(.text*) *(.glue_7*) *(.vfp11_veneer) *(.v4_bx) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	. = ALIGN(4);
	__bss_start = .;
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(4);
	__bss_end = .;
	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
