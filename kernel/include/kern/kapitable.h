//
// kapitable.h -- the kernel side of the kapi ABI table (see kern/kapi_abi.h).
//
#ifndef _kern_kapitable_h
#define _kern_kapitable_h

#include <circle/types.h>

// Fill the published function table (call once, before any app runs).
void KApiTableInit (void);

// The kernel's table: the system calls' entries (sys/el0.cpp dispatches "svc #0" through it,
// El0Init builds the apps' EL0 table from it). Kernel memory, never mapped into an app's space.
struct TKApiTable;
const TKApiTable *KApiKernelTable (void);

#endif // _kern_kapitable_h
