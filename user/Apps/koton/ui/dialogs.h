//
// ui/dialogs.h -- Koton's dialogs (wtk Modals in the studio's dark theme): a text prompt, the sound
// of a track (the General MIDI families and their instruments, the SoundFont's drum kits -- heard as
// they are picked), the song's key / meter / tempo / feel (a key change transposes the song or lets
// the chords follow their degrees), a cadence written on the chord track; and what the arrangement
// asks of them: its menus (a track header's, a lane's), a block of the track's kind made by a
// double-click, a section marker.
//
#ifndef _koton_dialogs_h
#define _koton_dialogs_h

#include "ui/arrange.h"
#include "ui/ed_riff.h"

namespace kui {

// a widget's place in the window
static inline int absX (Widget *w) { int x = 0; while (w && w->parent) { x += w->left; w = w->parent; } return x; }
static inline int absY (Widget *w) { int y = 0; while (w && w->parent) { y += w->top; w = w->parent; } return y; }

static void dlgButton (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->onButton (w.tag); }

// a Modal of the studio: centred, its title strip, OK / Cancel at the bottom right
class KDialog : public Modal
{
public:
	const char *m_title;
	KDialog (int w, int h, const char *title, bool cancel = true) : Modal (w, h), m_title (title)
	{
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = (r->height - height) / 2; }
		Button *b = new Button (width - (cancel ? 196 : 104), height - 40, 90, 28, "OK", dlgButton); b->tag = 1; addChild (b);
		if (cancel) { b = new Button (width - 100, height - 40, 90, 28, "Cancel", dlgButton); b->tag = 0; addChild (b); }
	}
	void onButton (int tag) override { if (tag == 0 || tag == 1) close (tag); else onOther (tag); }
	virtual void onOther (int tag) { (void) tag; }
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { close (1); return true; }
		return false;
	}
	void onDraw () override { drawBox (m_title); drawMore (canvas); }
	virtual void drawMore (Canvas &cv) { (void) cv; }
	int top0 () const { return titleH () + 14; }
	Label *lab (int x, int y, int w, const char *s) { Label *l = new Label (x, y, w, 22, s, C_TEXT, C_FACE); addChild (l); return l; }
};

// ---- a line of text --------------------------------------------------------------------------------------------------
class PromptDialog : public KDialog
{
public:
	Textbox *box_;
	PromptDialog (const char *title, const char *prompt, const char *text) : KDialog (420, 150, title)
	{
		lab (16, top0 (), 388, prompt);
		box_ = new Textbox (16, top0 () + 28, 388, 26, text);
		addChild (box_);
		box_->setFocus ();
	}
};
static bool promptText (const char *title, const char *prompt, char *text, int cap)
{
	PromptDialog *d = new PromptDialog (title, prompt, text);
	int r = d->run ();
	if (r == 1) snprintf (text, cap, "%s", d->box_->text);
	delete d;
	return r == 1;
}
static bool confirm (const char *title, const char *text) { return wk_messagebox (title, text, MB_YESNO) == 1; }

