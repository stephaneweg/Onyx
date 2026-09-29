//
// widgets -- a showcase of the WPF-style wtk controls (P5): RadioButton in a GroupBox,
// ToggleSwitch, NumericUpDown, ListBox, TreeView, Calendar, DatePicker, ImageBox, the
// colour dialog and tooltips (hover a control ~0.6 s); and the studio controls: a ToolBar of
// ToolButtons (a transport), an LcdDisplay (the time), a SegmentedControl, Knobs and VuMeters
// (a made-up signal while "playing": the gain and the pan knobs act on it). The status line
// reports every event.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "wtk/toolbar.h"
#include "img/imgload.hpp"

using namespace wtk;

#define W	660
#define H	548

static Label *g_status;
static Label *g_swatch;
static unsigned g_color = 0x004080E0;

// A sunken well of the theme (a field's look) round a child: the ImageBox.
class Well : public Widget
{
public:
	Well (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override { canvas.clear (parent ? parent->bgColor () : C_BG); wk_sunken (canvas, 0, 0, width, height, 5, C_FIELD); }
};

// A picture into the ImageBox, an icon's magenta key (0xFF00FF) made see-through (the box shows
// its background there).
static void show_icon (ImageBox *ib, const char *path)
{
	ImgFrames im;
	if (!img_load (path, &im)) return;
	unsigned *p = im.px[0];
	for (int i = 0; i < im.w * im.h; i++) if ((p[i] & 0x00FFFFFF) == WK_TRANSPARENT_KEY) p[i] = 0;
	ib->setPixels (p, im.w, im.h, true);
	img_free (&im);
}

static void say (const char *a, const char *b = "")
{
	static char t[128]; int p = 0;
	for (int i = 0; a[i] && p < 126; i++) t[p++] = a[i];
	for (int i = 0; b[i] && p < 126; i++) t[p++] = b[i];
	t[p] = '\0';
	g_status->setText (t);
}
static void num (char *b, int v) { int p = 0; if (v < 0) { b[p++] = '-'; v = -v; } char t[12]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; } while (n) b[p++] = t[--n]; b[p] = '\0'; }

static void on_radio (Widget &w)  { say ("Size: ", ((RadioButton &) w).text); }
static void on_toggle (Widget &w) { ToggleSwitch &t = (ToggleSwitch &) w; say (t.text, t.on ? " on" : " off"); }
static void on_num (Widget &w)    { char b[16]; num (b, ((NumericUpDown &) w).value); say ("Quantity: ", b); }
static void on_list (Widget &w)   { ListBox &l = (ListBox &) w; say ("Selected: ", l.item (l.sel)); }
static void on_list_go (Widget &w){ ListBox &l = (ListBox &) w; say ("Activated: ", l.item (l.sel)); }
static void on_tree (Widget &w)   { TreeView &t = (TreeView &) w; say ("Node: ", t.label (t.sel)); }
static void on_tree_go (Widget &w){ TreeView &t = (TreeView &) w; say ("Opened: ", t.label (t.sel)); }
static void on_cal (Widget &w)
{
	Calendar &c = (Calendar &) w; char b[16];
	b[0] = (char) ('0' + c.day / 10); b[1] = (char) ('0' + c.day % 10); b[2] = '/';
	b[3] = (char) ('0' + c.month / 10); b[4] = (char) ('0' + c.month % 10); b[5] = '/';
	num (b + 6, c.year);
	say ("Calendar: ", b);
}
static void on_date (Widget &w)   { char b[12]; ((DatePicker &) w).format (b); say ("Date: ", b); }

// ---- the studio controls ------------------------------------------------------------------------
static ToolButton *g_play, *g_rec;
static LcdDisplay *g_lcd;
static Knob *g_gain, *g_pan;
static VuMeter *g_vu, *g_vuMono;
static unsigned g_t0, g_played;			// ticks: when play started, played before
static const char *const PARTS[] = { "Chords", "Melody", "Drums" };

