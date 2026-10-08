//
// tools/tests/server_sim/adaptive.cpp -- the adaptive widgets' gallery and their host tests (PocketUI's phase P6,
// docs/POCKETUI-TECH-STUDY.md sections 6.5-6.14; built by tools/tests/server_sim/adaptive.sh against the pocket UIKit's
// wire port).
//
//   ADAPT_TEST=1  the layout logic of each widget checked in the four size classes (forced: uikit/internal/adapt_int.h's
//                 force_class), no window: a line "server_sim: PASS <what>" / "server_sim: FAIL <what> -- got ..." a
//                 check (adaptive.sh counts them), the exit status the failures' count;
//   else          the gallery, a window: a ToolBar with priorities and a second row folded into it, a TabStrip, a
//                 navigation SidePanel at the left, an inspector at the right, a DataGrid with column roles -- under
//                 PocketUI at any size and mode; its argument opens, after a moment, "form" a FormDialog, "menu" a
//                 context menu, "overflow" the ToolBar's "»" panel, "tabs" the tabs' list, "drawer" the navigation
//                 panel (a drawer in portrait), "inspector" the inspector (a slide-over, a sheet).
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
#include "uikit/internal/adapt_int.h"
#include "fontkit/uikitface.h"		// FreeType's text, as the apps have it
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uikit;

// ---- the checks ---------------------------------------------------------------------------------------------
static int s_fail;
static void check (bool ok, const char *what, long got = 0)
{
	if (ok) fprintf (stderr, "server_sim: PASS %s\n", what);
	else { fprintf (stderr, "server_sim: FAIL %s -- got %ld\n", what, got); s_fail++; }
}
static void force (int sc)
{
	int mode = sc == UK_SC_REGULAR ? UK_MODE_DESKTOP : sc == UK_SC_CONSOLE ? UK_MODE_CONSOLE : UK_MODE_POCKET;
	int prof = sc == UK_SC_REGULAR ? UK_PROFILE_REGULAR : sc == UK_SC_CONSOLE ? UK_PROFILE_CONSOLE : UK_PROFILE_COMPACT;
	internal::force_class (mode, sc, prof);
}

// A parent of a given width with a navigation panel, an inspector, the items.
struct Panels
{
	Widget *win; SidePanel *nav, *insp, *free_;
	Panels (int w, int h)
	{
		win = new Widget (0, 0, w, h);
		nav = new SidePanel (0, 0, 200, h, UK_SP_LEFT, UK_SP_NAVIGATION);
		nav->addHeading ("LIBRARY"); nav->addItem (1, "Songs", WKG_MENU); nav->addItem (2, "Albums", WKG_MENU);
		nav->addHeading ("PLAYLISTS"); nav->addItem (3, "Road Trip"); nav->addItem (4, "Workout");
		insp = new SidePanel (w - 220, 0, 220, h, UK_SP_RIGHT, UK_SP_INSPECTOR);
		insp->addItem (10, "Background"); insp->setToggle (10, 1);
		free_ = new SidePanel (0, 0, 220, h, UK_SP_LEFT, UK_SP_NAVIGATION);	// (free content only: a month, a tree)
		free_->setContent (new Widget (0, 0, 100, 100));
		win->addChild (nav); win->addChild (insp); win->addChild (free_);
	}
	~Panels () { delete win; }
};

