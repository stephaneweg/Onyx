# Handoff — where the work stands (for the next session)

Written at the end of a long cloud session so that a new session (e.g. a local one on the
user's Windows PC) can continue. Read `CLAUDE.md` first, then this. The user writes in French;
answer in French. The docs stay in English.

## Working conventions (keep them)

- Repo `stephaneweg/Onyx`; the cloud session worked on branch `claude/kind-rubin-vddz2w` and
  always pushed it to `main` as well. Circle is a submodule (`circle/`, fork
  `stephaneweg/circle`, branch `onyx`): commit there with
  `git -C circle -c user.name="stephaneweg" -c user.email="steph.wegener@gmail.com" commit ...`,
  push `git -C circle push origin HEAD:onyx`, then commit the submodule pointer in Onyx. Never
  modify the nested `circle/addon/wlan/hostap` (upstream, not pushable).
- The user never compiles for the Pi: they test the staged `sdcard/` on the Pi and `pc/dist` on
  Windows. `pc/dist` and `sdcard/` binaries are committed.
- Never commit: ROMs / ISOs / saves (`.z64 .n64 .v64 .sfc .smc .nes .gb* .sav .iso .gcm .wav`),
  `sdcard/etc/wpa_supplicant.conf` (psk REDACTED, pre-commit hook + skip-worktree — never defeat
  it), `sdcard/etc/ftpfs.ini`, `shelf.ini`, `SD:/apps/lisa.app/config.ini` (Groq key),
  `sdcard/etc/clock`. Check before each commit:
  `git diff --cached --name-only | grep -i -E "\.sfc$|\.smc$|\.nes$|\.gb|\.sav$|\.z64$|\.n64$|\.v64$|\.wav$|\.iso$|\.gcm$|wpa_supplicant|ftpfs.ini|shelf.ini|lisa.app/config"`
  must print nothing. Do not download commercial ROMs; the user's own ISO/ROMs stay local.
- Do not modify NetSurf. No model identifiers in code or commits. Commits end with a
  `Co-Authored-By:` line.
- Docs rule (CLAUDE.md): kapi / app changes → docs 02 / 03 / 04 (+ 05 for Circle patches),
  screenshots via `tools/screenshot/render.py`; the user runs `python docs/build_docs.py`.
- A rebuild of the Pi apps changes every `sdcard/apps/*/main` a little (build noise): commit only
  the apps that really changed (`git checkout -- sdcard/...` the others).

## Builds

- **Pi (Linux + AArch64 toolchain):** `export PATH=<arm-gnu-toolchain aarch64-none-elf>/bin:$PATH`;
  `cd kernel && (make -j8 || make -j1) && make stage` (the -j8 link race on crt0libc.o is known,
  the -j1 retry works). Circle libs: `make -C circle/lib`, `-C circle/lib/net`,
  `-C circle/addon/fatfs`, `-C circle/addon/wlan`, `(cd circle/addon/wlan/hostap/wpa_supplicant &&
  make -f Makefile.circle)`. After any `ffconf.h` change: rebuild libfatfs, libwlan,
  libwpa_supplicant and the kernel clean (the `FIL` layout).
- **Windows tools:** `sh pc/build.sh` → `pc/dist` (nemucore.dll with MinGW-w64 `g++`,
  `-lwinmm -lopengl32 -lgdi32`; the .NET Framework 4.8 WinForms exes with the .NET SDK). On
  Windows: MSYS2 (or WSL) with MinGW-w64 + the .NET SDK. The script rebuilds every exe; restore
  the unchanged ones (`git checkout pc/dist/<file>`) before committing.
- **Host tests:** `tools/tests/run_*.sh` (fs, v3d clip, gamepad, gc, nemu...). N64 headless:
  `g++ -std=c++17 -O2 [-DN64_TRACE] -I user -I user/basic tools/tests/n64/n64test.cpp user/n64/*.cpp`
  then `n64test <rom> <frames>` with `N64_SAV`, `N64_INPUT="f0-f1:hex;..."`, `N64_SNAP=1`,
  `N64_GFX=prefix N64_GFXEVERY=n`, `N64_FRAMELOG=f`, `N64_CIMG=f` (see the file's header). With
  the user's OoT ROM, the pause menu is reached with the input script: Start at 1000, A at 1200,
  1450, 1550, 1650, then A every 80 frames from 1800 to 16000, Start at 16500.

## Done recently (all pushed)

- **The random N64 freeze on the Pi — fixed.** Root cause (found with the crash record):
  `CScheduler::WakeTasks` asserted on a task whose `WaitWithTimeout` had just expired (Ready but
  still on the wait list) when the V3D frame-done interrupt set the event → Circle halted all
  cores. `kernel/sched/scheduler.cpp` now only unlinks such a task. The user played 15+ minutes
  without a freeze afterwards. The same bug is in upstream Circle: see below.
- **Crash record** (`kernel/sys/crashlog.cpp`, docs/02 §13): log tail, core-0 IRQ samples,
  breadcrumbs (GPU, display), power/temperature, panic line, in 64 KB of RAM kept out of the heap
  (top of the RAM above 4 GB on the user's 8 GB Pi — it survives the watchdog reset); the hardware
  watchdog (`hangreboot=`, 15 s); core 1 writes a report into `SD:/etc/crashdump.txt` sectors when
  core 0 stops (LED signs); next boot → `SD:/etc/lastcrash.txt`. `hangtest` freezes core 0 on
  purpose. `SD:/etc/clock` keeps the time across boots (files written before NTP get a date).
- **N64 (task done):** OoT pause background (the copy into the z-buffer drawn by the CPU into
  RDRAM, the host's frame written back as the framebuffer: `Machine::fbSnapshot`), the 8-bit
  coverage copy written as full (menu opens in ~1 s instead of ~4), decal z bias (z-fighting),
  V3D CPU clipping, polled display DMA (`dispdma`).
- **Onyx Remote:** telnet **Console** window; window fixed to the Pi's screen size when
  connected; the desktop no longer tiles.
- **Circle upstream contributions:** 5 clean branches on the fork (`pr/scheduler-waketasks-timeout`,
  `pr/emmc-high-speed-fixes`, `pr/heap-large-block-reuse`, `pr/2d-partial-update`,
  `pr/dhcp-restart`), based on upstream `develop`; texts, patches and an issue draft (RAM above
  3 GB on the Pi 4) in `docs/circle-upstream/`. The user opens the pull requests.

## Next: the GameCube on the Pi -- the TEV renderer shows a black screen (a local session, with the ISO)

- **Done (cloud session), all pushed:** option A of `docs/GC-WINDOWS-REPORT.md` §5 -- the GX on the
  V3D with generated QPU shaders:
  - **Kernel ABI v61** `gpu_program` / `gpu_render2` (`kernel/sys/v3d.cpp`): the app's own
    vertex / coordinate / fragment shaders, batches with uniform ranges, up to 8 textures, blend
    factors, write mask, scissor; generic CPU clipping (`V3DClipTriangleN`).
  - **QPU toolchain**: `user/v3d/qpu.h` (C++ instruction builder over Mesa's packer),
    `user/v3d/shaders.h` (pass-through VS / CS, simple FS), `tools/qpu/qpulib` (instruction
    restrictions checker), `tools/qpu/qpusim` (fragment-shader simulator).
  - **TEV generator** `user/v3d/gxtev.{h,cpp}`: a TEV configuration -> fragment shader in
    integers as the hardware; `user/v3d/gxtev_ref.h` = gxgl.cpp's GLSL TEV in C++. Checked:
    `tools/tests/run_qpu_test.sh` (thousands of random configs in the simulator, exact) and on
    the Pi: `/bin/v3dprog` -> **ALL PASS 22/22** (incl. 9 TEV configs, up to 16 stages / 8 lookups).
  - **gcemu backend** `user/Apps/gcemu/gxv3d.h` (`Rec`: gc::GxGpu on the app core -- vertices
    through gxgl.cpp's VS ported to C++, TEV program cached by key, uniforms, state; `Out`: main
    thread -> gpu_program / gpu_texture / gpu_render2). View > TEV Shaders On / Off.
  - **PC harness** `tools/tests/gc/gcv3d.cpp`: runs a `.dol` / `.iso` with the same `Rec` and
    draws the last frame with a software V3D (the kernel's clipping, perspective varyings, depth,
    cull, scissor, blending; the generated shaders in qpusim) -> `.ppm`. Build line in
    `tools/tests/run_gc_test.sh`; `gcv3d <iso> <fields> out.ppm`, `GCV3D_EVERY=n` for more
    frames, `GCV3D_DUMP=1` prints the batches. `gxtest.dol` renders right through it.
- **The problem:** on the Pi, The Wind Waker with the TEV renderer shows **only a black screen**
  (the old per-vertex path, View > TEV Shaders Off, still works). Not yet known whether
  `gpu_render2` fails (then `draw_into` falls back to the XFB in MEM1, black with a GPU renderer),
  whether every batch is dropped (`Rec::skipped`: a texture from an EFB copy, a program refused),
  or whether the geometry is off (culling / viewport / depth mapping in `Rec::draw`, only checked
  with gxtest's orthographic 2D draw).
- **Next step for the local session:** run the game on the PC through `gcv3d` with the ISO:
  if its frames are black too, the bug is in `gxv3d.h` (debug there: `GCV3D_DUMP=1`, compare
  with NintendoEMU's gxgl.cpp, which renders the game right); if they are right, the difference
  is the Pi (kernel `gpu_render2` limits / return code, `Out::render`, the real GPU) -- then add a
  diagnostic to gcemu (F12 counters: render2's result, batches drawn / recorded, skipped; a key
  dumping the frame to a file) for the user to report. Then stage 4: EFB copies to textures
  (render to texture in the kernel), fog, indirect textures; stage 5: performance (dual-issue
  scheduling of the generated code, the CPU vertex stage).
- **Testing on the Pi yourself (if you want, the Pi being on the local network: ask the user
  its IP):**
  - a console: `telnet <pi-ip>` (telnetd, port 23; or OnyxRemote's Console button);
  - deploy: in that console `ftpd SD:/` starts the FTP server (port 21) on the card's root; copy
    the rebuilt files there with any FTP client (`kernel8-rpi4.img` needs a reboot: `reboot`;
    `apps/gcemu.app/main`, `bin/*` do not);
  - run the game: `run gcemu SD1:/roms/ZeldaWIndWaker/ZeldaWindWaker.iso` (the second partition;
    quote the path if needed); `kmsg` shows the kernel's log (a GPU time-out is reported there);
  - see the picture: `OnyxRemote.exe` (`pc/dist/`) connects to rdpd (port 3390, started at boot)
    and shows the Onyx windows, gcemu's included (F12 in it: the speed line).

## Other open items

- VNC (`vncd`): the image froze while the sound went on, OnyxRemote (rdpd) kept working —
  not investigated yet.
- Ideas (IDEAS.md): an ISO9660 driver + `mount` of ISO / disk / partition images as volumes
  `VD0:`, `VD1:`…; an mstsc-compatible RDP server (~3000–4500 lines, TLS without NLA); the AX
  microcode's audio for the GameCube (Dolphin's `AXUCode`); NintendoEMU keyboard remapping.
