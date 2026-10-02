//
// lsock.cpp -- local sockets and the handles they carry between processes (kapi v76, WP-IPC:
// sock_pair, sock_sendmsg, sock_recvmsg, and the local half of every sock_* call and of poll;
// docs/POSIX-PLAN.md §14, docs/02 §8 "v76: IPC"). See kern/lsock.h.
//
// An end (TLsEnd) has a receive queue of messages (TLsMsg: the data, the handles carried). A send
// copies the caller's data into a new message on the PEER's queue; a receive copies out of its own
// queue. STREAM ends merge the messages (never past one that carried handles); SEQPACKET and DGRAM
// ends keep each whole. Nothing here waits with the interrupts masked or in the middle of a change:
// a blocking call looks, then sleeps on the I/O generation (IoWait), and every change calls IoWake.
// An end is referenced by the handle entries naming it (HANDLE_LSOCK), the messages carrying it and
// the spawn records; when the last goes, its queue is freed (the handles in it closed) and its peer
// sees the end. A call pins its handle (the table's Pin): a close from another thread meanwhile only
// marks it, so the end stays while the call runs.
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
#include <kern/lsock.h>
#include <kern/kapi_abi.h>
#include <kern/handle.h>
#include <kern/stream.h>
#include <kern/net.h>
#include <kern/iowait.h>
#include <kern/uaccess.h>
#include <circle/timer.h>
#include <circle/new.h>
#include <circle/util.h>

#define LS_MAGIC	0x4C534F43u		// "LSOC": a live TLsEnd
#define LS_IO_MAX	0x40000000ULL		// one call moves at most 1 GB

// ---- the objects ------------------------------------------------------------------------------

struct TLsMsg					// one allocation: this, the handles, the data
{
	TLsMsg	 *pNext;
	u32	  nLen;				// data bytes
	u32	  nOff;				// STREAM: bytes already read
	u32	  nXfer;			// handles carried (0 once delivered)
	u32	  nPad;
	TIpcXfer *pXfer;
	u8	 *pData;
};

struct TLsEnd
{
	u32	 nMagic;
	int	 nRefs;				// handle entries, carried copies, spawn records
	int	 nType;				// KAPI_SOCK_STREAM / _SEQPACKET / _DGRAM
	boolean	 bNonBlock;
	boolean	 bShutRd;			// SHUT_RD here: reads end, the peer's sends fail
	boolean	 bShutWr;			// SHUT_WR here: the peer reads the end, our sends fail
	boolean	 bPeerGone;			// the peer end was freed
	TLsEnd	*pPeer;
	TLsMsg	*pHead, *pTail;			// the receive queue
	u64	 nQueued;			// its unread data bytes
	unsigned nMsgs;
	unsigned nRcvBuf, nSndBuf;
	unsigned nRcvTimeoutMs, nSndTimeoutMs;
	unsigned nPid;				// the process that made or last received it
};

static u64	s_nTotal;			// queued data bytes, every end
static unsigned	s_nEnds;			// ends alive

static TLsEnd *EndNew (int nType, boolean bNonBlock, unsigned nPid)
{
	TLsEnd *e = new TLsEnd;
	if (e == 0) return 0;
	memset (e, 0, sizeof *e);
	e->nMagic = LS_MAGIC;
	e->nRefs = 1;
	e->nType = nType;
	e->bNonBlock = bNonBlock;
	e->nRcvBuf = IPC_LS_DEFAULT_BUF;
	e->nSndBuf = IPC_LS_DEFAULT_BUF;
	e->nPid = nPid;
	s_nEnds++;
	return e;
}

static TLsMsg *MsgNew (u64 nLen, unsigned nXfer)
{
	u64 nSize = sizeof (TLsMsg) + (u64) nXfer * sizeof (TIpcXfer) + nLen;
	u8 *p = new u8[nSize];
	if (p == 0) return 0;
	TLsMsg *m = (TLsMsg *) p;
	memset (m, 0, sizeof *m);
	m->nLen = (u32) nLen;
	m->nXfer = nXfer;
	m->pXfer = (TIpcXfer *) (p + sizeof (TLsMsg));
	m->pData = (u8 *) (m->pXfer + nXfer);
	return m;
}

static void MsgFree (TLsMsg *m, boolean bTeardown)
{
	for (unsigned i = 0; i < m->nXfer; i++) IpcXferDrop (&m->pXfer[i], bTeardown);
	delete [] (u8 *) m;
}

static void Enqueue (TLsEnd *e, TLsMsg *m)
{
	m->pNext = 0;
	if (e->pTail != 0) e->pTail->pNext = m; else e->pHead = m;
	e->pTail = m;
	e->nQueued += m->nLen;
	e->nMsgs++;
	s_nTotal += m->nLen;
}

