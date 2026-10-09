//
// apps/setup/system.h -- what the first-run wizard changes on the card and in the running system:
// SD:/etc/system.ini's keys (ntp, hostname), SD:/etc/wpa_supplicant.conf, SD:/cmdline.txt's size,
// SD:/etc/autostart (the services, the lines held back while the wizard runs), the processes. The
// language and the time zones (their summer time, system.ini's language=, timezone=, zone=) are
// SystemKit's: systemkit/locale.h. A newlib app: the C library's string functions.
//
#ifndef _setup_system_h
#define _setup_system_h

#include <string.h>
#include <stdio.h>
#include "appkit/appkit.h"

// ---- files --------------------------------------------------------------------------------------------------
// The whole file in buf (NUL-terminated) -> its length, -1 none.
static int file_read (const char *path, char *buf, int cap)
{
	void *f = kapi_open (path);
	if (!f) { buf[0] = 0; return -1; }
	int n = kapi_read (f, buf, cap - 1); kapi_close (f);
	if (n < 0) n = 0;
	buf[n] = 0;
	return n;
}
static bool file_write (const char *path, const char *buf, int n) { return kapi_save_file (path, buf, (unsigned) n) >= 0; }

// A line of text [s, e): its first word after blanks (and after a '#' when hash is set) is `word`?
static bool line_word (const char *s, const char *e, const char *word, bool hash)
{
	while (s < e && (*s == ' ' || *s == '\t')) s++;
	if (hash) { if (s >= e || *s != '#') return false; s++; while (s < e && (*s == ' ' || *s == '\t')) s++; }
	int n = (int) strlen (word);
	if (e - s < n || strncmp (s, word, n) != 0) return false;
	return s + n == e || s[n] == ' ' || s[n] == '\t' || s[n] == '\r' || s[n] == '=';
}

// "key = value" in an ini-like file (system.ini): the key's line replaced (its first one; the
// comments kept), else added at the end.
static bool ini_set (const char *path, const char *key, const char *value)
{
	static char in[8192], out[8400];
	int n = file_read (path, in, sizeof in); if (n < 0) n = 0;
	int o = 0; bool done = false;
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && in[i] != '\n') i++;
		int e = i; if (i < n) i++;
		if (!done && line_word (in + s, in + e, key, false))
		{
			o += snprintf (out + o, sizeof out - o, "%s=%s\n", key, value); done = true;
			continue;
		}
		if (o + (i - s) + 2 < (int) sizeof out) { memcpy (out + o, in + s, i - s); o += i - s; }
		if (i == n && e == n && n > 0 && in[n - 1] != '\n') out[o++] = '\n';
	}
	if (!done) o += snprintf (out + o, sizeof out - o, "%s=%s\n", key, value);
	return file_write (path, out, o);
}

// ---- Wi-Fi: SD:/etc/wpa_supplicant.conf (the Wi-Fi menu's format) -------------------------------------------
#define WPA_PATH	"SD:/etc/wpa_supplicant.conf"
#define MAXKNOWN	16
struct Known { char ssid[33], psk[64], keymgmt[24], proto[16]; int priority; };
static Known g_known[MAXKNOWN]; static int g_nknown;
static char g_wpaCountry[8] = "BE";

