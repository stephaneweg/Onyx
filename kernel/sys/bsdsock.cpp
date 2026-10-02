//
// bsdsock.cpp -- BSD sockets over the kernel's socket slots (sys/net.cpp): sock_open / connect /
// bind / listen / accept / send / recv / shutdown / close / getopt / setopt / name, and poll over
// sockets, streams and files (kapi v75 slots 229..241, docs/POSIX-PLAN.md §3.3).
//
// Owner: WP-NET. The kapis check the app's pointers, adopt an ancestor's socket (as tcp_*), and
// do every wait here: the slot layer (net.cpp) never waits -- it answers -KAPI_EAGAIN -- and a
// blocking call waits for the socket's readiness on the shared I/O generation (kern/iowait.h),
// then asks again. With netcore=1 the net core's readiness snapshot wakes the waiters (a tick
// hook: <= 10 ms); with netcore=0 nothing does, so the waits look again at every tick.
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#include <kern/kapi_abi.h>
#include <kern/net.h>
#include <kern/iowait.h>
#include <kern/uaccess.h>
#include <kern/handle.h>
#include <kern/stream.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/new.h>
#include <circle/util.h>

// Socket numbers. 0 .. 255 are IP sockets: sys/net.cpp's table (the stack's, shared with the
// tcp_* handles), served where the stack runs (core 0, or core 3 with netcore=1). The numbers from
// SOCK_LOCAL_BASE up are kept for the local sockets of WP-IPC (AF_UNIX: socketpair, handle
// passing): kernel objects of core 0 with their own buffers. They plug in at the "WP-IPC" marks:
// sock_open (a family in the type's high bits, rejected today), every call's dispatch (here they
// all reach the net layer, which answers -EBADF for these numbers), and SockPoll; their changes
// wake the waiters with IoWake, so the waits below serve them unchanged.
#define SOCK_LOCAL_BASE	0x10000

#define SOCK_IO_MAX	0x40000000ULL	// one call moves at most 1 GB (a short count beyond)
#define WAIT_SPINS	8		// yields before a wait sleeps (an answer often takes µs)
#define WAIT_TICK_MS	10		// netcore=0: the stack is looked at again each tick
#define WAIT_MAX_MS	100		// netcore=1: a wake missed costs at most this

// ---- the waits ----------------------------------------------------------------------------------

// A deadline in ms (0: none) from now.
struct TDeadline
{
	TDeadline (unsigned nMs) : m_nStart (CTimer::Get ()->GetClockTicks ()), m_nMs (nMs) {}
	boolean Passed (void) const	{ return m_nMs != 0 && Elapsed () >= m_nMs; }
	unsigned Left (void) const	{ return m_nMs == 0 ? KAPI_WAIT_FOREVER : (Elapsed () >= m_nMs ? 0 : m_nMs - Elapsed ()); }
	unsigned Elapsed (void) const	{ return (CTimer::Get ()->GetClockTicks () - m_nStart) / 1000; }
	unsigned m_nStart, m_nMs;
};

// One step of a wait for something to change: a few yields first, then a sleep on the I/O
// generation nGen (read before the caller looked), at most nLeftMs.
static void WaitStep (u32 nGen, unsigned *pRound, unsigned nLeftMs)
{
	if ((*pRound)++ < WAIT_SPINS)
	{
		CScheduler::Get ()->Yield ();
		return;
	}
	unsigned nStep = NetSockWakesOnChange () ? WAIT_MAX_MS : WAIT_TICK_MS;
	if (nLeftMs < nStep) nStep = nLeftMs;
	IoWait (nGen, nStep);
}

// The readiness of socket s (KAPI_POLL*; POLLNVAL: not a socket of the caller).
static unsigned SockPoll (int s, unsigned nPid)
{
	if (s >= SOCK_LOCAL_BASE) return KAPI_POLLNVAL;		// (WP-IPC: the local sockets)
	return NetSockPoll (s, nPid);
}

