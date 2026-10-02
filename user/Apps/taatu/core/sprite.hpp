//
// sprite.hpp -- portable 32-bit (0xAARRGGBB) image buffer + the blit/slice/mirror/tint ops
// the avatar compositor needs. No STL, malloc-backed, so it links on Onyx and the PC alike.
// The actual PNG decode is the platform's job (Onyx: img_load_mem; PC: a decoder for tests).
//
#ifndef TAATU_SPRITE_HPP
#define TAATU_SPRITE_HPP
#include <stdlib.h>
#include <string.h>

namespace taatu {

struct Rgba
{
    int w, h; unsigned *px;         // 0xAARRGGBB, row-major, w*h
    Rgba () : w (0), h (0), px (0) {}
    ~Rgba () { free (px); }
    bool alloc (int W, int H)
    {
        free (px); w = W; h = H;
        px = (unsigned *) calloc ((size_t) (W > 0 ? W : 0) * (H > 0 ? H : 0), 4);
        return px != 0;
    }
    void adopt (unsigned *p, int W, int H) { free (px); px = p; w = W; h = H; }
    unsigned at (int x, int y) const { return (unsigned) x < (unsigned) w && (unsigned) y < (unsigned) h ? px[y * w + x] : 0; }
private:
    Rgba (const Rgba &); Rgba &operator= (const Rgba &);
};

// alpha-over one pixel src onto dst (both 0xAARRGGBB).
static inline unsigned blend_px (unsigned dst, unsigned src)
{
    unsigned a = src >> 24; if (!a) return dst; if (a == 255) return src | 0xFF000000u;
    unsigned da = dst >> 24;
    unsigned sr = (src >> 16) & 255, sg = (src >> 8) & 255, sb = src & 255;
    unsigned dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;
    unsigned ia = 255 - a;
    unsigned rr = (sr * a + dr * ia) / 255, rg = (sg * a + dg * ia) / 255, rb = (sb * a + db * ia) / 255;
    unsigned ra = a + da * ia / 255;
    return (ra << 24) | (rr << 16) | (rg << 8) | rb;
}

// multiply-tint a pixel's RGB by a 0x00RRGGBB colour (keeps alpha). col == 0 -> unchanged.
static inline unsigned tint_px (unsigned p, unsigned col)
{
    if (!col) return p;
    unsigned a = p & 0xFF000000u;
    unsigned r = ((p >> 16) & 255) * ((col >> 16) & 255) / 255;
    unsigned g = ((p >> 8) & 255) * ((col >> 8) & 255) / 255;
    unsigned b = (p & 255) * (col & 255) / 255;
    return a | (r << 16) | (g << 8) | b;
}

// parse "#rrggbb" -> 0x00RRGGBB (0 if empty/invalid).
static inline unsigned parse_hex_color (const char *s)
{
    if (!s || !*s) return 0;
    if (*s == '#') s++;
    unsigned v = 0; int n = 0;
    for (; *s && n < 6; s++, n++)
    {
        int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : -1;
        if (d < 0) return 0; v = v * 16 + d;
    }
    return n == 6 ? v : 0;
}

// Blit a source CELL (sx,sy,cw,ch) of `src` onto `dst` at (dx,dy), alpha-over, optionally
// mirrored horizontally and tinted (multiply). Clipped to dst.
static inline void blit_cell (Rgba &dst, int dx, int dy, const Rgba &src,
                              int sx, int sy, int cw, int ch, bool mirror, unsigned tint)
{
    for (int y = 0; y < ch; y++)
    {
        int ty = dy + y; if ((unsigned) ty >= (unsigned) dst.h) continue;
        int syy = sy + y; if ((unsigned) syy >= (unsigned) src.h) continue;
        for (int x = 0; x < cw; x++)
        {
            int tx = dx + x; if ((unsigned) tx >= (unsigned) dst.w) continue;
            int sxx = sx + (mirror ? (cw - 1 - x) : x); if ((unsigned) sxx >= (unsigned) src.w) continue;
            unsigned s = src.px[syy * src.w + sxx];
            if (tint) s = tint_px (s, tint);
            unsigned *d = &dst.px[ty * dst.w + tx];
            *d = blend_px (*d, s);
        }
    }
}

} // namespace taatu
#endif
