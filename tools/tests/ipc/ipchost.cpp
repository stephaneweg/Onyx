// ipchost.cpp -- the kernel's v76 IPC (kernel/sys/lsock.cpp, sys/shm.cpp, sys/handle.cpp) on the PC:
// local sockets, handles carried between two processes' tables, shared memory objects and their
// reference counts. The kernel around them is stubbed here (user memory = host memory, one task
// whose "process" the test switches, a clock that advances 1 ms per look). Run by
// tools/tests/run_ipc_test.sh. docs/02 section 8 "v76: IPC".
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
#include <kern/kapi_abi.h>
#include <kern/handle.h>
#include <kern/lsock.h>
#include <kern/stream.h>
#include <kern/uaccess.h>
#include <kern/iowait.h>
#include <kern/net.h>
#include <kern/vm.h>
#include <kern/addrspace.h>
#include <kern/layout.h>
#include <circle/alloc.h>
#include <circle/timer.h>
#include <circle/sched/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
int kapi_sock_pair (int nType, unsigned nFlags, int *pSv);
long long kapi_sock_sendmsg (int s, const struct kapi_msghdr *pM, unsigned nFlags);
long long kapi_sock_recvmsg (int s, struct kapi_msghdr *pM, unsigned nFlags);
long long kapi_shm_create (unsigned long long nSize, unsigned nFlags);
long long kapi_shm_open (const char *pName, unsigned nOFlags, unsigned nMode);
int kapi_shm_unlink (const char *pName);
long long kapi_shm_ctl (long long h, int nOp, unsigned long long nArg);
int kapi_handle_close (long long h);
}

// ---- the stubbed kernel ---------------------------------------------------------------------------

static CAddressSpace *s_pCur;
static unsigned s_nTicks;
static u32 s_nGen;
static unsigned s_nWakes, s_nWaits;
static long s_nPages;				// palloc_high pages out
static int s_nOFileRefs;			// the fake open-file descriptions' holders
static unsigned s_IpOwner[256];			// the IP sockets' owners

void *CTask::GetUserData (unsigned)		{ return s_pCur; }
unsigned CTimer::GetClockTicks (void)		{ return s_nTicks += 1000; }

void *palloc_high (void)			{ s_nPages++; return aligned_alloc (KPAGE_SIZE, KPAGE_SIZE); }
void pfree (void *p)				{ s_nPages--; free (p); }

boolean UserIsKernelCaller (void)		{ return FALSE; }
u64 UserRangeAvail (const void *p)		{ return p ? ~0ULL : 0; }
boolean UserRange (const void *p, u64 n)	{ return p != 0 || n == 0; }
boolean UserReadable (const void *p, u64 n)	{ return p != 0 || n == 0; }
boolean UserWritable (void *p, u64 n)		{ return p != 0 || n == 0; }
boolean UserCopyIn (void *k, const void *u, u64 n)	{ if (n && !u) return FALSE; memcpy (k, u, n); return TRUE; }
boolean UserCopyOut (void *u, const void *k, u64 n)	{ if (n && !u) return FALSE; memcpy (u, k, n); return TRUE; }
int UserStrOut (char *b, unsigned n, const char *s)	{ strncpy (b, s, n); return 0; }
CUserStr::CUserStr (const char *p, u64 nMax, boolean) : m_pStr (0), m_pHeap (0), m_nLen (0), m_bNull (p == 0)
{
	if (p == 0 || strlen (p) >= nMax) return;
	m_pHeap = strdup (p);
	m_pStr = m_pHeap;
	m_nLen = strlen (p);
}
CUserStr::~CUserStr (void)			{ free (m_pHeap); }

u32 IoGen (void)				{ return s_nGen; }
void IoWake (void)				{ s_nGen++; s_nWakes++; }
int IoWait (u32, unsigned)			{ s_nWaits++; return 1; }	// (one flow: nobody else runs)

unsigned NetCurrentPid (void)			{ return s_pCur ? s_pCur->GetPid () : 0; }
extern "C" void SocketAdopt (int)		{}
unsigned NetSocketOwner (int h)			{ return h >= 0 && h < 256 ? s_IpOwner[h] : 0; }
boolean NetSocketAdopt (int h, unsigned nFrom, unsigned nTo)
{
	if (h < 0 || h >= 256 || s_IpOwner[h] != nFrom) return FALSE;
	s_IpOwner[h] = nTo;
	return TRUE;
}

