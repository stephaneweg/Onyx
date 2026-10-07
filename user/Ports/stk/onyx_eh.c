/*
 * onyx_eh.c -- C++ exceptions for the SuperTuxKart port (user/Ports/stk/).
 *
 * libgcc's DWARF unwinder finds a frame's unwind info in the objects registered with
 * __register_frame_info. A hosted link does that from crtbegin.o (frame_dummy); the Onyx apps link
 * with -nostartfiles (crt0libc.S is the entry), so this constructor does it -- priority 101: the
 * first of the .init_array (stk.ld sorts it), before any C++ constructor that might throw.
 * stk.ld keeps .eh_frame, names its start and ends it with a zero word.
 */
extern char __onyx_eh_frame_start[];
extern void __register_frame_info (const void *begin, void *object);

/* libgcc's `struct object' (unwind-dw2-fde.h): 6 pointer-sized words; a little more is harmless. */
static void *s_ehObject[8];

__attribute__((constructor(101)))
static void onyx_eh_register (void)
{
	__register_frame_info (__onyx_eh_frame_start, s_ehObject);
}
