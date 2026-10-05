//
// studio -- (the desktop simulator only) the studio widgets of uikit (Knob, VuMeter, SegmentedControl,
// ToolBar + ToolButton, LcdDisplay) among the classic controls, for a look at them -- in the
// theme of the card, or a dark studio palette (SIM_DARK=1) -- and at uikit's text through a FreeType
// face (built with -DWITH_FT: ft/uikitface.h, DejaVu Sans 13 px; SIM_FT="Family,px" another). Built
// and run by tools/tests/desktop_sim/studio.sh.
//
#include "kapi.h"
#include "applib.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#ifdef WITH_FT
#include "ft/uikitface.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

using namespace uikit;

static const char *const ART[] = { "Articulation", "Melodic cell", "Voicing" };
static const char *const MEL[] = { "Melodic line (rhythm only)", "Full melody (notes)" };
static const char *const KEYS[] = { "F# minor (aeolian)", "A major", "D dorian" };
static const char *const FRUIT[] = { "Chord progression", "Cadence (30 styles)", "Suggest next chord", "Voice leading",
				     "Drum pattern", "Euclidean rhythm", "Polyrhythm (rings)" };

static void fmt_hz (int v, char *o, int cap)  { snprintf (o, cap, "%d.%d Hz", v / 10, v % 10); }
static void fmt_pct (int v, char *o, int cap) { snprintf (o, cap, "%d %%", v); }
static void fmt_pan (int v, char *o, int cap) { if (v == 0) snprintf (o, cap, "C"); else snprintf (o, cap, "%c%d", v < 0 ? 'L' : 'R', v < 0 ? -v : v); }
static void fmt_db (int v, char *o, int cap)  { snprintf (o, cap, "%s%d.%d dB", v < 0 ? "-" : "+", abs (v) / 10, abs (v) % 10); }

// The styles and a few characters beyond Latin-1 (a face draws them; the bitmap font shows bytes).
class Styles : public Widget
{
public:
	Styles (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		int x = 0; const char *s[4] = { "Regular", "Italic", "Bold", "Bold italic" };
		for (int i = 0; i < 4; i++) { uk_text (canvas, x, 0, s[i], C_TEXT, i); x += uk_tw (s[i], i) + 14; }
		uk_text (canvas, x, 0, uk_textface () ? "Caf\xC3\xA9 na\xC3\xAFve \xE2\x80\x93 \xC2\xBD \xE2\x99\xAA \xE2\x82\xAC 5" : "Cafe naive - 1/2 5",
			 uk_mix (C_BG, C_TEXT, 170));
	}
};

