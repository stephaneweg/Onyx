//
// notes_mock -- (AutoDev round 1, the UX Designer's mock-ups; the desktop simulator only, never on the card)
// Notes' window and the Stickies widget drawn with UIKit itself, from canned sample notes, so the
// pictures of 04-ux-design.md are the real toolkit's pixels. Not the app: the owner-drawn pieces
// (NoteList, the Stickies cards, the toolbar's trash / pin / colour icons) are sketched here the way
// the plan builds them. Built and run by mockups.sh beside it; MOCK=<scene> picks the picture:
//   window   the window with six notes, "Shopping" selected
//   empty    the first start: no SD:/Notes, one empty new note, the caret in the editor
//   search   (should) the search field holding "wifi": one note left
//   error    the status line when a save failed
//   dialog   the refusal of a dropped file larger than 64 KB (a MessageBox)
//   stickies / stickies-empty / stickies-hover   the widget (a borderless, back-most, see-through window)
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "fontkit/uikitface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uikit;

// ---- the six colours (04-ux-design.md section 7): the paper, the dot, the band ---------------------------
enum { NC_YELLOW, NC_GREEN, NC_BLUE, NC_PINK, NC_PURPLE, NC_GREY, NC_COUNT };
static const unsigned PAPER[NC_COUNT] = { 0x00FCE9A6, 0x00D3EBC6, 0x00CFE0F3, 0x00F8D3D8, 0x00E2D6F0, 0x00E4E2DE };
static const unsigned DOT[NC_COUNT]   = { 0x00E8B21F, 0x0067A657, 0x005284C4, 0x00D9667A, 0x008A68C2, 0x00908C86 };
static const char *const CNAME[NC_COUNT] = { "Yellow", "Green", "Blue", "Pink", "Purple", "Grey" };
static const unsigned NOTE_INK = 0x002B2925;		// the text on every paper

// ---- the sample notes (the simulator's clock: Monday 28 September 2026, 12:34) --------------------------
struct Note { const char *text; int colour; bool pinned; const char *when; const char *full; };
static Note g_notes[] = {
	{ "Onyx to-do\n[x] notes.ini reader\n[x] the note list\n[ ] Stickies: drag the header\n[ ] icons for both apps\n[ ] docs/04 catalog",
	  NC_BLUE, true, "11:48", "Today, 11:48" },
	{ "Shopping\nmilk, eggs, butter\nbread (the sourdough one)\ncoffee beans\ntomatoes, basil\nbatteries AA x4",
	  NC_YELLOW, true, "09:15", "Today, 09:15" },
	{ "Wi-Fi at the club\nNetwork: ClubHouse-5G\nPassword: on the board by the bar\nAsk Marc about the printer",
	  NC_GREEN, true, "Yesterday", "Yesterday, 18:30" },
	{ "Gift ideas for L\xC3\xA9" "a\na book on volcanoes\nthe board game from the shop on rue Haute\nconcert tickets?",
	  NC_PINK, false, "Fri", "Friday 25 September, 20:02" },
	{ "Pi 4 GPIO pins\npin 1 3.3 V, pin 6 GND\nGPIO 17 = pin 11, GPIO 27 = pin 13\nI2C: SDA pin 3, SCL pin 5",
	  NC_GREY, false, "15 Sep", "15 September, 10:15" },
	{ "Books to read\nThe Left Hand of Darkness\nPiranesi\nThe Soul of a New Machine",
	  NC_PURPLE, false, "2 Sep", "2 September, 21:40" },
};
static const int NNOTES = sizeof g_notes / sizeof g_notes[0];
static int  g_shown[16], g_nshown = 0;		// the rows listed (the search's result)
static int  g_sel = 1;
static bool g_empty = false;			// the empty state: one new note, nothing on the card
static const char *g_scene = "window";

static void title_of (const char *t, char *o, int cap)
{ int i = 0; while (t[i] && t[i] != '\n' && i < cap - 1) { o[i] = t[i]; i++; } o[i] = 0; }
static void line_of (const char *t, int k, char *o, int cap)	// the k-th line (0: the title)
{
	while (k > 0 && *t) { if (*t == '\n') k--; t++; }
	int i = 0; while (t[i] && t[i] != '\n' && i < cap - 1) { o[i] = t[i]; i++; } o[i] = 0;
}

