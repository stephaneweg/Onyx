//
// net.cpp -- handle-based socket backend for the kapi layer.
//
// Apps cannot hold kernel C++ objects, so they open TCP connections by integer
// HANDLE. Each handle indexes a small table of Circle CSockets; the kapi_tcp_*
// entry points (sys/kapi.cpp) are thin extern-"C" shims over the functions here.
// Calls run on the *app's* task: Connect/DNS block cooperatively (yielding to the
// net stack's own CNetTask), Send blocks with a timeout, Recv is non-blocking.
// A socket records its owner pid so NetCloseByPid() can reclaim it if the process
// dies without closing (force-kill / crash), preventing slot + connection leaks. Only
// its owner may use it (send / recv / close / accept: another process's handle fails as a
// bad one); a descendant of the owner adopts it on first use (kapi.cpp: ftpd hands a
// client's socket to the session process it spawns).
//
// The same table serves the BSD sockets of kapi v75 (sys/bsdsock.cpp, docs/POSIX-PLAN.md §3.3):
// "the slot layer" below (Slot*: TCP and UDP, every call non-blocking; the waits are
// bsdsock.cpp's, on kern/iowait.h). A slot has a state, a pending error (SO_ERROR), a carry
// buffer (what a receive took from Circle that did not fit: no data lost to a small buffer,
// and MSG_PEEK) and a readiness snapshot (nStatus: KAPI_POLL* bits).
//
// With netcore=1 the whole stack runs on core 3 (see "the network core" at the end):
// the Do* / Slot* functions below then run there, on worker tasks, and the Net* entry
// points post them a request from core 0 and wait for its answer.
//
#include <kern/net.h>
#include <kern/iowait.h>
#include <circle/net/socket.h>
#include <circle/net/dnsclient.h>
#include <circle/net/ipaddress.h>
#include <circle/net/in.h>
#include <circle/net/error.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/networklayer.h>
#include <circle/net/checksumcalculator.h>
#include <circle/sched/scheduler.h>
#include <circle/multicore.h>					// SendIPI (the net core -> core 0)
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/string.h>
#include <circle/new.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>	// live association state
#include <wlan/bcm4343.h>					// escan (kapi_wlan_scan)
#include <circle/net/dhcpclient.h>				// Restart (kapi_wlan_reconnect)
#include <circle/netdevice.h>
#include <kern/kapi_abi.h>
#include <fatfs/ff.h>						// wpa_supplicant.conf read for the scan's names
#include <circle/util.h>
#include <circle/types.h>
#include <kern/ipc.h>
#include <kern/addrspace.h>

// 256 since v75 (64 since 2026-09-30, 16 before: a browser keeps a dozen open). Shared by
// every process.
#define MAX_SOCKETS	256

enum { SK_TCP = 1, SK_UDP = 2 };			// TSocketSlot::nType (0: free)

struct TSocketSlot
{
	CSocket  *pSocket;			// 0 free; SLOT_RESERVED taken, no CSocket (yet); else it
	unsigned  nOwnerPid;
	boolean   bListen;			// a listening socket, for NetInfo
	// (v75) the rest: see "the slot layer"
	u32	  nType;			// SK_*
	u32	  nState;			// NET_SS_* (kern/net.h)
	u32	  nStatus;			// the readiness snapshot: KAPI_POLL* (netcore=1)
	u32	  nError;			// pending SO_ERROR (a positive KAPI_E*), 0 none
	u32	  bCancel;			// closed while busy / connecting: the last user frees it
	u32	  nBusy;			// calls inside Circle that may yield (a send)
	u8	  bNonBlock, bShutRd, bShutWr, bEOF;
	u8	  bBroadcast, bBroadcastSet, bPad[2];
	u16	  nLocalPort, nPeerPort;	// nPeerPort: the peer of a TCP connection, a UDP default
	u8	  PeerIP[4];
	unsigned  nRcvTimeoutMs, nSndTimeoutMs;	// 0: none (bsdsock.cpp's waits)
	u8	 *pCarry;			// FRAME_BUFFER_SIZE, at the first receive that needs it
	unsigned  nCarryOff, nCarryLen;
	u8	  CarryIP[4];			// UDP: the carried datagram's sender
	u16	  nCarryPort;
};

static TSocketSlot s_Sockets[MAX_SOCKETS];		// zero-initialised (BSS)

// A slot taken without a CSocket: a connect still under way (its DNS lookup, its handshake: they
// block, and the net core's other workers run meanwhile -- two connects at once got the same
// slot), a new BSD socket, one whose connect failed.
#define SLOT_RESERVED	((CSocket *) 1)

static inline u32 Ld (u32 *p)		{ return __atomic_load_n (p, __ATOMIC_ACQUIRE); }
static inline void St (u32 *p, u32 v)	{ __atomic_store_n (p, v, __ATOMIC_RELEASE); }

