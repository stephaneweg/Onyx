//
// critters -- Critters: lead a stream of little creatures from a hatch to an exit across a pixel terrain they cannot
// cross alone, by giving some of them a role -- climber, floater, blocker, builder, digger, exploder. Twelve levels
// (six Training, six Expedition, each opened by solving the one before) and any level the player writes in a text file
// (the .level format: docs/04 "Critters"). The window (800 x 448, fixed): the play area (the terrain x2, scrolled
// sideways), the status line (the chosen role, what is under the pointer, Out, Saved, Time), the skill bar (the eight
// role slots, the release rate, pause, fast forward, all explode, the minimap). Before a level, the picker: the levels in
// their groups with their state (solved and the best, new, locked; a refused file with its reason), the chosen one's
// preview and facts. A start card before each level, the end card after it. 04-ux-design.md is the design.
//
// Keys: 1-8 choose a role, Tab / Shift+Tab highlight a creature, Enter give it the role (or click a creature), P / Space
// pause, F fast forward, N all explode (a card asks), - / + the release rate, Left / Right (Shift: faster) and Home / End
// scroll (also the pointer at the area's edges, the wheel, a right-button drag, the minimap), Esc the Paused card, M the
// sound, Ctrl+R restart, Ctrl+O a level file, F1 how to play, Ctrl+Q quit. The language is the system's (TR, lang/fr.txt).
//
// Files: the shipped levels SD:/apps/critters.app/levels/*.level, the player's SD:/docs/critters/*.level, a .level given
// as the argument (a double click in the File Viewer: played at once, or the picker with its error); the progress and the
// settings SD:/apps/critters.app/progress.ini (FileKit's fk_kv: written at each won level, the sound's switch, the last
// level played). For the tests and the level makers: "<file.level> --replay <file.sol> [--until <step>|end]" plays the
// level with a recorded solution; --until runs it at once (headless: the world is deterministic) to that step, paused
// there, or to its end. The core (no UI, integers only): terrain.h, level.h, world.h, solution.h, progress.h.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "uikit/lcd.h"
#include "uikit/lang.h"
#include "fontkit/uikitface.h"
#include "filekit/filekit.h"
#include "../games/game.h"
#include "terrain.h"
#include "level.h"
#include "world.h"
#include "solution.h"
#include "progress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uikit;
using namespace critters;

#define APPDIR		"SD:/apps/critters.app/"
#define LEVELDIR	APPDIR "levels"
#define USERDIR		"SD:/docs/critters"
#define PROGRESS	APPDIR "progress.ini"

#include "draw.h"

// ---- the levels of the picker (their facts only: one full Level is kept, for the one played -- 05 note 5) -----------
struct Entry
{
	char path[256], file[96], section[128];
	int group;				// 0 Training, 1 Expedition (shipped), 2 the player's (SD:/docs/critters), 3 elsewhere
	int chain;				// its place in the opening chain (the shipped levels in order), -1 none
	int num;				// its number in its group (1...)
	bool ok; char err[300];			// refused by the reader: why (translated): "line 24: unknown block [shap]"
	char name[2][NAMEC * 4 + 4], hint[2][HINTL];
	int w, h, count, save, timeSec, rate, roles[NROLES];
	Point hatch[MAXHATCH], exit[MAXEXIT]; int nhatch, nexit;
	Canvas *thumb;				// the preview's picture (made when first shown)
};
enum { MAXENT = 48, MAXEXTRA = 8 };
static Entry *g_ent[MAXENT]; static int g_nent = 0, g_sel = 0;
static char g_extra[MAXEXTRA][256]; static int g_nextra = 0;	// level files opened this run from elsewhere
static const char *g_chain[MAXENT]; static int g_nchain = 0;	// the shipped levels' sections, in order
static fk_kv *g_prog;
static Level *g_scan;				// the picker's scratch level (the facts, the previews)
static Terrain *g_scanT;
static void select_entry (int i);
static void play_selected ();
static bool locked (int i) { const Entry &e = *g_ent[i]; return e.chain > 0 && !progress_open (g_prog, g_chain, g_nchain, e.chain); }
static int prev_in_chain (int i) { for (int k = 0; k < g_nent; k++) if (g_ent[k]->chain >= 0 && g_ent[k]->chain == g_ent[i]->chain - 1) return k; return -1; }
static int group_size (int g) { int n = 0; for (int i = 0; i < g_nent; i++) if (g_ent[i]->group == g) n++; return n; }
static void make_thumb (Entry &e, int pw, int ph);
static void slot_action (int kind);
static void slot_wheel (int wheel);
static void slot_hover (int kind);
static void minimap_scroll (int lx);

#include "picker.h"
#include "bar.h"

// ---- the progress file ----------------------------------------------------------------------------------------------
static void save_progress ()
{
	if (fk_kv_save (g_prog, PROGRESS, "Critters -- the levels solved, the best results, the settings (written by the game)") == 0) printf ("critters: progress written\n");
	else printf ("critters: progress.ini cannot be written\n");
	fflush (stdout);
}

// ---- reading the levels -----------------------------------------------------------------------------------------------
static bool ends_level (const char *name) { int l = (int) strlen (name); return l > 6 && !fs_ci_cmp (name + l - 6, ".level"); }
// The reader's refusal in the system's language: "line 24: " + the reason's format translated, then filled
static void error_text (const LoadError &e, char *out, int cap)
{
	char r[240];
	if (e.fmt && e.fmt[0])
	{
		const char *a0 = e.arg[0];
		if (!strcmp (e.fmt, "too many %s (max %s)")) a0 = TR (e.arg[0]);	// (the things counted are words too)
		snprintf (r, sizeof r, TR (e.fmt), a0, e.arg[1]);
	}
	else snprintf (r, sizeof r, "%s", e.reason);
	if (e.line > 0) { char l[56]; snprintf (l, sizeof l, TR ("line %d: "), e.line); snprintf (out, cap, "%s%s", l, r); }
	else snprintf (out, cap, "%s", r);
}
// A text file read whole (at most cap bytes) -> malloc'd, or 0 (tooBig set when it was too big)
static char *read_text (const char *path, unsigned cap, bool &tooBig)
{
	tooBig = false;
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned n = kapi_fsize (f);
	char *text = 0;
	if (n > cap) tooBig = true;
	else if ((text = (char *) malloc (n + 1)) != 0)
	{
		int got = kapi_read (f, text, n);
		if (got < 0 || (unsigned) got != n) { free (text); text = 0; }
		else text[got] = 0;
	}
	kapi_close (f);
	return text;
}
// A level file into lv -> true; false: e says why
static bool read_level (const char *path, Level &lv, LoadError &e)
{
	memset (&e, 0, sizeof e);
	bool big; char *text = read_text (path, MAXFILE, big);
	if (!text) { load_error_file (e, big); return false; }
	bool ok = load_level (text, lv, e);
	free (text);
	return ok;
}
static Entry *load_entry (const char *path, int group)
{
	Entry *e = new Entry;
	memset (e, 0, sizeof *e);
	snprintf (e->path, sizeof e->path, "%s", path);
	snprintf (e->file, sizeof e->file, "%s", fs_basename (path));
	e->group = group; e->chain = -1;
	progress_section (path, group < 2, e->section, sizeof e->section);
	LoadError err;
	if (read_level (path, *g_scan, err))
	{
		const Level &l = *g_scan;
		e->ok = true;
		snprintf (e->name[0], sizeof e->name[0], "%s", l.name.get (0)); snprintf (e->name[1], sizeof e->name[1], "%s", l.name.get (1));
		snprintf (e->hint[0], sizeof e->hint[0], "%s", l.hint.get (0)); snprintf (e->hint[1], sizeof e->hint[1], "%s", l.hint.get (1));
		e->w = l.w; e->h = l.h; e->count = l.count; e->save = l.save; e->timeSec = l.timeSec; e->rate = l.rate;
		memcpy (e->roles, l.roles, sizeof e->roles);
		e->nhatch = l.nhatch; e->nexit = l.nexit;
		for (int i = 0; i < l.nhatch; i++) e->hatch[i] = l.hatch[i].at;
		for (int i = 0; i < l.nexit; i++) e->exit[i] = l.exit[i];
	}
	else
	{
		error_text (err, e->err, sizeof e->err);
		if (err.line > 0) printf ("critters: refused %s: line %d: %s\n", e->file, err.line, err.reason);
		else printf ("critters: refused %s: %s\n", e->file, err.reason);
		fflush (stdout);
	}
	return e;
}
static void free_entries () { for (int i = 0; i < g_nent; i++) { delete g_ent[i]->thumb; delete g_ent[i]; } g_nent = 0; g_nchain = 0; }
static int find_entry (const char *path) { for (int i = 0; i < g_nent; i++) if (!fs_ci_cmp (g_ent[i]->path, path)) return i; return -1; }
static void scan_dir (const char *dir, bool shipped)
{
	enum { MAXN = 40 };
	static char names[MAXN][128]; int n = 0;
	void *d = kapi_opendir (dir);
	if (!d) return;
	struct kapi_dirent de;
	while (kapi_readdir (d, &de) && n < MAXN)
		if (!de.is_dir && ends_level (de.name)) snprintf (names[n++], sizeof names[0], "%s", de.name);
	kapi_closedir (d);
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (names[j - 1], names[j]) > 0; j--)
	{ char t[128]; memcpy (t, names[j], 128); memcpy (names[j], names[j - 1], 128); memcpy (names[j - 1], t, 128); }
	if (shipped)						// Training first, then Expedition (each in its files' <nn> order)
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < n && g_nent < MAXENT; i++)
			{
				bool tr = !strncmp (names[i], "training-", 9);
				if ((pass == 0) != tr) continue;
				char p[256]; if (snprintf (p, sizeof p, "%s/%s", dir, names[i]) >= (int) sizeof p) continue;
				g_ent[g_nent++] = load_entry (p, pass);
			}
	else
		for (int i = 0; i < n && g_nent < MAXENT; i++)
		{
			char p[256]; if (snprintf (p, sizeof p, "%s/%s", dir, names[i]) >= (int) sizeof p) continue;
			if (find_entry (p) < 0) g_ent[g_nent++] = load_entry (p, 2);
		}
}
static bool in_dir (const char *path, const char *dir)
{
	int l = (int) strlen (dir);
	if ((int) strlen (path) <= l || path[l] != '/' || strchr (path + l + 1, '/')) return false;
	char head[256]; snprintf (head, sizeof head, "%.*s", l, path);
	return !fs_ci_cmp (head, dir);
}
// Every level read again (the shipped, the player's, those opened this run); the chosen one kept by its path, else the
// last played, else the first open level not solved
static void load_entries (const char *keep)
{
	char keepPath[256]; snprintf (keepPath, sizeof keepPath, "%s", keep ? keep : "");
	free_entries ();
	scan_dir (LEVELDIR, true);
	scan_dir (USERDIR, false);
	for (int i = 0; i < g_nextra && g_nent < MAXENT; i++) if (find_entry (g_extra[i]) < 0) g_ent[g_nent++] = load_entry (g_extra[i], 3);
	int num[4] = { 0, 0, 0, 0 };
	for (int i = 0; i < g_nent; i++)
	{
		Entry &e = *g_ent[i];
		e.num = ++num[e.group];
		if (e.group < 2) { e.chain = g_nchain; g_chain[g_nchain++] = e.section; }
	}
	g_sel = 0;
	int k = keepPath[0] ? find_entry (keepPath) : -1;
	if (k < 0)
	{
		const char *last = progress_setting (g_prog, "last", "");
		for (int i = 0; i < g_nent && last[0]; i++) if (!strcmp (g_ent[i]->section, last)) { k = i; break; }
	}
	if (k < 0)
		for (int i = 0; i < g_nent; i++)
			if (g_ent[i]->ok && !locked (i) && !progress_get (g_prog, g_ent[i]->section).solved) { k = i; break; }
	if (k >= 0) g_sel = k;
}
// The preview's picture: the level's terrain built (the scratch level) and scaled down to pw x ph
static void make_thumb (Entry &e, int pw, int ph)
{
	if (pw < 4 || ph < 4) return;
	LoadError err;
	if (!read_level (e.path, *g_scan, err) || !build_terrain (*g_scan, *g_scanT)) return;
	paint_labels (*g_scan, *g_scanT);
	Canvas *c = new Canvas;
	if (!c->alloc (pw, ph)) { delete c; return; }
	const Terrain &t = *g_scanT;
	for (int y = 0; y < ph; y++)
		for (int x = 0; x < pw; x++)
		{
			int lx = x * t.w / pw, ly = y * t.h / ph;
			c->px[y * c->stride + x] = t.col[ly * t.w + lx];
		}
	e.thumb = c;
}

