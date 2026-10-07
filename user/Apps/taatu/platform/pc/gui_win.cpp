//
// gui_win.cpp -- a Win32 GUI build of the TAATU client, to SEE the rendering on the PC
// (same portable core + renderer that the Onyx app uses; GDI+ decodes PNG, a DIB is the
// framebuffer). Modes:
//   --demo               render room 4 (Patio) + a sample avatar, no login (public assets)
//   --login              real login + join (prompts), live room
//   --shot <file.png> N  render for N s then save a screenshot PNG and exit (for calibration)
//
#include "../../core/client.hpp"
#include "../../core/auth.hpp"
#include "../../core/avatar_render.hpp"
#include "../../core/roommap.hpp"
#include "transport_tcp.hpp"
#include "tls_win.hpp"
#include "assets_pc.hpp"
#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <string.h>

using namespace taatu;

// ---- framebuffer + config --------------------------------------------------------------
#define WIN_W 1000
#define WIN_H 700


static unsigned *fb;
static int g_ox = 0, g_oy = 40;                 // where the room background's top-left sits
static int g_tw = -1, g_th = -1, g_orgx = -1, g_orgy = -1;   // projection overrides (-1 = from room)
static bool g_swap = false;                     // treat grid as [x][z] (engine uses gridZ,gridX)
static BITMAPINFO g_bmi;
static TaatuClient g_cli;
static PcAssetCache g_assets;
static RoomMap g_room;
static volatile LONG g_room_loaded = 0;
static bool g_flipx = false, g_flipy = false;
static void proj (int x, int z, int *sx, int *sy)
{
    int hw = g_room.tileW / 2, hh = g_room.tileH / 2;
    int ex = g_flipx ? (z - x) : (x - z);
    int ey = g_flipy ? (x - z) : (x + z);          // (x+z) normal; flip gives (x-z) for Y
    *sx = g_ox + g_room.originX + ex * hw;
    *sy = g_oy + g_room.originY + ey * hh;
}
static bool g_tr = false, g_grid = false;
static bool walk (int x, int z) { return g_tr ? g_room.walkable (z, x) : g_room.walkable (x, z); }
static CRITICAL_SECTION g_cs;
static volatile bool g_run = true;
static bool g_demo = false, g_tls = true;
static char g_host[128] = "taatu.world"; static int g_port = 443; static int g_roomId = 4;
static char g_shot[260] = ""; static int g_shotSec = 0;
static char g_input[200]; static int g_input_len = 0;

static void wlock (void *) { EnterCriticalSection (&g_cs); }
static void wunlock (void *) { LeaveCriticalSection (&g_cs); }
static unsigned now_ms () { return (unsigned) GetTickCount (); }

namespace taatu {
    void WsClient::plat_sleep_ms (unsigned ms) { Sleep (ms); }
    void PcTcpTransport::plat_nap () { Sleep (1); }
    void httpc_sleep_ms (unsigned ms) { Sleep (ms); }
}
static ITransport *mk_auto (void *) { return g_tls ? (ITransport *) new WinTlsTransport () : (ITransport *) new PcTcpTransport (); }

static const char *arg (int c, char **v, const char *k, const char *d) { for (int i = 1; i + 1 < c; i++) if (!strcmp (v[i], k)) return v[i + 1]; return d; }
static bool flag (int c, char **v, const char *k) { for (int i = 1; i < c; i++) if (!strcmp (v[i], k)) return true; return false; }

// ---- rendering (shared logic with the Onyx build) --------------------------------------
static inline void fb_fill (int x0, int y0, int w, int h, unsigned c) { for (int y = y0; y < y0 + h; y++) if ((unsigned) y < WIN_H) for (int x = x0; x < x0 + w; x++) if ((unsigned) x < WIN_W) fb[y * WIN_W + x] = c; }
static void blit_rgba_fb (int dx, int dy, const Rgba &img)
{
    for (int y = 0; y < img.h; y++) { int ty = dy + y; if ((unsigned) ty >= (unsigned) WIN_H) continue;
        for (int x = 0; x < img.w; x++) { int tx = dx + x; if ((unsigned) tx >= (unsigned) WIN_W) continue;
            unsigned s = img.px[y * img.w + x]; if (!(s >> 24)) continue;
            unsigned *d = &fb[ty * WIN_W + tx]; *d = blend_px (*d | 0xFF000000u, s) & 0x00FFFFFFu; } }
}
// cloth index (model.hpp Layer) -> render layer + whether it is a head item (col 6)
static const struct { int cloth, rl; bool head; } CLOTHMAP[N_LAYERS] = {
    { L_BOTTOM, RL_BOTTOM, false }, { L_TOP, RL_TOP, false }, { L_SHOES, RL_SHOES, false },
    { L_BEARD, RL_BEARD, true }, { L_HAIR, RL_HAIR, true }, { L_GLASSES, RL_GLASSES, true }, { L_HAT, RL_HAT, true } };