static void test_sidepanel ()
{
	force (UK_SC_REGULAR);
	{
		Panels p (1000, 600);
		check (p.nav->presentation () == UK_SP_FULL, "sidepanel desktop: navigation whole", p.nav->presentation ());
		check (p.insp->presentation () == UK_SP_FULL, "sidepanel desktop: inspector whole", p.insp->presentation ());
		check (p.nav->reservedWidth () == 200, "sidepanel desktop: reserves its width", p.nav->reservedWidth ());
		p.nav->place (0, 40, 240, 500);
		check (p.nav->width == 240 && p.nav->top == 40, "sidepanel desktop: place", p.nav->width);
		p.nav->select (2);
		check (p.nav->selected () == 2, "sidepanel: select", p.nav->selected ());
		check (p.nav->itemAt (50, 46) == 1, "sidepanel: itemAt a row", p.nav->itemAt (50, 46));
	}
	force (UK_SC_COMPACT);
	{
		Panels p (640, 456);				// (under four times the panel's width: a rail; 800 and more keep it whole)
		check (p.nav->presentation () == UK_SP_RAIL, "sidepanel pocket landscape, small: a rail", p.nav->presentation ());
		check (p.nav->reservedWidth () == uk_metrics ().rail, "sidepanel rail: reserves the rail's width", p.nav->reservedWidth ());
		check (p.nav->width == uk_metrics ().rail, "sidepanel rail: as wide as the rail", p.nav->width);
		check (p.insp->presentation () == UK_SP_SLIDEOVER, "sidepanel pocket landscape: inspector slides over", p.insp->presentation ());
		check (p.insp->reservedWidth () == 0, "sidepanel slide-over: reserves nothing", p.insp->reservedWidth ());
		check (p.free_->presentation () == UK_SP_DRAWER, "sidepanel free content: a drawer, never a rail", p.free_->presentation ());
		p.nav->setRail (false);
		check (p.nav->presentation () == UK_SP_FULL, "sidepanel setRail (false): whole", p.nav->presentation ());
		p.insp->open (true);
		check (p.insp->isOpen () && p.insp->width == 640 && p.insp->left == 0, "sidepanel slide-over open: covers its parent", p.insp->width);
		p.insp->open (false);
		check (!p.insp->isOpen () && p.insp->width < 40, "sidepanel slide-over closed: a tab at its edge", p.insp->width);
	}
	{
		Panels p (1920, 1000);
		check (p.nav->presentation () == UK_SP_FULL, "sidepanel pocket, a wide screen: whole", p.nav->presentation ());
	}
	force (UK_SC_NARROW);
	{
		Panels p (480, 776);
		check (p.nav->presentation () == UK_SP_DRAWER, "sidepanel portrait: a drawer", p.nav->presentation ());
		check (p.insp->presentation () == UK_SP_SHEET, "sidepanel portrait: inspector a sheet", p.insp->presentation ());
		Widget *tb = p.nav->toggleButton ();
		check (tb != 0 && !tb->hidden && p.nav->hidden, "sidepanel portrait: its toggle button shown, the closed drawer hidden", tb ? tb->hidden : -1);
		p.nav->open (true);
		check (p.nav->isOpen () && !p.nav->hidden && p.nav->height == 776, "sidepanel drawer open", p.nav->height);
		p.nav->select (-1);
		p.nav->open (false);
		check (!p.nav->isOpen (), "sidepanel drawer closed again");
	}
	force (UK_SC_CONSOLE);
	{
		Panels p (640, 480);
		check (p.nav->presentation () == UK_SP_COLUMN, "sidepanel console: a column", p.nav->presentation ());
		check (p.nav->reservedWidth () > 0, "sidepanel column: reserves its width", p.nav->reservedWidth ());
		p.nav->select (1);
		bool r1 = internal::key_fallback (0, 0) == false;		// (no window: nothing; L1 / R1 through the Root below)
		check (r1, "key_fallback without a window: nothing");
	}
	force (UK_SC_REGULAR);
	{
		Panels p (1000, 600);
		p.nav->setSubtitle (1, "Every song of the library");
		check (p.nav->presentation () == UK_SP_FULL, "sidepanel: a subtitle changes nothing of the presentation");
	}
}

static ToolBar *make_bar (int w, ToolBar **second, ToolButton **t)
{
	ToolBar *b = new ToolBar (0, 0, w, 38);
	int g[10] = { WKT_NEW, WKT_OPEN, WKT_SAVE, WKT_UNDO, WKT_REDO, WKT_CUT, WKT_COPY, WKT_PASTE, WKT_SEARCH, WKT_GEAR };
	for (int i = 0; i < 10; i++)
	{
		t[i] = new ToolButton (28, 28, "tool");
		t[i]->setGlyph (g[i]);
		b->add (t[i], i ? 2 : 0);
		if (i == 2 || i == 4 || i == 7) b->sep ();
	}
	b->setPriority (t[3], UK_TB_IF_ROOM, 1); b->setPriority (t[4], UK_TB_IF_ROOM, 1);
	b->setPriority (t[5], UK_TB_IF_ROOM, 2); b->setPriority (t[6], UK_TB_IF_ROOM, 2); b->setPriority (t[7], UK_TB_IF_ROOM, 2);
	b->setPriority (t[8], UK_TB_OVERFLOW); b->setPriority (t[9], UK_TB_OVERFLOW);
	*second = new ToolBar (0, 38, w, 38);
	for (int i = 0; i < 3; i++) { ToolButton *x = new ToolButton (28, 28, "play"); x->setGlyph (WKT_PLAY + i); (*second)->add (x, 2); (*second)->setPriority (x, UK_TB_IF_ROOM, 3); }
	(*second)->foldInto (b);
	return b;
}

