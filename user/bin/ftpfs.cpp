//
// ftpfs -- FTP / FTPS client as a file system: serves the paths
//
//     FTP:[user[:password]@]host[:port]/path        plain FTP (port 21)
//     FTPS:[user[:password]@]host[:port]/path       FTP over TLS: explicit (AUTH TLS) on
//                                                   port 21, implicit on port 990
//
// to every app through the kernel's user-space file-system hook (ABI v44, kern/vfs.h):
// the File Viewer browses FTP:ftp.gnu.org/gnu like a folder, tinypad opens and saves
// FTP:... files, the Shelf keeps them. The kernel starts ftpfs by itself the first time
// such a path is used.
//
// Credentials: in the path, or registered once with
//     ftpfs login <host> <user> <password>
// (sent to the running ftpfs over IPC, or kept by this one if it becomes the daemon);
// otherwise "anonymous". TLS = mbedTLS (tls/onyx_tls.hpp): the data connections resume
// the control connection's session (servers such as vsftpd require it). Certificates
// are NOT verified yet (no CA bundle on the card), like httpsget.
//
// Files are downloaded whole on open (RETR, <= 64 MB) and served from memory; a save
// uploads the whole buffer (STOR). One control connection per server, kept open and
// re-established once if it dropped.
//
#include "tls/onyx_tls.hpp"
#include "kapi.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define SERVICE		"ftpfs"
#define MSG_LOGIN	1		// "host\0user\0pass\0"
#define MAXSRV		4
#define MAXCRED		8
#define MAXFILES	16
#define FILE_MAX	(64u * 1024 * 1024)

// ---- a connection: plain TCP or TLS -------------------------------------------------------
struct Link
{
	int sock; bool tls; onyx_tls::Session *s;
	char rx[4096]; int rxlen, rxpos;		// buffered (Circle drops what does not fit)
};

static void link_init (Link &l) { l.sock = -1; l.tls = false; l.s = 0; l.rxlen = l.rxpos = 0; }
static bool link_tls (Link &l, const char *host)
{
	l.s = new onyx_tls::Session;
	if (onyx_tls::start (*l.s, l.sock, host) != 0) { delete l.s; l.s = 0; return false; }
	l.tls = true; l.rxlen = l.rxpos = 0;
	return true;
}
static int link_send (Link &l, const void *b, int n)
{
	if (l.tls) return onyx_tls::send (*l.s, b, n);
	const char *p = (const char *) b; int done = 0;
	while (done < n) { int r = kapi_tcp_send (l.sock, p + done, (unsigned) (n - done)); if (r <= 0) return -1; done += r; }
	return done;
}
static int link_recv_raw (Link &l, void *b, int n)	// >0 / 0 nothing / <0 closed
{
	return l.tls ? onyx_tls::recv (*l.s, b, n) : kapi_tcp_recv (l.sock, b, (unsigned) n);
}
static void link_close (Link &l)
{
	if (l.tls && l.s) { onyx_tls::stop (*l.s); delete l.s; }
	if (l.sock >= 0) kapi_tcp_close (l.sock);
	link_init (l);
}
// One byte-stream read into buf (<= n), waiting up to timeout_ticks. <0 closed/timeout.
static int link_read (Link &l, char *buf, int n, unsigned timeout_ticks)
{
	unsigned t0 = kapi_get_ticks ();
	for (;;)
	{
		if (l.rxpos < l.rxlen)
		{
			int k = l.rxlen - l.rxpos; if (k > n) k = n;
			memcpy (buf, l.rx + l.rxpos, k); l.rxpos += k;
			return k;
		}
		int r = link_recv_raw (l, l.rx, sizeof l.rx);
		if (r > 0) { l.rxlen = r; l.rxpos = 0; continue; }
		if (r < 0) return -1;
		if (kapi_get_ticks () - t0 > timeout_ticks) return -2;
		kapi_msleep (2);
	}
}

