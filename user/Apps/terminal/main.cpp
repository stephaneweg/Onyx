//
// terminal/main.cpp -- a "dumb" terminal (tty) with TABS. It owns the keyboard and the scrollbacks
// only; each tab's shell is a separate program, /bin/cmd, spawned wired to two pipes: keystrokes go
// to its stdin, and its stdout is drained into the tab's scrollback. So all command parsing /
// pipelines / builtins live in cmd, not here. The terminal does local line editing + echo
// (lineedit.h: the cursor moves in the line with Left / Right / Home / End, Up / Down recall the lines
// sent before, Enter sends the line, Ctrl-C stops the running command, Ctrl-D sends EOF); cmd prints
// the prompt.
//
// The tabs (uikit::TabStrip): each its own cmd, screen, scrollback, history and current folder. A new
// one with the "+", Shell > New Tab or Ctrl+Shift+T; Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+PgDn / Ctrl+PgUp
// or a click change tabs; the cross, a middle click, Ctrl+Shift+W close one -- after a question when
// something runs in it. A tab's title is the command running in it, else its folder (the prompt's);
// a dot marks a tab where something runs. Closing a tab terminates its cmd AND everything under it
// (kapi_proc_tree: the pipeline's stages, a script's cmd and what it started...) at once; a shell that
// ends by itself (exit) takes its tab away; the last tab gone, the window closes.
//
// Its own decorated uikit window, with a loop of its own (not Root::run): the terminal must also
// pump the shells' output pipes every frame.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"		// recursive widget toolkit (TermView draws into a Canvas)
#include "lineedit.h"		// the line being typed: its cursor, the history

#define W		620		// standalone window size
#define H		420
#define STRIP_H		30		// the tabs' strip
#define MAXTABS		16
#define SCROLLBACK	200		// bounded scrollback (rows)
#define COLS		112		// stored chars per line
#define TERM_BG		0x001A3A46	// the console's own colours (a terminal stays dark: a deep
#define TERM_FG		0x00D4EAF0	// teal, as the modernised CDE's mock-up)

static int g_fw = 8, g_fh = 16, g_vrows = 1, g_cols = COLS;	// g_cols = columns that fit the view

// A tab: its shell, its pipes, its screen. The scrollback ring holds the finalized lines + the line
// currently being built (the program output not yet newline-terminated: the prompt). The line being
// typed is not in there: it is drawn after `cur` (wrapped), and enters the scrollback when it is sent.
struct Tab
{
	char ring[SCROLLBACK][COLS + 1];
	int  rfirst, rcount;
	char cur[COLS + 1];
	int  curlen;
	int  scroll;			// 0 = bottom
	void *to_cmd, *from_cmd;	// keystrokes -> cmd's stdin; cmd's stdout -> the screen
	void *cmd;			// the shell's process handle
	int  pid;			// its pid (0: not known yet)
	LineEdit le;			// the line being typed (cmd takes 2047 characters) + the history
	char last[48];			// the last line sent (the title while it runs)
	int  sent;			// lines sent so far
	int  busy;			// something runs (not the shell idle at its prompt)
};
static Tab *g_tab;			// the tab shown
static char g_live[COLS + LE_LINE + 1];	// cur + the line being typed, as drawn

using namespace uikit;
static TabStrip *g_strip;
static Widget *g_view;
static bool g_quit;			// the last tab is gone

// ---- scrollback / output ----------------------------------------------------

static void ring_push (Tab *t, const char *s)
{
	int idx;
	if (t->rcount < SCROLLBACK) { idx = (t->rfirst + t->rcount) % SCROLLBACK; t->rcount++; }
	else { idx = t->rfirst; t->rfirst = (t->rfirst + 1) % SCROLLBACK; }
	int i = 0;
	for (; s[i] && i < COLS; i++) t->ring[idx][i] = s[i];
	t->ring[idx][i] = '\0';
}
static const char *ring_line (const Tab *t, int i) { return t->ring[(t->rfirst + i) % SCROLLBACK]; }
static void flush_cur (Tab *t) { t->cur[t->curlen] = '\0'; ring_push (t, t->cur); t->curlen = 0; t->cur[0] = '\0'; }

