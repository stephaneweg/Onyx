//
// terminal/main.cpp -- a "dumb" terminal (tty). It owns the keyboard and scrollback
// only; the actual shell is a separate program, /bin/cmd.elf. At startup the terminal
// spawns cmd wired to two pipes: keystrokes go to cmd's stdin, and cmd's stdout is
// drained into the scrollback. So all command parsing / pipelines / builtins live in
// cmd, not here. The terminal does local line editing + echo (Backspace edits, Enter
// sends the line, Ctrl-D sends EOF); cmd prints the prompt.
//
// It runs in TWO modes (same scrollback + rendering):
//   - embedded in the activity shell as a SECONDARY app: it registers, gets a surface
//     viewport, and is driven by the shell's forwarded input over the mailbox.
//   - standalone fallback (no shell): its own decorated wtk window.
// Both modes need a custom loop (not embed::run / Root::run) because the terminal must
// also pump cmd's output pipe every frame.
//
#include "kapi.h"
#include "wtk/wtk.h"		// recursive widget toolkit (TermView draws into a Canvas)
#include "applib.h"		// should_exit, pump_events, msleep
#include "embed.h"		// run embedded in the activity shell (surface + mailbox)

#define W		620		// standalone window size
#define H		400
#define SCROLLBACK	200		// bounded scrollback (rows)
#define COLS		112		// stored chars per line

static int g_fw = 8, g_fh = 16, g_vrows = 1, g_cols = COLS;	// g_cols = columns that fit the view

// Scrollback ring of finalized lines + the line currently being built (prompt + echo
// + the program output not yet newline-terminated).
static char g_ring[SCROLLBACK][COLS + 1];
static int  g_rfirst = 0, g_rcount = 0;
static char g_cur[COLS + 1];
static int  g_curlen = 0;
static int  g_scroll = 0;		// 0 = bottom

// The shell process + its pipes.
static void *g_to_cmd = 0;		// terminal writes keystrokes -> cmd stdin
static void *g_from_cmd = 0;		// cmd stdout -> terminal reads
static void *g_cmd = 0;

static char g_input[256];		// the line being typed (for editing + sending)
static int  g_inlen = 0;

// ---- scrollback / output ----------------------------------------------------

static void ring_push (const char *s)
{
	int idx;
	if (g_rcount < SCROLLBACK) { idx = (g_rfirst + g_rcount) % SCROLLBACK; g_rcount++; }
	else { idx = g_rfirst; g_rfirst = (g_rfirst + 1) % SCROLLBACK; }
	int i = 0;
	for (; s[i] && i < COLS; i++) g_ring[idx][i] = s[i];
	g_ring[idx][i] = '\0';
}
static const char *ring_line (int i) { return g_ring[(g_rfirst + i) % SCROLLBACK]; }
static void flush_cur (void) { g_cur[g_curlen] = '\0'; ring_push (g_cur); g_curlen = 0; g_cur[0] = '\0'; }

// One output byte into the display. Handles \n (newline), \t (tab), \b (erase, for
// local echo), and \f (form-feed = clear, emitted by cmd's `clear` builtin).
static void term_putc (char c)
{
	if (c == '\r') return;
	if (c == '\n') { flush_cur (); return; }
	if (c == '\f') { g_rcount = g_rfirst = g_curlen = g_scroll = 0; g_cur[0] = '\0'; return; }
	if (c == '\b') { if (g_curlen > 0) g_cur[--g_curlen] = '\0'; return; }
	if (c == '\t') { do { if (g_curlen < g_cols) g_cur[g_curlen++] = ' '; } while ((g_curlen % 4) && g_curlen < g_cols); g_cur[g_curlen] = '\0'; return; }
	if (g_curlen >= g_cols) flush_cur ();		// wrap at the visible width
	g_cur[g_curlen++] = c;
	g_cur[g_curlen] = '\0';			// keep g_cur a valid C-string: without this, a
						// shorter new line (e.g. the reprinted prompt) leaves
						// the previous line's tail visible past g_curlen.
}
static void term_puts (const char *s) { for (int i = 0; s[i]; i++) term_putc (s[i]); }

// ---- shell process ----------------------------------------------------------

static void start_cmd (void)
{
	g_to_cmd   = kapi_pipe ();
	g_from_cmd = kapi_pipe ();
	g_cmd = kapi_spawn ("SD:/bin/cmd.elf", "", g_to_cmd, g_from_cmd);
	if (g_cmd == 0) term_puts ("terminal: cannot start /bin/cmd.elf\n");
}

// Pump cmd's stdout into the scrollback. Returns the number of bytes consumed (so the
// caller can repaint only when there was output).
static int drain_cmd (void)
{
	if (g_from_cmd == 0) return 0;
	char b[256]; int n, guard = 0, got = 0;
	while ((n = kapi_stream_read_nb (g_from_cmd, b, sizeof b)) > 0 && guard++ < 64)
	{
		for (int k = 0; k < n; k++) term_putc (b[k]);
		g_scroll = 0;
		got += n;
	}
	return got;
}

