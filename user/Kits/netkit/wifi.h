//
// wifi.h -- the known Wi-Fi networks (SD:/etc/wpa_supplicant.conf: the country, a network={...} block each -- its ssid,
// psk, proto, key_mgmt, priority), kept ALL (the highest priority joined first), and joining one at once (kapi v60
// wlan_reconnect: wpa_supplicant reads the file again). The menu bar's Wi-Fi menu, the Wi-Fi applet and the console's
// Wi-Fi page share it. The scan is AppKit's (kapi_wlan_scan). C and C++.
//
//   struct wifi_known k[WIFI_KNOWN_MAX]; char cc[8]; int n = wifi_known_load (k, WIFI_KNOWN_MAX, cc, sizeof cc);
//   int r = wifi_join ("Maison", WLAN_SEC_WPA2, "the password");   // WIFI_OK, WIFI_SAVED (a reboot joins it), WIFI_E*
//   wifi_forget ("Voisin");
//
// SECURITY: the passwords are kept in CLEAR TEXT on the card (the radio needs them).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _wifi_onyx_h
#define _wifi_onyx_h
#include "appkit/appkit.h"
#include "nk_api.h"

#define WIFI_CONF		"SD:/etc/wpa_supplicant.conf"
#define WIFI_KNOWN_MAX		16

struct wifi_known			// a known network
{
	char ssid[33];
	char psk[64];			// its password ("" for an open one)
	char keymgmt[24];		// "WPA-PSK", "NONE" (open)
	char proto[16];			// "WPA2", "WPA", "" (none said)
	int  priority;			// the highest joined first
};

// The answers of wifi_join / wifi_forget / wifi_known_save
#define WIFI_OK			0	// written and joining (wlan_reconnect)
#define WIFI_SAVED		1	// written; a reboot joins it (no live Wi-Fi here)
#define WIFI_EWRITE		-1	// the file could not be written
#define WIFI_EFULL		-2	// WIFI_KNOWN_MAX known already
#define WIFI_EPASSWORD		-3	// a secured network's password is 8 to 63 characters
#define WIFI_EWEP		-4	// WEP is not supported
#define WIFI_ENAME		-5	// a name is 1 to 32 characters

// The known networks into out[0..max) (in the file's order) and the country into country (BE when none) -> how many.
NK_API int wifi_known_load (struct wifi_known *out, int max, char *country, int ccap);
// The file written anew from them (its comment header, country=, a block each) -> WIFI_OK or WIFI_EWRITE.
NK_API int wifi_known_save (const struct wifi_known *k, int n, const char *country);
// A network joined: added to the known ones, or its password updated (psk 0: kept), given the highest priority, the
// file written, then wlan_reconnect. security: WLAN_SEC_* (the scan's). -> WIFI_OK, WIFI_SAVED, WIFI_E*.
NK_API int wifi_join (const char *ssid, int security, const char *psk);
// The same with the fields given as they are (the Wi-Fi applet's manual form): key_mgmt, proto.
NK_API int wifi_join_as (const char *ssid, const char *psk, const char *keymgmt, const char *proto);
// A known network forgotten (its block removed, the file written, wlan_reconnect) -> WIFI_OK, WIFI_SAVED, WIFI_EWRITE
// (a name not known: WIFI_OK, nothing done).
NK_API int wifi_forget (const char *ssid);
// The country (two letters: the radio's channels) written -> WIFI_OK / WIFI_SAVED / WIFI_EWRITE.
NK_API int wifi_set_country (const char *country);
// A network known? -> its index in a fresh load, -1 not.
NK_API int wifi_is_known (const char *ssid);

#if defined (NK_BODIES_INLINE) && !defined (NK_IMPL)
#include "wifi.inc"
#endif

#endif
