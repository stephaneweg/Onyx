//
// soundconf -- the Control Panel's Sound applet (applet_proto.h; alone, a window of its own): the
// output (kapi v84 sound_output: Automatic, the jack, a USB headset, HDMI -- the ones that are there),
// the input (kapi v99 sound_input: a USB microphone or an I2S one on the header; its gain; a test with a level
// meter, through AudioKit's ak_in_*),
// the master volume (0..10) and mute (kapi v60 sound_volume), applied at once to everything played and
// kept in SD:/etc/sound.ini (volume.h: the menu bar applies it at start); a test sound (a short
// chime, when no app holds the sound output). The menu bar's speaker changes the same volume: the
// applet follows it.
//
#include "audiokit/audiokit.h"
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

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
	return o == KAPI_SND_OUT_JACK ? TR ("Headphone jack (3.5 mm)") : o == KAPI_SND_OUT_USB ? TR ("USB headset / DAC")
	     : o == KAPI_SND_OUT_HDMI ? TR ("HDMI (the screen)") : TR ("Automatic");
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
	static char line[200];			// (room for another language's words)
	int now = KAPI_SND_OUT_NOW (o), n = 0;
	const char *a = now == 0 ? (asked == KAPI_SND_OUT_USB || asked == KAPI_SND_OUT_AUTO
				    ? TR ("Not playing yet (the sound starts with the first app that plays; a USB device must be plugged in).")
				    : TR ("Not playing yet: the sound starts with the first app that plays."))
			       : TR ("Playing on: ");
	for (int i = 0; a[i] && n < 198; i++) line[n++] = a[i];
	if (now != 0) { const char *t = out_title (now); for (int i = 0; t[i] && n < 198; i++) line[n++] = t[i]; }
	line[n] = 0;
	g_outNow->setText (line);
}

// ---- the input (kapi v99): a microphone ------------------------------------------------------------------------
static Dropdown *g_in;
static Label    *g_inNow, *g_gainVal;
static Slider   *g_gain;
static Progress *g_level;
static Button   *g_inTest;
static const char *g_inOpt[4];
static int g_inVal[4], g_inN, g_inLast = -2, g_peak;
static bool g_testing;
static unsigned g_gainAt; static int g_gainDirty;

static const char *in_title (int i)
{
	return i == KAPI_SND_IN_NONE ? TR ("None") : i == KAPI_SND_IN_USB ? TR ("USB microphone / headset")
	     : i == KAPI_SND_IN_I2S ? TR ("I2S microphone (GPIO 18, 19, 20)") : TR ("Automatic");
}

static void gain_value (int db)
{
	char t[16]; int n = ax_itoa (db, t); const char *u = " dB"; for (int i = 0; u[i]; i++) t[n++] = u[i];
	t[n] = 0;
	g_gainVal->setText (t);
}

// The list, the gain and the line under them, from the kernel (every half second: a USB microphone comes and goes).
static void read_input (void)
{
	long r = kapi_sound_input (KAPI_SND_IN_SELECT, -1, -1);
	if ((int) r == g_inLast) return;
	g_inLast = (int) r;
	if (r < 0)					// a kernel before v99: no input
	{
		g_inOpt[0] = in_title (KAPI_SND_IN_NONE); g_inVal[0] = KAPI_SND_IN_NONE; g_inN = 1;
		g_in->setOptions (g_inOpt, 1, 0);
		g_inNow->setText (TR ("This system cannot record yet (its kernel is older)."));
		return;
	}
	int asked = KAPI_SND_IN_ASKED (r), sel = 0;
	g_inN = 0;
	for (int i = KAPI_SND_IN_AUTO; i <= KAPI_SND_IN_I2S; i++)
	{
		if (i == KAPI_SND_IN_USB && i != asked && !KAPI_SND_IN_HAS (r, i)) continue;
		if (i == asked) sel = g_inN;
		g_inOpt[g_inN] = in_title (i); g_inVal[g_inN++] = i;
	}
	g_in->setOptions (g_inOpt, g_inN, sel);
	g_in->invalidate (true);
	if (!g_gainDirty)
	{
		int db = (int) KAPI_SND_IN_GAIN (r);
		if (g_gain->value != db) { g_gain->value = db; g_gain->invalidate (true); }
		gain_value (db);
	}
	int now = (int) KAPI_SND_IN_NOW (r);
	const char *t = now == KAPI_SND_IN_USB ? TR ("Recording now: the USB microphone.")
		      : now == KAPI_SND_IN_I2S ? TR ("Recording now: the I2S microphone.")
		      : asked == KAPI_SND_IN_NONE ? TR ("No microphone.")
		      : asked == KAPI_SND_IN_I2S ? TR ("An INMP441 or SPH0645 on the header; L/R to the ground.")
		      : KAPI_SND_IN_HAS (r, KAPI_SND_IN_USB) ? TR ("A USB microphone is there.")
		      : TR ("No USB microphone: plug one in, or choose I2S.");
	g_inNow->setText (t);
}

