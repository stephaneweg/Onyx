//
// wallpaper.h -- the desktop's wallpaper settings (SD:/etc/wallpaper.ini) and its painter, shared
// by apps/voronoy (it paints the wallpaper at boot, and again when the Theme applet applies one)
// and the Theme applet (its preview, and the file it writes). Integer only (the apps' default).
//
//     mode      = voronoi      voronoi (cells), gradient (two colours), bubbles (a gradient with
//                              soft bubbles over it), solid (one colour), image (a picture file),
//                              pattern (a grey picture coloured by the gradient: multiplied)
//     color     = 0x4878B0     voronoi's base colour, the solid colour, the gradient's first
//     color2    = 0x1C2C48     the gradient's second (gradient, bubbles, pattern)
//     direction = vertical     the gradient: vertical (top to bottom) or horizontal (left to right)
//     points    = 28           voronoi's cells (1..64)
//     image     = SD:/x.jpg    image: the picture (BMP GIF PNG JPEG PCX WebP), painted by
//     style     = cover        apps/imageview --background: cover (the screen filled, centred)
//                              or tile (repeated from the top left)
//     tint      = no           image: yes -- its grey multiplies `color` (a tinted picture:
//                              painted by voronoy itself), no -- shown as it is
//     pattern   = SD:/wallpapers/waves.png   pattern: the grey picture (tools/gen_wallpapers.py
//                              makes the shipped ones, SD:/wallpapers): each pixel's grey
//                              multiplies the gradient -- white is the colour itself -- as
//                              "cover" fills the screen (wp_grey_cover, wp_multiply)
//
// No file: SD:/apps/voronoy.app/config.ini's base / points (before the Theme applet had it).
//
#ifndef _wallpaper_h
#define _wallpaper_h

#include "appkit/appkit.h"

#define WALLPAPER_INI	"SD:/etc/wallpaper.ini"

#define WALLPAPER_DIR	"SD:/wallpapers"		// the patterns

enum { WP_VORONOI, WP_GRADIENT, WP_BUBBLES, WP_SOLID, WP_IMAGE, WP_PATTERN, WP_NMODES };
static const char *const WP_MODE_KEY[WP_NMODES] = { "voronoi", "gradient", "bubbles", "solid", "image", "pattern" };

struct Wallpaper
{
	int mode;
	unsigned c1, c2;
	int vertical;			// the gradient's direction (1: top to bottom)
	int points;
	char image[200];
	int tile;			// image: 1 tiled, 0 cover
	int tint;			// image: 1 its grey times c1, 0 as it is
	char pattern[200];		// pattern: the grey picture
};

static inline void wp_defaults (Wallpaper &w)
{
	w.mode = WP_VORONOI; w.c1 = 0x004878B0; w.c2 = 0x001C2C48; w.vertical = 1; w.points = 28;
	w.image[0] = 0; w.tile = 0; w.tint = 0;
	const char *d = WALLPAPER_DIR "/waves.png";
	int i = 0; for (; d[i] && i < (int) sizeof w.pattern - 1; i++) w.pattern[i] = d[i];
	w.pattern[i] = 0;
}

static inline bool wp_eq (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = *a, y = *b;
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return false;
		if (!x) return true;
	}
}

static inline unsigned wp_colour (const char *v, unsigned def)
{
	if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v += 2;
	else if (v[0] == '#') v++;
	unsigned c = 0; int n = 0;
	for (; *v; v++, n++)
	{
		char ch = *v; int d;
		if (ch >= '0' && ch <= '9') d = ch - '0';
		else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
		else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
		else break;
		c = c * 16 + (unsigned) d;
	}
	return n ? c & 0x00FFFFFFu : def;
}

