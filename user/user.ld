/*
 * user.ld -- link userland ELF programs at USER_VA_BASE (8 GB), matching
 * kernel/include/kern/layout.h. Built with -z max-page-size=0x10000 so the RX and
 * RW LOAD segments land on separate 64 KB pages (our loader maps whole 64 KB pages
 * and assumes segments don't share one).
 */
ENTRY(_start)

/* Apps call the kernel through the kapi ABI table at a fixed VA (kern/kapi_abi.h),
 * so there are no kernel symbols to resolve at link time anymore. */

SECTIONS
{
	. = 0x200000000;		/* 8 GB = USER_VA_BASE */

	.text : {
		*(.text._start)
		*(.text*)
	}
	.rodata : {
		*(.rodata*)
		/* C++ global constructors / destructors (empty for C apps). crt0.S walks
		 * [__init_array_start, __init_array_end) before main. */
		. = ALIGN(8);
		__init_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) .init_array))
		__init_array_end = .;
		__fini_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) .fini_array))
		__fini_array_end = .;
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
