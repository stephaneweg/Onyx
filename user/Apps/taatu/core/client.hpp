//
// client.hpp -- TaatuClient: ties the Socket.IO layer to the World model and exposes the
// game actions (join room, chat, move). Portable (no STL, no platform calls): the platform
// supplies an ITransport and, when the net runs on its own thread, optional lock hooks that
// guard World reads from the GUI thread.
//
#ifndef TAATU_CLIENT_HPP
#define TAATU_CLIENT_HPP
#include "sio.hpp"
#include "model.hpp"
#include "json.hpp"
#include <string.h>
#include <stdio.h>

namespace taatu {

enum { TAATU_PATH_MAX = 128 };

class TaatuClient
{
public:
    World world;
    SioClient sio;

    // connection
    char host[128], path[64], origin[128], token[512], version[32];
    int  port; bool tls;

    // threading guard (optional; set by the platform net backend)
    void (*lock_fn) (void *); void (*unlock_fn) (void *); void *lock_ctx;
    // notify the UI that world changed (optional)
    void (*on_change) (void *); void *change_ctx;
    // trusted-input timestamp hook (ms clock) + last real input time, for _ui ti.
    unsigned (*now_ms) (void); unsigned last_input_ms;

    // movement in progress
    int path_x[TAATU_PATH_MAX], path_z[TAATU_PATH_MAX], path_len, path_idx;

    TaatuClient () : port (443), tls (true), lock_fn (0), unlock_fn (0), lock_ctx (0),
        on_change (0), change_ctx (0), now_ms (0), last_input_ms (0), path_len (0), path_idx (0)
    { host[0] = origin[0] = token[0] = 0; strcpy (path, "/socket.io/"); strcpy (version, "0.1-onyx"); }

    void set_target (const char *h, int p, bool use_tls, const char *orig)
    { cpy (host, sizeof host, h); port = p; tls = use_tls; cpy (origin, sizeof origin, orig ? orig : ""); }
    void set_token (const char *t) { cpy (token, sizeof token, t); }

    bool connect (ITransport &tp)
    {
        char auth[700];
        Buf tok; json_escape (tok, token);
        snprintf (auth, sizeof auth, "{\"token\":\"%s\",\"v\":\"%s\",\"ui\":1}", tok.p ? tok.p : "", version);
        sio.set_handlers (this, &s_event, &s_ack, &s_state);
        return sio.open (tp, host, port, path, origin[0] ? origin : 0, auth);
    }

    void poll () { sio.poll (); }
    bool connected () const { return sio.connected (); }
    bool closed () const { return sio.closed (); }

    void note_input () { if (now_ms) last_input_ms = now_ms (); }

    // ---- actions --------------------------------------------------------------
    int join_room (int roomId, int x, int z, int dir, bool appart = false)
    {
        world.room_id = roomId; world.is_appart = appart;
        char p[160];
        snprintf (p, sizeof p, "{\"roomId\":%d,\"absent\":false,\"in_game\":false,\"isAppart\":%s}", roomId, appart ? "true" : "false");
        sio.emit ("set-absent", p);
        snprintf (p, sizeof p, "{\"roomId\":%d,\"isAppart\":%s,\"position_x\":%d,\"position_z\":%d,\"direction\":%d}",
                  roomId, appart ? "true" : "false", x, z, dir);
        return sio.emit_ack ("join-room", p);
    }
    void leave_room ()
    {
        char p[80]; snprintf (p, sizeof p, "{\"roomId\":%d,\"isAppart\":%s}", world.room_id, world.is_appart ? "true" : "false");
        sio.emit ("leave-room", p);
    }
    void send_chat (const char *msg, const char *colorHex = "#000000")
    {
        Buf e; json_escape (e, msg);
        char p[512];
        snprintf (p, sizeof p, "{\"roomId\":%d,\"message\":\"%s\",\"isAppart\":%s,\"textColor\":\"%s\",\"entryStyle\":\"basic\",\"entry_style\":\"basic\"}",
                  world.room_id, e.p ? e.p : "", world.is_appart ? "true" : "false", colorHex);
        sio.emit ("chat-message", p, ti_for ("chat-message"));
    }
    void set_typing (bool on)
    {
        char p[80]; snprintf (p, sizeof p, "{\"roomId\":%d,\"typing\":%s}", world.room_id, on ? "true" : "false");
        sio.emit ("player-typing", p);
    }
    void update_position (int x, int z, int dir, bool dancing = false, bool sitting = false, bool force = false)
    {
        char p[300];
        snprintf (p, sizeof p, "{\"roomId\":%d,\"position_x\":%d,\"position_z\":%d,\"direction\":%d,\"is_dancing\":%s,\"is_sitting\":%s,\"in_bed_slot\":0,\"head_pointing\":0,%s\"isAppart\":%s}",
                  world.room_id, x, z, dir, dancing ? "true" : "false", sitting ? "true" : "false",
                  force ? "\"force\":true," : "", world.is_appart ? "true" : "false");
        sio.emit ("update-position", p, ti_for ("update-position"));
    }

