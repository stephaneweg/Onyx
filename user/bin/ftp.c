//
// ftp -- interactive command-line FTP / FTPS client (like the classic ftp command).
//
//   ftp [host [port]]            then commands at the "ftp>" prompt:
//
//   open [-s] host [port]        connect (-s = FTPS: TLS); asks for user + password
//   user name [password]         log in again as another user
//   ls / dir [path]              list (dir: with sizes)       pwd       cd path     cdup
//   get remote [local]           download (to the local folder, see lcd)
//   put local [remote]           upload                       mget / mput: several names
//   delete name   mkdir name   rmdir name   rename from to    size name
//   lcd [dir]     lpwd           local folder (the shell's current directory)
//   close   bye / quit / exit    help / ?
//
// It works through /bin/ftpfs (the FTP: / FTPS: file-system provider, ABI v44): the login
// is handed to ftpfs over IPC (ftpfs.h), then every command is an ordinary file operation
// on an FTP[S]:host[:port]/path path -- so it shares ftpfs's connection, its FTPS support
// and the logins with the File Viewer. The password is typed in clear (the terminal echoes).
//
#include "kapi.h"
#include "applib.h"
#include "umm.h"
#include "ftpfs.h"

static char g_base[160] = "";		// "FTP:host" / "FTPS:host:990" ("" = not connected)
static char g_host[100] = "";
static char g_cwd[256] = "/";

// ---- small helpers ------------------------------------------------------------------------
static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static void cat (char *d, int cap, const char *s) { int n = slen (d); for (int i = 0; s[i] && n < cap - 1; i++) d[n++] = s[i]; d[n] = '\0'; }
static int eq (const char *a, const char *b) { return ax_streq (a, b); }
static void outn (unsigned v) { char b[16]; b[ax_itoa ((int) v, b)] = '\0'; ax_puts (b); }

static int read_line (char *buf, int cap)	// from stdin; -1 at EOF
{
	int n = 0;
	for (;;)
	{
		char c;
		if (kapi_stdin_read (&c, 1) <= 0) return n ? n : -1;
		if (c == 4) return n ? n : -1;			// Ctrl-D
		if (c == '\r') continue;
		if (c == '\n') break;
		if (n < cap - 1) buf[n++] = c;
	}
	buf[n] = '\0';
	return n;
}

// Remote path (absolute or relative to g_cwd, "." / ".." folded) -> FTP:host/... in out.
static void remote (const char *arg, char *out, int cap)
{
	char tmp[512];
	if (arg[0] == '/') scpy (tmp, arg, sizeof tmp);
	else { scpy (tmp, g_cwd, sizeof tmp); if (slen (tmp) > 1) cat (tmp, sizeof tmp, "/"); cat (tmp, sizeof tmp, arg); }
	char seg[32][64]; int ns = 0;
	for (char *p = tmp; *p; )
	{
		while (*p == '/') p++;
		if (!*p) break;
		char s[64]; int k = 0;
		while (*p && *p != '/') { if (k < 63) s[k++] = *p; p++; }
		s[k] = '\0';
		if (eq (s, ".")) continue;
		if (eq (s, "..")) { if (ns) ns--; continue; }
		if (ns < 32) scpy (seg[ns++], s, 64);
	}
	scpy (out, g_base, cap);
	if (!ns) cat (out, cap, "/");
	for (int i = 0; i < ns; i++) { cat (out, cap, "/"); cat (out, cap, seg[i]); }
}
static void remote_rel (const char *full, char *rel, int cap)	// FTP:host/a/b -> /a/b
{
	const char *p = full + slen (g_base);
	scpy (rel, *p ? p : "/", cap);
}
static const char *base_name (const char *p) { const char *n = p; for (; *p; p++) if (*p == '/') n = p + 1; return n; }

static int connected (void) { if (!g_base[0]) { ax_putln ("Not connected."); return 0; } return 1; }

