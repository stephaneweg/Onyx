//
// irc.c -- a minimal IRC client over the Onyx WLAN TCP sockets (kapi_tcp_*).
//
// It owns a window with a scrollback of the conversation and a single input line.
// On start it waits for the network to come up (kapi_net_status), connects to the
// server from config.ini, registers (NICK/USER), auto-joins a channel on welcome
// (numeric 001), answers PINGs, and shows PRIVMSG/NOTICE/server lines. Typing a
// line sends it to the current channel; lines starting with '/' are commands:
//   /join #chan   /part [#chan]   /nick name   /msg target text
//   /me action    /quit [msg]     /raw <any IRC command>
//
// Plain-text IRC only (port 6667) -- there is no TLS in the stack yet. Recv is
// non-blocking (polled each frame); connect/DNS/send block cooperatively, which is
// fine for the brief connect and for short chat lines.
//
#include "kapi.h"
#include "uikit.hpp"
#include "applib.h"

#define W		680
#define H		440
#define SCROLLBACK	300		// bounded scrollback (rows)
#define COLS		120		// stored chars per line (longer lines truncate)

static unsigned *fb;
static int g_fw = 8, g_fh = 16, g_vrows = 2, g_chatrows = 1;

// Scrollback ring of complete lines (server + local echo). IRC lines arrive whole,
// so unlike the terminal there is no partial "current line".
static char g_ring[SCROLLBACK][COLS + 1];
static int  g_rfirst = 0, g_rcount = 0;
static int  g_scroll = 0;		// 0 = pinned to bottom

// Connection + config.
static char g_server[80]  = "irc.libera.chat";
static char g_nick[32]    = "onyx";
static char g_user[32]    = "onyx";
static char g_real[64]    = "Onyx IRC user";
static char g_channel[64] = "#onyx";
static unsigned g_port    = 6667;

static int  g_sock = -1;
static int  g_state = 0;		// 0 wait-net, 1 connecting-pending, 2 connected, 3 dead

// Input line being typed.
static char g_input[480];
static int  g_inlen = 0;

// Incoming byte stream -> line assembly.
static char g_rx[1024];
static int  g_rxlen = 0;

// ---- tiny string helpers (freestanding: no libc) ----------------------------

static int  slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scat (char *dst, unsigned cap, const char *src)
{
	int d = slen (dst), i = 0;
	while (src[i] && d + 1 < (int) cap) dst[d++] = src[i++];
	dst[d] = '\0';
}
static void scpy (char *dst, unsigned cap, const char *src) { dst[0] = '\0'; scat (dst, cap, src); }

// ---- scrollback --------------------------------------------------------------

static void ring_push (const char *s)
{
	int idx;
	if (g_rcount < SCROLLBACK) { idx = (g_rfirst + g_rcount) % SCROLLBACK; g_rcount++; }
	else { idx = g_rfirst; g_rfirst = (g_rfirst + 1) % SCROLLBACK; }
	int i = 0;
	for (; s[i] && i < COLS; i++) g_ring[idx][i] = s[i];
	g_ring[idx][i] = '\0';
}
static const char *ring_line (int i) { return g_ring[(g_rfirst + i) % SCROLLBACK]; }
static void show (const char *s) { ring_push (s); g_scroll = 0; }

// ---- socket I/O --------------------------------------------------------------

// Send all of buf[len]: kapi_tcp_send may do a partial send, so loop until it's all
// out (or the socket errors/closes).
static void send_all (const char *buf, int len)
{
	if (g_sock < 0) return;
	int off = 0;
	while (off < len)
	{
		int n = kapi_tcp_send (g_sock, buf + off, (unsigned) (len - off));
		if (n <= 0) break;			// closed / error
		off += n;
	}
}

// Send one IRC line: the text + CRLF terminator in a SINGLE send. Sending the "\r\n"
// as a separate call let it get split off / delayed (Nagle), so the server saw an
// unterminated line and silently dropped the message -- the bug behind "others don't
// receive my messages even though my local echo shows them".
static void send_line (const char *s)
{
	char buf[700];
	int n = 0;
	for (int i = 0; s[i] != '\0' && n < (int) sizeof buf - 2; i++) buf[n++] = s[i];
	buf[n++] = '\r';
	buf[n++] = '\n';
	send_all (buf, n);
}

// Find an IRC "trailing" parameter (the text after the first " :"), or 0.
static char *trailing (char *s)
{
	for (int i = 0; s[i]; i++)
		if (s[i] == ' ' && s[i + 1] == ':') return s + i + 2;
	return 0;
}