    // Begin a click-to-move: build a greedy iso path to (dx,dz), send update-destination,
    // then the platform calls step_move() on a timer to emit per-tile update-position.
    // NOTE: real pathfinding needs the room's walkable grid (a later phase). This greedy
    // diagonal-then-straight path matches the shape seen on the wire for open floors.
    void begin_move (int dx, int dz)
    {
        Avatar *me = world.find (world.self_id);
        int sx = me ? me->x : 0, sz = me ? me->z : 0;
        path_len = build_path (sx, sz, dx, dz, path_x, path_z, TAATU_PATH_MAX);
        path_idx = 0;
        // update-destination with the full path
        Buf p; p.add ("{\"roomId\":"); p.addu ((unsigned) world.room_id);
        p.add (",\"destination_x\":"); p.addu ((unsigned) dx);
        p.add (",\"destination_z\":"); p.addu ((unsigned) dz);
        p.add (",\"path\":[");
        for (int i = 0; i < path_len; i++) { if (i) p.addc (','); p.addc ('['); p.addu ((unsigned) path_x[i]); p.addc (','); p.addu ((unsigned) path_z[i]); p.addc (']'); }
        p.add ("],\"isAppart\":"); p.add (world.is_appart ? "true" : "false"); p.addc ('}');
        sio.emit ("update-destination", p.p, ti_for ("update-destination"));
    }
    // Emit the next tile of the current path (call on a ~200 ms timer). Returns false when done.
    bool step_move ()
    {
        if (path_idx >= path_len) return false;
        int x = path_x[path_idx], z = path_z[path_idx];
        int dir = 0;
        if (path_idx > 0) dir = dir_from (path_x[path_idx - 1], path_z[path_idx - 1], x, z);
        bool last = (path_idx == path_len - 1);
        update_position (x, z, dir, false, false, last);
        Avatar *me = world.find (world.self_id); if (me) { me->x = x; me->z = z; me->direction = dir; }
        path_idx++;
        return !last;
    }