int main (void)
{
	const int W = 780, H = 544;
#ifdef WITH_FT
	{
		char fam[64] = "DejaVu Sans"; int px = 13;
		const char *e = getenv ("SIM_FT");
		if (e) { const char *c = strchr (e, ','); int n = c ? (int) (c - e) : (int) strlen (e); if (n > 63) n = 63; memcpy (fam, e, n); fam[n] = 0; if (c) px = atoi (c + 1); }
		if (!ft_uikit_install (fam, px)) fprintf (stderr, "studio: no TrueType font\n");
	}
#endif
	Root root (W, H, "Studio widgets");
	if (getenv ("SIM_DARK"))				// a dark studio palette (as Koton's)
	{
		UkTheme t; uk_theme_defaults (t);
		t.theme = 4; t.active = 0x003A4458; t.window = 0x00262A31; t.button = 0x00323840; t.field = 0x001C1F24;
		t.accent = 0x0036A3C0; t.menubar = 0x00262A31;
		uk_theme_set (t);
		root.setBg (C_BG);
		uk_decorate_window ();
	}

	// the transport
	ToolBar *tb = new ToolBar (0, 0, W, 42); tb->line = true; root.addChild (tb);
	int sw[3] = { WKT_SAVE, WKT_UNDO, WKT_REDO };
	for (int i = 0; i < 3; i++) { ToolButton *b = (new ToolButton (30, 28, "Save / Undo / Redo"))->setGlyph (sw[i]); b->raised = true; tb->add (b, 2); }
	tb->sep ();
	ToolButton *st = (new ToolButton (30, 28, "To the start"))->setGlyph (WKT_TO_START); st->raised = true; tb->add (st, 2);
	ToolButton *play = (new ToolButton (38, 28, "Play (Space)"))->setGlyph (WKT_PLAY)->setToggle (true, true);
	play->filled = true; play->raised = true; tb->add (play, 2);
	ToolButton *stop = (new ToolButton (30, 28, "Stop"))->setGlyph (WKT_STOP); stop->raised = true; tb->add (stop, 2);
	ToolButton *rec = (new ToolButton (30, 28, "Record"))->setGlyph (WKT_RECORD); rec->raised = true; rec->iconColor = 0x00E0483C; tb->add (rec, 2);
	ToolButton *loop = (new ToolButton (30, 28, "Loop"))->setGlyph (WKT_LOOP)->setToggle (true, true); loop->raised = true; tb->add (loop, 2);
	ToolButton *met = (new ToolButton (30, 28, "Metronome"))->setGlyph (WKT_METRONOME)->setToggle (true); met->raised = true; tb->add (met, 2);
	tb->sep ();
	LcdDisplay *lcd = new LcdDisplay (0, 0, 180, 34, "6.3.2", "BAR.BEAT.16"); lcd->setSub ("0:14.83");
#ifdef WITH_FT
	FtTextFace *big = new FtTextFace; if (big->open ("DejaVu Sans", 24)) lcd->face = big;
	FtTextFace *small = new FtTextFace; if (small->open ("DejaVu Sans", 10)) lcd->smallFace = small;
#endif
	tb->add (lcd, 4);
	tb->sep ();
	ToolButton *ai = (new ToolButton (0, 28, "Compose with AI"))->setGlyph (WKT_SPARK)->setText ("Compose...")->fitWidth ();
	ai->raised = true; tb->add (ai, 2);
	VuMeter *master = new VuMeter (0, 0, 120, 16, false, true); tb->addRight (master, 10);
	master->setCdb (-620, -700);

	// the segmented controls
	SegmentedControl *sa = new SegmentedControl (12, 52, 330, 26, ART, 3, 0); root.addChild (sa);
	SegmentedControl *sb = new SegmentedControl (356, 52, 412, 26, MEL, 2, 1); sb->equalWidths = false; root.addChild (sb);

	// the knobs, the meters
	GroupBox *gk = new GroupBox (12, 88, 440, 160, "Sound chain"); root.addChild (gk);
	Knob *k1 = new Knob (10, 24, 56, 88, 1, 200, 8); k1->setLabel ("Rate"); k1->showValue = true; k1->format = fmt_hz; gk->addChild (k1);
	Knob *k2 = new Knob (76, 30, 48, 80, 0, 100, 35); k2->setLabel ("Depth"); k2->showValue = true; k2->format = fmt_pct; gk->addChild (k2);
	Knob *k3 = new Knob (134, 36, 40, 72, 0, 100, 70); k3->setLabel ("Mix"); k3->showValue = true; k3->format = fmt_pct; gk->addChild (k3);
	k3->setFocus ();
	Knob *k4 = new Knob (184, 30, 48, 80, -50, 50, -18); k4->setLabel ("Pan"); k4->showValue = true; k4->bipolar = true; k4->format = fmt_pan; gk->addChild (k4);
	Knob *k5 = new Knob (242, 36, 28, 28, 0, 100, 60); k5->tip = "a 28-px knob, no caption"; gk->addChild (k5);
	Knob *k6 = new Knob (242, 72, 28, 28, -600, 60, -120); k6->arcColor = 0x00E6AC2E; k6->bipolar = true; gk->addChild (k6);
	Knob *k7 = new Knob (280, 24, 64, 64, -600, 60, -62); k7->format = fmt_db; k7->setDefault (0); gk->addChild (k7);
	Knob *k8 = new Knob (280, 96, 64, 44, 0, 100, 25); k8->setLabel ("Off"); k8->disabled = true; gk->addChild (k8);
	VuMeter *v1 = new VuMeter (360, 24, 14, 124); gk->addChild (v1); v1->setCdb (-900, -1500);
	VuMeter *v2 = new VuMeter (384, 24, 14, 124); gk->addChild (v2); v2->setCdb (-300, 150);
	VuMeter *v3 = new VuMeter (408, 24, 20, 124, true, false); v3->segPx = 0; gk->addChild (v3); v3->setCdb (-2400, 0);
	v3->setCdb (-2400, 0);

	// the classic controls, under the face
	GroupBox *gc = new GroupBox (464, 88, 304, 160, "The piece"); root.addChild (gc);
	gc->addChild (new Label (10, 26, 60, 22, "Length"));
	NumericUpDown *nu = new NumericUpDown (74, 24, 76, 26, 1, 999, 32, 1, 0); gc->addChild (nu);
	gc->addChild (new Label (160, 26, 40, 22, "bars"));
	Dropdown *dd = new Dropdown (10, 58, 200, 26, KEYS, 3, 0, 0); gc->addChild (dd);
	Checkbox *c1 = new Checkbox (10, 92, 130, 22, "Drum track", true, 0); gc->addChild (c1);
	Checkbox *c2 = new Checkbox (150, 92, 150, 22, "Polyrhythm layer", false, 0); gc->addChild (c2);
	ToggleSwitch *ts = new ToggleSwitch (10, 120, 150, 24, "Open voicing", true, 0); gc->addChild (ts);
	RadioButton *r1 = new RadioButton (170, 120, 60, 22, "Fast", 1, true, 0); gc->addChild (r1);
	RadioButton *r2 = new RadioButton (232, 120, 64, 22, "Slow", 1, false, 0); gc->addChild (r2);
	Button *gen = new Button (220, 56, 76, 30, "Generate"); gc->addChild (gen);

	Label *ls = new Label (12, 258, 50, 22, "Style"); root.addChild (ls);
	Textbox *tx = new Textbox (64, 256, 388, 26, "Afro-jazz, Ghibli colours"); root.addChild (tx);
	Textarea *ta = new Textarea (12, 290, 440, 120, 4096); root.addChild (ta);
	ta->setContent ("A walk at dusk: a calm verse on a kora-like riff,\na chorus that opens up (IV -> V/III),\n"
			"a polyrhythm under the last 8 bars.\n\nWide glyphs: WWW MMM, narrow ones: iii lll.");
	ta->anchor = 51; ta->caret = 89;
	ListBox *lb = new ListBox (464, 256, 150, 154); root.addChild (lb);
	for (int i = 0; i < 7; i++) lb->add (FRUIT[i]);
	lb->setSel (2);
	DataGrid *dg = new DataGrid (624, 256, 144, 154); root.addChild (dg);
	dg->setColumns (2); dg->setColumn (0, "Layer", 70, GRID_LEFT); dg->setColumn (1, "Hits", 60, GRID_RIGHT);
	dg->setRows (4);
	dg->cellText = [] (DataGrid &, int r, int c, char *b, int cap) -> const char * {
		static const char *L[4] = { "E(3,8) conga", "E(5,12)", "E(7,16) clave", "Shaker" };
		if (c == 0) return L[r];
		snprintf (b, cap, "%d", 3 + 2 * r); return b; };
	dg->setSel (1);
	Styles *sty = new Styles (12, 420, 756, 20); root.addChild (sty);
	Label *st2 = new Label (12, 446, 756, 22, "Engine on core 2 \xB7 44.1 kHz \xB7 block 256 \xB7 latency 23 ms", uk_mix (C_BG, C_TEXT, 150));
	root.addChild (st2);
	Button *b1 = new Button (12, 480, 110, 30, "Cancel"); root.addChild (b1);
	Button *b2 = new Button (130, 480, 110, 30, "Place it"); root.addChild (b2);
	Button *b3 = new Button (248, 480, 110, 30, "Disabled"); b3->disabled = true; root.addChild (b3);

	if (getenv ("SIM_FOCUS_TEXT")) tx->setFocus ();
	root.run ();
	return 0;
}
