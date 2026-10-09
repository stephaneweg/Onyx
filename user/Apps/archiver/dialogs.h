//
// archiver/dialogs.h -- the Archiver's dialogs (uikit modals): Extract (what, where, which folders, a
// file that exists, the password), Add to the Archive (the files and folders, where in the archive,
// the compression), the job's progress (Background / Cancel), "it exists already", a line of text
// (a name, a password), the archive's properties.
//
#ifndef _archiver_dialogs_h
#define _archiver_dialogs_h

#include "widgets.h"

namespace ui {

static void dlg_btn (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }
static void dlg_enter (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (1); }

static inline void centre (Modal *m)
{
	Root *r = Root::current ();
	if (!r) return;
	if (m->width > r->width - 16) m->width = r->width - 16;
	if (m->height > r->height - 16) m->height = r->height - 16;
	m->left = (r->width - m->width) / 2; m->top = (r->height - m->height) / 2;
	m->canvas.resize (m->width, m->height);
}
// a section: an etched frame, its title in small capitals
static inline void section (Canvas &cv, int x, int y, int w, int h, const char *title)
{
	uk_etch_box (cv, x, y, w, h, 6, C_FACE);
	small_v (cv, x + 12, y + 4, 18, title, uk_mix (C_TEXT, C_FACE, 100), 2);
}
static inline Button *button (Modal *m, int x, int y, int w, const char *s, int tag)
{
	Button *b = new Button (x, y, w, 30, s, dlg_btn); b->tag = tag; m->addChild (b); return b;
}

// ---- a line of text ----------------------------------------------------------------------------------
class InputBox : public Modal
{
	const char *m_title, *m_msg;
public:
	Textbox *tb;
	InputBox (const char *title, const char *msg, const char *init, bool password = false) : Modal (420, 150), m_title (title), m_msg (msg)
	{
		centre (this);
		tb = new Textbox (16, titleH () + 40, width - 32, 28, init, dlg_enter);
		tb->maxLen = 255; tb->password = password;
		tb->setText (init); tb->caret = (int) strlen (init);
		tb->hasFocus = true; addChild (tb);
		button (this, width - 196, height - 44, 86, TR ("OK"), 1);
		button (this, width - 102, height - 44, 86, TR ("Cancel"), 0);
	}
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override { drawBox (m_title); text_v (canvas, 16, titleH () + 10, 22, m_msg, C_TEXT); }
};
static inline bool ask_text (const char *title, const char *msg, const char *init, char *out, int cap, bool password = false)
{
	InputBox b (title, msg, init, password);
	if (!b.run ()) return false;
	arc::scopy (out, b.tb->text, cap);
	return out[0] != 0;
}

// ---- it exists already ------------------------------------------------------------------------------
class ExistsBox : public Modal
{
	char m_name[200], m_where[200];
public:
	ExistsBox (const char *path) : Modal (600, 170)
	{
		centre (this);
		arc::scopy (m_name, arc::base_of (path), sizeof m_name);
		arc::scopy (m_where, path, sizeof m_where);
		char *sl = strrchr (m_where, '/'); if (sl) *sl = 0;
		static const char *lab[] = { TR ("Replace"), TR ("Replace All"), TR ("Skip"), TR ("Skip All"), TR ("Keep Both"), TR ("Cancel") };
		static const int tag[] = { arc::ANS_REPLACE, arc::ANS_REPLACE_ALL, arc::ANS_SKIP, arc::ANS_SKIP_ALL, arc::ANS_KEEP_BOTH, 0 };
		int x = 16;
		for (int i = 0; i < 6; i++) { int w = uk_tw (lab[i]) + 26; button (this, x, height - 46, w, lab[i], tag[i]); x += w + 6; }
	}
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override
	{
		drawBox (TR ("A file exists already"));
		char t[260]; arc::scopy (t, m_name, sizeof t); arc::scat (t, TR (" is there already."), sizeof t);
		char f[220]; uk_text_fit (t, width - 32, f, sizeof f, 2);
		text_v (canvas, 16, titleH () + 12, 22, f, C_TEXT, 2);
		uk_text_fit (m_where, width - 80, f, sizeof f);
		small_v (canvas, 16, titleH () + 38, 20, TR ("In"), uk_mix (C_TEXT, C_FACE, 100));
		small_v (canvas, 36, titleH () + 38, 20, f, C_TEXT);
	}
};

// ---- the job's progress ----------------------------------------------------------------------------
class ProgressBox : public Modal
{
public:
	char title[80], file[200], line[160]; int pct; bool cancelled, background;
	ProgressBox (const char *t) : Modal (460, 190), pct (0), cancelled (false), background (false)
	{
		centre (this);
		arc::scopy (title, t, sizeof title); file[0] = line[0] = 0;
		button (this, width - 230, height - 46, 116, TR ("Background"), 2);
		button (this, width - 106, height - 46, 90, TR ("Cancel"), 0);
	}
	void onButton (int tag) override { if (tag == 2) { background = true; close (2); } else { cancelled = true; close (0); } }
	bool onKey (long k) override { if (k == 27) { cancelled = true; close (0); return true; } return false; }
	void onDraw () override
	{
		drawBox (title);
		char f[200]; uk_text_fit (file, width - 32, f, sizeof f);
		text_v (canvas, 16, titleH () + 12, 22, f, C_TEXT);
		uk_progress_bar (canvas, 16, titleH () + 44, width - 32, 14, pct);
		small_v (canvas, 16, titleH () + 66, 20, line, uk_mix (C_TEXT, C_FACE, 100));
	}
};

// ---- the archive's properties -----------------------------------------------------------------------
class PropsBox : public Modal
{
public:
	enum { MAXR = 16 };
	char k[MAXR][32], v[MAXR][160]; int n;
	PropsBox (int rows) : Modal (480, 0), n (0)
	{
		height = titleH () + 24 + rows * 24 + 56;
		centre (this);
		button (this, width - 106, height - 46, 90, TR ("OK"), 1);
	}
	void row (const char *a, const char *b) { if (n < MAXR) { arc::scopy (k[n], a, 32); arc::scopy (v[n], b, 160); n++; } }
	void onButton (int tag) override { close (tag); }
	bool onKey (long kk) override { if (kk == 27 || kk == KEY_ENTER) { close (1); return true; } return false; }
	void onDraw () override
	{
		drawBox (TR ("Properties"));
		for (int i = 0; i < n; i++)
		{
			int y = titleH () + 14 + i * 24;
			small_v (canvas, 16, y, 22, k[i], uk_mix (C_TEXT, C_FACE, 100));
			char t[160]; uk_text_fit (v[i], width - 160, t, sizeof t, 2);
			text_v (canvas, 140, y, 22, t, C_TEXT, 2);
		}
	}
};

// ---- Extract ---------------------------------------------------------------------------------------
struct ExtractChoice
{
	bool all; char dest[300]; bool newFolder; char folderName[128];
	int layout, overwrite; bool openAfter; char password[128];
};
class ExtractBox : public Modal
{
public:
	RadioButton *rSel, *rAll, *rFull, *rCur, *rFlat;
	Textbox *dest, *fname, *pw;
	Checkbox *cNew, *cOpen;
	Dropdown *ow;
	char m_title[160], m_sub[200], m_hint[3][160], selText[80], allText[80];
	bool m_crypt;
	int gy[5];
	static const char *const *owOpts () { static const char *o[] = { TR ("Ask for each one"), TR ("Replace them"), TR ("Skip them"), TR ("Keep both (a new name)") }; return o; }
	ExtractBox (const char *archiveName, const char *sub, int nsel, arc::u64 selSize, int nall, arc::u64 allSize,
		    const char *startDest, const char *folderName, const char *hintFull, const char *hintCur, const char *hintFlat, bool crypt)
	  : Modal (620, 560), m_crypt (crypt)
	{
		centre (this);
		arc::scopy (m_title, TR ("Extract from "), sizeof m_title); arc::scat (m_title, archiveName, sizeof m_title);
		arc::scopy (m_sub, sub, sizeof m_sub);
		arc::scopy (m_hint[0], hintFull, 160); arc::scopy (m_hint[1], hintCur, 160); arc::scopy (m_hint[2], hintFlat, 160);
		char a[24], b[24];
		arc::u64_str ((arc::u64) nsel, a, sizeof a); arc::human_size (selSize, b, sizeof b);
		arc::scopy (selText, a, 80); arc::scat (selText, nsel == 1 ? TR (" item, ") : TR (" items, "), 80); arc::scat (selText, b, 80);
		arc::u64_str ((arc::u64) nall, a, sizeof a); arc::human_size (allSize, b, sizeof b);
		arc::scopy (allText, a, 80); arc::scat (allText, nall == 1 ? TR (" file, ") : TR (" files, "), 80); arc::scat (allText, b, 80);
		int x = 20, w = width - 40, y = titleH () + 58, rh = 24;
		unsigned bg = C_FACE;
		gy[0] = y;
		rSel = new RadioButton (x + 14, y + 26, 200, rh, TR ("The selection"), 1, nsel > 0, 0, bg);
		rAll = new RadioButton (x + 14, y + 26 + rh, 200, rh, TR ("Everything"), 1, nsel == 0, 0, bg);
		if (!nsel) rSel->disabled = true;
		addChild (rSel); addChild (rAll);
		y += 26 + 2 * rh + 18; gy[1] = y;
		dest = new Textbox (x + 14, y + 26, w - 28 - 104, 28, startDest, 0); dest->maxLen = 255; dest->setText (startDest);
		addChild (dest);
		button (this, x + w - 14 - 96, y + 25, 96, TR ("Browse..."), 3);
		cNew = new Checkbox (x + 14, y + 62, 210, 26, TR ("Into a new folder named"), true, 0, bg);
		fname = new Textbox (x + 228, y + 61, w - 228 - 14, 28, folderName, 0); fname->maxLen = 120; fname->setText (folderName);
		addChild (cNew); addChild (fname);
		y += 26 + 28 + 8 + 28 + 18; gy[2] = y;
		rFull = new RadioButton (x + 14, y + 26, 240, rh, TR ("Keep the archive's folders"), 2, true, 0, bg);
		rCur = new RadioButton (x + 14, y + 26 + rh, 240, rh, TR ("From the current folder down"), 2, false, 0, bg);
		rFlat = new RadioButton (x + 14, y + 26 + 2 * rh, 240, rh, TR ("All in one folder (flat)"), 2, false, 0, bg);
		addChild (rFull); addChild (rCur); addChild (rFlat);
		y += 26 + 3 * rh + 18; gy[3] = y;
		ow = new Dropdown (x + 130, y + 26, 200, 28, owOpts (), 4, 0, 0);
		cOpen = new Checkbox (x + 350, y + 27, w - 364, 26, TR ("Show the folder after"), true, 0, bg);
		addChild (ow); addChild (cOpen);
		y += 26 + 28 + 18; gy[4] = y;
		pw = new Textbox (x + 120, y, 240, 28, "", 0); pw->password = true; pw->maxLen = 120;
		if (!crypt) pw->disabled = true;
		addChild (pw);
		height = y + 28 + 62; centre (this);
		button (this, width - 20 - 110, height - 46, 110, TR ("Extract"), 1);
		button (this, width - 20 - 110 - 10 - 96, height - 46, 96, TR ("Cancel"), 0);
		ow->bringToFront ();
	}
	void onButton (int tag) override
	{
		if (tag == 3)
		{
			char p[300];
			if (uk_folder_open (p, sizeof p, dest->text)) { dest->setText (p); dest->invalidate (true); }
			return;
		}
		close (tag);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { close (1); return true; } return false; }
	void onDraw () override
	{
		drawBox (TR ("Extract"));
		int x = 20, w = width - 40;
		icon_archive (canvas, x, titleH () + 12, 38);
		char t[160]; uk_text_fit (m_title, w - 60, t, sizeof t, 2);
		text_v (canvas, x + 52, titleH () + 10, 22, t, C_TEXT, 2);
		uk_text_fit (m_sub, w - 60, t, sizeof t);
		small_v (canvas, x + 52, titleH () + 32, 18, t, uk_mix (C_TEXT, C_FACE, 100));
		section (canvas, x, gy[0], w, gy[1] - gy[0] - 10, TR ("WHAT"));
		section (canvas, x, gy[1], w, gy[2] - gy[1] - 10, TR ("WHERE"));
		section (canvas, x, gy[2], w, gy[3] - gy[2] - 10, TR ("FOLDERS"));
		section (canvas, x, gy[3], w, gy[4] - gy[3] - 10, TR ("IF A FILE EXISTS"));
		unsigned dm = uk_mix (C_TEXT, C_FACE, 100);
		small_v (canvas, x + 234, gy[0] + 26, 24, selText, dm);
		small_v (canvas, x + 234, gy[0] + 50, 24, allText, dm);
		for (int i = 0; i < 3; i++) { uk_text_fit (m_hint[i], w - 280, t, sizeof t); small_v (canvas, x + 262, gy[2] + 26 + i * 24, 24, t, dm); }
		text_v (canvas, x + 14, gy[3] + 26, 28, TR ("Existing files:"), C_TEXT);
		text_v (canvas, x, gy[4], 28, TR ("Password:"), m_crypt ? C_TEXT : C_DIS);
		if (!m_crypt) small_v (canvas, x + 370, gy[4], 28, TR ("(nothing is encrypted)"), dm);
	}
	void get (ExtractChoice &c)
	{
		c.all = rAll->checked || !rSel->checked;
		arc::scopy (c.dest, dest->text, sizeof c.dest);
		c.newFolder = cNew->checked; arc::scopy (c.folderName, fname->text, sizeof c.folderName);
		c.layout = rCur->checked ? arc::LAY_FROM_CURRENT : rFlat->checked ? arc::LAY_FLAT : arc::LAY_FULL;
		c.overwrite = ow->sel == 1 ? arc::OW_REPLACE : ow->sel == 2 ? arc::OW_SKIP : ow->sel == 3 ? arc::OW_KEEP_BOTH : arc::OW_ASK;
		c.openAfter = cOpen->checked;
		arc::scopy (c.password, pw->text, sizeof c.password);
	}
};

// ---- Add to the Archive ------------------------------------------------------------------------------
struct AddChoice { char into[300]; bool keepFolders; int level; bool replace; };
class AddBox : public Modal
{
public:
	enum { MAXI = 64 };
	char item[MAXI][300]; int nitem, selItem;
	Textbox *into;
	RadioButton *rKeep, *rFlat;
	SegmentedControl *level;
	Dropdown *exist;
	char m_title[160], m_dir[300];
	int listY, listH, gy[3];
	static const char *const *exOpts () { static const char *o[] = { TR ("Replace it"), TR ("Keep the old one") }; return o; }
	AddBox (const char *archiveName, const char *folder, const char *startDir) : Modal (660, 560), nitem (0), selItem (-1)
	{
		centre (this);
		arc::scopy (m_title, TR ("Add to "), sizeof m_title); arc::scat (m_title, archiveName, sizeof m_title);
		arc::scopy (m_dir, startDir, sizeof m_dir);
		int x = 20, w = width - 40;
		listY = titleH () + 64; listH = 150;
		button (this, x + w - 120, listY, 120, TR ("Add Files..."), 3);
		button (this, x + w - 120, listY + 38, 120, TR ("Add Folder..."), 4);
		button (this, x + w - 120, listY + 76, 120, TR ("Remove"), 5);
		int y = listY + listH + 16; gy[0] = y;
		into = new Textbox (x + 140, y + 28, w - 154, 28, folder, 0); into->maxLen = 255; into->setText (folder);
		addChild (into);
		rKeep = new RadioButton (x + 14, y + 64, 260, 26, TR ("Keep the folders I add"), 3, true, 0, C_FACE);
		rFlat = new RadioButton (x + 14, y + 90, 260, 26, TR ("Only the files"), 3, false, 0, C_FACE);
		addChild (rKeep); addChild (rFlat);
		y += 132; gy[1] = y;
		static const char *lv[] = { TR ("Store"), TR ("Fast"), TR ("Normal"), TR ("Best") };
		level = new SegmentedControl (x + 14, y + 28, 300, 28, lv, 4, 2, 0);
		addChild (level);
		exist = new Dropdown (x + 140, y + 66, 200, 28, exOpts (), 2, 0, 0);
		addChild (exist);
		y += 108; gy[2] = y;
		height = y + 60; centre (this);
		button (this, width - 20 - 110, height - 48, 110, TR ("Add"), 1);
		button (this, width - 20 - 110 - 10 - 96, height - 48, 96, TR ("Cancel"), 0);
		exist->bringToFront ();
	}
	void addPath (const char *p) { for (int i = 0; i < nitem; i++) if (!strcmp (item[i], p)) return; if (nitem < MAXI) arc::scopy (item[nitem++], p, 300); invalidate (true); }
	void onButton (int tag) override
	{
		char p[300];
		if (tag == 3) { if (uk_file_open (p, sizeof p, m_dir)) { addPath (p); arc::scopy (m_dir, p, sizeof m_dir); char *s = strrchr (m_dir, '/'); if (s) *s = 0; } return; }
		if (tag == 4) { if (uk_folder_open (p, sizeof p, m_dir)) { addPath (p); arc::scopy (m_dir, p, sizeof m_dir); } return; }
		if (tag == 5) { if (selItem >= 0 && selItem < nitem) { for (int i = selItem; i < nitem - 1; i++) arc::scopy (item[i], item[i + 1], 300); nitem--; selItem = -1; invalidate (true); } return; }
		if (tag == 1 && !nitem) return;
		close (tag);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int x = 20, w = width - 40 - 132;
		if (bl && mx >= x && mx < x + w && my >= listY && my < listY + listH)
		{
			int r = (my - listY - 4) / 26;
			selItem = r < nitem ? r : -1; invalidate (true);
		}
		return true;
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override
	{
		drawBox (TR ("Add to the Archive"));
		int x = 20, w = width - 40;
		icon_archive (canvas, x, titleH () + 12, 38);
		char t[200]; uk_text_fit (m_title, w - 60, t, sizeof t, 2);
		text_v (canvas, x + 52, titleH () + 10, 22, t, C_TEXT, 2);
		char cnt[40]; arc::u64_str ((arc::u64) nitem, cnt, sizeof cnt); arc::scat (cnt, nitem == 1 ? TR (" item to add") : TR (" items to add"), sizeof cnt);
		small_v (canvas, x + 52, titleH () + 32, 18, nitem ? cnt : TR ("Nothing yet: Add Files... / Add Folder..., or drop them on the archive"), uk_mix (C_TEXT, C_FACE, 100));
		int lw = w - 132;
		uk_sunken (canvas, x, listY, lw, listH, 5, C_FIELD);
		for (int i = 0; i < nitem && 4 + (i + 1) * 26 <= listH; i++)
		{
			int y = listY + 4 + i * 26;
			if (i == selItem) uk_hilite (canvas, x + 3, y, lw - 6, 24, 4, false);
			bool dir = arc::path_is_dir (item[i]);
			if (dir) icon_folder (canvas, x + 10, y + 3, 18); else icon_file (canvas, x + 10, y + 3, 18, kind_colour (item[i]));
			uk_text_fit (arc::base_of (item[i]), lw / 2 - 40, t, sizeof t, dir ? 2 : 0);
			text_v (canvas, x + 36, y, 24, t, C_FIELD_TEXT, dir ? 2 : 0);
			char d[300]; arc::scopy (d, item[i], sizeof d); char *s = strrchr (d, '/'); if (s) *s = 0;
			uk_text_fit (d, lw / 2 - 20, t, sizeof t);
			small_v (canvas, x + lw / 2, y, 24, t, dim_ink ());
		}
		section (canvas, x, gy[0], w, gy[1] - gy[0] - 12, TR ("WHERE IN THE ARCHIVE"));
		section (canvas, x, gy[1], w, gy[2] - gy[1] - 12, TR ("COMPRESSION"));
		text_v (canvas, x + 14, gy[0] + 28, 28, TR ("Into the folder:"), C_TEXT);
		small_v (canvas, x + 140, gy[0] + 56, 0, "", C_TEXT);
		unsigned dm = uk_mix (C_TEXT, C_FACE, 100);
		small_v (canvas, x + 290, gy[0] + 64, 26, TR ("a folder keeps its name and its tree"), dm);
		small_v (canvas, x + 290, gy[0] + 90, 26, TR ("the files of the folders, side by side"), dm);
		small_v (canvas, x + 330, gy[1] + 28, 28, TR ("Deflate; png, jpg, zip... are stored"), dm);
		text_v (canvas, x + 14, gy[1] + 66, 28, TR ("If a name exists:"), C_TEXT);
	}
	void get (AddChoice &c)
	{
		arc::scopy (c.into, into->text, sizeof c.into);
		while (c.into[0] == '/') memmove (c.into, c.into + 1, strlen (c.into));
		int n = (int) strlen (c.into); while (n && c.into[n - 1] == '/') c.into[--n] = 0;
		c.keepFolders = rKeep->checked;
		static const int lv[] = { 0, 1, 6, 9 };
		c.level = lv[level->selected < 0 ? 2 : level->selected];
		c.replace = exist->sel == 0;
	}
};

} // namespace ui

#endif
