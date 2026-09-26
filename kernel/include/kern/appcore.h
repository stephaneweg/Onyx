//
// appcore.h -- cores 2 and 3 as a resource an app can acquire ("app cores").
//
// The scheduler, the interrupts and every task stay on core 0, core 1 is the sound
// producer. An app may acquire core 2 or 3 (kapi_core_acquire), then run one function of
// ITS OWN code there (kapi_core_run): the core switches to the app's address space and
// calls fn (arg) on a stack the app provides, until fn returns. The code on that core
// makes no kapi call and does not allocate (the kernel and newlib's malloc are not
// multi-core safe): it computes and talks to the app's main thread through memory.
// See docs/02 (App cores).
//
#ifndef _kern_appcore_h
#define _kern_appcore_h

#include <circle/types.h>

struct TTrapFrame;
class CAddressSpace;

#define APPCORE_FIRST	2
#define APPCORE_LAST	3

// kapi_core_state() values
#define CORE_IDLE	0		// acquired, nothing running (fn returned)
#define CORE_RUNNING	1		// fn running (or about to start)
#define CORE_NOTYOURS	(-1)		// not acquired by the caller
#define CORE_FAULT	(-2)		// fn made a fault (bad access...): stopped

void AppCoreMain (unsigned nCore);			// cores 2, 3: from COnyxCores::Run
boolean AppCoreOnIRQExit (TTrapFrame *pFrame);		// secondary-core IRQ exit (TRUE: handled)
boolean AppCoreOnFault (TTrapFrame *pFrame);		// secondary-core sync exception (TRUE: handled)

// Stop and free the cores an address space holds (its teardown). FALSE if a core did not
// answer (its code masked the interrupts): the space must then NOT be freed.
boolean AppCoreReleaseAS (CAddressSpace *pAS);

// kapi (C linkage, like every kapi_* entry of the table)
extern "C" {
int  kapi_core_acquire (void);
int  kapi_core_run (int nCore, void (*pFunc) (void *), void *pArg, void *pStackTop);
int  kapi_core_state (int nCore);
void kapi_core_release (int nCore);
}

#endif
