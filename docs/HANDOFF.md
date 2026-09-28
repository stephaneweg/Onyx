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

## The GameCube on the Pi -- the TEV renderer (the black screen: fixed; next: the speed)

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
    the Pi: `/bin/v3dprog` -> **ALL PASS 24/24** (incl. 11 TEV configs, Wind Waker's first two).
  - **gcemu backend** `user/Apps/gcemu/gxv3d.h` (`Rec`: gc::GxGpu on the app core -- vertices
    through gxgl.cpp's VS ported to C++, TEV program cached by key, uniforms, state; `Out`: main
    thread -> gpu_program / gpu_texture / gpu_render2). View > TEV Shaders On / Off.
  - **PC harness** `tools/tests/gc/gcv3d.cpp`: runs a `.dol` / `.iso` with the same `Rec` and
    draws the last frame with a software V3D (the shaders in qpusim) -> `.ppm`. Build line in
    `tools/tests/run_gc_test.sh` (on Windows: WinLibs MinGW `g++`, see its line); options in its
    header (`GCV3D_EVERY`, `GCV3D_DUMP=1/2`, `GCV3D_SAVE`, `--replay`).
- **The black screen -- fixed (local session, tested on the user's Pi with the ISO).** Three bugs,
  one behind the other:
  1. **The GPU hung** on the first TEV frame (`gpu_render2` -3, kmsg "rendering timed out ... GPU
     left off", then -1 until a reboot). The TEV shaders without texture lookups were flagged
     `KAPI_GPU_P_FS_FINAL` (start in the final thread section). `v3dprog ww` (a w x h target, one
     quad with WW's first TEV program) showed it: hangs now and then on targets >= 512 pixels wide
     (5 runs of 7, mostly the first frame after boot), never at 64 x 64 (the v3dprog tests). Now
     `gxtev` always ends with the last-segment pair (as Mesa): no hang since, in hours of frames.
     docs/02 §15.
  2. **The window was never redrawn** once the game ran slower than real time: gcemu's loop asked
     for the next field before looking for the new picture (`ec_pending` then never 0 there, the
     F12 line showed `draw 0.0 ms`). The picture is now taken first. **n64emu has the same loop**
     (a separate task was suggested to the user: same fix).
  3. **No texture reached the GPU**: `Machine::gxTexture` allocated the texture pixels with `new`
     on the app core -- `umm` is not safe across cores and `kapi_sbrk` from core 2 grows the heap of
     the task core 0 happens to run (seldom gcemu): `px` null (and the decoding wrote through it).
     Now a pool of `TEX_POOL` texels (64 MB) made with the machine; full, every texture is
     forgotten (`texFlush`, the draws re-resolve). docs/03 (the app-core rules).
  And `Out::render` kept only half of `KAPI_GPU_MAX_VERTS`: the island flyover (147 K vertices)
  lost its last 250 batches (the logo, the sea); now 7/8 of it (the rest: the clipping's room).
- **State:** The Wind Waker runs with the TEV on the Pi -- the title (island flyover, logo, "Press
  Start"), the intro story; frames dumped on the Pi and replayed on the PC give the same picture
  (<= 2 % of the pixels differ, on edges). Diagnostics for the next bugs: F12's third line, F9 /
  `--diag[=FTP:<pc>:<port>]` dumps, `gcv3d --replay` (docs/03).
- **The speed (local session, first pass).** F12's fourth line / `--diag` measure it (docs/03).
  The island flyover (~370 batches, 147 K vertices a frame, ~4800 draws of ~5 vertices a field)
  went from ~7 to ~12 fields/s, lighter scenes 20-33 (60 is real time):
  - the drawing overlaps the next field (`Out::prepare` while the machine waits, `Out::submit`
    after the request), and a frame already in the window is not drawn again (30 fps: half);
  - the recorder: only the colour channels the program reads, the alpha from the colour's
    lighting when the controls match, the lights read once a draw, the last TEV configuration
    matched without its hash -- 366 -> 271 cycles a vertex on the PC, exact (the pictures
    identical, `gcv3d`);
  - `gpuTexture` remembers each map's answer within a field (`texMemo`, `texEpoch`: a field, an
    EFB copy, a TLUT load, a DMA, the pool emptied): 7.7 -> 0.4 ms a field.
  Where a heavy field's ~75 ms go now (app core): the recorder ~29, the vertices decoded + the GX
  state ~7.5, the FIFO ~2.5, the textures 0.4, **the CPU (JIT) and the rest ~36**; the main thread
  (~9 ms copy + ~43 ms `gpu_render2` a frame, the kernel's CPU clipping mostly) now runs beside.
  Next, by gain / effort:
  1. **The CPU**: profile the JIT on the Pi (`jitProfile` / `GC_PROFILE` in gctest: the hot
     blocks; the instructions still interpreted, the FPR loads / stores per instruction, the
     dispatch) -- 36 ms a field alone is twice real time.
  2. **The vertex stage on the GPU**: a generated vertex shader (the XF transform, the lighting,
     the texgens: `Rec::record`'s work) fed with the decoded vertices and the matrices / lights as
     uniforms -- removes most of the recorder's ~29 ms; the per-draw setup (~22 % of it) could
     meanwhile be cut by reusing the last draw's program / uniforms when `GxState::serial` has not
     changed.
  3. A fast path in the kernel's `V3DClipTriangleN` for the triangles fully inside (most): the
     main thread's ~43 ms (not on the critical path any more, but it is the display's latency).
  4. Dual-issue scheduling of the generated TEV code.
  Then stage 4: EFB copies to textures (`rec skipped` counts the draws reading one: the heat haze,
  the bloom...), the fog, the indirect textures.
- **The speed, second pass (local session, 2026-09-28).** `netcore=0` on the user's Pi (both app
  cores free): the GX runs on core 3 while the machine runs on core 2 (docs/03, "The GX on its
  own core"). A command-by-command copy of the FIFO near its end made that slower at first (4 fps):
  the commands are now read in place. The Wind Waker (PAL, 50 fields/s) on the Pi: the title's
  heavy flyover ~20 fields/s (the GX core ~90 % busy, ~45 ms a field: the recorder ~32), the
  lighter scenes 35-50, the user's first steps on Outset ~15 fps (60-65 %). JIT (docs/03): the
  memory fast path (one EOR check, loads into the destination), compare + branch fused on the
  host's flags (`crDead`); `gctest fuzz` passes (GC_FUZZC too). The JIT profile (--jitprof,
  200 s): 7.1 host instructions a guest one; lwz / stw 8.5, bc 7.7, b 9.2, lfs 12.3, cmpli 9.4
  before those two changes; a block run ~10 guest instructions; the CPU part is ~90 % JIT code,
  IPC ~0.85 (L2 refills ~4 a thousand instructions).
  **Fixed: the camera "in Link's head"** (the user's report; on the PC it was right): not the
  camera -- the recorder framed the vertices with the copy registers of the draw's time, and a
  half-size EFB copy to a texture in the middle of the frame made the rest a 2x zoom of the top
  left quarter (the title screen too). Found by comparing `gcrun GC_GL=1` and `gcv3d` on the same
  scripted game (`GC_PAD`, `GC_CARD`, `GC_HASH`: the same MEM1, so the drawing). Now the EFB's
  space + `Frame::rect` (docs/03).
  Then (same day): the JIT's base pointers (a D-form access through a register found in MEM1:
  one load / store; little measured gain -- the CPU part is memory-bound), the code buffer's
  hot / cold chunks (L1I refills 20 -> 17.5 a thousand instructions, -7 % cycles), the
  recorder's and the vertex decoder's per-draw plans (the recordings bit for bit the same:
  `GCV3D_VHASH`). The CPU-bound scene of the intro: 20.4 -> 17.0 ms a field (50 fields/s, full
  speed); the user saw 15-20 fps in the game before the GX plans.
  Next, by gain: the JIT's hot code smaller still (the not-taken paths of the branches the
  compiler hints as unlikely out of line; the FP compares fused), a second, optimising tier for
  the hot code (whole functions, the registers kept across blocks, the hot code packed), then the
  display's `gpu_render2` (~20-45 ms a frame on core 0: the kernel's clipping and copies).
- **The speed, third pass (local session, 2026-09-28 evening).** Measured in the game (Outset's
  bridge, ~96k vertices a frame) with `--statlog=FTP:<pc>:2121` (F12's lines whole, every
  second; docs/03): 30.6 -> **~38 fields/s** (~19 fps of 25; F10 said 14-15 fps, 60 %, before).
  - LR, CR, r3, r0 kept in host registers across the blocks (the JIT's SRA), a call counting as
    setting CR0 (`crDead`), `gctest jitsize` (6.56 -> 5.85 host instructions a guest one on the
    Pi profile's hot blocks); `gctest fuzz` (GC_FUZZC, GC_FUZZ2 too) passes.
  - gcemu's loop: the TEV frame prepared and drawn after the next field is asked for, two fields
    asked for when a frame is drawn and the game is behind (the machine, its field done, idled
    ~13 ms each frame while `gpu_render2` ran), the main thread naps while the field's end is
    far (a yield in a loop: core 0 busy, heat).
  - The kernel's `ClipFrame2`: the triangles inside copied once, straight into the GPU's buffer
    (were four copies): `gpu_render2` 34 -> 25 ms a frame; its split in kmsg (docs/02).
  - Where it stands: the machine (core 2) ~26 ms a field, busy ~99 % — the bottleneck; the GX
    (core 3) ~22-24 ms, ~87 %; the main thread ~32 ms a frame (prep 6.8, clip 9.8, GPU 14).
  - **The Pi was throttling**: kmsg `power: SoC 81-84 C ... soft temp limit NOW`, the cores
    ~1.3 GHz (the PMU's cycles against the time) instead of 1.5: a fan / heatsink is ~+15 %.
  - Measures (`--pmu=08,01,52,53`, docs/03): the machine's IPC 0.84, L1I refills 11.4 and L2
    read refills 3.2 a thousand instructions — its data from the RAM. `--nodraw` (the frames
    recorded, not drawn): the machine 37 -> 30 M cycles a field (IPC 1.02), ~45 fields/s, the GX
    core then 98 % busy: **the display's copies (core 0) cost the machine ~18 %** through the
    shared L2 / RAM, and the GX is right behind.
  - The JIT profile (`--jitprof`, 180 s, the game): 5.36 host instructions a guest one on the
    main paths; the branches ~33 % of them (bc 12.7, bclr 8.8 -- plus the dispatcher's --, b / bl
    7.7, bcctr 3.4), the loads / stores ~42 % (lwz 13.4 at 5.7 each, lfs 9.6 at 10.1), fcmpo 4.5 %
    at 14.8 each. The hottest loop (~17 %): a linked list searched through two function pointers
    a node (`8024A118..8024A150`: bctrl -> `8024A7E0` -> bctrl -> `80043E1C` -> blr -> blr), 22k
    nodes a field: four trips through the dispatcher a node.
  Next, by gain: (1) the display's memory traffic -- the recorder rejecting the triangles behind
  the eye (~17 % of them: the kernel drops them anyway), then `Out::prepare`'s copy and the
  kernel's (a GPU-visible buffer the app writes, clipped in place; per-batch strides and the XFB
  rectangle's transform in the shaders); (2) the JIT's calls / returns on the host's own `bl` /
  `ret` with a stack of return addresses (Dolphin's "BLR optimisation": no dispatcher trip, the
  return predicted), the indirect calls inline-cached; (3) the recorder on NEON (~470 ns a
  vertex, the GX's ~15 of its ~22 ms) before the GX caps the speed at ~45 fields/s.
- **Testing on the Pi yourself** (on the user's network; ask its IP -- it was 192.168.0.7):
  - a console: `telnet <pi-ip>` (telnetd, port 23; or OnyxRemote's Console button). If telnetd
    stops answering (a process spinning, see below): in the Pi's Terminal `ps`, then
    `kill <pid> --force`; or `reboot`.
  - files: an anonymous FTP server on the PC (e.g. Python's `pyftpdlib`, port 2121) and on the Pi
    `cat FTP:<pc-ip>:2121/<file> > SD:/<path>` (ftpfs; `cp` does not go through it); gcemu's
    `--diag=FTP:<pc-ip>:2121` writes its log and dumps straight there. Or `ftpd SD:/` on the Pi
    (port 21). `apps/gcemu.app/main` and `bin/*` need no reboot, `kernel8-rpi4.img` does.
  - run: `run gcemu SD1:/roms/ZeldaWIndWaker/ZeldaWIndWaker.iso --diag=...` (the second
    partition); `v3dprog` must pass after any GPU change; a GPU hang leaves the GPU off until a
    reboot (`v3dprog` then says `no GPU: V3D: stopped`).
  - pitfalls: `grep` / `wc` read stdin only (`wc < file`; with a file argument they wait, and
    once the telnet session is gone they spin on stdin's end -- an open bug, cmd too sometimes);
    `kmsg` streams until Ctrl+C and consumes the log (each line is read once).
  - the picture: `OnyxRemote.exe` (`pc/dist/`, rdpd port 3390); or VNC (vncd, no password):
    `python -m vncdotool.command -s <pi-ip> capture x.png`, `... key p` (a key).

## Other open items

- VNC (`vncd`): the image froze while the sound went on, OnyxRemote (rdpd) kept working —
  not investigated yet.
- Ideas (IDEAS.md): an ISO9660 driver + `mount` of ISO / disk / partition images as volumes
  `VD0:`, `VD1:`…; an mstsc-compatible RDP server (~3000–4500 lines, TLS without NLA); the AX
  microcode's audio for the GameCube (Dolphin's `AXUCode`); NintendoEMU keyboard remapping.
