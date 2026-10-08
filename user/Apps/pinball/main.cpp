//
// pinball -- Pinball: a pinball table with real physics (gravity, bounces, flippers that strike, a plunger to pull),
// three tables to play (Space Station, Haunted Manor, Volcano) and any table the player writes in a text file (the
// .table format: docs/04 "Pinball"). The window: the playfield at the left, scaled to fit, letterboxed in the
// table's colour; the panel at the right -- the table's name, the score and the ball, the message line, the bonus,
// the multiplier, the best score, the tilt's dots, the goal and the table's rules as goals, the keys. Before a game,
// the picker: the tables (a small picture, the name, the best score; a table refused by the reader greyed, its error
// in red), the chosen one's preview, goal and top 5. After a game whose score enters the table's top 5, the name.
//
// Keys: Left / Z and Right / M the flippers (held), Space / Down / Enter the plunger (hold to pull, release to launch;
// a tap launches at once), Up / N nudge (three in 5 s: TILT), P pause, Esc the pause card (Resume / Back to the
// tables), S the sound, Ctrl+N a new game, Ctrl+O a table file, Ctrl+Q quit. A gamepad (gamepad.h): L / L2 / d-pad
// left and R / R2 / B the flippers, A the plunger, Y nudge, Start pause, Select the pause card; in the picker the
// d-pad chooses, A / Start play, Select quits. The language is the system's (UIKit's TR, lang/fr.txt).
//
// Files: the shipped tables SD:/apps/pinball.app/tables/*.table, the player's SD:/docs/pinball/*.table, a .table given
// as the argument (a double click in the File Viewer: played at once, or the picker with its error); the high scores
// and the settings SD:/apps/pinball.app/scores.ini (FileKit's fk_kv: written at each new entry, the sound's switch, the
// last table played). For the tests: "--seed N" fixes the game's random seed, "--start multiball" starts a 2-ball
// multiball at the first launch (then the file). The core (no UI): table.h, physics.h, rules.h, scores.h.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "uikit/lang.h"
#include "fontkit/uikitface.h"
#include "filekit/filekit.h"
#include "gamepad.h"
#include "../games/game.h"
#include "table.h"
#include "physics.h"
#include "rules.h"
#include "scores.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uikit;
using pinball::Table;
using pinball::Game;
using pinball::World;
using pinball::Vec;

#define APPDIR		"SD:/apps/pinball.app/"
#define TABLEDIR	APPDIR "tables"
#define USERDIR		"SD:/docs/pinball"
#define SCORES		APPDIR "scores.ini"

#include "draw.h"

// ---- the tables of the picker ---------------------------------------------------------------------------------------
struct TableEntry
{
	char path[256], file[96], section[128];
	bool shipped;				// in the app's folder
	bool user;				// in SD:/docs/pinball
	Table *t;				// 0: refused by the reader
	Looks look;
	char err[300];				// why (translated): "line 12: unknown block [bumber]"
	long best;
	Canvas *mini;				// the list's small picture (made when first drawn)
};
enum { MAXENT = 32, MAXEXTRA = 8 };
static TableEntry *g_ent[MAXENT]; static int g_nent = 0, g_sel = 0;
static char g_extra[MAXEXTRA][256]; static int g_nextra = 0;	// tables opened this run from elsewhere
static fk_kv *g_scores;
static void select_entry (int i);
static void play_selected ();
static void open_path (const char *path);

#include "panel.h"
#include "picker.h"

// ---- the switches of the command line (for the tests) ---------------------------------------------------------------
static bool g_seedSet = false; static unsigned g_seed = 0;
static bool g_startMultiball = false;

// ---- the scores' file -----------------------------------------------------------------------------------------------
static void save_scores ()
{
	if (fk_kv_save (g_scores, SCORES, "Pinball -- high scores (written by the game)") == 0) printf ("pinball: scores written\n");
	else printf ("pinball: scores.ini cannot be written\n");
	fflush (stdout);
}
static long best_of (const char *section)
{
	pinball::ScoreLine sl[pinball::TOPN];
	return pinball::scores_read (g_scores, section, sl) > 0 ? sl[0].score : 0;
}

