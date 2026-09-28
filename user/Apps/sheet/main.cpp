//
// sheet -- a mini spreadsheet. Click a cell, type a value or a =formula, Enter to
// commit (arrows also move + commit). Formulas: + - * / ( ) numbers and cell refs
// (A1, B2, ...). Values are fixed-decimal (3 places). Recomputed after each edit.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "applib.h"

using namespace wtk;

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

// A cell's text, clipped to the cell (a view of the window's pixels over its box).
static void cell_text (int x, int y, int w, int h, int tx, const char *s, unsigned c)
{
	Canvas cc; cc.adopt (fb + y * W + x, w, h, W);
	cc.text (tx, (h - kapi_font_height ()) / 2 + 1, s, c);
}

// A header cell (a column's letter, a row's number): raised; the selected cell's row and column in
// the accent's tint.
static void header_cell (Canvas &cv, int x, int y, int w, int h, const char *s, bool cur, int r, int corners)
{
	unsigned f = cur ? wk_mix (C_FACE, C_ACCENT, 72) : C_FACE;
	wk_rbox (cv, x, y, w, h, r, wk_tone (f, 166), wk_tone (f, 128), 255, corners);
	wk_text_c (cv, x, y, w, h, s, C_TEXT, cur ? 2 : 0);
}

// The theme's look: the edit line a field; the table a field, its column and row headers raised,
// the selected cell in the accent.
static void redraw (void)
{
	Canvas cv; cv.adopt (fb, W, H);
	int fw = kapi_font_width (), fh = kapi_font_height ();
	cv.clear (C_BG);

	// Edit line: cell ref + a field with the current edit buffer.
	char ref[8]; ref[0] = (char) ('A' + g_sc); int rp = 1 + ax_itoa (g_sr + 1, ref + 1);
	ref[rp++] = ':'; ref[rp] = '\0';
	wk_text_l (cv, 4, 1, 20, ref, C_TEXT, 2);
	int ex = 4 + rp * fw + 4;
	wk_sunken (cv, ex, 1, W - ex - 3, 20, 4, C_FIELD, true);
	cv.text (ex + 4, 3, g_eb, C_FIELD_TEXT);
	cv.fillRect (ex + 4 + g_eblen * fw, 3, 2, fh, C_ACCENT);

	// The table: a field; the headers along its top and its left, the cells' grid.
	int tx = 2, ty = 23, tw = GX0 + COLS * CW - tx + 1, th = GY0 + ROWS * CH - ty + 1;
	unsigned grid = wk_mix (C_FIELD, C_FIELD_TEXT, 36), sep = wk_tone (C_FACE, 96);
	wk_rbox (cv, tx, ty, tw, th, 4, C_FIELD, C_FIELD);
	header_cell (cv, tx + 1, ty + 1, GX0 - tx - 2, GY0 - ty - 2, "", false, 3, WK_TL);	// the corner
	for (int c = 0; c < COLS; c++)
	{
		char h[2] = { (char) ('A' + c), 0 };
		int x = GX0 + c * CW, w = c < COLS - 1 ? CW - 1 : tw - (x - tx) - 1;
		header_cell (cv, x, ty + 1, w, GY0 - ty - 2, h, c == g_sc, 3, c < COLS - 1 ? 0 : WK_TR);
		if (c < COLS - 1) cv.fillRect (x + CW - 1, ty + 1, 1, GY0 - ty - 2, sep);
	}
	for (int r = 0; r < ROWS; r++)
	{
		char rh[4]; ax_itoa (r + 1, rh);
		int y = GY0 + r * CH, h = r < ROWS - 1 ? CH - 1 : th - (y - ty) - 1;
		header_cell (cv, tx + 1, y, GX0 - tx - 2, h, rh, r == g_sr, 3, r < ROWS - 1 ? 0 : WK_BL);
		if (r < ROWS - 1) cv.fillRect (tx + 1, y + CH - 1, GX0 - tx - 2, 1, sep);
	}
	cv.fillRect (tx + 1, GY0 - 1, tw - 2, 1, wk_tone (C_FACE, 80));	// the headers' edges
	cv.fillRect (GX0 - 1, ty + 1, 1, th - 2, wk_tone (C_FACE, 80));
	// Cells.
	for (int r = 0; r < ROWS; r++)
		for (int c = 0; c < COLS; c++)
		{
			int x = GX0 + c * CW, y = GY0 + r * CH;
			bool sel = r == g_sr && c == g_sc;
			if (c < COLS - 1) cv.fillRect (x + CW - 1, y, 1, CH, grid);
			if (r < ROWS - 1) cv.fillRect (x, y + CH - 1, CW, 1, grid);
			if (sel) wk_hilite (cv, x, y, CW - 1, CH - 1, 3, true);
			unsigned ink = sel ? wk_hilite_ink (true) : C_FIELD_TEXT;
			char out[24];
			if (g_kind[r][c] == 1 || g_kind[r][c] == 2)
			{
				int n = fmt_val (g_val[r][c], out);
				cell_text (x, y, CW - 1, CH - 1, CW - 5 - n * fw, out, ink);
			}
			else if (g_raw[r][c][0])
				cell_text (x, y, CW - 1, CH - 1, 3, g_raw[r][c], ink);
		}
	wk_rline (cv, tx, ty, tw, th, 4, wk_tone (C_FACE, 72), 210);
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
