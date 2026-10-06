//
// turtle -- Turtle Quest, a game that teaches programming: the player writes BASIC (Onyx BASIC, the language of
// QBasic and /bin/basic, with the turtle's words: world.h) to bring a turtle to its flag, pick coins, open doors,
// paint tiles and draw figures, through levels that go from simple moves to loops, conditions, variables,
// procedures and Logo's figures. The window: the levels of a pack on the left (their stars), the program in the
// middle (Run, Step, Stop, Reset, the speed; the words the level knows under it, a click writes one), the level on
// the right (what to do, a hint, the lesson of a new idea) and the turtle's board, the run played back on it -- the
// line being run lit in the program, the turtle walking. An error says where and why ("Bump! The turtle hit a wall",
// at its line); a level won gives 1 to 3 stars (fewer instructions: more stars -- a loop beats copied lines).
//
// Keys: F5 Run, F8 Step (a statement at a time), F7 Stop, F9 Reset, F1 the lesson, F2 the hint, Ctrl+N the next
// level, Ctrl+O a pack of levels, Ctrl+E the level editor. Menus: Game, Levels, Player (several players, each with
// their stars). The language is the system's (the Control Panel's Language & Region): in French the texts and the
// program's words -- AVANCER, REPETER, SI ... ALORS ... SINON, POUR ... JUSQUE ... SUITE, FIN SUB, FONCTION...
//
// Files: the packs SD:/apps/turtle.app/levels/*.turtle (and any .turtle opened, dropped or given as the argument --
// a double click on one in the File Viewer); the player's own levels SD:/docs/turtle/my-levels.turtle (written by
// the level editor); the players' progress -- their stars, their programs, the lessons seen --
// SD:/apps/turtle.app/progress.ini.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"	// the system's language (locale.h)
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"
#include "world.h"

using namespace uikit;
using namespace turtle;

#define APPDIR		"SD:/apps/turtle.app/"
#define PROGRESS	APPDIR "progress.ini"
#define USERDIR		"SD:/docs/turtle"
#define USERPACK	USERDIR "/my-levels.turtle"

static int g_lang = LANG_EN;				// the system's language (locale_language), taken at the start
static inline const char *L2 (const char *en, const char *fr) { return g_lang == LANG_FR ? fr : en; }

// ---- faces ------------------------------------------------------------------------------------------------------------
static TextFace *g_ui, *g_mono, *g_big;

// ---- the progress: (section, key) = value, kept in memory, written at each change --------------------------------------------
struct KV { char sec[40], key[48]; char *val; };
static Arr<KV> g_kv;
static const char *kv_get (const char *sec, const char *key, const char *def = "")
{
	for (int i = 0; i < g_kv.n; i++) if (!strcmp (g_kv[i].sec, sec) && !strcmp (g_kv[i].key, key)) return g_kv[i].val;
	return def;
}
static void kv_set (const char *sec, const char *key, const char *val)
{
	for (int i = 0; i < g_kv.n; i++)
		if (!strcmp (g_kv[i].sec, sec) && !strcmp (g_kv[i].key, key)) { free (g_kv[i].val); g_kv[i].val = tdup (val); return; }
	KV k; tcpy (k.sec, sec, sizeof k.sec); tcpy (k.key, key, sizeof k.key); k.val = tdup (val);
	g_kv.push (k);
}
static void kv_save ()
{
	size_t cap = 4096, n = 0; char *o = (char *) malloc (cap);
	auto put = [&] (const char *s, bool esc) {
		for (; *s; s++)
		{
			if (n + 4 > cap) { cap *= 2; o = (char *) realloc (o, cap); }
			if (esc && *s == '\n') { o[n++] = '\\'; o[n++] = 'n'; }
			else if (esc && *s == '\\') { o[n++] = '\\'; o[n++] = '\\'; }
			else if (*s != '\r') o[n++] = *s;
		}
	};
	put ("# Turtle Quest -- the players' progress (written by the game)\n", false);
	const char *last = "";
	for (int pass = 0; pass < 2; pass++)		// (the keys without a section first)
		for (int i = 0; i < g_kv.n; i++)
		{
			const KV &k = g_kv[i];
			if ((pass == 0) != (k.sec[0] == 0)) continue;
			if (pass == 1 && strcmp (last, k.sec)) { put ("\n[", false); put (k.sec, false); put ("]\n", false); last = k.sec; }
			put (k.key, false); put (" = ", false); put (k.val, true); put ("\n", false);
		}
	kapi_save_file (PROGRESS, o, (unsigned) n);
	free (o);
}
static char *load_file (const char *path, int *len = 0)
{
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned n = kapi_fsize (f);
	char *b = (char *) malloc (n + 1);
	int got = kapi_read (f, b, n);
	kapi_close (f);
	if (got < 0) got = 0;
	b[got] = 0;
	if (len) *len = got;
	return b;
}
static void kv_load ()
{
	char *s = load_file (PROGRESS);
	if (!s) return;
	char sec[40] = "";
	for (char *p = s; *p; )
	{
		char *e = strchr (p, '\n'); if (e) *e = 0;
		char *l = p; p = e ? e + 1 : p + strlen (p);
		while (*l == ' ') l++;
		if (!*l || *l == '#') continue;
		if (*l == '[') { char *c = strchr (l, ']'); if (c) *c = 0; tcpy (sec, l + 1, sizeof sec); continue; }
		char *eq = strstr (l, " = "); if (!eq) continue;
		*eq = 0;
		char *v = eq + 3, *w = v;
		for (char *r = v; *r; r++)
		{
			if (*r == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
			else if (*r == '\\' && r[1] == '\\') { *w++ = '\\'; r++; }
			else if (*r != '\r') *w++ = *r;
		}
		*w = 0;
		kv_set (sec, l, v);
	}
	free (s);
}
static char g_player[32] = "Player";
static int stars_of (const char *id) { char k[48]; snprintf (k, sizeof k, "%s", id); return atoi (kv_get (g_player, k, "0")); }

// ---- the packs --------------------------------------------------------------------------------------------------------
enum { MAXPACKS = 12 };
static Pack *g_packs[MAXPACKS]; static int g_npacks = 0;
static int g_pack = 0, g_level = 0;
static Level *g_L = 0;					// the level played (or edited)
static Arr<Seg> g_target;				// a drawing level's figure

static bool add_pack (const char *path, bool user, bool quiet)
{
	for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, path)) return true;
	if (g_npacks >= MAXPACKS) return false;
	char *src = load_file (path);
	if (!src) { if (!quiet) uk_messagebox ("Turtle Quest", L2 ("This file cannot be read.", "Ce fichier ne peut pas être lu."), MB_OK); return false; }
	Pack *pk = new Pack; char why[160];
	bool ok = parse_pack (*pk, src, why, sizeof why);
	free (src);
	if (!ok)
	{
		if (!quiet) { char m[300]; snprintf (m, sizeof m, "%s\n%s", L2 ("Not a pack of levels:", "Ce n'est pas un recueil de niveaux :"), why); uk_messagebox ("Turtle Quest", m, MB_OK); }
		delete pk; return false;
	}
	tcpy (pk->path, path, sizeof pk->path); pk->user = user;
	if (!pk->title[0][0])
	{
		const char *b = strrchr (path, '/'); b = b ? b + 1 : path;
		tcpy (pk->title[0], b, sizeof pk->title[0]);
		char *dot = strrchr (pk->title[0], '.'); if (dot) *dot = 0;
	}
	g_packs[g_npacks++] = pk;
	return true;
}
static void load_packs ()
{
	const char *dir = APPDIR "levels";
	char names[MAXPACKS][128]; int n = 0;
	void *d = kapi_opendir (dir);
	if (d)
	{
		struct kapi_dirent e;
		while (kapi_readdir (d, &e) && n < MAXPACKS)
		{
			int l = (int) strlen (e.name);
			if (e.is_dir || l < 8 || strcmp (e.name + l - 7, ".turtle")) continue;
			tcpy (names[n++], e.name, sizeof names[0]);
		}
		kapi_closedir (d);
	}
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (names[j - 1], names[j]) > 0; j--) { char t[128]; memcpy (t, names[j], 128); memcpy (names[j], names[j - 1], 128); memcpy (names[j - 1], t, 128); }
	for (int i = 0; i < n; i++) { char p[256]; snprintf (p, sizeof p, "%s/%s", dir, names[i]); add_pack (p, false, true); }
	if (lx_exists (USERPACK)) add_pack (USERPACK, true, true);
}

// ---- the widgets --------------------------------------------------------------------------------------------------------
class LevelList; class Board; class WordBar; class Card; class MsgBar; class Lesson; class ToolPal;
static Root *g_root;
static Dropdown *g_packBox;
static LevelList *g_list;
static CodeEdit *g_ed;
static Button *g_btRun, *g_btStep, *g_btStop, *g_btReset, *g_btNext, *g_btHint, *g_btLesson;
static Slider *g_speed;
static Label *g_lbSpeed, *g_lbCount;
static WordBar *g_words;
static Card *g_card;
static Board *g_board;
static MsgBar *g_msg;
static Lesson *g_lesson;
static Widget *g_editPanel;				// the level editor's fields (in place of the list)
static Textbox *g_edTitle, *g_edText, *g_edHint, *g_edWords, *g_edPar;
static Checkbox *g_edDraw;
static Dropdown *g_edConcept;
static ToolPal *g_tools;
static Label *g_edSize;
static Button *g_edBtn[3];
static const char *const CONCEPT_KEYS[] = { "move", "turn", "pick", "door", "repeat", "for", "if", "sensor", "while", "maze", "variable", "sub", "pen", "draw" };
enum { NCONCEPTS = 14 };
static Widget *g_edLabels[8]; static int g_nedLabels = 0;
static bool g_editing = false;
static bool g_showHint = false;

// The pen's 16 colours (QBasic's, a little brighter on the board)
static const unsigned PEN[16] = { 0x202020, 0x2F6FD0, 0x2E9E44, 0x1E9FAF, 0xD0342C, 0x9C3FB5, 0x9A6234, 0xA8A8A8,
				  0x606060, 0x5B9BFF, 0x5CCB5F, 0x4FD8E8, 0xFF6B5E, 0xE07BEF, 0xF2C230, 0xFFFFFF };

// ---- the playback --------------------------------------------------------------------------------------------------------
enum { P_IDLE, P_RUN, P_PAUSE, P_DONE };
static Run g_run;
static World g_view;					// the world as shown: the events applied so far
static int g_mode = P_IDLE, g_idx = 0, g_line = 0;
static double g_t = 0;					// the event under way: 0 .. 1
static bool g_stepping = false;
static unsigned g_lastUs = 0;

static void layout ();
static void show_level (int pack, int lv, bool keepCode);
static void refresh_count ();
static void set_message (int kind, const char *text, int stars = 0);
enum { M_NONE, M_INFO, M_OK, M_ERR };