// "key = value" lines of a file: fn (key, value) for each. -> false: no file.
static inline bool wp_lines (const char *path, void (*fn) (const char *k, const char *v, void *ctx), void *ctx)
{
	void *f = kapi_open (path);
	if (f == 0) return false;
	static char buf[2048];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	buf[n] = 0;
	for (char *p = buf; *p; )
	{
		char *l = p; while (*p && *p != '\n') p++;
		if (*p) *p++ = 0;
		while (*l == ' ' || *l == '\t') l++;
		if (*l == ';' || *l == '#') continue;
		char *eq = l; while (*eq && *eq != '=') eq++;
		if (*eq != '=') continue;
		char *ke = eq; while (ke > l && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
		*ke = 0;
		char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
		char *ve = v; while (*ve) ve++;
		while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) *--ve = 0;
		fn (l, v, ctx);
	}
	return true;
}

static inline void wp_key (const char *k, const char *v, void *ctx)
{
	Wallpaper &w = *(Wallpaper *) ctx;
	if (wp_eq (k, "mode")) { for (int i = 0; i < WP_NMODES; i++) if (wp_eq (v, WP_MODE_KEY[i])) w.mode = i; }
	else if (wp_eq (k, "color") || wp_eq (k, "base")) w.c1 = wp_colour (v, w.c1);
	else if (wp_eq (k, "color2")) w.c2 = wp_colour (v, w.c2);
	else if (wp_eq (k, "direction")) w.vertical = !wp_eq (v, "horizontal");
	else if (wp_eq (k, "points")) { int n = 0; for (const char *p = v; *p >= '0' && *p <= '9'; p++) n = n * 10 + (*p - '0'); if (n > 0) w.points = n > 64 ? 64 : n; }
	else if (wp_eq (k, "image")) { int i = 0; for (; v[i] && i < (int) sizeof w.image - 1; i++) w.image[i] = v[i]; w.image[i] = 0; }
	else if (wp_eq (k, "style")) w.tile = wp_eq (v, "tile");
	else if (wp_eq (k, "tint")) w.tint = wp_eq (v, "yes") || wp_eq (v, "1") || wp_eq (v, "on");
	else if (wp_eq (k, "pattern")) { int i = 0; for (; v[i] && i < (int) sizeof w.pattern - 1; i++) w.pattern[i] = v[i]; w.pattern[i] = 0; }
}

static inline void wp_load (Wallpaper &w)
{
	wp_defaults (w);
	if (!wp_lines (WALLPAPER_INI, wp_key, &w)) wp_lines ("SD:/apps/voronoy.app/config.ini", wp_key, &w);
}

static inline int wp_put (char *o, int p, int cap, const char *s) { while (s && *s && p < cap - 1) o[p++] = *s++; o[p] = 0; return p; }
static inline int wp_put_colour (char *o, int p, int cap, unsigned c)
{
	const char *hx = "0123456789ABCDEF";
	char b[9] = { '0', 'x' };
	for (int i = 0; i < 6; i++) b[2 + i] = hx[(c >> ((5 - i) * 4)) & 0xF];
	b[8] = 0;
	return wp_put (o, p, cap, b);
}

static inline bool wp_save (const Wallpaper &w)
{
	static char o[1024];
	int p = 0;
	p = wp_put (o, p, sizeof o, "; The desktop's wallpaper (wallpaper.h): painted by apps/voronoy, written by the\n"
		"; Control Panel's Theme applet. mode: voronoi, gradient, bubbles, solid, image or pattern.\nmode      = ");
	p = wp_put (o, p, sizeof o, WP_MODE_KEY[w.mode]);
	p = wp_put (o, p, sizeof o, "\ncolor     = "); p = wp_put_colour (o, p, sizeof o, w.c1);
	p = wp_put (o, p, sizeof o, "\ncolor2    = "); p = wp_put_colour (o, p, sizeof o, w.c2);
	p = wp_put (o, p, sizeof o, "\ndirection = "); p = wp_put (o, p, sizeof o, w.vertical ? "vertical" : "horizontal");
	p = wp_put (o, p, sizeof o, "\npoints    = ");
	char n[4] = { (char) ('0' + w.points / 10), (char) ('0' + w.points % 10), 0, 0 };
	p = wp_put (o, p, sizeof o, w.points >= 10 ? n : n + 1);
	p = wp_put (o, p, sizeof o, "\nimage     = "); p = wp_put (o, p, sizeof o, w.image);
	p = wp_put (o, p, sizeof o, "\nstyle     = "); p = wp_put (o, p, sizeof o, w.tile ? "tile" : "cover");
	p = wp_put (o, p, sizeof o, "\ntint      = "); p = wp_put (o, p, sizeof o, w.tint ? "yes" : "no");
	p = wp_put (o, p, sizeof o, "\npattern   = "); p = wp_put (o, p, sizeof o, w.pattern);
	p = wp_put (o, p, sizeof o, "\n");
	return kapi_save_file (WALLPAPER_INI, o, (unsigned) p) >= 0;
}

