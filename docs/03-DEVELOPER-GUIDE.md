# Onyx — Developer Guide

This guide explains how to **build** Onyx, how to **write an application** or a
**`/bin` tool**, how to **extend the `kapi` ABI**, and the **conventions** + **pitfalls** to
be aware of. For the details of how things work internally, see
[Kernel internals](02-KERNEL-INTERNALS.md).

## Contents

1. [Prerequisites and toolchain](#1-prerequisites-and-toolchain)
2. [Building Circle (once)](#2-building-circle-once)
3. [Building the kernel and applications](#3-building-the-kernel-and-applications)
4. [Deploying to the SD card](#4-deploying-to-the-sd-card)
5. [The application model](#5-the-application-model)
6. [Writing a graphical application](#6-writing-a-graphical-application)
7. [Writing a `/bin` tool](#7-writing-a-bin-tool)
8. [The `applib.h` library](#8-the-applibh-library)
9. [Packaging an app: `.app`, icons, `config.ini`](#9-packaging-an-app-app-icons-configini)
10. [Extending the `kapi` ABI](#10-extending-the-kapi-abi)
11. [Coding conventions](#11-coding-conventions)
12. [Debugging on hardware](#12-debugging-on-hardware)
13. [Known pitfalls](#13-known-pitfalls)

---

## 1. Prerequisites and toolchain

- **AArch64 bare-metal toolchain**: `aarch64-none-elf-` (GCC), used under **WSL** on
  a Windows development machine.
- **Circle**, pulled in as a **git submodule** at `circle/` (our fork
  `stephaneweg/circle`, branch `onyx`).
- `make`, `cp`, `mkdir` (standard Unix tools, via WSL).
- A **FAT32 SD card** and a **Raspberry Pi 4** to run on (there is no QEMU
  `raspi4b` in the reference dev environment; bring-up is done directly on
  hardware).

> **Important — which Circle?** `circle/` is a **git submodule** → our fork
> `stephaneweg/circle` (branch `onyx`), **not** any other clone. Patch Circle *there*
> (commit on `onyx`), rebuild the affected library, then record the new commit in the
> superproject (`git add circle && git commit`). The patches are listed in
> [Circle Changes](05-CIRCLE-CHANGES.md).

## 2. Building Circle (once)

```sh
# Circle is a git submodule (the fork stephaneweg/circle, branch onyx). Fetch it
# (or clone Onyx with --recurse-submodules in the first place):
git submodule update --init --recursive

# Configure for RPi 4 / AArch64. DEPTH=32 is REQUIRED: our GImage renders 32-bit
# pixels (Circle defaults to 16).
cd circle && ./configure -r 4 -p aarch64-none-elf- -d DEPTH=32 -f

# Build the libraries used by the kernel (make clean first when the fork's sysconfig.h
# changed -- e.g. the multi-core patch: the .o files do not depend on it):
for d in lib lib/sched lib/fs lib/fs/fat lib/usb lib/input lib/net lib/sound \
         addon/SDCard addon/fatfs addon/wlan; do (cd $d && make clean && make -j4) || break; done
(cd addon/wlan/hostap/wpa_supplicant && make -f Makefile.circle clean && make -f Makefile.circle -j4)
cd ..
```

> If you change `DEPTH` later, run `make clean` in `circle/lib` before
> rebuilding (the `.o` files do not depend on `Config.mk`).

The libraries linked by the kernel (cf. [`kernel/Makefile`](../kernel/Makefile)):
`libwpa_supplicant.a`, `libwlan.a`, `libfatfs.a`, `libsdcard.a`, `libnet.a`, `libsound.a`,
`libsched.a`, `libusb.a`, `libinput.a`, `libfs.a`, `libcircle.a`. Circle is built
**multi-core** (`ARM_ALLOW_MULTI_CORE`, fork patch #4): core 1 runs the sound producer.

> The Onyx-specific patches carried by this fork (branch `onyx`, on upstream tag `Step51`)
> are documented in [Circle Changes](05-CIRCLE-CHANGES.md).

## 3. Building the kernel and applications

From `kernel/`:

```sh
cd kernel
make           # -> kernel8-rpi4.img, THEN builds the apps (../user)
```

The default target (`all`):
1. compiles our sources + links against the Circle libs → **`kernel8-rpi4.img`**;
2. triggers `make -C ../user`, which builds **all the apps** (`user/*.elf`) and the
   **`/bin` tools** (`user/bin/*.elf`).

> **Kernel → apps order.** Since the apps go through the **fixed-address ABI table** and are
> **not** linked against the kernel's addresses, they no longer depend on the kernel
> build: a *kernel-only* change (that respects the ABI) does **not** require
> recompiling the apps. `make` rebuilds them anyway for convenience.

Details of the kernel wiring (which Circle files we replace, the order of the
`-I compat -I include` include path before `circle/include`): see
[Kernel internals §1](02-KERNEL-INTERNALS.md#1-scope-ours-vs-circle) and `kernel/Makefile`.

## 4. Deploying to the SD card

```sh
cd kernel
make stage     # copie l'image + chaque app + chaque outil vers ../sdcard/
```

`make stage`:
- copies `kernel8-rpi4.img` → `sdcard/`;
- for each `../user/<name>.elf`: creates `sdcard/apps/<name>.app/` and copies the ELF into it as
  **`main`** (the "Onyx" layout);
- copies each `../user/bin/<tool>.elf` → `sdcard/bin/<tool>`.

**Executables on the card have no extension**: `.elf` only exists in the build tree
(`user/*.elf`, `user/bin/*.elf`); staging drops it. Programs are recognized by their
content (ELF magic `7F 45 4C 46`), e.g. by the file manager.

Then copy **all** of the contents of `sdcard/` onto a **FAT32** card and boot the
Pi 4. See the [user guide](04-USER-GUIDE.md) for details about the card and
the configuration files.

**Keyboard layouts** live as binary files in `sdcard/etc/keymaps/<NAME>.kmap` (the
`keyb` tool — and the theme editor's dropdown, which lists whatever `.kmap` files are
actually present — load one and send it to the kernel via `kapi_set_keymap_data`, v27).
**The kernel compiles in no keyboard map at all**: at boot the keyboard table is empty and
stays so until `keyb` loads a layout from the card (see [Circle Changes §1](05-CIRCLE-CHANGES.md#1-keyboard-map-decoupled-from-the-kernel--max_tasks)),
so a layout is *only* ever a file — adding one needs no kernel rebuild. Regenerate the
`.kmap` files with `python tools/keymaps/genkeymaps.py`; it compiles each `<NAME>` from a
`keymap_<name>.h` table in **`tools/keymaps/maps/`** (the 8 layout sources, incl. `BE` =
Belgian azerty, all tracked in the Onyx repo — not in the Circle tree). The `.kmap` format
is `"OKM1"` + `u16` rows/cols + the `u16[128][5]` table (see the script header).

## 5. The application model

A Onyx application is a **single `.c` file** compiled into a **freestanding ELF** and
run in **EL1** in its own page table.

**Runtime** ([`user/crt0.S`](../user/crt0.S)):

```asm
_start:
    stp x29, x30, [sp, #-16]!   /* the stack is already set up by the kernel */
    bl  main                    /* call main() */
    ldp x29, x30, [sp], #16
    ret                         /* return -> the kernel terminates the task */
```

No argv is passed via the stack: `main()` takes no arguments. An app retrieves its argument
line via `kapi_get_args(buf, size)`, and exits early with `kapi_exit(status)`.

**Linking** ([`user/user.ld`](../user/user.ld)): everything is linked at
**`USER_VA_BASE` = 8 GB** (`. = 0x200000000;`). This is **not PIE**. The RX
(`.text`/`.rodata`) and RW (`.data`/`.bss`) sections are aligned on **64 KB** (`-z
max-page-size=0x10000`) so that the loader maps them onto distinct pages.
**No kernel symbol** is resolved at link time: everything goes through the ABI table.

**Compilation flags** ([`user/Makefile`](../user/Makefile)):

```
-ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only \
-O2 -Wall -Wextra -fno-stack-protector -I../kernel/include
```

- `-ffreestanding -nostdlib`: **no libc**. No `printf`, `malloc`, `string.h`…
  use `applib.h` (§8) and static/local buffers.
- `-mgeneral-regs-only`: integer-only codegen — the **default** for apps. Most apps
  do **integer / fixed-point arithmetic** (see `tinycalc`, `mandelbrot`).
- **Hardware float is available (opt-in).** The kernel now saves the full FP/SIMD
  state on every trap (see [Kernel internals §6](02-KERNEL-INTERNALS.md), trap
  frame), and FP/SIMD is enabled at EL0 (`CPACR_EL1`). To use `float`/`double` in an
  app, build it **without** `-mgeneral-regs-only` and **with** `-mcpu=cortex-a72`
  (the Pi 4 core). Basic FP (`+ - * /`, int↔double) compiles to hardware
  instructions with **no** soft-float runtime; transcendental math (`sin`/`cos`/
  `pow`…) lives in newlib's `libm` — see §5.1. For the bare-FP recipe, see the
  `fptest` target in [`user/bin/Makefile`](../user/bin/Makefile) (a target-specific
  `CFLAGS` override) and the `fptest` proof tool. Opt **only** the FP app in — leave
  the rest integer-only.
- `-I../kernel/include`: to include `<kern/kapi_abi.h>` (the shared ABI structure).

To add an app, create `user/<name>.c` and **add `<name>.elf` to the `PROGS` variable**
of [`user/Makefile`](../user/Makefile) (and `<tool>.elf` to the `PROGS` of
[`user/bin/Makefile`](../user/bin/Makefile) for a tool).

### 5.1. Apps using the C library (newlib)

The `aarch64-none-elf` toolchain ships **newlib** (`libc` + `libm`). An app can be
built against it to use the real `<stdio.h>` (`printf`, `FILE*`, `fopen`/`fseek`),
`<stdlib.h>` (`malloc`/`qsort`/`strtod`), `<string.h>`, and `<math.h>` instead of the
freestanding helpers (`applib.h`/`umm.h`). This is the foundation for porting large C
codebases (e.g. NetSurf).

How it works: [`user/libc/onyx_syscalls.c`](../user/libc/onyx_syscalls.c) implements
the handful of POSIX stubs newlib bottoms out in (`_sbrk`, `_read`, `_write`, `_open`,
`_close`, `_lseek`, `_fstat`, `_gettimeofday`, …) on top of the kapi ABI.
[`user/libc/crt0libc.S`](../user/libc/crt0libc.S) is the entry point — like `crt0.S`
but it calls `exit(main())` so stdio is flushed on the way out. Build flags drop
`-ffreestanding`/`-nostdlib` and use `-nostartfiles` (keep our entry, keep `libc`);
add `-mcpu=cortex-a72` (FP is required by `printf %f` and `libm`) and link `-lm`. See
the `LIBC_PROGS` rule in [`user/bin/Makefile`](../user/bin/Makefile) and the proof
tool [`user/bin/libctest.c`](../user/bin/libctest.c).

A **wtk app** can be a newlib app too (Doom, NetSurf, **Writer** — FreeType wants a libc): the
`writer.elf` rule of [`user/Makefile`](../user/Makefile) is the model — `NL_CFLAGS` /
`NL_CXXFLAGS` (hardware FP, `-nostartfiles`, sections for `--gc-sections`), `libc/crt0libc.o` +
`libc/onyx_syscalls.o`, the app, `wtk/libwtk.a`, then its libraries (`ft/libft.a`) and `-lm`. Take
the app out of the generic `APPS` list and add its `.elf` to `all:`; `make stage` stages it as any.

Notes / caveats:
- **One allocator** for a plain C newlib app: newlib's `malloc` owns the heap via
  `_sbrk`→`kapi_sbrk`; do **not** also link `umm.h`. (A wtk app on newlib has two, side by side:
  wtk's C++ objects on umm — `onyxpp.hpp`'s `operator new` — and the C libraries on `malloc`; each
  grows its own arena with `kapi_sbrk`.)
- **Files are buffered in RAM.** The kapi file API is sequential (no seek), so `_open`
  slurps the whole file into memory to give `fseek`/`ftell` full semantics, and a
  writable file is written back with `kapi_save_file()` on `close`. Fine for resource
  files; a future `kapi_lseek` would remove the whole-file buffering.
- **Size.** Static newlib pulls a fair amount of code (`printf` float support etc.).
  Acceptable for big apps; a `nano` variant can be revisited later for small tools.

## 6. Writing a graphical application

> **Notifications and clipboard (ABI v40).** `#include "notify.h"` then
> `notify ("My App", "Done.")` shows a bubble (the `notifyd` service, reached by IPC;
> launched on demand). `#include "clipboard.h"`: `clip_set_text`, `clip_get_text`,
> `clip_set_files (path, cut)`, `clip_get_file` — the kernel keeps one shared clipboard.
> **Full-screen apps (ABI v41)**: `unsigned *fb = kapi_fullscreen_begin (&w, &h);` gives a
> screen-sized buffer; draw into it and call `kapi_present_fb ()` once per frame (it also
> yields). The desktop is not drawn meanwhile and all input comes to your key/pointer
> handlers in screen coordinates. `kapi_fullscreen_end ()` (or exiting) restores the
> desktop. See `user/Apps/plasma`. **v55**: `unsigned *scr = kapi_fullscreen_direct (&w, &h, &stride);`
> (after `fullscreen_begin`) gives the **displayed framebuffer itself** (uncached; `stride` in
> pixels): draw there — or render there with `kapi_gpu_render` — and nothing is copied any
> more (`kapi_present_fb` only yields); tearing is possible. 0: keep the back buffer. Used
> by `n64emu` in full screen.
>
> Files: `fsutil.h` (`fs_join`, `fs_exists`, `fs_is_dir`, `fs_copy_tree`,
> `fs_remove_tree`, `fs_unique_name`) and `trash.h` (`trash_move`, `trash_restore`,
> `trash_purge`, `trash_empty`, `trash_count` — layout `SD:/.Trash/files` + `info/*.trashinfo`
> holding `Path=<original>`). Your own IPC service: `kapi_ipc_register ("name")`, clients `kapi_ipc_lookup ("name")` +
> `kapi_mailbox_send (pid, type, data, len)` (≤ 512 bytes); the service drains with
> `kapi_mailbox_recv`.

> **Drag & drop (ABI v42).** A **source** calls `kapi_drag_begin (DND_FILES, paths, len,
> label)` while the left button is held (from `onMouse`, once the cursor moved a few pixels
> from the press); `paths` is `\n`-separated. A **target** overrides the `wtk::Root`
> virtuals: `onDrop (x, y, type, data, len, flags)` (data NUL-terminated; `flags &
> DND_F_COPY` = Ctrl held), `onDragOver (x, y, leave, flags)` (highlight the drop spot),
> and the source gets `onDragDone (targetPid, flags)` (`DND_F_DESKTOP` = dropped on the
> desktop, `DND_F_CANCEL` = Esc). `kapi_get_modifiers ()` returns `MOD_CTRL` / `MOD_SHIFT` /
> `MOD_ALT`. Examples: `fileviewer` (source + target), `shelf`, and the document apps.
>
> **File associations**: `#include "fileassoc.h"` — `fa_open (path)` opens a path like a
> double-click (folder → File Viewer, `.app` / ELF → run, else the app `SD:/etc/fileassoc.ini`
> maps its extension to, as `SD:apps/<app>.app/main <path>`); `fa_app_for (path, app, cap)`
> only looks it up. **Unsaved changes**: `#include "docguard.h"` — keep
> `doc_hash (data, len)` of the document as loaded / saved, and before replacing it call
> `doc_confirm (name, changed, save_fn)` (a Yes / No / Cancel `MB_YESNOCANCEL` box; false =
> cancelled). A document app should accept a path argument (`kapi_get_args`) and a dropped
> file (`onDrop`), so it works with the File Viewer, the Shelf and `fileassoc.ini`.
> **Images**: `#include "img/imgload.hpp"` (in one TU) — `img_load (path, &frames)` decodes
> BMP / GIF (all frames + delays) / PNG / JPEG (stb_image, public domain), WebP
> (simplewebp, BSD-3) and PCX (our decoder) into `0xAARRGGBB` frames from the app's umm
> heap (`new unsigned[]` frames); `img_free (&frames)`; `img_is_image_name (name)`. The
> codecs are compiled once into `libwtk.a` (`wtk/imgload.cpp`, with FP/SIMD like
> `wtk/canvas.o`) and linked only into the apps that call them. Users: `imageview`,
> `fileviewer` (preview), `wtk::ImageBox`. `img_load_mem (data, len, &frames)` decodes a file's bytes
> already in memory (Writer's RTF pictures).
> **TrueType text** (`user/ft/`): the apps' FreeType — the upstream sources of
> `third_party/freetype-2.14.3` built lean by `user/Makefile` into `ft/libft.a`
> (`ft/onyx_ftoption.h`, `ft/onyx_ftmodule.h`: TrueType fonts only — truetype + sfnt —, anti-aliased
> — smooth —, hinted by the auto-hinter only — autofit, no bytecode interpreter —, their kerning read
> — GPOS too —; no compressed, web, bitmap, colour or variable fonts, no PostScript names; NetSurf
> keeps its own fuller build). FreeType wants a C library: an app using it is a **newlib** app (§5.1;
> Writer's rule in `user/Makefile` is the model). `#include "ft/fonts.h"` (header-only, one TU):
> `fnt::init ()` finds the families of `SD:/res/fonts` and `SD:/fonts` (each file's family name and
> style read from its own `name` / `head` / `OS/2` tables — no FreeType, three small reads), sorted;
> `fnt::count / name / find / styles`; `fnt::get (family, fnt::BOLD | fnt::ITALIC, size in 1/64 px)`
> a sized font (a style the family lacks is made: thickened, slanted) with its `ascent`, `descent`,
> `height`, underline place and thickness; `fnt::advance (f, cp)` — the design's advance, 1/64 px:
> exact at any size, so a line breaks at the same word at every zoom —, `fnt::kern`, `fnt::draw (cv,
> f, x64, baseline, cp, colour, clip)` — the glyph rendered once per quarter-pixel position, cached,
> blended with a slight gamma —, `draw_str / str_w` for Latin-1 labels. A character a font lacks
> comes from DejaVu Sans. `fnt::trim ()` drops the least used sizes when the cache is big — only at
> a moment no `Font *` is held (after a redraw); `fnt::g_onTrim` lets the app forget its own. The
> screenshots' build compiles the same sources for the PC (`shots.sh`).
> **Vector shapes** (`wtk/vpaint.h`, in `libwtk.a`, integer only): a `VPath` gathers outlines —
> `poly`, `rect`, `rrect`, `circle`, `ellipse`, `hole` (a disc cut out), strokes `line`, `polyline`,
> `arc` (round ends and joins), `arrowHead` — in 1/16 px (`V (px)`), then `fill (cv, colour, alpha)`
> paints their union (the non-zero rule: every outline turned the same way, a hole the other way),
> anti-aliased (four sub-rows a pixel, the spans' ends to 1/16 px). Writer's toolbar icons are drawn
> with it (`user/Apps/writer/icons.h`). `wk_sin / wk_cos (degrees)` × 16384.
> **File-system providers (ABI v44)**: an app can serve a whole path prefix to every other
> app — `kapi_vfs_register ("XYZ:")`, then loop on `kapi_vfs_next (&req, 1)` and answer each
> request (`req.op` = `VFS_OP_OPEN` / `READ` / `CLOSE` / `LIST` / `SAVE` / `MKDIR` / `REMOVE`
> / `RENAME`, see `user/kapi.h`) with `kapi_vfs_reply (req.id, status, data, len)`; a SAVE's
> payload is read with `kapi_vfs_req_data`. Example: `user/bin/ftpfs.cpp` (FTP / FTPS). The
> ordinary file kapis then work on `XYZ:...` paths in every app, unchanged.
> `ftpfs.h`: `ftpfs_login` / `ftpfs_login_site` (hand a login to ftpfs, optionally
> remembered), `ftpfs_forget`, `ftpfs_load_sites` (the remembered servers of `SD:/etc/ftpfs.ini`).
> **Sound (ABI v46)**: `kapi_sound_acquire ()` first (1 = the output is yours; 0 = another
> app has it) — the output is released and silenced by `kapi_sound_release ()` or when your
> process ends. Then `kapi_sound_start (voice 0..15, milliHz, SOUND_SQUARE / SINE / TRIANGLE /
> SAW / NOISE, volume 0..255)` plays a note until `kapi_sound_stop (voice)` (-1 = all), and
> `kapi_sound_write (frames, n)` streams PCM (s16 L/R at `SOUND_RATE` 44100 Hz; non-blocking,
> returns the frames taken — loop with a short sleep while it returns 0) for audio / MIDI
> players. Example: `user/bin/tone.c`.
> **FM instruments (ABI v47)**: fill a `struct kapi_fm_instrument` (2 operators, OPL2-style
> parameters, see `kern/kapi_abi.h`), `kapi_sound_instrument (voice, &ins)`, then
> `kapi_sound_start (voice, milliHz, SOUND_FM, volume)` / `kapi_sound_stop (voice)`. The FM Song
> formats (.FMS / .FMI) and their conversion are in `user/Apps/fmtracker/fms.h` (portable;
> host tests: `sh tools/tests/run_fms_test.sh`).
> **Held keys (ABI v48)**: key events only report presses; an action game polls
> `kapi_key_held (KEY_LEFT)` (also `KEY_RIGHT/UP/DOWN`, `KEY_ENTER`, 27, `' '`, `'a'..'z'` = the
> US position of the key, `'0'..'9'`) every frame — 1 while held and your window has the
> keyboard (USB keyboard and VNC). Returns 0 on an older kernel.
> **USB gamepads (ABI v50)**: `#include "gamepad.h"` — `pad_buttons (-1)` = the `PAD_*` buttons held
> on every pad (or `0..3` for one): `PAD_UP/DOWN/LEFT/RIGHT`, `PAD_A` (bottom face button), `PAD_B`
> (right), `PAD_X` (left), `PAD_Y` (top), `PAD_L/R/L2/R2`, `PAD_SELECT/START`, `PAD_L3/R3`,
> `PAD_HOME`; `pad_read (i, &in)` adds the sticks (`in.lx/ly/rx/ry`, -1000..1000), the USB ids and
> the raw bits. Nothing is pressed while your window has not the keyboard. The pad's raw state
> (`kapi_pad_state`) goes through **`SD:/etc/gamepad.ini`** (a `[vvvv:pppp]` section per pad
> model, `[default]` for the other generic pads; analog triggers as `l2_axis` / `r2_axis`; written by the
> Gamepad app, `padconf`) or the
> built-in mapping (pads Circle knows). Used by gbemu, gamelib, BASIC (`PAD`, `STICK`, `STRIG`).
> Host test: `sh tools/tests/run_gamepad_test.sh`.
> **The GPU (ABI v52)**: `kapi_gpu_info (buf, cap)` (1 = the V3D is usable; the first call brings
> it up) and `kapi_gpu_draw (verts, n, clear, pixels, w, h, stride)`: a triangle list of
> `struct kapi_gpu_vertex { float x, y, z; unsigned char r, g, b, a; }` in normalized device
> coordinates (y up, z −1 near … 1 far), depth-tested and Gouraud-shaded by the GPU into the
> app's pixels (e.g. its window canvas: `canvas.px`, `canvas.stride`). The app transforms and
> lights its geometry itself (floats: build it with `CXXFLAGS_FP`, like `teapot`); keep a
> software path for `r < 0`. See `user/Apps/teapot` (`teapot.h` is portable:
> `tools/tests/teapot/teapot_host.cpp` renders it on a PC) and docs/02 §15.
> **The GPU's full pipeline (ABI v53)**: textures — `int t = kapi_gpu_texture (-1, argb, w, h, stride);`
> (0xAARRGGBB, up to 2048 × 2048, 256 handles; the same call with `t` replaces the pixels,
> with `pixels = 0` frees it; freed anyway when the app ends) — and
> `kapi_gpu_render (&frame, verts, nv, batches, nb)`: `struct kapi_gpu_vertex3 { x, y, z, w, s, t, r, g, b, a }`,
> each `struct kapi_gpu_batch` draws `count` vertices from `first` with its own **4 × 4 matrix**
> (row by row, clip = M · (x y z w): build model × view × projection once per object on the
> CPU, the GPU transforms every vertex; `KAPI_GPU_B_NOMATRIX` if they are already in clip
> space), texture (−1 none), `KAPI_GPU_B_*` flags (depth test / writes, culling, blending
> alpha / add / multiply, linear filter, wrap). Draw opaque batches first, then the blended
> ones with `KAPI_GPU_B_NOZWRITE`. `frame.flags = KAPI_GPU_F_KEEP` draws over the pixels.
> **v54**: each vertex also carries `r2 g2 b2 a2`, a colour **added** after texel × colour
> (0 = unchanged; the N64 emulator's colour combiner is `texel × c1 + c2`), and
> `KAPI_GPU_B_ALPHATEST(t)` discards the fragments whose alpha is below `t` / 255 (cut-out
> foliage, fences, text) without blending or depth sorting.
> **ABI v61 — your own shaders**: `kapi_gpu_program (-1, &prog)` uploads a vertex, a coordinate
> and a fragment shader (V3D 4.2 QPU words, generated at run time with `user/v3d/qpu.h`:
> `qpu::Prog p; p << qpu::I ().a (V3D_QPU_A_FADD, rf (3), r1, r5).ldvary (r0); …`; every app
> links `v3d/libv3d.a`; test them on the PC with `tools/qpu/qpusim` —
> `tools/tests/run_qpu_test.sh`), then `kapi_gpu_render2 (&frame, verts, nv, stride, batches,
> nb, uniforms, nuni)` draws `struct kapi_gpu_batch2` batches of vertices of `stride` floats
> (the clip-space x y z w first, clipped by the kernel) with their program, uniform ranges,
> up to 8 textures (the kernel writes each one's TMU words at `fsUni + texUni[i]`), blending
> (`KAPI_GPU_BLEND2`), write mask and scissor. The vertex shader writes Xs Ys (24.8 fixed
> point: x × w/2 × 256 …) Zs 1/Wc then the varyings; the coordinate shader Xc Yc Zc Wc Xs Ys;
> the fragment shader ends with its TLB writes after the last thread switch — see the v53
> shaders in `kernel/sys/v3d_shaders.qasm` for the recipes, or take the ready-made ones of
> `user/v3d/shaders.h` (`passVS` / `passCS`: the position as it is, the other floats handed on as
> varyings; `flatFS`, `varyFS`, `texFS`; `viewUniforms (w, h)`). `/bin/v3dprog` checks them on
> the Pi (PASS / FAIL); `tools/tests/run_qpu_test.sh` checks the same programs on the PC (the
> instruction restrictions, then the fragment shaders in the simulator). The GameCube's TEV is
> generated by `user/v3d/gxtev.h` (`gxtev::build (config, shader)`: the stages in integers as the
> hardware, the texture lookups, the alpha test, the EFB format -> QPU code + the uniforms and
> varyings the draw must give); `tools/tests/v3d/gxtev_test.cpp` checks thousands of random
> configurations in the simulator against `user/v3d/gxtev_ref.h` (the GLSL TEV of `gxgl.cpp` in
> C++), `/bin/v3dprog` a few on the GPU. `gcemu` draws with them: `user/Apps/gcemu/gxv3d.h` is its
> `gc::GxGpu` -- the app core records each draw (the vertex stage of `gxgl.cpp` in C++, the TEV
> program by its configuration, the uniforms, the state), the main thread gives the frame to
> `kapi_gpu_render2`; `tools/tests/gc/gcv3d.cpp` runs a `.dol` / `.iso` on the PC with the same
> recorder and draws its frame with a software V3D (the shaders in `qpusim`) into a `.ppm`
> (`GCV3D_EVERY=n` more frames, `GCV3D_DUMP=1` / `2` the batches / their programs,
> `GCV3D_SAVE=<f.gxf>` the last frame dumped, `GCV3D_SKIPLOG=<file>` each draw the recorder leaves
> out and why -- `Rec::skipWhy`, `Rec::onSkip`; `GC_CARD=<f.sav>` a memory card, read only,
> `GC_PAD="f0-f1:hex;..."` the pad's buttons held from field f0 to f1, as `gcrun`'s: the games'
> menus scripted -- The Wind Waker's first steps on Outset: Start at 900, A at 1050, 1200, 2200,
> 2300, 2400, Start at 2500, A at 2600, then A every 60 fields to 22000; `GC_HASH=n`, in `gcrun`
> too: MEM1's hash every n fields, two runs compared -- the GL and the TEV renderers gave the
> same game, so a difference was the drawing's; `GCV3D_VHASH=1`: each finished frame's
> recording hashed, a line a frame -- a change of the recorder or the vertex decoder checked bit
> for bit over a whole run). The recorder's vertex loop and `gxPrimitive`'s decoder work from
> plans made once a draw (the matrices of the last index, the lit channels' lights, the texgens,
> the varyings; the attributes present with their formats and arrays, a vertex template) with
> the same arithmetic as before. The recorder's plan (`Rec::Plan`) is **kept from a draw to the
> next**: `planState` (the TEV's configuration and program, the textures, the batch and its
> uniforms, the vertex stage's plan) is made again only when `GxState::serial` changed,
> `planXf` (the lights, the texgens' post-transform matrices) when `Machine::xfSerial` did (every
> XF memory write, the indexed loads too), and the matrices of the vertices' indices stay while
> the XF memory is the same; a draw of the same plan right after its last one goes on in that
> batch. The Wind Waker's draws are 4.6 vertices on average, ~90 % of them with the last one's
> state and XF memory: its recorder 15.7 -> 8.2 ms a field on the Pi (the GX core 92 -> 61 %
> busy). A colour's varying (`/ 255`) comes from a table (the colours are integers 0..255) and a
> texture coordinate's `/ size` is a product when the size is a power of two (both exact: the
> recordings bit for bit the same, `GCV3D_VHASH` over 2400 fields). The GX's reset keeps its
> state's serial and `xfSerial` going (a plan made before must not match). A triangle whose three vertices are before the near plane or
> behind the eye (the kernel's first two clipping planes, `V3DClipDistN`: the kernel would drop
> it) is not recorded — ~10 % of The Wind Waker's, copied for nothing by the recorder,
> `Out::prepare` and the kernel before (the pictures the same: gcv3d; F12's fourth line counts
> them). The recorder keeps the vertices in the clip
> space of the whole EFB (640 x 528) and the scissors in its pixels; the frame's XFB copy
> rectangle, known at its end (`Frame::rect`), is applied when it is drawn (`frameView`,
> `frameScissor`: `Out::prepare`, gcv3d) -- the copy registers of the draw's time were used
> before, and a half-size copy to a texture in the middle of a frame (The Wind Waker's effects)
> framed what followed as a quarter of the picture (the camera "in Link's head"). **On the Pi**, gcemu's F12 third line says what
> the TEV renderer did (`gpu_render2`'s result, the batches drawn / recorded, the ones left out:
> program refused, texture missing, nothing visible, over the vertex limit), F9 dumps the frame
> shown into `SD:/gcdump/frame_<n>.gxf` (the recorded frame, its programs and textures, and the
> picture the GPU made), `--diag[=<folder>]` writes F12's lines of every second into
> `<folder>/diag.txt` (saved every 5 s), dumps a frame every 60 s (the machine waits meanwhile:
> a few seconds over FTP) and quits after 200 s; `--statlog[=<folder>]` writes the same lines
> into `<folder>/statlog.txt` without the dumps and the end (the last ~200 KB kept, saved every
> 5 s: a measure of a game played, F12's lines whole whatever the window's width); `--nodraw`
> (a measure) records the TEV frames but neither prepares nor draws them (the window stays
> still): the machine's speed without the display's work beside it; F12's fourth line is where a field's time went
> on the app core — the GX (`Machine::timeFifo`, all in), its primitives (`timePrim`: decoding,
> state, textures, the recorder), the textures (`timeTex`), the recorder (`Rec::drawTicks`) —
> and a frame's on the main thread (`Out::prepare`, `gpu_render2` — the kernel splits the latter
> in kmsg every 256 frames: `render2, a frame: clip … lists … GPU … target …`); the counters read the ARM's
> clock (`gcClock`, nothing on other hosts) — the folder may be an FTP
> one (`--diag=FTP:<pc>:<port>/<dir>`) so the files land on the PC at once (`--tevbuf`: the
> frames drawn into a buffer of gcemu's, then copied — the GPU no longer writing the window's
> pixels itself). F10: the game's frames a second (its XFB copies) and the speed alone, in a
> corner. `--pmu[=e1,e2,e3,e4]`: the app cores' performance counters, a fifth line (millions of
> cycles a field, instructions a cycle, and a thousand instructions' L1D refills, L2 refills,
> branch mispredictions -- the events, hex, default 08, 03, 17, 10), the CPU's part and the GX's,
> then the share of the machine's cycles inside the JIT's code (`in JIT`, its helpers too) and why
> its runs ended a field (`ends`: a VI line, DI, the audio DMA, AID, DSP, EXI, card, an AI sample;
> then the JIT's entries from C); `--noren` (a measure) prepares the TEV frames but does not draw
> them.
> `--jitprof` (with `--diag`): the JIT's profile every 60 s -- `jitprof.txt` (the costliest
> blocks, the host instructions each guest instruction costs, the instructions left to the
> interpreter, the slow memory accesses) and `jitprof.bin` (the blocks' code: `python
> tools/gc/jitprof.py jitprof.bin --top N --ppc <objdump> --a64 <objdump> --host` disassembles
> both sides). The machine's cores set FPCR to 0 (IEEE: to nearest, no flush-to-zero; nothing
> sets it at boot): F12 shows `FPCR was ...` when it was not.
> **The GX on its own core** (when a second app core is free -- `netcore=0` in `cmdline.txt`
> leaves cores 2 and 3 to the apps): the machine's core writes the FIFO (the write-gather pipe's
> bursts: `gatherFlush` publishes the PI's write pointer), the GX's core runs it meanwhile
> (`Machine::gxStep`: the commands up to that pointer, then `gxDoneW`), reading them in place (a
> command cut by the FIFO's end only is copied). The machine waits for it (`gxSync`) where the
> game can see its progress: a CP / PE / PI-FIFO register, a polling loop the JIT skips (the
> game waiting for the GPU: `idleHit`), a full FIFO (`gxRoom`); the PE's token / finish come back
> through `gxIrqBits` (raised on the machine's core); `gxLock` keeps the frame's flip and the
> texture pool from the main thread's `Out::prepare`. F12's fourth line gives the GX core's busy
> share and the machine's waits; `--gxone` keeps the GX on the machine's core, to compare. `gcv3d --replay
> <frame.gxf> <out.ppm>` draws a dump on the PC at the Pi's size next to the Pi's own picture
> (`out_pi.ppm`): the same picture → the GPU is right, the recording is the question; another
> one → the GPU side. `v3dprog ww <w> <h> <flags> <percent> <variant>` draws The Wind Waker's
> first frame (its TEV program, one quad) into a w × h target — the experiment that found the
> `P_FS_FINAL` hangs (docs/02 §15; variants: +1 the plain varyings shader, +2 RGB8, +4 no
> scissor, +8 / +16 the flat shader final / with a thread switch).
> Example: `user/Apps/gpudemo`. The kernel's shaders are QPU assembly (`kernel/sys/v3d_shaders.qasm`):
> after editing, `cd tools/qpu && make` re-assembles and checks them into `v3d_shaders.inc`
> (committed; the kernel build does not need the tool). docs/02 §15.
> **App cores (ABI v51)**: an app may take a whole core (2 or 3) for a function of its own —
> `int c = kapi_core_acquire ();` (−1: none free), `kapi_core_run (c, fn, arg, stack_top)` (the
> stack is the app's memory, 16-byte aligned), poll `kapi_core_state (c)` (`KAPI_CORE_IDLE` once
> `fn` returned, `_RUNNING`, `_FAULT`), `kapi_core_release (c)` (stops `fn` if needed; done at exit
> anyway). **`fn` makes no kapi call and no allocation** (neither the kernel nor `malloc` is
> multi-core safe) and never masks the interrupts: it computes and talks to the main thread
> through memory — `volatile` flags with barriers (`dmb ish`), `wfe` to wait, the main
> thread's `sev` to wake it, the clock read directly (`cntpct_el0`). Stop it cleanly with a flag
> it polls. Keep working without a free core (run the same function on the main thread).
> **Emulators**: `#include "emucore.h"` does all that — `ec_init (&ec, w, h, frame_fn)` (acquires a
> core if one is free), `frame_fn (EmuCore *)` runs one frame, writes the picture into
> `ec_back ()` then `ec_publish ()`, pushes its sound with `ec_audio_push`; the main thread
> `ec_request (&ec, n)`, `ec_pump` (frames made here when there is no core), `ec_take` /
> `ec_front` (the latest picture: a triple buffer, nobody waits), `ec_audio_pop`, `ec_hold` /
> `ec_resume` around anything that touches the machine (reset, battery save), `ec_shutdown`.
> Two rules the app core imposes: **its code allocates nothing** (the heap, `user/umm.h`, is not
> shared safely between two cores, and `kapi_sbrk` from an app core grows the heap of whatever
> task core 0 is running — make every buffer before, as gc::Machine's texture pool of
> `TEX_POOL` texels); and **take the new picture before asking for the next frame** (in the main
> loop: `if (ec_pending (&ec) == 0 && ec_take (&ec)) ...` first, then `ec_request`) — the other
> way round, a machine slower than real time always has its next frame asked first and the
> window is never drawn again. What reads the machine (its frame, its textures) runs while it
> waits, or under a lock it respects; what only reads a copy may run while it works: gcemu asks
> for the next field, then makes the kernel's batches of the TEV frame (`Out::prepare`, under the
> GX's lock: the recorder's finished frame and the textures kept still; it marks that frame
> `held`: of the recorder's three frames, the GX builds into the other two meanwhile) and draws
> it (`Out::submit` → `gpu_render3`, kapi v62: the kernel reads the frame's vertices where they
> are and frames them onto the XFB copy's rectangle on the way — kapi v63: the recorder's three
> frames are `gpu_vbuf` memory, drawn in place, nothing copied, the frame framed there once
> (`Frame::framed`: drawn again with `view` 0; F9's dump then says the whole EFB); an older
> kernel: the vertices copied into one array for `gpu_render2`, as before) while the field runs — two fields asked for at once when
> behind real time (the drawing, ~30 ms with ~100k vertices, is longer than a field: the
> machine, its field done, no longer idles until the next request; the picture is then taken
> after the second) — and not again when the same frame is already in the window (a 30 fps
> game: every other field). While a field runs the main thread yields (its end seen at once),
> but naps (`kapi_msleep (1)`: to the scheduler's next tick) while that end, expected from the
> last second's time a field, is further than 12 ms — a yield in a loop kept core 0 busy
> (heat: the Pi 4 throttles its clock above ~80 °C, kmsg's `power:` lines).
> See `gbemu` / `gbaemu` / `nesemu` / `snesemu`; `/bin/coretest` exercises the raw kapi.
> **Game kit** (`user/game.h`): `GameView` (a full-window widget: `paint`, `press` / `release` /
> `move` edges, `key`, `tick (dt)` at ~60 Hz), `GameRoot` (ticks it, routes every key to it),
> sound effects on voices 12..15 (`sfx (hz, ms, wave, vol)`, `sfx_later` for jingles,
> `sfx_win` / `sfx_lose`, `sfx_set_mute`; the output is acquired on first use), `rng` / `rng_n`,
> text helpers (`gtext`, `gtext_c` centred with a shadow, `gitoa`, `gcat`). Cards
> (`user/cards.h`): `card_face` / `card_back` / `card_slot` (64×88) and the bouncing-cards
> victory animation (`win_start` / `win_step`). Used by invaders, pipes, solitaire, freecell
> (Arkanoid is now the BASIC game). **Host test**: `sh tools/tests/run_games_test.sh [INVADERS …]` builds each game on
> the PC against a fake kapi table (`tools/tests/wtkhost/host_kapi.h`: every slot a stub,
> files from `sdcard/`), plays a scripted scenario (UBSan) and saves real screenshots to
> `/tmp/onyx_games`.
> **Rich Text Format** (`user/rtf.h`): `rtf::load (box, data, len)` parses an RTF document into
> a `RichTextBox` (styles, colour table → the 16-colour palette, `\'hh` / `\uN` → Latin-1,
> skipped destinations), `rtf::save (box, out, cap)` writes it back; `rtf::is_rtf`. Used by
> `rtfview` (Writer reads and writes RTF itself, with everything: `Apps/writer/fileio.h`).
> **Writer** (`user/Apps/writer/`, one TU: `main.cpp` includes the rest) — `doc.h` the document
> (paragraphs of code points, each with an index into the table of character formats — font of the
> font table, size in half-points, flags, colour, highlight; an image: the character U+FFFC whose
> format names the image and its size —, each paragraph its `ParaFmt` — style, alignment, indents
> and spacing in twips, line spacing, list, page break; the tables only grow, so an edit's undo just
> puts the paragraphs it copied back: `doc_begin` / `doc_end_edit`, typing coalesced), `layout.h`
> (lines at the zoom in 1/64 px from the fonts' design advances, each character's x kept —
> `Para::xs` — for drawing, hit-testing and the caret alike; lists' numbers; pages), `edit.h` (the
> selection, the edits, the clipboard — the system's text plus the piece of document kept here —,
> find), `view.h` (the page view widget: drawing, caret blink without a redraw, mouse, keys, images'
> resizing), `ui.h` (tool buttons, pick boxes dropping a `ListPopup`, the size box, colour popups,
> the ruler, the status bar), `icons.h` (`VPath` icons), `fileio.h` (RTF in / out — pictures as
> `\pict\pngblip` / `\jpegblip`, a PNG made when the image came as something else —, text, HTML),
> `dialogs.h`. A host test worth keeping in mind: random edits undone then redone must give back
> the same RTF. Host test: `run_games_test.sh RTF` (render + save / reload round trip).
> **Cardfile** (`user/Apps/cardfile/`, one TU: `main.cpp` includes the rest; integer only, no
> libc) — `model.h` the document: a form (`Field`: display name, column name, type `FT_*`,
> decimals, choices) and its records (an array of heap strings a record, the empty value shared),
> each value kept in one canonical text a type (integer `-12`, decimal `12.50` at the field's
> decimals — a `long long` scaled by 10^decimals when computed —, date ISO `2026-09-29`, colour
> `#3366CC`, yes / no `yes` or empty); the values read as typed (`parse_num`: `.` or `,` as the
> point, group separators; `parse_date`: D/M/Y, D.M.YY, D/M, DDMMYYYY, ISO), shown (`value_show`),
> made (`value_new`), compared (`value_cmp`: numbers, dates, a choice by its list's order, text
> folded — Latin-1 case and accents — with its runs of digits compared as numbers); the file
> (`doc_write` / `doc_read`: an INI-like head, the records tab-separated with `\t \n \\` escapes,
> read leniently — the format is described at the top of `main.cpp` and in docs/04); a type
> changed (`convert_count` / `convert_field`: through the value's shown text); the fields
> inserted, removed, moved (`doc_remap`: every record laid out again); CSV (`csv_write`,
> `csv_read` with each column's type guessed); the order (`build_order`: the search's words, a
> stable merge sort, empty values last). `widgets.h`: the editors — `LineEdit` (any length,
> selection, clipboard, a filter of the characters; over a `TextCore` it shares with `MemoEdit`,
> wrapped at the words), `DateEdit` + `CalPopup` (wtk's `Calendar`), `ColorEdit` + `ColorPopup`,
> `ChoiceBox` + `PickList` (a scrolling list over the window), `YesNoBox` —, the toolbar, the view
> switch, the search box, the navigator (`NavBar`), `VPath` icons. `formview.h` (the card: an
> editor a field, the commit and its validation, Tab order, scrolling), `listview.h` (a
> `DataGrid`), `designview.h`, `app.h` (the state the views share). Undo keeps the whole document
> written before each change (100 steps, 24 MB at most; one control's edits coalesced into one
> step). **Host test**: `sh tools/tests/run_cardfile_test.sh` (the values as typed, a round trip
> byte for byte, a file edited by hand, a type changed, fields moved, CSV, the order; ASan +
> UBSan).
> **Game Boy / Color core** (`user/gb/gb.h`, `gb/libgb.a`, linked into every app): `gb::Machine`
> — `load (rom, size)` (CGB mode from the header), `runFrame ()` → `fb` (160×144, 0x00RRGGBB),
> `setButtons (gb::BTN_* mask)`, `setAudioRate (hz)` + `audioRead (lr, n)` (s16 stereo),
> `setSaveRam` / `sram` / `sramDirty` (battery saves), `setDmgPalette`. The SM83 CPU with
> instruction timing, a scanline PPU (DMG + CGB), timer, OAM DMA / HDMA, MBC1/2/3(+RTC)/5, the
> 4-channel APU; integer only, no libc. Used by `gbemu` (the emulator: paced by the audio
> queue, else the clock; the machine on an app core through `emucore.h`) and `gamelib` (the
> library: title-screen thumbnails made headless). The ROM windows are pointers set when the
> game switches banks (`mapRom`), the background and window are drawn a tile at a time.
> **Host test**: `GB_TEST_ROMS=<unzipped c-sp game-boy-test-roms> sh tools/tests/run_gb_test.sh`
> (Blargg cpu_instrs / instr_timing / halt_bug, dmg-acid2, cgb-acid2 pixel-exact);
> `tools/tests/gb/gbtest.cpp <rom> <seconds> [out.ppm] ["t:mask,..."]` runs any ROM headless.
> **Game Boy Advance core** (`user/gba/gba.h`, `gba/libgba.a`): `gba::Machine` — the same shape as
> `gb::Machine` (`load`, `runFrame` → `fb` 240×160, `setButtons (gba::BTN_*)`, `setAudioRate`,
> `audioRead`, `setSaveRam` / `save` / `saveSize` / `saveDirty` / `saveType`). The ARM7TDMI (ARM +
> Thumb, the two prefetched opcodes kept: self-modifying code sees them), the memory map with
> WAITCNT wait states and open bus, the frame loop driven by events (line phases, timer
> overflows), DMA (immediate, V/H-blank, sound FIFO), 4 timers (cascade), the PPU a line at a
> time (modes 0-5, affine, sprites, windows, blending, mosaic), the PSG + Direct Sound A / B, the
> BIOS calls done in C++ (`gba_bios.cpp`: no BIOS image), SRAM / Flash 64-128 KB / EEPROM
> (found from the ROM's library string). Used by `gbaemu` and `gamelib`. **Host test**:
> `GBA_TEST_ROMS=<jsmolka gba-tests> sh tools/tests/run_gba_test.sh` (arm, thumb, memory, bios,
> nes, unsafe, saves); `tools/tests/gba/gbatest.cpp <rom> <seconds> [out.ppm] [keys]` runs a
> game headless (`GBA_SHOTS`, `GBA_AUDIO`, `GBA_SAVE`, `GBA_LOAD`, `GBA_REGS`; built with
> `-DGBA_DEBUG`, `GBA_WATCH=<addr>` prints every write there).
> **NES core** (`user/nes/nes.h`, `nes/libnes.a`): `nes::Machine` — the same shape (`load` an
> iNES / NES 2.0 file, `runFrame` → `fb` 256×240, `setButtons (nes::BTN_*)`, `setAudioRate`,
> `audioRead`, `setSaveRam` / `sram` (8 KB) / `battery` / `sramDirty`, `setPal`). The 6502 (official
> + stable unofficial opcodes, from the generated table `nes_optable.inc`), stepped an instruction
> at a time with the PPU and APU brought up to each access; the PPU (`nes_ppu.cpp`) renders a line
> at dot 1 from the loopy `v` / `t` registers, with the sprite 0 hit dot, vblank / NMI and the A12
> edges the MMC3 counts; the APU (`nes_apu.cpp`: 2 pulses, triangle, noise, DMC, the frame counter
> and its IRQ) mixes through the nonlinear tables `nes_mix.inc`. Mappers 0, 1, 2, 3, 4, 7, 66; NTSC
> and PAL (312 lines, 3.2 dots per CPU cycle, the PAL APU tables; `load` reads it from the header,
> `nesemu` also from the file name). Used by `nesemu` and `gamelib`. **Host test**:
> `NES_TEST_ROMS=<christopherpow nes-test-roms> sh tools/tests/run_nes_test.sh` (nestest against
> its trace, blargg all_instrs / instr_timing, apu_test 1-8, mmc3_test 1-3 and 5);
> `tools/tests/nes/nestest.cpp run <rom> <seconds> [out.ppm] [keys]` runs a game headless
> (`NES_PAL=1`, `NES_AUDIO=<file>`).
> **Super Nintendo core** (`user/snes/snes.h`, `snes/libsnes.a`): `snes::Machine` — the same
> shape (`load` a `.sfc` / `.smc`, a copier header skipped; `runFrame` → `fb` 256 × `height` (224,
> 239 with overscan); `setButtons (snes::BTN_*)`, the pad's 12 buttons as the joypad word;
> `setAudioRate`, `audioRead`; `setSaveRam` / `sram` / `sramSize` / `sramDirty`; `setPal`; `title`,
> `hirom`, `chip`). `snes.cpp`: LoROM / HiROM (the header that scores best), the bus as 4 KB
> pages (direct reads, the access speed of each page, FastROM), the 5A22's registers, DMA and
> HDMA (direct / indirect, the 8 transfer modes), a line = 1364 master clocks with its events (HDMA
> start, DRAM refresh, the H/V IRQ, the line drawn + HDMA) run between the instructions.
> `snes_cpu.cpp`: the 65C816 (every opcode and mode, the emulation-mode wraps, decimal mode, block
> moves). `snes_ppu.cpp`: modes 0-7 a line at a time (tile rows decoded 8 pixels at a time,
> offset-per-tile, mosaic, direct colour, Mode 7 + EXTBG, 32 sprites a line, the windows, colour
> math, brightness; modes 5 / 6 at half their width). `snes_apu.cpp`: the SPC700 with its IPL ROM
> and timers, the S-DSP (BRR, gaussian interpolation — `snes_gauss.inc`, a generated gaussian
> kernel —, ADSR / GAIN, noise, pitch modulation, echo + FIR), resampled from 32 kHz; the sound
> unit catches up with the CPU when the CPU touches its ports and every 16 lines. No enhancement
> chip (`load` refuses them). Used by `snesemu` and `gamelib`. **Host test**:
> `SNES_TEST_ROMS=<PeterLemon SNES> sh tools/tests/run_snes_test.sh` (the CPU, SPC700 and PPU test
> ROMs against their reference pictures; `SNES_CPUTEST=` gilyon's cputest-full.sfc, 649 tests);
> `tools/tests/snes/snestest.cpp <rom> <seconds> [out.ppm] [keys]` runs a game headless
> (`SNES_SHOTS`, `SNES_AUDIO`).
> **Nintendo 64** (`user/n64/`, in progress): `n64_cpu.cpp` the R4300i interpreter (MIPS III, COP0
> with the TLB / exceptions / Count-Compare, COP1), `n64_bus.cpp` the RCP interfaces (MI, VI,
> AI, PI DMA as ares does it, SI + PIF, SP / DP registers) and the boot (the IPL3's work done
> directly, CIC 6101-6106), `n64_gfx.cpp` the graphics tasks at a high level (F3DEX2: matrices,
> lit vertices, triangles; the RDP: TMEM loads, tiles, the texture formats decoded into a cache,
> the combiner evaluated per vertex, blending / depth — the decal z mode drawn 2·10⁻⁴ nearer in NDC
> depth, against z-fighting —, fill and texture rectangles; the rectangles into an off-screen colour image
> drawn by the CPU straight into RDRAM, and the renderer's framebuffers written back into RDRAM from
> the host's copy of the last frame (`Machine::fbSnapshot`, called by n64emu / NintendoEMU after each
> frame shown) when a game loads a texture from one — Ocarina of Time's pause background, copied
> into the z-buffer, its 8-bit coverage copy written as full so that the CPU anti-aliasing filter
> leaves it alone: the menu opens in ~1 s instead of ~4) into a
> `GFrame` of clip-space vertices + batches in the kapi v54 layout; `n64_audio.cpp` the audio
> tasks at a high level (the "nead" microcode of Zelda OoT / MM, recognised by its data: VADPCM,
> resampling, envelope mixer, interleave on a 4 KB DMEM image; other audio microcodes: silent)
> and the AI output (each DMA's frames resampled into a ring, `audioRead`).
> `n64emu` draws the frames with `kapi_gpu_render` (the machine on an app core, lockstep).
> **Host test**: `N64_TEST_ROMS=<PeterLemon N64> sh tools/tests/run_n64_test.sh` (89 of the 94 CPU
> test ROMs match their pictures); `tools/tests/n64/n64test.cpp <rom> <frames> [out.ppm]` runs a
> game headless (`N64_GFX=out.ppm`: the last graphics frame drawn by the BASIC 3D's software
> renderer; `N64_STATE`, `N64_THREADS` (libultra's threads found in RDRAM), `N64_WHO`, `N64_MEM`,
> `N64_WAV=out.wav`: the sound, 32 kHz stereo).
> **GameCube** (`user/gc/`, in progress): `gc_cpu.cpp` the Gekko (PowerPC 750CL) interpreter:
> integer, branch, the SPRs (BATs, HIDs, GQRs, the locked-cache DMA, timebase, decrementer), the
> exceptions, the FPU (exact single <-> double conversions, 25-bit multiplicands), the paired
> singles and the quantized loads / stores; `gc_mem.cpp` MEM1 (big-endian), the locked cache,
> the BAT translation (a map of 128 KB blocks); `gc_hw.cpp` PI, VI (lines, display interrupts,
> the YUV framebuffer), SI pads, EXI (RTC / SRAM), DI (disc reads), AI, the DSP interface
> (audio / ARAM DMA); `gc_gx.cpp` the CP / PE and the GX FIFO (linked mode: read as the CPU
> writes it; the commands, display lists, vertex sizes from VCD / VAT, tokens, "draw done");
> `gc_gxdraw.cpp` the drawing at a high level: vertices decoded and transformed (XF matrices,
> projection, viewport), lit, texgens, the TEV per vertex (texel x c1 + c2), the GX texture
> formats (TLUTs in TMEM; their mipmaps for a GPU) -> a `GFrame` in the kapi v54 layout, ended by
> the EFB -> XFB copy — or, with a GPU backend (`Machine::gpu`), the draws as the game gave them
> (`gc_gxgpu.cpp`, below); `gc_dsp.cpp` the DSP at a high level (as Dolphin's HLE): the ROM's
> boot mails, the microcode told by its CRC (AX, the "Zelda" one and its variants' flags, the
> memory card's), the mail queue with its interrupts (a mail's IRQ raised when the previous one is
> read, none seen while the DSP is halted), the task switch (DSP_YIELD, a new microcode, DSP_RESUME);
> AX's protocol acknowledged (silent), the Zelda protocol (command lists acked with sync mails, the
> frames rendered as the CPU says the voices are ready, DSP_FRAME_END) with its **audio renderer**
> `gc_zelda.cpp` (Dolphin's ZeldaAudioRenderer: the 0xC0-word voice blocks, AFC ADPCM and PCM8 /
> PCM16 from ARAM or MRAM, 4-tap resampling, volume ramps, Dolby, the reverbs, 0x50-sample frames
> into the game's buffers — a game on this microcode, e.g. The Wind Waker, waits for its streams
> to play: without it, a black screen after the intro); `gc_card.cpp` the **memory card** (an EXI
> device: the Nintendo commands 00 / 52 / 81 / 83 / 85 / 89 / F1 / F2 / F4, DMA with the card's
> delays, EXIINT, the EXT bit; a blank card formatted as the SDK's CARDFormat would, its serial
> from this console's SRAM, the SRAM's flash ID taken back from the card; the flash is the front
> end's: `cardInsert`, Dolphin's `.raw` layout, `card[s].dirty` after a write); `gc_boot.cpp` the
> IPL's state, `.dol` loading, discs booted by running their apploader on the CPU.
> The **sound out**: the audio DMA's 32-byte blocks (8 frames, right channel first, big-endian;
> 32 or 48 kHz by AICR's AIDFR, 32 kHz as the IPL leaves it) resampled to the front end's rate
> (`setAudioRate`) into a ring that `audioRead` empties. `runFrame` runs **one field** (the lines
> 1..313 / 314..625 in PAL): a front end calling it 50 / 60 times a second plays in real time
> (it ran a whole frame before: twice too fast).
> `gcemu` reads a disc image on demand (`kapi_seek`, the main thread serving the app core).
> The **write-gather pipe** (0x0C008000) keeps its bytes (`gather`) and sends them to the GX FIFO
> 32 at a time, as the hardware (`gatherFlush`: the burst at the PI's write pointer, then the
> commands run); the JIT appends a store to it inline (a call only at each burst).
> **The JIT** (`gc_jit.cpp`, AArch64 hosts): `Machine::jitEnable()` (code memory from the host's
> `gc::codeAlloc` hook: `kapi_code_alloc` (ABI v58) on Onyx, `mmap` RWX in the host test), then
> `run()` goes through `jitRun()`: the PowerPC code is translated a **block** at a time (to its
> branch, ≤ 64 instructions, within a 4 KB page) by a small built-in AArch64 assembler. The
> guest registers live in the `Machine` (x19 = it, x20 = MEM1, x21 = the helpers + a 64K-entry
> direct-mapped table of blocks, w22 = MEM1's size); **within a block** the GPRs, CR, XER, LR and
> CTR it uses are cached in host registers (x9–x15, x18: loaded at first use, the dirty
> ones written back at the exits, before the interpreter, and around a slow-path call); **LR,
> CR, r3 and r0** — the most used — **live in x24, x27, x28, x29 across the blocks** (`SRA_G` /
> `SRA_H`: callee-saved, the helpers keep them; loaded by the enter stub, stored by the exit
> stub and around the interpreter's calls, `lmw` loading them straight: no load at a block's
> first use, no store at its exits); the
> integer unit, CR logic, rotates,
> shifts, compares, divw / divwu, the branches (CTR / CR conditions, LR), mftb, the loads / stores,
> lmw / stmw and dcbz (MEM1 in one go) are native (a
> load / store whose address maps MEM1 through the OS's standard BATs, or in real mode, reads
> the host memory directly and byte-swaps -- with the BATs, the 0x80000000 mirror checked by one
> EOR and a compare, the value loaded straight into the destination's register or stored from the
> source's, the uncached 0xC0000000 one tried first on the cold path; a D-form access through a
> base register the translation finds in MEM1 (and not yet written in the block) makes that
> register's host pointer (`basePtr`: checked there -- not MEM1's then: the instruction by the
> interpreter, and out of the block) and the block's later accesses through it are one load /
> store with the displacement (`ptrAccess`; MEM1 has a guard zone of `MEM1_GUARD` bytes on each
> side for the displacements past its ends); the rest calls `read32`… with the cycle count exact),
> every other instruction (the FPU, the paired singles for now) calls the interpreter's `exec`.
> The FPU and the paired singles are native too, on NEON: `ps[32][2]` keeps both halves of an FPR
> side by side, so an FPR is one q register, cached like the GPRs (q8–q31, lane 0 = ps0); the
> paired singles compute both halves at once (FADD / FMLA .2D, by-element for muls0 / madds1,
> FCVTN + FCVTL for the single rounding, ZIP / EXT for the merges, FCMGE + BSL for ps_sel); the
> plain FPU works on lane 0 (fadd… fmadd, fres / frsqrte (ps_res / ps_rsqrte on both), the 25-bit multiplicand — skipped when the operand is
> known to hold a single exactly —, fsel, fcmp, frsp, fctiwz, the moves), lfs / lfd / stfs /
> stfd, psq_l / psq_st and their indexed forms, **specialised for the GQR's value found at
> translation** (a float type: one 8-byte access for the pair; u8 / u16 / s8 / s16 with a scale:
> both elements in one access, converted and scaled, or rounded to single, scaled, truncated and
> clamped), the GQR checked at run time (another value: the interpreter); a NaN result re-runs the
> instruction in the interpreter (from the cache's state at that branch), FPRF is set lazily
> (the source FPR noted at translation, `fprfVal` / `fprfPending` written only when needed).
> The cycles are a countdown in x26 (to `jitEnd`, = `jitUntil` when set; written to `cycles` when
> leaving and before a helper that reads them, resynced after one that may change `jitUntil`).
> The rare paths (a slow memory access, a NaN, a conversion's odd case, the interpreter leaving,
> an exit's stub) are emitted apart, each with the cache's state at its branch: the code buffer
> is in 1 MB chunks, the blocks' main code from a chunk's start up, their rare paths from its end
> down (a b.cond's reach) -- the main code dense in the I-cache (`--pmu=08,02,05,01` counts the
> TLB and L1I refills: the L1I ones were ~20 a thousand instructions, 17.5 with the split; the
> TLBs, with Onyx's 64 KB pages, ~0.3). In The Wind Waker's game (Outset, ~96k vertices a frame)
> `--pmu=08,01,52,53` gave 11.4 L1I refills and 3.2 L2 read refills a thousand instructions
> (0.1 write ones) — about as many as the L1D refills: the machine's data comes from the RAM,
> its IPC ~0.84; with `--nodraw` 37 → 30 M cycles a field for the same instructions (IPC 1.02):
> the display's copies on core 0 (through the L2 the four cores share) cost it ~18 %.
> A block's exit to a known address is **linked**:
> once that block is translated the exit's branch is patched to jump straight to it (back to
> its stub when that block is dropped), while `cycles < jitUntil` (the next event / the
> decrementer; `piUpdate` and `decWrite` zero it to come back); an exit to a register (blr,
> bctr) goes through the table; `jitRun` (C) takes the interrupts, translates what is missing.
> **Calls and returns are the host's own**: a call (`bl`, `bcctrl`) pushes a pair {its landing,
> the guest's return address} on the host's stack (`stp x1, x24, [sp, #-16]!`) and calls the
> target's block with `bl` (linked like an exit) / `blr` (`bcctrl`: the block looked up in the
> table there); a return (`blr`, `bclr`) pops the pair and, LR being its address, is a `ret` to
> the landing — which the core's return stack predicts —, else the stack is emptied and the
> dispatcher goes on (`retTo`, `callTo`, `callReg`); the landing, in the caller's block, is a
> linked exit to the instruction after the call (the host registers are the callee's there:
> the cache dropped). The stack holds a sentinel at `jitSpBase` (no LR is 1); it is emptied when
> the JIT leaves (the exit stub), at a mismatch, and past 8 KB (`jitSpLimit`: calls that never
> return, a return through `bctr`); a landing stays valid while the JIT runs (a dropped block's
> code stays in the buffer until `flushAll`, which runs from `jitRun` only). `bl $+4` (the pc
> read) pushes nothing. The Wind Waker: the branch mispredictions 5.6 -> 2.0 a thousand
> instructions, 37 -> 42 fields/s (its hottest loop, a list searched through two function
> pointers, went through the dispatcher four times a node). `gctest calltest` checks it
> against the interpreter. Blocks are keyed
> by address + MSR IR / DR; `icbi`, the DVD / ARAM / locked-cache DMAs drop the blocks of the
> 4 KB pages written; a BAT change, HID0's ICFI or a reset drop everything. `b .` jumps to the
> next event (idle), and so does a **polling loop** (a block of loads, compares, masks branching
> back to its start, each register it reads set earlier in the same pass or not by it at all:
> nothing changes until an interrupt / the hardware — the VBlank waits, the DSP's mail). A
> compare (or a record form) right before a conditional branch on its field's LT / GT / EQ: the
> branch tests the host's flags, and the CR field is made only on the paths where it may still
> be read (`crSet`, `crDead`: the guest code of the block's 4 KB page scanned from each path --
> set again before a read: not made; a call (`bl`) sets CR0 again as far as the caller knows —
> the ABI's volatile field: the callee may set it, the caller does not read it after without
> setting it again —; a return, leaving the page, a CR logic op on it: made) -- an exception
> that does not come back then leaves the older field, which only a crash report shows. `gcemu`: Game ▸ *Interpreter (no JIT)* / `--interp` to compare.
> Tested by `run_gc_test.sh` under `qemu-aarch64` (`GC_JIT=1`): cputest / pstest / hwtest /
> gxtest identical to the interpreter's, `gctest fuzz` (random sequences of FPU / paired-single /
> load-store / integer / branch instructions from random states with NaNs, infinities,
> denormals, run by both, every register, the FPSCR and the memory compared: it found a
> cache bug and an interpreter bug — `srawi` / `sraw` with rA = rS took CA from the result;
> `GC_FUZZC=1` their data through the uncached mirror, `GC_FUZZSTART=n` from the n-th,
> `GC_FUZZDUMP=<f>` each one's code, `GC_FUZZPROG=<f>` + `GC_FUZZLEN=n` a failing one cut down,
> `GC_FUZZG="3 11"` those GPRs, CR and XER printed, `GC_FUZZBLOCK=<pc>` the block there's code;
> `gctest calltest`: a program of calls built there (fib, a chain of 1500 calls, calls through a
> table, a tail call, `bl $+4`, a return to LR + 4, returns through `bctr`) run by both, twice
> (the blocks kept the second time), every register and the stack compared;
> `GC_FUZZ2=1` each one run a second time with the JIT's blocks kept and the data through the
> other mirror (the blocks' base pointers meet another value);
> the CR is not compared after an exception that stopped the run -- see `crDead`), `tools/tests/gc/bench.c` (sort, CRC, copies, calls)
> against qemu-ppc's result: ~40× the interpreter's speed (integer), ~10× (float);
> `GC_PROFILE=1` prints the host instructions per guest instruction, `GC_DUMP=prefix` the
> hottest blocks' code (for `aarch64-linux-gnu-objdump -b binary -m aarch64`); `gctest jitsize
> <jitprof.bin>` translates again the hot blocks of a Pi's profile (gcemu `--jitprof`) and prints
> their host instructions a guest one weighted by the runs counted there (6.56 → 5.85 with the
> base pointers, the hot / cold chunks and the kept registers), `GC_JITDUMP=<pc>` one's code.
> **The x86-64 JIT** (`gc_jit_x64.cpp`, NintendoEMU / x86-64 Linux hosts; `gc_jit.cpp` is the
> AArch64 one): the same design (blocks, links, the dispatcher's table, idle loops, the rare paths
> after the block, the cycles as a countdown) with a small x86-64 assembler (REX / ModRM / SIB,
> SSE2, VEX for FMA3): the guest GPRs cached in rbx, rbp, rsi, rdi, r12, r10, r11, the FPRs in
> xmm6–15 (a paired single = one xmm, both halves), the constants in the code buffer
> (RIP-relative); the helpers take only the `Machine *` (their arguments in `jitArg[]`: the
> Windows and System V ABIs alike). The same fuzz test (`gctest fuzz`) checks it against the
> interpreter. Two fixes found with it and shared: HID0's ICFI / DCFI read back 0 (a game setting
> ICFI flushed every block), and each AI sample is an event while the AI plays (`__AI_SRC_INIT`
> polls its counter).
> **The GX on a GPU with shaders** (`gc_gxgpu.cpp`): a front end sets `Machine::gpu` (a
> `GxGpu`: `draw`, `copy`); each primitive then reaches it as the game gave it — its vertices in
> model space with their matrix indices (`GxVertex`), its triangles / lines / points as indices —
> with a `GxState`: the XF's state (colour channels, texgens, projection) and the BP's (the 16 TEV
> stages, swap tables, konst colours, alpha test, z texture, fog, indirect texturing, depth /
> blending / culling / scissor, the maps' textures) laid out as a std140 uniform block, rebuilt
> when a register changed (`gxsDirty`; the XF memory — matrices, lights — has its own
> `xfSerial`). The EFB copies go to it too: to the XFB (the picture), or to a texture — kept by the
> GPU, not written to MEM1: a slot (`efbCopies`) remembers the address and a hash of MEM1 there,
> so a texture read from that address (MEM1 not written since) is the copy. NintendoEMU's
> backend is `pc/NintendoEMU/core/gxgl.cpp` (below); without a backend (the Pi today) nothing
> changes.
> **Host test** (`sh tools/tests/run_gc_test.sh`, needs gcc-powerpc-linux-gnu + qemu-user):
> `tools/tests/gc/cputest.c` compiled once runs under `qemu-ppc -cpu 750` and in the interpreter
> (10485 result words identical), `pstest.S` the paired singles against the manual, `hwtest.c`
> and `gxtest.c` bare-metal programs (built with `tools/gc/elf2dol.py`): the VI's picture and
> interrupts, the FIFO / PE, a textured quad and a shaded triangle drawn by the GX path.
> **Doom** (`user/doom/`): doomgeneric (`third_party/doomgeneric`, GPL-2.0, only the portable
> sources; `ONYX.md` lists the three `#ifdef ONYX` changes) built against **newlib** like the
> `/bin` libc tools (`../libc/crt0libc.S` + `onyx_syscalls.c`, `main (void)` + `kapi_get_args`),
> by `user/doom/Makefile` (called from `user/Makefile`) into `user/doom.elf`. `doom_onyx.c`: the
> `DG_*` platform functions — the window canvas *is* `DG_ScreenBuffer` (640 × 400, no copy),
> full screen at 4:3, keys from `kapi_key_held` + modifiers + key events (Tab, F-keys…),
> `gamepad.h`, `rename`/`mkdir` on the kapi; `doom_wtk.cpp`: window chrome + menu (wtk from C);
> **The engine runs on an app core** when one is free: `main` starts `doomgeneric_Tick` there
> after `doomgeneric_Create`; the main thread keeps the window, the input (a key queue), the
> pictures (a triple buffer: the engine swaps `DG_ScreenBuffer` between three slots), the
> sound output and the music. The engine's syscalls (malloc's `sbrk`, stdio, files, saves,
> `exit`) run on the main thread through the **libc RPC** (`libc/onyx_syscalls.c`:
> `onyx_rpc_enable`, `onyx_rpc3`, `onyx_rpc_serve` — any newlib program can use it for code on
> an app core; the main thread must then not use malloc meanwhile);
> `doom_sound.c`: `DG_sound_module` (16-channel mixer of the DS* lumps, run by the engine into
> a PCM ring that the main thread moves into `kapi_sound_write`)
> and `DG_music_module` (MUS and MIDI turned into one timed event list, played on the 16 kernel
> FM voices with the GENMIDI OPL patches converted to `kapi_fm_instrument`). **Host test**:
> `sh tools/tests/run_doom_test.sh` (headless on a virtual clock: frames, sound effects, FM notes).
> **Graphing calculator expressions** (`user/Apps/graphcalc/expr.h`): `gc::Parser::compile (src,
> program)` → an RPN `gc::Program` (`eval (x)`), `gc::fmt`; the maths of the BASIC core
> (`basic/basnum.h`). An app computing in `double` builds with FP: in `user/Makefile`,
> `graphcalc.elf: CXXFLAGS := $(CXXFLAGS_FP)`. Host test: `sh tools/tests/run_graphcalc_test.sh`.
> **HTTPS from a wtk app**: wtk apps are freestanding; do the TLS work in a newlib console
> tool and spawn it with pipes (`kapi_pipe`, `kapi_spawn`, write the request, `kapi_stream_eof`,
> poll `kapi_stream_read_nb` / `kapi_proc_done` from `Root::onTick`). Example: Lisa +
> `/bin/groq` (`user/Apps/lisa`, `user/bin/groq.cpp`).
> **Wi-Fi scan (ABI v45)**: `kapi_wlan_scan (ap, max)` fills `struct kapi_wlan_ap` entries
> (ssid, bssid, security `WLAN_SEC_*`, channel, freq, level dBm, connected), strongest first;
> it blocks ~3 s. Examples: `user/bin/wifiscan.c`, `wpaconf` (Scan button + Combobox).
> An app that **moves or renames** files should call `shelf_moved (from, to)`
> (`#include "shelfmsg.h"`, IPC to the `shelf` service: the dock's switcher) so the shelf's
> references follow.
> **WPF-style controls (P5)** — all in `wtk/wtk.h`, see `user/Apps/widgets` for each in use:
> `RadioButton (l, t, w, h, text, group, checked, cb)` — exclusive per `group` among its
> siblings (`wk_radio_checked (parent, group)`); `GroupBox (l, t, w, h, title)` — a titled
> frame, add controls as its children; `ToggleSwitch (…, text, on, cb)`;
> `NumericUpDown (…, min, max, value, step, cb)` — arrows, wheel, Up/Down, typed digits;
> `ListBox (…, onSelect, onActivate)` — `add`, `clear`, `item (i)`, `sel`, `setSel`;
> **`DataGrid (l, t, w, h)`** (`wtk/datagrid.h`) — a read-only table of rows and columns,
> virtual (a grid of any length costs what it shows): `setColumns (n)`, `setColumn (c, title,
> width, GRID_LEFT / GRID_RIGHT / GRID_CENTRE)`, `column (c)`, `autoSize (c, minW, maxW)` (the
> title's and the first rows' texts), `setRows (n)`, `sel`, `setSel (r)`, `ensureVisible`, `rowAt`;
> the app gives each cell's text (`cellText (grid, row, col, buf, cap)`) or draws it itself
> (`cellDraw (grid, cv, row, col, x, y, w, h, ink, selected)`: `cv` clipped to the cell; false →
> the text is drawn); a click on a title fires `onSort` (`clickedCol`: the app orders its rows and
> sets `sortCol` / `sortDesc`, the grid shows the arrow; `sortable = false`: plain titles), a
> title's right edge drags the column's width; `onSelect`, `onActivate` (a double click, Enter),
> `onContext` (a right click: `ctxRow` — -1 below the rows —, `ctxX`, `ctxY`); stripes, the row
> under the pointer tinted, both scroll bars (Shift + the wheel, Left / Right: sideways),
> `emptyText`, `user` (the app's). Used by Cardfile (its list, its design's fields);
> `TreeView (…, onSelect, onActivate)` — `add (parent, label)` → id, `expand`, `sel`,
> `label (id)`, `setUserData`; `Calendar (l, t, y, m, d, cb)` (size `CAL_W`×`CAL_H`) and
> `DatePicker (…, y, m, d, cb)` (`format (buf)` → `YYYY-MM-DD`); `ImageBox (…, IMG_FIT /
> IMG_FILL / IMG_NONE)` — `load (path)` (any `imgload` format) or `setPixels`;
> `wk_color_dialog (&color, title)` — RGB sliders + palette + preview, true = OK.
> `wk_file_open` / `wk_file_save` / **`wk_folder_open (out, cap, startDir)`** (a folder, no file
> name) — the file dialog; `..` at a volume's root lists the **volumes** that are mounted
> (`SD:`, `SD1:` … `SD3:` — the SD card's partitions, `USB:`…). Paths may start with any
> volume (`SD1:/roms/x.iso`); `kapi_fsize` is clamped to 4 GB − 1, **`kapi_fsize64`** (ABI v59)
> gives an exFAT file's real size; `kapi_rename` fails across volumes (copy + remove instead).
> `Combobox (l, t, w, h, text, onEnter, onPick)` — an editable `Textbox` with a drop-down
> list of suggestions (`addOption`, `clearOptions`, `pick (i)`; arrow click or Down / Up;
> `onPick` fires with `picked` = the index). Used by the File Viewer's Connect dialog.
> `Dropdown (l, t, w, h, options, n, initial, cb)` — the non-editable sibling, same look
> (field + drop button): `sel`, `setOptions`; Up / Down change the selection, Enter / Space
> open the list, Esc closes it; `cb` fires when the selection changes. The option strings
> are not copied (they must outlive the widget).
> **Keys with modifiers**: Ctrl / Shift + arrows, Home, End, Page Up / Down arrive as the
> plain `KEY_*` code; test `kapi_get_modifiers () & MOD_CTRL` / `MOD_SHIFT` (e.g. the FM
> Tracker's Ctrl+Up / Down transpose, the text widgets' Shift selection). Inside a key
> handler, `kapi_get_modifiers` returns the modifiers held **when that key was typed** (the
> kernel stores them in the key event, taken from the xterm `ESC[1;<m>X` form, which `vncd`
> also sends), not the live state. F1–F12 arrive as `KEY_F1` .. `KEY_F12` (0x110..0x11B). Text widgets (`Textbox`,
> `Textarea`, `RichTextBox`) accept the printable Latin-1 range too (`é è à ç ù €`… = 0xA0–0xFF,
> as the keymaps produce them).
> **Text selection**: `Textarea` and `RichTextBox` select with Shift + navigation keys, a
> mouse drag, Shift+click and ^A; typing replaces the selection. `Textarea` has
> `hasSelection`, `selStart` / `selEnd`, `selectedText`, `deleteSelection`, `selectAll`,
> `copy` / `cut` / `paste` (system clipboard; ^C / ^X / ^V work by themselves when the app's
> menu does not take them).
> **Tooltips**: set `widget->tip = "text"`; the `Root` shows it after the pointer rests
> ~0.6 s. (No RTTI: `Widget::asRadio ()` identifies radio buttons.)
> **`wtk::Root::onTick ()`** (virtual) runs once per event-loop iteration — poll a mailbox,
> a spawned process or a timer there. **`ask.h`**: `ask_begin (title, msg, yes, no)` opens
> the system Yes / No window (`apps/ask`) without blocking; `ask_poll (h)` returns -1 while
> open, then 1 / 0. `ask_text_begin (title, msg, ok, cancel, text)` asks for a line of text
> the same way (for an app that never has the keyboard: the dock); `ask_text_poll (out, cap)`
> returns -1, then 1 (the text in `out`) / 0.

> **The look: the modernised CDE (kapi v64).** Every control, and every window's frame, is
> drawn by code from a few theme colours — no bitmap (docs/gui-redesign/README.md).
> - **The palette** (`wtk/theme.h`): `C_BG` (an app's background: the face), `C_FACE`,
>   `C_FACE_HI`, `C_FACE_DN`, `C_BORDER`, `C_TEXT` (on the face), `C_ACCENT` (focus, selection,
>   checks), `C_DIS`, `C_FIELD` (a text field's, a list's background), `C_FIELD_TEXT`,
>   `C_SEL_TEXT` (on the accent), `C_FRAME_ACTIVE` / `C_FRAME_INACTIVE` (the frames), `C_DOCK`,
>   `C_BUTTON` / `C_BUTTON_TEXT` (a push button's, a drop-down's face: draw buttons with them),
>   `C_MENUBAR`, `WK_OUTLINE`. They are **variables**, read once from `SD:/etc/theme.txt` by
>   `wtk::init ()` (the `Root`'s constructor calls it): use them in drawing code, never copy them
>   into a `static const` or a global initialised at start-up (that runs before the theme is
>   read). The file: `theme` = Peach / Steel / Sage / Brick / Slate (the window in front's frame;
>   `active` = any colour instead), `inactive`, `window` (the content: the face; `face` still
>   read), `button`, `field`, `menubar` (these three follow the window's colour when absent),
>   `accent`, `outline` = none / dark / black, `dock` — the Control Panel's Theme applet writes
>   it. As values: `WkTheme` (`WK_AUTO`: derived from the window's), `wk_theme_defaults`,
>   `wk_theme_parse (text, t)`, `wk_theme_get (t)` (the palette in use), `wk_theme_set (t)` (the
>   palette made from it: a preview), `wk_theme_write (t, out, cap)` (theme.txt's text),
>   `wk_theme_reload ()` (a new theme
>   applied: the dock, the menu bar). Text: `C_TEXT` on the face, `C_FIELD_TEXT` on a field,
>   `C_SEL_TEXT` on the accent, `C_BUTTON_TEXT` on a button, `wk_ink_on (bg)` on any colour.
>   Content keeps its own colours (images, games' boards, a terminal's screen); the chrome
>   around it follows the theme.
> - **The painter** (`wtk/paint.h`, integer only): `wk_tone (c, level)` (a shade: 128 = `c`,
>   255 = white, 0 = black), `wk_mix`, `wk_rbox` (a rounded box with a vertical gradient, its
>   corners anti-aliased: per-radius tables), `wk_rline` (its outline), `wk_framed` (the framed
>   push button: a raised frame, a sunken well, the button in it — the gradients computed at its
>   size), `wk_raised`, `wk_sunken` (a field), `wk_etch_h / _v / _box`, `wk_check_mark`,
>   `wk_radio_mark`, `wk_switch_mark`, `wk_scroll_bar`, `wk_slider_mark`, `wk_progress_bar`,
>   `wk_popup` (a floating panel, its corners keyed), `wk_hilite` / `wk_hilite_ink` (a selected
>   row and its text), `wk_title_strip`, `wk_glyph` (`WKG_CHECK`, arrows, chevrons, close,
>   minimise, maximise, restore, lock, gear, power, reload, home, history — a clock…), `wk_text_c` / `wk_text_l` (style 2 = bold).
>   `tools/tests/desktop_sim/gallery/main.cpp` shows every control in every state.
> - **The frame**: `wk_decorate_window ()` (the `Root` calls it; an app drawing its own window
>   calls it after `kapi_resize_window`) draws the title bar, the borders, the rounded corners
>   (their outside see-through in the chrome's top byte), the title buttons — the window menu,
>   minimise, maximise, close, at the kernel's `KAPI_FRAME_*` places — and the title in bold, in
>   the active and the inactive frames' colours. Close and minimise are the kernel's; the window
>   menu and maximise come to the app as `GUI_EVENT_WINCTL`, handled by the `Root` (the frame's
>   state: `wk_window_state (WK_WIN_MENU | WK_WIN_RESIZABLE | WK_WIN_MAXIMISED)`, kept by the
>   `Root`; an app drawing its own window without a `Root` gets the window-menu button greyed).
>   An app with its own pointer handler answers `GUI_EVENT_WINCTL` itself: `KAPI_FRAME_MENU` →
>   `root.windowMenu ()`, `KAPI_FRAME_MAXIMISE` → `root.maximise (!root.maximised ())` (the
>   terminal does).
> - **Resizable windows**: an app whose layout follows its window's size (anchored widgets,
>   layout containers, or its own `layout ()`) calls **`root.setResizable (true)`**: its maximise
>   button (and a double click on the title) fills the work area — between the menu bar and the
>   dock — and restores it (`Root::maximise`: `kapi_resize_window2`, the canvas re-adopted, the
>   frame redrawn), then **`virtual void onResized ()`**. Otherwise the button is greyed. The
>   window menu (its button, top left): Restore / Maximise, Minimise, Move to *workspace* / On
>   All Workspaces (the names: `SD:/etc/dock.ini`'s `desk =` lines), Close (`Root::windowMenu`).
> - **`PopupMenu (x, y)`** (`wtk/dialog.h`): a pop-up menu — `add (label, id, enabled, hint)`,
>   `separator ()`, `run ()` → the id picked, −1 (a click elsewhere, Esc). A context menu.
> - **See-through windows** (`WIN_FLAG_ALPHA`, borderless): the canvas's top byte is each pixel's
>   transparency (0 opaque .. 255 see-through; a click on a wholly see-through pixel goes below).
>   Clear to `0xFF000000`, then draw with **`wk_paint_alpha (true)`** so the anti-aliased edges
>   over see-through pixels keep their colour (`wk_blend_px` for single pixels); `0xFE000000`
>   (almost clear) still takes the clicks. Examples: `dock`, `menubar`, `agenda`. (Onyx Remote
>   draws them over the other windows with the same transparency: rdpd sends them in 32 bits.)
> - **Present what you draw**: the compositor and the remote desktop (`rdpd`: a window is sent
>   again when its counter changes) see a canvas change at `kapi_present ()` — an app drawing
>   in its own loop presents after drawing, and only when something changed (`eyes`: when a
>   pupil moves).
> - **Workspaces** (kapi v65): a window opens on the current desk; `kapi_desk (set, count)` shows
>   desk `set` and/or sets how many there are (−1 / 0 keep them) → `KAPI_DESK_CUR (r)`,
>   `KAPI_DESK_COUNT (r)`, `KAPI_DESK_GEN (r)` (bumped at every change: poll it to redraw a
>   pager); `kapi_win_desk (id, n)` moves window `id` (0: yours) to desk `n` (−1: all; −2: ask).
>   `kapi_list_windows` and `kapi_raise_app` see the current desk only; `kapi_win_list` sees all,
>   `KAPI_WIN_OFFDESK` and `KAPI_WIN_DESK (state)` in their state. The dock is the pager
>   (`dockconf.h`: the desks' number and names).
> - **Control Panel applets** (`user/applet_proto.h`): any wtk app can be shown **inside** the
>   Control Panel (`apps/control`) instead of in a window of its own. Started with `--applet
>   <surface> <host pid>`, `wtk::Root`'s constructor sees it (**`wk_applet ()`**) and adopts the
>   host's shared surface as its canvas (the pane's size, 700 × 470: lay out for it, or centre
>   on `root.width`); the host copies it into its window at each present and sends the pointer
>   and the keys over the mailboxes. `Root::run ()` does it all; an app with its own loop calls
>   **`wk_pump ()`**, **`wk_present ()`** and **`wk_quit ()`** instead of `pump_events` /
>   `kapi_present` / `should_exit` (they fall back to those alone). `wk_applet_send (AP_THEME)`:
>   the host restarts the applet (a new theme applied). An applet needs no menu (the host has
>   one) and never calls `kapi_create_window`. To list it, add a link file to
>   `SD:/apps/control.app/applets/` (`name`, `icon`, `target`, `text`: the user guide §11) and
>   give its `app.txt` `category = Settings` (the menu bar leaves those out). Examples: `theme`,
>   `dockconf`, `soundconf`, `keyconf`, `config`, `padconf`, `wpaconf`.
> - **Shared settings headers**: `dockconf.h` (the dock's drawers, launchers and workspaces:
>   `SD:/etc/dock.ini`, read / written by the dock and the Panel applet; `DOCK_MSG_RELOAD` to the
>   IPC service `"dock"`), `wallpaper.h` (the wallpaper's modes and their painter:
>   `SD:/etc/wallpaper.ini`, used by `voronoy` and the Theme applet's preview; a pattern:
>   `wp_paint` paints its gradient, `wp_grey_cover` lays the grey picture over the area as
>   "cover" does — box-averaged when it shrinks, bilinear when it grows — and `wp_multiply`
>   multiplies the colours by it; the shipped patterns are made by `tools/gen_wallpapers.py`,
>   `--preview` draws `screenshots/wallpapers.png`), `volume.h` (the master volume,
>   `SD:/etc/sound.ini`).
> - **`Dropdown`**'s list opens **above** its box when it does not fit below and fits better
>   there — every parent clips its children, so a drop-down near a window's bottom (or in a
>   group box) keeps its list visible. Put the controls whose lists must pass a group box's
>   edge in the window (the Theme applet's Desktop box does: `desk_add`).
> - **Text on any background**: `wk_ink_for (bg)` is `C_TEXT` on the window's face (and on what
>   is as light, or as dark), else black or white — the ink `Label`s and `Checkbox`es need on a
>   program's own colour (the BASIC runtime's controls; a `Checkbox` picks it by itself).
> - **Dialogs**: `wk_file_open` / `wk_file_save` / `wk_folder_open` (a double-click on a file
>   picks it and confirms), `wk_color_dialog` (R / G / B sliders, a palette), `wk_messagebox`.
> - **The desktop simulator** (`sh tools/tests/desktop_sim/run.sh`): a wtk app built for the PC
>   against a stand-in kernel (`fakekapi.cpp`: files read from `sdcard/` — what the app saves
>   goes to `/tmp/onyx_sim_writes`, never to the card —, a script of pointer / key / menu events,
>   `dump` writes its window — frame and client — with its transparency), laid over the
>   wallpaper by `compose.py`; `wmtest.cpp` checks the kernel's window manager on the PC.
>   `shots.sh` makes the documentation's screenshots with it (below).

> **Menus.** Put commands in the **system menu bar**, not in button rows. After creating
> the `Root`, build a `wtk::Menu` once and publish it:
>
> ```cpp
> static Menu menu;
> menu.menu ("File");
> menu.item ("Open...", "^O", WK_CTRL ('O'), onOpen);   // label, shortcut text, key, void() callback
> menu.separator ();
> menu.item ("Save",    "^S", WK_CTRL ('S'), onSave);
> menu.publish ();                                       // kapi_set_menu (ABI v39)
> ```
>
> The `menubar` app shows the menus while your window is the active app and sends the
> chosen item back (`GUI_EVENT_MENU`); `wtk::Menu` runs the callback, then invalidates the
> root. Every key goes through `Menu::shortcut` first (so item shortcuts work anywhere in
> the app); **Ctrl-Q** quits. Commands and shortcuts are ignored while a modal dialog is
> open. Avoid `^I` (= Tab), `^H` (= Backspace), `^M` (= Enter) as shortcuts.


Minimal skeleton (window with kernel-managed widgets):

```c
#include "kapi.h"

static void on_ok (unsigned long sender, int ev, long value)
{
    (void) sender; (void) ev; (void) value;
    kapi_widget_set_text (g_label, "cliqué !");
}

static unsigned long g_label;

int main (void)
{
    /* Le canvas est mappé à 12 Go ; fb[y*w + x] = 0x00RRGGBB. */
    unsigned *fb = kapi_create_window (300, 200, "exemple");

    g_label = kapi_add_label  (10, 10, 200, 16, "prêt");
    (void)    kapi_add_button (10, 40, 80, 28, "OK", on_ok);

    kapi_wait_for_exit ();   /* pompe les événements à ~60 fps jusqu'à fermeture */
    return 0;
}
```

Key points:

- **`kapi_create_window(w, h, title)`** returns a pointer to the **canvas** (pixel buffer of
  `0x00RRGGBB`, width `w`). The app draws directly into it (no per-pixel
  call). The variant `kapi_create_window_ex(x, y, w, h, title, flags)` is for explicit
  placement and `WIN_FLAG_BORDERLESS`.
- **Kernel widgets**: `kapi_add_button/label/checkbox/textbox/progress/slider/textarea/`
  `scrollbar_v/scrollbar_h/icon(...)` return an `unsigned long` handle. Manipulate them
  with `kapi_widget_set_text/get_text`, `get_checked`, `get/set_value`, `set_rect`
  (move/hide by setting `w=h=0`), `set_icon`.
- **Event loop**: either `kapi_wait_for_exit()` (blocking, simple), or your
  own loop `while (!kapi_should_exit()) { ...; kapi_pump_events(); kapi_present();
  kapi_msleep(16); }` when you animate the canvas yourself.
- **App-drawn UI**: to draw text over your canvas, use
  `kapi_draw_text(x, y, s, color)` + `kapi_font_width/height()`. Capture the keyboard with
  `kapi_set_key_handler(fn)` (`GUI_EVENT_KEY` events, `KEY_*`/ASCII values) and the
  "outside-widget" clicks with `kapi_set_click_handler(fn)` (`GUI_EVENT_CANVAS_CLICK` /
  `..._MOTION`, coordinates encoded in `value`). Cursor position relative to the
  window: `kapi_cursor_pos(&x, &y)`.
- **Cooperative**: your app **must yield** regularly (`present`/`msleep`/
  `pump_events`/`wait_for_exit`), otherwise it freezes the whole system.

See the demos `demoD.c` (widget gallery), `demoE.c` (textarea + scrollview), and the
apps `tinypad.c`, `paint.c`, `mandelbrot.c` for complete examples.

## 7. Writing a `/bin` tool

A `/bin` tool follows the **same EL1 app model** but reads `stdin`, writes `stdout`, and
exits (no window). It is composable via the terminal's pipes.

```c
#include "kapi.h"
#include "applib.h"     /* ax_puts, ax_putln, ax_strlen, ax_itoa, ... */

int main (void)
{
    char args[128];
    kapi_get_args (args, sizeof args);   /* la ligne d'arguments (chaîne) */

    /* Lire stdin et le réécrire en majuscules, par exemple : */
    char buf[256];
    int n;
    while ((n = kapi_stdin_read (buf, sizeof buf)) > 0)
    {
        for (int i = 0; i < n; i++)
            if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
        kapi_stdout_write (buf, (unsigned) n);
    }
    return 0;   /* code de sortie récupérable via kapi_wait dans l'appelant */
}
```

- **`kapi_get_args(buf, size)`**: the entire argument line as **a single string**
  separated by spaces (there is no `argv` array; parse the first word yourself,
  etc.).
- **`kapi_stdin_read(buf, n)`**: reads the task's stdin (`0` = EOF). **`kapi_stdout_write`**: writes stdout.
- To read a file passed as an argument: `kapi_open/read/close`. To list a
  directory: `kapi_opendir/readdir/closedir`.

The existing tools to study: `ls`, `cat`, `grep`, `wc`, `echo`, `page`, `rm`, `mkdir`,
`touch`, `cp`, `mv`, `ps`, `kill`, `run`, `keyb` (in `user/bin/`).

### `memset` / `memcpy` in freestanding apps

Apps built with `-ffreestanding -nostdlib` (every wtk app and the plain `/bin` tools)
have no libc, yet GCC may still emit calls to `memset`/`memcpy`/`memmove` on its own
(e.g. `char buf[64] = "";`, struct copies). Since ABI v36 the kernel exposes Circle's
implementations in the kapi table; `user/kapi.h` defines them as weak
`kapi_memset`/`kapi_memcpy`/`kapi_memmove`, and `user/Makefile` / `user/bin/Makefile`
alias the C names onto them at link time (`KAPI_ALIASES`:
`-Wl,--defsym,memset=kapi_memset …`). A new freestanding link rule must add
`$(KAPI_ALIASES)`; newlib programs must not (they keep newlib's versions).

## 8. The `applib.h` library

[`user/applib.h`](../user/applib.h) is **header-only** (no libc). It provides:

- **Strings**: `ax_strlen`, `ax_streq`, `ax_strcat(dst, cap, &pos, src)` (concat without
  overflow), `ax_app_path(dst, cap, name, suffix)` (builds `SD:apps/<name><suffix>`),
  `ax_itoa(v, buf)`, `ax_fmt2(d, v)` (2-digit decimal).
- **Console**: `ax_puts(s)`, `ax_putln(s)` (to stdout).
- A minimal **`.ini` reader**: `app_ini_load("config.ini")` (from the app's folder via
  `kapi_app_dir`) or `app_ini_load_path("SD:skins/theme.txt")`; then
  `app_ini_get(section, key, default)` and `app_ini_get_int(...)`. Sections `[xxx]`, lines
  `key=value`, comments `;`/`#`.
- **App-drawn widgets** (not kernel widgets): `ax_dropdown` (drop-down list)
  and `ax_colorpick` (palette-based color picker), with `*_draw(...)` and
  `*_click(...)`. The app draws them in its canvas and routes the clicks via its
  `set_click_handler`. Used by `theme.c` and `mandelbrot.c`.

There is also [`user/httpc.h`](../user/httpc.h) — a header-only **HTTP/1.0 client**
over the v21 TCP socket calls. It does **no allocation** (the caller passes the
response buffer, so all memory stays in the app's address space): `http_get(url, buf,
cap, &resp)`, `http_post(...)`, or the general `http_request(method, url, headers,
body, len, buf, cap, &resp)`. Plain HTTP only (no TLS). Used by `/bin/wget`. This is
the recommended pattern for application-level protocols: build them in a user library
on top of the kernel's transport kapis, rather than adding them to the ABI.

For **REST / web-API** clients there is a reusable C++ class,
[`user/http.hpp`](../user/http.hpp) (`HttpClient`/`HttpResponse`) — a header-only,
freestanding **HTTP/1.1** client that works in any app (integer-only or newlib). It
adds, over `httpc.h`: custom default headers (chainable `.bearer(token)`,
`.accept(type)`, `.header(name,value)`), JSON helpers (`post_json`/`put_json`),
response-header lookup (`r.header("Content-Type", out, cap)`), and **chunked**
transfer decoding. Same no-allocation model (the caller passes `char buf[N]`; the body
points into it, NUL-terminated). Errors are a negative `r.status` (`HttpError`).
Example: `HttpClient api; api.bearer(tok).accept("application/json"); auto r =
api.get(url, buf, sizeof buf); if (r.ok()) …`. See `/bin/httpget` for a working demo.
**HTTPS:** the class has a transport seam (`http://` = plain kapi TCP; `https://` =
TLS). TLS is provided by [`user/tls/onyx_tls.hpp`](../user/tls/onyx_tls.hpp) — **mbedTLS
≥3.6.3** over the kapi sockets, with a **buffered** BIO (Circle's `CSocket::Receive`
discards the remainder of a TCP segment on a short read, so we read whole segments) and
**software-only crypto** (the Pi 4's Cortex-A72 has no ARMv8 crypto extensions).
**Verified end-to-end on real hardware** — `httpsget` downloads real pages. Same model
NetSurf uses (HTTPS from an external stack, libcurl+OpenSSL). To enable it in a **newlib**
app: `#define ONYX_HTTP_TLS` before `#include "http.hpp"` and link the cross-built mbedTLS
libs — `make -C user/tls` then `make -C user/bin MBEDTLS_DIR=../tls/mbedtls` (see
[`user/tls/README.md`](../user/tls/README.md)). The freestanding default (no
`ONYX_HTTP_TLS`) keeps `http://` only and returns `HTTP_ERR_HTTPS` for `https://`. Demo:
`/bin/httpsget`. **Not yet secure:** the RNG is a software PRNG (not the HW RNG, which
stalls on the Pi 4) and certificate verification is OFF — see the TLS README.

For **images** there is a reusable decoder, [`user/img/image.hpp`](../user/img/image.hpp)
(`onyximg::decode(data, len, &w, &h)`) — built on the cross-compiled **zlib + libpng +
libjpeg**. It sniffs the format (PNG signature / JPEG SOI) and decodes a byte buffer into
a freshly `malloc`'d array of `0xAARRGGBB` pixels (8-bit alpha in the top byte, which the
canvas ignores when blitting). Same split as TLS: the libraries are cross-built once
(`make -C user/img`, sources pinned in [`user/img/README.md`](../user/img/README.md) —
zlib 1.3.1, libpng 1.6.44, libjpeg IJG v9f), the Onyx glue is header-only. It is a
**newlib** component (uses `malloc` + the libs), so it is OPT-IN: `make -C user/bin
IMG_DIR=../img` builds the `/bin/imgtest` demo (decodes an embedded PNG and prints its
size). This is the same model NetSurf uses — link libpng/libjpeg/zlib, decode behind one
wrapper. Note: `image.hpp` is for full-colour web images; keep
[`user/bmp.hpp`](../user/bmp.hpp) for the magenta-keyed `0x00RRGGBB` icons loaded from SD.

For a **NetSurf-style framebuffer GUI** there is an Onyx **libnsfb** surface backend,
[`user/nsfb/onyx_surface.c`](../user/nsfb/onyx_surface.c). libnsfb is NetSurf's framebuffer
abstraction (its software plotters draw into a surface buffer); this backend makes an Onyx
window **content canvas** that surface: `initialise` → `kapi_create_window` (the
`0x00RRGGBB` canvas *is* the framebuffer), `update` → `kapi_present`, and `input` bridges
Onyx's callback-driven pointer/key events (`kapi_set_pointer/key_handler` + `pump_events`)
into libnsfb's poll-style event queue. The format is `NSFB_FMT_XRGB8888` — on little-endian
the plotter packs `0x00RRGGBB`, exactly the canvas layout (no R/B swap). The vendored
libnsfb is **unpatched**: the backend registers under the name `"onyx"` (resolved with
`nsfb_type_from_name("onyx")`) via a constructor that Onyx's `crt0` runs from `.init_array`.
Cross-built once (`make -C user/nsfb`, pinned in [`user/nsfb/README.md`](../user/nsfb/README.md)),
then OPT-IN: `make -C user/bin NSFB_DIR=../nsfb` builds the `/bin/nsfbdemo` demo (draws
shapes through libnsfb and tracks the cursor). `libnsfb.a` is linked `--whole-archive` so
the surface's registration constructor is not dropped.

The **NetSurf core library stack** also cross-builds for Onyx — `user/netsurf/` builds
libwapcaplet, libparserutils, libnsutils, libnsgif, libnsbmp, libhubbub (HTML), libcss
(CSS) and libdom (DOM) against newlib (`make -C user/netsurf`, versions pinned in
[`user/netsurf/README.md`](../user/netsurf/README.md)). The whole stack **links clean** —
no undefined symbols — against `crt0libc` + `onyx_syscalls` + newlib. Three Onyx-side fixes
made it self-contained: libparserutils is built `-DWITHOUT_ICONV_FILTER` (use its own
charset codecs; newlib has no `iconv`), everything is built `-fcommon` (the code predates
GCC 10's `-fno-common`), and `pread`/`pwrite` were added to `onyx_syscalls.c`. Code that the
upstream buildsystem normally generates with host tools is reproduced in the Makefile: perl
for the libparserutils charset aliases and libhubbub entities, a host-compiled `gen_parser`
for the libcss property parsers, and a gperf-free element-type table for libhubbub
([`user/netsurf/gen/`](../user/netsurf/gen/)). This is NetSurf brick 7. Brick 8 — an Onyx
**fetch scheme handler** ([`user/netsurf/onyx_fetch.c`](../user/netsurf/onyx_fetch.c)) —
drives the NetSurf fetch API over the Onyx TCP kapis (+ gzip via zlib), the curl-fetcher's
role without libcurl. Brick 9 wires the whole **NetSurf core + its framebuffer frontend** to
the `user/nsfb` `"onyx"` libnsfb surface + the `user/img` decoders: `make -f
user/netsurf/netsurf-app.mk stage` builds `netsurf.elf` (182 TUs + the libs) and installs it
as the `NetSurf` desktop app. **It runs on Onyx** — the window opens and real web pages render
through the full HTML/CSS engine (currently slow; the `onyx_main.c` entry shim passes
`-f onyx` to select the window surface). A console `nstest` (`netsurf-app.mk nstest`) smoke-
tests each library brick. See [`user/netsurf/README.md`](../user/netsurf/README.md).
NetSurf has since been changed a great deal for Onyx — its fonts (FreeType, web fonts,
metric-compatible stand-ins), CSS3 in libcss, flexbox / grid / baseline layout, anti-aliased
CSS3 painting, the native window: [`06-NETSURF-CHANGES.md`](06-NETSURF-CHANGES.md) lists the
changes. A change is checked on the PC first: `sh tools/tests/netsurf/shot.sh <url|file>
<out.png> [WxH]` renders a page with NetSurf built for the PC (the desktop simulator), and
`sh tools/tests/netsurf/chrome.sh <url|file> <out.png> [w] [h]` the same page in Chromium.

And [`user/uikit.h`](../user/uikit.h) — a **retained-mode widget toolkit** drawn
entirely in the app's canvas, driven by the kernel's **pointer stream** (ABI v22:
`set_pointer_handler` → `GUI_EVENT_PTR_MOVE/DOWN/UP/ENTER/LEAVE/WHEEL` with client coords;
`GUI_EVENT_PTR_WHEEL` carries a signed notch delta in the `lValue` wheel field, decoded
with `GUI_PTR_WHEEL` — scrollbars/text areas in both toolkits scroll on it).
Same memory model as the rest: widgets live in a caller-provided **fixed pool**
(`ui_widget pool[N]` in the app's `.bss`, freed automatically on exit — no user
`malloc`, no kernel object behind a widget). `ui_init`, `ui_button`/`ui_label`/
`ui_checkbox`/`ui_textbox`, `ui_on_event` (fed from the app's pointer + key handlers),
`ui_draw`. The **textbox** is a single-line editor: caret, Backspace/Delete, arrows,
Home/End, `Tab` to move focus, an optional password mask (`ui_set_password`), read
with `ui_get_text`. `tinycalc` (buttons) and `wpaconf` (a form of textboxes) use the
toolkit. This is the forward path for widgets: new ones are added here, in userland,
with no kernel/ABI change. The older **kernel-drawn widgets**
(`add_button`…, §earlier) still work and coexist; apps choose one model per window.

### Dynamic memory + C++ apps

Apps have **no `malloc` by default** — they use static buffers + the stack (both in
the app's address space). For dynamic memory, include [`user/umm.h`](../user/umm.h):
a small user allocator (size-class free lists + a `kapi_sbrk` arena). `umm_malloc` /
`umm_free` / `umm_calloc` / `umm_realloc`. The heap lives at `USER_HEAP_BASE` (10 GB);
its pages are owned by the address space, so they are **freed automatically when the
app exits** and show up in the app's page count (`ps` / `memmon`). `kapi_sbrk` is the
underlying primitive (rarely called directly). `/bin/heaptest` exercises it.

**C++ apps** are supported (freestanding subset — no exceptions, no RTTI, no STL):

- Name the source `*.cpp`; the user `Makefile` builds it with `g++`
  (`-fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit`).
- Include [`user/onyxpp.hpp`](../user/onyxpp.hpp): it defines `operator new`/`delete`
  (on `umm`) and the runtime stubs (`__cxa_pure_virtual`, `__dso_handle`,
  `__cxa_atexit`/`atexit` no-ops). Global constructors run via `crt0.S` (the
  `.init_array` walk); static destructors are **not** run (the app exits and its
  address space is reclaimed).
- Classes, inheritance and virtual methods (vtables) work; `new`/`delete` go through
  the user heap. No `std::string`/`std::vector` — write small containers on `umm` as
  needed. See [`user/cppdemo.cpp`](../user/cppdemo.cpp) for a working example.

## 9. Packaging an app: `.app`, icons, `config.ini`

Layout of an application on the card (produced by `make stage`):

```
SD:apps/<nom>.app/
  main           l'ELF de l'app, sans extension (obligatoire)
  icon.bmp       icône 40×40, BMP 24 bpp ; le magenta 0xFF00FF est transparent (optionnel)
  app.txt        métadonnées (display name + catégorie) lues par le launcher (optionnel)
  config.ini     configuration de l'app, lue via app_ini_load() (optionnel)
```

An app may also be written in **BASIC**: `main.bas` (or a compiled `main.bax`) instead of
`main`. The kernel only loads ELFs: **`user/launch.h`** resolves the rest from
**`SD:/etc/runners.ini`** ("extension = program", e.g. `bax = SD:/bin/basic`,
`gb` / `gbc = SD:/apps/gbemu.app/main`, `gba = SD:/apps/gbaemu.app/main`, `nes = SD:/apps/nesemu.app/main`, `sfc` / `smc = SD:/apps/snesemu.app/main`, `wad = SD:/apps/doom.app/main`):
`lx_launch (name, args)` starts an app (its `main`, else the first `main.<ext>` with a
runner), `lx_open (path, args)` a program file (an ELF, or by its runner), both through
`kapi_exec_as` so the process is named after the app. The launchers use it: the menu bar,
`run`, `fileassoc.h` (File Viewer, the dock), the dock, the Game Library. A new format = one line in
`runners.ini`. An app written in BASIC and shipped compiled is listed in `BASIC_APPS` of
`kernel/Makefile`: `make stage` compiles it with `tools/basc` (the host build of the same
compiler) to `apps/<name>.app/main.bax` (Arkanoid). See *Onyx BASIC* below.

The **app name** is the base name of the `.app` folder (without the suffix). That is what
you put in `/etc/autostart` / `/etc/quicklaunch.txt` and what `kapi_list_apps` returns.

**`app.txt`** — friendly metadata for launchers (`key = value`, no section, read with
`app_ini_load_path` / `app_ini_get(0, …)`). Every app under `SD:apps/` ships one:

```ini
name     = Text Editor          ; display name shown under the icon
category = Productivity          ; Productivity, Internet, Graphics, Games, Demos, Settings, Emulators, Shell
stack    = 8M                    ; optional: the app's stack (bytes, K or M), read by the KERNEL
```

**`stack`** is the one key the kernel reads (`AppStackSize`, `kernel/kernel.cpp`, when it
creates the app's task): an app runs on its kernel task's stack, **256 KB** by default; a
bigger one is asked for here — rounded up to 64 KB, at most 64 MB (smaller: ignored). The
stack is kernel memory (it must stay mapped when the address space switches), taken from
the kernel heap and reused after the app ends. NetSurf asks for 8 MB (its JavaScript
engine may use 4).

The menu bar's **Onyx** menu and the dock's drawers group the apps by `category` and show
their `name`. Three categories are **not listed** there: `Shell` (the desktop's own parts:
`menubar`, `dock`, `notifyd`, `agenda`, `lock`, `ask`, `shell`… and the retired `panel`,
`applist`, `shelf`), `Settings` (the Control Panel's applets: reached through it) and
`Emulators` (reached through the Game Library, which starts the right one for a game). A shell
component also creates its window with **`WIN_FLAG_SYSTEM`** (`kapi_create_window_ex` / the
positioned `wtk::Root` constructor), so it is left out of `kapi_list_windows` (the menu bar's
Open Windows, the dock's running dots) and shown on every workspace.

**Icons** — [`tools/gen_assets.py`](../tools/gen_assets.py) procedurally generates the
40×40 BMPs (BGR bottom-up, 4-byte padding) for all the apps (a document for `tinypad`,
a calculator for `tinycalc`, a glider for `life`, etc.) and the "9 squares" glyph of the
*apps* button in the panel. [`tools/preview_icons.py`](../tools/preview_icons.py) produces a
PNG preview montage. Workflow: run `gen_assets.py` (writes the `icon.bmp` files into
`sdcard/apps/<name>.app/`), then `preview_icons.py` to check visually.

**`config.ini`** — example read by `inidemo` / `voronoy` / `panel`:

```ini
; commentaire
greeting = bonjour
[app]
name = Mon App
version = 1
[display]
barwidth = 40
```

### Screenshots and documentation

- **Screenshots (the real apps)**: [`tools/tests/desktop_sim/shots.sh`](../tools/tests/desktop_sim/shots.sh)
  builds each documented app **for the PC** against the desktop simulator's stand-in kernel
  (`fakekapi.cpp`, wtk with its real image codecs and fonts), plays a short script of events
  (clicks, keys, menu commands; a canned shell session for the terminal `SIM_PIPE`, an IRC server
  `SIM_NET`, sample files from `tools/tests/desktop_sim/sd/` `SIM_OVERLAY`), dumps its window —
  the frame wtk drew and the client area — and writes `screenshots/<name>.png` (`shot.py`: the
  window alone, its rounded corners transparent) or lays several over the Voronoi wallpaper
  (`compose.py`: `desktop.png`, `menubar.png`, `volume.png`, `clock.png`, `wifimenu.png`,
  `dock.png`, `agenda.png`). Run `sh tools/tests/desktop_sim/shots.sh` (all) or
  `sh tools/tests/desktop_sim/shots.sh paint dock` (some); ~15 s, needs g++ and Pillow + numpy.
  **To add an app**: add it to `APPS` and a line `sim <app> <name> "<script>" …; png <name>`.
  The script's steps: `wait`, `down / up / move / wheel X Y`, `rdown / rup`, `key CODE`, `mods N`
  (the modifiers held from then on: 1 Ctrl, 2 Shift, 4 Alt — Shift+arrows select...), `menu N`
  (the app's menu item N: its items counted from 0 in the order the app adds them), `winctl N`,
  `dump FILE`, `exit`. Writer's is built with the apps' FreeType (the same sources, for the PC).
  (`nintendoemu.png` and `arkanoid.png` — an emulator, a BASIC program — still come from the
  older, simulated renderer [`tools/screenshot/render.py`](../tools/screenshot/render.py).)
- **Word/PDF exports**: [`docs/build_docs.py`](build_docs.py) converts each `.md` in
  `docs/` into `.docx` (via `pandoc`) then into `.pdf` (via Word/`docx2pdf`), in
  `docs/exports/`. Run `python docs/build_docs.py` after any modification to the `.md` files.
  Prerequisites (once): `pip install python-docx docx2pdf pypandoc_binary`.
- **Reminder of the project rule** (see `CLAUDE.md`): any modification of a `kapi`
  function or of an app **updates the docs in the same go**, regenerates the affected
  screenshot if the visual changes, then regenerates the exports.

### Onyx BASIC (the interpreter)

- **Core** (`user/basic/`): `bas.h` (API: `bas::compile`, `bas::run`, the `bas::Host`
  interface), `bascomp.cpp` (lexer + one-pass compiler to bytecode, with a pre-scan of the
  SUB / FUNCTION / DEF FN headers and of the TYPEs), `basvm.cpp` (the stack VM: tagged values,
  ref-counted strings, arrays and records -- a record shared at a store is copied first, so
  TYPEs have value semantics -- references (by-ref arguments, the address of an element or a
  field: `OP_REFG` / `OP_AADDRG` / `OP_FADDR`, stored through with `OP_STREF`), a frame per
  call), `basnum.cpp` (number formatting / parsing and the math functions, no libc).
  Numeric sub-types are a compile-time matter: values are doubles; a store into an INTEGER /
  LONG gets an `OP_CONV` (round half even, Overflow), a fixed string an `OP_FIXSTR`; a
  DOUBLE is only a printing precision (the compiler tracks `dblSeen` per expression).
  **Errors**: `VM::fail` fails the op; before the next one `trap ()` looks for the resume
  point (setjmp-like): with `ON ERROR GOTO` it unwinds the frames and the value stack and
  jumps to the handler; `RESUME` uses the statement table (`Program::stmts`) to restart or
  skip the statement that failed. **Events** (`ON TIMER` / `ON KEY`) are checked every 32
  ops and after each blocking statement, and fire as a GOSUB (the interrupted expression
  stays on the stack). **CHAIN**: `bas::run` loops, compiling the next program and moving the
  COMMON values and the open files into the new VM. Portable C++ (only `new` / `delete`): built into
  `basic/libbasic.a` with FP/SIMD (`-fno-math-errno`), and on a PC for the tests.
- **Runtime** `/bin/basic` (`basic/runtime.cpp`): the Onyx `bas::Host` — console (stdio)
  or a `wtk::Root` window (a text/graphics framebuffer + wtk controls, pumped by the VM through
  `Host::poll`, `Root::attach ()`). A windowed app (`WINDOW`) asks the platform its colours
  (`ScreenHost::windowColours`: Onyx = the theme's `C_BG` / `C_TEXT`, the PC none) and takes
  them as its background, text and drawing colours unless the program set `COLOR` first. Options `-d <dir>`, `-i` (report errors to the editor:
  mailbox to the IPC service `qbasic`, payload `line\0message\0`).
- **Editor** `apps/qbasic` links `libbasic.a` for the syntax check.
- **Adding a function**: an entry in `BFNS[]` (bascomp.cpp: name, id, result type, argument
  spec `N`/`S`/`?`, `[` = optional from here), a `B_*` id (basint.h), its case in
  `VM::builtin` (basvm.cpp); something the VM cannot do itself goes through a new
  `bas::Host` virtual (default no-op) implemented in runtime.cpp. Statements: `simpleStatement`
  (fixed arguments) or a dedicated `st*` parser, `S_*` id, `VM::statement`.
- **Keys**: the kernel delivers F1-F12 as `KEY_F1` .. `KEY_F12` (0x110..0x11B); the runtime
  turns them into QBasic's `INKEY$` codes (`CHR$(0) + CHR$(59..68)`, 133, 134).
- **Runtime graphics** (`runtime.cpp`): a mode table (size, text cell height, pages,
  colours, display scale), up to 8 page buffers (draw `apage`, show `vpage`), a 256-entry
  palette (the VGA default; `PALETTE` recolours the pixels already drawn), a text-cell
  buffer (`SCREEN ()`), `VIEW PRINT` rows, a clip rectangle (`VIEW`), scanline `PAINT`,
  `readRect` / `writeRect` for `GET` / `PUT`; `FULLSCREEN` uses `kapi_fullscreen_begin` and
  scales the visible page (aspect kept; whole-number zoom when it covers >= 85 %).
  `PLAY "MB"` notes go to a 32-note queue that `bgTick ()` plays from `pump ()`;
  `KEYDOWN` maps its key to a `KEY_*` code for `kapi_key_held`. `PAD` / `STICK` / `STRIG` call
  `Host::padButtons` / `padAxis` (Onyx: `user/gamepad.h`; the PC: winmm `joyGetPosEx`).
- **3D** (`basic/bas3d.h`, `basic/bas3dscene.h`): the VM's `Scene3D` turns the 3D statements
  into vertices + batches in the layout of kapi v53 (`G3Vertex` = `kapi_gpu_vertex3`,
  `G3Batch` = `kapi_gpu_batch`, same flag bits): the vertices stay in model space, each
  batch carries projection × view × model (a new batch when the matrix, the texture or the
  state changes); the shapes are lit on the CPU (the normal turned by the model matrix).
  `RENDER3D` calls `Host::render3d`; `ScreenHost` keeps a copy of each texture (`GRAB3D` →
  `Host::texture3d`) and asks the platform (`gpuTexture` / `gpuRender`: Onyx's runtime =
  `kapi_gpu_texture` / `kapi_gpu_render` into the active page), else draws with `swRender`
  (the same semantics in software: near-plane clipping, perspective-correct, depth, culling,
  blending). Host test: `sh tools/tests/run_basic3d_test.sh` (a window-less `ScreenHost`
  saves each `RENDER3D` as a PPM and checks some pixels).
- **Compiled programs** (`basbax.cpp`): `saveBax ()` writes a `Program` table by table
  (little-endian; header "OBAX", the format and the VM's opcode / builtin / statement counts,
  so a `.bax` from another VM is refused), `loadBax ()` reads it back, `load ()` takes a
  file's bytes (a `.bax`, else source to compile) -- used by `/bin/basic` (`-c` compiles),
  `CHAIN`, the PC `obcore.dll` (`ob_compile`, `ob_run` with a length). `runners.ini` sends
  `.bax` and `.bas` to `/bin/basic` (an app's `main.bax` before its `main.bas`). The tests run every program a second time
  through a `.bax` (`BAX=1`). **Add opcodes / builtins / statements at the end** of their
  enums: the counts in the header change, old `.bax` files are then refused cleanly.
- **Methods**: `SUB Type.Name` (the part before the last dot is a TYPE) is a procedure
  `TYPE.NAME` whose first parameter is `THIS`, the record by reference; `fieldPath ()` turns
  an unknown last part of a record path into `Ref.method`, and the call pushes the record's
  address (`emitAddr`) then the arguments (`callMethod`). `SUB Type.new` is the constructor:
  `DIM v AS Type (args)` and `NEW Type (args)` (`OP_NEWREC`: a fresh record) call it.
  Errors: `trap ()` keeps the SUB frames (each remembers its value-stack depth, `sp0`), so
  `RESUME [NEXT]` continues inside the procedure, as in QBasic.
- **Character set**: BASIC text is code page 437. `user/basic/basfont.h` (generated by
  `tools/gen_basfont.py` from Debian console-setup's VGA fonts) holds the 8 × 8 / 14 / 16
  glyphs that `glyph ()` plots, and the Latin-1 <-> CP437 tables: the lexer translates string
  literals and `DATA`, the host translates keys, and the VM's `cstr ()` / `pushL1 ()`
  translate what goes to or comes from the system (controls, paths, clipboard, dialogs);
  `craw ()` keeps screen text (`DRAWTEXT`) and `ENVIRON` as they are. A PC render of it: `run_games_test.sh BASICRT`
  (programs in `tools/tests/basic/rt/`).
- **The PC tools** (`pc/`, built on Linux by `sh pc/build.sh` into `pc/dist/`, which is
  committed): `obcore.dll` (`pc/obcore/obcore.cpp`, mingw-w64) is the core + `basscreen.h` with a
  Windows `ScreenHost` (SD:/ mapped to a folder, waveOut sound, callbacks for the window, the
  picture, the controls and the dialogs) and a C API (`ob_check`, `ob_words`, `ob_run`,
  `ob_key` / `ob_keyheld` / `ob_mouse` / `ob_event` / `ob_stop`); `OnyxBasic.exe`
  (`pc/OnyxBasic`, .NET Framework 4.8 WinForms, `EnableWindowsTargeting`) is the editor
  (`EditorForm.cs`: modules split / composed like `qbasic`) and the runtime (`Runner.cs`: the
  VM on a worker thread, the page drawn scaled into a bitmap, WinForms controls). Wine + wine-mono
  can run both on Linux for a check.
- **Volume and Wi-Fi from the menu bar** (ABI v60): `user/volume.h` (`volume_save` /
  `volume_restore`: `SD:/etc/sound.ini`) for the menu bar's volume box and `/bin/volume`
  (`kapi_sound_volume (vol, mute)`, −1 keeps). The Wi-Fi menu is its own app,
  `Apps/wifimenu` (a borderless window: it takes the keyboard for the password, unlike the
  TOPMOST menu bar): `kapi_wlan_scan`, the known networks parsed from / written back to
  `SD:/etc/wpa_supplicant.conf` (several `network={}` blocks, `priority`), then
  `kapi_wlan_reconnect`; it closes when another window has the keys (`kapi_win_list`,
  `KAPI_WIN_KEYS`).
- **NintendoEMU** (`pc/NintendoEMU`): `nemucore.dll` (`core/nemucore.cpp`, mingw-w64) builds the
  emulator cores **unchanged** (`user/gb`, `gba`, `nes`, `snes`, `n64`, `gc`) behind a C API —
  `ne_open` (the system from the extension; a GameCube disc read on demand through `discRead`),
  `ne_set_keys` (the Windows key states) + XInput pad 0, mapped per system exactly as the Onyx
  apps do, `ne_run_frame (h, draw)`, `ne_audio`, `ne_video` (the last picture, double-buffered
  under a lock: the game runs on its own thread), `ne_thumb` (the library's picture), `ne_save`
  (`<rom>.sav`, as on Onyx; a GameCube's is its **memory card** in slot A, a 2 MB raw image —
  Dolphin's `.raw` sizes accepted — inserted before the boot and written when `card[0].dirty`),
  waveOut (`ne_audio_*`; the GameCube's sound through `Machine::audioRead`). The N64 `GFrame`s go to
  **OpenGL** (`ne_gl_attach (h, hwnd)` from the game's thread: a WGL context on the picture
  control, GLSL 1.20 — colour = texel × colour + colour2, alpha test, the batch's matrix, blending
  / depth / cull / wrap from the flags, as `kapi_gpu_render`; the machine's dirty textures uploaded;
  a CPU-drawn framebuffer as a textured quad), else `bas::swTriangles` + `g3raster` in bands of 16
  rows on every core (`user/basic/bas3d.h`) at `ne_set_scale` × their size. A GameCube game with
  OpenGL 3.3 gets **`core/gxgl.cpp`** as its `Machine::gpu` (the GX's real pipeline on the GPU):
  the EFB is a framebuffer (RGBA8 + depth 24, `ne_set_scale` × 640 × 528, its rows the EFB's); a
  draw's vertices, the XF memory and the `GxState` go to three ring buffers (persistently mapped,
  their quarters fenced; else unsynchronized maps) and two uniform blocks; the vertex shader
  transforms, lights (the two channels, 8 lights: spot / specular attenuation, diffuse functions)
  and makes the texture coordinates (texgens, dual texture); the fragment shader is the TEV in
  integers as the hardware (the lerp with c + c >> 7, the scale inside it, compare modes, swap
  tables, konst colours, the last stage into PREV, & 255), indirect texturing (matrices, wraps,
  bump alpha), the alpha test, the z texture, the fog (with its range adjustment), RGBA6 / RGB565
  EFB formats, the destination alpha (dual-source blending); GX blending / logic ops / depth /
  culling / scissor are GL state. Each TEV configuration gets a **specialized program** (the
  ubershader's code with the stages unrolled and the state's choices as constants — ~1.6× faster
  on an integrated GPU), compiled by the driver's threads when it has `KHR_parallel_shader_compile`
  (the ubershader meanwhile), else at once (the driver caches them on disk: a pause only the first
  time a configuration is seen). Consecutive draws of one state are one draw call; textures are
  uploaded with their mipmaps, sampled through cached sampler objects. An EFB copy to a texture
  renders the rectangle into a texture of ours converted as the texture decoder would read it
  (intensity Y = 0.257 R + 0.504 G + 0.098 B + 16, RGB565, RGB5A3, the depth's bytes for Z
  formats), box-filtered when halved; a copy to the XFB is the picture, presented into the window
  (`gxgl_present`). `NEMU_GX_UBER=1` forces the ubershader, `NEMU_GX_SYNC=1` compiles at once,
  `NEMU_GX_DUMP=<prefix>` writes the GPU's picture every 250 fields (+ what GL is). Pads: XInput, else
  the first Windows joystick (winmm `joyGetPosEx`) through `ne_pad_map` (PAD_* bit → button / axis
  end / hat direction; `PadDialog.cs`). `NintendoEMU.exe` (.NET
  4.8 WinForms): `MainForm` (the library, `library.txt`, the pictures made by a worker thread),
  `GameForm` (the game thread: paced by the sound queue — ~70 ms — when the game makes sound,
  else by the clock, a picture skipped to catch up; the window draws with `StretchDIBits`).
  View ▸ 3D Renderer ▸ *Use the High-Performance Graphics Card* (`GpuPreference` in
  `GameForm.cs`) writes Windows' own per-application choice (`HKCU\Software\Microsoft\DirectX\
  UserGpuPreferences`, what Settings ▸ Display ▸ Graphics writes) for a laptop with two graphics
  cards: a .NET program cannot export `NvOptimusEnablement`.
  Test: `sh tools/tests/run_nemu_test.sh` (the C API on Linux: each core a few frames, a picture,
  the library pictures; `NEMU_ROMS=<folder>` of ROMs).
- **Tests**: `sh tools/tests/run_basic_test.sh` builds the core with a console host
  (`tools/tests/basic/host_main.cpp`, graphics / controls logged as text) under ASan + UBSan
  and compares `tools/tests/basic/progs/*.bas` with their `.out` (`--update` rewrites them).

## 10. Extending the `kapi` ABI

The ABI is **append-only**. To add a kernel function callable by apps, there are **5
points** to touch (all in the same direction, at the end):

1. **`kernel/include/kern/kapi_abi.h`** — add the function pointer **at the end** of
   `struct TKApiTable` (never in the middle, never reorder), with a version
   comment, and **increment `KAPI_ABI_VERSION`**.

   ```c
   // --- v22 additions ---   (next free version; v21 added the TCP sockets)
   int (*ma_fonction) (int arg);
   ```

2. **`kernel/sys/kapi.cpp`** — implement `extern "C" int kapi_ma_fonction(int arg)`. It
   runs in the app's context (use `CurrentAS()` for the current address space
   if needed).

3. **`kernel/sys/kapitable.cpp`** — declare the `extern "C"` prototype and **assign the
   pointer** in `KApiTableInit()`: `t->ma_fonction = kapi_ma_fonction;`.

4. **`user/kapi.h`** — add the inline wrapper:

   ```c
   static inline int kapi_ma_fonction (int arg) { return KT->ma_fonction (arg); }
   ```

5. Rebuild (`make`). The apps that want the new function use it; the
   old ones keep working (they ignore the new field).

> **Golden rule:** never change the signature or the order of an existing field. If some
> semantics must change, add a **new** entry. An app can query
> `((const struct TKApiTable *)KAPI_TABLE_VA)->version` to find out what is available.

If you add a new **GUI event** or a **window flag**, keep the values
synchronized between `kernel/gui/window.h` and the `#define`s in `user/kapi.h` (commented
"must match").

## 11. Coding conventions

- **Kernel (C++)**: Circle style. `CXxx` classes, `m_Xxx` members, CamelCase methods,
  `boolean`/`TRUE`/`FALSE` and Circle's `u8/u16/u32/u64` types. No exceptions or RTTI.
  `new`/`delete` go through Circle's heap.
- **Userland (C)**: freestanding C. `ax_` prefix for the `applib.h` helpers. Globals
  `g_xxx`. Bounded static buffers (no dynamic allocation on the app side in general).
- **kapi**: `extern "C"` functions named `kapi_xxx` on the kernel side; inline wrappers
  `kapi_xxx` on the app side.
- Respect the **comment density** and the **idiom** of the file you are modifying.
- **Git**: commit into the **Onyx repo** explicitly — the current working directory (cwd)
  drifts; a bare `git` may land in the wrong repo. (Note: `circle/` is not
  committed in this repo.)

## 12. Debugging on hardware

Bring-up is done **directly on the Pi 4** (no QEMU raspi4b). Tools:

- **On-screen exception dump**: an EL1 synchronous fault (or an EL0 fault) paints a
  panic + register dump on the HDMI framebuffer (`PanicToScreen` + Circle's handler).
  Note the `ELR` (faulting PC).
- **`addr2line`**: `aarch64-none-elf-addr2line -e kernel8-rpi4.elf <ELR>` to locate
  the faulting line (keep the unstripped `.elf` next to the `.img`).
- **Remote shell**: `/bin/telnetd` (autostarted, TCP port 23) serves the `cmd` shell over
  the network; from the dev machine, `python tools/onyx-telnet.py <pi-ip>` (or any telnet
  client) — handy to run `kmsg`, `ps`, `kill`, tests, without the Pi's keyboard. Its
  server side uses the ABI v37 `kapi_tcp_listen`/`kapi_tcp_accept`.
- **Remote desktop**: `/bin/vncd` (autostarted, VNC port 5900) — watch and drive the GUI
  from any VNC viewer, no monitor needed. It grabs the screen with `kapi_screen_grab`
  and injects input with `kapi_inject_pointer`/`kapi_inject_key` (ABI v38); it is a
  newlib program linked with the vendored zlib (`ZLIB_PROGS` in `user/bin/Makefile`).
- **Window-level remote desktop**: `/bin/rdpd` (autostarted, port 3390) + the Windows client
  `pc/OnyxRemote` (.NET Framework 4.8, built by `sh pc/build.sh` into `pc/dist/OnyxRemote.exe`):
  one MDI window (+ `TelnetForm`: a telnet console on telnetd, its own window -- IAC dropped,
  `ESC [2J` clears, the line sent whole + CR LF, telnetd echoes it) -- the Onyx menu bar across its top (stretched: `BarView`, its drop-down menus
  in a colour-keyed layer over the children), each Onyx window a child window (native frame or
  the Onyx one), the desktop + bubbles as the MDI area's background -- composited by the PC:
  nothing is composited for it on the Pi. `rdpd` lists the windows (ABI v56
  `kapi_win_list`), reads only those whose `gen` changed (`kapi_win_read`), compares their
  64 × 64 tiles with what the client has and sends the changed runs, LZ4-compressed (its own
  compressor, the standard block format), 32 or 16 bits a pixel; a frame when `chromeGen`
  changes; the desktop (`KAPI_WIN_DESKTOP`, the wallpaper + the backmost windows) only when
  the client asks for it (message 7). The hello carries the kernel's kapi version (the
  client warns below 56). The client sends the pointer in window coordinates (rdpd adds the window's place,
  raises it when clicked), keysyms (`user/bin/remotekeys.h`, shared with vncd: specials,
  modifiers, letters / digits as held keys only) and the characters typed (the PC's layout).
  The protocol is described at the top of `user/bin/rdpd.c`. Host test (mock kapi, a Python
  client): `sh tools/tests/run_rdpd_test.sh`.
- **Serial console**: `config.txt` must have `enable_uart=1` (PL011 clock). The boot
  log goes **also** to the HDMI screen (`CScreenDevice`) so it is readable without a serial
  cable.
- **Post-mortem console**: on an app's exit, the compositor clears and the logger
  is shown on the framebuffer (see [Kernel internals §11](02-KERNEL-INTERNALS.md#11-post-mortem-debug-console)).

## 13. Known pitfalls

- **`kapi_resize_window` keeps the buffer's size and row pitch.** The window buffer is made
  once, at the size given to `kapi_create_window`; resizing changes the size shown (clamped
  to that buffer), not the buffer, and the compositor reads its rows with the **creation
  width** as pitch. Draw with that pitch: `canvas.adopt (kapi_resize_window (w, h), w, h,
  creationWidth)`. To offer several sizes, create the window at the largest one (the
  emulators: their 4x zoom). Adopting with pitch = the new width gave a doubled, interlaced
  picture in `gbemu` at zoom 2x. The window's **frame follows the new size**: call
  `wtk::wk_decorate_window ()` after the resize so the title bar and borders are redrawn
  at it (else the frame keeps the old drawing).
- **`kapi_mkdir` / `kapi_remove` / `kapi_rename` return 0 on success** (-1 on failure),
  like POSIX — not a boolean. Test `== 0` for success (a `!kapi_rename (…)` "failure"
  check silently treated every successful move as failed; fixed in `trash.h`, `fsutil.h`,
  the File Viewer and `ftpd`).
- **Host tests** (`tools/tests/`): `run_fs_test.sh` (the Circle fork's FatFs + `diskio.cpp`
  sector cache on a RAM disk: 4000 random file operations checked against a model, cache on
  and off, same disk image — run it after touching `circle/addon/fatfs`), `run_trash_test.sh` (trash.h + fsutil.h against a mock
  kapi with the kernel's return conventions) and `run_ftpd_test.sh` (ftpd over real
  sockets, driven by Python's `ftplib`: login, LIST/NLST, RETR/STOR round trip, MKD/RMD,
  RNFR/RNTO, DELE, root jail, PORT, two concurrent sessions) and `run_ftpfs_test.sh`
  (the ftpfs FTP client against pyftpdlib — `pip install pyftpdlib` — and against ftpd:
  LIST/MLSD, RETR, a 300 KB STOR round trip, MKD, RNFR/RNTO, DELE/RMD, a missing file,
  reconnecting after a dropped control link). The socket mock reproduces Circle's
  "one segment per receive, the rest is dropped". Run them after touching those files.

- **Hardware float is opt-in.** Apps are integer-only by default
  (`-mgeneral-regs-only`); the kernel now saves the full FP/SIMD state on every trap,
  so an app may opt into `float`/`double` by building without `-mgeneral-regs-only`
  and with `-mcpu=cortex-a72` (see §5). Caveat: if **preemptive** scheduling is ever
  enabled (today the scheduler is purely cooperative), it **must** switch via the
  full trap frame — the FP save lives there, not in the cooperative `TaskSwitch`.
- **L3 tables shared with the kernel.** On the kernel side, never free an L3 table from the
  user area without checking that it is not shared with the kernel's L2 (cf.
  [Kernel internals §4](02-KERNEL-INTERNALS.md#4-memory-management-caddressspace)). Otherwise: global corruption.
- **Do not free the ABI table page.** It is global to the kernel; the destruction
  of an address space already skips it.
- **`DEPTH=32` for Circle.** `GImage` renders 32-bit; forgetting `-d DEPTH=32` (or changing
  `DEPTH` without `make clean` in `circle/lib`) gives wrong colors/breakage.
- **Cooperative.** Any app loop without `present`/`msleep`/`yield` freezes the system (no
  preemption).
- **Circle LF renormalization.** On Windows, Circle is checked out in CRLF; renormalize
  once (cf. §2) otherwise the build breaks.
- **The right Circle.** Patch `Zircon/circle`, not another clone.
