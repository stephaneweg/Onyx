//
// ui/editor.h -- the base of the context editors (the bottom pane: the selected block's editor). An
// Editor is a Panel that lays out uikit controls (labels, drop-downs, numeric fields, check boxes,
// buttons, sliders) as a form -- rows of a label and its control, in columns that wrap when the
// pane is full -- and, on its right, the hand-drawn pieces (a NoteGrid, a keyboard, the rings). The
// controls' callbacks all go through one trampoline to the editor's control (tag); an edit is
// bracketed by begin () (the undo checkpoint) and done () (the song changed: redrawn, recompiled).
// An editor that must show other controls after an edit (a style picked: its grid appears) asks
// to be rebuilt (g_rebuildEditor): the host makes it again from the model.
//
#ifndef _koton_editor_h
#define _koton_editor_h

#include "ui/grid.h"
#include "ui/audio.h"
#include "ui/ops.h"

namespace kui {

class Editor;
extern Editor *g_editor;			// the one shown
extern void (*g_onEdited) ();			// the app: the song changed (redraw the lanes...)
extern bool g_rebuildEditor;			// set by an editor: make it again (after the current event)
extern char g_status[160];			// the status bar's message

enum { ROW_H = 30, FIELD_H = 24 };

class Editor : public Panel
{
public:
	int track, item;
	Editor (int l, int t, int w, int h, int tr, int it) : Panel (l, t, w, h, PANEL), track (tr), item (it),
		m_fx (0), m_fy (0), m_fx0 (0), m_fy0 (0), m_colW (0), m_right (0), m_nSec (0), m_nHint (0)
	{ anchor = ANCHOR_FILL; }
	virtual ~Editor () { for (int i = 0; i < m_arrays.size (); i++) { for (int j = 0; j < m_arrayN[i]; j++) free (m_arrays[i][j]); free (m_arrays[i]); } }
	virtual void control (int tag, Widget &w) { (void) tag; (void) w; }
	virtual void refresh () {}			// the model changed elsewhere (undo, the lanes): re-read it
	virtual void tick () {}				// ~60 Hz: the playhead...
	virtual const char *title () { return "Editor"; }
	// a note from a MIDI keyboard: taken (true), else the app plays it on the selected track
	virtual bool midiNote (int note, int vel, bool on) { (void) note; (void) vel; (void) on; return false; }
	// what "Listen" plays (0: nothing to hear): by default the block alone, looping, with its track's sound
	virtual CompiledSong *previewSong () { return compileModulePreview (g_doc.p, track, item, SOUND_RATE); }
	Module *module () { return g_doc.module (track, item); }
	Track &trk () { return g_doc.p.tracks[track]; }
	double startBeat () { return g_doc.itemStart (track, item); }
	double lenBeats () { return g_doc.itemLen (track, item); }

	// the undo checkpoint (a slider dragged or a number held: one checkpoint for the gesture)
	void begin ()
	{
		unsigned now = kapi_get_ticks ();
		bool same = m_curTag >= 0 && m_curTag == s_lastTag && this == s_lastEd && now - s_lastTick < 100;
		if (!same) g_doc.checkpoint ();
		s_lastTag = m_curTag; s_lastEd = this; s_lastTick = now;
		m_len0 = item >= 0 ? g_doc.itemLen (track, item) : 0;
	}
	// the block's length may have changed: the next items keep their places
	void done () { if (item >= 0 && fabs (g_doc.itemLen (track, item) - m_len0) > 1e-9) g_doc.lengthChanged (track, item, m_len0); g_doc.changed (); if (g_onEdited) g_onEdited (); }
	void rebuild () { g_rebuildEditor = true; }

	void onDraw () override
	{
		canvas.clear (bg);
		for (int i = 0; i < m_nSec; i++) sectionAt (canvas, m_sec[i].x, m_sec[i].y, m_sec[i].w, m_sec[i].s);
		for (int i = 0; i < m_nHint; i++) wrapText (canvas, m_hint[i].x, m_hint[i].y, m_hint[i].w, m_hint[i].s, FAINT, 17);
		drawExtra (canvas);
	}
	virtual void drawExtra (Canvas &cv) { (void) cv; }

