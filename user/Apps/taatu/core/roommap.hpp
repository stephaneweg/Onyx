//
// roommap.hpp -- parse a TAATU room module (ROOM_DATA in /src/rooms/<file_path>) into the
// geometry the client needs: iso origin, grid size, walkable grid (0 walk / 1 blocked /
// 2 stair), entry point, and the texture names. Portable string parsing (no JS engine, no
// STL). The web client gets the room's file_path from GET /api/rooms/{id}.
//
#ifndef TAATU_ROOMMAP_HPP
#define TAATU_ROOMMAP_HPP
#include <string.h>
#include <stdlib.h>

namespace taatu {

enum { ROOM_GRID_MAX = 96 };     // xSize/zSize cap

struct RoomMap
{
    int originX, originY, xSize, zSize, entryX, entryZ;
    char bgGame[96], bgGeneral[96], foreground[96];
    signed char grid[ROOM_GRID_MAX * ROOM_GRID_MAX];   // grid[z*xSize + x]
    int gw, gh;
    // projection params (calibratable; origin from ROOM_DATA). 2:1-ish iso.
    int tileW, tileH, isoZ;

    RoomMap () { reset (); }
    void reset ()
    {
        originX = originY = 125; xSize = zSize = gw = gh = 0; entryX = entryZ = 0;
        bgGame[0] = bgGeneral[0] = foreground[0] = 0;
        memset (grid, 1, sizeof grid);
        tileW = 34; tileH = 22; isoZ = 20;        // TODO calibrate against a screenshot
    }

    int cell (int x, int z) const { return (unsigned) x < (unsigned) gw && (unsigned) z < (unsigned) gh ? grid[z * gw + x] : 1; }
    bool walkable (int x, int z) const { int v = cell (x, z); return v == 0 || v == 2; }

    // tile (x,z) -> screen pixel (centre of the tile) within the room background image.
    void iso (int x, int z, int *sx, int *sy) const
    {
        *sx = originX + (x - z) * (tileW / 2);
        *sy = originY + (x + z) * (tileH / 2);
    }
    // screen pixel -> nearest tile (inverse of iso).
    void unproject (int px, int py, int *x, int *z) const
    {
        int dx = px - originX, dy = py - originY;
        float a = (float) dx / (tileW / 2), b = (float) dy / (tileH / 2);
        *x = (int) ((a + b) / 2 + 0.5f);
        *z = (int) ((b - a) / 2 + 0.5f);
    }

    bool parse (const char *js, int len)
    {
        reset ();
        originX = find_int (js, "originX", 125);
        originY = find_int (js, "originY", 125);
        xSize   = find_int (js, "xSize", 0);
        zSize   = find_int (js, "zSize", 0);
        entryX  = find_int (js, "entryPointX", 0);
        entryZ  = find_int (js, "entryPointZ", 0);
        find_str (js, "backgroundGame", bgGame, sizeof bgGame);
        find_str (js, "backgroundGeneral", bgGeneral, sizeof bgGeneral);
        find_str (js, "foreground", foreground, sizeof foreground);
        parse_grid (js, len);
        if (!gw) { gw = xSize; gh = zSize; }
        return gw > 0 && gh > 0;
    }

private:
    // find a real `key: <int>` / `key = <int>` (skip occurrences in comments: require that
    // the key is immediately followed, after spaces, by ':' or '=' then a number).
    static int find_int (const char *js, const char *key, int def)
    {
        int kl = (int) strlen (key);
        for (const char *p = strstr (js, key); p; p = strstr (p + kl, key))
        {
            const char *q = p + kl;
            while (*q == ' ' || *q == '\t') q++;
            if (*q != ':' && *q != '=') continue; q++;
            while (*q == ' ' || *q == '\t') q++;
            bool neg = (*q == '-'); if (neg) q++;
            if (*q < '0' || *q > '9') continue;
            int v = 0; while (*q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0');
            return neg ? -v : v;
        }
        return def;
    }
    // find a real `key: "value"` (skip comment mentions: require ':'/'=' then '"').
    static void find_str (const char *js, const char *key, char *out, int cap)
    {
        out[0] = 0;
        int kl = (int) strlen (key);
        for (const char *p = strstr (js, key); p; p = strstr (p + kl, key))
        {
            const char *q = p + kl;
            while (*q == ' ' || *q == '\t') q++;
            if (*q != ':' && *q != '=') continue; q++;
            while (*q == ' ' || *q == '\t') q++;
            if (*q != '"') continue; q++;
            int i = 0; while (*q && *q != '"' && i + 1 < cap) out[i++] = *q++;
            out[i] = 0; return;
        }
    }
    // parse groundGrid: [ [n,n,...], [..], ... ] -> grid[z*gw+x]
    void parse_grid (const char *js, int len)
    {
        const char *p = strstr (js, "groundGrid"); if (!p) return;
        (void) len;
        p = strchr (p, '['); if (!p) return; p++;     // into the outer array
        int z = 0;
        while (*p && z < ROOM_GRID_MAX)
        {
            while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t' || *p == ',') p++;
            if (*p == ']') break;                      // end of outer array
            if (*p != '[') { p++; continue; }
            p++;                                       // into a row
            int x = 0;
            while (*p && *p != ']' && x < ROOM_GRID_MAX)
            {
                while (*p == ' ' || *p == ',' || *p == '\r' || *p == '\n' || *p == '\t') p++;
                if (*p == ']') break;
                bool neg = (*p == '-'); if (neg) p++;
                if (*p < '0' || *p > '9') { p++; continue; }
                int v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
                grid[z * ROOM_GRID_MAX + x] = (signed char) (neg ? -v : v);
                x++;
            }
            if (*p == ']') p++;
            if (x > gw) gw = x;
            z++;
        }
        gh = z;
        // compact rows from stride ROOM_GRID_MAX down to gw
        if (gw > 0 && gw < ROOM_GRID_MAX)
            for (int zz = 1; zz < gh; zz++)
                for (int xx = 0; xx < gw; xx++)
                    grid[zz * gw + xx] = grid[zz * ROOM_GRID_MAX + xx];
    }
};

} // namespace taatu
#endif
