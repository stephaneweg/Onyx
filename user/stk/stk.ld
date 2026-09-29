/*
 * stk.ld -- the link of the SuperTuxKart port's programs (user/stk/): user.ld plus what full C++
 * needs. user.ld throws the unwind tables away (/DISCARD/ .eh_frame): the Onyx apps are built
 * with -fno-exceptions. SuperTuxKart throws and catches (~110 throw, ~180 try) and uses RTTI
 * (~270 dynamic_cast), so here .eh_frame and .gcc_except_table are kept, the .eh_frame is ended
 * by a zero word (what crtend.o's __FRAME_END__ gives a hosted link) and its start is named for
 * onyx_eh.c, which registers it with libgcc's unwinder (__register_frame_info) before the
 * constructors run -- we link with -nostartfiles, so crtbegin.o's frame_dummy never does it.
 *
 * Same memory layout as user.ld: linked at USER_VA_BASE (8 GB), two LOAD segments (RX, RW) on
 * 64 KB pages.
 */
ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS (5);		/* PF_R | PF_X */
	data PT_LOAD FLAGS (6);		/* PF_R | PF_W */
}

SECTIONS
{
	. = 0x200000000;		/* 8 GB = USER_VA_BASE */

	.text : {
		*(.text._start)
		*(.text*)
	} :text
	.rodata : {
		*(.rodata*)
		. = ALIGN(8);
		__init_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) .init_array))
		__init_array_end = .;
		__fini_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) .fini_array))
		__fini_array_end = .;
	} :text
	.gcc_except_table : {
		*(.gcc_except_table*)
	} :text
	.eh_frame : {
		__onyx_eh_frame_start = .;
		KEEP(*(.eh_frame))
		LONG(0)			/* the terminator crtend.o would add */
	} :text

	. = ALIGN(0x10000);

	.data : {
		*(.data*)
		*(.data.rel.ro*)
		*(.got) *(.got.plt)
	} :data
	.bss : {
		*(.bss*)
		*(COMMON)
	} :data

	/DISCARD/ : { *(.comment) *(.note*) }
}