static TLsMsg *Dequeue (TLsEnd *e)		// the head, out of the queue (its unread bytes uncounted)
{
	TLsMsg *m = e->pHead;
	if (m == 0) return 0;
	e->pHead = m->pNext;
	if (e->pHead == 0) e->pTail = 0;
	u64 nLeft = m->nLen - m->nOff;
	e->nQueued -= nLeft;
	e->nMsgs--;
	s_nTotal -= nLeft;
	return m;
}

void IpcLocalRelease (void *pObj, boolean bTeardown)
{
	TLsEnd *e = (TLsEnd *) pObj;
	if (e == 0 || e->nMagic != LS_MAGIC || --e->nRefs > 0) return;
	if (e->pPeer != 0)
	{
		e->pPeer->pPeer = 0;
		e->pPeer->bPeerGone = TRUE;
		e->pPeer = 0;
	}
	e->nMagic = 0;					// (the queue may carry handles naming e: no use now)
	while (e->pHead != 0) MsgFree (Dequeue (e), bTeardown);
	delete e;
	s_nEnds--;
	IoWake ();					// (the peer: the end, EPIPE)
}

// ---- carried handles ----------------------------------------------------------------------------

static inline void *HandleOf (long long h)
{
	return h > 0 && h <= 0xFFFFFF ? (void *) (uintptr) (unsigned) h : (void *) (uintptr) 0x1000000;
}

int IpcXferTake (CHandleTable *pTable, const struct kapi_handle_xfer &In, unsigned nPid, TIpcXfer *pOut)
{
	memset (pOut, 0, sizeof *pOut);
	pOut->nTag = In.tag;
	pOut->nHK = (u8) In.kind;
	unsigned nKind = 0;
	void *pObj = 0;
	switch (In.kind)
	{
	case KAPI_HK_SOCKET:
	{
		if (In.h < 0 || In.h >= KAPI_SOCK_LOCAL_BASE) return -KAPI_EBADF;
		int s = (int) In.h;
		SocketAdopt (s);				// (an ancestor's: ours first)
		if (NetSocketOwner (s) != nPid || nPid == 0) return -KAPI_EBADF;
		pOut->nSock = s;
		pOut->nPid = nPid;
		return 0;
	}

	case KAPI_HK_OFILE:
		pObj = pTable != 0 ? pTable->Get (HandleOf (In.h), HANDLE_OFILE, &nKind) : 0;
		if (pObj == 0) return -KAPI_EBADF;
		OFileRef (pObj);
		pOut->nType = HANDLE_OFILE;
		break;

	case KAPI_HK_STREAM:
		pObj = pTable != 0 ? pTable->Get (HandleOf (In.h), HANDLE_STREAM, &nKind) : 0;
		if (pObj == 0) return -KAPI_EBADF;
		((CStream *) pObj)->AddRef ();
		pOut->nType = HANDLE_STREAM;
		break;

	case KAPI_HK_LSOCK:
		pObj = pTable != 0 ? pTable->Get (HandleOf (In.h), HANDLE_LSOCK, &nKind) : 0;
		if (pObj == 0) return -KAPI_EBADF;
		((TLsEnd *) pObj)->nRefs++;
		pOut->nType = HANDLE_LSOCK;
		break;

	case KAPI_HK_SHM:
		pObj = pTable != 0 ? pTable->Get (HandleOf (In.h), HANDLE_SHM, &nKind) : 0;
		if (pObj == 0) return -KAPI_EBADF;
		ShmRef (pObj);
		pOut->nType = HANDLE_SHM;
		break;

	default:
		return -KAPI_EINVAL;
	}
	pOut->pObj = pObj;
	pOut->nKind = (u8) nKind;
	return 0;
}

void IpcXferDrop (TIpcXfer *p, boolean bTeardown)
{
	if (p->pObj != 0)
	{
		void *pObj = p->pObj;
		p->pObj = 0;
		HandleObjectClose (pObj, p->nType, p->nKind, bTeardown);
	}
	p->nHK = KAPI_HK_NONE;
}