// Parse + act on one complete server line (CRLF already stripped).
static void handle_line (char *line)
{
	if (line[0] == '\0') return;

	// PING :token  ->  PONG :token  (keep-alive; must answer or we get dropped)
	if (line[0] == 'P' && line[1] == 'I' && line[2] == 'N' && line[3] == 'G'
	    && (line[4] == ' ' || line[4] == '\0'))
	{
		char buf[600]; scpy (buf, sizeof buf, "PONG"); scat (buf, sizeof buf, line + 4);
		send_line (buf);
		return;
	}

	char *p = line;
	char nick[64]; nick[0] = '\0';

	if (*p == ':')					// optional :prefix
	{
		p++;
		char *pre = p;
		while (*p && *p != ' ') p++;
		char saved = *p; *p = '\0';
		int i = 0; while (pre[i] && pre[i] != '!' && i < 63) { nick[i] = pre[i]; i++; }
		nick[i] = '\0';
		if (saved) p++;
	}

	char *cmd = p;					// command token
	while (*p && *p != ' ') p++;
	char savedc = *p; if (*p) *p = '\0';
	char *rest = savedc ? p + 1 : p;

	if (ax_streq (cmd, "PRIVMSG"))
	{
		char *target = rest; while (*rest && *rest != ' ') rest++;
		if (*rest) { *rest = '\0'; rest++; }
		char *text = rest; if (*text == ':') text++;
		char out[COLS + 1]; out[0] = '\0';
		// CTCP ACTION ("\x01ACTION ...\x01") -> "* nick ..."
		if (text[0] == '\x01')
		{
			char *a = text + 1;
			if (a[0]=='A'&&a[1]=='C'&&a[2]=='T'&&a[3]=='I'&&a[4]=='O'&&a[5]=='N') a += 6;
			if (*a == ' ') a++;
			for (int k = 0; a[k]; k++) if (a[k] == '\x01') { a[k] = '\0'; break; }
			scat (out, sizeof out, "* "); scat (out, sizeof out, nick);
			scat (out, sizeof out, " "); scat (out, sizeof out, a);
		}
		else
		{
			scat (out, sizeof out, "<"); scat (out, sizeof out, nick[0] ? nick : target);
			scat (out, sizeof out, "> "); scat (out, sizeof out, text);
		}
		show (out);
		(void) target;
	}
	else if (ax_streq (cmd, "NOTICE"))
	{
		char *target = rest; while (*rest && *rest != ' ') rest++;
		if (*rest) { *rest = '\0'; rest++; }
		char *text = rest; if (*text == ':') text++;
		char out[COLS + 1]; out[0] = '\0';
		scat (out, sizeof out, "-"); scat (out, sizeof out, nick[0] ? nick : "server");
		scat (out, sizeof out, "- "); scat (out, sizeof out, text);
		show (out); (void) target;
	}
	else if (cmd[0] == '0' && cmd[1] == '0' && cmd[2] == '1')	// 001 welcome
	{
		g_state = 2;
		show ("* registered");
		char j[96]; scpy (j, sizeof j, "JOIN "); scat (j, sizeof j, g_channel);
		send_line (j);
	}
	else if (ax_streq (cmd, "JOIN"))
	{
		char *ch = (rest[0] == ':') ? rest + 1 : rest;
		char out[COLS + 1]; out[0] = '\0';
		scat (out, sizeof out, "* "); scat (out, sizeof out, nick[0] ? nick : "you");
		scat (out, sizeof out, " joined "); scat (out, sizeof out, ch);
		show (out);
	}
	else if (ax_streq (cmd, "PART") || ax_streq (cmd, "QUIT"))
	{
		char out[COLS + 1]; out[0] = '\0';
		scat (out, sizeof out, "* "); scat (out, sizeof out, nick[0] ? nick : "?");
		scat (out, sizeof out, ax_streq (cmd, "PART") ? " left" : " quit");
		show (out);
	}
	else						// numerics / MOTD / everything else
	{
		char *tr = trailing (rest);
		show (tr ? tr : (rest[0] ? rest : cmd));
	}
}

