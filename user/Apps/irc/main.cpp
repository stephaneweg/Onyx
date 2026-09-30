//
// irc -- the Onyx IRC client (wtk), over the WLAN TCP sockets (kapi_tcp_*).
//
// The main window, a messaging app's: on top the server (a combo box of the servers used before),
// your nickname and Connect; on the left the conversations (the server, the channels joined, the
// private conversations, an unread count beside each); in the middle the channel -- its name and
// topic, the messages grouped by author under a coloured avatar, the line you type --; on the
// right its users (double-click one, or Message, to talk to them privately). Rooms (the toolbar)
// lists the server's channels (LIST): search, a minimum of users, sort by a column, double-click
// one (or Join) to join it.
//
// A private conversation opens in a window of its own, a messenger's (bubbles: yours on the
// right in the accent, theirs on the left): the same program started as "irc --pm <nick>" (one
// window a process). The main window keeps the connection: the conversation window talks to it
// through the kernel mailboxes (the "irc" IPC service) -- it says hello, gets the conversation so
// far and every new line, and sends what you type. A message from someone opens their window.
//
// On connecting, NICK and USER go at once (the nickname: the field's, kept in nick.txt); a
// nickname already in use (433) is retried with a '_' appended. config.ini ([irc]): server, port,
// nick, user, realname, channel (joined on connecting, a comma between several), password
// (NickServ IDENTIFY after the welcome). Commands: /join #chan, /part, /nick name, /msg nick text,
// /query nick, /me action, /topic text, /list, /server host[:port], /raw line, /quit; anything
// else goes to the server as it is. Plain-text IRC (port 6667): no TLS here. Text is UTF-8 on the
// wire, Latin-1 on the screen (the font's).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "wtk/toolbar.h"
#include "applib.h"

using namespace wtk;

#define W		880
#define H		580
#define TB_H		40		// the toolbar
#define SB_H		24		// the status bar
#define SIDE_W		200		// the conversations
#define USERS_W		170		// a channel's users
#define HEAD_H		46		// a channel's header (its name, its topic)
#define IN_H		46		// the line you type
#define MAXLINES	240		// a conversation's lines kept
#define MAXBUF		40		// conversations
#define MAXROOMS	15000		// the LIST kept
#define PM_EXE		"SD:/apps/irc.app/main"
#define HISTPATH	"SD:apps/irc.app/servers.txt"
#define NICKPATH	"SD:apps/irc.app/nick.txt"

// The mailbox messages between the main window and a conversation window (text, tab-separated).
enum { T_HELLO = 7201,		// window -> main: "peer"
       T_LINE,			// main -> window: "kind \t HH:MM \t nick \t text" (kind: m / M mine, a / A, n, e)
       T_SAY,			// window -> main: the text typed
       T_STATE,			// main -> window: "online \t my nick \t server \t peer" (online: 1 / 0)
       T_BYE,			// window -> main: closed
       T_CLOSE };		// main -> window: the main window ends

// ---- tiny string helpers (freestanding: no libc) -------------------------------------------------

static int  slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scat (char *dst, int cap, const char *src)
{
	int d = slen (dst), i = 0;
	while (src[i] && d + 1 < cap) dst[d++] = src[i++];
	dst[d] = '\0';
}
static void scpy (char *dst, int cap, const char *src) { dst[0] = '\0'; scat (dst, cap, src); }
static void scatn (char *dst, int cap, int v) { char b[16]; b[ax_itoa (v, b)] = '\0'; scat (dst, cap, b); }
static char lc (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
static bool ieq (const char *a, const char *b)		// ASCII case-insensitive (nicks, channels)
{
	while (*a && lc (*a) == lc (*b)) { a++; b++; }
	return *a == '\0' && *b == '\0';
}
static bool icontains (const char *s, const char *needle)
{
	if (!needle[0]) return true;
	for (; *s; s++)
	{
		int k = 0;
		while (needle[k] && lc (s[k]) == lc (needle[k])) k++;
		if (!needle[k]) return true;
	}
	return false;
}
static bool is_chan (const char *s) { return s[0] == '#' || s[0] == '&' || s[0] == '+' || s[0] == '!'; }
static bool is_word_char (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '[' || c == ']' || c == '\\' || c == '`' || c == '^' || c == '{' || c == '}' || c == '|'; }
static bool mentions (const char *text, const char *nick)	// nick as a word in text
{
	int n = slen (nick);
	if (n == 0) return false;
	for (int i = 0; text[i]; i++)
	{
		int k = 0;
		while (k < n && lc (text[i + k]) == lc (nick[k])) k++;
		if (k == n && (i == 0 || !is_word_char (text[i - 1])) && !is_word_char (text[i + n])) return true;
	}
	return false;
}

// The wire's UTF-8 -> the screen's Latin-1 (a character past U+00FF: '?'; a byte that is no
// UTF-8: kept, the old Latin-1 clients'); mIRC's formatting codes and the tabs dropped.
static void from_wire (const char *in, char *out, int cap)
{
	int o = 0;
	const unsigned char *s = (const unsigned char *) in;
	while (*s && o + 1 < cap)
	{
		unsigned c = *s;
		if (c == 3)					// ^C colour: digits[,digits]
		{
			s++;
			for (int k = 0; k < 2 && *s >= '0' && *s <= '9'; k++) s++;
			if (*s == ',' && s[1] >= '0' && s[1] <= '9') { s++; for (int k = 0; k < 2 && *s >= '0' && *s <= '9'; k++) s++; }
			continue;
		}
		if (c == 2 || c == 0x0F || c == 0x16 || c == 0x1D || c == 0x1F || c == 0x1E || c == 0x11) { s++; continue; }
		if (c == '\t') { out[o++] = ' '; s++; continue; }
		if (c < 0x80) { out[o++] = (char) c; s++; continue; }
		int n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		bool ok = n > 0;
		for (int k = 1; ok && k <= n; k++) if ((s[k] & 0xC0) != 0x80) ok = false;
		if (!ok) { out[o++] = (char) c; s++; continue; }
		unsigned cp = c & (n == 1 ? 0x1F : n == 2 ? 0x0F : 0x07);
		for (int k = 1; k <= n; k++) cp = (cp << 6) | (s[k] & 0x3F);
		out[o++] = cp <= 0xFF ? (char) cp : '?';
		s += n + 1;
	}
	out[o] = '\0';
}
static void to_wire (const char *in, char *out, int cap)	// the screen's Latin-1 -> UTF-8
{
	int o = 0;
	for (const unsigned char *s = (const unsigned char *) in; *s && o + 2 < cap; s++)
		if (*s < 0x80) out[o++] = (char) *s;
		else { out[o++] = (char) (0xC0 | (*s >> 6)); out[o++] = (char) (0x80 | (*s & 0x3F)); }
	out[o] = '\0';
}

static int now_mins (void)
{
	int h = 0, mi = 0;
	kapi_get_datetime (0, 0, 0, &h, &mi, 0);
	return h * 60 + mi;
}
static void fmt_time (int mins, char *o)			// "14:02"
{
	o[0] = (char) ('0' + mins / 600); o[1] = (char) ('0' + mins / 60 % 10); o[2] = ':';
	o[3] = (char) ('0' + mins % 60 / 10); o[4] = (char) ('0' + mins % 10); o[5] = '\0';
}

// ---- the look shared by both windows ---------------------------------------------------------------

static int g_cw = 8, g_fh = 16;					// a character's width, a line's height
#define RH	(g_fh + 3)					// a text row

static unsigned ink_soft (void) { return wk_mix (C_FIELD_TEXT, C_FIELD, 140); }	// times, events
static unsigned line_col (void) { return wk_mix (C_FIELD, C_FIELD_TEXT, 36); }	// separators

// A nickname's colour (the same everywhere: its avatar, its name).
static unsigned nick_color (const char *nick)
{
	static const unsigned C[] = { 0x00C0392B, 0x00D35400, 0x0027864F, 0x00138D75, 0x002471A3,
				      0x007D3C98, 0x00C2185B, 0x00A04000, 0x00546E7A, 0x006A8E23 };
	unsigned h = 5381;
	for (int i = 0; nick[i]; i++) h = h * 33 + (unsigned char) lc (nick[i]);
	return C[h % (sizeof C / sizeof C[0])];
}

// A disc of colour c, anti-aliased (wk_rbox's corners stop at a radius of 16).
static int isqrt (int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
static void disc (Canvas &cv, int x, int y, int s, unsigned c)
{
	int r16 = s * 8;					// the radius, in 1/16 px
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			int dx = i * 16 + 8 - r16, dy = j * 16 + 8 - r16;
			int d = isqrt (dx * dx + dy * dy);		// (1/16 px)
			int a = (r16 - d) * 16 + 128;			// a pixel's width of blending at the edge
			if (a <= 0) continue;
			wk_blend_px (cv, x + i, y + j, c, a > 255 ? 255 : a);
		}
}

// A round avatar: the nickname's colour, its first letter in white.
static void avatar (Canvas &cv, int x, int y, int s, const char *nick)
{
	disc (cv, x, y, s, nick_color (nick));
	const char *p = nick; while (*p && !is_word_char (*p)) p++;
	char l[2] = { *p ? *p : '?', 0 };
	if (l[0] >= 'a' && l[0] <= 'z') l[0] = (char) (l[0] - 32);
	wk_text_c (cv, x, y, s, s, l, 0x00FFFFFF, 2);
}

// Text cut to w px ("..." at the end when it does not fit).
static void fit (const char *s, int w, char *out, int cap, int style = 0)
{
	scpy (out, cap, s);
	if (wk_text_w (out, style) <= w) return;
	int n = slen (out);
	while (n > 0)
	{
		out[--n] = '\0';
		char t[512]; scpy (t, sizeof t, out); scat (t, sizeof t, "...");
		if (wk_text_w (t, style) <= w) { scpy (out, cap, t); return; }
	}
}

// Word wrap: the next row of s (maxc characters at most): its length; *next: where the next starts.
static int wrap_row (const char *s, int maxc, const char **next)
{
	if (maxc < 4) maxc = 4;
	int n = 0; while (s[n] && n <= maxc) n++;
	if (n <= maxc) { *next = s + n; return n; }
	int cut = maxc; while (cut > 0 && s[cut] != ' ') cut--;
	if (cut < maxc / 3) cut = maxc;				// one long word: cut it
	const char *p = s + cut; while (*p == ' ') p++;
	*next = p;
	return cut;
}
static int wrap_rows (const char *s, int maxc, int *widest = 0)
{
	int rows = 0, wmax = 0;
	if (!*s) { if (widest) *widest = 0; return 1; }
	while (*s) { const char *nx; int n = wrap_row (s, maxc, &nx); if (n > wmax) wmax = n; rows++; s = nx; }
	if (widest) *widest = wmax;
	return rows;
}
static void draw_n (Canvas &cv, int x, int y, const char *s, int n, unsigned c, int style = 0)
{
	char t[512]; if (n > 511) n = 511;
	for (int i = 0; i < n; i++) t[i] = s[i];
	t[n] = '\0';
	wk_text_l (cv, x, y, RH, t, c, style);
}

// A pixel-scrolled view pinned to its bottom (the newest): scroll = px up from the bottom.
struct Scroller
{
	int scroll = 0, total = 0, view = 0; bool drag = false;
	void clamp () { int m = total - view; if (m < 0) m = 0; if (scroll > m) scroll = m; if (scroll < 0) scroll = 0; }
	void bar (Canvas &cv, int w, int h)
	{
		WkThumb t = wk_thumb (total, view, total - view - scroll, h - 4);
		if (t.show) wk_draw_vscroll (cv, w - WK_SBW - 2, 2, WK_SBW, h - 4, t, C_FIELD, drag);
	}
	// The pointer: the wheel scrolls; the bar's thumb drags. true: handled (redraw).
	bool mouse (Widget &wd, int mx, int my, int bl, int wheel)
	{
		if (wheel) { scroll += wheel * 3 * RH; clamp (); return true; }
		WkThumb t = wk_thumb (total, view, total - view - scroll, wd.height - 4);
		if (drag && !bl) { drag = false; return true; }
		if (bl && !drag && t.show && mx >= wd.width - WK_SBW - 4) drag = true;
		if (drag)
		{
			long pos = wk_thumb_pos (my - 2, wd.height - 4, total, view, t.h);
			scroll = (int) (total - view - pos); clamp ();
			return true;
		}
		return false;
	}
};

// A header on the white: a title in bold, a line below it in grey, a separator under both.
class HeaderBar : public Widget
{
public:
	char title[80], sub[400];
	HeaderBar (int l, int t, int w, int h) : Widget (l, t, w, h) { title[0] = sub[0] = '\0'; }
	unsigned bgColor () override { return C_FIELD; }
	void set (const char *t, const char *s)
	{ scpy (title, sizeof title, t); from_wire (s, sub, sizeof sub); invalidate (true); }
	int rightPad = 0;					// px kept for a child on the right (Leave)
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		char b[400]; int tw = width - 32 - rightPad;
		fit (title, tw, b, sizeof b, 2);
		wk_text_l (canvas, 16, 5, RH, b, C_FIELD_TEXT, 2);
		fit (sub, tw, b, sizeof b);
		wk_text_l (canvas, 16, 5 + RH + 1, RH, b, ink_soft ());
		canvas.fillRect (0, height - 1, width, 1, line_col ());
	}
};