// ---- the sounds of the world's events (game.h's sfx: AudioKit's FM voices) ----------------------------------------------
static void play_sound (int kind)
{
	switch (kind)
	{
	case E_HATCH_OPEN: sfx_later (0, 330, 90, SOUND_TRIANGLE, 90); sfx_later (90, 440, 140, SOUND_TRIANGLE, 90); break;
	case E_OUT: sfx (660, 25, SOUND_TRIANGLE, 50); break;
	case E_ROLE: sfx (880, 40, SOUND_SQUARE, 60); break;
	case E_REFUSE: sfx (160, 90, SOUND_SQUARE, 70); break;
	case E_BRICK_WARN: sfx (1047, 30, SOUND_TRIANGLE, 60); break;
	case E_SPLAT: sfx (110, 120, SOUND_NOISE, 80); break;
	case E_DROWN: sfx (220, 160, SOUND_TRIANGLE, 80); break;
	case E_BURN: sfx (180, 140, SOUND_NOISE, 80); break;
	case E_TICK: sfx (1320, 20, SOUND_SQUARE, 40); break;
	case E_BURST: sfx (90, 220, SOUND_NOISE, 110); break;
	case E_SAVED: sfx_later (0, 1175, 50, SOUND_TRIANGLE, 70); sfx_later (60, 1568, 80, SOUND_TRIANGLE, 70); break;
	case E_VOID: sfx (150, 100, SOUND_TRIANGLE, 60); break;
	}
}
static void blip () { sfx (196, 70, SOUND_SQUARE, 70); }

// ---- the words for a creature (the status line: what is under the pointer, and why it would refuse) ---------------------
static const char *state_word (const Critter &k)
{
	switch (k.state)
	{
	case S_FALL: return k.flags & F_FLOATER ? TR ("Floating") : TR ("Falling");
	case S_WALK: return TR ("Walker");
	case S_CLIMB: return TR ("Climbing");
	case S_BLOCK: return TR ("Blocker");
	case S_BUILD: return TR ("Builder");
	case S_SHRUG: return TR ("Out of bricks");
	case S_DIG: return TR ("Digger");
	case S_EXIT: return TR ("Leaving");
	default: return "";
	}
}
static const char *refusal_word (int r, int role)
{
	switch (r)
	{
	case NO_COUNT: return TR ("none left");
	case LEAVING: return TR ("leaving");
	case ALREADY: return TR ("has it already");
	case IS_BLOCKER: return TR ("a blocker keeps blocking");
	case COUNTING_DOWN: return TR ("already counting down");
	case NOT_ON_GROUND: return role == R_BLOCKER ? TR ("cannot block now") : role == R_BUILDER ? TR ("cannot build now") : TR ("cannot dig now");
	default: return "";
	}
}

// ---- the widgets --------------------------------------------------------------------------------------------------------
enum { WW = 800, WH = 448, PLAYH = 320, STATUSY = 320, STATUSH = 24, BARY = 344 };
class CrittersView;
static GameRoot *g_root;
static CrittersView *g_view;
static StatusLine *g_status;
static BarFace *g_barFace;
static SkillSlot *g_slot[NSLOT];
static LcdDisplay *g_rate;
static ToolButton *g_minus, *g_plus;
static MiniMap *g_mini;
static LevelList *g_list;
static Preview *g_preview;
static LevelInfo *g_info;
static Legend *g_legend;
static ToolButton *g_play;
static Button *g_btResume, *g_btRestart, *g_btBack, *g_btNuke, *g_btCancel, *g_btRetry, *g_btNext, *g_btLevels;
static Widget *g_help;				// "How to play" over the picker
static Menu g_menu;
static int g_hoverSlot = -1;

static void show (Widget *w, bool on) { if (w->hidden == on) { w->hidden = !on; w->invalidate (true); } }
static void relayout ();
static void go_picker ();
static void start_level (int i);
static void open_path (const char *path);

// The "How to play" card (04 §4.3): the goal, then the six roles with their key, picture, name and what they do
static const char *const ROLE_DESC[NBUILT] = { TRN ("climbs walls instead of turning"), TRN ("survives any fall under its leaf"), TRN ("stands still: the others turn back"),
					       TRN ("lays a stair of 12 bricks"), TRN ("digs straight down through earth"), TRN ("bursts after 5 seconds, with the earth") };