// ---- the sound of a track ----------------------------------------------------------------------------------------------------
class SoundDialog : public KDialog
{
public:
	ListBox *fam, *inst;
	bool drum;
	int program, kit, family;
	Vec<Str> plugIds, plugNames;
	Str pluginId;				// an instrument plugin chosen (empty: the SoundFont)
	static void (*s_plugins) (Vec<Str> &ids, Vec<Str> &names);	// the instrument plugins (chain.h)
	SoundDialog (const Track &t) : KDialog (620, 420, t.type == TRACK_DRUM ? "Drum kit" : "Instrument"), fam (0), inst (0),
		drum (t.type == TRACK_DRUM), program (t.instrument), kit (t.drumKit), family (iclamp (t.instrument, 0, 127) / 8)
	{
		int y = top0 ();
		if (drum)
		{
			lab (16, y, 300, "The SoundFont's kits");
			inst = new ListBox (16, y + 26, width - 32, height - y - 80, pickInst, pickInst);
			for (int i = 0; i < g_names.nKits; i++) inst->add (g_names.kit[i]);
			if (!g_names.nKits) inst->add ("Standard kit");
			addChild (inst);
			inst->setSel (iclamp (kit, 0, imax (0, g_names.nKits - 1)));
		}
		else
		{
			lab (16, y, 220, "Family");
			lab (256, y, 300, "Instrument");
			fam = new ListBox (16, y + 26, 224, height - y - 80, pickFam, pickFam);
			for (int i = 0; i < 16; i++) fam->add (s_gmFamilies[i]);
			if (s_plugins) { s_plugins (plugIds, plugNames); if (plugIds.size ()) fam->add ("Plugin instruments"); }
			if (!t.instrumentPlugin.id.empty () && plugIds.size ()) { family = 16; pluginId = t.instrumentPlugin.id; }
			addChild (fam);
			inst = new ListBox (256, y + 26, width - 272, height - y - 80, pickInst, pickInst);
			addChild (inst);
			fam->setSel (family);
			fillInst ();
		}
	}
	void fillInst ()
	{
		inst->clear ();
		if (family == 16)
		{
			int sel = -1;
			for (int i = 0; i < plugIds.size (); i++) { inst->add (plugNames[i]); if (plugIds[i] == pluginId) sel = i; }
			inst->setSel (sel); inst->invalidate (true);
			return;
		}
		for (int i = 0; i < 8; i++) { char b[64]; snprintf (b, sizeof b, "%3d  %s", family * 8 + i + 1, g_names.gm[family * 8 + i]); inst->add (b); }
		inst->setSel (program / 8 == family ? program % 8 : -1);
		inst->invalidate (true);
	}
	void audition ()
	{
		Riff r; r.spq = SPQ;
		if (drum)
		{
			r.lengthSlices = 4 * SPQ;
			static const int kick = 36 - 12, snare = 38 - 12, hat = 42 - 12;
			for (int b = 0; b < 8; b++) r.notes.push (RiffNote (hat, b * 12, 6));
			r.notes.push (RiffNote (kick, 0, 6)); r.notes.push (RiffNote (snare, 24, 6)); r.notes.push (RiffNote (kick, 48, 6)); r.notes.push (RiffNote (kick, 60, 6)); r.notes.push (RiffNote (snare, 72, 6));
			int prog = kit >= 0 && kit < g_kitPrograms.size () ? g_kitPrograms[kit] : 0;
			g_audio.preview (compileRiffPreview (g_doc.p, r, prog, true, SOUND_RATE));
		}
		else
		{
			r.lengthSlices = 3 * SPQ;
			static const int n[4] = { 48, 52, 55, 60 };		// C4 E4 G4 C5 (0 = C0)
			for (int i = 0; i < 4; i++) r.notes.push (RiffNote (n[i], i * 12, 12));
			r.notes.push (RiffNote (48, 48, 24)); r.notes.push (RiffNote (52, 48, 24)); r.notes.push (RiffNote (55, 48, 24));
			g_audio.preview (compileRiffPreview (g_doc.p, r, program, false, SOUND_RATE));
		}
	}
	static void pickFam (Widget &w)
	{
		SoundDialog *d = (SoundDialog *) w.parent;
		d->family = iclamp (((ListBox &) w).sel, 0, d->plugIds.size () ? 16 : 15);
		d->fillInst ();
	}
	static void pickInst (Widget &w)
	{
		SoundDialog *d = (SoundDialog *) w.parent;
		int s = ((ListBox &) w).sel;
		if (s < 0) return;
		if (d->drum) d->kit = s;
		else if (d->family == 16) { if (s < d->plugIds.size ()) d->pluginId = d->plugIds[s]; return; }
		else { d->program = d->family * 8 + s; d->pluginId = ""; }
		d->audition ();
	}
};

void (*SoundDialog::s_plugins) (Vec<Str> &, Vec<Str> &) = 0;

// ---- the song: key, meter, tempo, feel ------------------------------------------------------------------------------------------
static const char *const s_tonics[17] = { "C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B" };
static const int s_tonicLetter[17] = { 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6 };
static const int s_tonicAcc[17] = { 0, 1, -1, 0, 1, -1, 0, 0, 1, -1, 0, 1, -1, 0, 1, -1, 0 };
static inline int tonicIndex (const Key &k) { for (int i = 0; i < 17; i++) if (s_tonicLetter[i] == k.tonicLetter && s_tonicAcc[i] == k.accidental) return i; return 0; }
static inline bool modeIsMinor (int m) { return modeScale (m)[2] == 3; }

