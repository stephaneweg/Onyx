// Host stub of Circle's CTransportLayer (tools/tests/circlenet): one connection (handle 0), the
// test's CTCPConnection; CSocket's calls go straight to it.
#ifndef _circle_net_transportlayer_h
#define _circle_net_transportlayer_h
#include <circle/net/netconfig.h>
#include <circle/net/networklayer.h>
#include <circle/net/netconnection.h>
#include <circle/net/ipaddress.h>
#include <circle/net/netbuffer.h>
#include <circle/types.h>

class CTransportLayer
{
public:
	CTransportLayer (void) : m_pConnection (0) {}
	void SetConnection (CNetConnection *p) { m_pConnection = p; }

	int Bind (u16, int) { return -1; }
	int Connect (const CIPAddress &, u16, u16, int) { return -1; }
	int Listen (u16, int) { return -1; }
	int Accept (CIPAddress *, u16 *, int) { return -1; }
	int Disconnect (int) { return 0; }
	int Send (CNetBuffer *pData, int nFlags, int h) { return m_pConnection->Send (pData, nFlags); }
	int Receive (CNetBuffer **pp, int nFlags, int h) { return m_pConnection->Receive (pp, nFlags); }
	int SendTo (CNetBuffer *pData, int nFlags, const CIPAddress &rIP, u16 nPort, int h)
		{ return m_pConnection->SendTo (pData, nFlags, rIP, nPort); }
	int ReceiveFrom (CNetBuffer **, int, CIPAddress *, u16 *, int) { return -1; }
	int SetOptionReceiveTimeout (unsigned n, int) { return m_pConnection->SetOptionReceiveTimeout (n); }
	int SetOptionSendTimeout (unsigned n, int) { return m_pConnection->SetOptionSendTimeout (n); }
	int SetOptionBroadcast (boolean, int) { return -1; }
	int SetOptionAddMembership (const CIPAddress &, int) { return -1; }
	int SetOptionDropMembership (const CIPAddress &, int) { return -1; }
	boolean IsConnected (int) const { return m_pConnection->IsConnected (); }
	const u8 *GetForeignIP (int) const { return m_pConnection->GetForeignIP (); }
	u16 GetOwnPort (int) const { return m_pConnection->GetOwnPort (); }
	u16 GetMSS (int) const { return m_pConnection->GetMSS (); }
	CNetConnection::TStatus GetStatus (int) const { return m_pConnection->GetStatus (); }

private:
	CNetConnection *m_pConnection;
};
#endif
