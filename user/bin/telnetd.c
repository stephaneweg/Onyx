//
// telnetd -- remote text shell over TCP (telnet-compatible, NOT secure).
//   usage: telnetd [port]          (default 23; e.g. `telnetd` in SD:/etc/autostart)
//
// Waits for the WLAN link, listens on the port (kapi_tcp_listen, ABI v37) and serves
// each client in a THREAD of its own (kernel v67; up to MAX_SESSIONS at once -- an older
// kernel: one client at a time, in the main thread): each connection gets its own
// /bin/cmd, wired exactly like the terminal does it -- two pipes, the client's keystrokes
// go to cmd's stdin and cmd's stdout goes back to the client. Line editing and echo are
// done here (the client is put in character mode with IAC WILL ECHO / WILL SGA; lineedit.h:
// the arrows move the cursor in the line and recall the lines sent before -- the client's
// ANSI escape sequences are read, the line is redrawn with backspaces and "erase to the end
// of the line"; a line longer than the client's window is not redrawn well), so any
// telnet client works: `telnet <ip>`, PuTTY (Telnet), or tools/onyx-telnet.py. Incoming
// telnet option negotiation is parsed and ignored.
//
// A client that goes without `exit` (its connection closed or reset, or nothing coming back
// to the NOP sent every KEEPALIVE_MS of silence) ends its session: the shell AND what it runs
// in the foreground are stopped (shellend.h) -- before 2026-10-05 a program still running kept
// its shell, both then blocked for good on a pipe nobody read. A program meant to stay after
// the session is started detached: `run SD:/bin/ftpd SD:/`. So does a connection from which
// nothing at all was heard after KEEPALIVE_MS (see session ()).
//
// No authentication, no encryption: anyone on the LAN who reaches the port gets a
// shell. Keep it for a trusted network.
//
#include "kapi.h"
#include "applib.h"
#include "lineedit.h"
#include "shellend.h"

#define IAC	255
#define WILL	251
#define WONT	252
#define DO	253
#define DONT	254
#define SB	250
#define SE	240
#define NOP	241
#define OPT_ECHO 1
#define OPT_SGA	 3

#define MAX_SESSIONS	8
// A silent session: a telnet NOP that often (is the client there?). Longer than the time the
// kernel's TCP takes to give a segment up (5 tries, 63 s from a 1 s timeout, 189 s from 3 s):
// every segment sent starts that timer again, so a NOP each 30 s kept a dead connection for ever
// (tried, 2026-10-05, against a PC whose firewall answers nothing).
#define KEEPALIVE_MS	300000

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
	struct LineEdit le;		// the line being typed (cmd takes 2047 characters), the history
	int  shown;			// where the client's cursor is in that line
	int  esc, escnum;		// an escape sequence being read (E_*), its number
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

enum { E_NONE, E_ESC, E_CSI, E_SS3 };

// The line shown again after an edit: back to its start, the text, the rest of the client's
// line erased, the cursor put back.
static void redraw (struct Session *s)
{
	for (int i = 0; i < s->shown; i++) out_byte (s, '\b');
	for (int i = 0; i < s->le.len; i++) out_byte (s, s->le.buf[i]);
	out_str (s, "\x1b[K");
	for (int i = s->le.len; i > s->le.cur; i--) out_byte (s, '\b');
	s->shown = s->le.cur;
}

// only the cursor moved: backspaces to go left, the line's own characters to go right
static void move (struct Session *s)
{
	while (s->shown > s->le.cur) { out_byte (s, '\b'); s->shown--; }
	while (s->shown < s->le.cur) out_byte (s, s->le.buf[s->shown++]);
}

static void submit_line (struct Session *s)
{
	out_str (s, "\r\n");
	kapi_stream_write (s->to_cmd, s->le.buf, (unsigned) s->le.len);
	kapi_stream_write (s->to_cmd, "\n", 1);
	le_commit (&s->le, 1);
	s->shown = 0;
}

// the end of an escape sequence: an arrow, Home / End / Delete
static void escape_key (struct Session *s, unsigned char c)
{
	int ch = 0;					// (the text changed: the line is drawn again)
	switch (c)
	{
	case 'A': ch = le_up (&s->le); break;
	case 'B': ch = le_down (&s->le); break;
	case 'C': le_right (&s->le); break;
	case 'D': le_left (&s->le); break;
	case 'H': le_home (&s->le); break;
	case 'F': le_end (&s->le); break;
	case '~':
		if (s->escnum == 1 || s->escnum == 7) le_home (&s->le);
		else if (s->escnum == 4 || s->escnum == 8) le_end (&s->le);
		else if (s->escnum == 3) ch = le_delete (&s->le);
		break;
	}
	if (ch) redraw (s); else move (s);
}