static void build_layers (const Avatar &a, LayerSrc L[RL_COUNT])
{
    for (int i = 0; i < RL_COUNT; i++) L[i] = LayerSrc ();
    const Rgba *body = g_assets.get (body_sheet_path (a.gender));
    unsigned skin = parse_hex_color (a.skin_tone_hex);
    L[RL_BODY].sheet = body; L[RL_BODY].present = body != 0; L[RL_BODY].tint = skin;            // headless body (frame col)
    L[RL_HEAD].sheet = body; L[RL_HEAD].present = body != 0; L[RL_HEAD].tint = skin; L[RL_HEAD].head = true;  // bare head (col 6)
    if (a.eye_sprite_path[0]) { L[RL_EYES].sheet = g_assets.get (a.eye_sprite_path); L[RL_EYES].present = L[RL_EYES].sheet != 0; L[RL_EYES].head = true; }
    for (int j = 0; j < N_LAYERS; j++) { const ClothingItem &ci = a.cloth[CLOTHMAP[j].cloth]; if (!ci.present || !ci.sprite_path[0]) continue;
        int rl = CLOTHMAP[j].rl; L[rl].sheet = g_assets.get (ci.sprite_path); L[rl].present = L[rl].sheet != 0; L[rl].head = CLOTHMAP[j].head;
        L[rl].tint = (ci.color_editable && ci.color_hex[0]) ? parse_hex_color (ci.color_hex) : 0; }
}
static bool draw_avatar_sprite (const Avatar &a, int sx, int sy)
{
    LayerSrc L[RL_COUNT]; build_layers (a, L);
    if (!L[RL_BODY].present) return false;
    Rgba f; compose_avatar (f, L, a.direction, 0);
    blit_rgba_fb (sx - AV_CELL_W / 2, sy - AV_CELL_H + 12, f);
    return true;
}
static void render ()
{
    fb_fill (0, 0, WIN_W, WIN_H, 0x00101018);
    EnterCriticalSection (&g_cs);
    const Rgba *bg = 0;
    if (g_room_loaded && g_room.bgGame[0]) { char p[160]; snprintf (p, sizeof p, "/images/game/%s", g_room.bgGame); bg = g_assets.get (p); }
    if (bg) blit_rgba_fb (g_ox, g_oy, *bg);
    if (g_grid && g_room_loaded)  // calibration overlay
    {
        for (int z = 0; z < g_room.gh; z++) for (int x = 0; x < g_room.gw; x++) if (walk (x, z))
        { int sx, sy; proj (x, z, &sx, &sy); unsigned r = (z * 8) & 0xFF, b = (x * 8) & 0xFF; fb_fill (sx - 1, sy - 1, 3, 3, (r << 16) | b); }
        // corner/center markers: (0,0)=red (max,0)=green (0,max)=blue (max,max)=yellow (mid,mid)=white
        int mx = g_room.gw - 1, mz = g_room.gh - 1; int sx, sy;
        struct { int x, z; unsigned c; } M[] = { {0,0,0x00FF0000},{mx,0,0x0000FF00},{0,mz,0x000080FF},{mx,mz,0x00FFFF00},{mx/2,mz/2,0x00FFFFFF} };
        for (int i = 0; i < 5; i++) { proj (M[i].x, M[i].z, &sx, &sy); fb_fill (sx - 4, sy - 4, 9, 9, M[i].c); }
    }
    int order[MAX_AVATARS], n = 0;
    for (int i = 0; i < g_cli.world.n_av; i++) if (g_cli.world.av[i].used) order[n++] = i;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
        if (g_cli.world.av[order[j]].x + g_cli.world.av[order[j]].z < g_cli.world.av[order[i]].x + g_cli.world.av[order[i]].z) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int k = 0; k < n; k++) { Avatar &a = g_cli.world.av[order[k]];
        int cx, cy; if (g_room_loaded) proj (a.x, a.z, &cx, &cy); else { cx = 500 + (a.x - a.z) * 17; cy = 120 + (a.x + a.z) * 11; }
        if (!draw_avatar_sprite (a, cx, cy)) { unsigned col = a.gender ? 0x00FF99CC : 0x00FFD080; fb_fill (cx - 6, cy - 34, 12, 30, col); } }
    // debug: a 5x zoomed inset of the first avatar, top-right
    if (g_grid && n > 0) { LayerSrc L[RL_COUNT]; build_layers (g_cli.world.av[order[0]], L); if (L[RL_BODY].present) {
        Rgba f; compose_avatar (f, L, g_cli.world.av[order[0]].direction, 0); int S = 5, px0 = WIN_W - AV_CELL_W * S - 10, py0 = 60;
        for (int yy = 0; yy < AV_CELL_H; yy++) for (int xx = 0; xx < AV_CELL_W; xx++) { unsigned s = f.px[yy * f.w + xx]; if (!(s >> 24)) continue;
            for (int dy = 0; dy < S; dy++) for (int dx = 0; dx < S; dx++) { int X = px0 + xx * S + dx, Y = py0 + yy * S + dy; if ((unsigned) X < WIN_W && (unsigned) Y < WIN_H) fb[Y * WIN_W + X] = s & 0x00FFFFFF; } } } }
    LeaveCriticalSection (&g_cs);
}

