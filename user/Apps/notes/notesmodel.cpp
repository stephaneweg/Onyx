//
// notesmodel.cpp -- the notes of Notes and Stickies (notesmodel.h): the folder, notes.ini, names, dates,
// colours, Notes' config.ini. No window here: unit-tested on the PC (tools/tests/run_notes_test.sh).
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
#include "Apps/notes/notesmodel.h"
#include "appkit/appkit.h"
#include "filekit/filekit.h"
#include "systemkit/systemkit.h"
#include <stdio.h>
#include <string.h>

// ---- small helpers ---------------------------------------------------------------------------------------
static void copy_n (char *d, int cap, const char *s, int n)		// n bytes of s (cut to cap - 1), ended
{
	if (cap <= 0) return;
	if (n > cap - 1) n = cap - 1;
	if (n < 0) n = 0;
	memcpy (d, s, (size_t) n);
	d[n] = 0;
}
static bool ends_txt (const char *n)
{
	int l = (int) strlen (n);
	return l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == 't' && (n[l - 2] | 32) == 'x' && (n[l - 1] | 32) == 't';
}
static bool is_path (const char *f) { return strchr (f, ':') || strchr (f, '/'); }
static void note_path (char *out, int cap, const char *file)		// a bare name -> NOTES_DIR/name
{
	if (is_path (file)) fs_copy (out, file, cap);
	else fs_join (out, cap, NOTES_DIR, file);
}
// A whole small file into buf (cap - 1 bytes at most, ended by a 0) -> its length, -1 none.
static int read_small (const char *path, char *buf, int cap)
{
	void *f = kapi_open (path);
	if (!f) return -1;
	int n = 0;
	while (n < cap - 1) { int k = kapi_read (f, buf + n, (unsigned) (cap - 1 - n)); if (k <= 0) break; n += k; }
	kapi_close (f);
	buf[n] = 0;
	return n;
}
// The text's UTF-8 cut: the largest k <= n that does not split a character of s.
static int u8_cut (const char *s, int n)
{
	int k = n;
	while (k > 0 && ((unsigned char) s[k] & 0xC0) == 0x80) k--;
	return k;
}
// Line l of the text (its non-blank lines counted from 0), trimmed, into out.
static void nth_line (const char *t, int len, int l, char *out, int cap)
{
	out[0] = 0;
	for (int i = 0; i < len; )
	{
		int s = i;
		while (i < len && t[i] != '\n') i++;
		int e = i; i++;
		while (s < e && (t[s] == ' ' || t[s] == '\t' || t[s] == '\r')) s++;
		while (e > s && (t[e - 1] == ' ' || t[e - 1] == '\t' || t[e - 1] == '\r')) e--;
		if (e == s) continue;
		if (l-- > 0) continue;
		int n = e - s < cap - 1 ? e - s : cap - 1;
		if (n < e - s) n = u8_cut (t + s, n);			// (cut: not inside a character)
		copy_n (out, cap, t + s, n);
		return;
	}
}

// ---- notes.ini -------------------------------------------------------------------------------------------
// The tiny .ini reader (AppKit's holds 64 entries: too few here): calls fn (section, key, value) for each
// "key = value" line; '#' and ';' lines are comments, so is a " ;..." after a value.
typedef void (*IniFn) (void *ctx, const char *sec, const char *key, const char *val);
static void ini_each (char *b, int n, IniFn fn, void *ctx)
{
	char sec[NOTE_FILE + 8] = "";
	for (int i = 0; i < n; )
	{
		int s = i;
		while (i < n && b[i] != '\n') i++;
		int e = i; i++;
		while (s < e && (b[s] == ' ' || b[s] == '\t')) s++;
		while (e > s && (b[e - 1] == ' ' || b[e - 1] == '\t' || b[e - 1] == '\r')) e--;
		if (e == s || b[s] == '#' || b[s] == ';') continue;
		if (b[s] == '[')
		{
			int k = s + 1; while (k < e && b[k] != ']') k++;
			copy_n (sec, sizeof sec, b + s + 1, k - s - 1);
			continue;
		}
		int eq = s; while (eq < e && b[eq] != '=') eq++;
		if (eq == e) continue;
		int ke = eq; while (ke > s && (b[ke - 1] == ' ' || b[ke - 1] == '\t')) ke--;
		int vs = eq + 1; while (vs < e && (b[vs] == ' ' || b[vs] == '\t')) vs++;
		int ve = vs; while (ve < e && !(b[ve] == ';' && ve > vs && (b[ve - 1] == ' ' || b[ve - 1] == '\t'))) ve++;
		while (ve > vs && (b[ve - 1] == ' ' || b[ve - 1] == '\t')) ve--;
		char key[32], val[NOTE_FILE + 8];
		copy_n (key, sizeof key, b + s, ke - s);
		copy_n (val, sizeof val, b + vs, ve - vs);
		fn (ctx, sec, key, val);
	}
}
static long long to_ll (const char *v)
{
	long long r = 0; int i = 0;
	while (v[i] == ' ') i++;
	for (; v[i] >= '0' && v[i] <= '9' && r < 100000000000000000LL; i++) r = r * 10 + (v[i] - '0');
	return r;
}
static int to_int (const char *v, int def)
{
	int i = 0, neg = 0, r = 0;
	if (v[i] == '-') { neg = 1; i++; }
	if (v[i] < '0' || v[i] > '9') return def;
	for (; v[i] >= '0' && v[i] <= '9' && r < 100000000; i++) r = r * 10 + (v[i] - '0');
	return neg ? -r : r;
}

