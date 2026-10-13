# wipeout-rewrite in Onyx

Upstream: https://github.com/phoboslab/wipeout-rewrite (the commit is in UPSTREAM). **It has no
licence**: see README.upstream.md, "License" -- no commercial release. docs/LICENSING.md.
The game's data (Sony's) is not here and is never shipped: SD:/wipeout/README.txt.

Only what Onyx builds is kept: the game (`src/wipeout/`), the core (`input`, `mem`, `system`,
`types`, `utils`), the software renderer (`render_software.c`) and the libraries it uses
(`libs/pl_mpeg.h`, `libs/qoa.h`, `libs/stb_image_write.h`); not the SDL / Sokol platforms, the
OpenGL renderer, the build files. The Onyx platform layer is `user/Ports/wipeout/`.

Changes here, all under `#ifdef ONYX`:

- `utils.h`: `die` calls `onyx_die` (the error in a notification too).
- `render_software.c`: `render_set_resolution` asks the platform (`platform_set_resolution`) to
  change the size it draws at (upstream's software renderer ignored it).
- `wipeout/game.c`: the settings' default resolution is 240P.