// ---- reading a table ------------------------------------------------------------------------------------------------
static bool ends_table (const char *name) { int l = (int) strlen (name); return l > 6 && !fs_ci_cmp (name + l - 6, ".table"); }
// The reader's refusal in the system's language: "line 12: " + the reason's format translated, then filled
static void error_text (const pinball::LoadError &e, char *out, int cap)
{
	char r[240];
	if (e.fmt)
	{
		const char *a0 = e.arg[0];
		if (!strcmp (e.fmt, "too many %s (max %s)")) a0 = TR (e.arg[0]);	// (the things counted are words too)
		snprintf (r, sizeof r, TR (e.fmt), a0, e.arg[1]);
	}
	else snprintf (r, sizeof r, "%s", e.reason);
	if (e.line > 0) { char l[56]; snprintf (l, sizeof l, TR ("line %d: "), e.line); snprintf (out, cap, "%s%s", l, r); }
	else snprintf (out, cap, "%s", r);
}
static TableEntry *load_entry (const char *path, bool shipped, bool user)
{
	TableEntry *e = new TableEntry;
	memset (e, 0, sizeof *e);
	snprintf (e->path, sizeof e->path, "%s", path);
	snprintf (e->file, sizeof e->file, "%s", fs_basename (path));
	e->shipped = shipped; e->user = user;
	pinball::scores_section (path, shipped, e->section, sizeof e->section);
	pinball::LoadError err; memset (&err, 0, sizeof err);
	void *f = kapi_open (path);
	if (!f) pinball::load_error_file (err, false);
	else
	{
		unsigned n = kapi_fsize (f);
		if (n > pinball::MAXFILE) pinball::load_error_file (err, true);
		else
		{
			char *text = (char *) malloc (n + 1);
			int got = text ? kapi_read (f, text, n) : -1;
			if (got < 0 || (unsigned) got != n) pinball::load_error_file (err, false);
			else
			{
				text[got] = 0;
				Table *t = new Table;
				if (pinball::load_table (text, *t, err)) { e->t = t; looks_of (*t, e->look); }
				else delete t;
			}
			free (text);
		}
		kapi_close (f);
	}
	if (!e->t)
	{
		error_text (err, e->err, sizeof e->err);
		if (err.line > 0) printf ("pinball: refused %s: line %d: %s\n", e->file, err.line, err.reason);
		else printf ("pinball: refused %s: %s\n", e->file, err.reason);
		fflush (stdout);
	}
	e->best = best_of (e->section);
	return e;
}
static void free_entries ()
{
	for (int i = 0; i < g_nent; i++) { delete g_ent[i]->t; delete g_ent[i]->mini; delete g_ent[i]; }
	g_nent = 0;
}
static int find_entry (const char *path)
{
	for (int i = 0; i < g_nent; i++) if (!fs_ci_cmp (g_ent[i]->path, path)) return i;
	return -1;
}
static void scan_dir (const char *dir, bool shipped, bool user)
{
	enum { MAXN = 32 };
	static char names[MAXN][128]; int n = 0;
	void *d = kapi_opendir (dir);
	if (!d) return;
	struct kapi_dirent de;
	while (kapi_readdir (d, &de) && n < MAXN)
		if (!de.is_dir && ends_table (de.name)) snprintf (names[n++], sizeof names[0], "%s", de.name);
	kapi_closedir (d);
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (names[j - 1], names[j]) > 0; j--)
	{ char t[128]; memcpy (t, names[j], 128); memcpy (names[j], names[j - 1], 128); memcpy (names[j - 1], t, 128); }
	for (int i = 0; i < n && g_nent < MAXENT; i++)
	{
		char p[256]; if (snprintf (p, sizeof p, "%s/%s", dir, names[i]) >= (int) sizeof p) continue;
		if (find_entry (p) < 0) g_ent[g_nent++] = load_entry (p, shipped, user);
	}
}
// Every table read again (the shipped, the player's, those opened this run); the chosen one kept by its path
static void load_entries (const char *keep)
{
	char keepPath[256]; snprintf (keepPath, sizeof keepPath, "%s", keep ? keep : "");
	free_entries ();
	scan_dir (TABLEDIR, true, false);
	scan_dir (USERDIR, false, true);
	for (int i = 0; i < g_nextra && g_nent < MAXENT; i++) if (find_entry (g_extra[i]) < 0) g_ent[g_nent++] = load_entry (g_extra[i], false, false);
	g_sel = 0;
	int k = keepPath[0] ? find_entry (keepPath) : -1;
	if (k < 0)					// the last table played
	{
		const char *last = pinball::scores_setting (g_scores, "table", "");
		for (int i = 0; i < g_nent && last[0]; i++) if (!strcmp (g_ent[i]->section, last)) { k = i; break; }
	}
	if (k >= 0) g_sel = k;
}
static bool in_dir (const char *path, const char *dir) { int l = (int) strlen (dir); return !strncmp (path, dir, l) && path[l] == '/' && !strchr (path + l + 1, '/'); }

// ---- the widgets ----------------------------------------------------------------------------------------------------
enum { PANELW = 260 };
class PinballView;
static GameRoot *g_root;
static PinballView *g_view;
static Heading *g_hdName, *g_hdGoal;
static LcdDisplay *g_lcdScore, *g_lcdMsg;
static Stats *g_stats;
static RuleList *g_rules;
static Legend *g_legend;
static Heading *g_hdChoose, *g_hdTable;
static TableList *g_list;
static Thumb *g_thumb;
static ScoreList *g_scoreList;
static ToolButton *g_play;
static Legend *g_legendRow;
static Button *g_btResume, *g_btBack, *g_btOk;
static Textbox *g_tbName;
static Menu g_menu;
static bool g_padSeen = false;

static void show (Widget *w, bool on) { if (w->hidden == on) { w->hidden = !on; w->invalidate (true); } }
static void relayout ();
static void go_picker ();
static void start_game (int i);
static void set_legend ();

static const char *const ORD[5] = { TRN ("1st"), TRN ("2nd"), TRN ("3rd"), TRN ("4th"), TRN ("5th") };

// The sounds of the table's events (game.h's sfx: AudioKit's FM voices)
static void play_sound (int k)
{
	switch (k)
	{
	case pinball::P_BUMPER: sfx (880, 60, SOUND_SQUARE, 70); break;
	case pinball::P_SLING: sfx (660, 45, SOUND_SQUARE, 60); break;
	case pinball::P_TARGET: case pinball::P_DROP: sfx (523, 60, SOUND_TRIANGLE, 90); break;
	case pinball::P_BANK: sfx_later (0, 523, 80); sfx_later (90, 784, 140); break;
	case pinball::P_LANE: sfx (1175, 35, SOUND_TRIANGLE, 60); break;
	case pinball::P_LANES: sfx_later (0, 784, 70); sfx_later (80, 988, 70); sfx_later (160, 1175, 120); break;
	case pinball::P_RAMP: sfx_later (0, 392, 60); sfx_later (60, 523, 60); sfx_later (120, 659, 110); break;
	case pinball::P_SAUCER: sfx (262, 160, SOUND_TRIANGLE, 100); break;
	case pinball::P_EJECT: sfx (180, 90, SOUND_NOISE, 90); break;
	case pinball::P_FLIPPER_UP: sfx (140, 30, SOUND_NOISE, 45); break;
	case pinball::P_LAUNCH: sfx (220, 140, SOUND_NOISE, 90); break;
	case pinball::P_DRAIN: sfx (196, 120, SOUND_TRIANGLE, 110); sfx_later (130, 147, 260, SOUND_TRIANGLE, 110); break;
	}
}

// ---- the playfield --------------------------------------------------------------------------------------------------
enum { M_PICKER, M_PLAY };
enum { O_NONE, O_PAUSE, O_OVER, O_NAME, O_SCORES };
static const unsigned RED = 0xE0453A;