struct IniSec { char file[NOTE_FILE]; int colour; bool pinned; long long modified; };
static IniSec g_sec[NOTES_MAX * 2];
static int g_nsec;
static char g_iniBuf[NOTES_MAX * 2 * 160];
static void on_sec (void *, const char *sec, const char *key, const char *val)
{
	if (!sec[0]) return;
	int i = g_nsec - 1;
	if (i < 0 || fs_ci_cmp (g_sec[i].file, sec))			// (a new section)
	{
		if (g_nsec >= NOTES_MAX * 2 || (int) strlen (sec) >= NOTE_FILE) return;
		i = g_nsec++;
		fs_copy (g_sec[i].file, sec, NOTE_FILE);
		g_sec[i].colour = NC_YELLOW; g_sec[i].pinned = false; g_sec[i].modified = 0;
	}
	if (!strcmp (key, "colour") || !strcmp (key, "color")) g_sec[i].colour = notes_colour_parse (val);
	else if (!strcmp (key, "pinned")) g_sec[i].pinned = to_int (val, 0) != 0;
	else if (!strcmp (key, "modified")) g_sec[i].modified = to_ll (val);
}
static void ini_load (void)
{
	g_nsec = 0;
	int n = read_small (NOTES_INI, g_iniBuf, sizeof g_iniBuf);
	if (n > 0) ini_each (g_iniBuf, n, on_sec, 0);
}
static const IniSec *ini_find (const char *file)
{
	for (int i = 0; i < g_nsec; i++) if (!fs_ci_cmp (g_sec[i].file, file)) return &g_sec[i];
	return 0;
}

bool notes_save_ini (const Notes &s)
{
	static const char head[] = "# Notes -- each note's colour, pin and last change (the notes are the .txt files beside this one)\n";
	int n = 0, cap = (int) sizeof g_iniBuf;
	n += snprintf (g_iniBuf + n, (size_t) (cap - n), "%s", head);
	for (int i = 0; i < s.count && n < cap - 1; i++)
	{
		const NoteInfo &e = s.n[i];
		if (!e.saved) continue;
		n += snprintf (g_iniBuf + n, (size_t) (cap - n), "\n[%s]\ncolour = %s\npinned = %d\nmodified = %lld\n",
			       e.file, notes_colour_name (e.colour), e.pinned ? 1 : 0, e.modified);
	}
	if (n >= cap) return false;
	kapi_mkdir (NOTES_DIR);
	return kapi_save_file (NOTES_INI, g_iniBuf, (unsigned) n) == n;
}

