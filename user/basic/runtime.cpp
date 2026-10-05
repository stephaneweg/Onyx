//
// basic/runtime.cpp -- /bin/basic, the Onyx BASIC runtime:  basic <program.bas> [args]
//
// Compiles the program (basic/bascomp.cpp) and runs it on the VM (basic/basvm.cpp) with an
// Onyx bas::Host:
//   * Started from a terminal (it has a stdout): PRINT / INPUT use the console -- until the
//     program opens its window (SCREEN, graphics, WINDOW, a control): then the window.
//   * Started as an app (a .app bundle with main.bas, the qbasic editor's Run, the File
//     Viewer): the first PRINT opens the window.
// The window is a QBasic-like screen (basscreen.h, shared with the PC runtime: 80 x 25 text
// cells = 640 x 400 by default; SCREEN 12 = 640 x 480, SCREEN 13 = 320 x 200) that text and
// graphics share, with uikit controls (BUTTON, TEXTBOX, ...) on top. A windowed app (WINDOW) is in
// the theme's colours -- the windows' face, the text on it --, unless COLOR chose others first;
// the QBasic screens (text, SCREEN n, the games) keep their own. The program's folder becomes the current
// directory, so it finds its files by relative names.
//
#include "kapi.h"
#include "clipboard.h"
#include "applib.h"
#include "notify.h"
#include "gamepad.h"
#include "uikit/uikit.h"
#include "basic/basscreen.h"
#include "audiokit/audiokit.h"		// AudioKit: PLAYFILE, MIDINOTE ... (lib/audiokit.imp.a)

using namespace uikit;

static int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

class OnyxHost;
static OnyxHost *g_host = 0;
static void on_control (Widget &w);
static void host_key (long k);
static void host_mouse (int x, int y, int b);

// ---- the screen window: shows the visible page (x the mode's scale) under the controls ----------------
class ScreenRoot : public Root
{
public:
	const unsigned *vis; int vw, vh, sx, sy;	// the visible page and its display scale
	bool fs; int fsOx, fsOy, fsW, fsH;	// full screen: where the picture is
	ScreenRoot (int w, int h, const char *title) : Root (w, h, title), vis (0), vw (w), vh (h), sx (1), sy (1),
		fs (false), fsOx (0), fsOy (0), fsW (1), fsH (1) {}
	void onDraw () override
	{
		if (!vis) return;
		for (int y = 0; y < height; y++)
		{
			unsigned *d = canvas.px + (long) y * canvas.stride;
			const unsigned *s = vis + (long) (y / sy) * vw;
			if (sx == 1) for (int x = 0; x < width; x++) d[x] = s[x];
			else for (int x = 0; x < width; x++) d[x] = s[x / sx];
		}
	}
	bool onKey (long k) override { host_key (k); return true; }
	void onResized () override;			// (a resizable window: the program's pages follow)
	bool onMouse (int x, int y, int bl, int br, int bm, int) override
	{
		int b = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
		if (x < 0) { host_mouse (-1, -1, b); return true; }
		// physical -> the program's pixels: / the zoom (320-wide modes are shown 2x) or, full
		// screen, x the virtual / physical size ratio (ScreenHost keeps it on the picture)
		if (fs) host_mouse ((x - fsOx) * vw / fsW, (y - fsOy) * vh / fsH, b);
		else host_mouse (x / sx, y / sy, b);
		return true;
	}
};

// ---- the host: the shared BASIC screen (basscreen.h) on an Onyx window --------------------------------
class OnyxHost : public bas::ScreenHost
{
public:
	ScreenRoot *root; bool console;
	unsigned lastPresent;
	char args[256];
	double tBase; unsigned tTicks0;
	unsigned *fsBuf; int fsW, fsH;			// full screen back buffer
	enum { MAXCTL = 128 };
	Widget *ctl[MAXCTL]; int ctlKind[MAXCTL]; int nctl;
	char *ddItems[MAXCTL]; const char *ddPtr[MAXCTL][32];

	OnyxHost () : root (0), console (false), lastPresent (0), fsBuf (0), fsW (0), fsH (0), nctl (0)
	{
		args[0] = 0;
		int h = 0, m = 0, s = 0;
		kapi_get_datetime (0, 0, 0, &h, &m, &s);
		tBase = h * 3600.0 + m * 60 + s; tTicks0 = kapi_get_ticks ();
		for (int i = 0; i < MAXCTL; i++) { ctl[i] = 0; ddItems[i] = 0; }
	}

