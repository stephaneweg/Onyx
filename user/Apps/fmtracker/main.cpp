//
// fmtracker -- the Onyx FM tracker: 8 channels of 2-operator FM instruments (the kernel's
// FM synthesizer, ABI v47) on a grid of time slices; reads and writes FM Song's .FMS
// files and .FMI instruments (fms.h).
//
//   * The window: a transport bar (play, from the start, stop, loop the pattern, the position
//     and the time, undo / redo, cut / copy / paste, follow), on the left the song's patterns
//     (new, duplicate, up, down, delete; the pattern's rows and speed; the octave and the step),
//     the grid, a piano under it.
//   * A column per channel; its header: the instrument (a click opens the instrument dialog),
//     M (mute the channel in this pattern; a right click too), S (solo, while the app runs), a
//     level meter. A row = one time slice (speed / 20 s).
//   * A cell: a note starts there ("C#4"), "---" = the note goes on, empty = silence. A
//     note lasts until the next note or silence of its channel (C4 then C4 = two notes).
//   * Keys: C D E F G A B enter a note (Shift = sharp; the cursor then goes `step` rows down), 0-7
//     the octave, Space a silence, Delete "---" (a block chosen: cleared), Backspace clears the
//     slice above, # toggles the sharp, Ctrl+Up / Ctrl+Down move the note(s) a semitone up / down,
//     arrows / Page Up / Page Down / Home / End move, Shift + those choose a block, Tab the next
//     channel. ^X ^C ^V the block, ^A the whole pattern, ^Z / ^Y undo / redo (the pattern's notes).
//     A click selects a cell, a drag a block; the wheel and the scrollbar scroll. A click on
//     the piano enters that note.
//   * Play (^P) plays from the cursor, follows the position and highlights it; Esc stops.
//     A song is a list of patterns (each with its own length and speed), played in order.
//
#include "audiokit/audiokit.h"
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "filekit/filekit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "fontkit/uikitface.h"			// FreeType's text (DejaVu Sans) for every widget
#include "fms.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

using namespace uikit;

static int g_W = 980, g_H = 660;	// the window's size (resizable: PocketUI fills it; the grid and the piano follow it)
#define W	g_W
#define H	g_H
#define TOOL_H	50
#define SIDE_W	156
#define ST_H	22
#define PIANO_H	58
#define HEAD_Y	TOOL_H
#define HEAD_H	46
#define GRID_X	SIDE_W
#define GRID_Y	(HEAD_Y + HEAD_H)
#define SB_W	14
#define NUM_W	40
#define COL_W	((W - SIDE_W - SB_W - NUM_W) / FMS_CH)
#define GRID_W	(NUM_W + FMS_CH * COL_W)
#define GRID_H	(H - ST_H - PIANO_H - GRID_Y)
#define ROW_H	(uk_fh () + 2)
#define INS_DIR	"SD:/apps/fmtracker.app/ins"
#define SONG_DIR "SD:/music/fms"
// The kinds of files the Open and Save dialogs offer (uikit/dialog.h; a name typed without an extension takes
// the kind's as it is written here: in capitals, as this program writes them).
static const char *const FMS_KINDS = "FM songs|*.FMS|All files|*";
static const char *const FMI_KINDS = "FM instruments|*.FMI|All files|*";
#define TEST_VOICE 15

static FmsSong g_song;
static int g_pat = 0, g_row = 0, g_ch = 0, g_top = 0, g_oct = 4, g_step = 1;
static bool g_sel = false; static int g_ar = 0, g_ac = 0;	// a block chosen: from this cell to the cursor
static bool g_playing = false, g_dirty = false, g_loop = false, g_follow = true;
static int g_solo = -1;					// the channel heard alone (-1: none)
static int g_playPat = 0, g_playRow = -1, g_shownRow = -1; static unsigned g_nextTick = 0;	// next row to play / the one sounding
static unsigned g_previewEnd = 0; static int g_previewVoice = -1;
static char g_path[256] = "";
static int g_audio = 0;				// 1 = the output is ours, -1 = unavailable
static int g_vu[FMS_CH]; static unsigned char g_snd[FMS_CH]; static unsigned g_vuTick = 0;	// the meters, the notes sounding

// the channels' colours (their headers' strip, the piano's lit keys)
static const unsigned CH_COL[FMS_CH] = { 0xC98A36, 0xBC5069, 0x5E9F58, 0xC85C40, 0x8E6CC4, 0x4A9FB0, 0xB5A040, 0x7484C8 };
static unsigned col_side () { return uk_mix (C_BG, C_FIELD, 70); }		// the side panel (as the Media Player's)
static unsigned f_mix (int t) { return uk_mix (C_FIELD, C_FIELD_TEXT, t); }	// a shade of the grid's background

class Grid; class ChanHeader; class PatList; class Piano;
static Grid *g_grid = 0;
static Widget *g_side = 0;			// the side panel
static ChanHeader *g_head[FMS_CH];
static PatList *g_plist = 0;
static Piano *g_piano = 0;
static Scrollbar *g_sb = 0;
static Label *g_status = 0, *g_songLabel = 0, *g_authorLabel = 0;
static LcdDisplay *g_lcd = 0;
static NumericUpDown *g_rowsBox = 0, *g_speedBox = 0, *g_octBox = 0, *g_stepBox = 0;
static ToolButton *g_btPlay = 0, *g_btLoop = 0, *g_btFollow = 0;
static Root *g_root = 0;

static FmsPattern &pat () { return g_song.pat[g_pat]; }

static void redraw ();			// repaint the grid, the headers, the list, the piano (below)
static int visible_rows () { return GRID_H / ROW_H; }

static void set_status (const char *a, const char *b = "")
{
	char s[200]; snprintf (s, sizeof s, "%s%s", a, b);
	g_status->setText (s);
}

// ---- the look (the theme's) -------------------------------------------------------------------------
// A heading on the face: bold, dimmed.
class Heading : public Label
{
public:
	Heading (int l, int t, int w, int h, const char *s, unsigned bg_) : Label (l, t, w, h, s, C_TEXT, bg_) {}
	void onDraw () override { canvas.clear (bg); uk_text_l (canvas, 2, 0, height, text, uk_mix (bg, C_TEXT, 150), 2); }
};

// The status bar: the face's gradient, an etched line along its top.
class StatusBar : public Label
{
public:
	StatusBar (int l, int t, int w, int h) : Label (l, t, w, h, "", C_TEXT, C_FACE) {}
	void onDraw () override
	{
		uk_rbox (canvas, 0, 0, width, height, 0, uk_tone (C_FACE, 170), uk_tone (C_FACE, 130));
		uk_etch_h (canvas, 0, 0, width, C_FACE);
		uk_text_l (canvas, 8, 2, height - 2, text, fg);
	}
};

// ---- audio ------------------------------------------------------------------------------------------
static void upload (int ch)
{
	if (g_audio != 1) return;
	struct kapi_fm_instrument k;
	fms_to_kapi (&g_song.ins[ch], &k);
	ak_fm_instrument (ch, &k);
}
static bool audio ()				// (AudioKit's FM synthesizer: its player takes the output when a note sounds)
{
	if (g_audio == 1) return true;
	g_audio = 1;
	for (int c = 0; c < FMS_CH; c++) upload (c);
	return true;
}
static void note_on (int voice, unsigned char v)
{
	if (voice >= 0 && voice < FMS_CH) { g_vu[voice] = 100; g_snd[voice] = v; }
	if (audio ()) ak_fm_start (voice, fms_note_mhz (v), SOUND_FM, 220);
}
static void note_off (int voice)
{
	if (voice < 0) for (int c = 0; c < FMS_CH; c++) g_snd[c] = 0;
	else if (voice < FMS_CH) g_snd[voice] = 0;
	if (g_audio == 1) ak_fm_stop (voice);
}
static bool silenced (int c) { return pat ().mute[c] || (g_solo >= 0 && g_solo != c); }
static void preview (int ch, unsigned char v)			// hear a note just entered
{
	if (g_playing || silenced (ch)) return;
	note_on (ch, v);
	g_previewVoice = ch; g_previewEnd = kapi_get_ticks () + 35;
}

// ---- undo: the pattern's notes before each change -------------------------------------------------------
struct Snap { int pat, rows, speed; unsigned char mute[FMS_CH]; unsigned char *n; };
enum { UNDO_MAX = 48 };
static Snap g_undo[UNDO_MAX], g_redo[UNDO_MAX]; static int g_nundo = 0, g_nredo = 0;

static void snap_push (Snap *st, int &n)
{
	if (n == UNDO_MAX) { delete [] st[0].n; memmove (st, st + 1, (UNDO_MAX - 1) * sizeof (Snap)); n--; }
	FmsPattern &p = pat ();
	Snap &s = st[n++];
	s.pat = g_pat; s.rows = p.rows; s.speed = p.speed;
	memcpy (s.mute, p.mute, FMS_CH);
	s.n = new unsigned char[FMS_CH * p.rows];
	memcpy (s.n, p.n, (size_t) FMS_CH * p.rows);
}
static void snap_clear (Snap *st, int &n) { for (int i = 0; i < n; i++) delete [] st[i].n; n = 0; }
static void undo_clear () { snap_clear (g_undo, g_nundo); snap_clear (g_redo, g_nredo); }	// (the patterns' list changed)
static void undo_push () { snap_clear (g_redo, g_nredo); snap_push (g_undo, g_nundo); g_dirty = true; }
static void refresh_pattern_ui ();
static void snap_back (Snap *from, int &nf, Snap *to, int &nt, const char *what)
{
	if (!nf) { set_status ("Nothing to ", what); return; }
	Snap s = from[--nf];
	if (s.pat >= g_song.npat) { delete [] s.n; return; }
	g_pat = s.pat;
	snap_push (to, nt);
	FmsPattern &p = pat ();
	delete [] p.n;
	p.n = s.n; p.rows = s.rows; p.speed = s.speed; memcpy (p.mute, s.mute, FMS_CH);
	g_dirty = true; g_sel = false;
	refresh_pattern_ui ();
}
static void op_undo () { snap_back (g_undo, g_nundo, g_redo, g_nredo, "undo."); }
static void op_redo () { snap_back (g_redo, g_nredo, g_undo, g_nundo, "redo."); }

