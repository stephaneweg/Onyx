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
// With netcore=1 the whole stack runs on core 3 (see "the network core" at the end):
// the Do* functions below then run there, on worker tasks, and the Net* entry points
// post them a request from core 0 and wait for its answer.
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
#include <wlan/bcm4343.h>					// escan (kapi_wlan_scan)
#include <circle/net/dhcpclient.h>				// Restart (kapi_wlan_reconnect)
#include <circle/netdevice.h>
#include <kern/kapi_abi.h>
#include <circle/util.h>
#include <circle/types.h>
#include <kern/ipc.h>
#include <kern/addrspace.h>

#define MAX_SOCKETS	16

struct TSocketSlot
{
	CSocket  *pSocket;
	unsigned  nOwnerPid;
	boolean   bListen;			// a listening socket (NetTcpListen), for NetInfo
};

static TSocketSlot s_Sockets[MAX_SOCKETS];		// zero-initialised (BSS)

// A slot taken by a connect still under way (its DNS lookup, its handshake: they block, and
// the net core's other workers run meanwhile -- two connects at once got the same slot).
#define SLOT_CONNECTING	((CSocket *) 1)

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

static CSocket *SockOf (int h)
{
	if (h < 0 || h >= MAX_SOCKETS) return 0;
	return s_Sockets[h].pSocket == SLOT_CONNECTING ? 0 : s_Sockets[h].pSocket;
}

static int DoConnect (const char *pHost, unsigned nPort, unsigned nOwnerPid)
{
	if (!NetIsUp () || pHost == 0 || nPort == 0 || nPort > 0xFFFF) return -1;

	int h = -1;
	for (int i = 0; i < MAX_SOCKETS; i++)
		if (s_Sockets[i].pSocket == 0) { h = i; break; }
	if (h < 0) return -2;					// table full
	s_Sockets[h].pSocket   = SLOT_CONNECTING;		// (ours, before anything blocks)
	s_Sockets[h].nOwnerPid = nOwnerPid;
	s_Sockets[h].bListen   = FALSE;

	// Resolve the host: dotted-quad literal, else the cache or a DNS lookup (blocks).
	CIPAddress IP;
	u8 raw[4];
	if (ParseDottedIP (pHost, raw))
	{
		IP.Set (raw);
	}
	else if (!ResolveName (pHost, &IP))
	{
		s_Sockets[h].pSocket = 0;
		return -3;					// name resolution failed
	}

	CSocket *pSock = new CSocket (g_pNet, IPPROTO_TCP);
	if (pSock == 0) { s_Sockets[h].pSocket = 0; return -4; }

	if (pSock->Connect (IP, (u16) nPort) < 0)		// TCP handshake (blocks)
	{
		delete pSock;
		s_Sockets[h].pSocket = 0;
		return -5;					// refused / timed out / no route
	}
	pSock->SetOptionSendTimeout (5000000);			// 5 s: never hang the app forever

	s_Sockets[h].pSocket   = pSock;
	return h;
}

static int DoSend (int hSock, const void *pBuf, unsigned nLen)
{
	CSocket *pSock = SockOf (hSock);
	if (pSock == 0) return -1;
	return pSock->Send (pBuf, nLen, 0);			// blocking (5 s send timeout)
}

static int DoRecv (int hSock, void *pBuf, unsigned nLen)
{
	CSocket *pSock = SockOf (hSock);
	if (pSock == 0) return -1;
	return pSock->Receive (pBuf, nLen, MSG_DONTWAIT);	// >0 data / 0 none / <0 closed
}