boolean IpcXferGive (CHandleTable *pTable, TIpcXfer *p, unsigned nPid, boolean bAdopt,
		     struct kapi_handle_xfer *pOut)
{
	memset (pOut, 0, sizeof *pOut);
	pOut->h = -1;
	pOut->fd = -1;
	pOut->tag = p->nTag;
	if (p->nHK == KAPI_HK_SOCKET)
	{
		boolean bOK = !bAdopt || NetSocketOwner (p->nSock) == nPid
			   || NetSocketAdopt (p->nSock, p->nPid, nPid);
		p->nHK = KAPI_HK_NONE;
		if (!bOK) return FALSE;			// (the sender closed it meanwhile)
		pOut->h = p->nSock;
		pOut->kind = KAPI_HK_SOCKET;
		return TRUE;
	}
	if (p->pObj == 0) return FALSE;
	void *h = pTable != 0 ? pTable->Add (p->pObj, p->nType, p->nKind) : 0;
	if (h == 0)
	{
		IpcXferDrop (p, FALSE);
		return FALSE;
	}
	if (p->nType == HANDLE_LSOCK) ((TLsEnd *) p->pObj)->nPid = nPid;
	p->pObj = 0;					// (the reference is the entry's now)
	pOut->h = (long long) (uintptr) h;
	pOut->kind = p->nHK;
	p->nHK = KAPI_HK_NONE;
	return TRUE;
}

// ---- the calls' common parts ------------------------------------------------------------------

struct TLsDeadline				// ms from now; 0: none
{
	TLsDeadline (unsigned nMs) : m_nStart (CTimer::GetClockTicks ()), m_nMs (nMs) {}
	unsigned Left (void) const
	{
		if (m_nMs == 0) return KAPI_WAIT_FOREVER;
		unsigned nGone = (CTimer::GetClockTicks () - m_nStart) / 1000;
		return nGone >= m_nMs ? 0 : m_nMs - nGone;
	}
	unsigned m_nStart, m_nMs;
};

// The caller's end s, pinned while this lives.
class CLsUse
{
public:
	CLsUse (int s) : m_pTable (HandlesCurrent ()), m_pEnd (0), m_nIdx (0)
	{
		if (m_pTable == 0 || !IpcIsLocal (s)) return;
		TLsEnd *e = (TLsEnd *) m_pTable->Pin ((void *) (uintptr) (unsigned) s, HANDLE_LSOCK, &m_nIdx);
		if (e != 0 && e->nMagic != LS_MAGIC)
		{
			m_pTable->Unpin (m_nIdx);
			e = 0;
		}
		m_pEnd = e;
	}
	~CLsUse (void)		{ if (m_pEnd != 0) m_pTable->Unpin (m_nIdx); }
	TLsEnd *End (void) const	{ return m_pEnd; }

private:
	CLsUse (const CLsUse &);
	CLsUse &operator= (const CLsUse &);
	CHandleTable *m_pTable;
	TLsEnd	     *m_pEnd;
	unsigned      m_nIdx;
};

static inline boolean IsStream (const TLsEnd *e)	{ return e->nType == KAPI_SOCK_STREAM; }

// Would a send fail at once? -> -EPIPE, or 0.
static int SendError (const TLsEnd *e)
{
	if (e->bShutWr || e->pPeer == 0 || e->pPeer->bShutRd) return -KAPI_EPIPE;
	return 0;
}

// Is the end of the data reached (once the queue is empty)?
static boolean AtEnd (const TLsEnd *e)
{
	return e->bShutRd || e->pPeer == 0 || e->pPeer->bShutWr;
}

static unsigned Readiness (const TLsEnd *e)
{
	unsigned m = 0;
	if (e->pHead != 0 || AtEnd (e)) m |= KAPI_POLLIN;
	const TLsEnd *p = e->pPeer;
	if (SendError (e) != 0 || p->nQueued < p->nRcvBuf) m |= KAPI_POLLOUT;
	if (e->pPeer == 0 || (e->bShutWr && (e->bShutRd || e->pPeer->bShutWr))) m |= KAPI_POLLHUP;
	return m;
}

// Gather nLen bytes from the user's iovecs, from byte nFrom on, into pDst. FALSE: a fault.
static boolean Gather (u8 *pDst, const struct kapi_iovec *pIov, unsigned nIov, u64 nFrom, u64 nLen)
{
	for (unsigned i = 0; i < nIov && nLen > 0; i++)
	{
		u64 nPiece = pIov[i].len;
		if (nFrom >= nPiece) { nFrom -= nPiece; continue; }
		u64 n = nPiece - nFrom;
		if (n > nLen) n = nLen;
		if (!UserCopyIn (pDst, (const u8 *) (uintptr) pIov[i].base + nFrom, n)) return FALSE;
		pDst += n;
		nLen -= n;
		nFrom = 0;
	}
	return nLen == 0;
}