// =====================================================================================================
// The main window: the connection, the channels
// =====================================================================================================

enum { LK_MSG, LK_ACTION, LK_NOTICE, LK_EVENT, LK_INFO, LK_ERROR };
struct Line { unsigned char kind, self, mention; short mins; char nick[32]; char text[420]; };
struct Nick { char pfx; char name[31]; };
enum { BK_SERVER, BK_CHAN, BK_QUERY };

struct Buf
{
	char  name[64]; int kind; bool joined, namesOpen, peerGone;
	Line *lines; int first, count;
	int   unread; bool mention;
	char  topic[420];
	Nick *nicks; int nnick, capn;
	int   scroll;
	int   pmPid; unsigned pmLaunch;			// a private conversation's window (0: none)
};
static Buf *g_buf[MAXBUF];
static int  g_nbuf = 0, g_cur = 0;

// Connection + config.
static char g_server[80]  = "irc.libera.chat";
static char g_nick[32]    = "onyx";			// the nickname in use (the server's word after 001)
static char g_user[32]    = "onyx";
static char g_real[64]    = "Onyx IRC user";
static char g_autojoin[128] = "#onyx";
static char g_password[64] = "";
static unsigned g_port    = 6667;
static int  g_sock = -1;
static int  g_state = 0;		// 0 waiting for the network, 1 registering, 2 registered, 3 offline
static char g_rx[2048];
static int  g_rxlen = 0;
static char g_hist[8][96];		// the servers used, the latest first
static int  g_histn = 0;

// Rooms (LIST).
struct Room { char name[48]; int users; char topic[112]; };
static Room *g_rooms = 0;
static int  *g_view = 0;
static int   g_nrooms = 0, g_nview = 0;
static bool  g_listing = false, g_showRooms = false;
static char  g_lastFilter[64] = "";
static int   g_lastMin = -1;

// The widgets.
class ChatView; class StatusBar;
static Combobox *g_srv; static Textbox *g_nickTb; static Button *g_connBtn; static ToolButton *g_roomsBtn;
static TreeView *g_tree; static HeaderBar *g_head; static Button *g_leave; static ChatView *g_chat;
static Label *g_usersLbl; static ListBox *g_users; static Button *g_pmBtn;
static Textbox *g_input; static Button *g_send;
static Panel *g_chatPane, *g_roomsPane, *g_inRow; static HeaderBar *g_roomsHead;
static DataGrid *g_grid; static Textbox *g_filter; static NumericUpDown *g_minU; static Label *g_roomsInfo;
static StatusBar *g_status;
static bool g_treeDirty = true, g_usersDirty = true, g_viewDirty = true;

static void connect_now (void);
static void send_line (const char *s);

// ---- conversations -----------------------------------------------------------------------------------

static int buf_find (const char *name)
{
	for (int i = 0; i < g_nbuf; i++) if (ieq (g_buf[i]->name, name)) return i;
	return -1;
}
static int buf_get (const char *name, int kind)
{
	int i = buf_find (name);
	if (i >= 0) return i;
	if (g_nbuf >= MAXBUF) return 0;				// (full: the server's)
	Buf *b = new Buf;
	scpy (b->name, sizeof b->name, name);
	b->kind = kind; b->joined = b->namesOpen = b->peerGone = false;
	b->lines = new Line[MAXLINES]; b->first = b->count = 0;
	b->unread = 0; b->mention = false; b->topic[0] = '\0';
	b->nicks = 0; b->nnick = b->capn = 0; b->scroll = 0;
	b->pmPid = 0; b->pmLaunch = 0;
	g_buf[g_nbuf++] = b;
	g_treeDirty = true;
	return g_nbuf - 1;
}
static void buf_remove (int i)
{
	if (i <= 0 || i >= g_nbuf) return;			// (never the server's)
	Buf *b = g_buf[i];
	delete [] b->lines; delete [] b->nicks; delete b;
	for (int k = i; k + 1 < g_nbuf; k++) g_buf[k] = g_buf[k + 1];
	g_nbuf--;
	if (g_cur >= i) g_cur = g_cur > 0 ? g_cur - 1 : 0;
	g_treeDirty = g_usersDirty = g_viewDirty = true;
}
static Line &buf_line (Buf *b, int i) { return b->lines[(b->first + i) % MAXLINES]; }

static void pm_send_line (Buf *b, const Line &l);

// A line into a conversation (the text already the screen's).
static void add_line (int bi, int kind, const char *nick, const char *text, bool self = false)
{
	if (bi < 0 || bi >= g_nbuf) bi = 0;
	Buf *b = g_buf[bi];
	int idx;
	if (b->count < MAXLINES) idx = (b->first + b->count++) % MAXLINES;
	else { idx = b->first; b->first = (b->first + 1) % MAXLINES; }
	Line &l = b->lines[idx];
	l.kind = (unsigned char) kind; l.self = self; l.mins = (short) now_mins ();
	scpy (l.nick, sizeof l.nick, nick ? nick : "");
	scpy (l.text, sizeof l.text, text);
	l.mention = !self && (kind == LK_MSG || kind == LK_ACTION) && b->kind == BK_CHAN && mentions (text, g_nick);
	if (bi != g_cur && (kind == LK_MSG || kind == LK_ACTION || (kind == LK_NOTICE && b->kind != BK_SERVER)))
	{
		b->unread++;
		if (l.mention) b->mention = true;
		g_treeDirty = true;
	}
	if (b->kind == BK_QUERY) pm_send_line (b, l);
	if (bi == g_cur) g_viewDirty = true;
}
static void info (const char *text, int kind = LK_INFO) { add_line (0, kind, "", text); }
static void info_here (const char *text, int kind = LK_ERROR) { add_line (g_cur, kind, "", text); }

// ---- a channel's users ---------------------------------------------------------------------------------

static int pfx_rank (char p) { return p == '~' ? 0 : p == '&' ? 1 : p == '@' ? 2 : p == '%' ? 3 : p == '+' ? 4 : 5; }
static int nick_index (Buf *b, const char *name)
{
	for (int i = 0; i < b->nnick; i++) if (ieq (b->nicks[i].name, name)) return i;
	return -1;
}
static void nick_add (Buf *b, const char *raw)
{
	char pfx = 0;
	while (*raw == '~' || *raw == '&' || *raw == '@' || *raw == '%' || *raw == '+')
	{ if (!pfx || pfx_rank (*raw) < pfx_rank (pfx)) pfx = *raw; raw++; }
	if (!*raw) return;
	int i = nick_index (b, raw);
	if (i >= 0) { if (pfx) b->nicks[i].pfx = pfx; return; }
	if (b->nnick == b->capn)
	{
		int nc = b->capn ? b->capn * 2 : 64;
		Nick *n = new Nick[nc];
		for (int k = 0; k < b->nnick; k++) n[k] = b->nicks[k];
		delete [] b->nicks; b->nicks = n; b->capn = nc;
	}
	Nick &n = b->nicks[b->nnick++];
	n.pfx = pfx; scpy (n.name, sizeof n.name, raw);
	if (g_buf[g_cur] == b) g_usersDirty = true;
}
static bool nick_del (Buf *b, const char *name)
{
	int i = nick_index (b, name);
	if (i < 0) return false;
	b->nicks[i] = b->nicks[--b->nnick];
	if (g_buf[g_cur] == b) g_usersDirty = true;
	return true;
}
static bool nick_less (const Nick &a, const Nick &b)
{
	int ra = pfx_rank (a.pfx), rb = pfx_rank (b.pfx);
	if (ra != rb) return ra < rb;
	const char *x = a.name, *y = b.name;
	while (*x && lc (*x) == lc (*y)) { x++; y++; }
	return lc (*x) < lc (*y);
}
static void nick_sort (Buf *b)				// (shell sort: a thousand users is quick)
{
	for (int gap = b->nnick / 2; gap > 0; gap /= 2)
		for (int i = gap; i < b->nnick; i++)
		{
			Nick t = b->nicks[i]; int j = i;
			while (j >= gap && nick_less (t, b->nicks[j - gap])) { b->nicks[j] = b->nicks[j - gap]; j -= gap; }
			b->nicks[j] = t;
		}
}

// ---- the channel view --------------------------------------------------------------------------------

#define AV	30		// an avatar
#define PADL	14		// the left margin
#define TX	(PADL + AV + 12)	// the messages' text

// The messages of the conversation shown, a messaging app's: a message from someone new (or after
// five minutes) opens a group -- the avatar, the name in its colour, the time -- the next ones from
// them follow under it; joins and parts in grey; a line naming you tinted; the server's lines plain.
class ChatView : public Widget
{
public:
	Scroller sc;
	ChatView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	unsigned bgColor () override { return C_FIELD; }