static void fmt_db (int v, char *o, int cap)		// tenths of a dB -> "-6.2 dB"
{
	char b[16]; int p = 0, a = v < 0 ? -v : v;
	b[p++] = v < 0 ? '-' : '+'; num (b + p, a / 10); while (b[p]) p++;
	b[p++] = '.'; b[p++] = (char) ('0' + a % 10); b[p++] = ' '; b[p++] = 'd'; b[p++] = 'B'; b[p] = '\0';
	int i = 0; for (; b[i] && i < cap - 1; i++) o[i] = b[i]; o[i] = '\0';
}
static void fmt_pan (int v, char *o, int cap)
{
	char b[8]; if (v == 0) { b[0] = 'C'; b[1] = '\0'; } else { b[0] = v < 0 ? 'L' : 'R'; num (b + 1, v < 0 ? -v : v); }
	int i = 0; for (; b[i] && i < cap - 1; i++) o[i] = b[i]; o[i] = '\0';
}
static void fmt_pct (int v, char *o, int cap) { char b[8]; num (b, v); int p = 0; while (b[p]) p++; b[p++] = ' '; b[p++] = '%'; b[p] = '\0'; int i = 0; for (; b[i] && i < cap - 1; i++) o[i] = b[i]; o[i] = '\0'; }

static void show_time (unsigned t)			// ticks (1/100 s) -> "0:14.83", the bar and beat at 120 BPM
{
	char b[16]; unsigned s = t / 100; int p = 0;
	num (b, (int) (s / 60)); while (b[p]) p++;
	b[p++] = ':'; b[p++] = (char) ('0' + s % 60 / 10); b[p++] = (char) ('0' + s % 10);
	b[p++] = '.'; b[p++] = (char) ('0' + t % 100 / 10); b[p++] = (char) ('0' + t % 10); b[p] = '\0';
	g_lcd->setText (b);
	unsigned beat = t / 50;				// (a beat: half a second)
	char c[16]; num (c, (int) (beat / 4 + 1)); p = 0; while (c[p]) p++;
	c[p++] = '.'; c[p++] = (char) ('1' + beat % 4); c[p] = '\0';
	char d[20] = "BAR "; int q = 4; for (int i = 0; c[i]; i++) d[q++] = c[i]; d[q] = '\0';
	g_lcd->setSub (d);
}
static void on_play (Widget &)
{
	if (g_play->on) { g_t0 = kapi_get_ticks (); say ("Transport: playing"); }
	else { g_played += kapi_get_ticks () - g_t0; say ("Transport: paused"); }
}
static void on_stop (Widget &) { g_play->setOn (false); g_rec->setOn (false); g_played = 0; show_time (0); say ("Transport: stopped"); }
static void on_rec (Widget &w)  { say ("Record: ", ((ToolButton &) w).on ? "armed" : "off"); }
static void on_loop (Widget &w) { say ("Loop: ", ((ToolButton &) w).on ? "on" : "off"); }
static void on_part (Widget &w) { SegmentedControl &s = (SegmentedControl &) w; say ("Part: ", s.label (s.selected)); }
static void on_knob (Widget &w) { Knob &k = (Knob &) w; char b[32]; k.valueText (b, sizeof b); char t[48] = ""; int p = 0; for (int i = 0; k.label ()[i]; i++) t[p++] = k.label ()[i]; t[p++] = ':'; t[p++] = ' '; t[p] = '\0'; say (t, b); }