// Scatter nLen bytes into the user's iovecs from byte nAt on. FALSE: a fault.
static boolean Scatter (const struct kapi_iovec *pIov, unsigned nIov, u64 nAt, const u8 *pSrc, u64 nLen)
{
	for (unsigned i = 0; i < nIov && nLen > 0; i++)
	{
		u64 nPiece = pIov[i].len;
		if (nAt >= nPiece) { nAt -= nPiece; continue; }
		u64 n = nPiece - nAt;
		if (n > nLen) n = nLen;
		if (!UserCopyOut ((u8 *) (uintptr) pIov[i].base + nAt, pSrc, n)) return FALSE;
		pSrc += n;
		nLen -= n;
		nAt = 0;
	}
	return nLen == 0;
}

// The iovecs checked (each readable / writable: probed and pinned for the call) -> their total, or
// -KAPI_EFAULT / -KAPI_EINVAL.
static long long CheckIov (const struct kapi_iovec *pIov, unsigned nIov, boolean bWrite)
{
	u64 nTotal = 0;
	for (unsigned i = 0; i < nIov; i++)
	{
		u64 n = pIov[i].len;
		if (n == 0) continue;
		if (n > LS_IO_MAX || nTotal + n > LS_IO_MAX) return -KAPI_EINVAL;
		void *p = (void *) (uintptr) pIov[i].base;
		if (bWrite ? !UserWritable (p, n) : !UserReadable (p, n)) return -KAPI_EFAULT;
		nTotal += n;
	}
	return (long long) nTotal;
}

// Send nTotal bytes of the iovecs (and the nX handles of pX, moved into the first message: then
// *pbMoved; else they stay the caller's) from e to its peer -> bytes / -errno.
static long long SendCore (TLsEnd *e, const struct kapi_iovec *pIov, unsigned nIov, u64 nTotal,
			   TIpcXfer *pX, unsigned nX, unsigned nFlags, boolean *pbMoved)
{
	*pbMoved = FALSE;
	if (IsStream (e) && nTotal == 0) return nX > 0 ? -KAPI_EINVAL : 0;	// (handles need a byte)
	if (!IsStream (e) && nTotal > e->nSndBuf) return -KAPI_EMSGSIZE;
	boolean bNonBlock = e->bNonBlock || (nFlags & KAPI_MSG_DONTWAIT);
	TLsDeadline Dl (e->nSndTimeoutMs);
	u64 nDone = 0;
	for (;;)
	{
		u32 nGen = IoGen ();
		int nErr = SendError (e);
		if (nErr != 0) return nDone > 0 ? (long long) nDone : nErr;
		TLsEnd *p = e->pPeer;
		u64 nRoom = p->nQueued < p->nRcvBuf ? p->nRcvBuf - p->nQueued : 0;
		u64 n = 0;
		if (IsStream (e))
		{
			n = nTotal - nDone;
			if (n > nRoom) n = nRoom;
		}
		else if (p->nMsgs == 0 || nTotal <= nRoom)
		{
			n = nTotal;
		}
		boolean bFits = IsStream (e) ? n > 0 : (p->nMsgs == 0 || nTotal <= nRoom);
		if (bFits)
		{
			if (s_nTotal + n > IPC_LS_TOTAL_MAX)
			{
				if (!IsStream (e) || s_nTotal >= IPC_LS_TOTAL_MAX)
				{
					return nDone > 0 ? (long long) nDone : -KAPI_ENOBUFS;
				}
				n = IPC_LS_TOTAL_MAX - s_nTotal;
			}
			unsigned nMsgX = nDone == 0 ? nX : 0;
			TLsMsg *m = MsgNew (n, nMsgX);
			if (m == 0) return nDone > 0 ? (long long) nDone : -KAPI_ENOMEM;
			if (!Gather (m->pData, pIov, nIov, nDone, n))
			{
				m->nXfer = 0;
				MsgFree (m, FALSE);
				return nDone > 0 ? (long long) nDone : -KAPI_EFAULT;
			}
			if (nMsgX > 0)
			{
				memcpy (m->pXfer, pX, nMsgX * sizeof (TIpcXfer));
				*pbMoved = TRUE;
			}
			Enqueue (p, m);
			IoWake ();
			nDone += n;
			if (nDone >= nTotal) return (long long) nDone;
			continue;
		}
		if (bNonBlock) return nDone > 0 ? (long long) nDone : -KAPI_EAGAIN;
		unsigned nLeft = Dl.Left ();
		if (nLeft == 0) return nDone > 0 ? (long long) nDone : -KAPI_EAGAIN;	// (SO_SNDTIMEO)
		IoWait (nGen, nLeft);
	}
}

