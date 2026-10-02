//
// iowait.h -- the shared readiness wait (kapi v75, docs/POSIX-PLAN.md §3): one I/O generation
// counter for the whole system, bumped by anything that may make an I/O object ready (a pipe
// written or drained, a socket's state changed, a connect completed), and a wait until it moves.
//
// A waiter (poll, a blocking accept, a pipe's reader) reads IoGen, checks its objects, and if
// none is ready sleeps in IoWait until the generation is no longer the one it read, or the
// timeout: then it checks again. A wake may be for another object (spurious wakes are the
// rule: the waiter always re-checks); none is lost, because the generation read before the
// check makes a wake in between return at once.
//
// Core 0 only (the kapis run there, the timer IRQ too). IoWake may be called from an IRQ.
// The tick hooks are how code without an IRQ of its own feeds the generation: the network core's
// readiness snapshot (WP-NET), the app cores' page-in requests (WP-MEM) -- each hook runs at
// every 100 Hz tick, in the timer interrupt, and calls IoWake (or sets its own event).
//
// Owner: WP-0 (shared by WP-MEM, WP-FILE/PROC and WP-NET; change it only together).
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
#ifndef _kern_iowait_h
#define _kern_iowait_h

#include <circle/types.h>

#define IOWAIT_TICK_HOOKS	8		// IoWaitAddTickHook's capacity

// The I/O generation (read it BEFORE checking the objects).
u32  IoGen (void);

// ++generation, and every IoWait sleeper woken (core 0; IRQ context allowed).
void IoWake (void);

// Sleep while IoGen () == nGen, at most nTimeoutMs (KAPI_WAIT_FOREVER: no limit; 0: only
// check) -> 0 the generation changed, 1 the timeout. Core 0, task context (it yields).
int  IoWait (u32 nGen, unsigned nTimeoutMs);

// pfn called at each 100 Hz tick (IRQ, core 0), up to IOWAIT_TICK_HOOKS hooks (at boot;
// FALSE: no room). A hook must be short and must not block.
boolean IoWaitAddTickHook (void (*pfn) (void));

// The periodic tick (exception.cpp's PeriodicTick, next to WordWaitTick): runs the hooks.
void IoWaitTick (void);

#endif // _kern_iowait_h
