//
// circuits -- Circuits, a puzzle game of logic gates: wire switches, NOT, AND, OR, XOR, NAND and NOR gates and lamps
// on a board until the lamps light exactly as the level's truth table says -- from one wire to a two-bit adder, in
// three worlds. The window: the levels on the left (the worlds, their stars, the padlocks of those not open yet), in
// the middle the level's card (what to do, Hint, Lesson), the palette of the gates the level allows, the board and the
// message bar; on the right the truth table (a click on a row sets the switches), the gate count and the stars it
// needs, Step, Reset and Check. The simulation is live (a switch clicked, the wires and lamps follow at once); Step
// shows the signal going through, one gate depth at a time. Check tries every row: the wrong ones are marked, a level
// won gives 1 to 3 stars (fewer gates: more stars) and opens the next one.
//
// Mouse: a palette button arms its gate (click then click on the board, or drag it there); a press on an output pin
// drags a wire to an input pin (a fed input's wire is picked up by a press on it); a click on a gate selects it, a
// drag moves it; a click on a switch toggles it; a right click deletes a gate or a wire.
// Keys: F5 Check, F8 Step, F7 Live, F9 Reset, F1 the lesson, F2 the hint, 1-6 a gate of the palette, the arrows move
// the gate selected, Del / Backspace delete, Esc cancels, Ctrl+Z / Ctrl+Y undo / redo, Ctrl+N / Ctrl+P the next /
// previous level, Ctrl+O a pack of levels, Ctrl+Q quit. The language is the system's (UIKit's TR, lang/fr.txt).
//
// Files: the packs SD:/apps/circuits.app/levels/*.circuits (and any .circuits opened, dropped or given as the
// argument -- a double click on one in the File Viewer); the player's progress -- the stars, the boards, the lessons
// seen, the levels open -- SD:/apps/circuits.app/progress.ini (FileKit's fk_kv). The engine: circuit.h (no UI).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"	// the clipboard
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "uikit/lang.h"
#include "fontkit/uikitface.h"
#include "circuit.h"
#include "lessons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uikit;
using namespace circuits;

#define APPDIR		"SD:/apps/circuits.app/"
#define PROGRESS	APPDIR "progress.ini"
#define PACKDIR		"SD:/docs/circuits"

// ---- the game's state ---------------------------------------------------------------------------------------------------
static int g_lang = 0;					// 0 English, 1 French (the system's, through uk_lang)
enum { MAXPACKS = 12 };
static Pack *g_packs[MAXPACKS]; static int g_npacks = 0;
static int g_pack = -1, g_level = -1;
static Level *g_L = 0;					// the level shown
static circuits::Progress g_pr;
static Circuit g_c;					// its board
static History *g_hist;					// (on the heap: 65 texts)
static Eval g_ev;					// the board evaluated on the current row
static unsigned g_row = 0;				// the switches, as a row of the truth table (the first input the MSB)
static int g_step = -1;					// step mode: the depth computed so far; -1 live
static int g_sel = -1;					// the gate selected
static int g_selDst = -1, g_selPin = 0;			// the wire selected: the part it feeds, the pin
static int g_armed = -1;				// the palette's gate armed (-1: Select)
static int g_errPart = -1;				// the part a refused Check named (outlined red)
static bool g_checked = false;				// a Check's result shown in the table
static CheckResult g_res;
static bool g_showHint = false;
static bool g_dirty = false, g_saveFailed = false;	// the board changed since saved; the last save failed
static unsigned g_dirtyAt = 0;
enum { M_NONE, M_INFO, M_OK, M_ERR };

static void set_message (int kind, const char *text, int stars = 0);
static void prompt ();
static void refresh ();
static void board_changed ();
static void select_gate (int i);
static void select_wire (int dst, int pin);
static void toggle_switch (int k);
static void set_row (unsigned r);
static void arm (int type);
static void level_clicked (int p, int l);
// The engine's refusals in words (TR'd where shown)
static const char *err_word (Err e)
{
	switch (e)
	{
	case E_OVERLAP: return TRN ("No room there: parts cannot overlap.");
	case E_OUTSIDE: return TRN ("Gates go between the inputs and the outputs.");
	case E_FULL: return TRN ("The board is full: 48 gates at most.");
	case E_LOOP: return TRN ("That wire would make a loop: in this game signals only go forward.");
	case E_NOT_ALLOWED: return TRN ("This gate is not allowed in this level.");
	default: return TRN ("A wire goes from an output to an input.");
	}
}

#include "gates.h"
#include "board.h"
#include "views.h"

// ---- the widgets --------------------------------------------------------------------------------------------------------------
static Root *g_root;
static LevelList *g_list;
static Card *g_card;
static Button *g_btHint, *g_btLesson, *g_btNext;
static ToolBar *g_tb;
static PaletteButton *g_pal[P_COUNT_];			// [P_SWITCH]: Select; [P_NOT .. P_NOR]: the gates
static Label *g_noGate, *g_lbTable, *g_lbMode;
static ToolButton *g_btDel, *g_btUndo, *g_btRedo, *g_btStep, *g_btReset, *g_btCheck;
static Board *g_board;
static MsgBar *g_msg;
static TruthTable *g_table;
static CountView *g_count;
static LessonCard *g_lesson;
static ResultCard *g_result;
static Menu g_menu;

