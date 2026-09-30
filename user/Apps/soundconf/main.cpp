//
// soundconf -- the Control Panel's Sound applet (applet_proto.h; alone, a window of its own): the
// master volume (0..10) and mute (kapi v60 sound_volume), applied at once to everything played and
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
		if (now - last >= 50) { last = now; read_volume (); }	// (the menu bar may change it)
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
	go->addChild (new Label (14, ct, W - 60, 20, "The Raspberry Pi's headphone jack (3.5 mm): the apps' music, sounds and", C_TEXT, go->bg));
	go->addChild (new Label (14, ct + 22, W - 60, 20, "emulators, mixed.", C_TEXT, go->bg));

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
	read_volume ();
	root.run ();
	return 0;
}
