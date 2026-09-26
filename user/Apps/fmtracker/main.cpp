//
// fmtracker -- the Onyx FM tracker: 8 channels of 2-operator FM instruments (the kernel's
// FM synthesizer, ABI v47) on a grid of time slices; reads and writes FM Song's .FMS
// files and .FMI instruments (fms.h).
//
//   * A column per channel; its header button opens the instrument dialog (right-click:
//     mute the channel in this pattern). A row = one time slice (speed / 20 s).
//   * A cell: a note starts there ("C#4"), "---" = the note goes on, empty = silence. A
//     note lasts until the next note or silence of its channel (C4 then C4 = two notes).
//   * Keys: C D E F G A B enter a note (Shift = sharp; the cursor then goes down), 0-7 the
//     octave, Space a silence, Delete "---", Backspace clears the slice above, # toggles
//     the sharp, Ctrl+Up / Ctrl+Down move the note a semitone up / down, arrows / Page Up /
//     Page Down / Home / End move, Tab the next channel.
//     A click selects a cell; the wheel and the scrollbar scroll.
//   * Play (^P) plays from the cursor, follows the position and highlights it; Esc stops.
//     A song is a list of patterns (each with its own length and speed), played in order.
//
#include "kapi.h"
#include "applib.h"
#include "fsutil.h"
#include "notify.h"
#include "wtk/wtk.h"
#include "fms.h"

using namespace wtk;

#define W	808
#define H	600
#define TOOL_H	36
#define HEAD_Y	(TOOL_H + 4)
#define HEAD_H	26
#define GRID_Y	(HEAD_Y + HEAD_H + 2)
#define ST_H	20
#define NUM_W	44
#define SB_W	16
#define COL_W	((W - NUM_W - SB_W - 4) / FMS_CH)
#define ROW_H	(wk_fh () + 2)
#define INS_DIR	"SD:/apps/fmtracker.app/ins"
#define SONG_DIR "SD:/music/fms"
#define TEST_VOICE 15

static FmsSong g_song;
static int g_pat = 0, g_row = 0, g_ch = 0, g_top = 0, g_oct = 4;
static bool g_playing = false, g_dirty = false;
static int g_playPat = 0, g_playRow = -1, g_shownRow = -1; static unsigned g_nextTick = 0;	// next row to play / the one sounding
static unsigned g_previewEnd = 0; static int g_previewVoice = -1;
static char g_path[256] = "";
static int g_audio = 0;				// 1 = the output is ours, -1 = unavailable

class Grid;
class ChanHeader;
static Grid *g_grid = 0;
static ChanHeader *g_head[FMS_CH];
static Scrollbar *g_sb = 0;
static Label *g_status = 0, *g_patLabel = 0, *g_octLabel = 0;
static NumericUpDown *g_rowsBox = 0, *g_speedBox = 0;
static Root *g_root = 0;

static FmsPattern &pat () { return g_song.pat[g_pat]; }
static void redraw ();			// repaint the grid + the headers (below)
static int visible_rows () { return (H - ST_H - GRID_Y) / ROW_H; }

static void set_status (const char *a, const char *b = "")
{
	char s[200]; fms_copy (s, a, sizeof s);
	int n = fms_len (s); for (int i = 0; b[i] && n < 198; i++) s[n++] = b[i];
	s[n] = 0;
	g_status->setText (s);
}
static void itoa_ (int v, char *b) { char t[12]; int n = 0, k = 0; if (v < 0) { b[k++] = '-'; v = -v; } do { t[n++] = (char) ('0' + v % 10); v /= 10; } while (v); while (n) b[k++] = t[--n]; b[k] = 0; }

