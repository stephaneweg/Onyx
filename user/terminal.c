//
// terminal.c -- a windowed text terminal. Parses a command line into a pipeline of
// stages (split on '|') with optional redirections (< file, > file, >> file), wires
// each stage's stdin/stdout to pipes/files, spawns /bin/<cmd>.elf for each, and
// drains the last stage's stdout into a bounded scrollback. Keyboard feeds the first
// stage's stdin (Enter sends a line, Ctrl-D ends it). PgUp/PgDn scroll.
//
#include "kapi.h"
#include "applib.h"

#define W		620
#define H		400
#define SCROLLBACK	100		// bounded scrollback (rows)
#define COLS		112		// stored chars per line
#define MAXSTAGES	6
#define MAXOWNED	(MAXSTAGES * 2 + 2)

static unsigned *fb;
static int g_fw = 8, g_fh = 16, g_vrows = 1, g_vcols = 1;

// Scrollback ring of finalized lines + the line currently being built from output.
static char g_ring[SCROLLBACK][COLS + 1];
static int  g_rfirst = 0, g_rcount = 0;
static char g_cur[COLS + 1];
static int  g_curlen = 0;
static int  g_scroll = 0;		// 0 = bottom

static char g_input[256];
static int  g_inlen = 0;

static int   g_running = 0;
static void *g_proc[MAXSTAGES]; static int g_nproc = 0;
static void *g_owned[MAXOWNED]; static int g_nowned = 0;
static void *g_in = 0;			// first stage stdin pipe (terminal writes), or 0
static void *g_out = 0;			// last stage stdout pipe (terminal reads), or 0

struct Stage { char cmd[64]; char args[160]; char infile[96]; char outfile[96]; int append; };
static struct Stage g_stage[MAXSTAGES];

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

static void term_putc (char c)
{
	if (c == '\r') return;
	if (c == '\n') { flush_cur (); return; }
	if (c == '\t') { do { if (g_curlen < COLS) g_cur[g_curlen++] = ' '; } while ((g_curlen % 4) && g_curlen < COLS); return; }
	if (g_curlen >= COLS) flush_cur ();
	g_cur[g_curlen++] = c;
}
static void term_puts (const char *s) { for (int i = 0; s[i]; i++) term_putc (s[i]); }

// ---- parsing ----------------------------------------------------------------