static void button_text (Button *b, const char *t)
{
	unsigned i = 0;
	for (; t[i] && i < sizeof b->text - 1; i++) b->text[i] = t[i];
	b->text[i] = 0;
}

static void test_stop (void)
{
	if (!g_testing) return;
	ak_in_close ();
	g_testing = false; g_peak = 0;
	button_text (g_inTest, TR ("Test"));
	g_inTest->invalidate (true);
	g_level->setValue (0);
}

static void on_input (Widget &w)
{
	int k = ((Dropdown &) w).sel;
	if (k < 0 || k >= g_inN) return;
	bool again = g_testing;
	test_stop ();					// (the next recording takes the new one)
	volume_set_input (g_inVal[k], -1);		// (kept in sound.ini)
	g_inLast = -2;
	read_input ();
	if (again) { void on_test_in (Widget &); on_test_in (*g_inTest); }
}

static void on_gain (Widget &w)
{
	int db = ((Slider &) w).value;
	kapi_sound_input (KAPI_SND_IN_SELECT, -1, db);	// (heard at once; the file once the slider rests)
	gain_value (db);
	g_gainDirty = 1; g_gainAt = kapi_get_ticks ();
}

void on_test_in (Widget &)
{
	if (g_testing) { test_stop (); read_input (); return; }
	int r = ak_in_open (16000);
	if (r <= 0)
	{
		g_status->setText (r == 0 ? TR ("Another program is recording.") : TR ("No microphone to record from."));
		return;
	}
	g_testing = true; g_peak = 0;
	g_status->setText (TR ("Speak: the bar shows the microphone's level."));
	button_text (g_inTest, TR ("Stop"));
	g_inTest->invalidate (true);
	g_inLast = -2;
}

// While it tests: the samples read every frame, the bar the loudest one (falling slowly).
static void test_tick (void)
{
	if (g_gainDirty && kapi_get_ticks () - g_gainAt >= 100) { g_gainDirty = 0; volume_set_input (-1, g_gain->value); }
	if (!g_testing) return;
	static short buf[2048];
	int n = ak_in_read (buf, 2048), p = 0;
	if (n < 0) { test_stop (); g_status->setText (TR ("The microphone is gone.")); g_inLast = -2; return; }
	if (n > 0) p = ak_in_peak (buf, n) * 100 / 32767;
	g_peak = p > g_peak ? p : g_peak > 2 ? g_peak - 2 : 0;
	if (g_level->value != g_peak) g_level->setValue (g_peak);
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
	static const unsigned NOTES[3] = { 523251, 659255, 783991 };
	for (int i = 0; i < 3; i++)
	{
		ak_fm_start (i, NOTES[i], SOUND_SINE, 170);
		kapi_msleep (140);
	}
	kapi_msleep (320);
	ak_fm_stop (-1);
	g_status->setText ("");
}

// ---- the mixer (kapi v85): the programs that play, each with its own volume ------------------------
#define MIX_ROWS 3
static GroupBox *g_mixBox;
static Label    *g_mixName[MIX_ROWS], *g_mixVal[MIX_ROWS], *g_mixNone;
static Slider   *g_mixVol[MIX_ROWS];
static Checkbox *g_mixMute[MIX_ROWS];
static Progress *g_mixLevel[MIX_ROWS];
static struct kapi_sound_client g_mix[MIX_ROWS];
static int g_mixN = -1;
static int g_mixDirty[MIX_ROWS]; static unsigned g_mixAt;	// rows to write to mixer.ini, once the slider rests