static void drain_socket (void)
{
	if (g_sock < 0) return;
	// >= FRAME_BUFFER_SIZE (1600): CSocket::Receive drops the tail of a segment that
	// does not fit, so a smaller buffer would lose data when lines coalesce in one TCP
	// segment.
	char b[1600]; int n, guard = 0;
	while ((n = kapi_tcp_recv (g_sock, b, sizeof b)) > 0 && guard++ < 16)
	{
		for (int k = 0; k < n; k++)
		{
			char c = b[k];
			if (c == '\r') continue;
			if (c == '\n') { g_rx[g_rxlen] = '\0'; handle_line (g_rx); g_rxlen = 0; }
			else if (g_rxlen < (int) sizeof g_rx - 1) g_rx[g_rxlen++] = c;
		}
		g_scroll = 0;
	}
	if (n < 0)					// peer closed / error
	{
		show ("* disconnected");
		kapi_tcp_close (g_sock);
		g_sock = -1;
		g_state = 3;
	}
}

// ---- commands + input --------------------------------------------------------

// Send a typed line. '/'-prefixed lines are commands; otherwise it's a message to
// the current channel (echoed locally, since the server doesn't echo our PRIVMSGs).
static void submit (char *s)
{
	if (s[0] == '\0') return;

	if (s[0] != '/')				// plain message
	{
		char msg[640]; scpy (msg, sizeof msg, "PRIVMSG "); scat (msg, sizeof msg, g_channel);
		scat (msg, sizeof msg, " :"); scat (msg, sizeof msg, s);
		send_line (msg);
		char out[COLS + 1]; out[0] = '\0';
		scat (out, sizeof out, "<"); scat (out, sizeof out, g_nick);
		scat (out, sizeof out, "> "); scat (out, sizeof out, s);
		show (out);
		return;
	}

	// Split "/cmd arg-rest".
	char *cmd = s + 1;
	char *arg = cmd; while (*arg && *arg != ' ') arg++;
	if (*arg) { *arg = '\0'; arg++; }
	while (*arg == ' ') arg++;

	if (ax_streq (cmd, "join") && arg[0])
	{
		scpy (g_channel, sizeof g_channel, arg);
		char j[96]; scpy (j, sizeof j, "JOIN "); scat (j, sizeof j, g_channel); send_line (j);
	}
	else if (ax_streq (cmd, "part"))
	{
		char j[96]; scpy (j, sizeof j, "PART ");
		scat (j, sizeof j, arg[0] ? arg : g_channel); send_line (j);
	}
	else if (ax_streq (cmd, "nick") && arg[0])
	{
		scpy (g_nick, sizeof g_nick, arg);
		char j[64]; scpy (j, sizeof j, "NICK "); scat (j, sizeof j, arg); send_line (j);
	}
	else if (ax_streq (cmd, "msg"))
	{
		char *target = arg; while (*arg && *arg != ' ') arg++;
		if (*arg) { *arg = '\0'; arg++; }
		char j[640]; scpy (j, sizeof j, "PRIVMSG "); scat (j, sizeof j, target);
		scat (j, sizeof j, " :"); scat (j, sizeof j, arg); send_line (j);
		char out[COLS + 1]; out[0] = '\0';
		scat (out, sizeof out, ">"); scat (out, sizeof out, target);
		scat (out, sizeof out, "< "); scat (out, sizeof out, arg);
		show (out);
	}
	else if (ax_streq (cmd, "me") && arg[0])
	{
		char j[640]; scpy (j, sizeof j, "PRIVMSG "); scat (j, sizeof j, g_channel);
		scat (j, sizeof j, " :\x01""ACTION "); scat (j, sizeof j, arg); scat (j, sizeof j, "\x01");
		send_line (j);
		char out[COLS + 1]; out[0] = '\0';
		scat (out, sizeof out, "* "); scat (out, sizeof out, g_nick);
		scat (out, sizeof out, " "); scat (out, sizeof out, arg); show (out);
	}
	else if (ax_streq (cmd, "quit"))
	{
		char j[160]; scpy (j, sizeof j, "QUIT :");
		scat (j, sizeof j, arg[0] ? arg : "Onyx IRC"); send_line (j);
		kapi_exit (0);
	}
	else if (ax_streq (cmd, "raw") && arg[0])
	{
		send_line (arg);
	}
	else						// /<anything else> -> raw IRC verb
	{
		char j[640]; scpy (j, sizeof j, cmd);
		if (arg[0]) { scat (j, sizeof j, " "); scat (j, sizeof j, arg); }
		send_line (j);
	}
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	switch (key)
	{
	case KEY_ENTER:
		submit (g_input);
		g_inlen = 0; g_input[0] = '\0';
		break;
	case KEY_BACKSPACE:
		if (g_inlen > 0) { g_inlen--; g_input[g_inlen] = '\0'; }
		break;
	case KEY_PGUP: g_scroll += g_chatrows - 1; break;
	case KEY_PGDN: g_scroll -= g_chatrows - 1; if (g_scroll < 0) g_scroll = 0; break;
	default:
		if (key >= ' ' && key < 0x7f && g_inlen < (int) sizeof g_input - 1)
		{
			g_input[g_inlen++] = (char) key; g_input[g_inlen] = '\0';
		}
		break;
	}
}

