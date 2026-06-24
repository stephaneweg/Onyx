//
// wpaconf/main.cpp -- GUI editor for the WLAN credentials in SD:/etc/wpa_supplicant.conf
// (C++ port, on the uikit.hpp toolkit).
//
// The kernel reads that file once, at boot (WLAN bring-up runs before any user
// process), so it must stay the canonical wpa_supplicant-format file. This editor
// scans the five fields we care about (country, ssid, psk, proto, key_mgmt) and, on
// Save, REGENERATES the file from a canonical template (keeping the security header).
//
// SECURITY: the psk is clear text on the SD card (the radio needs it). The git copy is
// auto-redacted by tools/git-hooks/pre-commit; this app only writes the working/SD copy.
//
// Live re-association isn't exposed by Circle's CWPASupplicant, so changes apply on the
// next boot -- "Save & Reboot" confirms via a modal, then kapi_reboot (ABI v25).
//
#include "kapi.h"
#include "uikit.hpp"
#include "uidialog.hpp"		// ui::messagebox (user-side modal, no kernel call)

#define WPA_PATH	"SD:/etc/wpa_supplicant.conf"
#define W		380
#define H		308
#define LBLW		78
#define FX		96
#define FW		(W - FX - 12)
#define FH		24

// ---- tiny string helpers -----------------------------------------------------
static int  slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int  scat (char *d, int at, const char *s) { int i = 0; if (s) while (s[i]) d[at++] = s[i++]; d[at] = 0; return at; }

// Value of `key` at the start of a line (after ws), quotes stripped, into out[]. 1 if found.
static int conf_get (const char *buf, const char *key, char *out, int cap)
{
	int kl = slen (key), i = 0;
	while (buf[i])
	{
		int ls = i;
		while (buf[i] && buf[i] != '\n') i++;
		int le = i; if (buf[i] == '\n') i++;
		int s = ls;
		while (s < le && (buf[s] == ' ' || buf[s] == '\t')) s++;
		if (s < le && (buf[s] == '#' || buf[s] == ';')) continue;
		if (le - s < kl) continue;
		int m = 1;
		for (int k = 0; k < kl; k++) if (buf[s + k] != key[k]) { m = 0; break; }
		if (!m) continue;
		int p = s + kl;
		while (p < le && (buf[p] == ' ' || buf[p] == '\t')) p++;
		if (p >= le || buf[p] != '=') continue;
		p++;
		while (p < le && (buf[p] == ' ' || buf[p] == '\t')) p++;
		int e = le;
		while (e > p && (buf[e - 1] == ' ' || buf[e - 1] == '\t')) e--;
		if (e - p >= 2 && buf[p] == '"' && buf[e - 1] == '"') { p++; e--; }
		int j = 0;
		for (int q = p; q < e && j < cap - 1; q++) out[j++] = buf[q];
		out[j] = 0;
		return 1;
	}
	return 0;
}

// ---- widgets / state ---------------------------------------------------------
static ui::Ui      *g_ui;
static ui::Textbox *g_ssid, *g_psk, *g_country, *g_proto, *g_keymgmt;
static ui::Checkbox *g_show;
static ui::Label   *g_status;

static void set_status (const char *s) { g_status->setText (s); g_ui->markDirty (); }

static void load_conf (void)
{
	char ssid[64] = "", psk[64] = "", country[16] = "BE", proto[24] = "WPA2", keymgmt[24] = "WPA-PSK";
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
	g_ssid->setText (ssid); g_psk->setText (psk); g_country->setText (country);
	g_proto->setText (proto); g_keymgmt->setText (keymgmt);
	g_ui->markDirty ();
}

static int save_conf (void)
{
	const char *ssid = g_ssid->getText (), *psk = g_psk->getText (), *country = g_country->getText ();
	const char *proto = g_proto->getText (), *keymgmt = g_keymgmt->getText ();
	int sl = slen (ssid), pl = slen (psk);
	if (sl < 1 || sl > 32) { set_status ("SSID must be 1..32 chars"); return 0; }
	int psk_mode = 0;
	for (int i = 0; keymgmt[i]; i++) if (keymgmt[i] == 'P' && keymgmt[i+1] == 'S' && keymgmt[i+2] == 'K') psk_mode = 1;
	if (psk_mode && (pl < 8 || pl > 63)) { set_status ("PSK must be 8..63 chars"); return 0; }

	static char out[2048]; int n = 0;
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
static void on_show   (ui::Widget &) { g_psk->password = !g_show->checked; g_ui->markDirty (); }
static void on_save   (ui::Widget &) { if (save_conf ()) set_status ("Saved. Reboot to apply."); }
static void on_reload (ui::Widget &) { load_conf (); }
static void on_reboot (ui::Widget &)
{
	if (!save_conf ()) return;
	ui::MessageBox mb (*g_ui, "Reboot", "Settings saved. Reboot now to apply them?", MB_YESNO);
	if (mb.run (g_ui))
		kapi_reboot ();					// does not return
	else
		set_status ("Saved. Reboot later to apply.");
}
static void on_evt (unsigned long s, int ev, long v) { g_ui->onEvent (s, ev, v); }

int main (void)
{
	unsigned *fb = kapi_create_window (W, H, "Wi-Fi Settings");
	if (fb == 0) return 1;

	ui::Ui ui (fb, W, H); g_ui = &ui;
	ui.col_bg = 0x00283038;

	ui.label (12, 10, 240, 20, "Wi-Fi Settings");

	int y = 42;
	ui.label (12, y, LBLW, FH, "SSID");     g_ssid    = &ui.textbox (FX, y, FW, FH, ""); y += 34;
	ui.label (12, y, LBLW, FH, "Password"); g_psk     = &ui.textbox (FX, y, FW, FH, ""); y += 30;
	g_show = &ui.checkbox (FX, y, 160, 20, "Show password", false, on_show);            y += 30;
	ui.label (12, y, LBLW, FH, "Country");  g_country = &ui.textbox (FX, y, FW, FH, ""); y += 34;
	ui.label (12, y, LBLW, FH, "Proto");    g_proto   = &ui.textbox (FX, y, FW, FH, ""); y += 34;
	ui.label (12, y, LBLW, FH, "Key mgmt"); g_keymgmt = &ui.textbox (FX, y, FW, FH, ""); y += 38;

	ui.button (12,  y, 84,  30, "Save",          on_save);
	ui.button (104, y, 150, 30, "Save & Reboot", on_reboot);
	ui.button (262, y, 106, 30, "Reload",        on_reload);                            y += 38;

	g_status = &ui.label (12, y, W - 24, 18, "");

	g_psk->password = true;				// mask by default
	load_conf ();
	ui.setFocus (*g_ssid);				// type into SSID immediately

	kapi_set_pointer_handler (on_evt);
	kapi_set_key_handler (on_evt);

	while (!should_exit ())
	{
		pump_events ();
		if (ui.dirty ()) { ui.background (); ui.drawAll (); present (); }
		msleep (16);
	}
	return 0;
}
