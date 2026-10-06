//
// notes/main.cpp -- Notes: quick notes kept on the card, one plain .txt a note in SD:/Notes (AutoDev round 1:
// autodev/rounds/01-notes/, the design in 04-ux-design.md). The window: a tool bar (New Note, Delete, Pin, the
// six colours), the list of the notes on the left (notelist.h, newest first), the selected note's text on the
// right in its paper's colour, a status line. No Save command: a note is written about a second after the
// typing stops, when another note is chosen and when the window closes; an empty note is not kept (a note
// emptied by the user goes to the Trash). Delete sends a note to the Trash (SystemKit) with a notification;
// a pinned note is shown on the desktop by Stickies (user/Apps/stickies, the "stickies" service: told to
// re-read after each change). Files (.txt, .md) dropped on the window become new notes; dropped text goes in
// at the caret. `notes <path>` opens on that note -- in the Notes already running, if any (the "notes" service:
// one Notes at a time). View > Show / Hide Stickies on the Desktop starts or stops Stickies (Show also makes
// sure of its line in SD:/etc/autostart, SystemKit's autostart_ensure). The model (files, notes.ini, names, dates, colours,
// config.ini) is notesmodel.cpp, shared with Stickies.
//
// Keys: Ctrl+N new note, Ctrl+D delete, Ctrl+P pin / unpin, Ctrl+E open in the Text Editor, Ctrl+X / C / V / A
// in the text, Up / Down / Page Up / Page Down / Home / End / Delete in the list, Enter or Tab from the list to
// the text and Tab back, Ctrl+Q quit (after the save).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "systemkit/systemkit.h"
#include "fontkit/uikitface.h"
#define DOC_MESSAGEBOX ft_messagebox
#include "docguard.h"
#include "Apps/notes/notesmodel.h"
#include "Apps/notes/notelist.h"
#include "Apps/notes/stickies_proto.h"
#include <stdio.h>
#include <string.h>

using namespace uikit;

enum { TB_H = 44, ST_H = 24, INFO_H = 34, MIN_W = 520, MIN_H = 320 };
enum { AUTOSAVE = 100, RESCAN = 500, MSG_TIME = 400 };	// ticks (1/100 s): the pause before a save, the idle
							// rescan of SD:/Notes, a passing status message
static const unsigned C_ERROR = 0x00B02A1E;		// the failed save's red (04 section 5)

// ---- the state -------------------------------------------------------------------------------------------
static Notes    g_notes;			// the list (~140 KB: static)
static NotesCfg g_cfg;
static int      g_sel = -1;			// the note in the editor (its row), -1 none
static bool     g_dirty;			// the editor's text differs from what is on the card
static bool     g_bad;				// the note could not be read (larger than 64 KB, gone): shown
						// empty, read-only, never written nor thrown away
static unsigned g_edHash;			// doc_hash of the editor's text as last seen
static unsigned g_lastEdit;			// ticks of the last change of the text
static bool     g_failed;			// the last save failed (retried at the next pause in typing)
static unsigned g_failedAt;			// ... the g_lastEdit it failed at
static unsigned g_sig, g_sigT;			// the folder's signature (the idle rescan), when it was taken
static char     g_buf[NOTE_MAX_BYTES + 1];	// a note read

// The status line's message (the right part): kind 0 dim, 1 "Saved" (a check), 2 an error (red, a cross).
static char     g_msg[160];
static int      g_msgKind;
static unsigned g_msgUntil;			// ticks it goes away at, 0: it stays

class NoteEdit;
class NotesRoot;
static NotesRoot  *g_root;
static NoteList   *g_list;
static NoteEdit   *g_ed;
static Widget     *g_info, *g_status;
static HSplitter  *g_split;
static ToolButton *g_del, *g_pin, *g_colBtn[NC_COUNT];

static NoteInfo *cur () { return g_sel >= 0 && g_sel < g_notes.count ? &g_notes.n[g_sel] : 0; }
static void update_chrome ();
static void build_menu ();
static void text_changed ();

static void set_msg (const char *s, int kind, unsigned time)
{
	snprintf (g_msg, sizeof g_msg, "%s", s ? s : "");
	g_msgKind = kind;
	g_msgUntil = time ? kapi_get_ticks () + time : 0;
	if (g_status) g_status->invalidate (true);
}