static void scopy (char *d, const char *s, int cap)
{ int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

// Resolve a redirection filename to a FatFs path (prefix SD:/ if no volume given).
static void resolve (char *dst, int cap, const char *name)
{
	int has = 0; for (int i = 0; name[i]; i++) if (name[i] == ':') { has = 1; break; }
	int p = 0;
	if (!has) { const char *r = "SD:/"; for (int i = 0; r[i] && p < cap - 1; i++) dst[p++] = r[i]; }
	for (int i = 0; name[i] && p < cap - 1; i++) dst[p++] = name[i];
	dst[p] = '\0';
}

static int read_word (const char *s, int i, char *dst, int cap)
{
	int t = 0;
	while (s[i] && s[i] != ' ' && s[i] != '<' && s[i] != '>' && t < cap - 1) dst[t++] = s[i++];
	dst[t] = '\0';
	return i;
}

static void parse_stage (const char *s, struct Stage *st)
{
	st->cmd[0] = st->args[0] = st->infile[0] = st->outfile[0] = '\0';
	st->append = 0;
	int i = 0;
	while (s[i])
	{
		while (s[i] == ' ') i++;
		if (!s[i]) break;
		if (s[i] == '<') { i++; while (s[i] == ' ') i++; i = read_word (s, i, st->infile, sizeof st->infile); continue; }
		if (s[i] == '>')
		{
			i++; if (s[i] == '>') { st->append = 1; i++; }
			while (s[i] == ' ') i++; i = read_word (s, i, st->outfile, sizeof st->outfile); continue;
		}
		char tok[96];
		i = read_word (s, i, tok, sizeof tok);
		if (tok[0] == '\0') continue;
		if (st->cmd[0] == '\0') scopy (st->cmd, tok, sizeof st->cmd);
		else
		{
			int n = ax_strlen (st->args);
			if (n > 0 && n < (int) sizeof st->args - 1) st->args[n++] = ' ';
			scopy (st->args + n, tok, (int) sizeof st->args - n);
		}
	}
}

// ---- pipeline execution -----------------------------------------------------

static void own (void *s) { if (s && g_nowned < MAXOWNED) g_owned[g_nowned++] = s; }

static void cleanup (void)		// drain remaining output, reap procs, close streams
{
	if (g_in) kapi_stream_eof (g_in);		// any stdin reader now sees EOF
	if (g_out)
	{
		char b[256]; int n;
		while ((n = kapi_stream_read_nb (g_out, b, sizeof b)) > 0)
			for (int k = 0; k < n; k++) term_putc (b[k]);
	}
	for (int i = 0; i < g_nproc; i++) kapi_wait (g_proc[i]);
	for (int i = 0; i < g_nowned; i++) kapi_stream_close (g_owned[i]);
	if (g_curlen > 0) flush_cur ();
	g_nproc = g_nowned = 0; g_in = g_out = 0; g_running = 0; g_scroll = 0;
}

static void run (void)
{
	// Split the input into stages on '|'.
	int ns = 0, i = 0;
	while (g_input[i] && ns < MAXSTAGES)
	{
		char sub[256]; int s = 0;
		while (g_input[i] && g_input[i] != '|' && s < (int) sizeof sub - 1) sub[s++] = g_input[i++];
		sub[s] = '\0';
		if (g_input[i] == '|') i++;
		parse_stage (sub, &g_stage[ns++]);
	}
	if (ns == 0 || g_stage[0].cmd[0] == '\0') return;

	if (ns == 1 && ax_streq (g_stage[0].cmd, "clear"))
	{ g_rcount = g_rfirst = g_curlen = g_scroll = 0; g_cur[0] = '\0'; return; }

	g_nproc = g_nowned = 0; g_in = g_out = 0;
	void *prev = 0; int failed = 0;
	for (int s = 0; s < ns; s++)
	{
		if (g_stage[s].cmd[0] == '\0') { term_puts ("syntax error\n"); failed = 1; break; }

		void *sin, *sout;
		if (s == 0)
		{
			if (g_stage[0].infile[0])
			{
				char path[112]; resolve (path, sizeof path, g_stage[0].infile);
				sin = kapi_file_in (path);
				if (!sin) { term_puts ("cannot open input file\n"); failed = 1; break; }
				own (sin);
			}
			else { g_in = kapi_pipe (); sin = g_in; own (g_in); }
		}
		else sin = prev;

		if (s == ns - 1)
		{
			if (g_stage[s].outfile[0])
			{
				char path[112]; resolve (path, sizeof path, g_stage[s].outfile);
				sout = kapi_file_out (path, g_stage[s].append);
				if (!sout) { term_puts ("cannot open output file\n"); failed = 1; break; }
				own (sout); g_out = 0;
			}
			else { g_out = kapi_pipe (); sout = g_out; own (g_out); }
		}
		else { sout = kapi_pipe (); own (sout); prev = sout; }

		char bin[160]; int p = 0;
		const char *pre = "SD:/bin/";
		for (int k = 0; pre[k]; k++) bin[p++] = pre[k];
		for (int k = 0; g_stage[s].cmd[k] && p < (int) sizeof bin - 6; k++) bin[p++] = g_stage[s].cmd[k];
		const char *suf = ".elf"; for (int k = 0; suf[k]; k++) bin[p++] = suf[k]; bin[p] = '\0';

		void *proc = kapi_spawn (bin, g_stage[s].args, sin, sout);
		if (!proc) { term_puts (g_stage[s].cmd); term_puts (": command not found\n"); failed = 1; break; }
		g_proc[g_nproc++] = proc;
	}

	if (failed) { cleanup (); return; }
	g_running = 1;
}

static void run_step (void)
{
	if (g_out)
	{
		char b[256]; int n, guard = 0;
		while ((n = kapi_stream_read_nb (g_out, b, sizeof b)) > 0 && guard++ < 64)
		{
			for (int k = 0; k < n; k++) term_putc (b[k]);
			g_scroll = 0;
		}
	}
	int alldone = 1;
	for (int i = 0; i < g_nproc; i++) if (!kapi_proc_done (g_proc[i])) alldone = 0;
	if (alldone) cleanup ();
}

// ---- input ------------------------------------------------------------------

static void submit_line (void)
{
	// Echo the typed line into the scrollback, then either run it (prompt) or feed
	// it to the running pipeline's stdin.
	if (g_running)
	{
		term_puts (g_input); term_putc ('\n');
		if (g_in) { kapi_stream_write (g_in, g_input, (unsigned) g_inlen); kapi_stream_write (g_in, "\n", 1); }
		g_inlen = 0; g_input[0] = '\0';
	}
	else
	{
		char line[260]; int p = 0;
		line[p++] = '$'; line[p++] = ' ';
		for (int i = 0; g_input[i] && p < (int) sizeof line - 1; i++) line[p++] = g_input[i];
		line[p] = '\0';
		term_puts (line); term_putc ('\n');
		run ();
		g_inlen = 0; g_input[0] = '\0';
	}
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	switch (key)
	{
	case KEY_ENTER: submit_line (); break;
	case KEY_BACKSPACE: if (g_inlen > 0) g_input[--g_inlen] = '\0'; break;
	case KEY_PGUP: g_scroll += g_vrows - 2; break;
	case KEY_PGDN: g_scroll -= g_vrows - 2; if (g_scroll < 0) g_scroll = 0; break;
	case 4: /* Ctrl-D */ if (g_running && g_in) kapi_stream_eof (g_in); break;
	default:
		if (key >= ' ' && key < 0x7f && g_inlen < (int) sizeof g_input - 1)
		{ g_input[g_inlen++] = (char) key; g_input[g_inlen] = '\0'; }
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

	int total = g_rcount + (g_curlen > 0 ? 1 : 0);
	int content_rows = g_vrows - 1;			// last row = input line
	int maxscroll = total - content_rows; if (maxscroll < 0) maxscroll = 0;
	if (g_scroll > maxscroll) g_scroll = maxscroll;
	int first = total - content_rows - g_scroll; if (first < 0) first = 0;

	for (int r = 0; r < content_rows; r++)
	{
		int idx = first + r;
		if (idx < 0 || idx >= total) continue;
		const char *line = (idx < g_rcount) ? ring_line (idx) : g_cur;
		kapi_draw_text (4, 4 + r * g_fh, line, 0x00c8d0c0);
	}

	// Input line at the bottom.
	int y = 4 + content_rows * g_fh;
	char ln[300]; int p = 0;
	if (!g_running) { ln[p++] = '$'; ln[p++] = ' '; }
	for (int i = 0; g_input[i] && p < (int) sizeof ln - 1; i++) ln[p++] = g_input[i];
	ln[p] = '\0';
	fill_rect (0, y, W, g_fh, 0x00182028);
	kapi_draw_text (4, y, ln, 0x00ffffff);
	int cx = 4 + p * g_fw;				// caret
	fill_rect (cx, y, 2, g_fh, 0x0060ff90);
}

int main (void)
{
	fb = kapi_create_window (W, H, "terminal");
	if (fb == 0) return 1;
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;
	g_vrows = (H - 8) / g_fh; if (g_vrows < 2) g_vrows = 2;
	g_vcols = (W - 8) / g_fw; if (g_vcols < 1) g_vcols = 1;

	kapi_set_key_handler (on_key);
	term_puts ("Zircon terminal -- try: ls /bin | grep e\n");

	while (!should_exit ())
	{
		pump_events ();
		if (g_running) run_step ();
		redraw ();
		msleep (16);
	}
	return 0;
}
