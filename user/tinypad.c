//
// tinypad.c -- a small text editor, inspired by MenuetOS's TinyPad. Unlike the demos
// it manages its own text buffer and draws it: text is rendered via kapi_draw_text
// (apps have no font of their own) and keystrokes arrive through a key handler
// (kapi_set_key_handler) as GUI_EVENT_KEY events. A toolbar (filename textbox +
// Open/Save buttons) sits on top; the editing area fills the rest.
//
// Model: an array of fixed-capacity lines. The cursor (cx,cy) is a column within a
// line and a line index; the view scrolls to keep it visible. Click the text area
// to edit the document; click the filename box to edit the path.
//
#include "kapi.h"

#define W		560
#define H		430
#define TOOLBAR_H	28
#define MAXLINES	200
#define MAXCOL		128

#define COL_TOOLBAR	0x00303840
#define COL_PAPER	0x00ffffff
#define COL_TEXT	0x00101010
#define COL_CARET	0x000060ff

static unsigned *fb;

static char g_text[MAXLINES][MAXCOL + 1];
static int  g_nlines = 1;
static int  g_cx = 0, g_cy = 0;		// cursor: column, line
static int  g_top = 0, g_left = 0;	// first visible line / column
static int  g_dirty = 1;

static int  g_fw = 8, g_fh = 16;	// font metrics (queried at startup)
static int  g_visrows, g_viscols;
static int  g_textX = 4, g_textY = TOOLBAR_H + 2;

static unsigned long g_fnbox = 0;	// filename textbox

// ---- small helpers ----------------------------------------------------------

static int slen (const char *s)
{
	int n = 0;
	while (s[n] != '\0') n++;
	return n;
}

static void lcpy (char *dst, const char *src)
{
	int i = 0;
	while (src[i] != '\0' && i < MAXCOL) { dst[i] = src[i]; i++; }
	dst[i] = '\0';
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
	{
		if (yy < 0) continue;
		for (int xx = x; xx < x + w && xx < W; xx++)
		{
			if (xx >= 0) fb[yy * W + xx] = c;
		}
	}
}

// ---- editing ----------------------------------------------------------------

static void insert_line (int at)		// open a blank line at index `at`
{
	if (g_nlines >= MAXLINES) return;
	for (int j = g_nlines; j > at; j--) lcpy (g_text[j], g_text[j - 1]);
	g_text[at][0] = '\0';
	g_nlines++;
}

static void delete_line (int at)
{
	if (g_nlines <= 1) { g_text[0][0] = '\0'; return; }
	for (int j = at; j < g_nlines - 1; j++) lcpy (g_text[j], g_text[j + 1]);
	g_nlines--;
}

static void insert_char (char c)
{
	char *l = g_text[g_cy];
	int len = slen (l);
	if (len >= MAXCOL) return;
	for (int i = len; i > g_cx; i--) l[i] = l[i - 1];
	l[g_cx] = c;
	l[len + 1] = '\0';
	g_cx++;
}

static void do_enter (void)
{
	char *l = g_text[g_cy];
	insert_line (g_cy + 1);
	lcpy (g_text[g_cy + 1], &l[g_cx]);	// tail moves to the new line
	l[g_cx] = '\0';
	g_cy++;
	g_cx = 0;
}

static void do_backspace (void)
{
	if (g_cx > 0)
	{
		char *l = g_text[g_cy];
		int len = slen (l);
		for (int i = g_cx - 1; i < len; i++) l[i] = l[i + 1];
		g_cx--;
	}
	else if (g_cy > 0)
	{
		int plen = slen (g_text[g_cy - 1]);
		char *cur = g_text[g_cy];
		if (plen + slen (cur) <= MAXCOL)
		{
			int i = 0;
			while (cur[i] != '\0') { g_text[g_cy - 1][plen + i] = cur[i]; i++; }
			g_text[g_cy - 1][plen + i] = '\0';
		}
		delete_line (g_cy);
		g_cy--;
		g_cx = plen;
	}
}

static void do_delete (void)
{
	char *l = g_text[g_cy];
	int len = slen (l);
	if (g_cx < len)
	{
		for (int i = g_cx; i < len; i++) l[i] = l[i + 1];
	}
	else if (g_cy < g_nlines - 1)
	{
		char *nxt = g_text[g_cy + 1];
		if (len + slen (nxt) <= MAXCOL)
		{
			int i = 0;
			while (nxt[i] != '\0') { l[len + i] = nxt[i]; i++; }
			l[len + i] = '\0';
		}
		delete_line (g_cy + 1);
	}
}

static void clamp_cursor (void)
{
	if (g_cy < 0) g_cy = 0;
	if (g_cy >= g_nlines) g_cy = g_nlines - 1;
	int len = slen (g_text[g_cy]);
	if (g_cx < 0) g_cx = 0;
	if (g_cx > len) g_cx = len;
}

// ---- load / save ------------------------------------------------------------

static void get_filename (char *buf)
{
	int n = kapi_widget_get_text (g_fnbox, buf, 96);
	buf[n] = '\0';
}