// ---- the icons the toolbar and the list draw (in the plan: UIKit's WKT_TRASH / WKT_PIN) ------------------
static void fill_box (Canvas &cv, int x, int y, int w, int h, unsigned c) { uk_rbox (cv, x, y, w, h, 0, c, c); }
static void draw_pin (Canvas &cv, int x, int y, int s, unsigned ink)		// a push pin, s x s
{
	int cx = x + s / 2;
	uk_rbox (cv, cx - s * 5 / 18, y + s / 9, s * 10 / 18, s * 3 / 18, 1, ink, ink);		// the cap
	uk_rbox (cv, cx - s * 3 / 18, y + s * 4 / 18, s * 6 / 18, s * 5 / 18, 0, ink, ink);	// the body
	uk_rbox (cv, cx - s * 6 / 18, y + s * 9 / 18, s * 12 / 18, s * 2 / 18 + 1, 1, ink, ink);	// the collar
	fill_box (cv, cx - 1, y + s * 11 / 18, 2, s * 6 / 18, ink);				// the needle
}
static void draw_trash (Canvas &cv, int x, int y, int s, unsigned ink, unsigned bg)
{
	int t = s / 10 > 1 ? s / 10 : 1;
	fill_box (cv, x + s * 2 / 18, y + s * 4 / 18, s * 14 / 18, t + 1, ink);			// the lid
	fill_box (cv, x + s * 7 / 18, y + s * 2 / 18, s * 4 / 18, t + 1, ink);			// its handle
	uk_rbox (cv, x + s * 4 / 18, y + s * 6 / 18, s * 10 / 18, s * 11 / 18, 2, ink, ink);	// the can
	uk_rbox (cv, x + s * 4 / 18 + t + 1, y + s * 6 / 18 + t + 1, s * 10 / 18 - 2 * t - 2, s * 11 / 18 - 2 * t - 2, 1, bg, bg);
	for (int k = 0; k < 2; k++) fill_box (cv, x + s * 7 / 18 + k * s * 4 / 18, y + s * 8 / 18, t, s * 7 / 18, ink);
}
static void draw_dot (Canvas &cv, int x, int y, int d, int c, bool ring)
{
	if (ring) uk_rline (cv, x - 3, y - 3, d + 6, d + 6, (d + 6) / 2, C_ACCENT), uk_rline (cv, x - 2, y - 2, d + 4, d + 4, (d + 4) / 2, C_ACCENT);
	uk_rbox (cv, x, y, d, d, d / 2, uk_tone (DOT[c], 150), DOT[c]);
	uk_rline (cv, x, y, d, d, d / 2, uk_tone (DOT[c], 90), 160);
}
enum { IC_TRASH, IC_PIN, IC_DOT0 };
static void tool_icon (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool off)
{
	(void) off;
	if (id == IC_TRASH) draw_trash (cv, x, y, size, ink, cv.px ? cv.px[(y + size / 2) * cv.stride + x + size / 2] & 0xFFFFFF : C_BG);
	else if (id == IC_PIN) draw_pin (cv, x, y, size, ink);
	else draw_dot (cv, x + 2, y + 2, size - 4, id - IC_DOT0, false);
}