// One output byte into a tab's display. Handles \n (newline), \t (tab), \b (erase, for
// local echo), and \f (form-feed = clear, emitted by cmd's `clear` builtin).
static void term_putc (Tab *t, char c)
{
	if (c == '\r') return;
	if (c == '\n') { flush_cur (t); return; }
	if (c == '\f') { t->rcount = t->rfirst = t->curlen = t->scroll = 0; t->cur[0] = '\0'; return; }
	if (c == '\b') { if (t->curlen > 0) t->cur[--t->curlen] = '\0'; return; }
	if (c == '\t') { do { if (t->curlen < g_cols) t->cur[t->curlen++] = ' '; } while ((t->curlen % 4) && t->curlen < g_cols); t->cur[t->curlen] = '\0'; return; }
	if (t->curlen >= g_cols) flush_cur (t);		// wrap at the visible width
	t->cur[t->curlen++] = c;
	t->cur[t->curlen] = '\0';			// keep cur a valid C-string: without this, a
							// shorter new line (e.g. the reprinted prompt) leaves
							// the previous line's tail visible past curlen.
}
static void term_puts (Tab *t, const char *s) { for (int i = 0; s[i]; i++) term_putc (t, s[i]); }

// ---- the shells -------------------------------------------------------------

static void start_cmd (Tab *t)
{
	t->to_cmd   = kapi_pipe ();
	t->from_cmd = kapi_pipe ();
	t->cmd = kapi_spawn ("SD:/bin/cmd", "", t->to_cmd, t->from_cmd);
	if (t->cmd == 0) term_puts (t, "terminal: cannot start /bin/cmd\n");
}

static int shell_pid (Tab *t)
{
	if (t->pid <= 0 && t->cmd)
	{
		struct kapi_proc_status ps;
		if (kapi_proc_wait (t->cmd, KAPI_WAIT_NOHANG | KAPI_WAIT_KEEP, &ps) >= 0 && ps.pid > 0) t->pid = ps.pid;
	}
	return t->pid;
}

// Pump a tab's cmd stdout into its scrollback -> the bytes consumed (the caller repaints the shown
// tab only when there was output).
static int drain_cmd (Tab *t)
{
	if (t->from_cmd == 0) return 0;
	char b[256]; int n, guard = 0, got = 0;
	while ((n = kapi_stream_read_nb (t->from_cmd, b, sizeof b)) > 0 && guard++ < 64)
	{
		for (int k = 0; k < n; k++) term_putc (t, b[k]);
		t->scroll = 0;
		got += n;
	}
	return got;
}

// Is the shell at its prompt ("<folder> $ ", cmd.c's prompt ()) with nothing under it?
static bool at_prompt (const Tab *t)
{
	return t->curlen >= 3 && t->cur[t->curlen - 3] == ' ' && t->cur[t->curlen - 2] == '$' && t->cur[t->curlen - 1] == ' ';
}
static void update_busy (Tab *t)
{
	int pid = shell_pid (t);
	int kids = pid > 0 ? kapi_proc_tree (pid, KAPI_TREE_LIST, 0, 0) : 0;
	t->busy = t->cmd != 0 && !kapi_proc_done (t->cmd) && (kids > 0 || (t->sent > 0 && !at_prompt (t)));
}

// The tab's title: the command running, else its folder (the prompt's: its last name; SD:/ itself).
static void tab_title (const Tab *t, char *out, int cap)
{
	int n = 0;
	if (t->busy && t->last[0])
		for (int i = 0; t->last[i] && n < cap - 1; i++) out[n++] = t->last[i];
	else if (at_prompt (t))
	{
		int end = t->curlen - 3, from = 0;		// the folder: cur[0 .. end)
		for (int i = 0; i < end - 1; i++) if (t->cur[i] == '/' || t->cur[i] == ':') from = i + 1;
		if (end > 0 && t->cur[end - 1] == '/') from = 0;	// (a volume's root: "SD:/")
		for (int i = from; i < end && n < cap - 1; i++) out[n++] = t->cur[i];
	}
	if (n == 0) { const char *s = "Shell"; while (*s && n < cap - 1) out[n++] = *s++; }
	out[n] = '\0';
}

static int tab_index (const Tab *t)
{
	for (int i = 0; i < g_strip->count (); i++) if (g_strip->data (i) == t) return i;
	return -1;
}

static void refresh_tab (Tab *t)
{
	int i = tab_index (t);
	if (i < 0) return;
	char title[48];
	tab_title (t, title, sizeof title);
	g_strip->setTitle (i, title);
	g_strip->setMark (i, t->busy != 0);
}