	static bool groups (const Line &l) { return l.kind == LK_MSG || l.kind == LK_NOTICE; }
	static bool opens (const Line &l, const Line *p)
	{
		if (!groups (l)) return false;
		return !(p && groups (*p) && p->kind == l.kind && ieq (p->nick, l.nick) && l.mins - p->mins < 5 && l.mins >= p->mins);
	}
	int textW () const { return width - TX - WK_SBW - 14; }
	int height_of (Buf *b, const Line &l, const Line *p)
	{
		if (b->kind == BK_SERVER) return wrap_rows (l.text, (width - PADL - 7 * g_cw - WK_SBW - 14) / g_cw) * RH;
		int rows = wrap_rows (l.text, textW () / g_cw - (l.kind == LK_ACTION ? slen (l.nick) + 3 : 0));
		int h = rows * RH;
		if (opens (l, p)) h += RH + 12;			// the gap, the name's row
		else if (!groups (l)) h += 4;
		if (l.mention) h += 2;
		return h;
	}
	void draw_line (Buf *b, const Line &l, const Line *p, int y, int h)
	{
		char t[8]; fmt_time (l.mins, t);
		if (b->kind == BK_SERVER)				// the server's: the time, the text
		{
			wk_text_l (canvas, PADL, y, RH, t, ink_soft ());
			unsigned c = l.kind == LK_ERROR ? 0x00C0392B : l.kind == LK_NOTICE ? 0x009A6400 : C_FIELD_TEXT;
			int x = PADL + 7 * g_cw, maxc = (width - x - WK_SBW - 14) / g_cw;
			for (const char *s = l.text; *s || s == l.text; )
			{
				const char *nx; int n = wrap_row (s, maxc, &nx);
				draw_n (canvas, x, y, s, n, c); y += RH;
				if (!*nx) break; s = nx;
			}
			return;
		}
		if (l.mention) wk_rbox (canvas, TX - 6, y + (opens (l, p) ? 12 : 0), width - TX - WK_SBW - 8, h - (opens (l, p) ? 12 : 0), 6,
					  wk_mix (C_FIELD, C_ACCENT, 40), wk_mix (C_FIELD, C_ACCENT, 40));
		if (opens (l, p))
		{
			y += 12;
			avatar (canvas, PADL, y + 2, AV, l.nick);
			char nm[40]; scpy (nm, sizeof nm, l.nick);
			if (l.kind == LK_NOTICE) scat (nm, sizeof nm, " (notice)");
			wk_text_l (canvas, TX, y, RH, nm, nick_color (l.nick), 2);
			wk_text_l (canvas, TX + wk_text_w (nm, 2) + 10, y, RH, t, ink_soft ());
			y += RH;
		}
		else if (!groups (l)) y += 2;
		unsigned c = l.kind == LK_NOTICE ? 0x009A6400 : l.kind == LK_EVENT ? ink_soft ()
			   : l.kind == LK_ERROR ? 0x00C0392B : l.kind == LK_ACTION ? nick_color (l.nick) : C_FIELD_TEXT;
		int x = TX, maxc = textW () / g_cw, style = l.kind == LK_ACTION ? 1 : 0;
		if (l.kind == LK_ACTION)
		{
			char a[40] = "* "; scat (a, sizeof a, l.nick); scat (a, sizeof a, " ");
			wk_text_l (canvas, x, y, RH, a, c, 3);
			x += slen (a) * g_cw; maxc -= slen (a);
		}
		for (const char *s = l.text; ; )
		{
			const char *nx; int n = wrap_row (s, maxc, &nx);
			draw_n (canvas, x, y, s, n, c, style); y += RH;
			if (!*nx) break; s = nx;
		}
	}
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		Buf *b = g_buf[g_cur];
		sc.view = height - 12;
		int n = b->count;
		static int hts[MAXLINES];
		int total = 0;
		for (int i = 0; i < n; i++)
		{
			const Line &l = buf_line (b, i);
			hts[i] = height_of (b, l, i > 0 ? &buf_line (b, i - 1) : 0);
			total += hts[i];
		}
		sc.total = total; sc.scroll = b->scroll; sc.clamp (); b->scroll = sc.scroll;
		if (n == 0)
		{
			wk_text_c (canvas, 0, height / 2 - RH, width, RH, b->kind == BK_SERVER ? "No server messages yet" : "No messages yet", ink_soft (), 2);
			wk_text_c (canvas, 0, height / 2, width, RH, b->kind == BK_SERVER ? "Connect to a server to start." : "Say hello to the channel.", ink_soft ());
			return;
		}
		int y = 6 + (sc.view > total ? sc.view - total : sc.view - total + sc.scroll);
		for (int i = 0; i < n; i++)
		{
			if (y + hts[i] >= 0 && y < height)
				draw_line (b, buf_line (b, i), i > 0 ? &buf_line (b, i - 1) : 0, y, hts[i]);
			y += hts[i];
		}
		sc.bar (canvas, width, height);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { sc.drag = false; return false; }
		sc.scroll = g_buf[g_cur]->scroll;
		if (sc.mouse (*this, mx, my, bl, wheel)) { g_buf[g_cur]->scroll = sc.scroll; invalidate (true); }
		return true;
	}
};

// The status bar: a light (green: connected, amber: on the way, grey: offline), what is going on;
// on the right the server and the channels.
class StatusBar : public Widget
{
public:
	StatusBar (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		wk_etch_h (canvas, 0, 0, width, C_BG);
		unsigned dot = g_state == 2 ? 0x0027AE60 : g_state == 1 ? 0x00E0A020 : 0x009A9A9A;
		wk_rbox (canvas, 10, (height - 8) / 2 + 1, 8, 8, 4, wk_tone (dot, 170), dot);
		char t[160] = "";
		if (g_state == 2) { scat (t, sizeof t, "Connected as "); scat (t, sizeof t, g_nick); }
		else if (g_state == 1) { scat (t, sizeof t, "Connecting to "); scat (t, sizeof t, g_server); scat (t, sizeof t, "..."); }
		else if (g_state == 0) scat (t, sizeof t, "Waiting for the network...");
		else scat (t, sizeof t, "Offline");
		wk_text_l (canvas, 24, 1, height - 1, t, C_TEXT);
		char r[160] = "";
		int nch = 0; for (int i = 0; i < g_nbuf; i++) if (g_buf[i]->kind == BK_CHAN && g_buf[i]->joined) nch++;
		if (g_sock >= 0) { scat (r, sizeof r, g_server); scat (r, sizeof r, ":"); scatn (r, sizeof r, (int) g_port); scat (r, sizeof r, "   "); }
		scatn (r, sizeof r, nch); scat (r, sizeof r, nch == 1 ? " channel" : " channels");
		wk_text_l (canvas, width - 10 - wk_text_w (r), 1, height - 1, r, wk_mix (C_TEXT, C_BG, 90));
	}
};

// ---- the private conversations' windows ------------------------------------------------------------

static void pm_payload (const Line &l, char *o, int cap)
{
	char k = l.kind == LK_ACTION ? 'a' : l.kind == LK_NOTICE ? 'n' : l.kind == LK_MSG ? 'm' : 'e';
	if (l.self && (k == 'm' || k == 'a')) k = (char) (k - 32);
	char t[8]; fmt_time (l.mins, t);
	o[0] = k; o[1] = '\0';
	scat (o, cap, "\t"); scat (o, cap, t); scat (o, cap, "\t"); scat (o, cap, l.nick);
	scat (o, cap, "\t"); scat (o, cap, l.text);
}
static void pm_send_line (Buf *b, const Line &l)
{
	if (b->pmPid <= 0) return;
	char m[512]; pm_payload (l, m, sizeof m);
	if (kapi_mailbox_send (b->pmPid, T_LINE, m, (unsigned) slen (m)) < 0) b->pmPid = 0;	// (gone)
}
static void pm_send_state (Buf *b)
{
	if (b->pmPid <= 0) return;
	char m[200] = "";
	scat (m, sizeof m, g_state == 2 && !b->peerGone ? "1" : "0"); scat (m, sizeof m, "\t");
	scat (m, sizeof m, g_nick); scat (m, sizeof m, "\t"); scat (m, sizeof m, g_server);
	scat (m, sizeof m, "\t"); scat (m, sizeof m, b->name);
	kapi_mailbox_send (b->pmPid, T_STATE, m, (unsigned) slen (m));
}
static void pm_state_all (void) { for (int i = 0; i < g_nbuf; i++) if (g_buf[i]->kind == BK_QUERY) pm_send_state (g_buf[i]); }

// A conversation with `nick` shown: its window, started if it is not there.
static void open_pm (const char *nick)
{
	while (*nick == '~' || *nick == '&' || *nick == '@' || *nick == '%' || *nick == '+') nick++;
	if (!*nick || ieq (nick, g_nick)) return;
	Buf *b = g_buf[buf_get (nick, BK_QUERY)];
	if (b->pmPid > 0) { pm_send_state (b); return; }
	unsigned now = kapi_get_ticks ();
	if (b->pmLaunch && now - b->pmLaunch < 500) return;	// (starting: its hello will come)
	b->pmLaunch = now;
	char args[64] = "--pm "; scat (args, sizeof args, b->name);
	kapi_exec_as (PM_EXE, args, "irc");
}

static void mailbox_pump (void)
{
	char m[520]; int from = 0, type = 0, n;
	while ((n = kapi_mailbox_recv (&from, &type, m, sizeof m - 1, 0)) >= 0)
	{
		m[n] = '\0';
		if (type == T_HELLO)
		{
			Buf *b = g_buf[buf_get (m, BK_QUERY)];
			b->pmPid = from; b->pmLaunch = 0;
			pm_send_state (b);
			for (int i = 0; i < b->count; i++) pm_send_line (b, buf_line (b, i));
			if (b->unread) { b->unread = 0; b->mention = false; g_treeDirty = true; }
		}
		else if (type == T_SAY || type == T_BYE)
		{
			int bi = -1;
			for (int i = 0; i < g_nbuf; i++) if (g_buf[i]->kind == BK_QUERY && g_buf[i]->pmPid == from) bi = i;
			if (bi < 0) continue;
			if (type == T_BYE) { g_buf[bi]->pmPid = 0; continue; }
			if (g_sock < 0 || g_state != 2) { add_line (bi, LK_ERROR, "", "Not connected: the message was not sent."); continue; }
			if (m[0] == '/' && m[1] == 'm' && m[2] == 'e' && m[3] == ' ')
			{
				char w[520]; to_wire (m + 4, w, sizeof w);
				char l[600] = "PRIVMSG "; scat (l, sizeof l, g_buf[bi]->name); scat (l, sizeof l, " :\x01" "ACTION ");
				scat (l, sizeof l, w); scat (l, sizeof l, "\x01"); send_line (l);
				add_line (bi, LK_ACTION, g_nick, m + 4, true);
			}
			else
			{
				char w[520]; to_wire (m, w, sizeof w);
				char l[600] = "PRIVMSG "; scat (l, sizeof l, g_buf[bi]->name); scat (l, sizeof l, " :"); scat (l, sizeof l, w);
				send_line (l);
				add_line (bi, LK_MSG, g_nick, m, true);
			}
		}
	}
}

// ---- socket I/O --------------------------------------------------------------------------------------

static void send_all (const char *buf, int len)
{
	if (g_sock < 0) return;
	int off = 0;
	while (off < len)
	{
		int n = kapi_tcp_send (g_sock, buf + off, (unsigned) (len - off));
		if (n <= 0) break;
		off += n;
	}
}
// One IRC line + CRLF in a SINGLE send (a "\r\n" sent apart got delayed: the server dropped the line).
static void send_line (const char *s)
{
	char buf[700]; int n = 0;
	for (int i = 0; s[i] != '\0' && n < (int) sizeof buf - 2; i++) buf[n++] = s[i];
	buf[n++] = '\r'; buf[n++] = '\n';
	send_all (buf, n);
}

// ---- the parser --------------------------------------------------------------------------------------