// ---- the view ---------------------------------------------------------------------------------------
static void sync_scroll ()
{
	int vis = visible_rows (), mx = pat ().rows - vis;
	if (mx < 0) mx = 0;
	if (g_top > mx) g_top = mx;
	if (g_top < 0) g_top = 0;
	g_sb->vmax = mx > 0 ? mx : 1;
	g_sb->value = g_top;
	g_sb->invalidate (true);
}
static void ensure_visible (int row, bool center)
{
	int vis = visible_rows ();
	if (center) g_top = row - vis / 2;
	else if (row < g_top) g_top = row;
	else if (row >= g_top + vis) g_top = row - vis + 1;
	sync_scroll ();
}
// The position's display: the pattern and the row (played, else the cursor's), the time from the song's start.
static void update_lcd ()
{
	int p = g_playing ? g_playPat : g_pat, r = g_playing ? (g_shownRow < 0 ? 0 : g_shownRow) : g_row;
	if (p >= g_song.npat) p = g_song.npat - 1;
	unsigned cs = 0;						// 1/100 s (a row: speed / 20 s)
	for (int i = 0; i < p; i++) cs += (unsigned) g_song.pat[i].rows * g_song.pat[i].speed * 5;
	cs += (unsigned) r * g_song.pat[p].speed * 5;
	char t[32], s[32];
	snprintf (t, sizeof t, "%02d:%03d", p + 1, r);
	snprintf (s, sizeof s, "%u:%02u.%u", cs / 6000, cs / 100 % 60, cs / 10 % 10);
	g_lcd->setText (t); g_lcd->setSub (s);
}
static void refresh_pattern_ui ()
{
	g_rowsBox->value = pat ().rows; g_rowsBox->invalidate (true);
	g_speedBox->value = pat ().speed; g_speedBox->invalidate (true);
	if (g_row >= pat ().rows) g_row = pat ().rows - 1;
	if (g_ar >= pat ().rows) g_sel = false;
	sync_scroll ();
	redraw ();
}
static void sel_rect (int &r0, int &r1, int &c0, int &c1)	// the block chosen (the cursor's cell alone without one)
{
	r0 = r1 = g_row; c0 = c1 = g_ch;
	if (!g_sel) return;
	if (g_ar < r0) r0 = g_ar; else r1 = g_ar;
	if (g_ac < c0) c0 = g_ac; else c1 = g_ac;
}

// ---- the instrument dialog -----------------------------------------------------------------------------
static const char *const WAVES[4] = { "Sine", "Half sine", "Abs sine", "Pulses" };
static const char *const CONNS[2] = { "FM", "Additive" };
static char g_presetNames[96][13]; static const char *g_presetPtr[96]; static int g_npreset = 0;

static void load_presets ()
{
	if (g_npreset) return;
	g_presetPtr[g_npreset] = "(preset...)"; g_npreset++;
	void *d = kapi_opendir (INS_DIR);
	if (!d) return;
	struct kapi_dirent e;
	while (g_npreset < 96 && kapi_readdir (d, &e))
	{
		int l = fms_len (e.name);
		if (e.is_dir || l < 5 || fs_lower (e.name[l - 1]) != 'i' || e.name[l - 4] != '.') continue;
		int k = 0; for (int i = 0; i < l - 4 && k < 12; i++) g_presetNames[g_npreset][k++] = e.name[i];
		g_presetNames[g_npreset][k] = 0;
		g_presetPtr[g_npreset] = g_presetNames[g_npreset];
		g_npreset++;
	}
	kapi_closedir (d);
	for (int i = 2; i < g_npreset; i++)			// sort (after the placeholder)
		for (int j = i; j > 1 && fs_ci_cmp (g_presetPtr[j - 1], g_presetPtr[j]) > 0; j--)
		{ const char *t = g_presetPtr[j]; g_presetPtr[j] = g_presetPtr[j - 1]; g_presetPtr[j - 1] = t; }
}

static bool read_fmi (const char *path, FmsIns *in)
{
	void *f = kapi_open (path);
	if (!f) return false;
	char b[1024]; int n = kapi_read (f, b, sizeof b - 1); kapi_close (f);
	if (n <= 0) return false;
	b[n] = 0;
	return fmi_parse (b, in);
}

// An operator's wave: one period of a sine reshaped (OPL2's four), y in -1 .. 1.
static double wave_at (int wave, double ph)
{
	double s = sin (ph), half = fmod (ph, M_PI);
	switch (wave & 3)
	{
	case 1: return s > 0 ? s : 0;
	case 2: return fabs (s);
	case 3: return half < M_PI / 2 ? fabs (s) : 0;
	default: return s;
	}
}
// The wave chooser: the four waves drawn, a click picks one.
class WavePicker : public Widget
{
public:
	int sel, hot; Action cb;
	WavePicker (int l, int t, int w, int h, int s, Action cb_) : Widget (l, t, w, h), sel (s), hot (-1), cb (cb_) {}
	int cellW () const { return width / 4; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		for (int k = 0; k < 4; k++)
		{
			int x = k * cellW (), w = cellW () - 6;
			bool on = k == sel;
			unsigned bg = on ? uk_mix (C_FIELD, C_ACCENT, 50) : C_FIELD;
			uk_rbox (canvas, x, 0, w, height, 5, bg, bg);
			uk_rline (canvas, x, 0, w, height, 5, on || k == hot ? C_ACCENT : uk_tone (C_FACE, 72), on ? 255 : 200);
			if (on) uk_rline (canvas, x + 1, 1, w - 2, height - 2, 4, C_ACCENT, 255);
			int mid = height / 2, amp = height / 2 - 6, py = -1;
			for (int i = 0; i <= w - 12; i++)			// two periods
			{
				int y = mid - (int) (wave_at (k, i * 4 * M_PI / (w - 12)) * amp);
				int a = py < 0 ? y : (py < y ? py : y), b = py < 0 ? y : (py > y ? py : y);
				canvas.fillRect (x + 6 + i, a, 2, b - a + 2, on ? C_ACCENT : uk_mix (C_FIELD, C_FIELD_TEXT, 150));
				py = y;
			}
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int h = (mx < 0 || my < 0 || my >= height || mx >= width) ? -1 : mx / cellW ();
		if (h != hot) { hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (h >= 0 && h < 4 && h != sel) { sel = h; invalidate (true); if (cb) cb (*this); } }
		else if (!bl) pressed = false;
		return h >= 0;
	}
};

class InsDialog;
static InsDialog *g_dlg = 0;
static void dlg_btn (Widget &w);
static void dlg_preset (Widget &w);
static void dlg_changed (Widget &w);

// The instrument's editor: its name, a preset, the algorithm (FM: operator 1 modulates operator 2;
// additive: both are heard) and the feedback, then each operator: its wave (four drawn, a click),
// its envelope drawn as the sliders move (attack, decay, sustain, release), its volume, its
// frequency's multiplier, its key scaling, its switches. Test plays it; so does every change.
class InsDialog : public Modal
{
public:
	FmsIns ins; int ch;
	Textbox *name;
	Dropdown *preset;
	SegmentedControl *conn;
	WavePicker *wave[2];
	Slider *sl[2][7], *fb;			// attack, decay, sustain, release, volume, multiplier, key scale
	Checkbox *chk[2][4];			// sustained, tremolo, vibrato, ksr
	int yB, yO, yS;				// the algorithm's row, the operators' top, their sliders'
	bool anim; unsigned animStart, animShown;	// Test: the wave drawn as the note sounds (its envelopes over time)
	enum { B_OK = 1, B_CANCEL = 0, B_TEST = 2, B_LOAD = 3, B_SAVE = 4 };
	enum { COLW = 308 };
	int colX (int o) const { return 16 + o * (COLW + 16); }

	// A slider's value <-> the instrument's byte: sustain and volume are attenuations in the file
	// (0 = loud): shown the other way round.
	static int field (int r) { static const int f[7] = { P_AR, P_DR, P_SL, P_RR, P_TL, P_MULT, P_KSL }; return f[r]; }
	static int hi (int r) { static const int h[7] = { 15, 15, 15, 15, 63, 15, 3 }; return h[r]; }
	static int to_slider (int r, int v) { return r == 2 || r == 4 ? hi (r) - v : v; }

	InsDialog (int channel) : Modal (664, 620), ch (channel), anim (false), animStart (0), animShown (0)
	{
		left = (W - width) / 2; top = (H - height) / 2;
		ins = g_song.ins[ch];
		int y = titleH () + 12;
		addChild (new Label (16, y + 4, 46, 20, "Name", C_TEXT, C_FACE));
		name = new Textbox (64, y, 110, 26, ins.name); addChild (name);
		load_presets ();
		preset = new Dropdown (186, y, 190, 26, g_presetPtr, g_npreset, 0, dlg_preset);
		Button *b;
		b = new Button (width - 190, y - 1, 84, 28, "Load...", dlg_btn); b->tag = B_LOAD; addChild (b);
		b = new Button (width - 100, y - 1, 84, 28, "Save...", dlg_btn); b->tag = B_SAVE; addChild (b);
		yB = y + 40;
		addChild (new Label (16, yB + 4, 70, 20, "Algorithm", C_TEXT, C_FACE));
		conn = new SegmentedControl (90, yB, 170, 26, CONNS, 2, ins.p[P_CON] & 1, dlg_changed); addChild (conn);
		addChild (new Label (446, yB + 4, 66, 20, "Feedback", C_TEXT, C_FACE));
		fb = new Slider (516, yB + 3, 100, 20, 0, 7, ins.p[P_FB] & 7, dlg_changed, C_FACE); addChild (fb);
		yO = yB + 42; yS = yO + 132;
		static const char *const rows[7] = { "Attack", "Decay", "Sustain", "Release", "Volume", "Multiplier", "Key scale" };
		static const char *const flags[4] = { "Held (sustain)", "Tremolo", "Vibrato", "Key scale rate" };
		static const int ffield[4] = { P_EGT, P_AM, P_VIB, P_KSR };
		for (int o = 0; o < 2; o++)
		{
			int x = colX (o);
			wave[o] = new WavePicker (x, yO + 22, COLW, 30, ins.p[P_WAVE + o] & 3, dlg_changed); addChild (wave[o]);
			for (int r = 0; r < 7; r++)
			{
				addChild (new Label (x, yS + r * 24 + 1, 76, 20, rows[r], C_TEXT, C_FACE));
				sl[o][r] = new Slider (x + 80, yS + r * 24, COLW - 116, 20, 0, hi (r), to_slider (r, ins.p[field (r) + o]), dlg_changed, C_FACE);
				addChild (sl[o][r]);
			}
			for (int f = 0; f < 4; f++)
			{
				chk[o][f] = new Checkbox (x + (f % 2) * 150, yS + 7 * 24 + 6 + (f / 2) * 24, 148, 22, flags[f], ins.p[ffield[f] + o] != 0, dlg_changed, C_FACE);
				addChild (chk[o][f]);
			}
		}
		b = new Button (16, height - 42, 90, 30, "Test", dlg_btn); b->tag = B_TEST; addChild (b);
		b = new Button (width - 196, height - 42, 86, 30, "OK", dlg_btn); b->tag = B_OK; addChild (b);
		b = new Button (width - 102, height - 42, 86, 30, "Cancel", dlg_btn); b->tag = B_CANCEL; addChild (b);
		addChild (preset);			// the drop-down last: its list opens over the controls below
		g_dlg = this;
	}
	~InsDialog () { g_dlg = 0; }