// ---- the packs and the progress -----------------------------------------------------------------------------------------
static bool ends_circuits (const char *name) { int l = (int) strlen (name); return l > 9 && !fs_ci_cmp (name + l - 9, ".circuits"); }
// A pack read and added (opened: from a file -- all its levels open) -> its index, -1 (quiet: no dialog)
static int add_pack (const char *path, bool opened, bool quiet)
{
	for (int i = 0; i < g_npacks; i++) if (!strcmp (g_packs[i]->path, path)) return i;
	if (g_npacks >= MAXPACKS) return -1;
	char m[600];
	fk_kv *d = fk_kv_load (path, FK_KV_PIPES);
	if (!d)
	{
		if (!quiet) { snprintf (m, sizeof m, TR ("\xE2\x80\x9C%s\xE2\x80\x9D cannot be read."), fs_basename (path)); ft_messagebox (TR ("Open Level Pack"), m, MB_OK); }
		return -1;
	}
	Pack *pk = new Pack; char why[200] = "";
	bool ok = parse_pack_kv (*pk, d, why, sizeof why);
	fk_kv_free (d);
	if (ok)
	{
		g_packs[g_npacks] = pk;
		ok = ids_unique (g_packs, g_npacks + 1, why, sizeof why);
	}
	if (!ok)
	{
		if (!quiet) { snprintf (m, sizeof m, TR ("\xE2\x80\x9C%s\xE2\x80\x9D cannot be opened:\n%s.\n\nNo level was added."), fs_basename (path), why); ft_messagebox (TR ("Open Level Pack"), m, MB_OK); }
		delete pk; return -1;
	}
	snprintf (pk->path, sizeof pk->path, "%s", path);
	pk->opened = opened;
	if (!pk->title[0][0])
	{
		snprintf (pk->title[0], sizeof pk->title[0], "%s", fs_basename (path));
		char *dot = strrchr (pk->title[0], '.'); if (dot) *dot = 0;
	}
	return g_npacks++;
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
			if (!e.is_dir && ends_circuits (e.name)) snprintf (names[n++], sizeof names[0], "%s", e.name);
		kapi_closedir (d);
	}
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp (names[j - 1], names[j]) > 0; j--) { char t[128]; memcpy (t, names[j], 128); memcpy (names[j], names[j - 1], 128); memcpy (names[j - 1], t, 128); }
	for (int i = 0; i < n; i++) { char p[256]; snprintf (p, sizeof p, "%s/%s", dir, names[i]); add_pack (p, false, true); }
}
static void save_progress ()
{
	if (fk_kv_save (g_pr.kv, PROGRESS, "# Circuits -- the player's progress (written by the game)") == 0) { g_saveFailed = false; return; }
	if (!g_saveFailed) set_message (M_ERR, TR ("Progress not saved: the card is full or read-only."));
	g_saveFailed = true;
}
static void save_board ()				// the board of the level shown into the progress, written
{
	if (!g_L) return;
	static char t[TEXTCAP];
	write_text (g_c, t, sizeof t);
	g_dirty = false;
	if (!strcmp (g_pr.circuit (g_L->id), t)) return;
	g_pr.setCircuit (g_L->id, t);
	save_progress ();
}
// The level after / before (p, l) in the list -> false: none
static bool next_of (int p, int l, int *np, int *nl)
{
	if (p < 0) return false;
	if (l + 1 < g_packs[p]->n) { *np = p; *nl = l + 1; return true; }
	for (int q = p + 1; q < g_npacks; q++) if (g_packs[q]->n) { *np = q; *nl = 0; return true; }
	return false;
}
static bool prev_of (int p, int l, int *np, int *nl)
{
	if (p < 0) return false;
	if (l > 0) { *np = p; *nl = l - 1; return true; }
	for (int q = p - 1; q >= 0; q--) if (g_packs[q]->n) { *np = q; *nl = g_packs[q]->n - 1; return true; }
	return false;
}
static bool next_open ()
{
	int np, nl;
	return next_of (g_pack, g_level, &np, &nl) && level_open (g_pr, *g_packs[np], nl);
}