// ---- input ------------------------------------------------------------------
// Apply one key to the line editor / scrollback. Shared by the embedded loop (SH_KEY)
// and the standalone window (TermView::onKey).
static void term_key (int key)
{
	switch (key)
	{
	case KEY_ENTER:
		if (g_to_cmd)
		{
			kapi_stream_write (g_to_cmd, g_input, (unsigned) g_inlen);
			kapi_stream_write (g_to_cmd, "\n", 1);
		}
		term_putc ('\n');				// echo the newline locally
		g_inlen = 0; g_input[0] = '\0';
		break;
	case KEY_BACKSPACE:
		if (g_inlen > 0) { g_inlen--; g_input[g_inlen] = '\0'; term_putc ('\b'); }
		break;
	case 3:	/* Ctrl-C */ if (g_to_cmd) kapi_stream_write (g_to_cmd, "\x03", 1); break;
	case 4:	/* Ctrl-D */ if (g_to_cmd) kapi_stream_write (g_to_cmd, "\x04", 1); break;
	case KEY_PGUP: g_scroll += g_vrows - 2; break;
	case KEY_PGDN: g_scroll -= g_vrows - 2; if (g_scroll < 0) g_scroll = 0; break;
	default:
		if (key >= ' ' && key < 0x7f && g_inlen < (int) sizeof g_input - 1)
		{
			g_input[g_inlen++] = (char) key; g_input[g_inlen] = '\0';
			term_putc ((char) key);				// local echo
		}
		break;
	}
}

// ---- view (wtk widget) -------------------------------------------------------
// Renders the scrollback into its Canvas; works for an owned window canvas (standalone)
// AND an adopted shell surface (embedded). Recomputes the visible rows/cols from its
// current logical size each frame, so a viewport resize just reflows the text.
using namespace wtk;

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
		canvas.clear (0x00101418);

		int total = g_rcount + 1;			// + the current line
		int maxscroll = total - g_vrows; if (maxscroll < 0) maxscroll = 0;
		if (g_scroll > maxscroll) g_scroll = maxscroll;
		int first = total - g_vrows - g_scroll; if (first < 0) first = 0;

		for (int r = 0; r < g_vrows; r++)
		{
			int idx = first + r;
			if (idx < 0 || idx >= total) continue;
			const char *line = (idx < g_rcount) ? ring_line (idx) : g_cur;
			canvas.text (4, 4 + r * g_fh, line, 0x00c8d0c0);
			if (idx == g_rcount && g_scroll == 0)		// caret at the end of the live line
			{
				int cx = 4 + g_curlen * g_fw;
				canvas.fillRect (cx, 4 + r * g_fh, 2, g_fh, 0x0060ff90);
			}
		}
	}

	bool onKey (long k) override { term_key ((int) k); invalidate (true); return true; }

	bool onMouse (int, int, int, int, int, int wheel) override	// wheel scrolls the scrollback
	{
		if (wheel) { g_scroll += wheel * 3; if (g_scroll < 0) g_scroll = 0; invalidate (true); }
		return true;
	}
};

// ---- standalone input trampolines (mirror wtk::Root::run's routing) ----------
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
	default: break;
	}
	g_saroot->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, 0);
}

static void sa_key (unsigned long, int ev, long v)
{ if (g_saroot != 0 && ev == GUI_EVENT_KEY) g_saroot->handleKey (v); }

// ---- entry -------------------------------------------------------------------
int main (void)
{
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	// --- embedded in the activity shell (secondary section) -------------
	embed::Host host;
	if (embed::attach (&host, ROLE_SECONDARY, "terminal"))
	{
		TermView view (host.w, host.h);
		view.canvas.adopt (host.pixels, host.w, host.h, host.stride);	// draw into the surface sub-rect
		start_cmd ();
		view.invalidate (true); view.draw ();
		kapi_shell_request (SH_PRESENT, &host.surface_id, sizeof (int));

		bool running = true;
		while (running && !should_exit ())
		{
			int from, type; unsigned char buf[128];
			while (kapi_mailbox_recv (&from, &type, buf, sizeof buf, 0) >= 0)	// non-blocking drain
			{
				switch (type)
				{
				case SH_PTR:
				{
					ShPtr *p = (ShPtr *) buf;
					view.handleMouse (p->x, p->y, p->buttons & 1,
							  (p->buttons >> 1) & 1, (p->buttons >> 2) & 1, p->wheel);
					break;
				}
				case SH_KEY:    term_key (*(int *) buf); view.invalidate (true); break;
				case SH_RESIZE: { ShResize *r = (ShResize *) buf; view.setBounds (r->w, r->h); view.invalidate (true); break; }
				case SH_CLOSE:  running = false; break;
				}
			}
			if (drain_cmd () > 0) view.invalidate (true);		// new cmd output -> repaint
			if (g_cmd && kapi_proc_done (g_cmd)) { kapi_wait (g_cmd); running = false; }	// shell ended
			if (!view.valid)
			{
				view.draw ();
				kapi_shell_request (SH_PRESENT, &host.surface_id, sizeof (int));
			}
			msleep (16);
		}
		if (g_to_cmd)   kapi_stream_close (g_to_cmd);
		if (g_from_cmd) kapi_stream_close (g_from_cmd);
		return 0;
	}

	// --- standalone fallback: our own decorated window ------------------
	Root root (W, H, "terminal");
	if (root.canvas.px == 0) return 1;
	root.setBg (0x00101418);
	TermView *view = new TermView (W, H);
	view->anchor = ANCHOR_FILL;
	root.addChild (view);
	view->setFocus ();			// keys route to the terminal
	g_saroot = &root;
	start_cmd ();

	kapi_set_pointer_handler (sa_ptr);
	kapi_set_key_handler (sa_key);
	while (!should_exit ())
	{
		if (drain_cmd () > 0) view->invalidate (true);	// show cmd's output (incl. the prompt) first,
		pump_events ();			// then echo the keys typed this frame (onKey invalidates)
		if (g_cmd && kapi_proc_done (g_cmd))	// the shell ended (exit / killed):
		{
			kapi_wait (g_cmd);		// the terminal closes with it
			break;
		}
		if (!root.valid) { root.draw (); kapi_present (); }
		msleep (16);
	}
	if (g_to_cmd)   kapi_stream_close (g_to_cmd);
	if (g_from_cmd) kapi_stream_close (g_from_cmd);
	return 0;
}