// ---- Stickies (the desktop widget: user/Apps/stickies; stickies_proto.h) ---------------------------------
// After a note is saved, pinned, coloured or thrown away: Stickies, if it runs, reads the notes again now
// (it also polls every few seconds).
static void stickies_reload ()
{
	int pid = kapi_ipc_lookup (STICKIES_SERVICE);
	if (pid > 0) kapi_mailbox_send (pid, STK_MSG_RELOAD, "", 0);
}
// View > Show / Hide Stickies on the Desktop: the setting kept (Stickies reads it at its start, and in its
// poll). Show: Stickies started when it is not running, and its line made sure of in SD:/etc/autostart
// (SystemKit's autostart_ensure: after the agenda's line; the only place Notes writes that file, 04 D2), the
// status line saying which; Hide: Stickies told to quit (autostart left as it is).
static void stickies_set_shown (bool on)
{
	g_cfg.stickies = on ? 1 : 0;
	notes_cfg_save (g_cfg);
	int pid = kapi_ipc_lookup (STICKIES_SERVICE);
	if (!on)
	{
		if (pid > 0) kapi_mailbox_send (pid, STK_MSG_QUIT, "", 0);
		set_msg (TR ("Stickies hidden."), 0, MSG_TIME);
		return;
	}
	int r = autostart_ensure ("run stickies", "run agenda", "# Stickies: the pinned notes on the desktop (Notes, View menu)");
	if (pid <= 0) lx_launch ("stickies", 0);
	set_msg (r == 2 ? TR ("Stickies shown, and started at every boot (SD:/etc/autostart).")
		 : r == 1 ? TR ("Stickies shown.") : TR ("Stickies shown (autostart not written)."), 0, MSG_TIME);
}

// ---- the folder's signature (the idle rescan: notes changed by another program) --------------------------
static unsigned folder_sig ()
{
	unsigned h = 2166136261u;
	void *d = kapi_opendir (NOTES_DIR);
	if (!d) return 0;
	struct kapi_dirent de;
	while (kapi_readdir (d, &de) == 1)
	{
		for (int i = 0; de.name[i]; i++) { h ^= (unsigned char) de.name[i]; h *= 16777619u; }
		h ^= de.size; h *= 16777619u;
	}
	kapi_closedir (d);
	return h ? h : 1;
}
static void sig_taken () { g_sig = folder_sig (); g_sigT = kapi_get_ticks (); }