// ---- servers + credentials ------------------------------------------------------------------
struct Cred { char host[128], user[64], pass[64]; };
static Cred g_cred[MAXCRED]; static int g_ncred = 0;

struct Server
{
	bool used, ftps, implicit;
	char host[128]; int port;
	char user[64], pass[64];
	Link ctl; bool up;
};
static Server g_srv[MAXSRV];

// Parse "FTP[S]:[user[:pass]@]host[:port]/path" -> the server fields + the remote path.
static bool parse (const char *p, Server &s, char *rpath, int rcap)
{
	memset (&s, 0, sizeof s);
	if (!strncasecmp (p, "FTPS:", 5)) { s.ftps = true; p += 5; }
	else if (!strncasecmp (p, "FTP:", 4)) p += 4;
	else return false;
	while (*p == '/') p++;
	const char *slash = strchr (p, '/'), *end = slash ? slash : p + strlen (p);
	const char *at = 0;
	for (const char *q = p; q < end; q++) if (*q == '@') at = q;
	if (at)
	{
		const char *colon = (const char *) memchr (p, ':', at - p);
		int ul = (int) ((colon ? colon : at) - p);
		snprintf (s.user, sizeof s.user, "%.*s", ul, p);
		if (colon) snprintf (s.pass, sizeof s.pass, "%.*s", (int) (at - colon - 1), colon + 1);
		p = at + 1;
	}
	const char *colon = (const char *) memchr (p, ':', end - p);
	snprintf (s.host, sizeof s.host, "%.*s", (int) ((colon ? colon : end) - p), p);
	s.port = colon ? atoi (colon + 1) : 21;
	if (s.port <= 0) s.port = 21;
	s.implicit = s.ftps && s.port == 990;
	if (!s.host[0]) return false;
	snprintf (rpath, rcap, "%s", slash ? slash : "/");
	if (!s.user[0])						// a registered login, else anonymous
	{
		for (int i = 0; i < g_ncred; i++)
			if (!strcasecmp (g_cred[i].host, s.host)) { strcpy (s.user, g_cred[i].user); strcpy (s.pass, g_cred[i].pass); break; }
		if (!s.user[0]) { strcpy (s.user, "anonymous"); strcpy (s.pass, "onyx@"); }
	}
	return true;
}

// ---- the FTP dialogue -------------------------------------------------------------------------
static char g_last[512];				// the last reply line

// Read one (possibly multi-line) reply; returns its code, or -1 if the link failed.
static int reply (Server &s)
{
	char line[512]; int n = 0, code = -1; bool multi = false;
	for (;;)
	{
		char c;
		int r = link_read (s.ctl, &c, 1, 3000);		// (from the buffer: no data lost)
		if (r <= 0) { s.up = false; return -1; }
		if (c == '\r') continue;
		if (c != '\n') { if (n < (int) sizeof line - 1) line[n++] = c; continue; }
		line[n] = '\0'; n = 0;
		int lc = (line[0] >= '0' && line[0] <= '9') ? atoi (line) : -1;
		if (code < 0 && lc >= 100) { code = lc; multi = line[3] == '-'; }
		snprintf (g_last, sizeof g_last, "%s", line);
		if (!multi || (lc == code && line[3] == ' ')) return code;
	}
}

static int cmd (Server &s, const char *fmt, const char *arg = 0)
{
	char b[600];
	int n = snprintf (b, sizeof b - 2, fmt, arg ? arg : "");
	b[n++] = '\r'; b[n++] = '\n';
	if (link_send (s.ctl, b, n) < 0) { s.up = false; return -1; }
	return reply (s);
}

