//
// whois -- query the WHOIS database (TCP port 43). Asks whois.iana.org first, then
// follows its "refer:" line to the registry that holds the domain (e.g. whois.verisign-
// grs.com for .com), unless a server is given.
//   usage: whois <domain> [server]
//
#include "kapi.h"
#include "applib.h"

static char g_resp[16384];

// Send "query\r\n" to server:43, read the whole answer into g_resp. Length, or -1.
static int query (const char *server, const char *q)
{
	int s = kapi_tcp_connect (server, 43);
	if (s < 0) return -1;
	char line[160]; int n = 0;
	for (int i = 0; q[i] && n < 156; i++) line[n++] = q[i];
	line[n++] = '\r'; line[n++] = '\n';
	kapi_tcp_send (s, line, (unsigned) n);
	int len = 0; unsigned idle = kapi_get_ticks ();
	for (;;)
	{
		int r = kapi_tcp_recv (s, g_resp + len, sizeof g_resp - 1 - len);
		if (r < 0) break;					// closed: done
		if (r > 0) { len += r; idle = kapi_get_ticks (); if (len >= (int) sizeof g_resp - 1) break; }
		else
		{
			if (kapi_get_ticks () - idle > 1000) break;	// 10 s of silence
			kapi_msleep (20);
		}
	}
	kapi_tcp_close (s);
	g_resp[len] = '\0';
	return len;
}

// The value of a "refer:" / "whois:" line, if any.
static int find_refer (char *out, int cap)
{
	for (const char *p = g_resp; *p; )
	{
		const char *l = p;
		while (*p && *p != '\n') p++;
		const char *keys[2] = { "refer:", "whois:" };
		for (int k = 0; k < 2; k++)
		{
			int m = 0; while (keys[k][m] && l[m] == keys[k][m]) m++;
			if (keys[k][m] == '\0')
			{
				const char *v = l + m; while (*v == ' ' || *v == '\t') v++;
				int n = 0; while (v < p && *v != '\r' && *v != ' ' && n < cap - 1) out[n++] = *v++;
				out[n] = '\0';
				if (n) return 1;
			}
		}
		if (*p) p++;
	}
	return 0;
}

static void print_resp (void)
{
	for (char *p = g_resp; *p; p++) if (*p != '\r') kapi_stdout_write (p, 1);
	ax_putln ("");
}

int main (void)
{
	char args[160];
	kapi_get_args (args, sizeof args);
	char dom[96], server[96] = ""; int i = 0, k = 0;
	while (args[i] == ' ') i++;
	while (args[i] && args[i] != ' ' && k < 95) dom[k++] = args[i++];
	dom[k] = '\0';
	while (args[i] == ' ') i++;
	k = 0; while (args[i] && args[i] != ' ' && k < 95) server[k++] = args[i++];
	server[k] = '\0';
	if (dom[0] == '\0') { ax_putln ("usage: whois <domain> [server]"); return 1; }
	if (!kapi_net_status (0, 0)) { ax_putln ("whois: network down"); return 1; }

	if (server[0] == '\0')
	{
		ax_putln ("[whois.iana.org]");
		if (query ("whois.iana.org", dom) < 0) { ax_putln ("whois: cannot reach whois.iana.org"); return 1; }
		if (!find_refer (server, sizeof server)) { print_resp (); return 0; }
	}
	ax_puts ("["); ax_puts (server); ax_putln ("]");
	if (query (server, dom) < 0) { ax_puts ("whois: cannot reach "); ax_putln (server); return 1; }
	print_resp ();
	return 0;
}
