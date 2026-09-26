//
// wifiscan -- list the Wi-Fi access points around (ABI v45 wlan_scan), strongest first.
//   usage: wifiscan
// Columns: signal (dBm + bars), channel, security, SSID; '*' = the network we are on.
// Takes about 3 seconds. See also wpaconf (pick a network from the list).
//
#include "kapi.h"
#include "applib.h"

static void pad (const char *s, int w) { int n = ax_strlen (s); ax_puts (s); while (n++ < w) ax_puts (" "); }

int main (void)
{
	static struct kapi_wlan_ap ap[48];
	ax_putln ("Scanning...");
	int n = kapi_wlan_scan (ap, 48);
	if (n <= 0) { ax_putln ("No access point found (is the Wi-Fi firmware on the card?)"); return 1; }
	static const char *sec[] = { "open", "WEP", "WPA", "WPA2" };
	ax_putln ("  SIGNAL        CH  SECURITY  SSID");
	for (int i = 0; i < n; i++)
	{
		char b[16]; int l = ap[i].level;
		int bars = l >= -55 ? 4 : l >= -67 ? 3 : l >= -78 ? 2 : l >= -88 ? 1 : 0;
		ax_puts (ap[i].connected ? "* " : "  ");
		ax_itoa (l, b); ax_puts (b); ax_puts (" dBm ");
		for (int k = 0; k < 4; k++) ax_puts (k < bars ? "|" : ".");
		ax_puts ("  ");
		ax_itoa (ap[i].channel, b); pad (b, 4);
		pad (sec[ap[i].security & 3], 10);
		ax_putln (ap[i].ssid[0] ? ap[i].ssid : "(hidden)");
	}
	return 0;
}
