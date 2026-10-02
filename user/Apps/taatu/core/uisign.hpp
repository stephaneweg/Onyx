//
// uisign.hpp -- the TAATU anti-bot "proof of UI" signer. The server sends an HMAC-SHA256
// session key via the 'ui-key' event ({k:"<64 hex>"}). Signed emits carry
//   _ui = { c, ti, t }   where   t = HMAC_SHA256(key, event + "|" + c + "|" + ti)  (hex)
// c is a strictly increasing per-connection counter (reset to 0 on each ui-key); ti is the
// "trusted human input" flag (1 for automatic keepalives; for input-driven actions, 1 when
// a real recent input backs it, else 0). On a native client every click/key IS real input.
//
#ifndef TAATU_UISIGN_HPP
#define TAATU_UISIGN_HPP
#include "crypto.hpp"
#include "buf.hpp"
#include <string.h>
#include <stdio.h>

namespace taatu {

class UiSigner
{
    unsigned char key[32];
    bool have;
    unsigned counter;
public:
    UiSigner () : have (false), counter (0) { memset (key, 0, sizeof key); }

    bool ready () const { return have; }

    // from the 'ui-key' event's hex string (64 chars). Resets the counter.
    bool set_key_hex (const char *hex)
    {
        if (!hex || strlen (hex) < 64) return false;
        if (!hex_decode (hex, 32, key)) return false;
        have = true; counter = 0;
        return true;
    }

    // Is this event one the server expects signed?
    static bool is_signed (const char *event)
    {
        return !strcmp (event, "update-position")    || !strcmp (event, "update-destination")
            || !strcmp (event, "chat-message")       || !strcmp (event, "room-acticon-emote")
            || !strcmp (event, "charlie-guess");
    }

    // Produce the _ui object text for `event` with trusted-input flag ti (0/1).
    // out must hold ~110 bytes. Returns false if no key yet.
    bool make_ui (const char *event, int ti, char *out, int cap)
    {
        if (!have) return false;
        unsigned c = ++counter;
        char msg[160];
        int ml = snprintf (msg, sizeof msg, "%s|%u|%d", event, c, ti ? 1 : 0);
        unsigned char mac[32]; char hexsig[65];
        crypto::hmac_sha256 (key, 32, (const unsigned char *) msg, (size_t) ml, mac);
        hex_encode (mac, 32, hexsig);
        int w = snprintf (out, cap, "{\"c\":%u,\"ti\":%d,\"t\":\"%s\"}", c, ti ? 1 : 0, hexsig);
        return w > 0 && w < cap;
    }
};

} // namespace taatu
#endif