// The window's loop: the time and a made-up signal while playing (the meters fall when it stops).
class ShowRoot : public Root
{
public:
	ShowRoot () : Root (W, H, "Widget Showcase"), m_seed (12345) {}
	void onTick () override
	{
		if (!g_play || !g_play->on) { if (g_vu) { g_vu->setQ16 (0, 0); g_vuMono->setQ16 (0, 0); } return; }
		unsigned t = g_played + kapi_get_ticks () - g_t0;
		show_time (t);
		m_seed = m_seed * 1103515245u + 12345u;		// (a beat's envelope, a little noise)
		int env = 65536 - (int) (t % 50) * 1100, noise = (int) ((m_seed >> 16) & 0x3FFF);
		if (env < 9000) env = 9000;
		long long lvl = (long long) (env + noise) * 3 / 5;
		int g = g_gain->value;				// tenths of a dB: x 10^(g/200), roughly (x2 every 6 dB)
		while (g <= -60) { lvl /= 2; g += 60; }
		while (g >= 60) { lvl *= 2; g -= 60; }
		lvl = lvl * (600 + g * 10) / 600;
		int pan = g_pan->value;				// -50 .. 50
		int l = (int) (lvl * (pan > 0 ? 50 - pan : 50) / 50), r = (int) (lvl * (pan < 0 ? 50 + pan : 50) / 50);
		g_vu->setQ16 (l, r);
		g_vuMono->setQ16 ((l + r) / 2, 0);
	}
private:
	unsigned m_seed;
};
static void on_color (Widget &)
{
	if (!wk_color_dialog (&g_color, "Pick a colour")) { say ("Colour: cancelled"); return; }
	g_swatch->bg = g_color; g_swatch->invalidate (true);
	static const char *HX = "0123456789ABCDEF";
	char h[8] = { '#', HX[(g_color >> 20) & 15], HX[(g_color >> 16) & 15], HX[(g_color >> 12) & 15],
		      HX[(g_color >> 8) & 15], HX[(g_color >> 4) & 15], HX[g_color & 15], 0 };
	say ("Colour: ", h);
}