struct Msg { char nick[64]; char *cmd; char *p[16]; int np; };

static bool parse (char *line, Msg &m)
{
	m.nick[0] = '\0'; m.np = 0; m.cmd = 0;
	char *s = line;
	if (*s == '@') { while (*s && *s != ' ') s++; while (*s == ' ') s++; }	// (IRCv3 tags: skipped)
	if (*s == ':')
	{
		s++;
		int i = 0; while (*s && *s != ' ' && *s != '!' && i < 63) m.nick[i++] = *s++;
		m.nick[i] = '\0';
		while (*s && *s != ' ') s++;
		while (*s == ' ') s++;
	}
	if (!*s) return false;
	m.cmd = s;
	while (*s && *s != ' ') s++;
	if (*s) *s++ = '\0';
	while (*s && m.np < 16)
	{
		while (*s == ' ') s++;
		if (!*s) break;
		if (*s == ':') { m.p[m.np++] = s + 1; break; }
		m.p[m.np++] = s;
		while (*s && *s != ' ') s++;
		if (*s) *s++ = '\0';
	}
	return true;
}
static const char *arg (Msg &m, int i) { return i < m.np ? m.p[i] : ""; }
static bool from_server (Msg &m) { for (int i = 0; m.nick[i]; i++) if (m.nick[i] == '.') return true; return m.nick[0] == '\0'; }

// ---- rooms ---------------------------------------------------------------------------------------------

static bool room_less (int a, int b)
{
	const Room &x = g_rooms[a], &y = g_rooms[b];
	bool desc = g_grid->sortDesc;
	int c = 0;
	if (g_grid->sortCol == 1) c = x.users < y.users ? -1 : x.users > y.users ? 1 : 0;
	else
	{
		const char *p = g_grid->sortCol == 2 ? x.topic : x.name, *q = g_grid->sortCol == 2 ? y.topic : y.name;
		while (*p && lc (*p) == lc (*q)) { p++; q++; }
		c = lc (*p) < lc (*q) ? -1 : lc (*p) > lc (*q) ? 1 : 0;
	}
	return desc ? c > 0 : c < 0;
}
static void rooms_info (void)
{
	char t[160] = "";
	if (g_sock < 0 || g_state != 2) scpy (t, sizeof t, "Connect to a server to see its rooms.");
	else if (g_listing) { scat (t, sizeof t, "Receiving the list... "); scatn (t, sizeof t, g_nrooms); scat (t, sizeof t, " rooms so far"); }
	else
	{
		scatn (t, sizeof t, g_nview); scat (t, sizeof t, " of "); scatn (t, sizeof t, g_nrooms);
		scat (t, sizeof t, " rooms.  Double-click a room (or Join) to join it.");
	}
	g_roomsInfo->setText (t);
}
static void rooms_refresh (void)
{
	scpy (g_lastFilter, sizeof g_lastFilter, g_filter->text);
	g_lastMin = g_minU->value;
	g_nview = 0;
	for (int i = 0; i < g_nrooms; i++)
		if (g_rooms[i].users >= g_lastMin && (icontains (g_rooms[i].name, g_lastFilter) || icontains (g_rooms[i].topic, g_lastFilter)))
			g_view[g_nview++] = i;
	for (int gap = g_nview / 2; gap > 0; gap /= 2)
		for (int i = gap; i < g_nview; i++)
		{
			int t = g_view[i], j = i;
			while (j >= gap && room_less (t, g_view[j - gap])) { g_view[j] = g_view[j - gap]; j -= gap; }
			g_view[j] = t;
		}
	g_grid->setRows (g_nview);
	g_grid->invalidate (true);
	rooms_info ();
}
static void rooms_request (void)
{
	if (g_sock < 0 || g_state != 2) { rooms_info (); return; }
	g_nrooms = 0; g_listing = true;
	send_line ("LIST");
	rooms_refresh ();
}
static const char *room_cell (DataGrid &, int row, int col, char *buf, int cap)
{
	if (row < 0 || row >= g_nview) return "";
	const Room &r = g_rooms[g_view[row]];
	if (col == 0) return r.name;
	if (col == 1) { buf[0] = '\0'; scatn (buf, cap, r.users); return buf; }
	return r.topic;
}
static void join_chan (const char *ch)
{
	if (g_sock < 0 || g_state != 2) { info_here ("Not connected."); return; }
	char j[96] = "JOIN "; scat (j, sizeof j, ch); send_line (j);
}
static void show_rooms (bool on);
static void room_join (Widget &)
{
	if (g_grid->sel < 0 || g_grid->sel >= g_nview) return;
	join_chan (g_rooms[g_view[g_grid->sel]].name);
	show_rooms (false);
}
static void room_sort (Widget &)
{
	int c = g_grid->clickedCol;
	if (g_grid->sortCol == c) g_grid->sortDesc = !g_grid->sortDesc;
	else { g_grid->sortCol = c; g_grid->sortDesc = c == 1; }
	rooms_refresh ();
}
static void room_filter (Widget &) { rooms_refresh (); }
static void room_reload (Widget &) { rooms_request (); }

// ---- switching what is shown ------------------------------------------------------------------------

static void layout_chat (void)
{
	Buf *b = g_buf[g_cur];
	bool users = b->kind == BK_CHAN;
	int pw = g_chatPane->width, ph = g_chatPane->height;
	int cw = users ? pw - USERS_W : pw;
	g_chat->resizeTo (cw, ph - HEAD_H - IN_H);
	g_usersLbl->hidden = g_users->hidden = g_pmBtn->hidden = !users;
	g_leave->hidden = b->kind != BK_CHAN;
	g_head->rightPad = g_leave->hidden ? 0 : g_leave->width + 16;
	if (b->kind == BK_SERVER)
	{
		char s[160] = "Server messages";
		if (g_state == 2) { scat (s, sizeof s, "  -  you are "); scat (s, sizeof s, g_nick); }
		g_head->set (g_server, s);
	}
	else g_head->set (b->name, b->topic[0] ? b->topic : (b->joined ? "No topic" : "You are not in this channel"));
	g_chatPane->invalidate (true);
}
static void switch_to (int i)
{
	if (i < 0 || i >= g_nbuf) return;
	if (g_buf[i]->kind == BK_QUERY) { open_pm (g_buf[i]->name); g_treeDirty = true; return; }
	g_cur = i;
	g_buf[i]->unread = 0; g_buf[i]->mention = false;
	g_treeDirty = g_usersDirty = g_viewDirty = true;
	if (g_showRooms) show_rooms (false);
	layout_chat ();
	g_input->setFocus ();
}
static void show_rooms (bool on)
{
	g_showRooms = on;
	g_roomsPane->hidden = !on; g_chatPane->hidden = on;
	g_roomsBtn->setOn (on);
	if (on)
	{
		char s[120] = "Rooms on "; scat (s, sizeof s, g_server);
		g_roomsHead->set (s, "The server's public channels: search them, sort them, join one.");
		if (g_nrooms == 0 && !g_listing) rooms_request (); else rooms_refresh ();
		g_filter->setFocus ();
	}
	else g_input->setFocus ();
	g_roomsPane->invalidate (true); g_chatPane->invalidate (true);
}

// ---- the server's lines ---------------------------------------------------------------------------

static void chan_event (int bi, const char *text) { add_line (bi, LK_EVENT, "", text); }