// The tab's shell and EVERYTHING under it terminated (its pipeline, a script's cmd, what they
// started), its pipes closed, its record freed.
static void tab_free (Tab *t)
{
	if (t->cmd && !kapi_proc_done (t->cmd))
	{
		int pid = shell_pid (t);
		if (pid > 0 && kapi_proc_tree (pid, KAPI_TREE_KILL, 0, 0) < 0)
			kapi_kill_pid (pid, 1);		// (a kernel before v91: the reaper ends its children)
	}
	if (t->to_cmd)   kapi_stream_close (t->to_cmd);	// (no pid yet: its stdin's end stops it)
	if (t->from_cmd) kapi_stream_close (t->from_cmd);
	if (t->cmd)
	{
		for (int k = 0; k < 100 && !kapi_proc_done (t->cmd); k++) kapi_msleep (10);
		if (kapi_proc_done (t->cmd)) kapi_wait (t->cmd);	// (the handle freed)
	}
	delete t;
}

static void show_tab (int i)
{
	Tab *t = (Tab *) g_strip->data (i);
	if (t == 0) return;
	g_strip->select (i);
	g_tab = t;
	g_view->invalidate (true);
}

static void new_tab ()
{
	if (g_strip->count () >= MAXTABS) return;
	Tab *t = new Tab ();				// (value-initialised: all zeros)
	if (t == 0) return;
	le_init (&t->le);
	int i = g_strip->add ("Shell", t);
	if (i < 0) { delete t; return; }
	start_cmd (t);
	show_tab (i);
}

static void remove_tab (int i)
{
	Tab *t = (Tab *) g_strip->data (i);
	g_strip->remove (i);
	if (t) tab_free (t);
	if (g_strip->count () == 0) { g_tab = 0; g_quit = true; return; }
	show_tab (g_strip->selected);
}

// A tab's close asked (the cross, a middle click, Ctrl+Shift+W, the menu): a question first when
// something runs there.
static void ask_close (int i)
{
	Tab *t = (Tab *) g_strip->data (i);
	if (t == 0) return;
	update_busy (t);
	if (t->busy)
	{
		char msg[160]; int n = 0;
		const char *a = "\"", *b = "\" is still running in this tab. Close the tab and stop it?";
		char what[48]; tab_title (t, what, sizeof what);
		for (int k = 0; a[k]; k++) msg[n++] = a[k];
		for (int k = 0; what[k] && n < 60; k++) msg[n++] = what[k];
		for (int k = 0; b[k] && n < (int) sizeof msg - 1; k++) msg[n++] = b[k];
		msg[n] = '\0';
		if (uk_messagebox ("Close Tab", msg, MB_OKCANCEL) != 1) return;
		i = tab_index (t);			// (the tabs may have moved meanwhile)
		if (i < 0) return;
	}
	remove_tab (i);
}

static void on_strip (Widget &) { show_tab (g_strip->selected); }
static void on_strip_close (Widget &) { ask_close (g_strip->closing); }
static void on_strip_new (Widget &) { new_tab (); }
static void m_new () { new_tab (); }
static void m_close () { if (g_strip->selected >= 0) ask_close (g_strip->selected); }
static void m_next () { g_strip->selectNext (1); }
static void m_prev () { g_strip->selectNext (-1); }

// ---- input ------------------------------------------------------------------
// Apply one key to the shown tab's line editor / scrollback.
static void term_key (Tab *t, int key)
{
	if (t == 0) return;
	if (key == KEY_PGUP) { t->scroll += g_vrows - 2; return; }	// the scrollback
	if (key == KEY_PGDN) { t->scroll -= g_vrows - 2; if (t->scroll < 0) t->scroll = 0; return; }
	t->scroll = 0;					// (typing shows the live line again)
	LineEdit *le = &t->le;
	switch (key)
	{
	case KEY_ENTER:
		if (t->to_cmd)
		{
			kapi_stream_write (t->to_cmd, le->buf, (unsigned) le->len);
			kapi_stream_write (t->to_cmd, "\n", 1);
		}
		if (le->len > 0)				// (the title while it runs)
		{
			int k = 0;
			for (; le->buf[k] && k < (int) sizeof t->last - 1; k++) t->last[k] = le->buf[k];
			t->last[k] = '\0';
			t->sent++;
		}
		term_puts (t, le->buf); term_putc (t, '\n');	// the line enters the scrollback
		le_commit (le, 1);
		break;
	case KEY_BACKSPACE: le_backspace (le); break;
	case KEY_DEL:	le_delete (le); break;
	case KEY_LEFT:	le_left (le); break;
	case KEY_RIGHT:	le_right (le); break;
	case KEY_HOME:	case 1: /* Ctrl-A */ le_home (le); break;
	case KEY_END:	case 5: /* Ctrl-E */ le_end (le); break;
	case KEY_UP:	le_up (le); break;		// the history
	case KEY_DOWN:	le_down (le); break;
	case 11: /* Ctrl-K */ le_kill (le); break;
	case 21: /* Ctrl-U */ le_clear (le); break;
	case 3:	/* Ctrl-C: the line typed is dropped, the running command stopped */
		term_puts (t, le->buf); term_puts (t, "^C\n");
		le_commit (le, 0);
		if (t->to_cmd) kapi_stream_write (t->to_cmd, "\x03", 1);
		break;
	case 4:	/* Ctrl-D */ if (t->to_cmd) kapi_stream_write (t->to_cmd, "\x04", 1); break;
	default:
		if (key >= ' ' && key < 0x7f) le_insert (le, (char) key);
		break;
	}
}

