//
// soundconf -- the Control Panel's Sound applet (applet_proto.h; alone, a window of its own): the
// output (kapi v84 sound_output: Automatic, the jack, a USB headset, HDMI -- the ones that are there),
// the master volume (0..10) and mute (kapi v60 sound_volume), applied at once to everything played and
// kept in SD:/etc/sound.ini (volume.h: the menu bar applies it at start); a test sound (a short
// chime, when no app holds the sound output). The menu bar's speaker changes the same volume: the
// applet follows it.
//
#include "kapi.h"
#include "applib.h"
#include "volume.h"
#include "wtk/wtk.h"
#include "ft/wtkface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace wtk;

#define W	700
#define H	470

static Slider   *g_vol;
static Checkbox *g_mute;
static Label    *g_value, *g_status;
static Root     *g_root;

// The output (kapi v84): the choices shown are the ones present (Automatic always).
static Dropdown *g_out;
static Label    *g_outNow;
static const char *g_outOpt[4];
static int g_outVal[4], g_outN, g_outLast = -2;

static const char *out_title (int o)
{
	return o == KAPI_SND_OUT_JACK ? "Headphone jack (3.5 mm)" : o == KAPI_SND_OUT_USB ? "USB headset / DAC"
	     : o == KAPI_SND_OUT_HDMI ? "HDMI (the screen)" : "Automatic";
}

// The list and the line under it, from the kernel (every half second: a USB device comes and goes).
static void read_output (void)
{
	int o = kapi_sound_output (-1);
	if (o == g_outLast) return;
	g_outLast = o;
	if (o < 0)					// an older kernel: the jack only
	{
		g_outOpt[0] = out_title (KAPI_SND_OUT_JACK); g_outVal[0] = KAPI_SND_OUT_JACK; g_outN = 1;
		g_out->setOptions (g_outOpt, 1, 0);
		g_outNow->setText ("");
		return;
	}
	int asked = KAPI_SND_OUT_ASKED (o), sel = 0;
	g_outN = 0;
	for (int i = KAPI_SND_OUT_AUTO; i <= KAPI_SND_OUT_HDMI; i++)
	{
		if (i != KAPI_SND_OUT_AUTO && i != asked && !KAPI_SND_OUT_HAS (o, i)) continue;
		if (i == asked) sel = g_outN;
		g_outOpt[g_outN] = out_title (i); g_outVal[g_outN++] = i;
	}
	g_out->setOptions (g_outOpt, g_outN, sel);
	g_out->invalidate (true);
	static char line[96];
	int now = KAPI_SND_OUT_NOW (o), n = 0;
	const char *a = now == 0 ? (asked == KAPI_SND_OUT_USB || asked == KAPI_SND_OUT_AUTO
				    ? "Not playing yet (the sound starts with the first app that plays; a USB device must be plugged in)."
				    : "Not playing yet: the sound starts with the first app that plays.")
			       : "Playing on: ";
	for (int i = 0; a[i] && n < 94; i++) line[n++] = a[i];
	if (now != 0) { const char *t = out_title (now); for (int i = 0; t[i] && n < 94; i++) line[n++] = t[i]; }
	line[n] = 0;
	g_outNow->setText (line);
}

static void on_output (Widget &w)
{
	int k = ((Dropdown &) w).sel;
	if (k < 0 || k >= g_outN) return;
	volume_set_output (g_outVal[k]);		// (applied at once, kept in sound.ini)
	g_outLast = -2;
	read_output ();
}

static void show_value (int v)
{
	char s[16]; int n = ax_itoa (v, s);
	const char *t = " / 10"; for (int i = 0; t[i]; i++) s[n++] = t[i];
	s[n] = 0;
	g_value->setText (s);
}

static void read_volume (void)
{
	int r = kapi_sound_volume (-1, -1);
	int v = r & 0xFF, m = (r & 0x100) ? 1 : 0;
	if (g_vol->value != v) { g_vol->value = v; g_vol->invalidate (true); }
	if (g_mute->checked != (m != 0)) { g_mute->checked = m != 0; g_mute->invalidate (true); }
	show_value (v);
}

