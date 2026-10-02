//
// assets.hpp -- Onyx asset cache for the TAATU client. Downloads sprite sheets / room
// backgrounds from taatu.world over TLS (http_request on a fresh OnyxTransport), decodes
// them with img_load_mem (codecs live in wtk/libwtk.a), and caches the decoded 0xAARRGGBB
// pixels as core Rgba. Fetches run on the network thread (blocking HTTP); the GUI thread
// reads the cached sheets (read-only once loaded) to compose avatars.
//
#ifndef TAATU_ASSETS_HPP
#define TAATU_ASSETS_HPP
#include "kapi.h"
#include "img/imgload.hpp"
#include "../../core/sprite.hpp"
#include "../../core/httpc.hpp"
#include "transport_onyx.hpp"
#include <string.h>

namespace taatu {

class AssetCache
{
public:
    enum { MAX = 256 };
    struct Entry { char path[192]; Rgba img; bool loaded; bool tried; };
    Entry e[MAX];
    int n;
    int mtx;                         // kapi mutex guarding the table
    char host[128]; int port; bool tls;

    AssetCache () : n (0), mtx (-1), port (443), tls (true) { host[0] = 0; }
    void init (const char *h, int p, bool t) { cpy (host, sizeof host, h); port = p; tls = t; mtx = kapi_mutex_create (); }

    // Return the decoded sheet for `path`, or 0 if not loaded yet. (GUI thread: read-only.)
    const Rgba *get (const char *path)
    {
        const Rgba *r = 0;
        kapi_mutex_lock (mtx, KAPI_WAIT_FOREVER);
        for (int i = 0; i < n; i++) if (e[i].loaded && !strcmp (e[i].path, path)) { r = &e[i].img; break; }
        kapi_mutex_unlock (mtx);
        return r;
    }

    // Ensure `path` is downloaded + decoded (blocking; call from the net thread). Returns
    // true if available afterwards. Caches failures (tried) to avoid re-hitting a 404.
    bool ensure (const char *path)
    {
        if (!path || !path[0]) return false;
        kapi_mutex_lock (mtx, KAPI_WAIT_FOREVER);
        for (int i = 0; i < n; i++) if (!strcmp (e[i].path, path)) { bool ok = e[i].loaded; kapi_mutex_unlock (mtx); return ok; }
        int idx = n < MAX ? n++ : -1;
        if (idx >= 0) { cpy (e[idx].path, sizeof e[idx].path, path); e[idx].loaded = false; e[idx].tried = true; }
        kapi_mutex_unlock (mtx);
        if (idx < 0) return false;

        OnyxTransport tp (tls);
        HttpResp rp;
        bool ok = http_request (tp, host, port, "GET", path, "", 0, 0, rp);
        if (!ok || rp.status != 200 || rp.body.n <= 0) return false;

        ImgFrames im;
        if (!img_load_mem (rp.body.p, (unsigned) rp.body.n, &im)) return false;
        Rgba tmp; bool got = false;
        if (im.n > 0 && im.px[0] && im.w > 0 && im.h > 0 && tmp.alloc (im.w, im.h))
        { memcpy (tmp.px, im.px[0], (size_t) im.w * im.h * 4); got = true; }
        img_free (&im);
        if (!got) return false;

        kapi_mutex_lock (mtx, KAPI_WAIT_FOREVER);
        e[idx].img.adopt (tmp.px, tmp.w, tmp.h); tmp.px = 0;   // move ownership into the cache
        e[idx].loaded = true;
        kapi_mutex_unlock (mtx);
        return true;
    }

private:
    static void cpy (char *d, int cap, const char *s) { int i = 0; if (!s) { d[0] = 0; return; } while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
};

// the body sheet path for a gender (base skin). (Confirm exact paths on first run.)
inline const char *body_sheet_path (int gender) { return gender ? "/images/body/sprite/woman_sprite.png" : "/images/body/sprite/man_sprite.png"; }

} // namespace taatu
#endif