// ---- drawing -----------------------------------------------------------------

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00101418);

	int total = g_rcount;
	int maxscroll = total - g_chatrows; if (maxscroll < 0) maxscroll = 0;
	if (g_scroll > maxscroll) g_scroll = maxscroll;
	int first = total - g_chatrows - g_scroll; if (first < 0) first = 0;

	for (int r = 0; r < g_chatrows; r++)
	{
		int idx = first + r;
		if (idx < 0 || idx >= total) continue;
		const char *line = ring_line (idx);
		unsigned col = 0x00c8d0c0;
		if (line[0] == '*') col = 0x0080b0ff;		// status lines in blue
		else if (line[0] == '-') col = 0x00ffc060;	// notices in amber
		kapi_draw_text (4, 4 + r * g_fh, line, col);
	}

	// Input row: separator + "[channel] " prompt + the line being typed + caret.
	int iy = 4 + g_chatrows * g_fh + 2;
	fill_rect (0, iy - 2, W, 1, 0x00303840);
	char prompt[80]; prompt[0] = '\0';
	scat (prompt, sizeof prompt, "["); scat (prompt, sizeof prompt, g_channel);
	scat (prompt, sizeof prompt, "] ");
	kapi_draw_text (4, iy, prompt, 0x0060ff90);
	int px = 4 + slen (prompt) * g_fw;
	kapi_draw_text (px, iy, g_input, 0x00ffffff);
	int cx = px + g_inlen * g_fw;
	fill_rect (cx, iy, 2, g_fh, 0x0060ff90);
}

// ---- main --------------------------------------------------------------------

int main (void)
{
	// Config (SD:apps/irc.app/config.ini, section [irc]); fall back to the defaults.
	if (app_ini_load ("config.ini") >= 0)
	{
		scpy (g_server,  sizeof g_server,  app_ini_get ("irc", "server",  g_server));
		scpy (g_nick,    sizeof g_nick,    app_ini_get ("irc", "nick",    g_nick));
		scpy (g_user,    sizeof g_user,    app_ini_get ("irc", "user",    g_user));
		scpy (g_real,    sizeof g_real,    app_ini_get ("irc", "realname",g_real));
		scpy (g_channel, sizeof g_channel, app_ini_get ("irc", "channel", g_channel));
		g_port = (unsigned) app_ini_get_int ("irc", "port", (int) g_port);
	}

	fb = kapi_create_window (W, H, "irc");
	if (fb == 0) return 1;
	ui::decorate_window ();
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;
	g_vrows = (H - 8) / g_fh; if (g_vrows < 3) g_vrows = 3;
	g_chatrows = g_vrows - 2;			// reserve a row for input + a gap

	kapi_set_key_handler (on_key);

	show ("Onyx IRC -- waiting for network ...");
	int announced_connecting = 0;

	while (!should_exit ())
	{
		drain_socket ();
		pump_events ();

		if (g_state == 0)			// wait for the link, then connect once
		{
			char ip[32];
			if (kapi_net_status (ip, sizeof ip))
			{
				if (!announced_connecting)
				{
					char m[COLS + 1]; m[0] = '\0';
					scat (m, sizeof m, "* network up (");
					scat (m, sizeof m, ip); scat (m, sizeof m, ") -- connecting to ");
					scat (m, sizeof m, g_server); show (m);
					announced_connecting = 1;
					redraw (); present ();		// paint before the blocking connect
				}
				g_sock = kapi_tcp_connect (g_server, g_port);
				if (g_sock >= 0)
				{
					char l[160];
					scpy (l, sizeof l, "NICK "); scat (l, sizeof l, g_nick); send_line (l);
					scpy (l, sizeof l, "USER "); scat (l, sizeof l, g_user);
					scat (l, sizeof l, " 0 * :"); scat (l, sizeof l, g_real); send_line (l);
					show ("* registering ...");
					g_state = 1;
				}
				else
				{
					show ("* connect failed -- /raw or restart to retry");
					g_state = 3;
				}
			}
		}

		redraw ();
		msleep (16);
	}

	if (g_sock >= 0) { send_line ("QUIT :Onyx IRC"); kapi_tcp_close (g_sock); }
	return 0;
}