// ---- GDI+ PNG screenshot ---------------------------------------------------------------
static int png_clsid (CLSID *clsid)
{
    UINT num = 0, size = 0; Gdiplus::GetImageEncodersSize (&num, &size); if (!size) return -1;
    Gdiplus::ImageCodecInfo *info = (Gdiplus::ImageCodecInfo *) malloc (size);
    Gdiplus::GetImageEncoders (num, size, info);
    int r = -1; for (UINT i = 0; i < num; i++) if (!wcscmp (info[i].MimeType, L"image/png")) { *clsid = info[i].Clsid; r = 0; break; }
    free (info); return r;
}
static void save_shot (const char *path)
{
    Gdiplus::Bitmap bmp (WIN_W, WIN_H, WIN_W * 4, PixelFormat32bppRGB, (BYTE *) fb);
    CLSID c; if (png_clsid (&c) != 0) return;
    wchar_t wp[300]; MultiByteToWideChar (CP_UTF8, 0, path, -1, wp, 300);
    bmp.Save (wp, &c, 0);
    printf ("[gui] screenshot saved: %s\n", path);
}

// ---- demo + net threads ----------------------------------------------------------------
static const char *SAMPLE_AVATAR =
    "{\"user_id\":1065,\"pseudo\":\"Demo\",\"gender\":\"male\",\"user_status\":\"VIPMAX\","
    "\"position_x\":15,\"position_z\":15,\"direction\":2,\"skin_tone_hex\":\"#e18863\","
    "\"equipped_clothing\":{"
    "\"top\":{\"sprite_path\":\"/images/sprite/Male/TOP/MTOP109.png\",\"color_editable\":0},"
    "\"bottom\":{\"sprite_path\":\"/images/sprite/Male/BOTTOM/designer_20260529_124232_f405e276.png\",\"color_editable\":0},"
    "\"hair\":{\"sprite_path\":\"/images/sprite/Male/HAIR/designer_20260509_212902_7d1d784d.png\",\"color_editable\":1},"
    "\"hat\":{\"sprite_path\":\"/images/sprite/Male/HAT/designer_20261001_201821_4635bf8f.png\",\"color_editable\":1,\"color_hex\":\"#9c948b\"},"
    "\"shoes\":{\"sprite_path\":\"/images/sprite/Male/SHOES/designer_20260628_162905_1d186ed1.png\",\"color_editable\":1},"
    "\"beard\":{\"sprite_path\":\"/images/sprite/Male/BEARD/designer_20260617_234042_ccb19ec1.png\",\"color_editable\":0},"
    "\"glasses\":{\"sprite_path\":\"/images/sprite/Male/GLASSES/designer_20260808_154004_1e7c7e26.png\",\"color_editable\":0}}}";

