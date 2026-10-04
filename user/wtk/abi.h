//
// wtk/abi.h -- wtk as a shared library (SD:/lib/wtk.so; docs/SHARED-LIBS-PLAN.md, docs/03 "Shared
// libraries"): THE RULES THAT KEEP OLD PROGRAMS WORKING when the library changes.
//
// A program built against wtk N must run on wtk N+1 without being rebuilt. Three things tie a
// program to the library, and each one is append-only:
//
//  1. THE EXPORT TABLE (wtk/wtk.abi, generated: tools/libgen/libgen.py): every global function of
//     the toolkit has a slot. A function is never removed and its signature never changes -- a
//     different one is a new function (foo2). New functions are appended by the build.
//
//  2. THE CLASSES' LAYOUT. Programs allocate widgets (new Button), embed them, derive them and read
//     their fields (left, width, canvas.px, hidden...): sizeof and every offset are compiled into
//     them. So: NEVER add, remove, reorder or retype a field of a class of these headers. A new
//     field goes into the reserve -- Widget::reserved_ / ext (every widget has them), Canvas's,
//     Root's -- or behind `ext` (a structure the library allocates). wtk/layout_lock.cpp fails the
//     library's build when a size or an offset moves.
//
//  3. THE VIRTUAL FUNCTIONS' ORDER. A program's vtables are built by its own compiler from these
//     headers. NEVER add, remove or reorder a virtual function. A new overridable takes one of the
//     reserved slots (Widget::wk_reserved0..7, Root::wk_rootReserved0..7): rename it, keep its
//     place; an older program's objects answer with the empty default.
//
// And one consequence of inline code: what is INLINE in these headers (a function's body, a
// constructor's, a default argument, a virtual's default) is compiled into the programs -- a later
// change to it reaches only the programs rebuilt after it. Code that may have to be fixed belongs in
// the .cpp files (an entry of the table), not in the headers.
//
// The global variables (the palette, the text face) are the program's, shared with the library:
// wtk/global.h, wtk/globals.inc (append-only too).
//
// A change that cannot respect these rules is another library: wtk2.so, beside this one.
//
#ifndef _wtk_abi_h
#define _wtk_abi_h
#endif