static void card_frame (Canvas &cv, int x, int y, int w, int h, const char *title, int &cy)
{
	int th = uk_fh () + 10;
	uk_rbox (cv, x, y, w, h, 8, C_FACE, C_FACE);
	uk_title_strip (cv, x + 1, y + 1, w - 2, th, title, 7);
	uk_rline (cv, x, y, w, h, 8, UK_OUTLINE == 2 ? 0 : uk_tone (C_FRAME_ACTIVE, 44), 255);
	cy = y + th;
}
enum { HELPW = 620, HELPH = 296 };
static void draw_help (Canvas &cv, int x, int y0)
{
	int y; card_frame (cv, x, y0, HELPW, HELPH, TR ("How to play"), y);
	char b[300];
	const char *s = TR ("Critters come out of the hatch and walk on. Click a role, then a critter, to give it a job. Lead enough of them to the glowing exit before the time runs out.");
	int st[4], lnn[4]; int nl = uk_text_wrap (s, (int) strlen (s), HELPW - 40, 3, st, lnn);
	for (int i = 0; i < nl; i++) { snprintf (b, sizeof b, "%.*s", lnn[i], s + st[i]); uk_text (cv, x + 20, y + 8 + i * uk_fh (), b, C_TEXT); }
	for (int r = 0; r < NBUILT; r++)
	{
		int col = r / 3, row = r % 3, rx = x + 16 + col * (HELPW / 2 - 8), ry = y + 68 + row * 56;
		uk_sunken (cv, rx, ry, 48, 48, 6, uk_tone (C_BG, 120)); role_icon (cv, r, rx + 24, ry + 24, 38, false);
		snprintf (b, sizeof b, "%d  %s", r + 1, TR (ROLE_NAME[r])); uk_text_l (cv, rx + 58, ry + 2, 22, b, C_TEXT, 2);
		const char *ds = TR (ROLE_DESC[r]); int st2[3], ln2[3]; int n2 = uk_text_wrap (ds, (int) strlen (ds), HELPW / 2 - 90, 2, st2, ln2);
		for (int i = 0; i < n2; i++) { snprintf (b, sizeof b, "%.*s", ln2[i], ds + st2[i]); uk_text (cv, rx + 58, ry + 24 + i * (uk_fh () - 1), b, C_DIS); }
	}
	UkFaceScope sc (face (11)); uk_text_c (cv, x, y0 + HELPH - 26, HELPW, 18, TR ("Esc or F1: close"), C_DIS);
}
class HelpCard : public Widget
{
public:
	HelpCard () : Widget ((WW - HELPW) / 2, (WH - HELPH) / 2, HELPW, HELPH) { canFocus = true; }
	void onDraw () override { canvas.clear (C_BG); draw_help (canvas, 0, 0); }
	bool onKey (long k) override { if (k == 27 || k == KEY_F1 || k == KEY_ENTER || k == ' ') { show (this, false); g_list->setFocus (); } return true; }
	bool onMouse (int mx, int, int bl, int, int, int) override { if (mx >= 0 && bl) { show (this, false); g_list->setFocus (); } return true; }
};

// ---- the play area ----------------------------------------------------------------------------------------------------
enum { M_PICKER, M_PLAY };
enum { O_NONE, O_CARD, O_MENU, O_NUKE, O_END, O_HELP };
static const char *const OVERLAY_NAME[] = { "none", "card", "menu", "nuke", "end", "help" };
struct Part { float x, y, vx, vy; int life, max; unsigned col; bool square; };	// a burst's particle (drawing only)
enum { MAXPART = 96 };

class CrittersView : public GameView
{
public:
	Level *lv; World *w; int cur;		// the level played (one kept), its world, its entry
	int mode, overlay, prevOverlay;
	int vx;					// the view's left edge, logical px
	Clock clk; bool userPaused;
	int role;				// the role chosen (-1 none)
	int focusC; bool kbFocus;		// the creature highlighted by Tab (and whether the keyboard has the highlight)
	bool inside;				// the pointer is over the area
	Solution *sol; Replay rp;		// --replay
	unsigned hatchAt;			// when the door opened (0: closed)
	unsigned endAt; bool ended;		// the end seen at; handled
	int endSaved, endTime; bool newBest, hasNext; int nextEntry;
	int dragX, dragVx;			// a right-button drag
	int miniStep;				// the minimap made at this step
	int refuseWho; unsigned refuseUntil;	// the brackets flashing red
	Part part[MAXPART]; int npart;

	CrittersView () : GameView (0, 0, WW, PLAYH), lv (new Level), w (new World), cur (-1), mode (M_PICKER), overlay (O_NONE), prevOverlay (O_NONE), vx (0),
		userPaused (false), role (-1), focusC (-1), kbFocus (false), inside (false), sol (0), hatchAt (0), endAt (0), ended (false),
		endSaved (0), endTime (0), newBest (false), hasNext (false), nextEntry (-1), dragX (0), dragVx (0), miniStep (0), refuseWho (-1), refuseUntil (0), npart (0)
	{ memset (lv, 0, sizeof *lv); }

	Entry &E () const { return *g_ent[cur]; }
	int ox () const { return lv->w * 2 < width ? (width - lv->w * 2) / 2 : 0; }
	int SX (int x) const { return ox () + (x - vx) * 2 + 1; }
	int SY (int y) const { return (y + 1) * 2; }
	int maxVx () const { int m = lv->w - width / 2; return m > 0 ? m : 0; }
	void clampVx () { if (vx > maxVx ()) vx = maxVx (); if (vx < 0) vx = 0; }
	bool running () const { return mode == M_PLAY && overlay == O_NONE && !userPaused && hatchAt && w->result == PLAYING; }
	bool modal () const { return overlay != O_NONE; }

	// ---- a level begins: the world reset, the view at its start; the start card (or the replay, no card) ----
	bool begin (int i, Solution *s)
	{
		LoadError err;
		if (!read_level (g_ent[i]->path, *lv, err) || !w->reset (*lv)) return false;
		paint_labels (*lv, w->t);
		w->t.take_dirty ();
		g_brickCol = lv->brick;
		cur = i; mode = M_PLAY; prevOverlay = O_NONE;
		vx = lv->start >= 0 ? lv->start : lv->hatch[0].at.x - width / 4;
		clampVx ();
		clk = Clock (); userPaused = false; role = -1; focusC = -1; kbFocus = false;
		delete sol; sol = s; rp.start (sol);
		hatchAt = 0; endAt = 0; ended = false; newBest = false; hasNext = false; nextEntry = -1;
		npart = 0; refuseWho = -1; miniStep = -100;
		for (int r = 0; r < NBUILT; r++) if (w->roles[r] > 0) { role = r; break; }	// (the first role it gives, chosen)
		g_mini->stale = true;
		printf ("critters: playing %s\n", lv->name.en); fflush (stdout);
		return true;
	}
	void setOverlay (int o);
	void openHatch (bool sound) { if (!hatchAt) { hatchAt = gms (); if (!hatchAt) hatchAt = 1; if (sound) play_sound (E_HATCH_OPEN); } }
	void startPlaying ()				// the start card dismissed: the door opens, the world runs
	{
		setOverlay (O_NONE);
		openHatch (true);
	}
	// --until: the world run at once to that step (or its end), the sounds dropped (05 note 3); then paused there
	void runUntil (int until)
	{
		openHatch (false);
		while (w->result == PLAYING && (until < 0 || w->step < until)) { rp.apply_due (*w); w->tick (); }
		g_mini->stale = true;
		if (w->result != PLAYING) { onEnd (false); setOverlay (O_END); }
		else { userPaused = true; printf ("critters: overlay pause\n"); fflush (stdout); }
	}
	// ---- the world's events: sounds, particles, the end ----
	void events ()
	{
		int sounds = 0;
		for (int i = 0; i < w->nev; i++)
		{
			const Event &e = w->ev[i];
			if (e.kind == E_BURST) burst (e.x, e.y - BURST_UP);
			if (e.kind == E_END) { onEnd (true); continue; }
			if (e.kind == E_HATCH_OPEN) continue;	// (the door: when the start card went)
			if (sounds < 3) { play_sound (e.kind); sounds++; }
		}
	}
	void burst (int x, int y)
	{
		unsigned s = (unsigned) (x * 7919 + y * 104729) | 1u;
		unsigned earth = lv->nshape ? lv->shape[0].colour : 0x8A5A34;
		for (int i = 0; i < 22 && npart < MAXPART; i++)
		{
			s = s * 1103515245u + 12345u; float a = (s >> 8) % 628 / 100.0f;
			s = s * 1103515245u + 12345u; float sp = 0.6f + (s >> 8) % 100 / 60.0f;
			Part &p = part[npart++];
			p.x = (float) x; p.y = (float) y; p.vx = cosf (a) * sp; p.vy = sinf (a) * sp - 1.2f;
			p.max = p.life = 18 + (int) ((s >> 4) % 14); p.square = i % 3 != 0;
			p.col = p.square ? uk_tone (earth, 100 + (i * 13) % 60) : (i % 2 ? 0xFFE060 : 0xFF8A30);
		}
	}
	void onEnd (bool live)
	{
		if (ended) return;
		ended = true; endAt = gms (); if (!endAt) endAt = 1;
		endSaved = w->saved; endTime = w->endStep / STEPS_PER_SEC;
		bool won = w->result == WON;
		printf ("critters: end %s %d/%d %ds\n", won ? "won" : "lost", w->saved, lv->save, endTime); fflush (stdout);
		if (won)
		{
			newBest = progress_won (g_prog, E ().section, w->saved, endTime);
			save_progress ();
			int c = E ().chain;
			if (c >= 0) for (int k = 0; k < g_nent; k++) if (g_ent[k]->chain == c + 1 && g_ent[k]->ok) { nextEntry = k; break; }
			hasNext = nextEntry >= 0;
		}
		if (live) { if (won) sfx_win (); else sfx_lose (); }
	}

