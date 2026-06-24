//
// wget -- fetch an HTTP URL and write the response body to stdout.
//   usage: wget <http://host[:port]/path>
// Plain HTTP only (no TLS). Needs the WLAN link up (check with `net`). Demonstrates
// the user-side HTTP client (httpc.h) layered on the ABI v21 TCP sockets.
//
#include "kapi.h"
#include "applib.h"
#include "httpc.h"

static char g_buf[64 * 1024];		// response buffer -> app .bss (its own address space)

int main (void)
{
	char args[512];
	kapi_get_args (args, sizeof args);

	int i = 0; while (args[i] == ' ' || args[i] == '\t') i++;	// first token = URL
	char url[400]; int u = 0;
	while (args[i] && args[i] != ' ' && args[i] != '\t' && args[i] != '\n'
	       && args[i] != '\r' && u < (int) sizeof url - 1) url[u++] = args[i++];
	url[u] = '\0';

	if (url[0] == '\0') { ax_putln ("usage: wget <http://host[:port]/path>"); return 1; }
	if (!kapi_net_status (0, 0)) { ax_putln ("wget: network down (see `net`)"); return 1; }

	http_response r;
	int n = http_get (url, g_buf, sizeof g_buf, &r);
	if (n < 0) { ax_putln (n == -1 ? "wget: bad URL" : "wget: connection failed"); return 1; }

	char nb[16];
	ax_puts ("HTTP "); ax_itoa (r.status, nb); ax_puts (nb);
	ax_puts (r.ok ? " -- " : " (non-2xx) -- "); ax_itoa (r.body_len, nb); ax_puts (nb);
	ax_putln (r.truncated ? " body bytes (TRUNCATED)" : " body bytes");

	if (r.body_len > 0)
	{
		kapi_stdout_write (r.body, (unsigned) r.body_len);
		if (r.body[r.body_len - 1] != '\n') kapi_stdout_write ("\n", 1);
	}
	return 0;
}