class PinballView : public GameView
{
public:
	Game *g; int cur;			// the game; the entry played (-1: none)
	int mode, overlay;
	Canvas layer; int layerW, layerH, layerFor;	// the static layer, cached for a table and a size
	View v;
	unsigned accUs;				// the time not stepped yet, microseconds
	bool tap, nudge;			// a plunger key event, a nudge, since the last frame
	unsigned prevPad;
	int shake;				// frames the field is still shaken (a nudge)
	bool multiballDone;
	char msg[64]; unsigned msgUntil; bool msgRed;	// the message line (msgUntil 0: none timed)
	long finalScore; int rank;		// game over: the score, its place in the top 5 (-1)

	PinballView () : GameView (0, 0, 340, 680), g (new Game), cur (-1), mode (M_PICKER), overlay (O_NONE), layerW (0), layerH (0), layerFor (-1),
		accUs (0), tap (false), nudge (false), prevPad (0), shake (0), multiballDone (false), msgUntil (0), msgRed (false), finalScore (0), rank (-1)
	{ msg[0] = 0; v.s = 1; v.ox = v.oy = 0; }

	const Table &T () const { return *g_ent[cur]->t; }
	TableEntry &E () const { return *g_ent[cur]; }

	void start (int i)
	{
		cur = i; mode = M_PLAY; overlay = O_NONE;
		g->start (T (), g_seedSet ? g_seed : (gms () * 2654435761u) ^ 0x5EEDu);
		accUs = 0; tap = nudge = false; shake = 0; multiballDone = false; msgUntil = 0; msg[0] = 0;
		layerFor = -1;
		printf ("pinball: playing %s\n", T ().name.en); fflush (stdout);
	}
	void message (const char *s, unsigned ms = 2000, bool red = false)
	{
		snprintf (msg, sizeof msg, "%s", s); msgUntil = gms () + ms; if (!msgUntil) msgUntil = 1; msgRed = red;
	}

	// ---- the frames ----
	void events ()
	{
		for (int i = 0; i < g->w.nev; i++)			// (the launch's speed: the simulator's tests read it)
			if (g->w.ev[i].kind == pinball::P_LAUNCH) { printf ("pinball: launch %d\n", (int) g->w.ev[i].speed); fflush (stdout); }
		if (g_startMultiball && !multiballDone)		// --start multiball: a 2-ball multiball at the first launch
			for (int i = 0; i < g->w.nev; i++)
				if (g->w.ev[i].kind == pinball::P_LAUNCH) { pinball::Action a = { pinball::A_MULTIBALL, 2 }; g->fire (a); multiballDone = true; break; }
		for (int i = 0; i < g->nev; i++)
		{
			const pinball::GEvent &e = g->ev[i];
			switch (e.kind)
			{
			case pinball::GE_SOUND: play_sound (e.index); break;
			case pinball::GE_MESSAGE: message (T ().rule[e.index].message.get (g_lang)); break;
			case pinball::GE_MULTIBALL: if (e.index < 0) message (TR ("MULTIBALL!")); sfx_win (); printf ("pinball: multiball\n"); fflush (stdout); break;
			case pinball::GE_EXTRABALL: if (e.index < 0) message (TR ("Extra ball!")); sfx_win (); break;
			case pinball::GE_BALLSAVED: message (TR ("Ball saved")); break;
			case pinball::GE_TILTWARN: message (TR ("Tilt warning"), 2000, true); sfx (98, 220, SOUND_SQUARE, 110); break;
			case pinball::GE_TILT: message (TR ("TILT"), 3000, true); sfx (73, 500, SOUND_SQUARE, 120); break;
			case pinball::GE_BALLEND: message (TR ("Ball lost"), 1500); printf ("pinball: ball %d lost, bonus %ld\n", g->ball, e.value); fflush (stdout); break;
			case pinball::GE_NEWBALL: msgUntil = 0; break;
			case pinball::GE_GAMEOVER: gameOver (e.value); break;
			}
		}
	}
	void gameOver (long score)
	{
		finalScore = score;
		printf ("pinball: game over %ld\n", score); fflush (stdout);
		sfx_lose ();
		message (TR ("Game over"), 600000);
		if (pinball::scores_qualifies (g_scores, E ().section, score))
		{
			pinball::ScoreLine sl[pinball::TOPN]; int n = pinball::scores_read (g_scores, E ().section, sl);
			rank = 0; while (rank < n && sl[rank].score >= score) rank++;
			setOverlay (O_NAME);
		}
		else { rank = -1; setOverlay (O_OVER); }
	}
	void confirmName ()
	{
		if (overlay != O_NAME) return;
		const char *typed = g_tbName->text; bool blank = true;
		for (const char *s = typed; *s; s++) if (*s != ' ') blank = false;
		const char *name = blank ? pinball::scores_setting (g_scores, "name", TR ("Player")) : typed;
		char clean[pinball::NAMEL]; pinball::scores_clean_name (name, clean);
		rank = pinball::scores_add (g_scores, E ().section, finalScore, clean);
		pinball::scores_set_setting (g_scores, "name", clean);
		printf ("pinball: %s rank %d: %ld %s\n", E ().section, rank + 1, finalScore, clean); fflush (stdout);
		save_scores ();
		E ().best = best_of (E ().section);
		setOverlay (O_SCORES);
	}
	void setOverlay (int o);
	void togglePause () { if (mode != M_PLAY) return; if (overlay == O_NONE) setOverlay (O_PAUSE); else if (overlay == O_PAUSE) setOverlay (O_NONE); }