	// ---- the platform (bas::ScreenHost) --------------------------------------------------------------
	int winW = 0, winH = 0;
	bool openWindow (int w, int h) override
	{
		root = new ScreenRoot (w, h, title[0] ? title : "BASIC");
		if (root->canvas.px == 0) { delete root; root = 0; return false; }
		winW = w; winH = h;				// the window buffer's size (and row pitch)
		root->attach ();				// (Root::run is not used: we pump ourselves)
		return true;
	}
	void resizeWindow (int w, int h) override
	{
		// The window's buffer keeps the size it was made with (the kernel clamps to it) and
		// its row pitch: draw with that pitch, whatever the new size.
		if (w > winW) w = winW;
		if (h > winH) h = winH;
		root->canvas.adopt (kapi_resize_window (w, h), w, h, winW);
		uikit::uk_decorate_window ();					// the frame follows
		root->width = w; root->height = h;
	}
	void pageChanged () override { root->vis = visible (); root->vw = W; root->vh = H; root->sx = sx; root->sy = sy; }
	void markDirty () override { root->invalidate (true); }
	void present (bool force) override
	{
		if (!root) return;
		unsigned now = kapi_get_ticks ();
		if (!force && now - lastPresent < 2) return;	// ~50 Hz at most
		lastPresent = now;
		if (fsBuf) { blitFull (); kapi_present_fb (); return; }
		if (!root->valid) { root->draw (); kapi_present (); }
	}
	void pumpEvents () override { pump_events (); if (root && !fsBuf) root->tooltipTick (); }
	// 3D on the GPU (kapi v53): the textures, the frame drawn right into the active page
	int gpuState = -1;					// -1 not asked, 0 no GPU, 1 yes
	bool gpuReady ()
	{
		if (gpuState < 0) gpuState = kapi_gpu_info (0, 0) == 1 && kapi_gpu_texture (-2, 0, 0, 0, 0) != -1 ? 1 : 0;
		return gpuState == 1;
	}
	bool gpu3d () override { return gpuReady (); }
	int gpuTexture (const unsigned *px, int w, int h) override
	{
		if (!gpuReady ()) return -1;
		return kapi_gpu_texture (-1, px, w, h, w);
	}
	int gpuRender (unsigned *dst, int w, int h, const bas::G3Vertex *v, int nv, const bas::G3Batch *b, int nb, unsigned clear, bool keep) override
	{
		if (!gpuReady ()) return -1;
		struct kapi_gpu_frame f = { dst, w, h, w, clear, keep ? KAPI_GPU_F_KEEP : 0u };
		int r = kapi_gpu_render (&f, (const struct kapi_gpu_vertex3 *) v, (unsigned) nv, (const struct kapi_gpu_batch *) b, (unsigned) nb);
		if (r == -1 || r == -3) gpuState = 0;		// (no GPU / it stopped: software from now on)
		return r;
	}
	bool stopRequested () override { return (console && !root) ? false : should_exit () != 0; }
	unsigned nowMs () override { return kapi_get_ticks () * 10; }
	// basic -p: the profile, written to SD:/basprof.txt every few seconds and at the end (and printed at the end)
	bas::Profile profile;
	unsigned clockUs () override { return kapi_clock_us (); }
	void profReport () override
	{
		char t[320]; int n = profile.text (t, sizeof t - 2);
		t[n++] = '\n'; t[n] = 0;
		save ("SD:/basprof.txt", t, n);
	}
	void sleepRaw (int ms) override { kapi_msleep ((unsigned) (ms > 0 ? ms : 1)); }
	bool keyHeld (int key) override { return kapi_key_held (key) != 0; }
	unsigned padButtons (int pad) override { return pad_buttons (pad); }
	int padAxis (int pad, int axis) override
	{
		struct pad_input in;
		if (!pad_read (pad, &in)) return 0;
		return axis == 0 ? in.lx : axis == 1 ? in.ly : axis == 2 ? in.rx : axis == 3 ? in.ry : 0;
	}

