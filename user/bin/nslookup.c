//
// nslookup -- resolve a host name through the DNS server (ABI v43 net_resolve).
//   usage: nslookup <name>
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[128];
	kapi_get_args (args, sizeof args);
	char host[96]; int i = 0, k = 0;
	while (args[i] == ' ') i++;
	while (args[i] && args[i] != ' ' && k < 95) host[k++] = args[i++];
	host[k] = '\0';
	if (host[0] == '\0') { ax_putln ("usage: nslookup <name>"); return 1; }

	static char info[1024];
	kapi_net_info (info, sizeof info);
	const char *dns = 0;					// "dns a.b.c.d" line
	for (const char *p = info; *p; )
	{
		if (p[0] == 'd' && p[1] == 'n' && p[2] == 's' && p[3] == ' ') { dns = p + 4; break; }
		while (*p && *p != '\n') p++;
		if (*p) p++;
	}
	ax_puts ("Server:  ");
	if (dns) { while (*dns && *dns != '\n') kapi_stdout_write (dns++, 1); }
	else ax_puts ("(none)");
	ax_putln ("");
	char ip[40];
	if (!kapi_net_resolve (host, ip, sizeof ip))
	{
		ax_puts ("** can't find "); ax_puts (host); ax_putln (" (no answer, or the network is down)");
		return 1;
	}
	ax_puts ("Name:    "); ax_putln (host);
	ax_puts ("Address: "); ax_putln (ip);
	return 0;
}