// ---- dates (the info line over the text) -----------------------------------------------------------------
static long long day_number (int y, int m, int d)			// days since 1970-01-01
{
	y -= m <= 2;
	long long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
	long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
// "Today, 09:15", "Yesterday, 18:30", "Friday 25 September, 20:02", "15 September, 10:15", "2 May 2025, 08:00" --
// in the system's language (the French: "vendredi 25 septembre, 20:02", the names in small letters).
static void long_date (long long when, long long now, char *out, int cap)
{
	static const char *const MON[12] = { TRN ("January"), TRN ("February"), TRN ("March"), TRN ("April"), TRN ("May"),
					     TRN ("June"), TRN ("July"), TRN ("August"), TRN ("September"), TRN ("October"),
					     TRN ("November"), TRN ("December") };
	static const char *const DAY[7] = { TRN ("Thursday"), TRN ("Friday"), TRN ("Saturday"), TRN ("Sunday"), TRN ("Monday"),
					    TRN ("Tuesday"), TRN ("Wednesday") };
	out[0] = 0;
	int y = (int) (when / 10000000000LL), mo = (int) (when / 100000000 % 100), d = (int) (when / 1000000 % 100);
	int h = (int) (when / 10000 % 100), mi = (int) (when / 100 % 100);
	if (when <= 0 || mo < 1 || mo > 12 || d < 1 || d > 31) return;
	int ny = (int) (now / 10000000000LL), nmo = (int) (now / 100000000 % 100), nd = (int) (now / 1000000 % 100);
	long long day = day_number (y, mo, d), diff = nmo >= 1 && nmo <= 12 ? day_number (ny, nmo, nd) - day : 1000;
	const char *M = TR (MON[mo - 1]);		// (the catalogue's French in small letters: "septembre")
	if (diff == 0) snprintf (out, (size_t) cap, "%s, %02d:%02d", TR ("Today"), h, mi);
	else if (diff == 1) snprintf (out, (size_t) cap, "%s, %02d:%02d", TR ("Yesterday"), h, mi);
	else if (diff >= 2 && diff <= 6) snprintf (out, (size_t) cap, "%s %d %s, %02d:%02d", TR (DAY[((day % 7) + 7) % 7]), d, M, h, mi);
	else if (y == ny) snprintf (out, (size_t) cap, "%d %s, %02d:%02d", d, M, h, mi);
	else snprintf (out, (size_t) cap, "%d %s %d, %02d:%02d", d, M, y, h, mi);
}

// ---- the widgets beside the list -------------------------------------------------------------------------
// The editor: a Textarea in the note's paper, its text one size up (a face of its own for its drawing, its
// clicks and its keys alike, so the caret follows the glyphs it shows). Every change of its text marks the
// note changed (the save comes after a pause); Tab goes back to the list.
class NoteEdit : public Textarea
{
public:
	FtTextFace *face;
	NoteEdit (int l, int t, int w, int h, int cap) : Textarea (l, t, w, h, cap), face (0) {}
	void onDraw () override { UkFaceScope s (face); Textarea::onDraw (); }
	bool onMouse (int mx, int my, int bl, int br, int bm, int wh) override
	{
		bool r;
		{ UkFaceScope s (face); r = Textarea::onMouse (mx, my, bl, br, bm, wh); }
		if (bl) text_changed ();
		return r;
	}
	bool onKey (long k) override
	{
		if (k == KEY_TAB && !(kapi_get_modifiers () & (MOD_CTRL | MOD_ALT)))
		{ ((Widget *) g_list)->setFocus (); return true; }
		int before = len;
		bool r;
		{ UkFaceScope s (face); r = Textarea::onKey (k); }
		text_changed ();
		bool typed = k == KEY_ENTER || (k >= 32 && k < 0x100 && k != 127);
		if (typed && !readonly && len == before && len >= NOTE_MAX_BYTES - 3)
			set_msg (TR ("This note is full (64 KB)."), 0, MSG_TIME);
		return r;
	}
};

// The colours' names (notesmodel.cpp's notes_colour_label, given to TR () where shown):
// TR: Yellow
// TR: Green
// TR: Blue
// TR: Pink
// TR: Purple
// TR: Grey
static unsigned paper_sheet (int c) { return uk_mix (C_FIELD, notes_colour_paper (c), 150); }

// The line over the text: when the note was changed, its colour, "On the desktop" when pinned.
class InfoLine : public Widget
{
public:
	InfoLine (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 140);
		NoteInfo *e = cur ();
		if (!e) return;
		if (!e->saved && !g_ed->len && !g_bad) { uk_text_l (canvas, 18, 0, height, TR ("New note \xC2\xB7 type: it is kept by itself, no need to save"), dim); return; }
		canvas.fillRect (12, height - 1, width - 24, 1, uk_mix (C_FIELD, C_FIELD_TEXT, 30));
		if (g_bad) { uk_text_l (canvas, 18, 0, height, TR ("Larger than 64 KB: open it in the Text Editor (Ctrl+E)"), dim); return; }
		char d[64], s[120];
		if (e->saved) long_date (e->modified, notes_now (), d, sizeof d); else snprintf (d, sizeof d, "%s", TR ("New note"));
		snprintf (s, sizeof s, "%s  \xC2\xB7  %s", d[0] ? d : TR ("Date unknown"), TR (notes_colour_label (e->colour)));
		uk_text_l (canvas, 18, 0, height, s, dim);
		if (e->pinned)
		{
			int x = 18 + uk_tw (s) + 14;
			uk_tool_glyph (canvas, WKT_PIN, x, (height - 14) / 2, 14, C_ACCENT);
			uk_text_l (canvas, x + 18, 0, height, TR ("On the desktop"), C_ACCENT);
		}
	}
};

// The status line at the window's bottom: the counts at the left, what happened at the right.
class StatusLine : public Widget
{
public:
	StatusLine (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		canvas.fillRect (0, 0, width, 1, uk_mix (C_BG, C_TEXT, 40));
		unsigned dim = uk_mix (C_BG, C_TEXT, 170);
		int saved = 0, pinned = 0;
		for (int i = 0; i < g_notes.count; i++) if (g_notes.n[i].saved) { saved++; if (g_notes.n[i].pinned) pinned++; }
		char l[96];
		const char *notes = saved > 1 ? TR ("notes") : TR ("note");
		if (g_notes.total > g_notes.count) snprintf (l, sizeof l, TR ("Showing the %d newest notes"), g_notes.count);
		else if (!saved) snprintf (l, sizeof l, "%s", TR ("No notes yet"));
		else if (!pinned) snprintf (l, sizeof l, "%d %s", saved, notes);
		else snprintf (l, sizeof l, "%d %s \xC2\xB7 %d %s", saved, notes, pinned, TR ("on the desktop"));
		uk_text_l (canvas, 12, 1, height - 1, l, dim);
		if (!g_msg[0]) return;
		unsigned c = g_msgKind == 2 ? C_ERROR : dim;
		int w = uk_tw (g_msg);
		if (g_msgKind == 2) uk_glyph (canvas, WKG_CLOSE, width - w - 26, height / 2, 9, c);
		else if (g_msgKind == 1) uk_glyph (canvas, WKG_CHECK, width - w - 22, height / 2, 10, dim);
		uk_text_l (canvas, width - w - 12, 1, height - 1, g_msg, c);
	}
};