	// Full screen: the visible page scaled into the display, proportions kept.
	void blitFull ()
	{
		const unsigned *v = visible ();
		int dw = W * sx, dh = H * sy;			// the displayed aspect
		int ow, oh;
		if ((long) fsW * dh <= (long) fsH * dw) { ow = fsW; oh = (int) ((long) fsW * dh / dw); }
		else { oh = fsH; ow = (int) ((long) fsH * dw / dh); }
		// A whole-number zoom when it fills nearly as much (1024 x 768: 320 x 200 at 3x, not
		// 3.2x): every pixel the same size, no uneven columns.
		int k = sx == sy ? ow / W : 0;
		if (k >= 1 && k * W * 100 >= ow * 85) { ow = k * W; oh = k * H; }
		int ox = (fsW - ow) / 2, oy = (fsH - oh) / 2;
		root->fsOx = ox; root->fsOy = oy; root->fsW = ow; root->fsH = oh; root->vw = W; root->vh = H;
		static int xmap[4096];
		for (int x = 0; x < ow && x < 4096; x++) xmap[x] = x * W / ow;
		for (int y = 0; y < oh; y++)
		{
			const unsigned *s = v + (long) (y * H / oh) * W;
			unsigned *d = fsBuf + (long) (oy + y) * fsW + ox;
			for (int x = 0; x < ow && x < 4096; x++) d[x] = s[xmap[x]];
		}
	}
	void fullscreen (bool on) override
	{
		if (!ensureWindow ()) return;
		if (on && !fsBuf)
		{
			fsBuf = kapi_fullscreen_begin (&fsW, &fsH);
			if (!fsBuf) return;
			for (long i = 0; i < (long) fsW * fsH; i++) fsBuf[i] = 0;
			root->fs = true;
			present (true);
		}
		else if (!on && fsBuf)
		{
			kapi_fullscreen_end (); fsBuf = 0; root->fs = false;
			root->invalidate (true); present (true);
		}
	}
	void leaveFullscreen () override { if (fsBuf) fullscreen (false); }
	// A windowed app (WINDOW) in the theme's colours: the windows' face, the text on it.
	bool windowColours (unsigned *face, unsigned *text) override
	{
		uikit::init ();					// (the theme: read once, SD:/etc/theme.txt)
		*face = C_BG; *text = C_TEXT;
		return true;
	}

	// ---- the console: started from a terminal, before the program opens its window ----------------------
	bool windowText () { return root != 0 || !console; }
	void out (const char *s, int n) override
	{
		if (windowText ()) { ScreenHost::out (s, n); return; }
		char t[256];						// a terminal: its font is Latin-1
		for (int i = 0; i < n; )
		{
			int m = 0;
			while (i < n && m < (int) sizeof t) t[m++] = (char) bas437ToLatin1[(unsigned char) s[i++]];
			kapi_stdout_write (t, (unsigned) m);
		}
		for (int i = 0; i < n; i++) ccol = s[i] == '\n' ? 0 : ccol + 1;
	}
	int inputLine (char *buf, int cap) override
	{
		if (windowText ()) return ScreenHost::inputLine (buf, cap);
		int n = 0;
		for (;;)
		{
			char c;
			if (kapi_stdin_read (&c, 1) <= 0) return n ? n : -1;
			if (c == 4) return n ? n : -1;
			if (c == '\r') continue;
			if (c == '\n') break;
			if (n < cap - 1) buf[n++] = (char) basLatin1To437[(unsigned char) c];
		}
		buf[n] = 0; ccol = 0;
		return n;
	}
	int inkey (char *o) override
	{
		if (root || !console) return ScreenHost::inkey (o);
		char c;
		if (kapi_kbd_ready () && kapi_stdin_read (&c, 1) > 0) { o[0] = (char) basLatin1To437[(unsigned char) c]; return 1; }
		return 0;
	}
	void cls (int m) override
	{
		if (!windowText ()) { kapi_stdout_write ("\n", 1); return; }
		ScreenHost::cls (m);
	}

	// ---- time --------------------------------------------------------------------------------------
	double timer () override
	{
		double t = tBase + (kapi_get_ticks () - tTicks0) / 100.0;
		while (t >= 86400) t -= 86400;
		return t;
	}
	static void two (char *p, int v) { p[0] = (char) ('0' + v / 10 % 10); p[1] = (char) ('0' + v % 10); }
	void date (char *o) override
	{
		int y = 0, mo = 0, d = 0; kapi_get_datetime (&y, &mo, &d, 0, 0, 0);
		two (o, mo); o[2] = '-'; two (o + 3, d); o[5] = '-'; two (o + 6, y / 100); two (o + 8, y % 100); o[10] = 0;
	}
	void time (char *o) override
	{
		int h = 0, m = 0, s = 0; kapi_get_datetime (0, 0, 0, &h, &m, &s);
		two (o, h); o[2] = ':'; two (o + 3, m); o[5] = ':'; two (o + 6, s); o[8] = 0;
	}
	unsigned seed () override { return kapi_get_ticks () * 2654435761u; }

	// ---- sound: SOUND / PLAY's voices are AudioKit's synthesizer (ak_fm_*; the kernel's until 2026-10-05) ----
	int audio = 0;					// 1: a voice was played
	bool soundReady () override { return true; }
	int note (int voice, double freq, int wave, int vol) override
	{
		if (freq <= 0) { if (audio == 1) ak_fm_stop (voice); return 0; }
		audio = 1;
		return ak_fm_start (voice, (unsigned) (freq * 1000), wave, vol) == 0 ? 0 : -1;
	}
	void endSound () override
	{
		if (akUsed) { ak_play_stop (); ak_notes_off (); }
		if (audio == 1) { ak_fm_silence (); audio = 0; }
	}

