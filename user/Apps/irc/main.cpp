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

// Connect bar: an editable "host[:port]" address + a [Connect] button + a clickable
// history of servers you've joined (persisted to SD:apps/irc.app/servers.txt).
static char g_addr[96] = "";
static int  g_addrlen = 0;
static bool g_addrfocus = false;	// true => typed keys edit the address, not the chat
#define HISTMAX 8
static char g_hist[HISTMAX][96];	// most-recent first
static int  g_histn = 0;
static int  g_chat_y = 4;		// top of the chat area (below the 2-row connect bar)
// Hit rects (filled by redraw, read by the pointer handler).
static int  g_barY0, g_barY1, g_addrX0, g_addrX1, g_btnX0, g_btnX1;
static int  g_recY0, g_recY1, g_recX0[HISTMAX], g_recX1[HISTMAX];

static void redraw (void);		// forward decls (definitions interleaved below)
static void connect_now (void);
static void set_server_from (const char *spec);

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

// ---- server history + (re)connect --------------------------------------------

#define HISTPATH "SD:apps/irc.app/servers.txt"

static void hist_save (void)
{
	char buf[HISTMAX * 98]; int n = 0;
	for (int i = 0; i < g_histn; i++)
	{
		for (int k = 0; g_hist[i][k] && n < (int) sizeof buf - 2; k++) buf[n++] = g_hist[i][k];
		buf[n++] = '\n';
	}
	kapi_save_file (HISTPATH, buf, (unsigned) n);
}

static void hist_load (void)
{
	void *f = kapi_open (HISTPATH);
	if (f == 0) return;
	static char buf[HISTMAX * 98];
	int got = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (got < 0) got = 0;
	buf[got] = '\0';
	int i = 0;
	while (buf[i] && g_histn < HISTMAX)
	{
		char line[96]; int j = 0;
		while (buf[i] && buf[i] != '\n' && buf[i] != '\r' && j < 95) line[j++] = buf[i++];
		line[j] = '\0';
		while (buf[i] == '\n' || buf[i] == '\r') i++;
		if (j > 0) { scpy (g_hist[g_histn], sizeof g_hist[0], line); g_histn++; }
	}
}

// Push "host[:port]" to the front of the history (dedup), cap HISTMAX, persist.
static void hist_add (const char *host, unsigned port)
{
	char entry[96]; int n = 0;
	for (int k = 0; host[k] && n < 80; k++) entry[n++] = host[k];
	if (port != 6667) { entry[n++] = ':'; n += ax_itoa ((int) port, entry + n); }
	entry[n] = '\0';

	int found = -1;
	for (int i = 0; i < g_histn; i++) if (ax_streq (g_hist[i], entry)) { found = i; break; }
	int start = (found >= 0) ? found : (g_histn < HISTMAX ? g_histn : HISTMAX - 1);
	for (int i = start; i > 0; i--) scpy (g_hist[i], sizeof g_hist[0], g_hist[i - 1]);
	scpy (g_hist[0], sizeof g_hist[0], entry);
	if (found < 0 && g_histn < HISTMAX) g_histn++;
	hist_save ();
}

// (Re)connect to g_server:g_port -- closes any current link, then NICK/USER register.
static void connect_now (void)
{
	if (g_sock >= 0) { send_line ("QUIT :reconnecting"); kapi_tcp_close (g_sock); g_sock = -1; }
	char m[COLS + 1]; m[0] = '\0';
	scat (m, sizeof m, "* connecting to "); scat (m, sizeof m, g_server); show (m);
	redraw (); present ();				// paint before the blocking connect
	g_sock = kapi_tcp_connect (g_server, g_port);
	if (g_sock >= 0)
	{
		char l[160];
		scpy (l, sizeof l, "NICK "); scat (l, sizeof l, g_nick); send_line (l);
		scpy (l, sizeof l, "USER "); scat (l, sizeof l, g_user);
		scat (l, sizeof l, " 0 * :"); scat (l, sizeof l, g_real); send_line (l);
		show ("* registering ...");
		g_state = 1;
		hist_add (g_server, g_port);
	}
	else { show ("* connect failed"); g_state = 3; }
}

// Parse "host[:port]" then (re)connect. Used by the Connect button, a history click,
// and the /server command.
static void set_server_from (const char *spec)
{
	while (*spec == ' ') spec++;
	if (*spec == '\0') return;
	char host[80]; int n = 0; unsigned port = 6667;
	while (*spec && *spec != ':' && *spec != ' ' && n < 79) host[n++] = *spec++;
	host[n] = '\0';
	if (*spec == ':')
	{
		spec++; port = 0;
		while (*spec >= '0' && *spec <= '9') port = port * 10 + (unsigned) (*spec++ - '0');
		if (port == 0) port = 6667;
	}
	scpy (g_server, sizeof g_server, host);
	g_port = port;
	connect_now ();
}