	// ---- giving a role (between steps: the world is never changed mid-step) ----
	void give (int who)
	{
		if (who < 0) return;
		if (userPaused || !running ()) { blip (); return; }
		if (role < 0) { blip (); return; }
		int r = w->assign (who, role);
		printf ("critters: role %s c%d %s (%d left)\n", ROLE_WORD[role], who, r == OK ? "ok" : "refused", w->roles[role]); fflush (stdout);
		play_sound (r == OK ? E_ROLE : E_REFUSE);
		if (r != OK) { refuseWho = who; refuseUntil = gms () + 400; }
	}
	void chooseRole (int r)
	{
		if (mode != M_PLAY) return;
		if (r < 0 || r >= NBUILT || w->roles[r] <= 0) { blip (); return; }
		role = r;
	}
	void chooseNext (int d)
	{
		for (int k = 1; k <= NBUILT; k++)
		{
			int r = ((role < 0 ? (d > 0 ? -1 : 0) : role) + d * k + 2 * NBUILT) % NBUILT;
			if (w->roles[r] > 0) { role = r; return; }
		}
	}
	// The creature under the pointer (the cursor's rule: World::pick) -> -1 none
	int under () const
	{
		if (!inside || mode != M_PLAY) return -1;
		int lx = (mx - ox ()) / 2 + vx, ly = my / 2;
		if (mx < ox ()) return -1;
		return w->pick (lx, ly, role);
	}
	int target () const { return kbFocus ? focusC : under (); }
	bool inView (int i) const { int sx = SX (w->c[i].x); return w->alive (i) && sx >= 0 && sx < width; }
	void tabFocus (bool back)
	{
		int list[MAXCRIT], n = 0;				// the creatures in view, left to right (then release order)
		for (int i = 0; i < w->nout; i++) if (inView (i) && w->c[i].state != S_EXIT) list[n++] = i;
		for (int a = 1; a < n; a++) for (int b = a; b > 0 && w->c[list[b - 1]].x > w->c[list[b]].x; b--) { int t = list[b]; list[b] = list[b - 1]; list[b - 1] = t; }
		if (!n) { focusC = -1; kbFocus = false; return; }
		int at = -1; for (int k = 0; k < n; k++) if (list[k] == focusC) at = k;
		at = at < 0 ? (back ? n - 1 : 0) : (at + (back ? n - 1 : 1)) % n;
		focusC = list[at]; kbFocus = true;
	}
	void togglePause ()
	{
		if (mode != M_PLAY || modal ()) return;
		if (!hatchAt) return;
		userPaused = !userPaused;
		printf ("critters: overlay %s\n", userPaused ? "pause" : "none"); fflush (stdout);
	}
	void toggleFast () { if (mode == M_PLAY && !modal ()) clk.fast = !clk.fast; }
	void askNuke ()
	{
		if (mode != M_PLAY || modal () || w->result != PLAYING) return;
		if (w->nuking || (w->inPlay () == 0 && w->nout >= lv->count) || !hatchAt) { blip (); return; }
		setOverlay (O_NUKE);
	}
	void nukeNow () { if (overlay == O_NUKE) { setOverlay (O_NONE); w->nuke (); } }
	void setRate (int d)
	{
		if (mode != M_PLAY || w->result != PLAYING) return;
		int r = w->rate + d; w->set_rate (r);
	}
	void help () { if (mode == M_PLAY && overlay != O_HELP) { prevOverlay = overlay; setOverlay (O_HELP); } else if (mode != M_PLAY) { show (g_help, true); g_help->setFocus (); } }
	static void toggleSound ()
	{
		sfx_set_mute (!g_sfx_mute);
		progress_set_setting (g_prog, "sound", g_sfx_mute ? "0" : "1");
		save_progress ();
	}
	// the overlays' buttons that show, in order (the keyboard moves between them)
	int cardButtons (Button **b)
	{
		int n = 0;
		if (overlay == O_MENU) { b[n++] = g_btResume; b[n++] = g_btRestart; b[n++] = g_btBack; }
		else if (overlay == O_NUKE) { b[n++] = g_btNuke; b[n++] = g_btCancel; }
		else if (overlay == O_END) { b[n++] = g_btRetry; if (hasNext) b[n++] = g_btNext; b[n++] = g_btLevels; }
		return n;
	}
	void moveFocus (int d)
	{
		Button *b[4]; int n = cardButtons (b); if (!n) return;
		int at = 0; for (int k = 0; k < n; k++) if (((Widget *) b[k])->hasFocus) at = k;
		b[(at + d + n) % n]->setFocus ();
	}

	// ---- the keys ----
	bool key (long k) override
	{
		if (mode == M_PICKER)
		{
			if (k == 27) { kapi_exit (0); return true; }
			if (k == 'm' || k == 'M') { toggleSound (); return true; }
			if (k == KEY_F1) { help (); return true; }
			return false;
		}
		switch (overlay)
		{
		case O_CARD:
			if (k == 27) go_picker ();
			else if (k == KEY_F1) help ();
			else startPlaying ();
			return true;
		case O_HELP:
			if (k == 27 || k == KEY_F1 || k == KEY_ENTER || k == ' ') setOverlay (prevOverlay);
			return true;
		case O_MENU: case O_NUKE: case O_END:			// (the focused button took Enter / Space)
			if (k == 27) { if (overlay == O_END) go_picker (); else setOverlay (O_NONE); }
			else if (overlay == O_NUKE && (k == 'n' || k == 'N' || k == KEY_ENTER)) nukeNow ();
			else if (k == KEY_TAB) moveFocus ((kapi_get_modifiers () & MOD_SHIFT) ? -1 : 1);
			else if (k == KEY_LEFT || k == KEY_UP) moveFocus (-1);
			else if (k == KEY_RIGHT || k == KEY_DOWN) moveFocus (1);
			return true;
		}
		if (k >= '1' && k <= '8') { chooseRole ((int) (k - '1')); return true; }
		switch (k)
		{
		case KEY_TAB: tabFocus ((kapi_get_modifiers () & MOD_SHIFT) != 0); return true;
		case KEY_ENTER: case '\n': if (kbFocus && focusC >= 0) give (focusC); else blip (); return true;
		case 'p': case 'P': case ' ': togglePause (); return true;
		case 'f': case 'F': toggleFast (); return true;
		case 'n': case 'N': askNuke (); return true;
		case '-': case '_': setRate (-5); return true;
		case '+': case '=': setRate (5); return true;
		case KEY_LEFT: case KEY_RIGHT: return true;		// (scrolling: polled while held)
		case KEY_HOME: vx = 0; return true;
		case KEY_END: vx = maxVx (); return true;
		case 27: setOverlay (O_MENU); return true;
		case 'm': case 'M': toggleSound (); return true;
		case KEY_F1: help (); return true;
		}
		return false;
	}

	// ---- the mouse ----
	bool onMouse (int x, int y, int bl, int br, int bm, int wheel) override
	{
		if (x < 0) inside = false; else inside = true;
		bool r = GameView::onMouse (x, y, bl, br, bm, wheel);
		if (wheel && mode == M_PLAY && !modal ()) { vx -= wheel * 24; clampVx (); }
		return r;
	}
	void press (int x, int y, bool right) override
	{
		if (mode != M_PLAY) return;
		if (right) { dragX = x; dragVx = vx; return; }
		switch (overlay)
		{
		case O_CARD: startPlaying (); return;
		case O_HELP: setOverlay (prevOverlay); return;
		case O_NUKE:
		{
			int cx = (width - 400) / 2, cy = (height - 172) / 2;
			if (x < cx || x >= cx + 400 || y < cy || y >= cy + 172) setOverlay (O_NONE);
			return;
		}
		case O_NONE: break;
		default: return;
		}
		kbFocus = false;
		int t = under ();
		if (t >= 0) give (t);
	}
	void move (int x, int) override
	{
		if (rb && mode == M_PLAY) { vx = dragVx - (x - dragX) / 2; clampVx (); }
		kbFocus = false;
	}

	void tick (unsigned dt) override;
	void paint () override;
	void drawCard (int &x, int &y, int w_, int h_, const char *title) { x = (width - w_) / 2; int y0 = (height - h_) / 2; card_frame (canvas, x, y0, w_, h_, title, y); }
};

static void cb_resume (Widget &) { g_view->setOverlay (O_NONE); }
static void cb_restart (Widget &) { if (g_view->cur >= 0) start_level (g_view->cur); }
static void cb_back (Widget &) { go_picker (); }
static void cb_nuke (Widget &) { g_view->nukeNow (); }
static void cb_cancel (Widget &) { g_view->setOverlay (O_NONE); }
static void cb_next (Widget &) { if (g_view->nextEntry >= 0) start_level (g_view->nextEntry); }

// The cards' buttons over the view's cards (04 §2.1)
static void place_buttons ()
{
	int th = uk_fh () + 10;
	static int vl, vt; vl = g_view->left; vt = g_view->top;	// (the cards are centred in the view)
	auto at = [] (Widget *b, int x, int y) { b->left = vl + x; b->top = vt + y; };
	int VW = g_view->width;
	int cx = (VW - 260) / 2, cy = (PLAYH - 176) / 2 + th;
	at (g_btResume, cx + 30, cy + 12); at (g_btRestart, cx + 30, cy + 48); at (g_btBack, cx + 30, cy + 84);
	cx = (VW - 400) / 2; cy = (PLAYH - 172) / 2 + th;
	at (g_btNuke, cx + 400 - 20 - 150 - 10 - 110, cy + 82); at (g_btCancel, cx + 400 - 20 - 110, cy + 82);
	int n = g_view->hasNext ? 3 : 2, bw = 104, gap = 12;
	cx = (VW - 380) / 2; cy = (PLAYH - 232) / 2 + th;
	int bx = cx + (380 - n * bw - (n - 1) * gap) / 2;
	at (g_btRetry, bx, cy + 150); bx += bw + gap;
	if (n == 3) { at (g_btNext, bx, cy + 150); bx += bw + gap; }
	at (g_btLevels, bx, cy + 150);
}