// ---- the message bar, the state of the buttons --------------------------------------------------------------------------
static void set_message (int kind, const char *text, int stars)
{
	g_msg->kind = kind; g_msg->stars = stars; snprintf (g_msg->text, sizeof g_msg->text, "%s", text);
	bool next = kind == M_OK && next_open ();
	((Widget *) g_btNext)->hidden = !next; g_msg->roomForNext = next;
	g_msg->invalidate (true); ((Widget *) g_btNext)->invalidate (true);
}
static bool first_level () { return g_pack == 0 && g_level == 0; }
static void prompt ()
{
	if (!g_L) return;
	if (first_level () && !g_pr.stars (g_L->id)) set_message (M_INFO, TR ("Welcome to Circuits! Wire the switch A to the lamp, then Check (F5)."));
	else set_message (M_INFO, TR ("Every lamp follows the switches. When you think it is right: Check (F5)."));
}
static void step_message ()
{
	char m[300];
	if (g_step >= g_ev.maxDepth) snprintf (m, sizeof m, TR ("Step %d of %d: every lamp is computed. F9: back to the start \xC2\xB7 F7: live."), g_step, g_ev.maxDepth);
	else if (g_step == 0) snprintf (m, sizeof m, TR ("Step 0 of %d: no gate is computed yet. F8: the first depth \xC2\xB7 F7: live."), g_ev.maxDepth);
	else snprintf (m, sizeof m, TR ("Step %d of %d: the gates of depth %d are computed. F8: the next depth \xC2\xB7 F9: back to the start \xC2\xB7 F7: live."), g_step, g_ev.maxDepth, g_step);
	set_message (M_INFO, m);
}
static void update_tools ()
{
	g_btUndo->setDisabled (!g_hist->canUndo ());
	g_btRedo->setDisabled (!g_hist->canRedo ());
	g_btDel->setDisabled (g_sel < 0 && g_selDst < 0);
	g_btStep->setOn (g_step >= 0);
	g_btReset->setDisabled (g_step < 0);
	static char mode[128];
	if (g_step >= 0) snprintf (mode, sizeof mode, TR ("Step %d of %d \xC2\xB7 F7: back to live"), g_step, g_ev.maxDepth);
	else snprintf (mode, sizeof mode, "%s", TR ("Live: click a switch or a row."));
	g_lbMode->setText (mode); g_lbMode->fg = g_step >= 0 ? C_ACCENT : C_DIS;
	((Widget *) g_lbMode)->invalidate (true);
}
// The board evaluated on the current row (live, or to the step's depth), every view of it redrawn
static void refresh ()
{
	if (!g_L) return;
	if (g_step >= 0) evaluate_to (g_c, g_row, g_step, g_ev); else evaluate (g_c, g_row, g_ev);
	update_tools ();
	g_board->invalidate (true); g_table->invalidate (true); g_count->invalidate (true);
}
// After the board changed (pushed: an edit; not: an undo / redo): step mode left, the Check's marks cleared, saved soon
static void after_edit (bool push)
{
	if (g_step >= 0) { g_step = -1; prompt (); }
	g_checked = false; g_errPart = -1;
	if (push) g_hist->push (g_c);
	if (g_sel >= g_c.n || (g_sel >= 0 && !gate_type (g_c.p[g_sel].type))) g_sel = -1;
	if (g_selDst >= 0 && (g_selDst >= g_c.n || g_c.p[g_selDst].in[g_selPin] < 0)) g_selDst = -1;
	g_dirty = true; g_dirtyAt = kapi_get_ticks ();
	if (g_msg->kind == M_ERR || g_msg->kind == M_OK) prompt ();
	refresh ();
}
static void board_changed () { after_edit (true); }
static void select_gate (int i) { g_sel = i; g_selDst = -1; update_tools (); g_board->invalidate (true); }
static void select_wire (int dst, int pin) { g_selDst = dst; g_selPin = pin; g_sel = -1; update_tools (); g_board->invalidate (true); }
static void set_row (unsigned r)
{
	g_row = r;
	if (g_step >= 0) { g_step = 0; refresh (); step_message (); return; }
	refresh ();
}
static void toggle_switch (int k) { set_row (g_row ^ (1u << (g_c.ni - 1 - k))); }
static void arm (int type)
{
	g_armed = type;
	g_pal[P_SWITCH]->setOn (type < 0);
	for (int t = P_NOT; t < P_COUNT_; t++) g_pal[t]->setOn (t == type);
	if (type >= 0)
	{
		select_gate (-1);
		char m[200]; snprintf (m, sizeof m, TR ("%s: click a free place on the board to put it (Esc: cancel)."), gate_word (type));
		set_message (M_INFO, m);
	}
	else if (g_msg->kind == M_INFO) prompt ();
	g_board->invalidate (true);
}

