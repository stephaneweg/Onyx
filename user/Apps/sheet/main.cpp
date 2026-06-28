//
// sheet -- a mini spreadsheet. Click a cell, type a value or a =formula, Enter to
// commit (arrows also move + commit). Formulas: + - * / ( ) numbers and cell refs
// (A1, B2, ...). Values are fixed-decimal (3 places). Recomputed after each edit.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "applib.h"

#define COLS	8			// A..H
#define ROWS	16
#define CW	62
#define CH	20
#define GX0	30			// grid origin (after row headers)
#define GY0	44			// grid origin (after edit line + col headers)
#define W	(GX0 + COLS * CW + 4)
#define H	(GY0 + ROWS * CH + 4)
#define RAWLEN	24
#define SCALE	1000			// fixed-decimal scale

static unsigned *fb;
static char      g_raw[ROWS][COLS][RAWLEN];
static long long g_val[ROWS][COLS];
static char      g_kind[ROWS][COLS];	// 0 text/empty, 1 number, 2 formula
static int       g_sr = 0, g_sc = 0;	// selected cell
static char      g_eb[RAWLEN]; static int g_eblen = 0;

// ---- expression evaluator (recursive descent over P) ------------------------

static const char *P;
static long long pexpr (void);

static long long pfactor (void)
{
	while (*P == ' ') P++;
	if (*P == '-') { P++; return -pfactor (); }
	if (*P == '(') { P++; long long v = pexpr (); while (*P == ' ') P++; if (*P == ')') P++; return v; }
	if ((*P >= 'A' && *P <= 'H') || (*P >= 'a' && *P <= 'h'))
	{
		int col = (*P >= 'a') ? (*P - 'a') : (*P - 'A'); P++;
		int row = 0, any = 0;
		while (*P >= '0' && *P <= '9') { row = row * 10 + (*P - '0'); P++; any = 1; }
		row--;
		if (any && row >= 0 && row < ROWS && col < COLS) return g_val[row][col];
		return 0;
	}
	long long ip = 0; while (*P >= '0' && *P <= '9') { ip = ip * 10 + (*P - '0'); P++; }
	long long fr = 0, sc = 1;
	if (*P == '.') { P++; while (*P >= '0' && *P <= '9' && sc < SCALE) { fr = fr * 10 + (*P - '0'); sc *= 10; P++; } }
	return ip * SCALE + (fr * SCALE) / sc;
}

static long long pterm (void)
{
	long long v = pfactor ();
	for (;;)
	{
		while (*P == ' ') P++;
		char o = *P; if (o != '*' && o != '/') break; P++;
		long long r = pfactor ();
		if (o == '*') v = (v * r) / SCALE; else v = (r != 0) ? (v * SCALE) / r : 0;
	}
	return v;
}

static long long pexpr (void)
{
	long long v = pterm ();
	for (;;)
	{
		while (*P == ' ') P++;
		char o = *P; if (o != '+' && o != '-') break; P++;
		long long r = pterm ();
		v = (o == '+') ? v + r : v - r;
	}
	return v;
}

// ---- model ------------------------------------------------------------------

static int is_number (const char *s)
{
	int i = 0, dig = 0, dot = 0;
	if (s[i] == '-') i++;
	for (; s[i]; i++)
	{
		if (s[i] >= '0' && s[i] <= '9') dig = 1;
		else if (s[i] == '.' && !dot) dot = 1;
		else return 0;
	}
	return dig;
}

static void recompute (void)
{
	for (int r = 0; r < ROWS; r++)
		for (int c = 0; c < COLS; c++)
		{
			const char *s = g_raw[r][c];
			g_kind[r][c] = (s[0] == '\0') ? 0 : (s[0] == '=') ? 2 : is_number (s) ? 1 : 0;
		}
	for (int pass = 0; pass < 8; pass++)		// resolve dependency chains
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < COLS; c++)
			{
				if (g_kind[r][c] == 2) { P = &g_raw[r][c][1]; g_val[r][c] = pexpr (); }
				else if (g_kind[r][c] == 1) { P = g_raw[r][c]; g_val[r][c] = pexpr (); }
				else g_val[r][c] = 0;
			}
}

static int fmt_val (long long v, char *b)
{
	int p = 0; long long a = v;
	if (a < 0) { b[p++] = '-'; a = -a; }
	p += ax_itoa ((int) (a / SCALE), b + p);
	int fr = (int) (a % SCALE);
	if (fr)
	{
		b[p++] = '.';
		char f[3] = { (char) ('0' + (fr / 100) % 10), (char) ('0' + (fr / 10) % 10), (char) ('0' + fr % 10) };
		int n = 3; while (n > 0 && f[n - 1] == '0') n--;
		for (int i = 0; i < n; i++) b[p++] = f[i];
	}
	b[p] = '\0';
	return p;
}

