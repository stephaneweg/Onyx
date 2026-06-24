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
#include <circle/string.h>
#include <circle/new.h>
#include <circle/types.h>

#define MAX_SOCKETS	16

struct TSocketSlot
{
	CSocket  *pSocket;
	unsigned  nOwnerPid;
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

void NetCloseByPid (unsigned nPid)
{
	if (nPid == 0) return;
	for (int i = 0; i < MAX_SOCKETS; i++)
		if (s_Sockets[i].pSocket != 0 && s_Sockets[i].nOwnerPid == nPid)
			NetTcpClose (i);
}

int NetStatus (char *pIPOut, unsigned nIPLen)
{
	if (!NetIsUp ())
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