class SongDialog : public KDialog
{
public:
	Dropdown *tonic, *mode, *den, *how;
	NumericUpDown *num, *bpm, *swing, *human, *minBars;
	SongDialog () : KDialog (460, 400, "The song")
	{
		const Project &p = g_doc.p;
		int y = top0 (), x = 150, w = 280;
		lab (16, y + 1, 130, "Key"); tonic = new Dropdown (x, y, 90, 24, s_tonics, 17, tonicIndex (p.key), 0); addChild (tonic);
		mode = new Dropdown (x + 100, y, w - 100, 24, g_modeNames, MODE_COUNT, effectiveMode (p.key), 0); addChild (mode);
		y += 32;
		static const char *const hows[2] = { "Transpose the song", "Chords follow their degrees" };
		lab (16, y + 1, 130, "A new key"); how = new Dropdown (x, y, w, 24, hows, 2, 0, 0); addChild (how);
		y += 40;
		lab (16, y + 1, 130, "Meter"); num = new NumericUpDown (x, y, 80, 24, 1, 32, p.timeSigNum, 1, 0); addChild (num);
		static const char *const dens[4] = { "/ 2", "/ 4", "/ 8", "/ 16" };
		int di = p.timeSigDen == 2 ? 0 : p.timeSigDen == 8 ? 2 : p.timeSigDen == 16 ? 3 : 1;
		den = new Dropdown (x + 90, y, 80, 24, dens, 4, di, 0); addChild (den);
		y += 32;
		lab (16, y + 1, 130, "Tempo (bpm)"); bpm = new NumericUpDown (x, y, 100, 24, 20, 400, iround (p.mainBpm ()), 1, 0); addChild (bpm);
		y += 32;
		lab (16, y + 1, 130, "Swing (%)"); swing = new NumericUpDown (x, y, 100, 24, 50, 75, iround (p.swingPercent > 0 ? p.swingPercent : 50), 1, 0); addChild (swing);
		y += 32;
		lab (16, y + 1, 130, "Humanize (%)"); human = new NumericUpDown (x, y, 100, 24, 0, 100, p.humanizePercent, 1, 0); addChild (human);
		y += 32;
		lab (16, y + 1, 130, "Length (bars)"); minBars = new NumericUpDown (x, y, 100, 24, 1, 999, imax (1, iround (p.minBeats / imax (1, p.barBeats ()))), 1, 0); addChild (minBars);
	}
	// what was chosen, applied to the song (the caller checkpoints first)
	void apply ()
	{
		Project &p = g_doc.p;
		Key nk = p.key;
		int ti = tonic->sel, m = mode->sel;
		nk.tonicLetter = s_tonicLetter[ti]; nk.accidental = s_tonicAcc[ti]; nk.mode = modeIsMinor (m) ? 1 : 0; nk.fullMode = m;
		if (nk.tonicLetter != p.key.tonicLetter || nk.accidental != p.key.accidental || effectiveMode (nk) != effectiveMode (p.key))
		{
			if (how->sel == 0) transposeProject (p, nk, 0, m);
			else { Key old = p.key; p.key = nk; resolveChordDegrees (p, old); }
		}
		static const int dens[4] = { 2, 4, 8, 16 };
		p.timeSigNum = num->value; p.timeSigDen = dens[iclamp (den->sel, 0, 3)];
		if (!p.tempo.size ()) { TempoChange t; t.beat = 0; t.bpm = 120; p.tempo.push (t); }
		p.tempo[0].bpm = bpm->value;
		p.swingPercent = swing->value <= 50 ? 0 : swing->value;
		p.humanizePercent = human->value;
		p.minBeats = minBars->value * imax (1, p.barBeats ());
		int ci = p.chordTrackIndex ();
		if (ci >= 0) revoiceTrack (p.tracks[ci]);
	}
};

