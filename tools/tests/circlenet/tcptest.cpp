//
// tcptest.cpp -- host test of our Circle fork's TCP (circle/lib/net/tcpconnection.cpp,
// retranstimeoutcalc.cpp, socket.cpp), built against stub Circle headers (stub/: a simulated
// clock and kernel timers, no tasks, a network layer that hands each segment to the test).
// Run by tools/tests/run_circlenet_test.sh.
//
// The test plays the peer: it reads the segments the connection sends and answers with
// segments of its own (PacketReceived), then checks the connection's congestion state.
//  1. the peer's data segments (ACK = SND.UNA, not advancing) are not duplicate ACKs
//  2. nor are window updates, nor pure ACKs with nothing in flight
//  3. three real duplicate ACKs still start a fast retransmit + fast recovery, a full ACK ends it
//  4. the RTO: 200 ms (20 ticks) on a LAN, not 1 s; Karn's algorithm after a fast retransmit
//  5. CSocket::Send: a chunk timing out after earlier chunks were queued answers their count
//
#define private public			// (the test reads the connection's state)
#define protected public
#include <circle/net/tcpconnection.h>
#include <circle/net/socket.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/checksumcalculator.h>
#include <circle/net/in.h>
#include <circle/net/error.h>
#include <circle/timer.h>
#include <circle/logger.h>
#undef private
#undef protected

extern "C" int printf (const char *, ...);
extern "C" int vprintf (const char *, __builtin_va_list);
extern "C" char *getenv (const char *);
extern "C" void exit (int);
extern "C" void abort (void);
extern "C" int fflush (void *);

// ---- the stubs' code ----------------------------------------------------------------------------

CTimer *CTimer::s_pThis = 0;
CTimer::CTimer (void) : m_nTicks (1000) { s_pThis = this; for (auto &t : m_Timers) t.bUsed = false; }
CTimer *CTimer::Get (void) { return s_pThis; }
unsigned CTimer::GetTicks (void) const { return m_nTicks; }
unsigned CTimer::GetTime (void) const { return 0; }
unsigned CTimer::GetClockTicks (void) { return s_pThis->m_nTicks * (CLOCKHZ / HZ); }
TKernelTimerHandle CTimer::StartKernelTimer (unsigned nDelay, TKernelTimerHandler *pHandler, void *pParam, void *pContext)
{
	for (unsigned i = 0; i < MaxTimers; i++)
		if (!m_Timers[i].bUsed)
		{
			m_Timers[i] = { true, m_nTicks + nDelay, pHandler, pParam, pContext };
			return i + 1;
		}
	abort ();
	return 0;
}
void CTimer::CancelKernelTimer (TKernelTimerHandle h) { if (h >= 1 && h <= MaxTimers) m_Timers[h - 1].bUsed = false; }
void CTimer::Advance (unsigned nTicks)
{
	for (unsigned k = 0; k < nTicks; k++)
	{
		m_nTicks++;
		for (unsigned i = 0; i < MaxTimers; i++)
			if (m_Timers[i].bUsed && (int) (m_nTicks - m_Timers[i].nElapsesAt) >= 0)
			{
				m_Timers[i].bUsed = false;
				(*m_Timers[i].pHandler) (i + 1, m_Timers[i].pParam, m_Timers[i].pContext);
			}
	}
}

CLogger *CLogger::Get (void) { static CLogger s; return &s; }
void CLogger::Write (const char *pSource, TLogSeverity, const char *pMessage, ...)
{
	if (!getenv ("CIRCLENET_LOG")) return;
	__builtin_va_list a; __builtin_va_start (a, pMessage);
	printf ("  [%s] ", pSource); vprintf (pMessage, a); printf ("\n");
	__builtin_va_end (a);
}

CNetSocket::CNetSocket (CNetSubSystem *p) : m_pNetSubSystem (p) {}
CNetSocket::~CNetSocket (void) {}
int CNetSocket::Connect (const char *, const char *) { return -1; }
CNetSubSystem *CNetSocket::GetNetSubSystem (void) { return m_pNetSubSystem; }

void DebugHexDump (const void *, unsigned, const char *, unsigned) {}

extern "C" void assertion_failed (const char *pExpr, const char *pFile, unsigned nLine)
{
	printf ("assertion failed: %s (%s:%u)\n", pExpr, pFile, nLine);
	fflush (0);
	abort ();
}

// ---- the peer -----------------------------------------------------------------------------------

