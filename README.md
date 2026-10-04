# Onyx — a multi-process operating system for the Raspberry Pi 4

**Onyx** is a homemade, **preemptive, multi-process operating system** for the **Raspberry
Pi 4** (AArch64, also the Pi 400), built on [Circle](https://github.com/rsta2/circle) as its
hardware layer (our fork, `circle/`). It loads **ELF programs from the SD card** and runs each
as an **isolated process at EL0** — its own page table and ASID, calling the kernel by
**system calls** through a stable, append-only ABI (`kapi`, v74). An app that crashes is
killed; the system goes on. It runs on real hardware, with a full graphical desktop.

![The Onyx desktop](screenshots/desktop.png)

*Onyx* is the name of the OS, its kernel and its GUI. The repository folder is historically
named `Zircon` (a legacy name, no relation to Google's Fuchsia); some in-OS strings and
screenshots still say *Zircon* — renaming them is a separate, pending task.

## What it has

- **Kernel**: per-process address spaces (64 KB pages, ASID), apps at EL0 with every kapi
  pointer checked and per-process handles; a preemptive scheduler for the apps (100 Hz, a
  non-preemptive kernel), threads (mutexes, events, a futex, real-time priority); streams,
  pipes and stdio; a software compositor and window manager; the `RAM:` volume; FAT32 and
  exFAT volumes (`SD:`, `SD1:`…).
- **The cores, by design**: core 0 runs the kernel, the desktop and the processes; core 1 the
  sound; cores 2–3 are lent whole to an app (the emulators, Doom); with `netcore=1` (the
  shipped card) core 3 runs the network.
- **Hardware**: HDMI (any resolution), USB keyboards, mice, gamepads and MIDI devices, Wi-Fi
  (TCP/IP, DHCP, DNS, NTP), sound (PWM, an FM synthesizer, low-latency mapped ring), the GPU
  (V3D: 3D and layer compositing).
- **Desktop**: a modernised CDE — menu bar, dock with drawers and workspaces, notifications, a
  shared clipboard with history, drag & drop, themes (CDE and Milk styles), a first-run wizard,
  a Control Panel, a package manager with automatic updates.
- **Applications** (~95 apps and services on the card, ~65 `/bin` tools):
  - *Office*: Letters (word processor, PDF export), Spreadsheet, Cardfile (a database), Ledger
    (accounting), Calendar, PDF Viewer (MuPDF), RTF Reader, Archiver, text editor, calculators.
  - *Internet*: **Jet Browser** (on WebKit: JavaScript with a JIT, GPU compositing, video),
    Mail (Gmail, Outlook, IMAP / POP3 / SMTP), IRC, Courier (an HTTP client), Lisa (AI chat).
  - *Media*: Paint (layers, blend modes on the GPU), Photos, Media Player (music and video,
    FFmpeg), Screenshot, Koton (a music studio with plugins), FM Tracker, Image Viewer.
  - *Emulators*: Game Boy / Color, Game Boy Advance, NES, SNES, Nintendo 64, GameCube, and a
    Game Library. *Games*: Doom (Freedoom), Tetris, Solitaire, FreeCell, Invaders, Arkanoid,
    Minesweeper, Sokoban, 2048…
  - *Programming*: Onyx BASIC (a compiler, a VM, 3D) with the QBasic editor; C / C++ apps.
  - *System*: terminal and shell (pipes, redirections), File Viewer, Task Manager, Memory
    Monitor; remote access: telnet (`telnetd`), VNC (`vncd`), Onyx Remote for Windows
    (`rdpd`), FTP (`ftpd`, `ftpfs`).

The full catalog, with the controls and the files each app reads and writes:
[`docs/04-USER-GUIDE.md`](docs/04-USER-GUIDE.md) §8 and §12.

## Try it

Copy **all of [`sdcard/`](sdcard/)** to the root of a FAT32 microSD card, put it in a
Raspberry Pi 4 with an HDMI screen, a USB keyboard and mouse, and power on. The first boot
runs **Setup** (keyboard, time zone, Wi-Fi, resolution). [`sdcard_lite/`](sdcard_lite/) is the
minimal card (the system only), every app then installed with the Package Manager.

## Build

Prerequisites: the `aarch64-none-elf` GCC toolchain (Linux or WSL), `make`, Python 3. Details:
[`docs/03-DEVELOPER-GUIDE.md`](docs/03-DEVELOPER-GUIDE.md) §1–§4.

```sh
git clone --recurse-submodules https://github.com/stephaneweg/Onyx.git
# Circle (once): configure for the Pi 4 / AArch64 / 32-bit pixels, build its libraries
cd circle && ./configure -r 4 -p aarch64-none-elf- -d DEPTH=32 -f && cd ..
#   then the libraries listed in docs/03 §2 (lib, lib/net, addon/fatfs, addon/wlan, wpa_supplicant...)

cd kernel
make -j8 || make -j1    # the kernel (kernel8-rpi4.img), then every app, /bin tool and plugin
make stage              # copy them to ../sdcard/ (apps/<name>.app/main, bin/<tool>: no extension)
cd ..
sh tools/webkit/build-web.sh                # Jet Browser (WebKit: docs/08; needs the WebKit tree built first)
```

A kernel-only change needs no app rebuild: the apps know only the kapi table's fixed address.
After a build, `./tools/el0scan.sh sdcard/apps sdcard/bin sdcard/koton` checks that no binary
holds an instruction EL0 may not run.

**Windows**: `sh pc/build.sh` → `pc/dist/` (Koton, the NintendoEMU emulators, Onyx BASIC,
Onyx Remote).

## Test

- **On the PC**: `tools/tests/run_*.sh` (the file systems, the RAM volume, the network, the
  emulators, the apps' engines, Mail, the PDF export, the packages…); the **desktop
  simulator** runs the real apps against a stand-in kernel and makes the screenshots
  (`sh tools/tests/desktop_sim/shots.sh [name…]` → `screenshots/`).
- **On the Pi**: `/bin` self-tests (`el0test`, `faulttest`, `threadtest`, `coretest`,
  `futextest`, `ramtest`, `gpcdemo test`, `fsbench`…), `kmsg`, `ps`, `sysstat`, the
  remote shell (`python tools/onyx-telnet.py <pi-ip>`); a hang or panic leaves
  `SD:/etc/lastcrash.txt`.

## Documentation

In [`docs/`](docs/README.md), in English (Word / PDF exports in `docs/exports/`,
`python docs/build_docs.py`):

| Document | Contents |
|---|---|
| [01 — Project Overview](docs/01-PROJECT-OVERVIEW.md) | what Onyx is, the architecture, the execution model, the cores |
| [02 — Kernel Internals](docs/02-KERNEL-INTERNALS.md) | boot, memory, scheduling, exceptions and system calls, the kapi ABI, GUI, network, sound, app cores, GPU |
| [03 — Developer Guide](docs/03-DEVELOPER-GUIDE.md) | build, the app model, wtk, writing apps and tools, extending the ABI, debugging |
| [04 — User Guide](docs/04-USER-GUIDE.md) | the card, the boot options, the desktop, the terminal, every app and tool |
| [05 — Circle Changes](docs/05-CIRCLE-CHANGES.md) | the patches of our Circle fork |
| [08 — Jet Browser, the WebKit port](docs/08-WEBKIT-PORT.md) | the browser |
| [EL0 protected mode](docs/EL0-PROTECTED-MODE.md) | how the apps moved to EL0 |
| [Licensing](docs/LICENSING.md) | the licences |
| [Handoff](docs/HANDOFF.md) | where the work stands, the next tasks |

[`ARCHITECTURE.md`](ARCHITECTURE.md) is the original design record (historical: apps at EL1,
cooperative scheduling — see its banner).

## Licences

Our own code is under the **MIT licence** (the user's decision, 2026-10-01). A program that
links a copyleft library is distributed under that library's licence, its own files staying
MIT: the **kernel** under **GPL-3.0-or-later** (Circle), **Jet Browser** under **LGPL-2.1-or-later**
(WebKit), the **Media Player** under **GPL-2.0-or-later** (FFmpeg), the **PDF Viewer** under
**AGPL-3.0** (MuPDF), **Doom** under the GPL (doomgeneric). Fonts, the sound font, Freedoom and
the firmware keep their own licences. Details and the inventory:
[`docs/LICENSING.md`](docs/LICENSING.md).