// ---- the level shown ------------------------------------------------------------------------------------------------------
enum { PAD = 10, LISTW = 214, RIGHTW = 238, TBH = 46, MSGH = 50 };
static void layout ()
{
	Root &R = *g_root;
	int W = R.width, H = R.height;
	int mid = PAD + LISTW + PAD, rx = W - PAD - RIGHTW, mw = rx - PAD - mid;
	g_list->left = PAD; g_list->top = PAD; g_list->resizeTo (LISTW, H - 2 * PAD);
	g_card->left = mid; g_card->top = PAD; g_card->resizeTo (mw, 66);
	int ch = g_card->need (); g_card->resizeTo (mw, ch);
	g_btHint->left = mid + mw - 176; g_btHint->top = PAD + 10;
	g_btLesson->left = mid + mw - 90; g_btLesson->top = PAD + 10;
	int ty = PAD + ch + 6;
	g_tb->left = mid; g_tb->top = ty; g_tb->resizeTo (mw, TBH);
	int by = ty + TBH + 6, bh = H - PAD - MSGH - 8 - by;
	g_board->left = mid; g_board->top = by; g_board->resizeTo (mw, bh);
	g_msg->left = mid; g_msg->top = H - PAD - MSGH; g_msg->resizeTo (mw, MSGH);
	g_btNext->left = mid + mw - 140; g_btNext->top = H - PAD - MSGH + 10;
	g_lbTable->left = rx; g_lbTable->top = PAD; g_lbTable->resizeTo (RIGHTW, 22);
	g_table->left = rx; g_table->top = PAD + 24; g_table->resizeTo (RIGHTW, g_table->need ());
	int cy = g_table->top + g_table->height + 10;
	g_count->left = rx; g_count->top = cy; g_count->resizeTo (RIGHTW, 66);
	int sy = H - PAD - 36 - 8 - 32;
	g_lbMode->left = rx; g_lbMode->top = sy - 22; g_lbMode->resizeTo (RIGHTW, 18);
	((Widget *) g_lbMode)->hidden = cy + 66 > sy - 22;		// (16 rows at the minimum height: no room)
	((Widget *) g_btStep)->left = rx; ((Widget *) g_btStep)->top = sy;
	((Widget *) g_btReset)->left = rx + (RIGHTW + 6) / 2; ((Widget *) g_btReset)->top = sy;
	((Widget *) g_btCheck)->left = rx; ((Widget *) g_btCheck)->top = H - PAD - 36;
	// the cards, centred over the board
	int lw = mw - 16 < 500 ? mw - 16 : 500, lh = bh - 16 < 330 ? bh - 16 : 330;
	g_lesson->left = mid + (mw - lw) / 2; g_lesson->top = by + (bh - lh) / 2; g_lesson->resizeTo (lw, lh); g_lesson->place ();
	int rw = mw - 16 < 420 ? mw - 16 : 420, rh = bh - 16 < 250 ? bh - 16 : 250;
	g_result->left = mid + (mw - rw) / 2; g_result->top = by + (bh - rh) / 2; g_result->resizeTo (rw, rh); g_result->place ();
	g_list->clamp ();
	R.invalidate (true);
}
// The palette: the gates the level allows, in their order (the others hidden); none: the label
static void palette_setup ()
{
	int k = 0;
	for (int t = P_NOT; t < P_COUNT_; t++)
	{
		PaletteButton *b = g_pal[t];
		((Widget *) b)->hidden = !g_L->allows (t);
		if (!g_L->allows (t)) continue;
		b->left = 6 + 40 + 11 + 1 + k * 41; b->top = (TBH - 40) / 2;
		k++;
		snprintf (b->tipText, sizeof b->tipText, TR ("Put a gate: %s (%d)"), gate_word (t), k);
	}
	((Widget *) g_noGate)->hidden = k > 0;
	arm (-1);
	g_tb->invalidate (true);
}
static void close_cards ()
{
	((Widget *) g_lesson)->hidden = true; ((Widget *) g_result)->hidden = true;
	g_root->invalidate (true);
}
static bool card_shown () { return !((Widget *) g_lesson)->hidden || !((Widget *) g_result)->hidden; }
// The lesson of the level's concept over the board (first: the first time -- remembered)
static bool show_lesson (bool first)
{
	const Lesson *ls = g_L && g_L->topic[0] ? find_lesson (g_L->topic) : 0;
	if (!ls)
	{
		if (!first) set_message (M_INFO, TR ("This level has no lesson of its own: its idea was taught before."));
		return false;
	}
	close_cards ();
	g_lesson->ls = ls; ((Widget *) g_lesson)->hidden = false; g_lesson->invalidate (true);
	char m[200];
	if (ls->gate >= 0) snprintf (m, sizeof m, TR ("New gate: %s. Read the card, then build."), gate_word (ls->gate));
	else snprintf (m, sizeof m, TR ("New idea: %s. Read the card, then build."), ls->title[g_lang]);
	set_message (M_INFO, m);
	if (first) { g_pr.setSeen (g_L->topic); save_progress (); }
	return true;
}
static void show_level (int p, int l)
{
	if (p < 0 || p >= g_npacks || l < 0 || l >= g_packs[p]->n) return;
	if (g_board->drag != Board::D_NONE) g_board->cancel ();
	save_board ();
	close_cards ();
	g_pack = p; g_level = l; g_L = g_packs[p]->lv[l];
	g_c.setup (*g_L);
	const char *t = g_pr.circuit (g_L->id);
	if (t[0] && !read_text (g_c, *g_L, t)) g_c.setup (*g_L);
	g_hist->start (g_c);
	g_row = 0; g_step = -1; g_sel = g_selDst = -1; g_errPart = -1; g_checked = false; g_showHint = false; g_dirty = false;
	palette_setup ();
	g_pr.setLast (fs_basename (g_packs[p]->path), g_L->id);
	if (!(g_L->topic[0] && !g_pr.seen (g_L->topic) && show_lesson (true))) prompt ();
	save_progress ();
	layout ();
	g_list->showSel ();
	refresh ();
}
static void level_clicked (int p, int l)
{
	if (p == g_pack && l == g_level) return;
	if (level_open (g_pr, *g_packs[p], l)) { show_level (p, l); return; }
	int pp, pl; char m[200];
	if (prev_of (p, l, &pp, &pl)) snprintf (m, sizeof m, TR ("Locked: win \xE2\x80\x9C%s\xE2\x80\x9D first."), g_packs[pp]->lv[pl]->titleOf (g_lang));
	else snprintf (m, sizeof m, "%s", TR ("This level is locked."));
	set_message (M_INFO, m);
}

