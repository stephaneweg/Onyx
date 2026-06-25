# Onyx libnsfb surface (NetSurf framebuffer backend)

`onyx_surface.c` is a **libnsfb** surface backend that makes an **Onyx window content
canvas** the framebuffer libnsfb draws into. libnsfb is NetSurf's framebuffer abstraction:
its software plotters render into a surface buffer, and a "surface" backend supplies that
buffer, flushes it, and feeds input. This is brick 6 of the NetSurf port and the path the
GUI browser will render through.

Same split as `user/tls/` and `user/img/`: the upstream library is cross-built here, the
Onyx glue (`onyx_surface.c`) lives in this repo and is compiled into the archive. The
vendored libnsfb is **unpatched**.

## How it maps to Onyx

| libnsfb surface op | Onyx kapi |
|---|---|
| `initialise` | `kapi_create_window(w,h,"NetSurf")` → the `0x00RRGGBB` canvas **is** `nsfb->ptr` |
| `update`     | `kapi_present()` (whole canvas — no dirty-rect kapi yet) |
| `geometry`   | `kapi_resize_window` (after the window exists) |
| `input`      | `kapi_set_pointer/key_handler` + `kapi_pump_events` → a ring of `nsfb_event_t` |
| `finalise`   | drop `nsfb->ptr` (the kernel frees the window on app exit) |

- **Pixel format** `NSFB_FMT_XRGB8888`. On little-endian the `32bpp-xrgb8888` plotter packs
  a colour into a `0x00RRGGBB` word — exactly the Onyx canvas layout, so **no R/B swap**.
- **Input** Onyx is callback-driven, libnsfb polls. The kapi handlers translate events
  (pointer move → `MOVE_ABSOLUTE`; buttons → `KEY_DOWN/UP` of `NSFB_KEY_MOUSE_*`; keys →
  `KEY_DOWN`) into a ring that `input()` drains; the window close box → `NSFB_CONTROL_QUIT`.
- **No enum patch** the backend registers under the name `"onyx"` with a private type value;
  the app resolves it with `nsfb_type_from_name("onyx")`, so libnsfb's `nsfb_type_e` is
  untouched. The registration is a `__attribute__((constructor))` (libnsfb's
  `NSFB_SURFACE_DEF`); Onyx's `crt0` runs `.init_array`, so it fires before `main`.

## Layout

- `onyx_surface.c` — the surface backend (this repo).
- `compat/endian.h` — a tiny `<endian.h>` shim (newlib has no glibc-style one; libnsfb's
  `plot.h` includes it then reads the GCC byte-order builtins). Keeps libnsfb unpatched.
- `Makefile` — cross-builds `libnsfb.a` (core + RAM + Onyx surfaces + software plotters;
  SDL/X/VNC/Wayland excluded — they need host libs).
- `libnsfb/` — the upstream source (git clone / submodule, **not** committed here).

## Building

1. Add the libnsfb source (once), pinned to the tested commit:

   ```sh
   git clone git://git.netsurf-browser.org/libnsfb.git user/nsfb/libnsfb
   git -C user/nsfb/libnsfb checkout b701cdc   # 0.2.2, 2026-01-09 (tested)
   ```

2. Build the library (the Onyx surface is compiled in):

   ```sh
   make -C user/nsfb
   ```

   → `user/nsfb/libnsfb/libnsfb.a`.

3. Build the demo app:

   ```sh
   make -C user/bin NSFB_DIR=../nsfb nsfbdemo.elf
   ```

   `nsfbdemo` opens an `"onyx"` surface and draws shapes with libnsfb's plotters, then
   tracks the cursor — proving the render + input path. **Link note:** the surface
   registers via a constructor, so `libnsfb.a` is linked with `--whole-archive` (otherwise
   the linker drops the un-referenced surface object and `"onyx"` never registers).

## Status

Cross-builds and links clean against newlib (no undefined symbols); the `"onyx"`
registration constructor is verified present in `.init_array`. Runtime rendering on
hardware is the next check. The cursor is left to the Onyx desktop (no libnsfb soft
cursor); dirty-rect updates and window resize are minimal first-pass.

Pinned: **libnsfb 0.2.2** @ `b701cdc`.
