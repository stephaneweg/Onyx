//
// sio.hpp -- Engine.IO v4 + Socket.IO v5 over the WebSocket, with the _ui signer.
// Transport is fixed to websocket (we skip polling + upgrade). Engine.IO framing: the first
// char of a text message is the packet type ('0' open, '2' ping -> reply '3' pong, '4'
// message, '1' close). A '4' carries a Socket.IO packet: '0' connect, '2' event, '3' ack,
// '4' error. Default namespace "/".
//
#ifndef TAATU_SIO_HPP
#define TAATU_SIO_HPP
#include "ws.hpp"
#include "uisign.hpp"
#include "buf.hpp"
#include <string.h>
#include <stdio.h>

namespace taatu {

// Called for each Socket.IO event. `name` is the event name; `args` is the whole JSON
// array text "[\"name\",payload,...]" (parse with json::Doc, read [1] for the payload).
typedef void (*SioEventCb) (void *ctx, const char *name, const char *args);
typedef void (*SioAckCb)   (void *ctx, int ackId, const char *args);
typedef void (*SioStateCb) (void *ctx, int state);   // see SIO_* below

enum { SIO_OPEN = 1, SIO_CONNECTED = 2, SIO_CLOSED = 3, SIO_ERROR = 4 };

class SioClient
{
    WsClient ws;
    ITransport *tp;
    Buf authObj;                 // the {auth} object text for '40'
    int state;
    void *ctx; SioEventCb onEvent; SioAckCb onAck; SioStateCb onState;
    unsigned ackSeq;
public:
    UiSigner ui;                 // public: TaatuClient drives ti and reads ready()

    SioClient () : tp (0), state (0), ctx (0), onEvent (0), onAck (0), onState (0), ackSeq (0) {}

    void set_handlers (void *c, SioEventCb ev, SioAckCb ack, SioStateCb st) { ctx = c; onEvent = ev; onAck = ack; onState = st; }
    bool connected () const { return state == SIO_CONNECTED; }
    bool closed () const { return ws.closed (); }

    // Open the WS and begin the Engine.IO/Socket.IO handshake. authObjJson is the connect
    // auth payload, e.g. {"token":"...","v":"...","ui":1}. sioPath e.g. "/socket.io/".
    bool open (ITransport &transport, const char *host, int port, const char *sioPath,
               const char *origin, const char *authObjJson)
    {
        tp = &transport;
        authObj.clear (); authObj.add (authObjJson && authObjJson[0] ? authObjJson : "{}");
        state = 0; ackSeq = 0;
        Buf path; path.add (sioPath && sioPath[0] ? sioPath : "/socket.io/");
        path.add ("?EIO=4&transport=websocket");
        if (!ws.open (transport, host, port, path.p, origin)) return false;
        return true;
    }

    // Pump the connection. Call frequently from the network thread.
    void poll ()
    {
        if (!tp) return;
        Buf m; int op;
        for (;;)
        {
            int r = ws.poll (m, op);
            if (r == 1) handle_eio (m.p, m.n);
            else if (r == 0) break;
            else { fire_state (SIO_CLOSED); break; }
        }
    }

    // Emit an event with a payload OBJECT text (e.g. {"roomId":4}). If the event is signed
    // (or forceSign>=0), a _ui field is injected (ti = forceSign, default 1). Returns true.
    bool emit (const char *event, const char *payloadObj, int forceSign = -1)
    {
        Buf pkt;
        pkt.add ("42[\"");
        pkt.add (event);
        pkt.add ("\",");
        bool sign = forceSign >= 0 || (UiSigner::is_signed (event) && ui.ready ());
        if (sign && ui.ready ())
        {
            int ti = forceSign >= 0 ? forceSign : 1;
            char uiobj[160];
            if (ui.make_ui (event, ti, uiobj, sizeof uiobj))
            {
                // inject "_ui":<uiobj> before the payload object's closing '}'
                inject_ui (pkt, payloadObj, uiobj);
            }
            else pkt.add (payloadObj && payloadObj[0] ? payloadObj : "{}");
        }
        else pkt.add (payloadObj && payloadObj[0] ? payloadObj : "{}");
        pkt.add ("]");
        return ws.send_text (pkt.p, pkt.n);
    }

