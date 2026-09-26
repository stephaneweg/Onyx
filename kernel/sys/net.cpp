//
// net.cpp -- handle-based TCP socket backend for the kapi layer.
//
// Apps cannot hold kernel C++ objects, so they open TCP connections by integer
// HANDLE. Each handle indexes a small table of Circle CSockets; the kapi_tcp_*
// entry points (sys/kapi.cpp) are thin extern-"C" shims over the functions here.
// Calls run on the *app's* task: Connect/DNS block cooperatively (yielding to the
// net stack's own CNetTask), Send blocks with a timeout, Recv is non-blocking.
// A socket records its owner pid so NetCloseByPid() can reclaim it if the process
// dies without closing (force-kill / crash), preventing slot + connection leaks.
//
#include <kern/net.h>
#include <circle/net/socket.h>
#include <circle/net/dnsclient.h>
#include <circle/net/ipaddress.h>
#include <circle/net/in.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/networklayer.h>
#include <circle/net/checksumcalculator.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/string.h>
#include <circle/new.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>	// live association state
#include <circle/types.h>

#define MAX_SOCKETS	16

struct TSocketSlot
{
	CSocket  *pSocket;
	unsigned  nOwnerPid;
	boolean   bListen;			// a listening socket (NetTcpListen), for NetInfo
};

static TSocketSlot s_Sockets[MAX_SOCKETS];		// zero-initialised (BSS)

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

static CSocket *SockOf (int h)
{
	if (h < 0 || h >= MAX_SOCKETS) return 0;
	return s_Sockets[h].pSocket;
}

int NetTcpConnect (const char *pHost, unsigned nPort, unsigned nOwnerPid)
{
	if (!NetIsUp () || pHost == 0 || nPort == 0 || nPort > 0xFFFF) return -1;

	int h = -1;
	for (int i = 0; i < MAX_SOCKETS; i++)
		if (s_Sockets[i].pSocket == 0) { h = i; break; }
	if (h < 0) return -2;					// table full

	// Resolve the host: dotted-quad literal, else a DNS lookup (blocks).
	CIPAddress IP;
	u8 raw[4];
	if (ParseDottedIP (pHost, raw))
	{
		IP.Set (raw);
	}
	else
	{
		CDNSClient DNS (g_pNet);
		if (!DNS.Resolve (pHost, &IP)) return -3;	// name resolution failed
	}

	CSocket *pSock = new CSocket (g_pNet, IPPROTO_TCP);
	if (pSock == 0) return -4;

	if (pSock->Connect (IP, (u16) nPort) < 0)		// TCP handshake (blocks)
	{
		delete pSock;
		return -5;					// refused / timed out / no route
	}
	pSock->SetOptionSendTimeout (5000000);			// 5 s: never hang the app forever

	s_Sockets[h].pSocket   = pSock;
	s_Sockets[h].nOwnerPid = nOwnerPid;
	return h;
}

int NetTcpSend (int hSock, const void *pBuf, unsigned nLen)
{
	CSocket *pSock = SockOf (hSock);
	if (pSock == 0) return -1;
	return pSock->Send (pBuf, nLen, 0);			// blocking (5 s send timeout)
}

int NetTcpRecv (int hSock, void *pBuf, unsigned nLen)
{
	CSocket *pSock = SockOf (hSock);
	if (pSock == 0) return -1;
	return pSock->Receive (pBuf, nLen, MSG_DONTWAIT);	// >0 data / 0 none / <0 closed
}

void NetTcpClose (int hSock)
{
	if (hSock < 0 || hSock >= MAX_SOCKETS) return;
	if (s_Sockets[hSock].pSocket != 0)
	{
		delete s_Sockets[hSock].pSocket;		// dtor terminates the connection
		s_Sockets[hSock].pSocket   = 0;
		s_Sockets[hSock].nOwnerPid = 0;
	}
}

// Free handle slot, or -1 if the table is full.
static int FreeSlot (void)
{
	for (int i = 0; i < MAX_SOCKETS; i++)
		if (s_Sockets[i].pSocket == 0) return i;
	return -1;
}

// Server side: a socket bound to nPort and listening. The handle is only good for
// NetTcpAccept (and NetTcpClose); it never carries data itself.
int NetTcpListen (unsigned nPort, unsigned nOwnerPid)
{
	if (!NetIsUp () || nPort == 0 || nPort > 0xFFFF) return -1;

	int h = FreeSlot ();
	if (h < 0) return -2;					// table full

	CSocket *pSock = new CSocket (g_pNet, IPPROTO_TCP);
	if (pSock == 0) return -4;
	if (pSock->Bind ((u16) nPort) < 0 || pSock->Listen () < 0)
	{
		delete pSock;
		return -6;					// port in use / bind failed
	}

	s_Sockets[h].pSocket   = pSock;
	s_Sockets[h].nOwnerPid = nOwnerPid;
	s_Sockets[h].bListen   = TRUE;
	return h;
}

// Wait (blocking, cooperatively) for the next incoming connection on a listening
// handle. Returns a new connected handle (use NetTcpSend/Recv/Close on it) and the
// peer's dotted IP in pIPOut, or <0 on error.
int NetTcpAccept (int hListen, char *pIPOut, unsigned nIPLen, unsigned nOwnerPid)
{
	CSocket *pListen = SockOf (hListen);
	if (pListen == 0) return -1;

	CIPAddress IP;
	u16 nPort = 0;
	CSocket *pConn = pListen->Accept (&IP, &nPort);		// blocks until a peer connects
	if (pConn == 0) return -5;

	int h = FreeSlot ();
	if (h < 0) { delete pConn; return -2; }			// table full -> drop the peer
	pConn->SetOptionSendTimeout (5000000);			// 5 s, like NetTcpConnect

	s_Sockets[h].pSocket   = pConn;
	s_Sockets[h].nOwnerPid = nOwnerPid;
	s_Sockets[h].bListen   = FALSE;

	if (pIPOut != 0 && nIPLen > 0)
	{
		CString s;
		IP.Format (&s);
		const char *p = (const char *) s;
		unsigned i = 0;
		for (; p[i] != '\0' && i < nIPLen - 1; i++) pIPOut[i] = p[i];
		pIPOut[i] = '\0';
	}
	return h;
}

void NetCloseByPid (unsigned nPid)
{
	if (nPid == 0) return;
	for (int i = 0; i < MAX_SOCKETS; i++)
		if (s_Sockets[i].pSocket != 0 && s_Sockets[i].nOwnerPid == nPid)
			NetTcpClose (i);
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
	CDNSClient DNS (g_pNet);
	return DNS.Resolve (pHost, &rIP);
}

int NetResolve (const char *pHost, char *pIPOut, unsigned nIPLen)
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
int NetPing (const char *pHost, unsigned nSeq, unsigned nTimeoutMs, char *pIPOut, unsigned nIPLen)
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
int NetInfo (char *pBuf, unsigned nCap)
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
		CSocket *p = s_Sockets[h].pSocket;
		if (p == 0) continue;
		put ("tcp "); putu ((unsigned) h); put (s_Sockets[h].bListen ? " listen " : " conn ");
		putu (p->GetOwnPort ()); put (" ");
		const u8 *f = p->GetForeignIP ();
		char ip[20] = "-";
		if (f != 0 && !s_Sockets[h].bListen) FormatIP (f, ip, sizeof ip);
		put (ip); put (" "); putu (s_Sockets[h].nOwnerPid); put ("\n");
	}
	pBuf[n] = '\0';
	return (int) n;
}