// ---- NoteList: owner-drawn rows beside the app (user/Apps/notes/notelist.h) ------------------------------
class NoteList : public Widget
{
public:
	enum { HEAD = 44, ROW = 56 };
	NoteList (int l, int t, int w, int h) : Widget (l, t, w, h) { canFocus = true; }
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 140), faint = uk_mix (C_FIELD, C_FIELD_TEXT, 30);
		// the head: what is listed, and how many
		bool searching = !strcmp (g_scene, "search");
		const char *h = searching ? "Found" : "Notes";
		uk_text (canvas, 14, (HEAD - uk_fh ()) / 2, h, C_FIELD_TEXT, 2);
		char n[40];
		if (searching) snprintf (n, sizeof n, "%d of %d", g_nshown, NNOTES);
		else snprintf (n, sizeof n, "%d", g_empty ? 1 : g_nshown);
		int nw = uk_tw (n);
		uk_rbox (canvas, width - nw - 30, 13, nw + 16, 19, 9, uk_mix (C_FIELD, C_FIELD_TEXT, 36), uk_mix (C_FIELD, C_FIELD_TEXT, 36));
		uk_text (canvas, width - nw - 22, 13 + (19 - uk_fh ()) / 2, n, C_FIELD_TEXT);
		canvas.fillRect (0, HEAD - 1, width, 1, faint);
		if (g_empty)
		{
			int y = HEAD + 4;
			uk_rbox (canvas, 6, y, width - 12, ROW - 4, 7, uk_mix (C_FIELD, C_ACCENT, 70), uk_mix (C_FIELD, C_ACCENT, 70));
			draw_dot (canvas, 16, y + 11, 10, NC_YELLOW, false);
			uk_text (canvas, 34, y + 6, "New Note", uk_mix (C_FIELD, C_FIELD_TEXT, 170), 3);
			uk_text (canvas, 34, y + 26, "Now", dim);
			return;
		}
		if (g_nshown == 0)
		{
			uk_text_c (canvas, 0, HEAD + 30, width, 20, "No note contains this text.", dim);
			return;
		}
		for (int r = 0; r < g_nshown; r++)
		{
			const Note &note = g_notes[g_shown[r]];
			int y = HEAD + 4 + r * ROW;
			if (y > height) break;
			bool sel = g_shown[r] == g_sel;
			if (sel) uk_rbox (canvas, 6, y, width - 12, ROW - 4, 7, uk_mix (C_FIELD, C_ACCENT, hasFocus ? 90 : 60), uk_mix (C_FIELD, C_ACCENT, hasFocus ? 90 : 60));
			else if (r + 1 < g_nshown && g_shown[r + 1] != g_sel) canvas.fillRect (34, y + ROW - 3, width - 46, 1, faint);
			draw_dot (canvas, 16, y + 11, 10, note.colour, false);
			char t[96], f[96], body[96];
			int ww = uk_tw (note.when);
			title_of (note.text, t, sizeof t);
			uk_text_fit (t, width - 34 - ww - 22, f, sizeof f, 2);
			uk_text (canvas, 34, y + 5, f, C_FIELD_TEXT, 2);
			uk_text (canvas, width - ww - 14, y + 5, note.when, sel ? uk_mix (C_FIELD, C_FIELD_TEXT, 190) : dim);
			line_of (note.text, 1, body, sizeof body);
			uk_text_fit (body, width - 34 - 14 - (note.pinned ? 22 : 0), f, sizeof f);
			uk_text (canvas, 34, y + 26, f, dim);
			if (note.pinned) draw_pin (canvas, width - 30, y + 26, 15, sel ? C_ACCENT : uk_mix (C_FIELD, C_FIELD_TEXT, 160));
		}
	}
};

// ---- NoteEdit's pane: the note's date line over the Textarea -------------------------------------------
class InfoLine : public Widget		// (in the app: a Label; drawn here for the pin glyph beside the words)
{
public:
	InfoLine (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 140);
		if (g_empty) { uk_text_l (canvas, 18, 0, height, "New note \xC2\xB7 type: it is kept by itself, no need to save", dim); return; }
		const Note &n = g_notes[g_sel];
		char s[120];
		snprintf (s, sizeof s, "%s  \xC2\xB7  %s", n.full, CNAME[n.colour]);
		uk_text_l (canvas, 18, 0, height, s, dim);
		if (n.pinned)
		{
			int x = 18 + uk_tw (s) + 14;
			draw_pin (canvas, x, (height - 14) / 2, 14, C_ACCENT);
			uk_text_l (canvas, x + 18, 0, height, "On the desktop", C_ACCENT);
		}
		canvas.fillRect (12, height - 1, width - 24, 1, uk_mix (C_FIELD, C_FIELD_TEXT, 30));
	}
};