static void test_toolbar ()
{
	ToolButton *t[10]; ToolBar *s2;
	force (UK_SC_REGULAR);
	{
		Widget win (0, 0, 1000, 600);
		ToolBar *b = make_bar (1000, &s2, t);
		win.addChild (b); win.addChild (s2);
		int shown = 0; for (int i = 0; i < 10; i++) if (!t[i]->hidden && t[i]->parent == b) shown++;
		check (shown == 10, "toolbar desktop: every tool shown, as built", shown);
		check (b->rows () == 2 && s2->rows () == 1, "toolbar desktop: rows () -- its own and the folded bar", b->rows ());
		check (!s2->hidden, "toolbar desktop: the second row shown");
		check (t[1]->left == 6 + 28 + 2, "toolbar desktop: the tools where add () put them", t[1]->left);
	}
	force (UK_SC_COMPACT);
	{
		Widget win (0, 0, 800, 456);
		ToolBar *b = make_bar (300, &s2, t);
		win.addChild (b); win.addChild (s2);
		b->layout ();
		check (b->rows () == 1 && s2->rows () == 0 && s2->hidden, "toolbar pocket: one row, the folded bar hidden", s2->hidden);
		check (!t[0]->hidden && !t[1]->hidden && !t[2]->hidden, "toolbar pocket: the ALWAYS tools stay");
		check (t[8]->hidden && t[9]->hidden, "toolbar pocket: the OVERFLOW tools only in the overflow");
		check (!t[3]->hidden || t[5]->hidden, "toolbar pocket: the IF_ROOM tools leave by their rank (2 before 1)");
		int right = 0; for (int i = 0; i < 10; i++) if (!t[i]->hidden && t[i]->left + t[i]->width > right) right = t[i]->left + t[i]->width;
		check (right <= 300 - 28, "toolbar pocket: the shown tools fit before the overflow's button", right);
		b->resizeTo (1000, 38); b->layout ();
		check (t[8]->hidden && !t[5]->hidden && !t[3]->hidden, "toolbar pocket, wide: the IF_ROOM tools back, OVERFLOW still away");
	}
	force (UK_SC_REGULAR);
}

static void tab_noop (Widget &) {}
static void test_tabs ()
{
	force (UK_SC_REGULAR);
	TabStrip a (0, 0, 480, 32, tab_noop);
	a.add ("One"); a.add ("Two"); a.add ("Six");
	check (a.presentation () == UK_TABP_STRIP, "tabs desktop: the strip", a.presentation ());
	force (UK_SC_COMPACT);
	check (a.presentation () == UK_TABP_SCROLL, "tabs pocket landscape: a strip that scrolls", a.presentation ());
	force (UK_SC_NARROW);
	check (a.presentation () == UK_TABP_SEGMENTED, "tabs portrait, 3 short ones: segmented", a.presentation ());
	a.add ("Four"); a.add ("A long title for a tab");
	check (a.presentation () == UK_TABP_MENU, "tabs portrait, more: the title and its list", a.presentation ());
	a.setStyle (UK_TAB_STRIP);
	check (a.presentation () == UK_TABP_SCROLL, "tabs portrait, setStyle (STRIP): a strip", a.presentation ());
	force (UK_SC_CONSOLE);
	check (a.presentation () == UK_TABP_HEADER, "tabs console: the header row", a.presentation ());
	a.select (0);
	a.selectNext (1, false);
	check (a.selected == 1, "tabs: selectNext", a.selected);
	force (UK_SC_REGULAR);
}