// Receive into the iovecs (nTotal bytes of room). The handles of the message read are moved to
// *ppX (a new array of *pnX, the caller's to deliver and delete) unless bPeek. *pOutFlags:
// KAPI_MSG_TRUNC. -> bytes, 0 the end / -errno.
static long long RecvCore (TLsEnd *e, const struct kapi_iovec *pIov, unsigned nIov, u64 nTotal,
			   unsigned nFlags, TIpcXfer **ppX, unsigned *pnX, unsigned *pOutFlags)
{
	*ppX = 0;
	*pnX = 0;
	*pOutFlags = 0;
	boolean bNonBlock = e->bNonBlock || (nFlags & KAPI_MSG_DONTWAIT);
	boolean bPeek = (nFlags & KAPI_MSG_PEEK) != 0;
	boolean bAll = (nFlags & KAPI_MSG_WAITALL) != 0 && !bPeek && IsStream (e);
	TLsDeadline Dl (e->nRcvTimeoutMs);
	u64 nDone = 0;
	for (;;)
	{
		u32 nGen = IoGen ();
		if (e->pHead != 0)
		{
			if (!IsStream (e))			// one message, whole or cut
			{
				TLsMsg *m = e->pHead;
				u64 n = m->nLen < nTotal ? m->nLen : nTotal;
				if (!Scatter (pIov, nIov, 0, m->pData, n)) return -KAPI_EFAULT;
				if (m->nLen > nTotal) *pOutFlags |= KAPI_MSG_TRUNC;
				if (!bPeek)
				{
					Dequeue (e);
					if (m->nXfer > 0)
					{
						*ppX = new TIpcXfer[m->nXfer];
						if (*ppX != 0)
						{
							memcpy (*ppX, m->pXfer, m->nXfer * sizeof (TIpcXfer));
							*pnX = m->nXfer;
							m->nXfer = 0;
						}
					}
					MsgFree (m, FALSE);		// (handles not taken: closed)
					IoWake ();			// (room for the sender)
				}
				return (long long) n;
			}

			// STREAM: across messages, up to (and including) one that carried handles
			boolean bTook = FALSE, bStop = FALSE, bHeld = FALSE;
			TLsMsg *m = e->pHead;
			u32 nOff = m->nOff;
			while (m != 0 && nDone < nTotal && !bStop)
			{
				if (m->nXfer > 0 && m->nOff == 0 && nOff == 0 && (nDone > 0 || bTook))
				{
					bHeld = TRUE;			// (its handles: the next read's)
					break;
				}
				u64 n = m->nLen - nOff;
				if (n > nTotal - nDone) n = nTotal - nDone;
				if (!Scatter (pIov, nIov, nDone, m->pData + nOff, n))
				{
					return nDone > 0 ? (long long) nDone : -KAPI_EFAULT;
				}
				nDone += n;
				if (bPeek)
				{
					m = m->pNext;
					nOff = 0;
					continue;
				}
				bTook = TRUE;
				if (m->nXfer > 0 && m->nOff == 0)	// its handles, with its first byte
				{
					*ppX = new TIpcXfer[m->nXfer];
					if (*ppX != 0)
					{
						memcpy (*ppX, m->pXfer, m->nXfer * sizeof (TIpcXfer));
						*pnX = m->nXfer;
						m->nXfer = 0;
					}
					bStop = TRUE;
				}
				m->nOff += (u32) n;
				e->nQueued -= n;
				s_nTotal -= n;
				if (m->nOff == m->nLen)
				{
					TLsMsg *pDone = m;
					m = m->pNext;
					e->pHead = m;
					if (m == 0) e->pTail = 0;
					e->nMsgs--;
					MsgFree (pDone, FALSE);
				}
				nOff = m != 0 ? m->nOff : 0;
			}
			if (bTook) IoWake ();
			if (nDone > 0 && (!bAll || nDone == nTotal || bStop || bHeld || bPeek)) return (long long) nDone;
		}
		if (nDone > 0 && !bAll) return (long long) nDone;
		if (AtEnd (e)) return (long long) nDone;		// (0: the end)
		if (bNonBlock) return nDone > 0 ? (long long) nDone : -KAPI_EAGAIN;
		unsigned nLeft = Dl.Left ();
		if (nLeft == 0) return nDone > 0 ? (long long) nDone : -KAPI_EAGAIN;	// (SO_RCVTIMEO)
		IoWait (nGen, nLeft);
	}
}