// The status line at the window's bottom: counts at the left, what happened at the right.
class Status : public Widget
{
public:
	Status (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		canvas.fillRect (0, 0, width, 1, uk_mix (C_BG, C_TEXT, 40));
		unsigned dim = uk_mix (C_BG, C_TEXT, 170);
		bool err = !strcmp (g_scene, "error");
		uk_text_l (canvas, 12, 1, height - 1, g_empty ? "No notes yet" : "6 notes \xC2\xB7 3 on the desktop", dim);
		const char *r = g_empty ? "" : err ? "Not saved: the card is full or read-only \xE2\x80\x94 trying again" : "Saved";
		unsigned c = err ? 0x00B02A1E : dim;
		int w = uk_tw (r);
		if (err) uk_glyph (canvas, WKG_CLOSE, width - w - 26, height / 2, 9, c);
		else if (*r) uk_glyph (canvas, WKG_CHECK, width - w - 22, height / 2, 10, dim);
		uk_text_l (canvas, width - w - 12, 1, height - 1, r, c);
	}
};

class HintBox : public Textbox		// (the search, a should: Mail's / Calendar's / Photos' HintBox, to UIKit)
{
public:
	const char *hint;
	HintBox (int l, int t, int w, int h, const char *h_) : Textbox (l, t, w, h, ""), hint (h_) {}
	void onDraw () override
	{
		Textbox::onDraw ();
		if (!text[0] && !hasFocus) uk_text_l (canvas, 7, 0, height, hint, uk_mix (C_FIELD_TEXT, C_FIELD, 110));
	}
};

// NoteEdit: the Textarea in the note's paper (its colours), its text one size up (a face of its own for its
// drawing, its clicks and its keys alike, so the caret follows the glyphs it shows).
class NoteEdit : public Textarea
{
public:
	FtTextFace *face;
	NoteEdit (int l, int t, int w, int h, int cap) : Textarea (l, t, w, h, cap), face (0) {}
	void onDraw () override { UkFaceScope s (face); Textarea::onDraw (); }
	bool onMouse (int mx, int my, int bl, int br, int bm, int wh) override { UkFaceScope s (face); return Textarea::onMouse (mx, my, bl, br, bm, wh); }
	bool onKey (long k) override { UkFaceScope s (face); return Textarea::onKey (k); }
};
static unsigned sheet (int c) { return uk_mix (C_FIELD, PAPER[c], 150); }

// ---- the Notes window ------------------------------------------------------------------------------------
static int notes_window ()
{
	const int W = 760, H = 480, TB = 44, ST = 24, SPLIT = 250;
	bool search = !strcmp (g_scene, "search");
	g_empty = !strcmp (g_scene, "empty");
	g_nshown = 0;
	for (int i = 0; i < NNOTES; i++)
		if (!search || i == 2) g_shown[g_nshown++] = i;
	if (search) g_sel = 2;
	Root root (W, H, "Notes");
	root.setResizable (true);

	ToolBar *tb = new ToolBar (0, 0, W, TB); tb->line = true; root.addChild (tb);
	ToolButton *nw = (new ToolButton (0, 30, "New note (Ctrl+N)"))->setGlyph (WKT_PLUS)->setText ("New Note")->fitWidth ();
	nw->filled = true; nw->setOn (true); tb->add (nw, 6);
	tb->sep ();
	ToolButton *del = (new ToolButton (0, 30, "Delete note: to the Trash (Ctrl+D)"))->setIcon (tool_icon, IC_TRASH)->setText ("Delete")->fitWidth ();
	tb->add (del, 4);
	ToolButton *pin = (new ToolButton (0, 30, "Pin to the desktop (Ctrl+P)"))->setIcon (tool_icon, IC_PIN)->setText ("Pin")->setToggle (true, !g_empty && g_notes[g_sel].pinned)->fitWidth ();
	tb->add (pin, 2);
	tb->sep ();
	int cur = g_empty ? NC_YELLOW : g_notes[g_sel].colour;
	for (int c = 0; c < NC_COUNT; c++)
	{
		ToolButton *b = (new ToolButton (26, 26, CNAME[c]))->setIcon (tool_icon, IC_DOT0 + c);
		b->iconSize = 18; b->setToggle (true, c == cur); tb->add (b, c ? 0 : 2);
	}
	if (g_empty) { del->setDisabled (true); }
	HintBox *find = new HintBox (0, 0, 190, 28, "Search notes");
	if (search) find->setText ("club");
	tb->addRight (find, 10);

	HSplitter *sp = new HSplitter (0, TB, W, H - TB - ST, SPLIT, C_FIELD);
	sp->anchor = ANCHOR_FILL; root.addChild (sp);
	NoteList *list = new NoteList (0, 0, SPLIT, H - TB - ST);
	Panel *pane = new Panel (0, 0, W - SPLIT, H - TB - ST, C_FIELD);
	InfoLine *info = new InfoLine (0, 0, W - SPLIT, 34); pane->addChild (info);
	NoteEdit *ed = new NoteEdit (10, 40, W - SPLIT - 20, H - TB - ST - 50, 65536);
	ed->face = new FtTextFace; if (!ed->face->open ("DejaVu Sans", 15)) ed->face = 0;
	int pc = g_empty ? NC_YELLOW : g_notes[g_sel].colour;
	ed->setColors (sheet (pc), NOTE_INK, C_ACCENT, uk_mix (sheet (pc), C_ACCENT, 90));
	ed->anchor = ANCHOR_FILL; pane->addChild (ed);
	sp->setPanes (list, pane);
	if (!g_empty) { ed->setContent (g_notes[g_sel].text); ed->caret = 0; ed->anchor = -1; }
	Status *st = new Status (0, H - ST, W, ST); st->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM; root.addChild (st);

	if (search) find->setFocus ();
	else if (g_empty || !strcmp (g_scene, "error")) ed->setFocus ();
	else list->setFocus ();
	if (!strcmp (g_scene, "dialog"))
		uk_messagebox ("Import a note", "\xE2\x80\x9C" "server-log.txt\xE2\x80\x9D is larger than 64 KB, the most a note can hold.\n"
			       "It was not imported; open it in the Text Editor instead.", MB_OK);
	root.run ();
	return 0;
}