	// ---- AudioKit (SD:/lib/audiokit.so): PLAYFILE, MIDINOTE ... ---------------------------------------------
	bool akUsed = false;
	void akStart () { akUsed = true; }
	int akPlay (const char *path, int loop) override { akStart (); return ak_play (path, loop); }
	void akCommand (int what, int v) override
	{
		akStart ();
		if (what == 0) ak_play_stop ();
		else if (what == 1) ak_play_pause (v);
		else if (what == 2) ak_play_volume (v < 0 ? 0 : v);
		else if (what == 3) ak_notes_off ();
	}
	double akQuery (int what) override
	{
		if (!akUsed) return 0;
		return what == 0 ? ak_play_state () : what == 1 ? ak_play_pos_ms () / 1000.0 : ak_play_len_ms () / 1000.0;
	}
	int akMidi (int cmd, int ch, int d1, int d2) override
	{
		akStart ();
		if (cmd == 0x90) return ak_note_on (ch, d1, d2);
		if (cmd == 0x80) ak_note_off (ch, d1);
		else if (cmd == 0xC0) ak_program (ch, d1);
		else if (cmd == 0xB0) ak_control (ch, d1, d2);
		return 0;
	}
	const char *akError () override { return ak_play_error (); }
	double akNoteHz (int key) override { return ak_note_mhz (key) / 1000.0; }
	int akNoteKey (const char *name) override { return ak_note_parse (name); }

