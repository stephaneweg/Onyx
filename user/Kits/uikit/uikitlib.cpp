//
// uikit/uikitlib.cpp -- uikit as the shared library SD:/lib/uikit.so (docs/SHARED-LIBS-PLAN.md): its export
// table's init. The table itself is generated (tools/libgen/libgen.py, uikit/uikit.abi): every global
// function of the toolkit, in an append-only order. Compiled into the library only.
//
#ifdef ONYX_LIB_BUILD
#include "lib.h"

extern "C" {
int onyx_lib_init (const TLibImports *imp);	// (librt.cpp: the imports kept, the constructors run)

// Once per process, before any other entry: the program's allocator and its variables (the palette,
// the text face: uikit/globals.cpp) taken, the toolkit's static constructors run.
int uikit_lib_init (const TLibImports *imp)
{
	return onyx_lib_init (imp) < 0 ? -1 : 0;
}
}
#endif
