/*
 * lib.ld -- an Onyx shared library (docs/SHARED-LIBS-PLAN.md): a position-independent image linked
 * at 0 (the kernel places it), two LOAD segments as user.ld's -- read + execute (the code, the
 * read-only data, the dynamic tables) and read + write (the data the kernel relocates once, the
 * export table, the vtables, .bss) -- on separate 64 KB pages. The ELF entry is the export table
 * (ld -e onyx_lib_table): the kernel finds it without a symbol table.
 */
PHDRS
{
	text PT_LOAD FILEHDR PHDRS FLAGS (5);	/* PF_R | PF_X */
	data PT_LOAD FLAGS (6);			/* PF_R | PF_W */
	dynamic PT_DYNAMIC FLAGS (6);
}

SECTIONS
{
	. = SIZEOF_HEADERS;
	.hash		: { *(.hash) } :text
	.gnu.hash	: { *(.gnu.hash) } :text
	.dynsym		: { *(.dynsym) } :text
	.dynstr		: { *(.dynstr) } :text
	.rela.dyn	: { *(.rela.*) } :text
	.text		: { *(.text*) } :text
	.rodata		: { *(.rodata*) } :text

	. = ALIGN(0x10000);

	.data.rel.ro	: { *(.data.rel.ro*) } :data
	.init_array	: {
		__lib_init_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) .init_array))
		__lib_init_array_end = .;
	} :data
	.dynamic	: { *(.dynamic) } :data :dynamic
	.got		: { *(.got) *(.got.plt) } :data
	.data		: { *(.data*) } :data
	.bss		: { *(.bss*) *(COMMON) } :data

	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.interp) }
}