// ---- the painter ---------------------------------------------------------------------------------
static inline unsigned wp_isqrt (unsigned n)
{
	if (n == 0) return 0;
	unsigned x = n, y = (x + 1) / 2;
	while (y < x) { x = y; y = (x + n / x) / 2; }
	return x;
}

static inline unsigned wp_mix (unsigned a, unsigned b, int t)		// t 0..255: a -> b
{
	int r = (int) ((a >> 16) & 255) + ((int) ((b >> 16) & 255) - (int) ((a >> 16) & 255)) * t / 255;
	int g = (int) ((a >> 8) & 255) + ((int) ((b >> 8) & 255) - (int) ((a >> 8) & 255)) * t / 255;
	int c = (int) (a & 255) + ((int) (b & 255) - (int) (a & 255)) * t / 255;
	return ((unsigned) r << 16) | ((unsigned) g << 8) | (unsigned) c;
}

// Voronoi's tint: the base colour darker near a cell's seed, lighter at its edges (SimpleOS's).
static inline unsigned wp_tint (unsigned base, unsigned dist)
{
	const unsigned mn = 96;
	unsigned cc = mn + dist * (255 - mn) / 255;
	if (cc > 255) cc = 255;
	return ((((base >> 16) & 0xFF) * cc) >> 8) << 16 | ((((base >> 8) & 0xFF) * cc) >> 8) << 8 | (((base & 0xFF) * cc) >> 8);
}

