//
// widgets -- a showcase of the WPF-style wtk controls (P5): RadioButton in a GroupBox,
// ToggleSwitch, NumericUpDown, ListBox, TreeView, Calendar, DatePicker, ImageBox, the
// colour dialog and tooltips (hover a control ~0.6 s). The status line reports every
// event.
//
#include "kapi.h"
#include "wtk/wtk.h"

using namespace wtk;

#define W	660
#define H	440

static Label *g_status;
static Label *g_swatch;
static unsigned g_color = 0x004080E0;

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
	Root root (W, H, "Widget Showcase");
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

	ImageBox *ib = new ImageBox (220, 258, 220, 136, IMG_FIT, 0x00141A22);
	ib->grow = true;
	ib->load ("SD:/apps/imageview.app/icon.bmp");
	ib->tip = "ImageBox: BMP GIF PNG JPEG PCX WebP";
	root.addChild (ib);

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

	g_status = new Label (10, H - 32, W - 20, 22, "Hover a control for its tooltip.", C_ACCENT);
	root.addChild (g_status);
	root.run ();
	return 0;
}