	// ---- controls -----------------------------------------------------------------------------------------------
	static void trampoline (Widget &w) { if (g_editor) { g_editor->m_curTag = w.tag; g_editor->control (w.tag, w); if (g_editor) g_editor->m_curTag = -1; } }
	int m_curTag = -1;
	static int s_lastTag; static Editor *s_lastEd; static unsigned s_lastTick;
	Label *label (int x, int y, int w, const char *s, unsigned fg = DIM)
	{
		Label *l = new Label (x, y, w, 22, s, fg, bg); addChild (l); return l;
	}
	Dropdown *dropdown (int x, int y, int w, int tag, const char *const *opts, int n, int sel)
	{
		Dropdown *d = new Dropdown (x, y, w, FIELD_H, opts, n, iclamp (sel, 0, n - 1), trampoline); d->tag = tag; addChild (d); return d;
	}
	NumericUpDown *number (int x, int y, int w, int tag, int lo, int hi, int v, int step = 1)
	{
		NumericUpDown *n = new NumericUpDown (x, y, w, FIELD_H, lo, hi, iclamp (v, lo, hi), step, trampoline); n->tag = tag; addChild (n); return n;
	}
	Checkbox *check (int x, int y, int w, const char *lab, int tag, bool on)
	{
		Checkbox *c = new Checkbox (x, y, w, 22, lab, on, trampoline, bg); c->tag = tag; addChild (c); return c;
	}
	Button *button (int x, int y, int w, const char *lab, int tag)
	{
		Button *b = new Button (x, y, w, 26, lab, trampoline); b->tag = tag; addChild (b); return b;
	}
	Slider *slider (int x, int y, int w, int tag, int lo, int hi, int v)
	{
		Slider *s = new Slider (x, y, w, 22, lo, hi, iclamp (v, lo, hi), trampoline, bg); s->tag = tag; addChild (s); return s;
	}
	// option strings made at run time, owned by the editor (freed with it)
	const char *const *strings (const char *const *src, int n)
	{
		char **a = (char **) malloc (sizeof (char *) * (size_t) (n > 0 ? n : 1));
		for (int i = 0; i < n; i++) { size_t l = strlen (src[i]); a[i] = (char *) malloc (l + 1); memcpy (a[i], src[i], l + 1); }
		m_arrays.push (a); m_arrayN.push (n);
		return (const char *const *) a;
	}

