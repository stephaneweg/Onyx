//
// ftpc -- the Onyx FTP server (hot deployment: drop files onto the card from a PC).
//
//   ftpc [homedir] [user] [password]     (defaults: SD:/  onyx  onyx)
//
// The FIRST instance becomes the server (IPC service "ftpd", TCP port 21). Running ftpc
// again while it is up does not start a second server: it just tells the running one
// "this user, this password, this root folder" (IPC) and exits -- so users are added
// on the fly, no configuration file. Each FTP client connection is served by its own
// child process (`ftpc --session ...`, spawned with the socket handle and the user
// table), so several clients (FileZilla opens more than one connection) work at once.
//
// Supported: USER PASS SYST FEAT OPTS PWD CWD CDUP TYPE MODE STRU PASV EPSV PORT LIST
// NLST MLSD RETR STOR APPE DELE MKD RMD RNFR RNTO SIZE NOOP QUIT (+ X* aliases). A user
// only sees its root folder ("/" = homedir; ".." cannot climb above it). Uploads are
// buffered in memory (<= 32 MB) and written in one go (kapi_save_file).
//
#include "kapi.h"
#include "applib.h"
#include "umm.h"

#define SERVICE		"ftpd"
#define MSG_ADDUSER	1		// "user\0pass\0home\0"
#define MAXUSERS	8
#define FIELD_SEP	'\x1F'		// session args: user SEP pass SEP home RS ...
#define REC_SEP		'\x1E'
#define DATA_PORT0	50000		// passive data ports rotate over 50000..50099
#define UPLOAD_MAX	(32u * 1024 * 1024)

struct User { char name[32], pass[32], home[128]; };
static struct User g_users[MAXUSERS];
static int g_nusers = 0;

