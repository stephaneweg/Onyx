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
#include "../../core/avatar_render.hpp"
#include "transport_onyx.hpp"
#include "assets.hpp"
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

// ---- room geometry + assets -------------------------------------------------------------
static AssetCache g_assets;
static RoomMap    g_room;
static volatile int g_room_loaded = 0;
static char       g_token[600];

static void ensure_room_bg ()
{
    if (!g_room.bgGame[0]) return;
    char p[160]; snprintf (p, sizeof p, "/images/game/%s", g_room.bgGame);
    g_assets.ensure (p);
}
// fetch /api/rooms/{id} -> file_path -> /src/rooms/{file_path} -> parse geometry (net thread)
static void load_room (int id)
{
    OnyxTransport tp (CFG_TLS);
    char path[48]; snprintf (path, sizeof path, "/api/rooms/%d", id);
    Buf h; if (g_token[0]) { h.add ("X-Session-Token: "); h.add (g_token); h.add ("\r\n"); }
    HttpResp rp;
    if (!http_request (tp, CFG_HOST, CFG_PORT, "GET", path, h.p ? h.p : "", 0, 0, rp) || rp.status != 200) return;
    json::Doc d; d.parse (rp.body.p ? rp.body.p : "", (unsigned long) rp.body.n, json::TOLERANT);
    const char *fp = d.root ()["room"]["file_path"].asStr ("");
    if (!fp[0]) return;
    char src[192]; snprintf (src, sizeof src, "/src/rooms/%s", fp);
    OnyxTransport tp2 (CFG_TLS);
    HttpResp rs;
    if (!http_request (tp2, CFG_HOST, CFG_PORT, "GET", src, "", 0, 0, rs) || rs.status != 200) return;
    kapi_mutex_lock (g_world_mtx, KAPI_WAIT_FOREVER);
    g_room.parse (rs.body.p ? rs.body.p : "", rs.body.n);
    kapi_mutex_unlock (g_world_mtx);
    ensure_room_bg ();
    g_room_loaded = 1; g_dirty = 1;
}
// ensure every present avatar's sheets are downloaded (net thread; sole writer, no lock vs poll)
static void ensure_avatar_assets ()
{
    for (int i = 0; i < g_cli.world.n_av; i++)
    {
        Avatar &a = g_cli.world.av[i]; if (!a.used) continue;
        g_assets.ensure (body_sheet_path (a.gender));
        if (a.eye_sprite_path[0]) g_assets.ensure (a.eye_sprite_path);
        for (int j = 0; j < N_LAYERS; j++) if (a.cloth[j].present && a.cloth[j].sprite_path[0]) g_assets.ensure (a.cloth[j].sprite_path);
    }
}