// Paint w x h pixels (stride: a row) -- not an image (apps/imageview does those); a pattern: its
// gradient (then wp_multiply by the pattern's grey). div: the cells computed at 1 / div of the
// resolution (voronoi: 2 for the screen, 1 for a small preview). yield: called now and then (a
// long job: kapi_yield), or 0.
static inline void wp_paint (unsigned *dst, int w, int h, int stride, const Wallpaper &wp, unsigned seed, int div, void (*yield) (void))
{
	if (w <= 0 || h <= 0) return;
	if (wp.mode == WP_SOLID || wp.mode == WP_IMAGE)
	{
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) dst[(long) y * stride + x] = wp.c1;
		return;
	}
	if (wp.mode == WP_GRADIENT || wp.mode == WP_BUBBLES || wp.mode == WP_PATTERN)
	{
		for (int y = 0; y < h; y++)
		{
			unsigned *d = dst + (long) y * stride;
			if (wp.vertical) { unsigned c = wp_mix (wp.c1, wp.c2, h > 1 ? y * 255 / (h - 1) : 0); for (int x = 0; x < w; x++) d[x] = c; }
			else for (int x = 0; x < w; x++) d[x] = wp_mix (wp.c1, wp.c2, w > 1 ? x * 255 / (w - 1) : 0);
		}
		if (wp.mode != WP_BUBBLES) return;
		// the bubbles: soft discs of a light tint, more or less see-through, of every size
		unsigned rng = seed | 1u;
		int nb = 26;
		for (int b = 0; b < nb; b++)
		{
			rng = rng * 1103515245u + 12345u; int cx = (int) ((rng >> 8) % (unsigned) w);
			rng = rng * 1103515245u + 12345u; int cy = (int) ((rng >> 8) % (unsigned) h);
			rng = rng * 1103515245u + 12345u; int r = (int) (h / 40 + (rng >> 8) % (unsigned) (h / 7 + 1));
			rng = rng * 1103515245u + 12345u; int op = 26 + (int) ((rng >> 8) % 60);
			if (r < 2) r = 2;
			unsigned tint = wp_mix (wp_mix (wp.c1, wp.c2, 128), 0x00FFFFFF, 150);
			for (int y = cy - r; y <= cy + r; y++)
			{
				if (y < 0 || y >= h) continue;
				for (int x = cx - r; x <= cx + r; x++)
				{
					if (x < 0 || x >= w) continue;
					int dx = x - cx, dy = y - cy;
					unsigned dd = (unsigned) (dx * dx + dy * dy);
					if (dd > (unsigned) (r * r)) continue;
					int t = (int) wp_isqrt (dd * 65536u / (unsigned) (r * r));	// 0 (centre) .. 256 (rim)
					int a = op;						// the body, then a brighter rim
					if (t >= 236) a = 2 * op * (256 - t) / 20;		// fading out at the edge
					else if (t >= 200) a = op + (t - 200) * op / 36;
					if (a < 0) a = 0;
					if (a > 255) a = 255;
					unsigned *p = dst + (long) y * stride + x;
					*p = wp_mix (*p, tint, a);
				}
			}
			if (yield && (b & 3) == 3) yield ();
		}
		return;
	}
	// voronoi: toroidal distances to the nearest of the seeds, computed at 1 / div
	if (div < 1) div = 1;
	int mx = w / div, my = h / div;
	if (mx < 1) mx = 1;
	if (my < 1) my = 1;
	int n = wp.points < 1 ? 1 : wp.points > 64 ? 64 : wp.points;
	int px[64], py[64];
	unsigned rng = seed | 1u;
	for (int i = 0; i < n; i++)
	{
		rng = rng * 1103515245u + 12345u; px[i] = (int) (rng % (unsigned) mx);
		rng = rng * 1103515245u + 12345u; py[i] = (int) (rng % (unsigned) my);
	}
	for (int y = 0; y < my; y++)
	{
		for (int x = 0; x < mx; x++)
		{
			unsigned best = 0xFFFFFFFF;
			for (int i = 0; i < n; i++)
			{
				int dx = x > px[i] ? x - px[i] : px[i] - x;
				dx = dx * 256 / mx; if (dx > 128) dx = 256 - dx;
				int dy = y > py[i] ? y - py[i] : py[i] - y;
				dy = dy * 256 / my; if (dy > 128) dy = 256 - dy;
				unsigned d = wp_isqrt ((unsigned) (dx * dx + dy * dy));
				if (d > 255) d = 255;
				if (d < best) best = d;
			}
			unsigned col = wp_tint (wp.c1, best);
			for (int j = 0; j < div; j++)
				for (int k = 0; k < div; k++)
				{
					int yy = y * div + j, xx = x * div + k;
					if (yy < h && xx < w) dst[(long) yy * stride + xx] = col;
				}
		}
		if (yield && (y & 15) == 0) yield ();
	}
}

// ---- a pattern: a grey picture multiplying the colours ---------------------------------------------
static inline unsigned wp_lum (unsigned c)
{
	return (((c >> 16) & 255) * 77 + ((c >> 8) & 255) * 150 + (c & 255) * 29) >> 8;
}

