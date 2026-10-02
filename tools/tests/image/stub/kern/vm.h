// host stub of kern/vm.h (tools/tests/run_image_test.sh): what the image code calls
#ifndef _kern_vm_h
#define _kern_vm_h
#include <circle/types.h>
class CAddressSpace;
void VmNoteRegion (CAddressSpace *pAS, u64 ulStart, u64 ulEnd, unsigned nProt, unsigned nKind);
boolean VmCommitOK (u64 nBytes);
void WordWaitsZap (u64 ulPhys, u64 nLen);
#endif