// ---- the folder ------------------------------------------------------------------------------------------
static bool newer (const NoteInfo &a, const NoteInfo &b)			// a before b in the list
{
	if (a.modified != b.modified) return a.modified > b.modified;
	return strcmp (a.file, b.file) > 0;
}
void notes_sort (Notes &s)
{
	for (int i = 1; i < s.count; i++)					// (insertion: stable, a few hundred)
	{
		NoteInfo t = s.n[i];
		int j = i;
		while (j > 0 && newer (t, s.n[j - 1])) { s.n[j] = s.n[j - 1]; j--; }
		s.n[j] = t;
	}
}
static void set_text_info (NoteInfo &e, const char *t, int len)
{
	notes_title (t, len, e.title, sizeof e.title);
	notes_preview (t, len, e.preview, sizeof e.preview);
}
int notes_scan (Notes &s)
{
	s.count = 0; s.total = 0;
	ini_load ();
	void *d = kapi_opendir (NOTES_DIR);
	if (!d) return 0;
	struct kapi_dirent de;
	while (kapi_readdir (d, &de) == 1)
	{
		if (de.is_dir || !ends_txt (de.name) || (int) strlen (de.name) >= NOTE_FILE) continue;
		s.total++;
		NoteInfo e;
		memset (&e, 0, sizeof e);
		fs_copy (e.file, de.name, NOTE_FILE);
		e.saved = true;
		if (const IniSec *k = ini_find (de.name)) { e.colour = k->colour; e.pinned = k->pinned; e.modified = k->modified; }
		else
		{
			char p[FS_PATHL]; note_path (p, sizeof p, de.name);
			e.colour = NC_YELLOW; e.pinned = false; e.modified = notes_file_time (p);
		}
		if (s.count < NOTES_MAX) { s.n[s.count++] = e; continue; }
		int old = 0;							// full: the oldest kept gives way
		for (int i = 1; i < s.count; i++) if (newer (s.n[old], s.n[i])) old = i;
		if (newer (e, s.n[old])) s.n[old] = e;
	}
	kapi_closedir (d);
	static char head[NOTE_HEAD + 1];
	for (int i = 0; i < s.count; i++)
	{
		char p[FS_PATHL]; note_path (p, sizeof p, s.n[i].file);
		int n = read_small (p, head, sizeof head);
		if (n < 0) n = 0;
		n = fs_text_fix (head, n);
		head[n] = 0;
		set_text_info (s.n[i], head, n);
	}
	notes_sort (s);
	return s.count;
}

int notes_read (const char *file, char *buf, int cap)
{
	char p[FS_PATHL]; note_path (p, sizeof p, file);
	void *f = kapi_open (p);
	if (!f) return -1;
	unsigned sz = kapi_fsize (f);
	if (sz > 2u * NOTE_MAX_BYTES + 2u) { kapi_close (f); return -2; }	// (even UTF-16 would be too large)
	char *tmp = (int) sz < cap ? buf : new char[sz + 1];
	if (!tmp) { kapi_close (f); return -3; }
	int n = 0;
	while (n < (int) sz) { int k = kapi_read (f, tmp + n, sz - (unsigned) n); if (k <= 0) break; n += k; }
	kapi_close (f);
	tmp[n] = 0;
	n = fs_text_fix (tmp, n);
	int o = 0;
	for (int i = 0; i < n; i++)						// "\r\n", a lone '\r' -> '\n'
	{
		if (tmp[i] == '\r') { tmp[o++] = '\n'; if (i + 1 < n && tmp[i + 1] == '\n') i++; }
		else tmp[o++] = tmp[i];
	}
	int r = o > NOTE_MAX_BYTES ? -2 : o >= cap ? -3 : o;
	if (r >= 0) { if (tmp != buf) memcpy (buf, tmp, (size_t) o); buf[o] = 0; }
	if (tmp != buf) delete [] tmp;
	return r;
}

int notes_write (Notes &s, const char *file, const char *text, int len, long long now)
{
	if (len > NOTE_MAX_BYTES) return -2;
	if (len < 0) len = 0;
	char p[FS_PATHL]; note_path (p, sizeof p, file);
	kapi_mkdir (NOTES_DIR);
	if (kapi_save_file (p, text, (unsigned) len) != len) return -1;
	const char *bare = fs_basename (p);
	int i = notes_find (s, bare);
	if (i < 0)
	{
		if (s.count >= NOTES_MAX) return 0;				// (written; the list full)
		memmove (&s.n[1], &s.n[0], sizeof s.n[0] * (size_t) s.count);
		s.count++; i = 0;
		memset (&s.n[0], 0, sizeof s.n[0]);
		fs_copy (s.n[0].file, bare, NOTE_FILE);
		s.n[0].colour = NC_YELLOW;
	}
	NoteInfo &e = s.n[i];
	e.modified = now; e.saved = true;
	set_text_info (e, text, len < NOTE_HEAD ? len : u8_cut (text, NOTE_HEAD));
	return 0;
}

