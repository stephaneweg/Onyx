//
// disks -- the volumes (kapi v93): the SD card's partitions, the USB sticks and disks, RAM:. Each with
// its state, file system, label, size and room; a USB stick ejected (made safe to remove), mounted
// again, or formatted (FAT32 / exFAT / FAT, a label). SD:, the system's volume, is never formatted (the
// kernel refuses it too); SD1:..SD3: only after a second question. `disks USB1:` opens on that volume.
// A device with several partitions shows them (USB1P1:, USB1P2:...): each formatted alone, or "Whole
// device" makes it one partition again (USB1:). Eject acts on the device: all its partitions.
// The list follows the sticks as they come and go.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see docs/LICENSING.md)
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"
#include "systemkit/systemkit.h"

using namespace uikit;

#define W	640
#define H	470
#define MAXV	16

static struct kapi_volume g_v[MAXV];
static int g_n = 0;
static unsigned g_sig = 0;
static char g_selName[8] = "";

static ListBox *g_list;
static Label *g_l1, *g_l2, *g_l3, *g_status;
static Progress *g_bar;
static Button *g_open, *g_eject, *g_mount, *g_format;
static Dropdown *g_fs;
static Textbox *g_label;
static Checkbox *g_whole;			// (a partition chosen) format the whole device instead

// "USB1P2" -> true: a partition of a USB device (its device's volume: "USB1")
static bool is_part (const char *n) { return n[0] == 'U' && n[1] == 'S' && n[2] == 'B' && n[3] && n[4] == 'P'; }

static const char *const FS_NAMES[] = { "Automatic (FAT32, exFAT from 32 GB)", "FAT32", "exFAT", "FAT (small volumes)" };
static const unsigned FS_CODES[] = { KAPI_FMT_AUTO, KAPI_FMT_FAT32, KAPI_FMT_EXFAT, KAPI_FMT_FAT };

