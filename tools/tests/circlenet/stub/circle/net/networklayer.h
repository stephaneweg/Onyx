// Host stub of Circle's CNetworkLayer (tools/tests/circlenet): Send hands the TCP segment to the
// test (a hook), nothing goes on a wire.
#ifndef _circle_net_networklayer_h
#define _circle_net_networklayer_h
#include <circle/net/netconfig.h>
#include <circle/net/netbuffer.h>
#include <circle/net/ipaddress.h>
#include <circle/net/icmphandler.h>
#include <circle/net/sizes.h>
#include <circle/types.h>

#define IP_OPTION_SIZE		0	// (as Circle's networklayer.h)

class CNetworkLayer
{
public:
	typedef void TSendHook (const void *pSegment, unsigned nLength, void *pContext);

	CNetworkLayer (void) : m_pHook (0), m_pContext (0) {}
	void SetHook (TSendHook *pHook, void *pContext) { m_pHook = pHook; m_pContext = pContext; }

	boolean Send (const CIPAddress &rReceiver, CNetBuffer *pPacket, int nProtocol,
		      boolean bRouterAlert = FALSE)
	{
		if (m_pHook) (*m_pHook) (pPacket->GetPtr (), pPacket->GetLength (), m_pContext);
		delete pPacket;
		return TRUE;
	}

private:
	TSendHook *m_pHook;
	void *m_pContext;
};
#endif
