// Host stub (tools/tests/circlenet): what CSocket asks the subsystem for.
#ifndef _circle_net_netsubsystem_h
#define _circle_net_netsubsystem_h
#include <circle/net/netconfig.h>
#include <circle/net/transportlayer.h>
class CNetSubSystem
{
public:
	CNetConfig *GetConfig (void) { return &m_Config; }
	CTransportLayer *GetTransportLayer (void) { return &m_Transport; }
	CNetConfig m_Config;
	CTransportLayer m_Transport;
};
#endif