struct THdr { u16 nSrc, nDst; u32 nSeq, nAck; u16 nOffFlags, nWin, nSum, nUrg; };
static inline u16 bs16 (u16 v) { return (u16) (v << 8 | v >> 8); }
static inline u32 bs32 (u32 v) { return __builtin_bswap32 (v); }

#define F_FIN (1 << 8)
#define F_SYN (1 << 9)
#define F_RST (1 << 10)
#define F_PSH (1 << 11)
#define F_ACK (1 << 12)

struct TSeg { u32 nSeq, nAck; unsigned nFlags, nLen; };
static TSeg s_Sent[4096];
static unsigned s_nSent;

static void OnSend (const void *p, unsigned n, void *)
{
	const THdr *h = (const THdr *) p;
	TSeg s;
	s.nSeq = bs32 (h->nSeq); s.nAck = bs32 (h->nAck);
	s.nFlags = h->nOffFlags & (F_FIN | F_SYN | F_RST | F_PSH | F_ACK);
	s.nLen = n - ((h->nOffFlags >> 4) & 0xF) * 4;
	if (s_nSent < 4096) s_Sent[s_nSent++] = s;
}

static unsigned s_nFail, s_nPass;
static void Check (bool b, const char *pWhat)
{
	printf ("  %s %s\n", b ? "ok  " : "FAIL", pWhat);
	if (b) s_nPass++; else s_nFail++;
}

static const u8 PiIP[4] = { 192, 168, 1, 10 }, PeerIP[4] = { 192, 168, 1, 20 };
static const u16 PiPort = 5000, PeerPort = 6000;

struct TWorld
{
	CTimer Timer;
	CNetSubSystem Net;
	CNetworkLayer NetLayer;
	CIPAddress Pi, Peer;
	CTCPConnection *pConn;
	u32 nPeerSeq;			// the peer's next sequence number
	u32 nWin;

	TWorld (void) : pConn (0), nPeerSeq (777000), nWin (64240)
	{
		Net.m_Config.SetIPAddress (PiIP);
		Pi.Set (PiIP); Peer.Set (PeerIP);
		NetLayer.SetHook (OnSend, 0);
		s_nSent = 0;
	}
	~TWorld (void)
	{
		if (pConn == 0) return;
		for (unsigned t = TCPTimerUser; t < TCPTimerUnknown; t++) pConn->StopTimer (t);
		pConn->m_State = TCPStateClosed;	// (the destructor wants a closed connection)
		delete pConn;
	}

	// a segment from the peer; nLen bytes of data
	void Inject (unsigned nFlags, u32 nSeq, u32 nAck, unsigned nLen = 0, unsigned nWinNow = 0)
	{
		u8 Pkt[1600] = { 0 };
		THdr *h = (THdr *) Pkt;
		unsigned nHdr = sizeof (THdr);
		if (nFlags & F_SYN)		// MSS option 1460
		{
			Pkt[20] = 2; Pkt[21] = 4; Pkt[22] = 1460 >> 8; Pkt[23] = 1460 & 0xFF;
			nHdr += 4;
		}
		h->nSrc = bs16 (PeerPort); h->nDst = bs16 (PiPort);
		h->nSeq = bs32 (nSeq); h->nAck = bs32 (nAck);
		h->nOffFlags = (u16) ((nHdr / 4) << 4) | (u16) nFlags;
		h->nWin = bs16 ((u16) (nWinNow ? nWinNow : nWin));
		for (unsigned i = 0; i < nLen; i++) Pkt[nHdr + i] = (u8) i;
		CChecksumCalculator Sum (Peer, Pi, IPPROTO_TCP);
		h->nSum = 0;
		h->nSum = Sum.Calculate (Pkt, nHdr + nLen);
		CNetBuffer *pBuf = new CNetBuffer (CNetBuffer::Receive, nHdr + nLen, Pkt);
		CIPAddress From (PeerIP), To (PiIP);
		if (pConn->PacketReceived (pBuf, From, To, IPPROTO_TCP) == 0) delete pBuf;
	}

	// active open from the Pi, the peer answers SYN+ACK: ESTABLISHED
	void Open (void)
	{
		pConn = new CTCPConnection (&Net.m_Config, &NetLayer, Peer, PeerPort, PiPort);
		Inject (F_SYN | F_ACK, nPeerSeq, pConn->m_nISS + 1);
		nPeerSeq++;
		pConn->Process ();
	}

