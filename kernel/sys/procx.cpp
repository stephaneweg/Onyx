//
// procx.cpp -- a process's POSIX side: spawn_ex / proc_wait, get_argv / get_env, getpid,
// clock_info, sleep_us (kapi v75 slots 222..228, docs/POSIX-PLAN.md §3.2).
//
// Owner: WP-FILE/PROC. This is the WP-0 skeleton: every entry returns -KAPI_ENOSYS and the hooks
// do nothing, until WP-FILE/PROC lands.
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
#include <kern/procx.h>
#include <kern/addrspace.h>
#include <kern/kapi_abi.h>

void ProcInfoTeardown (CAddressSpace *pAS)
{
	(void) pAS;				// (no TProcInfo yet: m_pProcInfo stays 0)
}

extern "C" {

long long kapi_spawn_ex (const struct kapi_spawn_attr *pAttr)
{
	return -KAPI_ENOSYS;
}

int kapi_proc_wait (void *hProc, unsigned nFlags, struct kapi_proc_status *pOut)
{
	return -KAPI_ENOSYS;
}

int kapi_get_argv (char *pBuf, unsigned nCap)
{
	return -KAPI_ENOSYS;
}

int kapi_get_env (char *pBuf, unsigned nCap)
{
	return -KAPI_ENOSYS;
}

int kapi_getpid (int nWhich)
{
	return -KAPI_ENOSYS;
}

int kapi_clock_info (struct kapi_clock_info *pOut)
{
	return -KAPI_ENOSYS;
}

int kapi_sleep_us (unsigned long long nMicros)
{
	return -KAPI_ENOSYS;
}

}
