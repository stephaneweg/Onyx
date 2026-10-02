//
// vm.cpp -- demand paging, vm_map / vm_unmap / vm_protect / vm_advise / vm_query / vm_stats,
// thread_create_ex / thread_info (kapi v75 slots 199..206, docs/POSIX-PLAN.md §3.1).
//
// Owner: WP-MEM. This is the WP-0 skeleton: every entry returns -KAPI_ENOSYS and the hooks do
// nothing, until WP-MEM lands.
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
#include <kern/vm.h>
#include <kern/addrspace.h>
#include <kern/kapi_abi.h>

void VmTeardown (CAddressSpace *pAS)
{
	(void) pAS;				// (no TVmSpace yet: m_pVm stays 0)
}

extern "C" {

long long kapi_vm_map (unsigned long long ulAddr, unsigned long long ulLen, unsigned nProt, unsigned nFlags)
{
	return -KAPI_ENOSYS;
}

int kapi_vm_unmap (unsigned long long ulAddr, unsigned long long ulLen)
{
	return -KAPI_ENOSYS;
}

int kapi_vm_protect (unsigned long long ulAddr, unsigned long long ulLen, unsigned nProt)
{
	return -KAPI_ENOSYS;
}

int kapi_vm_advise (unsigned long long ulAddr, unsigned long long ulLen, int nAdvice)
{
	return -KAPI_ENOSYS;
}

int kapi_vm_query (unsigned long long ulAddr, struct kapi_vm_region *pOut)
{
	return -KAPI_ENOSYS;
}

int kapi_vm_stats (int nPid, struct kapi_vm_stats *pOut)
{
	return -KAPI_ENOSYS;
}

int kapi_thread_create_ex (const struct kapi_thread_attr *pAttr)
{
	return -KAPI_ENOSYS;
}

int kapi_thread_info (int nTid, struct kapi_thread_info *pOut)
{
	return -KAPI_ENOSYS;
}

}
