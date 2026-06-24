/*
 * user.ld -- link userland ELF programs at USER_VA_BASE (8 GB), matching
 * kernel/include/kern/layout.h. Built with -z max-page-size=0x10000 so the RX and
 * RW LOAD segments land on separate 64 KB pages (our loader maps whole 64 KB pages
 * and assumes segments don't share one).
 *
 * Explicit PHDRS force exactly two LOAD segments -- a read/exec one (text + rodata)
 * and a read/write one (data + bss). This matters for C++ apps whose writable data
 * is entirely zero-init (.bss, no .data): without a dedicated RW segment the linker
 * would fold .bss into the RX segment, and the kernel loader (which maps a segment
 * RO when PF_X is set) would map it read-only -> a write fault on the first global.
 */
ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS (5);		/* PF_R | PF_X */
	data PT_LOAD FLAGS (6);		/* PF_R | PF_W */
}

/* Apps call the kernel through the kapi ABI table at a fixed VA (kern/kapi_abi.h),
 * so there are no kernel symbols to resolve at link time anymore. */

SECTIONS
{
	. = 0x200000000;		/* 8 GB = USER_VA_BASE */

	.text : {
		*(.text._start)
		*(.text*)
	} :text
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
	} :text

	. = ALIGN(0x10000);

	.data : {
		*(.data*)
		*(.data.rel.ro*)	/* C++ RELRO (vtables etc.); RW here is fine, no relro */
		*(.got) *(.got.plt)
	} :data
	.bss : {
		*(.bss*)
		*(COMMON)
	} :data

	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
