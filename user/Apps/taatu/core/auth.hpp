//
// auth.hpp -- the TAATU REST login flow, portable. Mirrors the web client:
//   GET  /api/csrf                 -> { token }            (+ Set-Cookie session)
//   POST /api/auth/login           { pseudo, password }    -> { session_token, user }
//                                   or 401 { mfa_required, challenge_id }
//   POST /api/auth/mfa-verify      { challenge_id, code, remember } -> { session_token, user }
// Every request carries X-Taatu-Device (UUID), X-Taatu-Fp (fnv1a), and, for POST,
// X-CSRF-Token + the captured Cookie. The platform supplies a transport factory (a fresh
// ITransport per request: plain TCP for dev, TLS for prod) and persists the device id.
//
#ifndef TAATU_AUTH_HPP
#define TAATU_AUTH_HPP
#include "httpc.hpp"
#include "crypto.hpp"
#include "json.hpp"
#include "buf.hpp"
#include <string.h>
#include <stdio.h>

namespace taatu {

enum { LOGIN_OK = 0, LOGIN_MFA = 1, LOGIN_FAIL = -1, LOGIN_NET = -2 };

class TaatuAuth
{
public:
    char host[128]; int port;
    char prefix[16];                 // "/api"
    char device_id[40], device_fp[9];
    char csrf[80];
    // cookie jar (PHPSESSID etc.): the server may regenerate the session mid-flow (login ->
    // mfa), so every response's Set-Cookie must be merged and resent, or the MFA challenge
    // ends up in a session we no longer present ("verification expiree").
    struct Cookie { char name[64], val[480]; };
    Cookie jar[8]; int njar;
    // results
    char token[600], challenge_id[160], email_masque[96], err[200];

    ITransport *(*mk) (void *); void *mk_ctx;    // transport factory (fresh, unconnected)

    TaatuAuth () : port (443), njar (0), mk (0), mk_ctx (0)
    { host[0] = csrf[0] = token[0] = challenge_id[0] = email_masque[0] = err[0] = 0; strcpy (prefix, "/api"); strcpy (device_id, ""); strcpy (device_fp, "00000000"); }

    void set_target (const char *h, int p) { cpystr (host, sizeof host, h); port = p; }