// ---- a cadence on the chord track --------------------------------------------------------------------------------------------------
class CadenceDialog : public KDialog
{
public:
	Dropdown *start, *style;
	NumericUpDown *bars, *cpm;
	CadenceDialog (int startDeg) : KDialog (460, 250, "Cadence")
	{
		int y = top0 (), x = 150;
		static const char *const degs[7] = { "I (tonic)", "ii", "iii", "IV", "V", "vi", "vii" };
		lab (16, y + 1, 130, "Style"); style = new Dropdown (x, y, 290, 24, g_cadenceStyles, CADENCE_STYLE_COUNT, 0, 0); addChild (style);
		y += 32;
		lab (16, y + 1, 130, "From degree"); start = new Dropdown (x, y, 140, 24, degs, 7, iclamp (startDeg, 0, 6), 0); addChild (start);
		y += 32;
		lab (16, y + 1, 130, "Bars"); bars = new NumericUpDown (x, y, 90, 24, 1, 64, 4, 1, 0); addChild (bars);
		y += 32;
		lab (16, y + 1, 130, "Chords a bar"); cpm = new NumericUpDown (x, y, 90, 24, 1, 8, 1, 1, 0); addChild (cpm);
	}
};

// the chord track's "Cadence...": chords appended, voice-led -> the first one selected
static void cadenceOnChordTrack ()
{
	Project &p = g_doc.p;
	if (p.chordTrackIndex () < 0) return;
	CadenceDialog *d = new CadenceDialog (lastChordDegree (p));
	if (d->run () == 1)
	{
		g_doc.checkpoint ();
		int first = insertCadence (g_doc, d->start->sel, d->style->sel, d->bars->value, d->cpm->value, (int) kapi_get_ticks ());
		g_doc.changed ();
		if (first >= 0) { g_doc.sel.track = p.chordTrackIndex (); g_doc.sel.item = first; }
		if (g_onEdited) g_onEdited ();
		g_rebuildEditor = true;
	}
	delete d;
}

// ---- new blocks ---------------------------------------------------------------------------------------------------------------------
// a module of a kind, set for the song (its bar, its key): what a lane's menu and the browser insert
static Module *newModule (int kind, int t)
{
	Project &p = g_doc.p;
	int bb = imax (1, p.barBeats ());
	Module *m = 0;
	switch (kind)
	{
	case M_PLAYRIFF: { char nm[48]; snprintf (nm, sizeof nm, "%s riff", p.tracks[t].name.c ()); m = g_doc.newRiff (nm, bb * 2); } break;
	case M_PATTERN:
	{
		// the next chord the co-pilot suggests after the last one, else the tonic
		int prev[3], bar; int ci = p.chordTrackIndex ();
		int n = ci >= 0 && p.tracks[ci].items.size () ? chordContext (p, ci, p.tracks[ci].items.size () - 1, prev, &bar) : 0;
		PatternModule *pg = newChordLike (0, bb);
		if (n && prev[n - 1] >= 0) { Vec<Suggestion> s = suggestNext (prev, n, bar, 4, MOOD_AUTO, p.key); if (s.size ()) applySuggestion (*pg, p.key, true, s[0]); }
		else { pg->degree = 0; applyDiatonic (*pg, p.key); }
		m = pg;
	} break;
	case M_ARTICULATION:
	{
		ArticulationModule *a = new ArticulationModule;
		a->beats = bb; a->lengthBeats = bb * 4;
		// continue the track's previous accompaniment (its style, its grid)
		const Track &tr = p.tracks[t];
		for (int i = tr.items.size () - 1; i >= 0; i--)
			if (tr.items[i].module && tr.items[i].module->kind == M_ARTICULATION)
			{
				ArticulationModule *prev = (ArticulationModule *) tr.items[i].module;
				*a = *prev; a->lengthBeats = articulationTotalBeats (*prev);
				break;
			}
		m = a;
	} break;
	case M_DRUMKIT: { DrumModule *d = new DrumModule; d->beatsPerBar = bb; d->repeats = 4; m = d; } break;
	case M_MELODICLINE:
	{
		MelodicLineModule *l = new MelodicLineModule;
		l->beatsPerBar = bb * 2;
		// a default rhythm: quarters, a longer note ending each bar
		Vec<RiffNote> n;
		for (int b = 0; b < bb * 2; b++) if (b % bb != bb - 1 || b == 0) n.push (RiffNote (0, b * 4, b % bb == bb - 2 ? 8 : 4));
		l->rhythm.setNotes (n, 4, bb * 2 * 4);
		m = l;
	} break;
	case M_POLYDRUM:
	{
		PolyDrumModule *pd = new PolyDrumModule; pd->beats = bb; pd->beatsPerBar = bb; pd->repeats = 4;
		EuclidLayer a; a.lane = 0; a.hits = 3; a.steps = 8; pd->layers.push (a);
		EuclidLayer b; b.lane = 2; b.hits = 5; b.steps = 12; pd->layers.push (b);
		EuclidLayer c; c.lane = 6; c.hits = 2; c.steps = 5; pd->layers.push (c);
		m = pd;
	} break;
	case M_MELODICPOLY:
	{
		MelodicPolyModule *mp = new MelodicPolyModule; mp->beats = bb; mp->beatsPerBar = bb; mp->repeats = 4;
		EuclidVoice a; a.voice = 0; a.hits = 3; a.steps = 8; mp->layers.push (a);
		EuclidVoice b; b.voice = 1; b.hits = 5; b.steps = 16; b.octave = 1; mp->layers.push (b);
		m = mp;
	} break;
	case M_POLYCHORD:
	{
		PolyChordModule *pc = new PolyChordModule; pc->cycleBeats = bb; pc->beats = bb * 4;
		static const int degs[4] = { 0, 5, 3, 4 };
		for (int i = 0; i < 4; i++)
		{
			PolyChordItem c; c.degree = degs[i]; c.beats = bb;
			RootQ q = diatonicChord (p.key, c.degree); c.root = q.root; c.quality = q.quality;
			pc->chords.push (c);
		}
		EuclidChordLayer a; a.hits = 3; a.steps = 8; a.toneIndex = 0; pc->layers.push (a);
		EuclidChordLayer b; b.hits = 5; b.steps = 8; b.toneIndex = 1; pc->layers.push (b);
		EuclidChordLayer c; c.hits = 4; c.steps = 12; c.toneIndex = 2; pc->layers.push (c);
		m = pc;
	} break;
	case M_CADENCE: { CadenceModule *cm = new CadenceModule; regenCadence (p, *cm, (int) kapi_get_ticks ()); m = cm; } break;
	}
	if (m && m->id.empty ()) m->id = newId ();
	return m;
}
// the kind a double-click makes on a track
static int defaultKind (int t)
{
	const Track &tr = g_doc.p.tracks[t];
	if (tr.type == TRACK_CHORD) return M_PATTERN;
	if (tr.type == TRACK_DRUM) return M_DRUMKIT;
	for (int i = tr.items.size () - 1; i >= 0; i--) if (tr.items[i].module && tr.items[i].module->kind != M_GENERATOR) return tr.items[i].module->kind;
	return M_PLAYRIFF;
}

