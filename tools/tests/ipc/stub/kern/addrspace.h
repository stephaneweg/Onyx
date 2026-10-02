// host stub of kern/addrspace.h (tools/tests/run_ipc_test.sh): a fake process = a handle table + pid
#ifndef _kern_addrspace_h
#define _kern_addrspace_h
#include <kern/handle.h>
class CAddressSpace
{
public:
	CHandleTable *GetHandles (void)		{ return &m_Handles; }
	unsigned GetPid (void) const		{ return m_nPid; }
	unsigned m_nPid;
	CHandleTable m_Handles;
};
#endif
