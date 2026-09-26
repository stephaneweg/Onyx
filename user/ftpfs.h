//
// ftpfs.h -- talk to /bin/ftpfs (the FTP: / FTPS: file-system provider) and read its list of
// remembered servers. Header-only, no libc (C and C++, freestanding apps and newlib tools).
//
//   ftpfs_login ("ftp.example.com", "me", "secret", remember)  -> 1 if delivered
//       hand a login over IPC (starting ftpfs if needed), so the password never has to be
//       written into an FTP: path. remember = also save it in SD:/etc/ftpfs.ini.
//   ftpfs_login_site (&site, remember)   same, with the port / FTPS / start folder too
//       (the File Viewer's Connect dialog: they pre-fill the form next time).
//   ftpfs_forget ("ftp.example.com")     drop a login (memory + the file).
//   ftpfs_load_sites (sites, max)        the remembered servers (the Connect dialog's list).
//
// SD:/etc/ftpfs.ini holds one line per server:
//     host user hexpass [port tls folder]        ("-" = empty user / password)
// hexpass = the password XOR a fixed key, in hex: OBFUSCATED, NOT ENCRYPTED.
// IPC: service "ftpfs"; message type 1 = "host\0user\0pass\0[port\0tls\0folder\0]",
// 2 = the same + remember, 3 = "host\0" forget (see user/bin/ftpfs.cpp).
//
#ifndef _ftpfs_h
#define _ftpfs_h

#include "kapi.h"

#define FTPFS_SITES_FILE	"SD:/etc/ftpfs.ini"
#define FTPFS_MAXSITES		16

struct ftpfs_site
{
	char host[128], user[64], pass[64], port[8], folder[128];
	int  tls;			// 1 = FTPS
};

static inline void ftpfs__cpy (char *d, const char *s, int cap)
{ int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

static const char ftpfs__key[] = "Onyx-ftpfs-v1";
static inline void ftpfs_obf_hex (const char *in, char *out, int cap)
{
	static const char H[] = "0123456789abcdef";
	int k = 0;
	for (int i = 0; in[i] && k < cap - 2; i++)
	{
		unsigned char c = (unsigned char) in[i] ^ (unsigned char) ftpfs__key[i % (int) (sizeof ftpfs__key - 1)];
		out[k++] = H[c >> 4]; out[k++] = H[c & 15];
	}
	out[k] = '\0';
}
static inline int ftpfs__hv (char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; }
static inline void ftpfs_unobf_hex (const char *in, char *out, int cap)
{
	int k = 0;
	for (int i = 0; in[i] && in[i + 1] && k < cap - 1; i += 2, k++)
		out[k] = (char) (((ftpfs__hv (in[i]) << 4) | ftpfs__hv (in[i + 1])) ^ ftpfs__key[k % (int) (sizeof ftpfs__key - 1)]);
	out[k] = '\0';
}

// Parse one file line; 1 if it is a server.
static inline int ftpfs_parse_site (const char *line, struct ftpfs_site *s)
{
	char w[6][140]; int nw = 0;
	const char *p = line;
	while (*p == ' ' || *p == '\t') p++;
	if (*p == '#' || *p == ';' || *p == '\0') return 0;
	while (*p && nw < 6)
	{
		int n = 0;
		if (nw == 5) { while (*p && n < 139) w[nw][n++] = *p++; while (n && (w[nw][n - 1] == ' ' || w[nw][n - 1] == '\t')) n--; }	// folder: the rest
		else while (*p && *p != ' ' && *p != '\t' && n < 139) w[nw][n++] = *p++;
		w[nw++][n] = '\0';
		while (*p == ' ' || *p == '\t') p++;
	}
	if (nw < 3) return 0;
	ftpfs__cpy (s->host, w[0], sizeof s->host);
	ftpfs__cpy (s->user, (w[1][0] == '-' && !w[1][1]) ? "" : w[1], sizeof s->user);
	if (w[2][0] == '-' && !w[2][1]) s->pass[0] = '\0'; else ftpfs_unobf_hex (w[2], s->pass, sizeof s->pass);
	ftpfs__cpy (s->port, nw > 3 ? w[3] : "", sizeof s->port);
	s->tls = nw > 4 && w[4][0] == '1';
	ftpfs__cpy (s->folder, nw > 5 ? w[5] : "", sizeof s->folder);
	return 1;
}

// Format one file line (with '\n'); returns its length.
static inline int ftpfs_format_site (const struct ftpfs_site *s, char *out, int cap)
{
	char hx[140];
	if (s->pass[0]) ftpfs_obf_hex (s->pass, hx, sizeof hx); else ftpfs__cpy (hx, "-", sizeof hx);
	const char *part[6] = { s->host, s->user[0] ? s->user : "-", hx, s->port[0] ? s->port : "21",
				s->tls ? "1" : "0", s->folder[0] ? s->folder : "/" };
	int n = 0;
	for (int k = 0; k < 6; k++)
	{
		if (k) { if (n < cap - 2) out[n++] = ' '; }
		for (int i = 0; part[k][i] && n < cap - 2; i++) out[n++] = part[k][i];
	}
	out[n++] = '\n'; out[n] = '\0';
	return n;
}

// The remembered servers, in file order. Returns how many.
static inline int ftpfs_load_sites (struct ftpfs_site *sites, int max)
{
	void *f = kapi_open (FTPFS_SITES_FILE);
	if (!f) return 0;
	static char buf[FTPFS_MAXSITES * 540 + 512];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return 0;
	buf[n] = '\0';
	int cnt = 0;
	char *line = buf;
	while (*line && cnt < max)
	{
		char *e = line; while (*e && *e != '\n' && *e != '\r') e++;
		char c = *e; *e = '\0';
		if (ftpfs_parse_site (line, &sites[cnt])) cnt++;
		if (!c) break;
		line = e + 1;
	}
	return cnt;
}

static inline int ftpfs__pid (void)
{
	int pid = kapi_ipc_lookup ("ftpfs");
	if (pid == 0)
	{
		kapi_exec ("SD:/bin/ftpfs", "");
		for (int i = 0; i < 300 && pid == 0; i++) { kapi_msleep (10); pid = kapi_ipc_lookup ("ftpfs"); }
	}
	return pid;
}

static inline int ftpfs__send (int type, const char *const *part, int nparts)
{
	int pid = ftpfs__pid ();
	if (pid == 0) return 0;
	char msg[512]; int n = 0;
	for (int k = 0; k < nparts; k++)
	{
		for (int i = 0; part[k][i] && n < (int) sizeof msg - 2; i++) msg[n++] = part[k][i];
		msg[n++] = '\0';
	}
	return kapi_mailbox_send (pid, type, msg, (unsigned) n) != 0;
}

static inline int ftpfs_login (const char *host, const char *user, const char *pass, int remember)
{
	const char *part[3] = { host, user, pass };
	return ftpfs__send (remember ? 2 : 1, part, 3);		// MSG_LOGIN(_SAVE)
}

static inline int ftpfs_login_site (const struct ftpfs_site *s, int remember)
{
	const char *part[6] = { s->host, s->user, s->pass, s->port, s->tls ? "1" : "0", s->folder };
	return ftpfs__send (remember ? 2 : 1, part, 6);
}

static inline int ftpfs_forget (const char *host)
{
	const char *part[1] = { host };
	return ftpfs__send (3, part, 1);			// MSG_FORGET
}

#endif
