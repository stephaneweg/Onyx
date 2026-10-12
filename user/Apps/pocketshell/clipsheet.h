//
// clipsheet.h -- the pocket shell's CLIPBOARD SHEET (docs/compact-shell/mockups/pocketterm/apps/ANSWERS.md, Q9;
// clipboard-sheet.png): the clipboard's history as a sheet over the app in front -- Ctrl + Shift + V in any app (a
// key of the shell's: PocketUI gives it before the app, uk_shell_keys), or "Clipboard history" in the Menu's column
// (the menu bar asks: SystemKit's SHELL_MSG_CLIPBOARD). Up / Down choose, A (Enter) PASTES INTO THE APP UNDER IT, Bksp
// forgets the entry, B (Esc) closes.
//
// The history is clipd's (SystemKit's clipproto.h: the widget's protocol -- CLIP_SUBSCRIBE, CLIP_LIST, CLIP_CURSOR,
// CLIP_DELETE). "Paste here" is two steps, with nothing new in the kernel nor in the apps: the chosen entry becomes
// clipd's cursor (what every app's paste asks for), the sheet lets the keys go, and the shell TYPES Ctrl + V
// (kapi_inject_modifiers, kapi_inject_key -- as its on-screen keyboard types): the server routes it to the app in
// front, which pastes as it always does. On the home nothing is typed (the entry is the clipboard's current one).
//
// Included by main.cpp after quick settings (D, F, tw, text_fit, LK, the overlay's canvas and its hits).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef POCKETSHELL_CLIPSHEET_H
#define POCKETSHELL_CLIPSHEET_H

enum { H_CLIPROW = 70 };
static ClipItemMsg g_cs[CLIP_RING], g_csTmp[CLIP_RING];
static int g_csN, g_csTmpN, g_csSel, g_csFirst, g_csPid;
static bool g_csListing, g_csFresh;			// (fresh: just opened -- the cursor's entry is the chosen one)
static unsigned g_csKeep;				// the chosen entry's id, kept through a new list

static void cs_send (int type, const void *d, unsigned n)	{ if (g_csPid) kapi_mailbox_send (g_csPid, type, d, n); }
static void cs_send_id (int type, unsigned id)			{ unsigned char b[4]; clipc_put32 (b, id); cs_send (type, b, 4); }
static void cs_ask (void)					{ if (!g_csListing) { g_csListing = true; g_csTmpN = 0; cs_send (CLIP_LIST, "", 1); } }

static void clip_open (void)
{
	if (g_over == OV_CLIP) { over_hide (); return; }
	int pid = clip_service_ ();
	if (pid != g_csPid) { g_csPid = pid; g_csN = 0; g_csListing = false; cs_send (CLIP_SUBSCRIBE, "", 1); }
	g_csFresh = true; g_csSel = 0; g_csFirst = 0;
	cs_ask ();
	over_show (OV_CLIP);
}

// clipd's answers and news (main.cpp's messages (): a message FROM clipd -- its types are the shell service's numbers).
static bool clip_message (int from, int type, const char *buf, int n)
{
	if (g_csPid == 0 || from != g_csPid) return false;
	if (type == CLIP_CHANGED) { if (g_over == OV_CLIP) { g_csListing = false; cs_ask (); } }
	else if (type == CLIP_ITEM) { if (n == (int) sizeof (ClipItemMsg) && g_csTmpN < CLIP_RING) memcpy (&g_csTmp[g_csTmpN++], buf, sizeof (ClipItemMsg)); }
	else if (type == CLIP_END)
	{
		memcpy (g_cs, g_csTmp, sizeof (ClipItemMsg) * (size_t) g_csTmpN);
		g_csN = g_csTmpN; g_csTmpN = 0; g_csListing = false;
		int sel = -1;
		for (int i = 0; i < g_csN; i++)
			if (g_csFresh ? g_cs[i].cursor != 0 : g_cs[i].id == g_csKeep) sel = i;
		if (sel >= 0) g_csSel = sel;
		g_csFresh = false;
		if (g_csSel >= g_csN) g_csSel = g_csN - 1;
		if (g_csSel < 0) g_csSel = 0;
		if (g_csN > 0) g_csKeep = g_cs[g_csSel].id;
		if (g_over == OV_CLIP) g_overDirty = true;
	}
	return true;
}

