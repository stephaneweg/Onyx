//
// wifi.inc -- wifi.h's bodies (NetKit; on the PC inline).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifdef NK_IMPL
#define NK_FN	extern "C"
#else
#define NK_FN	static inline
#endif
static inline int wf_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void wf_cpy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline int wf_eq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static inline void wf_cat (char *d, int *n, int cap, const char *s) { while (s && *s && *n < cap - 1) d[(*n)++] = *s++; d[*n] = 0; }
// "key=value" of a line [p, e) (quotes stripped) -> 1 found.
static inline int wf_kv (const char *p, const char *e, const char *key, char *out, int cap)
{
	while (p < e && (*p == ' ' || *p == '\t')) p++;
	int kl = wf_len (key);
	if (e - p <= kl || p[kl] != '=') return 0;
	for (int i = 0; i < kl; i++) if (p[i] != key[i]) return 0;
	p += kl + 1;
	while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
	if (p < e && *p == '"') { p++; if (e > p && e[-1] == '"') e--; }
	int n = 0; while (p < e && n < cap - 1) out[n++] = *p++;
	out[n] = 0;
	return 1;
}

NK_FN int wifi_known_load (struct wifi_known *out, int max, char *country, int ccap)
{
	int nk = 0;
	if (country && ccap > 2) wf_cpy (country, "BE", ccap);
	void *f = kapi_open (WIFI_CONF);
	if (!f) return 0;
	static char buf[6144];
	int n = kapi_read (f, buf, sizeof buf - 1); kapi_close (f);
	if (n <= 0) return 0;
	buf[n] = 0;
	struct wifi_known *cur = 0;
	for (const char *p = buf; *p; )
	{
		const char *e = p; while (*e && *e != '\n') e++;
		const char *q = p; while (q < e && (*q == ' ' || *q == '\t')) q++;
		if (q < e && *q != '#')
		{
			char v[64];
			if (!cur && e - q >= 9 && q[0] == 'n' && q[7] == '=' && q[8] == '{' && nk < max)
			{ cur = &out[nk++]; cur->ssid[0] = cur->psk[0] = cur->proto[0] = 0; wf_cpy (cur->keymgmt, "WPA-PSK", sizeof cur->keymgmt); cur->priority = 0; }
			else if (cur && *q == '}') { if (!cur->ssid[0]) nk--; cur = 0; }
			else if (cur)
			{
				if (wf_kv (q, e, "ssid", v, sizeof v)) wf_cpy (cur->ssid, v, sizeof cur->ssid);
				else if (wf_kv (q, e, "psk", v, sizeof v)) wf_cpy (cur->psk, v, sizeof cur->psk);
				else if (wf_kv (q, e, "key_mgmt", v, sizeof v)) wf_cpy (cur->keymgmt, v, sizeof cur->keymgmt);
				else if (wf_kv (q, e, "proto", v, sizeof v)) wf_cpy (cur->proto, v, sizeof cur->proto);
				else if (wf_kv (q, e, "priority", v, sizeof v)) { int k = 0; for (int i = 0; v[i] >= '0' && v[i] <= '9'; i++) k = k * 10 + v[i] - '0'; cur->priority = k; }
			}
			else if (country && wf_kv (q, e, "country", v, sizeof v)) wf_cpy (country, v, ccap);
		}
		p = *e ? e + 1 : e;
	}
	if (cur && !cur->ssid[0]) nk--;
	return nk;
}

NK_FN int wifi_known_save (const struct wifi_known *k, int nk, const char *country)
{
	static char out[6144]; int n = 0; out[0] = 0;
	wf_cat (out, &n, sizeof out,
		"#\n# wpa_supplicant.conf -- WLAN credentials for Onyx (read at boot, and again when a network is joined).\n"
		"# The network with the highest priority is joined first.\n#\n"
		"# SECURITY: the passwords are stored in CLEAR TEXT. Keep this file on the SD card only;\n"
		"# do NOT commit it to a public repository. Managed by the Wi-Fi menu, Wi-Fi Settings and the\n"
		"# console's Wi-Fi page (NetKit's wifi.h).\n#\n\n");
	wf_cat (out, &n, sizeof out, "country="); wf_cat (out, &n, sizeof out, country && country[0] ? country : "BE"); wf_cat (out, &n, sizeof out, "\n");
	for (int i = 0; i < nk; i++)
	{
		const struct wifi_known *w = &k[i];
		if (!w->ssid[0]) continue;
		wf_cat (out, &n, sizeof out, "\nnetwork={\n\tssid=\""); wf_cat (out, &n, sizeof out, w->ssid); wf_cat (out, &n, sizeof out, "\"\n");
		int psk = !wf_eq (w->keymgmt, "NONE");
		if (psk) { wf_cat (out, &n, sizeof out, "\tpsk=\""); wf_cat (out, &n, sizeof out, w->psk); wf_cat (out, &n, sizeof out, "\"\n"); }
		if (psk && w->proto[0]) { wf_cat (out, &n, sizeof out, "\tproto="); wf_cat (out, &n, sizeof out, w->proto); wf_cat (out, &n, sizeof out, "\n"); }
		wf_cat (out, &n, sizeof out, "\tkey_mgmt="); wf_cat (out, &n, sizeof out, w->keymgmt[0] ? w->keymgmt : "WPA-PSK"); wf_cat (out, &n, sizeof out, "\n");
		if (w->priority > 0)
		{
			char b[12]; int v = w->priority, t = 0; char r[12];
			while (v) { r[t++] = (char) ('0' + v % 10); v /= 10; }
			int bn = 0; while (t) b[bn++] = r[--t]; b[bn] = 0;
			wf_cat (out, &n, sizeof out, "\tpriority="); wf_cat (out, &n, sizeof out, b); wf_cat (out, &n, sizeof out, "\n");
		}
		wf_cat (out, &n, sizeof out, "}\n");
	}
	return kapi_save_file (WIFI_CONF, out, (unsigned) n) >= 0 ? WIFI_OK : WIFI_EWRITE;
}

