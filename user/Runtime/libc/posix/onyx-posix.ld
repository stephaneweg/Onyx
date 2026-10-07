/*
 * onyx-posix.ld -- the link of a program built on libonyxposix (docs/POSIX-PLAN.md §3.4).
 *
 * user.ld's layout (linked at USER_VA_BASE, 8 GB; an RX and an RW LOAD segment on 64 KB pages),
 * plus:
 *  - the unwind tables kept (.eh_frame, ended by a zero word, its start named
 *    __onyx_eh_frame_start for start.c, which registers it) and .gcc_except_table: C++
 *    exceptions work (as user/Ports/stk/stk.ld);
 *  - a PT_TLS segment (.tdata / .tbss) at the start of the RW segment, its image and sizes named
 *    for tls.c (__onyx_tdata_start, __onyx_tdata_size, __onyx_tls_size, __onyx_tls_align) --
 *    native TLS under WP-TC's toolchain; empty under the interim one (emutls);
 *  - the main thread's TLS block reserved at the end of .bss (__onyx_main_tls_area, when the
 *    program has TLS).
 * The kernel's loader maps the PT_LOAD segments only.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS (5);		/* PF_R | PF_X */
	data PT_LOAD FLAGS (6);		/* PF_R | PF_W */
	tls PT_TLS;
}

SECTIONS
{
	. = 0x200000000;		/* 8 GB = USER_VA_BASE */

	.text : {
		*(.text._start)
		*(.text.unlikely .text.unlikely.*)
		*(.text .text.*)
	} :text
	.rodata : {
		*(.rodata .rodata.*)
		. = ALIGN(8);
		__init_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
		KEEP(*(.init_array .ctors))
		__init_array_end = .;
		__fini_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
		KEEP(*(.fini_array .dtors))
		__fini_array_end = .;
	} :text
	.gcc_except_table : {
		*(.gcc_except_table .gcc_except_table.*)
	} :text
	.eh_frame : {
		__onyx_eh_frame_start = .;
		KEEP(*(.eh_frame))
		LONG(0)			/* the terminator crtend.o would add */
	} :text

	. = ALIGN(0x10000);

	.tdata : {
		__onyx_tdata_start = .;
		*(.tdata .tdata.* .gnu.linkonce.td.*)
		__onyx_tdata_end = .;
	} :data :tls
	.tbss : {
		__onyx_tbss_start = .;
		*(.tbss .tbss.* .gnu.linkonce.tb.*)
		*(.tcommon)
		__onyx_tbss_end = .;
	} :data :tls
	__onyx_tdata_size = __onyx_tdata_end - __onyx_tdata_start;
	__onyx_tls_size = __onyx_tbss_end - __onyx_tdata_start;
	__onyx_tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss));
	__onyx_main_tls_reserve = __onyx_tls_size > 0 ? 8192 + __onyx_tls_size + __onyx_tls_align : 0;

	.data : {
		*(.data .data.*)
		*(.data.rel.ro .data.rel.ro.*)
		*(.got) *(.got.plt)
	} :data
	.bss : {
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(64);
		__onyx_main_tls_area = .;
		. += __onyx_main_tls_reserve;
	} :data

	/DISCARD/ : { *(.comment) *(.note*) }
}