// An entry's line: a picture's size, the files' names, the text's start.
static void cs_line (const ClipItemMsg &m, char *o, int cap)
{
	int k = 0;
	if (!strcmp (m.kind, "image"))
	{
		char a[12], b[12]; int i = 0; unsigned v = m.w; char t[12];
		int ka = 0, kb = 0;
		do { t[i++] = (char) ('0' + v % 10); v /= 10; } while (v); while (i) a[ka++] = t[--i]; a[ka] = 0;
		v = m.h; do { t[i++] = (char) ('0' + v % 10); v /= 10; } while (v); while (i) b[kb++] = t[--i]; b[kb] = 0;
		lx_cat (o, cap, &k, TR ("picture")); lx_cat (o, cap, &k, "   "); lx_cat (o, cap, &k, a); lx_cat (o, cap, &k, " x "); lx_cat (o, cap, &k, b);
		return;
	}
	for (int j = 0; m.preview[j] && k < cap - 12; j++) o[k++] = m.preview[j] == '\n' || m.preview[j] == '\t' ? ' ' : m.preview[j];
	o[k] = 0;
	if (!strcmp (m.kind, "files-cut")) { lx_cat (o, cap, &k, "  "); lx_cat (o, cap, &k, TR ("(cut)")); }
	if (!o[0]) lx_cat (o, cap, &k, TR ("(empty)"));
}
// How long ago, from the entry's hour and minute: "now", "14 min", "3 h".
static void cs_when (const ClipItemMsg &m, char *o)
{
	int hh = 0, mi = 0;
	kapi_get_datetime (0, 0, 0, &hh, &mi, 0);
	int d = ((hh * 60 + mi) - (m.hour * 60 + m.minute) + 1440) % 1440, k = 0;
	if (d < 1) { strcpy (o, TR ("now")); return; }
	int v = d < 60 ? d : d / 60;
	char t[8]; int i = 0;
	do { t[i++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (i) o[k++] = t[--i];
	strcpy (o + k, d < 60 ? " min" : " h");
}

static int cs_rows_fit (int h) { int n = (h - D (48) - D (8)) / D (54); return n < 1 ? 1 : n; }

static void draw_clip (void)
{
	Canvas &cv = g_oc;
	g_overHits.clear ();
	uk_paint_alpha (true);
	bool flat = uk_flat ();
	cv.clear (flat ? (100u << 24) | UK_N_BG0 : (120u << 24) | 0x0A0F18);		// what is below, dimmed
	g_overHits.add (0, 0, g_aw, g_ah, H_BACK, 0);
	int foot = D (30);
	int w = D (448), h = g_ah - foot - D (14) - D (18);
	if (w > g_aw - D (16)) w = g_aw - D (16);
	if (h > D (388)) h = D (388);
	int x = (g_aw - w) / 2, y = D (14);
	unsigned face = LK (0xF6F6F7, UK_N_BG1), ink = LK (0x14181E, UK_N_TEXT1), dim = LK (0x5A606A, UK_N_TEXT2);
	uk_rbox (cv, x, y, w, h, D (16), face, LK (0xE9EAED, UK_N_BG1));
	uk_rline (cv, x, y, w, h, D (16), LK (0x8A909A, UK_N_LINE));
	g_overHits.add (x, y, w, h, H_PANEL, 0);
	{ UkFaceScope f (F (17)); text (cv, x + D (20), y + D (10) + (D (30) - uk_fh ()) / 2, TR ("Clipboard"), ink, 2); }
	{ UkFaceScope f (F (13)); const char *t = TR ("the last 10"); text (cv, x + w - D (20) - tw (t), y + D (10) + (D (30) - uk_fh ()) / 2, t, dim); }
	int fit = cs_rows_fit (h);
	if (g_csSel < g_csFirst) g_csFirst = g_csSel;
	if (g_csSel >= g_csFirst + fit) g_csFirst = g_csSel - fit + 1;
	if (g_csFirst > g_csN - fit) g_csFirst = g_csN - fit;
	if (g_csFirst < 0) g_csFirst = 0;
	if (g_csN == 0)
	{
		UkFaceScope f (F (15));
		const char *t = g_csPid ? TR ("Nothing copied yet") : TR ("The clipboard's history is not running");
		text_cfit (cv, x + D (20), y + D (48) + (h - D (56) - uk_fh ()) / 2, w - D (40), t, dim);
	}
	for (int k = 0; k < fit && g_csFirst + k < g_csN; k++)
	{
		int i = g_csFirst + k;
		const ClipItemMsg &m = g_cs[i];
		int rx = x + D (10), ry = y + D (48) + k * D (54), rw = w - D (20), rh = D (50);
		bool sel = i == g_csSel;
		if (sel) uk_rbox (cv, rx, ry, rw, rh, D (10), flat ? (unsigned) UK_N_ACCENT : C_ACCENT, flat ? (unsigned) UK_N_ACCENT : C_ACCENT);
		unsigned c1 = sel ? 0xFFFFFF : ink, c2 = sel ? 0xE8F0FC : dim;
		char when[16]; cs_when (m, when);
		int ww;
		{ UkFaceScope f (F (13)); ww = tw (when); text (cv, rx + rw - D (14) - ww, ry + (rh - uk_fh ()) / 2, when, c2); }
		char line[420]; cs_line (m, line, sizeof line);
		{ UkFaceScope f (F (15)); text_fit (cv, rx + D (14), ry + D (6), rw - D (28) - ww - D (16), line, c1, sel ? 2 : 0); }
		{
			UkFaceScope f (F (12));
			App *a = find_app (m.source);
			char from[80]; int n = 0;
			lx_cat (from, sizeof from, &n, TR ("from")); lx_cat (from, sizeof from, &n, " ");
			lx_cat (from, sizeof from, &n, a != 0 && a->label[0] ? a->label : m.source[0] ? m.source : "?");
			text_fit (cv, rx + D (14), ry + D (28), rw - D (28) - ww - D (16), from, c2);
		}
		g_overHits.add (rx, ry, rw, rh, H_CLIPROW, i);
	}
	if (g_csN > fit)						// more than the sheet shows: where one is
	{
		int ty = y + D (48), th = fit * D (54) - D (4), kh = th * fit / g_csN, ky = ty + (th - kh) * g_csFirst / (g_csN - fit);
		uk_rbox (cv, x + w - D (7), ky, D (3), kh, D (1), LK (0xB8BEC8, UK_N_BG3), LK (0xB8BEC8, UK_N_BG3));
	}
	// the foot: the sheet's keys, with the device's glyphs
	cv.fillRect (0, g_ah - foot, g_aw, foot, flat ? 0x000000 : 0x14181E);
	static UkPadHint H[4];
	H[0] = { "|", TR ("choose") }; H[1] = { "A", TR ("paste here") }; H[2] = { "[Bksp]", TR ("forget it") }; H[3] = { "B", TR ("close") };
	{ UkFaceScope f (F (13)); uk_pad_hints (cv, D (14), g_ah - foot + (foot - D (18)) / 2, D (18), H, 4, flat ? (unsigned) UK_N_TEXT2 : 0xC8CED8); }
	uk_paint_alpha (false);
	present (W_OVER);
}

// A: the chosen entry is the clipboard's current one, the sheet goes, Ctrl + V is typed for the app in front.
static void clip_paste (int i)
{
	if (i < 0 || i >= g_csN) { over_hide (); return; }
	cs_send_id (CLIP_CURSOR, g_cs[i].id);
	over_hide ();
	if (g_home) return;					// (no app under the sheet: the entry is chosen, nothing typed)
	kapi_inject_modifiers (MOD_CTRL);
	kapi_inject_key ("\x16");
	kapi_inject_modifiers (0);
}

static void clip_key (int code, unsigned mods)
{
	(void) mods;
	if (code == KEY_UP && g_csSel > 0) g_csSel--;
	else if (code == KEY_DOWN && g_csSel < g_csN - 1) g_csSel++;
	else if (code == KEY_PGUP || code == KEY_HOME) g_csSel = 0;
	else if (code == KEY_PGDN || code == KEY_END) g_csSel = g_csN > 0 ? g_csN - 1 : 0;
	else if (code == KEY_ENTER || code == ' ') { clip_paste (g_csSel); return; }
	else if ((code == KEY_BACKSPACE || code == KEY_DEL) && g_csSel < g_csN)
	{
		unsigned id = g_cs[g_csSel].id;
		g_csKeep = g_csSel + 1 < g_csN ? g_cs[g_csSel + 1].id : g_csSel > 0 ? g_cs[g_csSel - 1].id : 0;	// (the next one takes its place)
		cs_send_id (CLIP_DELETE, id);
		return;
	}
	else if (code == 0x1b || code == 0x16) { over_hide (); return; }	// B; Ctrl + Shift + V again
	else return;
	if (g_csSel < g_csN) g_csKeep = g_cs[g_csSel].id;
	g_overDirty = true;
}

#endif