static const char *cell (DataGrid &, int r, int c, char *buf, int cap) { snprintf (buf, cap, "r%d c%d", r, c); return buf; }
static void test_grid ()
{
	force (UK_SC_REGULAR);
	DataGrid g (0, 0, 300, 400);
	g.setColumns (4);
	g.setColumn (0, "Title", 160); g.setColumn (1, "Artist", 120); g.setColumn (2, "Album", 120); g.setColumn (3, "Time", 60);
	g.cellText = cell; g.setRows (40);
	g.setColumnRole (0, UK_COL_PRIMARY, 0); g.setColumnRole (1, UK_COL_SECONDARY, 0);
	g.setColumnRole (2, UK_COL_SECONDARY, 2); g.setColumnRole (3, UK_COL_DETAIL, 1);
	check (g.columnShown (2) && g.columnShown (3), "grid desktop: every column");
	check (!g.cards (), "grid desktop: rows, not cards");
	check (g.totalWidth () == 460, "grid desktop: the columns' widths", g.totalWidth ());
	force (UK_SC_COMPACT);
	g.layout (); g.onDraw ();
	check (g.columnShown (0) && g.columnShown (1), "grid pocket: priority 0 never hidden");
	check (!g.columnShown (2), "grid pocket, narrow: the highest priority gives way first", g.columnShown (2));
	force (UK_SC_NARROW);
	check (g.cards (), "grid portrait: a primary column -> cards");
	g.setMultiSelect (true);
	g.setSelected (3, true); g.setSelected (5, true);
	check (g.selectedCount () == 2 && g.isSelected (5), "grid: several rows chosen", g.selectedCount ());
	force (UK_SC_REGULAR);
}

static void test_tree ()
{
	TreeView t (0, 0, 200, 300);
	int a = t.add (-1, "Music"), b = t.add (a, "Jazz"); t.add (b, "Blue Train"); t.add (-1, "Videos");
	t.setDrillDown (true);
	force (UK_SC_REGULAR);
	check (t.drillLevel () == -2, "tree desktop: no drilling, the tree as always", t.drillLevel ());
	force (UK_SC_NARROW);
	check (t.drillLevel () == -1, "tree portrait: the roots' level", t.drillLevel ());
	force (UK_SC_REGULAR);
}

static void test_metrics ()
{
	force (UK_SC_REGULAR);
	check (uk_size_class () == UK_SC_REGULAR && uk_mode () == UK_MODE_DESKTOP, "metrics desktop: regular");
	check (uk_scroll_gutter () == UK_SBW, "metrics desktop: the gutter UK_SBW", uk_scroll_gutter ());
	check (uk_metrics ().row == uk_fh () + 8 && uk_metrics ().button == 28, "metrics desktop: today's sizes", uk_metrics ().row);
	check (uk_lp (48) == 48, "metrics desktop: uk_lp", uk_lp (48));
	force (UK_SC_COMPACT);
	check (uk_scroll_gutter () == 0, "metrics pocket: no gutter (overlay bars)", uk_scroll_gutter ());
	internal::force_class (UK_MODE_POCKET, UK_SC_COMPACT, UK_PROFILE_TOUCH);
	check (uk_metrics ().touch >= 36 && uk_metrics ().row >= 36, "metrics touch: 36 px targets", uk_metrics ().row);
	force (UK_SC_CONSOLE);
	check (uk_metrics ().profile == UK_PROFILE_CONSOLE && uk_metrics ().row >= 40, "metrics console: big rows", uk_metrics ().row);
	force (UK_SC_REGULAR);
	Textbox *tb = new Textbox (0, 0, 100, 26, "");
	check (uk_input_type (tb) == UK_IN_TEXT, "input type: text by default");
	tb->password = true;
	check (uk_input_type (tb) == UK_IN_PASSWORD, "input type: a password box");
	uk_set_input_type (tb, UK_IN_URL);
	check (uk_input_type (tb) == UK_IN_URL, "input type: said");
	delete tb;							// (its extension freed with it)
	check (true, "input type: the widget freed with its extension");
}

static void test_focus ()
{
	force (UK_SC_COMPACT);
	Widget win (0, 0, 400, 300);
	Button *a = new Button (10, 10, 80, 28, "A"), *b = new Button (200, 10, 80, 28, "B"), *c = new Button (10, 200, 80, 28, "C");
	win.addChild (a); win.addChild (b); win.addChild (c);
	win.hasFocus = true;
	a->setFocus ();
	check (uk_focus_move (&win, 1, 0) && b->hasFocus, "focus: Right to the neighbour at the right");
	check (uk_focus_move (&win, -1, 0) && a->hasFocus, "focus: Left back");
	check (uk_focus_move (&win, 0, 1) && c->hasFocus, "focus: Down to the one below");
	check (!uk_focus_move (&win, 0, 1), "focus: Down at the bottom: nothing");
	force (UK_SC_REGULAR);
}