u64 VmPoolFree (void)				{ return 1ULL << 32; }
boolean VmCommitOK (u64 n)			{ return n <= (1ULL << 31); }
void WordWaitsZap (u64, u64)			{}

void RamFsClose (void *)			{}
void RamFsCloseDir (void *)			{}
void VfsDropFile (void *)			{}
void VfsClose (void *)				{}
void VfsCloseDir (void *)			{}
FRESULT f_close (FIL *)				{ return 0; }
FRESULT f_closedir (DIR *)			{ return 0; }
void VolUntrack (FFOBJID *)			{}	// (kern/volume.h, v93)
void OFileRef (void *p)				{ (*(int *) p)++; s_nOFileRefs++; }
void OFileClose (void *p, boolean)		{ (*(int *) p)--; s_nOFileRefs--; }
void OFileRunDeferred (void)			{}

// A stream that counts its references (CPipeStream's role).
static int s_nStreams;
class CTestStream : public CStream
{
public:
	CTestStream (void)	{ s_nStreams++; }
	~CTestStream (void)	{ s_nStreams--; }
	int Read (void *, unsigned) override		{ return 0; }
	int Write (const void *, unsigned n) override	{ return (int) n; }
	void AddWriter (void) override			{ m_nWriters++; }
	void CloseWrite (void) override			{ m_nCloses++; }
	int m_nWriters = 1, m_nCloses = 0;
};


// ---- the checks ------------------------------------------------------------------------------------

static int s_nFail, s_nPass;
#define CHECK(c) do { if (c) s_nPass++; else { s_nFail++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static CAddressSpace P1, P2;			// two processes

static long long Send (int s, const void *p, u64 n, const kapi_handle_xfer *pX = 0, unsigned nX = 0, unsigned nFlags = 0)
{
	kapi_iovec Iov = { (u64) (uintptr_t) p, n };
	kapi_msghdr M;
	memset (&M, 0, sizeof M);
	M.iov = &Iov;
	M.iovcnt = 1;
	M.handles = (kapi_handle_xfer *) pX;
	M.nhandles = nX;
	return kapi_sock_sendmsg (s, &M, nFlags);
}

static long long Recv (int s, void *p, u64 n, kapi_handle_xfer *pX = 0, unsigned *pnX = 0, unsigned *pFlags = 0, unsigned nFlags = 0)
{
	kapi_iovec Iov = { (u64) (uintptr_t) p, n };
	kapi_msghdr M;
	memset (&M, 0, sizeof M);
	M.iov = &Iov;
	M.iovcnt = 1;
	M.handles = pX;
	M.nhandles = pnX ? *pnX : 0;
	long long r = kapi_sock_recvmsg (s, &M, nFlags);
	if (pnX) *pnX = M.nhandles;
	if (pFlags) *pFlags = M.flags;
	return r;
}

static kapi_handle_xfer X (long long h, int kind, unsigned tag = 0)
{
	kapi_handle_xfer x;
	memset (&x, 0, sizeof x);
	x.h = h; x.kind = kind; x.tag = tag;
	return x;
}

// Hand handle h of P1 (kind) to P2 the way spawn_ex2 / get_handles do -> P2's handle.
static long long Hand (long long h, int kind)
{
	s_pCur = &P1;
	TIpcXfer T;
	kapi_handle_xfer In = X (h, kind), Out;
	if (IpcXferTake (P1.GetHandles (), In, 1, &T) != 0) return -1;
	s_pCur = &P2;
	if (!IpcXferGive (P2.GetHandles (), &T, 2, FALSE, &Out)) return -1;
	return Out.h;
}

