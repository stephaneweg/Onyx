//
// procx.h -- a process's POSIX side: its argv and environment blocks, spawn_ex / proc_wait,
// getpid, the clock sample, sleep_us (kapi v75, docs/POSIX-PLAN.md §3.2, docs/02 §8 "v75: files
// and processes").
//
// Every process gets a TProcInfo when its task is made (kernel.cpp: LaunchApp, ExecPath,
// SpawnProcess -- in the spawner's context) and installs it into its address space when that
// exists (CUserProcessTask::Run). The blocks are "a\0b\0\0" strings (argv[0] first), at most
// PROCX_BLOCK_MAX bytes each, on the kernel heap. The environment a child gets:
//  - spawn_ex: the one given, or the caller's initial environment;
//  - spawn / exec / exec_as (kapi_spawn, kapi_exec...): the spawner's initial environment;
//  - a desktop launch (LaunchApp) or a process the kernel starts (init): the system default,
//    read at boot from SD:/etc/environment ("KEY=VALUE" lines; without it HOME=SD:/home,
//    PATH=SD:/bin, TMPDIR=RAM:/tmp, LANG=C.UTF-8).
// (The term reason a waiter reads lives in CAddressSpace: SetTermReason / GetTermReason; the
// teardown copies it into the spawn record, CProcess.)
//
// Owner: WP-FILE/PROC.
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
#ifndef _kern_procx_h
#define _kern_procx_h

#include <circle/types.h>

#define PROCX_BLOCK_MAX		(64 * 1024)	// an argv or environment block, its final NUL included

class CAddressSpace;
struct TProcInfo;

// A new process's POSIX side, made in the spawner's context: argv = pPath then pArgs split as
// a shell does (blanks, "double quotes"); the environment: the spawner's initial one if
// bInherit (and the spawner has one), else the system default. 0: out of memory.
TProcInfo *ProcInfoNew (const char *pPath, const char *pArgs, boolean bInherit);

// The child's space made (CUserProcessTask::Run): pInfo (0 allowed) is its own from now on, freed
// by its teardown; its spawn record learns its pid.
void ProcInfoInstall (CAddressSpace *pAS, TProcInfo *pInfo);

// A TProcInfo never installed (the task could not start).
void ProcInfoFree (TProcInfo *pInfo);

// The space ends (~CAddressSpace, before its spawn record is marked done): the term reason and
// the pid copied into the record (a FAULT / KILLED / OOM end: the exit status is the reason's
// code), the TProcInfo freed (CAddressSpace::m_pProcInfo, 0 if none), the waiters woken.
void ProcInfoTeardown (CAddressSpace *pAS);

// Boot (CKernel::StartAutostart): the system default environment read from SD:/etc/environment.
void ProcInfoBootInit (void);

#endif // _kern_procx_h