static void load_edit (void)
{
	g_eblen = 0;
	const char *s = g_raw[g_sr][g_sc];
	for (int i = 0; s[i] && g_eblen < RAWLEN - 1; i++) g_eb[g_eblen++] = s[i];
	g_eb[g_eblen] = '\0';
}

static void commit (void)
{
	int i = 0; for (; i < g_eblen && i < RAWLEN - 1; i++) g_raw[g_sr][g_sc][i] = g_eb[i];
	g_raw[g_sr][g_sc][i] = '\0';
	recompute ();
}

static void select_cell (int r, int c)
{
	commit ();
	if (r < 0) r = 0;
	else if (r >= ROWS) r = ROWS - 1;
	if (c < 0) c = 0;
	else if (c >= COLS) c = COLS - 1;
	g_sr = r; g_sc = c;
	load_edit ();
}

// ---- input ------------------------------------------------------------------

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	switch (key)
	{
	case KEY_ENTER: select_cell (g_sr + 1, g_sc); break;
	case KEY_UP:    select_cell (g_sr - 1, g_sc); break;
	case KEY_DOWN:  select_cell (g_sr + 1, g_sc); break;
	case KEY_LEFT:  select_cell (g_sr, g_sc - 1); break;
	case KEY_RIGHT: select_cell (g_sr, g_sc + 1); break;
	case KEY_BACKSPACE: if (g_eblen > 0) g_eb[--g_eblen] = '\0'; break;
	default:
		if (key >= ' ' && key < 0x7f && g_eblen < RAWLEN - 1)
		{ g_eb[g_eblen++] = (char) key; g_eb[g_eblen] = '\0'; }
		break;
	}
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK) return;
	int x = (int) ((val >> 16) & 0xFFFF), y = (int) (val & 0xFFFF);
	int c = (x - GX0) / CW, r = (y - GY0) / CH;
	if (x >= GX0 && y >= GY0 && c >= 0 && c < COLS && r >= 0 && r < ROWS) select_cell (r, c);
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
	fill_rect (0, 0, W, H, 0x00202830);

	// Edit line: cell ref + current edit buffer.
	char ref[8]; ref[0] = (char) ('A' + g_sc); int rp = 1 + ax_itoa (g_sr + 1, ref + 1);
	ref[rp++] = ':'; ref[rp] = '\0';
	fill_rect (0, 0, W, 20, 0x00303d4d);
	wtk::draw_text (fb, W, H, 4, 3, ref, 0x00ffd070);
	wtk::draw_text (fb, W, H, 4 + rp * kapi_font_width () + 4, 3, g_eb, 0x00ffffff);
	int cx = 4 + rp * kapi_font_width () + 4 + g_eblen * kapi_font_width ();
	fill_rect (cx, 3, 2, kapi_font_height (), 0x0060ff90);

	// Column headers.
	for (int c = 0; c < COLS; c++)
	{
		char h[2] = { (char) ('A' + c), 0 };
		wtk::draw_text (fb, W, H, GX0 + c * CW + CW / 2 - 3, 26, h, 0x0090b0d0);
	}
	// Row headers + cells.
	for (int r = 0; r < ROWS; r++)
	{
		char rh[4]; ax_itoa (r + 1, rh);
		wtk::draw_text (fb, W, H, 4, GY0 + r * CH + 3, rh, 0x0090b0d0);
		for (int c = 0; c < COLS; c++)
		{
			int x = GX0 + c * CW, y = GY0 + r * CH;
			unsigned bg = (r == g_sr && c == g_sc) ? 0x00355070 : 0x00283440;
			fill_rect (x, y, CW - 1, CH - 1, bg);
			char out[24];
			if (g_kind[r][c] == 1 || g_kind[r][c] == 2)
			{
				int n = fmt_val (g_val[r][c], out);
				wtk::draw_text (fb, W, H, x + CW - 4 - n * kapi_font_width (), y + 3, out, 0x00e8e8e8);
			}
			else if (g_raw[r][c][0])
				wtk::draw_text (fb, W, H, x + 3, y + 3, g_raw[r][c], 0x00d0d0c0);
		}
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "sheet");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();
	for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) g_raw[r][c][0] = '\0';
	recompute ();
	load_edit ();
	kapi_set_key_handler (on_key);
	kapi_set_click_handler (on_click);
	while (!should_exit ()) { pump_events (); redraw (); msleep (16); }
	return 0;
}
