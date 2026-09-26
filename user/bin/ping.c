//
// ping -- send ICMP echo requests and show the round-trip times (ABI v43 net_ping).
//   usage: ping <host> [count]        (default 4 requests, 1 s apart, 2 s timeout each)
//
#include "kapi.h"
#include "applib.h"

static void put_ms (int us)			// "12.345 ms"
{
	char b[24]; int n = ax_itoa (us / 1000, b); b[n] = '\0';
	ax_puts (b);
	int f = us % 1000;
	char d[5] = { '.', (char) ('0' + f / 100), (char) ('0' + f / 10 % 10), (char) ('0' + f % 10), 0 };
	ax_puts (d); ax_puts (" ms");
}

int main (void)
{
	char args[128];
	kapi_get_args (args, sizeof args);
	char host[96]; int i = 0, k = 0;
	while (args[i] == ' ') i++;
	while (args[i] && args[i] != ' ' && k < 95) host[k++] = args[i++];
	host[k] = '\0';
	while (args[i] == ' ') i++;
	int count = 0; while (args[i] >= '0' && args[i] <= '9') count = count * 10 + (args[i++] - '0');
	if (count <= 0) count = 4;
	if (host[0] == '\0') { ax_putln ("usage: ping <host> [count]"); return 1; }

	char ip[40] = "";
	int sent = 0, got = 0, tmin = 0x7FFFFFFF, tmax = 0; long tsum = 0;
	for (int s = 1; s <= count; s++)
	{
		unsigned t0 = kapi_get_ticks ();
		int r = kapi_net_ping (host, (unsigned) s, 2000, ip, sizeof ip);
		if (r == -1) { ax_putln ("ping: network down"); return 1; }
		if (r == -3) { ax_puts ("ping: cannot resolve "); ax_putln (host); return 1; }
		if (s == 1) { ax_puts ("PING "); ax_puts (host); ax_puts (" ("); ax_puts (ip); ax_putln ("): 32 data bytes"); }
		sent++;
		char b[16];
		if (r >= 0)
		{
			got++; tsum += r; if (r < tmin) tmin = r; if (r > tmax) tmax = r;
			ax_puts ("40 bytes from "); ax_puts (ip); ax_puts (": icmp_seq=");
			b[ax_itoa (s, b)] = '\0'; ax_puts (b); ax_puts (" time="); put_ms (r); ax_putln ("");
		}
		else { ax_puts ("Request timeout for icmp_seq "); b[ax_itoa (s, b)] = '\0'; ax_putln (b); }
		if (s < count)				// one request per second
		{
			unsigned el = (kapi_get_ticks () - t0) * 10;
			if (el < 1000) kapi_msleep (1000 - el);
		}
	}
	char b[16];
	ax_puts ("--- "); ax_puts (host); ax_putln (" ping statistics ---");
	b[ax_itoa (sent, b)] = '\0'; ax_puts (b); ax_puts (" packets transmitted, ");
	b[ax_itoa (got, b)] = '\0'; ax_puts (b); ax_puts (" received, ");
	b[ax_itoa (sent ? (sent - got) * 100 / sent : 0, b)] = '\0'; ax_puts (b); ax_putln ("% packet loss");
	if (got)
	{
		ax_puts ("round-trip min/avg/max = "); put_ms (tmin); ax_puts (" / ");
		put_ms ((int) (tsum / got)); ax_puts (" / "); put_ms (tmax); ax_putln ("");
	}
	return got ? 0 : 1;
}