// Wait until socket s has one of nEvents (or an error / hang-up), or the deadline: TRUE ready.
static boolean WaitReady (int s, unsigned nPid, unsigned nEvents, const TDeadline &Dl)
{
	unsigned nRound = 0;
	for (;;)
	{
		u32 nGen = IoGen ();
		if (SockPoll (s, nPid) & (nEvents | KAPI_POLLERR | KAPI_POLLHUP | KAPI_POLLNVAL)) return TRUE;
		unsigned nLeft = Dl.Left ();
		if (nLeft == 0) return FALSE;
		WaitStep (nGen, &nRound, nLeft);
	}
}

// ---- helpers ------------------------------------------------------------------------------------

static unsigned Caller (int s)
{
	SocketAdopt (s);					// (an ancestor's: ours from now on)
	return NetCurrentPid ();
}

static int GetAddr (const struct kapi_sockaddr *pUser, struct kapi_sockaddr *pOut)
{
	if (!UserGet (pOut, pUser)) return -KAPI_EFAULT;
	if (pOut->family != KAPI_AF_INET) return -KAPI_EAFNOSUPPORT;
	return 0;
}

static boolean PutAddr (struct kapi_sockaddr *pUser, const u8 *pIP, unsigned nPort)
{
	struct kapi_sockaddr a;
	memset (&a, 0, sizeof a);
	a.family = KAPI_AF_INET;
	a.port = (unsigned short) nPort;
	memcpy (a.addr, pIP, 4);
	return UserPut (pUser, a);
}

