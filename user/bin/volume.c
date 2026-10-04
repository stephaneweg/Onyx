//
// volume -- the master volume of the sound (kapi v60): 0 (silent) .. 10 (full), and mute.
//   volume              the volume now
//   volume 0..10        set it (and unmute)
//   volume mute | unmute | toggle
//   volume output                      which output plays (kapi v84), which ones are there
//   volume output auto|jack|usb|hdmi   choose it (applied at once)
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

static const char *out_name (int o)
{
	return o == KAPI_SND_OUT_JACK ? "jack" : o == KAPI_SND_OUT_USB ? "usb" : o == KAPI_SND_OUT_HDMI ? "hdmi" : o == 0 ? "none" : "?";
}

// "output [auto|jack|usb|hdmi]"
static int output (const char *p)
{
	while (*p == ' ') p++;
	int want = -1;
	if (ax_streq (p, "auto")) want = KAPI_SND_OUT_AUTO;
	else if (ax_streq (p, "jack")) want = KAPI_SND_OUT_JACK;
	else if (ax_streq (p, "usb")) want = KAPI_SND_OUT_USB;
	else if (ax_streq (p, "hdmi")) want = KAPI_SND_OUT_HDMI;
	else if (*p) { ax_putln ("usage: volume output [auto | jack | usb | hdmi]"); return 1; }
	int o = want >= 0 ? volume_set_output (want) : kapi_sound_output (-1);
	if (o < 0) { ax_putln ("volume: this kernel has one output, the jack (kapi v84 needed)"); return 1; }
	ax_puts ("output: asked "); ax_puts (volume_output_word (KAPI_SND_OUT_ASKED (o)));
	ax_puts (", playing on "); ax_puts (out_name (KAPI_SND_OUT_NOW (o)));
	ax_puts ((KAPI_SND_OUT_NOW (o)) == 0 ? " (the sound has not started, or the device is not there); there:" : "; there:");
	for (int i = KAPI_SND_OUT_JACK; i <= KAPI_SND_OUT_HDMI; i++) if (KAPI_SND_OUT_HAS (o, i)) { ax_puts (" "); ax_puts (out_name (i)); }
	ax_putln ("");
	return 0;
}

int main (void)
{
	char a[64]; kapi_get_args (a, sizeof a);
	int i = 0; while (a[i] == ' ') i++;
	const char *p = a + i;
	int r = kapi_sound_volume (-1, -1);
	if (!*p) { show (r); return 0; }
	if (p[0] == 'o' && p[1] == 'u' && p[2] == 't' && p[3] == 'p' && p[4] == 'u' && p[5] == 't' && (p[6] == ' ' || p[6] == 0)) return output (p + 6);
	if (ax_streq (p, "mute")) r = kapi_sound_volume (-1, 1);
	else if (ax_streq (p, "unmute")) r = kapi_sound_volume (-1, 0);
	else if (ax_streq (p, "toggle")) r = kapi_sound_volume (-1, (r & 0x100) ? 0 : 1);
	else if (*p >= '0' && *p <= '9')
	{
		int v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
		if (*p || v > 10) { ax_putln ("usage: volume [0..10 | mute | unmute | toggle | output [auto|jack|usb|hdmi]]"); return 1; }
		r = kapi_sound_volume (v, 0);
	}
	else { ax_putln ("usage: volume [0..10 | mute | unmute | toggle | output [auto|jack|usb|hdmi]]"); return 1; }
	volume_save (r & 0xFF, (r & 0x100) ? 1 : 0);
	show (r);
	return 0;
}