	void collect ()						// the controls -> ins
	{
		static const int ffield[4] = { P_EGT, P_AM, P_VIB, P_KSR };
		for (int o = 0; o < 2; o++)
		{
			for (int r = 0; r < 7; r++) ins.p[field (r) + o] = (unsigned char) to_slider (r, sl[o][r]->value);
			ins.p[P_WAVE + o] = (unsigned char) wave[o]->sel;
			for (int f = 0; f < 4; f++) ins.p[ffield[f] + o] = chk[o][f]->checked ? 1 : 0;
		}
		ins.p[P_FB] = (unsigned char) fb->value; ins.p[P_CON] = (unsigned char) (conn->selected == 1);
		fms_copy (ins.name, name->text, sizeof ins.name);
	}
	void show ()						// ins -> the controls
	{
		static const int ffield[4] = { P_EGT, P_AM, P_VIB, P_KSR };
		name->setText (ins.name);
		for (int o = 0; o < 2; o++)
		{
			for (int r = 0; r < 7; r++) { sl[o][r]->value = to_slider (r, ins.p[field (r) + o]); sl[o][r]->invalidate (true); }
			wave[o]->sel = ins.p[P_WAVE + o] & 3; wave[o]->invalidate (true);
			for (int f = 0; f < 4; f++) { chk[o][f]->checked = ins.p[ffield[f] + o] != 0; chk[o][f]->invalidate (true); }
		}
		fb->value = ins.p[P_FB] & 7; fb->invalidate (true);
		conn->select (ins.p[P_CON] & 1);
		invalidate (true);
	}
	void test ()
	{
		collect ();
		if (!audio ()) return;
		struct kapi_fm_instrument k; fms_to_kapi (&ins, &k);
		ak_fm_instrument (TEST_VOICE, &k);
		ak_fm_start (TEST_VOICE, fms_note_mhz (fms_make_note (1, 0, g_oct)), SOUND_FM, 220);
		g_previewVoice = TEST_VOICE; g_previewEnd = kapi_get_ticks () + 70;
	}
	void play () { test (); anim = true; animStart = animShown = kapi_get_ticks (); }	// ... and the wave moves with it
	void animate ()						// (from the window's tick)
	{
		if (!anim) return;
		unsigned now = kapi_get_ticks ();
		if (now - animStart > 170) anim = false;
		else if (now - animShown < 4) return;
		animShown = now;
		invalidate (true);
	}

	// An operator's loudness (0 .. 1) t seconds after the key went down (let go at 0.7 s): a sketch of
	// its envelope -- the rates as times that double every step and a half.
	static double rate_s (int r, double fastest) { return r <= 0 ? 30.0 : fastest * pow (2.0, (15 - r) * 0.62); }
	double env (int o, double t) const
	{
		int ar = ins.p[P_AR + o] & 15, dr = ins.p[P_DR + o] & 15, slv = ins.p[P_SL + o] & 15, rr = ins.p[P_RR + o] & 15;
		double ta = rate_s (ar, 0.003), td = rate_s (dr, 0.02), tr = rate_s (rr, 0.02), sus = pow (10.0, -slv * 3.0 / 20), off = 0.7;
		double held = t < off ? t : off, v;
		if (held < ta) v = held / ta;
		else
		{
			double d = (held - ta) / td;
			v = d < 1 ? 1 - (1 - sus) * d : sus;
			if (!ins.p[P_EGT + o] && d >= 1) { v = sus * (1 - (held - ta - td) / tr); if (v < 0) v = 0; }	// (not held: it goes on falling)
		}
		if (t > off) { v *= 1 - (t - off) / tr; if (v < 0) v = 0; }
		return v;
	}
	// The sound's wave, two periods: operator 1 (fed back into itself) bends operator 2's phase (FM) or
	// is added to it; each at its volume -- and, while Test plays, at its envelope's level then.
	void output_wave (int x, int y, int w, int h)
	{
		uk_sunken (canvas, x, y, w, h, 5, C_FIELD);
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 40);
		int mid = y + h / 2, amp = h / 2 - 7;
		canvas.fillRect (x + 6, mid, w - 12, 1, dim);
		double t = anim ? (kapi_get_ticks () - animStart) / 100.0 : -1;
		double a[2], mult[2];
		for (int o = 0; o < 2; o++)
		{
			a[o] = pow (10.0, -(ins.p[P_TL + o] & 63) * 0.75 / 20) * (t >= 0 ? env (o, t) : 1.0);
			int m = ins.p[P_MULT + o] & 15; mult[o] = m ? m : 0.5;
		}
		int fbk = ins.p[P_FB] & 7; double fbDepth = fbk ? M_PI * pow (2.0, fbk - 5) : 0;
		bool add = (ins.p[P_CON] & 1) != 0;
		const int SUB = 8; int n = (w - 12) * SUB, py = -1;
		double m1 = 0, m2 = 0;
		for (int i = 0; i <= n; i++)
		{
			double ph = i * 4 * M_PI / n;
			double mo = a[0] * wave_at (ins.p[P_WAVE] & 3, mult[0] * ph + fbDepth * (m1 + m2) / 2 + 64 * M_PI);
			m2 = m1; m1 = mo;
			double out = add ? (mo + a[1] * wave_at (ins.p[P_WAVE + 1] & 3, mult[1] * ph)) / 2
					 : a[1] * wave_at (ins.p[P_WAVE + 1] & 3, mult[1] * ph + 4 * M_PI * mo + 64 * M_PI);
			if (i % SUB) continue;
			int yy = mid - (int) (out * amp);
			int lo = py < 0 ? yy : (py < yy ? py : yy), hi_ = py < 0 ? yy : (py > yy ? py : yy);
			canvas.fillRect (x + 6 + i / SUB, lo, 2, hi_ - lo + 2, C_ACCENT);
			py = yy;
		}
	}
	void onButton (int tag) override
	{
		if (tag == B_TEST) { play (); return; }
		if (tag == B_LOAD)
		{
			char p[256];
			if (!uk_file_open (p, sizeof p, INS_DIR, FMI_KINDS)) return;
			FmsIns in;
			if (read_fmi (p, &in)) { ins = in; show (); } else uk_messagebox ("Instrument", "Not an .FMI instrument file.", MB_OK);
			return;
		}
		if (tag == B_SAVE)
		{
			collect ();
			char def[20]; snprintf (def, sizeof def, "%.8s.FMI", ins.name[0] ? ins.name : "INSTR");
			char p[256];
			if (!uk_file_save (p, sizeof p, INS_DIR, def, FMI_KINDS)) return;
			char t[600]; int len = fmi_write (&ins, t);
			if (kapi_save_file (p, t, (unsigned) len) < 0) uk_messagebox ("Instrument", "Cannot write the file.", MB_OK);
			return;
		}
		if (tag == B_OK) collect ();
		close (tag);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }

	void seg (int x0, int y0, int x1, int y1, unsigned c) { VPath p; p.line (V (x0), V (y0), V (x1), V (y1), V (2)); p.fill (canvas, c, 255); }
	// An operator's envelope: up to its volume (the attack), down to the sustain level (the decay), held
	// (or falling on, when it is not held), then the release once the key is let go.
	void envelope (int o, int x, int y, int w, int h)
	{
		uk_sunken (canvas, x, y, w, h, 5, C_FIELD);
		int ar = ins.p[P_AR + o] & 15, dr = ins.p[P_DR + o] & 15, slv = ins.p[P_SL + o] & 15, rr = ins.p[P_RR + o] & 15, tl = ins.p[P_TL + o] & 63;
		int bot = y + h - 7, top = y + 8 + tl * (h - 22) / 63;
		int sy = top + slv * (bot - top) / 15;
		int xa = x + 10 + (15 - ar) * 3, xd = xa + 6 + (15 - dr) * 3, xs = xd + 70, xr = xs + 6 + (15 - rr) * 4;
		if (xr > x + w - 8) xr = x + w - 8;
		int ey = ins.p[P_EGT + o] ? sy : sy + (bot - sy) / 2;	// (not held: it goes on falling)
		unsigned c = C_ACCENT, dim = uk_mix (C_FIELD, C_FIELD_TEXT, 40);
		canvas.fillRect (x + 6, bot + 1, w - 12, 1, dim);
		canvas.fillRect (xs, y + 6, 1, bot - y - 5, dim);		// the key let go
		seg (x + 10, bot, xa, top, c); seg (xa, top, xd, sy, c); seg (xd, sy, xs, ey, c); seg (xs, ey, xr, bot, c);
	}
	void box (int x, int y, const char *s)			// an operator in the algorithm's drawing
	{
		uk_rbox (canvas, x, y, 50, 22, 4, C_FIELD, C_FIELD);
		uk_rline (canvas, x, y, 50, 22, 4, uk_tone (C_FACE, 72), 220);
		uk_text_c (canvas, x, y, 50, 22, s, C_FIELD_TEXT);
	}
	void onDraw () override
	{
		char t[48]; snprintf (t, sizeof t, "Instrument of channel %d", ch + 1);
		drawBox (t);
		unsigned dim = uk_mix (C_FACE, C_TEXT, 150);
		// the algorithm
		int ax = 276, ay = yB + 2;
		if (ins.p[P_CON] & 1)			// additive: both to the output
		{
			box (ax, ay - 12, "Op 1"); box (ax, ay + 12, "Op 2");
			seg (ax + 50, ay - 1, ax + 70, ay - 1, C_ACCENT); seg (ax + 50, ay + 23, ax + 70, ay + 23, C_ACCENT);
			seg (ax + 70, ay - 1, ax + 70, ay + 23, C_ACCENT); seg (ax + 70, ay + 11, ax + 92, ay + 11, C_ACCENT);
			uk_text_l (canvas, ax + 98, ay, 22, "out", dim);
		}
		else
		{
			box (ax, ay, "Op 1"); box (ax + 70, ay, "Op 2");
			seg (ax + 50, ay + 11, ax + 70, ay + 11, C_ACCENT); seg (ax + 120, ay + 11, ax + 136, ay + 11, C_ACCENT);
			uk_text_l (canvas, ax + 140, ay, 22, "out", dim);
		}
		snprintf (t, sizeof t, "%d", ins.p[P_FB] & 7); uk_text_l (canvas, 624, yB + 3, 20, t, C_TEXT);
		for (int o = 0; o < 2; o++)
		{
			int x = colX (o);
			uk_text_l (canvas, x, yO, 20, o ? ((ins.p[P_CON] & 1) ? "Operator 2" : "Operator 2 - carrier (what is heard)")
							   : ((ins.p[P_CON] & 1) ? "Operator 1" : "Operator 1 - modulator (the timbre)"), C_TEXT, 2);
			envelope (o, x, yO + 58, COLW, 66);
			for (int r = 0; r < 7; r++)
			{
				if (r == 5) { int m = ins.p[P_MULT + o] & 15; if (m) snprintf (t, sizeof t, "x%d", m); else snprintf (t, sizeof t, "x1/2"); }
				else snprintf (t, sizeof t, "%d", sl[o][r]->value);
				uk_text_l (canvas, x + COLW - 30, yS + r * 24, 20, t, C_TEXT);
			}
		}
		uk_text_l (canvas, 16, yS + 7 * 24 + 58, 20, anim ? "The sound's wave, as the note sounds" : "The sound's wave (two periods, at full volume)", C_TEXT, 2);
		output_wave (16, yS + 7 * 24 + 80, width - 32, 62);
		uk_text_l (canvas, 118, height - 42, 30, "Test plays it: the wave moves with the note.", dim);
	}
};
static void dlg_btn (Widget &w) { ((Modal *) w.parent)->onButton (w.tag); }
static void dlg_changed (Widget &)				// a control moved: the drawing, the sound
{
	if (!g_dlg) return;
	g_dlg->collect ();
	g_dlg->invalidate (true);
	g_dlg->test ();
}
static void dlg_preset (Widget &w)
{
	Dropdown &d = (Dropdown &) w;
	if (!g_dlg || d.sel <= 0) return;
	char p[200]; snprintf (p, sizeof p, INS_DIR "/%s.FMI", d.opts[d.sel]);
	FmsIns in;
	if (read_fmi (p, &in)) { g_dlg->ins = in; g_dlg->show (); g_dlg->play (); }
}

static void edit_instrument (int ch)
{
	InsDialog d (ch);
	if (d.run () == InsDialog::B_OK)
	{
		g_song.ins[ch] = d.ins;
		upload (ch);
		g_dirty = true;
	}
	if (g_audio == 1) ak_fm_stop (TEST_VOICE);
	redraw ();
}
static void op_instrument () { edit_instrument (g_ch); }

// ---- the column headers ----------------------------------------------------------------------------------
// A channel's header: its colour, its instrument (a click: the instrument dialog), M (mute in this
// pattern), S (solo), its level.
class ChanHeader : public Widget
{
public:
	int ch, part, down;			// the part under the pointer / pressed: 0 the name, 1 M, 2 S, -1 none
	ChanHeader (int l, int t, int w, int h, int c) : Widget (l, t, w, h), ch (c), part (-1), down (-1) {}
	int partAt (int mx, int my) const
	{
		if (mx < 0 || my < 0 || mx >= width || my >= height) return -1;
		if (my < 25) return 0;
		return mx >= 4 && mx < 24 ? 1 : mx >= 26 && mx < 46 ? 2 : -1;
	}
	void pill (int x, const char *s, bool on, unsigned c, bool hot)
	{
		if (on) uk_rbox (canvas, x, 26, 20, 16, 4, uk_tone (c, 150), c);
		else uk_raised (canvas, x, 26, 20, 16, 4, C_BUTTON, hot ? UK_HOT : UK_NORMAL);
		uk_text_c (canvas, x, 26, 20, 16, s, on ? 0x00FFFFFFu : C_BUTTON_TEXT, on ? 2 : 0);
	}
	void onDraw () override
	{
		bool muted = pat ().mute[ch] != 0, quiet = silenced (ch), cur = g_ch == ch;
		unsigned bg = cur ? uk_mix (C_BG, C_ACCENT, 46) : C_BG;
		canvas.clear (bg);
		canvas.fillRect (0, 0, width - 1, 3, CH_COL[ch]);
		canvas.fillRect (width - 1, 0, 1, height, uk_tone (C_BG, 100));
		if (part == 0 || down == 0) uk_raised (canvas, 3, 5, width - 8, 19, 4, C_BUTTON, down == 0 ? UK_PRESSED : UK_HOT);
		char t[24], f[24]; snprintf (t, sizeof t, "%d  %s", ch + 1, g_song.ins[ch].name[0] ? g_song.ins[ch].name : "(none)");
		uk_text_fit (t, width - 16, f, sizeof f, 2);
		uk_text_l (canvas, 8, 5, 19, f, quiet ? C_DIS : C_TEXT, 2);
		pill (4, "M", muted, 0x00D9534A, part == 1);
		pill (26, "S", g_solo == ch, 0x00D08A1E, part == 2);
		int vx = 52, vw = width - vx - 8, fill = quiet ? 0 : g_vu[ch] * vw / 100;
		uk_rbox (canvas, vx, 31, vw, 6, 3, uk_tone (C_BG, 100), uk_tone (C_BG, 112));
		if (fill > 2) uk_rbox (canvas, vx, 31, fill, 6, 3, 0x0062B87A, 0x004A9E62);
	}
	void mute () { pat ().mute[ch] ^= 1; g_dirty = true; if (pat ().mute[ch]) note_off (ch); redraw (); }
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		int p = partAt (mx, my);
		if (p != part) { part = p; invalidate (true); }
		if (br && !pressed && p >= 0) { pressed = true; mute (); return true; }	// right click: mute / unmute
		if (bl && !pressed && p >= 0)
		{
			pressed = true; down = p;
			if (p == 1) mute ();
			else if (p == 2) { g_solo = g_solo == ch ? -1 : ch; if (g_playing) for (int c = 0; c < FMS_CH; c++) if (silenced (c)) note_off (c); redraw (); }
			invalidate (true);
			return true;
		}
		if (!bl && !br && pressed)
		{
			pressed = false;
			int was = down; down = -1; invalidate (true);
			if (was == 0 && p == 0) edit_instrument (ch);
		}
		return p >= 0;
	}
};

// ---- the grid ------------------------------------------------------------------------------------------------
static void move_to (int row, int ch)
{
	FmsPattern &p = pat ();
	if (row < 0) row = 0;
	if (row >= p.rows) row = p.rows - 1;
	if (ch < 0) ch = FMS_CH - 1;
	if (ch >= FMS_CH) ch = 0;
	g_row = row; g_ch = ch;
	ensure_visible (row, false);
}
static void set_cell (unsigned char v, int advance)
{
	FmsPattern &p = pat ();
	undo_push ();
	p.n[g_ch * p.rows + g_row] = v;
	g_sel = false;
	move_to (g_row + advance, g_ch);
	redraw ();
}
static void enter_note (int note, int sharp, int oct)		// a note typed or clicked on the piano
{
	unsigned char v = fms_make_note (note, sharp, oct);
	preview (g_ch, v);
	set_cell (v, g_step);
}