// ---- the notes: load, save, leave ------------------------------------------------------------------------
// The editor's text changed? (a key, a click -- its own clipboard keys --, the menu's Cut / Paste, a drop):
// marked changed, the row's title and preview following it.
static void text_changed ()
{
	unsigned h = doc_hash (g_ed->content (), (unsigned) g_ed->len);
	if (h == g_edHash) return;
	g_edHash = h;
	NoteInfo *e = cur ();
	if (!e || g_bad) return;
	g_dirty = true;
	g_lastEdit = kapi_get_ticks ();
	int n = g_ed->len < NOTE_HEAD ? g_ed->len : NOTE_HEAD;
	notes_title (g_ed->content (), n, e->title, sizeof e->title);
	notes_preview (g_ed->content (), n, e->preview, sizeof e->preview);
	if (g_msgKind == 1) set_msg ("", 0, 0);			// ("Saved" no longer true)
	g_list->invalidate (true);
	g_info->invalidate (true);
	g_del->setDisabled (!e->saved && !g_ed->len);
}

// The current note written (when changed and not empty). -> false the write failed (the status says so)
static bool save_current ()
{
	NoteInfo *e = cur ();
	if (!e || g_bad || !g_dirty || g_ed->len == 0) return true;
	char file[NOTE_FILE]; snprintf (file, sizeof file, "%s", e->file);
	int r = notes_write (g_notes, file, g_ed->content (), g_ed->len, notes_now ());
	if (r != 0)
	{
		g_failed = true; g_failedAt = g_lastEdit;
		set_msg (r == -2 ? TR ("Not saved: the note is larger than 64 KB")
				 : TR ("Not saved: the card is full or read-only \xE2\x80\x94 trying again"), 2, 0);
		return false;
	}
	notes_save_ini (g_notes);
	g_dirty = false; g_failed = false;
	notes_sort (g_notes);
	g_sel = notes_find (g_notes, file);
	g_list->sel = g_sel; g_list->now = notes_now (); g_list->showSel ();
	set_msg (TR ("Saved"), 1, 0);
	sig_taken ();
	stickies_reload ();
	update_chrome ();
	return true;
}

// Leaving the current note (another one chosen, a new one, the window closing): saved; empty, it is not
// kept -- never written: forgotten; emptied by the user: to the Trash, silently (04 D3).
static void leave_current ()
{
	NoteInfo *e = cur ();
	if (!e) return;
	save_current ();
	if (!g_bad && g_ed->len == 0)
	{
		if (!g_notes.n[g_sel].saved) notes_forget (g_notes, g_sel);
		else if (notes_trash (g_notes, g_notes.n[g_sel].file)) { sig_taken (); stickies_reload (); }
	}
	g_sel = -1; g_dirty = false; g_failed = false;
}

// Row i into the editor.
static void load (int i)
{
	if (i < 0 || i >= g_notes.count) i = g_notes.count ? 0 : -1;
	g_sel = i; g_dirty = false; g_failed = false; g_bad = false;
	g_buf[0] = 0;
	if (i >= 0 && g_notes.n[i].saved)
	{
		int n = notes_read (g_notes.n[i].file, g_buf, sizeof g_buf);
		if (n < 0)
		{
			g_buf[0] = 0; g_bad = true;
			set_msg (n == -2 ? TR ("This note is larger than 64 KB: open it in the Text Editor (Ctrl+E)") : TR ("This note could not be read"), 2, MSG_TIME);
		}
	}
	g_ed->setContent (g_buf);
	g_ed->caret = 0; g_ed->anchor = -1; g_ed->top = 0; g_ed->left = 0; g_ed->leftPx = 0;
	g_ed->readonly = g_bad || i < 0;
	g_edHash = doc_hash (g_ed->content (), (unsigned) g_ed->len);
	if (g_msgKind == 1) set_msg ("", 0, 0);
	g_list->sel = g_sel; g_list->now = notes_now (); g_list->showSel ();
	update_chrome ();
}

// The note named `file` chosen (the one left first: saved, or not kept); none of that name: the first row.
static void select_file (const char *file)
{
	char f[NOTE_FILE]; snprintf (f, sizeof f, "%s", file ? file : "");
	NoteInfo *e = cur ();
	if (e && !strcmp (e->file, f)) return;
	leave_current ();
	load (f[0] ? notes_find (g_notes, f) : 0);
	if (g_sel < 0 && notes_add_new (g_notes, notes_now ()) == 0) load (0);
}
static void select_row (int r) { if (r >= 0 && r < g_notes.count && r != g_sel) select_file (g_notes.n[r].file); }

// ---- the commands ----------------------------------------------------------------------------------------
static void on_new ()
{
	NoteInfo *e = cur ();
	if (e && !e->saved && !g_ed->len) { g_ed->setFocus (); return; }	// (already on an empty new note)
	leave_current ();
	if (notes_add_new (g_notes, notes_now ()) != 0) { set_msg (TR ("Too many notes: delete some first"), 2, MSG_TIME); load (0); return; }
	load (0);
	g_ed->setFocus ();
	g_list->invalidate (true);
}

