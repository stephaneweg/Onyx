//
// net -- show the WLAN link status + IP address (a quick way to check whether the
// kernel's network stack associated, without launching the IRC client).
//   usage: net
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char ip[40];
	if (kapi_net_status (ip, sizeof ip))
	{
		ax_puts ("link up, IP ");
		ax_putln (ip);
		return 0;
	}
	ax_putln ("link down (WLAN not associated -- check firmware + wpa_supplicant.conf)");
	return 1;
}