void CrittersView::setOverlay (int o)
{
	if (o != overlay) { printf ("critters: overlay %s\n", OVERLAY_NAME[o]); fflush (stdout); }	// (the simulator's tests wait for it)
	overlay = o;
	place_buttons ();
	show (g_btResume, o == O_MENU); show (g_btRestart, o == O_MENU); show (g_btBack, o == O_MENU);
	show (g_btNuke, o == O_NUKE); show (g_btCancel, o == O_NUKE);
	show (g_btRetry, o == O_END); show (g_btNext, o == O_END && hasNext); show (g_btLevels, o == O_END);
	if (o == O_MENU) g_btResume->setFocus ();
	else if (o == O_NUKE) g_btNuke->setFocus ();
	else if (o == O_END) { if (hasNext) g_btNext->setFocus (); else g_btRetry->setFocus (); }
	else setFocus ();
	clk.acc3 = 0;
	if (g_root) g_root->invalidate (true);
	redraw ();
}

void CrittersView::tick (unsigned dt)
{
	if (mode != M_PLAY || cur < 0) return;
	// scrolling: the held arrows (Shift: faster), the pointer at the edges -- not under a card
	if (!modal ())
	{
		int sp = (kapi_get_modifiers () & MOD_SHIFT) ? 12 : 4;
		if (kapi_key_held (KEY_LEFT)) vx -= sp;
		if (kapi_key_held (KEY_RIGHT)) vx += sp;
		if (inside && !rb && my >= 0 && my < height) { if (mx < 8) vx -= sp; else if (mx >= width - 8) vx += sp; }
		clampVx ();
	}
	clk.paused = !running ();
	for (int n = clk.due (dt); n > 0 && running (); n--)
	{
		rp.apply_due (*w);
		w->tick ();
		events ();
	}
	if (ended && overlay == O_NONE && gms () - endAt >= 500) setOverlay (O_END);
	// the particles (drawing only)
	for (int i = 0; i < npart; )
	{
		Part &p = part[i];
		p.x += p.vx * dt / 25.0f; p.y += p.vy * dt / 25.0f; p.vy += 0.08f * dt / 25.0f;
		if ((p.life -= (int) (dt / 20 ? dt / 20 : 1)) <= 0) part[i] = part[--npart]; else i++;
	}
	// the minimap's picture made again when the terrain changed (at most every 10 steps)
	if ((w->t.dirty.x1 >= w->t.dirty.x0 && w->step - miniStep >= 10) || g_mini->stale)
	{
		w->t.take_dirty (); miniStep = w->step;
		g_mini->rebuild (w->t);
	}
	if (focusC >= 0 && (!inView (focusC) || w->c[focusC].state == S_EXIT)) { focusC = -1; kbFocus = false; }
	g_mini->invalidate (true);
	redraw ();
}

void CrittersView::paint ()
{
	if (mode != M_PLAY || cur < 0) { canvas.clear (C_BG); return; }
	const Terrain &t = w->t;
	int o = ox ();
	blit_terrain (canvas, t, vx, o);
	unsigned now = gms ();
	int ph = (int) (now % 1000), pulse = ph < 500 ? ph * 255 / 500 : (1000 - ph) * 255 / 500;
	for (int i = 0; i < lv->nexit; i++) portal (canvas, SX (lv->exit[i].x), SY (lv->exit[i].y), pulse);
	for (int i = 0; i < lv->nhatch; i++) hatch (canvas, SX (lv->hatch[i].at.x), SY (lv->hatch[i].at.y) - 4, hatchAt != 0);
	// the creatures (release order: the latest on top)
	for (int i = 0; i < w->nout; i++)
	{
		const Critter &k = w->c[i];
		if (k.state == S_SAVED || k.state == S_DEAD || k.state == S_BURST) continue;
		int sx = SX (k.x), sy = SY (k.y);
		if (sx < -30 || sx > width + 30) continue;
		Look lk = { D_WALK, (int) (k.frame / 3), k.dir >= 0 ? 1 : -1, k.flags & 3, k.fuse >= 0 && k.fuse <= FUSE_TICK, 0 };
		int shrink = 0, sink = 0;
		switch (k.state)
		{
		case S_FALL: lk.state = (k.flags & F_FLOATER) && k.y - k.fallFrom >= 6 ? D_FLOAT : D_FALL; lk.frame = 0; break;
		case S_CLIMB: lk.state = D_CLIMB; lk.frame = k.frame / 4; break;
		case S_BLOCK: lk.state = D_BLOCK; lk.frame = 0; break;
		case S_BUILD: lk.state = D_BUILD; lk.frame = k.timer / 2; break;
		case S_SHRUG: lk.state = D_SHRUG; lk.frame = 0; break;
		case S_DIG: lk.state = D_DIG; lk.frame = k.frame / 2; break;
		case S_SPLAT: lk.state = D_SPLAT; lk.frame = 0; lk.flags = 0; break;
		case S_EXIT: lk.frame = 0; shrink = k.timer * NSHRINK / EXIT_STEPS; if (shrink >= NSHRINK) shrink = NSHRINK - 1; break;
		case S_DROWN: lk.frame = 0; sink = k.timer * 2; break;
		case S_BURN: lk.frame = 0; lk.hot = true; break;
		default: break;
		}
		blend_frame (canvas, frame_of (lk, shrink), sx, sy, sink);
		if (k.state == S_DROWN) for (int b = 0; b < 3; b++) el (canvas, sx - 4 + b * 4, sy - 6 - ((k.timer * 2 + b * 5) % 18), 1.4, 1.4, 0xBFE4FF, 200);
		if (k.state == S_BURN) for (int b = 0; b < 4; b++) el (canvas, sx - 6 + b * 4, sy - 8 - ((k.timer * 3 + b * 7) % 20), 1.3, 1.3, b & 1 ? 0xFFB040 : 0xFF6020, 230);
		if (k.fuse > 0 && w->alive (i) && k.state != S_EXIT)
		{
			char b[4]; int dg = (k.fuse + FUSE_TICK - 1) / FUSE_TICK; snprintf (b, sizeof b, "%d", dg);
			digit (canvas, sx, sy - 41 + (k.state == S_DIG ? 4 : 0), b, 13, dg == 1 ? 0xFF6A50 : 0xFFFFFF);
		}
	}
	for (int i = 0; i < npart; i++)
	{
		const Part &p = part[i];
		int a = p.life * 255 / p.max; double sx = o + (p.x - vx) * 2, sy = p.y * 2;
		if (p.square) box (canvas, sx - 1.5, sy - 1.5, 3, 3, p.col, a); else el (canvas, sx, sy, 1.6, 1.6, p.col, a);
	}
	// the brackets on the creature a click (or Enter) would reach (D9)
	if (!modal ())
	{
		int tg = target ();
		if (tg >= 0)
		{
			bool red = (role >= 0 && w->can_take (tg, role) != OK) || (tg == refuseWho && now < refuseUntil && (now / 100) % 2 == 0);
			brackets (canvas, SX (w->c[tg].x), SY (w->c[tg].y), red ? 0xFF5A4A : 0xFFFFFF, kbFocus);
		}
	}
	if (userPaused && !modal ())				// the pause banner (D10)
	{
		VPath p; p.rect (0, 0, V (width), V (height)); p.fill (canvas, 0x000000, 70);
		const char *tt = TR ("Paused"), *s = TR ("P or Space: resume");
		UkFaceScope sc (face (13)); int bw = uk_tw (tt, 2) + uk_tw (s) + 46;
		uk_rbox (canvas, (width - bw) / 2, 10, bw, 28, 14, uk_tone (C_FACE, 150), uk_tone (C_FACE, 120), 235);
		uk_tool_glyph (canvas, WKT_PAUSE, (width - bw) / 2 + 10, 16, 16, C_TEXT);
		uk_text_l (canvas, (width - bw) / 2 + 32, 10, 28, tt, C_TEXT, 2);
		uk_text_l (canvas, (width - bw) / 2 + 40 + uk_tw (tt, 2), 10, 28, s, C_DIS);
	}
	if (clk.fast && !modal ())				// the fast pill (D6)
	{
		uk_rbox (canvas, width - 76, 10, 66, 24, 12, uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 120), 230);
		uk_tool_glyph (canvas, WKT_FORWARD, width - 68, 14, 16, uk_ink_on (C_ACCENT)); uk_text_l (canvas, width - 46, 10, 24, "\xC3\x97" "3", uk_ink_on (C_ACCENT), 2);
	}
	if (overlay == O_NONE) return;
	if (overlay != O_CARD) { VPath p; p.rect (0, 0, V (width), V (height)); p.fill (canvas, 0x000000, 120); }
	char b[200], tm[16];
	int x, y;
	switch (overlay)
	{
	case O_CARD:						// the start card (D14)
	{
		const char *h = lv->hint.get (g_lang);
		int st[6], lnn[6]; int nl = *h ? uk_text_wrap (h, (int) strlen (h), 440 - 48, 3, st, lnn) : 0;
		int cw = 440, chh = 218 - (3 - nl) * uk_fh () - (nl ? 0 : 6);
		drawCard (x, y, cw, chh, lv->name.get (g_lang));
		mmss (lv->timeSec, tm, sizeof tm);
		snprintf (b, sizeof b, TR ("Save %d of %d"), lv->save, lv->count);
		{ UkFaceScope sc (face (16)); uk_text_c (canvas, x, y + 8, cw / 2 + 40, 24, b, C_TEXT, 2); }
		uk_glyph (canvas, WKG_HISTORY, x + cw / 2 + 58, y + 20, 16, C_DIS); uk_text_l (canvas, x + cw / 2 + 72, y + 8, 24, tm, C_TEXT, 2);
		int n = 0; for (int r = 0; r < NBUILT; r++) if (lv->roles[r]) n++;
		int rx = x + (cw - n * 52) / 2;
		for (int r = 0; r < NBUILT; r++) if (lv->roles[r])
		{
			uk_sunken (canvas, rx + 4, y + 40, 44, 44, 6, uk_tone (C_BG, 120)); role_icon (canvas, r, rx + 26, y + 61, 34, false);
			snprintf (b, sizeof b, "\xC3\x97%d", lv->roles[r]); uk_text_c (canvas, rx, y + 86, 52, 18, b, C_TEXT, 2); rx += 52;
		}
		for (int i = 0; i < nl; i++) { snprintf (b, sizeof b, "%.*s", lnn[i], h + st[i]); uk_text_c (canvas, x + 24, y + 110 + i * uk_fh (), cw - 48, uk_fh (), b, C_TEXT); }
		UkFaceScope sc (face (11)); uk_text_c (canvas, x, y + 110 + nl * uk_fh () + 12, cw, 18, TR ("Click or press a key to start"), C_DIS);
		break;
	}
	case O_MENU:
		drawCard (x, y, 260, 176, TR ("Paused"));
		{ UkFaceScope sc (face (11)); uk_text_c (canvas, x, y + 124, 260, 20, TR ("Esc: resume"), C_DIS); }
		break;
	case O_NUKE:
	{
		int cw = 400; drawCard (x, y, cw, 172, TR ("All explode?"));
		nuke_icon (canvas, x + 40, y + 38, 40, false);
		const char *s = TR ("Every critter still out bursts after a 5-second countdown, and no more come out. The level then ends.");
		int st[5], lnn[5]; int nl = uk_text_wrap (s, (int) strlen (s), cw - 96, 4, st, lnn);
		for (int i = 0; i < nl; i++) { snprintf (b, sizeof b, "%.*s", lnn[i], s + st[i]); uk_text (canvas, x + 76, y + 14 + i * uk_fh (), b, C_TEXT); }
		UkFaceScope sc (face (11)); uk_text_c (canvas, x, y + 116, cw, 18, TR ("N or Enter: all explode \xC2\xB7 Esc: cancel"), C_DIS);
		break;
	}
	case O_END:						// the end card (D15)
	{
		bool won = w->result == WON; int cw = 380;
		drawCard (x, y, cw, 232, won ? TR ("Level complete!") : TR ("Not enough critters saved"));
		snprintf (b, sizeof b, "%d / %d", endSaved, lv->count);
		{ UkFaceScope sc (face (30)); uk_text_c (canvas, x, y + 8, cw, 40, b, won ? C_TEXT : RED, 2); }
		snprintf (b, sizeof b, TR ("saved (%d %%)"), lv->count ? endSaved * 100 / lv->count : 0);
		uk_text_c (canvas, x, y + 48, cw, 18, b, C_DIS);
		uk_sunken (canvas, x + 24, y + 74, cw - 48, 34, 6, uk_tone (C_BG, 140));
		uk_etch_v (canvas, x + cw / 2, y + 80, 22, uk_tone (C_BG, 140));
		snprintf (b, sizeof b, "%s  %d", TR ("Needed"), lv->save); uk_text_c (canvas, x + 24, y + 74, cw / 2 - 24, 34, b, C_TEXT, 2);
		mmss (endTime, tm, sizeof tm); snprintf (b, sizeof b, "%s  %s", TR ("Time"), tm); uk_text_c (canvas, x + cw / 2, y + 74, cw / 2 - 24, 34, b, C_TEXT, 2);
		if (won && newBest)
		{
			const char *nb = TR ("New best!"); UkFaceScope sc (face (12)); int bw = uk_tw (nb, 2) + 24;
			uk_rbox (canvas, x + (cw - bw) / 2, y + 116, bw, 22, 11, uk_tone (C_ACCENT, 150), C_ACCENT); uk_text_c (canvas, x + (cw - bw) / 2, y + 116, bw, 22, nb, uk_ink_on (C_ACCENT), 2);
		}
		else if (won) { UkFaceScope sc (face (12)); uk_text_c (canvas, x, y + 116, cw, 22, hasNext ? TR ("The next level is open.") : TR ("Well done!"), C_DIS); }
		else { UkFaceScope sc (face (12)); uk_text_c (canvas, x, y + 116, cw, 22, TR ("Try another role, or give it sooner."), C_DIS); }
		break;
	}
	case O_HELP:
		draw_help (canvas, (width - HELPW) / 2, (height - HELPH) / 2);
		break;
	}
}

