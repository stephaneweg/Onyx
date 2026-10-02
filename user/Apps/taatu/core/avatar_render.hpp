//
// avatar_render.hpp -- compose a TAATU avatar frame from its layer sheets. Portable: given
// already-decoded sheets, it does the slicing + mirror + tint + layering. Sheet format (read
// from the web client): 7 cols x 5 rows of 34x80 cells.
//   - rows = base directions 0..4; dirs 5,6,7 = rows 3,2,1 mirrored horizontally.
//   - BODY-level art (body, bottom, top, shoes) lives in the WALK-FRAME columns (0..4).
//   - HEAD-level art (the bare head from the BODY sheet, plus eyes/beard/hair/glasses/hat)
//     lives in a single HEAD column (the last one, col 6): ONE head per direction. The head
//     is pasted AFTER the clothing and BEFORE hair/hats.
// Draw order (back -> front): body, bottom, top, shoes, head, eyes, beard, hair, glasses, hat.
//
#ifndef TAATU_AVATAR_RENDER_HPP
#define TAATU_AVATAR_RENDER_HPP
#include "sprite.hpp"

namespace taatu {

enum { AV_CELL_W = 34, AV_CELL_H = 80 };

// layers in BACK->FRONT draw order
enum RenderLayer { RL_BODY = 0, RL_BOTTOM, RL_TOP, RL_SHOES, RL_HEAD, RL_EYES, RL_BEARD, RL_HAIR, RL_GLASSES, RL_HAT, RL_COUNT };

struct LayerSrc
{
    const Rgba *sheet;      // decoded sheet, or 0 if absent
    unsigned tint;          // 0x00RRGGBB multiply tint, or 0 for none
    bool present;
    bool head;              // true: use the fixed HEAD column (one per direction), not the frame
    LayerSrc () : sheet (0), tint (0), present (false), head (false) {}
};

// direction (0..7) -> sheet row + horizontal mirror.
static inline void dir_to_row (int direction, int *row, bool *mirror)
{
    direction &= 7;
    if (direction <= 4) { *row = direction; *mirror = false; }
    else { *row = 8 - direction; *mirror = true; }     // 5->3, 6->2, 7->1 mirrored
}

// Compose one 34x80 frame for `direction`/`frame` into out (allocated here, transparent bg).
static inline void compose_avatar (Rgba &out, const LayerSrc layers[RL_COUNT], int direction, int frame)
{
    out.alloc (AV_CELL_W, AV_CELL_H);
    memset (out.px, 0, (size_t) AV_CELL_W * AV_CELL_H * 4);
    int row; bool mirror; dir_to_row (direction, &row, &mirror);
    for (int i = 0; i < RL_COUNT; i++)
    {
        const LayerSrc &L = layers[i];
        if (!L.present || !L.sheet || !L.sheet->px) continue;
        int cols = L.sheet->w / AV_CELL_W; if (cols < 1) cols = 1;
        int col = L.head ? (cols - 1) : frame;         // head items: last column (per direction)
        if (col >= cols) col = cols - 1; if (col < 0) col = 0;
        int sx = col * AV_CELL_W, sy = row * AV_CELL_H;
        if (sy + AV_CELL_H > L.sheet->h) sy = 0;        // sheet has fewer rows: fall back
        blit_cell (out, 0, 0, *L.sheet, sx, sy, AV_CELL_W, AV_CELL_H, mirror, L.tint);
    }
}

} // namespace taatu
#endif
