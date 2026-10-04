//
// wtk/global.h -- wtk's global variables (the palette, the installed text face: wtk/globals.inc).
//
// They are the PROGRAM's variables: a program is linked at a fixed address and reads them directly
// (C_BG, wk_face_...), also from the toolkit's inline code compiled into it. When wtk is the shared
// library SD:/lib/wtk.so (ONYX_LIB_BUILD; docs/SHARED-LIBS-PLAN.md) its code cannot name a
// program's address: each variable is then a REFERENCE, bound when the library starts in a process
// to the address the program handed over (user/lib.h TLibImports.data, in globals.inc's order) --
// or to a copy of the library's own for a variable added after the program was built.
//
#ifndef _wtk_global_h
#define _wtk_global_h

#ifdef ONYX_LIB_BUILD
#define WTK_VAR(type, name)	extern type &name
#else
#define WTK_VAR(type, name)	extern type name
#endif

#endif