static bool line_kv (const char *p, const char *e, const char *key, char *out, int cap)
{
	while (p < e && (*p == ' ' || *p == '\t')) p++;
	int kl = (int) strlen (key);
	if (e - p <= kl || p[kl] != '=' || strncmp (p, key, kl)) return false;
	p += kl + 1;
	while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
	if (p < e && *p == '"') { p++; if (e > p && e[-1] == '"') e--; }
	int n = 0; while (p < e && n < cap - 1) out[n++] = *p++;
	out[n] = 0;
	return true;
}
static void wpa_load ()
{
	static char buf[4096];
	g_nknown = 0;
	if (file_read (WPA_PATH, buf, sizeof buf) <= 0) return;
	Known *cur = 0;
	for (const char *p = buf; *p; )
	{
		const char *e = p; while (*e && *e != '\n') e++;
		const char *q = p; while (q < e && (*q == ' ' || *q == '\t')) q++;
		char v[64];
		if (q < e && *q != '#')
		{
			if (!cur && !strncmp (q, "network={", 9) && g_nknown < MAXKNOWN)
			{ cur = &g_known[g_nknown++]; memset (cur, 0, sizeof *cur); strcpy (cur->keymgmt, "WPA-PSK"); }
			else if (cur && *q == '}') cur = 0;
			else if (cur)
			{
				if (line_kv (q, e, "ssid", v, sizeof v)) snprintf (cur->ssid, sizeof cur->ssid, "%s", v);
				else if (line_kv (q, e, "psk", v, sizeof v)) snprintf (cur->psk, sizeof cur->psk, "%s", v);
				else if (line_kv (q, e, "key_mgmt", v, sizeof v)) snprintf (cur->keymgmt, sizeof cur->keymgmt, "%s", v);
				else if (line_kv (q, e, "proto", v, sizeof v)) snprintf (cur->proto, sizeof cur->proto, "%s", v);
				else if (line_kv (q, e, "priority", v, sizeof v)) { int k = 0; sscanf (v, "%d", &k); cur->priority = k; }
			}
			else if (line_kv (q, e, "country", v, sizeof v)) snprintf (g_wpaCountry, sizeof g_wpaCountry, "%s", v);
		}
		p = *e ? e + 1 : e;
	}
	if (cur && !cur->ssid[0]) g_nknown--;
}
static Known *wpa_known (const char *ssid) { for (int i = 0; i < g_nknown; i++) if (!strcmp (g_known[i].ssid, ssid)) return &g_known[i]; return 0; }
static bool wpa_save ()
{
	static char out[4096]; int n = 0;
	n += snprintf (out + n, sizeof out - n,
		"#\n# wpa_supplicant.conf -- WLAN credentials for Onyx (read at boot, and again when a network is\n"
		"# joined from the Wi-Fi menu). The network with the highest priority is joined first.\n#\n"
		"# SECURITY: the passwords are stored in CLEAR TEXT. Keep this file on the SD card only;\n"
		"# do NOT commit it to a public repository. Managed by Setup, the Wi-Fi menu and Wi-Fi Settings.\n#\n\n"
		"country=%s\n", g_wpaCountry);
	for (int i = 0; i < g_nknown; i++)
	{
		const Known &k = g_known[i]; bool psk = strcmp (k.keymgmt, "NONE") != 0;
		n += snprintf (out + n, sizeof out - n, "\nnetwork={\n\tssid=\"%s\"\n", k.ssid);
		if (psk) n += snprintf (out + n, sizeof out - n, "\tpsk=\"%s\"\n", k.psk);
		if (psk && k.proto[0]) n += snprintf (out + n, sizeof out - n, "\tproto=%s\n", k.proto);
		n += snprintf (out + n, sizeof out - n, "\tkey_mgmt=%s\n", k.keymgmt);
		if (k.priority > 0) n += snprintf (out + n, sizeof out - n, "\tpriority=%d\n", k.priority);
		n += snprintf (out + n, sizeof out - n, "}\n");
		if (n >= (int) sizeof out - 1) return false;
	}
	return file_write (WPA_PATH, out, n);
}
// The network joined first from now on (its password when given), the country's code (the radio's
// channels) -> false: the file not written.
static bool wpa_join (const char *ssid, const char *psk, bool open, const char *country)
{
	wpa_load ();
	if (country && country[0]) snprintf (g_wpaCountry, sizeof g_wpaCountry, "%s", country);
	Known *k = wpa_known (ssid);
	if (!k && g_nknown < MAXKNOWN) { k = &g_known[g_nknown++]; memset (k, 0, sizeof *k); snprintf (k->ssid, sizeof k->ssid, "%s", ssid); }
	if (!k) return false;
	if (psk && psk[0]) snprintf (k->psk, sizeof k->psk, "%s", psk);
	snprintf (k->keymgmt, sizeof k->keymgmt, "%s", open ? "NONE" : "WPA-PSK");
	int top = 0; for (int i = 0; i < g_nknown; i++) if (g_known[i].priority > top) top = g_known[i].priority;
	k->priority = top + 1;
	return wpa_save ();
}