	// ---- GUI controls (uikit) -------------------------------------------------------------------------------
	int control (int kind, int x, int y, int w, int h, const char *text, int val) override
	{
		if (!ensureWindow () || nctl >= MAXCTL - 1) return 0;
		int id = nctl + 1;
		Widget *wd = 0;
		switch (kind)
		{
		case bas::CTL_BUTTON:   wd = new Button (x, y, w, h, text, on_control); break;
		case bas::CTL_LABEL:    wd = new Label (x, y, w, h, text, uk_ink_for (rgb (bg, 0)), rgb (bg, 0)); break;
		case bas::CTL_TEXTBOX:  wd = new Textbox (x, y, w, h, text, on_control); break;
		case bas::CTL_CHECKBOX: wd = new Checkbox (x, y, w, h, text, val != 0, on_control, rgb (bg, 0)); break;
		case bas::CTL_PROGRESS: wd = new Progress (x, y, w, h, 0, 100, val); break;
		case bas::CTL_SLIDER:   wd = new Slider (x, y, w, h, 0, val > 0 ? val : 100, 0, on_control, rgb (bg, 0)); break;
		case bas::CTL_LISTBOX:
		{
			ListBox *lb = new ListBox (x, y, w, h, on_control, on_control);
			char item[128]; int n = 0;
			for (int i = 0; ; i++)
			{
				char c = text[i];
				if (c == '|' || c == 0) { item[n] = 0; if (n || c == '|') lb->add (item); n = 0; if (!c) break; continue; }
				if (n < 127) item[n++] = c;
			}
			wd = lb; break;
		}
		case bas::CTL_DROPDOWN:
		{
			int len = slen (text);
			char *copy = new char[len + 1]; scpy (copy, text, len + 1);
			ddItems[id] = copy;
			int n = 0; ddPtr[id][n++] = copy;
			for (int i = 0; i < len && n < 32; i++) if (copy[i] == '|') { copy[i] = 0; ddPtr[id][n++] = copy + i + 1; }
			wd = new Dropdown (x, y, w, h, ddPtr[id], n, 0, on_control);
			break;
		}
		default: return 0;
		}
		wd->tag = id;
		ctl[id] = wd; ctlKind[id] = kind; nctl++;
		root->addChild (wd);
		dirty ();
		return id;
	}
	Widget *get (int id) { return id > 0 && id <= nctl ? ctl[id] : 0; }
	// ---- the controls' place and state, the window's flags, the menus (QBStudio's code) ------------------------
	void moveControl (int id, int x, int y, int w, int h) override
	{
		Widget *wd = get (id); if (!wd) return;
		wd->left = x; wd->top = y; wd->resizeTo (w > 1 ? w : 1, h > 1 ? h : 1);
		root->invalidate (true); dirty ();
	}
	void showControl (int id, bool on) override { Widget *wd = get (id); if (!wd) return; wd->hidden = !on; root->invalidate (true); dirty (); }
	void enableControl (int id, bool on) override { Widget *wd = get (id); if (!wd) return; wd->disabled = !on; wd->invalidate (true); dirty (); }
	void focusControl (int id) override { Widget *wd = get (id); if (wd) wd->setFocus (); }
	void windowFlags (int flags) override { if (ensureWindow () && (flags & 1)) root->setResizable (true); }
	enum { MAXMENU = 48 };
	struct MenuItem { char title[32], item[64], key[16]; int id; };
	MenuItem menus[MAXMENU]; int nmenu = 0;
	Menu *menuBar = 0;
	int menuItem (const char *title, const char *item, const char *key) override;
	void setText (int id, const char *s) override
	{
		Widget *w = get (id); if (!w) return;
		switch (ctlKind[id])
		{
		case bas::CTL_BUTTON: { Button *b = (Button *) w; scpy (b->text, s, sizeof b->text); b->invalidate (true); break; }
		case bas::CTL_LABEL: ((Label *) w)->setText (s); break;
		case bas::CTL_TEXTBOX: ((Textbox *) w)->setText (s); break;
		case bas::CTL_CHECKBOX: { Checkbox *c = (Checkbox *) w; scpy (c->text, s, sizeof c->text); c->invalidate (true); break; }
		case bas::CTL_LISTBOX: ((ListBox *) w)->add (s); break;		// SETTEXT on a list = add an item
		}
		dirty ();
	}
	int getText (int id, char *buf, int cap) override
	{
		Widget *w = get (id); buf[0] = 0; if (!w) return 0;
		switch (ctlKind[id])
		{
		case bas::CTL_BUTTON: scpy (buf, ((Button *) w)->text, cap); break;
		case bas::CTL_LABEL: scpy (buf, ((Label *) w)->text, cap); break;
		case bas::CTL_TEXTBOX: scpy (buf, ((Textbox *) w)->text, cap); break;
		case bas::CTL_CHECKBOX: scpy (buf, ((Checkbox *) w)->text, cap); break;
		case bas::CTL_LISTBOX: { ListBox *l = (ListBox *) w; scpy (buf, l->item (l->sel), cap); break; }
		case bas::CTL_DROPDOWN: { Dropdown *d = (Dropdown *) w; if (d->sel >= 0 && d->sel < d->nopts) scpy (buf, d->opts[d->sel], cap); break; }
		}
		return slen (buf);
	}
	int getValue (int id) override
	{
		Widget *w = get (id); if (!w) return 0;
		switch (ctlKind[id])
		{
		case bas::CTL_CHECKBOX: return ((Checkbox *) w)->checked ? -1 : 0;
		case bas::CTL_LISTBOX: return ((ListBox *) w)->sel;
		case bas::CTL_DROPDOWN: return ((Dropdown *) w)->sel;
		case bas::CTL_PROGRESS: return ((Progress *) w)->value;
		case bas::CTL_SLIDER: return ((Slider *) w)->value;
		}
		return 0;
	}
	void setValue (int id, int v) override
	{
		Widget *w = get (id); if (!w) return;
		switch (ctlKind[id])
		{
		case bas::CTL_CHECKBOX: ((Checkbox *) w)->checked = v != 0; w->invalidate (true); break;
		case bas::CTL_LISTBOX: ((ListBox *) w)->setSel (v); break;
		case bas::CTL_DROPDOWN: { Dropdown *d = (Dropdown *) w; if (v >= 0 && v < d->nopts) { d->sel = v; d->invalidate (true); } break; }
		case bas::CTL_PROGRESS: ((Progress *) w)->setValue (v); break;
		case bas::CTL_SLIDER: { Slider *s = (Slider *) w; s->value = v < s->vmin ? s->vmin : v > s->vmax ? s->vmax : v; s->invalidate (true); break; }
		}
		dirty ();
	}
	// ---- system -----------------------------------------------------------------------------------------
	void notify (const char *t, const char *m) override { ::notify (t, m); }
	int msgbox (const char *t, const char *m, int b) override
	{
		if (!ensureWindow ()) return 0;
		int r = uk_messagebox (t, m, b);
		dirty (); present (true);
		return r;
	}
	int clipboard (char *buf, int cap) override { return clip_get_text (buf, cap); }	// (the shared clipboard: clipboard.h)
	void setClipboard (const char *s) override { clip_set_text (s); }
	bool fileDialog (bool save, const char *dir, char *o, int cap) override
	{
		if (!ensureWindow ()) return false;
		char name[128]; scpy (name, o, sizeof name);
		bool ok = save ? uk_file_save (o, (unsigned) cap, dir[0] ? dir : "SD:/", name[0] ? name : "untitled.txt")
			       : uk_file_open (o, (unsigned) cap, dir[0] ? dir : "SD:/");
		dirty (); present (true);
		return ok;
	}
	bool exec (const char *p, const char *a) override { return kapi_exec (p, a) != 0; }
	bool launch (const char *app) override { return kapi_launch (app) != 0; }
	bool chdir (const char *p) override { return kapi_chdir (p) != 0; }
	// SHELL "prog args": runs a /bin tool (or a path), its output on the screen; SHELL alone
	// opens a terminal.
	int shell (const char *cmd) override
	{
		int i = 0; while (cmd[i] == ' ') i++;
		if (!cmd[i]) return kapi_launch ("terminal") ? 0 : -1;
		char prog[200], path[220]; int n = 0;
		while (cmd[i] && cmd[i] != ' ' && n < 199) prog[n++] = cmd[i++];
		prog[n] = 0;
		while (cmd[i] == ' ') i++;
		bool hasPath = false; for (int k = 0; prog[k]; k++) if (prog[k] == '/' || prog[k] == ':') hasPath = true;
		if (hasPath) scpy (path, prog, sizeof path);
		else { scpy (path, "SD:/bin/", sizeof path); int k = slen (path); for (int j = 0; prog[j] && k < 218; j++) path[k++] = prog[j]; path[k] = 0; }
		if (!windowText ())				// the console: the tool writes there itself
		{
			void *pr = kapi_spawn (path, cmd + i, 0, kapi_stdout ());
			if (!pr) return -1;
			return kapi_wait (pr);
		}
		void *outp = kapi_pipe ();
		void *pr = kapi_spawn (path, cmd + i, 0, outp);
		if (!pr) { kapi_stream_close (outp); return -1; }
		char b[256];
		for (;;)
		{
			int r = kapi_stream_read_nb (outp, b, sizeof b);
			if (r > 0) { out (b, r); continue; }
			if (kapi_proc_done (pr)) { while ((r = kapi_stream_read_nb (outp, b, sizeof b)) > 0) out (b, r); break; }
			pump (); kapi_msleep (5);
		}
		int rc = kapi_wait (pr);
		kapi_stream_close (outp);
		return rc;
	}