static bool login (Server &s)
{
	link_init (s.ctl);
	s.ctl.sock = kapi_tcp_connect (s.host, (unsigned) s.port);
	if (s.ctl.sock < 0) return false;
	s.up = true;
	if (s.implicit && !link_tls (s.ctl, s.host)) { link_close (s.ctl); return false; }
	if (reply (s) != 220) { link_close (s.ctl); return false; }
	if (s.ftps && !s.implicit)
	{
		if (cmd (s, "AUTH TLS") != 234 || !link_tls (s.ctl, s.host)) { link_close (s.ctl); return false; }
	}
	int r = cmd (s, "USER %s", s.user);
	if (r == 331) r = cmd (s, "PASS %s", s.pass);
	if (r != 230) { link_close (s.ctl); return false; }
	cmd (s, "TYPE I");
	if (s.ftps) { cmd (s, "PBSZ 0"); cmd (s, "PROT P"); }
	return true;
}

// The live server for a path (connecting / reconnecting as needed).
static Server *server_for (const char *path, char *rpath, int rcap, bool fresh = false)
{
	Server want;
	if (!parse (path, want, rpath, rcap)) return 0;
	Server *s = 0, *freeS = 0;
	for (int i = 0; i < MAXSRV; i++)
	{
		if (g_srv[i].used && !strcasecmp (g_srv[i].host, want.host) && g_srv[i].port == want.port
		    && g_srv[i].ftps == want.ftps && !strcmp (g_srv[i].user, want.user)) { s = &g_srv[i]; break; }
		if (!g_srv[i].used && !freeS) freeS = &g_srv[i];
	}
	if (s && (fresh || !s->up)) { link_close (s->ctl); s->up = false; }
	if (!s)
	{
		if (!freeS) { freeS = &g_srv[0]; link_close (freeS->ctl); }	// evict the first
		s = freeS; *s = want; s->used = true; link_init (s->ctl); s->up = false;
	}
	if (!s->up && !login (*s)) { s->used = false; return 0; }
	return s;
}