static void mix_value (int i)
{
	char t[12]; int n = ax_itoa (g_mixVol[i]->value, t); t[n++] = ' '; t[n++] = '%'; t[n] = 0;
	g_mixVal[i]->setText (t);
}
static int mix_row (Widget &w, Widget **list)
{
	for (int i = 0; i < MIX_ROWS; i++) if (list[i] == &w) return i;
	return -1;
}
static void on_mix_vol (Widget &w)
{
	int i = mix_row (w, (Widget **) g_mixVol);
	if (i < 0 || i >= g_mixN) return;
	kapi_sound_client_volume (g_mix[i].pid, g_mixVol[i]->value, -1);	// (heard at once; the file a little later)
	g_mix[i].volume = g_mixVol[i]->value;
	mix_value (i);
	g_mixDirty[i] = 1; g_mixAt = kapi_get_ticks ();
}
static void on_mix_mute (Widget &w)
{
	int i = mix_row (w, (Widget **) g_mixMute);
	if (i < 0 || i >= g_mixN) return;
	g_mix[i].mute = g_mixMute[i]->checked ? 1 : 0;
	mixer_set (&g_mix[i], -1, g_mix[i].mute);
}
// The rows follow the kernel's list (every half second): a program starts to play, another ends.
static void read_mixer (void)
{
	unsigned now = kapi_get_ticks ();
	for (int i = 0; i < MIX_ROWS; i++)			// the sliders at rest for a second: the file
		if (g_mixDirty[i] && now - g_mixAt >= 100) { g_mixDirty[i] = 0; if (i < g_mixN) mixer_set (&g_mix[i], g_mix[i].volume, -1); }
	struct kapi_sound_client c[16];
	int n = kapi_sound_clients (c, 16);
	if (n < 0) { if (g_mixN != 0) { g_mixN = 0; g_mixNone->setText (TR ("This kernel has no mixer.")); } return; }
	if (n > MIX_ROWS) n = MIX_ROWS;
	bool same = n == g_mixN;
	for (int i = 0; same && i < n; i++) same = c[i].pid == g_mix[i].pid;
	for (int i = 0; i < MIX_ROWS; i++)
	{
		bool on = i < n;
		if (on)
		{
			bool drag = g_mixDirty[i] != 0;			// (being moved here: not pulled back)
			if (!same) { g_mixName[i]->setText (c[i].name); g_mixDirty[i] = 0; drag = false; }
			unsigned pid = c[i].pid; int vol = drag ? g_mix[i].volume : c[i].volume;
			g_mix[i] = c[i]; g_mix[i].pid = pid; g_mix[i].volume = vol;
			if (g_mixVol[i]->value != vol) { g_mixVol[i]->value = vol; g_mixVol[i]->invalidate (true); }
			if (g_mixMute[i]->checked != (c[i].mute != 0)) { g_mixMute[i]->checked = c[i].mute != 0; g_mixMute[i]->invalidate (true); }
			int lv = c[i].peak * 100 / 32767;
			if (g_mixLevel[i]->value != lv) { g_mixLevel[i]->value = lv; g_mixLevel[i]->invalidate (true); }
			mix_value (i);
		}
		Widget *ws[5] = { g_mixName[i], g_mixVol[i], g_mixVal[i], g_mixMute[i], g_mixLevel[i] };
		for (int k = 0; k < 5; k++) if (ws[k]->hidden != !on) { ws[k]->hidden = !on; g_mixBox->invalidate (true); }
	}
	if (g_mixNone->hidden != (n > 0)) { g_mixNone->hidden = n > 0; g_mixBox->invalidate (true); }
	g_mixN = n;
}