int notes_add_new (Notes &s, long long now)
{
	if (s.count >= NOTES_MAX) return -1;
	NoteInfo e;
	memset (&e, 0, sizeof e);
	if (!notes_new_name (s, e.file, sizeof e.file, now)) return -1;
	e.colour = NC_YELLOW; e.modified = now;
	memmove (&s.n[1], &s.n[0], sizeof s.n[0] * (size_t) s.count);
	s.n[0] = e; s.count++;
	return 0;
}

void notes_forget (Notes &s, int i)
{
	if (i < 0 || i >= s.count) return;
	memmove (&s.n[i], &s.n[i + 1], sizeof s.n[0] * (size_t) (s.count - i - 1));
	s.count--;
}

bool notes_trash (Notes &s, const char *file)
{
	int i = notes_find (s, file);
	if (i >= 0 && !s.n[i].saved) { notes_forget (s, i); return true; }
	char p[FS_PATHL]; note_path (p, sizeof p, i >= 0 ? s.n[i].file : file);
	if (!trash_move (p)) return false;
	if (i >= 0) notes_forget (s, i);
	notes_save_ini (s);
	return true;
}

bool notes_new_name (const Notes &s, char *out, int cap, long long now)
{
	if (now < 0) now = 0;
	int y = (int) (now / 10000000000LL), mo = (int) (now / 100000000 % 100), d = (int) (now / 1000000 % 100);
	int h = (int) (now / 10000 % 100), mi = (int) (now / 100 % 100), se = (int) (now % 100);
	for (int k = 1; k < 1000; k++)
	{
		char name[NOTE_FILE];
		if (k == 1) snprintf (name, sizeof name, "note-%04d%02d%02d-%02d%02d%02d.txt", y, mo, d, h, mi, se);
		else snprintf (name, sizeof name, "note-%04d%02d%02d-%02d%02d%02d-%d.txt", y, mo, d, h, mi, se, k);
		char p[FS_PATHL]; fs_join (p, sizeof p, NOTES_DIR, name);
		if (notes_find (s, name) >= 0 || fs_exists (p)) continue;
		if ((int) strlen (name) >= cap) return false;
		fs_copy (out, name, cap);
		return true;
	}
	return false;
}

int notes_find (const Notes &s, const char *file)
{
	if (!file) return -1;
	const char *b = is_path (file) ? fs_basename (file) : file;
	for (int i = 0; i < s.count; i++) if (!fs_ci_cmp (s.n[i].file, b)) return i;
	return -1;
}

int notes_pinned (const Notes &s, int *idx, int max)
{
	int k = 0;
	for (int i = 0; i < s.count; i++)
	{
		if (!s.n[i].pinned || !s.n[i].saved) continue;
		int p = 0;							// (its place among the top ones, kept ordered)
		while (p < k && !newer (s.n[i], s.n[idx[p]])) p++;
		if (p >= max) continue;
		int last = k < max ? k++ : max - 1;
		for (int j = last; j > p; j--) idx[j] = idx[j - 1];
		idx[p] = i;
	}
	return k;
}

// ---- text ------------------------------------------------------------------------------------------------
void notes_title (const char *text, int len, char *out, int cap) { if (cap > 0) nth_line (text ? text : "", text ? len : 0, 0, out, cap); }
void notes_preview (const char *text, int len, char *out, int cap) { if (cap > 0) nth_line (text ? text : "", text ? len : 0, 1, out, cap); }

// ---- time ------------------------------------------------------------------------------------------------
static long long days_from_civil (long long y, int m, int d)		// days since 1970-01-01 (H. Hinnant's)
{
	y -= m <= 2;
	long long era = (y >= 0 ? y : y - 399) / 400;
	long long yoe = y - era * 400;
	long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}