// ---- small string helpers -------------------------------------------------------------
static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static char low (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
static int ieq (const char *a, const char *b) { for (; *a && *b; a++, b++) if (low (*a) != low (*b)) return 0; return *a == *b; }
static void cat (char *d, int cap, const char *s) { int n = slen (d); for (int i = 0; s[i] && n < cap - 1; i++) d[n++] = s[i]; d[n] = '\0'; }
static void catn (char *d, int cap, unsigned v) { char t[12]; int k = 0; if (!v) t[k++] = '0'; while (v) { t[k++] = (char) ('0' + v % 10); v /= 10; } int n = slen (d); while (k && n < cap - 1) d[n++] = t[--k]; d[n] = '\0'; }

static void add_user (const char *name, const char *pass, const char *home)
{
	int i = 0;
	while (i < g_nusers && !ieq (g_users[i].name, name)) i++;
	if (i == g_nusers) { if (g_nusers >= MAXUSERS) return; g_nusers++; }
	scpy (g_users[i].name, name, sizeof g_users[i].name);
	scpy (g_users[i].pass, pass, sizeof g_users[i].pass);
	scpy (g_users[i].home, home, sizeof g_users[i].home);
	int n = slen (g_users[i].home);				// no trailing '/' (except "SD:/")
	while (n > 4 && g_users[i].home[n - 1] == '/') g_users[i].home[--n] = '\0';
}

// =========================================================================================
// Session (one FTP client), in its own process
// =========================================================================================
static int  g_ctl = -1;				// control connection handle
static int  g_user = -1;			// logged-in user index (-1 = not yet)
static char g_pendingUser[32];
static char g_cwd[256] = "/";			// the client-visible path
static int  g_pasv = -1;			// passive listening handle
static char g_portIP[40]; static int g_portPort = 0;	// active mode (PORT)
static char g_rnfr[300];
static char g_myIP[40];

static void reply (const char *s)
{
	char b[400]; scpy (b, s, sizeof b - 2); cat (b, sizeof b, "\r\n");
	kapi_tcp_send (g_ctl, b, (unsigned) slen (b));
}

// Client path (absolute or relative to g_cwd) -> normalised client path (in cpath) and
// the real SD path (in real). Never climbs above "/".
static void resolve (const char *arg, char *cpath, int ccap, char *real, int rcap)
{
	char tmp[512];
	if (arg[0] == '/') scpy (tmp, arg, sizeof tmp);
	else { scpy (tmp, g_cwd, sizeof tmp); if (slen (tmp) > 1) cat (tmp, sizeof tmp, "/"); cat (tmp, sizeof tmp, arg); }
	char seg[32][64]; int ns = 0;			// split + fold "." / ".."
	for (char *p = tmp; *p; )
	{
		while (*p == '/') p++;
		if (!*p) break;
		char s[64]; int k = 0;
		while (*p && *p != '/') { if (k < 63) s[k++] = *p; p++; }
		s[k] = '\0';
		if (s[0] == '.' && s[1] == '\0') continue;
		if (s[0] == '.' && s[1] == '.' && s[2] == '\0') { if (ns) ns--; continue; }
		if (ns < 32) scpy (seg[ns++], s, 64);
	}
	cpath[0] = '\0';
	for (int i = 0; i < ns; i++) { cat (cpath, ccap, "/"); cat (cpath, ccap, seg[i]); }
	if (!cpath[0]) scpy (cpath, "/", ccap);
	scpy (real, g_users[g_user].home, rcap);
	if (slen (cpath) > 1)
	{
		if (real[slen (real) - 1] == '/') cat (real, rcap, cpath + 1);
		else cat (real, rcap, cpath);
	}
}

static int is_dir (const char *p) { void *d = kapi_opendir (p); if (d) { kapi_closedir (d); return 1; } return 0; }

// Open the data connection (after 150): the passive socket's peer, or PORT's address.
static int data_open (void)
{
	if (g_pasv >= 0)
	{
		char ip[40];
		int h = kapi_tcp_accept (g_pasv, ip, sizeof ip);
		kapi_tcp_close (g_pasv); g_pasv = -1;
		return h;
	}
	if (g_portPort > 0) { int h = kapi_tcp_connect (g_portIP, (unsigned) g_portPort); g_portPort = 0; return h; }
	return -1;
}

static int data_send_all (int h, const char *b, unsigned n)
{
	while (n > 0)
	{
		unsigned c = n > 4096 ? 4096 : n;
		int r = kapi_tcp_send (h, b, c);
		if (r <= 0) return 0;
		b += r; n -= (unsigned) r;
	}
	return 1;
}

// One directory listing line: LIST (ls -l style), NLST (names) or MLSD (facts).
static void list_line (char *out, int cap, const struct kapi_dirent *e, int mode)
{
	out[0] = '\0';
	if (mode == 1) { scpy (out, e->name, cap); cat (out, cap, "\r\n"); return; }
	if (mode == 2)
	{
		cat (out, cap, e->is_dir ? "type=dir;" : "type=file;size=");
		if (!e->is_dir) { catn (out, cap, e->size); cat (out, cap, ";"); }
		cat (out, cap, " "); cat (out, cap, e->name); cat (out, cap, "\r\n");
		return;
	}
	cat (out, cap, e->is_dir ? "drwxr-xr-x 1 onyx onyx " : "-rw-r--r-- 1 onyx onyx ");
	char sz[16] = ""; catn (sz, sizeof sz, e->size);
	for (int i = slen (sz); i < 12; i++) cat (out, cap, " ");
	cat (out, cap, sz);
	cat (out, cap, " Jan  1  2026 "); cat (out, cap, e->name); cat (out, cap, "\r\n");
}

static void cmd_list (const char *arg, int mode)
{
	char cp[256], real[400];
	while (*arg == '-') { while (*arg && *arg != ' ') arg++; while (*arg == ' ') arg++; }	// "-la"
	resolve (arg, cp, sizeof cp, real, sizeof real);
	void *d = kapi_opendir (real);
	if (!d) { reply ("550 No such directory."); return; }
	reply ("150 Here comes the directory listing.");
	int h = data_open ();
	if (h < 0) { kapi_closedir (d); reply ("425 Cannot open the data connection."); return; }
	struct kapi_dirent e;
	char line[256];
	while (kapi_readdir (d, &e))
	{
		if (e.name[0] == '.' && (e.name[1] == '\0' || (e.name[1] == '.' && e.name[2] == '\0'))) continue;
		list_line (line, sizeof line, &e, mode);
		if (!data_send_all (h, line, (unsigned) slen (line))) break;
	}
	kapi_closedir (d);
	kapi_tcp_close (h);
	reply ("226 Directory send OK.");
}

static void cmd_retr (const char *arg)
{
	char cp[256], real[400];
	resolve (arg, cp, sizeof cp, real, sizeof real);
	void *f = kapi_open (real);
	if (!f || is_dir (real)) { if (f) kapi_close (f); reply ("550 Cannot open the file."); return; }
	reply ("150 Opening BINARY mode data connection.");
	int h = data_open ();
	if (h < 0) { kapi_close (f); reply ("425 Cannot open the data connection."); return; }
	static char buf[8192];
	int ok = 1, n;
	while ((n = kapi_read (f, buf, sizeof buf)) > 0) if (!data_send_all (h, buf, (unsigned) n)) { ok = 0; break; }
	kapi_close (f);
	kapi_tcp_close (h);
	reply (ok ? "226 Transfer complete." : "426 Connection closed; transfer aborted.");
}

static void cmd_stor (const char *arg, int append)
{
	char cp[256], real[400];
	resolve (arg, cp, sizeof cp, real, sizeof real);
	reply ("150 Ok to send data.");
	int h = data_open ();
	if (h < 0) { reply ("425 Cannot open the data connection."); return; }
	unsigned cap = 256 * 1024, len = 0;
	char *buf = (char *) umm_malloc (cap);
	if (append && buf)					// APPE: start from the existing file
	{
		void *f = kapi_open (real);
		if (f)
		{
			int n;
			while (buf && (n = kapi_read (f, buf + len, cap - len)) > 0)
			{
				len += (unsigned) n;
				if (len == cap) { cap *= 2; buf = (char *) umm_realloc (buf, cap); }
			}
			kapi_close (f);
		}
	}
	int ok = buf != 0; unsigned idle = kapi_get_ticks ();
	while (ok)
	{
		if (len == cap)
		{
			if (cap >= UPLOAD_MAX) { ok = 0; break; }
			cap *= 2; buf = (char *) umm_realloc (buf, cap);
			if (!buf) { ok = 0; break; }
		}
		int r = kapi_tcp_recv (h, buf + len, cap - len);
		if (r < 0) break;					// the client closed: end of file
		if (r > 0) { len += (unsigned) r; idle = kapi_get_ticks (); }
		else
		{
			if (kapi_get_ticks () - idle > 3000) { ok = 0; break; }	// 30 s stall
			kapi_msleep (2);
		}
	}
	kapi_tcp_close (h);
	if (ok && kapi_save_file (real, buf, len) < 0) ok = 0;
	if (buf) umm_free (buf);
	reply (ok ? "226 Transfer complete." : "451 Upload failed (too big, stalled or not writable).");
}

// The client-visible path in quotes (257 replies).
static void reply_path (const char *code, const char *p, const char *tail)
{
	char b[400]; scpy (b, code, sizeof b); cat (b, sizeof b, " \""); cat (b, sizeof b, p);
	cat (b, sizeof b, "\" "); cat (b, sizeof b, tail); reply (b);
}

static void session_cmd (char *line)
{
	char *arg = line; while (*arg && *arg != ' ') arg++;
	if (*arg) *arg++ = '\0';
	while (*arg == ' ') arg++;
	for (char *p = line; *p; p++) *p = (char) ((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
	char cp[256], real[400], b[400];

	if (ieq (line, "USER")) { scpy (g_pendingUser, arg, sizeof g_pendingUser); g_user = -1; reply ("331 Please specify the password."); return; }
	if (ieq (line, "PASS"))
	{
		for (int i = 0; i < g_nusers; i++)
		{
			if (!ieq (g_users[i].name, g_pendingUser)) continue;
			const char *x = g_users[i].pass, *y = arg;	// exact (case-sensitive) match;
			while (*x && *x == *y) { x++; y++; }		// an empty password accepts any
			if (g_users[i].pass[0] != '\0' && (*x || *y)) break;
			g_user = i; scpy (g_cwd, "/", sizeof g_cwd);
			reply ("230 Login successful.");
			return;
		}
		reply ("530 Login incorrect.");
		return;
	}
	if (ieq (line, "QUIT")) { reply ("221 Goodbye."); kapi_tcp_close (g_ctl); kapi_exit (0); }
	if (ieq (line, "SYST")) { reply ("215 UNIX Type: L8"); return; }
	if (ieq (line, "FEAT")) { reply ("211-Features:\r\n PASV\r\n EPSV\r\n SIZE\r\n MLSD\r\n UTF8\r\n211 End"); return; }
	if (ieq (line, "OPTS")) { reply ("200 OK."); return; }
	if (ieq (line, "NOOP")) { reply ("200 NOOP ok."); return; }
	if (g_user < 0) { reply ("530 Please login with USER and PASS."); return; }

	if (ieq (line, "PWD") || ieq (line, "XPWD")) { reply_path ("257", g_cwd, "is the current directory"); return; }
	if (ieq (line, "CWD") || ieq (line, "XCWD") || ieq (line, "CDUP") || ieq (line, "XCUP"))
	{
		resolve (ieq (line, "CDUP") || ieq (line, "XCUP") ? ".." : arg, cp, sizeof cp, real, sizeof real);
		if (!is_dir (real)) { reply ("550 Failed to change directory."); return; }
		scpy (g_cwd, cp, sizeof g_cwd);
		reply ("250 Directory successfully changed.");
		return;
	}
	if (ieq (line, "TYPE") || ieq (line, "MODE") || ieq (line, "STRU")) { reply ("200 OK."); return; }
	if (ieq (line, "PASV") || ieq (line, "EPSV"))
	{
		static int next = 0;
		if (g_pasv >= 0) { kapi_tcp_close (g_pasv); g_pasv = -1; }
		int port = 0;
		for (int t = 0; t < 100 && g_pasv < 0; t++)
		{
			port = DATA_PORT0 + (next++ % 100);
			g_pasv = kapi_tcp_listen ((unsigned) port);
		}
		if (g_pasv < 0) { reply ("425 Cannot open a passive port."); return; }
		if (ieq (line, "EPSV"))
		{
			scpy (b, "229 Entering Extended Passive Mode (|||", sizeof b); catn (b, sizeof b, (unsigned) port); cat (b, sizeof b, "|)");
		}
		else
		{
			scpy (b, "227 Entering Passive Mode (", sizeof b);
			for (int i = 0; g_myIP[i]; i++) { char c[2] = { g_myIP[i] == '.' ? ',' : g_myIP[i], 0 }; cat (b, sizeof b, c); }
			cat (b, sizeof b, ","); catn (b, sizeof b, (unsigned) port / 256);
			cat (b, sizeof b, ","); catn (b, sizeof b, (unsigned) port % 256); cat (b, sizeof b, ")");
		}
		reply (b);
		return;
	}
	if (ieq (line, "PORT"))					// h1,h2,h3,h4,p1,p2
	{
		int v[6] = { 0 }, k = 0;
		for (char *p = arg; *p && k < 6; p++) { if (*p == ',') k++; else if (*p >= '0' && *p <= '9') v[k] = v[k] * 10 + (*p - '0'); }
		g_portIP[0] = '\0';
		for (int i = 0; i < 4; i++) { if (i) cat (g_portIP, sizeof g_portIP, "."); catn (g_portIP, sizeof g_portIP, (unsigned) v[i]); }
		g_portPort = v[4] * 256 + v[5];
		reply ("200 PORT command successful.");
		return;
	}
	if (ieq (line, "LIST")) { cmd_list (arg, 0); return; }
	if (ieq (line, "NLST")) { cmd_list (arg, 1); return; }
	if (ieq (line, "MLSD")) { cmd_list (arg, 2); return; }
	if (ieq (line, "RETR")) { cmd_retr (arg); return; }
	if (ieq (line, "STOR")) { cmd_stor (arg, 0); return; }
	if (ieq (line, "APPE")) { cmd_stor (arg, 1); return; }
	if (ieq (line, "SIZE"))
	{
		resolve (arg, cp, sizeof cp, real, sizeof real);
		char dir[400]; scpy (dir, real, sizeof dir);
		int n = slen (dir); while (n > 0 && dir[n - 1] != '/') n--;
		const char *name = real + n;
		dir[n > 4 ? n - 1 : n] = '\0';
		void *d = kapi_opendir (dir); struct kapi_dirent e; int found = 0;
		while (d && kapi_readdir (d, &e)) if (!e.is_dir && ieq (e.name, name)) { found = 1; break; }
		if (d) kapi_closedir (d);
		if (!found) { reply ("550 Could not get the file size."); return; }
		scpy (b, "213 ", sizeof b); catn (b, sizeof b, e.size); reply (b);
		return;
	}
	if (ieq (line, "DELE"))
	{
		resolve (arg, cp, sizeof cp, real, sizeof real);
		reply (!is_dir (real) && kapi_remove (real) == 0 ? "250 Delete operation successful." : "550 Delete operation failed.");
		return;
	}
	if (ieq (line, "MKD") || ieq (line, "XMKD"))
	{
		resolve (arg, cp, sizeof cp, real, sizeof real);
		if (kapi_mkdir (real) == 0) reply_path ("257", cp, "created"); else reply ("550 Create directory operation failed.");
		return;
	}
	if (ieq (line, "RMD") || ieq (line, "XRMD"))
	{
		resolve (arg, cp, sizeof cp, real, sizeof real);
		reply (is_dir (real) && kapi_remove (real) == 0 ? "250 Remove directory operation successful." : "550 Remove directory operation failed (not empty?).");
		return;
	}
	if (ieq (line, "RNFR")) { resolve (arg, cp, sizeof cp, g_rnfr, sizeof g_rnfr); reply ("350 Ready for RNTO."); return; }
	if (ieq (line, "RNTO"))
	{
		resolve (arg, cp, sizeof cp, real, sizeof real);
		reply (g_rnfr[0] && kapi_rename (g_rnfr, real) == 0 ? "250 Rename successful." : "550 Rename failed.");
		g_rnfr[0] = '\0';
		return;
	}
	if (ieq (line, "ABOR")) { reply ("226 No transfer to abort."); return; }
	reply ("502 Command not implemented.");
}

static int session (char *a)				// a = "<handle> <users record>"
{
	int h = 0; while (*a >= '0' && *a <= '9') h = h * 10 + (*a++ - '0');
	while (*a == ' ') a++;
	while (*a)						// user SEP pass SEP home RS ...
	{
		char f[3][128]; int k = 0, n = 0;
		for (; *a && *a != REC_SEP; a++)
		{
			if (*a == FIELD_SEP) { f[k][n] = '\0'; if (k < 2) k++; n = 0; }
			else if (n < 127) f[k][n++] = *a;
		}
		f[k][n] = '\0';
		if (*a == REC_SEP) a++;
		if (k == 2) add_user (f[0], f[1], f[2]);
	}
	g_ctl = h;
	kapi_net_status (g_myIP, sizeof g_myIP);
	reply ("220 Onyx FTP server ready.");
	char line[512]; int len = 0; unsigned idle = kapi_get_ticks ();
	for (;;)
	{
		char c;
		int r = kapi_tcp_recv (g_ctl, &c, 1);
		if (r < 0) break;					// the client hung up
		if (r == 0)
		{
			if (kapi_get_ticks () - idle > 30000) { reply ("421 Timeout."); break; }	// 5 min idle
			kapi_msleep (5);
			continue;
		}
		idle = kapi_get_ticks ();
		if (c == '\n')
		{
			if (len && line[len - 1] == '\r') len--;
			line[len] = '\0';
			if (len) session_cmd (line);
			len = 0;
		}
		else if (len < (int) sizeof line - 1) line[len++] = c;
	}
	if (g_pasv >= 0) kapi_tcp_close (g_pasv);
	kapi_tcp_close (g_ctl);
	return 0;
}

// =========================================================================================
// Main instance: user table + accept loop
// =========================================================================================
static void drain_mailbox (void)
{
	int from = 0, type = 0, n;
	static char buf[520];
	while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 0)) >= 0)
	{
		if (type != MSG_ADDUSER) continue;
		buf[n] = '\0';
		const char *u = buf, *p = u + slen (u) + 1, *hm = p + slen (p) + 1;
		if (hm < buf + n) add_user (u, p, hm);
	}
}

