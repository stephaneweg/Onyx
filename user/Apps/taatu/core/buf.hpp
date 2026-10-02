//
// buf.hpp -- a tiny growable byte buffer (malloc-based, no STL) shared by the portable
// TAATU core. Links on Onyx (newlib, no libstdc++) and on the PC the same way.
//
#ifndef TAATU_BUF_HPP
#define TAATU_BUF_HPP
#include <stdlib.h>
#include <string.h>

namespace taatu {

struct Buf
{
    char *p; int n, cap;
    Buf () : p (0), n (0), cap (0) {}
    ~Buf () { free (p); }
    void clear () { n = 0; if (p) p[0] = 0; }
    bool reserve (int need)
    {
        if (need + 1 <= cap) return true;
        int c = cap ? cap : 256;
        while (c < need + 1) c *= 2;
        char *q = (char *) realloc (p, c);
        if (!q) return false;
        p = q; cap = c;
        return true;
    }
    void add (const void *d, int len)
    {
        if (len <= 0 || !reserve (n + len)) return;
        memcpy (p + n, d, len); n += len; p[n] = 0;
    }
    void add (const char *s) { if (s) add (s, (int) strlen (s)); }
    void addc (char c) { if (reserve (n + 1)) { p[n++] = c; p[n] = 0; } }
    void addu (unsigned v)                       // unsigned decimal
    {
        char t[12]; int i = 0;
        if (!v) { addc ('0'); return; }
        while (v) { t[i++] = (char) ('0' + v % 10); v /= 10; }
        while (i) addc (t[--i]);
    }
    // drop the first k bytes (keep the rest), compacting in place
    void drop_front (int k)
    {
        if (k <= 0) return;
        if (k >= n) { n = 0; if (p) p[0] = 0; return; }
        memmove (p, p + k, n - k); n -= k; p[n] = 0;
    }
    char *take () { char *q = p; p = 0; n = cap = 0; return q; }
};

// hex-encode src[len] into out (needs 2*len+1 bytes), lowercase, NUL-terminated.
inline void hex_encode (const unsigned char *src, int len, char *out)
{
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < len; i++) { out[2 * i] = H[src[i] >> 4]; out[2 * i + 1] = H[src[i] & 15]; }
    out[2 * len] = 0;
}
// parse 2*len hex chars from src into out[len]; returns true on success.
inline bool hex_decode (const char *src, int len, unsigned char *out)
{
    auto nib = [] (char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (int i = 0; i < len; i++) {
        int hi = nib (src[2 * i]), lo = nib (src[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (unsigned char) ((hi << 4) | lo);
    }
    return true;
}

} // namespace taatu
#endif