static long long stamp_of_epoch (long long t)				// seconds since 1970 -> YYYYMMDDHHMMSS
{
	long long z = (t >= 0 ? t : t - 86399) / 86400, sec = t - z * 86400;
	z += 719468;
	long long era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
	long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	long long d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
	y += m <= 2;
	return ((((y * 100 + m) * 100 + d) * 100 + sec / 3600) * 100 + sec / 60 % 60) * 100 + sec % 60;
}
long long notes_now (void)
{
	int y = 1970, mo = 1, d = 1, h = 0, mi = 0, se = 0;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, &se);
	return (((((long long) y * 100 + mo) * 100 + d) * 100 + h) * 100 + mi) * 100 + se;
}
long long notes_file_time (const char *path)
{
	struct kapi_stat st;
	if (!path || kapi_path_stat (path, &st) < 0 || st.mtime <= 0) return 0;
	struct kapi_clock_info ci;
	long long tz = kapi_clock_info (&ci) == 0 ? ci.tz_minutes : 0;
	return stamp_of_epoch (st.mtime + tz * 60);
}
void notes_date_label (long long modified, long long now, char *out, int cap)
{
	static const char *const MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static const char *const DAY[7] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };	// (1970-01-01: a Thursday)
	if (cap <= 0) return;
	out[0] = 0;
	if (modified <= 0) return;
	int y = (int) (modified / 10000000000LL), mo = (int) (modified / 100000000 % 100), d = (int) (modified / 1000000 % 100);
	int h = (int) (modified / 10000 % 100), mi = (int) (modified / 100 % 100);
	if (mo < 1 || mo > 12 || d < 1 || d > 31) return;
	int ny = (int) (now / 10000000000LL), nmo = (int) (now / 100000000 % 100), nd = (int) (now / 1000000 % 100);
	long long day = days_from_civil (y, mo, d), today = nmo >= 1 && nmo <= 12 ? days_from_civil (ny, nmo, nd) : day + 100000;
	long long diff = today - day;
	if (diff == 0) snprintf (out, (size_t) cap, "%02d:%02d", h, mi);
	else if (diff == 1) snprintf (out, (size_t) cap, "Yesterday");
	else if (diff >= 2 && diff <= 6) snprintf (out, (size_t) cap, "%s", DAY[((day % 7) + 7) % 7]);
	else if (y == ny) snprintf (out, (size_t) cap, "%d %s", d, MON[mo - 1]);
	else snprintf (out, (size_t) cap, "%02d/%02d/%04d", d, mo, y);
}

// ---- colours (04-ux-design.md section 7) -----------------------------------------------------------------
static const char *const CNAME[NC_COUNT] = { "yellow", "green", "blue", "pink", "purple", "grey" };
static const char *const CLABEL[NC_COUNT] = { "Yellow", "Green", "Blue", "Pink", "Purple", "Grey" };
static const unsigned PAPER[NC_COUNT] = { 0x00FCE9A6, 0x00D3EBC6, 0x00CFE0F3, 0x00F8D3D8, 0x00E2D6F0, 0x00E4E2DE };
static const unsigned DOT[NC_COUNT]   = { 0x00E8B21F, 0x0067A657, 0x005284C4, 0x00D9667A, 0x008A68C2, 0x00908C86 };
static int cl (int c) { return c >= 0 && c < NC_COUNT ? c : NC_YELLOW; }
const char *notes_colour_name (int c) { return CNAME[cl (c)]; }
const char *notes_colour_label (int c) { return CLABEL[cl (c)]; }
int notes_colour_parse (const char *v)
{
	for (int c = 0; v && c < NC_COUNT; c++) if (!fs_ci_cmp (v, CNAME[c])) return c;
	if (v && !fs_ci_cmp (v, "gray")) return NC_GREY;
	return NC_YELLOW;
}
unsigned notes_colour_paper (int c) { return PAPER[cl (c)]; }
unsigned notes_colour_dot (int c) { return DOT[cl (c)]; }

// ---- Notes' settings -------------------------------------------------------------------------------------
static void on_cfg (void *ctx, const char *, const char *key, const char *val)
{
	NotesCfg &c = *(NotesCfg *) ctx;
	if (!strcmp (key, "stickies")) c.stickies = to_int (val, c.stickies) != 0;
	else if (!strcmp (key, "last")) fs_copy (c.last, val, sizeof c.last);
	else if (!strcmp (key, "width")) c.width = to_int (val, c.width);
	else if (!strcmp (key, "height")) c.height = to_int (val, c.height);
	else if (!strcmp (key, "split")) c.split = to_int (val, c.split);
}
void notes_cfg_load (NotesCfg &c)
{
	c.stickies = 1; c.last[0] = 0; c.width = 760; c.height = 480; c.split = 250;
	char b[1024];
	int n = read_small (NOTES_CFG, b, sizeof b);
	if (n > 0) ini_each (b, n, on_cfg, &c);
}
bool notes_cfg_save (const NotesCfg &c)
{
	char b[512];
	int n = snprintf (b, sizeof b, "stickies = %d\nlast = %s\nwidth = %d\nheight = %d\nsplit = %d\n",
			  c.stickies ? 1 : 0, c.last, c.width, c.height, c.split);
	if (n <= 0 || n >= (int) sizeof b) return false;
	return kapi_save_file (NOTES_CFG, b, (unsigned) n) == n;
}
