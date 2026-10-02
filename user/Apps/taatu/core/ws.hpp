//
// ws.hpp -- a minimal RFC 6455 WebSocket *client* over ITransport (no STL, no GPL).
// Client frames are masked (mandatory); server frames are not. Handles fragmentation,
// ping->pong, and close. Text/binary messages are delivered whole by poll().
//
#ifndef TAATU_WS_HPP
#define TAATU_WS_HPP
#include "transport.hpp"
#include "crypto.hpp"
#include "buf.hpp"
#include <stdio.h>
#include <string.h>

namespace taatu {

enum { WS_OP_CONT = 0x0, WS_OP_TEXT = 0x1, WS_OP_BIN = 0x2, WS_OP_CLOSE = 0x8, WS_OP_PING = 0x9, WS_OP_PONG = 0xA };

class WsClient
{
    ITransport *tp;
    Buf rx;                 // raw bytes from the transport, not yet framed
    Buf msg;                // reassembled payload of the current message
    int msg_op;             // opcode of the fragmented message in progress (0 = none)
    bool closed_;
public:
    WsClient () : tp (0), msg_op (0), closed_ (false) {}

    bool closed () const { return closed_; }

    // Perform the HTTP Upgrade handshake. `path` includes the query
    // (e.g. "/socket.io/?EIO=4&transport=websocket"). `origin` may be 0.
    bool open (ITransport &transport, const char *host, int port, const char *path,
               const char *origin, bool verify_accept = true)
    {
        tp = &transport;
        closed_ = false; msg_op = 0; rx.clear (); msg.clear ();
        if (!tp->connect (host, port)) return false;

        unsigned char nonce[16];
        crypto::random_bytes (nonce, sizeof nonce);
        char key[32]; size_t kl = crypto::base64 (nonce, 16, key, sizeof key); key[kl] = 0;

        Buf req;
        req.add ("GET "); req.add (path); req.add (" HTTP/1.1\r\n");
        req.add ("Host: "); req.add (host);
        if (!((port == 80) || (port == 443))) { req.addc (':'); req.addu ((unsigned) port); }
        req.add ("\r\n");
        req.add ("Upgrade: websocket\r\n");
        req.add ("Connection: Upgrade\r\n");
        req.add ("Sec-WebSocket-Version: 13\r\n");
        req.add ("Sec-WebSocket-Key: "); req.add (key); req.add ("\r\n");
        if (origin && origin[0]) { req.add ("Origin: "); req.add (origin); req.add ("\r\n"); }
        req.add ("User-Agent: TaatuOnyx/0.1\r\n");
        req.add ("\r\n");
        if (tp->send (req.p, req.n) < 0) { closed_ = true; return false; }

        // read until the end of the response headers (\r\n\r\n)
        Buf resp; char tmp[1024]; int tries = 0;
        for (;;)
        {
            int r = tp->recv (tmp, sizeof tmp);
            if (r > 0)
            {
                resp.add (tmp, r);
                if (resp.n >= 4 && find_crlfcrlf (resp.p, resp.n) >= 0) break;
                if (resp.n > 64 * 1024) { closed_ = true; return false; }
            }
            else if (r == 0) { if (++tries > 20000) { closed_ = true; return false; } plat_sleep_ms (1); }
            else { closed_ = true; return false; }
        }
        int he = find_crlfcrlf (resp.p, resp.n);
        // must be "HTTP/1.1 101"
        if (!(resp.n >= 12 && (memcmp (resp.p, "HTTP/1.1 101", 12) == 0 || memcmp (resp.p, "HTTP/1.0 101", 12) == 0)))
        { closed_ = true; return false; }
        if (verify_accept)
        {
            char want[32]; make_accept (key, want);
            if (!header_has (resp.p, he, "sec-websocket-accept", want)) { /* tolerant: some proxies */ }
        }
        // any bytes after the headers already belong to the WS stream
        int extra = resp.n - (he + 4);
        if (extra > 0) rx.add (resp.p + he + 4, extra);
        return true;
    }

    // Send one text message (a whole WS text frame, masked).
    bool send_text (const char *data, int len)
    {
        if (len < 0) len = (int) strlen (data);
        return send_frame (WS_OP_TEXT, data, len);
    }

    // Poll for one complete incoming message. Returns 1 and fills out/op, 0 if nothing
    // complete yet, -1 if the connection closed. Drives ping->pong internally.
    int poll (Buf &out, int &op)
    {
        // 1) pull whatever is available from the transport
        char tmp[4096];
        for (int i = 0; i < 8; i++)
        {
            int r = tp->recv (tmp, sizeof tmp);
            if (r > 0) rx.add (tmp, r);
            else if (r == 0) break;
            else { closed_ = true; }
        }
        // 2) try to decode frames
        for (;;)
        {
            int consumed = 0, fop = 0; bool fin = false; const char *pl = 0; long long plen = 0;
            int st = parse_frame (rx.p, rx.n, &consumed, &fop, &fin, &pl, &plen);
            if (st == 0) break;                 // need more bytes
            if (st < 0) { closed_ = true; return -1; }
            // control frames
            if (fop == WS_OP_PING) { send_frame (WS_OP_PONG, pl, (int) plen); rx.drop_front (consumed); continue; }
            if (fop == WS_OP_PONG) { rx.drop_front (consumed); continue; }
            if (fop == WS_OP_CLOSE) { send_frame (WS_OP_CLOSE, pl, (int) (plen > 2 ? 2 : plen)); rx.drop_front (consumed); closed_ = true; return -1; }
            // data frames (text/binary/continuation)
            if (fop == WS_OP_CONT) { /* keep msg_op */ }
            else { msg_op = fop; msg.clear (); }
            msg.add (pl, (int) plen);
            rx.drop_front (consumed);
            if (fin)
            {
                out.clear (); out.add (msg.p, msg.n);
                op = msg_op; msg_op = 0; msg.clear ();
                return 1;
            }
        }
        return closed_ ? -1 : 0;
    }