	// ---- files ---------------------------------------------------------------------------------------------
	char *load (const char *path, int *len) override
	{
		*len = 0;
		void *f = kapi_open (path);
		if (!f) return 0;
		unsigned n = kapi_fsize (f);
		char *b = new char[n + 1];
		int r = kapi_read (f, b, n);
		kapi_close (f);
		*len = r > 0 ? r : 0; b[*len] = 0;
		return b;
	}
	bool save (const char *path, const char *d, int n) override { return kapi_save_file (path, d, (unsigned) n) >= 0; }
	bool remove (const char *p) override { return kapi_remove (p) == 0; }
	bool rename (const char *a, const char *b) override { return kapi_rename (a, b) == 0; }
	bool makeDir (const char *p) override { return kapi_mkdir (p) == 0; }
	bool exists (const char *p) override
	{
		void *f = kapi_open (p); if (f) { kapi_close (f); return true; }
		void *d = kapi_opendir (p); if (d) { kapi_closedir (d); return true; }
		return false;
	}
	int listDir (const char *dir, int index, char *o, int cap) override
	{
		o[0] = 0;
		void *d = kapi_opendir (dir[0] ? dir : ".");
		if (!d) return 0;
		struct kapi_dirent e; int i = 0;
		while (kapi_readdir (d, &e))
		{
			if (e.name[0] == '.') continue;
			if (i++ == index) { scpy (o, e.name, cap); if (e.is_dir) { int n = slen (o); if (n < cap - 1) { o[n] = '/'; o[n + 1] = 0; } } break; }
		}
		kapi_closedir (d);
		return slen (o);
	}
	const char *command () override { return args; }

};

static void host_key (long k) { if (g_host) g_host->pushKey (k); }
static void host_mouse (int x, int y, int b) { if (g_host) g_host->setMouse (x, y, b); }

static void on_control (Widget &w) { if (g_host) g_host->pushEvent (w.tag); }
void ScreenRoot::onResized () { if (g_host) g_host->userResized (width, height); }

