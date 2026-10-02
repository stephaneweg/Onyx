//
// assets_pc.hpp -- PC asset cache: download sprites/backgrounds over TLS (WinTlsTransport)
// and decode them with GDI+ (PNG/JPEG, native, no external lib) into core Rgba (0xAARRGGBB).
// Mirrors platform/onyx/assets.hpp so the renderer is identical on both.
//
#ifndef TAATU_ASSETS_PC_HPP
#define TAATU_ASSETS_PC_HPP
#ifdef _WIN32
#include "../../core/sprite.hpp"
#include "../../core/httpc.hpp"
#include "tls_win.hpp"
#include "transport_tcp.hpp"
#include <windows.h>
#include <gdiplus.h>
#include <objidl.h>
#include <string.h>

namespace taatu {

// decode PNG/JPEG bytes -> Rgba (0xAARRGGBB). GDI+ must be started (GdiplusStartup) first.
inline bool decode_image (const void *data, int len, Rgba &out)
{
    HGLOBAL hg = GlobalAlloc (GMEM_MOVEABLE, len); if (!hg) return false;
    void *p = GlobalLock (hg); if (!p) { GlobalFree (hg); return false; }
    memcpy (p, data, len); GlobalUnlock (hg);
    IStream *st = 0;
    if (CreateStreamOnHGlobal (hg, TRUE, &st) != S_OK) { GlobalFree (hg); return false; }
    bool ok = false;
    {
        Gdiplus::Bitmap bmp (st);
        if (bmp.GetLastStatus () == Gdiplus::Ok)
        {
            int w = (int) bmp.GetWidth (), h = (int) bmp.GetHeight ();
            Gdiplus::Rect r (0, 0, w, h); Gdiplus::BitmapData bd;
            if (bmp.LockBits (&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok)
            {
                if (out.alloc (w, h))
                {
                    for (int y = 0; y < h; y++)
                        memcpy (&out.px[y * w], (BYTE *) bd.Scan0 + y * bd.Stride, (size_t) w * 4);
                    ok = true;
                }
                bmp.UnlockBits (&bd);
            }
        }
    }
    st->Release ();     // releases the HGLOBAL too (fDeleteOnRelease = TRUE)
    return ok;
}

class PcAssetCache
{
public:
    enum { MAX = 256 };
    struct Entry { char path[192]; Rgba img; bool loaded; };
    Entry e[MAX]; int n;
    CRITICAL_SECTION cs;
    char host[128]; int port; bool tls;

    PcAssetCache () : n (0), port (443), tls (true) { host[0] = 0; InitializeCriticalSection (&cs); }
    void init (const char *h, int p, bool t) { lcpy (host, sizeof host, h); port = p; tls = t; }

    const Rgba *get (const char *path)
    {
        const Rgba *r = 0;
        EnterCriticalSection (&cs);
        for (int i = 0; i < n; i++) if (e[i].loaded && !strcmp (e[i].path, path)) { r = &e[i].img; break; }
        LeaveCriticalSection (&cs);
        return r;
    }
    bool ensure (const char *path)
    {
        if (!path || !path[0]) return false;
        EnterCriticalSection (&cs);
        for (int i = 0; i < n; i++) if (!strcmp (e[i].path, path)) { bool ok = e[i].loaded; LeaveCriticalSection (&cs); return ok; }
        int idx = n < MAX ? n++ : -1;
        if (idx >= 0) { lcpy (e[idx].path, sizeof e[idx].path, path); e[idx].loaded = false; }
        LeaveCriticalSection (&cs);
        if (idx < 0) return false;

        ITransport *tp = tls ? (ITransport *) new WinTlsTransport () : (ITransport *) new PcTcpTransport ();
        HttpResp rp; bool ok = http_request (*tp, host, port, "GET", path, "", 0, 0, rp); delete tp;
        if (!ok || rp.status != 200 || rp.body.n <= 0) return false;
        Rgba tmp; if (!decode_image (rp.body.p, rp.body.n, tmp)) return false;

        EnterCriticalSection (&cs);
        e[idx].img.adopt (tmp.px, tmp.w, tmp.h); tmp.px = 0; e[idx].loaded = true;
        LeaveCriticalSection (&cs);
        return true;
    }
private:
    static void lcpy (char *d, int cap, const char *s) { int i = 0; if (!s) { d[0] = 0; return; } while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
};

inline const char *body_sheet_path (int gender) { return gender ? "/images/body/sprite/woman_sprite.png" : "/images/body/sprite/man_sprite.png"; }

} // namespace taatu
#endif
#endif