static void TestPacket (int nType)
{
	s_pCur = &P1;
	int sv[2];
	CHECK (kapi_sock_pair (nType, KAPI_SOCKF_NONBLOCK, sv) == 0);
	CHECK (sv[0] >= KAPI_SOCK_LOCAL_BASE && sv[1] >= KAPI_SOCK_LOCAL_BASE && sv[0] != sv[1]);
	CHECK (IpcLocalPoll (sv[0]) == KAPI_POLLOUT);
	CHECK (Send (sv[0], "hello", 5) == 5);
	CHECK (Send (sv[0], "", 0) == 0);		// (a zero-length packet)
	CHECK (Send (sv[0], "world!!", 7) == 7);
	CHECK (IpcLocalPoll (sv[1]) == (KAPI_POLLIN | KAPI_POLLOUT));
	int v = 0;
	CHECK (IpcLocalGetOpt (sv[1], KAPI_SO_NREAD, &v) == 0 && v == 5);
	char b[64];
	unsigned nFl = 0;
	CHECK (Recv (sv[1], b, sizeof b, 0, 0, &nFl) == 5 && memcmp (b, "hello", 5) == 0 && nFl == 0);
	CHECK (Recv (sv[1], b, sizeof b) == 0);		// (the empty one)
	CHECK (Recv (sv[1], b, 3, 0, 0, &nFl) == 3 && memcmp (b, "wor", 3) == 0 && nFl == KAPI_MSG_TRUNC);
	CHECK (Recv (sv[1], b, sizeof b) == -KAPI_EAGAIN);	// (the rest of it dropped)
	// a packet over SO_SNDBUF: EMSGSIZE; the receiver's limit: one message always fits
	CHECK (IpcLocalSetOpt (sv[0], KAPI_SO_SNDBUF, 1 << 20) == 0);
	CHECK (IpcLocalSetOpt (sv[1], KAPI_SO_RCVBUF, 4096) == 0);
	static char big[2 << 20];
	CHECK (Send (sv[0], big, (2 << 20)) == -KAPI_EMSGSIZE);
	CHECK (Send (sv[0], big, 100000) == 100000);	// (an empty queue takes one)
	CHECK (Send (sv[0], big, 10) == -KAPI_EAGAIN);	// (full)
	CHECK ((IpcLocalPoll (sv[0]) & KAPI_POLLOUT) == 0);
	CHECK (Recv (sv[1], big, sizeof big) == 100000);
	CHECK ((IpcLocalPoll (sv[0]) & KAPI_POLLOUT) != 0);
	// the end: closed peer -> 0 after the queue, EPIPE, HUP
	CHECK (Send (sv[1], "bye", 3) == 3);
	CHECK (IpcLocalClose (sv[1]) == 0);
	CHECK ((IpcLocalPoll (sv[0]) & (KAPI_POLLIN | KAPI_POLLHUP)) == (KAPI_POLLIN | KAPI_POLLHUP));
	CHECK (Send (sv[0], "x", 1) == -KAPI_EPIPE);
	CHECK (Recv (sv[0], b, sizeof b) == 3);
	CHECK (Recv (sv[0], b, sizeof b) == 0);
	CHECK (IpcLocalGetOpt (sv[0], KAPI_SO_PEERPID, &v) == -KAPI_ENOTCONN);
	CHECK (IpcLocalClose (sv[0]) == 0);
	CHECK (IpcLocalClose (sv[0]) == -KAPI_EBADF);
	CHECK (IpcLocalPoll (sv[0]) == KAPI_POLLNVAL);
}

static void TestStream (void)
{
	s_pCur = &P1;
	int sv[2];
	CHECK (kapi_sock_pair (KAPI_SOCK_STREAM, KAPI_SOCKF_NONBLOCK, sv) == 0);
	CHECK (Send (sv[0], "abc", 3) == 3 && Send (sv[0], "defg", 4) == 4);
	char b[64];
	CHECK (Recv (sv[1], b, 2) == 2 && memcmp (b, "ab", 2) == 0);
	CHECK (Recv (sv[1], b, sizeof b, 0, 0, 0, KAPI_MSG_PEEK) == 5 && memcmp (b, "cdefg", 5) == 0);
	CHECK (Recv (sv[1], b, sizeof b) == 5 && memcmp (b, "cdefg", 5) == 0);
	// a stream takes what fits
	CHECK (IpcLocalSetOpt (sv[1], KAPI_SO_RCVBUF, 4096) == 0);
	static char big[20000];
	for (unsigned i = 0; i < sizeof big; i++) big[i] = (char) i;
	CHECK (Send (sv[0], big, sizeof big) == 4096);
	CHECK (Send (sv[0], big, 1) == -KAPI_EAGAIN);
	static char got[20000];
	CHECK (Recv (sv[1], got, sizeof got) == 4096 && memcmp (got, big, 4096) == 0);
	// a timed blocking send gives up: -EAGAIN after SO_SNDTIMEO (nothing drains here)
	CHECK (IpcLocalSetOpt (sv[0], KAPI_SO_NONBLOCK, 0) == 0);
	CHECK (IpcLocalSetOpt (sv[0], KAPI_SO_SNDTIMEO_MS, 50) == 0);
	CHECK (Send (sv[0], big, sizeof big) == 4096);	// (what fit, then the timeout)
	CHECK (IpcLocalSetOpt (sv[1], KAPI_SO_RCVTIMEO_MS, 30) == 0);
	CHECK (IpcLocalSetOpt (sv[1], KAPI_SO_NONBLOCK, 0) == 0);
	CHECK (Recv (sv[1], got, sizeof got) == 4096);
	CHECK (Recv (sv[1], got, sizeof got) == -KAPI_EAGAIN);	// (SO_RCVTIMEO)
	// SHUT_WR: the peer reads the end; our sends EPIPE
	CHECK (Send (sv[0], "zz", 2) == 2);
	CHECK (IpcLocalShutdown (sv[0], KAPI_SHUT_WR) == 0);
	CHECK (Send (sv[0], "q", 1) == -KAPI_EPIPE);
	CHECK (Recv (sv[1], got, sizeof got) == 2 && Recv (sv[1], got, sizeof got) == 0);
	CHECK (Send (sv[1], "back", 4) == 4);		// (the other way still works)
	CHECK (Recv (sv[0], got, sizeof got) == 4);
	CHECK (IpcLocalClose (sv[0]) == 0 && IpcLocalClose (sv[1]) == 0);
}