class Grid : public Widget
{
public:
	Grid (int l, int t, int w, int h) : Widget (l, t, w, h) { canFocus = true; }
	void onDraw () override
	{
		FmsPattern &p = pat ();
		int rh = ROW_H, vis = visible_rows ();
		int r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
		unsigned playBg = uk_mix (C_FIELD, 0x0048B068, 96), line = f_mix (26), gutter = uk_mix (C_FIELD, C_BG, 150);
		canvas.clear (C_FIELD);
		canvas.fillRect (0, 0, NUM_W, height, gutter);
		for (int i = 0; i < vis + 1; i++)
		{
			int r = g_top + i, y = i * rh;
			if (r >= p.rows) break;
			bool playRow = g_playing && g_playPat == g_pat && r == g_shownRow;
			unsigned rowBg = playRow ? playBg : r % 16 == 0 ? f_mix (30) : r % 4 == 0 ? f_mix (14) : C_FIELD;
			if (r == g_row && !playRow) rowBg = uk_mix (rowBg, C_ACCENT, 36);
			canvas.fillRect (NUM_W, y, width - NUM_W, rh, rowBg);
			if (playRow || r == g_row) canvas.fillRect (0, y, NUM_W, rh, uk_mix (gutter, playRow ? 0x0048B068u : C_ACCENT, 90));
			char num[8]; snprintf (num, sizeof num, "%02d", r);
			uk_text_l (canvas, NUM_W - 8 - uk_text_w (num, r % 4 == 0 ? 2 : 0), y, rh, num, r % 4 == 0 ? C_FIELD_TEXT : f_mix (120), r % 4 == 0 ? 2 : 0);
			for (int c = 0; c < FMS_CH; c++)
			{
				int x = NUM_W + c * COL_W;
				bool cur = r == g_row && c == g_ch, in = g_sel && r >= r0 && r <= r1 && c >= c0 && c <= c1;
				if (in && !cur) canvas.fillRect (x, y, COL_W, rh, uk_mix (rowBg, C_ACCENT, 96));
				canvas.fillRect (x, y, 1, rh, line);
				if (cur) uk_hilite (canvas, x + 1, y, COL_W - 1, rh, 4, hasFocus);	// the cursor: the selection's look
				unsigned char v = p.n[c * p.rows + r];
				char t[4]; fms_note_text (v, t);
				bool note = (v & 7) && !(v & 128);
				unsigned col = silenced (c) ? f_mix (70) : note ? C_FIELD_TEXT : f_mix (76);
				if (cur) col = uk_hilite_ink (hasFocus);
				uk_text_c (canvas, x, y, COL_W, rh, t, col, note && !cur ? 2 : 0);
			}
		}
		canvas.fillRect (NUM_W - 1, 0, 1, height, line);
		uk_rline (canvas, 0, 0, width, height, 0, hasFocus ? C_ACCENT : uk_tone (C_FACE, 72), hasFocus ? 255 : 210);
	}
	bool cellAt (int mx, int my, int &r, int &c)
	{
		r = g_top + my / ROW_H; c = (mx - NUM_W) / COL_W;
		if (r < 0) r = 0;
		if (r >= pat ().rows) r = pat ().rows - 1;
		if (c < 0) c = 0;
		if (c >= FMS_CH) c = FMS_CH - 1;
		return mx >= NUM_W;
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { pressed = false; return false; }
		if (wheel) { g_top -= wheel * 3; sync_scroll (); invalidate (true); return true; }
		int r, c;
		if (bl && !pressed)
		{
			pressed = true; setFocus ();
			if (cellAt (mx, my, r, c))
			{
				if (kapi_get_modifiers () & MOD_SHIFT) { if (!g_sel) { g_ar = g_row; g_ac = g_ch; g_sel = true; } }
				else { g_sel = false; g_ar = r; g_ac = c; }
				move_to (r, c);
			}
			redraw ();
		}
		else if (bl && pressed)				// a drag: the block from where it started
		{
			cellAt (mx, my, r, c);
			if (r != g_row || c != g_ch) { g_sel = r != g_ar || c != g_ac; move_to (r, c); redraw (); }
		}
		else if (!bl) pressed = false;
		return true;
	}
	bool onKey (long k) override
	{
		FmsPattern &p = pat ();
		const char *notes = "cdefgab";
		for (int i = 0; i < 7; i++)
			if (k == notes[i] || k == notes[i] - 32) { enter_note (i + 1, k < 'a', g_oct); return true; }
		if (k >= '0' && k <= '7') { g_oct = (int) (k - '0'); g_octBox->value = g_oct; g_octBox->invalidate (true); redraw (); return true; }
		if (k == ' ') { set_cell (FMS_OFF, g_step); return true; }
		if (k == KEY_DEL) { extern void op_clear_block (); if (g_sel) op_clear_block (); else set_cell (FMS_CONT, g_step); return true; }
		if (k == KEY_BACKSPACE) { if (g_row > 0) { move_to (g_row - 1, g_ch); set_cell (FMS_CONT, 0); } return true; }
		if (k == '#' || k == '+')
		{
			unsigned char v = p.n[g_ch * p.rows + g_row];
			if ((v & 7) && !(v & 128)) set_cell (fms_make_note (v & 7, !(v & 64), (v >> 3) & 7), 0);
			return true;
		}
		bool shift = (kapi_get_modifiers () & MOD_SHIFT) != 0;
		if ((k == KEY_UP || k == KEY_DOWN) && (kapi_get_modifiers () & MOD_CTRL))	// Ctrl+Up / Down: a semitone (Shift: an octave)
		{ extern void transpose (int); transpose ((k == KEY_UP ? 1 : -1) * (shift ? 12 : 1)); return true; }
		int vis = visible_rows (), row = g_row, ch = g_ch;
		switch (k)
		{
		case KEY_UP:    row--; break;
		case KEY_DOWN:  row++; break;
		case KEY_LEFT:  ch--; break;
		case KEY_RIGHT: case KEY_TAB: ch++; break;
		case KEY_PGUP:  row -= vis; break;
		case KEY_PGDN:  row += vis; break;
		case KEY_HOME:  row = 0; break;
		case KEY_END:   row = p.rows - 1; break;
		default: return false;
		}
		if (shift && k != KEY_TAB) { if (!g_sel) { g_ar = g_row; g_ac = g_ch; g_sel = true; } if (ch < 0) ch = 0; if (ch >= FMS_CH) ch = FMS_CH - 1; }
		else g_sel = false;
		move_to (row, ch);
		redraw ();
		return true;
	}
};

// ---- the patterns' list ------------------------------------------------------------------------------------------
class PatList : public Widget
{
public:
	enum { ROW = 24 };
	int top_, hot;
	PatList (int l, int t, int w, int h) : Widget (l, t, w, h), top_ (0), hot (-1) {}
	void show (int i) { int vis = height / ROW; if (i < top_) top_ = i; if (i >= top_ + vis) top_ = i - vis + 1; }
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		int vis = height / ROW;
		if (top_ > g_song.npat - vis) top_ = g_song.npat - vis;
		if (top_ < 0) top_ = 0;
		for (int k = 0; k < vis && top_ + k < g_song.npat; k++)
		{
			int i = top_ + k, y = k * ROW;
			bool on = i == g_pat;
			if (on) uk_hilite (canvas, 2, y + 1, width - 4, ROW - 2, 4, true);
			else if (i == hot) uk_rbox (canvas, 2, y + 1, width - 4, ROW - 2, 4, uk_tone (bg, 112), uk_tone (bg, 112));
			unsigned ink = on ? uk_hilite_ink (true) : C_TEXT, dim = on ? ink : uk_mix (bg, C_TEXT, 140);
			char t[24]; snprintf (t, sizeof t, "%02d", i + 1);
			uk_text_l (canvas, 22, y, ROW, t, ink, 2);
			snprintf (t, sizeof t, "%d rows", g_song.pat[i].rows);
			uk_text_l (canvas, 50, y, ROW, t, dim);
			if (g_playing && i == g_playPat) uk_glyph (canvas, WKG_RIGHT, 11, y + ROW / 2, 9, on ? ink : 0x0048B068u);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		int h = in ? top_ + my / ROW : -1;
		if (h >= g_song.npat) h = -1;
		if (h != hot) { hot = h; invalidate (true); }
		if (!in) { pressed = false; return false; }
		if (wheel) { top_ -= wheel; invalidate (true); return true; }
		if (bl && !pressed) { pressed = true; if (h >= 0 && h != g_pat) { g_pat = h; g_sel = false; refresh_pattern_ui (); } }
		else if (!bl) pressed = false;
		return true;
	}
};

// ---- the piano: the notes sounding (each in its channel's colour); a click enters that note ------------------------
class Piano : public Widget
{
public:
	enum { OCT = 8 };
	Piano (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	int kw () const { return (width - 16) / (OCT * 7); }
	int x0 () const { return (width - OCT * 7 * kw ()) / 2; }
	static bool hasBlack (int white) { int n = white % 7; return n != 2 && n != 6; }	// (not after E, B)
	unsigned lit (int white, bool black) const
	{
		for (int c = 0; c < FMS_CH; c++)
		{
			unsigned char v = g_snd[c];
			if (!(v & 7) || (v & 128)) continue;
			if (((v >> 3) & 7) * 7 + (v & 7) - 1 == white && ((v & 64) != 0) == black) return CH_COL[c];
		}
		return 0;
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		canvas.fillRect (0, 0, width, 1, uk_tone (C_BG, 100));
		int k = kw (), x = x0 (), y = 7, h = height - 16, bh = h * 6 / 10;
		for (int i = 0; i < OCT * 7; i++)
		{
			unsigned c = lit (i, false);
			uk_rbox (canvas, x + i * k, y, k - 1, h, 2, c ? c : 0x00FFFFFFu, c ? c : 0x00ECECECu);
		}
		for (int i = 0; i < OCT * 7; i++)
			if (hasBlack (i))
			{
				unsigned c = lit (i, true);
				uk_rbox (canvas, x + (i + 1) * k - k * 3 / 10 - 1, y, k * 6 / 10 + 1, bh, 2, c ? c : 0x00303030u, c ? uk_tone (c, 100) : 0x00181818u);
			}
		uk_rline (canvas, x - 1, y - 1, OCT * 7 * k + 1, h + 2, 2, uk_tone (C_BG, 72), 220);
		canvas.fillRect (x + g_oct * 7 * k, y + h + 3, 7 * k - 1, 3, C_ACCENT);		// the octave the keys type in
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0 || my < 0 || my >= height) { pressed = false; return false; }
		if (bl && !pressed)
		{
			pressed = true;
			int k = kw (), rx = mx - x0 (), h = height - 16;
			if (rx < 0 || rx >= OCT * 7 * k || my < 7 || my >= 7 + h) return true;
			int white = rx / k, sharp = 0;
			if (my < 7 + h * 6 / 10)					// a black key?
			{
				int off = rx - white * k;
				if (off >= k - k * 3 / 10 - 1 && hasBlack (white)) sharp = 1;
				else if (off <= k * 3 / 10 && white > 0 && hasBlack (white - 1)) { white--; sharp = 1; }
			}
			g_oct = white / 7; g_octBox->value = g_oct; g_octBox->invalidate (true);
			enter_note (white % 7 + 1, sharp, g_oct);
			g_grid_focus ();
		}
		else if (!bl) pressed = false;
		return true;
	}
	static void g_grid_focus ();
};

// The widgets repaint themselves when the song, the pattern or the position changed (invalidating the
// root only recomposes its children's canvases).
static void redraw ()
{
	if (g_grid) ((Widget *) g_grid)->invalidate (true);
	for (int c = 0; c < FMS_CH; c++) if (g_head[c]) ((Widget *) g_head[c])->invalidate (true);
	if (g_plist) { g_plist->show (g_pat); g_plist->invalidate (true); }
	if (g_piano) g_piano->invalidate (true);
	if (g_lcd) update_lcd ();
	if (g_root) g_root->invalidate (false);
}
void Piano::g_grid_focus () { ((Widget *) g_grid)->setFocus (); }