static void on_delete ()
{
	NoteInfo *e = cur ();
	if (!e || (!e->saved && !g_ed->len && !g_bad)) return;		// (a new empty note: nothing to throw away)
	if (!g_bad && g_dirty && !save_current ()) return;		// (the Trash gets the latest text)
	e = cur ();
	if (!e->saved) return;
	char file[NOTE_FILE]; snprintf (file, sizeof file, "%s", e->file);
	int at = g_sel;
	if (!notes_trash (g_notes, file)) { set_msg (TR ("The note could not be moved to the Trash"), 2, MSG_TIME); return; }
	g_sel = -1; g_dirty = false;
	sig_taken ();
	notify ("Notes", TR ("Note moved to the Trash"));
	stickies_reload ();
	if (g_notes.count == 0) { notes_add_new (g_notes, notes_now ()); load (0); g_ed->setFocus (); return; }
	load (at < g_notes.count ? at : g_notes.count - 1);
}

static void on_pin ()
{
	NoteInfo *e = cur ();
	if (!e) return;
	e->pinned = !e->pinned;
	if (e->saved) { notes_save_ini (g_notes); sig_taken (); stickies_reload (); }
	// Shown on the desktop while Stickies is on but not running: started (never autostart's line: 04 D2).
	if (e->pinned && g_cfg.stickies && !kapi_ipc_lookup (STICKIES_SERVICE)) lx_launch ("stickies", 0);
	update_chrome ();
}

static void set_colour (int c)
{
	NoteInfo *e = cur ();
	if (!e || c < 0 || c >= NC_COUNT) return;
	if (e->colour != c)
	{
		e->colour = c;
		if (e->saved) { notes_save_ini (g_notes); sig_taken (); stickies_reload (); }
	}
	update_chrome ();
}
static void on_yellow () { set_colour (NC_YELLOW); }
static void on_green ()  { set_colour (NC_GREEN); }
static void on_blue ()   { set_colour (NC_BLUE); }
static void on_pink ()   { set_colour (NC_PINK); }
static void on_purple () { set_colour (NC_PURPLE); }
static void on_grey ()   { set_colour (NC_GREY); }

static void on_cut ()        { g_ed->cut (); text_changed (); g_ed->setFocus (); }
static void on_copy ()       { g_ed->copy (); g_ed->setFocus (); }
static void on_select_all () { g_ed->selectAll (); g_ed->setFocus (); }
static void on_paste ()
{
	if (g_ed->readonly) return;
	static char clip[NOTE_MAX_BYTES + 1];
	int n = clip_get_text (clip, sizeof clip) ? (int) strlen (clip) : 0;
	int room = NOTE_MAX_BYTES - (g_ed->len - (g_ed->selEnd () - g_ed->selStart ()));
	g_ed->paste ();
	text_changed ();
	if (n > room) set_msg (TR ("This note is full (64 KB): the paste was cut."), 0, MSG_TIME);
	g_ed->setFocus ();
}
static void on_copy_note () { if (g_ed->len) clip_set_text_n (g_ed->content (), g_ed->len); }

// File > Open in Text Editor: the note saved, then its file in the Text Editor (tinypad).
static void on_open_editor ()
{
	NoteInfo *e = cur ();
	if (!e) return;
	save_current ();
	e = cur ();
	if (!e || !e->saved) return;
	char p[128]; snprintf (p, sizeof p, "%s/%s", NOTES_DIR, e->file);
	lx_launch ("tinypad", p);
}

static void on_view_stickies () { stickies_set_shown (!g_cfg.stickies); build_menu (); }

// The tool bar's buttons.
static void tb_new (Widget &)    { on_new (); }
static void tb_delete (Widget &) { on_delete (); }
static void tb_pin (Widget &)    { on_pin (); }
static void tb_colour (Widget &w) { set_colour (w.tag); }

// The list's callbacks.
static void list_pick (int r) { select_row (r); }
static void list_enter () { g_ed->setFocus (); g_list->invalidate (true); }