// Deliver nX handles into the caller's table and the user's array of nCap (0: none). -> how many
// were written; *pTrunc set when some were dropped.
static unsigned Deliver (TIpcXfer *pX, unsigned nX, struct kapi_handle_xfer *pUser, unsigned nCap,
			 boolean *pTrunc)
{
	CHandleTable *pTable = HandlesCurrent ();
	unsigned nPid = NetCurrentPid ();
	unsigned nOut = 0;
	for (unsigned i = 0; i < nX; i++)
	{
		struct kapi_handle_xfer Out;
		if (nOut >= nCap)
		{
			IpcXferDrop (&pX[i], FALSE);
			*pTrunc = TRUE;
			continue;
		}
		boolean bIP = pX[i].nHK == KAPI_HK_SOCKET;
		if (!IpcXferGive (pTable, &pX[i], nPid, TRUE, &Out) && !bIP)
		{
			*pTrunc = TRUE;				// (the table full: closed)
			continue;
		}						// (an IP socket gone: {-1, NONE})
		if (!UserCopyOut (&pUser[nOut], &Out, sizeof Out))
		{
			*pTrunc = TRUE;				// (written before: cannot be)
			continue;
		}
		nOut++;
	}
	return nOut;
}

// ---- bsdsock.cpp's dispatch -------------------------------------------------------------------------

unsigned IpcLocalPoll (int s)
{
	CLsUse Use (s);
	return Use.End () != 0 ? Readiness (Use.End ()) : KAPI_POLLNVAL;
}

long long IpcLocalSend (int s, const void *pBuf, u64 nLen, unsigned nFlags)
{
	if (nLen > LS_IO_MAX) nLen = LS_IO_MAX;
	if (nLen > 0 && !UserReadable (pBuf, nLen)) return -KAPI_EFAULT;
	CLsUse Use (s);
	if (Use.End () == 0) return -KAPI_EBADF;
	struct kapi_iovec Iov = { (u64) (uintptr) pBuf, nLen };
	boolean bMoved;
	return SendCore (Use.End (), &Iov, 1, nLen, 0, 0, nFlags, &bMoved);
}

long long IpcLocalRecv (int s, void *pBuf, u64 nLen, unsigned nFlags)
{
	if (nLen > LS_IO_MAX) nLen = LS_IO_MAX;
	if (nLen > 0 && !UserWritable (pBuf, nLen)) return -KAPI_EFAULT;
	CLsUse Use (s);
	if (Use.End () == 0) return -KAPI_EBADF;
	struct kapi_iovec Iov = { (u64) (uintptr) pBuf, nLen };
	TIpcXfer *pX;
	unsigned nX, nOutFlags;
	long long r = RecvCore (Use.End (), &Iov, 1, nLen, nFlags, &pX, &nX, &nOutFlags);
	if (pX != 0)					// (recv: no room for handles -- closed)
	{
		for (unsigned i = 0; i < nX; i++) IpcXferDrop (&pX[i], FALSE);
		delete [] pX;
	}
	return r;
}

int IpcLocalShutdown (int s, int nHow)
{
	CLsUse Use (s);
	TLsEnd *e = Use.End ();
	if (e == 0) return -KAPI_EBADF;
	if (e->pPeer == 0 && !e->bPeerGone) return -KAPI_ENOTCONN;
	if (nHow == KAPI_SHUT_RD || nHow == KAPI_SHUT_RDWR) e->bShutRd = TRUE;
	if (nHow == KAPI_SHUT_WR || nHow == KAPI_SHUT_RDWR) e->bShutWr = TRUE;
	IoWake ();
	return 0;
}

int IpcLocalClose (int s)
{
	CHandleTable *pTable = HandlesCurrent ();
	return pTable != 0 && pTable->Close ((void *) (uintptr) (unsigned) s, HANDLE_LSOCK) ? 0 : -KAPI_EBADF;
}

int IpcLocalGetOpt (int s, int nOpt, int *pValue)
{
	CLsUse Use (s);
	TLsEnd *e = Use.End ();
	if (e == 0) return -KAPI_EBADF;
	switch (nOpt)
	{
	case KAPI_SO_ERROR:	  *pValue = 0; break;
	case KAPI_SO_NONBLOCK:	  *pValue = e->bNonBlock ? 1 : 0; break;
	case KAPI_SO_RCVTIMEO_MS: *pValue = (int) e->nRcvTimeoutMs; break;
	case KAPI_SO_SNDTIMEO_MS: *pValue = (int) e->nSndTimeoutMs; break;
	case KAPI_SO_BROADCAST:	  *pValue = 0; break;
	case KAPI_SO_NREAD:
		if (e->pHead == 0) *pValue = 0;
		else if (IsStream (e)) *pValue = e->nQueued > 0x7FFFFFFF ? 0x7FFFFFFF : (int) e->nQueued;
		else *pValue = (int) e->pHead->nLen;
		break;
	case KAPI_SO_TYPE:	  *pValue = e->nType; break;
	case KAPI_SO_ACCEPTCONN:  *pValue = 0; break;
	case KAPI_SO_RCVBUF:	  *pValue = (int) e->nRcvBuf; break;
	case KAPI_SO_SNDBUF:	  *pValue = (int) e->nSndBuf; break;
	case KAPI_SO_PEERPID:
		if (e->pPeer == 0) return -KAPI_ENOTCONN;
		*pValue = (int) e->pPeer->nPid;
		break;
	case KAPI_SO_DOMAIN:	  *pValue = KAPI_AF_UNIX; break;
	default:		  return -KAPI_ENOPROTOOPT;
	}
	return 0;
}

