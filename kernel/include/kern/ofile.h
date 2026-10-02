//
// ofile.h -- open files with POSIX semantics: one node per open file, open-file descriptions
// with their own 64-bit offset (HANDLE_OFILE), stat, unlink / rename of open files (kapi v75,
// docs/POSIX-PLAN.md §3.2).
//
// Owner: WP-FILE/PROC. WP-0 (the v75 skeleton) only declared the close hook.
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#ifndef _kern_ofile_h
#define _kern_ofile_h

#include <circle/types.h>

// Close a HANDLE_OFILE object (HandleObjectClose: file_close, the process's teardown).
// bTeardown: the reaper's teardown, the interrupts masked -- nothing may block or yield.
void OFileClose (void *pObj, boolean bTeardown);

#endif // _kern_ofile_h