extern "C" {

// ---- the calls ----------------------------------------------------------------------------------

int kapi_sock_open (int nType, unsigned nFlags)
{
	// (WP-IPC: a family in the type's high bits would come here; any other value is refused)
	if (nType != KAPI_SOCK_STREAM && nType != KAPI_SOCK_DGRAM) return -KAPI_EPROTONOSUPPORT;
	if (nFlags & ~KAPI_SOCKF_NONBLOCK) return -KAPI_EINVAL;
	return NetSockOpen (nType, (nFlags & KAPI_SOCKF_NONBLOCK) != 0, NetCurrentPid ());
}

int kapi_sock_connect (int s, const struct kapi_sockaddr *pTo)
{
	struct kapi_sockaddr To;
	int r = GetAddr (pTo, &To);
	if (r < 0) return r;
	if (To.port == 0) return -KAPI_EINVAL;
	unsigned nPid = Caller (s);
	TNetSockView V;
	if (NetSockView (s, nPid, &V) < 0) return -KAPI_EBADF;
	r = NetSockConnect (s, nPid, To.addr, To.port);
	if (r != -KAPI_EINPROGRESS || V.bNonBlock) return r;

	// blocking: until the connect ends (Circle gives up after its retries: about a minute)
	unsigned nRound = 0;
	for (;;)
	{
		u32 nGen = IoGen ();
		if (NetSockView (s, nPid, &V) < 0) return -KAPI_EBADF;		// (closed meanwhile)
		if (V.nState != NET_SS_CONNECTING) break;
		WaitStep (nGen, &nRound, KAPI_WAIT_FOREVER);
	}
	if (V.nState == NET_SS_CONNECTED) return 0;
	int nErr = NetSockTakeError (s, nPid);
	return nErr > 0 ? -nErr : -KAPI_ECONNABORTED;
}

int kapi_sock_bind (int s, const struct kapi_sockaddr *pAddr)
{
	struct kapi_sockaddr A;
	int r = GetAddr (pAddr, &A);
	if (r < 0) return r;
	u8 Own[4];
	NetOwnIP (Own);
	boolean bAny = A.addr[0] == 0 && A.addr[1] == 0 && A.addr[2] == 0 && A.addr[3] == 0;
	if (!bAny && memcmp (A.addr, Own, 4) != 0) return -KAPI_EADDRNOTAVAIL;
	return NetSockBind (s, Caller (s), A.port);
}

int kapi_sock_listen (int s, int nBacklog)
{
	if (nBacklog < 1) nBacklog = 1;
	if (nBacklog > 32) nBacklog = 32;
	return NetSockListen (s, Caller (s), nBacklog);
}

int kapi_sock_accept (int s, struct kapi_sockaddr *pPeer, unsigned nFlags)
{
	if (nFlags & ~KAPI_SOCKF_NONBLOCK) return -KAPI_EINVAL;
	if (pPeer != 0 && !UserRange (pPeer, sizeof *pPeer)) return -KAPI_EFAULT;	// (before a peer is taken)
	unsigned nPid = Caller (s);
	TNetSockView V;
	if (NetSockView (s, nPid, &V) < 0) return -KAPI_EBADF;
	TDeadline Dl (V.nRcvTimeoutMs);
	u8 IP[4] = {0, 0, 0, 0};
	u16 nPort = 0;
	int r;
	for (;;)
	{
		r = NetSockAccept (s, nPid, (nFlags & KAPI_SOCKF_NONBLOCK) != 0, IP, &nPort);
		if (r != -KAPI_EAGAIN || V.bNonBlock) break;
		if (!WaitReady (s, nPid, KAPI_POLLIN, Dl)) break;		// (timeout: -EAGAIN)
		if (NetSockView (s, nPid, &V) < 0) return -KAPI_EBADF;
	}
	if (r >= 0 && pPeer != 0) PutAddr (pPeer, IP, nPort);
	return r;
}

long long kapi_sock_send (int s, const void *pBuf, unsigned long long nLen, unsigned nFlags,
			 const struct kapi_sockaddr *pTo)
{
	if (nLen > SOCK_IO_MAX) nLen = SOCK_IO_MAX;
	if (nLen > 0 && !UserReadable (pBuf, nLen)) return -KAPI_EFAULT;
	struct kapi_sockaddr To;
	if (pTo != 0)
	{
		int r = GetAddr (pTo, &To);
		if (r < 0) return r;
	}
	unsigned nPid = Caller (s);
	TNetSockView V;
	if (NetSockView (s, nPid, &V) < 0) return -KAPI_EBADF;
	const u8 *pTarget = pTo != 0 && V.nType == KAPI_SOCK_DGRAM ? To.addr : 0;	// (TCP: ignored)
	unsigned nTargetPort = pTarget != 0 ? To.port : 0;
	boolean bNonBlock = V.bNonBlock || (nFlags & KAPI_MSG_DONTWAIT);
	TDeadline Dl (V.nSndTimeoutMs);
	const u8 *p = (const u8 *) pBuf;
	unsigned long long nDone = 0;
	for (;;)
	{
		unsigned long long nRest = nLen - nDone;
		int r = NetSockSend (s, nPid, p + nDone, nRest > 0x7FFFFFFF ? 0x7FFFFFFF : (unsigned) nRest,
				     pTarget, nTargetPort);
		if (r > 0)
		{
			nDone += (unsigned) r;
			if (nDone == nLen || V.nType == KAPI_SOCK_DGRAM) break;
			continue;
		}
		if (r == 0) break;				// (an empty send)
		if (r != -KAPI_EAGAIN) return nDone > 0 ? (long long) nDone : r;
		if (bNonBlock) break;
		if (!WaitReady (s, nPid, KAPI_POLLOUT, Dl)) break;	// (SO_SNDTIMEO)
	}
	return nDone > 0 || nLen == 0 ? (long long) nDone : -KAPI_EAGAIN;
}

long long kapi_sock_recv (int s, void *pBuf, unsigned long long nLen, unsigned nFlags,
			 struct kapi_sockaddr *pFrom)
{
	if (nLen > SOCK_IO_MAX) nLen = SOCK_IO_MAX;
	if (nLen > 0 && !UserWritable (pBuf, nLen)) return -KAPI_EFAULT;
	if (pFrom != 0 && !UserRange (pFrom, sizeof *pFrom)) return -KAPI_EFAULT;
	unsigned nPid = Caller (s);
	TNetSockView V;
	if (NetSockView (s, nPid, &V) < 0) return -KAPI_EBADF;
	if (nLen == 0) return 0;
	boolean bNonBlock = V.bNonBlock || (nFlags & KAPI_MSG_DONTWAIT);
	boolean bPeek = (nFlags & KAPI_MSG_PEEK) != 0;
	boolean bAll = (nFlags & KAPI_MSG_WAITALL) != 0 && !bPeek && V.nType == KAPI_SOCK_STREAM;
	TDeadline Dl (V.nRcvTimeoutMs);
	u8 *p = (u8 *) pBuf;
	unsigned long long nDone = 0;
	u8 IP[4] = {0, 0, 0, 0};
	u16 nPort = 0;
	long long nResult;
	for (;;)
	{
		unsigned long long nRest = nLen - nDone;
		int r = NetSockRecv (s, nPid, p + nDone, nRest > 0x7FFFFFFF ? 0x7FFFFFFF : (unsigned) nRest,
				     bPeek ? KAPI_MSG_PEEK : 0, IP, &nPort);
		if (r > 0)
		{
			nDone += (unsigned) r;
			if (!bAll || nDone == nLen) { nResult = (long long) nDone; break; }
			continue;				// (MSG_WAITALL: more)
		}
		if (r == 0) { nResult = (long long) nDone; break; }	// the end
		if (r != -KAPI_EAGAIN) { nResult = nDone > 0 ? (long long) nDone : r; break; }
		if (nDone > 0 && !bAll) { nResult = (long long) nDone; break; }
		if (bNonBlock || !WaitReady (s, nPid, KAPI_POLLIN, Dl))
		{
			nResult = nDone > 0 ? (long long) nDone : -KAPI_EAGAIN;	// (SO_RCVTIMEO)
			break;
		}
	}
	if (nResult > 0 && pFrom != 0) PutAddr (pFrom, IP, nPort);
	return nResult;
}

int kapi_sock_shutdown (int s, int nHow)
{
	if (nHow != KAPI_SHUT_RD && nHow != KAPI_SHUT_WR && nHow != KAPI_SHUT_RDWR) return -KAPI_EINVAL;
	return NetSockShutdown (s, Caller (s), nHow);
}

int kapi_sock_close (int s)
{
	return NetSockClose (s, Caller (s));
}

int kapi_sock_getopt (int s, int nOpt, int *pValue)
{
	if (!UserRange (pValue, sizeof *pValue)) return -KAPI_EFAULT;
	unsigned nPid = Caller (s);
	TNetSockView V;
	if (NetSockView (s, nPid, &V) < 0) return -KAPI_EBADF;
	int nValue;
	switch (nOpt)
	{
	case KAPI_SO_ERROR:	  nValue = NetSockTakeError (s, nPid); if (nValue < 0) return nValue; break;
	case KAPI_SO_NONBLOCK:	  nValue = V.bNonBlock ? 1 : 0; break;
	case KAPI_SO_RCVTIMEO_MS: nValue = (int) V.nRcvTimeoutMs; break;
	case KAPI_SO_SNDTIMEO_MS: nValue = (int) V.nSndTimeoutMs; break;
	case KAPI_SO_BROADCAST:	  nValue = V.bBroadcast ? 1 : 0; break;
	case KAPI_SO_NREAD:
		nValue = (int) V.nCarry;
		if (nValue == 0 && (SockPoll (s, nPid) & KAPI_POLLIN)) nValue = 1;	// (more is ready)
		break;
	case KAPI_SO_TYPE:	  nValue = V.nType; break;
	case KAPI_SO_ACCEPTCONN:  nValue = V.nState == NET_SS_LISTEN ? 1 : 0; break;
	default:		  return -KAPI_ENOPROTOOPT;
	}
	return UserPut (pValue, nValue) ? 0 : -KAPI_EFAULT;
}

int kapi_sock_setopt (int s, int nOpt, int nValue)
{
	return NetSockSetOpt (s, Caller (s), nOpt, nValue);
}

int kapi_sock_name (int s, int nPeer, struct kapi_sockaddr *pOut)
{
	if (!UserRange (pOut, sizeof *pOut)) return -KAPI_EFAULT;
	unsigned nPid = Caller (s);
	TNetSockView V;
	if (NetSockView (s, nPid, &V) < 0) return -KAPI_EBADF;
	boolean bConnected = V.nType == KAPI_SOCK_STREAM ? V.nState == NET_SS_CONNECTED : V.nPeerPort != 0;
	if (nPeer)
	{
		if (!bConnected) return -KAPI_ENOTCONN;
		return PutAddr (pOut, V.PeerIP, V.nPeerPort) ? 0 : -KAPI_EFAULT;
	}
	u8 IP[4] = {0, 0, 0, 0};
	if (bConnected) NetOwnIP (IP);				// (bound to "any" until then)
	return PutAddr (pOut, IP, V.nLocalPort) ? 0 : -KAPI_EFAULT;
}

// ---- poll ---------------------------------------------------------------------------------------

#define POLL_INLINE	32		// entries on the stack (more: the heap)

// The readiness of one entry (revents before the events mask), HasSocket set for a socket.
static unsigned PollOne (const struct kapi_pollfd &f, unsigned nPid, boolean *pSocket)
{
	if (f.kind == KAPI_PK_NONE || f.h < 0) return 0;		// (ignored)
	switch (f.kind)
	{
	case KAPI_PK_SOCKET:
		*pSocket = TRUE;
		if (f.h < SOCK_LOCAL_BASE && NetSocketOwner (f.h) != nPid) SocketAdopt (f.h);
		return SockPoll (f.h, nPid);

	case KAPI_PK_STREAM:
	{
		CHandleTable *pTable = HandlesCurrent ();
		CStream *pStream = pTable != 0 ? (CStream *) pTable->Get ((void *) (uintptr) (unsigned) f.h, HANDLE_STREAM) : 0;
		return pStream != 0 ? pStream->PollMask () : KAPI_POLLNVAL;	// (no yield in between)
	}

	case KAPI_PK_FILE:
	{
		CHandleTable *pTable = HandlesCurrent ();
		void *h = (void *) (uintptr) (unsigned) f.h;
		if (pTable != 0 && (   pTable->Get (h, HANDLE_FILE) != 0 || pTable->Get (h, HANDLE_OFILE) != 0
				    || pTable->Get (h, HANDLE_DIR) != 0))
		{
			return KAPI_POLLIN | KAPI_POLLOUT;		// (a file never blocks)
		}
		return KAPI_POLLNVAL;
	}

	default:
		return KAPI_POLLNVAL;
	}
}

int kapi_poll (struct kapi_pollfd *pFds, unsigned n, int nTimeoutMs)
{
	if (n > KAPI_POLL_MAX) return -KAPI_EINVAL;
	struct kapi_pollfd Inline[POLL_INLINE];
	struct kapi_pollfd *pK = Inline;
	if (n > POLL_INLINE)
	{
		pK = new struct kapi_pollfd[n];
		if (pK == 0) return -KAPI_ENOMEM;
	}
	if (n > 0 && !UserCopyIn (pK, pFds, (u64) n * sizeof *pK))
	{
		if (pK != Inline) delete [] pK;
		return -KAPI_EFAULT;
	}

	unsigned nPid = NetCurrentPid ();
	TDeadline Dl (nTimeoutMs > 0 ? (unsigned) nTimeoutMs : 0);
	unsigned nRound = 0;
	int nReady;
	for (;;)
	{
		u32 nGen = IoGen ();				// (before looking: no wake lost)
		nReady = 0;
		boolean bSockets = FALSE;
		for (unsigned i = 0; i < n; i++)
		{
			unsigned m = PollOne (pK[i], nPid, &bSockets);
			// the asked events, and always the error, hang-up and invalid bits
			m &= (unsigned) (unsigned short) pK[i].events | KAPI_POLLERR | KAPI_POLLHUP | KAPI_POLLNVAL;
			pK[i].revents = (short) m;
			if (m != 0) nReady++;
		}
		if (nReady > 0 || nTimeoutMs == 0) break;
		unsigned nLeft = nTimeoutMs < 0 ? KAPI_WAIT_FOREVER : Dl.Left ();
		if (nLeft == 0) break;
		if (bSockets || nRound < WAIT_SPINS) WaitStep (nGen, &nRound, nLeft);
		else IoWait (nGen, nLeft);			// (streams and files: their changes wake us)
	}

	boolean bOK = n == 0 || UserCopyOut (pFds, pK, (u64) n * sizeof *pK);
	if (pK != Inline) delete [] pK;
	return bOK ? nReady : -KAPI_EFAULT;
}

}