	// ---- the form: label + control rows in columns --------------------------------------------------------------
	// start at (x, y), columns colW px wide; the rows wrap to a new column at the bottom of the pane
	void formStart (int x, int y, int colW) { m_fx = m_fx0 = x; m_fy = m_fy0 = y; m_colW = colW; m_right = x + colW; }
	void formColumn () { m_fx += m_colW + 16; m_fy = m_fy0; m_right = m_fx + m_colW; }
	int rowY (int h = ROW_H) { if (m_fy + h > height - 6 && m_fy > m_fy0) formColumn (); int y = m_fy; m_fy += h; return y; }
	int formX () const { return m_fx; }
	int formRight () const { return m_right; }		// where the area right of the form starts
	enum { LABEL_W = 96 };
	Dropdown *fDrop (const char *lab, int tag, const char *const *opts, int n, int sel)
	{
		int y = rowY ();
		label (m_fx, y + 1, LABEL_W, lab);
		return dropdown (m_fx + LABEL_W, y, m_colW - LABEL_W, tag, opts, n, sel);
	}
	NumericUpDown *fNum (const char *lab, int tag, int lo, int hi, int v, int step = 1)
	{
		int y = rowY ();
		label (m_fx, y + 1, LABEL_W, lab);
		return number (m_fx + LABEL_W, y, imin (110, m_colW - LABEL_W), tag, lo, hi, v, step);
	}
	Checkbox *fCheck (const char *lab, int tag, bool on) { int y = rowY (26); return check (m_fx, y, m_colW, lab, tag, on); }
	Slider *fSlider (const char *lab, int tag, int lo, int hi, int v)
	{
		int y = rowY ();
		label (m_fx, y + 1, LABEL_W, lab);
		return slider (m_fx + LABEL_W, y + 1, m_colW - LABEL_W, tag, lo, hi, v);
	}
	Button *fButton (const char *lab, int tag, int w = 0) { int y = rowY (32); return button (m_fx, y + 2, w ? w : m_colW, lab, tag); }
	// two buttons side by side
	void fButtons (const char *a, int tagA, const char *b, int tagB)
	{
		int y = rowY (32), w = (m_colW - 8) / 2;
		button (m_fx, y + 2, w, a, tagA); button (m_fx + w + 8, y + 2, w, b, tagB);
	}
	void fSection (const char *s)
	{
		if (m_fy + 24 + ROW_H > height - 6 && m_fy > m_fy0) formColumn ();	// (a heading never alone at a column's foot)
		int y = rowY (24); addSection (m_fx, y + 4, m_colW, s);
	}
	void fNote (const char *s, int lines = 2)	// a grey hint (wrapped)
	{
		int y = rowY (17 * lines + 6);
		addHint (m_fx, y + 2, m_colW, s);
	}
	void addHint (int x, int y, int w, const char *s)
	{
		if (m_nHint < 8) { m_hint[m_nHint].x = x; m_hint[m_nHint].y = y; m_hint[m_nHint].w = w; snprintf (m_hint[m_nHint].s, sizeof m_hint[0].s, "%s", s); m_nHint++; }
	}
	void addSection (int x, int y, int w, const char *s)
	{
		if (m_nSec < 16) { m_sec[m_nSec].x = x; m_sec[m_nSec].y = y; m_sec[m_nSec].w = w; snprintf (m_sec[m_nSec].s, sizeof m_sec[0].s, "%s", s); m_nSec++; }
	}
	static void sectionAt (Canvas &cv, int x, int y, int w, const char *s)
	{
		textL (cv, x, y, 16, s, DIM, 2);
		int tx = x + tw (s, 2) + 8;
		hline (cv, tx, x + w, y + 8, LINE);
	}