static void TestHandles (void)
{
	s_pCur = &P1;
	int a[2], c[2];
	CHECK (kapi_sock_pair (KAPI_SOCK_SEQPACKET, KAPI_SOCKF_NONBLOCK, a) == 0);
	// a[1] goes to P2 (as spawn_ex2 would give it)
	long long b2 = Hand (a[1], KAPI_HK_LSOCK);
	CHECK (b2 >= KAPI_SOCK_LOCAL_BASE);
	s_pCur = &P1;
	CHECK (IpcLocalClose (a[1]) == 0);			// (P2's handle keeps the end)
	int v = 0;
	CHECK (IpcLocalGetOpt (a[0], KAPI_SO_PEERPID, &v) == 0 && v == 2);

	// P1 sends: a shm, a stream, an "open file", a local socket, an IP socket
	long long hs = kapi_shm_create (100000, 0);
	CHECK (hs > 0);
	CHECK (kapi_shm_ctl (hs, KAPI_SHM_GET_SEALS, 0) == KAPI_SEAL_SEAL);
	u64 ph1 = 0;
	CHECK (ShmObjectsTotal () == 1);
	CTestStream *pStream = new CTestStream;
	void *hStream = P1.GetHandles ()->Add (pStream, HANDLE_STREAM);
	static int OFile = 1;					// (its holders)
	void *hOFile = P1.GetHandles ()->Add (&OFile, HANDLE_OFILE);
	CHECK (kapi_sock_pair (KAPI_SOCK_STREAM, 0, c) == 0);
	s_IpOwner[7] = 1;
	kapi_handle_xfer Out[8] = {
		X (hs, KAPI_HK_SHM, 11), X ((long long) (uintptr_t) hStream, KAPI_HK_STREAM, 12),
		X ((long long) (uintptr_t) hOFile, KAPI_HK_OFILE, 13), X (c[1], KAPI_HK_LSOCK, 14),
		X (7, KAPI_HK_SOCKET, 15) };
	CHECK (Send (a[0], "five", 4, Out, 5) == 4);
	CHECK (pStream->GetRefs () == 2 && OFile == 2);
	// a bad handle: nothing sent, nothing kept
	kapi_handle_xfer Bad[2] = { X (hs, KAPI_HK_SHM), X (12345, KAPI_HK_STREAM) };
	CHECK (Send (a[0], "x", 1, Bad, 2) == -KAPI_EBADF);
	// the receiving end over its own connection: refused
	kapi_handle_xfer Self = X (a[0], KAPI_HK_LSOCK);
	CHECK (Send (a[0], "x", 1, &Self, 1) == 1);		// (a[0] is the SENDING end: allowed)
	CHECK (IpcLocalClose (hs) == -KAPI_EBADF);		// (not a socket)
	CHECK (kapi_handle_close (hs) == 0);			// (the message keeps the object)
	CHECK (ShmObjectsTotal () == 1);
	P1.GetHandles ()->Close (hStream, HANDLE_STREAM);
	P1.GetHandles ()->Close (hOFile, HANDLE_OFILE);
	CHECK (IpcLocalClose (c[1]) == 0);
	CHECK (pStream->GetRefs () == 1 && OFile == 1 && s_nStreams == 1);

	// P2 receives
	s_pCur = &P2;
	char b[16];
	kapi_handle_xfer In[8];
	unsigned n = 8, fl = 0;
	CHECK (Recv ((int) b2, b, sizeof b, In, &n, &fl) == 4 && n == 5 && fl == 0);
	CHECK (In[0].kind == KAPI_HK_SHM && In[0].tag == 11 && In[0].h > 0);
	CHECK (In[1].kind == KAPI_HK_STREAM && In[1].tag == 12);
	CHECK (In[2].kind == KAPI_HK_OFILE && In[2].tag == 13);
	CHECK (In[3].kind == KAPI_HK_LSOCK && In[3].tag == 14 && In[3].h >= KAPI_SOCK_LOCAL_BASE);
	CHECK (In[4].kind == KAPI_HK_SOCKET && In[4].h == 7 && s_IpOwner[7] == 2);	// (adopted)
	CHECK (P2.GetHandles ()->Get ((void *) (uintptr_t) In[1].h, HANDLE_STREAM) == pStream);
	CHECK (kapi_shm_ctl (In[0].h, KAPI_SHM_GET_SIZE, 0) == 100000);
	// the local socket received works: P1's c[0] <-> P2's In[3]
	s_pCur = &P1;
	CHECK (Send (c[0], "via", 3) == 3);
	s_pCur = &P2;
	CHECK (IpcLocalRecv ((int) In[3].h, b, sizeof b, 0) == 3 && memcmp (b, "via", 3) == 0);
	// the second message: a[0] itself (the sending end) arrived; then a[0]'s peer is P2's own end
	unsigned n2 = 4;
	CHECK (Recv ((int) b2, b, sizeof b, In + 5, &n2) == 1 && n2 == 1 && In[5].kind == KAPI_HK_LSOCK);
	kapi_handle_xfer Loop = X ((long long) b2, KAPI_HK_LSOCK);
	CHECK (Send ((int) In[5].h, "x", 1, &Loop, 1) == -KAPI_EINVAL);	// (to b2 itself, through a[0])
	// the shm frames are the same object's
	void *o1 = P2.GetHandles ()->Get ((void *) (uintptr_t) In[0].h, HANDLE_SHM);
	CHECK (ShmFrame (o1, 1, &ph1) == 0 && ph1 != 0);
	CHECK (ShmFrame (o1, 2, &ph1) == -KAPI_EFAULT);		// (100000 bytes: 2 pages)
	// closes: everything goes
	CHECK (kapi_handle_close (In[0].h) == 0);
	CHECK (ShmObjectsTotal () == 0 && ShmPagesTotal () == 0);
	CHECK (kapi_handle_close (In[1].h) == 0 && s_nStreams == 0);
	CHECK (kapi_handle_close (In[2].h) == 0 && OFile == 0);
	CHECK (IpcLocalClose ((int) In[3].h) == 0 && IpcLocalClose ((int) In[5].h) == 0);
	CHECK (IpcLocalClose ((int) b2) == 0);
	s_pCur = &P1;
	CHECK (IpcLocalClose (c[0]) == 0);
	CHECK (IpcLocalClose (a[0]) == 0);			// (P1's own handle: still open)
}

