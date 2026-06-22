/*
 * user.ld -- link userland ELF programs at USER_VA_BASE (8 GB), matching
 * kernel/include/kern/layout.h. Built with -z max-page-size=0x10000 so the RX and
 * RW LOAD segments land on separate 64 KB pages (our loader maps whole 64 KB pages
 * and assumes segments don't share one).
 */
ENTRY(_start)

SECTIONS
{
	. = 0x200000000;		/* 8 GB = USER_VA_BASE */

	.text : {
		*(.text._start)
		*(.text*)
	}
	.rodata : {
		*(.rodata*)
	}

	. = ALIGN(0x10000);

	.data : {
		*(.data*)
	}
	.bss : {
		*(.bss*)
		*(COMMON)
	}

	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