    // ---- static helpers -------------------------------------------------------
    static int build_path (int sx, int sz, int dx, int dz, int *px, int *pz, int cap)
    {
        int n = 0; int x = sx, z = sz;
        px[n] = x; pz[n] = z; n++;
        while ((x != dx || z != dz) && n < cap)
        {
            if (x < dx) x++; else if (x > dx) x--;
            if (z < dz) z++; else if (z > dz) z--;
            px[n] = x; pz[n] = z; n++;
        }
        return n;
    }
    // 8-way iso direction from (ax,az)->(bx,bz). (Mapping to confirm vs the web.)
    static int dir_from (int ax, int az, int bx, int bz)
    {
        int ddx = (bx > ax) - (bx < ax);
        int ddz = (bz > az) - (bz < az);
        // 0..7 clockwise; placeholder mapping
        static const int M[3][3] = { { 0, 1, 2 }, { 7, 0, 3 }, { 6, 5, 4 } };
        return M[ddz + 1][ddx + 1];
    }
    static void json_escape (Buf &out, const char *s)
    {
        out.clear ();
        for (const char *p = s; p && *p; p++)
        {
            unsigned char c = (unsigned char) *p;
            if (c == '"' || c == '\\') { out.addc ('\\'); out.addc ((char) c); }
            else if (c == '\n') out.add ("\\n");
            else if (c == '\r') out.add ("\\r");
            else if (c == '\t') out.add ("\\t");
            else if (c < 0x20) { char b[8]; snprintf (b, sizeof b, "\\u%04x", c); out.add (b); }
            else out.addc ((char) c);
        }
    }

private:
    int ti_for (const char *event)
    {
        // automatic keepalives -> 1; input-driven -> 1 if a real input is recent.
        if (!now_ms || !last_input_ms) return 1;
        unsigned win = strcmp (event, "update-position") == 0 ? 20000 : 3000;
        unsigned dt = now_ms () - last_input_ms;
        return dt < win ? 1 : 0;
    }

    void lock ()   { if (lock_fn) lock_fn (lock_ctx); }
    void unlock () { if (unlock_fn) unlock_fn (lock_ctx); }
    void changed () { if (on_change) on_change (change_ctx); }

    // ---- event dispatch -------------------------------------------------------
    static void s_event (void *ctx, const char *name, const char *args) { ((TaatuClient *) ctx)->on_event (name, args); }
    static void s_ack   (void *ctx, int id, const char *args)           { ((TaatuClient *) ctx)->on_ack (id, args); }
    static void s_state (void *ctx, int st)                             { ((TaatuClient *) ctx)->on_state (st); }

    void on_state (int st)
    {
        if (st == SIO_CONNECTED)
        {
            // (re)join the current room if we have one
        }
        changed ();
    }
    void on_ack (int /*id*/, const char *args)
    {
        // join-room ack: {ok, position_x, position_z, user_id}
        json::Doc d; if (!d.parse (args, (unsigned long) strlen (args), json::TOLERANT)) return;
        const json::Value &a = d.root ()[0];
        if (a.has ("user_id")) { lock (); world.self_id = a["user_id"].asLong (world.self_id);
            Avatar *me = world.find_or_add (world.self_id); if (me) { me->x = a["position_x"].asInt (me->x); me->z = a["position_z"].asInt (me->z); } unlock (); changed (); }
    }
    void on_event (const char *name, const char *args)
    {
        json::Doc d; if (!d.parse (args, (unsigned long) strlen (args), json::TOLERANT)) return;
        const json::Value &p = d.root ()[1];     // args = ["name", payload]

        if (!strcmp (name, "ui-key")) { sio.ui.set_key_hex (p["k"].asStr ("")); return; }

        lock ();
        if (!strcmp (name, "room-players"))
        {
            world.clear_room ();
            const json::Value &arr = p["players"];
            for (unsigned i = 0; i < arr.size (); i++) world.apply_full (arr[i]);
        }
        else if (!strcmp (name, "room-tick"))
        {
            const json::Value &arr = p["updates"];
            for (unsigned i = 0; i < arr.size (); i++) world.apply_tick (arr[i]);
        }
        else if (!strcmp (name, "chat-message")) world.push_chat (p);
        else if (!strcmp (name, "player-typing")) { Avatar *a = world.find (p["user_id"].asLong (0)); if (a) a->typing = p["typing"].asBool (false); }
        else if (!strcmp (name, "player-absent")) { Avatar *a = world.find (p["user_id"].asLong (0)); if (a) a->is_absent = p["absent"].asBool (false); }
        unlock ();
        changed ();
    }
};

} // namespace taatu
#endif