// The time an event takes at the speed chosen (ms); 0: at once
static double ev_ms (const Ev &e)
{
	static const double SCALE[10] = { 4, 2.6, 1.7, 1.15, 0.8, 0.55, 0.36, 0.22, 0.1, 0 };
	int s = g_speed ? g_speed->value : 5; if (s < 1) s = 1; if (s > 10) s = 10;
	double k = SCALE[s - 1];
	switch (e.kind)
	{
	case EV_LINE: return 110 * k;
	case EV_MOVE: { double dx = e.c - e.a, dy = e.d - e.b; return 320 * k * sqrt (dx * dx + dy * dy); }
	case EV_TURN: { double d = fabs (e.b - e.a); if (d > 180) d = 360 - d; return 240 * k * (d / 90 < 0.4 ? 0.4 : d / 90); }
	case EV_BUMP: return k ? 420 * (k < 0.5 ? 0.5 : k) : 0;
	case EV_PICK: case EV_DOOR: return 260 * k;
	case EV_TELEPORT: return 420 * k;
	}
	return 0;
}

// ---- drawing helpers ------------------------------------------------------------------------------------------------------
static void wrap_lines (const char *s, int w, char (*out)[200], int maxLines, int *n)
{
	*n = 0;
	while (*s && *n < maxLines)
	{
		int len = 0, lastSp = -1;
		while (s[len] && s[len] != '\n')
		{
			char t[200]; int l = len + 1 < 199 ? len + 1 : 199; memcpy (t, s, l); t[l] = 0;
			if (uk_tw (t) > w && len > 0) break;
			if (s[len] == ' ') lastSp = len;
			len++;
			if (len >= 198) break;
		}
		int cut = len;
		if (s[len] && s[len] != '\n' && lastSp > 0) cut = lastSp;
		memcpy (out[*n], s, cut); out[*n][cut] = 0; (*n)++;
		s += cut;
		if (*s == ' ' || *s == '\n') s++;
	}
}
static void star (Canvas &cv, int cx, int cy, int r, unsigned c, bool filled)
{
	int pts[20];
	for (int i = 0; i < 10; i++)
	{
		int rr = i & 1 ? r * 2 / 5 : r, a = i * 36;
		pts[2 * i] = V (cx) + uk_sin (a) * rr * 16 / 16384;
		pts[2 * i + 1] = V (cy) - uk_cos (a) * rr * 16 / 16384;
	}
	VPath p; p.poly (pts, 10);
	if (filled) p.fill (cv, c);
	else { VPath o; o.polyline (pts, 10, V (1) + 8, true); o.fill (cv, c); }
}
static int ftoi (double v) { return (int) floor (v + 0.5); }

