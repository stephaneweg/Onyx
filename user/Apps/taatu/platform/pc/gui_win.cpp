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
// app state + in-window login
enum { ST_LOGIN = 0, ST_CONNECTING = 1, ST_WORLD = 2, ST_MFA = 3, ST_MAP = 4, ST_BUILDING = 5 };
static volatile LONG g_state = ST_CONNECTING;
static char g_lp[64] = "", g_lw[128] = "";       // login pseudo / password
static int  g_lfield = 0;                         // 0 = pseudo, 1 = password
static volatile bool g_creds_ready = false;
static char g_login_err[160] = "";
static char g_mfa_code[16] = "";                  // emailed 2FA code
static char g_mfa_hint[96] = "";                  // masked email the code was sent to
static volatile bool g_mfa_ready = false;
static volatile bool g_busy = false;              // a network login/verify is in flight
// ---- world home (map) data (sprites all fetched at runtime, like the web client) --------
// Building layout from the web map.js (mapConfig.buildings): sprite + percent position on
// the map background. Season "summer": the sprite base name gets "_summer" before .png.
struct BldInfo { int id; const char *display; const char *sprite; float xp, yp; };
static const BldInfo BLD[] = {
    { 1, "Le Parc",            "map_square_summer.png",     34.0f, 31.3f },
    { 2, "Les Apparts",        "map_appart_summer.png",     55.2f, 11.5f },
    { 3, "Event Center",       "map_cine_summer.png",       78.8f, 35.8f },
    { 4, "Le Night Club",      "map_dancefloor_summer.png", 54.9f, 66.7f },
    { 5, "Le Backstage",       "map_backstage_summer.png",  36.5f, 58.5f },
    { 6, "Le Shopping Center", "map_mall_summer.png",       77.7f, 61.5f },
};
enum { NBLD = 6 };
struct RoomInfo { int id, building_id, player_count; bool is_appart; char name[64]; };
static RoomInfo g_rooms[320]; static int g_nrooms = 0;
static int g_sel_bld = -1;            // building-view: index into BLD (-1 = none)
static RECT g_bld_rect[NBLD];         // building hit rectangles on the map (screen)
static RECT g_row_rect[32]; static int g_row_room[32]; static int g_nrow = 0;  // building-view room rows
static int g_bgw = 0, g_bgh = 0, g_bgx = 0, g_bgy = 0;   // map background placement
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
// nearest-neighbour scaled alpha-over blit into fb.
static void blit_scaled_fb (int dx, int dy, int dw, int dh, const Rgba &img)
{
    if (dw <= 0 || dh <= 0 || img.w <= 0 || img.h <= 0) return;
    for (int y = 0; y < dh; y++) { int ty = dy + y; if ((unsigned) ty >= (unsigned) WIN_H) continue; int sy = y * img.h / dh;
        for (int x = 0; x < dw; x++) { int tx = dx + x; if ((unsigned) tx >= (unsigned) WIN_W) continue; int sx = x * img.w / dw;
            unsigned s = img.px[sy * img.w + sx]; if (!(s >> 24)) continue;
            unsigned *d = &fb[ty * WIN_W + tx]; *d = blend_px (*d | 0xFF000000u, s) & 0x00FFFFFF; } }
}
// draw the world map (terrain + building sprites, scaled to fit) into fb; fills g_bld_rect
static void draw_map ()
{
    const Rgba *bg = g_assets.get ("/images/game/map_summerbgr.png");
    float scale = 1.0f;
    if (bg) {
        float sx = (float) WIN_W / bg->w, sy = (float) (WIN_H - 46) / bg->h; scale = sx < sy ? sx : sy;
        g_bgw = (int) (bg->w * scale); g_bgh = (int) (bg->h * scale);
        g_bgx = (WIN_W - g_bgw) / 2; g_bgy = 46 + ((WIN_H - 46) - g_bgh) / 2;
        blit_scaled_fb (g_bgx, g_bgy, g_bgw, g_bgh, *bg);
    } else { g_bgw = WIN_W; g_bgh = WIN_H - 46; g_bgx = 0; g_bgy = 46; }
    (void) scale;
    // each building is a FULL-FRAME overlay (same size as the terrain, mostly transparent):
    // stack them all at the terrain rect. The label sits at the building's xPercent/yPercent.
    for (int i = 0; i < NBLD; i++) {
        char pth[96]; snprintf (pth, sizeof pth, "/images/game/%s", BLD[i].sprite);
        const Rgba *sp = g_assets.get (pth);
        if (sp) blit_scaled_fb (g_bgx, g_bgy, g_bgw, g_bgh, *sp);
        int cx = g_bgx + (int) (BLD[i].xp / 100.0f * g_bgw);
        int cy = g_bgy + (int) (BLD[i].yp / 100.0f * g_bgh);
        g_bld_rect[i].left = cx - 46; g_bld_rect[i].top = cy - 20; g_bld_rect[i].right = cx + 46; g_bld_rect[i].bottom = cy + 20;  // label anchor
    }
}
// which building overlay is non-transparent at screen (x,y)? topmost wins. -1 none.
static int building_at (int x, int y)
{
    int hit = -1;
    for (int i = 0; i < NBLD; i++) {
        char pth[96]; snprintf (pth, sizeof pth, "/images/game/%s", BLD[i].sprite);
        const Rgba *sp = g_assets.get (pth); if (!sp || g_bgw <= 0 || g_bgh <= 0) continue;
        int ox = (x - g_bgx) * sp->w / g_bgw, oy = (y - g_bgy) * sp->h / g_bgh;
        if ((unsigned) ox < (unsigned) sp->w && (unsigned) oy < (unsigned) sp->h) {
            unsigned a = sp->px[oy * sp->w + ox] >> 24; if (a > 48) hit = i;   // last (topmost) non-transparent
        }
    }
    return hit;
}
static void render ()
{
    fb_fill (0, 0, WIN_W, WIN_H, g_state == ST_WORLD ? 0x00101018 : 0x000d1018);
    if (g_state == ST_MAP || g_state == ST_BUILDING) { draw_map (); return; }   // WM_PAINT draws labels/list
    if (g_state != ST_WORLD) return;             // login / connecting: WM_PAINT draws the form
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
enum { CMD_CHAT = 1, CMD_MOVE = 2, CMD_ENTER = 3, CMD_LEAVE = 4 };
struct Cmd { int kind, a, b; char text[256]; };
static Cmd g_cmd[32]; static volatile int g_ch = 0, g_ct = 0; static CRITICAL_SECTION g_ccs; static bool g_ccs_init = false;
static void cmd_init () { if (!g_ccs_init) { InitializeCriticalSection (&g_ccs); g_ccs_init = true; } }
static void push_cmd (const Cmd &c) { cmd_init (); EnterCriticalSection (&g_ccs); int nx = (g_ct + 1) % 32; if (nx != g_ch) { g_cmd[g_ct] = c; g_ct = nx; } LeaveCriticalSection (&g_ccs); }
static bool pop_cmd (Cmd &o) { cmd_init (); bool g = false; EnterCriticalSection (&g_ccs); if (g_ch != g_ct) { o = g_cmd[g_ch]; g_ch = (g_ch + 1) % 32; g = true; } LeaveCriticalSection (&g_ccs); return g; }
static char g_token[600]; static const char *g_pseudo = "", *g_password = "";
static bool g_autoroom = false;                 // --autoroom: skip the map, enter --room directly

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

// ---- world home: fetch rooms, download the map assets, live occupancy -------------------
static void parse_rooms_json (const char *body, int len)
{
    json::Doc d; if (!d.parse (body ? body : "", (unsigned long) len, json::TOLERANT)) return;
    const json::Value &arr = d.root ()["rooms"]; int n = 0;
    EnterCriticalSection (&g_cs);
    for (unsigned i = 0; i < arr.size () && n < 320; i++) {
        const json::Value &r = arr[i];
        if (!r["is_active"].asBool (false)) continue;
        RoomInfo &ri = g_rooms[n++];
        ri.id = r["id"].asInt (0); ri.building_id = r["building_id"].asInt (0);
        ri.player_count = r["player_count"].asInt (0); ri.is_appart = r["is_appart"].asBool (false);
        const char *nm = r["name"].asStr (""); int k = 0; while (nm[k] && k < 63) { ri.name[k] = nm[k]; k++; } ri.name[k] = 0;
    }
    g_nrooms = n;
    LeaveCriticalSection (&g_cs);
}
static void on_raw (void *, const char *name, const char *args)   // map-occupancy -> room counts
{
    if (strcmp (name, "map-occupancy")) return;
    json::Doc d; if (!d.parse (args ? args : "", (unsigned long) strlen (args), json::TOLERANT)) return;
    const json::Value &p = d.root ()[1];
    const json::Value &rooms = p["rooms"]; const json::Value &apparts = p["apparts"];
    EnterCriticalSection (&g_cs);
    for (int i = 0; i < g_nrooms; i++) {
        char key[16]; snprintf (key, sizeof key, "%d", g_rooms[i].id);
        const json::Value &src = g_rooms[i].is_appart ? apparts : rooms;
        if (src.has (key)) g_rooms[i].player_count = src[key].asInt (g_rooms[i].player_count);
    }
    LeaveCriticalSection (&g_cs);
}
static DWORD WINAPI dl_map_assets (LPVOID)   // heavy map images, off the net thread
{
    g_assets.ensure ("/images/game/map_summerbgr.png");
    for (int i = 0; i < NBLD; i++) { char p[96]; snprintf (p, sizeof p, "/images/game/%s", BLD[i].sprite); g_assets.ensure (p); }
    return 0;
}
static void fetch_home ()
{
    ITransport *tp = mk_auto (0);
    Buf h; if (g_token[0]) { h.add ("X-Session-Token: "); h.add (g_token); h.add ("\r\n"); }
    HttpResp rp; bool ok = http_request (*tp, g_host, g_port, "GET", "/api/rooms?include_inactive=1", h.p ? h.p : "", 0, 0, rp); delete tp;
    if (ok && rp.status == 200) parse_rooms_json (rp.body.p, rp.body.n);
    CreateThread (0, 0, dl_map_assets, 0, 0, 0);     // map terrain + overlays download in the background
}

static bool g_mapdemo = false;
static DWORD WINAPI net_thread (LPVOID)
{
    g_assets.init (g_host, g_port, g_tls);
    if (g_mapdemo)                               // --map: render the world map, no login (public assets)
    {
        g_assets.ensure ("/images/game/map_summerbgr.png");
        for (int i = 0; i < NBLD; i++) { char p[96]; snprintf (p, sizeof p, "/images/game/%s", BLD[i].sprite); g_assets.ensure (p); }
        g_state = ST_MAP;
        return 0;
    }
    if (g_demo)
    {
        load_room_demo ();
        json::Doc d; d.parse (SAMPLE_AVATAR, (unsigned long) strlen (SAMPLE_AVATAR), json::TOLERANT);
        EnterCriticalSection (&g_cs); g_cli.world.self_id = 1065; g_cli.world.apply_full (d.root ()); LeaveCriticalSection (&g_cs);
        ensure_avatar_assets (); g_state = ST_WORLD;
        return 0;
    }
    // --- live: token (env/arg) or in-window REST login, then WebSocket ---
    const char *envtok = getenv ("TAATU_TOKEN"); if (envtok) lstrcpynA (g_token, envtok, sizeof g_token);
    if (!g_token[0] && g_pseudo[0]) { lstrcpynA (g_lp, g_pseudo, sizeof g_lp); lstrcpynA (g_lw, g_password, sizeof g_lw); g_creds_ready = true; }

    while (g_run && !g_token[0])               // login screen loop (retry on failure)
    {
        g_state = ST_LOGIN; g_busy = false;
        while (g_run && !g_creds_ready) Sleep (40);
        if (!g_run) return 0;
        g_creds_ready = false; g_busy = true; g_login_err[0] = 0;
        TaatuAuth auth; auth.set_target (g_host, g_port); auth.mk = mk_auto;
        char uuid[37]; TaatuAuth::gen_uuid (uuid); lstrcpynA (auth.device_id, uuid, sizeof auth.device_id);
        auth.set_fingerprint ("TaatuOnyx/0.1 (PC)", "fr", "onyx", "Europe/Brussels", WIN_W, WIN_H, 32, 4, 0, 0);
        int r = auth.login (g_lp, g_lw, true);
        memset (g_lw, 0, sizeof g_lw);          // wipe the password from memory
        g_busy = false;
        if (r == LOGIN_OK) { lstrcpynA (g_token, auth.token, sizeof g_token); break; }
        if (r == LOGIN_MFA)                     // 2FA: ask for the emailed code
        {
            g_login_err[0] = 0; lstrcpynA (g_mfa_hint, auth.email_masque, sizeof g_mfa_hint); g_state = ST_MFA; g_mfa_code[0] = 0; g_mfa_ready = false;
            for (;;)
            {
                g_busy = false;
                while (g_run && !g_mfa_ready) Sleep (40);
                if (!g_run) return 0;
                g_mfa_ready = false; g_busy = true; g_login_err[0] = 0;
                int r2 = auth.mfa_verify (auth.challenge_id, g_mfa_code, true);
                memset (g_mfa_code, 0, sizeof g_mfa_code); g_busy = false;
                if (r2 == LOGIN_OK) { lstrcpynA (g_token, auth.token, sizeof g_token); break; }
                lstrcpynA (g_login_err, auth.err[0] ? auth.err : "Code invalide", sizeof g_login_err);
                if (r2 == LOGIN_NET) break;     // network issue -> back to the login screen
            }
            if (g_token[0]) break;
        }
        else lstrcpynA (g_login_err, auth.err[0] ? auth.err : "Echec du login", sizeof g_login_err);
    }
    if (!g_run) return 0;

    g_state = ST_CONNECTING;
    g_cli.now_ms = now_ms; g_cli.on_event_raw = on_raw; g_cli.set_target (g_host, g_port, g_tls, "https://taatu.world"); g_cli.set_token (g_token);
    ITransport *tp = mk_auto (0);
    if (!g_cli.connect (*tp)) { lstrcpynA (g_login_err, "Connexion au serveur impossible", sizeof g_login_err); g_state = ST_LOGIN; g_token[0] = 0; delete tp; return 1; }
    bool onMap = false, inRoom = false; unsigned last_step = 0, last_assets = 0, last_occ = 0;
    while (g_run && !g_cli.closed ())
    {
        g_cli.poll ();
        if (g_cli.connected () && !onMap)       // arrive on the WORLD HOME (map), do NOT auto-join
        {
            onMap = true; g_state = ST_MAP;     // switch the UI to the map at once (assets stream in)
            fetch_home ();                      // then download terrain + building overlays + the rooms
            g_cli.request_map_occupancy ();
            // honour an explicit --room on the command line (skip the map)
            if (g_roomId > 0 && g_autoroom) { Cmd c; c.kind = CMD_ENTER; c.a = g_roomId; c.b = 0; push_cmd (c); }
        }
        Cmd c;
        while (pop_cmd (c))
        {
            g_cli.note_input ();
            if (c.kind == CMD_CHAT) g_cli.send_chat (c.text);
            else if (c.kind == CMD_MOVE) g_cli.begin_move (c.a, c.b);
            else if (c.kind == CMD_ENTER)
            {
                EnterCriticalSection (&g_cs); g_cli.world.clear_room (); LeaveCriticalSection (&g_cs);
                InterlockedExchange (&g_room_loaded, 0);
                load_room_live (c.a);
                int ex = g_room.entryX ? g_room.entryX : 20, ez = g_room.entryZ ? g_room.entryZ : 20;
                g_cli.join_room (c.a, ex, ez, 1, c.b != 0);
                inRoom = true; g_state = ST_WORLD;
            }
            else if (c.kind == CMD_LEAVE)
            {
                if (inRoom) g_cli.leave_room ();
                inRoom = false; InterlockedExchange (&g_room_loaded, 0);
                EnterCriticalSection (&g_cs); g_cli.world.clear_room (); LeaveCriticalSection (&g_cs);
                g_cli.request_map_occupancy (); g_state = ST_MAP;
            }
        }
        unsigned t = now_ms ();
        if (inRoom && t - last_step > 230) { g_cli.step_move (); last_step = t; }
        if (inRoom && t - last_assets > 500) { ensure_avatar_assets (); last_assets = t; }
        if (!inRoom && t - last_occ > 5000) { g_cli.request_map_occupancy (); last_occ = t; }
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
        SetBkMode (hdc, TRANSPARENT);
        if (g_state == ST_MAP || g_state == ST_BUILDING)
        {
            SetTextColor (hdc, RGB (240, 244, 252));
            HFONT tf = CreateFontA (22, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Segoe UI");
            HGDIOBJ ot = SelectObject (hdc, tf);
            TextOutA (hdc, 14, 10, "TAATU - Choisis un lieu", 23);
            SelectObject (hdc, ot); DeleteObject (tf);
            if (!g_assets.get ("/images/game/map_summerbgr.png")) { SetTextColor (hdc, RGB (180, 190, 210)); TextOutA (hdc, WIN_W / 2 - 90, WIN_H / 2, "Chargement de la carte...", 25); }
            // building labels + live counts
            EnterCriticalSection (&g_cs);
            for (int i = 0; i < NBLD; i++) {
                int tot = 0; for (int r = 0; r < g_nrooms; r++) if (g_rooms[r].building_id == BLD[i].id) tot += g_rooms[r].player_count;
                int cx = (g_bld_rect[i].left + g_bld_rect[i].right) / 2, by = g_bld_rect[i].bottom;
                char lab[80]; snprintf (lab, sizeof lab, "%s (%d)", BLD[i].display, tot);
                SetTextColor (hdc, RGB (20, 24, 32)); TextOutA (hdc, cx - (int) strlen (lab) * 3 + 1, by + 3, lab, (int) strlen (lab));
                SetTextColor (hdc, RGB (255, 255, 255)); TextOutA (hdc, cx - (int) strlen (lab) * 3, by + 2, lab, (int) strlen (lab));
            }
            if (g_state == ST_BUILDING && g_sel_bld >= 0)
            {
                int px = WIN_W - 340, pw = 330, py = 60, ph = WIN_H - 120;
                RECT pr = { px, py, px + pw, py + ph }; HBRUSH pb = CreateSolidBrush (RGB (24, 28, 40)); FillRect (hdc, &pr, pb); DeleteObject (pb);
                FrameRect (hdc, &pr, (HBRUSH) GetStockObject (GRAY_BRUSH));
                SetTextColor (hdc, RGB (240, 244, 252));
                HFONT bf = CreateFontA (20, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Segoe UI"); HGDIOBJ ob = SelectObject (hdc, bf);
                TextOutA (hdc, px + 12, py + 8, BLD[g_sel_bld].display, (int) strlen (BLD[g_sel_bld].display));
                SelectObject (hdc, ob); DeleteObject (bf);
                SetTextColor (hdc, RGB (170, 180, 200)); TextOutA (hdc, px + 12, py + 32, "Clique une salle pour entrer - Echap: retour", 44);
                g_nrow = 0; int y = py + 56;
                for (int r = 0; r < g_nrooms && g_nrow < 32; r++) {
                    if (g_rooms[r].building_id != BLD[g_sel_bld].id) continue;
                    RECT rr = { px + 8, y, px + pw - 8, y + 22 };
                    if (g_rooms[r].player_count > 0) { HBRUSH hb = CreateSolidBrush (RGB (36, 44, 60)); FillRect (hdc, &rr, hb); DeleteObject (hb); }
                    char line[90]; snprintf (line, sizeof line, "%s  (%d)", g_rooms[r].name, g_rooms[r].player_count);
                    SetTextColor (hdc, g_rooms[r].player_count > 0 ? RGB (255, 255, 255) : RGB (180, 188, 204));
                    TextOutA (hdc, px + 12, y + 3, line, (int) strlen (line));
                    g_row_rect[g_nrow] = rr; g_row_room[g_nrow] = r; g_nrow++;
                    y += 23; if (y > py + ph - 24) break;
                }
            }
            LeaveCriticalSection (&g_cs);
            EndPaint (h, &ps); return 0;
        }
        if (g_state != ST_WORLD)
        {
            int bx = WIN_W / 2 - 160, by = 210;
            HFONT big = CreateFontA (34, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Segoe UI");
            HGDIOBJ of = SelectObject (hdc, big); SetTextColor (hdc, RGB (240, 244, 252));
            TextOutA (hdc, bx, by - 70, "TAATU", 5); SelectObject (hdc, of); DeleteObject (big);
            if (g_state == ST_MFA)
            {
                SetTextColor (hdc, RGB (205, 212, 226));
                char lbl[140]; if (g_mfa_hint[0]) snprintf (lbl, sizeof lbl, "Code envoye a %s", g_mfa_hint); else lstrcpynA (lbl, "Code recu par email", sizeof lbl);
                TextOutA (hdc, bx, by, lbl, (int) strlen (lbl));
                RECT r = { bx, by + 20, bx + 200, by + 46 }; HBRUSH b = CreateSolidBrush (RGB (58, 70, 96)); FillRect (hdc, &r, b); DeleteObject (b);
                FrameRect (hdc, &r, (HBRUSH) GetStockObject (GRAY_BRUSH));
                HFONT cf = CreateFontA (22, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Consolas");
                HGDIOBJ oc = SelectObject (hdc, cf); SetTextColor (hdc, RGB (255, 255, 255));
                TextOutA (hdc, bx + 8, by + 24, g_mfa_code, (int) strlen (g_mfa_code)); SelectObject (hdc, oc); DeleteObject (cf);
                SetTextColor (hdc, RGB (165, 176, 198));
                TextOutA (hdc, bx, by + 62, g_busy ? "Verification..." : "Entree : valider le code", g_busy ? 15 : 24);
            }
            else
            {
                SetTextColor (hdc, RGB (205, 212, 226));
                TextOutA (hdc, bx, by, "Pseudo", 6);
                TextOutA (hdc, bx, by + 62, "Mot de passe", 12);
                for (int fld = 0; fld < 2; fld++) { int y = by + 20 + fld * 62; RECT r = { bx, y, bx + 320, y + 26 };
                    HBRUSH b = CreateSolidBrush (g_lfield == fld && !g_busy ? RGB (58, 70, 96) : RGB (38, 44, 58)); FillRect (hdc, &r, b); DeleteObject (b);
                    FrameRect (hdc, &r, (HBRUSH) GetStockObject (GRAY_BRUSH)); }
                SetTextColor (hdc, RGB (255, 255, 255));
                TextOutA (hdc, bx + 7, by + 24, g_lp, (int) strlen (g_lp));
                char stars[130]; int pl = (int) strlen (g_lw); for (int i = 0; i < pl && i < 129; i++) stars[i] = '*'; stars[pl < 129 ? pl : 129] = 0;
                TextOutA (hdc, bx + 7, by + 86, stars, (int) strlen (stars));
                SetTextColor (hdc, RGB (165, 176, 198));
                const char *hint = g_busy ? "Connexion..." : "Tab : champ suivant     Entree : valider";
                TextOutA (hdc, bx, by + 130, hint, (int) strlen (hint));
            }
            if (g_login_err[0]) { SetTextColor (hdc, RGB (255, 120, 120)); TextOutA (hdc, bx, by + (g_state == ST_MFA ? 92 : 158), g_login_err, (int) strlen (g_login_err)); }
            EndPaint (h, &ps); return 0;
        }
        SetTextColor (hdc, RGB (235, 238, 248));
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
        if (g_state == ST_MAP) {
            int b = building_at (x, y);
            if (b >= 0) { g_sel_bld = b; g_state = ST_BUILDING; InvalidateRect (h, 0, FALSE); }
            return 0;
        }
        if (g_state == ST_BUILDING) {
            for (int i = 0; i < g_nrow; i++) if (x >= g_row_rect[i].left && x < g_row_rect[i].right && y >= g_row_rect[i].top && y < g_row_rect[i].bottom)
                { RoomInfo &ri = g_rooms[g_row_room[i]]; Cmd c; c.kind = CMD_ENTER; c.a = ri.id; c.b = ri.is_appart ? 1 : 0; c.text[0] = 0; push_cmd (c); return 0; }
            return 0;
        }
        if (g_room_loaded) { int tx, tz; g_room.unproject (x - g_ox, y - g_oy, &tx, &tz);
            if (tx >= 0 && tz >= 0 && (!g_room.gw || walk (tx, tz))) { Cmd c; c.kind = CMD_MOVE; c.a = tx; c.b = tz; c.text[0] = 0; push_cmd (c); } }
        return 0;
    }
    case WM_KEYDOWN:
    {
        if (w == VK_ESCAPE) {
            if (g_state == ST_BUILDING) { g_state = ST_MAP; g_sel_bld = -1; InvalidateRect (h, 0, FALSE); }
            else if (g_state == ST_WORLD) { Cmd c; c.kind = CMD_LEAVE; c.text[0] = 0; push_cmd (c); }
        }
        return 0;
    }
    case WM_CHAR:
    {
        if (g_state == ST_MFA)
        {
            int len = (int) strlen (g_mfa_code);
            if (w == 13) { if (len) g_mfa_ready = true; }
            else if (w == 8) { if (len) g_mfa_code[len - 1] = 0; }
            else if (w >= 32 && w < 127 && len < (int) sizeof g_mfa_code - 1) { g_mfa_code[len] = (char) w; g_mfa_code[len + 1] = 0; }
            InvalidateRect (h, 0, FALSE); return 0;
        }
        if (g_state == ST_LOGIN)
        {
            char *f = g_lfield == 0 ? g_lp : g_lw; int cap = g_lfield == 0 ? (int) sizeof g_lp : (int) sizeof g_lw; int len = (int) strlen (f);
            if (w == 9) g_lfield ^= 1;                                    // Tab
            else if (w == 13) { if (g_lfield == 0 && g_lp[0]) g_lfield = 1; else if (g_lp[0] && g_lw[0]) g_creds_ready = true; }
            else if (w == 8) { if (len) f[len - 1] = 0; }
            else if (w >= 32 && w < 127 && len < cap - 1) { f[len] = (char) w; f[len + 1] = 0; }
            InvalidateRect (h, 0, FALSE); return 0;
        }
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
    g_demo = flag (argc, argv, "--demo"); g_mapdemo = flag (argc, argv, "--map");
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
    g_autoroom = flag (argc, argv, "--autoroom");

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
