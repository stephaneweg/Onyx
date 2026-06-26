//
// terminal.c -- a windowed "dumb" terminal (tty). It owns the window, keyboard and
// scrollback only; the actual shell is a separate program, /bin/cmd.elf. At startup
// the terminal spawns cmd wired to two pipes: keystrokes go to cmd's stdin, and
// cmd's stdout is drained into the scrollback. So all command parsing / pipelines /
// builtins live in cmd, not here. The terminal does local line editing + echo
// (Backspace edits, Enter sends the line, Ctrl-D sends EOF); cmd prints the prompt.
//
#include "kapi.h"
#include "uikit.hpp"
#include "applib.h"

#define W		620
#define H		400
#define SCROLLBACK	200		// bounded scrollback (rows)
#define COLS		112		// stored chars per line

static unsigned *fb;
static int g_fw = 8, g_fh = 16, g_vrows = 1, g_cols = COLS;	// g_cols = columns that fit the window

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
	if (g_curlen >= g_cols) flush_cur ();		// wrap at the visible width (was COLS, off-screen)
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

static void drain_cmd (void)		// pump cmd's stdout into the scrollback
{
	if (g_from_cmd == 0) return;
	char b[256]; int n, guard = 0;
	while ((n = kapi_stream_read_nb (g_from_cmd, b, sizeof b)) > 0 && guard++ < 64)
	{
		for (int k = 0; k < n; k++) term_putc (b[k]);
		g_scroll = 0;
	}
}

// ---- input ------------------------------------------------------------------

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
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

// ---- drawing ----------------------------------------------------------------

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00101418);

	int total = g_rcount + 1;			// + the current line
	int maxscroll = total - g_vrows; if (maxscroll < 0) maxscroll = 0;
	if (g_scroll > maxscroll) g_scroll = maxscroll;
	int first = total - g_vrows - g_scroll; if (first < 0) first = 0;

	for (int r = 0; r < g_vrows; r++)
	{
		int idx = first + r;
		if (idx < 0 || idx >= total) continue;
		const char *line = (idx < g_rcount) ? ring_line (idx) : g_cur;
		kapi_draw_text (4, 4 + r * g_fh, line, 0x00c8d0c0);
		if (idx == g_rcount && g_scroll == 0)		// caret at the end of the live line
		{
			int cx = 4 + g_curlen * g_fw;
			fill_rect (cx, 4 + r * g_fh, 2, g_fh, 0x0060ff90);
		}
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "terminal");
	if (fb == 0) return 1;
	ui::decorate_window ();
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;
	g_vrows = (H - 8) / g_fh; if (g_vrows < 2) g_vrows = 2;
	g_cols = (W - 8) / g_fw; if (g_cols < 8) g_cols = 8; if (g_cols > COLS) g_cols = COLS;	// wrap to window width

	kapi_set_key_handler (on_key);
	start_cmd ();

	while (!should_exit ())
	{
		drain_cmd ();			// show cmd's output (incl. the prompt) first,
		pump_events ();			// then echo the keys typed this frame after it
		if (g_cmd && kapi_proc_done (g_cmd))		// the shell ended (exit / killed):
		{
			kapi_wait (g_cmd);			// the terminal closes with it
			break;
		}
		redraw ();
		msleep (16);
	}
	if (g_to_cmd)   kapi_stream_close (g_to_cmd);
	if (g_from_cmd) kapi_stream_close (g_from_cmd);
	return 0;
}