// The network core sleeps between its rounds once the network has been quiet this long (us); 0:
// never, it polls on (NetCoreMain). The trial word netsleep=<ms>.
static unsigned s_nSleepIdleUs = 50000;
static unsigned s_nSleepUs = 1000;		// the sleep between two rounds of the quiet network (netsleepus=)
static u32 s_nLastPostUs;			// when core 0 last posted a request (atomic)
extern "C" unsigned onyx_wl_lastact;		// ether4330.c: when a frame was last read or written
extern "C" unsigned onyx_wl_polls;		// ... the times the chip was asked and had nothing
static inline boolean Cas (u32 *p, u32 nFrom, u32 nTo)
{
	return __atomic_compare_exchange_n (p, &nFrom, nTo, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
static inline boolean RealSocket (CSocket *p) { return p != 0 && p != SLOT_RESERVED; }

static void Changed (void);			// a slot's readiness may have changed (the waiters)

// A free slot taken for nPid (atomically: core 0 opens BSD sockets while the net core's
// workers take slots for connects and accepts), its fields cleared; -1 if the table is full.
static int ClaimSlot (unsigned nPid, u32 nType, u32 nState)
{
	for (int i = 0; i < MAX_SOCKETS; i++)
	{
		CSocket *pNull = 0;
		if (   s_Sockets[i].pSocket == 0
		    && __atomic_compare_exchange_n (&s_Sockets[i].pSocket, &pNull, SLOT_RESERVED, FALSE,
						    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		{
			TSocketSlot &s = s_Sockets[i];
			s.bListen = FALSE;
			s.nStatus = 0; s.nError = 0; s.bCancel = 0; s.nBusy = 0;
			s.bNonBlock = s.bShutRd = s.bShutWr = s.bEOF = 0;
			s.bBroadcast = s.bBroadcastSet = 0;
			s.nLocalPort = s.nPeerPort = 0;
			memset (s.PeerIP, 0, sizeof s.PeerIP);
			s.nRcvTimeoutMs = s.nSndTimeoutMs = 0;
			s.pCarry = 0; s.nCarryOff = s.nCarryLen = 0; s.nCarryPort = 0;
			s.nType = nType;
			St (&s.nState, nState);
			__atomic_store_n (&s.nOwnerPid, nPid, __ATOMIC_RELEASE);
			return i;
		}
	}
	return -1;
}

// Slot h given back (its CSocket deleted: the dtor ends the connection). On the stack's core.
static void FreeSlot (int h)
{
	TSocketSlot &s = s_Sockets[h];
	CSocket *p = s.pSocket;
	if (RealSocket (p)) delete p;
	delete [] s.pCarry;
	s.pCarry = 0; s.nCarryLen = 0;
	s.nType = 0;
	St (&s.nState, NET_SS_FREE);
	St (&s.nStatus, 0);
	__atomic_store_n (&s.nOwnerPid, 0, __ATOMIC_RELEASE);
	__atomic_store_n (&s.pSocket, (CSocket *) 0, __ATOMIC_RELEASE);
	Changed ();
}

// ---- a DNS cache: a page's resources ask for the same few hosts again and again ----
#define DNS_CACHE	32
#define DNS_CACHE_TTL	300		// seconds (the answer's own TTL is not read)

struct TDNSCacheEntry
{
	char	 szName[128];
	u8	 IP[4];
	unsigned nExpires;			// CTimer::GetUptime () seconds
};

static TDNSCacheEntry s_DNSCache[DNS_CACHE];
static unsigned s_nDNSNext;

static boolean LookupCached (const char *pHost, CIPAddress *pIP)
{
	unsigned nNow = CTimer::Get ()->GetUptime ();
	for (unsigned i = 0; i < DNS_CACHE; i++)
	{
		TDNSCacheEntry &e = s_DNSCache[i];
		if (e.szName[0] != '\0' && nNow < e.nExpires && strcasecmp (e.szName, pHost) == 0)
		{
			pIP->Set (e.IP);
			return TRUE;
		}
	}
	return FALSE;
}

static void RememberHost (const char *pHost, const CIPAddress &IP)
{
	if (strlen (pHost) >= sizeof s_DNSCache[0].szName) return;
	TDNSCacheEntry &e = s_DNSCache[s_nDNSNext++ % DNS_CACHE];
	strcpy (e.szName, pHost);
	IP.CopyTo (e.IP);
	e.nExpires = CTimer::Get ()->GetUptime () + DNS_CACHE_TTL;
}

// A host name to its address: the cache, else a DNS lookup (blocks), remembered.
static boolean ResolveName (const char *pHost, CIPAddress *pIP)
{
	if (LookupCached (pHost, pIP)) return TRUE;
	CDNSClient DNS (g_pNet);
	if (!DNS.Resolve (pHost, pIP)) return FALSE;
	RememberHost (pHost, *pIP);
	return TRUE;
}

// Parse "a.b.c.d" into four bytes. Returns TRUE only for a well-formed dotted quad
// (so a hostname like "irc.libera.chat" falls through to DNS).
static boolean ParseDottedIP (const char *s, u8 ip[4])
{
	unsigned nOctet = 0, nVal = 0, nDigits = 0;
	for (const char *p = s; ; p++)
	{
		if (*p >= '0' && *p <= '9')
		{
			nVal = nVal * 10 + (unsigned) (*p - '0');
			if (nVal > 255 || ++nDigits > 3) return FALSE;
		}
		else if (*p == '.' || *p == '\0')
		{
			if (nDigits == 0 || nOctet >= 4) return FALSE;
			ip[nOctet++] = (u8) nVal;
			nVal = 0; nDigits = 0;
			if (*p == '\0') break;
		}
		else return FALSE;
	}
	return nOctet == 4;
}

// Socket h, if nPid may use it (its owner; nPid 0: the kernel itself), else 0.
static CSocket *SockOf (int h, unsigned nPid)
{
	if (h < 0 || h >= MAX_SOCKETS) return 0;
	if (nPid != 0 && s_Sockets[h].nOwnerPid != nPid) return 0;	// (not the caller's)
	CSocket *p = s_Sockets[h].pSocket;
	return RealSocket (p) ? p : 0;
}

static int DoConnect (const char *pHost, unsigned nPort, unsigned nOwnerPid)
{
	if (!NetIsUp () || pHost == 0 || nPort == 0 || nPort > 0xFFFF) return -1;

	int h = ClaimSlot (nOwnerPid, SK_TCP, NET_SS_CONNECTING);	// (ours, before anything blocks)
	if (h < 0) return -2;					// table full
	TSocketSlot &s = s_Sockets[h];

	// Resolve the host: dotted-quad literal, else the cache or a DNS lookup (blocks).
	CIPAddress IP;
	u8 raw[4];
	CSocket *pSock = 0;
	int nResult = h;
	if (ParseDottedIP (pHost, raw))
	{
		IP.Set (raw);
	}
	else if (!ResolveName (pHost, &IP))
	{
		nResult = -3;					// name resolution failed
	}

	if (nResult >= 0)
	{
		pSock = new CSocket (g_pNet, IPPROTO_TCP);
		if (pSock == 0) nResult = -4;
		else if (pSock->Connect (IP, (u16) nPort) < 0)	// TCP handshake (blocks)
		{
			nResult = -5;				// refused / timed out / no route
		}
	}
	if (nResult >= 0 && Ld (&s.bCancel))			// (its owner died meanwhile)
	{
		nResult = -5;
	}
	if (nResult < 0)
	{
		delete pSock;
		FreeSlot (h);
		return nResult;
	}
	pSock->SetOptionSendTimeout (5000000);			// 5 s: never hang the app forever

	IP.CopyTo (s.PeerIP);
	s.nPeerPort  = (u16) nPort;
	s.nLocalPort = pSock->GetOwnPort ();
	__atomic_store_n (&s.pSocket, pSock, __ATOMIC_RELEASE);
	St (&s.nState, NET_SS_CONNECTED);
	Changed ();
	return h;
}

// A call into Circle that may yield (CSocket::Send yields between its segments; a blocking
// accept waits): a close meanwhile only marks the slot, and the last of them closes it.
static void BeginBusy (int h)	{ s_Sockets[h].nBusy++; }
static void EndBusy (int h)
{
	TSocketSlot &s = s_Sockets[h];
	if (--s.nBusy == 0 && Ld (&s.bCancel) && Ld (&s.nState) != NET_SS_CONNECTING) FreeSlot (h);
}

static int DoSend (int hSock, const void *pBuf, unsigned nLen, unsigned nPid)
{
	CSocket *pSock = SockOf (hSock, nPid);
	if (pSock == 0) return -1;
	// Blocking (5 s send timeout). The count is the bytes queued: Circle's CSocket::Send
	// queues MSS-sized chunks, and when a later chunk times out the earlier ones are
	// counted (our Circle patch: it used to answer the error, those bytes already sent).
	BeginBusy (hSock);
	int n = pSock->Send (pBuf, nLen, 0);
	EndBusy (hSock);
	return n;
}

static int SlotRecvTCP (int h, u8 *pBuf, unsigned nLen, unsigned nFlags);

// tcp_recv: >0 data / 0 nothing yet / <0 closed. Through the carry buffer: a buffer smaller
// than a segment no longer loses the rest of it (since v75; it did with netcore=0).
static int DoRecv (int hSock, void *pBuf, unsigned nLen, unsigned nPid)
{
	if (SockOf (hSock, nPid) == 0 || s_Sockets[hSock].nType != SK_TCP) return -1;
	if (nLen == 0) return 0;
	int n = SlotRecvTCP (hSock, (u8 *) pBuf, nLen, 0);
	if (n > 0) return n;
	if (n == -KAPI_EAGAIN) return 0;
	return -1;						// the end, or an error
}

static void DoClose (int hSock, unsigned nPid = 0)	// (nPid 0: the kernel's own closes)
{
	if (hSock < 0 || hSock >= MAX_SOCKETS) return;
	TSocketSlot &s = s_Sockets[hSock];
	if (nPid != 0 && s.nOwnerPid != nPid) return;		// (not the caller's)
	if (s.pSocket == 0) return;
	if (Ld (&s.nState) == NET_SS_CONNECTING || s.nBusy != 0)
	{
		St (&s.bCancel, 1);				// its connector / last user frees it
		return;
	}
	FreeSlot (hSock);					// (the CSocket's dtor ends the connection)
}

// Server side: a socket bound to nPort and listening. The handle is only good for
// NetTcpAccept (and NetTcpClose); it never carries data itself.
static int DoListen (unsigned nPort, unsigned nOwnerPid)
{
	if (!NetIsUp () || nPort == 0 || nPort > 0xFFFF) return -1;

	int h = ClaimSlot (nOwnerPid, SK_TCP, NET_SS_LISTEN);
	if (h < 0) return -2;					// table full

	CSocket *pSock = new CSocket (g_pNet, IPPROTO_TCP);
	if (pSock == 0) { FreeSlot (h); return -4; }
	if (pSock->Bind ((u16) nPort) < 0 || pSock->Listen () < 0)
	{
		delete pSock;
		FreeSlot (h);
		return -6;					// port in use / bind failed
	}

	TSocketSlot &s = s_Sockets[h];
	s.bListen    = TRUE;
	s.nLocalPort = (u16) nPort;
	__atomic_store_n (&s.pSocket, pSock, __ATOMIC_RELEASE);
	Changed ();
	return h;
}

// Wait (blocking, cooperatively) for the next incoming connection on a listening
// handle. Returns a new connected handle (use NetTcpSend/Recv/Close on it) and the
// peer's dotted IP in pIPOut, or <0 on error.
static int DoAccept (int hListen, char *pIPOut, unsigned nIPLen, unsigned nOwnerPid)
{
	CSocket *pListen = SockOf (hListen, nOwnerPid);
	if (pListen == 0 || Ld (&s_Sockets[hListen].nState) != NET_SS_LISTEN) return -1;

	CIPAddress IP;
	u16 nPort = 0;
	u16 nLocal = s_Sockets[hListen].nLocalPort;
	BeginBusy (hListen);
	CSocket *pConn = pListen->Accept (&IP, &nPort);		// blocks until a peer connects
	EndBusy (hListen);
	if (pConn == 0) return -5;

	int h = ClaimSlot (nOwnerPid, SK_TCP, NET_SS_CONNECTED);
	if (h < 0) { delete pConn; return -2; }			// table full -> drop the peer
	pConn->SetOptionSendTimeout (5000000);			// 5 s, like NetTcpConnect

	TSocketSlot &s = s_Sockets[h];
	IP.CopyTo (s.PeerIP);
	s.nPeerPort  = nPort;
	s.nLocalPort = nLocal;
	__atomic_store_n (&s.pSocket, pConn, __ATOMIC_RELEASE);
	Changed ();

	if (pIPOut != 0 && nIPLen > 0)
	{
		CString Str;
		IP.Format (&Str);
		const char *p = (const char *) Str;
		unsigned i = 0;
		for (; p[i] != '\0' && i < nIPLen - 1; i++) pIPOut[i] = p[i];
		pIPOut[i] = '\0';
	}
	return h;
}

static void DoCloseByPid (unsigned nPid)
{
	if (nPid == 0) return;
	for (int i = 0; i < MAX_SOCKETS; i++)
		if (s_Sockets[i].pSocket != 0 && s_Sockets[i].nOwnerPid == nPid)
			DoClose (i);
}

// ---- the slot layer: BSD sockets (kapi v75, sys/bsdsock.cpp) ---------------------------------
//
// Each Slot* function runs where the stack runs (core 0 with netcore=0, a worker of core 3 with
// netcore=1) and never waits: -KAPI_EAGAIN means "not now", and bsdsock.cpp waits for the
// readiness (IoWait) and calls again. A connect is the exception: it is run by a task of its
// own (a one-shot task on core 0, or a detached request on the net core), so that every
// caller only waits on IoWait. Results: >= 0 success, -KAPI_E* failure.

// Circle's NET_ERROR_* (negative) -> -KAPI_E*.
static int MapNetError (int nErr)
{
	switch (-nErr)
	{
	case NET_ERROR_WOULD_BLOCK:		return -KAPI_EAGAIN;
	case NET_ERROR_PERMISSION_DENIED:	return -KAPI_EACCES;
	case NET_ERROR_INVALID_VALUE:		return -KAPI_EINVAL;
	case NET_ERROR_PROTOCOL_NOT_SUPPORTED:	return -KAPI_ENETUNREACH;	// (no address yet)
	case NET_ERROR_OPERATION_NOT_SUPPORTED:	return -KAPI_EOPNOTSUPP;
	case NET_ERROR_CONNECTION_RESET:	return -KAPI_ECONNRESET;
	case NET_ERROR_IS_CONNECTED:		return -KAPI_EISCONN;
	case NET_ERROR_NOT_CONNECTED:		return -KAPI_ENOTCONN;
	case NET_ERROR_CONNECTION_TIMED_OUT:	return -KAPI_ETIMEDOUT;
	case NET_ERROR_CONNECTION_REFUSED:	return -KAPI_ECONNREFUSED;
	case NET_ERROR_DESTINATION_UNREACHABLE:	return -KAPI_EHOSTUNREACH;
	case NET_ERROR_PROTOCOL_ERROR:		return -KAPI_ECONNABORTED;
	default:				return -KAPI_EIO;
	}
}

static boolean EnsureCarry (TSocketSlot &s)
{
	if (s.pCarry == 0) s.pCarry = new u8[FRAME_BUFFER_SIZE];
	return s.pCarry != 0;
}

// The readiness of slot h now (KAPI_POLL*). On the stack's core (AcceptReady may replace dead
// backlog connections).
static u32 EvalStatus (int h)
{
	TSocketSlot &s = s_Sockets[h];
	CSocket *p = s.pSocket;
	if (p == 0) return KAPI_POLLNVAL;
	u32 nState = Ld (&s.nState);
	u32 m = 0;
	if (s.nType == SK_UDP)
	{
		m = KAPI_POLLOUT;
		if (s.nCarryLen > 0 || s.bShutRd || (RealSocket (p) && p->GetStatus ().bRxReady)) m |= KAPI_POLLIN;
		return m;
	}
	switch (nState)
	{
	case NET_SS_NEW:
	case NET_SS_BOUND:	return KAPI_POLLOUT | KAPI_POLLHUP;	// (as Linux: not connected)
	case NET_SS_CONNECTING:	return 0;
	case NET_SS_FAILED:	return KAPI_POLLOUT | KAPI_POLLERR | KAPI_POLLHUP;
	case NET_SS_LISTEN:	// (not while a tcp_accept waits in Circle's Accept: it owns the backlog)
				return RealSocket (p) && s.nBusy == 0 && p->AcceptReady () ? KAPI_POLLIN : 0;
	case NET_SS_CONNECTED:	break;
	default:		return 0;
	}
	if (!RealSocket (p)) return 0;
	if (s.nCarryLen > 0 || s.bEOF || s.bShutRd) m |= KAPI_POLLIN;
	CSocket::TStatus Stat = p->GetStatus ();
	if (Stat.bRxReady) m |= KAPI_POLLIN;			// data, the peer's FIN, an error
	if (Stat.bConnected)
	{
		if (Stat.bTxReady) m |= KAPI_POLLOUT;
	}
	else
	{
		m |= KAPI_POLLIN | KAPI_POLLHUP;			// was connected, no longer is (reset)
	}
	return m;
}

static u32 s_nSnapGen;				// netcore=1: bumped when a snapshot changes

static void UpdateStatus (int h)
{
	u32 m = EvalStatus (h);
	if (Ld (&s_Sockets[h].nStatus) != m)
	{
		St (&s_Sockets[h].nStatus, m);
		Changed ();
	}
}

// A connect run to its end (by its own task, see above): CONNECTED, or FAILED + its error; a
// socket closed meanwhile is freed here.
static void SlotConnectRun (int h)
{
	TSocketSlot &s = s_Sockets[h];
	int nErr = 0;
	CSocket *p = new CSocket (g_pNet, IPPROTO_TCP);
	if (p == 0) nErr = KAPI_ENOBUFS;
	else if (s.nLocalPort != 0 && p->Bind (s.nLocalPort) < 0) nErr = KAPI_EADDRINUSE;
	else
	{
		CIPAddress IP (s.PeerIP);
		int r = p->Connect (IP, s.nPeerPort);		// blocks: the handshake
		if (r < 0) nErr = -MapNetError (r);
	}
	if (Ld (&s.bCancel))
	{
		delete p;
		FreeSlot (h);
		return;
	}
	if (nErr != 0)
	{
		delete p;
		St (&s.nError, (u32) nErr);
		St (&s.nState, NET_SS_FAILED);
	}
	else
	{
		s.nLocalPort = p->GetOwnPort ();
		__atomic_store_n (&s.pSocket, p, __ATOMIC_RELEASE);
		St (&s.nState, NET_SS_CONNECTED);
	}
	UpdateStatus (h);
	Changed ();
}

// A port no listening / bound socket of ours uses (bind to port 0 for TCP: Circle's TCP needs a
// port to listen on). Circle's own ephemeral ports are 60000..60999.
static u16 s_nNextPort = 61000;
static boolean PortInUse (u32 nType, u16 nPort, int hExcept)
{
	for (int i = 0; i < MAX_SOCKETS; i++)
	{
		const TSocketSlot &s = s_Sockets[i];
		if (i == hExcept || s.pSocket == 0 || s.nType != nType || s.nLocalPort != nPort) continue;
		u32 nState = s.nState;
		if (nType == SK_UDP || nState == NET_SS_BOUND || nState == NET_SS_LISTEN) return TRUE;
	}
	return FALSE;
}
static u16 EphemeralPort (u32 nType)
{
	for (unsigned n = 0; n < 1000; n++)
	{
		u16 nPort = s_nNextPort;
		if (++s_nNextPort > 61999) s_nNextPort = 61000;
		if (!PortInUse (nType, nPort, -1)) return nPort;
	}
	return 0;
}

static int SlotBind (int h, unsigned nPort)
{
	TSocketSlot &s = s_Sockets[h];
	if (Ld (&s.nState) != NET_SS_NEW || s.nLocalPort != 0 || RealSocket (s.pSocket)) return -KAPI_EINVAL;
	if (nPort != 0 && PortInUse (s.nType, (u16) nPort, h)) return -KAPI_EADDRINUSE;
	if (s.nType == SK_TCP)
	{
		if (nPort == 0) nPort = EphemeralPort (SK_TCP);	// (Circle's TCP listens on a port)
		if (nPort == 0) return -KAPI_EADDRINUSE;
		s.nLocalPort = (u16) nPort;
		St (&s.nState, NET_SS_BOUND);
		return 0;
	}
	CSocket *p = new CSocket (g_pNet, IPPROTO_UDP);		// UDP: bound now (0: ephemeral)
	if (p == 0) return -KAPI_ENOBUFS;
	if (p->Bind ((u16) nPort) < 0) { delete p; return -KAPI_EADDRINUSE; }
	s.nLocalPort = p->GetOwnPort ();
	__atomic_store_n (&s.pSocket, p, __ATOMIC_RELEASE);
	St (&s.nState, NET_SS_BOUND);
	UpdateStatus (h);
	return 0;
}

static int SlotListen (int h, unsigned nBacklog)
{
	TSocketSlot &s = s_Sockets[h];
	if (s.nType != SK_TCP) return -KAPI_EOPNOTSUPP;
	u32 nState = Ld (&s.nState);
	if (nState == NET_SS_LISTEN) return 0;
	if (nState == NET_SS_NEW)
	{
		int r = SlotBind (h, 0);
		if (r < 0) return r;
	}
	else if (nState != NET_SS_BOUND) return -KAPI_EINVAL;
	if (nBacklog < 1) nBacklog = 1;
	if (nBacklog > SOCKET_MAX_LISTEN_BACKLOG) nBacklog = SOCKET_MAX_LISTEN_BACKLOG;
	CSocket *p = new CSocket (g_pNet, IPPROTO_TCP);
	if (p == 0) return -KAPI_ENOBUFS;
	if (p->Bind (s.nLocalPort) < 0 || p->Listen (nBacklog) < 0) { delete p; return -KAPI_EADDRINUSE; }
	s.bListen = TRUE;
	__atomic_store_n (&s.pSocket, p, __ATOMIC_RELEASE);
	St (&s.nState, NET_SS_LISTEN);
	UpdateStatus (h);
	return 0;
}

// A connection waiting on listening socket h -> a new CONNECTED slot (its peer in pIP/pPort).
static int SlotAccept (int h, unsigned nPid, boolean bNonBlock, u8 *pIP, u16 *pPort)
{
	TSocketSlot &s = s_Sockets[h];
	CSocket *pListen = s.pSocket;
	if (Ld (&s.nState) != NET_SS_LISTEN || !RealSocket (pListen)) return -KAPI_EINVAL;
	if (!pListen->AcceptReady ()) { UpdateStatus (h); return -KAPI_EAGAIN; }
	int n = ClaimSlot (nPid, SK_TCP, NET_SS_CONNECTED);
	if (n < 0) return -KAPI_ENFILE;				// (it stays in the backlog)
	CIPAddress IP;
	u16 nPort = 0;
	CSocket *pConn = pListen->Accept (&IP, &nPort);		// (ready: does not block)
	UpdateStatus (h);
	if (pConn == 0) { FreeSlot (n); return -KAPI_ECONNABORTED; }	// (the peer left)
	TSocketSlot &c = s_Sockets[n];
	IP.CopyTo (c.PeerIP);
	c.nPeerPort  = nPort;
	c.nLocalPort = s.nLocalPort;
	c.bNonBlock  = bNonBlock ? 1 : 0;
	__atomic_store_n (&c.pSocket, pConn, __ATOMIC_RELEASE);
	UpdateStatus (n);
	if (pIP != 0) memcpy (pIP, c.PeerIP, 4);
	if (pPort != 0) *pPort = nPort;
	return n;
}

// TCP: what the connection has now, up to nLen (the carry first; segments go straight into the
// buffer while a whole one fits, else through the carry). -EAGAIN nothing yet, 0 the end.
static int SlotRecvTCP (int h, u8 *pBuf, unsigned nLen, unsigned nFlags)
{
	TSocketSlot &s = s_Sockets[h];
	CSocket *p = s.pSocket;
	boolean bPeek = (nFlags & KAPI_MSG_PEEK) != 0;
	unsigned n = 0;
	if (s.nCarryLen > 0)
	{
		n = s.nCarryLen < nLen ? s.nCarryLen : nLen;
		memcpy (pBuf, s.pCarry + s.nCarryOff, n);
		if (bPeek) return (int) n;
		s.nCarryOff += n; s.nCarryLen -= n;
		if (n == nLen) return (int) n;
	}
	if (s.bShutRd || s.bEOF) return (int) n;		// (0: the end)
	while (n < nLen)
	{
		boolean bDirect = !bPeek && nLen - n >= FRAME_BUFFER_SIZE;
		if (!bDirect && !EnsureCarry (s)) return n > 0 ? (int) n : -KAPI_ENOBUFS;
		int r = bDirect ? p->Receive (pBuf + n, nLen - n, MSG_DONTWAIT)
				: p->Receive (s.pCarry, FRAME_BUFFER_SIZE, MSG_DONTWAIT);
		if (r > 0)
		{
			if (bDirect) { n += (unsigned) r; continue; }
			unsigned k = nLen - n < (unsigned) r ? nLen - n : (unsigned) r;
			memcpy (pBuf + n, s.pCarry, k);
			if (bPeek) { s.nCarryOff = 0; s.nCarryLen = (unsigned) r; return (int) k; }
			n += k;
			s.nCarryOff = k; s.nCarryLen = (unsigned) r - k;
			continue;
		}
		if (r == 0) break;				// nothing more for now
		if (n > 0) break;				// the data first, the end next time
		if (p->GetStatus ().bConnected)			// (CLOSE-WAIT: the peer's FIN)
		{
			s.bEOF = 1;
			return 0;
		}
		return MapNetError (r);
	}
	return n > 0 ? (int) n : -KAPI_EAGAIN;
}

// UDP: one datagram (cut to nLen; the rest of it dropped, unless MSG_PEEK), its sender.
static int SlotRecvUDP (int h, u8 *pBuf, unsigned nLen, unsigned nFlags, u8 *pIP, u16 *pPort)
{
	TSocketSlot &s = s_Sockets[h];
	CSocket *p = s.pSocket;
	while (s.nCarryLen == 0)
	{
		if (s.bShutRd) return 0;
		if (!RealSocket (p)) return -KAPI_EAGAIN;		// (not bound: nothing can come)
		if (!EnsureCarry (s)) return -KAPI_ENOBUFS;
		CIPAddress From;
		u16 nFrom = 0;
		int r = p->ReceiveFrom (s.pCarry, FRAME_BUFFER_SIZE, MSG_DONTWAIT, &From, &nFrom);
		if (r == 0) return -KAPI_EAGAIN;
		if (r < 0) return r == -NET_ERROR_DESTINATION_UNREACHABLE ? -KAPI_ECONNREFUSED : MapNetError (r);
		if (s.nPeerPort != 0 && (nFrom != s.nPeerPort || memcmp (From.Get (), s.PeerIP, 4) != 0))
		{
			continue;				// connected: only its peer's
		}
		From.CopyTo (s.CarryIP);
		s.nCarryPort = nFrom;
		s.nCarryOff = 0; s.nCarryLen = (unsigned) r;
	}
	unsigned k = s.nCarryLen < nLen ? s.nCarryLen : nLen;
	memcpy (pBuf, s.pCarry + s.nCarryOff, k);
	if (pIP != 0) memcpy (pIP, s.CarryIP, 4);
	if (pPort != 0) *pPort = s.nCarryPort;
	if (!(nFlags & KAPI_MSG_PEEK)) s.nCarryLen = 0;
	return (int) k;
}

static int SlotRecv (int h, unsigned nPid, u8 *pBuf, unsigned nLen, unsigned nFlags, u8 *pIP, u16 *pPort)
{
	if (h < 0 || h >= MAX_SOCKETS || s_Sockets[h].pSocket == 0 || s_Sockets[h].nOwnerPid != nPid)
	{
		return -KAPI_EBADF;
	}
	TSocketSlot &s = s_Sockets[h];
	int r;
	if (s.nType == SK_UDP)
	{
		r = SlotRecvUDP (h, pBuf, nLen, nFlags, pIP, pPort);
	}
	else
	{
		switch (Ld (&s.nState))
		{
		case NET_SS_CONNECTED:	break;
		case NET_SS_CONNECTING:	return -KAPI_EAGAIN;
		case NET_SS_FAILED:	return -KAPI_ENOTCONN;
		default:		return -KAPI_ENOTCONN;
		}
		r = SlotRecvTCP (h, pBuf, nLen, nFlags);
		if (r >= 0 && pIP != 0) memcpy (pIP, s.PeerIP, 4);
		if (r >= 0 && pPort != 0) *pPort = s.nPeerPort;
	}
	UpdateStatus (h);
	return r;
}

// A UDP socket's CSocket, bound now if it is not yet (port 0: an ephemeral one).
static int EnsureUDP (int h)
{
	TSocketSlot &s = s_Sockets[h];
	if (RealSocket (s.pSocket)) return 0;
	if (s.nLocalPort != 0 && PortInUse (SK_UDP, s.nLocalPort, h)) return -KAPI_EADDRINUSE;
	CSocket *p = new CSocket (g_pNet, IPPROTO_UDP);
	if (p == 0) return -KAPI_ENOBUFS;
	if (p->Bind (s.nLocalPort) < 0) { delete p; return -KAPI_EADDRINUSE; }
	s.nLocalPort = p->GetOwnPort ();
	__atomic_store_n (&s.pSocket, p, __ATOMIC_RELEASE);
	if (Ld (&s.nState) == NET_SS_NEW) St (&s.nState, NET_SS_BOUND);
	return 0;
}

#define UDP_MAX_PAYLOAD	1472			// 1500 - IP - UDP headers (Circle does not fragment)
#define SEND_MAX	32768			// queued at once by a non-blocking send

// Up to SEND_MAX bytes queued (TCP: only while Circle's queue is under its threshold).
static int SlotSend (int h, unsigned nPid, const u8 *pBuf, unsigned nLen, const u8 *pIP, unsigned nPort)
{
	if (h < 0 || h >= MAX_SOCKETS || s_Sockets[h].pSocket == 0 || s_Sockets[h].nOwnerPid != nPid)
	{
		return -KAPI_EBADF;
	}
	TSocketSlot &s = s_Sockets[h];
	if (s.bShutWr) return -KAPI_EPIPE;
	int r;
	if (s.nType == SK_UDP)
	{
		u8 IP[4];
		if (pIP != 0) { memcpy (IP, pIP, 4); }
		else if (s.nPeerPort != 0) { memcpy (IP, s.PeerIP, 4); nPort = s.nPeerPort; }
		else return -KAPI_EDESTADDRREQ;
		if (nPort == 0) return -KAPI_EINVAL;
		if (nLen > UDP_MAX_PAYLOAD) return -KAPI_EMSGSIZE;
		if (nLen == 0) return 0;			// (Circle sends no empty datagram)
		r = EnsureUDP (h);
		if (r < 0) return r;
		CSocket *p = s.pSocket;
		if (s.bBroadcast && !s.bBroadcastSet) { p->SetOptionBroadcast (TRUE); s.bBroadcastSet = 1; }
		r = p->SendTo (pBuf, nLen, MSG_DONTWAIT, CIPAddress (IP), (u16) nPort);
		return r >= 0 ? r : MapNetError (r);
	}
	switch (Ld (&s.nState))
	{
	case NET_SS_CONNECTED:	break;
	case NET_SS_CONNECTING:	return -KAPI_EAGAIN;
	case NET_SS_FAILED:	return -KAPI_EPIPE;
	default:		return -KAPI_ENOTCONN;
	}
	CSocket *p = s.pSocket;
	CSocket::TStatus Status = p->GetStatus ();
	if (!Status.bConnected) return -KAPI_EPIPE;		// (reset, timed out)
	if (!Status.bTxReady) { UpdateStatus (h); return -KAPI_EAGAIN; }
	if (nLen == 0) return 0;
	if (nLen > SEND_MAX) nLen = SEND_MAX;
	BeginBusy (h);
	r = p->Send (pBuf, nLen, MSG_DONTWAIT);			// (yields between its segments)
	if (!Ld (&s.bCancel)) UpdateStatus (h);
	EndBusy (h);						// (may free the slot: closed meanwhile)
	if (r < 0) r = r == -NET_ERROR_CONNECTION_RESET ? -KAPI_EPIPE : MapNetError (r);
	return r == 0 ? -KAPI_EAGAIN : r;
}

static int SlotClose (int h, unsigned nPid)
{
	if (h < 0 || h >= MAX_SOCKETS || s_Sockets[h].pSocket == 0 || s_Sockets[h].nOwnerPid != nPid)
	{
		return -KAPI_EBADF;
	}
	DoClose (h, nPid);
	return 0;
}

// Is slot h (still) nPid's? (A request checks again on the net core.)
static boolean SlotOwned (int h, unsigned nPid)
{
	return h >= 0 && h < MAX_SOCKETS && s_Sockets[h].pSocket != 0 && s_Sockets[h].nOwnerPid == nPid;
}

// Live: g_bNetUp only says the first DHCP bind happened; the Wi-Fi association can drop
// (and come back) later, so also ask wpa_supplicant (the menu bar's Wi-Fi icon polls this).
int NetStatus (char *pIPOut, unsigned nIPLen)
{
	if (!NetIsUp () || !CWPASupplicant::IsConnected ())
	{
		if (pIPOut != 0 && nIPLen > 0) pIPOut[0] = '\0';
		return 0;
	}
	if (pIPOut != 0 && nIPLen > 0)
	{
		CString s;
		g_pNet->GetConfig ()->GetIPAddress ()->Format (&s);
		const char *p = (const char *) s;
		unsigned i = 0;
		for (; p[i] != '\0' && i < nIPLen - 1; i++) pIPOut[i] = p[i];
		pIPOut[i] = '\0';
	}
	return 1;
}

// ---- v43: ping, DNS, network info ---------------------------------------------------------
static void FormatIP (const u8 *ip, char *pOut, unsigned nCap)
{
	CString s;
	CIPAddress (ip).Format (&s);
	const char *p = (const char *) s;
	unsigned i = 0;
	for (; p[i] != '\0' && i + 1 < nCap; i++) pOut[i] = p[i];
	if (nCap) pOut[i] = '\0';
}

// Resolve pHost (dotted quad or DNS name) into rIP. FALSE if it cannot be resolved.
static boolean ResolveHost (const char *pHost, CIPAddress &rIP)
{
	u8 ip[4];
	if (ParseDottedIP (pHost, ip)) { rIP.Set (ip); return TRUE; }
	return ResolveName (pHost, &rIP);
}

static int DoResolve (const char *pHost, char *pIPOut, unsigned nIPLen)
{
	if (!NetIsUp () || pHost == 0) return 0;
	CIPAddress IP;
	if (!ResolveHost (pHost, IP)) return 0;
	FormatIP (IP.Get (), pIPOut, nIPLen);
	return 1;
}

// One ICMP echo request to pHost; returns the round-trip time in microseconds, or
// -1 network down / -3 unresolved / -4 timeout / -5 send failed. Blocks cooperatively
// (yields while waiting). The replies come from Circle's secondary ICMP queue
// (CNetworkLayer::EnableReceiveICMP), enabled only while a ping is in flight.
static int DoPing (const char *pHost, unsigned nSeq, unsigned nTimeoutMs, char *pIPOut, unsigned nIPLen)
{
	if (!NetIsUp () || pHost == 0) return -1;
	CIPAddress IP;
	if (!ResolveHost (pHost, IP)) return -3;
	if (pIPOut) FormatIP (IP.Get (), pIPOut, nIPLen);

	static unsigned s_nPingers = 0;
	CNetworkLayer *pNL = g_pNet->GetNetworkLayer ();
	if (s_nPingers++ == 0) pNL->EnableReceiveICMP (TRUE);

	const u16 nId = 0x4F4E;				// "ON"
	u8 Pkt[8 + 32];
	Pkt[0] = 8; Pkt[1] = 0; Pkt[2] = Pkt[3] = 0;	// echo request, checksum 0 for now
	Pkt[4] = (u8) (nId >> 8); Pkt[5] = (u8) nId;
	Pkt[6] = (u8) (nSeq >> 8); Pkt[7] = (u8) nSeq;
	for (unsigned i = 0; i < 32; i++) Pkt[8 + i] = (u8) ('a' + i % 26);
	u16 nSum = CChecksumCalculator::SimpleCalculate (Pkt, sizeof Pkt);
	Pkt[2] = (u8) nSum; Pkt[3] = (u8) (nSum >> 8);	// already in network order (as icmphandler)

	unsigned nStart = CTimer::Get ()->GetClockTicks ();
	int nResult = -5;
	if (pNL->Send (IP, Pkt, sizeof Pkt, IPPROTO_ICMP))
	{
		nResult = -4;
		static u8 Buf[FRAME_BUFFER_SIZE];
		while (CTimer::Get ()->GetClockTicks () - nStart < nTimeoutMs * 1000)
		{
			unsigned nLen = 0;
			CIPAddress From, To;
			while (pNL->ReceiveICMP (Buf, &nLen, &From, &To))
			{
				if (nLen >= 8 && Buf[0] == 0 && From == IP
				    && Buf[4] == (u8) (nId >> 8) && Buf[5] == (u8) nId
				    && Buf[6] == (u8) (nSeq >> 8) && Buf[7] == (u8) nSeq)
				{
					nResult = (int) (CTimer::Get ()->GetClockTicks () - nStart);
					break;
				}
			}
			if (nResult >= 0) break;
			CScheduler::Get ()->Yield ();		// let the net task receive
		}
	}
	if (--s_nPingers == 0) pNL->EnableReceiveICMP (FALSE);
	return nResult;
}

// Text summary for netstat: "key value" lines (hostname, ip, mask, gateway, dns, dhcp),
// then one "tcp <handle> listen|conn <local port> <remote ip> <pid>" line per socket.
static int DoInfo (char *pBuf, unsigned nCap)
{
	if (pBuf == 0 || nCap == 0) return 0;
	unsigned n = 0;
	auto put = [&] (const char *s) { for (unsigned i = 0; s[i] && n + 1 < nCap; i++) pBuf[n++] = s[i]; };
	auto putu = [&] (unsigned v) { char t[12]; int k = 0; if (!v) t[k++] = '0'; while (v) { t[k++] = (char) ('0' + v % 10); v /= 10; } while (k) { if (n + 1 < nCap) pBuf[n++] = t[--k]; else k--; } };
	put ("up "); put (NetIsUp () ? "yes" : "no"); put ("\n");
	if (g_pNet != 0 && NetIsUp ())
	{
		CNetConfig *pC = g_pNet->GetConfig ();
		char ip[20];
		put ("hostname "); put (g_pNet->GetHostname () ? g_pNet->GetHostname () : "?"); put ("\n");
		FormatIP (pC->GetIPAddress ()->Get (), ip, sizeof ip); put ("ip "); put (ip); put ("\n");
		FormatIP (pC->GetNetMask (), ip, sizeof ip); put ("mask "); put (ip); put ("\n");
		FormatIP (pC->GetDefaultGateway ()->Get (), ip, sizeof ip); put ("gateway "); put (ip); put ("\n");
		FormatIP (pC->GetDNSServer ()->Get (), ip, sizeof ip); put ("dns "); put (ip); put ("\n");
		put ("dhcp "); put (pC->IsDHCPUsed () ? "yes" : "no"); put ("\n");
	}
	for (int h = 0; h < MAX_SOCKETS; h++)
	{
		const TSocketSlot &s = s_Sockets[h];
		if (!RealSocket (s.pSocket)) continue;		// (free, a connect under way, ...)
		// (v75: the BSD sockets too; a UDP one: "udp <h> bound <port> <default peer|-> <pid>")
		boolean bUDP = s.nType == SK_UDP;
		put (bUDP ? "udp " : "tcp "); putu ((unsigned) h);
		put (bUDP ? " bound " : s.bListen ? " listen " : " conn ");
		putu (s.nLocalPort); put (" ");
		char ip[20] = "-";
		if (!s.bListen && s.nPeerPort != 0) FormatIP (s.PeerIP, ip, sizeof ip);
		put (ip); put (" "); putu (s.nOwnerPid); put ("\n");
	}
	pBuf[n] = '\0';
	return (int) n;
}

// ---- v45: Wi-Fi scan ------------------------------------------------------------------------
// The BCM4343 firmware's "escan": the driver queues one result message per access point seen
// (CBcm4343Device::ReceiveScanResult). wpa_supplicant's Circle driver reads the same queue for
// its own scans, so while it is still looking for its network a scan here can take its
// results (it simply scans again). The layout below is the firmware's, as in hostap's
// src/drivers/driver_circle.cpp (not packed: natural alignment, same compiler).
namespace {
struct TBssInfo
{
	u32 version; u32 length; u8 BSSID[6]; u16 beacon_period; u16 capability;
	u8 SSID_len; u8 SSID[32];
	struct { u32 count; u8 rates[16]; } rateset;
	u16 chanspec; u16 atim_window; u8 dtim_period; u16 RSSI; s8 phy_noise;
	u8 n_cap; u32 nbss_cap; u8 ctl_ch; u32 reserved32[1]; u8 flags; u8 reserved[3];
	u8 basic_mcs[16];
	u16 ie_offset; u32 ie_length; u16 SNR;
};
struct TEscanResult { u32 buflen; u32 version; u16 sync_id; u16 bss_count; TBssInfo bss; };
}

static int ChanFreq (unsigned chan)
{
	if (chan >= 1 && chan <= 13) return 2412 + (int) (chan - 1) * 5;
	if (chan == 14) return 2484;
	if (chan >= 32 && chan <= 173) return 5160 + (int) (chan - 32) * 5;
	return 0;
}

// Security from the capability word + the information elements.
static unsigned char BssSecurity (const TBssInfo *b, unsigned nMsgLen, unsigned nBssOff)
{
	const u8 *ie = (const u8 *) b + b->ie_offset;
	unsigned n = b->ie_length;
	if (nBssOff + b->ie_offset + n > nMsgLen) n = 0;		// (truncated: no IEs)
	unsigned char sec = (b->capability & 0x10) ? WLAN_SEC_WEP : WLAN_SEC_OPEN;	// privacy bit
	for (unsigned i = 0; i + 2 <= n; )
	{
		unsigned id = ie[i], len = ie[i + 1];
		if (i + 2 + len > n) break;
		if (id == 48) return WLAN_SEC_WPA2;				// RSN
		if (id == 221 && len >= 4 && ie[i + 2] == 0x00 && ie[i + 3] == 0x50
		    && ie[i + 4] == 0xF2 && ie[i + 5] == 0x01) sec = WLAN_SEC_WPA;	// WPA vendor IE
		i += 2 + len;
	}
	return sec;
}

// kapi v60 wlan_reconnect: wpa_supplicant reads SD:/etc/wpa_supplicant.conf again and associates
// by it, and DHCP starts over (another network: another address; our Circle fork, docs/05 §14).
// wpa_supplicant registers its SIGHUP handler (wpa_supplicant_reconfig: every interface reloads
// its configuration) with eloop_register_signal_reconfig, which Circle's eloop ignores: the kernel
// is linked with --wrap for it (Makefile), keeps the handler, and runs it from the supplicant's own
// event loop (a 0 s timeout: eloop is not thread-safe; we are on its core, cooperatively).
extern "C"
{
	typedef void (*eloop_signal_handler) (int sig, void *signal_ctx);
	typedef void (*eloop_timeout_handler) (void *eloop_data, void *user_ctx);
	int eloop_register_timeout (unsigned int secs, unsigned int usecs, eloop_timeout_handler handler, void *eloop_data, void *user_data);
	static eloop_signal_handler s_pReconfig = 0; static void *s_pReconfigCtx = 0;
	int __wrap_eloop_register_signal_reconfig (eloop_signal_handler handler, void *user_data)
	{
		s_pReconfig = handler; s_pReconfigCtx = user_data;
		return 0;
	}
	static void ReconfigTimeout (void *, void *) { if (s_pReconfig) s_pReconfig (1, s_pReconfigCtx); }	// (1: SIGHUP)
}

// ---- the Wi-Fi driver's switches (our Circle fork, addon/wlan/ether4330.c; docs/05 §26) ----------
extern "C"
{
	extern char onyx_scan_ssid[4][33];	// the names a scan probes for beside the wildcard
	extern int  onyx_scan_nssid;
	extern int  onyx_scan_5g_bias;		// dB added to a 5 GHz BSS's level in the scan results
	extern int  onyx_wlpoll;		// the chip polled (the net core) instead of its interrupt awaited
	extern int  onyx_wlstat;		// the driver's timing lines in the log
	extern int  onyx_wlfast;		// 1: SDIO lengths rounded up to blocks; 2: the next frame read whole; 4: its locks without a yield
	extern int  onyx_wl_ampdu_tx, onyx_wl_ampdu_rx, onyx_wl_ba_wsize, onyx_wl_ampdu_mpdu, onyx_wl_frameburst, onyx_wl_ampdu_rts, onyx_wl_hostreorder, onyx_wl_rx_ba_wsize, onyx_wl_bw5;	// the firmware's aggregation (-1: its own)
	extern int  onyx_tcp_ackn;		// TCP: full segments for one delayed acknowledgement
	extern int  onyx_tcp_trace;		// TCP: a line in the log at each retransmission timeout
	extern int  onyx_tcp_window;		// TCP: the scaled receive window, in segments
	extern int  onyx_tcp_ws;		// TCP: window scaling, a receive window that follows the queue (docs/05 §27)
	void kapi_reboot (void);
}
#define WLAN_5G_BIAS	25			// a network on both bands: 5 GHz unless it is 25 dB weaker

static boolean s_bNetStat = FALSE;		// cmdline netstat=1: the net core's and the driver's pace in the log

// The networks named in wpa_supplicant's configuration, for the driver's scan: every `ssid="..."`
// line (the first four). A scan probes for them by name over both bands, so an access point that
// leaves its name out of the beacons of one band is still seen as that network; the supplicant's
// own driver glue (hostap, upstream's) asks for a plain scan and is left as it is.
void NetWlanNames (const char *pConfigFile)
{
	static char Buf[4096];
	FIL File;
	UINT n = 0;
	int nNames = 0;
	if (f_open (&File, pConfigFile, FA_READ) == FR_OK)
	{
		if (f_read (&File, Buf, sizeof Buf - 1, &n) != FR_OK) n = 0;
		f_close (&File);
	}
	Buf[n] = '\0';
	for (const char *p = Buf; *p != '\0' && nNames < 4; )
	{
		while (*p == ' ' || *p == '\t') p++;
		if (memcmp (p, "ssid=\"", 6) == 0)
		{
			p += 6;
			unsigned k = 0;
			const char *q = p;
			while (*q != '\0' && *q != '\n' && *q != '\r') q++;	// the line's end
			while (q > p && q[-1] != '"') q--;			// its last quote
			if (q > p) for (q--; p < q && k < 32; ) onyx_scan_ssid[nNames][k++] = *p++;
			onyx_scan_ssid[nNames][k] = '\0';
			if (k > 0) nNames++;
		}
		while (*p != '\0' && *p != '\n') p++;
		if (*p == '\n') p++;
	}
	onyx_scan_nssid = nNames;
	onyx_scan_5g_bias = WLAN_5G_BIAS;
}

// ---- a trial of the driver's switches, for one boot -----------------------------------------------
// SD:/etc/net-trial.txt holds "name=value" words (wlfast, tcpws, netstat, secs). The bring-up reads it,
// DELETES it, applies it; after `secs` seconds (default 180) the Pi restarts -- without the trial,
// the file being gone -- unless SD:/etc/net-trial.keep exists by then. A switch that keeps the
// Wi-Fi from coming up thus costs one restart, not a card taken out: the way to try a change of
// the driver on a Pi that is only reachable by its network.
#define NET_TRIAL_FILE	"SD:/etc/net-trial.txt"
#define NET_TRIAL_KEEP	"SD:/etc/net-trial.keep"
#define NET_TRIAL_LOG	"SD:/etc/net-trial.log"
static u32 s_nTrialEnd;				// CTimer ticks; 0: no trial

static int TrialValue (const char *pText, const char *pKey, int nDefault)
{
	unsigned nLen = strlen (pKey);
	for (const char *p = pText; *p != '\0'; p++)
	{
		if ((p == pText || p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\r' || p[-1] == '\t')
		    && memcmp (p, pKey, nLen) == 0 && p[nLen] == '=')
		{
			int n = 0;
			for (p += nLen + 1; *p >= '0' && *p <= '9'; p++) n = n * 10 + (*p - '0');
			return n;
		}
	}
	return nDefault;
}

void NetTrialLoad (void)
{
	char Buf[256];
	FIL File;
	UINT n = 0;
	if (f_open (&File, NET_TRIAL_FILE, FA_READ) != FR_OK) return;
	if (f_read (&File, Buf, sizeof Buf - 1, &n) != FR_OK) n = 0;
	f_close (&File);
	Buf[n] = '\0';
	f_unlink (NET_TRIAL_KEEP);
	if (f_unlink (NET_TRIAL_FILE) != FR_OK) return;		// (it must not come back at the next boot)
	onyx_wlfast = TrialValue (Buf, "wlfast", onyx_wlfast);
	onyx_tcp_ws = TrialValue (Buf, "tcpws", onyx_tcp_ws);
	onyx_tcp_window = TrialValue (Buf, "tcpwin", onyx_tcp_window);
	onyx_tcp_ackn = TrialValue (Buf, "ackn", onyx_tcp_ackn);
	onyx_wl_ampdu_tx = TrialValue (Buf, "ampdutx", onyx_wl_ampdu_tx);
	onyx_wl_ampdu_rx = TrialValue (Buf, "ampdurx", onyx_wl_ampdu_rx);
	onyx_wl_ba_wsize = TrialValue (Buf, "bawsize", onyx_wl_ba_wsize);
	onyx_wl_ampdu_mpdu = TrialValue (Buf, "ampdumpdu", onyx_wl_ampdu_mpdu);
	onyx_wl_frameburst = TrialValue (Buf, "frameburst", onyx_wl_frameburst);
	onyx_wl_ampdu_rts = TrialValue (Buf, "ampdurts", onyx_wl_ampdu_rts);
	onyx_wl_hostreorder = TrialValue (Buf, "hostreorder", onyx_wl_hostreorder);
	onyx_wl_rx_ba_wsize = TrialValue (Buf, "rxbawsize", onyx_wl_rx_ba_wsize);
	onyx_wl_bw5 = TrialValue (Buf, "bw5", onyx_wl_bw5);
	s_nSleepIdleUs = (unsigned) TrialValue (Buf, "netsleep", (int) (s_nSleepIdleUs / 1000)) * 1000;
	s_nSleepUs = (unsigned) TrialValue (Buf, "netsleepus", (int) s_nSleepUs);
	if (TrialValue (Buf, "netstat", 0)) { s_bNetStat = TRUE; onyx_wlstat = 1; onyx_tcp_trace = 1; }
	unsigned nSecs = (unsigned) TrialValue (Buf, "secs", 180);
	s_nTrialEnd = CTimer::Get ()->GetTicks () + nSecs * HZ;
	if (s_nTrialEnd == 0) s_nTrialEnd = 1;
	CLogger::Get ()->Write ("net", LogWarning, "a trial for %u s: wlfast=%d tcpws=%d netsleep=%u ms / %u us (then a restart, unless " NET_TRIAL_KEEP " exists)",
				nSecs, onyx_wlfast, onyx_tcp_ws, s_nSleepIdleUs / 1000, s_nSleepUs);
}

// Core 0's main task, every 250 ms.
void NetTrialPoll (void)
{
	if (s_nTrialEnd == 0 || (int) (CTimer::Get ()->GetTicks () - s_nTrialEnd) < 0) return;
	s_nTrialEnd = 0;
	FILINFO Info;
	if (f_stat (NET_TRIAL_KEEP, &Info) == FR_OK) return;
	CLogger::Get ()->Write ("net", LogWarning, "the trial is over: restarting");
	// The kernel log's tail into SD:/etc/net-trial.log: what the driver said during a trial that
	// cut the network is read from the PC after the restart.
	static char Log[24 * 1024];
	int nLog = CLogger::Get ()->Read (Log, sizeof Log, FALSE);
	FIL File;
	UINT nDone;
	if (nLog > 0 && f_open (&File, NET_TRIAL_LOG, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
	{
		f_write (&File, Log, (UINT) nLog, &nDone);
		f_close (&File);
	}
	kapi_reboot ();
}

// kernel.cpp, before the bring-up: the driver's switches from the command line.
void NetWlanOptions (boolean bStat)
{
	s_bNetStat = bStat;
	onyx_wlstat = bStat;
	onyx_wlpoll = g_bNetCore;
	// The frames' path of the driver (one SDIO command a frame, its locks without a turn of the
	// scheduler, the bus at 50 MHz, the controller's registers written without a wait) and TCP's
	// window scaling: on (docs/05 sections 26 and 27;
	// each was tried alone by the trial file below, which can still turn them off for a boot).
	onyx_wlfast = 31;
	// The frames sent are not aggregated (A-MPDU): aggregated, half of what the Pi sent during a
	// download was lost (docs/05 section 26, 5). TCP then acknowledges one segment in eight (a
	// frame each on the radio, now), within 10 to 20 ms (onyx_tcp_ws bit 2, docs/05 section 27).
	onyx_wl_ampdu_tx = 0;
	onyx_wl_frameburst = 1;		// (several frames in one transmit opportunity: what is sent without A-MPDU goes twice as fast)
	onyx_tcp_ws = 3;
	onyx_tcp_ackn = 8;
}

static int DoWlanReconnect (void)
{
	if (g_pNet == 0 || CNetDevice::GetNetDevice (NetDeviceTypeWLAN) == 0 || s_pReconfig == 0) return -1;
	NetWlanNames ("SD:/etc/wpa_supplicant.conf");		// (the file changed: the new network's name)
	if (eloop_register_timeout (0, 0, ReconfigTimeout, 0, 0) != 0) return -1;
	CDHCPClient::Restart ();
	return 0;
}

static int DoWlanScan (struct kapi_wlan_ap *pOut, int nMax)
{
	if (pOut == 0 || nMax <= 0) return 0;
	CNetDevice *pDev = CNetDevice::GetNetDevice (NetDeviceTypeWLAN);
	if (pDev == 0) return 0;
	CBcm4343Device *pWLAN = (CBcm4343Device *) pDev;

	static u8 Buf[FRAME_BUFFER_SIZE];
	unsigned nLen;
	while (pWLAN->ReceiveScanResult (Buf, &nLen)) {}		// stale messages
	onyx_scan_5g_bias = 0;						// the levels as heard, for the list
	if (!pWLAN->Control ("escan %u", 6)) { onyx_scan_5g_bias = WLAN_5G_BIAS; return 0; }

	const u8 *pOwn = 0;						// the BSSID we are on
	u8 Own[6];
	if (g_pNet != 0 && CWPASupplicant::IsConnected ())
	{
		const CMACAddress *pB = pWLAN->GetBSSID ();
		if (pB != 0) { pB->CopyTo (Own); pOwn = Own; }
	}

	int nCount = 0;
	unsigned nStart = CTimer::Get ()->GetTicks ();
	while (CTimer::Get ()->GetTicks () - nStart < 5 * HZ)		// (both bands: 5 GHz's DFS channels are listened to)
	{
		CScheduler::Get ()->MsSleep (50);
		while (pWLAN->ReceiveScanResult (Buf, &nLen))
		{
			if (nLen < sizeof (TEscanResult)) continue;
			const TEscanResult *r = (const TEscanResult *) Buf;
			unsigned off = (unsigned) ((const u8 *) &r->bss - Buf);
			for (unsigned k = 0; k < r->bss_count && off + sizeof (TBssInfo) <= nLen; k++)
			{
				const TBssInfo *b = (const TBssInfo *) (Buf + off);
				if (b->length == 0) break;
				unsigned chan = b->chanspec & 0xFF;		// (a wide channel: its centre)
				if (b->n_cap && b->ctl_ch != 0) chan = b->ctl_ch;	// the control channel
				int level = (s16) b->RSSI;
				// One entry per BSSID (keep the strongest reading).
				int j = 0;
				while (j < nCount && memcmp (pOut[j].bssid, b->BSSID, 6) != 0) j++;
				if (j == nCount)
				{
					if (nCount >= nMax) { off += b->length; continue; }
					nCount++;
					kapi_wlan_ap &a = pOut[j];
					memset (&a, 0, sizeof a);
					unsigned sl = b->SSID_len < 32 ? b->SSID_len : 32;
					for (unsigned i = 0; i < sl; i++) a.ssid[i] = b->SSID[i] ? (char) b->SSID[i] : ' ';
					a.ssid[sl] = '\0';
					memcpy (a.bssid, b->BSSID, 6);
					a.level = -1000;
				}
				kapi_wlan_ap &a = pOut[j];
				if (level > a.level)
				{
					a.level = level;
					a.channel = (unsigned char) chan;
					a.freq = ChanFreq (chan);
					a.security = BssSecurity (b, nLen, off);
				}
				a.connected = pOwn != 0 && memcmp (pOwn, b->BSSID, 6) == 0;
				off += b->length;
			}
		}
	}
	pWLAN->Control ("escan 0");					// stop
	onyx_scan_5g_bias = WLAN_5G_BIAS;

	for (int i = 1; i < nCount; i++)				// strongest first
	{
		kapi_wlan_ap t = pOut[i]; int j = i - 1;
		while (j >= 0 && pOut[j].level < t.level) { pOut[j + 1] = pOut[j]; j--; }
		pOut[j + 1] = t;
	}
	return nCount;
}

// ---- the network core (netcore=1) --------------------------------------------------------------
//
// Core 3 runs its own scheduler (CScheduler is per core) with every task of the stack on it:
// the bring-up, Circle's CNetTask / PHY / DHCP / NTP tasks, the WLAN driver's kprocs,
// wpa_supplicant -- all created there, so they stay there -- and a pool of worker tasks that
// run the apps' requests. The stack is then used from one core only, as it was written for.
// Core 0 keeps the apps: a kapi call copies its arguments (the app's memory is not mapped on
// core 3) into a request slot, marks it posted, and waits -- spinning a little (most answers
// take microseconds), then yielding to the other tasks of core 0. A worker claims the slot
// (compare-and-swap), runs the Do* function, and marks it done.
// A process that dies while it waits (or with a socket open) is cleaned up by NetCloseByPid,
// called from its teardown: its requests are orphaned (the worker closes what they made) and
// its pid goes to a ring the net core's main loop reads (DoCloseByPid there).
// The BSD sockets' readiness (v75): the main loop recomputes every open slot's snapshot at each
// turn and bumps s_nSnapGen when one changed; core 0's tick hook (NetPollTick) turns that into
// IoWake, so a poll / a blocking BSD call on core 0 wakes within a tick. A non-blocking connect
// is a DETACHED request: its caller does not wait, the worker frees the request itself.

volatile boolean g_bNetCore = FALSE;

enum { NR_CONNECT = 1, NR_SEND, NR_RECV, NR_CLOSE, NR_LISTEN, NR_ACCEPT, NR_RESOLVE, NR_PING, NR_INFO, NR_SCAN, NR_RECONF,
       NR_SBIND, NR_SLISTEN, NR_SCONNECT, NR_SACCEPT, NR_SSEND, NR_SRECV, NR_SCLOSE };	// (NR_S*: v75)
enum { RQ_FREE, RQ_POSTED, RQ_CLAIMED, RQ_DONE, RQ_ORPHAN };

#define NET_REQS	32
#define NET_REQBUF	32768		// (a recv gathers the segments that fit: fewer round trips)
#define NET_WORKERS	6		// at start; more when all are busy (an accept waits for long)
#define NET_WORKERS_MAX	24
#define NET_CLOSES	64
#define NET_SPIN_US	300		// the caller spins this long before yielding

struct TNetReq
{
	u32	 nState;			// RQ_* (atomic)
	unsigned nOp, nPid;
	int	 h;
	unsigned n1, n2;
	boolean	 bDetached;			// (v75) nobody waits: the worker frees it
	u8	 Addr[4];			// (v75) an IPv4 address in or out
	u16	 nPort;
	char	 szHost[128];
	char	 szIP[20];
	unsigned nData;
	int	 nResult;
	u8	 Buf[NET_REQBUF];
};

// On the heap (NetCoreStart), not in the BSS: NET_REQS x NET_REQBUF is 1 MB, and the kernel's
// image + BSS must end below 2 MB (KERNEL_MAX_SIZE: kernel/Makefile's sizecheck).
static TNetReq *s_Req;
static u32 s_ClosePid[NET_CLOSES];
static u32 s_nCloseIn, s_nCloseOut;		// core 0 / core 3 (atomic)
static CTask *(*s_pfnBringup) (void) = 0;
static volatile boolean s_bGo = FALSE, s_bReady = FALSE;
static unsigned s_nWorkers = 0, s_nIdle = 0;	// (net core only)
static char s_NoticeTitle[32], s_NoticeText[96];
static u32 s_bNotice;
static u32 s_nSnapSeen;				// (core 0's tick: the last s_nSnapGen woken for)

static void CopyStr (char *d, const char *s, unsigned nCap)
{
	unsigned i = 0;
	if (s != 0) for (; s[i] && i + 1 < nCap; i++) d[i] = s[i];
	d[i] = '\0';
}

// A slot's readiness may have changed: wake the waiters (core 0: at once; the net core: a
// generation core 0's tick hook watches).
static u32 s_bReadyIPI;				// an IPI to core 0 is on its way (the net core sets it, core 0 clears it)

static void Changed (void)
{
	if (g_bNetCore)
	{
		__atomic_add_fetch (&s_nSnapGen, 1, __ATOMIC_RELEASE);
		// Core 0 is told at once (an inter-core interrupt), not at its next 100 Hz tick: a
		// blocking recv / send / poll waited up to 10 ms for each turn -- an echo's round
		// trip was 10 ms on the LAN, a download a few hundred KB/s. One IPI at a time: core 0
		// clears the flag before it reads the generation, so a change after that sends another.
		if (!__atomic_exchange_n (&s_bReadyIPI, 1, __ATOMIC_ACQ_REL))
		{
			CMultiCoreSupport::SendIPI (0, IPI_NET_READY);
		}
	}
	else IoWake ();
}

// Core 0, at each 100 Hz tick (IRQ) -- the fallback -- and at the net core's IPI: the net core
// changed a snapshot -> wake the waiters.
static void NetPollTick (void)
{
	u32 nGen = Ld (&s_nSnapGen);
	if (nGen != s_nSnapSeen)
	{
		s_nSnapSeen = nGen;
		IoWake ();
	}
}

// Core 0's IPI handler (kernel.cpp, COnyxCores::IPIHandler): IRQ context, as the tick hook.
void NetReadyIPI (void)
{
	St (&s_bReadyIPI, 0);
	NetPollTick ();
}

// ---- net core side ----
static void Execute (TNetReq &r)
{
	switch (r.nOp)
	{
	case NR_CONNECT: r.nResult = DoConnect (r.szHost, r.n1, r.nPid); break;
	case NR_SEND:	 r.nResult = DoSend (r.h, r.Buf, r.nData, r.nPid); break;
	case NR_RECV:	 r.nResult = DoRecv (r.h, r.Buf, r.nData, r.nPid); break;	// (gathers segments)
	case NR_CLOSE:	 DoClose (r.h, r.nPid); r.nResult = 0; break;
	case NR_LISTEN:	 r.nResult = DoListen (r.n1, r.nPid); break;
	case NR_ACCEPT:	 r.nResult = DoAccept (r.h, r.szIP, sizeof r.szIP, r.nPid); break;
	case NR_RESOLVE: r.nResult = DoResolve (r.szHost, r.szIP, sizeof r.szIP); break;
	case NR_PING:	 r.nResult = DoPing (r.szHost, r.n1, r.n2, r.szIP, sizeof r.szIP); break;
	case NR_INFO:	 r.nResult = DoInfo ((char *) r.Buf, r.nData); break;
	case NR_SCAN:	 r.nResult = DoWlanScan ((kapi_wlan_ap *) r.Buf, (int) r.n1); break;
	case NR_RECONF:	 r.nResult = DoWlanReconnect (); break;
	// v75: the BSD sockets (the owner was checked by the core 0 side; checked again here when a
	// slot may have changed hands meanwhile)
	case NR_SBIND:	 r.nResult = SlotOwned (r.h, r.nPid) ? SlotBind (r.h, r.n1) : -KAPI_EBADF; break;
	case NR_SLISTEN: r.nResult = SlotOwned (r.h, r.nPid) ? SlotListen (r.h, r.n1) : -KAPI_EBADF; break;
	case NR_SCONNECT: SlotConnectRun (r.h); r.nResult = 0; break;
	case NR_SACCEPT: r.nResult = SlotOwned (r.h, r.nPid) ? SlotAccept (r.h, r.nPid, r.n1 != 0, r.Addr, &r.nPort)
							      : -KAPI_EBADF; break;
	case NR_SSEND:	 r.nResult = SlotSend (r.h, r.nPid, r.Buf, r.nData, r.n2 ? r.Addr : 0, r.nPort); break;
	case NR_SRECV:	 r.nResult = SlotRecv (r.h, r.nPid, r.Buf, r.nData, r.n1, r.Addr, &r.nPort); break;
	case NR_SCLOSE:	 r.nResult = SlotClose (r.h, r.nPid); break;
	default:	 r.nResult = -1; break;
	}
}

static void Finish (TNetReq &r)
{
	if (r.bDetached)				// (nobody reads it)
	{
		St (&r.nState, RQ_FREE);
		return;
	}
	u32 nOld = __atomic_exchange_n (&r.nState, (u32) RQ_DONE, __ATOMIC_ACQ_REL);
	if (nOld == RQ_ORPHAN)				// its caller is gone: undo, free the slot
	{
		if ((r.nOp == NR_CONNECT || r.nOp == NR_ACCEPT || r.nOp == NR_LISTEN || r.nOp == NR_SACCEPT)
		    && r.nResult >= 0) DoClose (r.nResult);
		St (&r.nState, RQ_FREE);
	}
}

class CNetWorker : public CTask
{
public:
	CNetWorker (void) { SetName ("netwrk"); }
	void Run (void) override
	{
		for (;;)
		{
			for (unsigned i = 0; i < NET_REQS; i++)
			{
				TNetReq &r = s_Req[i];
				if (Ld (&r.nState) != RQ_POSTED || !Cas (&r.nState, RQ_POSTED, RQ_CLAIMED)) continue;
				s_nIdle--;
				Execute (r);
				Finish (r);
				s_nIdle++;
			}
			CScheduler::Get ()->Yield ();
		}
	}
};

static boolean AnyPosted (void)
{
	for (unsigned i = 0; i < NET_REQS; i++) if (Ld (&s_Req[i].nState) == RQ_POSTED) return TRUE;
	return FALSE;
}

// Every open slot's readiness snapshot recomputed (only the open ones are looked at).
static void Snapshot (void)
{
	for (int h = 0; h < MAX_SOCKETS; h++)
	{
		if (__atomic_load_n (&s_Sockets[h].pSocket, __ATOMIC_ACQUIRE) != 0) UpdateStatus (h);
	}
}

static unsigned s_nRounds, s_nSnapUs, s_nStatStart, s_nRoundLast, s_nRoundMax, s_nSleeps, s_nSleptUs;

void NetCoreMain (void)
{
	while (!s_bGo) asm volatile ("wfe");
	asm volatile ("dmb ish" ::: "memory");
	new CScheduler;					// this core's; this context is its "main" task
	unsigned nEventFor = 0;				// the sleep the event stream is set for
	unsigned nPollsAtSleep = 0;			// onyx_wl_polls at the last sleep
	if (s_pfnBringup != 0) (*s_pfnBringup) ();	// the bring-up task (created here: runs here)
	for (unsigned i = 0; i < NET_WORKERS; i++) new CNetWorker;
	s_nWorkers = s_nIdle = NET_WORKERS;
	asm volatile ("dmb ish" ::: "memory");
	s_bReady = TRUE;
	for (;;)
	{
		while (s_nCloseOut != Ld (&s_nCloseIn))
		{
			u32 nPid = s_ClosePid[s_nCloseOut % NET_CLOSES];
			St (&s_nCloseOut, s_nCloseOut + 1);
			DoCloseByPid (nPid);
		}
		if (s_nIdle == 0 && s_nWorkers < NET_WORKERS_MAX && AnyPosted ())
		{
			new CNetWorker;
			s_nWorkers++; s_nIdle++;
		}
		unsigned nT0 = CTimer::Get ()->GetClockTicks ();
		Snapshot ();
		s_nSnapUs += CTimer::Get ()->GetClockTicks () - nT0;
		CScheduler::Get ()->ReapTerminatedTasks ();	// (the bring-up task, once done)
		CScheduler::Get ()->Yield ();
		// (cmdline netstat=1: the net core's pace in the log, every 5 s -- how many rounds of its
		// scheduler a second, what a round and the snapshot cost: the stack's tasks all wait by
		// yielding, so a round's length is the unit of every wait)
		s_nRounds++;
		unsigned nNow = CTimer::Get ()->GetClockTicks ();
		if (s_nRoundLast != 0 && nNow - s_nRoundLast > s_nRoundMax) s_nRoundMax = nNow - s_nRoundLast;
		s_nRoundLast = nNow;
		// (Between two sleeps the chip is asked once, whole: that takes several rounds -- each
		// of its SDIO commands waits one --, so the rounds go on until the driver has counted an
		// empty answer.)
		// The quiet network: no frame read or written and no request from core 0 for a while --
		// the core sleeps a millisecond (less when a request is posted: its SEV; the event stream
		// ends a WFE every ~1.2 ms at most), then does one round: the chip is asked a thousand
		// times a second instead of all the time. The first frame or request brings the polling back at once.
		if (   s_nSleepIdleUs != 0
		    && nNow - onyx_wl_lastact > s_nSleepIdleUs
		    && nNow - Ld (&s_nLastPostUs) > s_nSleepIdleUs
		    && onyx_wl_polls != nPollsAtSleep
		    && s_nCloseOut == Ld (&s_nCloseIn)
		    && !AnyPosted ())
		{
			// (a WFE also ends at every spin lock released, on any core -- Circle's unlock sends
			// the event --, this core's own included: asked again until the time has passed)
			if (nEventFor != s_nSleepUs)
			{
				// the timer's event stream on this core: a WFE ends at least every
				// 2^(bit + 1) counter ticks -- the bit chosen so that this is the sleep's
				// length or a little less (54 MHz: bit 15 is ~1.2 ms, bit 12 ~150 us)
				nEventFor = s_nSleepUs;
				u64 f; asm volatile ("mrs %0, cntfrq_el0" : "=r" (f));
				u64 nTicks = f / 1000000 * s_nSleepUs;
				unsigned nBit = 15;
				while (nBit > 4 && (2ull << nBit) > nTicks) nBit--;
				u64 v; asm volatile ("mrs %0, cntkctl_el1" : "=r" (v));
				v = (v & ~0xF0ul) | ((u64) nBit << 4) | (1ul << 2);
				asm volatile ("msr cntkctl_el1, %0; isb" :: "r" (v));
			}
			nPollsAtSleep = onyx_wl_polls;
			unsigned nWoke;
			do
			{
				asm volatile ("wfe");
				nWoke = CTimer::Get ()->GetClockTicks ();
			}
			while (   nWoke - nNow < s_nSleepUs
			       && s_nCloseOut == Ld (&s_nCloseIn)
			       && !AnyPosted ());
			CScheduler::Get ()->NoteSleptUs (nWoke - nNow);
			s_nSleeps++; s_nSleptUs += nWoke - nNow;
			s_nRoundLast = nWoke;			// (the sleep is not a round's length)
		}
		if (s_bNetStat && nNow - s_nStatStart >= 5000000)
		{
			unsigned nUs = nNow - s_nStatStart;
			CLogger::Get ()->Write ("net", LogNotice, "core 3: %u rounds/s, %u us a round (the longest %u us), snapshot %u us a round, %u tasks' workers (%u idle); asleep %u %% of the time (%u sleeps)",
						(unsigned) ((u64) s_nRounds * 1000000 / nUs), s_nRounds ? nUs / s_nRounds : 0, s_nRoundMax,
						s_nRounds ? s_nSnapUs / s_nRounds : 0, s_nWorkers, s_nIdle,
						(unsigned) ((u64) s_nSleptUs * 100 / nUs), s_nSleeps);
			s_nRounds = 0; s_nSnapUs = 0; s_nStatStart = nNow; s_nRoundMax = 0; s_nSleeps = 0; s_nSleptUs = 0;
		}
	}
}

// ---- core 0 side ----
void NetCoreStart (CTask *(*pfnBringup) (void))
{
	s_Req = new TNetReq[NET_REQS];			// (before the net core runs: all RQ_FREE)
	assert (s_Req != 0);
	memset (s_Req, 0, sizeof (TNetReq) * NET_REQS);
	s_pfnBringup = pfnBringup;
	IoWaitAddTickHook (NetPollTick);		// (the BSD sockets' readiness)
	asm volatile ("dmb ish" ::: "memory");
	s_bGo = TRUE;
	asm volatile ("dsb ish; sev" ::: "memory");
}

void NetCoreNotify (const char *pTitle, const char *pText)
{
	CopyStr (s_NoticeTitle, pTitle, sizeof s_NoticeTitle);
	CopyStr (s_NoticeText, pText, sizeof s_NoticeText);
	St (&s_bNotice, 1);
}

void NetCorePoll (void)
{
	if (Ld (&s_bNotice) && CScheduler::ThisCore () == 0)
	{
		IpcNotify (s_NoticeTitle, s_NoticeText);
		St (&s_bNotice, 0);
	}
}

// A free slot, filled by the caller then posted (core 0 tasks are not preempted in the
// kernel: nobody else takes it meanwhile). 0: the net core is not running.
static TNetReq *NewReq (unsigned nOp, unsigned nPid)
{
	if (!s_bReady) return 0;
	for (;;)
	{
		for (unsigned i = 0; i < NET_REQS; i++)
		{
			TNetReq &r = s_Req[i];
			if (Ld (&r.nState) != RQ_FREE) continue;
			r.nOp = nOp; r.nPid = nPid; r.h = -1; r.n1 = r.n2 = 0; r.nData = 0; r.nResult = -1;
			r.bDetached = FALSE; r.nPort = 0;
			r.szHost[0] = r.szIP[0] = '\0';
			return &r;
		}
		CScheduler::Get ()->Yield ();			// all 32 in use
	}
}

static void Post (TNetReq *r)
{
	St (&s_nLastPostUs, CTimer::Get ()->GetClockTicks ());	// (the net core stays awake a while)
	St (&r->nState, RQ_POSTED);
	asm volatile ("sev");
}

// Wait for the answer; the caller reads its outputs, then Release (). Most answers take
// microseconds (recv, send, close): spin. Then yield to the other tasks for a while (a
// connect, a DNS lookup), then sleep in longer and longer steps -- an accept may wait for
// hours (ftpd, telnetd, vncd) and must not keep core 0 busy meanwhile.
static void Wait (TNetReq *r)
{
	unsigned nStart = CTimer::Get ()->GetClockTicks ();
	while (Ld (&r->nState) != RQ_DONE)
	{
		unsigned nWaited = CTimer::Get ()->GetClockTicks () - nStart;
		if (nWaited < NET_SPIN_US) continue;
		if (nWaited < 5000) CScheduler::Get ()->Yield ();
		else CScheduler::Get ()->MsSleep (nWaited < 200000 ? 1 : 10);
	}
}
static void Release (TNetReq *r) { St (&r->nState, RQ_FREE); }

static unsigned CurrentPid (void);
static void NetCount (unsigned nPid, int nRx, int nTx);		// (v80 net_stats, below)

int NetTcpConnect (const char *pHost, unsigned nPort, unsigned nOwnerPid)
{
	if (!g_bNetCore) return DoConnect (pHost, nPort, nOwnerPid);
	if (!NetIsUp () || pHost == 0) return -1;
	TNetReq *r = NewReq (NR_CONNECT, nOwnerPid); if (r == 0) return -1;
	CopyStr (r->szHost, pHost, sizeof r->szHost); r->n1 = nPort;
	Post (r); Wait (r);
	int n = r->nResult; Release (r); return n;
}

int NetTcpSend (int hSock, const void *pBuf, unsigned nLen)
{
	if (!g_bNetCore) { int k = DoSend (hSock, pBuf, nLen, CurrentPid ()); NetCount (CurrentPid (), 0, k); return k; }
	int nTotal = 0;
	const u8 *p = (const u8 *) pBuf;
	do
	{
		unsigned k = nLen > NET_REQBUF ? NET_REQBUF : nLen;
		TNetReq *r = NewReq (NR_SEND, CurrentPid ()); if (r == 0) return -1;
		r->h = hSock; r->nData = k;
		memcpy (r->Buf, p, k);				// (the app's memory: mapped here, on core 0)
		Post (r); Wait (r);
		int n = r->nResult; Release (r);
		if (n < 0) return nTotal > 0 ? nTotal : n;	// (the requests before: queued)
		NetCount (CurrentPid (), 0, n);
		nTotal += n; p += n; nLen -= (unsigned) n;
		if ((unsigned) n < k) break;			// short: the bytes queued, then a timeout
	}
	while (nLen > 0);
	return nTotal;
}

int NetTcpRecv (int hSock, void *pBuf, unsigned nLen)
{
	if (!g_bNetCore) { int k = DoRecv (hSock, pBuf, nLen, CurrentPid ()); NetCount (CurrentPid (), k, 0); return k; }
	if (nLen == 0) return 0;
	TNetReq *r = NewReq (NR_RECV, CurrentPid ()); if (r == 0) return -1;
	r->h = hSock; r->nData = nLen > NET_REQBUF ? NET_REQBUF : nLen;
	Post (r); Wait (r);
	int n = r->nResult;
	if (n > 0) memcpy (pBuf, r->Buf, (unsigned) n);
	NetCount (CurrentPid (), n, 0);
	Release (r); return n;
}

void NetTcpClose (int hSock)
{
	if (!g_bNetCore) { DoClose (hSock, CurrentPid ()); return; }
	TNetReq *r = NewReq (NR_CLOSE, CurrentPid ()); if (r == 0) return;
	r->h = hSock;
	Post (r); Wait (r); Release (r);
}

int NetTcpListen (unsigned nPort, unsigned nOwnerPid)
{
	if (!g_bNetCore) return DoListen (nPort, nOwnerPid);
	if (!NetIsUp ()) return -1;
	TNetReq *r = NewReq (NR_LISTEN, nOwnerPid); if (r == 0) return -1;
	r->n1 = nPort;
	Post (r); Wait (r);
	int n = r->nResult; Release (r); return n;
}

int NetTcpAccept (int hListen, char *pIPOut, unsigned nIPLen, unsigned nOwnerPid)
{
	if (!g_bNetCore) return DoAccept (hListen, pIPOut, nIPLen, nOwnerPid);
	TNetReq *r = NewReq (NR_ACCEPT, nOwnerPid); if (r == 0) return -1;
	r->h = hListen;
	Post (r); Wait (r);
	int n = r->nResult;
	if (n >= 0 && pIPOut != 0 && nIPLen > 0) CopyStr (pIPOut, r->szIP, nIPLen);
	Release (r); return n;
}

// ---- (kapi v80 net_stats) the bytes a process sent and received through its sockets ----
// Counted where the calls return, on core 0 (a process's tasks all run there): no lock. A
// process's line is freed when it ends (NetCloseByPid); the totals stay.
#define NET_PIDS	64
static struct { unsigned nPid; u64 ulRx, ulTx; } s_PidNet[NET_PIDS];
static u64 s_ulRxTotal, s_ulTxTotal;

static void NetCount (unsigned nPid, int nRx, int nTx)
{
	if (nRx <= 0 && nTx <= 0) return;
	if (nRx > 0) s_ulRxTotal += (unsigned) nRx;
	if (nTx > 0) s_ulTxTotal += (unsigned) nTx;
	if (nPid == 0) return;
	int nFree = -1;
	for (int i = 0; i < NET_PIDS; i++)
	{
		if (s_PidNet[i].nPid == nPid)
		{
			if (nRx > 0) s_PidNet[i].ulRx += (unsigned) nRx;
			if (nTx > 0) s_PidNet[i].ulTx += (unsigned) nTx;
			return;
		}
		if (nFree < 0 && s_PidNet[i].nPid == 0) nFree = i;
	}
	if (nFree < 0) return;					// (64 processes with sockets: the totals only)
	s_PidNet[nFree].ulRx = nRx > 0 ? (unsigned) nRx : 0;
	s_PidNet[nFree].ulTx = nTx > 0 ? (unsigned) nTx : 0;
	s_PidNet[nFree].nPid = nPid;
}

int NetStats (unsigned nPid, u64 *pRx, u64 *pTx, unsigned *pSockets)
{
	*pRx = *pTx = 0; *pSockets = 0;
	for (int h = 0; h < MAX_SOCKETS; h++)
		if (s_Sockets[h].pSocket != 0 && (nPid == 0 || s_Sockets[h].nOwnerPid == nPid)) (*pSockets)++;
	if (nPid == 0) { *pRx = s_ulRxTotal; *pTx = s_ulTxTotal; return 0; }
	for (int i = 0; i < NET_PIDS; i++)
		if (s_PidNet[i].nPid == nPid) { *pRx = s_PidNet[i].ulRx; *pTx = s_PidNet[i].ulTx; return 0; }
	return 0;						// (no socket used yet: zeros)
}

void NetCloseByPid (unsigned nPid)
{
	for (int i = 0; i < NET_PIDS; i++)			// (v80: its line of the counts)
		if (nPid != 0 && s_PidNet[i].nPid == nPid) s_PidNet[i].nPid = 0;
	if (!g_bNetCore) { DoCloseByPid (nPid); return; }
	if (nPid == 0 || !s_bReady) return;
	// (from the teardown, IRQs masked: nothing here waits)
	for (unsigned i = 0; i < NET_REQS; i++)
	{
		TNetReq &r = s_Req[i];
		if (r.nPid != nPid) continue;
		if (r.bDetached) continue;				// (a connect: runs on, sees its slot closed)
		if (Cas (&r.nState, RQ_POSTED, RQ_FREE)) continue;	// not started: dropped
		if (Cas (&r.nState, RQ_CLAIMED, RQ_ORPHAN)) continue;	// running: the worker undoes it
		if (Ld (&r.nState) == RQ_DONE) St (&r.nState, RQ_FREE);	// (its sockets: closed by pid)
	}
	u32 nIn = Ld (&s_nCloseIn);
	if (nIn - Ld (&s_nCloseOut) < NET_CLOSES)
	{
		s_ClosePid[nIn % NET_CLOSES] = nPid;
		St (&s_nCloseIn, nIn + 1);
		asm volatile ("sev");
	}
}

int NetResolve (const char *pHost, char *pIPOut, unsigned nIPLen)
{
	if (!g_bNetCore) return DoResolve (pHost, pIPOut, nIPLen);
	if (!NetIsUp () || pHost == 0) return 0;
	TNetReq *r = NewReq (NR_RESOLVE, CurrentPid ()); if (r == 0) return 0;
	CopyStr (r->szHost, pHost, sizeof r->szHost);
	Post (r); Wait (r);
	int n = r->nResult;
	if (n > 0 && pIPOut != 0 && nIPLen > 0) CopyStr (pIPOut, r->szIP, nIPLen);
	Release (r); return n;
}

int NetPing (const char *pHost, unsigned nSeq, unsigned nTimeoutMs, char *pIPOut, unsigned nIPLen)
{
	if (!g_bNetCore) return DoPing (pHost, nSeq, nTimeoutMs, pIPOut, nIPLen);
	if (!NetIsUp () || pHost == 0) return -1;
	TNetReq *r = NewReq (NR_PING, CurrentPid ()); if (r == 0) return -1;
	CopyStr (r->szHost, pHost, sizeof r->szHost); r->n1 = nSeq; r->n2 = nTimeoutMs;
	Post (r); Wait (r);
	int n = r->nResult;
	if (pIPOut != 0 && nIPLen > 0) CopyStr (pIPOut, r->szIP, nIPLen);
	Release (r); return n;
}

int NetInfo (char *pBuf, unsigned nCap)
{
	if (!g_bNetCore) return DoInfo (pBuf, nCap);
	if (pBuf == 0 || nCap == 0) return 0;
	TNetReq *r = NewReq (NR_INFO, CurrentPid ());
	if (r == 0) { CopyStr (pBuf, "up no\n", nCap); return (int) (nCap > 6 ? 6 : nCap - 1); }
	r->nData = nCap > NET_REQBUF ? NET_REQBUF : nCap;
	Post (r); Wait (r);
	int n = r->nResult;
	if (n >= 0) { memcpy (pBuf, r->Buf, (unsigned) n); pBuf[n < (int) nCap ? n : (int) nCap - 1] = '\0'; }
	Release (r); return n;
}

int NetWlanReconnect (void)
{
	if (!g_bNetCore) return DoWlanReconnect ();
	TNetReq *r = NewReq (NR_RECONF, CurrentPid ()); if (r == 0) return -1;
	Post (r); Wait (r);
	int n = r->nResult;
	Release (r); return n;
}

int NetWlanScan (struct kapi_wlan_ap *pOut, int nMax)
{
	if (!g_bNetCore) return DoWlanScan (pOut, nMax);
	if (pOut == 0 || nMax <= 0) return 0;
	int nFit = (int) (NET_REQBUF / sizeof (kapi_wlan_ap));
	if (nMax > nFit) nMax = nFit;
	TNetReq *r = NewReq (NR_SCAN, CurrentPid ()); if (r == 0) return 0;
	r->n1 = (unsigned) nMax;
	Post (r); Wait (r);
	int n = r->nResult;
	if (n > 0) memcpy (pOut, r->Buf, (unsigned) n * sizeof (kapi_wlan_ap));
	Release (r); return n;
}

// Socket h's owner pid (0: no such socket). Read on core 0 while the net core may change
// the slot: an answer for a moment; the net core checks the owner again for each request.
unsigned NetSocketOwner (int h)
{
	if (h < 0 || h >= MAX_SOCKETS) return 0;
	if (__atomic_load_n (&s_Sockets[h].pSocket, __ATOMIC_ACQUIRE) == 0) return 0;
	return __atomic_load_n (&s_Sockets[h].nOwnerPid, __ATOMIC_ACQUIRE);
}

// Socket h passes from nFrom to nTo (a descendant of its owner uses it), if it is still
// nFrom's. TRUE if it is nTo's now.
boolean NetSocketAdopt (int h, unsigned nFrom, unsigned nTo)
{
	if (h < 0 || h >= MAX_SOCKETS || nFrom == 0 || nTo == 0) return FALSE;
	unsigned nOld = nFrom;
	return __atomic_compare_exchange_n (&s_Sockets[h].nOwnerPid, &nOld, nTo, FALSE,
					    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

// The calling process (its requests are orphaned if it dies while it waits).
static unsigned CurrentPid (void)
{
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	CAddressSpace *pAS = pTask != 0 ? (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER) : 0;
	return pAS != 0 ? pAS->GetPid () : 0;
}

unsigned NetCurrentPid (void)
{
	return CurrentPid ();
}

// ---- v75: the BSD sockets, core 0 side (sys/bsdsock.cpp calls these) -------------------------
//
// netcore=0: the slot layer directly (core 0 is the stack's core). netcore=1: a request to a
// worker of core 3 (data through its 32 KB buffer); the fields core 0 reads (state, options,
// the snapshot) are read in place.

// Slot h if nPid owns it, else 0.
static TSocketSlot *OwnedSlot (int h, unsigned nPid)
{
	if (h < 0 || h >= MAX_SOCKETS) return 0;
	TSocketSlot &s = s_Sockets[h];
	if (__atomic_load_n (&s.pSocket, __ATOMIC_ACQUIRE) == 0) return 0;
	if (__atomic_load_n (&s.nOwnerPid, __ATOMIC_ACQUIRE) != nPid) return 0;
	return &s;
}

// A connect run by a one-shot task of core 0 (netcore=0), so that every caller only waits on
// IoWait (a blocking connect too): an app killed meanwhile leaves no task inside Circle.
class CNetConnectTask : public CTask
{
public:
	CNetConnectTask (int h) : m_h (h) { SetName ("netconn"); }
	void Run (void) override { SlotConnectRun (m_h); }
private:
	int m_h;
};

int NetSockOpen (int nType, boolean bNonBlock, unsigned nPid)
{
	if (!NetIsUp ()) return -KAPI_ENETDOWN;
	int h = ClaimSlot (nPid, nType == KAPI_SOCK_DGRAM ? SK_UDP : SK_TCP, NET_SS_NEW);
	if (h < 0) return -KAPI_ENFILE;
	s_Sockets[h].bNonBlock = bNonBlock ? 1 : 0;
	St (&s_Sockets[h].nStatus, nType == KAPI_SOCK_DGRAM ? KAPI_POLLOUT : KAPI_POLLOUT | KAPI_POLLHUP);
	return h;
}

int NetSockView (int h, unsigned nPid, TNetSockView *pView)
{
	TSocketSlot *s = OwnedSlot (h, nPid);
	if (s == 0) return -KAPI_EBADF;
	pView->nType	     = s->nType == SK_UDP ? KAPI_SOCK_DGRAM : KAPI_SOCK_STREAM;
	pView->nState	     = Ld (&s->nState);
	pView->bNonBlock     = s->bNonBlock;
	pView->nRcvTimeoutMs = s->nRcvTimeoutMs;
	pView->nSndTimeoutMs = s->nSndTimeoutMs;
	pView->bBroadcast    = s->bBroadcast;
	pView->nLocalPort    = s->nLocalPort;
	pView->nPeerPort     = s->nPeerPort;
	memcpy (pView->PeerIP, s->PeerIP, 4);
	pView->nCarry	     = s->nCarryLen;
	return 0;
}

int NetSockSetOpt (int h, unsigned nPid, int nOpt, int nValue)
{
	TSocketSlot *s = OwnedSlot (h, nPid);
	if (s == 0) return -KAPI_EBADF;
	switch (nOpt)
	{
	case KAPI_SO_NONBLOCK:	  s->bNonBlock = nValue != 0; break;
	case KAPI_SO_RCVTIMEO_MS: s->nRcvTimeoutMs = nValue > 0 ? (unsigned) nValue : 0; break;
	case KAPI_SO_SNDTIMEO_MS: s->nSndTimeoutMs = nValue > 0 ? (unsigned) nValue : 0; break;
	case KAPI_SO_BROADCAST:
		if (s->nType != SK_UDP) return -KAPI_ENOPROTOOPT;
		s->bBroadcast = nValue != 0;		// (applied by the next send)
		break;
	default:		  return -KAPI_ENOPROTOOPT;
	}
	return 0;
}

int NetSockTakeError (int h, unsigned nPid)
{
	TSocketSlot *s = OwnedSlot (h, nPid);
	if (s == 0) return -KAPI_EBADF;
	return (int) __atomic_exchange_n (&s->nError, 0u, __ATOMIC_ACQ_REL);
}

int NetSockShutdown (int h, unsigned nPid, int nHow)
{
	TSocketSlot *s = OwnedSlot (h, nPid);
	if (s == 0) return -KAPI_EBADF;
	if (s->nType == SK_TCP ? Ld (&s->nState) != NET_SS_CONNECTED : s->nPeerPort == 0) return -KAPI_ENOTCONN;
	if (nHow == KAPI_SHUT_RD || nHow == KAPI_SHUT_RDWR) s->bShutRd = 1;
	if (nHow == KAPI_SHUT_WR || nHow == KAPI_SHUT_RDWR) s->bShutWr = 1;
	if (g_bNetCore) St (&s->nStatus, Ld (&s->nStatus) | (s->bShutRd ? KAPI_POLLIN : 0));
	IoWake ();						// (pollers: readable now)
	return 0;
}

// The readiness of socket h (KAPI_POLL*), KAPI_POLLNVAL if it is not nPid's.
unsigned NetSockPoll (int h, unsigned nPid)
{
	if (OwnedSlot (h, nPid) == 0) return KAPI_POLLNVAL;
	if (g_bNetCore) return Ld (&s_Sockets[h].nStatus);
	return EvalStatus (h);
}

boolean NetSockWakesOnChange (void)
{
	return g_bNetCore;					// (else: poll the stack each tick)
}

int NetSockBind (int h, unsigned nPid, unsigned nPort)
{
	if (OwnedSlot (h, nPid) == 0) return -KAPI_EBADF;
	if (!g_bNetCore) return SlotBind (h, nPort);
	TNetReq *r = NewReq (NR_SBIND, nPid); if (r == 0) return -KAPI_ENETDOWN;
	r->h = h; r->n1 = nPort;
	Post (r); Wait (r);
	int n = r->nResult; Release (r); return n;
}

int NetSockListen (int h, unsigned nPid, int nBacklog)
{
	if (OwnedSlot (h, nPid) == 0) return -KAPI_EBADF;
	if (!g_bNetCore) return SlotListen (h, (unsigned) nBacklog);
	TNetReq *r = NewReq (NR_SLISTEN, nPid); if (r == 0) return -KAPI_ENETDOWN;
	r->h = h; r->n1 = (unsigned) nBacklog;
	Post (r); Wait (r);
	int n = r->nResult; Release (r); return n;
}

// TCP: the connect started (-EINPROGRESS: bsdsock.cpp waits for the state to leave CONNECTING,
// or returns that), or why not. UDP: the default peer set (0).
int NetSockConnect (int h, unsigned nPid, const u8 *pIP, unsigned nPort)
{
	TSocketSlot *s = OwnedSlot (h, nPid);
	if (s == 0) return -KAPI_EBADF;
	if (s->nType == SK_UDP)
	{
		memcpy (s->PeerIP, pIP, 4);			// (read by the next send / receive)
		s->nPeerPort = (u16) nPort;
		return 0;
	}
	switch (Ld (&s->nState))
	{
	case NET_SS_NEW:
	case NET_SS_BOUND:	break;
	case NET_SS_CONNECTING:	return -KAPI_EALREADY;
	case NET_SS_CONNECTED:	return -KAPI_EISCONN;
	case NET_SS_FAILED:
	{
		int nErr = (int) __atomic_exchange_n (&s->nError, 0u, __ATOMIC_ACQ_REL);
		if (nErr != 0) return -nErr;			// (the last one's, not read yet)
		break;						// (again)
	}
	default:		return -KAPI_EINVAL;		// (listening)
	}
	if (!NetIsUp ()) return -KAPI_ENETUNREACH;
	memcpy (s->PeerIP, pIP, 4);
	s->nPeerPort = (u16) nPort;
	St (&s->nStatus, 0);
	St (&s->nState, NET_SS_CONNECTING);			// (from here, a close only marks it)
	if (!g_bNetCore)
	{
		CNetConnectTask *pTask = new CNetConnectTask (h);
		if (pTask == 0)
		{
			St (&s->nState, NET_SS_NEW);
			return -KAPI_ENOBUFS;
		}
		return -KAPI_EINPROGRESS;
	}
	TNetReq *r = NewReq (NR_SCONNECT, nPid);
	if (r == 0) { St (&s->nState, NET_SS_NEW); return -KAPI_ENETDOWN; }
	r->h = h; r->bDetached = TRUE;
	Post (r);						// (the worker frees the request)
	return -KAPI_EINPROGRESS;
}

int NetSockAccept (int h, unsigned nPid, boolean bNonBlock, u8 *pIP, u16 *pPort)
{
	if (OwnedSlot (h, nPid) == 0) return -KAPI_EBADF;
	if (!g_bNetCore) return SlotAccept (h, nPid, bNonBlock, pIP, pPort);
	TNetReq *r = NewReq (NR_SACCEPT, nPid); if (r == 0) return -KAPI_ENETDOWN;
	r->h = h; r->n1 = bNonBlock ? 1 : 0;
	Post (r); Wait (r);
	int n = r->nResult;
	if (n >= 0) { memcpy (pIP, r->Addr, 4); *pPort = r->nPort; }
	Release (r); return n;
}

int NetSockSend (int h, unsigned nPid, const void *pBuf, unsigned nLen, const u8 *pIP, unsigned nPort)
{
	if (!g_bNetCore) { int k = SlotSend (h, nPid, (const u8 *) pBuf, nLen, pIP, nPort); NetCount (nPid, 0, k); return k; }
	if (nLen > NET_REQBUF) nLen = NET_REQBUF;
	TNetReq *r = NewReq (NR_SSEND, nPid); if (r == 0) return -KAPI_ENETDOWN;
	r->h = h; r->nData = nLen;
	if (pIP != 0) { memcpy (r->Addr, pIP, 4); r->nPort = (u16) nPort; r->n2 = 1; }
	memcpy (r->Buf, pBuf, nLen);				// (the app's memory: mapped here, on core 0)
	Post (r); Wait (r);
	int n = r->nResult; Release (r);
	NetCount (nPid, 0, n);
	return n;
}

int NetSockRecv (int h, unsigned nPid, void *pBuf, unsigned nLen, unsigned nFlags, u8 *pIP, u16 *pPort)
{
	if (!g_bNetCore)
	{
		int k = SlotRecv (h, nPid, (u8 *) pBuf, nLen, nFlags, pIP, pPort);
		if (!(nFlags & KAPI_MSG_PEEK)) NetCount (nPid, k, 0);
		return k;
	}
	if (nLen > NET_REQBUF) nLen = NET_REQBUF;
	TNetReq *r = NewReq (NR_SRECV, nPid); if (r == 0) return -KAPI_ENETDOWN;
	r->h = h; r->nData = nLen; r->n1 = nFlags;
	Post (r); Wait (r);
	int n = r->nResult;
	if (n > 0) memcpy (pBuf, r->Buf, (unsigned) n);
	if (!(nFlags & KAPI_MSG_PEEK)) NetCount (nPid, n, 0);	// (a peeked byte is counted when it is read)
	if (n >= 0) { memcpy (pIP, r->Addr, 4); *pPort = r->nPort; }
	Release (r); return n;
}

int NetSockClose (int h, unsigned nPid)
{
	if (OwnedSlot (h, nPid) == 0) return -KAPI_EBADF;
	int n;
	if (!g_bNetCore) n = SlotClose (h, nPid);
	else
	{
		TNetReq *r = NewReq (NR_SCLOSE, nPid); if (r == 0) return -KAPI_ENETDOWN;
		r->h = h;
		Post (r); Wait (r);
		n = r->nResult; Release (r);
	}
	IoWake ();						// (a poll on it: POLLNVAL now)
	return n;
}

// The local address (the stack's own IP while it has one).
void NetOwnIP (u8 *pIP)
{
	memset (pIP, 0, 4);
	if (NetIsUp ()) g_pNet->GetConfig ()->GetIPAddress ()->CopyTo (pIP);
}
