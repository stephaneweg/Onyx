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
#include "sk_api.h"


#define WALLPAPER_INI	"SD:/etc/wallpaper.ini"

#define WALLPAPER_DIR	"SD:/wallpapers"		// the patterns

enum { WP_VORONOI, WP_GRADIENT, WP_BUBBLES, WP_SOLID, WP_IMAGE, WP_PATTERN, WP_NMODES };

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
SK_API void wp_defaults (Wallpaper &w);	// the wallpaper without a file: voronoi, 28 points, the blues 0x4878B0 and 0x1C2C48, the pattern waves.png
SK_API bool wp_eq (const char *a, const char *b);	// true if a and b are the same string, the case of A..Z ignored
SK_API unsigned wp_colour (const char *v, unsigned def);	// a colour in hexadecimal ("0xRRGGBB", "#RRGGBB", "RRGGBB") -> 0x00RRGGBB; def if v has no hex digit
// "key = value" lines of a file: fn (key, value) for each. -> false: no file.
SK_API bool wp_lines (const char *path, void (*fn) (const char *k, const char *v, void *ctx), void *ctx);
SK_API void wp_key (const char *k, const char *v, void *ctx);
SK_API void wp_load (Wallpaper &w);
SK_API int wp_put (char *o, int p, int cap, const char *s);
SK_API int wp_put_colour (char *o, int p, int cap, unsigned c);
SK_API bool wp_save (const Wallpaper &w);
// ---- the painter ---------------------------------------------------------------------------------
SK_API unsigned wp_isqrt (unsigned n);	// the integer square root of n (rounded down)

// The colour between a and b (0x00RRGGBB), each channel apart -> a for t 0, b for t 255.
static inline unsigned wp_mix (unsigned a, unsigned b, int t)		// t 0..255: a -> b
{
	int r = (int) ((a >> 16) & 255) + ((int) ((b >> 16) & 255) - (int) ((a >> 16) & 255)) * t / 255;
	int g = (int) ((a >> 8) & 255) + ((int) ((b >> 8) & 255) - (int) ((a >> 8) & 255)) * t / 255;
	int c = (int) (a & 255) + ((int) (b & 255) - (int) (a & 255)) * t / 255;
	return ((unsigned) r << 16) | ((unsigned) g << 8) | (unsigned) c;
}
// Voronoi's tint: the base colour darker near a cell's seed, lighter at its edges (SimpleOS's).
SK_API unsigned wp_tint (unsigned base, unsigned dist);
// Paint w x h pixels (stride: a row) -- not an image (apps/imageview does those); a pattern: its
// gradient (then wp_multiply by the pattern's grey). div: the cells computed at 1 / div of the
// resolution (voronoi: 2 for the screen, 1 for a small preview). yield: called now and then (a
// long job: kapi_yield), or 0.
SK_API void wp_paint (unsigned *dst, int w, int h, int stride, const Wallpaper &wp, unsigned seed, int div, void (*yield) (void));
// ---- a pattern: a grey picture multiplying the colours ---------------------------------------------
SK_API unsigned wp_lum (unsigned c);	// the luminance 0..255 of a colour: (77 R + 150 G + 29 B) / 256
// The grey of a picture (0xAARRGGBB, iw x ih: its luminance) over w x h as "cover" lays it (the
// area filled, centred, the rest cut off): the average of the pixels under each one where it
// shrinks (a small preview), bilinear where it grows (a bigger screen).
SK_API void wp_grey_cover (const unsigned *img, int iw, int ih, unsigned char *out, int w, int h);
// ... tiled from the top left instead (a tinted picture's "tile" style): the picture at the
// screen's scale (sw x sh: the screen w x h stands for -- a small preview's).
SK_API void wp_grey_tile (const unsigned *img, int iw, int ih, unsigned char *out, int w, int h, int sw, int sh);
// The colours multiplied by the grey (w x h): white keeps them, black makes them black.
SK_API void wp_multiply (unsigned *dst, int w, int h, int stride, const unsigned char *grey);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "wallpaper.inc"
#endif

#endif