// ---- audio ------------------------------------------------------------------------------------------
static void upload (int ch)
{
	if (g_audio != 1) return;
	struct kapi_fm_instrument k;
	fms_to_kapi (&g_song.ins[ch], &k);
	kapi_sound_instrument (ch, &k);
}
static bool audio ()
{
	if (g_audio == 1) return true;
	int r = kapi_sound_acquire ();
	g_audio = r == 1 ? 1 : -1;
	if (g_audio != 1) { set_status (r < 0 ? "No audio output." : "The audio output is used by another program."); return false; }
	for (int c = 0; c < FMS_CH; c++) upload (c);
	return true;
}
static void note_on (int voice, unsigned char v) { if (audio ()) kapi_sound_start (voice, fms_note_mhz (v), SOUND_FM, 220); }
static void note_off (int voice) { if (g_audio == 1) kapi_sound_stop (voice); }

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
static void refresh_pattern_ui ()
{
	char b[32] = "Pattern "; int n = fms_len (b);
	itoa_ (g_pat + 1, b + n); n = fms_len (b); b[n++] = '/'; itoa_ (g_song.npat, b + n);
	g_patLabel->setText (b);
	g_rowsBox->value = pat ().rows; g_rowsBox->invalidate (true);
	g_speedBox->value = pat ().speed; g_speedBox->invalidate (true);
	if (g_row >= pat ().rows) g_row = pat ().rows - 1;
	sync_scroll ();
	for (int c = 0; c < FMS_CH; c++) ((Widget *) g_head[c])->invalidate (true);
	redraw ();
}
static void refresh_octave ()
{
	char b[16] = "Octave "; b[7] = (char) ('0' + g_oct); b[8] = 0;
	g_octLabel->setText (b);
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

class InsDialog;
static InsDialog *g_dlg = 0;
static void dlg_btn (Widget &w);
static void dlg_preset (Widget &w);
static void dlg_changed (Widget &w);

class InsDialog : public Modal
{
public:
	FmsIns ins; int ch;
	Textbox *name;
	Dropdown *preset, *wave[2], *conn;
	NumericUpDown *num[2][7], *fb;		// mult, level, ksl, attack, decay, sustain, release
	Checkbox *chk[2][4];			// sustained, tremolo, vibrato, ksr
	enum { B_OK = 1, B_CANCEL = 0, B_TEST = 2, B_LOAD = 3, B_SAVE = 4 };

	InsDialog (int channel) : Modal (560, 452), ch (channel)
	{
		left = (W - width) / 2; top = (H - height) / 2;
		ins = g_song.ins[ch];
		int y = wk_fh () + 16;
		addChild (new Label (12, y + 4, 60, 20, "Name", C_TEXT, C_FACE_DN));
		name = new Textbox (70, y, 120, 26, ins.name); addChild (name);
		load_presets ();
		preset = new Dropdown (200, y, 170, 26, g_presetPtr, g_npreset, 0, dlg_preset);
		Button *b;
		b = new Button (380, y - 1, 80, 28, "Load...", dlg_btn); b->tag = B_LOAD; addChild (b);
		b = new Button (466, y - 1, 82, 28, "Save...", dlg_btn); b->tag = B_SAVE; addChild (b);
		y += 40;
		static const char *const rows[7] = { "Multiplier", "Level (0 = loud)", "Key scale level", "Attack", "Decay", "Sustain level", "Release" };
		static const int lo[7] = { 0, 0, 0, 0, 0, 0, 0 }, hi[7] = { 15, 63, 3, 15, 15, 15, 15 };
		static const int field[7] = { P_MULT, P_TL, P_KSL, P_AR, P_DR, P_SL, P_RR };
		addChild (new Label (200, y, 150, 20, "Modulator", C_ACCENT, C_FACE_DN));
		addChild (new Label (380, y, 150, 20, "Carrier", C_ACCENT, C_FACE_DN));
		y += 24;
		for (int r = 0; r < 7; r++)
		{
			addChild (new Label (12, y + r * 30 + 4, 180, 20, rows[r], C_TEXT, C_FACE_DN));
			for (int o = 0; o < 2; o++)
			{
				num[o][r] = new NumericUpDown (200 + o * 180, y + r * 30, 110, 26, lo[r], hi[r], ins.p[field[r] + o], 1, dlg_changed);
				addChild (num[o][r]);
			}
		}
		y += 7 * 30;
		addChild (new Label (12, y + 4, 180, 20, "Wave", C_TEXT, C_FACE_DN));
		for (int o = 0; o < 2; o++) wave[o] = new Dropdown (200 + o * 180, y, 150, 26, WAVES, 4, ins.p[P_WAVE + o] & 3, dlg_changed);
		y += 32;
		static const char *const flags[4] = { "Sustain", "Tremolo", "Vibrato", "KSR" };
		static const int ffield[4] = { P_EGT, P_AM, P_VIB, P_KSR };
		for (int f = 0; f < 4; f++)
			for (int o = 0; o < 2; o++)
			{
				chk[o][f] = new Checkbox (200 + o * 180 + (f % 2) * 88, y + (f / 2) * 24, 88, 22, flags[f], ins.p[ffield[f] + o] != 0, dlg_changed, C_FACE_DN);
				addChild (chk[o][f]);
			}
		y += 54;
		addChild (new Label (12, y + 4, 90, 20, "Feedback", C_TEXT, C_FACE_DN));
		fb = new NumericUpDown (100, y, 80, 26, 0, 7, ins.p[P_FB] & 7, 1, dlg_changed); addChild (fb);
		addChild (new Label (200, y + 4, 90, 20, "Connection", C_TEXT, C_FACE_DN));
		conn = new Dropdown (292, y, 130, 26, CONNS, 2, ins.p[P_CON] & 1, dlg_changed);
		b = new Button (12, height - 40, 90, 30, "Test", dlg_btn); b->tag = B_TEST; addChild (b);
		b = new Button (width - 192, height - 40, 86, 30, "OK", dlg_btn); b->tag = B_OK; addChild (b);
		b = new Button (width - 98, height - 40, 86, 30, "Cancel", dlg_btn); b->tag = B_CANCEL; addChild (b);
		// the drop-downs last: their lists open over the controls below
		addChild (wave[0]); addChild (wave[1]); addChild (conn); addChild (preset);
		g_dlg = this;
	}
	~InsDialog () { g_dlg = 0; }

	void collect ()
	{
		static const int field[7] = { P_MULT, P_TL, P_KSL, P_AR, P_DR, P_SL, P_RR };
		static const int ffield[4] = { P_EGT, P_AM, P_VIB, P_KSR };
		for (int o = 0; o < 2; o++)
		{
			for (int r = 0; r < 7; r++) ins.p[field[r] + o] = (unsigned char) num[o][r]->value;
			ins.p[P_WAVE + o] = (unsigned char) wave[o]->sel;
			for (int f = 0; f < 4; f++) ins.p[ffield[f] + o] = chk[o][f]->checked ? 1 : 0;
		}
		ins.p[P_FB] = (unsigned char) fb->value; ins.p[P_CON] = (unsigned char) conn->sel;
		fms_copy (ins.name, name->text, sizeof ins.name);
	}
	void show ()						// ins -> the controls
	{
		static const int field[7] = { P_MULT, P_TL, P_KSL, P_AR, P_DR, P_SL, P_RR };
		static const int ffield[4] = { P_EGT, P_AM, P_VIB, P_KSR };
		name->setText (ins.name);
		for (int o = 0; o < 2; o++)
		{
			for (int r = 0; r < 7; r++) { num[o][r]->value = ins.p[field[r] + o]; num[o][r]->invalidate (true); }
			wave[o]->sel = ins.p[P_WAVE + o] & 3; wave[o]->invalidate (true);
			for (int f = 0; f < 4; f++) { chk[o][f]->checked = ins.p[ffield[f] + o] != 0; chk[o][f]->invalidate (true); }
		}
		fb->value = ins.p[P_FB] & 7; fb->invalidate (true);
		conn->sel = ins.p[P_CON] & 1; conn->invalidate (true);
		invalidate (true);
	}
	void test ()
	{
		collect ();
		if (!audio ()) return;
		struct kapi_fm_instrument k; fms_to_kapi (&ins, &k);
		kapi_sound_instrument (TEST_VOICE, &k);
		kapi_sound_start (TEST_VOICE, fms_note_mhz (fms_make_note (1, 0, g_oct)), SOUND_FM, 220);
		g_previewVoice = TEST_VOICE; g_previewEnd = kapi_get_ticks () + 70;
	}
	void onButton (int tag) override
	{
		if (tag == B_TEST) { test (); return; }
		if (tag == B_LOAD)
		{
			char p[256];
			if (!wk_file_open (p, sizeof p, INS_DIR)) return;
			FmsIns in;
			if (read_fmi (p, &in)) { ins = in; show (); } else wk_messagebox ("Instrument", "Not an .FMI instrument file.", MB_OK);
			return;
		}
		if (tag == B_SAVE)
		{
			collect ();
			char def[20]; fms_copy (def, ins.name[0] ? ins.name : "INSTR", 9); int n = fms_len (def); fms_copy (def + n, ".FMI", 5);
			char p[256];
			if (!wk_file_save (p, sizeof p, INS_DIR, def)) return;
			char t[600]; int len = fmi_write (&ins, t);
			if (kapi_save_file (p, t, (unsigned) len) < 0) wk_messagebox ("Instrument", "Cannot write the file.", MB_OK);
			return;
		}
		if (tag == B_OK) collect ();
		close (tag);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override
	{
		canvas.clear (C_FACE_DN);
		canvas.frameRect (0, 0, width, height, C_ACCENT);
		char t[48] = "Instrument of channel "; int n = fms_len (t); t[n++] = (char) ('1' + ch); t[n] = 0;
		canvas.text (10, 6, t, C_TEXT);
	}
};
static void dlg_btn (Widget &w) { ((Modal *) w.parent)->onButton (w.tag); }
static void dlg_changed (Widget &) {}
static void dlg_preset (Widget &w)
{
	Dropdown &d = (Dropdown &) w;
	if (!g_dlg || d.sel <= 0) return;
	char p[200]; fms_copy (p, INS_DIR "/", sizeof p);
	int n = fms_len (p); fms_copy (p + n, d.opts[d.sel], sizeof p - n);
	n = fms_len (p); fms_copy (p + n, ".FMI", sizeof p - n);
	FmsIns in;
	if (read_fmi (p, &in)) { g_dlg->ins = in; g_dlg->show (); g_dlg->test (); }
}

static void edit_instrument (int ch)
{
	InsDialog d (ch);
	if (d.run () == InsDialog::B_OK)
	{
		g_song.ins[ch] = d.ins;
		upload (ch);
		g_dirty = true;
		((Widget *) g_head[ch])->invalidate (true);
	}
	if (g_audio == 1) kapi_sound_stop (TEST_VOICE);
	redraw ();
}

// ---- the column headers ----------------------------------------------------------------------------------
class ChanHeader : public Widget
{
public:
	int ch; bool down;
	ChanHeader (int l, int t, int w, int h, int c) : Widget (l, t, w, h), ch (c), down (false) {}
	void onDraw () override
	{
		bool muted = pat ().mute[ch] != 0;
		canvas.clear (down ? C_FACE_DN : (hover ? C_FACE_HI : C_FACE));
		canvas.frameRect (0, 0, width, height, g_ch == ch ? C_ACCENT : C_BORDER);
		char t[24]; t[0] = (char) ('1' + ch); t[1] = ' '; fms_copy (t + 2, g_song.ins[ch].name[0] ? g_song.ins[ch].name : "(none)", 12);
		canvas.text (6, (height - wk_fh ()) / 2, t, muted ? C_DIS : C_TEXT);
		if (muted) canvas.text (width - 3 * wk_fw () - 4, (height - wk_fh ()) / 2, "off", 0x00FF7070);
	}
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != hover) { hover = in; invalidate (true); }
		if (br && !pressed && in)			// right click: mute / unmute
		{
			pressed = true;
			pat ().mute[ch] ^= 1; g_dirty = true;
			if (pat ().mute[ch]) note_off (ch);
			invalidate (true);
			return true;
		}
		if (bl && !pressed && in) { pressed = true; down = true; invalidate (true); return true; }
		if (!bl && !br && pressed)
		{
			pressed = false;
			if (down) { down = false; invalidate (true); if (in) edit_instrument (ch); }
		}
		return in;
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
	int oldCh = g_ch;
	g_row = row; g_ch = ch;
	ensure_visible (row, false);
	if (oldCh != ch) { ((Widget *) g_head[oldCh])->invalidate (true); ((Widget *) g_head[ch])->invalidate (true); }
}

class Grid : public Widget
{
public:
	Grid (int l, int t, int w, int h) : Widget (l, t, w, h) { canFocus = true; }
	void onDraw () override
	{
		FmsPattern &p = pat ();
		int rh = ROW_H, fh = wk_fh (), vis = visible_rows ();
		canvas.clear (0x00101820);
		for (int i = 0; i < vis; i++)
		{
			int r = g_top + i, y = i * rh;
			if (r >= p.rows) break;
			bool playRow = g_playing && g_playPat == g_pat && r == g_shownRow;
			unsigned rowBg = playRow ? 0x00305A30 : (r % 4 == 0 ? 0x00182430 : 0x00101820);
			canvas.fillRect (0, y, width, rh, rowBg);
			char num[8]; itoa_ (r, num);
			canvas.text (NUM_W - 8 - fms_len (num) * wk_fw (), y + (rh - fh) / 2, num, r % 4 == 0 ? 0x00A0B0C0 : 0x00607080);
			for (int c = 0; c < FMS_CH; c++)
			{
				int x = NUM_W + c * COL_W;
				bool cur = r == g_row && c == g_ch;
				if (r == g_row) canvas.fillRect (x, y, COL_W, rh, cur ? 0x00355070 : (playRow ? 0x00305A30 : 0x00202C3A));
				canvas.fillRect (x, y, 1, rh, 0x00283444);
				unsigned char v = p.n[c * p.rows + r];
				char t[4]; fms_note_text (v, t);
				unsigned col = p.mute[c] ? 0x00606870 : ((v & 7) && !(v & 128) ? 0x00F0F0A0 : 0x00506070);
				canvas.text (x + (COL_W - 3 * wk_fw ()) / 2, y + (rh - fh) / 2, t, col);
				if (cur && hasFocus) canvas.frameRect (x, y, COL_W, rh, C_ACCENT);
			}
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { pressed = false; return false; }
		if (wheel) { g_top -= wheel * 3; sync_scroll (); invalidate (true); return true; }
		if (bl && !pressed)
		{
			pressed = true; setFocus ();
			int r = g_top + my / ROW_H, c = (mx - NUM_W) / COL_W;
			if (mx >= NUM_W && c >= 0 && c < FMS_CH && r < pat ().rows) move_to (r, c);
			redraw ();
		}
		else if (!bl) pressed = false;
		return true;
	}
	void setCell (unsigned char v, int advance)
	{
		FmsPattern &p = pat ();
		p.n[g_ch * p.rows + g_row] = v;
		g_dirty = true;
		move_to (g_row + advance, g_ch);
		redraw ();
	}
	bool onKey (long k) override
	{
		FmsPattern &p = pat ();
		const char *notes = "cdefgab";
		for (int i = 0; i < 7; i++)
			if (k == notes[i] || k == notes[i] - 32)
			{
				unsigned char v = fms_make_note (i + 1, k < 'a', g_oct);
				if (!p.mute[g_ch] && !g_playing)			// hear it
				{
					note_on (g_ch, v);
					g_previewVoice = g_ch; g_previewEnd = kapi_get_ticks () + 35;
				}
				setCell (v, 1);
				return true;
			}
		if (k >= '0' && k <= '7') { g_oct = (int) (k - '0'); refresh_octave (); return true; }
		if (k == ' ') { setCell (FMS_OFF, 1); return true; }
		if (k == KEY_DEL) { setCell (FMS_CONT, 1); return true; }
		if (k == KEY_BACKSPACE) { if (g_row > 0) { move_to (g_row - 1, g_ch); setCell (FMS_CONT, 0); } return true; }
		if (k == '#' || k == '+')
		{
			unsigned char v = p.n[g_ch * p.rows + g_row];
			if ((v & 7) && !(v & 128)) setCell (fms_make_note (v & 7, !(v & 64), (v >> 3) & 7), 0);
			return true;
		}
		if ((k == KEY_UP || k == KEY_DOWN) && (kapi_get_modifiers () & MOD_CTRL))	// Ctrl+Up / Down: a semitone
		{
			unsigned char v = p.n[g_ch * p.rows + g_row];
			if ((v & 7) && !(v & 128))
			{
				v = fms_transpose (v, k == KEY_UP ? 1 : -1);
				if (!p.mute[g_ch] && !g_playing) { note_on (g_ch, v); g_previewVoice = g_ch; g_previewEnd = kapi_get_ticks () + 35; }
				setCell (v, 0);
			}
			return true;
		}
		int vis = visible_rows ();
		switch (k)
		{
		case KEY_UP:    move_to (g_row - 1, g_ch); break;
		case KEY_DOWN:  move_to (g_row + 1, g_ch); break;
		case KEY_LEFT:  move_to (g_row, g_ch - 1); break;
		case KEY_RIGHT: case KEY_TAB: move_to (g_row, g_ch + 1); break;
		case KEY_PGUP:  move_to (g_row - vis, g_ch); break;
		case KEY_PGDN:  move_to (g_row + vis, g_ch); break;
		case KEY_HOME:  move_to (0, g_ch); break;
		case KEY_END:   move_to (p.rows - 1, g_ch); break;
		default: return false;
		}
		redraw ();
		return true;
	}
};

// Invalidating the root only recomposes its children's canvases: the grid and the headers
// must repaint themselves when the song, the pattern or the position changed.
static void redraw ()
{
	if (g_grid) ((Widget *) g_grid)->invalidate (true);
	for (int c = 0; c < FMS_CH; c++) if (g_head[c]) ((Widget *) g_head[c])->invalidate (true);
	if (g_root) g_root->invalidate (false);
}

// ---- playback --------------------------------------------------------------------------------------------------
static void play_row ()
{
	FmsPattern &p = g_song.pat[g_playPat];
	for (int c = 0; c < FMS_CH; c++)
	{
		unsigned char v = p.n[c * p.rows + g_playRow];
		if (v & 128) note_off (c);
		else if ((v & 7) == 0) continue;
		else if (p.mute[c]) note_off (c);
		else note_on (c, v);
	}
}
static void op_stop ()
{
	if (!g_playing) return;
	g_playing = false;
	note_off (-1);
	redraw ();
	set_status ("Stopped.");
}
static void op_play ()
{
	if (g_playing) { op_stop (); return; }
	if (!audio ()) return;
	for (int c = 0; c < FMS_CH; c++) upload (c);
	g_playing = true; g_playPat = g_pat; g_playRow = g_row;
	g_nextTick = kapi_get_ticks ();
	set_status ("Playing -- Esc or Stop to stop.");
}
static void tick ()
{
	unsigned now = kapi_get_ticks ();
	if (g_previewVoice >= 0 && (int) (now - g_previewEnd) >= 0) { note_off (g_previewVoice); g_previewVoice = -1; }
	if (!g_playing) return;
	int guard = 0;
	while (g_playing && (int) (now - g_nextTick) >= 0 && guard++ < 8)
	{
		if (g_playRow >= g_song.pat[g_playPat].rows) { g_playRow = 0; g_playPat = (g_playPat + 1) % g_song.npat; }
		play_row ();
		// follow: show the pattern being played, its row centred
		if (g_pat != g_playPat) { g_pat = g_playPat; refresh_pattern_ui (); }
		ensure_visible (g_playRow, true);
		g_nextTick += (unsigned) g_song.pat[g_playPat].speed * 5;	// speed / 20 s, 100 ticks/s
		g_shownRow = g_playRow++;
		redraw ();
	}
	if ((int) (now - g_nextTick) > 50) g_nextTick = now;			// fell behind: resync
}

// ---- song commands ------------------------------------------------------------------------------------------------
static void title_status () { set_status (g_path[0] ? g_path : "Untitled", g_dirty ? "  (modified)" : ""); }

static bool confirm_discard ()
{
	if (!g_dirty) return true;
	int r = wk_messagebox ("FM Tracker", "The song has changed. Save it first?", MB_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) { extern void op_save (); op_save (); return !g_dirty; }
	return true;
}
static void after_load ()
{
	g_pat = 0; g_row = 0; g_ch = 0; g_top = 0;
	for (int c = 0; c < FMS_CH; c++) upload (c);
	g_dirty = false;
	refresh_pattern_ui ();
	title_status ();
}
static void op_new ()
{
	if (!confirm_discard ()) return;
	op_stop ();
	fms_clear (&g_song); fms_new (&g_song);
	FmsIns in;
	char p[200]; fms_copy (p, INS_DIR "/PIANO.FMI", sizeof p);
	if (read_fmi (p, &in)) for (int c = 0; c < FMS_CH; c++) g_song.ins[c] = in;
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
	if (!ok) { fms_clear (&s); wk_messagebox ("FM Tracker", "This is not an FM Song (.FMS) file.", MB_OK); return false; }
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
	if (wk_file_open (p, sizeof p, g_path[0] ? g_path : SONG_DIR)) load_song (p);
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
	if (!wk_file_save (p, sizeof p, g_path[0] ? g_path : SONG_DIR, def)) return;
	int n = fms_len (p);
	if (!(n > 4 && p[n - 4] == '.')) fms_copy (p + n, ".FMS", sizeof p - n);
	if (!write_song (p)) { set_status ("Cannot save ", p); return; }
	fms_copy (g_path, p, sizeof g_path);
	g_dirty = false; title_status ();
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
	InfoDialog () : Modal (440, 180)
	{
		left = (W - width) / 2; top = (H - height) / 2;
		static const char *const lab[3] = { "Title", "Author", "Comment" };
		const char *val[3] = { g_song.title, g_song.author, g_song.comment };
		static const int cap[3] = { 20, 20, 50 };
		for (int i = 0; i < 3; i++)
		{
			addChild (new Label (12, wk_fh () + 20 + i * 32, 80, 20, lab[i], C_TEXT, C_FACE_DN));
			t[i] = new Textbox (96, wk_fh () + 16 + i * 32, cap[i] > 20 ? 330 : 200, 26, val[i]);
			addChild (t[i]);
		}
		Button *b;
		b = new Button (width - 184, height - 38, 82, 28, "OK", info_btn); b->tag = 1; addChild (b);
		b = new Button (width - 94, height - 38, 82, 28, "Cancel", info_btn); b->tag = 0; addChild (b);
		t[0]->setFocus ();
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override { canvas.clear (C_FACE_DN); canvas.frameRect (0, 0, width, height, C_ACCENT); canvas.text (10, 6, "Song information", C_TEXT); }
};
static void op_info ()
{
	InfoDialog d;
	if (!d.run ()) return;
	fms_copy (g_song.title, d.t[0]->text, sizeof g_song.title);
	fms_copy (g_song.author, d.t[1]->text, sizeof g_song.author);
	fms_copy (g_song.comment, d.t[2]->text, sizeof g_song.comment);
	g_dirty = true;
}

// Edit / Pattern.
static void op_insert_row ()
{
	FmsPattern &p = pat ();
	unsigned char *n = p.n + g_ch * p.rows;
	for (int r = p.rows - 1; r > g_row; r--) n[r] = n[r - 1];
	n[g_row] = FMS_CONT; g_dirty = true; redraw ();
}
static void op_delete_row ()
{
	FmsPattern &p = pat ();
	unsigned char *n = p.n + g_ch * p.rows;
	for (int r = g_row; r < p.rows - 1; r++) n[r] = n[r + 1];
	n[p.rows - 1] = FMS_CONT; g_dirty = true; redraw ();
}
static void op_clear_channel ()
{
	FmsPattern &p = pat ();
	for (int r = 0; r < p.rows; r++) p.n[g_ch * p.rows + r] = FMS_CONT;
	g_dirty = true; redraw ();
}
static void add_pattern (bool duplicate)
{
	if (g_song.npat >= FMS_MAXPAT) { set_status ("At most 64 patterns."); return; }
	op_stop ();
	for (int i = g_song.npat; i > g_pat + 1; i--) g_song.pat[i] = g_song.pat[i - 1];
	FmsPattern &src = g_song.pat[g_pat], &np = g_song.pat[g_pat + 1];
	fms_pattern_alloc (&np, src.rows, src.speed);
	if (duplicate) { for (int i = 0; i < FMS_CH * src.rows; i++) np.n[i] = src.n[i]; for (int c = 0; c < FMS_CH; c++) np.mute[c] = src.mute[c]; }
	g_song.npat++; g_pat++; g_row = 0; g_top = 0; g_dirty = true;
	refresh_pattern_ui ();
}
static void op_new_pattern () { add_pattern (false); }
static void op_dup_pattern () { add_pattern (true); }
static void op_delete_pattern ()
{
	if (g_song.npat <= 1) { set_status ("A song keeps at least one pattern."); return; }
	if (!wk_messagebox ("Pattern", "Delete this pattern?", MB_YESNO)) return;
	op_stop ();
	delete [] g_song.pat[g_pat].n;
	for (int i = g_pat; i + 1 < g_song.npat; i++) g_song.pat[i] = g_song.pat[i + 1];
	g_song.npat--;
	if (g_pat >= g_song.npat) g_pat = g_song.npat - 1;
	g_dirty = true;
	refresh_pattern_ui ();
}
static void op_prev_pattern () { if (g_pat > 0) { g_pat--; refresh_pattern_ui (); } }
static void op_next_pattern () { if (g_pat + 1 < g_song.npat) { g_pat++; refresh_pattern_ui (); } }
static void on_prev (Widget &) { op_prev_pattern (); }
static void on_next (Widget &) { op_next_pattern (); }
static void on_add (Widget &) { op_new_pattern (); }
static void on_play (Widget &) { op_play (); g_grid->setFocus (); }
static void on_stop (Widget &) { op_stop (); g_grid->setFocus (); }
static void on_rows (Widget &w) { fms_pattern_resize (&pat (), ((NumericUpDown &) w).value); g_dirty = true; refresh_pattern_ui (); }
static void on_speed (Widget &w) { pat ().speed = ((NumericUpDown &) w).value; g_dirty = true; }
static void on_scroll (Widget &w) { g_top = ((Scrollbar &) w).value; g_grid->invalidate (true); }

class TrackerRoot : public Root
{
public:
	TrackerRoot () : Root (W, H, "FM Tracker") {}
	void onTick () override { tick (); }
	bool onKey (long k) override
	{
		if (k == 27) { op_stop (); return true; }
		return g_grid->onKey (k);
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

int main (void)
{
	TrackerRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setBg (C_FACE_DN);
	fms_new (&g_song);

	// toolbar
	int x = 8;
	root.addChild (new Button (x, 4, 70, 28, "Play", on_play)); x += 76;
	root.addChild (new Button (x, 4, 70, 28, "Stop", on_stop)); x += 84;
	root.addChild (new Button (x, 4, 28, 28, "<", on_prev)); x += 32;
	g_patLabel = new Label (x, 8, 110, 20, "Pattern 1/1", C_TEXT, C_FACE_DN); root.addChild (g_patLabel); x += 112;
	root.addChild (new Button (x, 4, 28, 28, ">", on_next)); x += 32;
	root.addChild (new Button (x, 4, 28, 28, "+", on_add)); x += 44;
	root.addChild (new Label (x, 8, 44, 20, "Rows", C_TEXT, C_FACE_DN)); x += 46;
	g_rowsBox = new NumericUpDown (x, 4, 76, 28, 1, FMS_MAXROWS, 64, 1, on_rows); root.addChild (g_rowsBox); x += 88;
	root.addChild (new Label (x, 8, 52, 20, "Speed", C_TEXT, C_FACE_DN)); x += 54;
	g_speedBox = new NumericUpDown (x, 4, 64, 28, 1, 40, 3, 1, on_speed); root.addChild (g_speedBox); x += 76;
	g_octLabel = new Label (x, 8, 90, 20, "Octave 4", C_ACCENT, C_FACE_DN); root.addChild (g_octLabel);

	// channel headers, grid, scrollbar, status
	for (int c = 0; c < FMS_CH; c++) { g_head[c] = new ChanHeader (NUM_W + c * COL_W, HEAD_Y, COL_W - 2, HEAD_H, c); root.addChild (g_head[c]); }
	g_grid = new Grid (0, GRID_Y, NUM_W + FMS_CH * COL_W, H - ST_H - GRID_Y);
	root.addChild (g_grid);
	g_sb = new Scrollbar (W - SB_W - 2, GRID_Y, SB_W, H - ST_H - GRID_Y, true, 1, 0, on_scroll);
	root.addChild (g_sb);
	g_status = new Label (0, H - ST_H, W, ST_H, "", C_TEXT, C_FACE);
	root.addChild (g_status);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New",            "^N", WK_CTRL ('N'), op_new);
	menu.item ("Open...",        "^O", WK_CTRL ('O'), op_open);
	menu.item ("Save",           "^S", WK_CTRL ('S'), op_save);
	menu.item ("Save As...",     "",   0,             op_save_as);
	menu.separator ();
	menu.item ("Song Info...",   "",   0,             op_info);
	menu.menu ("Edit");
	menu.item ("Insert Slice",   "^E", WK_CTRL ('E'), op_insert_row);
	menu.item ("Delete Slice",   "^D", WK_CTRL ('D'), op_delete_row);
	menu.item ("Clear Channel",  "",   0,             op_clear_channel);
	menu.menu ("Pattern");
	menu.item ("Previous",       "^B", WK_CTRL ('B'), op_prev_pattern);
	menu.item ("Next",           "^F", WK_CTRL ('F'), op_next_pattern);
	menu.separator ();
	menu.item ("New Pattern",    "",   0,             op_new_pattern);
	menu.item ("Duplicate",      "",   0,             op_dup_pattern);
	menu.item ("Delete...",      "",   0,             op_delete_pattern);
	menu.menu ("Play");
	menu.item ("Play / Stop",    "^P", WK_CTRL ('P'), op_play);
	menu.item ("Stop",           "Esc", 0,            op_stop);
	menu.publish ();

	char args[256];
	if (kapi_get_args (args, sizeof args) > 0 && args[0] && load_song (args)) {}
	else
	{
		FmsIns in;
		if (read_fmi (INS_DIR "/PIANO.FMI", &in)) for (int c = 0; c < FMS_CH; c++) g_song.ins[c] = in;
		after_load ();
		set_status ("C D E F G A B: a note (Shift = #), 0-7: octave, Space: silence, Del: ---, ^P: play");
	}
	refresh_octave ();
	g_grid->setFocus ();
	root.run ();
	return 0;
}