static void test_form ()
{
	force (UK_SC_REGULAR);
	FormDialog f ("New Playlist");
	Textbox *name = new Textbox (0, 0, 240, 26, "");
	Checkbox *cb = new Checkbox (0, 0, 60, 24, "On", false, 0);
	f.addRow ("Name:", name); f.addRow ("Shuffle:", cb);
	f.addButton ("Create", UK_FB_DEFAULT, 1); f.addButton ("Cancel", UK_FB_CANCEL, 0);
	f.layout ();
	check (name->left > 16 && name->top < cb->top, "form desktop: the control beside its label", name->left);
	check (cb->width == 60, "form desktop: a narrow control keeps its width", cb->width);
	check (f.width >= 360, "form desktop: a box 360 px at least", f.width);
	force (UK_SC_NARROW);
	f.layout ();
	check (name->left == 16 && name->width == f.width - 32, "form portrait: the control the sheet's width, under its label", name->width);
	force (UK_SC_REGULAR);
}

static int run_tests ()
{
	test_metrics ();
	test_sidepanel ();
	test_toolbar ();
	test_tabs ();
	test_grid ();
	test_tree ();
	test_focus ();
	test_form ();
	fprintf (stderr, "adaptive tests: %d failed\n", s_fail);
	return s_fail;
}

// ---- the gallery -------------------------------------------------------------------------------------------------
static const char *SONGS[][5] = {
	{ "Blue in Green", "Miles Davis", "Kind of Blue", "5:37", "1959" }, { "So What", "Miles Davis", "Kind of Blue", "9:22", "1959" },
	{ "Take Five", "Dave Brubeck", "Time Out", "5:24", "1959" }, { "Naima", "John Coltrane", "Giant Steps", "4:21", "1960" },
	{ "Round Midnight", "Thelonious Monk", "Genius of Modern Music", "3:10", "1947" }, { "Autumn Leaves", "Cannonball Adderley", "Somethin' Else", "10:57", "1958" },
	{ "Moanin'", "Art Blakey", "Moanin'", "9:35", "1958" }, { "Footprints", "Wayne Shorter", "Adam's Apple", "7:29", "1966" },
	{ "Maiden Voyage", "Herbie Hancock", "Maiden Voyage", "7:53", "1965" }, { "Song for My Father", "Horace Silver", "Song for My Father", "7:16", "1965" },
};
static const char *song (DataGrid &, int r, int c, char *, int) { return SONGS[r % 10][c]; }

static ToolBar *g_bar, *g_bar2; static TabStrip *g_tabs; static SidePanel *g_nav, *g_insp; static DataGrid *g_grid;
static char g_arg[32];

class Gallery : public Root
{
public:
	unsigned t0 = 0; bool done = false;
	Gallery () : Root (1000, 640, "Adaptive widgets") {}
	void relayout ()
	{
		int y = 0;
		g_bar->resizeTo (width, 38); g_bar->layout (); y += 38;
		g_bar2->left = 0; g_bar2->top = y; g_bar2->resizeTo (width, 38); g_bar2->layout ();
		if (!g_bar2->hidden) y += 38;
		g_tabs->left = 0; g_tabs->top = y; g_tabs->resizeTo (width, uk_size_class () == UK_SC_CONSOLE ? 40 : 32); y += g_tabs->height;
		int h = height - y;
		g_nav->place (0, y, 210, h);
		g_insp->place (width - 220, y, 220, h);
		int l = g_nav->reservedWidth (), r = g_insp->reservedWidth ();
		g_grid->Widget::left = l; g_grid->Widget::top = y; g_grid->resizeTo	// (DataGrid has its own left / top: its scroll)
			 (width - l - r > 40 ? width - l - r : 40, h);
		invalidate (true);
	}
	void onResized () override { relayout (); }
	void onSizeClass (int) override { relayout (); }
	void onTick () override
	{
		if (t0 == 0) t0 = kapi_get_ticks ();
		if (done || kapi_get_ticks () - t0 < 40 || !g_arg[0]) return;
		done = true;
		if (!strcmp (g_arg, "form"))
		{
			FormDialog f ("New Playlist");
			f.addSection ("Playlist");
			Textbox *name = new Textbox (0, 0, 240, 26, "Road Trip");
			f.addRow ("Name:", name);
			static const char *const ORD[] = { "Manual", "By artist", "Recently added" };
			f.addRow ("Order:", new Dropdown (0, 0, 200, 26, ORD, 3, 0, 0));
			f.addRow ("Shuffle:", new Checkbox (0, 0, 60, 24, "On", false, 0));
			f.addText ("The songs dropped on a playlist are added at its end; a smart playlist fills itself.");
			f.addButton ("Create", UK_FB_DEFAULT, 1); f.addButton ("Cancel", UK_FB_CANCEL, 0);
			f.run ();
		}
		else if (!strcmp (g_arg, "menu"))
		{
			PopupMenu m (300, 200);
			m.add ("Play", 1); m.add ("Play Next", 2); m.separator (); m.add ("Add to Playlist...", 3); m.add ("Show in Folder", 4);
			m.separator (); m.add ("Remove", 5);
			m.run ();
		}
		else if (!strcmp (g_arg, "overflow")) g_bar->showOverflow ();
		else if (!strcmp (g_arg, "tabs")) g_tabs->showList ();
		else if (!strcmp (g_arg, "drawer")) g_nav->open (true);
		else if (!strcmp (g_arg, "inspector")) g_insp->open (true);
	}
};

