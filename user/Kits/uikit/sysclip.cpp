//
// sysclip.cpp -- SystemKit for UIKit's text fields (sysclip.h). In the shared library: SystemKit's import
// stubs are linked in, its table is opened here the first time; elsewhere (a PC build) the clipboard's
// code is in the program.
//
#include "uikit.h"
#include "sysclip.h"
#ifdef ONYX_LIB_BUILD
#include "lib.h"
extern "C" {
const void *onyx_systemkit_table;			// the import stubs' table (lib/systemkit_stubs.o, linked in)
const TLibImports *onyx_lib_imports (void);		// (librt.cpp: what the program gave this library)
}
namespace uikit {
bool uk_clip_ready ()
{
	if (onyx_systemkit_table == 0)
	{
		int err = 0;
		const TLibHeader *t = (const TLibHeader *) kapi_lib_open ("systemkit", 1, &err);
		if (t != 0)
		{
			TLibImports imp = *onyx_lib_imports ();		// (the program's allocator: one heap; none of UIKit's variables)
			imp.size = sizeof imp; imp.ndata = 0; imp.data = 0;
			if (t->init (&imp) >= 0) onyx_systemkit_table = t;
		}
	}
	return onyx_systemkit_table != 0;
}
}
#else
namespace uikit { bool uk_clip_ready () { return true; } }
#endif
