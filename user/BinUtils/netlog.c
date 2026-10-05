//
// netlog -- the network's start written to SD:/netlog.txt, for a Pi without a screen: the
// kernel starts it before init when cmdline.txt has netlog=1 (or run it by hand). It checks
// SD:/etc/wpa_supplicant.conf (its psk is never written, only its length) and the Wi-Fi
// firmware, then keeps the kernel log (the net: lines, wpa_supplicant's, DHCP) as it comes,
// the file rewritten every 2 s. It stops 30 s after the link is up, or after 5 minutes; at the
// end the programs running (telnetd, vncd, rdpd, Setup...: started by init?), and when the link
// never came up, the access points around. Read the file on
// the PC (the card in a reader). It takes the kernel log's events: kmsg sees none meanwhile.
//   usage: netlog
//
#include "appkit/appkit.h"

#define OUT_PATH	"SD:/netlog.txt"
#define WPA_PATH	"SD:/etc/wpa_supplicant.conf"
#define HZ		100			// kapi_get_ticks: Circle's HZ ticks
#define RUN_MAX		(300 * HZ)		// 5 minutes at most
#define AFTER_UP	(30 * HZ)		// kept going this long once the link is up (the services listening)

static char g_head[6000]; static int g_hn;	// the checks (written once)
static char g_log[56000]; static int g_ln;	// the kernel log since netlog started
static char g_tail[4000]; static int g_tn;	// the result, the access points
static char g_out[66000];
static unsigned g_t0;

static void put (char *b, int cap, int *n, const char *s) { while (*s && *n < cap - 1) b[(*n)++] = *s++; b[*n] = 0; }
static void putn (char *b, int cap, int *n, int v) { char t[16]; ax_itoa (v, t); put (b, cap, n, t); }
#define H(s)  put (g_head, sizeof g_head, &g_hn, s)
#define HN(v) putn (g_head, sizeof g_head, &g_hn, v)
#define T(s)  put (g_tail, sizeof g_tail, &g_tn, s)
#define TN(v) putn (g_tail, sizeof g_tail, &g_tn, v)

static void stamp (char *b, int cap, int *n)
{
	unsigned t = kapi_get_ticks () - g_t0;
	char s[8]; put (b, cap, n, "[");
	putn (b, cap, n, (int) (t / HZ)); put (b, cap, n, ".");
	ax_fmt2 (s, (int) (t % HZ)); s[2] = 0; put (b, cap, n, s); put (b, cap, n, "s] ");
}

