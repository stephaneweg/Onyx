# Onyx image codecs (PNG / JPEG)

`image.hpp` is a reusable image decoder for Onyx apps, built on **zlib**, **libpng**
and **libjpeg**. It decodes a PNG or JPEG byte buffer (no files) into a freshly
`malloc`'d array of `0xAARRGGBB` pixels for the canvas. Same split as `user/tls/`: the
Onyx-side glue is header-only here; this directory's `Makefile` builds the upstream
libraries.

NetSurf and other browsers decode images the same way — they ship no codec of their
own, they link libpng / libjpeg / zlib (and libnsgif/libnssvg for the formats those
don't cover). Onyx does the equivalent: the codecs cross-built here, behind one wrapper.

## Layout

- `image.hpp` — header-only decoder: `onyximg::decode(data, len, &w, &h)` sniffs the
  format (PNG signature / JPEG SOI) and returns `unsigned *` of `w*h` pixels, or `0`.
  `decode_png` / `decode_jpeg` are also exposed. Caller `free()`s the result. This is a
  **newlib** component (uses `malloc` + the libs); link it in a newlib C++ app.
- `Makefile` — cross-builds the three upstream libraries for `aarch64-none-elf` + newlib.
- `jconfig.h` — the libjpeg build config (copied next to the libjpeg sources by `make`).
- `zlib/`, `libpng/`, `libjpeg/` — the upstream sources (git submodules / unpacked
  tarballs, **not** committed here).

## Building

1. Add the three upstream sources (once), pinned to the tested releases:

   ```sh
   # zlib 1.3.1
   curl -fL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz | tar xz
   mv zlib-1.3.1 user/img/zlib

   # libpng 1.6.44 (needs zlib's headers)
   curl -fL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.44.tar.gz | tar xz
   mv libpng-1.6.44 user/img/libpng

   # libjpeg — IJG reference, v9f
   curl -fL https://www.ijg.org/files/jpegsrc.v9f.tar.gz | tar xz
   mv jpeg-9f user/img/libjpeg
   ```

   (Or use the libpng / IJG git mirrors as submodules at the same tags.)

2. Build the libraries:

   ```sh
   make -C user/img
   ```

   → `user/img/zlib/libz.a`, `user/img/libpng/libpng.a`, `user/img/libjpeg/libjpeg.a`.

3. Build an image app, pointing it at the libs and headers. The startup + kapi syscall
   layer are compiled as C objects (gcc), then linked with g++ alongside the libs — the
   same crt0 split the HTTPS apps use (see `../bin/Makefile`):

   ```sh
   aarch64-none-elf-gcc  $(CFLAGS_LIBC) -c ../libc/crt0libc.S    -o crt0libc.o
   aarch64-none-elf-gcc  $(CFLAGS_LIBC) -c ../libc/onyx_syscalls.c -o onyx_syscalls.o
   aarch64-none-elf-g++  $(CXXFLAGS_LIBC) -I../img -Iuser/img/libpng -Iuser/img/zlib -Iuser/img/libjpeg \
       crt0libc.o onyx_syscalls.o app.cpp \
       -Luser/img/libpng -Luser/img/zlib -Luser/img/libjpeg -lpng -ljpeg -lz -lm -o app.elf
   ```

   The C stubs **must** be compiled by `gcc` (C linkage); g++ would mangle
   `_close`/`_read`/… and newlib would not find them.

## Output format

`decode()` returns one `unsigned` per pixel, `0xAARRGGBB` (8-bit alpha in the top byte,
then R, G, B). The Onyx canvas ignores the top byte when blitting, so opaque images
display as-is; the alpha is preserved for code that wants to composite. This differs
from `bmp.hpp`, which yields `0x00RRGGBB` magenta-keyed icons from SD files — keep
`bmp.hpp` for those; `image.hpp` is for full-colour web images.

## Config / build notes

- **zlib**: the `inflate`/`deflate` *stream* API plus the `gz*` file helpers. libpng only
  needs the stream API, but the NetSurf port (brick 9, `utils/messages.c`) reads gzipped
  files via `gzopen`, so the `gz*` TUs are built too (they bottom out in newlib
  `open`/`read`/`close`, resolved at link time in a newlib app).
- **libpng**: built pure-C with `-DPNG_ARM_NEON_OPT=0` (the NEON intrinsics TU is left
  out for a safe first bring-up). Uses the upstream `scripts/pnglibconf.h.prebuilt`.
- **libjpeg**: the IJG library objects plus `jmemnobs` (the no-temp-file memory
  manager). The `jpegtran` app and the alternate memory managers are excluded.
- All three use the newlib app world (`-mcpu=cortex-a72`, hardware FP, PIC/SSP off);
  they pull `malloc`/`memcpy`/… from newlib, so they are **not** built `-nostdlib`.

## Status

PNG and JPEG decode is **cross-built and links clean** (newlib + the three libs, no
undefined symbols). The `image.hpp` wrapper compiles and links; end-to-end decode on
hardware is the next check. GIF / SVG are not covered here (NetSurf brings its own
`libnsgif` / `libnssvg`).
