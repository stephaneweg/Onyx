//
// main.cpp -- TAATU native client for Onyx (GUI). Reuses the portable core/ (WebSocket +
// Engine.IO + Socket.IO + _ui signing + world model) over OnyxTransport (onyx_tls/kapi).
// The network runs on its own thread; the GUI thread draws and queues actions to it.
//
// Phase 1: connect, join a room, draw avatars (iso dots + pseudos) and the chat log, send
// chat, click-to-move. Sprite compositing + real pathfinding + asset cache come next
// (see ../../DESIGN.md).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "applib.h"
#include "../../core/client.hpp"
#include "../../core/auth.hpp"
#include "transport_onyx.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

using namespace taatu;

// ---- config (override via SD:/apps/taatu.app/config.ini later) --------------------------
static const char *CFG_HOST  = "taatu.world";
static int         CFG_PORT  = 443;
static bool        CFG_TLS   = true;
static const char *CFG_ORIG  = "https://taatu.world";
static int         CFG_ROOM  = 4;
static int         CFG_SX = 27, CFG_SZ = 27;

// ---- window -----------------------------------------------------------------------------
#define WIN_W 1000
#define WIN_H 700
#define ISO_OX 500
#define ISO_OY 120
#define TILE_W 44         // iso tile half-width  (TODO: calibrate to the web client)
#define TILE_H 22         // iso tile half-height

static unsigned *fb;
static TaatuClient g_cli;

// ---- cross-thread guards + queues -------------------------------------------------------
static int   g_world_mtx;      // kapi mutex handle
static volatile int g_dirty = 1;
static volatile int g_net_run = 1;

static void wlock (void *)   { kapi_mutex_lock (g_world_mtx, KAPI_WAIT_FOREVER); }
static void wunlock (void *) { kapi_mutex_unlock (g_world_mtx); }
static unsigned nowms () { return kapi_clock_us () / 1000; }

// a tiny command queue GUI -> net
enum { CMD_CHAT = 1, CMD_MOVE = 2 };
struct Cmd { int kind; int a, b; char text[256]; };
enum { CMDQ = 32 };
static Cmd  g_cmd[CMDQ];
static volatile int g_cmd_head, g_cmd_tail;
static int  g_cmd_mtx;

static void push_cmd (const Cmd &c)
{
    kapi_mutex_lock (g_cmd_mtx, KAPI_WAIT_FOREVER);
    int next = (g_cmd_tail + 1) % CMDQ;
    if (next != g_cmd_head) { g_cmd[g_cmd_tail] = c; g_cmd_tail = next; }
    kapi_mutex_unlock (g_cmd_mtx);
}
static bool pop_cmd (Cmd &out)
{
    bool got = false;
    kapi_mutex_lock (g_cmd_mtx, KAPI_WAIT_FOREVER);
    if (g_cmd_head != g_cmd_tail) { out = g_cmd[g_cmd_head]; g_cmd_head = (g_cmd_head + 1) % CMDQ; got = true; }
    kapi_mutex_unlock (g_cmd_mtx);
    return got;
}

static void mark_dirty (void *) { g_dirty = 1; }

// ---- the network thread -----------------------------------------------------------------
static int net_thread (void *)
{
    // ensure a session token (TAATU_TOKEN, else REST login with TAATU_PSEUDO/PASSWORD)
    char tok[600]; tok[0] = 0;
    const char *envtok = getenv ("TAATU_TOKEN");
    if (envtok) { strncpy (tok, envtok, sizeof tok - 1); tok[sizeof tok - 1] = 0; }
    do_login_if_needed (tok, sizeof tok);
    g_cli.set_token (tok);

    OnyxTransport tp (CFG_TLS);
    if (!g_cli.connect (tp)) { g_dirty = 1; return 1; }
    bool joined = false;
    unsigned last_step = 0;
    while (g_net_run)
    {
        g_cli.poll ();
        if (g_cli.closed ()) break;

        if (g_cli.connected () && !joined)
        {
            g_cli.note_input ();
            g_cli.join_room (CFG_ROOM, CFG_SX, CFG_SZ, 1);
            joined = true;
        }
        // drain GUI commands
        Cmd c;
        while (pop_cmd (c))
        {
            g_cli.note_input ();
            if (c.kind == CMD_CHAT) g_cli.send_chat (c.text);
            else if (c.kind == CMD_MOVE) g_cli.begin_move (c.a, c.b);
        }
        // advance an in-progress move ~ every 230 ms
        unsigned t = nowms ();
        if (t - last_step > 230) { g_cli.step_move (); last_step = t; }

        kapi_msleep (10);
    }
    return 0;
}