// ---- the bar and the status line: updated from the world after each tick (each repainted only when it changed) ---------
void MiniMap::onDraw ()
{
	canvas.clear (bgColor ());
	uk_sunken (canvas, 0, 0, width, height, 6, 0x101418);
	CrittersView &v = *g_view;
	if (v.mode != M_PLAY || v.cur < 0 || !img.px) return;
	canvas.putOther (img, x0, y0, false);
	const Level &L = *v.lv;
	for (int i = 0; i < L.nexit; i++) { VPath p; p.circle (V (x0) + (int) (L.exit[i].x * sx * 16), V (y0) + (int) ((L.exit[i].y - 6) * sy * 16), V (3)); p.fill (canvas, 0x60F0D8); }
	for (int i = 0; i < L.nhatch; i++) uk_rbox (canvas, x0 + (int) (L.hatch[i].at.x * sx) - 3, y0 + (int) ((L.hatch[i].at.y - 8) * sy) - 2, 7, 4, 2, 0xA8783E, 0x6A5644);
	for (int i = 0; i < v.w->nout; i++)
		if (v.w->alive (i) || v.w->c[i].state == S_EXIT)
			canvas.fillRect (x0 + (int) (v.w->c[i].x * sx) - 1, y0 + (int) ((v.w->c[i].y - 4) * sy) - 1, 2, 2, 0xFFD24A);
	int vw = (int) (v.width / 2 * sx); if (vw > mw) vw = mw;
	uk_rline (canvas, x0 + (int) (v.vx * sx), y0 - 2, vw, mh + 4, 2, 0xFFFFFF, 230);
}
static void update_bar ()
{
	CrittersView &v = *g_view;
	if (v.mode != M_PLAY || v.cur < 0) return;
	World &w = *v.w;
	bool blockers = w.onlyBlockersLeft ();
	bool pulse = blockers && (gms () / 500) % 2 == 0;
	for (int i = 0; i < 8; i++) g_slot[i]->set (i < NBUILT ? w.roles[i] : 0, i == v.role, false, false);
	g_slot[K_PAUSE]->set (0, false, v.userPaused || v.overlay == O_CARD, false);
	g_slot[K_FAST]->set (0, false, v.clk.fast, false);
	g_slot[K_NUKE]->set (0, false, w.nuking, pulse);
	char b[16]; snprintf (b, sizeof b, "%d", w.rate); g_rate->setText (b);
	snprintf (b, sizeof b, "\xE2\x89\xA5%d", v.lv->rate); g_rate->setSub (b);
	bool lo = w.rate <= v.lv->rate, hi = w.rate >= 99;
	if (((Widget *) g_minus)->disabled != lo) g_minus->setDisabled (lo);
	if (((Widget *) g_plus)->disabled != hi) g_plus->setDisabled (hi);
	StatusInfo s; memset (&s, 0, sizeof s);
	s.role = v.role; s.left = v.role >= 0 ? w.roles[v.role] : 0;
	s.out = w.inPlay (); s.saved = w.saved; s.needed = v.lv->save; s.timeLeft = w.timeLeft (); s.onlyBlockers = blockers;
	if (g_hoverSlot >= 0)
	{
		static const char *const KEYN[3] = { "P", "F", "N" };
		static const char *const SLOTN[3] = { TRN ("Pause"), TRN ("Fast forward"), TRN ("All explode") };
		char k[4]; snprintf (k, sizeof k, "%d", g_hoverSlot + 1);
		const char *nm = g_hoverSlot < 8 ? TR (ROLE_NAME[g_hoverSlot]) : TR (SLOTN[g_hoverSlot - 8]);
		snprintf (s.under, sizeof s.under, TR ("%s \xC2\xB7 key %s"), nm, g_hoverSlot < 8 ? k : KEYN[g_hoverSlot - 8]);
	}
	else if (!v.modal ())
	{
		int t = v.target ();
		if (t >= 0)
		{
			const Critter &k = w.c[t];
			int n = 0;					// how many overlap there
			for (int i = 0; i < w.nout; i++)
				if (w.alive (i) && w.c[i].x >= k.x - BOX_DX * 2 && w.c[i].x <= k.x + BOX_DX * 2 && w.c[i].y >= k.y - BOX_UP && w.c[i].y <= k.y + BOX_UP) n++;
			s.nunder = n;
			char st[64]; snprintf (st, sizeof st, "%s", state_word (k));
			if (k.fuse >= 0 && w.alive (t)) { char d[16]; snprintf (d, sizeof d, " (%d)", (k.fuse + FUSE_TICK - 1) / FUSE_TICK); strncat (st, d, sizeof st - strlen (st) - 1); }
			int r = v.role >= 0 ? w.can_take (t, v.role) : OK;
			if (r != OK && r != NOT_ALIVE) { snprintf (s.under, sizeof s.under, "%s \xE2\x80\x94 %s", st, refusal_word (r, v.role)); s.underRed = true; }
			else snprintf (s.under, sizeof s.under, "%s", st);
		}
	}
	g_status->set (s);
}
static void slot_action (int kind)
{
	CrittersView &v = *g_view;
	if (v.mode != M_PLAY || v.modal ()) return;
	if (kind < 8) v.chooseRole (kind);
	else if (kind == K_PAUSE) v.togglePause ();
	else if (kind == K_FAST) v.toggleFast ();
	else v.askNuke ();
}
static void slot_wheel (int wheel) { if (g_view->mode == M_PLAY && !g_view->modal ()) g_view->chooseNext (wheel > 0 ? -1 : 1); }
static void slot_hover (int kind) { g_hoverSlot = kind; }
static void minimap_scroll (int lx) { CrittersView &v = *g_view; if (v.mode != M_PLAY) return; v.vx = lx - v.width / 4; v.clampVx (); }
static void cb_minus (Widget &) { g_view->setRate (-5); }
static void cb_plus (Widget &) { g_view->setRate (5); }