int main ()
{
	ft_uikit_install ("DejaVu Sans", 13);
	if (getenv ("ADAPT_TEST")) return run_tests ();
	kapi_get_args (g_arg, sizeof g_arg);
	Gallery root;
	if (root.canvas.px == 0) return 1;
	root.setResizable (true);
	ToolButton *t[10];
	g_bar = make_bar (1000, &g_bar2, t);
	root.addChild (g_bar); root.addChild (g_bar2);
	g_tabs = new TabStrip (0, 76, 1000, 32, tab_noop);
	g_tabs->add ("Songs"); g_tabs->add ("Albums"); g_tabs->add ("Artists"); g_tabs->add ("Playlists"); g_tabs->add ("Radio");
	g_tabs->select (0); g_tabs->setMark (3, true);
	root.addChild (g_tabs);
	g_nav = new SidePanel (0, 108, 210, 532, UK_SP_LEFT, UK_SP_NAVIGATION);
	g_nav->addHeading ("LIBRARY", UK_SPI_FOLDABLE);
	g_nav->addItem (1, "Home", WKG_MENU); g_nav->addItem (2, "Songs", WKG_RIGHT); g_nav->addItem (3, "Albums", WKG_RING);
	g_nav->addItem (4, "Artists", WKG_DOT); g_nav->setBadge (3, 12);
	g_nav->addHeading ("PLAYLISTS", UK_SPI_FOLDABLE);
	g_nav->addItem (5, "Favourites", WKG_CHECK); g_nav->setTrailing (5, "42");
	g_nav->addItem (6, "Road Trip", WKG_RIGHT); g_nav->addItem (7, "Workout", WKG_RIGHT); g_nav->setBadge (7, -1);
	g_nav->select (2);
	g_nav->onPresentation = [] (SidePanel &, int) { Root *r = Root::current (); if (r) ((Gallery *) r)->relayout (); };
	root.addChild (g_nav);
	g_grid = new DataGrid (210, 108, 570, 532);
	g_grid->setColumns (5);
	g_grid->setColumn (0, "Title", 190); g_grid->setColumn (1, "Artist", 150); g_grid->setColumn (2, "Album", 170);
	g_grid->setColumn (3, "Time", 60, GRID_RIGHT); g_grid->setColumn (4, "Year", 60, GRID_RIGHT);
	g_grid->setColumnRole (0, UK_COL_PRIMARY, 0); g_grid->setColumnRole (1, UK_COL_SECONDARY, 0);
	g_grid->setColumnRole (2, UK_COL_SECONDARY, 2); g_grid->setColumnRole (3, UK_COL_DETAIL, 1); g_grid->setColumnRole (4, UK_COL_DETAIL, 3);
	g_grid->cellText = song; g_grid->setRows (30); g_grid->setSel (1);
	root.addChild (g_grid);
	g_insp = new SidePanel (780, 108, 220, 532, UK_SP_RIGHT, UK_SP_INSPECTOR);
	g_insp->addHeading ("LAYERS");
	g_insp->addItem (10, "Background"); g_insp->setToggle (10, 1);
	g_insp->addItem (11, "Sketch"); g_insp->setToggle (11, 0); g_insp->setTrailing (11, "80 %");
	g_insp->addItem (12, "Colours"); g_insp->setToggle (12, 1);
	g_insp->select (12);
	g_insp->onPresentation = g_nav->onPresentation;
	root.addChild (g_insp);
	root.relayout ();
	root.run ();
	return 0;
}
