// host stub of FatFs ff.h (tools/tests/run_ipc_test.sh): the types the kernel headers name
#ifndef _fatfs_ff_h
#define _fatfs_ff_h
typedef unsigned UINT;
typedef int FRESULT;
typedef struct { int dummy; unsigned long *cltbl; } FIL;
typedef struct { int dummy; } DIR;
FRESULT f_close (FIL *);
FRESULT f_closedir (DIR *);
#endif