// ---- the menus (uikit::Menu: no check marks -- the labels say what an item will do, 04 D1) ----------------
static Menu g_menu;
static void build_menu ()
{
	NoteInfo *e = cur ();
	g_menu = Menu ();
	g_menu.menu (TR ("File"));
	g_menu.item (TR ("New Note"),            "^N", UK_CTRL ('N'), on_new);
	g_menu.separator ();
	g_menu.item (TR ("Open in Text Editor"), "^E", UK_CTRL ('E'), on_open_editor);
	g_menu.separator ();
	g_menu.item (TR ("Delete Note"),         "^D", UK_CTRL ('D'), on_delete);
	g_menu.menu (TR ("Edit"));
	g_menu.item (TR ("Cut"),                 "^X", UK_CTRL ('X'), on_cut);
	g_menu.item (TR ("Copy"),                "^C", UK_CTRL ('C'), on_copy);
	g_menu.item (TR ("Paste"),               "^V", UK_CTRL ('V'), on_paste);
	g_menu.separator ();
	g_menu.item (TR ("Select All"),          "^A", UK_CTRL ('A'), on_select_all);
	g_menu.item (TR ("Copy Note"),           "",   0,             on_copy_note);
	g_menu.menu (TR ("Note"));
	g_menu.item (e && e->pinned ? TR ("Unpin from Desktop") : TR ("Pin to Desktop"), "^P", UK_CTRL ('P'), on_pin);
	g_menu.separator ();
	static const MenuAction COL[NC_COUNT] = { on_yellow, on_green, on_blue, on_pink, on_purple, on_grey };
	for (int c = 0; c < NC_COUNT; c++) g_menu.item (TR (notes_colour_label (c)), "", 0, COL[c]);
	g_menu.menu (TR ("View"));
	g_menu.item (g_cfg.stickies ? TR ("Hide Stickies from the Desktop") : TR ("Show Stickies on the Desktop"), "", 0, on_view_stickies);
	g_menu.publish ();
}

// Everything that shows the current note's state: the tool bar, the editor's paper, the info and status
// lines, the list, the menus' labels.
static void update_chrome ()
{
	NoteInfo *e = cur ();
	int c = e ? e->colour : NC_YELLOW;
	g_pin->setOn (e && e->pinned);
	g_pin->setDisabled (!e);
	for (int k = 0; k < NC_COUNT; k++) g_colBtn[k]->setOn (k == c);
	g_del->setDisabled (!e || (!e->saved && !g_ed->len && !g_bad));
	unsigned sheet = paper_sheet (c);
	g_ed->setColors (sheet, NOTE_INK, C_ACCENT, uk_mix (sheet, C_ACCENT, 90));
	g_list->sel = g_sel;
	g_list->invalidate (true);
	g_info->invalidate (true);
	g_status->invalidate (true);
	build_menu ();
}

// ---- dropped files: new notes ----------------------------------------------------------------------------
static bool text_name (const char *p)
{
	int n = (int) strlen (p);
	return (n > 4 && !fs_ci_cmp (p + n - 4, ".txt")) || (n > 3 && !fs_ci_cmp (p + n - 3, ".md"));
}
static void import_files (const char *data)
{
	char big[400] = "", other[400] = "", last[NOTE_FILE] = "", prev[NOTE_FILE] = "";
	int nBig = 0, nOther = 0, done = 0;
	if (NoteInfo *e = cur ()) snprintf (prev, sizeof prev, "%s", e->file);
	leave_current ();
	for (const char *p = data; *p; )
	{
		char path[FS_PATHL]; int n = 0;
		while (p[n] && p[n] != '\n') n++;
		snprintf (path, sizeof path, "%.*s", n < (int) sizeof path - 1 ? n : (int) sizeof path - 1, p);
		p += n; if (*p == '\n') p++;
		if (!path[0]) continue;
		const char *name = fs_basename (path);
		int len = text_name (path) ? notes_read (path, g_buf, sizeof g_buf) : -1;
		char *list = len == -2 ? big : other; int *cnt = len == -2 ? &nBig : &nOther;
		if (len < 0) { snprintf (list + strlen (list), 400 - strlen (list), "%s\xE2\x80\x9C%s\xE2\x80\x9D", *cnt ? ", " : "", name); (*cnt)++; continue; }
		if (notes_add_new (g_notes, notes_now ()) != 0) break;
		char file[NOTE_FILE]; snprintf (file, sizeof file, "%s", g_notes.n[0].file);
		if (notes_write (g_notes, file, g_buf, len, notes_now ()) != 0)
		{ notes_forget (g_notes, 0); set_msg (TR ("Not saved: the card is full or read-only"), 2, MSG_TIME); break; }
		snprintf (last, sizeof last, "%s", file); done++;
	}
	if (done) { notes_save_ini (g_notes); sig_taken (); stickies_reload (); }
	load (notes_find (g_notes, last[0] ? last : prev));
	if (g_sel < 0 && notes_add_new (g_notes, notes_now ()) == 0) load (0);
	if (nBig || nOther)
	{
		char t[1000];
		if (nBig == 1 && !nOther)
			snprintf (t, sizeof t, TR ("%s is larger than 64 KB, the most a note can hold.\nIt was not imported; open it in the Text Editor instead."), big);
		else
		{
			int k = 0;
			if (nBig) k += snprintf (t + k, sizeof t - (size_t) k, TR ("Larger than 64 KB, the most a note can hold: %s.\n"), big);
			if (nOther) snprintf (t + k, sizeof t - (size_t) k, TR ("Not a text file (.txt, .md) or unreadable: %s.\n"), other);
			strncat (t, TR ("They were not imported."), sizeof t - strlen (t) - 1);
		}
		ft_messagebox (TR ("Import a note"), t, MB_OK);
	}
}

