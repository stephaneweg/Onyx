//
// vm.h -- an app's virtual memory: lazy regions (VMAs), demand paging, vm_map & co. (kapi v75,
// docs/POSIX-PLAN.md §3.1).
//
// Owner: WP-MEM. WP-0 (the v75 skeleton) only declared the teardown hook; TVmSpace, TVma,
// VmFaultIn and the rest come with WP-MEM.
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
#ifndef _kern_vm_h
#define _kern_vm_h

#include <circle/types.h>

class CAddressSpace;
struct TVmSpace;			// (WP-MEM: the space's regions, pins, deferred zaps, statistics)

// The space ends (~CAddressSpace): its TVmSpace freed (CAddressSpace::m_pVm, 0 if none).
void VmTeardown (CAddressSpace *pAS);

#endif // _kern_vm_h