// The tabs' keys, before anything else: Ctrl+Shift+T / W, Ctrl+(Shift+)Tab, Ctrl+PgUp / PgDn.
// (Ctrl with a letter comes as its control code -- ^T = 20 -- with Shift held as a modifier.)
static bool tab_keys (long k)
{
	unsigned m = kapi_get_modifiers ();
	if (!(m & MOD_CTRL)) return false;
	bool shift = (m & MOD_SHIFT) != 0;
	if (k == KEY_TAB) { g_strip->selectNext (shift ? -1 : 1); return true; }
	if (k == KEY_PGUP) { g_strip->selectNext (-1); return true; }
	if (k == KEY_PGDN) { g_strip->selectNext (1); return true; }
	if (!shift) return false;
	if (k == UK_CTRL ('T') || k == 'T' || k == 't') { new_tab (); return true; }
	if (k == UK_CTRL ('W') || k == 'W' || k == 'w') { m_close (); return true; }
	return false;
}

// ---- view (uikit widget) -------------------------------------------------------
// Renders the shown tab's scrollback into its Canvas. Recomputes the visible rows/cols from its
// current logical size each frame, so a window resize just reflows the text.
class TermView : public Widget
{
public:
	TermView (int w, int h) : Widget (0, 0, w, h) { canFocus = true; }

	void recompute ()
	{
		g_vrows = (height - 8) / g_fh; if (g_vrows < 2) g_vrows = 2;
		g_cols  = (width  - 8) / g_fw; if (g_cols < 8) g_cols = 8; if (g_cols > COLS) g_cols = COLS;
	}

	void onDraw () override
	{
		recompute ();
		canvas.clear (TERM_BG);
		Tab *t = g_tab;
		if (t == 0) return;

		// the live line: the unfinished output (the prompt), then the line being typed --
		// wrapped over as many rows as it takes
		int live = 0;
		for (int i = 0; i < t->curlen; i++) g_live[live++] = t->cur[i];
		for (int i = 0; i < t->le.len; i++) g_live[live++] = t->le.buf[i];
		g_live[live] = 0;
		int caret = t->curlen + t->le.cur;
		int liverows = live / g_cols + 1;

		int total = t->rcount + liverows;
		int maxscroll = total - g_vrows; if (maxscroll < 0) maxscroll = 0;
		if (t->scroll > maxscroll) t->scroll = maxscroll;
		int first = total - g_vrows - t->scroll; if (first < 0) first = 0;

		for (int r = 0; r < g_vrows; r++)
		{
			int idx = first + r;
			if (idx < 0 || idx >= total) continue;
			if (idx < t->rcount) { canvas.text (4, 4 + r * g_fh, ring_line (t, idx), TERM_FG); continue; }
			int k = idx - t->rcount;			// a row of the live line
			char row[COLS + 1]; int n = 0;
			for (int i = k * g_cols; i < live && n < g_cols; i++) row[n++] = g_live[i];
			row[n] = 0;
			canvas.text (4, 4 + r * g_fh, row, TERM_FG);
			if (caret / g_cols == k)			// the caret, at the cursor
				canvas.fillRect (4 + (caret % g_cols) * g_fw, 4 + r * g_fh, 2, g_fh, uk_tone (C_ACCENT, 180));	// (the accent, lit)
		}
	}

	bool onKey (long k) override { term_key (g_tab, (int) k); invalidate (true); return true; }

	bool onMouse (int, int, int bl, int, int, int wheel) override	// wheel scrolls the scrollback
	{
		if (bl && !hasFocus) setFocus ();
		if (wheel && g_tab) { g_tab->scroll += wheel * 3; if (g_tab->scroll < 0) g_tab->scroll = 0; invalidate (true); }
		return true;
	}
};