	// ---- the keys ----
	bool key (long k) override
	{
		if (mode == M_PICKER)
		{
			if (k == 27) { kapi_exit (0); return true; }
			if (k == 's' || k == 'S') { toggleSound (); return true; }
			return false;
		}
		switch (overlay)
		{
		case O_PAUSE:					// (the focused button took Enter / Space)
			if (k == 'p' || k == 'P' || k == 27) setOverlay (O_NONE);
			else if (k == KEY_UP || k == KEY_DOWN) { if (((Widget *) g_btResume)->hasFocus) g_btBack->setFocus (); else g_btResume->setFocus (); }
			return true;
		case O_NAME:					// (the name's field took the rest; Enter: its callback)
			if (k == 27) confirmName ();
			return true;
		case O_OVER: case O_SCORES:
			if (k == KEY_ENTER || k == '\n' || k == ' ' || k == 27) go_picker ();
			return true;
		}
		if (g->state == pinball::G_BALLEND) { g->skip (); return true; }	// any key: the bonus count skipped
		switch (k)
		{
		case KEY_LEFT: case KEY_RIGHT: case 'z': case 'Z': case 'm': case 'M': return true;	// (the flippers: polled)
		case ' ': case KEY_DOWN: case KEY_ENTER: case '\n': tap = true; return true;	// (polled; a tap launches)
		case KEY_UP: case 'n': case 'N': nudge = true; return true;
		case 'p': case 'P': togglePause (); return true;
		case 27: setOverlay (O_PAUSE); return true;
		case 's': case 'S': toggleSound (); return true;
		}
		return false;
	}
	static void toggleSound ()
	{
		sfx_set_mute (!g_sfx_mute);
		pinball::scores_set_setting (g_scores, "sound", g_sfx_mute ? "0" : "1");
		save_scores ();
	}

	// ---- the tick ----
	void tick (unsigned dt) override;
	void paint () override;
	void card (int w, int h, const char *title, int &x, int &y)
	{
		x = (width - w) / 2; y = (height - h) / 2;
		int th = uk_fh () + 10;
		uk_rbox (canvas, x, y, w, h, 8, C_FACE, C_FACE);
		uk_title_strip (canvas, x + 1, y + 1, w - 2, th, title, 7);
		uk_rline (canvas, x, y, w, h, 8, UK_OUTLINE == 2 ? 0 : uk_tone (C_FRAME_ACTIVE, 44), 255);
		y += th;
	}
	// the overlays' cards: their places (the widgets on them are placed from these)
	void cardPlace (int o, int &x, int &y, int &w, int &h) const
	{
		w = o == O_PAUSE ? 240 : 300;
		h = o == O_PAUSE ? 150 : o == O_NAME ? 196 : o == O_SCORES ? 236 : 150;
		x = (width - w) / 2; y = (height - h) / 2 + uk_fh () + 10;
	}
};

static void name_ok (Widget &) { g_view->confirmName (); }
static void cb_resume (Widget &) { g_view->setOverlay (O_NONE); }
static void cb_back (Widget &) { go_picker (); }

// Places the overlays' widgets over the view's cards
static void place_overlay_widgets ()
{
	int x, y, w, h;
	g_view->cardPlace (O_PAUSE, x, y, w, h);
	((Widget *) g_btResume)->left = g_view->left + x + 24; ((Widget *) g_btResume)->top = g_view->top + y + 10;
	((Widget *) g_btBack)->left = g_view->left + x + 24; ((Widget *) g_btBack)->top = g_view->top + y + 48;
	g_view->cardPlace (O_NAME, x, y, w, h);
	((Widget *) g_tbName)->left = g_view->left + x + 20; ((Widget *) g_tbName)->top = g_view->top + y + 90;
	((Widget *) g_btOk)->left = g_view->left + x + w - 20 - 90; ((Widget *) g_btOk)->top = g_view->top + y + 128;
}

void PinballView::setOverlay (int o)
{
	static const char *const NAMES[] = { "none", "pause", "over", "name", "scores" };
	if (o != overlay) { printf ("pinball: overlay %s\n", NAMES[o]); fflush (stdout); }	// (the simulator's tests wait for it)
	overlay = o;
	show (g_btResume, o == O_PAUSE); show (g_btBack, o == O_PAUSE);
	show (g_tbName, o == O_NAME); show (g_btOk, o == O_NAME);
	place_overlay_widgets ();
	if (o == O_PAUSE) g_btResume->setFocus ();
	else if (o == O_NAME)
	{
		g_tbName->setText (pinball::scores_setting (g_scores, "name", TR ("Player")));
		g_tbName->setFocus ();
		g_tbName->onTabFocus ();			// (the caret at the end)
	}
	else setFocus ();
	if (o == O_NONE) accUs = 0;
	if (g_root) g_root->invalidate (true);
	redraw ();
}

void PinballView::tick (unsigned dt)
{
	unsigned pad = pad_buttons (-1), edge = pad & ~prevPad;
	prevPad = pad;
	if (pad && !g_padSeen) { g_padSeen = true; set_legend (); }
	if (mode == M_PICKER)
	{
		if (edge & PAD_UP) g_list->onKey (KEY_UP);
		if (edge & PAD_DOWN) g_list->onKey (KEY_DOWN);
		if (edge & (PAD_A | PAD_START)) play_selected ();
		else if (edge & PAD_SELECT) kapi_exit (0);
		return;
	}
	if (cur < 0) return;
	if (overlay != O_NONE)					// the pad's buttons as keys for the card's widgets
	{
		if (edge & PAD_UP) g_root->handleKey (KEY_UP);
		if (edge & PAD_DOWN) g_root->handleKey (KEY_DOWN);
		if (overlay == O_PAUSE && (edge & (PAD_START | PAD_B))) setOverlay (O_NONE);
		else if ((overlay == O_OVER || overlay == O_SCORES) && (edge & (PAD_A | PAD_START))) go_picker ();
		else if (edge & PAD_A) g_root->handleKey (KEY_ENTER);
		else if (edge & PAD_B) g_root->handleKey (27);
		return;
	}
	if (edge & (PAD_START | PAD_SELECT)) { setOverlay (O_PAUSE); return; }
	if (g->state == pinball::G_BALLEND && edge) g->skip ();
	if (edge & PAD_Y) nudge = true;
	pinball::Input in;
	in.left = kapi_key_held (KEY_LEFT) || kapi_key_held ('z') || (pad & (PAD_L | PAD_L2 | PAD_LEFT));
	in.right = kapi_key_held (KEY_RIGHT) || kapi_key_held ('m') || (pad & (PAD_R | PAD_R2 | PAD_B));
	in.plunger = kapi_key_held (' ') || kapi_key_held (KEY_DOWN) || kapi_key_held (KEY_ENTER) || (pad & PAD_A);
	accUs += dt * 1000;
	int n = 0;
	while (accUs >= 16667 && n < 3)
	{
		in.tap = tap && !in.plunger;			// (a key held is a pull, not a tap)
		bool nd = nudge && (g->state == pinball::G_PLAY || g->state == pinball::G_READY) && !g->tilted;
		g->frame (in, nudge);
		tap = nudge = false;
		if (nd) shake = 3;
		else if (shake > 0) shake--;
		events ();					// (the game's events are cleared by the next frame)
		accUs -= 16667; n++;
		if (overlay != O_NONE || mode != M_PLAY) break;
	}
	if (n == 3 && accUs >= 16667) accUs = 0;		// (late: the game slows rather than jumps)
	redraw ();
}

