//
// wpaconf.c -- a GUI editor for the WLAN credentials in SD:/etc/wpa_supplicant.conf.
//
// The kernel reads that file ONCE, during WLAN bring-up at boot (before any user
// process runs), so it must stay the canonical on-disk config in wpa_supplicant
// format. This editor therefore parses the five fields we care about (country,
// ssid, psk, proto, key_mgmt) and, on Save, REGENERATES the file from a canonical
// template (keeping the security header). There is no separate .ini: a second
// file would be a second source of truth the kernel can't read early enough -- and
// a second clear-text copy of the passphrase to keep out of git.
//
// SECURITY: the psk is stored in clear text on the SD card (the radio needs it).
// The tracked copy in git is auto-redacted by tools/git-hooks/pre-commit; this app
// only ever writes the working/SD copy, so it has no effect on git history.
//
// Live re-association is not exposed by Circle's CWPASupplicant (ctor takes the
// config path; no reconfigure), so new settings apply on the next boot. The
// "Save & Reboot" button uses kapi_reboot (ABI v25) to make that one click.
//
// Widgets are user-side (uikit.h): label + textbox + checkbox + button, drawn in
// our own canvas and driven by the kernel pointer/key streams.
//
#include "kapi.h"
#include "uikit.h"

#define WPA_PATH	"SD:/etc/wpa_supplicant.conf"

#define W		380
#define H		308
#define LBLW		78
#define FX		96			// field x
#define FW		(W - FX - 12)		// field width
#define FH		24

// ---- tiny string helpers -----------------------------------------------------

static int  slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int  scat (char *d, int at, const char *s)	// append, return new length
{ int i = 0; if (s) while (s[i]) d[at++] = s[i++]; d[at] = 0; return at; }

// Find `key` at the start of a line in `buf` (after leading whitespace), return its
// value (after '=', surrounding quotes stripped) in out[]. Returns 1 if found.
static int conf_get (const char *buf, const char *key, char *out, int cap)
{
	int kl = slen (key);
	int i = 0;
	while (buf[i])
	{
		int ls = i;
		while (buf[i] && buf[i] != '\n') i++;		// [ls, i) = this line
		int le = i;
		if (buf[i] == '\n') i++;

		int s = ls;
		while (s < le && (buf[s] == ' ' || buf[s] == '\t')) s++;
		if (s < le && (buf[s] == '#' || buf[s] == ';')) continue;
		// key token must match exactly, then '=' (optional surrounding spaces)
		if (le - s < kl) continue;
		int m = 1;
		for (int k = 0; k < kl; k++) if (buf[s + k] != key[k]) { m = 0; break; }
		if (!m) continue;
		int p = s + kl;
		while (p < le && (buf[p] == ' ' || buf[p] == '\t')) p++;
		if (p >= le || buf[p] != '=') continue;		// not "key ="
		p++;
		while (p < le && (buf[p] == ' ' || buf[p] == '\t')) p++;
		int e = le;
		while (e > p && (buf[e - 1] == ' ' || buf[e - 1] == '\t')) e--;
		if (e - p >= 2 && buf[p] == '"' && buf[e - 1] == '"') { p++; e--; }	// strip quotes
		int j = 0;
		for (int q = p; q < e && j < cap - 1; q++) out[j++] = buf[q];
		out[j] = 0;
		return 1;
	}
	return 0;
}

// ---- widgets / state ---------------------------------------------------------

#define NB	16
static ui_widget  g_pool[NB];
static ui_context g_ui;
static int  id_ssid, id_psk, id_show, id_country, id_proto, id_keymgmt, id_status;

static void set_status (const char *s) { ui_set_text (&g_ui, id_status, s); }

static void load_conf (void)
{
	char ssid[64] = "", psk[64] = "", country[16] = "BE";
	char proto[24] = "WPA2", keymgmt[24] = "WPA-PSK";

	void *f = kapi_open (WPA_PATH);
	if (f)
	{
		static char buf[2048];
		int n = kapi_read (f, buf, sizeof (buf) - 1);
		kapi_close (f);
		if (n < 0) n = 0;
		buf[n] = 0;
		conf_get (buf, "ssid", ssid, sizeof ssid);
		conf_get (buf, "psk", psk, sizeof psk);
		conf_get (buf, "country", country, sizeof country);
		conf_get (buf, "proto", proto, sizeof proto);
		conf_get (buf, "key_mgmt", keymgmt, sizeof keymgmt);
		set_status ("Loaded /etc/wpa_supplicant.conf");
	}
	else set_status ("No config yet -- fill in and Save");

	ui_set_text (&g_ui, id_ssid, ssid);
	ui_set_text (&g_ui, id_psk, psk);
	ui_set_text (&g_ui, id_country, country);
	ui_set_text (&g_ui, id_proto, proto);
	ui_set_text (&g_ui, id_keymgmt, keymgmt);
}