// ---- the screen: SD:/cmdline.txt's width= height= (Display's) ---------------------------------------------
static bool cmdline_size (int w, int h)
{
	static char in[1024], out[1100];
	file_read ("SD:/cmdline.txt", in, sizeof in);
	int o = snprintf (out, sizeof out, "width=%d height=%d", w, h);
	for (char *t = strtok (in, " \t\r\n"); t; t = strtok (0, " \t\r\n"))
		if (strncmp (t, "width=", 6) && strncmp (t, "height=", 7) && o + (int) strlen (t) + 2 < (int) sizeof out)
			o += snprintf (out + o, sizeof out - o, " %s", t);
	out[o++] = '\n';
	return file_write ("SD:/cmdline.txt", out, o);
}

// ---- SD:/etc/autostart and the sessions' files ------------------------------------------------------------------
// While the wizard runs, the lines it holds back (the menu bar, the dock...) are written
// "#setup: <line>"; at its end it gives them back, removes its own "run setup" line and the comment
// lines about it ("#setup# ..."), sets the keyboard's line and turns the remote services' lines on
// or off ("#telnetd" when off). Since the sessions (2026-10-08, SystemKit's session.h) the held-back lines,
// Setup's line and its comments are in the session's file (SD:/etc/session/desktop on a new card); the
// keyboard's and the services' lines stay in the autostart. Both are handled: each file's Setup lines.
#define AUTOSTART	"SD:/etc/autostart"
static const char *const SERVICES[4] = { "telnetd", "vncd", "rdpd", "ftpd" };

static void services_read (bool on[4])
{
	static char in[8192];
	for (int k = 0; k < 4; k++) on[k] = false;
	int n = file_read (AUTOSTART, in, sizeof in); if (n < 0) return;
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && in[i] != '\n') i++;
		int e = i; if (i < n) i++;
		for (int k = 0; k < 4; k++) if (line_word (in + s, in + e, SERVICES[k], false)) on[k] = true;
	}
}
// Setup's own lines of a line [ls, le): 1 its "run setup" line or a "#setup#" comment (dropped), 2 a held-back line
// (line + 8 is the line given back), 0 another.
static int setup_line (const char *ls, const char *le, const char *line)
{
	if (line_word (ls, le, "run", false))
	{
		const char *a = strstr (line, "run") + 3; while (*a == ' ' || *a == '\t') a++;
		if (!strncmp (a, "setup", 5) && (a[5] == 0 || a[5] == ' ' || a[5] == '\r')) return 1;	// (the wizard's own line)
	}
	if (!strncmp (line, "#setup# ", 8)) return 1;		// (the comment about the wizard)
	if (!strncmp (line, "#setup: ", 8)) return 2;
	return 0;
}

// A session's file (SD:/etc/session/<mode>): Setup's line and comments removed, the held-back lines given back --
// and, when run, added to held (the session running: they start now). A file without Setup's lines: not written.
static void session_finish (const char *path, bool run, char *held, int heldCap, int *h)
{
	static char in[8192], out[8600];
	int n = file_read (path, in, sizeof in); if (n <= 0) return;
	int o = 0; bool changed = false;
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && in[i] != '\n') i++;
		int e = i; if (i < n) i++;
		const char *ls = in + s, *le = in + e;
		while (le > ls && le[-1] == '\r') le--;
		char line[512]; int ll = (int) (le - ls) < 511 ? (int) (le - ls) : 511;
		memcpy (line, ls, ll); line[ll] = 0;
		int k = setup_line (ls, le, line);
		if (k == 1) { changed = true; continue; }
		if (k == 2)
		{
			changed = true;
			o += snprintf (out + o, sizeof out - o, "%s\n", line + 8);
			if (run) *h += snprintf (held + *h, heldCap - *h, "%s\n", line + 8);
			continue;
		}
		o += snprintf (out + o, sizeof out - o, "%s\n", line);
		if (o >= (int) sizeof out - 600) return;			// (too long: left as it is)
	}
	if (changed) file_write (path, out, o);
}

