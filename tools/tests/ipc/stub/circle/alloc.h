// host stub of Circle alloc.h (tools/tests/run_ipc_test.sh): 64 KB pages from the C heap
#ifndef _circle_alloc_h
#define _circle_alloc_h
void *palloc_high (void);
void pfree (void *pPage);
#endif