// ---- the network thread -----------------------------------------------------------------
static int net_thread (void *)
{
    // ensure a session token (TAATU_TOKEN, else REST login with TAATU_PSEUDO/PASSWORD)
    g_token[0] = 0;
    const char *envtok = getenv ("TAATU_TOKEN");
    if (envtok) { strncpy (g_token, envtok, sizeof g_token - 1); g_token[sizeof g_token - 1] = 0; }
    do_login_if_needed (g_token, sizeof g_token);
    g_cli.set_token (g_token);

    OnyxTransport tp (CFG_TLS);
    if (!g_cli.connect (tp)) { g_dirty = 1; return 1; }
    bool joined = false;
    unsigned last_step = 0, last_assets = 0;
    while (g_net_run)
    {
        g_cli.poll ();
        if (g_cli.closed ()) break;

        if (g_cli.connected () && !joined)
        {
            g_cli.note_input ();
            g_cli.join_room (CFG_ROOM, CFG_SX, CFG_SZ, 1);
            load_room (CFG_ROOM);
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
        // keep avatar assets loaded (cheap once cached), a few times a second
        if (joined && t - last_assets > 500) { ensure_avatar_assets (); last_assets = t; g_dirty = 1; }

        kapi_msleep (10);
    }
    return 0;
}

// ---- room/avatar drawing (GUI thread) ---------------------------------------------------
#define ROOM_OX 0
#define ROOM_OY 36
// cloth index (model.hpp Layer) -> render layer + whether it is a head item (col 6)
static const struct { int cloth, rl; bool head; } CLOTHMAP[N_LAYERS] = {
    { L_BOTTOM, RL_BOTTOM, false }, { L_TOP, RL_TOP, false }, { L_SHOES, RL_SHOES, false },
    { L_BEARD, RL_BEARD, true }, { L_HAIR, RL_HAIR, true }, { L_GLASSES, RL_GLASSES, true }, { L_HAT, RL_HAT, true } };

// alpha-over an Rgba onto the (opaque 0x00RRGGBB) framebuffer at (dx,dy).
static void blit_rgba_fb (int dx, int dy, const Rgba &img)
{
    for (int y = 0; y < img.h; y++)
    {
        int ty = dy + y; if ((unsigned) ty >= (unsigned) WIN_H) continue;
        for (int x = 0; x < img.w; x++)
        {
            int tx = dx + x; if ((unsigned) tx >= (unsigned) WIN_W) continue;
            unsigned s = img.px[y * img.w + x]; unsigned a = s >> 24; if (!a) continue;
            unsigned *dpx = &fb[ty * WIN_W + tx];
            *dpx = blend_px (*dpx | 0xFF000000u, s) & 0x00FFFFFFu;
        }
    }
}
static void build_layers (const Avatar &a, LayerSrc L[RL_COUNT])
{
    for (int i = 0; i < RL_COUNT; i++) L[i] = LayerSrc ();
    const Rgba *body = g_assets.get (body_sheet_path (a.gender));
    unsigned skin = parse_hex_color (a.skin_tone_hex);
    L[RL_BODY].sheet = body; L[RL_BODY].present = body != 0; L[RL_BODY].tint = skin;                    // headless body (frame col)
    L[RL_HEAD].sheet = body; L[RL_HEAD].present = body != 0; L[RL_HEAD].tint = skin; L[RL_HEAD].head = true;  // bare head (col 6)
    if (a.eye_sprite_path[0]) { L[RL_EYES].sheet = g_assets.get (a.eye_sprite_path); L[RL_EYES].present = L[RL_EYES].sheet != 0; L[RL_EYES].head = true; }
    for (int j = 0; j < N_LAYERS; j++)
    {
        const ClothingItem &ci = a.cloth[CLOTHMAP[j].cloth]; if (!ci.present || !ci.sprite_path[0]) continue;
        int rl = CLOTHMAP[j].rl;
        L[rl].sheet = g_assets.get (ci.sprite_path); L[rl].present = L[rl].sheet != 0; L[rl].head = CLOTHMAP[j].head;
        L[rl].tint = (ci.color_editable && ci.color_hex[0]) ? parse_hex_color (ci.color_hex) : 0;
    }
}
// returns true if a composed sprite was drawn (else the caller draws the placeholder)
static bool draw_avatar_sprite (const Avatar &a, int screenX, int screenY)
{
    LayerSrc L[RL_COUNT]; build_layers (a, L);
    if (!L[RL_BODY].present) return false;        // body not downloaded yet
    Rgba frame; compose_avatar (frame, L, a.direction, 0);
    blit_rgba_fb (screenX - AV_CELL_W / 2, screenY - AV_CELL_H + 12, frame);   // feet ~12px up from cell bottom
    return true;
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
    // room background (downloaded) or a placeholder grid
    const Rgba *bg = 0;
    if (g_room_loaded && g_room.bgGame[0]) { char bp[160]; snprintf (bp, sizeof bp, "/images/game/%s", g_room.bgGame); bg = g_assets.get (bp); }
    if (bg) blit_rgba_fb (ROOM_OX, ROOM_OY, *bg);
    else for (int x = 0; x < 30; x++) for (int z = 0; z < 30; z++) if (((x + z) & 1) == 0) diamond (x, z, 0x001b1b2a);
    // avatars, sorted back-to-front by (x+z)
    int order[MAX_AVATARS], n = 0;
    for (int i = 0; i < g_cli.world.n_av; i++) if (g_cli.world.av[i].used) order[n++] = i;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
        if (g_cli.world.av[order[j]].x + g_cli.world.av[order[j]].z < g_cli.world.av[order[i]].x + g_cli.world.av[order[i]].z)
        { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int k = 0; k < n; k++)
    {
        Avatar &a = g_cli.world.av[order[k]];
        int cx, cy;
        if (g_room_loaded) { g_room.iso (a.x, a.z, &cx, &cy); cx += ROOM_OX; cy += ROOM_OY; }
        else iso (a.x, a.z, &cx, &cy);
        if (!draw_avatar_sprite (a, cx, cy))
        {
            unsigned col = a.user_id == g_cli.world.self_id ? 0x0070E0FF : (a.gender ? 0x00FF99CC : 0x00FFD080);
            fill (cx - 6, cy - 34, 12, 30, a.is_absent ? 0x00707080 : col);
            px (cx, cy, 0x00FFFFFF);
        }
        wtk::draw_text (fb, WIN_W, WIN_H, cx - (int) strlen (a.pseudo) * kapi_font_width () / 2, cy - AV_CELL_H + 2, a.pseudo, 0x00FFFFFF, 1, 1);
        if (a.typing) wtk::draw_text (fb, WIN_W, WIN_H, cx - 6, cy - AV_CELL_H - 10, "...", 0x00AAD4FF, 1, 1);
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
    int tx, tz;
    if (g_room_loaded) g_room.unproject (x - ROOM_OX, y - ROOM_OY, &tx, &tz);
    else { int dx = x - ISO_OX, dy = y - ISO_OY; tx = (dx / (TILE_W / 2) + dy / (TILE_H / 2)) / 2; tz = (dy / (TILE_H / 2) - dx / (TILE_W / 2)) / 2; }
    if (tx < 0) tx = 0; if (tz < 0) tz = 0;
    if (g_room_loaded && !g_room.walkable (tx, tz)) return;    // don't walk into blocked tiles
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
    g_assets.init (CFG_HOST, CFG_PORT, CFG_TLS);

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
