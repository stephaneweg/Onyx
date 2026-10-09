# Onyx — briefing for a local Claude Code agent: testing and debugging the Nintendo DS emulator

*Written 2026-10-09 by the cloud session that wrote the DS emulator (`ndsemu`, steps D0–D5) and fixed
its first crash. Give this whole file to the local agent. The user tests on their **Raspberry Pi 4**
with **their own game dumps**, which the cloud session cannot have; your job is to make those games
run, find what breaks, fix it, and publish the fixes.*

---

## 0. Who you are working for, and the rules

- The user is the author of **Onyx**, a homemade multi-process OS for the **Raspberry Pi 4** (AArch64)
  built on **Circle**. **The user writes in French: answer in French.** The docs stay in **English**.
- **Read first**: `CLAUDE.md` (binding rules), `docs/HANDOFF.md` — its section **"The Nintendo DS
  emulator `ndsemu`"** (what is built, what was fixed, the known gaps) —, then this file.
- The rules of `CLAUDE.md` in short:
  - **git**: before every development and every commit `git fetch origin main` + `git merge origin/main`;
    commit, `git push origin HEAD:main` (and the working branch). Commit messages end with a
    `Co-Authored-By:` line.
  - **docs**: an app's change visible to the user → `docs/04` (the `ndsemu` row); the HANDOFF section
    kept current; `python docs/build_docs.py` after editing the `.md`.
  - **packages**: a fix that changes the card → `make -C kernel stage`, then
    `.claude/skills/onyx-packages/SKILL.md` (`sh tools/pkg/publish.sh`, **and** the Pi 5:
    `make -C kernel BOARD=pi5 stage` + `publish.sh --board pi5`). The key is the user's
    (`~/.onyx/pkg-key.pem`); **never generate another one, never write it anywhere**.
  - **licence**: the emulator is ours, **MIT**, written from GBATEK's documentation. **Never paste code
    from melonDS / DeSmuME / NooDS (GPL)** — reading their behaviour to understand a bug is fine, copying
    is not (`docs/LICENSING.md`, "Emulators — clean-room").
  - **never commit a ROM, a save, a BIOS or firmware** (`tools/tests/nds/roms/` is git-ignored: the
    user's games stay outside the repository, e.g. `~/nds-roms/`).

## 1. Where the code is

| What | Where |
|---|---|
| The core (CPUs, memory, I/O, cartridge, 2D, 3D, sound) | `user/Emulators/nds/` — `nds.h` declares everything; `nds_cpu.cpp` the interpreters, `nds_mem.cpp` the buses and page tables, `nds_io.cpp`, `nds_cart.cpp` (the save chip), `nds_bios.cpp` (the HLE BIOS, the direct boot), `nds_gpu2d.cpp`, `nds_gpu3d.cpp` + `nds_render3d.cpp`, `nds_spu.cpp`, `nds_jit.cpp` (the ARM/Thumb → AArch64 JIT) |
| The app | `user/Apps/ndsemu/main.cpp` (FreeType, EN/FR: `sdcard/apps/ndsemu.app/lang/fr.txt`) |
| PC test runner | `tools/tests/nds/ndstest.cpp`, built by `sh tools/tests/run_nds_test.sh` |
| Our own test ROMs | `tools/tests/nds/src/` (`build.sh`, needs `arm-none-eabi-gcc`) → `tools/tests/nds/roms/*.nds`; `check.sh` runs the 25 checks |
| JIT fuzzer | `tools/tests/nds/jitfuzz.cpp` (the JIT against the interpreter, random blocks) |

## 2. Your tools on the PC

The PC is x86-64: there the core runs **interpreted** (the JIT is AArch64 only). For the JIT, use
**qemu-aarch64 user mode** (WSL: `sudo apt install qemu-user g++-aarch64-linux-gnu binutils-aarch64-linux-gnu`).

```sh
# the interpreter, on the PC
sh tools/tests/run_nds_test.sh                       # builds /tmp/onyx_ndstest + the 25 checks
/tmp/onyx_ndstest ~/nds-roms/game.nds 600 out.ppm    # 600 frames, the last picture

# the JIT, under qemu (same results expected, picture for picture)
aarch64-linux-gnu-g++ -std=c++17 -O2 -g -static -Iuser -Iuser/Kits -Iuser/Runtime -Iuser/Include \
  -Iuser/Libs -Iuser/Emulators tools/tests/nds/ndstest.cpp user/Emulators/nds/*.cpp -o /tmp/ndstest_a64
NDS_JIT=1 qemu-aarch64 /tmp/ndstest_a64 ~/nds-roms/game.nds 600 jit.ppm
cmp jit.ppm out.ppm                                  # JIT == interpreter?

# the JIT on our checks
printf '#!/bin/sh\nNDS_JIT=1 exec qemu-aarch64 /tmp/ndstest_a64 "$@"\n' > /tmp/jitrun.sh
sh tools/tests/nds/check.sh /tmp/jitrun.sh
```

`ndstest`'s variables (all optional):

| Variable | Use |
|---|---|
| `NDS_KEYS=100-110:1;200-210:8` | buttons held over frame ranges, a **hex mask** each (`nds.h` `BTN_*`: A 1, B 2, SELECT 4, START 8, RIGHT 10, LEFT 20, UP 40, DOWN 80, R 100, L 200, X 400, Y 800) — to get past title screens |
| `NDS_TOUCH=300-305:128,96` (`;` between ranges) | the touch screen pressed at x,y over frames |
| `NDS_SAV=file.sav` | a save loaded (the save type then known) |
| `NDS_SHOTS=dir` `NDS_SHOTEVERY=60` | a picture every N frames (watch where a game stops) |
| `NDS_TRACE=1` (`NDS_NHIST=64`) | on a stop, the last instructions of each CPU |
| `NDS_WATCH9=addr,len` / `NDS_WATCH7` | log the writes to a memory range |
| `NDS_DUMP=addr,words` | main memory dumped at the end |
| `NDS_WAV=file.wav` | the sound recorded |
| `NDS_JIT=1` | the JIT (AArch64 / qemu only); then the JIT's statistics are printed |
| `NDS_JITCODE=file` | every compiled block's host code (marker `0xFFFFFFFF`, guest pc, then the code): `aarch64-linux-gnu-objdump -D -b binary -m aarch64 file` |
| `NDS_JITNO=hex` / `NDS_JITNODP=hex` | instruction kinds left to the interpreter — **bisect a JIT bug**: the game works with a kind excluded → that kind's code generation is wrong |
| `NDS_BIOS7=bios7.bin` | the user's ARM7 BIOS (only for dumps with an encrypted secure area) |

A fault inside the JIT's code under qemu prints its **offset in the code buffer**, the word there and
the registers (`ndstest`'s fault handler).

## 3. Testing on the Pi

- Copy a game: FTP (`ftpd SD:/` on the Pi, or `cat FTP:<pc-ip>:2121/game.nds > SD:/roms/game.nds` from
  an FTP server on the PC), or the SD card. Games go in `SD:/roms/` (the Game Library finds them).
- Run: from the Game Library, or the Terminal / telnet: `run ndsemu SD:/roms/game.nds`
  (`--interp` without the JIT, `--fullscreen`). **F12** shows the speed (fps, frame time, JIT or
  interpreter, `3D on a core`). Game ▸ Interpreter switches at run time.
- After a crash: `dmesg` (kmsg). A line like `appcore: core 2: fault ... at pc 39xxxxxxx` is in the
  JIT's code (the code region starts at `0x390000000`; ndsemu's 16 MB buffer is the first allocation):
  **pc − buffer start = the offset** to look for under qemu with `NDS_JITCODE`. A pc in `0x2000xxxxx`
  is the emulator's own C++ (`aarch64-none-elf-addr2line -e user/ndsemu.elf <pc>` with the
  build's ELF).
- Updating: `apps/ndsemu.app/main` can be replaced without a reboot (or `pkg upgrade` once published).

## 4. The test campaign (what the user expects)

For each of the user's games (start with **Pokémon Mystery Dungeon: Explorers of Sky**, the one that
crashed — fixed in `ndsemu` 1.0.1 —, then the others they give you):