static void on_vol (Widget &w)
{
	int v = ((Slider &) w).value;
	int r = kapi_sound_volume (v, 0);			// (moving it unmutes, as the menu bar's)
	g_mute->checked = (r & 0x100) != 0; g_mute->invalidate (true);
	show_value (v);
	volume_save (v, g_mute->checked ? 1 : 0);
}

static void on_mute (Widget &w)
{
	int r = kapi_sound_volume (-1, ((Checkbox &) w).checked ? 1 : 0);
	volume_save (r & 0xFF, (r & 0x100) ? 1 : 0);
}

// A chime: C E G, a sine each, the notes one after the other (the voices 0..2).
static void on_test (Widget &)
{
	int a = kapi_sound_acquire ();
	if (a <= 0) { g_status->setText (a == 0 ? "Another app is playing: try again later." : "No sound output."); return; }
	static const unsigned NOTES[3] = { 523251, 659255, 783991 };
	for (int i = 0; i < 3; i++)
	{
		kapi_sound_start (i, NOTES[i], SOUND_SINE, 170);
		kapi_msleep (140);
	}
	kapi_msleep (320);
	kapi_sound_stop (-1);
	kapi_msleep (60);
	kapi_sound_release ();
	g_status->setText ("");
}

class SoundRoot : public Root
{
public:
	unsigned last = 0;
	SoundRoot () : Root (W, H, "Sound") {}
	void onTick () override
	{
		unsigned now = kapi_get_ticks ();
		if (now - last >= 50) { last = now; read_volume (); read_output (); }	// (the menu bar may change it; a USB device)
	}
};

int main (void)
{
	ft_wtk_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	SoundRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	int X = root.width > W ? (root.width - W) / 2 : 0;
	GroupBox *go = new GroupBox (X + 10, 8, W - 20, 90, "Output");
	root.addChild (go);
	int ct = go->contentTop () + 6;
	go->addChild (new Label (14, ct + 4, 90, 20, "Play on", C_TEXT, go->bg));
	g_outOpt[0] = out_title (KAPI_SND_OUT_AUTO); g_outVal[0] = KAPI_SND_OUT_AUTO; g_outN = 1;
	// (on the window, not in the group box: a parent clips its children, the open list needs the room)
	int outL = go->left + 110, outT = go->top + ct;
	g_outNow = new Label (14, ct + 34, W - 60, 20, "", C_DIS, go->bg); go->addChild (g_outNow);

	GroupBox *gv = new GroupBox (X + 10, 108, W - 20, 150, "Volume");
	root.addChild (gv);
	ct = gv->contentTop () + 8;
	gv->addChild (new Label (14, ct + 4, 90, 20, "Master", C_TEXT, gv->bg));
	g_vol = new Slider (110, ct, 380, 28, 0, 10, 10, on_vol, gv->bg); gv->addChild (g_vol);
	g_value = new Label (504, ct + 4, 80, 20, "", C_TEXT, gv->bg); gv->addChild (g_value);
	g_mute = new Checkbox (110, ct + 44, 200, 24, "Mute", false, on_mute, gv->bg); gv->addChild (g_mute);
	gv->addChild (new Button (110, ct + 80, 170, 30, "Play a test sound", on_test));

	g_status = new Label (X + 12, 270, W - 24, 22, "", C_DIS, root.bg);
	root.addChild (g_status);
	root.addChild (new Label (X + 12, H - 60, W - 24, 20, "Kept in SD:/etc/sound.ini. The menu bar's speaker changes it too.", C_DIS, root.bg));
	g_out = new Dropdown (outL, outT, 300, 26, g_outOpt, 1, 0, on_output);
	root.addChild (g_out);				// (last: over the groups when its list is open)
	read_volume ();
	read_output ();
	root.run ();
	return 0;
}