void PinballView::paint ()
{
	if (mode != M_PLAY || cur < 0) { canvas.clear (C_BG); return; }
	const Table &t = T ();
	if (layerFor != cur || layerW != width || layerH != height || !layer.px)
	{
		v = fit_view (t, width, height);
		if (layer.alloc (width, height))
		{
			layer.clear (uk_tone (t.background, 70));	// the letterbox's bars: the table's background, darker
			draw_static (layer, t, v, false);
			layerFor = cur; layerW = width; layerH = height;
		}
	}
	int dx = shake > 0 ? 3 : 0, dy = shake > 0 ? -2 : 0;
	if (dx || dy) canvas.clear (uk_tone (t.background, 70));
	if (layer.px) canvas.putOther (layer, dx, dy, false);
	View vv = v; vv.ox += dx; vv.oy += dy;
	draw_dynamic (canvas, t, E ().look, vv, g);
	char b[96], n[32], c[32];
	if (g->state == pinball::G_BALLEND && overlay == O_NONE)	// the bonus count (D13)
	{
		int w = 280, h = 92, x = (width - w) / 2, y = height - h - 150;
		uk_rbox (canvas, x, y, w, h, 10, uk_tone (C_FACE, 170), uk_tone (C_FACE, 126), 235);
		uk_rline (canvas, x, y, w, h, 10, uk_tone (C_FACE, 70), 220);
		uk_text_c (canvas, x, y + 8, w, 20, TR ("Bonus"), C_DIS, 2);
		fmt (g->bonus, n, sizeof n); fmt (g->lastBonus, c, sizeof c);
		if (g->tilted) snprintf (b, sizeof b, "%s", TR ("TILT"));
		else snprintf (b, sizeof b, "%s \xC3\x97 %d  =  %s", n, g->mult, c);
		{ UkFaceScope sc (face (20)); uk_text_c (canvas, x, y + 32, w, 30, b, C_TEXT, 2); }
		{ UkFaceScope sc (face (11)); uk_text_c (canvas, x, y + 64, w, 20, TR ("any key: skip"), C_DIS); }
	}
	if (overlay == O_NONE) return;
	{ VPath p; p.rect (0, 0, V (width), V (height)); p.fill (canvas, 0x000000, 130); }
	int x, y, w, h;
	cardPlace (overlay, x, y, w, h);
	switch (overlay)
	{
	case O_PAUSE:
		card (w, h, TR ("Paused"), x, y);
		uk_text_c (canvas, x, y + 92, w, 28, TR ("P or Start: resume"), C_DIS);
		break;
	case O_OVER:
		card (w, h, TR ("Game over"), x, y);
		fmt (finalScore, n, sizeof n);
		{ UkFaceScope sc (face (24)); uk_text_c (canvas, x, y + 16, w, 40, n, C_TEXT, 2); }
		uk_text_c (canvas, x, y + 70, w, 22, TR ("Enter or A: back to the tables"), C_DIS);
		break;
	case O_NAME:
		card (w, h, TR ("Game over"), x, y);
		fmt (finalScore, n, sizeof n);
		{ UkFaceScope sc (face (24)); uk_text_c (canvas, x, y + 6, w, 34, n, C_TEXT, 2); }
		snprintf (b, sizeof b, TR ("New high score: %s place!"), TR (ORD[rank >= 0 && rank < 5 ? rank : 4]));
		uk_text_c (canvas, x, y + 40, w, 20, b, uk_tone (C_ACCENT, 90), 2);
		uk_text_l (canvas, x + 20, y + 66, 20, TR ("Your name:"), C_TEXT);
		break;
	case O_SCORES:
	{
		card (w, h, TR ("Best scores"), x, y);
		{ UkFaceScope sc (face (12)); char fit[120]; uk_text_fit (t.name.get (g_lang), w - 20, fit, sizeof fit, 2); uk_text_c (canvas, x, y + 4, w, 18, fit, C_DIS, 2); }
		pinball::ScoreLine sl[pinball::TOPN]; int k = pinball::scores_read (g_scores, E ().section, sl);
		for (int i = 0; i < pinball::TOPN; i++)
		{
			int ry = y + 28 + i * 26;
			if (i == rank) uk_hilite (canvas, x + 12, ry, w - 24, 24, 5, true);
			unsigned ink = i == rank ? uk_hilite_ink (true) : C_TEXT;
			snprintf (b, sizeof b, "%d", i + 1); uk_text_l (canvas, x + 22, ry, 24, b, i == rank ? ink : C_DIS, 2);
			if (i >= k) { uk_text_l (canvas, x + 44, ry, 24, "\xE2\x80\x94", uk_mix (C_FACE, C_DIS, 150)); continue; }
			fmt (sl[i].score, n, sizeof n); int sw = uk_text_w (n, 2);
			char fit[80]; uk_text_fit (sl[i].name, w - 66 - sw - 30, fit, sizeof fit);
			uk_text_l (canvas, x + 44, ry, 24, fit, ink);
			uk_text_l (canvas, x + w - 22 - sw, ry, 24, n, ink, 2);
		}
		uk_text_c (canvas, x, y + 164, w, 22, TR ("Enter or A: back to the tables"), C_DIS);
		break;
	}
	}
}

