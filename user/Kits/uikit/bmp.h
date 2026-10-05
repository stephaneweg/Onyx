//
// bmp.h -- an icon's picture for UIKit and the apps (the docks, the lists, the skins).
//
// ui::icon_load reads a picture file through ImageKit -- any format it reads: the icons are BMP
// today, they may be PNG tomorrow -- and gives it as the icons are drawn: 0x00RRGGBB, top-down, the
// see-through pixels in the icons' key colour (magenta, 0x00FF00FF). (Until 2026-10-05 this was
// user/bmp.hpp, a 24-bit BMP reader compiled into each program.)
//
#ifndef ONYX_BMP_H
#define ONYX_BMP_H

namespace ui {

// The picture of a file (new[]: the caller's to delete[]), its size in *pw, *ph; 0 if it cannot be read.
unsigned *icon_load (const char *path, int *pw, int *ph);
// (the name the programs knew: the same)
unsigned *bmp_decode (const char *path, int *pw, int *ph);

} // namespace ui

#endif // ONYX_BMP_H
