//
// volume -- the master volume of the sound (kapi v60): 0 (silent) .. 10 (full), and mute.
//   volume              the volume now
//   volume 0..10        set it (and unmute)
//   volume mute | unmute | toggle
//   volume output                      which output plays (kapi v84), which ones are there
//   volume output auto|jack|usb|hdmi   choose it (applied at once)
//   volume apps                        the mixer (kapi v85): the programs that play, each one's volume
//   volume app <name | pid> 0..100 | mute | unmute     a program's own volume (kept in SD:/etc/mixer.ini)
// Kept in SD:/etc/sound.ini (applied again at boot by the menu bar, whose icon follows).
//
#include "appkit/appkit.h"
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

// "apps", "app <name | pid> <0..100 | mute | unmute>": the mixer
static int apps (void)
{
	struct kapi_sound_client c[16];
	int n = kapi_sound_clients (c, 16);
	if (n < 0) { ax_putln ("volume: this kernel has no mixer (kapi v85 needed)"); return 1; }
	if (n == 0) { ax_putln ("no program is playing"); return 0; }
	if (n > 16) n = 16;
	ax_putln ("  PID  VOLUME  LEVEL  NAME");
	for (int i = 0; i < n; i++)
	{
		char b[16]; int k;
		k = ax_itoa ((int) c[i].pid, b); for (int s = k; s < 5; s++) ax_puts (" "); ax_puts (b);
		k = ax_itoa (c[i].volume, b); for (int s = k; s < 8; s++) ax_puts (" "); ax_puts (b);
		k = ax_itoa (c[i].peak * 100 / 32767, b); for (int s = k; s < 6; s++) ax_puts (" "); ax_puts (b); ax_puts ("%  ");
		ax_puts (c[i].name); ax_putln (c[i].mute ? "  (muted)" : "");
	}
	return 0;
}
static int app (const char *p)
{
	while (*p == ' ') p++;
	char who[32]; int w = 0;
	while (*p && *p != ' ' && w < 31) who[w++] = *p++;
	who[w] = 0;
	while (*p == ' ') p++;
	int vol = -1, mute = -1;
	if (ax_streq (p, "mute")) mute = 1;
	else if (ax_streq (p, "unmute")) mute = 0;
	else if (*p >= '0' && *p <= '9') { vol = 0; while (*p >= '0' && *p <= '9') vol = vol * 10 + (*p++ - '0'); if (*p || vol > 100) vol = -2; else mute = 0; }
	if (!w || (vol < 0 && mute < 0) || vol == -2) { ax_putln ("usage: volume app <name | pid> <0..100 | mute | unmute>"); return 1; }
	int pid = 0; for (int i = 0; who[i] >= '0' && who[i] <= '9'; i++) { pid = pid * 10 + (who[i] - '0'); if (!who[i + 1]) w = 0; }
	struct kapi_sound_client c[16];
	int n = kapi_sound_clients (c, 16), done = 0;
	if (n < 0) { ax_putln ("volume: this kernel has no mixer (kapi v85 needed)"); return 1; }
	if (n > 16) n = 16;
	for (int i = 0; i < n; i++)
		if (w == 0 ? (int) c[i].pid == pid : ax_streq (c[i].name, who)) { mixer_set (&c[i], vol, mute); done++; }
	if (!done) { ax_puts ("volume: "); ax_puts (who); ax_putln (" is not playing (volume apps: the programs that are)"); return 1; }
	return apps ();
}

int main (void)
{
	char a[64]; kapi_get_args (a, sizeof a);
	int i = 0; while (a[i] == ' ') i++;
	const char *p = a + i;
	int r = kapi_sound_volume (-1, -1);
	if (!*p) { show (r); return 0; }
	if (ax_streq (p, "apps")) return apps ();
	if (p[0] == 'a' && p[1] == 'p' && p[2] == 'p' && p[3] == ' ') return app (p + 4);
	if (p[0] == 'o' && p[1] == 'u' && p[2] == 't' && p[3] == 'p' && p[4] == 'u' && p[5] == 't' && (p[6] == ' ' || p[6] == 0)) return output (p + 6);
	if (ax_streq (p, "mute")) r = kapi_sound_volume (-1, 1);
	else if (ax_streq (p, "unmute")) r = kapi_sound_volume (-1, 0);
	else if (ax_streq (p, "toggle")) r = kapi_sound_volume (-1, (r & 0x100) ? 0 : 1);
	else if (*p >= '0' && *p <= '9')
	{
		int v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
		if (*p || v > 10) { ax_putln ("usage: volume [0..10 | mute | unmute | toggle | output [auto|jack|usb|hdmi] | apps | app <name> <0..100|mute|unmute>]"); return 1; }
		r = kapi_sound_volume (v, 0);
	}
	else { ax_putln ("usage: volume [0..10 | mute | unmute | toggle | output [auto|jack|usb|hdmi] | apps | app <name> <0..100|mute|unmute>]"); return 1; }
	volume_save (r & 0xFF, (r & 0x100) ? 1 : 0);
	show (r);
	return 0;
}