// The grey of a picture (0xAARRGGBB, iw x ih: its luminance) over w x h as "cover" lays it (the
// area filled, centred, the rest cut off): the average of the pixels under each one where it
// shrinks (a small preview), bilinear where it grows (a bigger screen).
static inline void wp_grey_cover (const unsigned *img, int iw, int ih, unsigned char *out, int w, int h)
{
	if (!img || iw <= 0 || ih <= 0 || w <= 0 || h <= 0) return;
	long kx = (long) iw * 65536 / w, ky = (long) ih * 65536 / h, k = kx < ky ? kx : ky;	// (16.16)
	long ox = ((long) iw * 65536 - k * w) / 2, oy = ((long) ih * 65536 - k * h) / 2;
	for (int y = 0; y < h; y++)
	{
		unsigned char *o = out + (long) y * w;
		if (k >= 65536)
		{
			int sy0 = (int) ((oy + k * y) >> 16), sy1 = (int) ((oy + k * (y + 1)) >> 16);
			if (sy1 <= sy0) sy1 = sy0 + 1;
			if (sy1 > ih) sy1 = ih;
			for (int x = 0; x < w; x++)
			{
				int sx0 = (int) ((ox + k * x) >> 16), sx1 = (int) ((ox + k * (x + 1)) >> 16);
				if (sx1 <= sx0) sx1 = sx0 + 1;
				if (sx1 > iw) sx1 = iw;
				unsigned sum = 0, n = 0;
				for (int sy = sy0; sy < sy1; sy++)
					for (int sx = sx0; sx < sx1; sx++) { sum += wp_lum (img[(long) sy * iw + sx]); n++; }
				o[x] = (unsigned char) (n ? sum / n : 255);
			}
		}
		else
		{
			long fy = oy + k * y + k / 2 - 32768; if (fy < 0) fy = 0;
			int sy = (int) (fy >> 16), ty = (int) ((fy >> 8) & 255);
			if (sy >= ih) sy = ih - 1;
			int sy2 = sy + 1 < ih ? sy + 1 : ih - 1;
			for (int x = 0; x < w; x++)
			{
				long fx = ox + k * x + k / 2 - 32768; if (fx < 0) fx = 0;
				int sx = (int) (fx >> 16), tx = (int) ((fx >> 8) & 255);
				if (sx >= iw) sx = iw - 1;
				int sx2 = sx + 1 < iw ? sx + 1 : iw - 1;
				unsigned a = wp_lum (img[(long) sy * iw + sx]), b = wp_lum (img[(long) sy * iw + sx2]);
				unsigned c = wp_lum (img[(long) sy2 * iw + sx]), d = wp_lum (img[(long) sy2 * iw + sx2]);
				unsigned top = a * (256 - (unsigned) tx) + b * (unsigned) tx, bot = c * (256 - (unsigned) tx) + d * (unsigned) tx;
				o[x] = (unsigned char) ((top * (256 - (unsigned) ty) + bot * (unsigned) ty) >> 16);
			}
		}
	}
}

// ... tiled from the top left instead (a tinted picture's "tile" style): the picture at the
// screen's scale (sw x sh: the screen w x h stands for -- a small preview's).
static inline void wp_grey_tile (const unsigned *img, int iw, int ih, unsigned char *out, int w, int h, int sw, int sh)
{
	if (!img || iw <= 0 || ih <= 0 || w <= 0 || h <= 0) return;
	for (int y = 0; y < h; y++)
	{
		int sy = (int) (((long) y * sh / h) % ih);
		for (int x = 0; x < w; x++) out[(long) y * w + x] = (unsigned char) wp_lum (img[(long) sy * iw + (int) (((long) x * sw / w) % iw)]);
	}
}

// The colours multiplied by the grey (w x h): white keeps them, black makes them black.
static inline void wp_multiply (unsigned *dst, int w, int h, int stride, const unsigned char *grey)
{
	for (int y = 0; y < h; y++)
	{
		unsigned *d = dst + (long) y * stride;
		const unsigned char *g = grey + (long) y * w;
		for (int x = 0; x < w; x++)
		{
			unsigned c = d[x], k = (unsigned) g[x] + 1;
			d[x] = ((((c >> 16) & 255) * k >> 8) << 16) | ((((c >> 8) & 255) * k >> 8) << 8) | ((c & 255) * k >> 8);
		}
	}
}

#endif
