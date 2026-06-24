//
// net.h -- kernel network globals + socket kapi helpers.
//
// The network stack (Circle's CNetSubSystem + the BCM4343 WLAN driver + wpa_
// supplicant) is brought up on the primary core in a background task (see
// CNetBringupTask in kernel.cpp). It self-drives via Circle's own CNetTask /
// CPHYTask workers running on our cooperative scheduler -- nothing here needs to
// pump it. These globals let the kapi layer reach the running subsystem.
//
#ifndef _kern_net_h
#define _kern_net_h

#include <circle/types.h>

class CNetSubSystem;

// Set once during boot to the kernel's CNetSubSystem instance (non-null even
// before the link is up). Use NetIsUp() to test whether it is actually usable.
extern CNetSubSystem *g_pNet;

// TRUE once the stack is DHCP-bound (or statically configured) and running.
extern volatile boolean g_bNetUp;

static inline boolean NetIsUp (void) { return g_pNet != 0 && g_bNetUp; }

// ---- Socket kapi backend (implemented in sys/net.cpp) -----------------------
// Thin handle-based TCP wrapper over Circle's CSocket, exposed to apps through
// the kapi table. Handles are small non-negative ints; negative returns are
// errors. See kapi_abi.h for the ABI entry points. Each socket records its owner
// pid so NetCloseByPid() can reclaim leaked connections when a process dies.
int   NetTcpConnect (const char *pHost, unsigned nPort, unsigned nOwnerPid); // >=0 / <0
int   NetTcpSend    (int hSock, const void *pBuf, unsigned nLen);
int   NetTcpRecv    (int hSock, void *pBuf, unsigned nLen);	// non-blocking; 0 = nothing
void  NetTcpClose   (int hSock);
int   NetStatus     (char *pIPOut, unsigned nIPLen);		// 1 up / 0 down; fills dotted IP
void  NetCloseByPid (unsigned nPid);				// reclaim a dead process's sockets

#endif