// Regenerate the whole file from the edited fields. Returns 1 on success.
static int save_conf (void)
{
	const char *ssid    = ui_get_text (&g_ui, id_ssid);
	const char *psk     = ui_get_text (&g_ui, id_psk);
	const char *country = ui_get_text (&g_ui, id_country);
	const char *proto   = ui_get_text (&g_ui, id_proto);
	const char *keymgmt = ui_get_text (&g_ui, id_keymgmt);

	int sl = slen (ssid), pl = slen (psk);
	if (sl < 1 || sl > 32) { set_status ("SSID must be 1..32 chars"); return 0; }
	// WPA-PSK passphrases are 8..63 chars (only enforce when key_mgmt uses PSK).
	int psk_mode = 0;
	for (int i = 0; keymgmt[i]; i++) if (keymgmt[i] == 'P' && keymgmt[i+1] == 'S' && keymgmt[i+2] == 'K') psk_mode = 1;
	if (psk_mode && (pl < 8 || pl > 63)) { set_status ("PSK must be 8..63 chars"); return 0; }

	static char out[2048];
	int n = 0;
	n = scat (out, n,
		"#\n"
		"# wpa_supplicant.conf -- WLAN credentials for Onyx (read at boot by the kernel).\n"
		"#\n"
		"# SECURITY: this file stores your Wi-Fi passphrase in CLEAR TEXT. Keep it on the\n"
		"# SD card only; do NOT commit it to a public repository. Managed by the 'wpaconf'\n"
		"# app (Wi-Fi Settings) -- reboot to apply changes.\n"
		"#\n\n");
	n = scat (out, n, "country=");  n = scat (out, n, country);  n = scat (out, n, "\n\n");
	n = scat (out, n, "network={\n");
	n = scat (out, n, "\tssid=\"");     n = scat (out, n, ssid);    n = scat (out, n, "\"\n");
	n = scat (out, n, "\tpsk=\"");      n = scat (out, n, psk);     n = scat (out, n, "\"\n");
	n = scat (out, n, "\tproto=");      n = scat (out, n, proto);   n = scat (out, n, "\n");
	n = scat (out, n, "\tkey_mgmt=");   n = scat (out, n, keymgmt); n = scat (out, n, "\n");
	n = scat (out, n, "}\n");

	if (kapi_save_file (WPA_PATH, out, (unsigned) n) < 0) { set_status ("Save FAILED (write error)"); return 0; }
	return 1;
}

// ---- callbacks ---------------------------------------------------------------

static void on_show (int id)  { (void) id; ui_set_password (&g_ui, id_psk, !ui_checked (&g_ui, id_show)); }
static void on_save (int id)  { (void) id; if (save_conf ()) set_status ("Saved. Reboot to apply."); }
static void on_reload (int id){ (void) id; load_conf (); }
static void on_reboot (int id){ (void) id; if (save_conf ()) { set_status ("Saved -- rebooting..."); kapi_reboot (); } }

static void on_evt (unsigned long s, int ev, long v) { ui_on_event (&g_ui, s, ev, v); }

int main (void)
{
	unsigned *fb = kapi_create_window (W, H, "Wi-Fi Settings");
	if (fb == 0) return 1;

	ui_init (&g_ui, g_pool, NB, fb, W, H);
	g_ui.col_bg = 0x00283038;

	ui_label (&g_ui, 12, 10, 240, 20, "Wi-Fi Settings");

	int y = 42;
	ui_label   (&g_ui, 12, y, LBLW, FH, "SSID");
	id_ssid = ui_textbox (&g_ui, FX, y, FW, FH, "", 0);            y += 34;
	ui_label   (&g_ui, 12, y, LBLW, FH, "Password");
	id_psk  = ui_textbox (&g_ui, FX, y, FW, FH, "", 0);            y += 30;
	id_show = ui_checkbox (&g_ui, FX, y, 160, 20, "Show password", 0, on_show);  y += 30;
	ui_label   (&g_ui, 12, y, LBLW, FH, "Country");
	id_country = ui_textbox (&g_ui, FX, y, FW, FH, "", 0);         y += 34;
	ui_label   (&g_ui, 12, y, LBLW, FH, "Proto");
	id_proto   = ui_textbox (&g_ui, FX, y, FW, FH, "", 0);         y += 34;
	ui_label   (&g_ui, 12, y, LBLW, FH, "Key mgmt");
	id_keymgmt = ui_textbox (&g_ui, FX, y, FW, FH, "", 0);         y += 38;

	ui_button (&g_ui, 12,  y, 84,  30, "Save",          on_save);
	ui_button (&g_ui, 104, y, 150, 30, "Save & Reboot", on_reboot);
	ui_button (&g_ui, 262, y, 106, 30, "Reload",        on_reload);  y += 38;

	id_status = ui_label (&g_ui, 12, y, W - 24, 18, "");

	ui_set_password (&g_ui, id_psk, 1);		// mask by default
	load_conf ();
	g_ui.focus = id_ssid;				// type into SSID immediately

	kapi_set_pointer_handler (on_evt);
	kapi_set_key_handler (on_evt);

	while (!should_exit ())
	{
		pump_events ();
		if (ui_dirty (&g_ui)) { ui_background (&g_ui); ui_draw (&g_ui); present (); }
		msleep (16);
	}
	return 0;
}