// a block put on track t at `beat`, selected
static int insertBlock (ArrangeView &v, int t, int kind, double beat)
{
	g_doc.checkpoint ();
	Module *m = newModule (kind, t);
	if (!m) return -1;
	int i = beat < 0 ? g_doc.append (t, m) : g_doc.place (t, m, beat);
	if (g_doc.p.tracks[t].type == TRACK_CHORD) revoiceTrack (g_doc.p.tracks[t]);
	g_doc.changed ();
	v.select (t, i);
	if (g_onEdited) g_onEdited ();
	return i;
}

void ArrangeView::insertDefault (int t, double beat) { insertBlock (*this, t, defaultKind (t), beat); }

void ArrangeView::addMarker (double beat)
{
	char name[64] = "Section";
	Project &p = g_doc.p;
	int at = -1;
	for (int i = 0; i < p.markers.size (); i++) if (fabs (p.markers[i].beat - beat) < 0.01) { at = i; snprintf (name, sizeof name, "%s", p.markers[i].name.c ()); }
	if (!promptText (at >= 0 ? "Section" : "New section", "Its name (empty: removed):", name, sizeof name)) return;
	g_doc.checkpoint ();
	if (at >= 0) { if (!name[0]) p.markers.removeAt (at); else p.markers[at].name = name; }
	else if (name[0]) { Marker m; m.beat = beat; m.name = name; int k = 0; while (k < p.markers.size () && p.markers[k].beat < beat) k++; p.markers.insert (k, m); }
	g_doc.changed ();
	invalidate (true);
	if (onEdited) onEdited ();
}