// ---- the block: clear, cut, copy, paste, transpose ----------------------------------------------------------------
static unsigned char g_clip[FMS_CH * FMS_MAXROWS]; static int g_clipR = 0, g_clipC = 0;

void op_clear_block ()
{
	FmsPattern &p = pat ();
	int r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
	undo_push ();
	for (int c = c0; c <= c1; c++) for (int r = r0; r <= r1; r++) p.n[c * p.rows + r] = FMS_CONT;
	redraw ();
}
static void op_copy ()
{
	FmsPattern &p = pat ();
	int r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
	g_clipR = r1 - r0 + 1; g_clipC = c1 - c0 + 1;
	for (int c = 0; c < g_clipC; c++) for (int r = 0; r < g_clipR; r++) g_clip[c * g_clipR + r] = p.n[(c0 + c) * p.rows + r0 + r];
	char s[64]; snprintf (s, sizeof s, "Copied %d row%s x %d channel%s.", g_clipR, g_clipR == 1 ? "" : "s", g_clipC, g_clipC == 1 ? "" : "s");
	set_status (s);
}
static void op_cut () { op_copy (); op_clear_block (); }
static void op_paste ()						// at the cursor (the block's top left when one is chosen)
{
	if (!g_clipR) { set_status ("Nothing to paste: copy a block first."); return; }
	FmsPattern &p = pat ();
	int r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
	undo_push ();
	for (int c = 0; c < g_clipC && c0 + c < FMS_CH; c++)
		for (int r = 0; r < g_clipR && r0 + r < p.rows; r++) p.n[(c0 + c) * p.rows + r0 + r] = g_clip[c * g_clipR + r];
	redraw ();
}
static void op_select_all () { g_ar = 0; g_ac = 0; g_row = pat ().rows - 1; g_ch = FMS_CH - 1; g_sel = true; redraw (); }
void transpose (int delta)					// the block's notes (the cursor's without one)
{
	FmsPattern &p = pat ();
	int r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
	bool any = false;
	for (int c = c0; c <= c1 && !any; c++) for (int r = r0; r <= r1; r++) { unsigned char v = p.n[c * p.rows + r]; if ((v & 7) && !(v & 128)) { any = true; break; } }
	if (!any) return;
	undo_push ();
	for (int c = c0; c <= c1; c++)
		for (int r = r0; r <= r1; r++) p.n[c * p.rows + r] = fms_transpose (p.n[c * p.rows + r], delta);
	if (!g_sel) preview (g_ch, p.n[g_ch * p.rows + g_row]);
	redraw ();
}
static void op_up1 () { transpose (1); }
static void op_down1 () { transpose (-1); }
static void op_up12 () { transpose (12); }
static void op_down12 () { transpose (-12); }

// ---- playback --------------------------------------------------------------------------------------------------
static void play_row ()
{
	FmsPattern &p = g_song.pat[g_playPat];
	for (int c = 0; c < FMS_CH; c++)
	{
		unsigned char v = p.n[c * p.rows + g_playRow];
		if (v & 128) note_off (c);
		else if ((v & 7) == 0) continue;
		else if (p.mute[c] || (g_solo >= 0 && g_solo != c)) note_off (c);
		else note_on (c, v);
	}
}
static void op_stop ()
{
	if (!g_playing) return;
	g_playing = false;
	note_off (-1);
	g_btPlay->setOn (false);
	redraw ();
	set_status ("Stopped.");
}
static void start_play (int patn, int row)
{
	g_btPlay->setOn (false);
	if (!audio ()) return;
	for (int c = 0; c < FMS_CH; c++) upload (c);
	g_playing = true; g_playPat = patn; g_playRow = row; g_shownRow = row;
	g_nextTick = kapi_get_ticks ();
	g_btPlay->setOn (true);
	set_status (g_loop ? "Playing this pattern in a loop -- Esc or Stop to stop." : "Playing -- Esc or Stop to stop.");
}
static void op_play () { if (g_playing) op_stop (); else start_play (g_pat, g_row); }
static void op_play_start () { op_stop (); start_play (g_loop ? g_pat : 0, 0); }
static void op_loop () { g_loop = !g_loop; g_btLoop->setOn (g_loop); set_status (g_loop ? "Loop: the pattern plays again and again." : "Loop off: the patterns play in order."); }
static void op_follow () { g_follow = !g_follow; g_btFollow->setOn (g_follow); }
static void tick ()
{
	unsigned now = kapi_get_ticks ();
	if (g_dlg) g_dlg->animate ();
	if (g_previewVoice >= 0 && (int) (now - g_previewEnd) >= 0) { note_off (g_previewVoice); g_previewVoice = -1; if (g_piano) g_piano->invalidate (true); }
	// the meters: a note falls to a floor while it sounds, to nothing once it stopped
	int dt = (int) (now - g_vuTick);
	if (dt >= 3)
	{
		g_vuTick = now;
		if (dt > 20) dt = 20;
		for (int c = 0; c < FMS_CH; c++)
		{
			int v = g_vu[c], floor_ = g_snd[c] ? 35 : 0;
			if (v > floor_) { v -= dt * (g_snd[c] ? 2 : 6); if (v < floor_) v = floor_; g_vu[c] = v; ((Widget *) g_head[c])->invalidate (true); }
		}
	}
	if (!g_playing) return;
	int guard = 0;
	while (g_playing && (int) (now - g_nextTick) >= 0 && guard++ < 8)
	{
		if (g_playRow >= g_song.pat[g_playPat].rows) { g_playRow = 0; if (!g_loop) g_playPat = (g_playPat + 1) % g_song.npat; }
		play_row ();
		if (g_follow)				// show the pattern being played, its row centred
		{
			if (g_pat != g_playPat) { g_pat = g_playPat; g_sel = false; refresh_pattern_ui (); }
			ensure_visible (g_playRow, true);
		}
		g_nextTick += (unsigned) g_song.pat[g_playPat].speed * 5;	// speed / 20 s, 100 ticks/s
		g_shownRow = g_playRow++;
		redraw ();
	}
	if ((int) (now - g_nextTick) > 50) g_nextTick = now;			// fell behind: resync
}

// ---- song commands ------------------------------------------------------------------------------------------------
static void title_status () { set_status (g_path[0] ? g_path : "Untitled", g_dirty ? "  (modified)" : ""); }
static void song_labels ()
{
	g_songLabel->setText (g_song.title[0] ? g_song.title : (g_path[0] ? fs_basename (g_path) : "Untitled"));
	g_authorLabel->setText (g_song.author);
}

static bool confirm_discard ()
{
	if (!g_dirty) return true;
	int r = uk_messagebox ("FM Tracker", "The song has changed. Save it first?", MB_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) { extern void op_save (); op_save (); return !g_dirty; }
	return true;
}
static void after_load ()
{
	g_pat = 0; g_row = 0; g_ch = 0; g_top = 0; g_sel = false; g_solo = -1;
	undo_clear ();
	for (int c = 0; c < FMS_CH; c++) upload (c);
	g_dirty = false;
	song_labels ();
	refresh_pattern_ui ();
	title_status ();
}
static void op_new ()
{
	if (!confirm_discard ()) return;
	op_stop ();
	fms_clear (&g_song); fms_new (&g_song);
	FmsIns in;
	if (read_fmi (INS_DIR "/PIANO.FMI", &in)) for (int c = 0; c < FMS_CH; c++) g_song.ins[c] = in;
	g_path[0] = 0;
	after_load ();
}
static bool load_song (const char *path)
{
	void *f = kapi_open (path);
	if (!f) { set_status ("Cannot open ", path); return false; }
	unsigned n = kapi_fsize (f);
	unsigned char *b = new unsigned char[n + 1];
	int r = kapi_read (f, b, n); kapi_close (f);
	static FmsSong s;
	s.npat = 0;
	bool ok = r > 0 && fms_parse (b, r, &s);
	delete [] b;
	if (!ok) { fms_clear (&s); uk_messagebox ("FM Tracker", "This is not an FM Song (.FMS) file.", MB_OK); return false; }
	op_stop ();
	fms_clear (&g_song);
	g_song = s; s.npat = 0;					// (the patterns move to g_song)
	fms_copy (g_path, path, sizeof g_path);
	after_load ();
	return true;
}
static void op_open ()
{
	if (!confirm_discard ()) return;
	char p[256];
	if (uk_file_open (p, sizeof p, g_path[0] ? g_path : SONG_DIR, FMS_KINDS)) load_song (p);
}
static bool write_song (const char *path)
{
	int n = fms_size (&g_song);
	unsigned char *b = new unsigned char[n];
	fms_write (&g_song, b);
	bool ok = kapi_save_file (path, b, (unsigned) n) >= 0;
	delete [] b;
	return ok;
}
static void op_save_as ()
{
	char p[256];
	char def[40]; fms_copy (def, g_path[0] ? fs_basename (g_path) : "SONG.FMS", sizeof def);
	if (!uk_file_save (p, sizeof p, g_path[0] ? g_path : SONG_DIR, def, FMS_KINDS)) return;
	int n = fms_len (p);
	if (!(n > 4 && p[n - 4] == '.')) fms_copy (p + n, ".FMS", sizeof p - n);
	if (!write_song (p)) { set_status ("Cannot save ", p); return; }
	fms_copy (g_path, p, sizeof g_path);
	g_dirty = false; title_status (); song_labels ();
}
// File > Export WAV...: the song as AudioKit plays it (ak_open reads the FM Song -- the one saved to a
// file of the moment --, the frames go to a WAV file as they come).
static void op_export_wav ()
{
	char p[256];
	char def[40]; fms_copy (def, g_path[0] ? fs_basename (g_path) : "SONG.FMS", sizeof def);
	int dn = fms_len (def);
	if (dn > 4 && def[dn - 4] == '.') fms_copy (def + dn - 4, ".WAV", sizeof def - (dn - 4)); else fms_copy (def + dn, ".WAV", sizeof def - dn);
	if (!uk_file_save (p, sizeof p, g_path[0] ? g_path : SONG_DIR, def, "WAV audio|*.WAV|All files|*")) return;
	int n = fms_len (p);
	if (!(n > 4 && p[n - 4] == '.')) fms_copy (p + n, ".WAV", sizeof p - n);
	static const char tmp[] = "SD:/tmp/fmtracker-export.fms";
	kapi_mkdir ("SD:/tmp");
	if (!write_song (tmp)) { set_status ("Cannot export ", p); return; }
	char err[128];
	ak_stream *st = ak_open (tmp, err, sizeof err);
	bool ok = false;
	if (st != 0)
	{
		struct ak_info in; ak_info_of (st, &in);
		long long frames = in.length_ms * AUDIOKIT_RATE / 1000 + AUDIOKIT_RATE / 10;
		ak_wav *w = ak_wav_begin (p, AUDIOKIT_RATE, 2, frames);
		if (w != 0)
		{
			static short buf[2 * 4096];
			set_status ("Exporting ", p);
			for (int k; (k = ak_read (st, buf, 4096)) > 0; ) ak_wav_write (w, buf, k);
			ok = ak_wav_end (w) == 0;
		}
		ak_close (st);
	}
	kapi_remove (tmp);
	set_status (ok ? "Exported " : "Cannot export ", p);
}

