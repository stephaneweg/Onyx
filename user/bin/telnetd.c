//
// telnetd -- remote text shell over TCP (telnet-compatible, NOT secure).
//   usage: telnetd [port]          (default 23; e.g. `telnetd` in SD:/etc/autostart)
//
// Waits for the WLAN link, listens on the port (kapi_tcp_listen, ABI v37) and serves
// one client at a time: each connection gets its own /bin/cmd, wired exactly like the
// terminal does it -- two pipes, the client's keystrokes go to cmd's stdin and cmd's
// stdout goes back to the client. Line editing and echo are done here (the client is
// put in character mode with IAC WILL ECHO / WILL SGA), so any telnet client works:
// `telnet <ip>`, PuTTY (Telnet), or tools/onyx-telnet.py. Incoming telnet option
// negotiation is parsed and ignored.
//
// No authentication, no encryption: anyone on the LAN who reaches the port gets a
// shell. Keep it for a trusted network.
//
#include "kapi.h"
#include "applib.h"

#define IAC	255
#define WILL	251
#define WONT	252
#define DO	253
#define DONT	254
#define SB	250
#define SE	240
#define OPT_ECHO 1
#define OPT_SGA	 3

static int  g_sock;			// connected client
static void *g_to_cmd, *g_from_cmd;	// cmd's stdin / stdout pipes

static char g_out[1024];		// pending bytes for the client
static int  g_outlen;

static void flush_out (void)
{
	if (g_outlen > 0) kapi_tcp_send (g_sock, g_out, (unsigned) g_outlen);
	g_outlen = 0;
}
static void out_byte (char c)
{
	if (g_outlen >= (int) sizeof g_out) flush_out ();
	g_out[g_outlen++] = c;
}
static void out_str (const char *s) { while (*s) out_byte (*s++); }

// cmd output -> client: "\n" -> CRLF, form-feed (`clear`) -> ANSI clear screen,
// a literal 0xFF is doubled (telnet escaping).
static void out_from_cmd (const char *b, int n)
{
	for (int i = 0; i < n; i++)
	{
		char c = b[i];
		if (c == '\n')		out_str ("\r\n");
		else if (c == '\f')	out_str ("\x1b[2J\x1b[H");
		else if ((unsigned char) c == IAC) { out_byte ((char) IAC); out_byte ((char) IAC); }
		else			out_byte (c);
	}
}

// ---- client input: telnet parser + line editor -------------------------------

enum { S_DATA, S_IAC, S_OPT, S_SB, S_SB_IAC };
static int  g_state;
static int  g_skip_lf;			// swallow the LF / NUL that follows a CR
static char g_line[512];
static int  g_linelen;
static int  g_quit;			// Ctrl-D on an empty line -> end cmd

static void submit_line (void)
{
	out_str ("\r\n");
	kapi_stream_write (g_to_cmd, g_line, (unsigned) g_linelen);
	kapi_stream_write (g_to_cmd, "\n", 1);
	g_linelen = 0;
}

static void key (unsigned char c)
{
	if (g_skip_lf) { g_skip_lf = 0; if (c == '\n' || c == 0) return; }

	if (c == '\r')		 { g_skip_lf = 1; submit_line (); }
	else if (c == '\n')	 submit_line ();		// raw clients (nc) send bare LF
	else if (c == 8 || c == 127)				// Backspace / DEL
	{
		if (g_linelen > 0) { g_linelen--; out_str ("\b \b"); }
	}
	else if (c == 3)					// Ctrl-C: drop the line, tell cmd
	{
		g_linelen = 0;
		out_str ("^C\r\n");
		kapi_stream_write (g_to_cmd, "\x03", 1);
	}
	else if (c == 4)					// Ctrl-D: EOF on an empty line
	{
		if (g_linelen == 0) { kapi_stream_write (g_to_cmd, "\x04", 1); g_quit = 1; }
	}
	else if (c >= ' ' && c < 127 && g_linelen < (int) sizeof g_line - 1)
	{
		g_line[g_linelen++] = (char) c;
		out_byte ((char) c);				// server-side echo
	}
}