1. **Boot**: does it reach the title screen? (Interpreter on the PC first: if the interpreter fails, the
   bug is in the emulation, not the JIT.)
2. **JIT = interpreter**: same pictures under qemu (`cmp`). Different → bisect with `NDS_JITNO`, then
   the block (`NDS_JITCODE`, `NDS_TRACE`); add the pattern to `jitfuzz` if it can catch it; fix in `nds_jit.cpp`.
3. **Play** a few minutes on the Pi: controls, touch screen, sound (crackles? latency?), 3D.
4. **Save**: save in game, quit, restart — the save is there (`<rom>.sav` beside the ROM). A game that
   says its save is corrupt or cannot save → the save chip's type was guessed wrong (`nds_cart.cpp`,
   `detect`): note the game's code (`ndstest` prints `title [code]`) and the size it expects.
5. **Speed** (F12): note fps and frame time with the JIT; 2D games should hold 60. Too slow → measure
   where (`--interp` vs JIT, `3D on a core` or not) before optimising.
6. Write each game's result in a table in `docs/HANDOFF.md`'s DS section (game, code, boots, plays,
   saves, fps, bugs).

## 5. Known gaps (do not chase them as bugs unless a game needs them)

- Instructions left to the interpreter by the JIT: SMULxy / SMLAxy / QADD… (the DSP extension), MRC /
  MCR, SWI, MSR / MRS — correct but slower.
- The 3D rasterizer divides per pixel (speed); no anti-aliasing; edge marking / fog approximations.
- The idle-loop skip exists only in the JIT.
- Not emulated: Wi-Fi, the microphone, the GBA slot, the DSi mode, the firmware's own boot menu.
- Encrypted secure areas need the user's `bios7.bin` in `SD:/apps/ndsemu.app/`.

## 6. Already fixed (so you know the territory)

- **1.0.1**: the blocks' exits to the JIT's exit stub were conditional branches (±1 MB reach); past
  1 MB of compiled code they jumped into nothing — the crash `undefined instruction at pc 0x390200020`.
  Exits now go through a `b` beside them; `Asm::patch` traps an out-of-range branch when built with
  `-DNDS_DEBUG`. **Build `ndstest` with `-DNDS_DEBUG` when chasing a JIT bug.**

## 7. When you finish

Report to the user in French: the games tested and their state, what you fixed (commits), the
packages published (`ndsemu` version), what remains. Update `docs/HANDOFF.md`'s DS section.