static void TestTruncAndDiscard (void)
{
	s_pCur = &P1;
	int a[2];
	CHECK (kapi_sock_pair (KAPI_SOCK_DGRAM, KAPI_SOCKF_NONBLOCK, a) == 0);
	// 254 handles in one message (WebKit's attachmentMaxAmount); 256 the limit; 257 refused
	long long hs = kapi_shm_create (4096, KAPI_SHM_ALLOW_SEALING);
	static kapi_handle_xfer Many[KAPI_IPC_HANDLES_MAX + 1];
	for (unsigned i = 0; i <= KAPI_IPC_HANDLES_MAX; i++) Many[i] = X (hs, KAPI_HK_SHM, i);
	CHECK (Send (a[0], "m", 1, Many, 254) == 1);
	CHECK (Send (a[0], "m", 1, Many, KAPI_IPC_HANDLES_MAX + 1) == -KAPI_EINVAL);
	CHECK (Send (a[0], "n", 1, Many, 5) == 1);
	CHECK (kapi_handle_close (hs) == 0 && ShmObjectsTotal () == 1);
	static kapi_handle_xfer In[256];
	unsigned n = 256, fl = 0;
	char b[4];
	CHECK (Recv (a[1], b, 1, In, &n, &fl) == 1 && n == 254 && fl == 0);
	CHECK (In[253].tag == 253);
	for (unsigned i = 0; i < n; i++) kapi_handle_close (In[i].h);
	n = 2;
	CHECK (Recv (a[1], b, 1, In, &n, &fl) == 1 && n == 2 && fl == KAPI_MSG_CTRUNC);	// (3 closed)
	CHECK (kapi_handle_close (In[0].h) == 0 && ShmObjectsTotal () == 1);
	CHECK (kapi_handle_close (In[1].h) == 0 && ShmObjectsTotal () == 0);
	// a message discarded (the receiving end closed): its handles closed
	hs = kapi_shm_create (65536, 0);
	CHECK (Send (a[0], "d", 1, Many, 0) == 1);
	kapi_handle_xfer One = X (hs, KAPI_HK_SHM);
	CHECK (Send (a[0], "d", 1, &One, 1) == 1);
	kapi_handle_close (hs);
	CHECK (ShmObjectsTotal () == 1);
	CHECK (IpcLocalClose (a[1]) == 0);
	CHECK (ShmObjectsTotal () == 0);
	// MSG_PEEK delivers no handles; a recv without room closes them
	int c[2];
	CHECK (kapi_sock_pair (KAPI_SOCK_SEQPACKET, KAPI_SOCKF_NONBLOCK, c) == 0);
	hs = kapi_shm_create (10, 0);
	One = X (hs, KAPI_HK_SHM);
	CHECK (Send (c[0], "pk", 2, &One, 1) == 2);
	kapi_handle_close (hs);
	n = 4;
	CHECK (Recv (c[1], b, 2, In, &n, &fl, KAPI_MSG_PEEK) == 2 && n == 0);
	CHECK (IpcLocalRecv (c[1], b, 2, 0) == 2 && ShmObjectsTotal () == 0);
	// stream: the read stops at a send that carried handles, and after it
	int d[2];
	CHECK (kapi_sock_pair (KAPI_SOCK_STREAM, KAPI_SOCKF_NONBLOCK, d) == 0);
	hs = kapi_shm_create (10, 0);
	One = X (hs, KAPI_HK_SHM);
	CHECK (Send (d[0], "aa", 2) == 2 && Send (d[0], "bb", 2, &One, 1) == 2 && Send (d[0], "cc", 2) == 2);
	CHECK (Send (d[0], "", 0, &One, 1) == -KAPI_EINVAL);		// (handles need a byte)
	kapi_handle_close (hs);
	char s6[8];
	n = 4;
	CHECK (Recv (d[1], s6, 6, In, &n) == 2 && n == 0 && memcmp (s6, "aa", 2) == 0);
	n = 4;
	CHECK (Recv (d[1], s6, 6, In, &n) == 2 && n == 1 && memcmp (s6, "bb", 2) == 0);
	CHECK (Recv (d[1], s6, 6) == 2 && memcmp (s6, "cc", 2) == 0);
	kapi_handle_close (In[0].h);
	CHECK (ShmObjectsTotal () == 0);
	// a process ends with queued messages and ends (the teardown)
	s_pCur = &P2;
	int e[2];
	CHECK (kapi_sock_pair (KAPI_SOCK_SEQPACKET, 0, e) == 0);
	hs = kapi_shm_create (200000, 0);
	One = X (hs, KAPI_HK_SHM);
	CHECK (Send (e[0], "t", 1, &One, 1) == 1);
	void *o = P2.GetHandles ()->Get ((void *) (uintptr_t) hs, HANDLE_SHM);
	u64 ph;
	CHECK (ShmFrame (o, 0, &ph) == 0 && ShmPagesTotal () == 1);
	P2.GetHandles ()->CloseAll (TRUE);
	CHECK (ShmObjectsTotal () == 0 && ShmPagesTotal () == 0 && P2.GetHandles ()->GetCount () == 0);
	s_pCur = &P1;
	CHECK (IpcLocalClose (a[0]) == 0 && IpcLocalClose (c[0]) == 0 && IpcLocalClose (c[1]) == 0);
	CHECK (IpcLocalClose (d[0]) == 0 && IpcLocalClose (d[1]) == 0);
}