int main (void)
{
	static char args[1024];
	kapi_get_args (args, sizeof args);
	if (args[0] == '-' && args[1] == '-' && args[2] == 's')	// "--session <h> <users>"
	{
		char *a = args; while (*a && *a != ' ') a++;
		while (*a == ' ') a++;
		return session (a);
	}

	char f[3][128] = { "SD:/", "onyx", "onyx" };		// [homedir] [user] [password]
	int k = 0;
	for (char *p = args; *p && k < 3; )
	{
		while (*p == ' ') p++;
		if (!*p) break;
		int n = 0;
		while (*p && *p != ' ') { if (n < 127) f[k][n++] = *p; p++; }
		f[k][n] = '\0'; k++;
	}
	if (!kapi_ipc_register (SERVICE))				// a server already runs: add the user
	{
		int pid = kapi_ipc_lookup (SERVICE);
		char msg[400]; int n = 0;
		const char *part[3] = { f[1], f[2], f[0] };		// "user\0pass\0home\0"
		for (int j = 0; j < 3; j++) { for (int i = 0; part[j][i] && n < 396; i++) msg[n++] = part[j][i]; msg[n++] = '\0'; }
		if (pid && kapi_mailbox_send (pid, MSG_ADDUSER, msg, (unsigned) n))
		{ ax_puts ("ftpc: the running server now accepts user "); ax_puts (f[1]); ax_puts (" (root "); ax_puts (f[0]); ax_putln (")"); return 0; }
		ax_putln ("ftpc: cannot reach the running server");
		return 1;
	}
	add_user (f[1], f[2], f[0]);
	int ls = -1;
	for (int t = 0; t < 30 && ls < 0; t++)			// the network may still be coming up
	{
		if (kapi_net_status (0, 0)) ls = kapi_tcp_listen (21);
		if (ls < 0) kapi_msleep (2000);
	}
	if (ls < 0) { ax_putln ("ftpc: cannot listen on port 21 (network down?)"); return 1; }
	char ip[40] = ""; kapi_net_status (ip, sizeof ip);
	ax_puts ("ftpc: FTP server on "); ax_puts (ip); ax_puts (":21 -- user "); ax_puts (f[1]);
	ax_puts (", root "); ax_putln (f[0]);

	static void *kids[32]; int nkids = 0;
	for (;;)
	{
		char peer[40];
		int h = kapi_tcp_accept (ls, peer, sizeof peer);	// blocks until a client connects
		drain_mailbox ();					// users added meanwhile count now
		for (int i = 0; i < nkids; )				// reap finished sessions
			if (kapi_proc_done (kids[i])) { kapi_wait (kids[i]); kids[i] = kids[--nkids]; }
			else i++;
		if (h < 0) { kapi_msleep (100); continue; }
		static char sargs[1024];
		scpy (sargs, "--session ", sizeof sargs); catn (sargs, sizeof sargs, (unsigned) h); cat (sargs, sizeof sargs, " ");
		for (int i = 0; i < g_nusers; i++)
		{
			char sep[2] = { FIELD_SEP, 0 }, rs[2] = { REC_SEP, 0 };
			cat (sargs, sizeof sargs, g_users[i].name); cat (sargs, sizeof sargs, sep);
			cat (sargs, sizeof sargs, g_users[i].pass); cat (sargs, sizeof sargs, sep);
			cat (sargs, sizeof sargs, g_users[i].home); cat (sargs, sizeof sargs, rs);
		}
		void *kid = kapi_spawn ("SD:/bin/ftpc", sargs, 0, 0);
		if (kid && nkids < 32) kids[nkids++] = kid;
		else if (!kid) { kapi_tcp_send (h, "421 Server busy.\r\n", 18); kapi_tcp_close (h); }
	}
}