static void connect_from_addr (void)		// the Connect button / Enter in the address bar
{
	g_addrfocus = false;
	if (g_addr[0] != '\0') set_server_from (g_addr);
}

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
	else if ((ax_streq (cmd, "server") || ax_streq (cmd, "connect")) && arg[0])
	{
		set_server_from (arg);			// /server host[:port] -- reconnect elsewhere
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

	if (g_addrfocus)				// editing the connect-bar address field
	{
		if (key == KEY_ENTER)            connect_from_addr ();
		else if (key == 27)              g_addrfocus = false;	// Esc -> back to chat
		else if (key == KEY_BACKSPACE)   { if (g_addrlen > 0) g_addr[--g_addrlen] = '\0'; }
		else if (key >= ' ' && key < 0x7f && g_addrlen < (int) sizeof g_addr - 1)
			{ g_addr[g_addrlen++] = (char) key; g_addr[g_addrlen] = '\0'; }
		return;
	}

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

static void frame_rect (int x, int y, int w, int h, unsigned c)
{
	fill_rect (x, y, w, 1, c); fill_rect (x, y + h - 1, w, 1, c);
	fill_rect (x, y, 1, h, c); fill_rect (x + w - 1, y, 1, h, c);
}

// The connect bar: "Server:" + an editable address field + a [Connect] button on the
// first row, and a clickable "Recent:" server history on the second. Hit rects are
// stashed in globals for the pointer handler.
static void draw_connect_bar (void)
{
	int by = 3;					// row 0 baseline
	g_barY0 = 0; g_barY1 = g_fh + 3;
	kapi_draw_text (4, by, "Server:", 0x0090a0b0);

	int ax = 4 + 8 * g_fw;				// address field box
	int aw = 30 * g_fw;
	g_addrX0 = ax; g_addrX1 = ax + aw;
	fill_rect (ax, by - 1, aw, g_fh + 2, g_addrfocus ? 0x00203040 : 0x00181e26);
	frame_rect (ax, by - 1, aw, g_fh + 2, g_addrfocus ? 0x0060ff90 : 0x00404a5a);
	kapi_draw_text (ax + 3, by, g_addr, 0x00ffffff);
	if (g_addrfocus) fill_rect (ax + 3 + g_addrlen * g_fw, by, 2, g_fh, 0x0060ff90);

	int bx = ax + aw + 8;				// [Connect] button
	int bw = 9 * g_fw + 6;
	g_btnX0 = bx; g_btnX1 = bx + bw;
	fill_rect (bx, by - 1, bw, g_fh + 2, 0x00355070);
	frame_rect (bx, by - 1, bw, g_fh + 2, 0x0060a0e0);
	kapi_draw_text (bx + 3, by, "Connect", 0x00ffffff);

	int ry = by + g_fh + 4;				// row 1: recent-server history
	g_recY0 = ry - 1; g_recY1 = ry + g_fh;
	kapi_draw_text (4, ry, "Recent:", 0x0090a0b0);
	int rx = 4 + 8 * g_fw;
	for (int i = 0; i < g_histn && i < HISTMAX; i++)
	{
		int w = slen (g_hist[i]) * g_fw;
		if (rx + w > W - 4) break;
		g_recX0[i] = rx; g_recX1[i] = rx + w;
		kapi_draw_text (rx, ry, g_hist[i], 0x0080c8ff);
		rx += w + 2 * g_fw;
	}
	fill_rect (0, ry + g_fh + 2, W, 1, 0x00303840);	// separator under the bar
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00101418);
	draw_connect_bar ();

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
		kapi_draw_text (4, g_chat_y + r * g_fh, line, col);
	}

	// Input row: separator + "[channel] " prompt + the line being typed + caret.
	int iy = g_chat_y + g_chatrows * g_fh + 2;
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

// Pointer: click the address field to edit it, the Connect button or a Recent server
// to (re)connect; a click elsewhere returns typing focus to the chat input.
static void on_ptr (unsigned long s, int ev, long v)
{
	(void) s;
	if (ev != GUI_EVENT_PTR_DOWN || !(GUI_PTR_CHANGED (v) & 1)) return;
	int x = GUI_PTR_X (v), y = GUI_PTR_Y (v);
	if (y >= g_barY0 && y <= g_barY1)
	{
		if (x >= g_addrX0 && x <= g_addrX1) { g_addrfocus = true;   return; }
		if (x >= g_btnX0  && x <= g_btnX1)  { connect_from_addr (); return; }
	}
	if (y >= g_recY0 && y <= g_recY1)
		for (int i = 0; i < g_histn; i++)
			if (x >= g_recX0[i] && x <= g_recX1[i])
			{
				scpy (g_addr, sizeof g_addr, g_hist[i]);
				g_addrlen = slen (g_addr);
				connect_from_addr ();
				return;
			}
	g_addrfocus = false;
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

	// Seed the connect-bar address from the configured server; load server history.
	scpy (g_addr, sizeof g_addr, g_server);
	if (g_port != 6667)
	{
		int n = slen (g_addr); g_addr[n++] = ':';
		n += ax_itoa ((int) g_port, g_addr + n); g_addr[n] = '\0';
	}
	g_addrlen = slen (g_addr);
	hist_load ();

	fb = kapi_create_window (W, H, "irc");
	if (fb == 0) return 1;
	ui::decorate_window ();
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;
	g_vrows = (H - 8) / g_fh; if (g_vrows < 3) g_vrows = 3;
	g_chat_y = 4 + 2 * g_fh + 6;			// below the 2-row connect bar + separator
	g_chatrows = g_vrows - 4; if (g_chatrows < 1) g_chatrows = 1;	// bar + input reserved

	kapi_set_key_handler (on_key);
	kapi_set_pointer_handler (on_ptr);

	show ("Onyx IRC -- waiting for network ...");

	while (!should_exit ())
	{
		drain_socket ();
		pump_events ();

		if (g_state == 0)			// wait for the link, then connect once
		{
			char ip[32];
			if (kapi_net_status (ip, sizeof ip))
			{
				char m[COLS + 1]; m[0] = '\0';
				scat (m, sizeof m, "* network up ("); scat (m, sizeof m, ip);
				scat (m, sizeof m, ")"); show (m);
				connect_now ();			// connect to g_server:g_port (sets g_state)
			}
		}

		redraw ();
		msleep (16);
	}

	if (g_sock >= 0) { send_line ("QUIT :Onyx IRC"); kapi_tcp_close (g_sock); }
	return 0;
}
