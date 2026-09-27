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

## Next: the GameCube emulator (start with the Windows version)

- **Symptom:** The Legend of Zelda: The Wind Waker (the user's ISO, 1.2 GB, local only) shows a
  black screen, on the Pi (`gcemu`) and on Windows (NintendoEMU). The last status seen (window
  title on Windows, F12 line on the Pi): `pc 80308104, DVD 0 reads (at 00000000), picture: not
  set up yet, 3D frames 0, DSP step 1` — the game never reads the disc after the boot. The user
  says it is still "0 DVD reads" after the DSP boot-mail fix below.
- **Already done:** VI output black until the XFB address is set (was green noise);
  the DSP ROM boot mails decoded by key like Dolphin (A001 address, A002 length, C002, B002,
  D001 start → DSP_INIT) in `user/gc/gc_dsp.cpp`; `Machine::status()` (`user/gc/gc_hw.cpp`) now
  also shows **what the game polls**: `| polls <address> x<reads in a row>, EE, PI intsr/intmr,
  DSP csr, DI sr + cr, EXI0 cr + ...` — build NintendoEMU (`sh pc/build.sh`), run the ISO, read the
  title bar: the polled register says what it waits for (DSP mailbox / DSP csr → the DSP HLE or
  the AX / "Zelda" ucode; DI → the disc interface; EXI → the memory card / RTC / SRAM; nothing
  polled with EE 0/1 → an interrupt that never comes).
- **Likely next steps:** find what 80308104 waits on; Wind Waker uses the "Zelda" DSP ucode (not
  AX): it may need an HLE of it (Dolphin's `ZeldaUCode` is the reference); check DI command
  handling / DI interrupt, EXI transfers completing (TCINT), the PI interrupt mask.
- **Known limits:** the interpreter on x86 runs ~75 MIPS (~15 % speed); the JIT is AArch64-only
  (Pi). NintendoEMU plays GC games only slowly on Windows for now.

## Other open items

- VNC (`vncd`): the image froze while the sound went on, OnyxRemote (rdpd) kept working —
  not investigated yet.
- Ideas (IDEAS.md): an ISO9660 driver + `mount` of ISO / disk / partition images as volumes
  `VD0:`, `VD1:`…; an mstsc-compatible RDP server (~3000–4500 lines, TLS without NLA); GC
  interpreter idle-loop skipping / an x86 JIT; NintendoEMU keyboard remapping.