	// the app queues nBytes (MSS-sized buffers), the connection sends what its window allows
	void Queue (unsigned nBytes)
	{
		static u8 Data[1460];
		while (nBytes > 0)
		{
			unsigned n = nBytes > pConn->m_nSND_MSS ? pConn->m_nSND_MSS : nBytes;
			CNetBuffer *p = new CNetBuffer (CNetBuffer::TCPSend, n, Data);
			if (pConn->Send (p, MSG_DONTWAIT) < 0) { delete p; break; }
			nBytes -= n;
		}
		pConn->Process ();
	}

	// the data segments sent with this sequence number (from index nFrom)
	unsigned CountSent (u32 nSeq, unsigned nFrom = 0)
	{
		unsigned n = 0;
		for (unsigned i = nFrom; i < s_nSent; i++)
			if (s_Sent[i].nSeq == nSeq && s_Sent[i].nLen > 0) n++;
		return n;
	}

	u32 Una (void) const { return pConn->m_nSND_UNA; }
	u32 Nxt (void) const { return pConn->m_nSND_NXT; }
};

// ---- the tests ----------------------------------------------------------------------------------

static void TestPeerDataIsNotDupAck (void)
{
	printf ("1. the peer's data segments (input messages) while the Pi sends\n");
	TWorld W; W.Open ();
	Check (W.pConn->m_State == TCPStateEstablished, "established");
	W.Queue (20 * 1460);
	Check (W.Nxt () != W.Una (), "data in flight");
	unsigned nMark = s_nSent;
	u32 nUna = W.Una ();
	u32 nCWND = W.pConn->m_nCWND;
	for (int i = 0; i < 5; i++)		// five small messages from the peer, ACK not advancing
	{
		W.Inject (F_ACK | F_PSH, W.nPeerSeq, nUna, 20);
		W.nPeerSeq += 20;
		W.pConn->Process ();
	}
	Check (W.pConn->m_nDupAckCount == 0, "no duplicate ACK counted");
	Check (!W.pConn->m_bFastRecovery, "no fast recovery");
	Check (W.CountSent (nUna, nMark) == 0, "nothing retransmitted");
	Check (W.pConn->m_nCWND == nCWND, "the congestion window kept");
	Check (W.pConn->m_nRCV_NXT == W.nPeerSeq, "the peer's data received");
}

static void TestWindowUpdateAndIdle (void)
{
	printf ("2. window updates; pure ACKs with nothing in flight\n");
	TWorld W; W.Open ();
	// nothing in flight: three pure ACKs (keep-alives, the peer's delayed ACKs)
	for (int i = 0; i < 3; i++) W.Inject (F_ACK, W.nPeerSeq, W.Una ());
	Check (W.pConn->m_nDupAckCount == 0, "idle: no duplicate ACK counted");
	W.Queue (8 * 1460);
	u32 nUna = W.Una ();
	for (int i = 0; i < 3; i++) W.Inject (F_ACK, W.nPeerSeq, nUna, 0, 30000 + 1000 * i);	// the window moves
	Check (W.pConn->m_nDupAckCount == 0, "window updates: no duplicate ACK counted");
	Check (!W.pConn->m_bFastRecovery, "no fast recovery");
}

static void TestRealDupAcks (void)
{
	printf ("3. three real duplicate ACKs: fast retransmit, fast recovery, then a full ACK\n");
	TWorld W; W.Open ();
	W.Queue (12 * 1460);
	// the first segment is lost: the peer ACKs the SYN's sequence for the next ones
	u32 nUna = W.Una ();
	unsigned nMark = s_nSent;
	for (int i = 0; i < 3; i++) { W.Inject (F_ACK, W.nPeerSeq, nUna); W.pConn->Process (); }
	Check (W.pConn->m_bFastRecovery, "fast recovery entered");
	Check (W.CountSent (nUna, nMark) == 1, "the lost segment sent again once");
	u32 nRecover = W.pConn->m_nRecover;
	W.Inject (F_ACK, W.nPeerSeq, nRecover);	// everything outstanding then: ACKed
	W.pConn->Process ();
	Check (!W.pConn->m_bFastRecovery, "a full ACK ends fast recovery");
	Check (W.pConn->m_nDupAckCount == 0, "the count reset");
}

