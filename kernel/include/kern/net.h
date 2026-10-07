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
// pid so NetCloseByPid() can reclaim leaked connections when a process dies; send /
// recv / close / accept work only on the calling process's own sockets.
int   NetTcpConnect (const char *pHost, unsigned nPort, unsigned nOwnerPid); // >=0 / <0
int   NetTcpSend    (int hSock, const void *pBuf, unsigned nLen);
int   NetTcpRecv    (int hSock, void *pBuf, unsigned nLen);	// non-blocking; 0 = nothing
void  NetTcpClose   (int hSock);
int   NetTcpListen  (unsigned nPort, unsigned nOwnerPid);	// listening handle >=0 / <0
int   NetTcpAccept  (int hListen, char *pIPOut, unsigned nIPLen, unsigned nOwnerPid); // blocks
int   NetStatus     (char *pIPOut, unsigned nIPLen);		// 1 up / 0 down; fills dotted IP
// (kapi v80) The bytes a process's sockets sent and received, its open sockets; pid 0: every process.
int   NetStats (unsigned nPid, u64 *pRx, u64 *pTx, unsigned *pSockets);
void  NetCloseByPid (unsigned nPid);				// reclaim a dead process's sockets
int   NetResolve    (const char *pHost, char *pIPOut, unsigned nIPLen);	// 1 / 0
int   NetPing       (const char *pHost, unsigned nSeq, unsigned nTimeoutMs,
		     char *pIPOut, unsigned nIPLen);		// RTT us, or <0
int   NetInfo       (char *pBuf, unsigned nCap);		// netstat text
unsigned NetSocketOwner (int hSock);				// its owner pid, 0 = none
boolean  NetSocketAdopt (int hSock, unsigned nFrom, unsigned nTo);	// owner nFrom -> nTo

// ---- BSD sockets (kapi v75, sys/bsdsock.cpp; docs/POSIX-PLAN.md §3.3) --------------------
// The same table as the tcp_* handles (MAX_SOCKETS 256, every process). These calls never wait
// (-KAPI_EAGAIN: not now); bsdsock.cpp waits on kern/iowait.h. Results >= 0, or -KAPI_E*.
// nPid: the caller (NetCurrentPid); another process's socket is -KAPI_EBADF.
enum				// a socket's state
{
	NET_SS_FREE = 0, NET_SS_NEW, NET_SS_BOUND, NET_SS_LISTEN, NET_SS_CONNECTING,
	NET_SS_CONNECTED, NET_SS_FAILED
};
struct TNetSockView		// what core 0 reads of a socket (NetSockView)
{
	int	 nType;			// KAPI_SOCK_STREAM / KAPI_SOCK_DGRAM
	unsigned nState;		// NET_SS_*
	boolean	 bNonBlock, bBroadcast;
	unsigned nRcvTimeoutMs, nSndTimeoutMs;
	u16	 nLocalPort, nPeerPort;	// nPeerPort 0: no peer (UDP: no default)
	u8	 PeerIP[4];
	unsigned nCarry;		// bytes in its carry buffer
};
unsigned NetCurrentPid (void);
extern "C" void SocketAdopt (int hSock);	// (sys/kapi.cpp) a descendant of its owner adopts it
int   NetSockOpen   (int nType, boolean bNonBlock, unsigned nPid);	// -ENETDOWN / -ENFILE
int   NetSockView   (int h, unsigned nPid, TNetSockView *pView);
int   NetSockSetOpt (int h, unsigned nPid, int nOpt, int nValue);	// KAPI_SO_NONBLOCK/_TIMEO/_BROADCAST
int   NetSockTakeError (int h, unsigned nPid);			// the pending error (positive), cleared
int   NetSockShutdown (int h, unsigned nPid, int nHow);
unsigned NetSockPoll (int h, unsigned nPid);			// KAPI_POLL* now (NVAL: not the caller's)
boolean NetSockWakesOnChange (void);	// TRUE: a readiness change calls IoWake (else poll each tick)
int   NetSockBind   (int h, unsigned nPid, unsigned nPort);
int   NetSockListen (int h, unsigned nPid, int nBacklog);
int   NetSockConnect (int h, unsigned nPid, const u8 *pIP, unsigned nPort);	// TCP: -EINPROGRESS
int   NetSockAccept (int h, unsigned nPid, boolean bNonBlock, u8 *pIP, u16 *pPort);
int   NetSockSend   (int h, unsigned nPid, const void *pBuf, unsigned nLen, const u8 *pIP, unsigned nPort);
int   NetSockRecv   (int h, unsigned nPid, void *pBuf, unsigned nLen, unsigned nFlags, u8 *pIP, u16 *pPort);
int   NetSockClose  (int h, unsigned nPid);
void  NetOwnIP      (u8 *pIP);				// 0.0.0.0 while down

// Wi-Fi scan (kapi_wlan_scan, ABI v45): ~3 s, fills pOut strongest first; returns the count.
struct kapi_wlan_ap;
int   NetWlanScan (struct kapi_wlan_ap *pOut, int nMax);
int   NetWlanReconnect (void);			// wpa_supplicant.conf read again, DHCP again (v60)

// ---- The network core (cmdline netcore=1) ------------------------------------
// The stack runs on core 3 with its own scheduler; the Net* calls above post it requests
// (sys/net.cpp). Core 3 is then no longer an app core.
class CTask;
extern volatile boolean g_bNetCore;
void  NetCoreMain (void);			// core 3 (COnyxCores::Run): waits for the start
void  NetCoreStart (CTask *(*pfnBringup) (void));	// core 0: bring the stack up on core 3
void  NetCoreNotify (const char *pTitle, const char *pText);	// net core: a notice for core 0
void  NetCorePoll (void);			// core 0, now and then: deliver it (IpcNotify)
// The net core's inter-core interrupt to core 0: a socket's readiness changed (the waiters of the
// BSD calls are woken at once, not at the next 100 Hz tick). IPI_USER + 0 is the app cores' stop.
#define IPI_NET_READY	(IPI_USER + 1)
void  NetReadyIPI (void);			// core 0, IRQ (COnyxCores::IPIHandler)
// The Wi-Fi driver's switches: the scan's names read from wpa_supplicant's configuration (before
// the supplicant starts, and at each reconnect), and cmdline netstat=1 (the pace in the log).
void  NetWlanNames (const char *pConfigFile);
void  NetWlanOptions (boolean bStat);
// A one-boot trial of the driver's switches (SD:/etc/net-trial.txt, deleted when read), ended by
// a restart: NetTrialLoad from the bring-up task, NetTrialPoll from core 0's main task.
void  NetTrialLoad (void);
void  NetTrialPoll (void);

#endif