static void handle_line (char *raw)
{
	if (!raw[0]) return;
	Msg m;
	if (!parse (raw, m)) return;
	const char *cmd = m.cmd;

	if (ax_streq (cmd, "PING")) { char b[600] = "PONG :"; scat (b, sizeof b, arg (m, 0)); send_line (b); return; }

	char text[480]; from_wire (arg (m, m.np - 1), text, sizeof text);
	char t[560];

	if (ax_streq (cmd, "PRIVMSG") || ax_streq (cmd, "NOTICE"))
	{
		bool notice = cmd[0] == 'N';
		const char *target = arg (m, 0);
		int bi;
		if (notice && (from_server (m) || ieq (target, "*"))) bi = 0;
		else if (is_chan (target)) bi = buf_get (target, BK_CHAN);
		else if (notice) bi = (ieq (m.nick, "NickServ") || ieq (m.nick, "ChanServ")) ? (g_buf[g_cur]->kind == BK_CHAN ? g_cur : 0) : buf_find (m.nick) >= 0 ? buf_find (m.nick) : g_cur;
		else bi = buf_get (m.nick, BK_QUERY);
		if (text[0] == '\x01')					// CTCP
		{
			char *c = text + 1; int k = 0; while (c[k] && c[k] != '\x01') k++; c[k] = '\0';
			if (c[0] == 'A' && c[1] == 'C' && c[2] == 'T' && c[3] == 'I' && c[4] == 'O' && c[5] == 'N')
			{
				add_line (bi, LK_ACTION, m.nick, c[6] == ' ' ? c + 7 : c + 6);
				if (g_buf[bi]->kind == BK_QUERY && g_buf[bi]->pmPid <= 0) open_pm (m.nick);
			}
			else if (!notice && c[0] == 'V' && c[1] == 'E' && c[2] == 'R')
			{ char r[160] = "NOTICE "; scat (r, sizeof r, m.nick); scat (r, sizeof r, " :\x01VERSION Onyx IRC (Raspberry Pi 4)\x01"); send_line (r); }
			return;
		}
		add_line (bi, notice ? LK_NOTICE : LK_MSG, m.nick[0] ? m.nick : "server", text);
		if (g_buf[bi]->kind == BK_QUERY && g_buf[bi]->pmPid <= 0) open_pm (m.nick);
		return;
	}
	if (ax_streq (cmd, "JOIN"))
	{
		const char *ch = arg (m, 0);
		int bi = buf_get (ch, BK_CHAN);
		Buf *b = g_buf[bi];
		if (ieq (m.nick, g_nick))
		{
			b->joined = true; b->nnick = 0;
			scpy (t, sizeof t, "You joined "); scat (t, sizeof t, ch); chan_event (bi, t);
			switch_to (bi);
		}
		else { nick_add (b, m.nick); scpy (t, sizeof t, m.nick); scat (t, sizeof t, " joined"); chan_event (bi, t); }
		g_treeDirty = true;
		return;
	}
	if (ax_streq (cmd, "PART") || ax_streq (cmd, "KICK"))
	{
		bool kick = cmd[0] == 'K';
		int bi = buf_find (arg (m, 0));
		if (bi < 0) return;
		const char *who = kick ? arg (m, 1) : m.nick;
		if (ieq (who, g_nick)) { g_buf[bi]->joined = false; g_buf[bi]->nnick = 0; g_usersDirty = true; }
		else nick_del (g_buf[bi], who);
		scpy (t, sizeof t, who);
		if (kick) { scat (t, sizeof t, " was kicked by "); scat (t, sizeof t, m.nick); }
		else scat (t, sizeof t, " left");
		if (m.np > (kick ? 2 : 1) && text[0]) { scat (t, sizeof t, " ("); scat (t, sizeof t, text); scat (t, sizeof t, ")"); }
		chan_event (bi, t);
		if (bi == g_cur) layout_chat ();
		return;
	}
	if (ax_streq (cmd, "QUIT"))
	{
		scpy (t, sizeof t, m.nick); scat (t, sizeof t, " quit");
		if (text[0]) { scat (t, sizeof t, " ("); scat (t, sizeof t, text); scat (t, sizeof t, ")"); }
		for (int i = 0; i < g_nbuf; i++)
			if ((g_buf[i]->kind == BK_CHAN && nick_del (g_buf[i], m.nick)) || (g_buf[i]->kind == BK_QUERY && ieq (g_buf[i]->name, m.nick)))
			{
				chan_event (i, t);
				if (g_buf[i]->kind == BK_QUERY) { g_buf[i]->peerGone = true; pm_send_state (g_buf[i]); }
			}
		return;
	}
	if (ax_streq (cmd, "NICK"))
	{
		const char *nn = arg (m, 0);
		bool me = ieq (m.nick, g_nick);
		scpy (t, sizeof t, me ? "You are" : m.nick); scat (t, sizeof t, me ? " now known as " : " is now known as "); scat (t, sizeof t, nn);
		for (int i = 0; i < g_nbuf; i++)
		{
			Buf *b = g_buf[i];
			int k = b->kind == BK_CHAN ? nick_index (b, m.nick) : -1;
			if (k >= 0) { scpy (b->nicks[k].name, sizeof b->nicks[k].name, nn); chan_event (i, t); if (i == g_cur) g_usersDirty = true; }
			else if (b->kind == BK_QUERY && ieq (b->name, m.nick))
			{ scpy (b->name, sizeof b->name, nn); chan_event (i, t); pm_send_state (b); g_treeDirty = true; }
		}
		if (me) { scpy (g_nick, sizeof g_nick, nn); g_nickTb->setText (nn); info (t, LK_EVENT); pm_state_all (); layout_chat (); }
		return;
	}
	if (ax_streq (cmd, "TOPIC"))
	{
		int bi = buf_find (arg (m, 0));
		if (bi < 0) return;
		scpy (g_buf[bi]->topic, sizeof g_buf[bi]->topic, text);
		scpy (t, sizeof t, m.nick); scat (t, sizeof t, " set the topic: "); scat (t, sizeof t, text);
		chan_event (bi, t);
		if (bi == g_cur) layout_chat ();
		return;
	}
	if (ax_streq (cmd, "MODE"))
	{
		const char *ch = arg (m, 0);
		int bi = buf_find (ch);
		if (bi < 0 || !is_chan (ch)) return;
		Buf *b = g_buf[bi];
		scpy (t, sizeof t, m.nick); scat (t, sizeof t, " sets mode");
		for (int i = 1; i < m.np; i++) { scat (t, sizeof t, " "); scat (t, sizeof t, m.p[i]); }
		chan_event (bi, t);
		const char *md = arg (m, 1); bool plus = true; int pi = 2;	// (+o / +v: the users' marks)
		for (; *md; md++)
		{
			if (*md == '+' || *md == '-') { plus = *md == '+'; continue; }
			if (*md == 'o' || *md == 'v' || *md == 'h')
			{
				int k = nick_index (b, arg (m, pi++));
				char pf = *md == 'o' ? '@' : *md == 'h' ? '%' : '+';
				if (k >= 0) { if (plus) { if (pfx_rank (pf) < pfx_rank (b->nicks[k].pfx)) b->nicks[k].pfx = pf; } else if (b->nicks[k].pfx == pf) b->nicks[k].pfx = 0; }
				if (bi == g_cur) g_usersDirty = true;
			}
			else if (*md == 'b' || *md == 'k' || *md == 'e' || *md == 'I' || (*md == 'l' && plus)) pi++;
		}
		return;
	}
	if (ax_streq (cmd, "ERROR")) { info (text, LK_ERROR); return; }

	// Numerics.
	if (cmd[0] >= '0' && cmd[0] <= '9')
	{
		int num = (cmd[0] - '0') * 100 + (cmd[1] - '0') * 10 + (cmd[2] - '0');
		switch (num)
		{
		case 1:							// welcome: registered
		{
			g_state = 2;
			scpy (g_nick, sizeof g_nick, arg (m, 0)); g_nickTb->setText (g_nick);
			info (text);
			if (g_password[0])
			{
				char l[160] = "PRIVMSG NickServ :IDENTIFY "; scat (l, sizeof l, g_nick); scat (l, sizeof l, " "); scat (l, sizeof l, g_password);
				send_line (l); info ("Identifying with NickServ...", LK_EVENT);
			}
			if (g_autojoin[0]) join_chan (g_autojoin);
			pm_state_all (); layout_chat ();
			return;
		}
		case 433:						// the nickname is taken
		case 432: case 436:
		{
			scpy (t, sizeof t, arg (m, 1)); scat (t, sizeof t, ": "); scat (t, sizeof t, text);
			info (t, LK_ERROR);
			if (g_state == 1 && num == 433)			// (registering: try another one)
			{
				char nn[32]; scpy (nn, sizeof nn, arg (m, 1));
				int n = slen (nn);
				if (n < 15) { nn[n] = '_'; nn[n + 1] = '\0'; }
				else nn[n - 1] = (char) ('0' + (kapi_get_ticks () % 10));
				char l[64] = "NICK "; scat (l, sizeof l, nn); send_line (l);
				scpy (t, sizeof t, "Trying the nickname "); scat (t, sizeof t, nn); info (t, LK_EVENT);
			}
			if (g_buf[g_cur]->kind != BK_SERVER) info_here (t);
			return;
		}
		case 331: case 332:					// the topic
		{
			int bi = buf_find (arg (m, 1));
			if (bi < 0) return;
			scpy (g_buf[bi]->topic, sizeof g_buf[bi]->topic, num == 332 ? text : "");
			if (bi == g_cur) layout_chat ();
			return;
		}
		case 333: case 366: case 329: case 315:		// (the topic's author, the end of the names...)
			if (num == 366) { int bi = buf_find (arg (m, 1)); if (bi >= 0) g_buf[bi]->namesOpen = false; }
			return;
		case 353:						// the names: "= #chan :@bob alice"
		{
			int bi = buf_find (arg (m, 2));
			if (bi < 0) return;
			Buf *b = g_buf[bi];
			if (!b->namesOpen) { b->namesOpen = true; b->nnick = 0; }
			char *s = m.p[m.np - 1];
			while (*s)
			{
				while (*s == ' ') s++;
				char *w = s; while (*s && *s != ' ') s++;
				char sv = *s; *s = '\0';
				if (*w) nick_add (b, w);
				*s = sv;
			}
			if (bi == g_cur) g_usersDirty = true;
			return;
		}
		case 321: g_nrooms = 0; g_listing = true; return;	// the LIST
		case 322:
		{
			if (!g_rooms || g_nrooms >= MAXROOMS) return;
			Room &r = g_rooms[g_nrooms++];
			scpy (r.name, sizeof r.name, arg (m, 1));
			r.users = 0; for (const char *u = arg (m, 2); *u >= '0' && *u <= '9'; u++) r.users = r.users * 10 + (*u - '0');
			const char *tp = text;
			if (tp[0] == '[' && tp[1] == '+') { while (*tp && *tp != ']') tp++; if (*tp) tp++; while (*tp == ' ') tp++; }	// ("[+nt] ": the modes)
			scpy (r.topic, sizeof r.topic, tp);
			return;
		}
		case 323: g_listing = false; if (g_showRooms) rooms_refresh (); return;
		case 375: case 372: case 376: case 2: case 3: case 4: case 5: case 250: case 251: case 252: case 253:
		case 254: case 255: case 265: case 266: case 396: case 900:
		{
			if (num == 4 || num == 5)			// (the server's version, its features: the words)
			{
				t[0] = '\0';
				for (int i = 1; i < m.np; i++) { if (i > 1) scat (t, sizeof t, " "); char w[480]; from_wire (m.p[i], w, sizeof w); scat (t, sizeof t, w); }
				info (t);
			}
			else info (text);
			return;
		}
		default:
			if (num >= 400)					// an error: where you are, and the server's
			{
				t[0] = '\0';
				if (m.np > 2) { scat (t, sizeof t, arg (m, 1)); scat (t, sizeof t, ": "); }
				scat (t, sizeof t, text);
				info (t, LK_ERROR);
				if (g_buf[g_cur]->kind != BK_SERVER) info_here (t);
			}
			else
			{
				t[0] = '\0';
				for (int i = 1; i < m.np; i++) { if (i > 1) scat (t, sizeof t, " "); char w[480]; from_wire (m.p[i], w, sizeof w); scat (t, sizeof t, w); }
				info (t);
			}
			return;
		}
	}
	// Anything else: the server's buffer.
	scpy (t, sizeof t, cmd); scat (t, sizeof t, " "); scat (t, sizeof t, text);
	info (t);
}

static void go_offline (const char *why)
{
	if (g_sock >= 0) { kapi_tcp_close (g_sock); g_sock = -1; }
	g_state = 3;
	info (why, LK_ERROR);
	for (int i = 1; i < g_nbuf; i++)
		if (g_buf[i]->kind == BK_CHAN && g_buf[i]->joined) { g_buf[i]->joined = false; g_buf[i]->nnick = 0; chan_event (i, why); }
	g_usersDirty = true; g_listing = false;
	scpy (g_connBtn->text, sizeof g_connBtn->text, "Connect"); g_connBtn->invalidate (true);
	pm_state_all (); layout_chat ();
}

static void drain_socket (void)
{
	if (g_sock < 0) return;
	// >= FRAME_BUFFER_SIZE (1600): CSocket::Receive drops the tail of a segment that does not fit.
	char b[1600]; int n, guard = 0;
	while (guard++ < 16 && (n = kapi_tcp_recv (g_sock, b, sizeof b)) > 0)
		for (int k = 0; k < n; k++)
		{
			char c = b[k];
			if (c == '\r') continue;
			if (c == '\n') { g_rx[g_rxlen] = '\0'; handle_line (g_rx); g_rxlen = 0; }
			else if (g_rxlen < (int) sizeof g_rx - 1) g_rx[g_rxlen++] = c;
			if (g_sock < 0) return;
		}
	if (guard <= 16 && n < 0) go_offline ("Disconnected from the server.");
}

// ---- the servers used, the nickname ----------------------------------------------------------------

static void hist_save (void)
{
	char buf[8 * 98]; int n = 0;
	for (int i = 0; i < g_histn; i++)
	{
		for (int k = 0; g_hist[i][k] && n < (int) sizeof buf - 2; k++) buf[n++] = g_hist[i][k];
		buf[n++] = '\n';
	}
	kapi_save_file (HISTPATH, buf, (unsigned) n);
}
static int read_small (const char *path, char *buf, int cap)
{
	void *f = kapi_open (path);
	if (f == 0) { buf[0] = '\0'; return 0; }
	int got = kapi_read (f, buf, (unsigned) cap - 1);
	kapi_close (f);
	if (got < 0) got = 0;
	buf[got] = '\0';
	return got;
}
static void hist_load (void)
{
	static char buf[8 * 98];
	read_small (HISTPATH, buf, sizeof buf);
	int i = 0;
	while (buf[i] && g_histn < 8)
	{
		char line[96]; int j = 0;
		while (buf[i] && buf[i] != '\n' && buf[i] != '\r' && j < 95) line[j++] = buf[i++];
		line[j] = '\0';
		while (buf[i] == '\n' || buf[i] == '\r') i++;
		if (j > 0) { scpy (g_hist[g_histn], sizeof g_hist[0], line); g_histn++; }
	}
}
static void hist_options (void)
{
	g_srv->clearOptions ();
	for (int i = 0; i < g_histn; i++) g_srv->addOption (g_hist[i]);
}
static void hist_add (const char *host, unsigned port)
{
	char entry[96]; scpy (entry, sizeof entry, host);
	if (port != 6667) { scat (entry, sizeof entry, ":"); scatn (entry, sizeof entry, (int) port); }
	int found = -1;
	for (int i = 0; i < g_histn; i++) if (ieq (g_hist[i], entry)) { found = i; break; }
	int start = found >= 0 ? found : (g_histn < 8 ? g_histn : 7);
	for (int i = start; i > 0; i--) scpy (g_hist[i], sizeof g_hist[0], g_hist[i - 1]);
	scpy (g_hist[0], sizeof g_hist[0], entry);
	if (found < 0 && g_histn < 8) g_histn++;
	hist_save (); hist_options ();
}