static bool ci_same (const char *a, const char *b)
{
	for (; *a && *b; a++, b++) { char x = *a >= 'a' && *a <= 'z' ? (char) (*a - 32) : *a, y = *b >= 'a' && *b <= 'z' ? (char) (*b - 32) : *b; if (x != y) return false; }
	return *a == *b;
}
// The menus' items: a callback each (Menu's take no argument), their ids in a table
static int g_menuIds[OnyxHost::MAXMENU];
template <int N> static void menu_thunk () { if (g_host) g_host->pushEvent (g_menuIds[N]); }
template <int N> struct MenuThunks { static void fill (MenuAction *t) { t[N - 1] = menu_thunk<N - 1>; MenuThunks<N - 1>::fill (t); } };
template <> struct MenuThunks<0> { static void fill (MenuAction *) {} };
// MENUITEM (title$, item$[, key$]): the item added to its menu (made at its first item); its id, as a control's
int OnyxHost::menuItem (const char *title, const char *item, const char *key)
{
	if (!ensureWindow () || nmenu >= MAXMENU || nctl >= MAXCTL - 1) return 0;
	int id = nctl + 1; nctl++;
	ctl[id] = 0; ctlKind[id] = 0;
	MenuItem &m = menus[nmenu++];
	scpy (m.title, title, sizeof m.title); scpy (m.item, item, sizeof m.item); scpy (m.key, key ? key : "", sizeof m.key); m.id = id;
	// the bar made again: the menus in the order of their first items, "-" a separator; "Ctrl+X" a shortcut
	static MenuAction thunks[MAXMENU];
	static bool filled; if (!filled) { MenuThunks<MAXMENU>::fill (thunks); filled = true; }
	delete menuBar; menuBar = new Menu;
	bool done[MAXMENU] = { false };
	for (int i = 0; i < nmenu; i++)
	{
		if (done[i]) continue;
		menuBar->menu (menus[i].title);
		for (int j = i; j < nmenu; j++)
		{
			if (done[j] || !ci_same (menus[j].title, menus[i].title)) continue;
			done[j] = true;
			if (menus[j].item[0] == '-' && !menus[j].item[1]) { menuBar->separator (); continue; }
			g_menuIds[j] = menus[j].id;
			const char *k = menus[j].key; long code = 0; char shown[16] = "";
			if (ci_same (k, "") == false && (k[0] == 'C' || k[0] == 'c') && (k[1] == 't' || k[1] == 'T') && (k[2] == 'r' || k[2] == 'R') && (k[3] == 'l' || k[3] == 'L') && k[4] == '+' && k[5]) { char c = k[5] >= 'a' && k[5] <= 'z' ? (char) (k[5] - 32) : k[5]; code = UK_CTRL (c); shown[0] = '^'; shown[1] = c; shown[2] = 0; }
			menuBar->item (menus[j].item, shown, code, thunks[j]);
		}
	}
	menuBar->publish ();
	return id;
}

