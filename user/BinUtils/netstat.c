//
// netstat -- the network configuration and the open TCP and UDP sockets (ABI v43 net_info; UDP and
// the BSD sockets since v75: up to 256 lines).
//   usage: netstat
//
#include "appkit/appkit.h"

static void pad (const char *s, int w)		// s, then spaces up to w columns
{
	ax_puts (s);
	for (int n = ax_strlen (s); n < w; n++) ax_puts (" ");
}

int main (void)
{
	static char info[12288];
	kapi_net_info (info, sizeof info);
	int sockets = 0;
	for (char *p = info; *p; )
	{
		char *line = p;
		while (*p && *p != '\n') p++;
		if (*p) *p++ = '\0';
		int udp = line[0] == 'u' && line[1] == 'd' && line[2] == 'p' && line[3] == ' ';
		if ((line[0] == 't' && line[1] == 'c' && line[2] == 'p' && line[3] == ' ') || udp)
		{
			// "tcp <h> listen|conn <port> <remote> <pid>", "udp <h> bound <port> <peer|-> <pid>" (v75)
			char *f[6]; int n = 0;
			for (char *q = line; *q && n < 6; )
			{
				while (*q == ' ') *q++ = '\0';
				if (*q) f[n++] = q;
				while (*q && *q != ' ') q++;
			}
			if (n < 6) continue;
			if (sockets++ == 0) { ax_putln (""); ax_putln ("Proto  State    Local port  Remote address   PID"); }
			pad (udp ? "udp" : "tcp", 7);
			pad (udp ? "BOUND" : f[2][0] == 'l' ? "LISTEN" : "ESTAB", 9);
			pad (f[3], 12);
			pad (f[4][0] == '-' ? "*" : f[4], 17);
			ax_putln (f[5]);
			continue;
		}
		char *v = line; while (*v && *v != ' ') v++;
		if (*v) *v++ = '\0';
		pad (line, 10); ax_putln (v);
	}
	if (sockets == 0) { ax_putln (""); ax_putln ("(no open sockets)"); }
	return 0;
}