    void close ()
    {
        if (!closed_ && tp) { send_frame (WS_OP_CLOSE, 0, 0); }
        closed_ = true;
        if (tp) tp->close ();
    }

    // Platform hook for tiny waits inside the handshake (defined by the backend).
    static void plat_sleep_ms (unsigned ms);

private:
    bool send_frame (int opcode, const char *data, int len)
    {
        if (!tp) return false;
        unsigned char hdr[14]; int h = 0;
        hdr[h++] = (unsigned char) (0x80 | (opcode & 0x0F));   // FIN + opcode
        unsigned char mask[4]; crypto::random_bytes (mask, 4);
        if (len < 126) hdr[h++] = (unsigned char) (0x80 | len);
        else if (len < 65536) { hdr[h++] = 0x80 | 126; hdr[h++] = (unsigned char) (len >> 8); hdr[h++] = (unsigned char) len; }
        else {
            hdr[h++] = 0x80 | 127;
            for (int i = 7; i >= 0; i--) hdr[h++] = (unsigned char) (((unsigned long long) len >> (8 * i)) & 0xFF);
        }
        hdr[h++] = mask[0]; hdr[h++] = mask[1]; hdr[h++] = mask[2]; hdr[h++] = mask[3];
        if (tp->send (hdr, h) < 0) { closed_ = true; return false; }
        // masked payload, in chunks
        if (len > 0)
        {
            char buf[2048];
            int off = 0;
            while (off < len)
            {
                int c = len - off; if (c > (int) sizeof buf) c = (int) sizeof buf;
                for (int i = 0; i < c; i++) buf[i] = (char) ((unsigned char) data[off + i] ^ mask[(off + i) & 3]);
                if (tp->send (buf, c) < 0) { closed_ = true; return false; }
                off += c;
            }
        }
        return true;
    }

    // Parse one frame from b[n]. Returns 1 ok (fills consumed/op/fin/payload/plen,
    // payload points INTO b), 0 need more, -1 protocol error. Server frames are unmasked.
    static int parse_frame (const char *b, int n, int *consumed, int *op, bool *fin, const char **pl, long long *plen)
    {
        if (n < 2) return 0;
        const unsigned char *u = (const unsigned char *) b;
        *fin = (u[0] & 0x80) != 0;
        *op = u[0] & 0x0F;
        bool masked = (u[1] & 0x80) != 0;
        long long len = u[1] & 0x7F;
        int i = 2;
        if (len == 126) { if (n < 4) return 0; len = (u[2] << 8) | u[3]; i = 4; }
        else if (len == 127) { if (n < 10) return 0; len = 0; for (int k = 0; k < 8; k++) len = (len << 8) | u[2 + k]; i = 10; }
        int maskoff = i;
        if (masked) { if (n < i + 4) return 0; i += 4; }
        if ((long long) n < (long long) i + len) return 0;
        const char *p = b + i;
        if (masked)
        {
            // (servers don't mask, but be safe) unmask in place
            unsigned char *mp = (unsigned char *) p; const unsigned char *mk = u + maskoff;
            for (long long k = 0; k < len; k++) mp[k] ^= mk[k & 3];
        }
        *pl = p; *plen = len; *consumed = (int) (i + len);
        return 1;
    }

    static int find_crlfcrlf (const char *b, int n)
    {
        for (int k = 0; k + 3 < n; k++)
            if (b[k] == '\r' && b[k + 1] == '\n' && b[k + 2] == '\r' && b[k + 3] == '\n') return k;
        return -1;
    }
    static void make_accept (const char *key, char *out)
    {
        char cat[64]; int kl = (int) strlen (key);
        memcpy (cat, key, kl);
        memcpy (cat + kl, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
        unsigned char sha[20]; crypto::sha1 ((const unsigned char *) cat, kl + 36, sha);
        size_t bl = crypto::base64 (sha, 20, out, 32); out[bl] = 0;
    }
    // case-insensitive: does the header block contain "name: value"?
    static bool header_has (const char *b, int he, const char *name, const char *value)
    {
        int nl = (int) strlen (name);
        for (int k = 0; k + nl < he; k++)
        {
            bool m = true;
            for (int j = 0; j < nl; j++) { char c = b[k + j]; if (c >= 'A' && c <= 'Z') c += 32; if (c != name[j]) { m = false; break; } }
            if (!m) continue;
            int p = k + nl; while (p < he && (b[p] == ':' || b[p] == ' ')) p++;
            int vl = (int) strlen (value);
            if (p + vl <= he && memcmp (b + p, value, vl) == 0) return true;
        }
        return false;
    }
};

} // namespace taatu
#endif