static void DoClose (int hSock)
{
	if (hSock < 0 || hSock >= MAX_SOCKETS) return;
	if (s_Sockets[hSock].pSocket != 0 && s_Sockets[hSock].pSocket != SLOT_CONNECTING)
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
static int DoListen (unsigned nPort, unsigned nOwnerPid)
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
static int DoAccept (int hListen, char *pIPOut, unsigned nIPLen, unsigned nOwnerPid)
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

static void DoCloseByPid (unsigned nPid)
{
	if (nPid == 0) return;
	for (int i = 0; i < MAX_SOCKETS; i++)
		if (s_Sockets[i].pSocket != 0 && s_Sockets[i].nOwnerPid == nPid)
			DoClose (i);
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
		CSocket *p = s_Sockets[h].pSocket;
		if (p == 0 || p == SLOT_CONNECTING) continue;
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

static int DoWlanReconnect (void)
{
	if (g_pNet == 0 || CNetDevice::GetNetDevice (NetDeviceTypeWLAN) == 0 || s_pReconfig == 0) return -1;
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
	if (!pWLAN->Control ("escan %u", 5)) return 0;

	const u8 *pOwn = 0;						// the BSSID we are on
	u8 Own[6];
	if (g_pNet != 0 && CWPASupplicant::IsConnected ())
	{
		const CMACAddress *pB = pWLAN->GetBSSID ();
		if (pB != 0) { pB->CopyTo (Own); pOwn = Own; }
	}

	int nCount = 0;
	unsigned nStart = CTimer::Get ()->GetTicks ();
	while (CTimer::Get ()->GetTicks () - nStart < 3 * HZ + HZ / 2)
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
				unsigned chan = b->chanspec & 0xFF;
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

volatile boolean g_bNetCore = FALSE;

enum { NR_CONNECT = 1, NR_SEND, NR_RECV, NR_CLOSE, NR_LISTEN, NR_ACCEPT, NR_RESOLVE, NR_PING, NR_INFO, NR_SCAN, NR_RECONF };
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

static inline u32 Ld (u32 *p)		{ return __atomic_load_n (p, __ATOMIC_ACQUIRE); }
static inline void St (u32 *p, u32 v)	{ __atomic_store_n (p, v, __ATOMIC_RELEASE); }
static inline boolean Cas (u32 *p, u32 nFrom, u32 nTo)
{
	return __atomic_compare_exchange_n (p, &nFrom, nTo, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
static void CopyStr (char *d, const char *s, unsigned nCap)
{
	unsigned i = 0;
	if (s != 0) for (; s[i] && i + 1 < nCap; i++) d[i] = s[i];
	d[i] = '\0';
}

// ---- net core side ----
static void Execute (TNetReq &r)
{
	switch (r.nOp)
	{
	case NR_CONNECT: r.nResult = DoConnect (r.szHost, r.n1, r.nPid); break;
	case NR_SEND:	 r.nResult = DoSend (r.h, r.Buf, r.nData); break;
	case NR_RECV:
	{
		// as many segments as fit (one round trip for several)
		unsigned n = 0;
		int k = DoRecv (r.h, r.Buf, r.nData);
		if (k > 0)
		{
			n = (unsigned) k;
			while (n + FRAME_BUFFER_SIZE <= r.nData && (k = DoRecv (r.h, r.Buf + n, r.nData - n)) > 0) n += (unsigned) k;
			r.nResult = (int) n;
		}
		else r.nResult = k;
		break;
	}
	case NR_CLOSE:	 DoClose (r.h); r.nResult = 0; break;
	case NR_LISTEN:	 r.nResult = DoListen (r.n1, r.nPid); break;
	case NR_ACCEPT:	 r.nResult = DoAccept (r.h, r.szIP, sizeof r.szIP, r.nPid); break;
	case NR_RESOLVE: r.nResult = DoResolve (r.szHost, r.szIP, sizeof r.szIP); break;
	case NR_PING:	 r.nResult = DoPing (r.szHost, r.n1, r.n2, r.szIP, sizeof r.szIP); break;
	case NR_INFO:	 r.nResult = DoInfo ((char *) r.Buf, r.nData); break;
	case NR_SCAN:	 r.nResult = DoWlanScan ((kapi_wlan_ap *) r.Buf, (int) r.n1); break;
	case NR_RECONF:	 r.nResult = DoWlanReconnect (); break;
	default:	 r.nResult = -1; break;
	}
}

static void Finish (TNetReq &r)
{
	u32 nOld = __atomic_exchange_n (&r.nState, (u32) RQ_DONE, __ATOMIC_ACQ_REL);
	if (nOld == RQ_ORPHAN)				// its caller is gone: undo, free the slot
	{
		if ((r.nOp == NR_CONNECT || r.nOp == NR_ACCEPT || r.nOp == NR_LISTEN) && r.nResult >= 0) DoClose (r.nResult);
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

void NetCoreMain (void)
{
	while (!s_bGo) asm volatile ("wfe");
	asm volatile ("dmb ish" ::: "memory");
	new CScheduler;					// this core's; this context is its "main" task
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
		CScheduler::Get ()->ReapTerminatedTasks ();	// (the bring-up task, once done)
		CScheduler::Get ()->Yield ();
	}
}

// ---- core 0 side ----
void NetCoreStart (CTask *(*pfnBringup) (void))
{
	s_Req = new TNetReq[NET_REQS];			// (before the net core runs: all RQ_FREE)
	assert (s_Req != 0);
	memset (s_Req, 0, sizeof (TNetReq) * NET_REQS);
	s_pfnBringup = pfnBringup;
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
			r.szHost[0] = r.szIP[0] = '\0';
			return &r;
		}
		CScheduler::Get ()->Yield ();			// all 32 in use
	}
}

static void Post (TNetReq *r) { St (&r->nState, RQ_POSTED); asm volatile ("sev"); }

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
	if (!g_bNetCore) return DoSend (hSock, pBuf, nLen);
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
		if (n < 0) return nTotal > 0 ? nTotal : n;
		nTotal += n; p += n; nLen -= (unsigned) n;
		if ((unsigned) n < k) break;
	}
	while (nLen > 0);
	return nTotal;
}

int NetTcpRecv (int hSock, void *pBuf, unsigned nLen)
{
	if (!g_bNetCore) return DoRecv (hSock, pBuf, nLen);
	if (nLen == 0) return 0;
	TNetReq *r = NewReq (NR_RECV, CurrentPid ()); if (r == 0) return -1;
	r->h = hSock; r->nData = nLen > NET_REQBUF ? NET_REQBUF : nLen;
	Post (r); Wait (r);
	int n = r->nResult;
	if (n > 0) memcpy (pBuf, r->Buf, (unsigned) n);
	Release (r); return n;
}

void NetTcpClose (int hSock)
{
	if (!g_bNetCore) { DoClose (hSock); return; }
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

void NetCloseByPid (unsigned nPid)
{
	if (!g_bNetCore) { DoCloseByPid (nPid); return; }
	if (nPid == 0 || !s_bReady) return;
	// (from the teardown, IRQs masked: nothing here waits)
	for (unsigned i = 0; i < NET_REQS; i++)
	{
		TNetReq &r = s_Req[i];
		if (r.nPid != nPid) continue;
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

// The calling process (its requests are orphaned if it dies while it waits).
static unsigned CurrentPid (void)
{
	CTask *pTask = CScheduler::Get ()->GetCurrentTask ();
	CAddressSpace *pAS = pTask != 0 ? (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER) : 0;
	return pAS != 0 ? pAS->GetPid () : 0;
}