static inline int wf_apply (void) { return kapi_wlan_reconnect () < 0 ? WIFI_SAVED : WIFI_OK; }

NK_FN int wifi_join_as (const char *ssid, const char *psk, const char *keymgmt, const char *proto)
{
	int sl = wf_len (ssid);
	if (sl < 1 || sl > 32) return WIFI_ENAME;
	static struct wifi_known k[WIFI_KNOWN_MAX];
	char cc[8];
	int nk = wifi_known_load (k, WIFI_KNOWN_MAX, cc, sizeof cc);
	int open = keymgmt && wf_eq (keymgmt, "NONE");
	struct wifi_known *w = 0;
	for (int i = 0; i < nk; i++) if (wf_eq (k[i].ssid, ssid)) w = &k[i];
	if (!open && (psk == 0 || !psk[0]) && w && w->psk[0]) psk = w->psk;	// (known: its password kept)
	int pl = wf_len (psk);
	if (!open && (pl < 8 || pl > 63)) return WIFI_EPASSWORD;
	if (!w)
	{
		if (nk >= WIFI_KNOWN_MAX) return WIFI_EFULL;
		w = &k[nk++];
		wf_cpy (w->ssid, ssid, sizeof w->ssid); w->psk[0] = w->proto[0] = 0; w->priority = 0;
	}
	if (w->psk != psk) wf_cpy (w->psk, open ? "" : psk, sizeof w->psk);
	wf_cpy (w->keymgmt, keymgmt && keymgmt[0] ? keymgmt : "WPA-PSK", sizeof w->keymgmt);
	wf_cpy (w->proto, open ? "" : (proto ? proto : ""), sizeof w->proto);
	int top = 0; for (int i = 0; i < nk; i++) if (k[i].priority > top) top = k[i].priority;
	w->priority = top + 1;						// (joined first)
	if (wifi_known_save (k, nk, cc) != WIFI_OK) return WIFI_EWRITE;
	return wf_apply ();
}

NK_FN int wifi_join (const char *ssid, int security, const char *psk)
{
	if (security == WLAN_SEC_WEP) return WIFI_EWEP;
	if (security == WLAN_SEC_OPEN) return wifi_join_as (ssid, "", "NONE", "");
	return wifi_join_as (ssid, psk, "WPA-PSK", security == WLAN_SEC_WPA ? "WPA" : "WPA2");
}

NK_FN int wifi_forget (const char *ssid)
{
	static struct wifi_known k[WIFI_KNOWN_MAX];
	char cc[8];
	int nk = wifi_known_load (k, WIFI_KNOWN_MAX, cc, sizeof cc), m = 0, found = 0;
	for (int i = 0; i < nk; i++) { if (wf_eq (k[i].ssid, ssid)) { found = 1; continue; } k[m++] = k[i]; }
	if (!found) return WIFI_OK;
	if (wifi_known_save (k, m, cc) != WIFI_OK) return WIFI_EWRITE;
	return wf_apply ();
}

NK_FN int wifi_set_country (const char *country)
{
	static struct wifi_known k[WIFI_KNOWN_MAX];
	char cc[8];
	int nk = wifi_known_load (k, WIFI_KNOWN_MAX, cc, sizeof cc);
	if (wifi_known_save (k, nk, country) != WIFI_OK) return WIFI_EWRITE;
	return wf_apply ();
}

NK_FN int wifi_is_known (const char *ssid)
{
	static struct wifi_known k[WIFI_KNOWN_MAX];
	int nk = wifi_known_load (k, WIFI_KNOWN_MAX, 0, 0);
	for (int i = 0; i < nk; i++) if (wf_eq (k[i].ssid, ssid)) return i;
	return -1;
}
#undef NK_FN