// Open a passive data connection (EPSV, else PASV) -> connected Link (plain; TLS later).
static bool data_open (Server &s, Link &d)
{
	link_init (d);
	int port = -1;
	if (cmd (s, "EPSV") == 229)
	{
		const char *p = strstr (g_last, "|||");
		if (p) port = atoi (p + 3);
	}
	if (port <= 0 && cmd (s, "PASV") == 227)
	{
		const char *p = strchr (g_last, '(');
		int v[6] = { 0 };
		if (p && sscanf (p + 1, "%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
			port = v[4] * 256 + v[5];		// (the host part is ignored: NAT-safe)
	}
	if (port <= 0) return false;
	d.sock = kapi_tcp_connect (s.host, (unsigned) port);
	return d.sock >= 0;
}

// Transfer command on an open data link: sends it, expects 125/150, starts TLS if FTPS.
static bool data_begin (Server &s, Link &d, const char *fmt, const char *arg)
{
	int r = cmd (s, fmt, arg);
	if (r != 125 && r != 150) { link_close (d); return false; }
	if (s.ftps && !link_tls (d, s.host)) { link_close (d); reply (s); return false; }
	return true;
}

// Read the whole data stream into a growing malloc buffer.
static char *data_read_all (Link &d, unsigned *plen, unsigned cap_max)
{
	unsigned cap = 64 * 1024, len = 0;
	char *buf = (char *) malloc (cap);
	while (buf)
	{
		if (cap - len < 8192)
		{
			if (cap >= cap_max) { free (buf); return 0; }
			char *nb = (char *) realloc (buf, cap * 2);
			if (!nb) { free (buf); return 0; }
			buf = nb; cap *= 2;
		}
		int r = link_read (d, buf + len, (int) (cap - len), 3000);
		if (r < 0) break;					// closed = done (or 30 s stall)
		len += (unsigned) r;
	}
	if (buf) buf[len < cap ? len : cap - 1] = '\0';
	*plen = len;
	return buf;
}

// Case-insensitive substring search (newlib has no strcasestr).
static const char *ci_find (const char *hay, const char *needle)
{
	size_t n = strlen (needle);
	for (; *hay; hay++) if (!strncasecmp (hay, needle, n)) return hay;
	return 0;
}

// ---- directory listing: MLSD, else LIST (Unix / DOS formats) ---------------------------------
static int g_outLen, g_outCap; static unsigned char *g_out;
static void out_entry (const char *name, unsigned size, bool dir)
{
	int nl = (int) strlen (name);
	if (!nl || !strcmp (name, ".") || !strcmp (name, "..")) return;
	if (g_outLen + 6 + nl > g_outCap)
	{
		g_outCap = (g_outCap + 6 + nl) * 2;
		g_out = (unsigned char *) realloc (g_out, g_outCap);
	}
	unsigned char *p = g_out + g_outLen;
	p[0] = size; p[1] = size >> 8; p[2] = size >> 16; p[3] = size >> 24; p[4] = dir;
	memcpy (p + 5, name, nl + 1);
	g_outLen += 6 + nl;
}

static int parse_listing (char *txt, bool mlsd)
{
	int count = 0;
	for (char *line = strtok (txt, "\r\n"); line; line = strtok (0, "\r\n"))
	{
		if (mlsd)					// "type=dir;size=12; name"
		{
			char *sp = strchr (line, ' ');
			if (!sp) continue;
			*sp = '\0';
			if (ci_find (line, "type=cdir") || ci_find (line, "type=pdir")) continue;
			bool dir = ci_find (line, "type=dir;") != 0 || ci_find (line, "type=dir") == line;
			const char *sz = ci_find (line, "size=");
			out_entry (sp + 1, sz ? (unsigned) strtoul (sz + 5, 0, 10) : 0, dir);
			count++;
			continue;
		}
		if (line[0] == 'd' || line[0] == '-' || line[0] == 'l')	// Unix: 9th field = name
		{
			char *f = line; int field = 0; unsigned size = 0;
			while (*f && field < 8)
			{
				while (*f && *f != ' ') f++;
				while (*f == ' ') f++;
				field++;
				if (field == 4) size = (unsigned) strtoul (f, 0, 10);
			}
			if (!*f) continue;
			char *arrow = strstr (f, " -> "); if (arrow) *arrow = '\0';	// symlink
			out_entry (f, size, line[0] == 'd');
			count++;
		}
		else if (line[0] >= '0' && line[0] <= '9')		// DOS: "01-01-24  12:00PM  <DIR>  name"
		{
			char *f = line; int field = 0; bool dir = false; unsigned size = 0;
			while (*f && field < 3)
			{
				if (field == 2) { if (!strncmp (f, "<DIR>", 5)) dir = true; else size = (unsigned) strtoul (f, 0, 10); }
				while (*f && *f != ' ') f++;
				while (*f == ' ') f++;
				field++;
			}
			if (*f) { out_entry (f, size, dir); count++; }
		}
	}
	return count;
}

static int op_list (const char *path)
{
	char rp[300];
	for (int attempt = 0; attempt < 2; attempt++)
	{
		Server *s = server_for (path, rp, sizeof rp, attempt > 0);
		if (!s) return -1;
		for (int mlsd = 1; mlsd >= 0; mlsd--)
		{
			Link d;
			if (!data_open (*s, d)) break;
			if (!data_begin (*s, d, mlsd ? "MLSD %s" : "LIST %s", rp)) { if (!s->up) break; continue; }
			unsigned len = 0;
			char *txt = data_read_all (d, &len, 8u * 1024 * 1024);
			link_close (d);
			int end = reply (*s);
			if (!txt) return -1;
			g_outLen = 0;
			int n = (end == 226 || end == 250) ? parse_listing (txt, mlsd != 0) : -1;
			free (txt);
			return n;
		}
		if (s->up) return -1;					// a real error, not a dropped link
	}
	return -1;
}

// ---- open files (downloaded whole) -----------------------------------------------------------
struct OpenFile { bool used; char *data; unsigned len; };
static OpenFile g_files[MAXFILES];

static int op_open (const char *path, unsigned *psize)
{
	char rp[300];
	int fid = -1;
	for (int i = 0; i < MAXFILES; i++) if (!g_files[i].used) { fid = i; break; }
	if (fid < 0) return -1;
	for (int attempt = 0; attempt < 2; attempt++)
	{
		Server *s = server_for (path, rp, sizeof rp, attempt > 0);
		if (!s) return -1;
		Link d;
		if (!data_open (*s, d)) { if (s->up) return -1; continue; }
		if (!data_begin (*s, d, "RETR %s", rp)) { if (s->up) return -1; continue; }
		unsigned len = 0;
		char *data = data_read_all (d, &len, FILE_MAX);
		link_close (d);
		int end = reply (*s);
		if (!data || (end != 226 && end != 250)) { free (data); return -1; }
		g_files[fid].used = true; g_files[fid].data = data; g_files[fid].len = len;
		*psize = len;
		return fid;
	}
	return -1;
}

static int op_save (const struct kapi_vfs_req &q)
{
	char rp[300];
	for (int attempt = 0; attempt < 2; attempt++)
	{
		Server *s = server_for (q.path, rp, sizeof rp, attempt > 0);
		if (!s) return -1;
		Link d;
		if (!data_open (*s, d)) { if (s->up) return -1; continue; }
		if (!data_begin (*s, d, "STOR %s", rp)) { if (s->up) return -1; continue; }
		static char chunk[32 * 1024];
		unsigned off = 0; bool ok = true;
		while (off < q.in_len)
		{
			int n = kapi_vfs_req_data (q.id, chunk, sizeof chunk, off);
			if (n <= 0 || link_send (d, chunk, n) < 0) { ok = false; break; }
			off += (unsigned) n;
		}
		link_close (d);
		int end = reply (*s);
		return ok && (end == 226 || end == 250) ? (int) off : -1;
	}
	return -1;
}

// A simple command on the server of `path` (MKD / DELE / RMD / RNFR + RNTO); retried once
// on a dropped link. Returns 0 / -1.
static int op_simple (int op, const char *path, const char *path2)
{
	char rp[300], rp2[300];
	for (int attempt = 0; attempt < 2; attempt++)
	{
		Server *s = server_for (path, rp, sizeof rp, attempt > 0);
		if (!s) return -1;
		int r = -1;
		if (op == VFS_OP_MKDIR) r = cmd (*s, "MKD %s", rp) == 257 ? 0 : -1;
		else if (op == VFS_OP_REMOVE)
		{
			int c = cmd (*s, "DELE %s", rp);
			if (c != 250 && s->up) c = cmd (*s, "RMD %s", rp);
			r = c == 250 ? 0 : -1;
		}
		else if (op == VFS_OP_RENAME)
		{
			Server tmp;
			if (!parse (path2, tmp, rp2, sizeof rp2)) return -1;
			if (cmd (*s, "RNFR %s", rp) == 350) r = cmd (*s, "RNTO %s", rp2) == 250 ? 0 : -1;
		}
		if (s->up) return r;
	}
	return -1;
}

static void add_cred (const char *host, const char *user, const char *pass)
{
	int i = 0;
	while (i < g_ncred && strcasecmp (g_cred[i].host, host)) i++;
	if (i == g_ncred) { if (g_ncred >= MAXCRED) return; g_ncred++; }
	snprintf (g_cred[i].host, sizeof g_cred[i].host, "%s", host);
	snprintf (g_cred[i].user, sizeof g_cred[i].user, "%s", user);
	snprintf (g_cred[i].pass, sizeof g_cred[i].pass, "%s", pass);
	for (int k = 0; k < MAXSRV; k++)				// new credentials: log in again
		if (g_srv[k].used && !strcasecmp (g_srv[k].host, host)) { link_close (g_srv[k].ctl); g_srv[k].used = false; }
}

// Logins sent over IPC (`ftpfs login ...`, the File Viewer's Connect dialog).
static void drain_logins (void)
{
	int from, type, n;
	static char mb[520];
	while ((n = kapi_mailbox_recv (&from, &type, mb, sizeof mb - 1, 0)) >= 0)
	{
		if (type != MSG_LOGIN) continue;
		mb[n] = '\0';
		const char *h = mb, *u = h + strlen (h) + 1, *p = u + strlen (u) + 1;
		if (p < mb + n) add_cred (h, u, p);
	}
}

int main (void)
{
	static char args[512];
	kapi_get_args (args, sizeof args);
	char *w[4] = { 0 }; int nw = 0;
	for (char *p = strtok (args, " "); p && nw < 4; p = strtok (0, " ")) w[nw++] = p;
	bool isLogin = nw >= 1 && !strcmp (w[0], "login");
	if (isLogin && nw < 4) { printf ("usage: ftpfs login <host> <user> <password>\n"); return 1; }

	if (!kapi_ipc_register (SERVICE))			// a daemon already runs
	{
		if (!isLogin) { printf ("ftpfs: already running\n"); return 0; }
		char msg[300]; int n = snprintf (msg, sizeof msg, "%s%c%s%c%s", w[1], 0, w[2], 0, w[3]) + 1;
		int pid = kapi_ipc_lookup (SERVICE);
		if (pid && kapi_mailbox_send (pid, MSG_LOGIN, msg, (unsigned) n)) { printf ("ftpfs: login for %s registered\n", w[1]); return 0; }
		printf ("ftpfs: cannot reach the running ftpfs\n");
		return 1;
	}
	if (isLogin) { add_cred (w[1], w[2], w[3]); printf ("ftpfs: login for %s registered\n", w[1]); }
	if (!kapi_vfs_register ("FTP:") || !kapi_vfs_register ("FTPS:")) { printf ("ftpfs: FTP: is served by another process\n"); return 1; }

	g_outCap = 64 * 1024; g_out = (unsigned char *) malloc (g_outCap);
	for (;;)
	{
		drain_logins ();
		struct kapi_vfs_req q;
		if (!kapi_vfs_next (&q, 1)) continue;			// (waits up to ~0.5 s)
		drain_logins ();		// a login sent just before this request must win
		switch (q.op)
		{
		case VFS_OP_OPEN:
		{
			unsigned size = 0;
			int fid = op_open (q.path, &size);
			kapi_vfs_reply (q.id, fid, fid >= 0 ? &size : 0, fid >= 0 ? 4 : 0);
			break;
		}
		case VFS_OP_READ:
		{
			int fid = (int) q.a0; unsigned off = (unsigned) q.a1, len = (unsigned) q.a2;
			if (fid < 0 || fid >= MAXFILES || !g_files[fid].used) { kapi_vfs_reply (q.id, -1, 0, 0); break; }
			OpenFile &f = g_files[fid];
			unsigned n2 = off >= f.len ? 0 : (f.len - off < len ? f.len - off : len);
			kapi_vfs_reply (q.id, (int) n2, f.data + off, n2);
			break;
		}
		case VFS_OP_CLOSE:
		{
			int fid = (int) q.a0;
			if (fid >= 0 && fid < MAXFILES && g_files[fid].used) { free (g_files[fid].data); g_files[fid].used = false; }
			kapi_vfs_reply (q.id, 0, 0, 0);
			break;
		}
		case VFS_OP_LIST:
		{
			g_outLen = 0;
			int cnt = op_list (q.path);
			kapi_vfs_reply (q.id, cnt, g_out, cnt >= 0 ? (unsigned) g_outLen : 0);
			break;
		}
		case VFS_OP_SAVE:
			kapi_vfs_reply (q.id, op_save (q), 0, 0);
			break;
		case VFS_OP_MKDIR: case VFS_OP_REMOVE: case VFS_OP_RENAME:
			kapi_vfs_reply (q.id, op_simple (q.op, q.path, q.path2), 0, 0);
			break;
		default:
			kapi_vfs_reply (q.id, -1, 0, 0);
		}
	}
}