int IpcLocalSetOpt (int s, int nOpt, int nValue)
{
	CLsUse Use (s);
	TLsEnd *e = Use.End ();
	if (e == 0) return -KAPI_EBADF;
	unsigned nBuf = nValue < IPC_LS_MIN_BUF ? IPC_LS_MIN_BUF
		      : nValue > IPC_LS_MAX_BUF ? IPC_LS_MAX_BUF : (unsigned) nValue;
	switch (nOpt)
	{
	case KAPI_SO_NONBLOCK:	  e->bNonBlock = nValue != 0; break;
	case KAPI_SO_RCVTIMEO_MS: if (nValue < 0) return -KAPI_EINVAL; e->nRcvTimeoutMs = (unsigned) nValue; break;
	case KAPI_SO_SNDTIMEO_MS: if (nValue < 0) return -KAPI_EINVAL; e->nSndTimeoutMs = (unsigned) nValue; break;
	case KAPI_SO_BROADCAST:	  break;
	case KAPI_SO_RCVBUF:	  e->nRcvBuf = nBuf; IoWake (); break;
	case KAPI_SO_SNDBUF:	  e->nSndBuf = nBuf; break;
	default:		  return -KAPI_ENOPROTOOPT;
	}
	return 0;
}

int IpcLocalName (int s, int nPeer)
{
	CLsUse Use (s);
	TLsEnd *e = Use.End ();
	if (e == 0) return -KAPI_EBADF;
	if (nPeer && e->pPeer == 0) return -KAPI_ENOTCONN;
	return 0;
}

// ---- the kapis ------------------------------------------------------------------------------------