void ArrangeView::renameTrack (int t)
{
	char name[64]; snprintf (name, sizeof name, "%s", g_doc.p.tracks[t].name.c ());
	if (!promptText ("Track", "The track's name:", name, sizeof name) || !name[0]) return;
	g_doc.checkpoint ();
	g_doc.p.tracks[t].name = name;
	g_doc.changed ();
	invalidate (true);
	if (onEdited) onEdited ();
}

void ArrangeView::chooseSound (int t)
{
	Track &tr = g_doc.p.tracks[t];
	if (tr.type == TRACK_CHORD) { if (ChordEditor::s_cadenceDialog) ChordEditor::s_cadenceDialog (); return; }
	SoundDialog *d = new SoundDialog (tr);
	int r = d->run ();
	g_audio.stopPreview ();
	if (r == 1)
	{
		g_doc.checkpoint ();
		if (d->drum) tr.drumKit = d->kit;
		else { tr.instrument = d->program; tr.instrumentPlugin.id = d->pluginId; if (d->pluginId.empty ()) tr.instrumentPlugin.state = ""; }
		g_doc.changed ();
		invalidate (true);
		if (onEdited) onEdited ();
	}
	delete d;
}

// the header's menu
enum { HM_RENAME = 1, HM_SOUND, HM_UP, HM_DOWN, HM_COLLAPSE, HM_DUP, HM_DELETE, HM_ADD_INST, HM_ADD_DRUM, HM_CADENCE, HM_CLEAR };
void ArrangeView::headerMenu (int t, int mx, int my)
{
	Track &tr = g_doc.p.tracks[t];
	PopupMenu *m = new PopupMenu (absX (this) + mx, absY (this) + my);
	bool chord = tr.type == TRACK_CHORD;
	m->add ("Rename...", HM_RENAME, !chord);
	if (!chord) m->add (tr.type == TRACK_DRUM ? "Drum kit..." : "Instrument...", HM_SOUND);
	else m->add ("Cadence...", HM_CADENCE);
	m->add (tr.collapsed ? "Expand" : "Collapse", HM_COLLAPSE, !chord);
	m->separator ();
	m->add ("Move up", HM_UP, !chord && t > 0);
	m->add ("Move down", HM_DOWN, !chord && t + 1 < g_doc.p.tracks.size () && g_doc.p.tracks[t + 1].type != TRACK_CHORD);
	m->add ("Duplicate the track", HM_DUP, !chord);
	m->add ("Remove every block", HM_CLEAR, tr.items.size () > 0);
	m->add ("Delete the track", HM_DELETE, !chord);
	m->separator ();
	m->add ("Add an instrument track", HM_ADD_INST);
	m->add ("Add a drum track", HM_ADD_DRUM);
	int id = m->run ();
	delete m;
	switch (id)
	{
	case HM_RENAME: renameTrack (t); return;
	case HM_SOUND: chooseSound (t); return;
	case HM_CADENCE: if (ChordEditor::s_cadenceDialog) ChordEditor::s_cadenceDialog (); return;
	case -1: case 0: return;
	}
	g_doc.checkpoint ();
	switch (id)
	{
	case HM_COLLAPSE: tr.collapsed = !tr.collapsed; break;
	case HM_UP: g_doc.moveTrack (t, -1); break;
	case HM_DOWN: g_doc.moveTrack (t, 1); break;
	case HM_DUP:
	{
		Track c = tr;
		char nm[80]; snprintf (nm, sizeof nm, "%s copy", tr.name.c ()); c.name = nm;
		for (int i = 0; i < c.items.size (); i++)
			if (c.items[i].module)
			{
				c.items[i].module->id = newId ();
				if (c.items[i].module->kind == M_PLAYRIFF)
				{
					PlayRiffModule *pr = (PlayRiffModule *) c.items[i].module;
					const Riff *r = g_doc.p.riffById (pr->riffId);
					if (r) { Riff nr = *r; nr.id = newId (); pr->riffId = nr.id; g_doc.p.riffs.push (nr); }
				}
			}
		g_doc.p.tracks.insert (t + 1, c);
	} break;
	case HM_CLEAR: tr.items.clear (); g_doc.sel = Selection (); break;
	case HM_DELETE: g_doc.removeTrack (t); break;
	case HM_ADD_INST: { char nm[32]; snprintf (nm, sizeof nm, "Track %d", g_doc.p.tracks.size ()); g_doc.selTrack = g_doc.addTrack (nm, TRACK_INSTRUMENT, 0); } break;
	case HM_ADD_DRUM: g_doc.selTrack = g_doc.addTrack ("Drums", TRACK_DRUM, 0); break;
	}
	g_doc.changed ();
	if (onSelect) onSelect ();
	invalidate (true);
	if (onEdited) onEdited ();
}

