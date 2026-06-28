//
// sokoban -- push every box ($) onto a target (.). Arrows move, r resets the level,
// n next level. Levels load from SD:/apps/sokoban.app/levels.txt (blank-line
// separated; #=wall .=target $=box @=player *=box-on-target +=player-on-target),
// with an embedded fallback.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "applib.h"

#define GW	24
#define GH	18
#define CELL	22
#define OX	8
#define OY	26
#define W	(OX * 2 + GW * CELL)
#define H	(OY + GH * CELL + 8)

static unsigned *fb;
static char  g_buf[4096];
static int   g_lvloff[32], g_nlvl = 0, g_cur = 0;
static char  g_wall[GH][GW], g_box[GH][GW], g_target[GH][GW];
static int   g_px, g_py, g_won = 0;

static const char *EMBED =
	"#######\n# .   #\n# $@  #\n#  $ .#\n#     #\n#######\n\n"
	"########\n#  .   #\n# #$## #\n# @ $. #\n#      #\n########\n";

static int is_ws (char c) { return c == ' ' || c == '\t' || c == '\r'; }

static void find_levels (void)
{
	g_nlvl = 0;
	int i = 0, started = 0;
	while (g_buf[i])
	{
		int ls = i; while (g_buf[i] && g_buf[i] != '\n') i++;
		int le = i; if (g_buf[i] == '\n') i++;
		int blank = 1; for (int k = ls; k < le; k++) if (!is_ws (g_buf[k])) { blank = 0; break; }
		if (!blank && !started) { if (g_nlvl < 32) g_lvloff[g_nlvl++] = ls; started = 1; }
		else if (blank) started = 0;
	}
}

static void load_level (int n)
{
	if (g_nlvl == 0) return;
	if (n < 0) n = 0;
	if (n >= g_nlvl) n = 0;
	g_cur = n; g_won = 0;
	for (int r = 0; r < GH; r++) for (int c = 0; c < GW; c++)
		{ g_wall[r][c] = g_box[r][c] = g_target[r][c] = 0; }

	int i = g_lvloff[n], r = 0;
	while (g_buf[i] && r < GH)
	{
		int ls = i; while (g_buf[i] && g_buf[i] != '\n') i++;
		int le = i; if (g_buf[i] == '\n') i++;
		int blank = 1; for (int k = ls; k < le; k++) if (!is_ws (g_buf[k])) { blank = 0; break; }
		if (blank) break;
		int c = 0;
		for (int k = ls; k < le && c < GW; k++)
		{
			char ch = g_buf[k];
			if (ch == '\r') continue;
			if (ch == '#') g_wall[r][c] = 1;
			if (ch == '.' || ch == '*' || ch == '+') g_target[r][c] = 1;
			if (ch == '$' || ch == '*') g_box[r][c] = 1;
			if (ch == '@' || ch == '+') { g_px = c; g_py = r; }
			c++;
		}
		r++;
	}
}

static void check_win (void)
{
	for (int r = 0; r < GH; r++) for (int c = 0; c < GW; c++)
		if (g_box[r][c] && !g_target[r][c]) { g_won = 0; return; }
	g_won = 1;
}

static void move (int dx, int dy)
{
	if (g_won) return;
	int nx = g_px + dx, ny = g_py + dy;
	if (nx < 0 || nx >= GW || ny < 0 || ny >= GH || g_wall[ny][nx]) return;
	if (g_box[ny][nx])
	{
		int bx = nx + dx, by = ny + dy;
		if (bx < 0 || bx >= GW || by < 0 || by >= GH || g_wall[by][bx] || g_box[by][bx]) return;
		g_box[ny][nx] = 0; g_box[by][bx] = 1;
	}
	g_px = nx; g_py = ny;
	check_win ();
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	switch (key)
	{
	case KEY_LEFT:  move (-1, 0); break;
	case KEY_RIGHT: move (1, 0); break;
	case KEY_UP:    move (0, -1); break;
	case KEY_DOWN:  move (0, 1); break;
	case 'r': case 'R': load_level (g_cur); break;
	case 'n': case 'N': load_level (g_cur + 1); break;
	}
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00181c20);
	wtk::draw_text (fb, W, H, 8, 8, g_won ? "SOLVED!  n:next r:reset" : "arrows move  r:reset n:next",
			g_won ? 0x0060ff90 : 0x0090a0b0);
	for (int r = 0; r < GH; r++)
		for (int c = 0; c < GW; c++)
		{
			int x = OX + c * CELL, y = OY + r * CELL;
			if (g_wall[r][c]) { fill_rect (x, y, CELL - 1, CELL - 1, 0x00586070); continue; }
			if (g_target[r][c]) fill_rect (x + CELL / 2 - 3, y + CELL / 2 - 3, 6, 6, 0x00c06060);
			if (g_box[r][c])
				fill_rect (x + 2, y + 2, CELL - 5, CELL - 5,
					   g_target[r][c] ? 0x0060c060 : 0x00b07840);
		}
	fill_rect (OX + g_px * CELL + 4, OY + g_py * CELL + 4, CELL - 9, CELL - 9, 0x004090ff);
}

int main (void)
{
	fb = kapi_create_window (W, H, "sokoban");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();

	void *f = kapi_open ("SD:/apps/sokoban.app/levels.txt");
	if (f != 0)
	{
		int n = kapi_read (f, g_buf, sizeof (g_buf) - 1);
		kapi_close (f);
		if (n < 0) n = 0;
		g_buf[n] = '\0';
	}
	if (g_buf[0] == '\0')
	{
		int i = 0; for (; EMBED[i] && i < (int) sizeof (g_buf) - 1; i++) g_buf[i] = EMBED[i];
		g_buf[i] = '\0';
	}
	find_levels ();
	load_level (0);

	kapi_set_key_handler (on_key);
	while (!should_exit ()) { pump_events (); redraw (); msleep (16); }
	return 0;
}