// ---- drawing ----------------------------------------------------------------------------
static inline void px (int x, int y, unsigned c) { if ((unsigned) x < WIN_W && (unsigned) y < WIN_H) fb[y * WIN_W + x] = c; }
static void fill (int x0, int y0, int w, int h, unsigned c)
{
    for (int y = y0; y < y0 + h; y++) if ((unsigned) y < WIN_H)
        for (int x = x0; x < x0 + w; x++) if ((unsigned) x < WIN_W) fb[y * WIN_W + x] = c;
}
static void iso (int tx, int tz, int *sx, int *sy)
{
    *sx = ISO_OX + (tx - tz) * TILE_W / 2;
    *sy = ISO_OY + (tx + tz) * TILE_H / 2;
}
// a filled iso diamond centred at tile (tx,tz)
static void diamond (int tx, int tz, unsigned c)
{
    int cx, cy; iso (tx, tz, &cx, &cy);
    for (int dy = -TILE_H / 2; dy <= TILE_H / 2; dy++)
    {
        int span = (TILE_W / 2) * (TILE_H / 2 - (dy < 0 ? -dy : dy)) / (TILE_H / 2);
        for (int dx = -span; dx <= span; dx++) px (cx + dx, cy + dy, c);
    }
}

static char g_input[200]; static int g_input_len = 0;

static void redraw ()
{
    fill (0, 0, WIN_W, WIN_H, 0x00101018);
    // title / status
    const char *st = g_cli.closed () ? "deconnecte" : (g_cli.connected () ? "connecte" : "connexion...");
    char hdr[96]; snprintf (hdr, sizeof hdr, "TAATU  -  %s  -  salle %d", st, g_cli.world.room_id);
    wtk::draw_text (fb, WIN_W, WIN_H, 12, 10, hdr, 0x00E8EEF8, 1, 2);

    kapi_mutex_lock (g_world_mtx, KAPI_WAIT_FOREVER);
    // floor grid (placeholder 30x30)
    for (int x = 0; x < 30; x++) for (int z = 0; z < 30; z++)
        if (((x + z) & 1) == 0) diamond (x, z, 0x001b1b2a);
    // avatars, sorted back-to-front by (x+z)
    int order[MAX_AVATARS], n = 0;
    for (int i = 0; i < g_cli.world.n_av; i++) if (g_cli.world.av[i].used) order[n++] = i;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
        if (g_cli.world.av[order[j]].x + g_cli.world.av[order[j]].z < g_cli.world.av[order[i]].x + g_cli.world.av[order[i]].z)
        { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int k = 0; k < n; k++)
    {
        Avatar &a = g_cli.world.av[order[k]];
        int cx, cy; iso (a.x, a.z, &cx, &cy);
        unsigned col = a.user_id == g_cli.world.self_id ? 0x0070E0FF : (a.gender ? 0x00FF99CC : 0x00FFD080);
        // body as a vertical capsule on the tile
        fill (cx - 6, cy - 34, 12, 30, a.is_absent ? 0x00707080 : col);
        px (cx, cy, 0x00FFFFFF);
        wtk::draw_text (fb, WIN_W, WIN_H, cx - (int) strlen (a.pseudo) * kapi_font_width () / 2, cy - 48, a.pseudo, 0x00FFFFFF, 1, 1);
        if (a.typing) wtk::draw_text (fb, WIN_W, WIN_H, cx - 6, cy - 60, "...", 0x00AAD4FF, 1, 1);
    }
    // chat log (bottom-left)
    int cy = WIN_H - 150;
    for (int i = 0; i < g_cli.world.chat_count && i < 8; i++)
    {
        int idx = g_cli.world.chat_count - 1 - i; if (idx < 0) break;
        const ChatMsg &m = g_cli.world.chat_at (idx);
        char line[300]; snprintf (line, sizeof line, "%s: %s", m.pseudo, m.message);
        wtk::draw_text (fb, WIN_W, WIN_H, 12, cy, line, 0x00D6DEEA, 1, 1);
        cy -= kapi_font_height () + 2;
    }
    kapi_mutex_unlock (g_world_mtx);

    // chat input line
    fill (8, WIN_H - 28, WIN_W - 16, 22, 0x00202838);
    char inp[220]; snprintf (inp, sizeof inp, "> %s", g_input);
    wtk::draw_text (fb, WIN_W, WIN_H, 14, WIN_H - 25, inp, 0x00FFFFFF, 1, 1);
}