// ---- the actions ----------------------------------------------------------------------------------------------------------
static void inputs_text (unsigned row, char *out, int cap)	// "A = 1 and B = 0", "A = 1, B = 0 and Cin = 1"
{
	int n = 0; out[0] = 0;
	for (int k = 0; k < g_L->ninputs; k++)
	{
		const char *sep = k == 0 ? "" : k + 1 == g_L->ninputs ? TR (" and ") : ", ";
		n += snprintf (out + n, n < cap ? cap - n : 0, "%s%s = %u", sep, g_L->inName[k], (row >> (g_L->ninputs - 1 - k)) & 1);
	}
}
static void do_check ()
{
	if (!g_L || card_shown ()) return;
	if (g_board->drag != Board::D_NONE) g_board->cancel ();
	g_step = -1;
	CheckResult r = check (g_c, *g_L);
	char m[400];
	if (r.err == E_LAMP_OPEN)
	{
		g_errPart = r.errPart; g_checked = false;
		snprintf (m, sizeof m, TR ("Lamp %s is not connected: wire a gate's output to it, then Check again."), g_c.p[r.errPart].name);
		set_message (M_ERR, m); refresh (); return;
	}
	if (r.err != E_OK)
	{
		g_errPart = r.errPart; g_checked = false;
		snprintf (m, sizeof m, TR ("This %s gate (outlined in red) has an input not connected: wire it, then Check again."), gate_word (g_c.p[r.errPart].type));
		set_message (M_ERR, m); refresh (); return;
	}
	g_errPart = -1; g_checked = true; g_res = r;
	if (!r.won)
	{
		g_row = (unsigned) r.firstWrong;
		int o = 0; while (o + 1 < g_L->noutputs && ((g_L->want[o] ^ r.got[o]) >> r.firstWrong & 1) == 0) o++;
		bool light = (g_L->want[o] >> r.firstWrong) & 1;
		char head[80], ins[120], body[300];
		if (r.nwrong == 1) snprintf (head, sizeof head, "%s", TR ("1 row is wrong:"));
		else snprintf (head, sizeof head, TR ("%d rows are wrong:"), r.nwrong);
		inputs_text ((unsigned) r.firstWrong, ins, sizeof ins);
		if (g_L->noutputs == 1) snprintf (body, sizeof body, light ? TR ("with %s the lamp must light.") : TR ("with %s the lamp must stay off."), ins);
		else snprintf (body, sizeof body, light ? TR ("with %s lamp %s must light.") : TR ("with %s lamp %s must stay off."), ins, g_L->outName[o]);
		snprintf (m, sizeof m, "%s %s %s", head, body, TR ("The switches are set on that row."));
		set_message (M_ERR, m); refresh (); return;
	}
	// won: the stars kept, the next level opened, the board saved
	bool rec = g_pr.record (g_L->id, r.stars);
	unlock_after (g_pr, g_packs, g_npacks, g_pack, g_level);
	{ static char t[TEXTCAP]; write_text (g_c, t, sizeof t); g_pr.setCircuit (g_L->id, t); g_dirty = false; save_progress (); }
	if (r.stars == 3) snprintf (m, sizeof m, TR ("Well done! %d %s \xE2\x80\x94 the best possible."), r.gates, gates_word (r.gates));
	else snprintf (m, sizeof m, TR ("Well done! %d %s. Three stars need %d."), r.gates, gates_word (r.gates), g_L->par3);
	set_message (M_OK, m, r.stars);
	int np, nl;
	g_result->stars = r.stars; g_result->gates = r.gates; g_result->record = rec; g_result->last = !next_of (g_pack, g_level, &np, &nl);
	snprintf (g_result->stay->text, sizeof g_result->stay->text, "%s", r.stars == 3 ? TR ("Stay here") : TR ("Try for \xE2\x98\x85\xE2\x98\x85\xE2\x98\x85"));
	snprintf (g_result->next->text, sizeof g_result->next->text, "%s", TR ("Next level"));
	((Widget *) g_result->next)->hidden = g_result->last;
	close_cards ();
	((Widget *) g_result)->hidden = false; g_result->invalidate (true);
	g_list->invalidate (true);
	refresh ();
}
static void do_step ()
{
	if (!g_L || card_shown ()) return;
	if (g_step < 0) g_step = 0;
	else { evaluate (g_c, g_row, g_ev); if (g_step < g_ev.maxDepth) g_step++; }
	g_checked = false;
	refresh (); step_message ();
}
static void do_live () { if (g_step < 0) return; g_step = -1; refresh (); prompt (); }
static void do_reset () { if (g_step < 0) return; g_step = 0; refresh (); step_message (); }
static void do_next ()
{
	int np, nl;
	if (next_of (g_pack, g_level, &np, &nl) && level_open (g_pr, *g_packs[np], nl)) show_level (np, nl);
	else set_message (M_INFO, TR ("No next level open yet."));
}
static void do_prev () { int np, nl; if (prev_of (g_pack, g_level, &np, &nl)) show_level (np, nl); }
static void do_clear () { if (!g_L) return; close_cards (); g_c.clear (); select_gate (-1); board_changed (); }
static void do_undo () { if (g_L && !card_shown () && g_hist->undo (g_c, *g_L)) { g_sel = g_selDst = -1; after_edit (false); } }
static void do_redo () { if (g_L && !card_shown () && g_hist->redo (g_c, *g_L)) { g_sel = g_selDst = -1; after_edit (false); } }
static void do_delete ()
{
	if (!g_L || card_shown ()) return;
	if (g_sel >= 0) { g_c.remove (g_sel); g_sel = -1; board_changed (); }
	else if (g_selDst >= 0) { g_c.disconnect (g_selDst, g_selPin); g_selDst = -1; board_changed (); }
}
static void do_hint ()
{
	if (!g_L) return;
	g_showHint = !g_showHint;
	if (g_showHint) { char m[600]; snprintf (m, sizeof m, "%s %s", TR ("Hint:"), g_L->hintOf (g_lang)); set_message (M_INFO, m); }
	else prompt ();
	layout ();
}
static void do_lesson () { if (!((Widget *) g_lesson)->hidden) { close_cards (); prompt (); } else show_lesson (false); }
static void copy_table ()
{
	if (!g_L) return;
	static char t[4096];
	truth_table_text (*g_L, g_checked ? &g_res : 0, t, sizeof t);
	clip_set_text (t);
	set_message (M_INFO, TR ("Truth table copied."));
}
static void copy_circuit ()
{
	if (!g_L) return;
	static char t[TEXTCAP];
	write_text (g_c, t, sizeof t);
	clip_set_text (t);
	set_message (M_INFO, TR ("Circuit copied."));
}
static void open_pack (const char *path)
{
	int p = add_pack (path, true, false);
	if (p < 0) return;
	g_list->invalidate (true);
	show_level (p, 0);
}
static void move_sel (int dx, int dy)
{
	if (g_sel < 0 || card_shown ()) return;
	if (g_c.move (g_sel, g_c.p[g_sel].x + dx, g_c.p[g_sel].y + dy) == E_OK) board_changed ();
}
static void arm_nth (int n)					// the n-th gate of the palette (0-based)
{
	for (int t = P_NOT; t < P_COUNT_; t++) if (g_L && g_L->allows (t) && n-- == 0) { arm (t); return; }
}
static void on_card_ok (Widget &) { close_cards (); prompt (); }
static void on_result_stay (Widget &) { close_cards (); }
static void on_result_next (Widget &) { close_cards (); do_next (); }
static void cb_hint (Widget &) { do_hint (); }
static void cb_lesson (Widget &) { do_lesson (); }
static void cb_next (Widget &) { do_next (); }
static void cb_step (Widget &) { do_step (); }
static void cb_reset (Widget &) { do_reset (); }
static void cb_check (Widget &) { do_check (); }
static void cb_undo (Widget &) { do_undo (); }
static void cb_redo (Widget &) { do_redo (); }
static void cb_del (Widget &) { do_delete (); }