// ---- the panel: updated from the game after each tick (each widget repainted only when its value changed) -----------
static void lcd_message (const char *s, bool red)
{
	char b[32]; int n = 0;					// (the LCD holds 31 bytes: cut at a character's start)
	while (s[n] && n < 31) n++;
	if (s[n]) while (n > 0 && ((unsigned char) s[n] & 0xC0) == 0x80) n--;
	memcpy (b, s, n); b[n] = 0;
	unsigned ink = red ? RED : UK_AUTO;
	TextFace *f = face (16);
	{ UkFaceScope sc (f); if (uk_tw (b, 0) > g_lcdMsg->width - 16) f = face (13); }
	if (g_lcdMsg->ink != ink || g_lcdMsg->face != f) { g_lcdMsg->ink = ink; g_lcdMsg->face = f; g_lcdMsg->invalidate (true); }
	g_lcdMsg->setText (b);
}
static void update_panel ()
{
	PinballView &V = *g_view;
	if (V.mode != M_PLAY || V.cur < 0) return;
	Game &g = *V.g;
	char b[64], n[32];
	fmt (g.score, n, sizeof n); g_lcdScore->setText (n);
	snprintf (b, sizeof b, "%d / %d", g.ball, V.T ().balls); g_lcdScore->setSub (b);
	unsigned now = gms ();
	if (V.msgUntil && (int) (now - V.msgUntil) >= 0) V.msgUntil = 0;
	if (V.msgUntil) lcd_message (V.msg, V.msgRed);
	else if (g.state == pinball::G_READY) { snprintf (b, sizeof b, TR ("Ball %d: launch it!"), g.ball); lcd_message (b, false); }
	else if (g.tilted) lcd_message (TR ("TILT"), true);
	else lcd_message ("", false);
	fmt (g.bonus, n, sizeof n); g_stats->set (0, n);
	snprintf (b, sizeof b, "\xC3\x97%d", g.mult); g_stats->set (1, b);
	long best = V.E ().best; fmt (best, n, sizeof n); g_stats->set (2, best > 0 ? n : "\xE2\x80\x94");
	g_stats->setTilt (g.tilted ? 3 : g.nudges ());
	g_rules->update (g);
}

class PinRoot : public GameRoot
{
public:
	PinRoot () : GameRoot (600, 680, "Pinball") {}
	void onTick () override { GameRoot::onTick (); update_panel (); }
	void onResized () override { relayout (); }
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type != DND_FILES || g_view->mode != M_PICKER) return;
		char path[256]; int n = 0; while (data[n] && data[n] != '\n' && n < 255) { path[n] = data[n]; n++; } path[n] = 0;
		if (ends_table (path)) open_path (path);
	}
};