int main (void)
{
	static char argbuf[512];
	kapi_get_args (argbuf, sizeof argbuf);
	char path[256], cwd[256] = ""; int i = 0, n = 0;
	bool ide = false, compileOnly = false, prof = false;
	static char service[32] = "qbasic";
	// Options: -d <dir> (current directory, default: the program's folder), -i (report a
	// syntax / runtime error to the qbasic editor over IPC: service "qbasic"; -s <name>: to the service
	// <name>, QBStudio's "qbstudio"), -c (compile
	// only: write the program's .bax -- basic -c prog.bas -> prog.bax -- and stop).
	for (;;)
	{
		while (argbuf[i] == ' ') i++;
		if (argbuf[i] == '-' && argbuf[i + 1] == 'i' && (argbuf[i + 2] == ' ' || !argbuf[i + 2])) { ide = true; i += 2; continue; }
		if (argbuf[i] == '-' && argbuf[i + 1] == 's' && argbuf[i + 2] == ' ')	// -s <service>: as -i, to another editor (QBStudio)
		{
			i += 3; while (argbuf[i] == ' ') i++;
			int k = 0; while (argbuf[i] && argbuf[i] != ' ' && k < 31) service[k++] = argbuf[i++];
			service[k] = 0; ide = true; continue;
		}
		if (argbuf[i] == '-' && argbuf[i + 1] == 'c' && (argbuf[i + 2] == ' ' || !argbuf[i + 2])) { compileOnly = true; i += 2; continue; }
		if (argbuf[i] == '-' && argbuf[i + 1] == 'p' && (argbuf[i + 2] == ' ' || !argbuf[i + 2])) { prof = true; i += 2; continue; }	// -p: where the time goes
		if (argbuf[i] == '-' && argbuf[i + 1] == 'd' && argbuf[i + 2] == ' ')
		{
			i += 3; while (argbuf[i] == ' ') i++;
			int k = 0;
			if (argbuf[i] == '"') { i++; while (argbuf[i] && argbuf[i] != '"' && k < 255) cwd[k++] = argbuf[i++]; if (argbuf[i]) i++; }
			else while (argbuf[i] && argbuf[i] != ' ' && k < 255) cwd[k++] = argbuf[i++];
			cwd[k] = 0;
			continue;
		}
		break;
	}
	if (argbuf[i] == '"') { i++; while (argbuf[i] && argbuf[i] != '"' && n < 255) path[n++] = argbuf[i++]; if (argbuf[i]) i++; }
	else while (argbuf[i] && argbuf[i] != ' ' && n < 255) path[n++] = argbuf[i++];
	path[n] = 0;
	while (argbuf[i] == ' ') i++;

	static OnyxHost host;
	g_host = &host;
	host.console = kapi_stdout () != 0;
	scpy (host.args, argbuf + i, sizeof host.args);
	if (!path[0])
	{
		ax_putln ("usage: basic <program.bas | program.bax> [arguments]   basic -c <program.bas>");
		return 1;
	}

	// Window title: the app name (apps/<name>.app/main.bas) or the file name.
	{
		int e = slen (path), s = e; while (s > 0 && path[s - 1] != '/' && path[s - 1] != ':') s--;
		const char *base = path + s;
		if ((base[0] == 'm' || base[0] == 'M') && s >= 5 && path[s - 5] == '.')		// "<name>.app/main.bas"
		{
			int ps = s - 1; while (ps > 0 && path[ps - 1] != '/' && path[ps - 1] != ':') ps--;
			int k = 0; for (int j = ps; j < s - 5 && k < 63; j++) host.title[k++] = path[j];
			host.title[k] = 0;
		}
		else scpy (host.title, base, sizeof host.title);
	}

	int len = 0;
	char *src = host.load (path, &len);
	if (!src)
	{
		if (host.console) { ax_puts ("basic: cannot read "); ax_putln (path); }
		else notify ("BASIC", "Cannot read the program file.");
		return 1;
	}
	// The program's folder becomes the current directory (relative file names).
	if (compileOnly) {}					// (paths stay the shell's)
	else if (cwd[0]) kapi_chdir (cwd);
	else
	{
		char dir[256]; scpy (dir, path, sizeof dir);
		int e = slen (dir); while (e > 0 && dir[e - 1] != '/') e--;
		if (e > 0) { dir[e - 1 > 0 && dir[e - 2] != ':' ? e - 1 : e] = 0; kapi_chdir (dir); }
	}

	bas::Error err;
	bas::Program *prog = bas::load (src, len, &err);	// a .bax runs as it is
	delete [] src;
	if (compileOnly)					// basic -c prog.bas: prog.bax
	{
		if (!prog) { ax_puts ("Syntax error in line "); char n[12]; n[bas::formatNum (err.line, n)] = 0; ax_puts (n); ax_puts (": "); ax_putln (err.msg); return 2; }
		char out[260]; scpy (out, path, sizeof out);
		int e = slen (out), d = e; while (d > 0 && out[d - 1] != '.' && out[d - 1] != '/') d--;
		if (d > 0 && out[d - 1] == '.') e = d - 1;
		scpy (out + e, ".bax", sizeof out - e);
		char *bytes; int n = bas::saveBax (prog, &bytes);
		bool ok = kapi_save_file (out, bytes, (unsigned) n) >= 0;
		delete [] bytes; bas::destroy (prog);
		ax_puts (ok ? "compiled: " : "cannot write "); ax_putln (out);
		return ok ? 0 : 1;
	}
	char msg[200];
	auto report = [&] (const char *kind)
	{
		int k = 0;
		for (int j = 0; kind[j] && k < 190; j++) msg[k++] = kind[j];
		char num[12]; int nl = bas::formatNum (err.line, num);
		for (int j = 0; j < nl && k < 190; j++) msg[k++] = num[j];
		msg[k++] = ':'; msg[k++] = ' ';
		for (int j = 0; err.msg[j] && k < 198; j++) msg[k++] = err.msg[j];
		msg[k] = 0;
		if (ide)						// tell the editor: line \0 message
		{
			int pid = kapi_ipc_lookup (service);
			if (pid)
			{
				char m[180]; int q = bas::formatNum (err.line, m); m[q++] = 0;
				for (int j = 0; err.msg[j] && q < 178; j++) m[q++] = err.msg[j];
				m[q++] = 0;
				kapi_mailbox_send (pid, 1, m, (unsigned) q);
			}
		}
		if (host.fsBuf) host.fullscreen (false);
		if (host.root) { uk_messagebox ("BASIC", msg, MB_OK); }
		else if (host.console) ax_putln (msg);
		else notify (host.title, msg);
	};
	if (!prog) { report ("Syntax error in line "); return 2; }
	if (prof) host.prof = &host.profile;
	int r = bas::run (prog, host, &err);
	if (prof && host.console) { char t[320]; host.profile.text (t, sizeof t); ax_putln (t); }
	if (r) report ("Error in line ");
	bas::destroy (prog);
	return r ? 3 : 0;
}