class SoundRoot : public Root
{
public:
	unsigned last = 0;
	SoundRoot () : Root (W, H, TR ("Sound")) {}
	void onTick () override
	{
		unsigned now = kapi_get_ticks ();
		if (now - last >= 50) { last = now; read_volume (); read_output (); read_input (); read_mixer (); }	// (the menu bar may change it; a USB device)
		test_tick ();
	}
};

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();				// the words in the system's language (before the widgets)
	SoundRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	int X = root.width > W ? (root.width - W) / 2 : 0;
	GroupBox *go = new GroupBox (X + 10, 8, W - 20, 150, TR ("Output and input"));
	root.addChild (go);
	int ct = go->contentTop () + 6;
	go->addChild (new Label (14, ct + 4, 94, 20, TR ("Play on"), C_TEXT, go->bg));
	g_outOpt[0] = out_title (KAPI_SND_OUT_AUTO); g_outVal[0] = KAPI_SND_OUT_AUTO; g_outN = 1;
	// (on the window, not in the group box: a parent clips its children, the open list needs the room)
	int outL = go->left + 110, outT = go->top + ct;
	g_outNow = new Label (14, ct + 32, W - 60, 20, "", C_DIS, go->bg); go->addChild (g_outNow);
	// the input: the list (on the window, as the output's), the level, the test, the gain
	go->addChild (new Label (14, ct + 64, 94, 20, TR ("Record from"), C_TEXT, go->bg));
	g_inOpt[0] = in_title (KAPI_SND_IN_AUTO); g_inVal[0] = KAPI_SND_IN_AUTO; g_inN = 1;
	int inL = go->left + 110, inT = go->top + ct + 60;
	g_level = new Progress (420, ct + 67, 120, 12, 0, 100, 0); go->addChild (g_level);
	g_inTest = new Button (552, ct + 59, 100, 28, TR ("Test"), on_test_in); go->addChild (g_inTest);
	g_inNow = new Label (14, ct + 94, 390, 20, "", C_DIS, go->bg); go->addChild (g_inNow);
	go->addChild (new Label (420, ct + 94, 44, 20, TR ("Gain"), C_TEXT, go->bg));
	g_gain = new Slider (466, ct + 90, 130, 28, 0, 48, 0, on_gain, go->bg); go->addChild (g_gain);
	g_gainVal = new Label (602, ct + 94, 56, 20, "", C_TEXT, go->bg); go->addChild (g_gainVal);

	GroupBox *gv = new GroupBox (X + 10, 164, W - 20, 104, TR ("Volume"));
	root.addChild (gv);
	ct = gv->contentTop () + 8;
	gv->addChild (new Label (14, ct + 4, 90, 20, TR ("Master"), C_TEXT, gv->bg));
	g_vol = new Slider (110, ct, 380, 28, 0, 10, 10, on_vol, gv->bg); gv->addChild (g_vol);
	g_value = new Label (504, ct + 4, 80, 20, "", C_TEXT, gv->bg); gv->addChild (g_value);
	g_mute = new Checkbox (110, ct + 40, 120, 24, TR ("Mute"), false, on_mute, gv->bg); gv->addChild (g_mute);
	gv->addChild (new Button (250, ct + 38, 170, 30, TR ("Play a test sound"), on_test));

	// the mixer: a row per program that plays (the first MIX_ROWS of them)
	g_mixBox = new GroupBox (X + 10, 274, W - 20, 136, TR ("Programs playing"));
	root.addChild (g_mixBox);
	ct = g_mixBox->contentTop () + 4;
	g_mixNone = new Label (14, ct + 4, W - 60, 20, TR ("No program is playing."), C_DIS, g_mixBox->bg); g_mixBox->addChild (g_mixNone);
	for (int i = 0; i < MIX_ROWS; i++)
	{
		int y = ct + i * 32;
		g_mixName[i] = new Label (14, y + 4, 130, 20, "", C_TEXT, g_mixBox->bg);
		g_mixVol[i] = new Slider (150, y, 260, 28, 0, 100, 100, on_mix_vol, g_mixBox->bg);
		g_mixVal[i] = new Label (420, y + 4, 50, 20, "", C_TEXT, g_mixBox->bg);
		g_mixMute[i] = new Checkbox (476, y + 2, 76, 24, TR ("Mute"), false, on_mix_mute, g_mixBox->bg);
		g_mixLevel[i] = new Progress (560, y + 8, 100, 12, 0, 100, 0);
		Widget *ws[5] = { g_mixName[i], g_mixVol[i], g_mixVal[i], g_mixMute[i], g_mixLevel[i] };
		for (int k = 0; k < 5; k++) { ws[k]->hidden = true; g_mixBox->addChild (ws[k]); }
	}

	g_status = new Label (X + 12, 414, W - 24, 20, "", C_DIS, root.bg);
	root.addChild (g_status);
	root.addChild (new Label (X + 12, H - 34, W - 24, 20, TR ("Kept in SD:/etc/sound.ini and mixer.ini. The menu bar's speaker changes the master too."), C_DIS, root.bg));
	g_in = new Dropdown (inL, inT, 300, 26, g_inOpt, 1, 0, on_input);
	root.addChild (g_in);				// (last: over the groups when its list is open)
	g_out = new Dropdown (outL, outT, 300, 26, g_outOpt, 1, 0, on_output);
	root.addChild (g_out);				// (the output's last of all: its list opens over the input's)
	{ Widget *g[3] = { go, gv, g_mixBox }; uk_fit_pane (&root, W, H, g, 3); }	// (pocket's 640 px pane: the layout fitted to it)
	read_volume ();
	read_output ();
	read_input ();
	read_mixer ();
	root.run ();
	ak_in_close ();
	return 0;
}