// ---- the menus ----------------------------------------------------------------------------------------------------------------
static void m_quit () { save_board (); kapi_exit (0); }
static void m_open ()
{
	kapi_mkdir (PACKDIR);					// (the chooser starts there: made the first time)
	char path[256] = "";
	if (ft_file_open (path, sizeof path, PACKDIR, TR ("Circuits levels|*.circuits|All files|*")) && path[0]) open_pack (path);
}
static void m_about ()
{
	ft_messagebox (TR ("About Circuits"), TR ("Circuits 1.0 \xE2\x80\x94 wire logic gates until the lamps match the truth table.\nMIT License."), MB_OK);
}
static void m_restart () { do_clear (); }
static void build_menu ()
{
	g_menu = Menu ();
	g_menu.menu (TR ("Game"));
	g_menu.item (TR ("Next Level"), "^N", UK_CTRL ('N'), do_next);
	g_menu.item (TR ("Previous Level"), "^P", UK_CTRL ('P'), do_prev);
	g_menu.item (TR ("Restart Level"), "", 0, m_restart);
	g_menu.separator ();
	g_menu.item (TR ("Quit"), "^Q", UK_CTRL ('Q'), m_quit);
	g_menu.menu (TR ("Edit"));
	g_menu.item (TR ("Undo"), "^Z", UK_CTRL ('Z'), do_undo);
	g_menu.item (TR ("Redo"), "^Y", UK_CTRL ('Y'), do_redo);
	g_menu.separator ();
	g_menu.item (TR ("Delete"), TR ("Del"), KEY_DEL, do_delete);
	g_menu.item (TR ("Clear Board"), "", 0, do_clear);
	g_menu.separator ();
	g_menu.item (TR ("Copy Truth Table"), "", 0, copy_table);
	g_menu.item (TR ("Copy Circuit"), "", 0, copy_circuit);
	g_menu.menu (TR ("Simulate"));
	g_menu.item (TR ("Check"), "F5", KEY_F1 + 4, do_check);
	g_menu.separator ();
	g_menu.item (TR ("Step"), "F8", KEY_F1 + 7, do_step);
	g_menu.item (TR ("Live"), "F7", KEY_F1 + 6, do_live);
	g_menu.item (TR ("Reset"), "F9", KEY_F1 + 8, do_reset);
	g_menu.menu (TR ("Levels"));
	g_menu.item (TR ("Open Level Pack..."), "^O", UK_CTRL ('O'), m_open);
	g_menu.menu (TR ("Help"));
	g_menu.item (TR ("Lesson"), "F1", KEY_F1, do_lesson);
	g_menu.item (TR ("Hint"), "F2", KEY_F1 + 1, do_hint);
	g_menu.separator ();
	g_menu.item (TR ("About Circuits"), "", 0, m_about);
	g_menu.publish ();
}