// ---- commands ------------------------------------------------------------------------------
static void cmd_open (char **a, int n)
{
	int tls = 0, i = 1;
	if (i < n && eq (a[i], "-s")) { tls = 1; i++; }
	if (i >= n) { ax_putln ("usage: open [-s] host [port]"); return; }
	const char *host = a[i], *port = i + 1 < n ? a[i + 1] : 0;
	char user[64], pass[64];
	ax_puts ("User ("); ax_puts (host); ax_puts ("): ");
	if (read_line (user, sizeof user) < 0) return;
	pass[0] = '\0';
	if (user[0]) { ax_puts ("Password: "); if (read_line (pass, sizeof pass) < 0) return; }
	if (user[0] && !ftpfs_login (host, user, pass)) { ax_putln ("ftp: cannot start /bin/ftpfs"); return; }
	char base[160];
	scpy (base, tls ? "FTPS:" : "FTP:", sizeof base); cat (base, sizeof base, host);
	if (port && !eq (port, "21")) { cat (base, sizeof base, ":"); cat (base, sizeof base, port); }
	char root[170]; scpy (root, base, sizeof root); cat (root, sizeof root, "/");
	ax_puts ("Connecting to "); ax_puts (host); ax_putln (tls ? " (FTPS)..." : "...");
	void *d = kapi_opendir (root);
	if (!d) { ax_putln ("ftp: connection or login failed."); return; }
	kapi_closedir (d);
	scpy (g_base, base, sizeof g_base); scpy (g_host, host, sizeof g_host); scpy (g_cwd, "/", sizeof g_cwd);
	ax_puts ("Connected to "); ax_puts (host); ax_putln (user[0] ? "." : " (anonymous).");
}

static void cmd_user (char **a, int n)
{
	if (!connected ()) return;
	if (n < 2) { ax_putln ("usage: user name [password]"); return; }
	char pass[64];
	if (n >= 3) scpy (pass, a[2], sizeof pass);
	else { ax_puts ("Password: "); if (read_line (pass, sizeof pass) < 0) return; }
	ftpfs_login (g_host, a[1], pass);
	char root[170]; remote ("/", root, sizeof root);
	void *d = kapi_opendir (root);
	if (!d) { ax_putln ("Login failed."); return; }
	kapi_closedir (d);
	ax_putln ("Logged in.");
}

static void cmd_ls (const char *arg, int longfmt)
{
	if (!connected ()) return;
	char p[300]; remote (arg ? arg : ".", p, sizeof p);
	void *d = kapi_opendir (p);
	if (!d) { ax_putln ("ls: cannot list that folder."); return; }
	struct kapi_dirent e; int count = 0;
	while (kapi_readdir (d, &e))
	{
		if (longfmt)
		{
			ax_puts (e.is_dir ? "d  " : "-  ");
			char b[16]; int k = e.is_dir ? 0 : ax_itoa ((int) e.size, b); b[k] = '\0';
			for (int i = k; i < 11; i++) ax_puts (" ");
			ax_puts (b); ax_puts ("  ");
		}
		ax_puts (e.name); ax_putln (e.is_dir && !longfmt ? "/" : "");
		count++;
	}
	kapi_closedir (d);
	if (count == 0) ax_putln ("(empty)");
}

static void cmd_cd (const char *arg)
{
	if (!connected ()) return;
	char p[300]; remote (arg, p, sizeof p);
	void *d = kapi_opendir (p);
	if (!d) { ax_puts ("cd: no such folder: "); ax_putln (arg); return; }
	kapi_closedir (d);
	remote_rel (p, g_cwd, sizeof g_cwd);
	ax_puts ("Now in "); ax_putln (g_cwd);
}