static int same (const char *a, const char *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
static int exists (const char *p) { void *f = kapi_open (p); if (!f) return 0; kapi_close (f); return 1; }

// SD:/etc/wpa_supplicant.conf: there, its encoding, each line (the psk's length only)
static void check_conf (void)
{
	static char buf[4096];
	H ("== " WPA_PATH "\n");
	void *f = kapi_open (WPA_PATH);
	if (!f)
	{
		H ("  MISSING: the kernel finds no Wi-Fi settings (the name must be exactly etc/wpa_supplicant.conf)\n");
		if (exists ("SD:/etc/wpa_supplicant.conf.txt")) H ("  found etc/wpa_supplicant.conf.txt instead: remove the .txt\n");
		if (exists ("SD:/wpa_supplicant.conf")) H ("  found wpa_supplicant.conf at the root: move it into etc/\n");
		return;
	}
	int n = kapi_read (f, buf, sizeof buf - 1); kapi_close (f);
	if (n < 0) n = 0;
	buf[n] = 0;
	H ("  "); HN (n); H (" bytes");
	int cr = 0, high = 0, nets = 0, country = 0, ssid = 0, psk = 0, open_ = 0;
	for (int i = 0; i < n; i++) { if (buf[i] == '\r') cr++; if ((unsigned char) buf[i] >= 0x80) high++; }
	int start = 0;
	if (n >= 3 && (unsigned char) buf[0] == 0xEF && (unsigned char) buf[1] == 0xBB && (unsigned char) buf[2] == 0xBF)
	{ H (", STARTS WITH A UTF-8 BOM: wpa_supplicant rejects the first line (save it as UTF-8 without BOM, or ANSI)"); start = 3; high -= 3; }
	if (cr) { H (", Windows line ends (CR LF: accepted)"); }
	if (high) { H (", "); HN (high); H (" non-ASCII bytes"); }
	H ("\n");
	for (int i = start, ln = 1; i < n; ln++)
	{
		int e = i; while (e < n && buf[e] != '\n') e++;
		int a = i, b = e; while (a < b && (buf[a] == ' ' || buf[a] == '\t' || buf[a] == '\r')) a++;
		while (b > a && (buf[b - 1] == ' ' || buf[b - 1] == '\t' || buf[b - 1] == '\r')) b--;
		i = e + 1;
		if (a == b || buf[a] == '#') continue;
		char line[200]; int k = 0;
		int eq = -1; for (int j = a; j < b; j++) if (buf[j] == '=') { eq = j; break; }
		H ("  line "); HN (ln); H (": ");
		if (eq >= 0 && ((eq - a == 3 && buf[a] == 'p' && buf[a + 1] == 's' && buf[a + 2] == 'k')
				|| (eq - a == 8 && buf[a] == 'p' && buf[a + 1] == 'a' && buf[a + 2] == 's' && buf[a + 3] == 's')))
		{
			int q = buf[eq + 1] == '"' && buf[b - 1] == '"' && b - eq - 1 >= 2;
			int len = q ? b - eq - 3 : b - eq - 1;
			H (eq - a == 3 ? "psk = " : "password = "); H ("(hidden: "); HN (len);
			H (q ? " characters, quoted" : " characters, NOT quoted (only a 64-digit hex key goes without quotes)");
			if (q && (len < 8 || len > 63)) H (": a passphrase is 8 to 63 characters");
			H (")\n");
			psk = 1;
			continue;
		}
		for (int j = a; j < b && k < (int) sizeof line - 1; j++) line[k++] = buf[j];
		line[k] = 0;
		H (line); H ("\n");
		if (k >= 8 && line[0] == 'c' && line[1] == 'o' && line[7] == '=') country = 1;
		if (k >= 8 && line[0] == 'n' && line[1] == 'e' && line[2] == 't') nets++;
		if (k >= 5 && line[0] == 's' && line[1] == 's' && line[2] == 'i' && line[3] == 'd' && line[4] == '=') ssid = 1;
		if (k >= 12 && line[0] == 'k' && line[1] == 'e' && line[2] == 'y' && line[8] == '=' && line[9] == 'N' && line[10] == 'O') open_ = 1;
	}
	if (!country) H ("  NO country= line: the driver does not join without it (country=BE, FR...)\n");
	if (!nets) H ("  NO network={ ... } block\n");
	if (nets && !ssid) H ("  NO ssid=\"...\" line\n");
	if (nets && !psk && !open_) H ("  NO psk=\"...\" line (key_mgmt=NONE for an open network)\n");
}

static void check_firmware (void)
{
	static const char *f[] = { "SD:/firmware/brcmfmac43455-sdio.bin", "SD:/firmware/brcmfmac43455-sdio.txt",
				   "SD:/firmware/brcmfmac43455-sdio.clm_blob", 0 };
	H ("== the Wi-Fi chip's firmware (the Pi 4's CYW43455)\n");
	for (int i = 0; f[i]; i++) { H ("  "); H (f[i] + 4); H (exists (f[i]) ? ": ok\n" : ": MISSING\n"); }
}

static void drain (void)
{
	char src[64], msg[224]; int sev;
	static const char *tag[] = { "PANIC", "ERROR", "WARN ", "NOTE ", "DEBUG" };
	while (kapi_klog_read (&sev, src, sizeof src, msg, sizeof msg))
	{
		if (g_ln > (int) sizeof g_log - 400) continue;	// (full: the rest dropped, the end still written)
		stamp (g_log, sizeof g_log, &g_ln);
		put (g_log, sizeof g_log, &g_ln, tag[sev < 0 || sev > 4 ? 4 : sev]); put (g_log, sizeof g_log, &g_ln, " ");
		put (g_log, sizeof g_log, &g_ln, src); put (g_log, sizeof g_log, &g_ln, ": ");
		put (g_log, sizeof g_log, &g_ln, msg); put (g_log, sizeof g_log, &g_ln, "\n");
	}
}

static void save (void)
{
	int n = 0;
	put (g_out, sizeof g_out, &n, g_head);
	put (g_out, sizeof g_out, &n, "== the kernel log (since netlog started; times from then)\n");
	put (g_out, sizeof g_out, &n, g_log);
	put (g_out, sizeof g_out, &n, g_tail);
	kapi_save_file (OUT_PATH, g_out, (unsigned) n);
}

// the programs running ("<state><kind> <name>" a line): did init start the services?
static void tasks (void)
{
	static char buf[3000];
	T ("== the programs running (telnetd, vncd, rdpd: started by etc/autostart; their \"listening on\" lines above)\n");
	int n = kapi_list_tasks (buf, sizeof buf);
	if (n <= 0) { T ("  (none listed)\n"); return; }
	buf[sizeof buf - 1] = 0;
	for (char *t = buf; *t; )
	{
		char *e = t; while (*e && *e != '\n') e++;
		char c = *e; *e = 0; T ("  "); T (t); T ("\n"); if (!c) break; t = e + 1;
	}
	static const char *want[] = { "telnetd", "vncd", "rdpd", 0 };
	for (int i = 0; want[i]; i++)
	{
		int found = 0; int l = ax_strlen (want[i]);
		for (char *t = buf; *t; t++)
			if ((t == buf || t[-1] == ' ') && same (t, want[i], l) && (t[l] == '\n' || t[l] == 0 || t[l] == ' ')) found = 1;
		if (!found) { T ("  "); T (want[i]); T (" is NOT running (its line in etc/autostart? init stopped before it?)\n"); }
	}
}

static void scan (void)
{
	static struct kapi_wlan_ap ap[32];
	static const char *sec[] = { "open", "WEP", "WPA", "WPA2" };
	T ("== the access points around (the link never came up)\n");
	int n = kapi_wlan_scan (ap, 32);
	if (n <= 0) { T ("  none found (the Wi-Fi chip did not start? see the firmware above and the log)\n"); return; }
	for (int i = 0; i < n; i++)
	{
		T ("  "); TN (ap[i].level); T (" dBm  channel "); TN (ap[i].channel);
		T (ap[i].channel > 14 ? " (5 GHz)  " : " (2.4 GHz)  "); T (sec[ap[i].security & 3]); T ("  ");
		T (ap[i].ssid[0] ? ap[i].ssid : "(hidden)"); T (ap[i].connected ? "  <- connected\n" : "\n");
	}
}

int main (void)
{
	g_t0 = kapi_get_ticks ();
	H ("netlog -- the network's start on this Onyx (cmdline.txt netlog=1; docs/04, \"Without a screen\")\n");
	int y, mo, d, h, mi, s;
	if (kapi_get_datetime (&y, &mo, &d, &h, &mi, &s))
	{
		char t[4]; H ("written at boot, clock "); HN (y); H ("-"); ax_fmt2 (t, mo); t[2] = 0; H (t); H ("-"); ax_fmt2 (t, d); H (t);
		H (" "); ax_fmt2 (t, h); H (t); H (":"); ax_fmt2 (t, mi); H (t); H (" (no network yet: the card's last known time)\n");
	}
	H ("\n");
	check_conf ();
	check_firmware ();
	H ("\n");
	save ();

	char ip[40];
	unsigned upAt = 0, lastSave = 0;
	for (;;)
	{
		drain ();
		unsigned now = kapi_get_ticks () - g_t0;
		if (!upAt && kapi_net_status (ip, sizeof ip))
		{
			upAt = now ? now : 1;
			stamp (g_log, sizeof g_log, &g_ln); put (g_log, sizeof g_log, &g_ln, "netlog: the link is up, IP ");
			put (g_log, sizeof g_log, &g_ln, ip); put (g_log, sizeof g_log, &g_ln, "\n");
		}
		if ((upAt && now - upAt >= AFTER_UP) || now >= RUN_MAX) break;
		if (now - lastSave >= 2 * HZ) { save (); lastSave = now; }
		kapi_msleep (100);
	}
	T ("\n== the result\n");
	if (upAt) { T ("  the link is UP, IP "); T (ip); T (", after "); TN ((int) (upAt / HZ)); T (" s\n"); tasks (); }
	else
	{
		T ("  the link is DOWN after "); TN (RUN_MAX / HZ); T (" s\n");
		tasks ();
		save ();
		scan ();
		drain ();
	}
	save ();
	return 0;
}