static void ensure_avatar_assets ()
{
    for (int i = 0; i < g_cli.world.n_av; i++) { Avatar &a = g_cli.world.av[i]; if (!a.used) continue;
        g_assets.ensure (body_sheet_path (a.gender));
        if (a.eye_sprite_path[0]) g_assets.ensure (a.eye_sprite_path);
        for (int j = 0; j < N_LAYERS; j++) if (a.cloth[j].present && a.cloth[j].sprite_path[0]) g_assets.ensure (a.cloth[j].sprite_path); }
}
static void load_room_demo ()
{
    ITransport *tp = mk_auto (0); HttpResp rs;
    bool ok = http_request (*tp, g_host, g_port, "GET", "/src/rooms/nightclub/patio.js", "", 0, 0, rs); delete tp;
    printf ("[gui] patio.js GET ok=%d status=%d bytes=%d\n", ok, rs.status, rs.body.n);
    if (ok && rs.status == 200) { EnterCriticalSection (&g_cs); g_room.parse (rs.body.p, rs.body.n);
        if (g_tw > 0) g_room.tileW = g_tw; if (g_th > 0) g_room.tileH = g_th;
        if (g_orgx >= 0) g_room.originX = g_orgx; if (g_orgy >= 0) g_room.originY = g_orgy;
        if (g_orgx < 0) g_room.originX += (g_room.zSize * g_room.tileW) / 4;
        LeaveCriticalSection (&g_cs);
        printf ("[gui] room: origin=(%d,%d) size=%dx%d grid=%dx%d bgGame='%s'\n", g_room.originX, g_room.originY, g_room.xSize, g_room.zSize, g_room.gw, g_room.gh, g_room.bgGame);
        char p[160]; snprintf (p, sizeof p, "/images/game/%s", g_room.bgGame); bool be = g_assets.ensure (p);
        const Rgba *bg = g_assets.get (p); printf ("[gui] bg '%s' ensured=%d loaded=%d size=%dx%d\n", p, be, bg != 0, bg ? bg->w : 0, bg ? bg->h : 0);
        InterlockedExchange (&g_room_loaded, 1); }
}
// ---- GUI->net command queue + live config ----------------------------------------------
enum { CMD_CHAT = 1, CMD_MOVE = 2 };
struct Cmd { int kind, a, b; char text[256]; };
static Cmd g_cmd[32]; static volatile int g_ch = 0, g_ct = 0; static CRITICAL_SECTION g_ccs; static bool g_ccs_init = false;
static void cmd_init () { if (!g_ccs_init) { InitializeCriticalSection (&g_ccs); g_ccs_init = true; } }
static void push_cmd (const Cmd &c) { cmd_init (); EnterCriticalSection (&g_ccs); int nx = (g_ct + 1) % 32; if (nx != g_ch) { g_cmd[g_ct] = c; g_ct = nx; } LeaveCriticalSection (&g_ccs); }
static bool pop_cmd (Cmd &o) { cmd_init (); bool g = false; EnterCriticalSection (&g_ccs); if (g_ch != g_ct) { o = g_cmd[g_ch]; g_ch = (g_ch + 1) % 32; g = true; } LeaveCriticalSection (&g_ccs); return g; }
static char g_token[600]; static const char *g_pseudo = "", *g_password = "";