int main (void)
{
	ShowRoot root;
	if (root.canvas.px == 0) return 1;
	int y0 = 0, mo = 0, d0 = 0;
	kapi_get_datetime (&y0, &mo, &d0, 0, 0, 0);
	if (y0 < 1970) { y0 = 2026; mo = 1; d0 = 1; }

	// Left column: choices.
	GroupBox *gb = new GroupBox (10, 8, 200, 104, "Size");
	root.addChild (gb);
	RadioButton *r1 = new RadioButton (12, 22, 170, 22, "Small",  1, false, on_radio);
	RadioButton *r2 = new RadioButton (12, 46, 170, 22, "Medium", 1, true,  on_radio);
	RadioButton *r3 = new RadioButton (12, 70, 170, 22, "Large",  1, false, on_radio);
	gb->addChild (r1); gb->addChild (r2); gb->addChild (r3);
	r1->tip = "RadioButton: one choice per group";

	ToggleSwitch *t1 = new ToggleSwitch (10, 122, 200, 24, "Wi-Fi", true, on_toggle);
	ToggleSwitch *t2 = new ToggleSwitch (10, 150, 200, 24, "Dark mode", false, on_toggle);
	t1->tip = "ToggleSwitch: click or Space";
	root.addChild (t1); root.addChild (t2);

	root.addChild (new Label (10, 186, 80, 20, "Quantity"));
	NumericUpDown *nu = new NumericUpDown (100, 182, 110, 26, 0, 99, 3, 1, on_num);
	nu->tip = "NumericUpDown: arrows, wheel, or type";
	root.addChild (nu);

	ListBox *lb = new ListBox (10, 218, 200, 176, on_list, on_list_go);
	static const char *fruits[] = { "Apple", "Apricot", "Banana", "Blueberry", "Cherry", "Grape",
		"Kiwi", "Lemon", "Mango", "Melon", "Orange", "Peach", "Pear", "Plum", "Raspberry", "Strawberry" };
	for (int i = 0; i < 16; i++) lb->add (fruits[i]);
	lb->setSel (2);
	lb->tip = "ListBox: double-click or Enter activates";
	root.addChild (lb);

	// Middle column: tree + image.
	TreeView *tv = new TreeView (220, 8, 220, 240, on_tree, on_tree_go);
	int sd = tv->add (-1, "SD:");
	int apps = tv->add (sd, "apps");
	tv->add (apps, "fileviewer.app"); tv->add (apps, "imageview.app"); tv->add (apps, "shelf.app");
	int etc = tv->add (sd, "etc");
	tv->add (etc, "autostart"); tv->add (etc, "fileassoc.ini"); tv->add (etc, "shelf.ini");
	int bin = tv->add (sd, "bin");
	tv->add (bin, "ls"); tv->add (bin, "cat"); tv->add (bin, "ping");
	tv->expand (sd, true); tv->expand (etc, true);
	tv->tip = "TreeView: [+]/[-], double-click, Left/Right";
	root.addChild (tv);

	Well *well = new Well (220, 258, 220, 136);
	ImageBox *ib = new ImageBox (3, 4, 214, 129, IMG_FIT, C_FIELD);
	ib->grow = true;
	show_icon (ib, "SD:/apps/imageview.app/icon.bmp");
	ib->tip = "ImageBox: BMP GIF PNG JPEG PCX WebP";
	well->addChild (ib);
	root.addChild (well);

	// Right column: dates + colour.
	Calendar *cal = new Calendar (452, 8, y0, mo, d0, on_cal);
	cal->tip = "Calendar: < > or Page Up/Down, click a day";
	root.addChild (cal);
	root.addChild (new Label (452, 182, 198, 20, "Date"));
	DatePicker *dp = new DatePicker (452, 204, CAL_W, 26, y0, mo, d0, on_date);
	dp->tip = "DatePicker: click to open a calendar";
	root.addChild (dp);
	Button *cb = new Button (452, 246, 120, 28, "Colour...", on_color);
	cb->tip = "Opens the colour dialog";
	root.addChild (cb);
	g_swatch = new Label (582, 246, 68, 28, "", C_TEXT, g_color);
	root.addChild (g_swatch);

	// Bottom: the studio controls.
	GroupBox *gs = new GroupBox (10, 404, W - 20, 108, "Studio");
	root.addChild (gs);
	ToolBar *tb = new ToolBar (8, 22, 186, 34);
	gs->addChild (tb);
	g_play = (new ToolButton (34, 28, "ToolButton: a toggle, filled when on (play / pause)", on_play))->setGlyph (WKT_PLAY)->setToggle (true, true);
	g_play->filled = true; g_play->raised = true;
	ToolButton *stop = (new ToolButton (30, 28, "Stop", on_stop))->setGlyph (WKT_STOP); stop->raised = true;
	g_rec = (new ToolButton (30, 28, "Record: a toggle, its icon red", on_rec))->setGlyph (WKT_RECORD)->setToggle (true); g_rec->raised = true;
	g_rec->iconColor = 0x00E0483C;
	ToolButton *loop = (new ToolButton (30, 28, "Loop: a toggle, tinted when on", on_loop))->setGlyph (WKT_LOOP)->setToggle (true, true);
	loop->raised = true;
	tb->add (g_play, 0); tb->add (stop, 3); tb->add (g_rec, 3); tb->add (loop, 3);
	g_lcd = new LcdDisplay (200, 22, 222, 34, "0:00.00", "TIME");
	g_lcd->tip = "LcdDisplay: large digits, a caption, a second line";
	gs->addChild (g_lcd);
	g_t0 = kapi_get_ticks (); show_time (0);
	SegmentedControl *sc = new SegmentedControl (8, 66, 414, 26, PARTS, 3, 0, on_part);
	sc->tip = "SegmentedControl: one of a few, Left / Right";
	gs->addChild (sc);
	g_gain = new Knob (432, 16, 56, 84, -600, 60, -30, on_knob);
	g_gain->setLabel ("Gain"); g_gain->showValue = true; g_gain->format = fmt_db; g_gain->setDefault (0);
	g_gain->tip = "Knob: drag up / down (Shift: fine), the wheel, a double click resets";
	g_pan = new Knob (490, 16, 48, 84, -50, 50, 0, on_knob);
	g_pan->setLabel ("Pan"); g_pan->showValue = true; g_pan->bipolar = true; g_pan->format = fmt_pan; g_pan->setDefault (0);
	Knob *mix = new Knob (540, 16, 48, 84, 0, 100, 70, on_knob);
	mix->setLabel ("Mix"); mix->showValue = true; mix->format = fmt_pct;
	gs->addChild (g_gain); gs->addChild (g_pan); gs->addChild (mix);
	g_vu = new VuMeter (596, 18, 16, 80);
	g_vu->tip = "VuMeter: the peaks held, a click clears the clip lights";
	g_vuMono = new VuMeter (618, 18, 10, 80, true, false); g_vuMono->segPx = 0;
	gs->addChild (g_vu); gs->addChild (g_vuMono);

	g_status = new Label (10, H - 32, W - 20, 22, "Hover a control for its tooltip.", wk_mix (C_ACCENT, C_TEXT, 140));
	root.addChild (g_status);
	root.run ();
	return 0;
}