static void from_client (const unsigned char *b, int n)
{
	for (int i = 0; i < n; i++)
	{
		unsigned char c = b[i];
		switch (g_state)
		{
		case S_DATA:	if (c == IAC) g_state = S_IAC; else key (c); break;
		case S_IAC:
			if (c == IAC)	{ key (c); g_state = S_DATA; }	// escaped 0xFF
			else if (c >= WILL && c <= DONT) g_state = S_OPT;
			else if (c == SB) g_state = S_SB;
			else		g_state = S_DATA;		// NOP, GA, AYT, ...
			break;
		case S_OPT:	g_state = S_DATA; break;		// option byte: ignored
		case S_SB:	if (c == IAC) g_state = S_SB_IAC; break;
		case S_SB_IAC:	g_state = (c == SE) ? S_DATA : S_SB; break;
		}
	}
}

// ---- one client session --------------------------------------------------------

static void session (const char *peer)
{
	g_state = S_DATA; g_skip_lf = 0; g_linelen = 0; g_quit = 0; g_outlen = 0;

	static const unsigned char nego[] = { IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA, IAC, DO, OPT_SGA };
	kapi_tcp_send (g_sock, nego, sizeof nego);

	g_to_cmd   = kapi_pipe ();
	g_from_cmd = kapi_pipe ();
	void *proc = kapi_spawn ("SD:/bin/cmd", "", g_to_cmd, g_from_cmd);
	if (!proc)
	{
		out_str ("telnetd: cannot start /bin/cmd\r\n"); flush_out ();
		kapi_stream_close (g_to_cmd); kapi_stream_close (g_from_cmd);
		return;
	}
	out_str ("Onyx remote shell -- connected from "); out_str (peer); out_str ("\r\n");

	static unsigned char in[512];
	static char buf[512];
	int peer_gone = 0;
	for (;;)
	{
		int busy = 0, n;

		n = kapi_tcp_recv (g_sock, in, sizeof in);
		if (n < 0) { peer_gone = 1; break; }		// client closed the connection
		if (n > 0) { from_client (in, n); busy = 1; }

		while ((n = kapi_stream_read_nb (g_from_cmd, buf, sizeof buf)) > 0)
		{
			out_from_cmd (buf, n); busy = 1;
		}
		flush_out ();

		if (kapi_proc_done (proc)) break;		// `exit` / Ctrl-D / killed
		if (!busy) kapi_msleep (10);
	}

	if (peer_gone)
	{
		// Client dropped: end cmd with EOF on its stdin, give it a moment to exit.
		kapi_stream_eof (g_to_cmd);
		for (int i = 0; i < 300 && !kapi_proc_done (proc); i++)
		{
			while (kapi_stream_read_nb (g_from_cmd, buf, sizeof buf) > 0) {}	// keep it unblocked
			kapi_msleep (10);
		}
	}
	else
	{
		while ((kapi_stream_read_nb (g_from_cmd, buf, sizeof buf)) > 0) {}
		out_str ("\r\nsession closed\r\n");
		flush_out ();
	}
	if (kapi_proc_done (proc)) kapi_wait (proc);	// reap (else cmd is left to finish)
	kapi_stream_close (g_to_cmd);
	kapi_stream_close (g_from_cmd);
}

int main (void)
{
	char args[64];
	kapi_get_args (args, sizeof args);
	unsigned port = 0;
	for (int i = 0; args[i] >= '0' && args[i] <= '9'; i++) port = port * 10 + (unsigned) (args[i] - '0');
	if (port == 0 || port > 65535) port = 23;

	char ip[32];
	if (!kapi_net_status (ip, sizeof ip))
	{
		ax_putln ("telnetd: waiting for the network...");
		while (!kapi_net_status (ip, sizeof ip)) kapi_msleep (1000);
	}

	int lsock = kapi_tcp_listen (port);
	if (lsock < 0) { ax_putln ("telnetd: cannot listen (port in use?)"); return 1; }

	char nb[16]; ax_itoa ((int) port, nb);
	ax_puts ("telnetd: listening on "); ax_puts (ip); ax_puts (":"); ax_putln (nb);

	for (;;)
	{
		char peer[32];
		g_sock = kapi_tcp_accept (lsock, peer, sizeof peer);	// blocks
		if (g_sock < 0) { kapi_msleep (500); continue; }
		ax_puts ("telnetd: client "); ax_putln (peer);
		session (peer);
		kapi_tcp_close (g_sock);
		ax_putln ("telnetd: client gone");
	}
}