static void TestShm (void)
{
	s_pCur = &P1;
	long long h = kapi_shm_create (0, KAPI_SHM_ALLOW_SEALING);
	CHECK (h > 0 && kapi_shm_ctl (h, KAPI_SHM_GET_SIZE, 0) == 0 && kapi_shm_ctl (h, KAPI_SHM_GET_SEALS, 0) == 0);
	CHECK (kapi_shm_ctl (h, KAPI_SHM_SET_SIZE, 300000) == 0);
	void *o = P1.GetHandles ()->Get ((void *) (uintptr_t) h, HANDLE_SHM);
	u64 ph;
	CHECK (ShmFrame (o, 4, &ph) == 0);			// (300000: 5 pages)
	memset ((void *) (uintptr_t) ph, 0xAB, KPAGE_SIZE);
	CHECK (ShmFrame (o, 5, &ph) == -KAPI_EFAULT);
	CHECK (kapi_shm_ctl (h, KAPI_SHM_SET_SIZE, 4 * 65536 + 100) == 0);	// (shrink: the tail zeroed)
	CHECK (ShmFrame (o, 4, &ph) == 0 && ((unsigned char *) (uintptr_t) ph)[99] == 0xAB
	       && ((unsigned char *) (uintptr_t) ph)[100] == 0);
	ShmMapAttach (o);					// (a space maps it)
	CHECK (kapi_shm_ctl (h, KAPI_SHM_SET_SIZE, 10) == -KAPI_EBUSY);
	CHECK (kapi_shm_ctl (h, KAPI_SHM_SET_SIZE, 1 << 20) == 0);	// (grow: allowed)
	CHECK (kapi_shm_ctl (h, KAPI_SHM_ADD_SEALS, KAPI_SEAL_WRITE) == -KAPI_EBUSY);
	ShmMapDetach (o, FALSE);
	CHECK (kapi_shm_ctl (h, KAPI_SHM_ADD_SEALS, KAPI_SEAL_SHRINK | KAPI_SEAL_GROW | KAPI_SEAL_SEAL) == 0);
	CHECK (kapi_shm_ctl (h, KAPI_SHM_SET_SIZE, 10) == -KAPI_EPERM);
	CHECK (kapi_shm_ctl (h, KAPI_SHM_SET_SIZE, 2 << 20) == -KAPI_EPERM);
	CHECK (kapi_shm_ctl (h, KAPI_SHM_ADD_SEALS, KAPI_SEAL_WRITE) == -KAPI_EPERM);
	CHECK (kapi_shm_ctl (h, KAPI_SHM_GET_SEALS, 0) == (KAPI_SEAL_SHRINK | KAPI_SEAL_GROW | KAPI_SEAL_SEAL));
	CHECK (kapi_shm_ctl (h, 99, 0) == -KAPI_EINVAL);
	CHECK (kapi_handle_close (h) == 0 && ShmObjectsTotal () == 0 && ShmPagesTotal () == 0);
	CHECK (kapi_shm_ctl (h, KAPI_SHM_GET_SIZE, 0) == -KAPI_EBADF);

	// named
	CHECK (kapi_shm_open ("/wk", KAPI_O_RDWR, 0600) == -KAPI_ENOENT);
	long long a = kapi_shm_open ("/wk", KAPI_O_RDWR | KAPI_O_CREAT | KAPI_O_EXCL, 0600);
	CHECK (a > 0 && kapi_shm_ctl (a, KAPI_SHM_SET_SIZE, 5000) == 0);
	CHECK (kapi_shm_open ("wk", KAPI_O_RDWR | KAPI_O_CREAT | KAPI_O_EXCL, 0600) == -KAPI_EEXIST);
	s_pCur = &P2;
	long long b = kapi_shm_open ("wk", KAPI_O_RDONLY, 0);
	CHECK (b > 0 && kapi_shm_ctl (b, KAPI_SHM_GET_SIZE, 0) == 5000);
	CHECK (kapi_shm_ctl (b, KAPI_SHM_GET_ACCESS, 0) == KAPI_O_RDONLY);
	CHECK (kapi_shm_ctl (b, KAPI_SHM_SET_SIZE, 1) == -KAPI_EINVAL);
	CHECK (kapi_shm_ctl (b, KAPI_SHM_GET_ID, 0) > 0);
	CHECK (kapi_shm_open ("a/b", KAPI_O_RDWR | KAPI_O_CREAT, 0) == -KAPI_EINVAL);
	CHECK (kapi_shm_open ("", KAPI_O_RDWR | KAPI_O_CREAT, 0) == -KAPI_EINVAL);
	CHECK (kapi_shm_open ("0123456789012345678901234567890123456789012345678901234567890123", KAPI_O_RDWR | KAPI_O_CREAT, 0) == -KAPI_ENAMETOOLONG);
	CHECK (kapi_shm_unlink ("/wk") == 0 && kapi_shm_unlink ("/wk") == -KAPI_ENOENT);
	CHECK (ShmObjectsTotal () == 1);
	CHECK (kapi_handle_close (b) == 0);
	s_pCur = &P1;
	CHECK (kapi_handle_close (a) == 0 && ShmObjectsTotal () == 0);
}

