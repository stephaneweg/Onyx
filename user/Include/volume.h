//
// volume.h -- the master volume (kapi v60 sound_volume: 0..10 and mute), kept in
// SD:/etc/sound.ini ("volume = 7", "mute = 0") so it comes back after a reboot: the menu bar
// applies it at start. Used by the menu bar's volume box and /bin/volume.
// (v84) The file also says which output plays ("output = auto | jack | usb | hdmi": the kernel reads
// it when the sound first starts, kapi sound_output changes it at once): volume_save keeps that line.
//
#ifndef _volume_h
#define _volume_h
#include "appkit/appkit.h"

#define VOLUME_INI	"SD:/etc/sound.ini"

// An output's word in sound.ini (KAPI_SND_OUT_*).
static inline const char *volume_output_word (int out)
{
	return out == KAPI_SND_OUT_JACK ? "jack" : out == KAPI_SND_OUT_USB ? "usb" : out == KAPI_SND_OUT_HDMI ? "hdmi" : "auto";
}

static inline void volume_save (int vol, int mute)
{
	char b[200]; int n = 0;
	const char *h = "; the master volume (0..10) and mute: the menu bar and /bin/volume\nvolume = ";
	for (int i = 0; h[i]; i++) b[n++] = h[i];
	if (vol >= 10) { b[n++] = '1'; b[n++] = '0'; } else b[n++] = (char) ('0' + (vol < 0 ? 0 : vol));
	const char *m = "\nmute = ";
	for (int i = 0; m[i]; i++) b[n++] = m[i];
	b[n++] = mute ? '1' : '0'; b[n++] = '\n';
	int o = kapi_sound_output (-1);			// (the output asked for: the kernel has it, from this file)
	if (o >= 0)
	{
		const char *t = "; the output: auto (a USB headset if there is one, else the jack), jack, usb, hdmi\noutput = ";
		for (int i = 0; t[i]; i++) b[n++] = t[i];
		const char *w = volume_output_word (KAPI_SND_OUT_ASKED (o));
		for (int i = 0; w[i]; i++) b[n++] = w[i];
		b[n++] = '\n';
	}
	kapi_save_file (VOLUME_INI, b, (unsigned) n);
}

// The output chosen (KAPI_SND_OUT_*): applied now, kept in the file -> sound_output's result.
static inline int volume_set_output (int out)
{
	int o = kapi_sound_output (out);
	int r = kapi_sound_volume (-1, -1);
	if (o >= 0) volume_save (r & 0xFF, (r & 0x100) ? 1 : 0);
	return o;
}

// ---- the mixer (kapi v85): a program's own volume ------------------------------------------------
// Every program that plays has a channel (kapi_sound_clients); its volume (0..100) and its mute are
// remembered by its name in SD:/etc/mixer.ini ("media = 60", "media.mute = 1"), which the kernel
// reads when the sound first starts.
#define MIXER_INI	"SD:/etc/mixer.ini"

// The channel's volume and mute set now (-1: kept) and written to the file -> volume | 0x100 muted, -1.
static inline int mixer_set (const struct kapi_sound_client *c, int volume, int mute)
{
	int r = kapi_sound_client_volume (c->pid, volume, mute);
	if (r < 0) return r;
	static char old[4096], out[4400];
	int n = 0, o = 0;
	void *f = kapi_open (MIXER_INI);
	if (f != 0) { n = kapi_read (f, old, sizeof old - 1); kapi_close (f); if (n < 0) n = 0; }
	old[n] = 0;
	int nl = ax_strlen (c->name);
	if (n == 0)
	{
		const char *h = "; the mixer: each program's own volume (0..100) and mute -- the Sound applet, /bin/volume\n";
		for (int i = 0; h[i]; i++) out[o++] = h[i];
	}
	for (int i = 0; i < n; )				// the other programs' lines kept
	{
		int e = i; while (e < n && old[e] != '\n') e++;
		int same = e - i > nl && (old[i + nl] == ' ' || old[i + nl] == '=' || old[i + nl] == '.');
		for (int k = 0; same && k < nl; k++) if (old[i + k] != c->name[k]) same = 0;
		if (!same && e > i && o + (e - i) + 1 < (int) sizeof out - 120) { for (int k = i; k < e; k++) out[o++] = old[k]; out[o++] = '\n'; }
		i = e + 1;
	}
	char num[16];
	for (int k = 0; k < nl; k++) out[o++] = c->name[k];
	out[o++] = ' '; out[o++] = '='; out[o++] = ' ';
	ax_itoa (r & 0xFF, num); for (int k = 0; num[k]; k++) out[o++] = num[k];
	out[o++] = '\n';
	for (int k = 0; k < nl; k++) out[o++] = c->name[k];
	const char *m = ".mute = "; for (int k = 0; m[k]; k++) out[o++] = m[k];
	out[o++] = (r & 0x100) ? '1' : '0'; out[o++] = '\n';
	kapi_save_file (MIXER_INI, out, (unsigned) o);
	return r;
}

// the saved volume -> the kernel (no file: full, not muted)
static inline void volume_restore (void)
{
	if (app_ini_load_path (VOLUME_INI) < 0) return;
	int v = app_ini_get_int (0, "volume", 10), m = app_ini_get_int (0, "mute", 0);
	kapi_sound_volume (v < 0 ? 0 : v > 10 ? 10 : v, m ? 1 : 0);
}
#endif
