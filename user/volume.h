//
// volume.h -- the master volume (kapi v60 sound_volume: 0..10 and mute), kept in
// SD:/etc/sound.ini ("volume = 7", "mute = 0") so it comes back after a reboot: the menu bar
// applies it at start. Used by the menu bar's volume box and /bin/volume.
// (v84) The file also says which output plays ("output = auto | jack | usb | hdmi": the kernel reads
// it when the sound first starts, kapi sound_output changes it at once): volume_save keeps that line.
//
#ifndef _volume_h
#define _volume_h
#include "kapi.h"
#include "applib.h"

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

// the saved volume -> the kernel (no file: full, not muted)
static inline void volume_restore (void)
{
	if (app_ini_load_path (VOLUME_INI) < 0) return;
	int v = app_ini_get_int (0, "volume", 10), m = app_ini_get_int (0, "mute", 0);
	kapi_sound_volume (v < 0 ? 0 : v > 10 ? 10 : v, m ? 1 : 0);
}
#endif