// ---- Stickies: the widget (user/Apps/stickies/main.cpp) ------------------------------------------------
// The geometry, in px (04-ux-design.md section 8).
enum { SW = 240, SHDR = 34, CX = 12, CW = 216, CPAD = 10, CBAND = 5, CGAP = 12, BODY_LINES = 6, CR = 6 };

// Text straight on the wallpaper (a see-through canvas): the face's glyphs drawn white on black in a scratch
// canvas, their coverage then blended as the agenda blends its bitmap glyphs -- a soft shadow, then the ink.
// (The plan: agenda's wall_text made a UIKit helper that works with a face too, uk_text_over.)
static void wall_text (Canvas &cv, int x, int y, const char *s, unsigned ink, int style)
{
	static Canvas sc; int w = uk_tw (s, style) + 2, h = uk_fh () + 2;
	sc.resize (w, h); sc.clear (0);
	uk_text (sc, 0, 0, s, 0x00FFFFFF, style);
	for (int pass = 0; pass < 2; pass++)
		for (int ry = 0; ry < h; ry++)
			for (int rx = 0; rx < w; rx++)
			{
				int a = sc.px[ry * sc.stride + rx] & 255; if (!a) continue;
				if (pass == 0) { uk_blend_px (cv, x + rx + 1, y + ry + 1, 0, a * 150 / 255); uk_blend_px (cv, x + rx + 2, y + ry + 2, 0, a * 50 / 255); }
				else uk_blend_px (cv, x + rx, y + ry, ink, a);
			}
}

// Word-wrap (the planned uikit uk_text_wrap): the lines of s that fit in w px, at most max.
static int wrap (const char *s, int w, int max, int *st, int *ln, bool *more)
{
	int n = (int) strlen (s), i = 0, k = 0;
	*more = false;
	while (i < n)
	{
		if (k == max) { *more = true; break; }
		int e = i, lastSp = -1;
		while (e < n && s[e] != '\n' && uk_tw_n (s + i, e - i + 1) <= w) { if (s[e] == ' ') lastSp = e; e++; }
		if (e < n && s[e] != '\n' && lastSp > i) e = lastSp;
		st[k] = i; ln[k] = e - i; k++;
		i = e; if (i < n && (s[i] == '\n' || s[i] == ' ')) i++;
	}
	return k;
}

