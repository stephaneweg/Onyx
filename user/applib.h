//
// applib.h -- tiny freestanding string helpers for the apps (no libc in EL1 apps).
// Header-only; included by the shell apps that build paths and parse config files.
//
#ifndef _applib_h
#define _applib_h

#include "kapi.h"		// kapi_open/read/close/app_dir (for the INI reader)

// Append src to dst[*pos], advancing *pos, never overflowing cap. NUL-terminates.
static inline void ax_strcat (char *dst, int cap, int *pos, const char *src)
{
	while (*src != '\0' && *pos < cap - 1)
	{
		dst[(*pos)++] = *src++;
	}
	dst[*pos] = '\0';
}

// Build "SD:apps/<name><suffix>" (e.g. suffix ".app/icon.bmp") into dst.
static inline void ax_app_path (char *dst, int cap, const char *name, const char *suffix)
{
	int p = 0;
	ax_strcat (dst, cap, &p, "SD:apps/");
	ax_strcat (dst, cap, &p, name);
	ax_strcat (dst, cap, &p, suffix);
}

static inline int ax_streq (const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) { a++; b++; }
	return *a == *b;
}

static inline int ax_strlen (const char *s)
{
	int n = 0;
	while (s[n] != '\0') n++;
	return n;
}

// Console helpers for /bin tools: write a string / a line to stdout.
static inline void ax_puts (const char *s)  { kapi_stdout_write (s, (unsigned) ax_strlen (s)); }
static inline void ax_putln (const char *s) { ax_puts (s); kapi_stdout_write ("\n", 1); }

// Signed int -> string. Returns length.
static inline int ax_itoa (int v, char *b)
{
	char t[12]; int n = 0, p = 0;
	if (v < 0) { b[p++] = '-'; v = -v; }
	if (v == 0) t[n++] = '0';
	while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
	while (n) b[p++] = t[--n];
	b[p] = '\0';
	return p;
}

// Two-digit zero-padded decimal into d[0],d[1].
static inline void ax_fmt2 (char *d, int v)
{
	if (v < 0) v = 0;
	d[0] = (char) ('0' + (v / 10) % 10);
	d[1] = (char) ('0' + v % 10);
}

// ---- .ini config reader -----------------------------------------------------
//
// A minimal "old .ini" reader: [section] headers and key=value lines, ';' or '#'
// comments, whitespace trimmed. Loaded from the app's OWN folder (kapi_app_dir).
// Shared by-source for now (header-only); each app gets its own store.
//
#define INI_MAX		64	// max key/value entries
#define INI_STRLEN	64	// max section/key/value length
#define INI_BUFSZ	2048	// max file size read

static char g_ini_sec[INI_MAX][INI_STRLEN];
static char g_ini_key[INI_MAX][INI_STRLEN];
static char g_ini_val[INI_MAX][INI_STRLEN];
static int  g_ini_n = 0;
static char g_ini_buf[INI_BUFSZ];

// Copy buf[s..e) into dst, trimming surrounding spaces/tabs, capped to INI_STRLEN.
static inline void ax_ini_range (char *dst, const char *buf, int s, int e)
{
	while (s < e && (buf[s] == ' ' || buf[s] == '\t')) s++;
	while (e > s && (buf[e - 1] == ' ' || buf[e - 1] == '\t')) e--;
	int j = 0;
	for (int i = s; i < e && j < INI_STRLEN - 1; i++) dst[j++] = buf[i];
	dst[j] = '\0';
}