class CrRoot : public GameRoot
{
public:
	CrRoot () : GameRoot (WW, WH, TR ("Critters")) {}
	void onTick () override { GameRoot::onTick (); update_bar (); }
	void onResized () override { relayout (); }
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type != DND_FILES || g_view->mode != M_PICKER) return;
		char path[256]; int n = 0; while (data[n] && data[n] != '\n' && n < 255) { path[n] = data[n]; n++; } path[n] = 0;
		if (ends_level (path)) open_path (path);
	}
};

// ---- the screens --------------------------------------------------------------------------------------------------------
static void relayout ()
{
	bool play = g_view->mode == M_PLAY;
	Widget *playW[] = { g_view, g_status, g_barFace, g_rate, g_minus, g_plus, g_mini };
	for (Widget *w : playW) show (w, play);
	for (int i = 0; i < NSLOT; i++) show (g_slot[i], play);
	bool ok = g_sel >= 0 && g_sel < g_nent && g_ent[g_sel]->ok;
	// The window resized (PocketUI fills it): the play screen -- the field as wide as the window (more of the level
	// seen), the status line and the bar under it, the three centred in its height; the picker's list taller, the
	// preview bigger, the facts and Play under it. At the default 800 x 448 everything where it always was.
	int W = g_root->width, H = g_root->height, fy = H > WH ? (H - WH) / 2 : 0, dx = W > WW ? (W - WW) / 2 : 0, ex = H > WH ? H - WH : 0;
	g_view->left = 0; g_view->top = fy; g_view->resizeTo (W, PLAYH); g_view->clampVx ();
	g_status->left = 0; g_status->top = fy + STATUSY; g_status->resizeTo (W, STATUSH);
	g_barFace->left = 0; g_barFace->top = fy + BARY; g_barFace->resizeTo (W, WH - BARY);
	for (int i = 0; i < NSLOT; i++) { g_slot[i]->left = dx + (i < 8 ? 8 + i * 44 : 466 + (i - 8) * 44); g_slot[i]->top = fy + BARY + 6; }
	((Widget *) g_rate)->left = dx + 370; ((Widget *) g_rate)->top = fy + BARY + 6;
	((Widget *) g_minus)->left = dx + 370; ((Widget *) g_minus)->top = fy + BARY + 52;
	((Widget *) g_plus)->left = dx + 413; ((Widget *) g_plus)->top = fy + BARY + 52;
	g_mini->left = dx + 610; g_mini->top = fy + BARY + 6;
	g_help->left = (W - HELPW) / 2; g_help->top = (H - HELPH) / 2;
	g_list->resizeTo (300, H - 24);
	g_preview->resizeTo (W - 336, (ok ? 150 : 200) + ex);
	g_info->top = 174 + ex; g_info->resizeTo (W - 340, 216);
	((Widget *) g_play)->left = W - 12 - 140; ((Widget *) g_play)->top = H - 12 - 36;
	g_legend->top = H - 12 - 29; g_legend->resizeTo (W - 326 - 12 - 140 - 8, 22);
	Widget *pick[] = { g_list, g_preview, g_info, g_legend, g_play };
	for (Widget *w : pick) show (w, !play);
	if (!play) show (g_info, ok);
	else show (g_help, false);
	place_buttons ();
	g_root->invalidate (true);
}
// The picker shows level i (its preview, facts; Play for an open level that loaded)
static void select_entry (int i)
{
	if (i < 0 || i >= g_nent) return;
	g_sel = i;
	bool can = g_ent[i]->ok && !locked (i);
	g_play->setOn (can); g_play->setDisabled (!can);
	g_list->ensureVisible (i);
	relayout ();
	g_list->invalidate (true); g_preview->invalidate (true); g_info->invalidate (true); ((Widget *) g_play)->invalidate (true);
}
static void play_selected ()
{
	if (g_sel < 0 || g_sel >= g_nent) return;
	if (!g_ent[g_sel]->ok || locked (g_sel)) { blip (); return; }
	start_level (g_sel);
}
static bool begin_level (int i, Solution *s)
{
	Entry &e = *g_ent[i];
	if (strcmp (progress_setting (g_prog, "last", ""), e.section))	// (the picker's choice next time)
	{ progress_set_setting (g_prog, "last", e.section); save_progress (); }
	if (!g_view->begin (i, s)) { ft_messagebox (TR ("Critters"), TR ("Not enough memory for this level."), MB_OK); return false; }
	relayout ();
	g_view->setFocus ();
	return true;
}
static void start_level (int i)
{
	if (!begin_level (i, 0)) { go_picker (); return; }
	g_view->setOverlay (O_CARD);
	update_bar ();
}
static void go_picker ()
{
	char keep[256] = "";
	if (g_view->cur >= 0 && g_view->cur < g_nent) snprintf (keep, sizeof keep, "%s", g_ent[g_view->cur]->path);
	else if (g_sel >= 0 && g_sel < g_nent) snprintf (keep, sizeof keep, "%s", g_ent[g_sel]->path);
	g_view->mode = M_PICKER; g_view->cur = -1;
	g_view->setOverlay (O_NONE);
	load_entries (keep);				// (read again: a level corrected meanwhile)
	g_list->scroll = 0;
	select_entry (g_sel);
	g_list->setFocus ();
	printf ("critters: picker (%d levels)\n", g_nent); fflush (stdout);
}
// A level file given (the argument, Open, a drop): played at once (its start card), or the picker with its error
static int add_path (const char *path)		// its entry (read, listed for this run) -> -1: the list is full
{
	int i = find_entry (path);
	if (i >= 0) return i;
	bool user = in_dir (path, USERDIR);
	if (!user && g_nextra < MAXEXTRA) snprintf (g_extra[g_nextra++], sizeof g_extra[0], "%s", path);
	if (g_nent >= MAXENT) return -1;
	g_ent[g_nent] = load_entry (path, user ? 2 : 3); i = g_nent++;
	g_ent[i]->num = group_size (g_ent[i]->group);
	return i;
}
static void open_path (const char *path)
{
	int i = add_path (path);
	if (i < 0) return;
	if (g_ent[i]->ok) start_level (i);
	else
	{
		if (g_view->mode == M_PLAY) { g_view->mode = M_PICKER; g_view->cur = -1; g_view->setOverlay (O_NONE); }
		select_entry (i);
		g_list->setFocus ();
		printf ("critters: picker (%d levels)\n", g_nent); fflush (stdout);
	}
}