static void load_file (void)
{
	char path[100];
	get_filename (path);
	if (path[0] == '\0') return;

	void *f = kapi_open (path);
	g_nlines = 1; g_cx = 0; g_cy = 0; g_top = 0; g_left = 0;
	g_text[0][0] = '\0';
	if (f == 0) { g_dirty = 1; return; }

	static char buf[MAXLINES * (MAXCOL + 1)];
	int n = kapi_read (f, buf, sizeof (buf) - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	buf[n] = '\0';

	int li = 0, col = 0;
	g_text[0][0] = '\0';
	for (int i = 0; i < n && li < MAXLINES; i++)
	{
		char c = buf[i];
		if (c == '\r') continue;
		if (c == '\n')
		{
			g_text[li][col] = '\0';
			li++; col = 0;
			if (li < MAXLINES) g_text[li][0] = '\0';
			continue;
		}
		if (c == '\t') c = ' ';
		if (col < MAXCOL) g_text[li][col++] = c;
	}
	if (li < MAXLINES) g_text[li][col] = '\0';
	g_nlines = li + 1;
	if (g_nlines < 1) g_nlines = 1;
	g_dirty = 1;
}

static void save_file (void)
{
	char path[100];
	get_filename (path);
	if (path[0] == '\0') return;

	static char buf[MAXLINES * (MAXCOL + 1)];
	int p = 0;
	for (int i = 0; i < g_nlines; i++)
	{
		char *l = g_text[i];
		int j = 0;
		while (l[j] != '\0') buf[p++] = l[j++];
		buf[p++] = '\n';
	}
	kapi_save_file (path, buf, p);
}

// ---- handlers ---------------------------------------------------------------

static void on_open (unsigned long s, int e, long v) { (void) s; (void) e; (void) v; load_file (); }
static void on_save (unsigned long s, int e, long v) { (void) s; (void) e; (void) v; save_file (); }

static void on_key (unsigned long sender, int ev, long key)
{
	(void) sender;
	if (ev != GUI_EVENT_KEY) return;

	switch (key)
	{
	case KEY_LEFT:
		if (g_cx > 0) g_cx--;
		else if (g_cy > 0) { g_cy--; g_cx = slen (g_text[g_cy]); }
		break;
	case KEY_RIGHT:
		if (g_cx < slen (g_text[g_cy])) g_cx++;
		else if (g_cy < g_nlines - 1) { g_cy++; g_cx = 0; }
		break;
	case KEY_UP:    if (g_cy > 0) g_cy--; break;
	case KEY_DOWN:  if (g_cy < g_nlines - 1) g_cy++; break;
	case KEY_HOME:  g_cx = 0; break;
	case KEY_END:   g_cx = slen (g_text[g_cy]); break;
	case KEY_PGUP:  g_cy -= g_visrows; if (g_cy < 0) g_cy = 0; break;
	case KEY_PGDN:  g_cy += g_visrows; if (g_cy > g_nlines - 1) g_cy = g_nlines - 1; break;
	case KEY_ENTER: do_enter (); break;
	case KEY_BACKSPACE: do_backspace (); break;
	case KEY_DEL:   do_delete (); break;
	case KEY_TAB:   insert_char (' '); insert_char (' '); break;
	default:
		if (key >= ' ' && key < 0x7f) insert_char ((char) key);
		break;
	}
	clamp_cursor ();
	g_dirty = 1;
}

// ---- drawing ----------------------------------------------------------------

static void redraw (void)
{
	// Keep the cursor in view.
	if (g_cy < g_top) g_top = g_cy;
	if (g_cy >= g_top + g_visrows) g_top = g_cy - g_visrows + 1;
	if (g_cx < g_left) g_left = g_cx;
	if (g_cx >= g_left + g_viscols) g_left = g_cx - g_viscols + 1;
	if (g_top < 0) g_top = 0;
	if (g_left < 0) g_left = 0;

	fill_rect (0, 0, W, TOOLBAR_H, COL_TOOLBAR);
	fill_rect (0, TOOLBAR_H, W, H - TOOLBAR_H, COL_PAPER);

	for (int r = 0; r < g_visrows; r++)
	{
		int li = g_top + r;
		if (li >= g_nlines) break;
		char *l = g_text[li];
		int len = slen (l);

		char vis[MAXCOL + 1];
		int j = 0;
		for (int c = g_left; c < len && j < g_viscols && j < MAXCOL; c++) vis[j++] = l[c];
		vis[j] = '\0';
		if (j > 0) kapi_draw_text (g_textX, g_textY + r * g_fh, vis, COL_TEXT);
	}

	// Caret.
	int cxp = g_textX + (g_cx - g_left) * g_fw;
	int cyp = g_textY + (g_cy - g_top) * g_fh;
	fill_rect (cxp, cyp, 2, g_fh, COL_CARET);
}

int main (void)
{
	fb = kapi_create_window (W, H, "tinypad");
	if (fb == 0) return 1;

	g_fw = kapi_font_width ();
	g_fh = kapi_font_height ();
	if (g_fw < 1) g_fw = 8;
	if (g_fh < 1) g_fh = 16;
	g_visrows = (H - g_textY - 2) / g_fh;
	g_viscols = (W - g_textX - 2) / g_fw;
	if (g_visrows < 1) g_visrows = 1;
	if (g_viscols < 1) g_viscols = 1;

	// Toolbar: filename box + Open/Save.
	g_fnbox = kapi_add_textbox (4, 4, W - 160, 20, 0);
	kapi_widget_set_text (g_fnbox, "SD:notes.txt");
	kapi_add_button (W - 152, 4, 70, 20, "Open", on_open);
	kapi_add_button (W - 78,  4, 70, 20, "Save", on_save);

	kapi_set_key_handler (on_key);

	g_text[0][0] = '\0';

	while (!should_exit ())
	{
		pump_events ();
		if (g_dirty)
		{
			redraw ();
			g_dirty = 0;
		}
		msleep (16);
	}
	return 0;
}