// Load an explicit absolute path (any app, not just one's own folder). Returns the
// entry count, or -1 if the file is absent.
static inline int app_ini_load_path (const char *path)
{
	g_ini_n = 0;

	void *f = kapi_open (path);
	if (f == 0) return -1;
	int n = kapi_read (f, g_ini_buf, sizeof (g_ini_buf) - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	g_ini_buf[n] = '\0';

	char cur[INI_STRLEN]; cur[0] = '\0';			// current section
	int i = 0;
	while (g_ini_buf[i] != '\0' && g_ini_n < INI_MAX)
	{
		while (g_ini_buf[i] == ' ' || g_ini_buf[i] == '\t') i++;
		int ls = i;
		while (g_ini_buf[i] != '\0' && g_ini_buf[i] != '\n' && g_ini_buf[i] != '\r') i++;
		int le = i;
		while (g_ini_buf[i] == '\n' || g_ini_buf[i] == '\r') i++;
		while (le > ls && (g_ini_buf[le - 1] == ' ' || g_ini_buf[le - 1] == '\t')) le--;
		if (le <= ls) continue;				// blank
		char c0 = g_ini_buf[ls];
		if (c0 == ';' || c0 == '#') continue;		// comment
		if (c0 == '[')					// [section]
		{
			int e = le;
			if (e > ls + 1 && g_ini_buf[e - 1] == ']') e--;
			ax_ini_range (cur, g_ini_buf, ls + 1, e);
			continue;
		}
		int eq = ls;					// key=value
		while (eq < le && g_ini_buf[eq] != '=') eq++;
		if (eq >= le) continue;				// no '='
		int k = g_ini_n;
		int j = 0;					// section: cur is already clean
		while (cur[j] != '\0' && j < INI_STRLEN - 1) { g_ini_sec[k][j] = cur[j]; j++; }
		g_ini_sec[k][j] = '\0';
		ax_ini_range (g_ini_key[k], g_ini_buf, ls, eq);
		ax_ini_range (g_ini_val[k], g_ini_buf, eq + 1, le);
		g_ini_n++;
	}
	return g_ini_n;
}

// Load <appdir>/filename (e.g. "config.ini") from the calling app's OWN folder.
// Returns entry count, or -1 if absent.
static inline int app_ini_load (const char *filename)
{
	char path[96];
	int pn = kapi_app_dir (path, sizeof (path));		// "SD:apps/<self>.app/"
	for (int i = 0; filename[i] != '\0' && pn < (int) sizeof (path) - 1; i++)
		path[pn++] = filename[i];
	path[pn] = '\0';
	return app_ini_load_path (path);
}

// Look up a value. section may be 0/"" for keys before any [section]. Returns def
// if not found.
static inline const char *app_ini_get (const char *section, const char *key, const char *def)
{
	const char *sec = section ? section : "";
	for (int i = 0; i < g_ini_n; i++)
		if (ax_streq (g_ini_sec[i], sec) && ax_streq (g_ini_key[i], key))
			return g_ini_val[i];
	return def;
}

// Same, parsed as a signed int32.
static inline int app_ini_get_int (const char *section, const char *key, int def)
{
	const char *v = app_ini_get (section, key, 0);
	if (v == 0) return def;
	int i = 0, neg = 0, any = 0; long r = 0;
	if (v[0] == '-') { neg = 1; i = 1; } else if (v[0] == '+') i = 1;
	while (v[i] >= '0' && v[i] <= '9') { r = r * 10 + (v[i] - '0'); i++; any = 1; }
	if (!any) return def;
	return (int) (neg ? -r : r);
}

// ---- app-drawn widgets: dropdown + colour picker ----------------------------
//
// These are NOT kernel widgets: the app renders them into its own window canvas
// (fb, width W, height H) and routes clicks through its canvas-click handler. The
// owning app redraws each frame; the open "overlay" (list / palette) is drawn last
// so it sits on top. Click handlers return 1 if they consumed the click.
//
// Drawing primitives into the app canvas (clipped to W x H).
static inline void ax_fill (unsigned *fb, int W, int H, int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h; yy++)
		if (yy >= 0 && yy < H)
			for (int xx = x; xx < x + w; xx++)
				if (xx >= 0 && xx < W) fb[yy * W + xx] = c;
}
static inline void ax_frame (unsigned *fb, int W, int H, int x, int y, int w, int h, unsigned c)
{
	ax_fill (fb, W, H, x, y, w, 1, c);
	ax_fill (fb, W, H, x, y + h - 1, w, 1, c);
	ax_fill (fb, W, H, x, y, 1, h, c);
	ax_fill (fb, W, H, x + w - 1, y, 1, h, c);
}

// ---- dropdown ----
typedef struct {
	int x, y, w, h;			// closed box rect (canvas coords)
	const char *const *opts;	// option labels
	int nopts;
	int sel;			// selected index
	int open;			// list expanded?
} ax_dropdown;

static inline void ax_dropdown_draw (ax_dropdown *d, unsigned *fb, int W, int H)
{
	ax_fill (fb, W, H, d->x, d->y, d->w, d->h, 0x00203040);
	ax_frame (fb, W, H, d->x, d->y, d->w, d->h, 0x00708ca8);
	if (d->sel >= 0 && d->sel < d->nopts)
		kapi_draw_text (d->x + 6, d->y + (d->h - 16) / 2, d->opts[d->sel], 0x00e6e6e6);
	kapi_draw_text (d->x + d->w - 14, d->y + (d->h - 16) / 2, d->open ? "^" : "v", 0x0090b0c8);
	if (d->open)
		for (int i = 0; i < d->nopts; i++)
		{
			int ry = d->y + d->h + i * d->h;
			ax_fill (fb, W, H, d->x, ry, d->w, d->h, i == d->sel ? 0x00355070 : 0x00182838);
			ax_frame (fb, W, H, d->x, ry, d->w, d->h, 0x00405468);
			kapi_draw_text (d->x + 6, ry + (d->h - 16) / 2, d->opts[i], 0x00e6e6e6);
		}
}

