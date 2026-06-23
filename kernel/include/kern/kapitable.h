//
// kapitable.h -- the kernel side of the kapi ABI table (see kern/kapi_abi.h).
//
#ifndef _kern_kapitable_h
#define _kern_kapitable_h

#include <circle/types.h>

// Fill the published function table (call once, before any app runs).
void KApiTableInit (void);

// Physical address of the table page (identity-mapped: PA == kernel VA). Each
// CAddressSpace maps this to KAPI_TABLE_VA so apps reach it at a fixed address.
u64 KApiTablePhys (void);

#endif // _kern_kapitable_h