// ---- standalone input trampolines (mirror uikit::Root::run's routing) ----------
static Root *g_saroot = 0;

static void sa_ptr (unsigned long, int ev, long v)
{
	static int bl = 0, br = 0, bm = 0;
	if (g_saroot == 0) return;
	int c = GUI_PTR_CHANGED (v);
	switch (ev)
	{
	case GUI_EVENT_PTR_DOWN:  if (c & 1) bl = 1; if (c & 2) br = 1; if (c & 4) bm = 1; break;
	case GUI_EVENT_PTR_UP:    if (c & 1) bl = 0; if (c & 2) br = 0; if (c & 4) bm = 0; break;
	case GUI_EVENT_PTR_LEAVE: g_saroot->handleMouse (-1, -1, 0, 0, 0, 0); return;
	case GUI_EVENT_PTR_WHEEL: g_saroot->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, GUI_PTR_WHEEL (v)); return;
	case GUI_EVENT_WINCTL:				// a title button: the window menu, maximise
		if (v == KAPI_FRAME_MENU) g_saroot->windowMenu ();
		else if (v == KAPI_FRAME_MAXIMISE) g_saroot->maximise (!g_saroot->maximised ());
		return;
	default: break;
	}
	g_saroot->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, 0);
}

static void sa_key (unsigned long, int ev, long v)
{
	if (g_saroot == 0 || ev != GUI_EVENT_KEY) return;
	bool modal = false;				// (a question open: the keys are its own)
	for (Widget *n = g_saroot->lastChild; n; n = n->prevSib) if (n->modal) modal = true;
	if (!modal && tab_keys (v)) { g_saroot->invalidate (true); return; }
	g_saroot->handleKey (v);
}

// ---- entry -------------------------------------------------------------------
int main (void)
{
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	Root root (W, H, "terminal");
	if (root.canvas.px == 0) return 1;
	root.setBg (TERM_BG);				// (the console covers the window)
	g_strip = new TabStrip (0, 0, W, STRIP_H, on_strip);
	g_strip->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_strip->activeFace = TERM_BG;			// (the chosen tab opens onto the console)
	g_strip->onClose = on_strip_close;
	g_strip->onNew = on_strip_new;
	root.addChild (g_strip);
	TermView *view = new TermView (W, H - STRIP_H);
	view->top = STRIP_H;
	view->anchor = ANCHOR_FILL;
	root.addChild (view);
	g_view = view;
	root.setResizable (true);			// (the view reflows to any size: maximise works)
	view->setFocus ();			// keys route to the terminal
	g_saroot = &root;

	static Menu menu;
	menu.menu ("Shell");
	menu.item ("New Tab", "Shift+^T", 0, m_new);
	menu.item ("Close Tab", "Shift+^W", 0, m_close);
	menu.separator ();
	menu.item ("Next Tab", "^Tab", 0, m_next);
	menu.item ("Previous Tab", "Shift+^Tab", 0, m_prev);
	menu.publish ();

	new_tab ();

	kapi_set_pointer_handler (sa_ptr);
	kapi_set_key_handler (sa_key);
	unsigned frame = 0;
	while (!should_exit () && !g_quit)
	{
		for (int i = 0; i < g_strip->count (); i++)	// every tab's output (the shown one repainted)
		{
			Tab *t = (Tab *) g_strip->data (i);
			if (drain_cmd (t) > 0)
			{
				refresh_tab (t);		// (a new prompt: its folder in the title)
				if (t == g_tab) view->invalidate (true);
			}
		}
		pump_events ();			// then echo the keys typed this frame (onKey invalidates)
		for (int i = 0; i < g_strip->count (); i++)	// a shell that ended (exit, killed): its tab goes
		{
			Tab *t = (Tab *) g_strip->data (i);
			if (t->cmd && kapi_proc_done (t->cmd))
			{
				drain_cmd (t);
				remove_tab (i);
				i--;
			}
		}
		if (g_quit) break;
		if (frame++ % 20 == 0)			// (~3 times a second) the titles, the marks
			for (int i = 0; i < g_strip->count (); i++)
			{
				Tab *t = (Tab *) g_strip->data (i);
				update_busy (t);
				refresh_tab (t);
			}
		if (!root.valid) { root.draw (); kapi_present (); }
		msleep (16);
	}
	while (g_strip->count () > 0)			// the window closed: every tab's shell and what it runs
	{
		Tab *t = (Tab *) g_strip->data (0);
		g_strip->remove (0);
		tab_free (t);
	}
	return 0;
}
