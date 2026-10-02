//
// telnetd -- remote text shell over TCP (telnet-compatible, NOT secure).
//   usage: telnetd [port]          (default 23; e.g. `telnetd` in SD:/etc/autostart)
//
// Waits for the WLAN link, listens on the port (kapi_tcp_listen, ABI v37) and serves
// each client in a THREAD of its own (kernel v67; up to MAX_SESSIONS at once -- an older
// kernel: one client at a time, in the main thread): each connection gets its own
// /bin/cmd, wired exactly like the terminal does it -- two pipes, the client's keystrokes
// go to cmd's stdin and cmd's stdout goes back to the client. Line editing and echo are
// done here (the client is put in character mode with IAC WILL ECHO / WILL SGA), so any
// telnet client works: `telnet <ip>`, PuTTY (Telnet), or tools/onyx-telnet.py. Incoming
// telnet option negotiation is parsed and ignored. A session whose cmd is stuck (a tool
// waiting on its stdin after the client left) no longer holds the others up.
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

#define MAX_SESSIONS	8

// One client: its socket, cmd's pipes, the output buffer, the telnet parser + line editor.
struct Session
{
	volatile int used;		// (slot taken; freed by its thread at the end)
	int  sock;			// connected client
	void *to_cmd, *from_cmd;	// cmd's stdin / stdout pipes
	char peer[32];
	char out[1024];			// pending bytes for the client
	int  outlen;
	int  state;			// telnet parser state (S_*)
	int  skip_lf;			// swallow the LF / NUL that follows a CR
	char line[2048];				// (cmd takes 2047)
	int  linelen;
	int  quit;			// Ctrl-D on an empty line -> end cmd
	unsigned char in[512];
	char buf[512];
};
static struct Session g_sess[MAX_SESSIONS];
static volatile int g_log;		// kapi_lock: the log lines on telnetd's own stdout

static void log2 (const char *a, const char *b)
{
	kapi_lock (&g_log);
	ax_puts (a); ax_putln (b);
	kapi_unlock (&g_log);
}

static void flush_out (struct Session *s)
{
	if (s->outlen > 0) kapi_tcp_send (s->sock, s->out, (unsigned) s->outlen);
	s->outlen = 0;
}
static void out_byte (struct Session *s, char c)
{
	if (s->outlen >= (int) sizeof s->out) flush_out (s);
	s->out[s->outlen++] = c;
}
static void out_str (struct Session *s, const char *p) { while (*p) out_byte (s, *p++); }

// cmd output -> client: "\n" -> CRLF, form-feed (`clear`) -> ANSI clear screen,
// a literal 0xFF is doubled (telnet escaping).
static void out_from_cmd (struct Session *s, const char *b, int n)
{
	for (int i = 0; i < n; i++)
	{
		char c = b[i];
		if (c == '\n')		out_str (s, "\r\n");
		else if (c == '\f')	out_str (s, "\x1b[2J\x1b[H");
		else if ((unsigned char) c == IAC) { out_byte (s, (char) IAC); out_byte (s, (char) IAC); }
		else			out_byte (s, c);
	}
}

// ---- client input: telnet parser + line editor -------------------------------

enum { S_DATA, S_IAC, S_OPT, S_SB, S_SB_IAC };

static void submit_line (struct Session *s)
{
	out_str (s, "\r\n");
	kapi_stream_write (s->to_cmd, s->line, (unsigned) s->linelen);
	kapi_stream_write (s->to_cmd, "\n", 1);
	s->linelen = 0;
}

static void key (struct Session *s, unsigned char c)
{
	if (s->skip_lf) { s->skip_lf = 0; if (c == '\n' || c == 0) return; }

	if (c == '\r')		 { s->skip_lf = 1; submit_line (s); }
	else if (c == '\n')	 submit_line (s);		// raw clients (nc) send bare LF
	else if (c == 8 || c == 127)				// Backspace / DEL
	{
		if (s->linelen > 0) { s->linelen--; out_str (s, "\b \b"); }
	}
	else if (c == 3)					// Ctrl-C: drop the line, tell cmd
	{
		s->linelen = 0;
		out_str (s, "^C\r\n");
		kapi_stream_write (s->to_cmd, "\x03", 1);
	}
	else if (c == 4)					// Ctrl-D: EOF on an empty line
	{
		if (s->linelen == 0) { kapi_stream_write (s->to_cmd, "\x04", 1); s->quit = 1; }
	}
	else if (c >= ' ' && c < 127 && s->linelen < (int) sizeof s->line - 1)
	{
		s->line[s->linelen++] = (char) c;
		out_byte (s, (char) c);				// server-side echo
	}
}