// ---- the window ------------------------------------------------------------------------------------------
class NotesRoot : public Root
{
public:
	NotesRoot (int w, int h) : Root (w, h, "Notes") {}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type == DND_FILES) { import_files (data); return; }
		if (type == DND_TEXT && !g_ed->readonly) { g_ed->insertText (data); text_changed (); g_ed->setFocus (); }
	}
	void onTick () override
	{
		unsigned now = kapi_get_ticks ();
		if (g_msgUntil && (int) (now - g_msgUntil) >= 0) set_msg ("", 0, 0);
		// the save after a pause in typing (after a failure: at the next pause, not each second)
		if (g_dirty && now - g_lastEdit >= AUTOSAVE && !(g_failed && g_failedAt == g_lastEdit)) save_current ();
		// the messages to the "notes" service: a note to open (Stickies' clicks, a second Notes)
		int from = 0, type = 0, n;
		char m[520];
		while ((n = kapi_mailbox_recv (&from, &type, m, sizeof m - 1, 0)) >= 0)
		{
			m[n < (int) sizeof m - 1 ? n : (int) sizeof m - 1] = 0;
			if (type != NOTES_MSG_OPEN || !m[0]) continue;
			if (notes_find (g_notes, m) < 0) { save_current (); rescan (); }
			if (notes_find (g_notes, m) >= 0) select_file (g_notes.n[notes_find (g_notes, m)].file);
		}
		// notes changed by another program (the Text Editor, FTP, a PC): every few seconds while idle
		if (!g_dirty && now - g_sigT >= RESCAN)
		{
			unsigned s = folder_sig ();
			g_sigT = now;
			if (s != g_sig) rescan ();
		}
	}
	// SD:/Notes read again; the selection (and an empty new note) kept; the current note's text reloaded
	// if it changed on the card (only while it is not being edited).
	static void rescan ()
	{
		NoteInfo keep; bool fresh = false;
		if (NoteInfo *e = cur ()) { keep = *e; fresh = !e->saved; }
		else memset (&keep, 0, sizeof keep);
		notes_scan (g_notes);
		g_sig = folder_sig ();
		if (fresh && g_notes.count < NOTES_MAX)
		{
			memmove (&g_notes.n[1], &g_notes.n[0], sizeof g_notes.n[0] * (size_t) g_notes.count);
			g_notes.n[0] = keep; g_notes.count++;
		}
		int i = keep.file[0] ? notes_find (g_notes, keep.file) : -1;
		if (i < 0) { load (0); if (g_sel < 0 && notes_add_new (g_notes, notes_now ()) == 0) load (0); return; }
		g_sel = i;
		if (!fresh && !g_dirty)
		{
			int n = notes_read (g_notes.n[i].file, g_buf, sizeof g_buf);
			if (n >= 0 && doc_hash (g_buf, (unsigned) n) != g_edHash) { load (i); return; }
		}
		g_list->sel = g_sel; g_list->showSel ();
		update_chrome ();
	}
};

// The colour buttons' dots (the tool bar's ToolIconFn).
static void colour_icon (Canvas &cv, int id, int x, int y, int size, unsigned, bool) { notes_draw_dot (cv, x + 2, y + 2, size - 4, id); }