	// ---- a grid on the right, with its bar (Listen is the host's; the resolution, the style seeds...) ----------
	static const char *const *resolutionNames () { static const char *const r[] = { "1 / beat", "2 / beat", "3 / beat", "4 / beat", "6 / beat", "8 / beat", "12 / beat", "24 / beat" }; return r; }
	static int resolutionIndex (int spb) { static const int v[] = { 1, 2, 3, 4, 6, 8, 12, 24 }; for (int i = 0; i < 8; i++) if (v[i] == spb) return i; return 3; }
	static int resolutionAt (int i) { static const int v[] = { 1, 2, 3, 4, 6, 8, 12, 24 }; return v[iclamp (i, 0, 7)]; }
	// notes rescaled from one resolution to another (those left outside `cols` dropped)
	static void rescale (Vec<RiffNote> &n, int from, int to, int cols)
	{
		if (from == to || from <= 0) return;
		Vec<RiffNote> k;
		for (int i = 0; i < n.size (); i++)
		{
			int st = iround (n[i].start * (double) to / from), ln = imax (1, iround (n[i].length * (double) to / from));
			if (st < cols) { RiffNote x = n[i]; x.start = st; x.length = imin (ln, cols - st); k.push (x); }
		}
		n = move (k);
	}
	// the grid's callbacks back to the editor
	static void gridBegin (NoteGrid &g) { ((Editor *) g.ctx)->begin (); }
	static void gridChanged (NoteGrid &g) { ((Editor *) g.ctx)->gridEdited (g); }
	virtual void gridEdited (NoteGrid &g) { (void) g; }
	NoteGrid *makeGrid (int x, int y, int w, int h)
	{
		NoteGrid *g = new NoteGrid (x, y, w, h);
		g->ctx = this; g->onBegin = gridBegin; g->onChange = gridChanged;
		addChild (g);
		return g;
	}

protected:
	double m_len0;
private:
	int m_fx, m_fy, m_fx0, m_fy0, m_colW, m_right;
	struct Sec { int x, y, w; char s[48]; } m_sec[16];
	int m_nSec;
	struct Hint { int x, y, w; char s[240]; } m_hint[8];
	int m_nHint;
	Vec<char **> m_arrays; Vec<int> m_arrayN;
};

int Editor::s_lastTag = -1; Editor *Editor::s_lastEd = 0; unsigned Editor::s_lastTick = 0;

// ---- a small keyboard with notes lit ------------------------------------------------------------------------------------
class MiniKeys : public Widget
{
public:
	int lowC;				// the first C shown (MIDI)
	int octaves;
	unsigned lit[4];			// bit per semitone from lowC
	unsigned litColour;
	MiniKeys (int l, int t, int w, int h) : Widget (l, t, w, h), lowC (48), octaves (3), litColour (0x78C4F0) { lit[0] = lit[1] = lit[2] = lit[3] = 0; }
	void clear () { lit[0] = lit[1] = lit[2] = lit[3] = 0; invalidate (true); }
	void light (int midi) { int k = midi - lowC; if (k >= 0 && k < 128) lit[k >> 5] |= 1u << (k & 31); invalidate (true); }
	bool on (int midi) const { int k = midi - lowC; return k >= 0 && k < 128 && ((lit[k >> 5] >> (k & 31)) & 1); }
	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (parent ? parent->bgColor () : BG);
		static const int white[7] = { 0, 2, 4, 5, 7, 9, 11 };
		int nW = octaves * 7, kw = width / nW;
		for (int i = 0; i < nW; i++)
		{
			int midi = lowC + 12 * (i / 7) + white[i % 7];
			box (cv, i * kw, 0, kw - 1, height, 3, on (midi) ? litColour : 0xFFFFFF);
			if (white[i % 7] == 0) { char b[8]; snprintf (b, sizeof b, "C%d", midi / 12 - 1); textC (cv, i * kw, height - 16, kw, 14, b, 0x505A64); }
		}
		static const int blackAfter[5] = { 0, 1, 3, 4, 5 };
		static const int blackPc[5] = { 1, 3, 6, 8, 10 };
		for (int o = 0; o < octaves; o++)
			for (int j = 0; j < 5; j++)
			{
				int x = (o * 7 + blackAfter[j] + 1) * kw - kw * 3 / 10;
				int midi = lowC + 12 * o + blackPc[j];
				box (cv, x, 0, kw * 6 / 10, height * 6 / 10, 2, on (midi) ? darker (litColour, 60) : 0x1E2024);
			}
	}
};

// ---- a row of tabs drawn as one pill (until uikit's SegmentedControl is used) ------------------------------------------------
class Tabs : public Widget
{
public:
	const char *const *names; int n, sel; Action cb;
	Tabs (int l, int t, int w, int h, const char *const *names_, int n_, int sel_, Action cb_) : Widget (l, t, w, h), names (names_), n (n_), sel (sel_), cb (cb_) {}
	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (parent ? parent->bgColor () : PANEL);
		box (cv, 0, 0, width, height, 6, FIELD);
		int sw = width / imax (1, n);
		for (int i = 0; i < n; i++)
		{
			if (i == sel) box (cv, i * sw + 2, 2, sw - 4, height - 4, 5, ACC2);
			else if (i) vline (cv, i * sw, 6, height - 6, LINE);
			textC (cv, i * sw, 0, sw, height, names[i], i == sel ? 0xFFFFFF : DIM);
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) my; (void) br; (void) bm; (void) wheel;
		if (m_b.edge (bl, 0) == 1)
		{
			int i = iclamp (mx / imax (1, width / imax (1, n)), 0, n - 1);
			if (i != sel) { sel = i; invalidate (true); if (cb) cb (*this); }
		}
		return true;
	}
private:
	Buttons m_b;
};

} // namespace kui

#endif