void op_save ()
{
	if (!g_path[0]) { op_save_as (); return; }
	if (!write_song (g_path)) { set_status ("Cannot save ", g_path); return; }
	g_dirty = false; title_status ();
}

// Song > Info...: title / author / comment.
static void info_btn (Widget &w) { ((Modal *) w.parent)->close (w.tag); }
class InfoDialog : public Modal
{
public:
	Textbox *t[3];
	InfoDialog () : Modal (440, 190)
	{
		left = (W - width) / 2; top = (H - height) / 2;
		static const char *const lab[3] = { "Title", "Author", "Comment" };
		const char *val[3] = { g_song.title, g_song.author, g_song.comment };
		static const int cap[3] = { 20, 20, 50 };
		for (int i = 0; i < 3; i++)
		{
			addChild (new Label (12, titleH () + 16 + i * 32, 80, 20, lab[i], C_TEXT, C_FACE));
			t[i] = new Textbox (96, titleH () + 12 + i * 32, cap[i] > 20 ? 330 : 200, 26, val[i]);
			addChild (t[i]);
		}
		Button *b;
		b = new Button (width - 184, height - 38, 82, 28, "OK", info_btn); b->tag = 1; addChild (b);
		b = new Button (width - 94, height - 38, 82, 28, "Cancel", info_btn); b->tag = 0; addChild (b);
		t[0]->setFocus ();
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override { drawBox ("Song information"); }
};
static void op_info ()
{
	InfoDialog d;
	if (!d.run ()) return;
	fms_copy (g_song.title, d.t[0]->text, sizeof g_song.title);
	fms_copy (g_song.author, d.t[1]->text, sizeof g_song.author);
	fms_copy (g_song.comment, d.t[2]->text, sizeof g_song.comment);
	g_dirty = true;
	song_labels ();
}

// Edit / Pattern.
static void op_insert_row ()
{
	FmsPattern &p = pat ();
	undo_push ();
	unsigned char *n = p.n + g_ch * p.rows;
	for (int r = p.rows - 1; r > g_row; r--) n[r] = n[r - 1];
	n[g_row] = FMS_CONT; redraw ();
}
static void op_delete_row ()
{
	FmsPattern &p = pat ();
	undo_push ();
	unsigned char *n = p.n + g_ch * p.rows;
	for (int r = g_row; r < p.rows - 1; r++) n[r] = n[r + 1];
	n[p.rows - 1] = FMS_CONT; redraw ();
}
static void op_clear_channel ()
{
	FmsPattern &p = pat ();
	undo_push ();
	for (int r = 0; r < p.rows; r++) p.n[g_ch * p.rows + r] = FMS_CONT;
	redraw ();
}
static void add_pattern (bool duplicate)
{
	if (g_song.npat >= FMS_MAXPAT) { set_status ("At most 64 patterns."); return; }
	op_stop (); undo_clear ();
	for (int i = g_song.npat; i > g_pat + 1; i--) g_song.pat[i] = g_song.pat[i - 1];
	FmsPattern &src = g_song.pat[g_pat], &np = g_song.pat[g_pat + 1];
	fms_pattern_alloc (&np, src.rows, src.speed);
	if (duplicate) { for (int i = 0; i < FMS_CH * src.rows; i++) np.n[i] = src.n[i]; for (int c = 0; c < FMS_CH; c++) np.mute[c] = src.mute[c]; }
	g_song.npat++; g_pat++; g_row = 0; g_top = 0; g_sel = false; g_dirty = true;
	refresh_pattern_ui ();
}
static void op_new_pattern () { add_pattern (false); }
static void op_dup_pattern () { add_pattern (true); }
static void op_delete_pattern ()
{
	if (g_song.npat <= 1) { set_status ("A song keeps at least one pattern."); return; }
	if (!uk_messagebox ("Pattern", "Delete this pattern?", MB_YESNO)) return;
	op_stop (); undo_clear ();
	delete [] g_song.pat[g_pat].n;
	for (int i = g_pat; i + 1 < g_song.npat; i++) g_song.pat[i] = g_song.pat[i + 1];
	g_song.npat--;
	if (g_pat >= g_song.npat) g_pat = g_song.npat - 1;
	g_dirty = true; g_sel = false;
	refresh_pattern_ui ();
}
static void move_pattern (int d)				// earlier / later in the song
{
	int to = g_pat + d;
	if (to < 0 || to >= g_song.npat) return;
	op_stop (); undo_clear ();
	FmsPattern t = g_song.pat[g_pat]; g_song.pat[g_pat] = g_song.pat[to]; g_song.pat[to] = t;
	g_pat = to; g_dirty = true;
	refresh_pattern_ui ();
}
static void op_pattern_up () { move_pattern (-1); }
static void op_pattern_down () { move_pattern (1); }
static void op_prev_pattern () { if (g_pat > 0) { g_pat--; g_sel = false; refresh_pattern_ui (); } }
static void op_next_pattern () { if (g_pat + 1 < g_song.npat) { g_pat++; g_sel = false; refresh_pattern_ui (); } }
static void op_mute () { g_head[g_ch]->mute (); }
static void op_solo () { g_solo = g_solo == g_ch ? -1 : g_ch; redraw (); }

static void focus_grid () { ((Widget *) g_grid)->setFocus (); }
static void on_play (Widget &) { op_play (); focus_grid (); }
static void on_play_start (Widget &) { op_play_start (); focus_grid (); }
static void on_stop (Widget &) { op_stop (); focus_grid (); }
static void on_loop (Widget &) { op_loop (); focus_grid (); }
static void on_follow (Widget &) { op_follow (); focus_grid (); }
static void on_undo (Widget &) { op_undo (); focus_grid (); }
static void on_redo (Widget &) { op_redo (); focus_grid (); }
static void on_cut (Widget &) { op_cut (); focus_grid (); }
static void on_copy (Widget &) { op_copy (); focus_grid (); }
static void on_paste (Widget &) { op_paste (); focus_grid (); }
static void on_add (Widget &) { op_new_pattern (); focus_grid (); }
static void on_dup (Widget &) { op_dup_pattern (); focus_grid (); }
static void on_pup (Widget &) { op_pattern_up (); focus_grid (); }
static void on_pdown (Widget &) { op_pattern_down (); focus_grid (); }
static void on_pdel (Widget &) { op_delete_pattern (); focus_grid (); }
static void on_rows (Widget &w) { undo_push (); fms_pattern_resize (&pat (), ((NumericUpDown &) w).value); refresh_pattern_ui (); }
static void on_speed (Widget &w) { undo_push (); pat ().speed = ((NumericUpDown &) w).value; redraw (); }
static void on_oct (Widget &w) { g_oct = ((NumericUpDown &) w).value; redraw (); }
static void on_step (Widget &w) { g_step = ((NumericUpDown &) w).value; }
static void on_scroll (Widget &w) { g_top = ((Scrollbar &) w).value; ((Widget *) g_grid)->invalidate (true); }

// The side panel's small icons: up, down, delete.
static void side_icon (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool)
{ uk_glyph (cv, id == 0 ? WKG_UP : id == 1 ? WKG_DOWN : WKG_CLOSE, x + size / 2, y + size / 2, id == 2 ? 9 : 10, ink); }

class TrackerRoot : public Root
{
public:
	TrackerRoot () : Root (W, H, "FM Tracker") {}
	void onDraw () override
	{
		Root::onDraw ();
		canvas.fillRect (0, TOOL_H - 1, width, 1, uk_tone (bg, 100));			// the toolbar's edge
		canvas.fillRect (SIDE_W - 1, TOOL_H, 1, height - TOOL_H - ST_H, uk_tone (bg, 100));	// the side panel's
		canvas.fillRect (GRID_X, HEAD_Y, NUM_W, HEAD_H, bg);
	}
	void onTick () override { tick (); }
	void onResized () override;
	bool onKey (long k) override
	{
		if (k == 27) { if (g_sel && !g_playing) { g_sel = false; redraw (); } else op_stop (); return true; }
		return ((Widget *) g_grid)->onKey (k);
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type != DND_FILES || !confirm_discard ()) return;
		char p[256]; int n = 0;
		while (data[n] && data[n] != '\n' && n < 255) { p[n] = data[n]; n++; }
		p[n] = 0;
		load_song (p);
	}
};

// The window resized: the side panel taller, the channels wider (COL_W), the grid taller, the piano wider
void TrackerRoot::onResized ()
{
	g_W = width; g_H = height;
	((Widget *) g_btFollow)->left = W - 96;
	g_side->resizeTo (SIDE_W - 1, H - TOOL_H - ST_H);
	for (int c = 0; c < FMS_CH; c++) { g_head[c]->left = GRID_X + NUM_W + c * COL_W; g_head[c]->resizeTo (COL_W, HEAD_H); }
	((Widget *) g_grid)->resizeTo (GRID_W, GRID_H);
	((Widget *) g_sb)->left = GRID_X + GRID_W; ((Widget *) g_sb)->resizeTo (SB_W, GRID_H);
	((Widget *) g_piano)->top = H - ST_H - PIANO_H; ((Widget *) g_piano)->resizeTo (W - SIDE_W, PIANO_H);
	((Widget *) g_status)->top = H - ST_H; ((Widget *) g_status)->resizeTo (W, ST_H);
	ensure_visible (g_row, false);
	invalidate (true);
}