// ---- the screens ----------------------------------------------------------------------------------------------------
static void set_legend ()
{
	g_legend->n = 0;
	if (g_padSeen)
	{
		g_legend->add ("L", TR ("Left flipper")); g_legend->add ("R", TR ("Right flipper")); g_legend->add ("A", TR ("Plunger (hold)"));
		g_legend->add ("Y", TR ("Nudge")); g_legend->add (TR ("Start"), TR ("Pause"));
	}
	else
	{
		g_legend->add ("Z|\xE2\x86\x90", TR ("Left flipper")); g_legend->add ("M|\xE2\x86\x92", TR ("Right flipper"));
		g_legend->add (TR ("Space"), TR ("Plunger (hold)")); g_legend->add ("N|\xE2\x86\x91", TR ("Nudge")); g_legend->add ("P", TR ("Pause"));
	}
	g_legend->invalidate (true);
}
static void relayout ()
{
	int W = g_root->width, H = g_root->height;
	bool play = g_view->mode == M_PLAY;
	// the play screen: the field at the left, the panel at the right
	g_view->left = 0; g_view->top = 0; g_view->resizeTo (W - PANELW, H);
	int px = W - PANELW + 12, pw = PANELW - 24;
	g_hdName->left = px; g_hdName->top = 10; g_hdName->resizeTo (pw, 26);
	((Widget *) g_lcdScore)->left = px; ((Widget *) g_lcdScore)->top = 40; g_lcdScore->resizeTo (pw, 58);
	((Widget *) g_lcdMsg)->left = px; ((Widget *) g_lcdMsg)->top = 104; g_lcdMsg->resizeTo (pw, 34);
	g_stats->left = px; g_stats->top = 146; g_stats->resizeTo (pw, 132);
	const char *goal = play && g_view->cur >= 0 ? g_view->T ().goal.get (g_lang) : "";
	int gh = 13 + 10 + wrapped (goal, pw - 2) * uk_fh () + 4;
	g_hdGoal->left = px; g_hdGoal->top = 290; g_hdGoal->resizeTo (pw, gh);
	int lgTop = H - 131;
	g_legend->left = px; g_legend->top = lgTop; g_legend->resizeTo (pw, 125);
	int rTop = 290 + gh + 4, rBot = H >= 640 ? lgTop - 8 : H - 6;
	g_rules->left = px; g_rules->top = rTop; g_rules->resizeTo (pw, rBot - rTop > 20 ? rBot - rTop : 20);
	Widget *playW[] = { g_view, g_hdName, g_lcdScore, g_lcdMsg, g_stats, g_hdGoal, g_rules, g_legend };
	for (Widget *w : playW) show (w, play);
	if (play) { show (g_legend, H >= 640); show (g_rules, H >= 600 && rBot - rTop >= 21); }
	// the picker
	int L = 300;
	g_hdChoose->left = 16; g_hdChoose->top = 12; g_hdChoose->resizeTo (L - 16, 30);
	g_list->left = 12; g_list->top = 46; g_list->resizeTo (L - 12, H - 108); g_list->clampScroll ();
	bool ok = g_sel >= 0 && g_sel < g_nent && g_ent[g_sel]->t;
	// (taller than the default 680: the preview taller -- it grows with the window --; the details at most 600 wide)
	int thH = 330 + (H > 680 ? H - 680 : 0), dw = W - L - 30 < 600 ? W - L - 30 : 600;
	g_thumb->left = L + 14; g_thumb->top = 12; g_thumb->resizeTo (W - L - 26, ok ? thH : H - 104);
	const char *tg = ok ? g_ent[g_sel]->t->goal.get (g_lang) : "";
	int glH = 15 + 10 + wrapped (tg, dw - 2) * uk_fh () + 4;
	g_hdTable->left = L + 16; g_hdTable->top = thH + 20; g_hdTable->resizeTo (dw, glH);
	g_scoreList->left = L + 16; g_scoreList->top = thH + 20 + glH + 6; g_scoreList->resizeTo (dw, 136);
	((Widget *) g_play)->left = L + 16; ((Widget *) g_play)->top = H - 80; g_play->resizeTo (dw, 36);
	g_legendRow->left = 14; g_legendRow->top = H - 32; g_legendRow->resizeTo (W - 28, 22);
	Widget *pick[] = { g_hdChoose, g_list, g_thumb, g_hdTable, g_scoreList, g_play, g_legendRow };
	for (Widget *w : pick) show (w, !play);
	if (!play) { show (g_hdTable, ok); show (g_scoreList, ok); }
	place_overlay_widgets ();
	g_root->invalidate (true);
}
// The picker shows table i (its preview, name, goal, top 5; Play for a table that loaded)
static void select_entry (int i)
{
	if (i < 0 || i >= g_nent) return;
	g_sel = i;
	TableEntry &e = *g_ent[i];
	if (e.t)
	{
		g_hdTable->set (e.t->name.get (g_lang), e.t->goal.get (g_lang));
		g_hdTable->sw0 = e.look.lampCol; g_hdTable->sw1 = e.look.slingCol;
	}
	g_play->setOn (e.t != 0); g_play->setDisabled (e.t == 0);
	g_list->ensureVisible (i);
	relayout ();
	g_list->invalidate (true); g_thumb->invalidate (true); g_scoreList->invalidate (true); g_hdTable->invalidate (true); ((Widget *) g_play)->invalidate (true);
}
static void play_selected ()
{
	if (g_sel < 0 || g_sel >= g_nent) return;
	if (!g_ent[g_sel]->t) { sfx (196, 70, SOUND_SQUARE, 70); return; }
	start_game (g_sel);
}
static void start_game (int i)
{
	TableEntry &e = *g_ent[i];
	if (strcmp (pinball::scores_setting (g_scores, "table", ""), e.section))	// (the picker's choice next time)
	{ pinball::scores_set_setting (g_scores, "table", e.section); save_scores (); }
	g_view->start (i);
	g_hdName->set (e.t->name.get (g_lang), "");
	g_hdName->sw0 = e.look.lampCol; g_hdName->sw1 = e.look.slingCol;
	g_hdGoal->set (TR ("Goal"), e.t->goal.get (g_lang));
	g_rules->t = e.t; g_rules->k = &e.look; g_rules->clear (); g_rules->invalidate (true);
	g_view->setOverlay (O_NONE);
	relayout ();
	g_view->setFocus ();
	update_panel ();
}
static void go_picker ()
{
	char keep[256] = "";
	if (g_view->cur >= 0 && g_view->cur < g_nent) snprintf (keep, sizeof keep, "%s", g_ent[g_view->cur]->path);
	else if (g_sel >= 0 && g_sel < g_nent) snprintf (keep, sizeof keep, "%s", g_ent[g_sel]->path);
	g_view->mode = M_PICKER; g_view->cur = -1;
	g_view->setOverlay (O_NONE);
	load_entries (keep);				// (read again: a table corrected meanwhile)
	g_thumb->forget ();
	g_list->scroll = 0;
	select_entry (g_sel);
	g_list->setFocus ();
	printf ("pinball: picker (%d tables)\n", g_nent); fflush (stdout);
}
// A table file given (the argument, Open, a drop): played at once, or the picker with its error
static void open_path (const char *path)
{
	int i = find_entry (path);
	if (i < 0)
	{
		bool user = in_dir (path, USERDIR);
		if (!user && g_nextra < MAXEXTRA) snprintf (g_extra[g_nextra++], sizeof g_extra[0], "%s", path);
		if (g_nent < MAXENT) { g_ent[g_nent] = load_entry (path, false, user); i = g_nent++; }
		else return;
	}
	if (g_ent[i]->t) start_game (i);
	else
	{
		if (g_view->mode == M_PLAY) { g_view->mode = M_PICKER; g_view->cur = -1; g_view->setOverlay (O_NONE); }
		g_thumb->forget ();
		select_entry (i);
		g_list->setFocus ();
	}
}

