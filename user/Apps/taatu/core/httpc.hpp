//
// httpc.hpp -- a minimal portable HTTP/1.1 client over ITransport (one request per
// connection, Connection: close). Used for the REST auth flow (csrf, login, mfa-verify).
// Captures the status, the body, and the first Set-Cookie (the server's CSRF session). No
// gzip (we never offer it), handles Content-Length and chunked. No STL.
//
#ifndef TAATU_HTTPC_HPP
#define TAATU_HTTPC_HPP
#include "transport.hpp"
#include "buf.hpp"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace taatu {

struct HttpResp
{
    int status;
    Buf body;
    char cookie[1024];       // the first Set-Cookie value, up to ';' (name=value)
    HttpResp () : status (0) { cookie[0] = 0; }
};

// A tiny sleep hook (reuses the WS one, defined by each platform).
void httpc_sleep_ms (unsigned ms);

inline int httpc_find_crlfcrlf (const char *b, int n)
{
    for (int k = 0; k + 3 < n; k++)
        if (b[k] == '\r' && b[k + 1] == '\n' && b[k + 2] == '\r' && b[k + 3] == '\n') return k;
    return -1;
}
// case-insensitive header value (into out). Returns true if found. `block` is the header text.
inline bool httpc_header (const char *b, int he, const char *name, char *out, int cap)
{
    int nl = (int) strlen (name);
    for (int k = 0; k + nl < he; k++)
    {
        if (k && b[k - 1] != '\n') continue;            // start of a header line
        bool m = true;
        for (int j = 0; j < nl; j++) { char c = b[k + j]; if (c >= 'A' && c <= 'Z') c += 32; char d = name[j]; if (d >= 'A' && d <= 'Z') d += 32; if (c != d) { m = false; break; } }
        if (!m) continue;
        int p = k + nl; if (p < he && b[p] == ':') p++;
        while (p < he && b[p] == ' ') p++;
        int i = 0; while (p < he && b[p] != '\r' && b[p] != '\n' && i + 1 < cap) out[i++] = b[p++];
        out[i] = 0; return true;
    }
    if (cap) out[0] = 0;
    return false;
}

// collect every Set-Cookie's "name=value" (up to ';') into out, '\n'-separated.
inline void httpc_all_cookies (const char *b, int he, char *out, int cap)
{
    out[0] = 0; int o = 0;
    for (int k = 0; k + 11 < he; k++)
    {
        if (k && b[k - 1] != '\n') continue;
        bool m = true; const char *name = "set-cookie:";
        for (int j = 0; j < 11; j++) { char c = b[k + j]; if (c >= 'A' && c <= 'Z') c += 32; if (c != name[j]) { m = false; break; } }
        if (!m) continue;
        int p = k + 11; while (p < he && b[p] == ' ') p++;
        if (o && o + 1 < cap) out[o++] = '\n';
        while (p < he && b[p] != ';' && b[p] != '\r' && b[p] != '\n' && o + 1 < cap) out[o++] = b[p++];
        out[o] = 0;
    }
}

// Perform one request. `tp` must be freshly made (unconnected). Returns true on a complete
// HTTP response (out.status/body/cookie filled), false on a transport failure.
inline bool http_request (ITransport &tp, const char *host, int port, const char *method,
                          const char *path, const char *extraHeaders, const char *body, int bodyLen,
                          HttpResp &out)
{
    if (!tp.connect (host, port)) return false;

    Buf req;
    req.add (method); req.addc (' '); req.add (path); req.add (" HTTP/1.1\r\n");
    req.add ("Host: "); req.add (host);
    if (!((port == 80) || (port == 443))) { req.addc (':'); req.addu ((unsigned) port); }
    req.add ("\r\n");
    req.add ("Accept: application/json\r\n");
    req.add ("User-Agent: TaatuOnyx/0.1\r\n");
    if (extraHeaders && extraHeaders[0]) req.add (extraHeaders);     // each line ends with \r\n
    if (body && bodyLen > 0) { req.add ("Content-Length: "); req.addu ((unsigned) bodyLen); req.add ("\r\n"); }
    req.add ("Connection: close\r\n\r\n");
    if (body && bodyLen > 0) req.add (body, bodyLen);
    if (tp.send (req.p, req.n) < 0) { tp.close (); return false; }

    Buf raw; char tmp[2048];
    int he = -1; long long want = -1; bool chunked = false; unsigned idle = 0;
    for (;;)
    {
        int r = tp.recv (tmp, sizeof tmp);
        if (r > 0)
        {
            raw.add (tmp, r); idle = 0;
            if (he < 0)
            {
                he = httpc_find_crlfcrlf (raw.p, raw.n);
                if (he >= 0)
                {
                    const char *sp = strchr (raw.p, ' ');
                    out.status = sp ? atoi (sp + 1) : 0;
                    char v[64];
                    if (httpc_header (raw.p, he, "transfer-encoding", v, sizeof v) && strstr (v, "chunked")) chunked = true;
                    if (!chunked && httpc_header (raw.p, he, "content-length", v, sizeof v)) want = atoll (v);
                    httpc_all_cookies (raw.p, he, out.cookie, sizeof out.cookie);  // all Set-Cookie name=value, '\n'-separated
                }
            }
            if (he >= 0)
            {
                int got = raw.n - (he + 4);
                if (want >= 0 && got >= want) break;
                if (chunked && got >= 5 && !memcmp (raw.p + raw.n - 5, "0\r\n\r\n", 5)) break;
            }
        }
        else if (r == 0) { if (++idle > 10000) break; httpc_sleep_ms (1); }
        else break;                 // closed -> response complete (Connection: close)
    }
    tp.close ();
    if (he < 0) { out.status = out.status ? out.status : -1; return out.status > 0; }

    int bstart = he + 4;
    int blen = raw.n > bstart ? raw.n - bstart : 0;
    char *b = raw.p + bstart;
    out.body.clear ();
    if (chunked)
    {
        int r = 0;
        while (r < blen)
        {
            int sz = 0, any = 0;
            while (r < blen) { char c = b[r]; int h = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; if (h < 0) break; sz = sz * 16 + h; r++; any = 1; }
            while (r < blen && b[r] != '\n') r++;
            if (r < blen) r++;
            if (!any || sz == 0) break;
            if (sz > blen - r) sz = blen - r;
            out.body.add (b + r, sz); r += sz;
            if (r < blen && b[r] == '\r') r++;
            if (r < blen && b[r] == '\n') r++;
        }
    }
    else out.body.add (b, want >= 0 && want < blen ? (int) want : blen);
    return true;
}

// FNV-1a 32-bit -> 8 lowercase hex chars (matches the web client's device_fp).
inline void fnv1a_hex8 (const char *s, char out[9])
{
    unsigned h = 0x811c9dc5u;
    for (const unsigned char *p = (const unsigned char *) s; *p; p++)
    {
        h ^= *p;
        h = h + ((h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24));   // h *= 16777619 (mod 2^32)
    }
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < 8; i++) out[i] = H[(h >> ((7 - i) * 4)) & 0xF];
    out[8] = 0;
}

} // namespace taatu
#endif