// ---- the window -----------------------------------------------------------------------------------------------------------------
class CircuitsRoot : public Root
{
public:
	CircuitsRoot () : Root (1000, 620, "Circuits") {}
	void onTick () override
	{
		if (g_dirty && kapi_get_ticks () - g_dirtyAt >= 100) save_board ();	// (1 s after the last change)
	}
	void onResized () override { layout (); }
	bool onKey (long k) override
	{
		if (card_shown ())				// a card: Enter its default button, Esc closes it
		{
			if (k == 27 || k == KEY_ENTER)
			{
				bool next = k == KEY_ENTER && !((Widget *) g_result)->hidden && !((Widget *) g_result->next)->hidden;
				close_cards ();
				if (next) do_next (); else if (g_msg->kind == M_INFO) prompt ();
				return true;
			}
			if (k == UK_CTRL ('Q')) return g_menu.shortcut (k);
			return true;
		}
		switch (k)
		{
		case KEY_F1 + 4: do_check (); return true;
		case KEY_F1 + 7: do_step (); return true;
		case KEY_F1 + 6: do_live (); return true;
		case KEY_F1 + 8: do_reset (); return true;
		case KEY_F1: do_lesson (); return true;
		case KEY_F1 + 1: do_hint (); return true;
		case KEY_DEL: case KEY_BACKSPACE: do_delete (); return true;
		case KEY_LEFT: move_sel (-1, 0); return true;
		case KEY_RIGHT: move_sel (1, 0); return true;
		case KEY_UP: move_sel (0, -1); return true;
		case KEY_DOWN: move_sel (0, 1); return true;
		case 27:					// a drag, then the armed gate, then the selection
			if (g_board->drag != Board::D_NONE) { g_board->cancel (); prompt (); }
			else if (g_armed >= 0) arm (-1);
			else { select_gate (-1); select_wire (-1, 0); }
			return true;
		}
		if (k >= '1' && k <= '6') { arm_nth ((int) (k - '1')); return true; }
		return g_menu.shortcut (k);
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type != DND_FILES) return;
		char path[256]; int n = 0; while (data[n] && data[n] != '\n' && n < 255) { path[n] = data[n]; n++; } path[n] = 0;
		if (ends_circuits (path)) open_pack (path);
	}
};

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();
	g_lang = !strcmp (uk_lang (), "fr") ? 1 : 0;
	g_big = open_face (18); g_small = open_face (10); g_huge = open_face (30); g_tiny = open_face (9);
	g_hist = new History;
	load_packs ();
	g_pr.attach (fk_kv_load (PROGRESS, FK_KV_ESCAPES));
	if (g_npacks && g_packs[0]->n && !g_pr.isOpen (g_packs[0]->lv[0]->id)) open_first (g_pr, g_packs, g_npacks);

	CircuitsRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	g_list = new LevelList (PAD, PAD, LISTW, 600); root.addChild (g_list);
	g_card = new Card (0, 0, 500, 66); root.addChild (g_card);
	g_btHint = new Button (0, 0, 80, 28, TR ("Hint"), cb_hint); ((Widget *) g_btHint)->tip = "F2"; root.addChild (g_btHint);
	g_btLesson = new Button (0, 0, 80, 28, TR ("Lesson"), cb_lesson); ((Widget *) g_btLesson)->tip = "F1"; root.addChild (g_btLesson);
	// the palette: Select, the six gates (those of the level shown), Delete / Undo / Redo at the right
	g_tb = new ToolBar (0, 0, 508, TBH); g_tb->line = false; root.addChild (g_tb);
	g_pal[P_SWITCH] = new PaletteButton (-1); snprintf (g_pal[P_SWITCH]->tipText, sizeof g_pal[0]->tipText, "%s", TR ("Select and move (Esc)"));
	g_tb->add (g_pal[P_SWITCH], 0);
	g_tb->sep ();
	for (int t = P_NOT; t < P_COUNT_; t++) { g_pal[t] = new PaletteButton (t); g_tb->add (g_pal[t], 1); }
	g_noGate = new Label (6 + 40 + 11 + 8, (TBH - 30) / 2, 290, 30, TR ("No gate in this level: a wire is enough."), C_DIS); g_tb->addChild (g_noGate);
	g_btRedo = (new ToolButton (34, 34, TR ("Redo (Ctrl+Y)"), cb_redo))->setGlyph (WKT_REDO); g_tb->addRight (g_btRedo, 0);
	g_btUndo = (new ToolButton (34, 34, TR ("Undo (Ctrl+Z)"), cb_undo))->setGlyph (WKT_UNDO); g_tb->addRight (g_btUndo, 0);
	g_btDel = (new ToolButton (34, 34, TR ("Delete (Del)"), cb_del))->setGlyph (WKT_TRASH); g_tb->addRight (g_btDel, 4);
	g_board = new Board (0, 0, 508, 386); root.addChild (g_board);
	g_msg = new MsgBar (0, 0, 508, MSGH); root.addChild (g_msg);
	g_btNext = new Button (0, 0, 126, 30, TR ("Next level"), cb_next); ((Widget *) g_btNext)->hidden = true; root.addChild (g_btNext);
	// the bench: the truth table, the count, the simulation
	g_lbTable = new Label (0, 0, RIGHTW, 22, TR ("Truth table"), C_TEXT); root.addChild (g_lbTable);
	g_table = new TruthTable (0, 0, RIGHTW, 100); root.addChild (g_table);
	g_count = new CountView (0, 0, RIGHTW, 66); root.addChild (g_count);
	g_lbMode = new Label (0, 0, RIGHTW, 18, "", C_DIS); root.addChild (g_lbMode);
	g_btStep = (new ToolButton ((RIGHTW - 6) / 2, 32, TR ("One gate depth more (F8)"), cb_step))->setIcon (circuits_icon, IC_STEP)->setText (TR ("Step"))->setToggle (true, false);
	g_btStep->raised = true; root.addChild (g_btStep);
	g_btReset = (new ToolButton ((RIGHTW - 6) / 2, 32, TR ("Back to step 0 (F9)"), cb_reset))->setIcon (circuits_icon, IC_RESET)->setText (TR ("Reset"));
	g_btReset->raised = true; root.addChild (g_btReset);
	g_btCheck = (new ToolButton (RIGHTW, 36, TR ("Check every row (F5)"), cb_check))->setIcon (circuits_icon, IC_CHECK)->setText (TR ("Check"));
	g_btCheck->filled = true; g_btCheck->setOn (true); root.addChild (g_btCheck);
	// the cards over the board
	g_lesson = new LessonCard (0, 0, 500, 330); ((Widget *) g_lesson)->hidden = true; root.addChild (g_lesson);
	g_result = new ResultCard (0, 0, 420, 250); ((Widget *) g_result)->hidden = true; root.addChild (g_result);
	root.setResizable (true);
	root.setMinSize (920, 600);
	build_menu ();
	layout ();
	root.fitWorkArea ();
	if (g_npacks == 0)
	{
		ft_messagebox ("Circuits", TR ("No level found in SD:/apps/circuits.app/levels."), MB_OK);
		return 1;
	}
	// where the player was; then the pack given (after: a refusal's dialog over the level shown)
	int p = 0, l = 0;
	const char *lp = g_pr.lastPack (), *ll = g_pr.lastLevel ();
	for (int i = 0; i < g_npacks; i++)
		if (!strcmp (fs_basename (g_packs[i]->path), lp))
		{
			int k = g_packs[i]->find (ll);
			if (k >= 0 && level_open (g_pr, *g_packs[i], k)) { p = i; l = k; }
		}
	char args[256] = ""; int an = kapi_get_args (args, sizeof args);
	if (an > 0 && args[0] && ends_circuits (args)) { int ap = add_pack (args, true, true); if (ap >= 0) { p = ap; l = 0; } }
	show_level (p, l);
	if (an > 0 && args[0] && (!ends_circuits (args) || p < 0 || strcmp (g_packs[p]->path, args))) open_pack (args);	// (refused: the dialog says why)
	root.run ();
	save_board ();
	return 0;
}