// ---- connecting ------------------------------------------------------------------------------------

static void connect_now (void)
{
	// The server ("host[:port]") and the nickname: the fields'.
	const char *spec = g_srv->text; while (*spec == ' ') spec++;
	if (*spec)
	{
		char host[80]; int n = 0; unsigned port = 6667;
		while (*spec && *spec != ':' && *spec != ' ' && n < 79) host[n++] = *spec++;
		host[n] = '\0';
		if (*spec == ':') { spec++; port = 0; while (*spec >= '0' && *spec <= '9') port = port * 10 + (unsigned) (*spec++ - '0'); if (!port) port = 6667; }
		scpy (g_server, sizeof g_server, host); g_port = port;
	}
	if (g_nickTb->text[0])
	{
		char nn[32]; int n = 0;
		for (const char *p = g_nickTb->text; *p && n < 30; p++) if (*p != ' ') nn[n++] = *p;
		nn[n] = '\0';
		if (n) scpy (g_nick, sizeof g_nick, nn);
	}
	kapi_save_file (NICKPATH, g_nick, (unsigned) slen (g_nick));
	if (g_sock >= 0) { send_line ("QUIT :reconnecting"); kapi_tcp_close (g_sock); g_sock = -1; }
	for (int i = 1; i < g_nbuf; i++) if (g_buf[i]->kind == BK_CHAN) { g_buf[i]->joined = false; g_buf[i]->nnick = 0; }
	g_nrooms = 0; g_listing = false;
	char t[160] = "Connecting to "; scat (t, sizeof t, g_server); scat (t, sizeof t, ":"); scatn (t, sizeof t, (int) g_port); scat (t, sizeof t, "...");
	info (t, LK_EVENT);
	g_state = 1;
	switch_to (0);
	if (Root::current ()) { Root::current ()->draw (); wk_present (); }	// (painted before the blocking connect)
	g_sock = kapi_tcp_connect (g_server, g_port);
	if (g_sock < 0) { g_state = 3; info ("The connection failed.", LK_ERROR); layout_chat (); return; }
	char l[160];
	scpy (l, sizeof l, "NICK "); scat (l, sizeof l, g_nick); send_line (l);
	scpy (l, sizeof l, "USER "); scat (l, sizeof l, g_user); scat (l, sizeof l, " 0 * :"); scat (l, sizeof l, g_real); send_line (l);
	scpy (t, sizeof t, "Signing in as "); scat (t, sizeof t, g_nick); scat (t, sizeof t, "..."); info (t, LK_EVENT);
	scpy (g_connBtn->text, sizeof g_connBtn->text, "Disconnect"); g_connBtn->invalidate (true);
	hist_add (g_server, g_port);
	layout_chat ();
}

static void on_connect (Widget &)
{
	if (g_sock >= 0)
	{
		send_line ("QUIT :Onyx IRC");
		go_offline ("You disconnected.");
		return;
	}
	connect_now ();
}
static void on_server_enter (Widget &) { connect_now (); }
static void on_nick_enter (Widget &)
{
	if (g_sock >= 0 && g_state == 2 && g_nickTb->text[0] && !ieq (g_nickTb->text, g_nick))
	{ char l[64] = "NICK "; scat (l, sizeof l, g_nickTb->text); send_line (l); }
	else if (g_sock < 0) connect_now ();
	g_input->setFocus ();
}

// ---- what you type -----------------------------------------------------------------------------------

static void say (int bi, const char *s, bool action)
{
	Buf *b = g_buf[bi];
	if (b->kind == BK_SERVER) { info_here ("This is the server's page: pick a channel (or /msg nick text)."); return; }
	if (g_sock < 0 || g_state != 2) { info_here ("Not connected."); return; }
	if (b->kind == BK_CHAN && !b->joined) { info_here ("You are not in this channel: /join it again."); return; }
	char w[520]; to_wire (s, w, sizeof w);
	char l[640] = "PRIVMSG "; scat (l, sizeof l, b->name); scat (l, sizeof l, " :");
	if (action) { scat (l, sizeof l, "\x01" "ACTION "); scat (l, sizeof l, w); scat (l, sizeof l, "\x01"); }
	else scat (l, sizeof l, w);
	send_line (l);
	add_line (bi, action ? LK_ACTION : LK_MSG, g_nick, s, true);
	b->scroll = 0;
}

static void submit (char *s)
{
	if (s[0] == '\0') return;
	if (s[0] != '/' || s[1] == '/') { say (g_cur, s[0] == '/' ? s + 1 : s, false); return; }
	char *cmd = s + 1;
	char *a = cmd; while (*a && *a != ' ') a++;
	if (*a) *a++ = '\0';
	while (*a == ' ') a++;
	for (char *c = cmd; *c; c++) *c = lc (*c);

	if (ax_streq (cmd, "join") && a[0]) join_chan (a);
	else if (ax_streq (cmd, "part") || ax_streq (cmd, "close") || ax_streq (cmd, "leave"))
	{
		Buf *b = g_buf[g_cur];
		const char *ch = a[0] ? a : b->name;
		if (is_chan (ch) && g_sock >= 0) { char j[96] = "PART "; scat (j, sizeof j, ch); send_line (j); }
		int bi = buf_find (ch);
		if (bi > 0) { buf_remove (bi); switch_to (g_cur); }
	}
	else if (ax_streq (cmd, "nick") && a[0]) { char j[64] = "NICK "; scat (j, sizeof j, a); send_line (j); if (g_state != 2) { scpy (g_nick, sizeof g_nick, a); g_nickTb->setText (a); } }
	else if ((ax_streq (cmd, "msg") || ax_streq (cmd, "query")) && a[0])
	{
		char *target = a; while (*a && *a != ' ') a++;
		if (*a) *a++ = '\0';
		if (is_chan (target)) { int bi = buf_find (target); if (bi >= 0 && a[0]) say (bi, a, false); else if (a[0]) { char w[520]; to_wire (a, w, sizeof w); char l[640] = "PRIVMSG "; scat (l, sizeof l, target); scat (l, sizeof l, " :"); scat (l, sizeof l, w); send_line (l); } }
		else
		{
			int bi = buf_get (target, BK_QUERY);
			if (a[0]) say (bi, a, false);
			open_pm (target);
		}
	}
	else if (ax_streq (cmd, "me") && a[0]) say (g_cur, a, true);
	else if (ax_streq (cmd, "topic"))
	{
		Buf *b = g_buf[g_cur];
		if (b->kind != BK_CHAN) return;
		char w[480]; to_wire (a, w, sizeof w);
		char l[600] = "TOPIC "; scat (l, sizeof l, b->name);
		if (a[0]) { scat (l, sizeof l, " :"); scat (l, sizeof l, w); }
		send_line (l);
	}
	else if (ax_streq (cmd, "list")) show_rooms (true);
	else if (ax_streq (cmd, "quit"))
	{
		char j[160] = "QUIT :"; scat (j, sizeof j, a[0] ? a : "Onyx IRC"); send_line (j);
		kapi_exit (0);
	}
	else if ((ax_streq (cmd, "server") || ax_streq (cmd, "connect")) && a[0]) { g_srv->setText (a); connect_now (); }
	else if (ax_streq (cmd, "clear")) { g_buf[g_cur]->count = 0; g_viewDirty = true; }
	else if (ax_streq (cmd, "raw") && a[0]) send_line (a);
	else { char j[640]; scpy (j, sizeof j, cmd); for (char *c = j; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char) (*c - 32); if (a[0]) { scat (j, sizeof j, " "); scat (j, sizeof j, a); } send_line (j); }
}

static void on_send (Widget &)
{
	char s[512]; scpy (s, sizeof s, g_input->text);
	g_input->setText ("");
	submit (s);
	g_input->setFocus ();
}
static void on_leave (Widget &) { char s[16] = "/part"; submit (s); }
static void on_user_pm (Widget &)
{
	if (g_users->sel >= 0) open_pm (g_users->item (g_users->sel));
}
static void on_tree (Widget &)
{
	int id = g_tree->sel;
	if (id < 0) return;
	int bi = g_tree->userData (id);
	if (bi >= 0 && bi < g_nbuf) switch_to (bi);
	else g_treeDirty = true;				// (a heading: the selection back where it was)
}
static void on_rooms (Widget &) { show_rooms (!g_showRooms); }

// ---- keeping the widgets up to date --------------------------------------------------------------

static void tree_rebuild (void)
{
	int top = g_tree->top;
	g_tree->clear ();
	char l[48];
	const char *sv = g_server; if (sv[0] == 'i' && sv[1] == 'r' && sv[2] == 'c' && sv[3] == '.') sv += 4;
	int srv = g_tree->add (-1, sv); g_tree->setUserData (srv, 0);
	int sel = g_cur == 0 ? srv : -1, dm = -1;
	for (int pass = 0; pass < 2; pass++)
		for (int i = 1; i < g_nbuf; i++)
		{
			Buf *b = g_buf[i];
			if ((pass == 0) != (b->kind == BK_CHAN)) continue;
			scpy (l, sizeof l, b->name);
			if (b->kind == BK_CHAN && !b->joined) scat (l, sizeof l, " (left)");
			if (b->unread) { scat (l, sizeof l, "  ("); scatn (l, sizeof l, b->unread); if (b->mention) scat (l, sizeof l, " @"); scat (l, sizeof l, ")"); }
			int parent = srv;
			if (pass == 1)
			{
				if (dm < 0) { dm = g_tree->add (-1, "Private messages"); g_tree->setUserData (dm, -1); }
				parent = dm;
			}
			int id = g_tree->add (parent, l);
			g_tree->setUserData (id, i);
			if (i == g_cur) sel = id;
		}
	g_tree->expand (srv, true);
	if (dm >= 0) g_tree->expand (dm, true);
	g_tree->sel = sel;
	g_tree->top = top;
	g_tree->invalidate (true);
}
static void users_rebuild (void)
{
	Buf *b = g_buf[g_cur];
	int top = g_users->top;
	g_users->clear ();
	if (b->kind == BK_CHAN)
	{
		nick_sort (b);
		char e[40];
		for (int i = 0; i < b->nnick; i++)
		{
			e[0] = b->nicks[i].pfx; e[1] = '\0';
			scat (e, sizeof e, b->nicks[i].name);
			g_users->add (e);
		}
	}
	g_users->top = top < g_users->count ? top : 0;
	char t[40] = "Users  "; scatn (t, sizeof t, b->nnick);
	g_usersLbl->setText (t);
	g_users->invalidate (true);
}