int main (void)
{
	// One Notes at a time: one already running (the "notes" service) is sent this one's argument (the note to
	// open; empty: only come forward) and raised, and this one ends here.
	int other = kapi_ipc_lookup (NOTES_SERVICE);
	if (other > 0)
	{
		char a[160] = "";
		kapi_get_args (a, sizeof a);
		kapi_mailbox_send (other, NOTES_MSG_OPEN, a, (unsigned) strlen (a) + 1);
		kapi_raise_app (NOTES_SERVICE);
		return 0;
	}
	kapi_ipc_register (NOTES_SERVICE);			// (failed: Notes runs alone, nothing forwarded to it)
	ft_uikit_install ("DejaVu Sans", 13);			// (before the widgets; false: the bitmap font)
	uk_lang_init ();					// the words in the system's language (before the widgets)
	notes_cfg_load (g_cfg);

	int w = g_cfg.width < MIN_W ? MIN_W : g_cfg.width > 2000 ? 2000 : g_cfg.width;
	int h = g_cfg.height < MIN_H ? MIN_H : g_cfg.height > 1400 ? 1400 : g_cfg.height;
	NotesRoot root (w, h);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);
	root.setMinSize (MIN_W, MIN_H);

	// the tool bar: New Note (the accent pill), Delete, Pin, the six colours
	ToolBar *tb = new ToolBar (0, 0, w, TB_H); tb->line = true;
	tb->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	root.addChild (tb);
	ToolButton *nb = (new ToolButton (0, 30, TR ("New note (Ctrl+N)"), tb_new))->setGlyph (WKT_PLUS)->setText (TR ("New Note"))->fitWidth ();
	nb->filled = true; nb->setOn (true);
	tb->add (nb, 6);
	tb->sep ();
	g_del = (new ToolButton (0, 30, TR ("Delete note: to the Trash (Ctrl+D)"), tb_delete))->setGlyph (WKT_TRASH)->setText (TR ("Delete"))->fitWidth ();
	tb->add (g_del, 4);
	g_pin = (new ToolButton (0, 30, TR ("Pin to the desktop (Ctrl+P)"), tb_pin))->setGlyph (WKT_PIN)->setText (TR ("Pin"))->setToggle (true, false)->fitWidth ();
	tb->add (g_pin, 2);
	tb->sep ();
	for (int c = 0; c < NC_COUNT; c++)
	{
		g_colBtn[c] = (new ToolButton (26, 26, TR (notes_colour_label (c)), tb_colour))->setIcon (colour_icon, c)->setToggle (true, c == NC_YELLOW);
		g_colBtn[c]->iconSize = 18; g_colBtn[c]->tag = c;
		tb->add (g_colBtn[c], c ? 0 : 2);
	}

	// the body: the list | the info line over the text
	int bh = h - TB_H - ST_H, split = g_cfg.split < 180 ? 180 : g_cfg.split > w - 260 ? w - 260 : g_cfg.split;
	g_split = new HSplitter (0, TB_H, w, bh, split, C_FIELD);
	g_split->minA = 180; g_split->minB = 260;
	g_split->anchor = ANCHOR_FILL;
	root.addChild (g_split);
	g_list = new NoteList (0, 0, split, bh, &g_notes);
	g_list->onPick = list_pick; g_list->onEnter = list_enter; g_list->onDelete = on_delete;
	Panel *pane = new Panel (0, 0, w - split, bh, C_FIELD);
	g_info = new InfoLine (0, 0, w - split, INFO_H);
	g_info->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	pane->addChild (g_info);
	g_ed = new NoteEdit (10, INFO_H + 6, w - split - 20, bh - INFO_H - 16, NOTE_MAX_BYTES + 1);
	g_ed->face = new FtTextFace;
	if (!g_ed->face->open ("DejaVu Sans", 15)) { delete g_ed->face; g_ed->face = 0; }
	g_ed->Widget::anchor = ANCHOR_FILL;			// (Textarea's own `anchor` is its selection's)
	pane->addChild (g_ed);
	g_split->setPanes (g_list, pane);
	g_status = new StatusLine (0, h - ST_H, w, ST_H);
	g_status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.addChild (g_status);

	// the notes, and the one to show: the argument's, else the one left last time, else the newest; none:
	// a new empty note (written only once something is typed)
	notes_scan (g_notes);
	sig_taken ();
	char arg[160] = "";
	kapi_get_args (arg, sizeof arg);
	int i = arg[0] ? notes_find (g_notes, arg) : -1;
	if (i < 0 && g_cfg.last[0]) i = notes_find (g_notes, g_cfg.last);
	if (i < 0 && g_notes.count) i = 0;
	if (i < 0 && notes_add_new (g_notes, notes_now ()) == 0) i = 0;
	load (i);
	NoteInfo *e = cur ();
	if (e && !e->saved) g_ed->setFocus (); else ((Widget *) g_list)->setFocus ();
	root.fitWorkArea ();

	root.run ();

	// The window closed: the note saved -- a save that fails puts its text on the clipboard and says so
	// (nothing typed is lost silently, 04 D8) -- or, empty, not kept; the settings kept.
	e = cur ();
	if (e && !g_bad && g_dirty && g_ed->len && !save_current ())
	{
		clip_set_text_n (g_ed->content (), g_ed->len);
		char t[200]; snprintf (t, sizeof t, TR ("Notes could not save \xE2\x80\x9C%s\xE2\x80\x9D: its text is on the clipboard"), e->title[0] ? e->title : TR ("New Note"));
		notify ("Notes", t);
		g_dirty = false;
	}
	e = cur ();
	snprintf (g_cfg.last, sizeof g_cfg.last, "%s", e && e->saved && (g_ed->len || g_bad) ? e->file : "");
	leave_current ();
	if (!root.maximised ()) { g_cfg.width = root.width; g_cfg.height = root.height; }
	g_cfg.split = g_split->split;
	notes_cfg_save (g_cfg);
	return 0;
}