// The autostart as it will be -> the held-back lines to run now in held (one a line).
static bool autostart_finish (const char *keyb, const bool on[4], char *held, int heldCap)
{
	static char in[8192], out[8600];
	int n = file_read (AUTOSTART, in, sizeof in); if (n < 0) n = 0;
	int o = 0, h = 0; bool keybDone = false, seen[4] = { false, false, false, false };
	held[0] = 0;
	for (int m = 0; m < session_modes (); m++)		// the sessions' files (the one running: its lines started)
	{
		char path[64];
		if (session_file (m, path, sizeof path)) session_finish (path, m == session_mode (), held, heldCap, &h);
	}
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && in[i] != '\n') i++;
		int e = i; if (i < n) i++;
		const char *ls = in + s, *le = in + e;
		while (le > ls && le[-1] == '\r') le--;
		char line[512]; int ll = (int) (le - ls) < 511 ? (int) (le - ls) : 511;
		memcpy (line, ls, ll); line[ll] = 0;
		int sl = setup_line (ls, le, line);
		if (sl == 1) continue;				// (the wizard's own line, its comments)
		if (sl == 2)						// a held-back line: given back, and run now
		{
			o += snprintf (out + o, sizeof out - o, "%s\n", line + 8);
			h += snprintf (held + h, heldCap - h, "%s\n", line + 8);
			continue;
		}
		if (line_word (ls, le, "keyb", false) && keyb && keyb[0])
		{
			if (!keybDone) o += snprintf (out + o, sizeof out - o, "keyb %s\n", keyb);
			keybDone = true; continue;
		}
		int svc = -1;
		for (int k = 0; k < 4; k++) if (line_word (ls, le, SERVICES[k], false) || line_word (ls, le, SERVICES[k], true)) svc = k;
		if (svc >= 0)
		{
			const char *t = ls; while (t < le && (*t == ' ' || *t == '\t' || *t == '#')) t++;	// (the command itself)
			if (!seen[svc]) o += snprintf (out + o, sizeof out - o, "%s%.*s\n", on[svc] ? "" : "#", (int) (le - t), t);
			seen[svc] = true; continue;
		}
		o += snprintf (out + o, sizeof out - o, "%s\n", line);
		if (o >= (int) sizeof out - 600) break;
	}
	if (!keybDone && keyb && keyb[0]) o += snprintf (out + o, sizeof out - o, "keyb %s\n", keyb);
	for (int k = 0; k < 4; k++)
		if (on[k] && !seen[k]) o += snprintf (out + o, sizeof out - o, "%s\n", SERVICES[k]);
	return file_write (AUTOSTART, out, o);
}

// A line of the autostart run as init runs it: SD:/bin/<first word> <the rest> ("sleep" skipped).
static void run_line (const char *line)
{
	while (*line == ' ' || *line == '\t') line++;
	if (!*line || *line == '#') return;
	char cmd[64]; int k = 0;
	while (line[k] && line[k] != ' ' && line[k] != '\t' && k < 40) { cmd[k] = line[k]; k++; }
	cmd[k] = 0;
	if (!strcmp (cmd, "sleep")) return;
	const char *args = line + k; while (*args == ' ' || *args == '\t') args++;
	char path[80]; snprintf (path, sizeof path, "SD:bin/%s", cmd);
	kapi_exec (path, args);
}

// The processes named `name` asked to end (a service turned off).
static void stop_process (const char *name)
{
	static char b[4096];
	int n = kapi_list_procs (b, sizeof b); if (n <= 0) return;
	b[n < (int) sizeof b ? n : (int) sizeof b - 1] = 0;
	for (char *l = strtok (b, "\n"); l; l = strtok (0, "\n"))
	{
		int pid = 0, pages = 0; char kind[4], state[8], nm[48];	// ("<pid> <a|k> <state> <pages> <name>")
		if (sscanf (l, "%d %3s %7s %d %47s", &pid, kind, state, &pages, nm) == 5 && !strcmp (nm, name) && pid > 0) kapi_kill_pid (pid, 0);
	}
}

// Is `name` a process now?
static bool running (const char *name)
{
	static char b[4096];
	int n = kapi_list_procs (b, sizeof b); if (n <= 0) return false;
	b[n < (int) sizeof b ? n : (int) sizeof b - 1] = 0;
	for (char *l = strtok (b, "\n"); l; l = strtok (0, "\n"))
	{
		int pid = 0, pages = 0; char kind[4], state[8], nm[48];
		if (sscanf (l, "%d %3s %7s %d %47s", &pid, kind, state, &pages, nm) == 5 && !strcmp (nm, name)) return true;
	}
	return false;
}

#endif