static void cat (char *d, int cap, const char *s) { int n = 0; while (d[n]) n++; while (*s && n < cap - 1) d[n++] = *s++; d[n] = 0; }
static bool same (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

static void size_text (unsigned long long b, char *out, int cap)	// "14.9 GB"
{
	const char *unit = " KB"; unsigned long long d = 1024;
	if (b >= (1ull << 40)) { unit = " TB"; d = 1ull << 40; }
	else if (b >= (1ull << 30)) { unit = " GB"; d = 1ull << 30; }
	else if (b >= (1ull << 20)) { unit = " MB"; d = 1ull << 20; }
	unsigned long long w = b / d, t = (b % d) * 10 / d;
	char tmp[24]; int k = 0; do { tmp[k++] = (char) ('0' + w % 10); w /= 10; } while (w);
	int n = 0; while (k && n < cap - 1) out[n++] = tmp[--k];
	if (d > 1024 && b / d < 100 && n < cap - 3) { out[n++] = '.'; out[n++] = (char) ('0' + t); }
	for (const char *u = unit; *u && n < cap - 1; u++) out[n++] = *u;
	out[n] = 0;
}

static const char *state_text (const struct kapi_volume &v)
{
	switch (v.state)
	{
	case KAPI_VST_MOUNTED:		return "mounted";
	case KAPI_VST_EJECTED:		return "ejected: it can be removed";
	case KAPI_VST_UNREADABLE:	return (v.flags & KAPI_VF_IOERR) ? "cannot be read" : "not formatted";
	case KAPI_VST_REMOVED:		return "removed";
	default:			return "?";
	}
}

static void vname (const struct kapi_volume &v, char *out)	// "USB2:"
{
	int n = 0; while (v.name[n] && n < 7) { out[n] = v.name[n]; n++; }
	out[n++] = ':'; out[n] = 0;
}

static int sel_index (void)
{
	int s = g_list->sel, k = 0;
	for (int i = 0; i < g_n; i++)
	{
		if (g_v[i].state == KAPI_VST_REMOVED) continue;
		if (k++ == s) return i;
	}
	return -1;
}

static void show_details (void)
{
	int i = sel_index ();
	g_open->disabled = g_eject->disabled = g_mount->disabled = g_format->disabled = true;
	if (i < 0) { g_l1->setText (""); g_l2->setText (""); g_l3->setText (""); g_bar->setValue (0); }
	else
	{
		const struct kapi_volume &v = g_v[i];
		scopy (g_selName, v.name, sizeof g_selName);
		char a[160] = "", s1[24], s2[24];
		char vn[12]; vname (v, vn);
		cat (a, sizeof a, vn); cat (a, sizeof a, "  ");
		cat (a, sizeof a, v.label[0] ? v.label : (v.flags & KAPI_VF_REMOVABLE) ? "USB stick" : (v.flags & KAPI_VF_RAM) ? "memory" : "SD card");
		cat (a, sizeof a, " -- "); cat (a, sizeof a, state_text (v));
		g_l1->setText (a);
		a[0] = 0;
		if (v.state == KAPI_VST_MOUNTED)
		{
			size_text (v.total, s1, sizeof s1);
			cat (a, sizeof a, v.type); cat (a, sizeof a, ", "); cat (a, sizeof a, s1);
			if (v.free != ~0ull) { size_text (v.free, s2, sizeof s2); cat (a, sizeof a, ", free "); cat (a, sizeof a, s2); }
			if (v.open) { char n[12]; ax_itoa ((int) v.open, n); cat (a, sizeof a, ", "); cat (a, sizeof a, n); cat (a, sizeof a, " file(s) open"); }
			g_bar->setValue (v.total && v.free != ~0ull ? (int) ((v.total - v.free) * 100 / v.total) : 0);
		}
		else g_bar->setValue (0);
		g_l2->setText (a);
		a[0] = 0;
		if (v.device[0])
		{
			cat (a, sizeof a, "Device "); cat (a, sizeof a, v.device);
			if (v.device_size) { size_text (v.device_size, s1, sizeof s1); cat (a, sizeof a, ", "); cat (a, sizeof a, s1); }
		}
		if (v.flags & KAPI_VF_SYSTEM) cat (a, sizeof a, a[0] ? " -- the system's volume" : "The system's volume");
		g_l3->setText (a);
		g_open->disabled = v.state != KAPI_VST_MOUNTED;
		g_eject->disabled = !(v.flags & KAPI_VF_REMOVABLE) || v.state != KAPI_VST_MOUNTED;
		g_mount->disabled = !((v.flags & KAPI_VF_REMOVABLE) && (v.state == KAPI_VST_EJECTED || v.state == KAPI_VST_UNREADABLE));
		g_format->disabled = !(v.flags & KAPI_VF_FORMATTABLE) || (v.flags & KAPI_VF_SYSTEM);
		if (g_label->text[0] == 0 && v.label[0]) g_label->setText (v.label);
		bool part = is_part (v.name);
		if (g_whole->hidden == part) { g_whole->hidden = !part; g_whole->checked = false; g_whole->invalidate (true); }
	}
	g_open->invalidate (true); g_eject->invalidate (true); g_mount->invalidate (true); g_format->invalidate (true);
}

static unsigned sig_of (void)
{
	unsigned s = (unsigned) g_n;
	for (int i = 0; i < g_n; i++) s = s * 31 + g_v[i].gen * 7 + g_v[i].state + g_v[i].open;
	return s;
}

static void refresh (bool force)
{
	int n = kapi_vol_list (g_v, MAXV, KAPI_VOLS_ROOM);
	g_n = n < 0 ? 0 : n > MAXV ? MAXV : n;
	unsigned s = sig_of ();
	if (!force && s == g_sig) return;
	g_sig = s;
	char keep[8]; scopy (keep, g_selName, sizeof keep);
	g_list->clear ();
	int sel = -1, k = 0;
	for (int i = 0; i < g_n; i++)
	{
		const struct kapi_volume &v = g_v[i];
		if (v.state == KAPI_VST_REMOVED) continue;
		char row[64] = "", vn[12], sz[24];
		vname (v, vn);
		cat (row, sizeof row, vn); while (ax_strlen (row) < 9) cat (row, sizeof row, " ");
		cat (row, sizeof row, v.label[0] ? v.label : (v.flags & KAPI_VF_REMOVABLE) ? "USB stick" : (v.flags & KAPI_VF_RAM) ? "Memory" : "SD card");
		while (ax_strlen (row) < 24) cat (row, sizeof row, " ");
		if (v.state == KAPI_VST_MOUNTED) { size_text (v.total, sz, sizeof sz); cat (row, sizeof row, sz); cat (row, sizeof row, " "); cat (row, sizeof row, v.type); }
		else cat (row, sizeof row, state_text (v));
		g_list->add (row);
		if (same (v.name, keep)) sel = k;
		k++;
	}
	if (sel < 0 && k > 0) sel = 0;
	if (sel >= 0) g_list->setSel (sel);
	show_details ();
}

static void on_select (Widget &) { g_label->setText (""); show_details (); }

static void on_open (Widget &w)
{
	if (w.disabled) return;				// (a button greyed: not for this volume)
	int i = sel_index ();
	if (i < 0) return;
	char p[16]; vname (g_v[i], p); cat (p, sizeof p, "/");
	lx_launch ("fileviewer", p);
}

static void on_eject (Widget &w)
{
	if (w.disabled) return;				// (a button greyed: not for this volume)
	int i = sel_index ();
	if (i < 0) return;
	char vn[12]; vname (g_v[i], vn);
	int r = kapi_vol_eject (vn, 0);
	if (r == -KAPI_EBUSY)
	{
		if (!uk_messagebox ("Eject", "Files are still open on this volume (they were saved). Eject it anyway?", MB_YESNO)) { g_status->setText ("Not ejected: files are open on it."); return; }
		r = kapi_vol_eject (vn, KAPI_EJECT_FORCE);
	}
	char m[96] = ""; cat (m, sizeof m, vn);
	cat (m, sizeof m, r == 0 ? " can be removed safely." : " could not be ejected.");
	g_status->setText (m);
	refresh (true);
}

static void on_mount (Widget &w)
{
	if (w.disabled) return;				// (a button greyed: not for this volume)
	int i = sel_index ();
	if (i < 0) return;
	char vn[12]; vname (g_v[i], vn);
	int r = kapi_vol_mount (vn);
	char m[96] = ""; cat (m, sizeof m, vn);
	cat (m, sizeof m, r == 0 ? " is mounted." : r == -KAPI_EINVAL ? " has no FAT / exFAT file system: format it." : " could not be mounted.");
	g_status->setText (m);
	refresh (true);
}

static Root *g_root;

static void on_format (Widget &w)
{
	if (w.disabled) return;				// (a button greyed: not for this volume)
	int i = sel_index ();
	if (i < 0) return;
	const struct kapi_volume &v = g_v[i];
	char vn[12]; vname (v, vn);
	if (v.flags & KAPI_VF_SYSTEM) { g_status->setText ("SD: is the system's volume: it is never formatted."); return; }
	if (is_part (v.name) && g_whole->checked) { vn[0] = 'U'; vn[1] = 'S'; vn[2] = 'B'; vn[3] = v.name[3]; vn[4] = ':'; vn[5] = 0; }	// ("USB1:")
	struct kapi_format f;
	kapi_memset (&f, 0, sizeof f);
	f.fs = FS_CODES[g_fs->sel >= 0 && g_fs->sel < 4 ? g_fs->sel : 0];
	scopy (f.label, g_label->text, sizeof f.label);
	char q[240] = "Everything on "; cat (q, sizeof q, vn);
	cat (q, sizeof q, is_part (v.name) && g_whole->checked ? " -- the whole device, all its partitions -- will be erased; it becomes one partition. Format it?"
								 : " will be erased. Format it?");
	if (!uk_messagebox ("Format", q, MB_YESNO)) return;
	if (!(v.flags & KAPI_VF_REMOVABLE))
	{
		char q2[200] = ""; cat (q2, sizeof q2, vn);
		cat (q2, sizeof q2, " is a partition of the SD card, the card Onyx runs from. Really erase it?");
		if (!uk_messagebox ("Format a partition of the SD card", q2, MB_YESNO)) return;
		f.flags |= KAPI_FMT_CARD;
	}
	g_status->setText ("Formatting... (a big stick takes a few seconds)");
	g_root->draw (); uk_present ();
	int r = kapi_vol_format (vn, &f);
	if (r == -KAPI_EBUSY && uk_messagebox ("Format", "Files are open on this volume. Format it anyway?", MB_YESNO))
	{
		f.flags |= KAPI_FMT_FORCE;
		r = kapi_vol_format (vn, &f);
	}
	char m[160] = ""; cat (m, sizeof m, vn);
	switch (-r)
	{
	case 0:			cat (m, sizeof m, " was formatted."); notify ("Disks", m); break;
	case KAPI_EINVAL:	cat (m, sizeof m, ": the label is not valid (11 characters, none of \" * + , . / : ; < = > ? [ \\ ] |)."); break;
	case KAPI_ENOSPC:	cat (m, sizeof m, ": too small or too big for that file system."); break;
	case KAPI_EBUSY:	cat (m, sizeof m, " was not formatted: files are open on it."); break;
	case KAPI_EPERM:	cat (m, sizeof m, " may not be formatted."); break;
	case KAPI_EROFS:	cat (m, sizeof m, ": the device is write-protected."); break;
	default:		cat (m, sizeof m, ": the format failed."); break;
	}
	g_status->setText (m);
	g_label->setText ("");
	refresh (true);
}

class DisksRoot : public Root
{
public:
	DisksRoot () : Root (W, H, "Disks") {}
	void onResized () override;
	void onTick () override
	{
		static unsigned last = 0;
		unsigned now = kapi_get_ticks ();
		if (now - last < 100) return;
		last = now;
		refresh (false);
	}
};

// The window resized (a frame dragged, PocketUI's fill): the two boxes as wide as the window, the volumes' list
// taller (the room the height gives), the buttons, the Format box and the status line under it
static GroupBox *g_gv, *g_gf;
void DisksRoot::onResized ()
{
	int cw = width - 20, ex = height - H;			// (lower than 470, down to 440: the list shorter)
	g_gv->left = 10; g_gv->resizeTo (cw, 270 + ex);
	int ct = g_gv->contentTop () + 4, y = ct + 128 + ex;
	g_list->resizeTo (cw - 24, 120 + ex);
	Widget *ls[4] = { g_l1, g_l2, g_l3, g_bar };
	for (int i = 0; i < 4; i++) { ls[i]->top = y + (i == 3 ? 68 : i * 22); ls[i]->resizeTo (cw - 24, ls[i]->height); }
	Widget *bt[3] = { g_open, g_eject, g_mount };
	for (int i = 0; i < 3; i++) { bt[i]->left = 10 + i * 130; bt[i]->top = 286 + ex; }
	g_gf->left = 10; g_gf->top = 324 + ex; g_gf->resizeTo (cw, 104);
	((Widget *) g_format)->left = cw - 12 - 130;
	((Widget *) g_status)->left = 12; ((Widget *) g_status)->top = height - 34; g_status->resizeTo (width - 24, 22);
	invalidate (true);
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	DisksRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	int X = root.width > W ? (root.width - W) / 2 : 0;

	GroupBox *gv = g_gv = new GroupBox (X + 10, 8, W - 20, 270, "Volumes");
	root.addChild (gv);
	int ct = gv->contentTop () + 4;
	g_list = new ListBox (12, ct, W - 44, 120, on_select, on_open);
	gv->addChild (g_list);
	int y = ct + 128;
	g_l1 = new Label (12, y, W - 44, 20, "", C_TEXT, gv->bg); gv->addChild (g_l1);
	g_l2 = new Label (12, y + 22, W - 44, 20, "", C_TEXT, gv->bg); gv->addChild (g_l2);
	g_l3 = new Label (12, y + 44, W - 44, 20, "", C_DIS, gv->bg); gv->addChild (g_l3);
	g_bar = new Progress (12, y + 68, W - 44, 12, 0, 100, 0); gv->addChild (g_bar);

	int by = 286;
	g_open  = new Button (X + 10, by, 120, 30, "Open", on_open);    root.addChild (g_open);
	g_eject = new Button (X + 140, by, 120, 30, "Eject", on_eject); root.addChild (g_eject);
	g_mount = new Button (X + 270, by, 120, 30, "Mount", on_mount); root.addChild (g_mount);

	GroupBox *gf = g_gf = new GroupBox (X + 10, 324, W - 20, 104, "Format");
	root.addChild (gf);
	int ft = gf->contentTop () + 4;
	gf->addChild (new Label (12, ft + 4, 110, 20, "File system", C_TEXT, gf->bg));
	g_fs = new Dropdown (124, ft, 300, 28, FS_NAMES, 4, 0, 0);
	gf->addChild (new Label (12, ft + 38, 110, 20, "Label", C_TEXT, gf->bg));
	g_label = new Textbox (124, ft + 34, 180, 28, "");
	g_label->maxLen = 11;
	gf->addChild (g_label);
	g_whole = new Checkbox (316, ft + 38, 150, 22, "Whole device", false, 0, gf->bg);
	g_whole->hidden = true;
	gf->addChild (g_whole);
	g_format = new Button (W - 20 - 12 - 130, ft + 34, 130, 30, "Format...", on_format);
	gf->addChild (g_format);
	gf->addChild (g_fs);					// (last: its list opens over the others)

	g_status = new Label (X + 12, H - 34, W - 24, 22, "", C_TEXT, root.bg);
	root.addChild (g_status);

	root.setResizable (true);
	root.setMinSize (W, 440);
	refresh (true);
	char args[64];
	if (kapi_get_args (args, sizeof args) > 0 && args[0])	// `disks USB:`: that volume
	{
		char nm[8]; int n = 0;
		while (args[n] && args[n] != ':' && args[n] != ' ' && n < 7) { nm[n] = args[n] >= 'a' && args[n] <= 'z' ? (char) (args[n] - 32) : args[n]; n++; }
		nm[n] = 0;
		if (same (nm, "USB")) scopy (nm, "USB1", sizeof nm);	// (USB: is USB1:)
		scopy (g_selName, nm, sizeof g_selName);
		refresh (true);
	}
	root.run ();
	return 0;
}
