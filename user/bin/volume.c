//
// volume -- the master volume of the sound (kapi v60): 0 (silent) .. 10 (full), and mute.
//   volume              the volume now
//   volume 0..10        set it (and unmute)
//   volume mute | unmute | toggle
// Kept in SD:/etc/sound.ini (applied again at boot by the menu bar, whose icon follows).
//
#include "kapi.h"
#include "applib.h"
#include "volume.h"

static void show (int r)
{
	char b[16]; ax_puts ("volume "); ax_itoa (r & 0xFF, b); ax_puts (b); ax_puts ("/10");
	ax_putln ((r & 0x100) ? " (muted)" : "");
}

int main (void)
{
	char a[64]; kapi_get_args (a, sizeof a);
	int i = 0; while (a[i] == ' ') i++;
	const char *p = a + i;
	int r = kapi_sound_volume (-1, -1);
	if (!*p) { show (r); return 0; }
	if (ax_streq (p, "mute")) r = kapi_sound_volume (-1, 1);
	else if (ax_streq (p, "unmute")) r = kapi_sound_volume (-1, 0);
	else if (ax_streq (p, "toggle")) r = kapi_sound_volume (-1, (r & 0x100) ? 0 : 1);
	else if (*p >= '0' && *p <= '9')
	{
		int v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
		if (*p || v > 10) { ax_putln ("usage: volume [0..10 | mute | unmute | toggle]"); return 1; }
		r = kapi_sound_volume (v, 0);
	}
	else { ax_putln ("usage: volume [0..10 | mute | unmute | toggle]"); return 1; }
	volume_save (r & 0xFF, (r & 0x100) ? 1 : 0);
	show (r);
	return 0;
}