// ---- the menu -------------------------------------------------------------------------------------------------------
static void m_new () { if (g_view->mode == M_PLAY && g_view->cur >= 0) start_game (g_view->cur); else play_selected (); }
static void m_pause () { g_view->togglePause (); }
static void m_choose ()
{
	if (g_view->mode != M_PLAY) return;
	if (g_view->overlay == O_OVER || g_view->overlay == O_SCORES) go_picker ();
	else if (g_view->overlay == O_NONE) g_view->setOverlay (O_PAUSE);
}
static void m_open ()
{
	char path[256] = "";
	void *d = kapi_opendir (USERDIR);
	if (d) kapi_closedir (d);
	if (g_view->mode == M_PLAY && g_view->overlay == O_NONE) g_view->setOverlay (O_PAUSE);
	if (ft_file_open (path, sizeof path, d ? USERDIR : "SD:/docs", TR ("Pinball tables|*.table|All files|*")) && path[0]) open_path (path);
}
static void m_sound () { PinballView::toggleSound (); }
static void m_quit () { kapi_exit (0); }
static void build_menu ()
{
	g_menu.menu (TR ("Game"));
	g_menu.item (TR ("New Game"), "^N", UK_CTRL ('N'), m_new);
	g_menu.item (TR ("Pause"), "P", 0, m_pause);
	g_menu.item (TR ("Choose a Table..."), TR ("Esc"), 0, m_choose);
	g_menu.item (TR ("Open a Table File..."), "^O", UK_CTRL ('O'), m_open);
	g_menu.separator ();
	g_menu.item (TR ("Sound On / Off"), "S", 0, m_sound);
	g_menu.separator ();
	g_menu.item (TR ("Quit"), "^Q", UK_CTRL ('Q'), m_quit);
	g_menu.publish ();
}

static void cb_play (Widget &) { play_selected (); }

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();
	g_lang = !strcmp (uk_lang (), "fr") ? 1 : 0;
	g_scores = fk_kv_load (SCORES, FK_KV_ESCAPES);
	if (!g_scores) g_scores = fk_kv_new (FK_KV_ESCAPES);
	sfx_set_mute (!strcmp (pinball::scores_setting (g_scores, "sound", "1"), "0"));

	// the command line: [--seed N] [--start multiball] [file.table]
	static char args[512] = ""; int an = kapi_get_args (args, sizeof args);
	char *p = an > 0 ? args : (char *) "";
	for (;;)
	{
		while (*p == ' ') p++;
		if (!strncmp (p, "--seed ", 7)) { p += 7; g_seed = (unsigned) strtoul (p, &p, 0); g_seedSet = true; continue; }
		if (!strncmp (p, "--start ", 8)) { p += 8; while (*p == ' ') p++; if (!strncmp (p, "multiball", 9)) { g_startMultiball = true; p += 9; } continue; }
		break;
	}
	int pl = (int) strlen (p); while (pl > 0 && (p[pl - 1] == ' ' || p[pl - 1] == '\n' || p[pl - 1] == '\r')) p[--pl] = 0;
	const char *argPath = p;

	PinRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	g_view = new PinballView; root.addChild (g_view); root.view = g_view;
	// the panel
	g_hdName = new Heading (0, 0, 236, 26); g_hdName->titlePx = 16; g_hdName->swatch = true; root.addChild (g_hdName);
	g_lcdScore = new LcdDisplay (0, 0, 236, 58, "0", TR ("BALL")); g_lcdScore->face = face (26); g_lcdScore->smallFace = face (11); root.addChild (g_lcdScore);
	g_lcdMsg = new LcdDisplay (0, 0, 236, 34, ""); g_lcdMsg->face = face (16); g_lcdMsg->centred = true; root.addChild (g_lcdMsg);
	g_stats = new Stats (0, 0, 236, 132); root.addChild (g_stats);
	g_hdGoal = new Heading (0, 0, 236, 40); g_hdGoal->titlePx = 13; root.addChild (g_hdGoal);
	g_rules = new RuleList (0, 0, 236, 100); root.addChild (g_rules);
	g_legend = new Legend (0, 0, 236, 125, false); root.addChild (g_legend);
	set_legend ();
	// the picker
	g_hdChoose = new Heading (0, 0, 284, 30); g_hdChoose->titlePx = 18; g_hdChoose->set (TR ("Choose a table"), ""); root.addChild (g_hdChoose);
	g_list = new TableList (0, 0, 288, 572); root.addChild (g_list);
	g_thumb = new Thumb (0, 0, 274, 330); root.addChild (g_thumb);
	g_hdTable = new Heading (0, 0, 270, 60); g_hdTable->titlePx = 15; g_hdTable->swatch = true; root.addChild (g_hdTable);
	g_scoreList = new ScoreList (0, 0, 270, 136); root.addChild (g_scoreList);
	g_play = (new ToolButton (270, 36, TR ("Play this table (Enter)"), cb_play))->setGlyph (WKT_PLAY)->setText (TR ("Play"));
	g_play->filled = true; g_play->raised = true; root.addChild (g_play);
	g_legendRow = new Legend (0, 0, 572, 22, true);
	g_legendRow->add ("\xE2\x86\x91|\xE2\x86\x93", TR ("choose")); g_legendRow->add (TR ("Enter"), TR ("play")); g_legendRow->add (TR ("Esc"), TR ("quit"));
	root.addChild (g_legendRow);
	// the overlays' widgets (over the view's cards)
	g_btResume = new Button (0, 0, 192, 30, TR ("Resume"), cb_resume); root.addChild (g_btResume);
	g_btBack = new Button (0, 0, 192, 30, TR ("Back to the tables"), cb_back); root.addChild (g_btBack);
	g_tbName = new Textbox (0, 0, 260, 26, "", name_ok); g_tbName->maxLen = 4 * pinball::NAMEC; root.addChild (g_tbName);
	g_btOk = new Button (0, 0, 90, 28, TR ("OK"), name_ok); root.addChild (g_btOk);
	Widget *ov[] = { g_btResume, g_btBack, g_tbName, g_btOk };
	for (Widget *w : ov) w->hidden = true;

	root.setResizable (true);
	root.setMinSize (600, 440);			// (a pocket's 800 x 480: the play screen without its legend)
	build_menu ();
	load_entries (0);
	bool shipped = false;
	for (int i = 0; i < g_nent; i++) if (g_ent[i]->shipped) shipped = true;
	if (!shipped && !argPath[0])
	{
		ft_messagebox ("Pinball", TR ("No table found in SD:/apps/pinball.app/tables."), MB_OK);
		return 1;
	}
	relayout ();
	root.fitWorkArea ();
	if (argPath[0]) open_path (argPath);
	else go_picker ();
	relayout ();
	root.run ();
	return 0;
}