static void TestRTO (void)
{
	printf ("4. the retransmission timeout\n");
	{
		TWorld W; W.Open ();		// (the SYN's RTT: 0 ticks -- a LAN)
		Check (W.pConn->m_RTOCalculator.GetRTO () == 20, "RTO after a LAN sample: 20 ticks (200 ms)");
		W.Queue (1460);
		u32 nUna = W.Una ();
		unsigned nMark = s_nSent;
		W.Timer.Advance (19); W.pConn->Process ();
		Check (W.CountSent (nUna, nMark) == 0, "not retransmitted after 190 ms");
		W.Timer.Advance (2); W.pConn->Process ();
		Check (W.CountSent (nUna, nMark) == 1, "retransmitted after 200 ms");
		Check (W.pConn->m_RTOCalculator.GetRTO () == 40, "backed off: 400 ms");
		W.Inject (F_ACK, W.nPeerSeq, W.Nxt ());
		W.Queue (1460);
		W.Timer.Advance (3);
		W.Inject (F_ACK, W.nPeerSeq, W.Nxt ());
		Check (W.pConn->m_RTOCalculator.GetRTO () == 20, "a new sample: back to 200 ms");
	}
	{
		TWorld W;
		W.pConn = new CTCPConnection (&W.Net.m_Config, &W.NetLayer, W.Peer, PeerPort, PiPort);
		Check (W.pConn->m_RTOCalculator.GetRTO () == 100, "initial RTO 1 s (RFC 6298)");
		unsigned nMark = s_nSent;
		W.Timer.Advance (101); W.pConn->Process ();
		bool bSyn = false;
		for (unsigned i = nMark; i < s_nSent; i++) if (s_Sent[i].nFlags & F_SYN) bSyn = true;
		Check (bSyn, "SYN sent again after 1 s");
	}
	{
		// Karn: the ACK of a segment sent twice (fast retransmit) is no RTT sample
		TWorld W; W.Open ();
		W.Queue (12 * 1460);
		u32 nUna = W.Una ();
		unsigned nRTO = W.pConn->m_RTOCalculator.GetRTO ();
		W.Timer.Advance (15);		// (under the RTO: no timeout)
		for (int i = 0; i < 3; i++) { W.Inject (F_ACK, W.nPeerSeq, nUna); W.pConn->Process (); }
		W.Inject (F_ACK, W.nPeerSeq, W.Nxt ());	// (a sample would be 15 ticks: RTO 15 + 30)
		Check (W.pConn->m_RTOCalculator.GetRTO () == nRTO, "Karn: no sample from a retransmitted segment");
	}
	{
		// the connection gives up after MAX_RETRANSMISSIONS timeouts, not before ~50 s
		TWorld W; W.Open ();
		W.Queue (1460);
		unsigned t = 0;
		while (W.pConn->m_nErrno == 0 && t < 200 * HZ) { W.Timer.Advance (1); t++; W.pConn->Process (); }
		printf ("       (given up after %u.%02u s)\n", t / HZ, t % HZ);
		Check (t >= 45 * HZ && t < 120 * HZ, "a dead peer given up after 45..120 s");
	}
}

static void TestSocketSendCount (void)
{
	printf ("5. CSocket::Send: a timeout after some chunks were queued\n");
	TWorld W; W.Open ();
	W.Net.m_Transport.SetConnection (W.pConn);
	W.pConn->SetOptionSendTimeout (5000000);
	CSocket Sock (&W.Net, IPPROTO_TCP);
	Sock.m_hConnection = 0;
	static u8 Big[200000];
	unsigned nBefore = W.pConn->m_TxQueue.GetBytesQueued ();
	int n = Sock.Send (Big, sizeof Big, 0);		// the peer ACKs nothing: the queue fills
	unsigned nQueued = W.pConn->m_TxQueue.GetBytesQueued () - nBefore;
	printf ("       (Send answered %d, %u bytes queued)\n", n, nQueued);
	Check (n > 0 && (unsigned) n == nQueued, "the count is the bytes queued");
	int n2 = Sock.Send (Big, 1000, 0);
	Check (n2 == -NET_ERROR_WOULD_BLOCK, "the next call: the timeout");
	Sock.m_hConnection = -1;		// (the test's connection: deleted by TWorld)
}

int main (void)
{
	TestPeerDataIsNotDupAck ();
	TestWindowUpdateAndIdle ();
	TestRealDupAcks ();
	TestRTO ();
	TestSocketSendCount ();
	printf ("%s: %u passed, %u failed\n", s_nFail ? "FAIL" : "PASS", s_nPass, s_nFail);
	return s_nFail ? 1 : 0;
}