static void load_room_live (int id)
{
    ITransport *tp = mk_auto (0); char path[48]; snprintf (path, sizeof path, "/api/rooms/%d", id);
    Buf h; if (g_token[0]) { h.add ("X-Session-Token: "); h.add (g_token); h.add ("\r\n"); }
    HttpResp rp; bool ok = http_request (*tp, g_host, g_port, "GET", path, h.p ? h.p : "", 0, 0, rp); delete tp;
    if (!ok || rp.status != 200) { printf ("[gui] room meta failed status=%d\n", rp.status); return; }
    json::Doc d; d.parse (rp.body.p ? rp.body.p : "", (unsigned long) rp.body.n, json::TOLERANT);
    const char *fp = d.root ()["room"]["file_path"].asStr (""); if (!fp[0]) return;
    char src[192]; snprintf (src, sizeof src, "/src/rooms/%s", fp);
    ITransport *tp2 = mk_auto (0); HttpResp rs; bool ok2 = http_request (*tp2, g_host, g_port, "GET", src, "", 0, 0, rs); delete tp2;
    if (!ok2 || rs.status != 200) return;
    EnterCriticalSection (&g_cs); g_room.parse (rs.body.p, rs.body.n);
    if (g_tw > 0) g_room.tileW = g_tw; if (g_th > 0) g_room.tileH = g_th;
    if (g_orgx >= 0) g_room.originX = g_orgx; if (g_orgy >= 0) g_room.originY = g_orgy;
        if (g_orgx < 0) g_room.originX += (g_room.zSize * g_room.tileW) / 4;
    LeaveCriticalSection (&g_cs);
    char p[160]; snprintf (p, sizeof p, "/images/game/%s", g_room.bgGame); g_assets.ensure (p);
    InterlockedExchange (&g_room_loaded, 1);
}

static DWORD WINAPI net_thread (LPVOID)
{
    g_assets.init (g_host, g_port, g_tls);
    if (g_demo)
    {
        load_room_demo ();
        json::Doc d; d.parse (SAMPLE_AVATAR, (unsigned long) strlen (SAMPLE_AVATAR), json::TOLERANT);
        EnterCriticalSection (&g_cs); g_cli.world.self_id = 1065; g_cli.world.apply_full (d.root ()); LeaveCriticalSection (&g_cs);
        ensure_avatar_assets ();
        return 0;
    }
    // --- live: token (env/arg) or REST login, then WebSocket ---
    const char *envtok = getenv ("TAATU_TOKEN"); if (envtok) lstrcpynA (g_token, envtok, sizeof g_token);
    if (!g_token[0] && g_pseudo[0])
    {
        TaatuAuth auth; auth.set_target (g_host, g_port); auth.mk = mk_auto;
        char uuid[37]; TaatuAuth::gen_uuid (uuid); lstrcpynA (auth.device_id, uuid, sizeof auth.device_id);
        auth.set_fingerprint ("TaatuOnyx/0.1 (PC)", "fr", "onyx", "Europe/Brussels", WIN_W, WIN_H, 32, 4, 0, 0);
        int r = auth.login (g_pseudo, g_password, true);
        if (r == LOGIN_OK) lstrcpynA (g_token, auth.token, sizeof g_token);
        else { printf ("[gui] login failed: %s\n", auth.err); return 1; }
    }
    g_cli.now_ms = now_ms; g_cli.set_target (g_host, g_port, g_tls, "https://taatu.world"); g_cli.set_token (g_token);
    ITransport *tp = mk_auto (0);
    if (!g_cli.connect (*tp)) { printf ("[gui] ws connect failed\n"); delete tp; return 1; }
    bool joined = false; unsigned last_step = 0, last_assets = 0;
    while (g_run && !g_cli.closed ())
    {
        g_cli.poll ();
        if (g_cli.connected () && !joined) { g_cli.note_input (); g_cli.join_room (g_roomId, 27, 27, 1); load_room_live (g_roomId); joined = true; }
        Cmd c; while (pop_cmd (c)) { g_cli.note_input (); if (c.kind == CMD_CHAT) g_cli.send_chat (c.text); else if (c.kind == CMD_MOVE) g_cli.begin_move (c.a, c.b); }
        unsigned t = now_ms ();
        if (t - last_step > 230) { g_cli.step_move (); last_step = t; }
        if (joined && t - last_assets > 500) { ensure_avatar_assets (); last_assets = t; }
        Sleep (10);
    }
    delete tp; return 0;
}