static void from_client (struct Session *s, const unsigned char *b, int n)
{
	for (int i = 0; i < n; i++)
	{
		unsigned char c = b[i];
		switch (s->state)
		{
		case S_DATA:	if (c == IAC) s->state = S_IAC; else key (s, c); break;
		case S_IAC:
			if (c == IAC)	{ key (s, c); s->state = S_DATA; }	// escaped 0xFF
			else if (c >= WILL && c <= DONT) s->state = S_OPT;
			else if (c == SB) s->state = S_SB;
			else		s->state = S_DATA;		// NOP, GA, AYT, ...
			break;
		case S_OPT:	s->state = S_DATA; break;		// option byte: ignored
		case S_SB:	if (c == IAC) s->state = S_SB_IAC; break;
		case S_SB_IAC:	s->state = (c == SE) ? S_DATA : S_SB; break;
		}
	}
}

// ---- one client session --------------------------------------------------------

static void session (struct Session *s)
{
	s->state = S_DATA; s->skip_lf = 0; s->linelen = 0; s->quit = 0; s->outlen = 0;

	static const unsigned char nego[] = { IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA, IAC, DO, OPT_SGA };
	kapi_tcp_send (s->sock, nego, sizeof nego);

	s->to_cmd   = kapi_pipe ();
	s->from_cmd = kapi_pipe ();
	void *proc = kapi_spawn ("SD:/bin/cmd", "", s->to_cmd, s->from_cmd);
	if (!proc)
	{
		out_str (s, "telnetd: cannot start /bin/cmd\r\n"); flush_out (s);
		kapi_stream_close (s->to_cmd); kapi_stream_close (s->from_cmd);
		return;
	}
	out_str (s, "Onyx remote shell -- connected from "); out_str (s, s->peer); out_str (s, "\r\n");

	int peer_gone = 0;
	for (;;)
	{
		int busy = 0, n;

		n = kapi_tcp_recv (s->sock, s->in, sizeof s->in);
		if (n < 0) { peer_gone = 1; break; }		// client closed the connection
		if (n > 0) { from_client (s, s->in, n); busy = 1; }

		while ((n = kapi_stream_read_nb (s->from_cmd, s->buf, sizeof s->buf)) > 0)
		{
			out_from_cmd (s, s->buf, n); busy = 1;
		}
		flush_out (s);

		if (kapi_proc_done (proc)) break;		// `exit` / Ctrl-D / killed
		if (!busy) kapi_msleep (10);
	}

	if (peer_gone)
	{
		// Client dropped: end cmd with EOF on its stdin, give it a moment to exit.
		kapi_stream_eof (s->to_cmd);
		for (int i = 0; i < 300 && !kapi_proc_done (proc); i++)
		{
			while (kapi_stream_read_nb (s->from_cmd, s->buf, sizeof s->buf) > 0) {}	// keep it unblocked
			kapi_msleep (10);
		}
	}
	else
	{
		while ((kapi_stream_read_nb (s->from_cmd, s->buf, sizeof s->buf)) > 0) {}
		out_str (s, "\r\nsession closed\r\n");
		flush_out (s);
	}
	if (kapi_proc_done (proc)) kapi_wait (proc);	// reap (else cmd is left to finish)
	kapi_stream_close (s->to_cmd);
	kapi_stream_close (s->from_cmd);
}

// A session from start to end: the thread's body (or called directly without threads).
static int session_thread (void *arg)
{
	struct Session *s = arg;
	session (s);
	kapi_tcp_close (s->sock);
	log2 ("telnetd: client gone ", s->peer);
	__asm__ volatile ("dmb ish" ::: "memory");
	s->used = 0;				// (the slot is free again)
	return 0;
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
	int threads = KT->version >= 67;

	for (;;)
	{
		struct Session *s = 0;
		for (int i = 0; i < MAX_SESSIONS && s == 0; i++) if (!g_sess[i].used) s = &g_sess[i];
		if (s == 0) { kapi_msleep (200); continue; }	// all taken: wait for one to end

		s->sock = kapi_tcp_accept (lsock, s->peer, sizeof s->peer);	// blocks
		if (s->sock < 0) { kapi_msleep (500); continue; }
		log2 ("telnetd: client ", s->peer);
		s->used = 1;
		// its own thread; without threads (an older kernel), right here, one at a time
		if (!threads || kapi_thread_create (session_thread, s, 64 * 1024, "session") < 0)
			session_thread (s);
	}
}