static ToolButton *tool (Widget &to, int x, int y, int w, int h, const char *tip, Action cb, int glyph)
{
	ToolButton *b = new ToolButton (w, h, tip, cb);
	if (glyph != WKT_NONE) b->setGlyph (glyph);
	b->raised = true; b->left = x; b->top = y;
	to.addChild (b);
	return b;
}

int main (void)
{
	FtTextFace *big = 0;
	if (ft_uikit_install ("DejaVu Sans", 13))		// (FreeType's text: uk_fw / uk_fh follow it)
	{
		big = new FtTextFace;
		if (!big->open ("DejaVu Sans Mono", 22) && !big->open ("DejaVu Sans", 22)) { delete big; big = 0; }
	}
	TrackerRoot root;				// (its background: the theme's face)
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	fms_new (&g_song);

	// the transport bar
	int x = 10;
	g_btPlay = tool (root, x, 8, 38, 34, "Play from the cursor / stop (Ctrl+P)", on_play, WKT_PLAY); x += 42;
	g_btPlay->setToggle (true); g_btPlay->filled = true;
	tool (root, x, 8, 34, 34, "Play from the start", on_play_start, WKT_TO_START); x += 38;
	tool (root, x, 8, 34, 34, "Stop (Esc)", on_stop, WKT_STOP); x += 38;
	g_btLoop = tool (root, x, 8, 34, 34, "Loop: play this pattern again and again (Ctrl+L)", on_loop, WKT_LOOP); x += 46;
	g_btLoop->setToggle (true);
	g_lcd = new LcdDisplay (x, 6, 216, 38, "01:000", "PATTERN : ROW"); g_lcd->face = big; root.addChild (g_lcd); x += 228;
	tool (root, x, 8, 32, 34, "Undo (Ctrl+Z)", on_undo, WKT_UNDO); x += 34;
	tool (root, x, 8, 32, 34, "Redo (Ctrl+Y)", on_redo, WKT_REDO); x += 44;
	tool (root, x, 8, 32, 34, "Cut the block (Ctrl+X)", on_cut, WKT_CUT); x += 34;
	tool (root, x, 8, 32, 34, "Copy the block (Ctrl+C)", on_copy, WKT_COPY); x += 34;
	tool (root, x, 8, 32, 34, "Paste at the cursor (Ctrl+V)", on_paste, WKT_PASTE); x += 44;
	g_btFollow = tool (root, W - 96, 8, 86, 34, "Follow the position while it plays", on_follow, WKT_NONE);
	g_btFollow->setText ("Follow"); g_btFollow->setToggle (true, true);

	// the side panel: the patterns, this pattern, the typing
	unsigned sbg = col_side ();
	Panel *side = new Panel (0, TOOL_H, SIDE_W - 1, H - TOOL_H - ST_H, sbg);
	root.addChild (side); g_side = side;
	side->addChild (new Heading (10, 8, 130, 18, "PATTERNS", sbg));
	g_plist = new PatList (4, 30, SIDE_W - 9, 9 * PatList::ROW); side->addChild (g_plist);
	int by = 30 + 9 * PatList::ROW + 8, bx = 6;
	tool (*side, bx, by, 27, 26, "A new pattern after this one", on_add, WKT_PLUS); bx += 29;
	tool (*side, bx, by, 27, 26, "Duplicate this pattern", on_dup, WKT_COPY); bx += 29;
	tool (*side, bx, by, 27, 26, "Move it earlier in the song", on_pup, WKT_NONE)->setIcon (side_icon, 0); bx += 29;
	tool (*side, bx, by, 27, 26, "Move it later in the song", on_pdown, WKT_NONE)->setIcon (side_icon, 1); bx += 29;
	tool (*side, bx, by, 27, 26, "Delete this pattern", on_pdel, WKT_NONE)->setIcon (side_icon, 2);
	int sy = by + 40;
	side->addChild (new Heading (10, sy, 130, 18, "THIS PATTERN", sbg)); sy += 24;
	side->addChild (new Label (12, sy + 5, 52, 20, "Rows", C_TEXT, sbg));
	g_rowsBox = new NumericUpDown (66, sy, 80, 28, 1, FMS_MAXROWS, 64, 1, on_rows); side->addChild (g_rowsBox); sy += 32;
	side->addChild (new Label (12, sy + 5, 52, 20, "Speed", C_TEXT, sbg));
	g_speedBox = new NumericUpDown (66, sy, 80, 28, 1, 40, 3, 1, on_speed); side->addChild (g_speedBox);
	g_speedBox->tip = "A row lasts speed / 20 s"; sy += 42;
	side->addChild (new Heading (10, sy, 130, 18, "TYPING", sbg)); sy += 24;
	side->addChild (new Label (12, sy + 5, 52, 20, "Octave", C_TEXT, sbg));
	g_octBox = new NumericUpDown (66, sy, 80, 28, 0, 7, g_oct, 1, on_oct); side->addChild (g_octBox); sy += 32;
	side->addChild (new Label (12, sy + 5, 52, 20, "Step", C_TEXT, sbg));
	g_stepBox = new NumericUpDown (66, sy, 80, 28, 0, 16, g_step, 1, on_step); side->addChild (g_stepBox);
	g_stepBox->tip = "The rows the cursor goes down after a note"; sy += 42;
	side->addChild (new Heading (10, sy, 130, 18, "SONG", sbg)); sy += 22;
	g_songLabel = new Label (12, sy, SIDE_W - 22, 20, "", C_TEXT, sbg); side->addChild (g_songLabel); sy += 20;
	g_authorLabel = new Label (12, sy, SIDE_W - 22, 20, "", uk_mix (sbg, C_TEXT, 140), sbg); side->addChild (g_authorLabel);

	// channel headers, grid, scrollbar, piano, status
	for (int c = 0; c < FMS_CH; c++) { g_head[c] = new ChanHeader (GRID_X + NUM_W + c * COL_W, HEAD_Y, COL_W, HEAD_H, c); root.addChild (g_head[c]); }
	g_grid = new Grid (GRID_X, GRID_Y, GRID_W, GRID_H);
	root.addChild (g_grid);
	g_sb = new Scrollbar (GRID_X + GRID_W, GRID_Y, SB_W, GRID_H, true, 1, 0, on_scroll);
	root.addChild (g_sb);
	g_piano = new Piano (SIDE_W, H - ST_H - PIANO_H, W - SIDE_W, PIANO_H);
	root.addChild (g_piano);
	g_status = new StatusBar (0, H - ST_H, W, ST_H);
	root.addChild (g_status);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New",            "^N", UK_CTRL ('N'), op_new);
	menu.item ("Open...",        "^O", UK_CTRL ('O'), op_open);
	menu.item ("Save",           "^S", UK_CTRL ('S'), op_save);
	menu.item ("Save As...",     "",   0,             op_save_as);
	menu.item ("Export WAV...",  "",   0,             op_export_wav);
	menu.separator ();
	menu.item ("Song Info...",   "",   0,             op_info);
	menu.menu ("Edit");
	menu.item ("Undo",           "^Z", UK_CTRL ('Z'), op_undo);
	menu.item ("Redo",           "^Y", UK_CTRL ('Y'), op_redo);
	menu.separator ();
	menu.item ("Cut",            "^X", UK_CTRL ('X'), op_cut);
	menu.item ("Copy",           "^C", UK_CTRL ('C'), op_copy);
	menu.item ("Paste",          "^V", UK_CTRL ('V'), op_paste);
	menu.item ("Select the Pattern", "^A", UK_CTRL ('A'), op_select_all);
	menu.separator ();
	menu.item ("Insert Slice",   "^E", UK_CTRL ('E'), op_insert_row);
	menu.item ("Delete Slice",   "^D", UK_CTRL ('D'), op_delete_row);
	menu.item ("Clear Channel",  "",   0,             op_clear_channel);
	menu.separator ();
	menu.item ("Semitone Up",    "Ctrl+Up",   0,      op_up1);
	menu.item ("Semitone Down",  "Ctrl+Down", 0,      op_down1);
	menu.item ("Octave Up",      "",   0,             op_up12);
	menu.item ("Octave Down",    "",   0,             op_down12);
	menu.menu ("Pattern");
	menu.item ("Previous",       "^B", UK_CTRL ('B'), op_prev_pattern);
	menu.item ("Next",           "^F", UK_CTRL ('F'), op_next_pattern);
	menu.separator ();
	menu.item ("New Pattern",    "",   0,             op_new_pattern);
	menu.item ("Duplicate",      "",   0,             op_dup_pattern);
	menu.item ("Move Earlier",   "",   0,             op_pattern_up);
	menu.item ("Move Later",     "",   0,             op_pattern_down);
	menu.item ("Delete...",      "",   0,             op_delete_pattern);
	menu.menu ("Channel");
	menu.item ("Instrument...",  "",   0,             op_instrument);
	menu.item ("Mute in this Pattern", "", 0,         op_mute);
	menu.item ("Solo",           "",   0,             op_solo);
	menu.menu ("Play");
	menu.item ("Play / Stop",    "^P", UK_CTRL ('P'), op_play);
	menu.item ("From the Start", "",   0,             op_play_start);
	menu.item ("Loop the Pattern", "^L", UK_CTRL ('L'), op_loop);
	menu.item ("Follow",         "",   0,             op_follow);
	menu.item ("Stop",           "Esc", 0,            op_stop);
	menu.publish ();

	char args[256];
	if (kapi_get_args (args, sizeof args) > 0 && args[0] && load_song (args)) {}
	else
	{
		FmsIns in;
		if (read_fmi (INS_DIR "/PIANO.FMI", &in)) for (int c = 0; c < FMS_CH; c++) g_song.ins[c] = in;
		after_load ();
		set_status ("C D E F G A B: a note (Shift = #), 0-7: octave, Space: silence, Del: ---, Shift+arrows: a block, ^P: play");
	}
	focus_grid ();
	root.setResizable (true);
	root.setMinSize (760, 440);
	root.run ();
	return 0;
}