// ---- window ----------------------------------------------------------------------------
static LRESULT CALLBACK WndProc (HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_TIMER: render (); InvalidateRect (h, 0, FALSE); return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC hdc = BeginPaint (h, &ps);
        StretchDIBits (hdc, 0, 0, WIN_W, WIN_H, 0, 0, WIN_W, WIN_H, fb, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
        SetBkMode (hdc, TRANSPARENT); SetTextColor (hdc, RGB (235, 238, 248));
        char hdr[96]; snprintf (hdr, sizeof hdr, "TAATU (PC) - %s - salle %d - %d avatars", g_demo ? "demo" : "live", g_roomId, g_cli.world.n_av);
        TextOutA (hdc, 10, 8, hdr, (int) strlen (hdr));
        EnterCriticalSection (&g_cs);
        for (int i = 0; i < g_cli.world.n_av; i++) { Avatar &a = g_cli.world.av[i]; if (!a.used) continue;
            int cx, cy; if (g_room_loaded) proj (a.x, a.z, &cx, &cy); else { cx = 500; cy = 300; }
            SetTextColor (hdc, RGB (255, 255, 255)); TextOutA (hdc, cx - (int) strlen (a.pseudo) * 3, cy - AV_CELL_H - 2, a.pseudo, (int) strlen (a.pseudo)); }
        // chat log (bottom-left)
        SetTextColor (hdc, RGB (214, 222, 234));
        for (int i = 0; i < g_cli.world.chat_count && i < 8; i++) { int idx = g_cli.world.chat_count - 1 - i; if (idx < 0) break;
            const ChatMsg &mm = g_cli.world.chat_at (idx); char line[300]; snprintf (line, sizeof line, "%s: %s", mm.pseudo, mm.message);
            TextOutA (hdc, 10, WIN_H - 60 - i * 16, line, (int) strlen (line)); }
        LeaveCriticalSection (&g_cs);
        char inp[220]; snprintf (inp, sizeof inp, "> %s", g_input); SetTextColor (hdc, RGB (255, 255, 180)); TextOutA (hdc, 10, WIN_H - 38, inp, (int) strlen (inp));
        EndPaint (h, &ps); return 0;
    }
    case WM_LBUTTONDOWN:
    {
        int x = LOWORD (l), y = HIWORD (l);
        if (g_room_loaded) { int tx, tz; g_room.unproject (x - g_ox, y - g_oy, &tx, &tz);
            if (tx >= 0 && tz >= 0 && (!g_room.gw || walk (tx, tz))) { Cmd c; c.kind = CMD_MOVE; c.a = tx; c.b = tz; c.text[0] = 0; push_cmd (c); } }
        return 0;
    }
    case WM_CHAR:
    {
        if (w == 13) { if (g_input_len) { Cmd c; c.kind = CMD_CHAT; lstrcpynA (c.text, g_input, sizeof c.text); push_cmd (c); g_input_len = 0; g_input[0] = 0; } }
        else if (w == 8) { if (g_input_len) g_input[--g_input_len] = 0; }
        else if (w >= 32 && w < 127 && g_input_len < 198) { g_input[g_input_len++] = (char) w; g_input[g_input_len] = 0; }
        return 0;
    }
    case WM_DESTROY: g_run = false; PostQuitMessage (0); return 0;
    }
    return DefWindowProc (h, m, w, l);
}