class IrcRoot : public Root
{
public:
	IrcRoot () : Root (W, H, "IRC") {}
	void onTick () override
	{
		if (g_state == 0)
		{
			char ip[32];
			if (kapi_net_status (ip, sizeof ip))
			{
				char m[80] = "Network up ("; scat (m, sizeof m, ip); scat (m, sizeof m, ")"); info (m, LK_EVENT);
				connect_now ();
			}
		}
		drain_socket ();
		mailbox_pump ();
		static unsigned lastStatus = 0; unsigned now = kapi_get_ticks ();
		if (g_treeDirty) { g_treeDirty = false; tree_rebuild (); g_status->invalidate (true); }
		if (g_usersDirty) { g_usersDirty = false; users_rebuild (); }
		if (g_viewDirty) { g_viewDirty = false; g_chat->invalidate (true); }
		if (g_showRooms)
		{
			if (!ax_streq (g_filter->text, g_lastFilter) || g_minU->value != g_lastMin) rooms_refresh ();
			else if (g_listing && now - lastStatus > 50) { lastStatus = now; rooms_refresh (); }
		}
		static int lastState = -1;
		if (lastState != g_state) { lastState = g_state; g_status->invalidate (true); g_roomsBtn->invalidate (true); }
	}
};

static Label *label (int x, int y, int h, const char *s, unsigned bg, unsigned fg)
{
	return new Label (x, y, wk_text_w (s) + 6, h, s, fg, bg);
}

static int main_window (void)
{
	if (app_ini_load ("config.ini") >= 0)
	{
		scpy (g_server,   sizeof g_server,   app_ini_get ("irc", "server",   g_server));
		scpy (g_nick,     sizeof g_nick,     app_ini_get ("irc", "nick",     g_nick));
		scpy (g_user,     sizeof g_user,     app_ini_get ("irc", "user",     g_user));
		scpy (g_real,     sizeof g_real,     app_ini_get ("irc", "realname", g_real));
		scpy (g_autojoin, sizeof g_autojoin, app_ini_get ("irc", "channel",  g_autojoin));
		scpy (g_password, sizeof g_password, app_ini_get ("irc", "password", g_password));
		g_port = (unsigned) app_ini_get_int ("irc", "port", (int) g_port);
	}
	char nk[40];
	if (read_small (NICKPATH, nk, sizeof nk) > 0)
	{
		int n = 0; while (nk[n] && nk[n] != '\n' && nk[n] != '\r' && nk[n] != ' ') n++;
		nk[n] = '\0';
		if (n) scpy (g_nick, sizeof g_nick, nk);
	}
	hist_load ();
	buf_get ("*server*", BK_SERVER);
	kapi_ipc_register ("irc");

	IrcRoot root;
	if (root.canvas.px == 0) return 1;
	g_cw = wk_text_w ("M"); if (g_cw < 1) g_cw = 8;
	g_fh = wk_fh ();
	g_rooms = new Room[MAXROOMS]; g_view = new int[MAXROOMS];

	// The toolbar: the server, the nickname, Connect; Rooms on the right.
	ToolBar *tb = new ToolBar (0, 0, W, TB_H);
	tb->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	root.addChild (tb);
	tb->space (6);
	tb->add (label (0, 0, 24, "Server", C_BG, C_TEXT), 0);
	char addr[96]; scpy (addr, sizeof addr, g_server);
	if (g_port != 6667) { scat (addr, sizeof addr, ":"); scatn (addr, sizeof addr, (int) g_port); }
	g_srv = new Combobox (0, 0, 220, 28, addr, on_server_enter, 0);
	g_srv->tip = "The server (host or host:port) -- the arrow: the ones used before";
	hist_options ();
	tb->add (g_srv, 4);
	tb->space (10);
	tb->add (label (0, 0, 24, "Nickname", C_BG, C_TEXT), 0);
	g_nickTb = new Textbox (0, 0, 130, 28, g_nick, on_nick_enter);
	g_nickTb->maxLen = 30;
	g_nickTb->tip = "Your nickname: sent when you connect (Enter while connected: change it)";
	tb->add (g_nickTb, 4);
	g_connBtn = new Button (0, 0, 104, 28, "Connect", on_connect);
	tb->add (g_connBtn, 8);
	g_roomsBtn = (new ToolButton (0, 30, "Browse the server's rooms", on_rooms))->setGlyph (WKT_SEARCH)->setText ("Rooms")->setToggle (true)->fitWidth ();
	tb->addRight (g_roomsBtn, 8);

	// The body: the conversations | the channel (or the rooms).
	int bodyH = H - TB_H - SB_H;
	HSplitter *split = new HSplitter (0, TB_H, W, bodyH, SIDE_W, C_BG);
	split->anchor = ANCHOR_FILL; split->minA = 140; split->minB = 400;
	root.addChild (split);

	Panel *side = new Panel (0, 0, SIDE_W, bodyH, C_BG);
	g_tree = new TreeView (8, 8, SIDE_W - 12, bodyH - 16, on_tree, on_tree);
	g_tree->anchor = ANCHOR_FILL;
	g_tree->tip = "Your conversations: click one to show it";
	side->addChild (g_tree);

	int mw = W - SIDE_W - 6, mh = bodyH;
	Panel *mainP = new Panel (0, 0, mw, mh, C_FIELD);
	split->setPanes (side, mainP);
	mw = mainP->width; mh = mainP->height;

	g_chatPane = new Panel (0, 0, mw, mh, C_FIELD); g_chatPane->anchor = ANCHOR_FILL;
	mainP->addChild (g_chatPane);
	g_head = new HeaderBar (0, 0, mw, HEAD_H); g_head->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_chatPane->addChild (g_head);
	g_leave = new Button (mw - 96, 9, 84, 28, "Leave", on_leave);
	g_leave->anchor = ANCHOR_RIGHT | ANCHOR_TOP; g_leave->tip = "Leave this channel";
	g_chatPane->addChild (g_leave);
	g_chat = new ChatView (0, HEAD_H, mw - USERS_W, mh - HEAD_H - IN_H); g_chat->anchor = ANCHOR_FILL;
	g_chatPane->addChild (g_chat);
	g_usersLbl = new Label (mw - USERS_W + 4, HEAD_H + 6, USERS_W - 12, 20, "Users", wk_mix (C_FIELD_TEXT, C_FIELD, 120), C_FIELD);
	g_usersLbl->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
	g_chatPane->addChild (g_usersLbl);
	g_users = new ListBox (mw - USERS_W + 2, HEAD_H + 28, USERS_W - 12, mh - HEAD_H - IN_H - 28 - 38, 0, on_user_pm);
	g_users->anchor = ANCHOR_RIGHT | ANCHOR_TOP | ANCHOR_BOTTOM;
	g_users->tip = "Double-click someone to talk to them privately";
	g_chatPane->addChild (g_users);
	g_pmBtn = new Button (mw - USERS_W + 2, mh - IN_H - 34, USERS_W - 12, 28, "Message", on_user_pm);
	g_pmBtn->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM; g_pmBtn->tip = "A private conversation with the user selected";
	g_chatPane->addChild (g_pmBtn);
	g_inRow = new Panel (0, mh - IN_H, mw, IN_H, C_FIELD); g_inRow->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	g_chatPane->addChild (g_inRow);
	g_input = new Textbox (12, 8, mw - 12 - 100, 30, "", on_send);
	g_input->maxLen = 400; g_input->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_TOP;
	g_inRow->addChild (g_input);
	g_send = new Button (mw - 92, 8, 80, 30, "Send", on_send);
	g_send->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
	g_inRow->addChild (g_send);

	g_roomsPane = new Panel (0, 0, mw, mh, C_FIELD); g_roomsPane->anchor = ANCHOR_FILL; g_roomsPane->hidden = true;
	mainP->addChild (g_roomsPane);
	g_roomsHead = new HeaderBar (0, 0, mw, HEAD_H); g_roomsHead->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_roomsPane->addChild (g_roomsHead);
	int ry = HEAD_H + 10, x = 12;
	Label *l1 = label (x, ry + 2, 24, "Search", C_FIELD, C_FIELD_TEXT); g_roomsPane->addChild (l1); x += l1->width + 4;
	g_filter = new Textbox (x, ry, 200, 28, "", room_filter); g_filter->tip = "Rooms whose name or topic has this";
	g_roomsPane->addChild (g_filter); x += 200 + 14;
	Label *l2 = label (x, ry + 2, 24, "Min. users", C_FIELD, C_FIELD_TEXT); g_roomsPane->addChild (l2); x += l2->width + 4;
	g_minU = new NumericUpDown (x, ry, 86, 28, 0, 100000, 5, 5, room_filter); g_roomsPane->addChild (g_minU); x += 86 + 14;
	Button *rf = new Button (x, ry, 96, 28, "Refresh", room_reload); rf->tip = "Ask the server for the list again";
	g_roomsPane->addChild (rf);
	Button *jb = new Button (mw - 96, ry, 84, 28, "Join", room_join); jb->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
	g_roomsPane->addChild (jb);
	g_grid = new DataGrid (12, ry + 40, mw - 24, mh - ry - 40 - 34);
	g_grid->anchor = ANCHOR_FILL;
	g_grid->setColumns (3);
	g_grid->setColumn (0, "Room", 190); g_grid->setColumn (1, "Users", 70, GRID_RIGHT); g_grid->setColumn (2, "Topic", mw - 24 - 190 - 70 - WK_SBW - 6);
	g_grid->cellText = room_cell; g_grid->sortable = true; g_grid->sortCol = 1; g_grid->sortDesc = true;
	g_grid->onSort = room_sort; g_grid->onActivate = room_join;
	g_grid->emptyText = "No room to show";
	g_roomsPane->addChild (g_grid);
	g_roomsInfo = new Label (12, mh - 28, mw - 24, 22, "", ink_soft (), C_FIELD);
	g_roomsInfo->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	g_roomsPane->addChild (g_roomsInfo);

	g_status = new StatusBar (0, H - SB_H, W, SB_H);
	g_status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.addChild (g_status);

	root.setResizable (true);
	root.fitWorkArea ();
	info ("Onyx IRC -- waiting for the network...", LK_EVENT);
	layout_chat ();
	g_input->setFocus ();
	root.run ();

	for (int i = 0; i < g_nbuf; i++) if (g_buf[i]->pmPid > 0) kapi_mailbox_send (g_buf[i]->pmPid, T_CLOSE, "", 0);
	if (g_sock >= 0) { send_line ("QUIT :Onyx IRC"); kapi_tcp_close (g_sock); }
	return 0;
}

// =====================================================================================================
// A private conversation's window: a messenger's
// =====================================================================================================

#define PW	400
#define PH	560
#define PMAX	300

struct PmLine { char kind; bool self; char time[6]; char text[420]; };
static PmLine *g_pl = 0;
static int  g_pfirst = 0, g_pcount = 0;
static char g_peer[40], g_me[32] = "", g_psrv[80] = "";
static int  g_mainPid = 0;
static bool g_online = false;
static Textbox *g_pin; static Button *g_psend;
class BubbleView; class PmHeader;
static BubbleView *g_bubbles; static PmHeader *g_phead;

static PmLine &pl (int i) { return g_pl[(g_pfirst + i) % PMAX]; }

