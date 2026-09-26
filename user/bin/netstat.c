//
// netstat -- the network configuration and the open TCP sockets (ABI v43 net_info).
//   usage: netstat
//
#include "kapi.h"
#include "applib.h"

static void pad (const char *s, int w)		// s, then spaces up to w columns
{
	ax_puts (s);
	for (int n = ax_strlen (s); n < w; n++) ax_puts (" ");
}

int main (void)
{
	static char info[2048];
	kapi_net_info (info, sizeof info);
	int sockets = 0;
	for (char *p = info; *p; )
	{
		char *line = p;
		while (*p && *p != '\n') p++;
		if (*p) *p++ = '\0';
		if (line[0] == 't' && line[1] == 'c' && line[2] == 'p' && line[3] == ' ')
		{
			// "tcp <h> listen|conn <port> <remote> <pid>"
			char *f[6]; int n = 0;
			for (char *q = line; *q && n < 6; )
			{
				while (*q == ' ') *q++ = '\0';
				if (*q) f[n++] = q;
				while (*q && *q != ' ') q++;
			}
			if (n < 6) continue;
			if (sockets++ == 0) { ax_putln (""); ax_putln ("Proto  State    Local port  Remote address   PID"); }
			pad ("tcp", 7);
			pad (f[2][0] == 'l' ? "LISTEN" : "ESTAB", 9);
			pad (f[3], 12);
			pad (f[4][0] == '-' ? "*" : f[4], 17);
			ax_putln (f[5]);
			continue;
		}
		char *v = line; while (*v && *v != ' ') v++;
		if (*v) *v++ = '\0';
		pad (line, 10); ax_putln (v);
	}
	if (sockets == 0) { ax_putln (""); ax_putln ("(no open TCP sockets)"); }
	return 0;
}