    // ---- device identity ------------------------------------------------------
    static void gen_uuid (char out[37])
    {
        unsigned char b[16]; crypto::random_bytes (b, 16);
        b[6] = (b[6] & 0x0f) | 0x40; b[8] = (b[8] & 0x3f) | 0x80;
        static const char *H = "0123456789abcdef";
        int o = 0;
        for (int i = 0; i < 16; i++) { if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-'; out[o++] = H[b[i] >> 4]; out[o++] = H[b[i] & 15]; }
        out[o] = 0;
    }
    // build the stable fingerprint from native traits (same formula as the web client).
    void set_fingerprint (const char *ua, const char *lang, const char *platform, const char *tz,
                          int w, int h, int depth, int cores, int devmem, int touch)
    {
        char traits[320];
        snprintf (traits, sizeof traits, "%s|%s|%s|%s|%s|%dx%dx%d|%d|%d|%d",
                  ua, lang, lang, platform, tz, w, h, depth, cores, devmem, touch);
        fnv1a_hex8 (traits, device_fp);
    }

    // ---- flow -----------------------------------------------------------------
    bool fetch_csrf ()
    {
        err[0] = 0;
        ITransport *tp = mk ? mk (mk_ctx) : 0; if (!tp) { strcpy (err, "no transport"); return false; }
        Buf hdr; common_headers (hdr, false);
        char path[48]; snprintf (path, sizeof path, "%s/csrf", prefix);
        HttpResp rp;
        bool ok = http_request (*tp, host, port, "GET", path, hdr.p ? hdr.p : "", 0, 0, rp);
        delete tp;
        if (!ok) { strcpy (err, "network"); return false; }
        merge_cookies (rp.cookie);
        json::Doc d; if (!d.parse (rp.body.p ? rp.body.p : "", (unsigned long) rp.body.n, json::TOLERANT)) { strcpy (err, "csrf parse"); return false; }
        cpystr (csrf, sizeof csrf, d.root ()["token"].asStr (""));
        return csrf[0] != 0;
    }

    // Returns LOGIN_OK (token filled), LOGIN_MFA (challenge_id filled), LOGIN_FAIL (err),
    // or LOGIN_NET. Fetches a CSRF token first if we don't have one.
    int login (const char *pseudo, const char *password, bool remember)
    {
        if (!csrf[0] && !fetch_csrf ()) return LOGIN_NET;
        Buf body; body.add ("{\"pseudo\":\""); json_escape_into (body, pseudo);
        body.add ("\",\"password\":\""); json_escape_into (body, password);
        body.add (remember ? "\",\"remember\":true}" : "\"}");
        char path[64]; snprintf (path, sizeof path, "%s/auth/login", prefix);
        return post_auth (path, body);
    }
    int mfa_verify (const char *challenge, const char *code, bool remember)
    {
        if (!csrf[0] && !fetch_csrf ()) return LOGIN_NET;
        Buf body; body.add ("{\"challenge_id\":\""); json_escape_into (body, challenge);
        body.add ("\",\"code\":\""); json_escape_into (body, code);
        body.add (remember ? "\",\"remember\":true}" : "\"}");
        char path[64]; snprintf (path, sizeof path, "%s/auth/mfa-verify", prefix);
        return post_auth (path, body);
    }

private:
    static void cpystr (char *d, int cap, const char *s) { int i = 0; if (!s) { d[0] = 0; return; } while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }

    // merge '\n'-separated "name=value" cookies from a response into the jar.
    void merge_cookies (const char *setc)
    {
        if (!setc || !setc[0]) return;
        const char *p = setc;
        while (*p)
        {
            const char *eol = p; while (*eol && *eol != '\n') eol++;
            const char *eq = p; while (eq < eol && *eq != '=') eq++;
            if (eq < eol) {
                char nm[64]; int nl = (int) (eq - p); if (nl > 63) nl = 63; memcpy (nm, p, nl); nm[nl] = 0;
                int vi = -1; for (int i = 0; i < njar; i++) if (!strcmp (jar[i].name, nm)) { vi = i; break; }
                if (vi < 0 && njar < 8) vi = njar++;
                if (vi >= 0) { cpystr (jar[vi].name, sizeof jar[vi].name, nm); int vl = (int) (eol - (eq + 1)); if (vl < 0) vl = 0; if (vl > 479) vl = 479; memcpy (jar[vi].val, eq + 1, vl); jar[vi].val[vl] = 0; }
            }
            p = *eol ? eol + 1 : eol;
        }
    }
    void cookie_header (Buf &h)
    {
        if (!njar) return;
        h.add ("Cookie: ");
        for (int i = 0; i < njar; i++) { if (i) h.add ("; "); h.add (jar[i].name); h.addc ('='); h.add (jar[i].val); }
        h.add ("\r\n");
    }
    void common_headers (Buf &h, bool with_csrf)
    {
        if (device_id[0]) { h.add ("X-Taatu-Device: "); h.add (device_id); h.add ("\r\n"); }
        if (device_fp[0]) { h.add ("X-Taatu-Fp: "); h.add (device_fp); h.add ("\r\n"); }
        cookie_header (h);
        if (with_csrf && csrf[0]) { h.add ("X-CSRF-Token: "); h.add (csrf); h.add ("\r\n"); }
    }

    bool do_post (const char *path, Buf &body, HttpResp &rp)
    {
        rp.body.clear (); rp.status = 0; rp.cookie[0] = 0;
        ITransport *tp = mk ? mk (mk_ctx) : 0; if (!tp) { strcpy (err, "no transport"); return false; }
        Buf hdr; hdr.add ("Content-Type: application/json\r\n"); common_headers (hdr, true);
        bool ok = http_request (*tp, host, port, "POST", path, hdr.p, body.p, body.n, rp);
        delete tp;
        if (ok) merge_cookies (rp.cookie);       // the server may rotate the session here
        return ok;
    }
    int post_auth (const char *path, Buf &body)
    {
        err[0] = token[0] = challenge_id[0] = 0;
        HttpResp rp;
        if (!do_post (path, body, rp)) { strcpy (err, "network"); return LOGIN_NET; }
        if (rp.status == 403) { fetch_csrf (); if (!do_post (path, body, rp)) { strcpy (err, "network"); return LOGIN_NET; } }  // CSRF rotated: refresh + replay

        json::Doc d; d.parse (rp.body.p ? rp.body.p : "", (unsigned long) rp.body.n, json::TOLERANT);
        const json::Value &r = d.root ();
        if (r["session_token"].isStr ()) { cpystr (token, sizeof token, r["session_token"].asStr ("")); return LOGIN_OK; }
        if (r["mfa_required"].isStr () || r["mfa_required"].asBool (false) || r["challenge_id"].isStr ())
        { cpystr (challenge_id, sizeof challenge_id, r["challenge_id"].asStr ("")); cpystr (email_masque, sizeof email_masque, r["email_masque"].asStr ("")); return LOGIN_MFA; }
        cpystr (err, sizeof err, r["error"].asStr (rp.status == 200 ? "unexpected response" : "login failed"));
        return LOGIN_FAIL;
    }
};

} // namespace taatu
#endif
