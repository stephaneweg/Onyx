//
// appkit.c -- AppKit (SD:/lib/appkit.so): the ONE interface between the programs and the kernel.
//
// Every kapi_* function a program calls is here, exported BY NAME (appkit/appkit.abi, append-only):
// appkit.h declares them, appkit_calls.inc (included below) has their bodies, each one calling the
// kernel through the kernel's table (KT, at KAPI_TABLE_VA). Nothing else reads that table: when the
// kernel's table is restructured -- an entry moved, removed, two merged, a structure changed --, the
// bodies of appkit_calls.inc (or a function written here for the call that needs more than a relay)
// are adapted and AppKit is rebuilt; no program is.
//
// The kernel loads it by itself when the first program starts, maps it into every program and copies
// its table where the programs' stubs read it (APPKIT_TABLE_VA, kern/kapi_abi.h): a program does
// nothing to have it. It has no state: no memory of its own, no constructor. It is shipped with the
// kernel (the package onyx); a new AppKit is the next start's.
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
#define KAPI_IMPL
#include "appkit.h"
#include "appkit_calls.inc"
#include "appkit_lib.inc"
#include "lib.h"

// (the libraries' table has an init: nothing to do -- the kernel does not call it, no program does)
int appkit_init (const struct TLibImports *imp)
{
	(void) imp;
	return 0;
}
