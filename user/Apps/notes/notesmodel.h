//
// notesmodel.h -- the notes of Notes and Stickies, without any window: the folder SD:/Notes (one .txt a note,
// the first line its title), SD:/Notes/notes.ini (each note's colour, pin and last change), the names of new
// notes, the dates the list shows, Notes' config.ini. Shared by user/Apps/notes (which writes) and
// user/Apps/stickies (which only reads), linked into both (user/Makefile: FT_EXTRA_<app>); unit-tested on the
// PC by tools/tests/run_notes_test.sh (tools/tests/notes/model_test.cpp).
//
// The files (AutoDev round 1, 02-product-analysis.md section 5):
//   SD:/Notes/note-YYYYMMDD-HHMMSS[-k].txt   a note: its text exactly, UTF-8, '\n' lines (a "\r\n" read is
//                                           written back as '\n'), at most NOTE_MAX_BYTES; its name never
//                                           changes (the creation time, local)
//   SD:/Notes/notes.ini                      [<file name>] colour = yellow|green|blue|pink|purple|grey,
//                                           pinned = 0|1, modified = YYYYMMDDHHMMSS (local): the list's order
//                                           and date. A .txt without a section: yellow, not pinned, its file's
//                                           time; a section without a file: dropped at the next write.
//   SD:/apps/notes.app/config.ini            stickies, last, width, height, split (plain "key = value" lines)
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
#ifndef NOTES_NOTESMODEL_H
#define NOTES_NOTESMODEL_H

#define NOTES_DIR      "SD:/Notes"
#define NOTES_INI      "SD:/Notes/notes.ini"
#define NOTES_CFG      "SD:/apps/notes.app/config.ini"
#define STICKIES_CFG   "SD:/apps/stickies.app/config.ini"
#define NOTE_MAX_BYTES 65536			// a note's text, at most (a read buffer: NOTE_MAX_BYTES + 1)
#define NOTES_MAX      512			// the notes listed (the newest ones)
#define NOTE_FILE      72			// a note's file name, its 0 included
#define NOTE_TITLE     96			// a title / a preview kept, its 0 included
#define NOTE_HEAD      2048			// the bytes of each note read by notes_scan (its title, its preview)
#define NOTE_INK       0x002B2925u		// the text on every paper (04-ux-design.md section 7)

enum { NC_YELLOW, NC_GREEN, NC_BLUE, NC_PINK, NC_PURPLE, NC_GREY, NC_COUNT };

struct NoteInfo
{
	char      file[NOTE_FILE];		// the bare name ("note-20260928-091500.txt")
	char      title[NOTE_TITLE];		// the first non-blank line ("" while empty: the list shows "New Note")
	char      preview[NOTE_TITLE];		// the next non-blank line, "" none
	int       colour;			// NC_*
	bool      pinned;			// shown on the desktop by Stickies
	bool      saved;			// its file is on the card (false: a new note not written yet)
	long long modified;			// YYYYMMDDHHMMSS local, 0 unknown (sorted last, no date shown)
};
struct Notes
{
	NoteInfo n[NOTES_MAX];
	int      count;				// the notes in n (newest first after notes_scan / notes_sort)
	int      total;				// the .txt notes notes_scan found (> count: only the NOTES_MAX newest kept)
};

// ---- the folder ------------------------------------------------------------------------------------------
// The *.txt of NOTES_DIR (folders, notes.ini and other files skipped; names longer than NOTE_FILE - 1 skipped)
// with their notes.ini sections; each note's first NOTE_HEAD bytes read for its title and preview; sorted
// newest first. -> count (0: no folder, or no note)
int  notes_scan (Notes &s);
// Newest first (modified descending, then the name descending: the later of two notes made the same second).
void notes_sort (Notes &s);
// A note's text: `file` a bare name (in NOTES_DIR) or a path (a dropped file); BOM / UTF-16 fixed (FileKit's
// fs_text_fix), "\r\n" and a lone '\r' made '\n', a 0 ending it. -> its length; -1 unreadable / missing,
// -2 larger than NOTE_MAX_BYTES (never cut), -3 cap too small for it (cap NOTE_MAX_BYTES + 1 always fits)
int  notes_read (const char *file, char *buf, int cap);
// Write a note's text (len bytes, at most NOTE_MAX_BYTES) as file (a bare name) in NOTES_DIR (made if missing);
// its entry in s (added at the top when absent) gets modified = now, saved, its title and preview.
// -> 0 written, -1 the write failed (card full / read-only), -2 too large
int  notes_write (Notes &s, const char *file, const char *text, int len, long long now);
// notes.ini written whole: the comment line, then one section a SAVED note of s, in the list's order.
bool notes_save_ini (const Notes &s);
// A new note: a free name (notes_new_name) at the top of s, yellow, not pinned, not saved, modified = now.
// -> its index (0), -1 the list full
int  notes_add_new (Notes &s, long long now);
// Entry i out of the list (nothing on the card changes).
void notes_forget (Notes &s, int i);
// The note `file` (a bare name or a path) to the Trash (SystemKit's trash_move), out of s, notes.ini written.
// A note never saved is only forgotten. -> true done
bool notes_trash (Notes &s, const char *file);
// A free name for a note made at `now`: note-YYYYMMDD-HHMMSS.txt, else ...-2.txt, -3... -- free on the card
// AND not taken in s (a new note not written yet holds its name). -> false none (out too small)
bool notes_new_name (const Notes &s, char *out, int cap, long long now);
// The index of the note `file` (a bare name or a path; case ignored, as FAT) in s, -1 none.
int  notes_find (const Notes &s, const char *file);
// The `max` pinned notes most recently changed, newest first: their indexes into idx. -> how many
int  notes_pinned (const Notes &s, int *idx, int max);

// ---- text ------------------------------------------------------------------------------------------------
// The title: the first line holding more than blanks, trimmed, cut to cap - 1 bytes at a UTF-8 character.
void notes_title (const char *text, int len, char *out, int cap);
// The preview: the next such line after the title's ("" none).
void notes_preview (const char *text, int len, char *out, int cap);

// ---- time ------------------------------------------------------------------------------------------------
// Now, YYYYMMDDHHMMSS local (kapi_get_datetime; before the clock is set: the time since boot, 1970...).
long long notes_now (void);
// A file's modification time, local (kapi_path_stat's UTC mtime + kapi_clock_info's tz_minutes), 0 unknown.
long long notes_file_time (const char *path);
// The list's date of a note changed at `modified`, seen at `now` (04-ux-design.md 2.2): today "09:15", the day
// before "Yesterday", within the week "Fri", this year "15 Sep", before "15/09/2025", modified 0 "".
void notes_date_label (long long modified, long long now, char *out, int cap);

// ---- colours ---------------------------------------------------------------------------------------------
const char *notes_colour_name (int c);		// "yellow"... (notes.ini's word); out of range: "yellow"
const char *notes_colour_label (int c);		// "Yellow"... (the menus, the tips)
int  notes_colour_parse (const char *v);	// a notes.ini word (case ignored) -> NC_*; unknown: NC_YELLOW
unsigned notes_colour_paper (int c);		// 0x00RRGGBB: the card's paper
unsigned notes_colour_dot (int c);		// the dot / the card's band

// ---- Notes' settings (SD:/apps/notes.app/config.ini; Stickies reads `stickies`) --------------------------
struct NotesCfg { int stickies; char last[NOTE_FILE]; int width, height, split; };
void notes_cfg_load (NotesCfg &c);		// the defaults first: 1, "", 760, 480, 250
bool notes_cfg_save (const NotesCfg &c);	// plain lines, no [section]

#endif
