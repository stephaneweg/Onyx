//
// systemkit.cpp -- SystemKit (SD:/lib/systemkit.so): its headers' code, compiled once for the system.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#define SK_IMPL
#define FS_INLINE				// (the few file helpers it needs: its own copy, no library opened)
#include "onyxpp.hpp"
#include "lib.h"
#include "wallpaper.h"
#include "volume.h"
#include "trash.h"
#include "preloadini.h"
#include "fileassoc.h"
#include "notify.h"
#include "clipboard.h"
#include "dockconf.h"
#include "wallpaper.inc"
#include "volume.inc"
#include "trash.inc"
#include "preloadini.inc"
#include "fileassoc.inc"
#include "notify.inc"
#include "clipboard.inc"
#include "dockconf.inc"

// (the library's table: its init -- the library runtime's, Runtime/librt.cpp)
extern "C" int onyx_lib_init (const TLibImports *imp);
extern "C" int sk_lib_init (const TLibImports *imp)
{
	return onyx_lib_init (imp) < 0 ? -1 : 0;
}