// ---- the menu -----------------------------------------------------------------------------------------------------------
static void m_restart () { if (g_view->mode == M_PLAY && g_view->cur >= 0) start_level (g_view->cur); }
static void m_pause () { g_view->togglePause (); }
static void m_fast () { g_view->toggleFast (); }
static void m_nuke () { g_view->askNuke (); }
static void m_levels ()
{
	if (g_view->mode != M_PLAY) return;
	if (g_view->overlay == O_END || g_view->overlay == O_CARD) go_picker ();
	else if (g_view->overlay == O_NONE) g_view->setOverlay (O_MENU);
}
static void m_open ()
{
	char path[256] = "";
	void *d = kapi_opendir (USERDIR);
	if (d) kapi_closedir (d);
	if (g_view->mode == M_PLAY && g_view->overlay == O_NONE) g_view->setOverlay (O_MENU);
	if (ft_file_open (path, sizeof path, d ? USERDIR : "SD:/docs", TR ("Critters levels|*.level|All files|*")) && path[0]) open_path (path);
}
static void m_sound () { CrittersView::toggleSound (); }
static void m_quit () { kapi_exit (0); }
static void m_help () { g_view->help (); }
static void m_about () { ft_messagebox (TR ("About Critters"), TR ("Critters 1.0 \xE2\x80\x94 lead the little creatures to the exit.\nMIT License."), MB_OK); }
static void build_menu ()
{
	g_menu.menu (TR ("Game"));
	g_menu.item (TR ("Restart Level"), "^R", UK_CTRL ('R'), m_restart);
	g_menu.item (TR ("Pause"), "P", 0, m_pause);
	g_menu.item (TR ("Fast Forward"), "F", 0, m_fast);
	g_menu.item (TR ("All Explode"), "N", 0, m_nuke);
	g_menu.separator ();
	g_menu.item (TR ("Levels..."), TR ("Esc"), 0, m_levels);
	g_menu.item (TR ("Open a Level File..."), "^O", UK_CTRL ('O'), m_open);
	g_menu.separator ();
	g_menu.item (TR ("Sound On / Off"), "M", 0, m_sound);
	g_menu.separator ();
	g_menu.item (TR ("Quit"), "^Q", UK_CTRL ('Q'), m_quit);
	g_menu.menu (TR ("Help"));
	g_menu.item (TR ("How to Play"), "F1", 0, m_help);
	g_menu.separator ();
	g_menu.item (TR ("About Critters"), "", 0, m_about);
	g_menu.publish ();
}

static void cb_play (Widget &) { play_selected (); }

// The command line: "[file.level] [--replay file.sol] [--until N|end]" (a path may hold spaces: it ends at " --")
static char g_argPath[256], g_solPath[256]; static int g_until = -2;	// -2: no --until, -1: to the end
static void parse_args ()
{
	static char args[768] = ""; int an = kapi_get_args (args, sizeof args);
	char *p = an > 0 ? args : (char *) "";
	for (int n = (int) strlen (p); n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\n' || p[n - 1] == '\r'); ) p[--n] = 0;
	while (*p == ' ') p++;
	char *fl = strstr (p, "--");
	while (fl && fl != p && fl[-1] != ' ') fl = strstr (fl + 2, "--");
	int pl = fl ? (int) (fl - p) : (int) strlen (p);
	while (pl > 0 && p[pl - 1] == ' ') pl--;
	snprintf (g_argPath, sizeof g_argPath, "%.*s", pl, p);
	while (fl && *fl)
	{
		if (!strncmp (fl, "--replay", 8))
		{
			fl += 8; while (*fl == ' ') fl++;
			char *e = strstr (fl, " --"); int l = e ? (int) (e - fl) : (int) strlen (fl);
			while (l > 0 && fl[l - 1] == ' ') l--;
			snprintf (g_solPath, sizeof g_solPath, "%.*s", l, fl); fl += l;
		}
		else if (!strncmp (fl, "--until", 7))
		{
			fl += 7; while (*fl == ' ') fl++;
			if (!strncmp (fl, "end", 3)) { g_until = -1; fl += 3; } else g_until = (int) strtol (fl, &fl, 10);
		}
		else fl++;
		while (*fl == ' ') fl++;
	}
}
// The argument's level played with its solution (--replay; --until): no start card -- 05 note 11
static bool replay_arg ()
{
	int i = add_path (g_argPath);
	if (i < 0 || !g_ent[i]->ok) return false;
	Solution *s = new Solution; s->n = 0;
	if (g_solPath[0])
	{
		bool big; char *text = read_text (g_solPath, 1 << 20, big);
		LoadError e; memset (&e, 0, sizeof e);
		bool read = text != 0, ok = read && load_solution (text, *s, e);
		free (text);
		if (!ok)
		{
			char m[400], r[200];
			if (!read) snprintf (r, sizeof r, "%s", TR ("cannot read the file"));
			else error_text (e, r, sizeof r);
			snprintf (m, sizeof m, "%s\n%s", fs_basename (g_solPath), r);
			printf ("critters: refused %s: %s\n", fs_basename (g_solPath), read ? e.reason : "cannot read the file"); fflush (stdout);
			ft_messagebox (TR ("Critters"), m, MB_OK);
			delete s;
			return false;
		}
	}
	if (!begin_level (i, s)) return false;
	g_view->setOverlay (O_NONE);
	if (g_until != -2) g_view->runUntil (g_until);
	else g_view->openHatch (false);
	update_bar ();
	return true;
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();
	g_lang = !strcmp (uk_lang (), "fr") ? 1 : 0;
	g_prog = fk_kv_load (PROGRESS, FK_KV_ESCAPES);
	if (!g_prog) g_prog = fk_kv_new (FK_KV_ESCAPES);
	sfx_set_mute (!strcmp (progress_setting (g_prog, "sound", "1"), "0"));
	g_scan = new Level; g_scanT = new Terrain;
	parse_args ();

	CrRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	g_view = new CrittersView; root.addChild (g_view); root.view = g_view;
	g_status = new StatusLine (0, STATUSY, WW, STATUSH); root.addChild (g_status);
	g_barFace = new BarFace (0, BARY, WW, WH - BARY); root.addChild (g_barFace);
	for (int i = 0; i < 8; i++) { g_slot[i] = new SkillSlot (8 + i * 44, BARY + 6, 44, 92, i); root.addChild (g_slot[i]); }
	g_rate = new LcdDisplay (370, BARY + 6, 84, 40, "50", TR ("RATE")); g_rate->face = face (20); g_rate->smallFace = face (10); root.addChild (g_rate);
	g_minus = (new ToolButton (41, 44, TR ("Slower release (-)"), cb_minus))->setGlyph (WKT_MINUS); g_minus->raised = true; g_minus->left = 370; g_minus->top = BARY + 52; root.addChild (g_minus);
	g_plus = (new ToolButton (41, 44, TR ("Faster release (+)"), cb_plus))->setGlyph (WKT_PLUS); g_plus->raised = true; g_plus->left = 413; g_plus->top = BARY + 52; root.addChild (g_plus);
	for (int i = 0; i < 3; i++) { g_slot[8 + i] = new SkillSlot (466 + i * 44, BARY + 6, 44, 92, 8 + i); root.addChild (g_slot[8 + i]); }
	g_mini = new MiniMap (610, BARY + 6, WW - 618, 92); root.addChild (g_mini);
	// the picker
	g_list = new LevelList (12, 12, 300, WH - 24); root.addChild (g_list);
	g_preview = new Preview (324, 12, WW - 336, 150); root.addChild (g_preview);
	g_info = new LevelInfo (326, 174, WW - 340, 216); root.addChild (g_info);
	g_play = (new ToolButton (140, 36, TR ("Play this level (Enter)"), cb_play))->setGlyph (WKT_PLAY)->setText (TR ("Play"));
	g_play->filled = true; g_play->raised = true; g_play->left = WW - 12 - 140; g_play->top = WH - 12 - 36; root.addChild (g_play);
	g_legend = new Legend (326, WH - 12 - 29, WW - 326 - 12 - 140 - 8, 22);
	g_legend->add ("\xE2\x86\x91|\xE2\x86\x93", TR ("choose")); g_legend->add (TR ("Enter"), TR ("play")); g_legend->add (TR ("Esc"), TR ("quit"));
	root.addChild (g_legend);
	// the cards' buttons (over the view's cards: shown with them)
	g_btResume = new Button (0, 0, 200, 30, TR ("Resume"), cb_resume); g_btRestart = new Button (0, 0, 200, 30, TR ("Restart Level"), cb_restart);
	g_btBack = new Button (0, 0, 200, 30, TR ("Back to the levels"), cb_back);
	g_btNuke = new Button (0, 0, 150, 30, TR ("All explode"), cb_nuke); g_btCancel = new Button (0, 0, 110, 30, TR ("Cancel"), cb_cancel);
	g_btRetry = new Button (0, 0, 104, 30, TR ("Retry"), cb_restart); g_btNext = new Button (0, 0, 104, 30, TR ("Next"), cb_next);
	g_btLevels = new Button (0, 0, 104, 30, TR ("Levels"), cb_back);
	Widget *ov[] = { g_btResume, g_btRestart, g_btBack, g_btNuke, g_btCancel, g_btRetry, g_btNext, g_btLevels };
	for (Widget *w : ov) { w->hidden = true; root.addChild (w); }
	g_help = new HelpCard; g_help->hidden = true; root.addChild (g_help);

	build_menu ();
	load_entries (0);
	bool shipped = false;
	for (int i = 0; i < g_nent; i++) if (g_ent[i]->group < 2) shipped = true;
	if (!shipped && !g_argPath[0])
	{
		ft_messagebox (TR ("Critters"), TR ("No level found in SD:/apps/critters.app/levels."), MB_OK);
		return 1;
	}
	root.setResizable (true);
	root.setMinSize (WW, WH);
	relayout ();
	root.fitWorkArea ();
	if (g_argPath[0])
	{
		if (!(g_solPath[0] || g_until != -2) || !replay_arg ()) open_path (g_argPath);
	}
	else go_picker ();
	relayout ();
	root.run ();
	return 0;
}