// Route a canvas click. Returns 1 if consumed (opened/closed/selected). When an
// option is picked, sel is updated and the list closes.
static inline int ax_dropdown_click (ax_dropdown *d, int cx, int cy)
{
	if (d->open)
	{
		if (cx >= d->x && cx < d->x + d->w && cy >= d->y + d->h
		    && cy < d->y + d->h * (1 + d->nopts))
		{
			d->sel = (cy - (d->y + d->h)) / d->h;
			d->open = 0;
			return 1;
		}
		d->open = 0;		// click elsewhere: close
	}
	if (cx >= d->x && cx < d->x + d->w && cy >= d->y && cy < d->y + d->h)
	{
		d->open = !d->open;
		return 1;
	}
	return 0;
}

// ---- colour picker (fixed palette grid) ----
#define AX_PAL_COLS	8
#define AX_PAL_ROWS	5
#define AX_PAL_CELL	18

// 40-colour palette: a grey ramp + four hue rows at increasing brightness.
static inline unsigned ax_palette_color (int i)
{
	static const unsigned pal[AX_PAL_COLS * AX_PAL_ROWS] = {
		0x00000000,0x00202020,0x00404040,0x00606060,0x00909090,0x00c0c0c0,0x00e8e8e8,0x00ffffff,
		0x00400000,0x00800000,0x00c02020,0x00ff4040,0x00ff8080,0x00ffc0c0,0x00ffe0a0,0x00ffd040,
		0x00204000,0x00308020,0x0040c040,0x0060ff60,0x00a0ffa0,0x0020c0a0,0x0040e0d0,0x00a0ffe0,
		0x00002040,0x00204080,0x004078c0,0x004890e0,0x0080b0ff,0x00a0c8ff,0x00c0d8ff,0x00e0ecff,
		0x00200040,0x00502080,0x008040c0,0x00b060e0,0x00d0a0ff,0x00ff60c0,0x00ffa0d8,0x00ffd0a0,
	};
	return (i >= 0 && i < AX_PAL_COLS * AX_PAL_ROWS) ? pal[i] : 0;
}

typedef struct {
	int x, y, w, h;		// swatch rect
	unsigned color;		// current colour (0x00RRGGBB)
	int open;		// palette expanded?
} ax_colorpick;

static inline void ax_colorpick_draw (ax_colorpick *p, unsigned *fb, int W, int H)
{
	ax_fill (fb, W, H, p->x, p->y, p->w, p->h, p->color);
	ax_frame (fb, W, H, p->x, p->y, p->w, p->h, 0x00ffffff);
	ax_frame (fb, W, H, p->x - 1, p->y - 1, p->w + 2, p->h + 2, 0x00000000);
	if (p->open)
		for (int r = 0; r < AX_PAL_ROWS; r++)
			for (int c = 0; c < AX_PAL_COLS; c++)
			{
				int gx = p->x + c * AX_PAL_CELL, gy = p->y + p->h + 2 + r * AX_PAL_CELL;
				ax_fill (fb, W, H, gx, gy, AX_PAL_CELL, AX_PAL_CELL,
					 ax_palette_color (r * AX_PAL_COLS + c));
				ax_frame (fb, W, H, gx, gy, AX_PAL_CELL, AX_PAL_CELL, 0x00303030);
			}
}

static inline int ax_colorpick_click (ax_colorpick *p, int cx, int cy)
{
	if (p->open)
	{
		int gx0 = p->x, gy0 = p->y + p->h + 2;
		if (cx >= gx0 && cx < gx0 + AX_PAL_COLS * AX_PAL_CELL
		    && cy >= gy0 && cy < gy0 + AX_PAL_ROWS * AX_PAL_CELL)
		{
			int c = (cx - gx0) / AX_PAL_CELL, r = (cy - gy0) / AX_PAL_CELL;
			p->color = ax_palette_color (r * AX_PAL_COLS + c);
			p->open = 0;
			return 1;
		}
		p->open = 0;
	}
	if (cx >= p->x && cx < p->x + p->w && cy >= p->y && cy < p->y + p->h)
	{
		p->open = !p->open;
		return 1;
	}
	return 0;
}

#endif