// A pipe's write end carried (KAPI_HXF_WRITER): one more writer, closed once by its holder's close.
static void TestWriter (void)
{
	s_pCur = &P1;
	CTestStream *p = new CTestStream;
	void *h = P1.GetHandles ()->Add (p, HANDLE_STREAM);
	kapi_handle_xfer In = X ((long long) (uintptr_t) h, KAPI_HK_STREAM, 5), Out;
	In.flags = KAPI_HXF_WRITER;
	TIpcXfer T;
	CHECK (IpcXferTake (P1.GetHandles (), In, 1, &T) == 0 && p->m_nWriters == 2 && p->GetRefs () == 2);
	s_pCur = &P2;
	CHECK (IpcXferGive (P2.GetHandles (), &T, 2, FALSE, &Out) && Out.flags == KAPI_HXF_WRITER && Out.tag == 5);
	unsigned nKind = 0;
	CHECK (P2.GetHandles ()->Get ((void *) (uintptr_t) Out.h, HANDLE_STREAM, &nKind) == p && nKind == HKIND_STREAM_WRITER);
	CHECK (kapi_handle_close (Out.h) == 0 && p->m_nCloses == 1 && p->GetRefs () == 1);	// (its end: CloseWrite)
	// a carried write end dropped with its message: CloseWrite too; a read end: no
	s_pCur = &P1;
	CHECK (IpcXferTake (P1.GetHandles (), In, 1, &T) == 0);
	IpcXferDrop (&T, FALSE);
	CHECK (p->m_nCloses == 2 && p->GetRefs () == 1);
	In.flags = 0;
	CHECK (IpcXferTake (P1.GetHandles (), In, 1, &T) == 0);
	IpcXferDrop (&T, FALSE);
	CHECK (p->m_nCloses == 2 && p->m_nWriters == 3);
	P1.GetHandles ()->Close (h, HANDLE_STREAM);
	CHECK (s_nStreams == 0);
}

int main (void)
{
	P1.m_nPid = 1;
	P2.m_nPid = 2;
	TestPacket (KAPI_SOCK_SEQPACKET);
	TestPacket (KAPI_SOCK_DGRAM);
	TestStream ();
	TestHandles ();
	TestTruncAndDiscard ();
	TestShm ();
	TestWriter ();
	s_pCur = &P1;
	CHECK (kapi_sock_pair (3, 0, 0) == -KAPI_EPROTONOSUPPORT);
	CHECK (kapi_sock_pair (KAPI_SOCK_STREAM, 0, 0) == -KAPI_EFAULT);
	CHECK (kapi_sock_sendmsg (5, 0, 0) == -KAPI_EOPNOTSUPP);	// (an IP socket)
	P1.GetHandles ()->CloseAll (TRUE);
	P2.GetHandles ()->CloseAll (TRUE);
	CHECK (s_nPages == 0 && s_nStreams == 0 && s_nOFileRefs == -1);	// (-1: the first holder was not a ref)
	CHECK (P1.GetHandles ()->GetCount () == 0);
	printf ("ipchost: %d passed, %d failed (%u wakes, %u waits)\n", s_nPass, s_nFail, s_nWakes, s_nWaits);
	return s_nFail != 0;
}