static void cmd_get (const char *rem, const char *loc)
{
	if (!connected ()) return;
	char p[300]; remote (rem, p, sizeof p);
	if (!loc) loc = base_name (rem);
	unsigned t0 = kapi_get_ticks ();
	void *f = kapi_open (p);					// (ftpfs downloads it whole)
	if (!f) { ax_puts ("get: cannot download "); ax_putln (rem); return; }
	unsigned size = kapi_fsize (f);
	char *buf = (char *) umm_malloc (size ? size : 1);
	int n = buf ? kapi_read (f, buf, size) : -1;
	kapi_close (f);
	if (n < 0 || (unsigned) n != size) { ax_putln ("get: read failed"); if (buf) umm_free (buf); return; }
	int w = kapi_save_file (loc, buf, size);
	umm_free (buf);
	if (w < 0) { ax_puts ("get: cannot write "); ax_putln (loc); return; }
	unsigned ms = (kapi_get_ticks () - t0) * 10;
	ax_puts (loc); ax_puts (": "); outn (size); ax_puts (" bytes in "); outn (ms); ax_puts (" ms");
	if (ms) { ax_puts (" ("); outn (size / ms); ax_puts (" KB/s)"); }
	ax_putln ("");
}

static void cmd_put (const char *loc, const char *rem)
{
	if (!connected ()) return;
	if (!rem) rem = base_name (loc);
	char p[300]; remote (rem, p, sizeof p);
	unsigned t0 = kapi_get_ticks ();
	void *f = kapi_open (loc);
	if (!f) { ax_puts ("put: cannot open "); ax_putln (loc); return; }
	unsigned size = kapi_fsize (f);
	char *buf = (char *) umm_malloc (size ? size : 1);
	int n = buf ? kapi_read (f, buf, size) : -1;
	kapi_close (f);
	if (n < 0 || (unsigned) n != size) { ax_putln ("put: read failed"); if (buf) umm_free (buf); return; }
	int w = kapi_save_file (p, buf, size);				// (ftpfs uploads it)
	umm_free (buf);
	if (w < 0) { ax_puts ("put: upload failed: "); ax_putln (rem); return; }
	unsigned ms = (kapi_get_ticks () - t0) * 10;
	ax_puts (rem); ax_puts (": "); outn (size); ax_puts (" bytes in "); outn (ms); ax_puts (" ms");
	if (ms) { ax_puts (" ("); outn (size / ms); ax_puts (" KB/s)"); }
	ax_putln ("");
}

static void cmd_size (const char *rem)
{
	if (!connected ()) return;
	char p[300], dir[300]; remote (rem, p, sizeof p);
	scpy (dir, p, sizeof dir);
	int k = slen (dir); while (k > 0 && dir[k - 1] != '/') k--;
	const char *name = p + k;
	dir[k] = '\0';
	void *d = kapi_opendir (dir);
	struct kapi_dirent e;
	while (d && kapi_readdir (d, &e))
		if (ax_streq (e.name, name)) { kapi_closedir (d); ax_puts (name); ax_puts (": "); outn (e.size); ax_putln (" bytes"); return; }
	if (d) kapi_closedir (d);
	ax_puts ("size: no such file: "); ax_putln (rem);
}

static void simple (int ok, const char *what, const char *name)
{
	ax_puts (what); ax_puts (ok ? " ok: " : " failed: "); ax_putln (name);
}

static void help (void)
{
	ax_putln ("open [-s] host [port]   user name [pass]   close   bye / quit");
	ax_putln ("ls [path]   dir [path]   cd path   cdup   pwd");
	ax_putln ("get remote [local]   put local [remote]   mget names...   mput names...");
	ax_putln ("delete name   mkdir name   rmdir name   rename from to   size name");
	ax_putln ("lcd [dir]   lpwd        (-s = FTPS, TLS)");
}

// Split a line into words (in place); returns the count.
static int split (char *line, char **w, int max)
{
	int n = 0;
	for (char *p = line; *p && n < max; )
	{
		while (*p == ' ' || *p == '\t') *p++ = '\0';
		if (!*p) break;
		w[n++] = p;
		while (*p && *p != ' ' && *p != '\t') p++;
	}
	return n;
}

