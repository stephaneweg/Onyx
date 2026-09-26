# doomgeneric in Onyx

Upstream: https://github.com/ozkl/doomgeneric (GPL-2.0, see LICENSE; the commit is in
UPSTREAM). Only the portable sources are kept (the other platforms' `doomgeneric_*.c` and
the SDL / Allegro sound files are left out). The Onyx platform layer is in `user/doom/`
(`doom_onyx.c`: window, full screen, keys, gamepad; `doom_sound.c`: sound effects and the
MUS music on the kernel's FM voices; `Makefile`).

Changes here, all under `#ifdef ONYX`:

- `i_sound.c`: no `<SDL_mixer.h>` (the modules are Onyx's).
- `m_misc.c`: `M_FileExists` asks the kernel (`onyx_file_exists`) instead of `fopen`, which
  in Onyx's newlib reads the whole file (a WAD is tens of MB).
- `m_config.c`: the configuration and saves go to `SD:/doom/`.