// a lane's menu: what to put there, what to do with the block under the pointer
enum { LM_KIND = 100, LM_DUP = 1, LM_DELETE, LM_SUGGEST, LM_CADENCE, LM_CHAIN, LM_TO_RIFF };
void ArrangeView::laneMenu (int t, int i, double beat, int mx, int my)
{
	Track &tr = g_doc.p.tracks[t];
	double at = snapBeat (beat);
	if (i >= 0) select (t, i);
	PopupMenu *m = new PopupMenu (absX (this) + mx, absY (this) + my);
	if (tr.type == TRACK_CHORD)
	{
		m->add ("A chord here", LM_KIND + M_PATTERN);
		m->add ("Cadence...", LM_CADENCE);
		if (i >= 0) m->add ("Chain 4 bars after it", LM_CHAIN);
		m->add ("Poly chords here", LM_KIND + M_POLYCHORD);
	}
	else if (tr.type == TRACK_DRUM)
	{
		m->add ("Drums here", LM_KIND + M_DRUMKIT);
		m->add ("Polyrhythm here", LM_KIND + M_POLYDRUM);
	}
	else
	{
		m->add ("Riff here", LM_KIND + M_PLAYRIFF);
		m->add ("Accompaniment here", LM_KIND + M_ARTICULATION);
		m->add ("Melodic line here", LM_KIND + M_MELODICLINE);
		m->add ("Melodic rings here", LM_KIND + M_MELODICPOLY);
		m->add ("Poly chords here", LM_KIND + M_POLYCHORD);
	}
	if (i >= 0)
	{
		m->separator ();
		const Module *mod = tr.items[i].module;
		if (mod && mod->kind != M_PLAYRIFF && tr.type != TRACK_CHORD) m->add ("Freeze into a riff", LM_TO_RIFF);
		m->add ("Duplicate", LM_DUP, true, "^D");
		m->add ("Delete", LM_DELETE, true, "Del");
	}
	int id = m->run ();
	delete m;
	if (id >= LM_KIND) { insertBlock (*this, t, id - LM_KIND, at); return; }
	switch (id)
	{
	case LM_DUP: duplicateSelected (); break;
	case LM_DELETE: deleteSelected (); break;
	case LM_CADENCE: if (ChordEditor::s_cadenceDialog) ChordEditor::s_cadenceDialog (); break;
	case LM_CHAIN:
	{
		g_doc.checkpoint ();
		int last = chainProgression (g_doc, t, i, 4);
		g_doc.changed ();
		select (t, last);
		if (onEdited) onEdited ();
	} break;
	case LM_TO_RIFF:				// the block's notes as an ordinary riff (Koton's "freeze")
	{
		Project &p = g_doc.p;
		double s = g_doc.itemStart (t, i), len = g_doc.itemLen (t, i);
		int carry[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
		Riff cell;
		Riff r = renderModule (*tr.items[i].module, p, s, carry, &cell);
		g_doc.checkpoint ();
		char nm[80]; char lab_[64]; moduleLabel (p, *tr.items[i].module, lab_, sizeof lab_); snprintf (nm, sizeof nm, "%s (frozen)", lab_);
		PlayRiffModule *pr = g_doc.newRiff (nm, len);
		Riff *nr = p.riffById (pr->riffId);
		nr->spq = r.spq > 0 ? r.spq : SPQ; nr->lengthSlices = imax (1, iround (len * nr->spq)); nr->notes = r.notes;
		if (cell.notes.size () && cell.spq > 0)
			for (int k = 0; k < cell.notes.size (); k++)
			{
				RiffNote n = cell.notes[k]; n.start = n.start * nr->spq / cell.spq; n.length = imax (1, n.length * nr->spq / cell.spq);
				nr->notes.push (n);
			}
		pr->id = newId ();
		delete tr.items[i].module;
		tr.items[i].module = pr;
		g_doc.changed ();
		select (t, i);
		if (onEdited) onEdited ();
	} break;
	}
}

} // namespace kui

#endif