// ---- input ------------------------------------------------------------------------------
static void on_key (unsigned long, int ev, long key)
{
    if (ev != GUI_EVENT_KEY) return;
    g_cli.note_input ();
    if (key == KEY_ENTER)
    {
        if (g_input_len > 0) { Cmd c; c.kind = CMD_CHAT; cpy (c.text, sizeof c.text, g_input); push_cmd (c); g_input_len = 0; g_input[0] = 0; }
    }
    else if (key == KEY_BACKSPACE) { if (g_input_len > 0) g_input[--g_input_len] = 0; }
    else if (key >= 32 && key < 127 && g_input_len < (int) sizeof g_input - 1) { g_input[g_input_len++] = (char) key; g_input[g_input_len] = 0; }
    g_dirty = 1;
}
// screen -> tile (inverse iso), then queue a move
static void on_click (unsigned long, int ev, long value)
{
    if (ev != GUI_EVENT_CANVAS_CLICK) return;
    int x = GUI_PTR_X (value), y = GUI_PTR_Y (value);
    int dx = x - ISO_OX, dy = y - ISO_OY;
    int tx = (dx / (TILE_W / 2) + dy / (TILE_H / 2)) / 2;
    int tz = (dy / (TILE_H / 2) - dx / (TILE_W / 2)) / 2;
    if (tx < 0) tx = 0; if (tz < 0) tz = 0;
    g_cli.note_input ();
    Cmd c; c.kind = CMD_MOVE; c.a = tx; c.b = tz; c.text[0] = 0; push_cmd (c);
    g_dirty = 1;
}

// platform hooks for the core
namespace taatu {
    void WsClient::plat_sleep_ms (unsigned ms) { kapi_msleep (ms); }
    void httpc_sleep_ms (unsigned ms) { kapi_msleep (ms); }
}

// transport factory for the REST auth flow (a fresh kapi/TLS socket per request)
static taatu::ITransport *mk_onyx (void *) { return new taatu::OnyxTransport (CFG_TLS); }

// Headless login: if we have no session token but have credentials, run the REST flow.
// (MFA + a proper on-screen login form are a TODO: for now creds come from the environment
// / config, and an MFA challenge is reported but not yet prompted in the GUI.)
static bool do_login_if_needed (char *token_out, int cap)
{
    if (token_out[0]) return true;
    const char *pseudo = getenv ("TAATU_PSEUDO");
    const char *password = getenv ("TAATU_PASSWORD");
    if (!pseudo || !pseudo[0] || !password) return false;
    TaatuAuth auth;
    auth.set_target (CFG_HOST, CFG_PORT);
    auth.mk = mk_onyx;
    char uuid[37]; TaatuAuth::gen_uuid (uuid); strncpy (auth.device_id, uuid, sizeof auth.device_id - 1);
    int w = WIN_W, h = WIN_H; kapi_screen_size (&w, &h);
    auth.set_fingerprint ("TaatuOnyx/0.1 (rpi4)", "fr", "onyx", "Europe/Brussels", w, h, 32, 4, 0, 0);
    int r = auth.login (pseudo, password, true);
    if (r == LOGIN_OK) { strncpy (token_out, auth.token, cap - 1); token_out[cap - 1] = 0; return true; }
    return false;   // MFA / failure: GUI login form is the next step
}

int main (void)
{
    if (KT->version < 67) { /* threads needed */ }
    fb = kapi_create_window (WIN_W, WIN_H, "TAATU");
    if (!fb) return 1;
    wtk::wk_decorate_window ();

    g_world_mtx = kapi_mutex_create ();
    g_cmd_mtx = kapi_mutex_create ();

    g_cli.now_ms = nowms;
    g_cli.lock_fn = wlock; g_cli.unlock_fn = wunlock; g_cli.lock_ctx = 0;
    g_cli.on_change = mark_dirty; g_cli.change_ctx = 0;
    g_cli.set_target (CFG_HOST, CFG_PORT, CFG_TLS, CFG_ORIG);
    // the session token is obtained on the net thread: TAATU_TOKEN, else a REST login with
    // TAATU_PSEUDO/TAATU_PASSWORD (a proper on-screen login form + config.ini is the next step)

    kapi_set_key_handler (on_key);
    kapi_set_click_handler (on_click);

    int tid = kapi_thread_create (net_thread, 0, 512 * 1024, "taatu-net");
    (void) tid;

    while (!should_exit ())
    {
        kapi_pump_wait (16);
        if (g_dirty) { g_dirty = 0; redraw (); present (); }
    }
    g_net_run = 0;
    kapi_msleep (50);
    return 0;
}