int main (int argc, char **argv)
{
    g_demo = flag (argc, argv, "--demo");
    lstrcpynA (g_host, arg (argc, argv, "--host", "taatu.world"), sizeof g_host);
    g_port = atoi (arg (argc, argv, "--port", "443"));
    g_tls = (g_port == 443) || !strcmp (arg (argc, argv, "--tls", "0"), "1");
    g_roomId = atoi (arg (argc, argv, "--room", "4"));
    lstrcpynA (g_shot, arg (argc, argv, "--shot", ""), sizeof g_shot);
    g_shotSec = atoi (arg (argc, argv, "--shotsec", "6"));
    g_flipx = flag (argc, argv, "--flipx"); g_flipy = flag (argc, argv, "--flipy"); g_tr = flag (argc, argv, "--tr"); g_grid = flag (argc, argv, "--grid");
    g_ox = atoi (arg (argc, argv, "--ox", "0")); g_oy = atoi (arg (argc, argv, "--oy", "0"));
    g_tw = atoi (arg (argc, argv, "--tw", "-1")); g_th = atoi (arg (argc, argv, "--th", "-1"));
    g_orgx = atoi (arg (argc, argv, "--orgx", "-1")); g_orgy = atoi (arg (argc, argv, "--orgy", "-1"));
    g_pseudo = arg (argc, argv, "--pseudo", ""); g_password = arg (argc, argv, "--password", "");

    InitializeCriticalSection (&g_cs);
    fb = (unsigned *) calloc (WIN_W * WIN_H, 4);
    memset (&g_bmi, 0, sizeof g_bmi);
    g_bmi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = WIN_W; g_bmi.bmiHeader.biHeight = -WIN_H;   // top-down
    g_bmi.bmiHeader.biPlanes = 1; g_bmi.bmiHeader.biBitCount = 32; g_bmi.bmiHeader.biCompression = BI_RGB;

    Gdiplus::GdiplusStartupInput gsi; ULONG_PTR gtok; Gdiplus::GdiplusStartup (&gtok, &gsi, 0);

    // --dirs <file>: a montage of the sample avatar in all 8 directions (row 1) and the 7
    // walk frames of direction 2 (row 2), 4x scale, to check mirroring + animation.
    const char *dirsFile = arg (argc, argv, "--dirs", "");
    if (dirsFile[0])
    {
        g_assets.init (g_host, g_port, g_tls);
        json::Doc d; d.parse (SAMPLE_AVATAR, (unsigned long) strlen (SAMPLE_AVATAR), json::TOLERANT);
        g_cli.world.self_id = 1065; g_cli.world.apply_full (d.root ());
        ensure_avatar_assets ();
        for (int i = 0; i < WIN_W * WIN_H; i++) fb[i] = 0x00384050;
        Avatar base = g_cli.world.av[0]; int S = 4;
        for (int dir = 0; dir < 8; dir++) {
            Avatar t = base; t.direction = dir; LayerSrc L[RL_COUNT]; build_layers (t, L);
            Rgba f; compose_avatar (f, L, dir, 0);
            int x0 = 16 + dir * (AV_CELL_W * S + 14), y0 = 50;
            for (int yy = 0; yy < AV_CELL_H; yy++) for (int xx = 0; xx < AV_CELL_W; xx++) { unsigned s = f.px[yy * f.w + xx]; if (!(s >> 24)) continue;
                for (int dy = 0; dy < S; dy++) for (int dx = 0; dx < S; dx++) { int X = x0 + xx * S + dx, Y = y0 + yy * S + dy; if ((unsigned) X < WIN_W && (unsigned) Y < WIN_H) fb[Y * WIN_W + X] = s & 0xFFFFFF; } }
        }
        for (int fr = 0; fr < 7; fr++) {
            Avatar t = base; t.direction = 2; LayerSrc L[RL_COUNT]; build_layers (t, L);
            Rgba f; compose_avatar (f, L, 2, fr);
            int x0 = 16 + fr * (AV_CELL_W * S + 14), y0 = 420;
            for (int yy = 0; yy < AV_CELL_H; yy++) for (int xx = 0; xx < AV_CELL_W; xx++) { unsigned s = f.px[yy * f.w + xx]; if (!(s >> 24)) continue;
                for (int dy = 0; dy < S; dy++) for (int dx = 0; dx < S; dx++) { int X = x0 + xx * S + dx, Y = y0 + yy * S + dy; if ((unsigned) X < WIN_W && (unsigned) Y < WIN_H) fb[Y * WIN_W + X] = s & 0xFFFFFF; } }
        }
        save_shot (dirsFile); Gdiplus::GdiplusShutdown (gtok); return 0;
    }

    g_cli.now_ms = now_ms; g_cli.lock_fn = wlock; g_cli.unlock_fn = wunlock;

    CreateThread (0, 0, net_thread, 0, 0, 0);

    // headless screenshot mode: render a few seconds, save PNG, exit
    if (g_shot[0])
    {
        for (int t = 0; t < g_shotSec * 10; t++) { render (); Sleep (100); }
        save_shot (g_shot);
        Gdiplus::GdiplusShutdown (gtok);
        return 0;
    }

    WNDCLASSA wc; memset (&wc, 0, sizeof wc);
    wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandle (0); wc.lpszClassName = "TaatuWin"; wc.hCursor = LoadCursor (0, IDC_ARROW);
    RegisterClassA (&wc);
    RECT r = { 0, 0, WIN_W, WIN_H }; AdjustWindowRect (&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA ("TaatuWin", "TAATU", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);
    ShowWindow (hwnd, SW_SHOW);
    SetTimer (hwnd, 1, 33, 0);
    MSG msg; while (GetMessage (&msg, 0, 0, 0)) { TranslateMessage (&msg); DispatchMessage (&msg); }
    Gdiplus::GdiplusShutdown (gtok);
    return 0;
}