extern "C" {

int kapi_sock_pair (int nType, unsigned nFlags, int *pSv)
{
	if (nType != KAPI_SOCK_STREAM && nType != KAPI_SOCK_SEQPACKET && nType != KAPI_SOCK_DGRAM)
	{
		return -KAPI_EPROTONOSUPPORT;
	}
	if (nFlags & ~KAPI_SOCKF_NONBLOCK) return -KAPI_EINVAL;
	if (pSv == 0 || !UserRange (pSv, 2 * sizeof (int))) return -KAPI_EFAULT;
	CHandleTable *pTable = HandlesCurrent ();
	if (pTable == 0) return -KAPI_ENOMEM;
	unsigned nPid = NetCurrentPid ();
	boolean bNB = (nFlags & KAPI_SOCKF_NONBLOCK) != 0;
	TLsEnd *a = EndNew (nType, bNB, nPid), *b = EndNew (nType, bNB, nPid);
	if (a == 0 || b == 0)
	{
		if (a != 0) IpcLocalRelease (a, FALSE);
		if (b != 0) IpcLocalRelease (b, FALSE);
		return -KAPI_ENOMEM;
	}
	a->pPeer = b;
	b->pPeer = a;
	void *ha = pTable->Add (a, HANDLE_LSOCK);
	if (ha == 0)
	{
		IpcLocalRelease (a, FALSE);
		IpcLocalRelease (b, FALSE);
		return -KAPI_EMFILE;
	}
	void *hb = pTable->Add (b, HANDLE_LSOCK);
	if (hb == 0)
	{
		pTable->Close (ha, HANDLE_LSOCK);
		IpcLocalRelease (b, FALSE);
		return -KAPI_EMFILE;
	}
	int Sv[2] = { (int) (uintptr) ha, (int) (uintptr) hb };
	if (!UserCopyOut (pSv, Sv, sizeof Sv))
	{
		pTable->Close (ha, HANDLE_LSOCK);
		pTable->Close (hb, HANDLE_LSOCK);
		return -KAPI_EFAULT;
	}
	return 0;
}

// The message header and its iovecs copied in (pIov: KAPI_IPC_IOV_MAX entries).
static int GetMsg (const struct kapi_msghdr *pUser, struct kapi_msghdr *pM, struct kapi_iovec *pIov)
{
	if (pUser == 0 || !UserGet (pM, pUser)) return -KAPI_EFAULT;
	if (pM->iovcnt > KAPI_IPC_IOV_MAX || pM->nhandles > KAPI_IPC_HANDLES_MAX) return -KAPI_EINVAL;
	if (pM->iovcnt > 0 && (pM->iov == 0 || !UserCopyIn (pIov, pM->iov, pM->iovcnt * sizeof *pIov)))
	{
		return -KAPI_EFAULT;
	}
	return 0;
}

long long kapi_sock_sendmsg (int s, const struct kapi_msghdr *pUser, unsigned nFlags)
{
	if (!IpcIsLocal (s)) return s >= 0 ? -KAPI_EOPNOTSUPP : -KAPI_EBADF;
	struct kapi_msghdr M;
	struct kapi_iovec Iov[KAPI_IPC_IOV_MAX];
	int r = GetMsg (pUser, &M, Iov);
	if (r < 0) return r;
	long long nTotal = CheckIov (Iov, M.iovcnt, FALSE);
	if (nTotal < 0) return nTotal;
	CLsUse Use (s);
	TLsEnd *e = Use.End ();
	if (e == 0) return -KAPI_EBADF;

	TIpcXfer *pX = 0;
	unsigned nX = M.nhandles;
	if (nX > 0)
	{
		struct kapi_handle_xfer *pIn = new struct kapi_handle_xfer[nX];
		pX = new TIpcXfer[nX];
		if (pIn == 0 || pX == 0)
		{
			delete [] pIn; delete [] pX;
			return -KAPI_ENOMEM;
		}
		if (M.handles == 0 || !UserCopyIn (pIn, M.handles, nX * sizeof *pIn))
		{
			delete [] pIn; delete [] pX;
			return -KAPI_EFAULT;
		}
		CHandleTable *pTable = HandlesCurrent ();
		unsigned nPid = NetCurrentPid ();
		for (unsigned i = 0; i < nX; i++)
		{
			r = 0;
			if (   pIn[i].kind == KAPI_HK_LSOCK && pTable != 0 && e->pPeer != 0
			    && pTable->Get (HandleOf (pIn[i].h), HANDLE_LSOCK) == e->pPeer)
			{
				r = -KAPI_EINVAL;		// (the receiving end itself: a cycle)
			}
			if (r == 0) r = IpcXferTake (pTable, pIn[i], nPid, &pX[i]);
			if (r < 0)
			{
				for (unsigned k = 0; k < i; k++) IpcXferDrop (&pX[k], FALSE);
				delete [] pIn; delete [] pX;
				return r;
			}
		}
		delete [] pIn;
	}
	boolean bMoved;
	long long n = SendCore (e, Iov, M.iovcnt, (u64) nTotal, pX, nX, nFlags, &bMoved);
	if (!bMoved && pX != 0)
	{
		for (unsigned i = 0; i < nX; i++) IpcXferDrop (&pX[i], FALSE);	// (not sent)
	}
	delete [] pX;
	return n;
}

long long kapi_sock_recvmsg (int s, struct kapi_msghdr *pUser, unsigned nFlags)
{
	if (!IpcIsLocal (s)) return s >= 0 ? -KAPI_EOPNOTSUPP : -KAPI_EBADF;
	struct kapi_msghdr M;
	struct kapi_iovec Iov[KAPI_IPC_IOV_MAX];
	int r = GetMsg (pUser, &M, Iov);
	if (r < 0) return r;
	if (!UserWritable (pUser, sizeof *pUser)) return -KAPI_EFAULT;
	long long nTotal = CheckIov (Iov, M.iovcnt, TRUE);
	if (nTotal < 0) return nTotal;
	unsigned nCap = M.handles != 0 ? M.nhandles : 0;
	if (nCap > 0 && !UserWritable (M.handles, nCap * sizeof (struct kapi_handle_xfer))) return -KAPI_EFAULT;
	CLsUse Use (s);
	TLsEnd *e = Use.End ();
	if (e == 0) return -KAPI_EBADF;

	TIpcXfer *pX;
	unsigned nX, nOutFlags;
	long long n = RecvCore (e, Iov, M.iovcnt, (u64) nTotal, nFlags, &pX, &nX, &nOutFlags);
	unsigned nGot = 0;
	if (pX != 0)
	{
		boolean bTrunc = FALSE;
		nGot = Deliver (pX, nX, M.handles, nCap, &bTrunc);
		if (bTrunc) nOutFlags |= KAPI_MSG_CTRUNC;
		delete [] pX;
	}
	if (n >= 0)
	{
		unsigned Out[2] = { nGot, nOutFlags };	// nhandles (20), flags (24)
		UserCopyOut (&pUser->nhandles, Out, sizeof Out);
	}
	return n;
}

}