// ---- the level list ---------------------------------------------------------------------------------------------------------
class LevelList : public Widget
{
public:
	int topRow = 0, hot = -1;
	LevelList (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	int rowH () { return 34; }
	int count () { return g_pack < g_npacks ? g_packs[g_pack]->levels.n : 0; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		Canvas clip; clip.adopt (canvas.px + 2 * canvas.stride + 2, width - 4, height - 4, canvas.stride);
		int n = count (), RH = rowH (), rows = (height - 4) / RH;
		if (topRow > n - rows) topRow = n - rows;
		if (topRow < 0) topRow = 0;
		for (int i = topRow; i < n && (i - topRow) * RH < height; i++)
		{
			const Level &L = *g_packs[g_pack]->levels[i];
			int y = (i - topRow) * RH, s = stars_of (L.id);
			bool sel = i == g_level && !g_editing;
			unsigned ink = C_FIELD_TEXT;
			if (sel) { uk_hilite (clip, 2, y + 1, clip.w - 4, RH - 2, 5); ink = uk_hilite_ink (); }
			else if (i == hot) clip.fillRect (2, y + 1, clip.w - 4, RH - 2, uk_mix (C_FIELD, C_ACCENT, 26));
			// the number in a disc: filled once won
			VPath d; d.circle (V (20), V (y + RH / 2), V (11));
			d.fill (clip, s ? 0x3E9B4F : uk_mix (C_FIELD, C_FIELD_TEXT, 40));
			char num[8]; snprintf (num, sizeof num, "%d", i + 1);
			uk_text_c (clip, 9, y + RH / 2 - 11, 22, 22, num, 0xFFFFFF, 2);
			char t[80]; tcpy (t, L.titleOf (g_lang), sizeof t);
			int room = clip.w - 40 - 52;
			while (strlen (t) > 3 && uk_tw (t) > room) { int l = (int) strlen (t); t[l - 1] = 0; while (l > 1 && ((unsigned char) t[l - 2] & 0xC0) == 0x80) { t[l - 2] = 0; l--; } t[strlen (t) - 1] = '.'; }
			uk_text_l (clip, 38, y, RH, t, ink, sel ? 2 : 0);
			for (int k = 0; k < 3; k++) star (clip, clip.w - 46 + k * 15, y + RH / 2, 6, k < s ? 0xF2B705 : uk_mix (sel ? C_ACCENT : C_FIELD, ink, 70), k < s);
		}
		if (n > rows)
		{
			UkThumb th = uk_thumb (n, rows, topRow, height - 4);
			uk_draw_vscroll (canvas, width - UK_SBW - 2, 2, UK_SBW, height - 4, th, C_FIELD);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { if (hot >= 0) { hot = -1; invalidate (true); } return false; }
		if (wheel) { topRow -= wheel; invalidate (true); return true; }
		int i = topRow + (my - 2) / rowH ();
		if (i >= count ()) i = -1;
		if (i != hot) { hot = i; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (i >= 0 && (i != g_level || g_editing)) show_level (g_pack, i, false); }
		if (!bl) pressed = false;
		return true;
	}
	void showSel () { int rows = (height - 4) / rowH (); if (g_level < topRow) topRow = g_level; if (g_level >= topRow + rows) topRow = g_level - rows + 1; invalidate (true); }
};

// ---- the board ---------------------------------------------------------------------------------------------------------------
static const char TOOL_CH[] = { '#', '.', '*', 'k', 'c', 'D', 'p', '>', ' ' };
enum { NTOOLS = 9 };
static int g_tool = 0;
static void edit_cell (int c, int r);

class Board : public Widget
{
public:
	int cs = 40, ox = 0, oy = 0;			// a cell's size, the grid's place
	bool dragging = false;
	Board (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void fit ()
	{
		if (!g_L) return;
		int w = g_L->w, h = g_L->h;
		cs = (width - 16) / w; int c2 = (height - 16) / h; if (c2 < cs) cs = c2;
		if (cs > 64) cs = 64;
		if (cs < 8) cs = 8;
		ox = (width - cs * w) / 2; oy = (height - cs * h) / 2;
	}
	int px (double x) { return ox + ftoi (x * cs) + cs / 2; }
	unsigned ink (int c) { return g_L && g_L->draw ? PEN[c & 15] : uk_mix (0xF1E7C6, PEN[c & 15], 150); }	// (a trail on the floor: softer)
	int py (double y) { return oy + ftoi (y * cs) + cs / 2; }
	void tile (Canvas &cv, int c, int r, char k, const World &w, double pickT, int pickCell)
	{
		int x = ox + c * cs, y = oy + r * cs, s = cs;
		bool draw = g_L->draw;
		if (k == ' ') return;
		if (k == '#')
		{
			uk_rbox (cv, x + 1, y + 1, s - 2, s - 2, s / 8, 0x8E9AAA, 0x6C7889);
			cv.fillRect (x + s / 5, y + s / 2, s * 3 / 5, 1, 0x7C889A);
			return;
		}
		if (draw)
		{
			cv.fillRect (x + s / 2, y + s / 2, 2, 2, 0xD6D1C2);
			return;
		}
		cv.fillRect (x, y, s, s, ((c + r) & 1) ? 0xEFE4C2 : 0xF4EBCD);
		if (k == 'p')
		{
			if (w.painted[r][c]) cv.fillRect (x + 2, y + 2, s - 4, s - 4, uk_mix (0xF4EBCD, PEN[w.color & 15], 150));
			for (int q = 3; q < s - 3; q += 6) { cv.fillRect (x + q, y + 2, 3, 2, 0xB06BD8); cv.fillRect (x + q, y + s - 4, 3, 2, 0xB06BD8); cv.fillRect (x + 2, y + q, 2, 3, 0xB06BD8); cv.fillRect (x + s - 4, y + q, 2, 3, 0xB06BD8); }
		}
		int cx = x + s / 2, cy = y + s / 2;
		double sc = (pickCell == c + r * 256) ? 1 - pickT : 1;
		if (k == '*')
		{
			VPath pole; pole.line (V (cx - s / 6), V (y + s / 6), V (cx - s / 6), V (y + s * 5 / 6), V (2)); pole.fill (cv, 0x5A4632);
			int f[6] = { V (cx - s / 6), V (y + s / 6), V (cx + s / 3), V (y + s / 6 + s / 8), V (cx - s / 6), V (y + s / 6 + s / 4) };
			VPath fl; fl.poly (f, 3); fl.fill (cv, 0xE0483E);
			VPath base; base.ellipse (V (cx - s / 6), V (y + s * 5 / 6), V (s / 6), V (s / 16 + 1)); base.fill (cv, 0x5A4632, 120);
		}
		else if (k == 'c' && sc > 0.02)
		{
			int rr = (int) (s * 0.24 * sc) + 1;
			VPath o; o.circle (V (cx), V (cy), V (rr)); o.fill (cv, 0xC8961E);
			VPath i; i.circle (V (cx), V (cy), V (rr) * 3 / 4); i.fill (cv, 0xF6CF3C);
			VPath g; g.circle (V (cx - rr / 3), V (cy - rr / 3), V (rr) / 4); g.fill (cv, 0xFFF3B0, 200);
		}
		else if (k == 'k' && sc > 0.02)
		{
			int rr = (int) (s * 0.14 * sc) + 1;
			VPath b; b.circle (V (cx - rr), V (cy), V (rr)); b.hole (V (cx - rr), V (cy), V (rr) / 2); b.fill (cv, 0xD9A21B);
			VPath sh; sh.line (V (cx - rr / 2), V (cy), V (cx + rr * 2), V (cy), V (rr) * 3 / 5); sh.fill (cv, 0xD9A21B);
			VPath t; t.line (V (cx + rr * 3 / 2), V (cy), V (cx + rr * 3 / 2), V (cy + rr), V (rr) / 2); t.fill (cv, 0xD9A21B);
		}
		else if (k == 'D')
		{
			uk_rbox (cv, x + 2, y + 2, s - 4, s - 4, s / 6, 0xB0773F, 0x8A5A2B);
			cv.fillRect (x + s / 2, y + 4, 1, s - 8, 0x6E4520);
			VPath kh; kh.circle (V (cx + s / 5), V (cy - s / 16), V (s / 14 + 1)); kh.fill (cv, 0x2A1A0C);
			VPath ks; ks.line (V (cx + s / 5), V (cy), V (cx + s / 5), V (cy + s / 8), V (s / 18 + 1)); ks.fill (cv, 0x2A1A0C);
		}
	}
	void turtle (Canvas &cv, double x, double y, double hd)
	{
		int cx = px (x), cy = py (y), s = cs;
		int a = ftoi (hd);
		auto at = [&] (int deg, double dist, int &ox2, int &oy2) {
			ox2 = V (cx) + (int) (uk_sin (a + deg) * dist * s * 16 / 16384);
			oy2 = V (cy) - (int) (uk_cos (a + deg) * dist * s * 16 / 16384);
		};
		int lx, ly;
		for (int k = 0; k < 4; k++)
		{
			static const int LEG[4] = { 45, -45, 135, -135 };
			at (LEG[k], 0.33, lx, ly); VPath l; l.circle (lx, ly, s * 16 / 9); l.fill (cv, 0x6FB86A);
		}
		at (180, 0.38, lx, ly); { VPath t; t.circle (lx, ly, s * 16 / 16); t.fill (cv, 0x6FB86A); }
		at (0, 0.40, lx, ly); { VPath h; h.circle (lx, ly, s * 16 * 15 / 100); h.fill (cv, 0x7CC576); }
		int ex, ey; at (-16, 0.45, ex, ey); { VPath e; e.circle (ex, ey, s * 16 / 34 + 8); e.fill (cv, 0x1E2A1E); }
		at (16, 0.45, ex, ey); { VPath e; e.circle (ex, ey, s * 16 / 34 + 8); e.fill (cv, 0x1E2A1E); }
		VPath sh; sh.circle (V (cx), V (cy), s * 16 * 34 / 100); sh.fill (cv, 0x2F7A35);
		VPath in; in.circle (V (cx), V (cy), s * 16 * 27 / 100); in.fill (cv, 0x4FA052);
		for (int k = 0; k < 6; k++) { at (k * 60 + 30, 0.19, lx, ly); VPath d; d.circle (lx, ly, s * 16 / 19); d.fill (cv, 0x3B8A3F); }
		VPath c; c.circle (V (cx), V (cy), s * 16 / 10); c.fill (cv, 0x5DB560);
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (!g_L) return;
		fit ();
		bool draw = g_L->draw;
		bool edit = g_editing && g_mode == P_IDLE;		// (the editor's map; its Test plays as a level)
		uk_rbox (canvas, 0, 0, width, height, 10, draw ? 0xFBFAF5 : 0x9CCB7E, draw ? 0xF4F2EA : 0x86B96A);
		const World &w = g_view;
		// the event under way
		const Ev *e = (g_mode == P_RUN || g_mode == P_PAUSE) && g_idx < g_run.ev.n ? &g_run.ev[g_idx] : 0;
		double t = g_t;
		int pickCell = e && (e->kind == EV_PICK || e->kind == EV_DOOR) ? e->i : -1;
		for (int r = 0; r < g_L->h; r++)
			for (int c = 0; c < g_L->w; c++)
			{
				char k = edit ? g_L->map[r][c] : w.cell[r][c];
				if (edit && (k == '>' || k == '<' || k == '^' || k == 'v')) k = '.';
				tile (canvas, c, r, k, w, t, pickCell);
			}
		if (draw && g_target.n)				// the figure to reproduce
			for (int i = 0; i < g_target.n; i++)
			{
				const Seg &s = g_target[i];
				VPath p; p.line (V (px (s.x1)), V (py (s.y1)), V (px (s.x2)), V (py (s.y2)), V (cs / 6 + 3)); p.fill (canvas, 0xD9D6CC);
			}
		// the pen's lines
		int lw = draw ? V (3) : V (cs / 10 + 2);
		for (int i = 0; i < w.segs.n; i++)
		{
			const Seg &s = w.segs[i];
			VPath p; p.line (V (px (s.x1)), V (py (s.y1)), V (px (s.x2)), V (py (s.y2)), lw); p.fill (canvas, ink (s.color));
		}
		double tx = w.x, ty = w.y, th = w.h;
		if (e && e->kind == EV_MOVE)
		{
			tx = e->a + (e->c - e->a) * t; ty = e->b + (e->d - e->b) * t;
			if (w.pen) { VPath p; p.line (V (px (e->a)), V (py (e->b)), V (px (tx)), V (py (ty)), lw); p.fill (canvas, ink (w.color)); }
		}
		else if (e && e->kind == EV_TURN)
		{
			double d = e->b - e->a; while (d > 180) d -= 360; while (d < -180) d += 360;
			th = e->a + d * t;
		}
		else if (e && e->kind == EV_BUMP)
		{
			double k = sin (t * 3.14159) * 0.28;
			tx = e->a + (e->c - e->a) * k; ty = e->b + (e->d - e->b) * k;
		}
		if (edit)
		{
			for (int r = 0; r < g_L->h; r++) for (int c = 0; c < g_L->w; c++)
			{
				char k = g_L->map[r][c];
				if (k == '>' || k == '<' || k == '^' || k == 'v') turtle (canvas, c, r, k == '^' ? 0 : k == '>' ? 90 : k == 'v' ? 180 : 270);
			}
			unsigned gl = draw ? 0xE4E0D4 : 0x00000000;
			for (int c = 0; c <= g_L->w; c++) canvas.fillRect (ox + c * cs, oy, 1, cs * g_L->h, uk_mix (0x86B96A, gl, 60));
			for (int r = 0; r <= g_L->h; r++) canvas.fillRect (ox, oy + r * cs, cs * g_L->w, 1, uk_mix (0x86B96A, gl, 60));
		}
		else turtle (canvas, tx, ty, th);
		if (e && e->kind == EV_BUMP && t > 0.3 && t < 0.9)	// a star where it bumped
		{
			int bx = px (e->a + (e->c - e->a) * 0.5), by = py (e->b + (e->d - e->b) * 0.5);
			star (canvas, bx, by, cs / 4 + 2, 0xFFD23F, true);
		}
		// what the turtle carries
		if (!g_editing && (w.keys || w.coinsTotal))
		{
			char b[64]; int n = 0;
			if (w.coinsTotal) n += snprintf (b + n, sizeof b - n, "%s %d / %d", L2 ("Coins", "Pièces"), w.coins, w.coinsTotal);
			if (w.keys) n += snprintf (b + n, sizeof b - n, "%s%s %d", n ? "    " : "", L2 ("Keys", "Clés"), w.keys);
			int bw = uk_tw (b) + 20;
			uk_rbox (canvas, width - bw - 8, 8, bw, 24, 12, 0xFFFFFF, 0xF0F0F0, 200);
			uk_text_c (canvas, width - bw - 8, 8, bw, 24, b, 0x303030, 2);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (!g_editing || !g_L) return false;
		if (mx < 0) { dragging = false; return false; }
		int c = (mx - ox) / cs, r = (my - oy) / cs;
		bool in = mx >= ox && my >= oy && c < g_L->w && r < g_L->h;
		if (bl && !pressed) { pressed = true; if (in) { edit_cell (c, r); dragging = g_tool != 7; } }
		else if (bl && dragging && in && g_L->map[r][c] != TOOL_CH[g_tool]) edit_cell (c, r);
		if (!bl) { pressed = false; dragging = false; }
		return true;
	}
};

// ---- the words the level knows: a click writes one ------------------------------------------------------------------------------
static const char *const CONTROL_WORDS[] = { "REPEAT", "FOR", "IF", "WHILE", "SUB", 0 };
static const char *const CONTROL_FR[] = { "REPETER", "POUR", "SI", "TANTQUE", "SUB", 0 };
class WordBar : public Widget
{
public:
	struct Chip { int x, y, w, id; char text[24]; };
	Chip chips[32]; int n = 0, hot = -1;
	WordBar (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void build ()
	{
		n = 0;
		if (!g_L) return;
		int x = 0, y = 0, H = 24;
		auto add = [&] (const char *t, int id) {
			if (n >= 32) return;
			int w = uk_tw (t, 2) + 18;
			if (x + w > width && x > 0) { x = 0; y += H + 5; }
			Chip &c = chips[n++]; c.x = x; c.y = y; c.w = w; c.id = id; tcpy (c.text, t, sizeof c.text);
			x += w + 5;
		};
		for (int i = 0; WORDS[i].name; i++) if (g_L->knows (WORDS[i].id)) add (word_name (WORDS[i].id, g_lang), WORDS[i].id);
		// the control words the level's idea brings (and the ones before it)
		static const char *const ORDER[] = { "repeat", "for", "if", "sensor", "while", "maze", "variable", "sub", "pen", "draw", 0 };
		int lv = -1; for (int i = 0; ORDER[i]; i++) if (!strcmp (ORDER[i], g_L->topic)) lv = i;
		const int NEED[5] = { 0, 1, 2, 4, 7 };		// REPEAT from "repeat", FOR from "for", IF "if", WHILE "while", SUB "sub"
		for (int k = 0; k < 5; k++) if (lv >= NEED[k]) add (g_lang == LANG_FR ? CONTROL_FR[k] : CONTROL_WORDS[k], 100 + k);
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		for (int i = 0; i < n; i++)
		{
			const Chip &c = chips[i];
			bool ctl = c.id >= 100;
			unsigned face = ctl ? 0xE6DDF5 : 0xDCEFD9, edge = ctl ? 0x8E6CC8 : 0x4E9A57;
			if (i == hot) face = uk_mix (face, edge, 60);
			uk_rbox (canvas, c.x, c.y, c.w, 24, 12, face, uk_mix (face, edge, 20));
			uk_rline (canvas, c.x, c.y, c.w, 24, 12, edge, 160);
			uk_text_c (canvas, c.x, c.y, c.w, 24, c.text, uk_mix (edge, 0x000000, 120), 2);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int h = -1;
		if (mx >= 0) for (int i = 0; i < n; i++) if (mx >= chips[i].x && mx < chips[i].x + chips[i].w && my >= chips[i].y && my < chips[i].y + 24) h = i;
		if (h != hot)
		{
			hot = h; invalidate (true);
			static char tipText[200];
			if (h >= 0 && chips[h].id < 100) { tcpy (tipText, word_help (chips[h].id, g_lang), sizeof tipText); tip = tipText; }
			else tip = 0;
		}
		if (mx < 0) return false;
		if (bl && !pressed && h >= 0)
		{
			pressed = true;
			const Chip &c = chips[h];
			char ins[160];
			if (c.id >= 100)
			{
				static const char *const TPL_EN[] = { "REPEAT 4\n  \nEND REPEAT", "FOR i = 1 TO 5\n  \nNEXT", "IF  THEN\n  \nEND IF", "WHILE NOT ONGOAL ()\n  \nWEND", "SUB MyWord\n  \nEND SUB" };
				static const char *const TPL_FR[] = { "REPETER 4\n  \nFIN REPETER", "POUR i = 1 JUSQUE 5\n  \nSUITE", "SI  ALORS\n  \nFIN SI", "TANTQUE NON SURBUT ()\n  \nFIN TANTQUE", "SUB MonMot\n  \nFIN SUB" };
				tcpy (ins, (g_lang == LANG_FR ? TPL_FR : TPL_EN)[c.id - 100], sizeof ins);
			}
			else
			{
				bool fn = false; for (int i = 0; WORDS[i].name; i++) if (WORDS[i].id == c.id) fn = WORDS[i].kind != 's';
				snprintf (ins, sizeof ins, fn ? "%s ()" : "%s ", c.text);
			}
			if (!g_ed->readonly) { g_ed->insertText (ins); g_ed->setFocus (); }
		}
		if (!bl) pressed = false;
		return true;
	}
};

// ---- the level's card: its title, what to do, the hint -----------------------------------------------------------------------------
class Card : public Widget
{
public:
	const char *cardText ()
	{
		return g_editing ? L2 ("Draw the level with the tools on the left, write a solution, Test it (it sets the instructions for the stars), then Save.",
			"Dessine le niveau avec les outils à gauche, écris une solution, Teste-la (elle fixe les instructions pour les étoiles), puis Enregistre.")
			: g_L->textOf (g_lang);
	}
	Card (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (!g_L) return;
		uk_rbox (canvas, 0, 0, width, height, 10, uk_mix (C_FIELD, 0xFFF6D8, 120), uk_mix (C_FIELD, 0xFFEFC2, 120));
		uk_rline (canvas, 0, 0, width, height, 10, 0xE0C77A);
		char t[160];
		if (g_editing) snprintf (t, sizeof t, "%s -- %s", L2 ("Level editor", "Éditeur de niveaux"), g_L->titleOf (g_lang));
		else snprintf (t, sizeof t, "%d. %s", g_level + 1, g_L->titleOf (g_lang));
		{ UkFaceScope fs (g_big); uk_text (canvas, 14, 8, t, 0x3A2E10, 2); }
		int y = 8 + (g_big ? g_big->height () : 20) + 4;
		char lines[8][200]; int n;
		wrap_lines (cardText (), width - 28 - (g_editing ? 0 : 170), lines, 4, &n);
		for (int i = 0; i < n; i++) { uk_text (canvas, 14, y, lines[i], 0x3A3A3A); y += uk_fh () + 2; }
		if (g_showHint && !g_editing)
		{
			char hl[4][200]; int hn; char h[500]; snprintf (h, sizeof h, "%s %s", L2 ("Hint:", "Indice :"), g_L->hintOf (g_lang));
			wrap_lines (h, width - 28, hl, 4, &hn);
			y += 2;
			for (int i = 0; i < hn; i++) { uk_text (canvas, 14, y, hl[i], 0x8A5A00, i == 0 ? 0 : 0); y += uk_fh () + 2; }
		}
	}
	int need ()
	{
		if (!g_L) return 80;
		char lines[8][200]; int n;
		wrap_lines (cardText (), width - 28 - (g_editing ? 0 : 170), lines, 4, &n);
		int h = 8 + (g_big ? g_big->height () : 20) + 4 + n * (uk_fh () + 2) + 10;
		if (g_showHint && !g_editing) { char hl[4][200]; int hn; char hh[500]; snprintf (hh, sizeof hh, "%s %s", L2 ("Hint:", "Indice :"), g_L->hintOf (g_lang)); wrap_lines (hh, width - 28, hl, 4, &hn); h += 2 + hn * (uk_fh () + 2); }
		return h < 76 ? 76 : h;
	}
};

// ---- the message under the board -----------------------------------------------------------------------------------------------
class MsgBar : public Widget
{
public:
	int kind = M_NONE, stars = 0; char text[300] = "";
	MsgBar (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (kind == M_NONE || !text[0]) return;
		unsigned face = kind == M_OK ? 0xDFF3DA : kind == M_ERR ? 0xFBE0DC : 0xE4ECF7, edge = kind == M_OK ? 0x4E9A57 : kind == M_ERR ? 0xC8463B : 0x5B7FB5;
		uk_rbox (canvas, 0, 0, width, height, 10, face, face);
		uk_rline (canvas, 0, 0, width, height, 10, edge);
		int x = 14;
		if (kind == M_OK && stars) for (int k = 0; k < 3; k++) { star (canvas, x + 11, height / 2, 11, k < stars ? 0xF2B705 : 0xC9D6C5, true); x += 26; }
		else if (kind == M_OK)
		{
			VPath d; d.circle (V (x + 10), V (height / 2), V (10)); d.fill (canvas, edge);
			uk_glyph (canvas, WKG_CHECK, x + 10, height / 2, 12, 0xFFFFFF); x += 28;
		}
		else { VPath d; d.circle (V (x + 10), V (height / 2), V (10)); d.fill (canvas, edge); uk_text_c (canvas, x, height / 2 - 10, 20, 20, kind == M_ERR ? "!" : "i", 0xFFFFFF, 2); x += 28; }
		int room = width - x - 12 - (g_btNext && !((Widget *) g_btNext)->hidden ? 140 : 0);
		char lines[3][200]; int n; wrap_lines (text, room, lines, 3, &n);
		int lh = uk_fh () + 1, y = (height - n * lh) / 2;
		bool titled = strchr (text, '\n') != 0;
		for (int i = 0; i < n; i++) { uk_text (canvas, x + 6, y, lines[i], uk_mix (edge, 0x000000, 140), i == 0 && titled ? 2 : 0); y += lh; }
	}
};
static void set_message (int kind, const char *text, int stars)
{
	g_msg->kind = kind; g_msg->stars = stars; tcpy (g_msg->text, text, sizeof g_msg->text);
	((Widget *) g_msg)->invalidate (true);
}

// ---- the lesson: a new idea's card, over the board --------------------------------------------------------------------------------
static void on_lesson_ok (Widget &);
class Lesson : public Widget
{
public:
	const Concept *c = 0;
	Button *ok;
	Lesson (int l, int t, int w, int h) : Widget (l, t, w, h)
	{
		ok = new Button (w - 120, h - 46, 104, 30, "OK", on_lesson_ok);
		ok->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM;
		addChild (ok);
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (!c) return;
		uk_rbox (canvas, 0, 0, width, height, 14, 0xFFFFFF, 0xF6F4FB);
		uk_rline (canvas, 0, 0, width, height, 14, 0x8E6CC8);
		uk_rbox (canvas, 0, 0, width, 44, 14, 0x8E6CC8, 0x7A58B8, 255, UK_TL | UK_TR);
		char t[120]; snprintf (t, sizeof t, "%s  %s", L2 ("New idea:", "Nouvelle idée :"), c->title[g_lang]);
		{ UkFaceScope fs (g_big); uk_text_l (canvas, 18, 0, 44, t, 0xFFFFFF, 2); }
		int y = 58;
		const char *s = c->text[g_lang];
		while (*s && y < height - 56)
		{
			const char *e = strchr (s, '\n'); int n = e ? (int) (e - s) : (int) strlen (s);
			char line[400]; if (n > 399) n = 399; memcpy (line, s, n); line[n] = 0;
			s = e ? e + 1 : s + n;
			if (line[0] == ' ' && line[1] == ' ')		// code: in the monospaced face, on a tint
			{
				UkFaceScope fs (g_mono);
				canvas.fillRect (18, y - 1, width - 36, uk_fh () + 3, 0xF1ECFA);
				uk_text (canvas, 24, y, line + 2, 0x4B2E83);
				y += uk_fh () + 3;
				continue;
			}
			if (!line[0]) { y += 8; continue; }
			char lines[8][200]; int k; wrap_lines (line, width - 40, lines, 8, &k);
			for (int i = 0; i < k; i++) { uk_text (canvas, 20, y, lines[i], 0x303030); y += uk_fh () + 3; }
		}
	}
	bool onMouse (int, int, int, int, int, int) override { return true; }	// (the board under it is not clicked)
};
static void show_lesson (bool show)
{
	if (show && g_L) ((Lesson *) g_lesson)->c = find_concept (g_L->topic);
	((Widget *) g_lesson)->hidden = !show || !g_lesson->c;
	if (!((Widget *) g_lesson)->hidden)
	{
		char k[40]; snprintf (k, sizeof k, "seen.%s", g_L->topic);
		kv_set (g_player, k, "1"); kv_save ();
	}
	((Widget *) g_lesson)->invalidate (true);
	g_root->invalidate (true);
}
static void on_lesson_ok (Widget &) { show_lesson (false); g_ed->setFocus (); }

// ---- the level editor's tools ----------------------------------------------------------------------------------------------------
class ToolPal : public Widget
{
public:
	ToolPal (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	int cell () { return (width - 2 * 4) / 3; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		static const char *const EN[NTOOLS] = { "Wall", "Floor", "Flag", "Key", "Coin", "Door", "Paint", "Turtle", "Water" };
		static const char *const FR[NTOOLS] = { "Mur", "Sol", "Drapeau", "Clé", "Pièce", "Porte", "Peinture", "Tortue", "Eau" };
		int c = cell ();
		for (int i = 0; i < NTOOLS; i++)
		{
			int x = (i % 3) * (c + 4), y = (i / 3) * 34;
			if (i == g_tool) uk_hilite (canvas, x, y, c, 30, 6);
			else uk_raised (canvas, x, y, c, 30, 6, C_FACE);
			unsigned sw = i == 0 ? 0x7C889A : i == 1 ? 0xF0E6C6 : i == 2 ? 0xE0483E : i == 3 ? 0xD9A21B : i == 4 ? 0xF6CF3C : i == 5 ? 0x9A6234 : i == 6 ? 0xB06BD8 : i == 7 ? 0x4FA052 : 0x4A90C8;
			uk_rbox (canvas, x + 5, y + 9, 11, 12, 3, sw, sw);
			uk_text_l (canvas, x + 19, y, 30, g_lang == LANG_FR ? FR[i] : EN[i], i == g_tool ? uk_hilite_ink () : C_TEXT);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) return false;
		if (bl && !pressed)
		{
			pressed = true;
			int c = cell (), i = (my / 34) * 3 + mx / (c + 4);
			if (i >= 0 && i < NTOOLS) { g_tool = i; invalidate (true); }
		}
		if (!bl) pressed = false;
		return true;
	}
};

// ---- the run -------------------------------------------------------------------------------------------------------------------
static void set_running (bool on)
{
	g_ed->readonly = on;
	((Widget *) g_btRun)->disabled = on && g_mode == P_RUN;
	((Widget *) g_btStop)->disabled = !on;
	((Widget *) g_btRun)->invalidate (true); ((Widget *) g_btStop)->invalidate (true);
	g_ed->invalidate (true);
}
static void reset_view ()
{
	if (g_L) g_view.reset (*g_L);
	g_mode = P_IDLE; g_idx = 0; g_t = 0; g_line = 0; g_stepping = false;
	g_ed->hiLine = 0; g_ed->invalidate (true);
	((Widget *) g_btNext)->hidden = true;
	set_running (false);
	((Widget *) g_board)->invalidate (true);
}
static void save_code ()
{
	if (!g_L || g_editing) return;
	char k[64]; snprintf (k, sizeof k, "%s.code", g_L->id);
	if (strcmp (kv_get (g_player, k, ""), g_ed->text ())) { kv_set (g_player, k, g_ed->text ()); kv_save (); }
}
// The record made from the program, the playback started (step: a statement at a time)
static bool start_run (bool step)
{
	if (!g_L) return false;
	show_lesson (false);
	reset_view ();
	g_ed->clearMarks ();
	save_code ();
	run_program (*g_L, g_ed->text (), g_lang, g_run, g_L->draw ? &g_target : 0);
	if (g_run.result == R_COMPILE)
	{
		g_ed->addMark (g_run.errLine); g_ed->hiLine = g_run.errLine; g_ed->gotoLine (g_run.errLine - 1);
		set_message (M_ERR, g_run.msg);
		return false;
	}
	set_message (M_INFO, step ? L2 ("Step by step: F8 (or Step) runs the lit line.", "Pas à pas : F8 (ou Pas) exécute la ligne éclairée.") : L2 ("Running...", "En route..."));
	g_mode = P_RUN; g_stepping = step; g_lastUs = 0;
	set_running (true);
	return true;
}
static void finish_run ()
{
	g_mode = P_DONE; g_line = 0;
	set_running (false);
	g_ed->hiLine = 0;
	const Run &R = g_run;
	if (R.result == R_WON)
	{
		const char *m = R.stars == 3 ? L2 ("Perfect!", "Parfait !") : R.stars == 2 ? L2 ("Well done!", "Bravo !") : L2 ("You made it!", "Réussi !");
		char more[160] = "";
		if (R.stars < 3 && g_L->par3)
			snprintf (more, sizeof more, L2 ("%d instructions: aim for %d for three stars.", "%d instructions : vise %d pour 3 étoiles."), R.count, g_L->par3);
		else snprintf (more, sizeof more, L2 ("%d instruction(s).", "%d instruction(s)."), R.count);
		char all[400]; snprintf (all, sizeof all, "%s\n%s", m, more);
		set_message (M_OK, all, R.stars);
		if (!g_editing)
		{
			int was = stars_of (g_L->id);
			if (R.stars > was) { char v[8]; snprintf (v, sizeof v, "%d", R.stars); kv_set (g_player, g_L->id, v); kv_save (); }
			((Widget *) g_btNext)->hidden = g_level + 1 >= g_packs[g_pack]->levels.n;
			((Widget *) g_list)->invalidate (true);
		}
		else if (g_L->par3 == 0 || R.count < g_L->par3)		// the editor: the solution sets the stars' counts
		{
			g_L->par3 = R.count; g_L->par2 = R.count + (R.count + 2) / 3;
			char p[24]; snprintf (p, sizeof p, "%d %d", g_L->par3, g_L->par2); g_edPar->setText (p);
		}
	}
	else
	{
		if (R.errLine > 0) { g_ed->addMark (R.errLine); g_ed->hiLine = R.errLine; g_ed->showLine (R.errLine - 1); }
		set_message (R.result == R_LOST ? M_INFO : M_ERR, R.msg);
	}
	g_msg->invalidate (true);
	layout ();
}
// The playback, at each tick: the events due since the last one
static void playback_tick ()
{
	if (g_mode != P_RUN) return;
	unsigned now = kapi_clock_us ();
	double dt = g_lastUs ? (unsigned) (now - g_lastUs) / 1000.0 : 16;
	if (dt > 100) dt = 100;
	if (dt < 1) dt = 16;
	g_lastUs = now;
	int guard = 0;
	bool linePassed = false;
	while (g_mode == P_RUN && g_idx < g_run.ev.n && guard++ < 4000)
	{
		const Ev &e = g_run.ev[g_idx];
		if (e.kind == EV_LINE && g_stepping && linePassed) { g_mode = P_PAUSE; set_running (true); break; }
		double ms = ev_ms (e);
		if (ms > 0)
		{
			g_t += dt / ms;
			if (g_t < 1) { dt = 0; break; }
			dt = (g_t - 1) * ms; g_t = 0;
		}
		apply (g_view, e);
		if (e.kind == EV_LINE)
		{
			g_line = e.line; g_ed->hiLine = e.line; g_ed->showLine (e.line - 1); g_ed->invalidate (true);
			if (g_stepping) linePassed = true;
		}
		if (e.kind == EV_PRINT && g_run.out)
		{
			char m[300]; snprintf (m, sizeof m, "%s %s", L2 ("The turtle says:", "La tortue dit :"), g_run.out + e.i);
			int l = (int) strlen (m); while (l && (m[l - 1] == '\n' || m[l - 1] == '\r')) m[--l] = 0;
			if (l > (int) strlen (L2 ("The turtle says:", "La tortue dit :")) + 1) set_message (M_INFO, m);
		}
		g_idx++;
		if (dt <= 0 && ev_ms (e) > 0) break;
	}
	if (g_mode == P_RUN && g_idx >= g_run.ev.n) finish_run ();
	((Widget *) g_board)->invalidate (true);
}

// ---- actions ------------------------------------------------------------------------------------------------------------------------
static void do_run ()
{
	if (g_mode == P_PAUSE) { g_stepping = false; g_mode = P_RUN; g_lastUs = 0; set_running (true); return; }
	if (g_mode == P_RUN) return;
	start_run (false);
}
static void do_step ()
{
	if (g_mode == P_PAUSE) { g_stepping = true; g_mode = P_RUN; g_lastUs = 0; return; }
	if (g_mode == P_RUN) { g_stepping = true; return; }
	if (start_run (true))
	{
		// to the first statement, lit, and wait there
		while (g_idx < g_run.ev.n && g_run.ev[g_idx].kind != EV_LINE) { apply (g_view, g_run.ev[g_idx]); g_idx++; }
		if (g_idx < g_run.ev.n) { g_line = g_run.ev[g_idx].line; g_ed->hiLine = g_line; g_ed->showLine (g_line - 1); apply (g_view, g_run.ev[g_idx]); g_idx++; }
		g_mode = P_PAUSE; set_running (true);
		if (g_idx >= g_run.ev.n) finish_run ();
		g_ed->invalidate (true); ((Widget *) g_board)->invalidate (true);
	}
}
static void do_stop ()
{
	if (g_mode != P_RUN && g_mode != P_PAUSE) return;
	g_mode = P_IDLE; g_t = 0; g_ed->hiLine = 0;
	set_running (false);
	set_message (M_INFO, L2 ("Stopped. Reset puts the turtle back at its start.", "Arrêté. « Au départ » remet la tortue à sa place."));
	((Widget *) g_board)->invalidate (true);
}
static void do_reset () { reset_view (); g_ed->clearMarks (); set_message (M_NONE, ""); }
static void do_next ()
{
	if (g_editing || g_pack >= g_npacks) return;
	if (g_level + 1 < g_packs[g_pack]->levels.n) show_level (g_pack, g_level + 1, false);
	else if (g_pack + 1 < g_npacks) show_level (g_pack + 1, 0, false);
}
static void do_prev () { if (!g_editing && g_level > 0) show_level (g_pack, g_level - 1, false); }
static void do_hint () { g_showHint = !g_showHint; layout (); }
static void do_lesson () { show_lesson (((Widget *) g_lesson)->hidden); }
static void cb_run (Widget &) { do_run (); }
static void cb_step (Widget &) { do_step (); }
static void cb_stop (Widget &) { do_stop (); }
static void cb_reset (Widget &) { do_reset (); }
static void cb_next (Widget &) { do_next (); }
static void cb_hint (Widget &) { do_hint (); }
static void cb_lesson (Widget &) { do_lesson (); }
static void cb_speed (Widget &) {}
static void cb_text (Widget &) { refresh_count (); g_ed->clearMarks (); if (g_mode == P_DONE) { g_mode = P_IDLE; } }

static void refresh_count ()
{
	if (!g_L) return;
	int n = count_instructions (g_ed->text ());
	char t[160];
	if (g_L->par3) snprintf (t, sizeof t, L2 ("Instructions: %d       \xE2\x98\x85\xE2\x98\x85\xE2\x98\x85 \xE2\x89\xA4 %d       \xE2\x98\x85\xE2\x98\x85 \xE2\x89\xA4 %d",
		"Instructions : %d       \xE2\x98\x85\xE2\x98\x85\xE2\x98\x85 \xE2\x89\xA4 %d       \xE2\x98\x85\xE2\x98\x85 \xE2\x89\xA4 %d"), n, g_L->par3, g_L->par2);
	else snprintf (t, sizeof t, L2 ("Instructions: %d", "Instructions : %d"), n);
	g_lbCount->setText (t);
}

// A level shown: its program (the player's last, or its start), the board reset; its lesson the first time
static void show_level (int pack, int lv, bool keepCode)
{
	if (g_npacks == 0) return;
	if (!g_editing) save_code ();
	if (g_editing) { g_editing = false; g_editPanel->hidden = true; ((Widget *) g_list)->hidden = false; ((Widget *) g_packBox)->hidden = false; }
	if (pack != g_pack) { g_pack = pack; g_packBox->sel = pack; ((Widget *) g_packBox)->invalidate (true); }
	g_level = lv;
	g_L = g_packs[pack]->levels[lv];
	g_target.clear ();
	if (g_L->draw) target_of (*g_L, g_target);
	set_language (g_lang);
	if (!keepCode)
	{
		char k[64]; snprintf (k, sizeof k, "%s.code", g_L->id);
		const char *code = kv_get (g_player, k, 0);
		if (!code) code = g_L->start ? g_L->start : "";
		g_ed->setText (code);
		g_ed->setCaret (g_ed->length ());
	}
	g_ed->clearMarks ();
	g_showHint = false;
	reset_view ();
	set_message (M_NONE, "");
	g_words->build ();
	refresh_count ();
	g_list->showSel ();
	kv_set ("", "pack", g_packs[pack]->path); { char v[8]; snprintf (v, sizeof v, "%d", lv); kv_set ("", "level", v); }
	kv_save ();
	char k[40]; snprintf (k, sizeof k, "seen.%s", g_L->topic);
	layout ();
	show_lesson (!kv_get (g_player, k, "")[0]);
	g_ed->setFocus ();
	g_root->invalidate (true);
}

// ---- the level editor --------------------------------------------------------------------------------------------------------------------
static Level g_edLevel;					// the level being edited (a copy)
static void edit_size ()
{
	char t[40]; snprintf (t, sizeof t, "%d x %d", g_edLevel.w, g_edLevel.h);
	g_edSize->setText (t);
}
static void edit_cell (int c, int r)
{
	Level &L = g_edLevel;
	char k = TOOL_CH[g_tool];
	if (g_mode != P_IDLE) reset_view ();
	if (k == '>')					// the turtle: moved here, or turned when it is here already
	{
		char cur = L.map[r][c];
		static const char ROT[] = "^>v<";
		const char *p = strchr (ROT, cur);
		if (p && *p) { L.map[r][c] = ROT[(p - ROT + 1) % 4]; }
		else
		{
			for (int y = 0; y < L.h; y++) for (int x = 0; x < L.w; x++) if (strchr (ROT, L.map[y][x]) && L.map[y][x]) L.map[y][x] = '.';
			L.map[r][c] = '>';
		}
	}
	else
	{
		if (strchr ("^>v<", L.map[r][c]) && L.map[r][c]) return;	// (the turtle stays: move it with its tool)
		if (k == '*') for (int y = 0; y < L.h; y++) for (int x = 0; x < L.w; x++) if (L.map[y][x] == '*') L.map[y][x] = '.';
		L.map[r][c] = k;
	}
	((Widget *) g_board)->invalidate (true);
}
static void edit_resize (int dw, int dh)
{
	Level &L = g_edLevel;
	int w = L.w + dw, h = L.h + dh;
	if (w < 3 || h < 3 || w > MAXW || h > MAXH) return;
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++) if (y >= L.h || x >= L.w) L.map[y][x] = (y == h - 1 || x == w - 1 || y == 0 || x == 0) ? '#' : '.';
		L.map[y][w] = 0;
	}
	// the new border: walls
	if (dw > 0) for (int y = 0; y < h; y++) { L.map[y][w - 2] = L.map[y][w - 2] == '#' && y > 0 && y < h - 1 ? '.' : L.map[y][w - 2]; L.map[y][w - 1] = '#'; }
	if (dh > 0) for (int x = 0; x < w; x++) { if (x > 0 && x < w - 1 && L.map[h - 2][x] == '#') L.map[h - 2][x] = '.'; L.map[h - 1][x] = '#'; }
	L.w = w; L.h = h;
	edit_size ();
	g_view.reset (L);
	((Widget *) g_board)->invalidate (true);
}
static void cb_w_minus (Widget &) { edit_resize (-1, 0); }
static void cb_w_plus (Widget &) { edit_resize (1, 0); }
static void cb_h_minus (Widget &) { edit_resize (0, -1); }
static void cb_h_plus (Widget &) { edit_resize (0, 1); }
static void edit_collect ()			// the fields into the level
{
	Level &L = g_edLevel;
	tcpy (L.title[g_lang], g_edTitle->text, sizeof L.title[0]);
	tcpy (L.text[g_lang], g_edText->text, sizeof L.text[0]);
	tcpy (L.hint[g_lang], g_edHint->text, sizeof L.hint[0]);
	tcpy (L.words, g_edWords->text, sizeof L.words);
	L.par3 = atoi (g_edPar->text); const char *q = strchr (g_edPar->text, ' '); L.par2 = q ? atoi (q) : L.par3 + 2;
	L.draw = g_edDraw->checked ? (L.draw ? L.draw : 1) : 0;	// (a colour level stays one)
	int ci = g_edConcept->sel; if (ci < 0 || ci >= NCONCEPTS) ci = 0;
	tcpy (L.topic, CONCEPT_KEYS[ci], sizeof L.topic);
	free (L.solution); L.solution = tdup (g_ed->text ());
}
static void open_editor (bool fresh)
{
	if (g_mode == P_RUN || g_mode == P_PAUSE) do_stop ();
	if (!g_editing) save_code ();
	if (fresh || !g_L)
	{
		Level blank; blank.w = 10; blank.h = 7;
		for (int y = 0; y < blank.h; y++) { for (int x = 0; x < blank.w; x++) blank.map[y][x] = (y == 0 || x == 0 || y == blank.h - 1 || x == blank.w - 1) ? '#' : '.'; blank.map[y][blank.w] = 0; }
		blank.map[3][1] = '>'; blank.map[3][8] = '*';
		tcpy (blank.title[0], "My level", sizeof blank.title[0]); tcpy (blank.title[1], "Mon niveau", sizeof blank.title[1]);
		tcpy (blank.topic, "move", sizeof blank.topic);
		blank.solution = tdup (g_lang == LANG_FR ? "AVANCER 7\n" : "FORWARD 7\n");
		g_edLevel.set (blank);
		snprintf (g_edLevel.id, sizeof g_edLevel.id, "my-%u", (unsigned) (kapi_get_ticks () % 100000));
	}
	else g_edLevel.set (*g_L);
	g_editing = true;
	g_L = &g_edLevel;
	g_target.clear ();
	g_editPanel->hidden = false; ((Widget *) g_list)->hidden = true; ((Widget *) g_packBox)->hidden = true;
	g_edTitle->setText (g_edLevel.titleOf (g_lang)); g_edText->setText (g_edLevel.textOf (g_lang)); g_edHint->setText (g_edLevel.hintOf (g_lang));
	g_edWords->setText (g_edLevel.words);
	char p[24]; snprintf (p, sizeof p, "%d %d", g_edLevel.par3, g_edLevel.par2); g_edPar->setText (g_edLevel.par3 ? p : "");
	g_edDraw->checked = g_edLevel.draw != 0; ((Widget *) g_edDraw)->invalidate (true);
	int ci = 0; for (int i = 0; i < NCONCEPTS; i++) if (!strcmp (CONCEPT_KEYS[i], g_edLevel.topic)) ci = i;
	g_edConcept->sel = ci; ((Widget *) g_edConcept)->invalidate (true);
	g_ed->setText (g_edLevel.solution ? g_edLevel.solution : "");
	refresh_count ();
	edit_size ();
	reset_view ();
	g_words->build ();
	show_lesson (false);
	set_message (M_INFO, L2 ("Click or drag on the board with a tool. The turtle tool turns the turtle when clicked on it.", "Clique ou glisse sur le plateau avec un outil. L'outil tortue fait tourner la tortue quand on clique dessus."));
	layout ();
	g_root->invalidate (true);
}
static void cb_ed_test (Widget &)
{
	edit_collect ();
	g_target.clear ();
	if (g_edLevel.draw) target_of (g_edLevel, g_target);
	g_edLevel.par3 = 0;				// (the solution sets them again)
	start_run (false);
}
static void cb_ed_save (Widget &)
{
	edit_collect ();
	if (!g_edLevel.id[0]) snprintf (g_edLevel.id, sizeof g_edLevel.id, "my-%u", (unsigned) (kapi_get_ticks () % 100000));
	// the player's pack: loaded (or made), the level put in place of the one with its id, or added
	int pi = -1;
	for (int i = 0; i < g_npacks; i++) if (g_packs[i]->user) pi = i;
	if (pi < 0)
	{
		if (g_npacks >= MAXPACKS) return;
		Pack *pk = new Pack; pk->user = true; tcpy (pk->path, USERPACK, sizeof pk->path);
		tcpy (pk->title[0], "My levels", sizeof pk->title[0]); tcpy (pk->title[1], "Mes niveaux", sizeof pk->title[1]);
		g_packs[g_npacks] = pk; pi = g_npacks++;
	}
	Pack &pk = *g_packs[pi];
	int at = -1;
	for (int i = 0; i < pk.levels.n; i++) if (!strcmp (pk.levels[i]->id, g_edLevel.id)) at = i;
	bool copyOfBuiltin = at < 0 && !strncmp (g_edLevel.id, "my-", 3) == 0;
	if (copyOfBuiltin) { char id[32]; snprintf (id, sizeof id, "my-%s", g_edLevel.id); tcpy (g_edLevel.id, id, sizeof g_edLevel.id); for (int i = 0; i < pk.levels.n; i++) if (!strcmp (pk.levels[i]->id, g_edLevel.id)) at = i; }
	if (at < 0) { pk.levels.push (new Level); at = pk.levels.n - 1; }
	pk.levels[at]->set (g_edLevel);
	kapi_mkdir (USERDIR);
	char *txt = write_pack (pk);
	int ok = kapi_save_file (pk.path, txt, (unsigned) strlen (txt));
	free (txt);
	// the packs' list follows
	static const char *names[MAXPACKS];
	static char titles[MAXPACKS][80];
	for (int i = 0; i < g_npacks; i++) { tcpy (titles[i], g_packs[i]->titleOf (g_lang), sizeof titles[0]); names[i] = titles[i]; }
	g_packBox->setOptions (names, g_npacks, g_pack);
	char m[200]; snprintf (m, sizeof m, ok >= 0 ? L2 ("Saved in \"%s\".", "Enregistré dans « %s ».") : L2 ("Could not write %s.", "Impossible d'écrire %s."), ok >= 0 ? pk.titleOf (g_lang) : pk.path);
	set_message (ok >= 0 ? M_OK : M_ERR, m, 0);
	g_msg->stars = 0;
}
static void cb_ed_close (Widget &)
{
	int p = g_pack, l = g_level;
	for (int i = 0; i < g_npacks; i++) if (g_packs[i]->user) for (int k = 0; k < g_packs[i]->levels.n; k++) if (!strcmp (g_packs[i]->levels[k]->id, g_edLevel.id)) { p = i; l = k; }
	g_editing = false;
	show_level (p, l, false);
}

// ---- menus ----------------------------------------------------------------------------------------------------------------------------
static Menu g_menu;
static void build_menu ();
static void retitle ();
static void m_run () { do_run (); }
static void m_step () { do_step (); }
static void m_stop () { do_stop (); }
static void m_reset () { do_reset (); }
static void m_next () { do_next (); }
static void m_prev () { do_prev (); }
static void m_hint () { do_hint (); }
static void m_lesson () { do_lesson (); }
static void m_quit () { save_code (); kapi_exit (0); }
static void m_open ()
{
	char path[256];
	if (!uk_file_open (path, sizeof path, USERDIR, L2 ("Turtle Quest levels|*.turtle|All files|*", "Niveaux de Turtle Quest|*.turtle|Tous les fichiers|*"))) return;
	if (add_pack (path, false, false)) { retitle (); build_menu (); for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, path)) show_level (i, 0, false); }
}
static void m_edit () { open_editor (false); }
static void m_new () { open_editor (true); }
// players
enum { MAXPLAYERS = 8 };
static char g_players[MAXPLAYERS][32]; static int g_nplayers = 0;
static void players_read ()
{
	g_nplayers = 0;
	char buf[400]; tcpy (buf, kv_get ("", "players", ""), sizeof buf);
	for (char *p = buf; *p && g_nplayers < MAXPLAYERS; )
	{
		char *e = strchr (p, '|'); if (e) *e = 0;
		if (*p) tcpy (g_players[g_nplayers++], p, 32);
		p = e ? e + 1 : p + strlen (p);
	}
	if (!g_nplayers) { tcpy (g_players[0], "Player", 32); g_nplayers = 1; }
}
static void players_write ()
{
	char buf[400] = ""; for (int i = 0; i < g_nplayers; i++) { if (i) strcat (buf, "|"); strcat (buf, g_players[i]); }
	kv_set ("", "players", buf); kv_set ("", "player", g_player); kv_save ();
}
static void choose_player (int i)
{
	if (i < 0 || i >= g_nplayers) return;
	save_code ();
	tcpy (g_player, g_players[i], sizeof g_player);
	players_write (); build_menu ();
	if (!g_editing) show_level (g_pack, g_level, false);
	((Widget *) g_list)->invalidate (true);
}
static void m_p0 () { choose_player (0); } static void m_p1 () { choose_player (1); } static void m_p2 () { choose_player (2); } static void m_p3 () { choose_player (3); }
static void m_p4 () { choose_player (4); } static void m_p5 () { choose_player (5); } static void m_p6 () { choose_player (6); } static void m_p7 () { choose_player (7); }
static const MenuAction M_PLAYER[MAXPLAYERS] = { m_p0, m_p1, m_p2, m_p3, m_p4, m_p5, m_p6, m_p7 };
static void name_btn (Widget &w) { ((Modal *) w.parent)->close (w.tag); }
static void name_enter (Widget &w) { ((Modal *) w.parent)->close (1); }
class NameBox : public Modal
{
public:
	Textbox *tb;
	NameBox () : Modal (340, 150)
	{
		Root *r = Root::current ();
		left = ((r ? r->width : 800) - width) / 2; top = ((r ? r->height : 600) - height) / 2;
		tb = new Textbox (16, Modal::titleH () + 34, width - 32, 28, "", name_enter); tb->maxLen = 24; addChild (tb);
		Button *b = new Button (width - 192, height - 42, 84, 30, "OK", name_btn); b->tag = 1; addChild (b);
		b = new Button (width - 100, height - 42, 84, 30, L2 ("Cancel", "Annuler"), name_btn); b->tag = 0; addChild (b);
	}
	void onDraw () override
	{
		drawBox (L2 ("New player", "Nouveau joueur"));
		uk_text (canvas, 16, Modal::titleH () + 10, L2 ("The player's name:", "Le nom du joueur :"), C_TEXT);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
};
static void m_newplayer ()
{
	if (g_nplayers >= MAXPLAYERS) { uk_messagebox ("Turtle Quest", L2 ("Eight players at most.", "Huit joueurs au plus."), MB_OK); return; }
	NameBox *d = new NameBox;
	int ok = d->run ();
	char name[32]; tcpy (name, d->tb->text, sizeof name);
	delete d;
	for (char *p = name; *p; p++) if (*p == '|' || *p == '[' || *p == ']' || *p == '=') *p = ' ';
	if (!ok || !name[0]) return;
	for (int i = 0; i < g_nplayers; i++) if (!strcmp (g_players[i], name)) { choose_player (i); return; }
	tcpy (g_players[g_nplayers++], name, 32);
	choose_player (g_nplayers - 1);
}
static void m_about ()
{
	uk_messagebox ("Turtle Quest", L2 ("Turtle Quest -- learn to program with a turtle.\nThe programs are Onyx BASIC, with the turtle's words.\n\nMIT License -- the Onyx contributors.",
		"Turtle Quest -- apprends à programmer avec une tortue.\nLes programmes sont en Onyx BASIC, avec les mots de la tortue.\n\nLicence MIT -- les contributeurs d'Onyx."), MB_OK);
}
static void build_menu ()
{
	g_menu = Menu ();
	g_menu.menu (L2 ("Game", "Jeu"));
	g_menu.item (L2 ("Run", "Lancer"), "F5", KEY_F1 + 4, m_run);
	g_menu.item (L2 ("Step", "Pas à pas"), "F8", KEY_F1 + 7, m_step);
	g_menu.item (L2 ("Stop", "Arrêter"), "F7", KEY_F1 + 6, m_stop);
	g_menu.item (L2 ("Reset", "Au départ"), "F9", KEY_F1 + 8, m_reset);
	g_menu.separator ();
	g_menu.item (L2 ("Next Level", "Niveau suivant"), "^N", UK_CTRL ('N'), m_next);
	g_menu.item (L2 ("Previous Level", "Niveau précédent"), "^P", UK_CTRL ('P'), m_prev);
	g_menu.item (L2 ("Lesson", "Leçon"), "F1", KEY_F1, m_lesson);
	g_menu.item (L2 ("Hint", "Indice"), "F2", KEY_F1 + 1, m_hint);
	g_menu.separator ();
	g_menu.item (L2 ("Quit", "Quitter"), "^Q", UK_CTRL ('Q'), m_quit);
	g_menu.menu (L2 ("Levels", "Niveaux"));
	g_menu.item (L2 ("Open Level Pack...", "Ouvrir un recueil..."), "^O", UK_CTRL ('O'), m_open);
	g_menu.separator ();
	g_menu.item (L2 ("Edit This Level", "Modifier ce niveau"), "^E", UK_CTRL ('E'), m_edit);
	g_menu.item (L2 ("New Level", "Nouveau niveau"), "", 0, m_new);
	g_menu.menu (L2 ("Player", "Joueur"));
	for (int i = 0; i < g_nplayers; i++)
	{
		static char lbl[MAXPLAYERS][48];
		snprintf (lbl[i], sizeof lbl[i], "%s%s", g_players[i], strcmp (g_players[i], g_player) ? "" : "  *");
		g_menu.item (lbl[i], "", 0, M_PLAYER[i]);
	}
	g_menu.separator ();
	g_menu.item (L2 ("New Player...", "Nouveau joueur..."), "", 0, m_newplayer);
	g_menu.item (L2 ("About Turtle Quest", "À propos de Turtle Quest"), "", 0, m_about);
	g_menu.publish ();
}

// ---- the window ------------------------------------------------------------------------------------------------------------------------
static void cb_pack (Widget &) { int p = g_packBox->sel; if (p >= 0 && p < g_npacks && p != g_pack) show_level (p, 0, false); }
static void retitle ()
{
	static const char *names[MAXPACKS];
	static char titles[MAXPACKS][80];
	for (int i = 0; i < g_npacks; i++) { tcpy (titles[i], g_packs[i]->titleOf (g_lang), sizeof titles[0]); names[i] = titles[i]; }
	g_packBox->setOptions (names, g_npacks, g_pack);
	tcpy (g_btRun->text, L2 ("Run", "Lancer"), sizeof g_btRun->text);
	tcpy (g_btStep->text, L2 ("Step", "Pas"), sizeof g_btStep->text);
	tcpy (g_btStop->text, L2 ("Stop", "Arrêt"), sizeof g_btStop->text);
	tcpy (g_btReset->text, L2 ("Reset", "Au départ"), sizeof g_btReset->text);
	tcpy (g_btNext->text, L2 ("Next level", "Niveau suivant"), sizeof g_btNext->text);
	tcpy (g_btHint->text, L2 ("Hint", "Indice"), sizeof g_btHint->text);
	tcpy (g_btLesson->text, L2 ("Lesson", "Leçon"), sizeof g_btLesson->text);
	g_lbSpeed->setText (L2 ("Speed", "Vitesse"));
	if (g_edBtn[0])
	{
		tcpy (g_edBtn[0]->text, L2 ("Test", "Tester"), sizeof g_edBtn[0]->text);
		tcpy (g_edBtn[1]->text, L2 ("Save", "Enregistrer"), sizeof g_edBtn[1]->text);
		tcpy (g_edBtn[2]->text, L2 ("Close", "Fermer"), sizeof g_edBtn[2]->text);
		static const char *const EN[] = { "Title", "What to do", "Hint", "Words (empty: all)", "Idea taught", "Instructions for 3 and 2 stars", "Size", "Tool" };
		static const char *const FR[] = { "Titre", "Ce qu'il faut faire", "Indice", "Mots (vide : tous)", "Idée enseignée", "Instructions pour 3 et 2 étoiles", "Taille", "Outil" };
		for (int i = 0; i < g_nedLabels; i++) ((Label *) g_edLabels[i])->setText (g_lang == LANG_FR ? FR[i] : EN[i]);
		tcpy (g_edDraw->text, L2 ("Drawing", "Dessin"), sizeof g_edDraw->text);
	}
	Widget *bts[] = { g_btRun, g_btStep, g_btStop, g_btReset, g_btNext, g_btHint, g_btLesson, g_edBtn[0], g_edBtn[1], g_edBtn[2], g_edDraw };
	for (unsigned i = 0; i < sizeof bts / sizeof bts[0]; i++) if (bts[i]) bts[i]->invalidate (true);
	// the ideas' names in the editor's list
	{
		static const char *opts[NCONCEPTS];
		for (int i = 0; i < NCONCEPTS; i++) { const Concept *c = find_concept (CONCEPT_KEYS[i]); opts[i] = c ? c->title[g_lang] : CONCEPT_KEYS[i]; }
		if (g_edConcept) g_edConcept->setOptions (opts, NCONCEPTS, g_edConcept->sel);
	}
	((Widget *) g_btRun)->tip = L2 ("Run the program (F5)", "Lancer le programme (F5)");
	((Widget *) g_btStep)->tip = L2 ("One statement at a time (F8)", "Une instruction à la fois (F8)");
	g_words->build ();
	refresh_count ();
	g_root->invalidate (true);
}
enum { PAD = 10, LISTW = 220, EDW = 340, TOOLH = 32 };
static void layout ()
{
	Root &R = *g_root;
	int W = R.width, H = R.height;
	int mid = PAD + LISTW + PAD;
	int edw = W > 1000 ? EDW + (W - 1000) / 3 : EDW;
	int right = mid + edw + PAD, rw = W - right - PAD;
	// left: the pack and its levels, or the level editor
	g_packBox->left = PAD; g_packBox->top = PAD; g_packBox->resizeTo (LISTW, 28);
	g_list->left = PAD; g_list->top = PAD + 36; g_list->resizeTo (LISTW, H - PAD - 36 - PAD);
	g_editPanel->left = PAD; g_editPanel->top = PAD; g_editPanel->resizeTo (LISTW, H - 2 * PAD);
	// the middle: the buttons, the speed, the program, its words, its count
	int x = mid;
	Widget *bts[4] = { g_btRun, g_btStep, g_btStop, g_btReset };
	int bw[4] = { 84, 70, 70, 0 };
	bw[3] = edw - (bw[0] + bw[1] + bw[2]) - 3 * 6;
	for (int i = 0; i < 4; i++) { bts[i]->left = x; bts[i]->top = PAD; bts[i]->resizeTo (bw[i], TOOLH); x += bw[i] + 6; }
	g_lbSpeed->left = mid; g_lbSpeed->top = PAD + TOOLH + 6; g_lbSpeed->resizeTo (70, 22);
	g_speed->left = mid + 72; g_speed->top = PAD + TOOLH + 6; g_speed->resizeTo (edw - 72, 22);
	int ey = PAD + TOOLH + 6 + 22 + 8;
	g_words->resizeTo (edw, 60); g_words->build ();
	int wordsH = 0; for (int i = 0; i < g_words->n; i++) if (g_words->chips[i].y + 24 > wordsH) wordsH = g_words->chips[i].y + 24;
	g_words->resizeTo (edw, wordsH);
	int countH = 22;
	g_ed->left = mid; g_ed->top = ey; g_ed->resizeTo (edw, H - ey - PAD - countH - 8 - wordsH - 8);
	g_words->left = mid; g_words->top = g_ed->top + g_ed->height + 8;
	g_lbCount->left = mid; g_lbCount->top = H - PAD - countH; g_lbCount->resizeTo (edw, countH);
	// the right: the card, the board, the message
	g_card->left = right; g_card->top = PAD; g_card->resizeTo (rw, 100);
	int ch = g_card->need (); g_card->resizeTo (rw, ch);
	g_btLesson->left = right + rw - 90; g_btLesson->top = PAD + 10; g_btLesson->resizeTo (80, 28);
	g_btHint->left = right + rw - 176; g_btHint->top = PAD + 10; g_btHint->resizeTo (80, 28);
	((Widget *) g_btHint)->hidden = ((Widget *) g_btLesson)->hidden = g_editing;
	int msgH = 62;
	int by = PAD + ch + PAD;
	g_board->left = right; g_board->top = by; g_board->resizeTo (rw, H - by - PAD - msgH - PAD);
	g_msg->left = right; g_msg->top = H - PAD - msgH; g_msg->resizeTo (rw, msgH);
	g_btNext->left = right + rw - 140; g_btNext->top = H - PAD - msgH + 15; g_btNext->resizeTo (126, 32);
	int lw = rw - 40 < 560 ? rw - 40 : 560, lh = g_board->height - 30 < 360 ? g_board->height - 30 : 360;
	if (lh < 200) lh = 200;
	g_lesson->left = right + (rw - lw) / 2; g_lesson->top = by + (g_board->height - lh) / 2;
	g_lesson->resizeTo (lw, lh);
	g_lesson->ok->left = lw - 120; g_lesson->ok->top = lh - 46;
	R.invalidate (true);
}

class TurtleRoot : public Root
{
public:
	TurtleRoot () : Root (1000, 640, "Turtle Quest") {}
	void onTick () override { playback_tick (); }
	void onResized () override { layout (); }
	bool onKey (long k) override
	{
		switch (k)
		{
		case KEY_F1 + 4: do_run (); return true;
		case KEY_F1 + 7: do_step (); return true;
		case KEY_F1 + 6: do_stop (); return true;
		case KEY_F1 + 8: do_reset (); return true;
		case KEY_F1: do_lesson (); return true;
		case KEY_F1 + 1: do_hint (); return true;
		case 27: if (!((Widget *) g_lesson)->hidden) { show_lesson (false); return true; } do_stop (); return true;
		}
		return g_menu.shortcut (k);
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type != DND_FILES) return;
		char path[256]; int n = 0; while (data[n] && data[n] != '\n' && n < 255) { path[n] = data[n]; n++; } path[n] = 0;
		if (add_pack (path, false, false)) { retitle (); for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, path)) show_level (i, 0, false); }
	}
};

class EdPanel : public Widget
{
public:
	EdPanel (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override { canvas.clear (bgColor ()); }
};
static Widget *ed_label (Widget *p, int y, const char *en, const char *fr)
{
	Label *l = new Label (0, y, LISTW, 18, L2 (en, fr), C_DIS);
	p->addChild (l);
	if (g_nedLabels < 8) g_edLabels[g_nedLabels++] = l;
	return l;
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	g_ui = ft_uikit_face ();
	{ FtTextFace *m = new FtTextFace; if (m->open ("DejaVu Sans Mono", 14)) g_mono = m; else delete m; }
	{ FtTextFace *b = new FtTextFace; if (b->open ("DejaVu Sans", 18)) g_big = b; else delete b; }
	kv_load ();
	g_lang = !strcmp (locale_language (), "fr") ? LANG_FR : LANG_EN;
	set_language (g_lang);
	players_read ();
	tcpy (g_player, kv_get ("", "player", g_players[0]), sizeof g_player);
	load_packs ();
	char args[256]; int an = kapi_get_args (args, sizeof args);
	if (an > 0 && args[0]) add_pack (args, false, false);

	TurtleRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	static const char *none[1] = { "" };
	g_packBox = new Dropdown (PAD, PAD, LISTW, 28, none, 0, 0, cb_pack); root.addChild (g_packBox);
	g_list = new LevelList (PAD, PAD + 36, LISTW, 400); root.addChild (g_list);
	g_btRun = new Button (0, 0, 80, TOOLH, "Run", cb_run); root.addChild (g_btRun);
	g_btStep = new Button (0, 0, 70, TOOLH, "Step", cb_step); root.addChild (g_btStep);
	g_btStop = new Button (0, 0, 70, TOOLH, "Stop", cb_stop); root.addChild (g_btStop);
	g_btReset = new Button (0, 0, 70, TOOLH, "Reset", cb_reset); root.addChild (g_btReset);
	g_lbSpeed = new Label (0, 0, 70, 22, "Speed", C_TEXT, root.bg); root.addChild (g_lbSpeed);
	g_speed = new Slider (0, 0, 200, 22, 1, 10, atoi (kv_get ("", "speed", "5")), cb_speed, root.bg); root.addChild (g_speed);
	g_ed = new CodeEdit (0, 0, 300, 300);
	g_ed->mono = g_mono; g_ed->ui = g_ui; g_ed->onChange = cb_text;
	g_ed->isKeyword = [] (const char *w, int n) -> bool {
		static char words[6000]; static int len = -1, lang = -1;
		if (lang != g_lang) { set_language (g_lang); len = bas::wordList (words, sizeof words); lang = g_lang; }
		for (int i = 0; i < len; )
		{
			int j = i; while (j < len && words[j] != ' ') j++;
			if (j - i == n) { int k = 0; while (k < n && tup (w[k]) == words[i + k]) k++; if (k == n) return true; }
			i = j + 1;
		}
		return false;
	};
	root.addChild (g_ed);
	g_words = new WordBar (0, 0, EDW, 60); root.addChild (g_words);
	g_lbCount = new Label (0, 0, EDW, 22, "", C_DIS, root.bg); root.addChild (g_lbCount);
	g_card = new Card (0, 0, 400, 100); root.addChild (g_card);
	g_btHint = new Button (0, 0, 80, 28, "Hint", cb_hint); root.addChild (g_btHint);
	g_btLesson = new Button (0, 0, 80, 28, "Lesson", cb_lesson); root.addChild (g_btLesson);
	g_board = new Board (0, 0, 400, 400); root.addChild (g_board);
	g_msg = new MsgBar (0, 0, 400, 54); root.addChild (g_msg);
	g_btNext = new Button (0, 0, 126, 32, "Next level", cb_next); root.addChild (g_btNext);
	((Widget *) g_btNext)->hidden = true;
	g_lesson = new Lesson (0, 0, 520, 340); root.addChild (g_lesson);
	((Widget *) g_lesson)->hidden = true;
	// the level editor's panel
	{
		Widget *p = g_editPanel = new EdPanel (PAD, PAD, LISTW, 600);
		int y = 0;
		ed_label (p, y, "Title", "Titre"); y += 18;
		g_edTitle = new Textbox (0, y, LISTW, 26); g_edTitle->maxLen = 70; p->addChild (g_edTitle); y += 32;
		ed_label (p, y, "What to do", "Ce qu'il faut faire"); y += 18;
		g_edText = new Textbox (0, y, LISTW, 26); g_edText->maxLen = 400; p->addChild (g_edText); y += 32;
		ed_label (p, y, "Hint", "Indice"); y += 18;
		g_edHint = new Textbox (0, y, LISTW, 26); g_edHint->maxLen = 400; p->addChild (g_edHint); y += 32;
		ed_label (p, y, "Words (empty: all)", "Mots (vide : tous)"); y += 18;
		g_edWords = new Textbox (0, y, LISTW, 26); g_edWords->maxLen = 250; p->addChild (g_edWords); y += 32;
		ed_label (p, y, "Idea taught", "Idée enseignée"); y += 18;
		g_edConcept = new Dropdown (0, y, LISTW, 26, CONCEPT_KEYS, NCONCEPTS, 0, 0); p->addChild (g_edConcept); y += 32;
		ed_label (p, y, "Instructions for 3 and 2 stars", "Instructions pour 3 et 2 étoiles"); y += 18;
		g_edPar = new Textbox (0, y, 100, 26); p->addChild (g_edPar);
		g_edDraw = new Checkbox (110, y, LISTW - 110, 26, L2 ("Drawing", "Dessin"), false, 0); p->addChild (g_edDraw); y += 34;
		ed_label (p, y, "Size", "Taille"); y += 18;
		g_edSize = new Label (0, y, 58, 26, "", C_TEXT); p->addChild (g_edSize);
		{
			int bw = (LISTW - 60 - 3 * 4) / 4, x = 60;
			p->addChild (new Button (x, y, bw, 26, "W-", cb_w_minus)); x += bw + 4; p->addChild (new Button (x, y, bw, 26, "W+", cb_w_plus)); x += bw + 4;
			p->addChild (new Button (x, y, bw, 26, "H-", cb_h_minus)); x += bw + 4; p->addChild (new Button (x, y, LISTW - x, 26, "H+", cb_h_plus));
		}
		y += 34;
		ed_label (p, y, "Tool", "Outil"); y += 18;
		g_tools = new ToolPal (0, y, LISTW, 3 * 34); p->addChild (g_tools); y += 3 * 34 + 6;
		{
			int b0 = 62, b1 = 92;
			g_edBtn[0] = new Button (0, y, b0, 30, "Test", cb_ed_test); p->addChild (g_edBtn[0]);
			g_edBtn[1] = new Button (b0 + 6, y, b1, 30, "Save", cb_ed_save); p->addChild (g_edBtn[1]);
			g_edBtn[2] = new Button (b0 + b1 + 12, y, LISTW - b0 - b1 - 12, 30, "Close", cb_ed_close); p->addChild (g_edBtn[2]);
		}
		p->hidden = true;
		root.addChild (p);
	}
	root.setResizable (true);
	root.setMinSize (920, 560);
	retitle ();
	build_menu ();
	layout ();
	root.fitWorkArea ();
	if (g_npacks == 0)
	{
		uk_messagebox ("Turtle Quest", L2 ("No level found in SD:/apps/turtle.app/levels.", "Aucun niveau dans SD:/apps/turtle.app/levels."), MB_OK);
		return 1;
	}
	// where the player was (or the pack given)
	int p = 0, l = 0;
	if (an > 0 && args[0]) { for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, args)) p = i; }
	else
	{
		const char *pp = kv_get ("", "pack", "");
		for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, pp)) { p = i; l = atoi (kv_get ("", "level", "0")); }
		if (l >= g_packs[p]->levels.n) l = 0;
	}
	show_level (p, l, false);
	root.run ();
	save_code ();
	{ char v[8]; snprintf (v, sizeof v, "%d", g_speed->value); kv_set ("", "speed", v); kv_save (); }
	return 0;
}