    // Emit expecting a server ack ('42<id>[...]'); the ack comes back via onAck. id returned.
    int emit_ack (const char *event, const char *payloadObj)
    {
        unsigned id = ++ackSeq;
        Buf pkt; pkt.add ("42"); pkt.addu (id); pkt.add ("[\""); pkt.add (event); pkt.add ("\",");
        pkt.add (payloadObj && payloadObj[0] ? payloadObj : "{}");
        pkt.add ("]");
        ws.send_text (pkt.p, pkt.n);
        return (int) id;
    }

    void close () { ws.close (); }

private:
    void fire_state (int s) { if (s != state || s == SIO_ERROR) { state = s; if (onState) onState (ctx, s); } }

    void handle_eio (const char *s, int n)
    {
        if (n <= 0) return;
        char t = s[0];
        if (t == '0') { // engine.io open -> send socket.io connect with auth
            Buf c; c.add ("40"); c.add (authObj.p, authObj.n);
            ws.send_text (c.p, c.n);
            fire_state (SIO_OPEN);
        }
        else if (t == '2') { ws.send_text ("3", 1); }        // engine ping -> pong
        else if (t == '3') { /* engine pong */ }
        else if (t == '1') { fire_state (SIO_CLOSED); }
        else if (t == '4') handle_sio (s + 1, n - 1);
    }

    void handle_sio (const char *s, int n)
    {
        if (n <= 0) return;
        char t = s[0];
        int i = 1;
        // optional namespace "/foo,"
        if (i < n && s[i] == '/') { while (i < n && s[i] != ',') i++; if (i < n) i++; }
        // optional ack id (digits)
        int ackId = -1;
        if (i < n && s[i] >= '0' && s[i] <= '9') { ackId = 0; while (i < n && s[i] >= '0' && s[i] <= '9') ackId = ackId * 10 + (s[i++] - '0'); }
        if (t == '0') { fire_state (SIO_CONNECTED); return; }     // connect ack ("40{sid}")
        if (t == '1') { fire_state (SIO_CLOSED); return; }
        if (t == '4') { fire_state (SIO_ERROR); return; }
        if (t == '2' || t == '3')
        {
            int br = i; while (br < n && s[br] != '[') br++;
            if (br >= n) return;
            const char *arr = s + br;                             // "[...]" (NUL-terminated by Buf)
            if (t == '3') { if (onAck) onAck (ctx, ackId, arr); return; }
            // event: extract the name (first array string)
            char name[64]; if (!first_string (arr, name, sizeof name)) return;
            if (onEvent) onEvent (ctx, name, arr);
        }
    }

    // insert "_ui":<uiobj> into payloadObj before its final '}'. Handles "{}" (no comma).
    static void inject_ui (Buf &out, const char *payloadObj, const char *uiobj)
    {
        const char *p = (payloadObj && payloadObj[0]) ? payloadObj : "{}";
        int len = (int) strlen (p);
        // find last '}'
        int close = len - 1; while (close >= 0 && p[close] != '}') close--;
        if (close < 0) { out.add (p); return; }
        // is the object empty (only whitespace between '{' and '}')?
        bool empty = true;
        for (int k = 1; k < close; k++) if (p[k] != ' ' && p[k] != '\t' && p[k] != '\n' && p[k] != '\r') { empty = false; break; }
        out.add (p, close);                    // everything up to (not incl.) '}'
        if (!empty) out.addc (',');
        out.add ("\"_ui\":"); out.add (uiobj);
        out.addc ('}');
    }

    // extract the first JSON string of an array "[\"name\",...]" into out.
    static bool first_string (const char *arr, char *out, int cap)
    {
        const char *p = arr; if (*p != '[') return false; p++;
        while (*p == ' ') p++;
        if (*p != '"') return false; p++;
        int i = 0;
        while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; if (i + 1 < cap) out[i++] = *p; p++; }
        out[i] = 0;
        return true;
    }
};

} // namespace taatu
#endif