static void key (struct Session *s, unsigned char c)
{
	if (s->skip_lf) { s->skip_lf = 0; if (c == '\n' || c == 0) return; }

	if (s->esc == E_ESC)					// ESC [ ... or ESC O x
	{
		s->esc = c == '[' ? E_CSI : c == 'O' ? E_SS3 : E_NONE;
		s->escnum = 0;
		if (s->esc != E_NONE) return;
	}
	else if (s->esc == E_CSI)
	{
		if (c >= '0' && c <= '9') { s->escnum = s->escnum * 10 + (c - '0'); return; }
		if (c == ';') { s->escnum = 0; return; }
		s->esc = E_NONE;
		escape_key (s, c);
		return;
	}
	else if (s->esc == E_SS3) { s->esc = E_NONE; s->escnum = 0; escape_key (s, c); return; }

	if (c == 27)		 s->esc = E_ESC;
	else if (c == '\r')	 { s->skip_lf = 1; submit_line (s); }
	else if (c == '\n')	 submit_line (s);		// raw clients (nc) send bare LF
	else if (c == 8 || c == 127)				// Backspace / DEL
	{
		if (le_backspace (&s->le)) redraw (s);
	}
	else if (c == 1)	 { le_home (&s->le); move (s); }		// Ctrl-A
	else if (c == 5)	 { le_end (&s->le); move (s); }			// Ctrl-E
	else if (c == 11)	 { if (le_kill (&s->le)) redraw (s); }		// Ctrl-K
	else if (c == 21)	 { if (le_clear (&s->le)) redraw (s); }		// Ctrl-U
	else if (c == 3)					// Ctrl-C: drop the line, tell cmd
	{
		le_commit (&s->le, 0); s->shown = 0;
		out_str (s, "^C\r\n");
		kapi_stream_write (s->to_cmd, "\x03", 1);
	}
	else if (c == 4)					// Ctrl-D: EOF on an empty line
	{
		if (s->le.len == 0) { kapi_stream_write (s->to_cmd, "\x04", 1); s->quit = 1; }
	}
	else if (c >= ' ' && c < 127)
	{
		int at_end = s->le.cur == s->le.len;
		if (!le_insert (&s->le, (char) c)) return;
		if (at_end) { out_byte (s, (char) c); s->shown++; }	// server-side echo
		else redraw (s);
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
	s->state = S_DATA; s->skip_lf = 0; s->quit = 0; s->outlen = 0;
	le_init (&s->le); s->shown = 0; s->esc = E_NONE; s->escnum = 0;

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

	int peer_gone = 0, idle = 0, heard = 0;
	for (;;)
	{
		int busy = 0, n;

		n = kapi_tcp_recv (s->sock, s->in, sizeof s->in);
		if (n < 0) { peer_gone = 1; break; }		// client closed the connection
		if (n > 0) { from_client (s, s->in, n); busy = 1; heard = 1; }

		while ((n = kapi_stream_read_nb (s->from_cmd, s->buf, sizeof s->buf)) > 0)
		{
			out_from_cmd (s, s->buf, n); busy = 1;
		}
		flush_out (s);

		if (kapi_proc_done (proc)) break;		// `exit` / Ctrl-D / killed
		if (busy) { idle = 0; continue; }
		kapi_msleep (10);
		// Nothing said for a while: a telnet NOP, which every client ignores. A client that
		// left without a word reaching us (its machine off, its last packet lost) is found
		// out by it: reset by the machine, or never acknowledged, the connection ends and
		// tcp_recv says so. (A send's own result tells nothing: a slow client fails it too.)
		if ((idle += 10) >= KEEPALIVE_MS)
		{
			static const unsigned char nop[] = { IAC, NOP };
			idle = 0;
			// Not one byte from this client since it connected, not even an answer to the
			// negotiation: the session is ended. The kernel's network now and then hands over
			// a connection that is deaf and mute (made right after another one ended: the
			// client gets no greeting and gives up, nothing of it is ever received -- its
			// close neither -- and nothing sent to it times out; seen 2026-10-05, an open
			// bug there): such sessions kept a shell each, for ever.
			if (!heard) { peer_gone = 1; break; }
			kapi_tcp_send (s->sock, nop, sizeof nop);
		}
	}

	if (peer_gone)
	{
		// Client dropped: the shell and what it runs in the foreground end with the session
		// (shellend.h: Ctrl-C, the end of its input, its output read meanwhile; terminated if
		// it is still there after a few seconds).
		shell_end (proc, s->to_cmd, s->from_cmd);
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
	int threads = kapi_abi_version () >= 67;

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
