//
// volume.h -- the master volume (kapi v60 sound_volume: 0..10 and mute), kept in
// SD:/etc/sound.ini ("volume = 7", "mute = 0") so it comes back after a reboot: the menu bar
// applies it at start. Used by the menu bar's volume box and /bin/volume.
//
#ifndef _volume_h
#define _volume_h
#include "kapi.h"
#include "applib.h"

#define VOLUME_INI	"SD:/etc/sound.ini"

static inline void volume_save (int vol, int mute)
{
	char b[80]; int n = 0;
	const char *h = "; the master volume (0..10) and mute: the menu bar and /bin/volume\nvolume = ";
	for (int i = 0; h[i]; i++) b[n++] = h[i];
	if (vol >= 10) { b[n++] = '1'; b[n++] = '0'; } else b[n++] = (char) ('0' + (vol < 0 ? 0 : vol));
	const char *m = "\nmute = ";
	for (int i = 0; m[i]; i++) b[n++] = m[i];
	b[n++] = mute ? '1' : '0'; b[n++] = '\n';
	kapi_save_file (VOLUME_INI, b, (unsigned) n);
}

// the saved volume -> the kernel (no file: full, not muted)
static inline void volume_restore (void)
{
	if (app_ini_load_path (VOLUME_INI) < 0) return;
	int v = app_ini_get_int (0, "volume", 10), m = app_ini_get_int (0, "mute", 0);
	kapi_sound_volume (v < 0 ? 0 : v > 10 ? 10 : v, m ? 1 : 0);
}
#endif