// The header: their avatar, their name, whether they are there.
class PmHeader : public Widget
{
public:
	PmHeader (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		avatar (canvas, 14, (height - 32) / 2, 32, g_peer);
		char b[64]; fit (g_peer, width - 80, b, sizeof b, 2);
		wk_text_l (canvas, 58, height / 2 - RH, RH, b, C_FIELD_TEXT, 2);
		unsigned dot = g_online ? 0x0027AE60 : 0x009A9A9A;
		wk_rbox (canvas, 58, height / 2 + (RH - 8) / 2 + 1, 8, 8, 4, wk_tone (dot, 170), dot);
		char s[140] = "";
		if (!g_mainPid) scpy (s, sizeof s, "IRC is not running");
		else if (g_online) { scat (s, sizeof s, "Online on "); scat (s, sizeof s, g_psrv); }
		else scpy (s, sizeof s, "Offline");
		fit (s, width - 90, b, sizeof b);
		wk_text_l (canvas, 72, height / 2 + 1, RH, b, ink_soft ());
		canvas.fillRect (0, height - 1, width, 1, line_col ());
	}
};

// The bubbles: theirs on the left (a grey bubble, their avatar beside the last of a run), yours on
// the right (the accent); the time centred over a run that starts after a pause; an action or an
// event a grey line in the middle.
class BubbleView : public Widget
{
public:
	Scroller sc;
	BubbleView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	unsigned bgColor () override { return C_FIELD; }
	static bool bubble (const PmLine &l) { return l.kind == 'm' || l.kind == 'M' || l.kind == 'n'; }
	static int mins (const PmLine &l) { return ((l.time[0] - '0') * 10 + l.time[1] - '0') * 60 + (l.time[3] - '0') * 10 + l.time[4] - '0'; }
	static bool newTime (const PmLine &l, const PmLine *p)
	{ return !p || mins (l) - mins (*p) >= 15 || mins (l) < mins (*p); }
	static bool sameRun (const PmLine &l, const PmLine *nx) { return nx && bubble (*nx) && bubble (l) && nx->self == l.self && !newTime (*nx, &l); }
	int maxc () const { int w = (width - 24 - WK_SBW - 40) * 74 / 100 - 24; return w / g_cw; }
	int height_of (const PmLine &l, const PmLine *p)
	{
		int h = 0;
		if (newTime (l, p)) h += RH + 10;
		if (!bubble (l)) return h + wrap_rows (l.text, (width - 40) / g_cw) * RH + 8;
		h += wrap_rows (l.text, maxc ()) * RH + 14;
		h += (p && bubble (*p) && p->self == l.self && !newTime (l, p)) ? 3 : 10;
		return h;
	}
	void draw_line (const PmLine &l, const PmLine *p, const PmLine *nx, int y)
	{
		if (newTime (l, p)) { wk_text_c (canvas, 0, y + 4, width - WK_SBW, RH, l.time, ink_soft ()); y += RH + 10; }
		if (!bubble (l))
		{
			unsigned c = ink_soft ();
			int mc = (width - 40) / g_cw;
			for (const char *s = l.text; ; )
			{
				const char *nxs; int n = wrap_row (s, mc, &nxs);
				char t[512]; int k = 0; for (; k < n && k < 511; k++) t[k] = s[k]; t[k] = '\0';
				wk_text_c (canvas, 0, y + 4, width - WK_SBW, RH, t, c, l.kind == 'a' || l.kind == 'A' ? 1 : 0);
				y += RH;
				if (!*nxs) break; s = nxs;
			}
			return;
		}
		y += (p && bubble (*p) && p->self == l.self && !newTime (l, p)) ? 3 : 10;
		int widest = 0, rows = wrap_rows (l.text, maxc (), &widest);
		int bw = widest * g_cw + 24, bh = rows * RH + 14;
		int x = l.self ? width - WK_SBW - 12 - bw : 14 + 28 + 8;
		unsigned fill = l.self ? C_ACCENT : wk_mix (C_FIELD, C_FIELD_TEXT, 24);
		unsigned ink = l.self ? wk_ink_on (C_ACCENT) : (l.kind == 'n' ? 0x009A6400 : C_FIELD_TEXT);
		wk_rbox (canvas, x, y, bw, bh, 14, fill, fill);
		int ty = y + 7;
		for (const char *s = l.text; ; )
		{
			const char *nxs; int n = wrap_row (s, maxc (), &nxs);
			draw_n (canvas, x + 12, ty, s, n, ink); ty += RH;
			if (!*nxs) break; s = nxs;
		}
		if (!l.self && !sameRun (l, nx)) avatar (canvas, 14, y + bh - 28, 28, g_peer);
	}
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		sc.view = height - 10;
		static int hts[PMAX];
		int total = 0;
		for (int i = 0; i < g_pcount; i++) { hts[i] = height_of (pl (i), i ? &pl (i - 1) : 0); total += hts[i]; }
		sc.total = total; sc.clamp ();
		if (g_pcount == 0)
		{
			avatar (canvas, (width - 64) / 2, height / 2 - 90, 64, g_peer);
			wk_text_c (canvas, 0, height / 2 - 14, width, RH, g_peer, C_FIELD_TEXT, 2);
			char s[80] = "Say hello to "; scat (s, sizeof s, g_peer);
			wk_text_c (canvas, 0, height / 2 + 8, width, RH, s, ink_soft ());
			return;
		}
		int y = 4 + (sc.view > total ? sc.view - total : sc.view - total + sc.scroll);
		for (int i = 0; i < g_pcount; i++)
		{
			if (y + hts[i] >= 0 && y < height)
				draw_line (pl (i), i ? &pl (i - 1) : 0, i + 1 < g_pcount ? &pl (i + 1) : 0, y);
			y += hts[i];
		}
		sc.bar (canvas, width, height);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { sc.drag = false; return false; }
		if (sc.mouse (*this, mx, my, bl, wheel)) invalidate (true);
		return true;
	}
};

static void pm_add (const char *payload)
{
	// "kind \t HH:MM \t nick \t text"
	char f[4][420]; int fi = 0, k = 0;
	for (int i = 0; i < 4; i++) f[i][0] = '\0';
	for (const char *s = payload; *s && fi < 4; s++)
	{
		if (*s == '\t' && fi < 3) { f[fi][k] = '\0'; fi++; k = 0; continue; }
		if (k < 419) f[fi][k++] = *s;
		f[fi][k] = '\0';
	}
	int idx;
	if (g_pcount < PMAX) idx = (g_pfirst + g_pcount++) % PMAX;
	else { idx = g_pfirst; g_pfirst = (g_pfirst + 1) % PMAX; }
	PmLine &l = g_pl[idx];
	char kind = f[0][0];
	l.kind = kind; l.self = kind == 'M' || kind == 'A';
	scpy (l.time, sizeof l.time, f[1][0] ? f[1] : "00:00");
	if (kind == 'a' || kind == 'A') { scpy (l.text, sizeof l.text, "* "); scat (l.text, sizeof l.text, f[2]); scat (l.text, sizeof l.text, " "); scat (l.text, sizeof l.text, f[3]); }
	else scpy (l.text, sizeof l.text, f[3]);
	if (g_bubbles) { g_bubbles->sc.scroll = 0; g_bubbles->invalidate (true); }
}

static void pm_state (const char *payload)		// "online \t me \t server \t peer"
{
	char f[4][80]; int fi = 0, k = 0;
	for (int i = 0; i < 4; i++) f[i][0] = '\0';
	for (const char *s = payload; *s && fi < 4; s++)
	{
		if (*s == '\t') { f[fi][k] = '\0'; fi++; k = 0; continue; }
		if (k < 79) f[fi][k++] = *s;
		f[fi][k] = '\0';
	}
	g_online = f[0][0] == '1';
	scpy (g_me, sizeof g_me, f[1]); scpy (g_psrv, sizeof g_psrv, f[2]);
	if (f[3][0]) scpy (g_peer, sizeof g_peer, f[3]);
	g_phead->invalidate (true); g_bubbles->invalidate (true);
}

static void pm_send (Widget &)
{
	if (!g_pin->text[0]) return;
	if (!g_mainPid) return;
	kapi_mailbox_send (g_mainPid, T_SAY, g_pin->text, (unsigned) slen (g_pin->text));
	g_pin->setText ("");
	g_pin->setFocus ();
}

class PmRoot : public Root
{
public:
	PmRoot () : Root (PW, PH, g_peer) {}
	void onTick () override
	{
		char m[520]; int from = 0, type = 0, n;
		while ((n = kapi_mailbox_recv (&from, &type, m, sizeof m - 1, 0)) >= 0)
		{
			m[n] = '\0';
			if (from != g_mainPid) continue;
			if (type == T_LINE) pm_add (m);
			else if (type == T_STATE) pm_state (m);
			else if (type == T_CLOSE) kapi_exit (0);
		}
		static unsigned last = 0; unsigned now = kapi_get_ticks ();
		if (now - last > 100)					// (every second: the main window still there?)
		{
			last = now;
			int pid = kapi_ipc_lookup ("irc");
			if (pid != g_mainPid)
			{
				g_mainPid = pid; g_online = false;
				if (pid) kapi_mailbox_send (pid, T_HELLO, g_peer, (unsigned) slen (g_peer));
				g_phead->invalidate (true);
			}
		}
	}
};

static int pm_window (const char *peer)
{
	scpy (g_peer, sizeof g_peer, peer);
	g_pl = new PmLine[PMAX];
	PmRoot root;
	if (root.canvas.px == 0) return 1;
	g_cw = wk_text_w ("M"); if (g_cw < 1) g_cw = 8;
	g_fh = wk_fh ();
	root.setBg (C_FIELD);

	g_phead = new PmHeader (0, 0, PW, 56); g_phead->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	root.addChild (g_phead);
	g_bubbles = new BubbleView (0, 56, PW, PH - 56 - 50); g_bubbles->anchor = ANCHOR_FILL;
	root.addChild (g_bubbles);
	Panel *row = new Panel (0, PH - 50, PW, 50, C_FIELD); row->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.addChild (row);
	g_pin = new Textbox (10, 10, PW - 20 - 76, 30, "", pm_send);
	g_pin->maxLen = 400; g_pin->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_TOP;
	row->addChild (g_pin);
	g_psend = new Button (PW - 80, 10, 70, 30, "Send", pm_send); g_psend->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
	row->addChild (g_psend);

	g_mainPid = kapi_ipc_lookup ("irc");
	if (g_mainPid) kapi_mailbox_send (g_mainPid, T_HELLO, g_peer, (unsigned) slen (g_peer));
	root.setResizable (true);
	g_pin->setFocus ();
	root.run ();
	if (g_mainPid) kapi_mailbox_send (g_mainPid, T_BYE, g_peer, (unsigned) slen (g_peer));
	return 0;
}

int main (void)
{
	char a[128];
	int n = kapi_get_args (a, sizeof a);
	if (n < 0) n = 0;
	a[n < (int) sizeof a ? n : (int) sizeof a - 1] = '\0';
	const char *p = a; while (*p == ' ') p++;
	if (p[0] == '-' && p[1] == '-' && p[2] == 'p' && p[3] == 'm' && p[4] == ' ')
	{
		p += 5; while (*p == ' ') p++;
		char nk[40]; int k = 0; while (p[k] && p[k] != ' ' && k < 39) { nk[k] = p[k]; k++; } nk[k] = '\0';
		if (k) return pm_window (nk);
	}
	return main_window ();
}