int main (void)
{
	char args[128];
	kapi_get_args (args, sizeof args);
	char *w[16]; int n;
	if (args[0])					// ftp host [port]
	{
		char line[160] = "open ";
		cat (line, sizeof line, args);
		n = split (line, w, 16);
		cmd_open (w, n);
	}
	char line[256];
	for (;;)
	{
		ax_puts ("ftp> ");
		if (read_line (line, sizeof line) < 0) break;
		n = split (line, w, 16);
		if (n == 0) continue;
		const char *c = w[0];
		char p[300];
		if (eq (c, "bye") || eq (c, "quit") || eq (c, "exit")) break;
		else if (eq (c, "help") || eq (c, "?")) help ();
		else if (eq (c, "open")) cmd_open (w, n);
		else if (eq (c, "close") || eq (c, "disconnect")) { g_base[0] = '\0'; ax_putln ("Closed."); }
		else if (eq (c, "user")) cmd_user (w, n);
		else if (eq (c, "ls")) cmd_ls (n > 1 ? w[1] : 0, 0);
		else if (eq (c, "dir")) cmd_ls (n > 1 ? w[1] : 0, 1);
		else if (eq (c, "pwd")) { if (connected ()) { ax_puts (g_host); ax_puts (":"); ax_putln (g_cwd); } }
		else if (eq (c, "cd")) { if (n > 1) cmd_cd (w[1]); else ax_putln ("usage: cd path"); }
		else if (eq (c, "cdup")) cmd_cd ("..");
		else if (eq (c, "get") || eq (c, "recv")) { if (n > 1) cmd_get (w[1], n > 2 ? w[2] : 0); else ax_putln ("usage: get remote [local]"); }
		else if (eq (c, "put") || eq (c, "send")) { if (n > 1) cmd_put (w[1], n > 2 ? w[2] : 0); else ax_putln ("usage: put local [remote]"); }
		else if (eq (c, "mget")) { for (int i = 1; i < n; i++) cmd_get (w[i], 0); }
		else if (eq (c, "mput")) { for (int i = 1; i < n; i++) cmd_put (w[i], 0); }
		else if (eq (c, "size")) { if (n > 1) cmd_size (w[1]); }
		else if ((eq (c, "delete") || eq (c, "del") || eq (c, "rm")) && n > 1)
		{ if (connected ()) { remote (w[1], p, sizeof p); simple (kapi_remove (p) == 0, "delete", w[1]); } }
		else if ((eq (c, "mkdir") || eq (c, "md")) && n > 1)
		{ if (connected ()) { remote (w[1], p, sizeof p); simple (kapi_mkdir (p) == 0, "mkdir", w[1]); } }
		else if ((eq (c, "rmdir") || eq (c, "rd")) && n > 1)
		{ if (connected ()) { remote (w[1], p, sizeof p); simple (kapi_remove (p) == 0, "rmdir", w[1]); } }
		else if ((eq (c, "rename") || eq (c, "ren")) && n > 2)
		{
			if (connected ()) { char q[300]; remote (w[1], p, sizeof p); remote (w[2], q, sizeof q); simple (kapi_rename (p, q) == 0, "rename", w[1]); }
		}
		else if (eq (c, "lcd"))
		{
			if (n > 1 && !kapi_chdir (w[1])) { ax_puts ("lcd: no such folder: "); ax_putln (w[1]); }
			char cwd[256]; kapi_getcwd (cwd, sizeof cwd); ax_puts ("Local folder: "); ax_putln (cwd);
		}
		else if (eq (c, "lpwd")) { char cwd[256]; kapi_getcwd (cwd, sizeof cwd); ax_putln (cwd); }
		else if (eq (c, "binary") || eq (c, "bin") || eq (c, "ascii") || eq (c, "passive") || eq (c, "prompt"))
			ax_putln ("(always binary, passive)");
		else { ax_puts ("?Invalid command: "); ax_putln (c); }
	}
	return 0;
}