static void sticky_icon (Canvas &cv, int x, int y)		// the header's: a small yellow note, its corner turned
{
	uk_rbox (cv, x, y, 16, 16, 2, 0x00FFF0B0, PAPER[NC_YELLOW]);
	uk_rline (cv, x, y, 16, 16, 2, 0x00000000, 80);
	for (int k = 0; k < 5; k++) fill_box (cv, x + 11 + k, y + 11 + (4 - k), 5 - (4 - k) > 0 ? 1 : 1, k + 1, uk_tone (DOT[NC_YELLOW], 100));
	fill_box (cv, x + 3, y + 5, 8, 1, 0x00A08850); fill_box (cv, x + 3, y + 8, 6, 1, 0x00A08850);
}

class StickiesRoot : public Root
{
public:
	int nPinned, hover;
	StickiesRoot (int x, int y, int h, int n, int hv) : Root (x, y, SW, h, "stickies",
		WIN_FLAG_BORDERLESS | WIN_FLAG_BACKMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA), nPinned (n), hover (hv) {}

	static int card_h (const Note &n, int *lines)
	{
		const char *body = strchr (n.text, '\n'); body = body ? body + 1 : "";
		int st[BODY_LINES], ln[BODY_LINES]; bool more;
		*lines = wrap (body, CW - 2 * CPAD, BODY_LINES, st, ln, &more);
		return CBAND + CPAD + 18 + *lines * 17 + CPAD;
	}
	void card (int y, const Note &n, bool hot)
	{
		int lines, h = card_h (n, &lines);
		unsigned p = PAPER[n.colour];
		// the shadow: soft, below (blended into the see-through canvas)
		uk_rbox (canvas, CX + 1, y + 3, CW, h, CR + 1, 0x00000000, 0x00000000, hot ? 70 : 45);
		uk_rbox (canvas, CX, y + 1, CW, h + 1, CR, 0x00000000, 0x00000000, 40);
		uk_rbox (canvas, CX, y, CW, h, CR, uk_tone (p, 140), p);					// the paper
		uk_rbox (canvas, CX, y, CW, CBAND, CR, uk_tone (DOT[n.colour], 150), uk_tone (DOT[n.colour], 140), 255, UK_TL | UK_TR);	// the band
		uk_rline (canvas, CX, y, CW, h, CR, hot ? 0x00FFFFFF : uk_tone (p, 80), hot ? 230 : 150);
		if (hot) uk_rline (canvas, CX + 1, y + 1, CW - 2, h - 2, CR - 1, 0x00FFFFFF, 140);
		char t[96], f[96];
		title_of (n.text, t, sizeof t);
		int ty = y + CBAND + CPAD - 2;
		uk_text_fit (t, CW - 2 * CPAD - (hot ? 18 : 0), f, sizeof f, 2);
		uk_text (canvas, CX + CPAD, ty, f, NOTE_INK, 2);
		if (hot) uk_glyph (canvas, WKG_CHEV_RIGHT, CX + CW - CPAD - 4, ty + uk_fh () / 2, 9, uk_mix (p, NOTE_INK, 150));
		const char *body = strchr (n.text, '\n'); body = body ? body + 1 : "";
		int st[BODY_LINES], ln[BODY_LINES]; bool more;
		int k = wrap (body, CW - 2 * CPAD, BODY_LINES, st, ln, &more);
		for (int i = 0; i < k; i++)
		{
			char l[128]; int m = ln[i] < 127 ? ln[i] : 127; memcpy (l, body + st[i], m); l[m] = 0;
			int ly = ty + 20 + i * 17, lx = CX + CPAD;
			unsigned ink = uk_mix (p, NOTE_INK, 215);
			bool box = !strncmp (l, "[ ] ", 4) || !strncmp (l, "[x] ", 4);
			if (box)						// a checklist line (a should): its box
			{
				bool done = l[1] == 'x';
				int bx = lx, by = ly + (uk_fh () - 12) / 2;
				if (done) { uk_rbox (canvas, bx, by, 12, 12, 3, DOT[n.colour], uk_tone (DOT[n.colour], 110)); uk_glyph (canvas, WKG_CHECK, bx + 6, by + 6, 9, 0x00FFFFFF); }
				else { uk_rbox (canvas, bx, by, 12, 12, 3, uk_tone (p, 160), uk_tone (p, 150)); uk_rline (canvas, bx, by, 12, 12, 3, uk_mix (p, NOTE_INK, 150)); }
				memmove (l, l + 4, strlen (l + 4) + 1); lx += 18;
				if (done) ink = uk_mix (p, NOTE_INK, 120);
			}
			if (i == k - 1 && more) { char c2[128]; strcpy (c2, l); strcat (c2, "\xE2\x80\xA6"); uk_text_fit (c2, CX + CW - CPAD - lx, l, sizeof l); if (!strstr (l, "...") && !strstr (l, "\xE2\x80\xA6")) strcat (l, "\xE2\x80\xA6"); }
			uk_text (canvas, lx, ly, l, ink);
			if (box && l[0] && ink != uk_mix (p, NOTE_INK, 215)) {}
		}
	}
	void onDraw () override
	{
		canvas.clear (0xFE000000u);			// almost see-through: the clicks still land (the agenda's CATCH)
		uk_paint_alpha (true);
		sticky_icon (canvas, 14, 9);
		const char *a = "Pinned notes";
		int fy = 5 + (26 - uk_fh ()) / 2;
		wall_text (canvas, 38, fy, a, 0x00FAFCFF, 2);
		if (nPinned) { char c[8]; snprintf (c, sizeof c, "(%d)", nPinned); wall_text (canvas, 38 + uk_tw (a, 2) + 6, fy, c, 0x00B8C4D0, 0); }
		for (int i = 10; i < SW - 10; i++)		// the etched line (the agenda's)
		{ uk_blend_px (canvas, i, 33, 0, 110); uk_blend_px (canvas, i, 34, 0x00FFFFFF, 70); }
		if (!nPinned)					// nothing pinned: a dashed place, a hint
		{
			int y = SHDR + 10, h = 62;
			for (int x = CX + 6; x < CX + CW - 6; x += 8) { fill_box (canvas, x, y, 4, 1, 0); uk_blend_px (canvas, x, y, 0x00FFFFFF, 0); }
			for (int x = CX + 6; x < CX + CW - 6; x += 8)
			{
				for (int k = 0; k < 4; k++) { uk_blend_px (canvas, x + k, y, 0x00FFFFFF, 120); uk_blend_px (canvas, x + k, y + h - 1, 0x00FFFFFF, 120); }
			}
			for (int yy = y + 6; yy < y + h - 6; yy += 8)
				for (int k = 0; k < 4; k++) { uk_blend_px (canvas, CX, yy + k, 0x00FFFFFF, 120); uk_blend_px (canvas, CX + CW - 1, yy + k, 0x00FFFFFF, 120); }
			uk_rbox (canvas, CX + 1, y + 1, CW - 2, h - 2, 6, 0x00FFFFFF, 0x00FFFFFF, hover ? 40 : 18);
			draw_pin (canvas, CX + 14, y + 12, 16, 0x00E8EEF4);
			wall_text (canvas, CX + 38, y + 10, "No notes pinned", 0x00FAFCFF, 2);
			wall_text (canvas, CX + 38, y + 31, "Click to open Notes", 0x00B8C4D0, 0);
			uk_paint_alpha (false);
			return;
		}
		int y = SHDR + 10;
		for (int i = 0, k = 0; i < NNOTES && k < nPinned; i++)
		{
			if (!g_notes[i].pinned) continue;
			int lines; int h = card_h (g_notes[i], &lines);
			card (y, g_notes[i], hover == k);
			y += h + CGAP; k++;
		}
		uk_paint_alpha (false);
	}
};

static int stickies (bool none, int hv)
{
	int n = 0, h = SHDR + 10;
	for (int i = 0; i < NNOTES; i++) if (g_notes[i].pinned && !none) { int l; h += StickiesRoot::card_h (g_notes[i], &l) + CGAP; n++; }
	if (none) h += 62 + 10;
	h += 6;
	StickiesRoot root (1024 - SW - 8, 40, h, n, hv);
	(void) h;
	root.run ();
	return 0;
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	const char *m = getenv ("MOCK"); if (m) g_scene = m;
	if (!strcmp (g_scene, "stickies")) return stickies (false, -1);
	if (!strcmp (g_scene, "stickies-hover")) return stickies (false, 1);
	if (!strcmp (g_scene, "stickies-empty")) return stickies (true, -1);
	return notes_window ();
}
