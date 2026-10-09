# Onyx on the Raspberry Pi 3 (and the Zero 2 W) — the porting study

> **A study only (2026-10-09), nothing built.** It comes from a read-only audit of the tree at `7dc12c1d` (the
> Circle fork at `ec821a94`, upstream Circle 51.1.1) and follows the shape of [`PI5-PORT.md`](PI5-PORT.md). Line
> numbers drift: search for the symbol when a reference is off. **(verify)** marks what only a board on the desk
> can settle.
>
> **The short answer.** For the **kernel and the boot**, the Pi 3 is the same order of work as the Pi 5 — less,
> even: Circle supports it well, its CPU is ARMv8.0 like the Pi 4's (the Pi 4's programs run on it unchanged), its
> peripherals are the Pi 4's family (the same GPIO, PWM, DMA, mailbox), and the core number is in Aff0 as on the
> Pi 4. Two blockers (the user window's VA, a link error), a handful of breaks. **Two things are of another
> order:** the **GPU** — VideoCore IV (V3D 2.1) is another generation, not an evolution of the Pi 4's like the
> Pi 5's 7.1: another instruction set, other control lists, another texture tiling, no MMU; a new driver, not a
> port — and the **board's means**: 1 GB of RAM (512 MB on the Zero 2 W) and an in-order A53 at 1.2-1.4 GHz, so
> Jet Browser and the GameCube emulator are out of reach whatever is written, and the N64 runs below full speed.
> A Pi 3 Onyx is a **desktop, the apps, the light emulators (GB/GBA/NES/SNES), BASIC, Koton, the network
> services** — a 1 GB Pi 4 that is two to three times slower.

## Contents

1. [The Pi 3 in one page](#1-the-pi-3-in-one-page)
2. [Verdict: what breaks, what carries over](#2-verdict-what-breaks-what-carries-over)
3. [Phase 0 — decisions](#3-phase-0--decisions)
4. [Phase 1 — build and SD card](#4-phase-1--build-and-sd-card)
5. [Phase 2 — the boot blockers](#5-phase-2--the-boot-blockers)
6. [Phase 3 — first boot to the desktop](#6-phase-3--first-boot-to-the-desktop)
7. [Phase 4 — memory: a small-memory profile](#7-phase-4--memory-a-small-memory-profile)
8. [Phase 5 — sound, network, storage, GPIO](#8-phase-5--sound-network-storage-gpio)
9. [Phase 6 — the GPU: VideoCore IV (V3D 2.1)](#9-phase-6--the-gpu-videocore-iv-v3d-21)
10. [What runs, what does not](#10-what-runs-what-does-not)
11. [The Zero 2 W](#11-the-zero-2-w)
12. [Effort, compared with the Pi 5](#12-effort-compared-with-the-pi-5)
13. [Test plan](#13-test-plan)
14. [Open questions (verify on hardware)](#14-open-questions-verify-on-hardware)
15. [Sources](#15-sources)

---

## 1. The Pi 3 in one page

| | Raspberry Pi 4 (today) | Raspberry Pi 3 B / 3 B+ / 3 A+ | Zero 2 W |
|---|---|---|---|
| SoC | BCM2711 | BCM2837 (3 B) / BCM2837B0 (3 B+, 3 A+) | BCM2710A1 (the BCM2837 in a package with its RAM) |
| CPU | 4× Cortex-A72 (ARMv8.0, out of order) @ 1.5-1.8 GHz | 4× **Cortex-A53** (ARMv8.0, **in order**) @ 1.2 GHz (3 B) / 1.4 GHz (3 B+) | 4× A53 @ **1.0 GHz** |
| ISA | ARMv8.0 + CRC32, no crypto, no LSE | the same (CRC32 **(verify)**, no crypto) | the same |
| MPIDR | core in Aff0 | core in Aff0 (no change) | the same |
| RAM | 1-8 GB | **1 GB** LPDDR2 (minus the GPU's share) | **512 MB** |
| GPU | V3D 4.2, 8 QPUs, an MMU | **VideoCore IV, V3D 2.1**: 12 QPUs @ ~300-400 MHz **(verify)**, no MMU, other ISA | the same |
| Peripherals | `0xFE00_0000` | `0x3F00_0000` (`ARM_IO_BASE`); the GPU sees RAM at bus `0xC000_0000` | the same |
| Interrupts | GIC-400 | **no GIC**: the BCM2835 controller + the ARM-local one (IPIs through its mailboxes) | the same |
| Core start | spin table (armstub) | spin table (the firmware's built-in armstub8) | the same |
| USB | xHCI (VL805) | **DWHCI** (DesignWare OTG, USB 2.0; the 3 B / 3 B+'s Ethernet on it) | one OTG micro-USB port |
| Ethernet | BCM54213 | SMSC LAN9514 (3 B, 100 Mbit/s) / **LAN7800** (3 B+, ~300 Mbit/s over USB 2.0); none on the 3 A+ | none |
| Wi-Fi | BCM43455 (2.4 + 5 GHz) | **BCM43430** (3 B, 2.4 GHz) / BCM43455 (3 B+, 3 A+) | BCM43430 rev 2 / **43436** (2.4 GHz) |
| Audio | jack (PWM1), HDMI, USB | jack (**PWM0**, 250 MHz clock), HDMI; **no USB audio in Circle** | HDMI only (no jack) |
| Display | 2× micro-HDMI, up to 4K | 1× HDMI, up to 1920×1200 **(verify)** | 1× mini-HDMI |
| Boot files | `start4.elf`, `fixup4.dat`, armstub, `bcm2711-*.dtb` | **`bootcode.bin`**, `start.elf`, `fixup.dat` (`kernel8.img`; the DT optional) | the same |

**Circle supports it** (`circle/README.md`: the Pi 3 B, 3 A+, 3 B+ and Zero 2 W "Tested", AArch64 with
`RASPPI = 3`): `-mcpu=cortex-a53`, `TARGET = kernel8` (`circle/Rules.mk:107-110`), the BCM2835 interrupt
controller, the spin-table multicore, DWHCI USB, the SMSC951x and LAN7800 Ethernet, the BCM43430 / 43455 Wi-Fi,
the PWM and HDMI sound, the SD card (SDHOST — the Wi-Fi takes the EMMC controller), the legacy hardware RNG,
and on the Pi 1-3 only the firmware's **GLES 2 through VCHIQ** (`circle/addon/vc4`). Not on the Pi 3: xHCI, the
GIC, DMA4, PCIe, the USB audio class (`circle/lib/usb/Makefile:36-44`, `lib/sound/Makefile:35-37`).

## 2. Verdict: what breaks, what carries over

### Blockers (the Pi 3 kernel will not link or will not boot)

| # | Problem | Where | Phase |
|---|---|---|---|
| B1 | **The user window is out of the VA space.** Circle gives the Pi 3 `TCR_EL1.T0SZ_4GB` (a 32-bit VA), while every program lives at [8 GB, 60 GB) (code 8 GB, heap 10 GB, the kits 16-28 GB, the window-server slots to 60 GB) → the first app faults at its first instruction | `circle/lib/memory64.cpp:358-360` (the `RASPPI == 3` branch), `kernel/include/kern/layout.h` | 2 |
| B2 | **`g_ulOnyxCrashArea` is not defined** on the Pi 3: the fork defines it inside `#if RASPPI >= 4` (patch 15), `kernel/sys/crashlog.cpp` uses it → link error | `circle/lib/memory64.cpp:128-130` | 2 |
| B3 | **Pi-4-only code compiled unconditionally**: `V3D_IRQ GIC_SPI(74)` (no `GIC_SPI` on the Pi 3: `bcm2711int.h` only) and the V3D 4.2 bring-up poking `ARM_IO_BASE + 0xC00000` — the VC4's V3D base on the Pi 3, but other registers: a hang or garbage at the first `gpu_info`; `COutUSB : CUSBSoundBaseDevice` (not built for the Pi 3) | `kernel/sys/v3d.cpp:66-118`, `kernel/sys/sound.cpp:35, 348-374` | 2 (fence), 6 (port) |

### Breaks (a feature fails or is wrong)

| Problem | Where | Phase |
|---|---|---|
| **The memory split is the Pi 4's**: a fixed 256 MB pager for the apps (`PAGE_RESERVE`), the rest kernel heap — on 1 GB the apps get ~250 MB, as on a 1 GB Pi 4 (Jet ran out on Wikipedia there, `HANDOFF.md`); on 512 MB the kernel heap is ~180 MB | `circle/include/circle/memorymap64.h:43`, `circle/lib/memory64.cpp:79-82`, `kernel/mm/vm.cpp` | 4 |
| **The header's PWM and the jack share PWM0 on the Pi 3** (the Pi 4's jack is on PWM1), and the clock is 250 MHz, not 125 MHz: `gpio.cpp`'s `PWM_CLOCK`, `PwmApply`, `GpioPwmClockKeep` are wrong there | `kernel/sys/gpio.cpp:57, 209-246` | 5 |
| **The SD waits busy-spin**: the fork's yielding waits (patches 7/7b) and High Speed (patch 9) are in the EMMC driver, compiled out with `USE_SDHOST` (which the Pi 3 needs for the Wi-Fi): loads and ISO streaming block core 0 | `circle/addon/SDCard/emmc.cpp:2177-2215, 2639-2670` | 5 |
| Wi-Fi: the 3 B's **43430** and the Zero 2 W's **43436** firmware files are not on the card (only 43455 / 43456) | `sdcard/firmware/`, `circle/addon/wlan/ether4330.c:289-306` | 1 |
| No USB audio (Circle builds the audio class for the Pi 4/5 only): `sound.ini` `output = usb` must fall back | `kernel/sys/sound.cpp` | 5 |
| `HasJack ()` says yes on a Zero 2 W (no jack) | `kernel/sys/sound.cpp:376-382` | 5 |
| `kapi_kernel_info` says `board pi4` (it knows 4 and 5) | `kernel/sys/kapi.cpp:347-350` | 1 |
| `config.txt`'s global `core_freq=300` lowers the Pi 3's VPU / L2 / SDRAM clock **(verify)** | `sdcard/config.txt:11` | 1 |
| GPU apps: `gpu_info` must say 0 until Phase 6; `gpu_program` apps (gcemu, v3dprog) bring **V3D 4.2 / 7.1** shaders | `user/Libs/v3d/*`, `user/Apps/gcemu/gxv3d.h` | 2, 6 |

### Carries over unchanged

Everything the Pi 5 study lists as board-independent (the trap frame, the vectors, the syscalls, the address
spaces and their 64 KB granule, the scheduler, IPC, the VFS, the ELF loader, Elegant and UIKit, the kits, TCP/IP,
FatFs and its patches, the daemons, BASIC, the emulators' cores, the GameCube JIT) — and more than for the Pi 5:

- **The Pi 4's userland as it is.** `-mcpu=cortex-a72` is ARMv8.0 + CRC, no LSE, no crypto: the A53 runs it
  (the tuning is the A72's scheduling only). TLS already turns the AES / SHA instructions off (`user/Libs/tls/Makefile:24-35`).
  The EL0 setup (`El0CoreInit`, no PAN) is ARMv8.0's, as the A72's.
- **The core number** is in Aff0 (no Pi 5 B1), `TPIDRRO_EL0` as everywhere.
- **The IPIs** go through `CMultiCoreSupport::SendIPI` (`net.cpp`'s `IPI_NET_READY`, `appcore.cpp`'s `IPI_USER`):
  Circle does them with the ARM-local mailboxes on the Pi 3. One point to check: Circle's Pi 3 handler reads core
  0's pending register on every core **(verify)** (§14).
- **The display**: the firmware framebuffer, the compositor's 2D DMA on the legacy `CDMAChannel` (patch 12 is on
  it), the runtime resolution change (the Pi 3's mailbox has it all).
- **No hard-coded `0xFE…` address** in the kernel outside `v3d.cpp`; no GIC, no DMA4 use. Every timing reads
  `CNTFRQ_EL0` (19.2 MHz on the Pi 3). The watchdog, the reboot, the ACT LED, `CCPUThrottle` are generic.
- **The network choice made for the Pi 5** (`system.ini` `network = auto | ethernet | wlan`, fork patch 30)
  serves the Pi 3 B / B+ as it is: their USB Ethernet is a `NetDeviceTypeEthernet` device too.

## 3. Phase 0 — decisions

1. **The distribution.** Two ways, the user to choose:
   - **(A) A separate one, as the Pi 5** (`sdcard3/`, `sdcard3_lite/`, the repository's `pi3/` folder,
     `versions3.ini`, `BOARD=pi3`): the same machinery (`mkcard3.py`, `publish.sh --board pi3`, `pkglib.h`'s
     board). Cheap to keep, since the programs are the Pi 4's own files (git stores them once); its own
     packages decide what is offered (no Jet, no gcemu).
   - **(B) One card for the Pi 3 and the Pi 4**: `config.txt` already has `[pi3]` / `[pi3+]` / `[pi02]` sections
     with `kernel=kernel8.img`; two kernels and both boot firmwares on one card, the same programs. Simpler for
     the user, but the package rule gets complicated (a Pi 3 must not be offered Jet, its kernel package differs).
   - **Recommended: (A)**, for the same reasons as the Pi 5 (a card is one product, its packages its own), with
     the Pi 4's program binaries copied in by `mkcard3.py` instead of a third build of the apps.
2. **The boards.** The Pi 3 B+ (1.4 GHz, LAN7800, 43455 dual-band Wi-Fi) is the easy target; the 3 B (43430,
   2.4 GHz only) next; the Zero 2 W (512 MB) last, as a small-memory profile (§11).
3. **The GPU's ambition** (§9): none at first (`gpu_info` 0: every app has its CPU path except gcemu, out of reach
   anyway), then the kernel's own `gpu_render` path on VideoCore IV (the 3D demos, BASIC 3D, n64emu), never
   `gpu_program` (the app shaders: gcemu, v3dprog — Pi 4 / Pi 5 only).

## 4. Phase 1 — build and SD card

### 4.1 Circle for the Pi 3

- A third tree, `circle3/` (beside `circle/` and `circle5/`), made by a `tools/pi3/circle3.sh` copied from
  `tools/pi5/circle5.sh`: `./configure -r 3 -p aarch64-none-elf- -d DEPTH=32 -f`, the same libraries
  (`lib`, `lib/sched`, `lib/fs`, `lib/fs/fat`, `lib/usb`, `lib/input`, `lib/net`, `lib/sound`, `addon/SDCard`,
  `addon/fatfs`, `addon/wlan`, wpa_supplicant).
- `circle/Rules.mk` then gives `-mcpu=cortex-a53`, `TARGET = kernel8`, `LOADADDR 0x80000`, the 2 MB limit.
- Built on the Pi 3 and not on the Pi 4: `interrupt.o` (BCM2835), `dmachannel.o` only, DWHCI; **not built**:
  `interruptgic`, `dma4channel`, `bcmpciehostbridge`, `bcm54213`, xHCI, `usbaudiostreaming`, `usbsoundbasedevice`.
  The kernel must link none of them (B3).

### 4.2 The Onyx kernel

- `kernel/Makefile`: `BOARD ?= pi4 | pi5 | pi3` → `CIRCLEHOME = ../circle3`, `SDCARD = ../sdcard3`, the `.board`
  clean, `stage` running `python3 ../tools/pi3/mkcard3.py` (as the Pi 5's).
- `kapi_kernel_info`: `board pi3` (`RASPPI == 3`), and `user/Libs/pkg/pkglib.h`'s `board ()` / default repository
  `…/pi3`, `tools/pkg/mkrepo.py` / `publish.sh` `--board pi3`, `packages.ini` `files.pi3` / `summary.pi3` and
  `boards =` (`jet`, `gcemu`, `v3dprog` and the GPU-only demos `boards = pi4 pi5`).

### 4.3 The SD card for the Pi 3 (`sdcard3/`)

| File | Source | Note |
|---|---|---|
| `kernel8.img` | the Pi 3 build | the firmware's default name for a 64-bit Pi 3 (`arm_64bit=1`) |
| `bootcode.bin`, `start.elf`, `fixup.dat` | `circle/boot/Makefile:19-24` downloads them | the Pi 3 boots from the card's firmware (no EEPROM) |
| `config.txt` | a Pi 3 one in `tools/pi3/overlay/` | `arm_64bit=1`, `kernel_address=0x80000`, `framebuffer_depth=32`, `gpu_mem` (§7), **no `core_freq=300`** (the Pi 4's; move it under `[pi4]` in `sdcard/config.txt` too), `hdmi_force_hotplug=1` |
| `cmdline.txt` | the overlay | `width=1920 height=1080` (or 1280×720 on the Zero 2 W), `netcore=1`, no `sdhs` (no effect with SDHOST) |
| `firmware/brcmfmac43430-sdio.{bin,txt,clm_blob}`, `brcmfmac43436-sdio.*` (+ the 43455 ones already there) | `circle/addon/wlan/firmware/Makefile:13-22` | the 3 B and the Zero 2 W |
| `bcm2710-rpi-3-b(-plus).dtb`, `bcm2710-rpi-zero-2-w.dtb` | optional | Circle needs no DT on the Pi 3; Onyx's DT reader is `RASPPI >= 4` |
| `etc/pkg/pkg.ini` | the overlay | `repo = https://stephaneweg.github.io/onyx-packages/pi3` |
| no `start4.elf`, `fixup4.dat`, armstub, `bcm2711-*.dtb` | — | the firmware's built-in armstub8 does the spin table |

## 5. Phase 2 — the boot blockers

### 5.1 B1 — the user window in the VA space (a fork patch)

In `circle/lib/memory64.cpp`, the `RASPPI == 3` branch: keep `TCR_EL1_IPS_4GB` (every physical address is below
1 GB) and take **`TCR_EL1_T0SZ_64GB`**, as the Pi 4. With the 64 KB granule a 36-bit VA still starts the walk at
level 2, so the tables keep their shape: Circle's level-2 table (64 KB, 8192 entries) gets its 3 RAM entries
(`LEVEL2_TABLE_ENTRIES 3`, 1.5 GB) and Onyx's per-process copy (`kernel/mm/addrspace.cpp`, the user entries from
index 16 = 8 GB) works unchanged. `layout.h`'s `USER_VA_CEILING` ("T0SZ_64GB hard limit on RPi 4") then holds for
the Pi 3 too; `KERNEL_IDENTITY_END` (4 GB) is above all of the Pi 3's RAM. A fork patch (docs/05), `#if RASPPI == 3`.

### 5.2 B2 — the crash area

Move `u64 g_ulOnyxCrashArea = 0;` out of `#if RASPPI >= 4` (`circle/lib/memory64.cpp:128-130`). With 0, `crashlog.cpp`
takes its record from the heap (no record kept across a watchdog reboot). Better, later: 64 KB at the top of the
low RAM, out of the heap, as the Pi 5's seg0 top (patch 29) — if the Pi 3's RAM survives a watchdog reset
**(verify)**.

### 5.3 B3 — fence what the Pi 3 has not

- `kernel/sys/v3d.cpp`: under `RASPPI <= 3`, `PowerOn` / `Up` say "no GPU" before any register or interrupt
  (`gpu_info` 0) — the Pi 5's B4 again; `V3D_IRQ` = `ARM_IRQ_3D` (`bcm2835int.h:54`) for Phase 6.
- `kernel/sys/sound.cpp`: `COutUSB` under `RASPPI >= 4`; `output = usb` → the jack or HDMI.
- `kernel/sys/gpio.cpp`: the PWM under its own board test (§8.4).

## 6. Phase 3 — first boot to the desktop

Aim: the desktop on HDMI, a USB keyboard and mouse, the SD card, the terminal; no sound, no network.

1. **Serial log**: the mini-UART / PL011 on GPIO 14/15 (the Pi 3's Bluetooth takes the PL011 unless
   `dtoverlay=disable-bt` / `miniuart-bt` **(verify)** which one Circle's `CSerialDevice` drives with Onyx's
   `config.txt`).
2. **Framebuffer**: 32 bpp, 1920×1080 — the firmware's double-height framebuffer (Onyx's present) takes
   1920×2160×4 ≈ 16 MB of `gpu_mem`: `gpu_mem=64` is enough without the GPU (§7).
3. **USB** on DWHCI: the keyboard, the mouse, a hub. Gamepads and USB MIDI work on DWHCI; Circle warns that MIDI
   can drop events without its USB FIQ (`usbboost=`, `USE_USB_FIQ`, `circle/README.md:32-34`) — Onyx runs without
   the FIQ (`sysconfig.h:243`): leave it so, check a MIDI keyboard.
4. **SD card** on SDHOST: it works, slower (no High Speed, busy waits: §8.3).
5. **SMP and the cores' roles**: as on the Pi 4 (core 0 the kernel and the processes, core 1 the sound and the
   watchdog, cores 2-3 the app cores, core 3 the network with `netcore=1`). **Watch the heat**: the 3 B throttles
   at 80 °C, the 3 B+ softens at 60 °C **(verify)**; Onyx pins the CPU at its maximum (`CCPUThrottle
   CPUSpeedMaximum`): a heatsink is a must, a fan for the long sessions.

## 7. Phase 4 — memory: a small-memory profile

The layout (the VAs) does not depend on the RAM; the **split** does. Today (`circle/include/circle/memorymap64.h:43`,
`circle/lib/memory64.cpp:79-82`, docs/02 §3): a fixed **256 MB pager** (`PAGE_RESERVE`) is the apps' pool on a board
without a high zone, the rest is the kernel's heap (the windows' canvases and chrome, the wallpaper, the
full-screen buffer, the network's 1 MB of requests, the crash buffers, `RAM:`).

| | 1 GB Pi 4 / Pi 3 (today's split) | Pi 3, proposed | Zero 2 W, proposed |
|---|---|---|---|
| ARM RAM (`gpu_mem=64`) | ~960 MB | ~960 MB | ~448 MB |
| the apps' pool | 256 MB | **~550-600 MB** | ~200 MB |
| kernel heap | ~680 MB | ~300 MB **(verify the canvases fit)** | ~200 MB |
| `RAM:` | ~60 MB (a quarter of the free) | 32 MB or off | off |

What to do (a fork patch and a kernel option, useful to the 1 GB Pi 4 too — it is `HANDOFF.md`'s "a reserve chosen
at boot from the RAM size, a small-memory profile, a `vmmap` tool"):

1. **`PAGE_RESERVE` chosen at boot** from the RAM the firmware gives (a share, a floor and a ceiling), not a
   constant; a `pagereserve=` option in `cmdline.txt` to override it.
2. **The kernel's big allocations sized by the screen**, not by `SCREEN_MAX_W/H` (already so for the canvases:
   check the full-screen buffer and the compositor's back buffer).
3. **The preload list** (SystemKit's) and Jet's 80 MB preload off below 2 GB.
4. **`memmon`** shows the two pools; the Package Manager warns before installing an app that needs more than the
   pool (a `ram =` key in `packages.ini`).

## 8. Phase 5 — sound, network, storage, GPIO

### 8.1 Sound

- **The jack** (PWM0, Circle's `CPWMSoundBaseDevice`, its 250 MHz clock on the Pi 3): Onyx's mixer and its
  short-chunk low-latency trick are the Pi 4's (the legacy PWM driver, not the RP1's: it holds).
- **HDMI** (`CHDMISoundBaseDevice`, supported on the Pi 3): as on the Pi 4.
- **No USB audio** (Circle): `sound.ini` `output = usb` and `auto` must skip it; the Sound applet must not offer
  it (`kapi_sound_output`'s list).
- Koton's latencies are the Pi 4's (the same device); its CPU budget is not (§10).

### 8.2 Network

- **Wi-Fi**: `CBcm4343Device` with the 43430 (3 B: 2.4 GHz only, so patch 26's 5 GHz preference is moot) or the
  43455 (3 B+); the firmware files on the card (§4.3). The SDIO bus is the EMMC controller's (hence SDHOST for
  the card).
- **Ethernet**: the 3 B's LAN9514 and the 3 B+'s LAN7800 are USB devices Circle drives (`smsc951x`, `lan7800`):
  `network = auto` (the Pi 5's work) takes them when a cable is plugged in — on the 3 B+, ~300 Mbit/s, much better
  than the Wi-Fi for FTP and the remote desktop. Default on the Pi 3: `auto`.
- The daemons (FTP, telnet, VNC, rdpd) are pure kapi: nothing to change; their CPU cost is higher (rdpd's
  encoding, §10).

### 8.3 Storage

- **SDHOST** (`USE_SDHOST`, `circle/include/circle/sysconfig.h:415-419`): the Pi 3's card controller when the Wi-Fi
  takes the EMMC one. The fork's yielding waits (patches 7/7b) and High Speed (patch 9) are the EMMC driver's:
  **port them to `sdhost.cpp`** (a wait hook in its data loop, its clock at 50 MHz) — else every file load blocks
  core 0 (the desktop freezes while an app starts). A fork patch.
- USB mass storage on DWHCI: works (patch 28's volumes are generic), at USB 2.0 speed.

### 8.4 GPIO

The header is the Pi 4's (BCM2835-style GPIO: `kernel/sys/gpio.cpp` as it is, pins, edges, I2C 1, SPI 0), except
the **PWM**: on the Pi 3 the jack is on PWM0 too, so the header's PWM (GPIO 12/13/18/19, PWM0) **collides with the
sound** and its clock is 250 MHz. Either refuse the header's PWM while the jack plays (`EBUSY`), or run it at the
sound's 250 MHz (`PWM_CLOCK` by board) and stop the jack while it is used. The simplest: `KAPI_GPIO_PWM` →
`-KAPI_EBUSY` when the sound's output is the jack, the 250 MHz clock otherwise.

### 8.5 Small things

- **The hardware RNG**: Circle's legacy `CBcmRandomNumberGenerator` is the Pi 3's own (the BCM2835 RNG, which the
  Pi 4 lacks): `kapi_random` can take its entropy on the Pi 3 (the Pi 4's comment, `kapi.cpp:1070-1082`, says why
  not there).
- `HasJack ()`: no jack on the Zero 2 W (and the 3 A+ has one).
- The "Raspberry Pi 4" strings (`Apps/irc/main.cpp:907`, `Apps/telegram/mtproto.h:87`, `Apps/lisa/main.cpp:349`,
  `Apps/setup/main.cpp:352`, `Runtime/libc/posix/misc.c:149`): from `kapi_kernel_info`'s board.

## 9. Phase 6 — the GPU: VideoCore IV (V3D 2.1)

### 9.1 Why it is another job than the Pi 5's

The Pi 5's V3D 7.1 is the Pi 4's 4.2 grown up: the same packet families (Mesa's one `v3d_packet.xml` with
`min_ver` / `max_ver`), the same TMU / TLB model, the same QPU encoding with fewer registers — a port, done in a
day with a translator (`user/Libs/v3d/qpu.h`). The Pi 3's VideoCore IV is the generation before:

| | V3D 4.2 (Pi 4) / 7.1 (Pi 5) | VideoCore IV, V3D 2.1 (Pi 3) |
|---|---|---|
| GPU memory | its own MMU (Onyx: off), physical addresses | **no MMU**: bus addresses (`0xC000_0000 \| phys`), contiguous buffers |
| control lists | `v3d_packet.xml` 4.x | Mesa's `vc4` packets (`vc4_packet.h`, the `v21` packets): another binning config, the **rendering list written by the host one tile at a time** (tile coordinates, a branch to the bin's sub-list, a store per tile) |
| shader state | GL shader state record, attribute records (4.x layout) | the 2.1 GL / NV shader records (other layout), the VPM set up by the shader itself (VPM / VCD setup registers) |
| QPU | 4.2: accumulators r0-r5, one regfile of 64; 7.1: no accumulators | **accumulators r0-r5 + two regfiles A and B of 32**, other instruction encoding, read ports by file, the TMU / SFU / VPM / TLB reached by writing special registers, varyings read through r5 |
| textures | UIF / LT tiling | **T-format (4 KB tiles of 1 KB micro-tiles) and LT-format**, RGBA8888 / RGB565 / ETC1, up to 2048×2048 |
| render targets | RGBA8 / 16-bit float, 64×64 tiles | RGBA8888 / BGR565 only, 64×64 tiles (32×32 multisampled) |
| power | PM_GRAFX + ASB (Pi 4), SMS (Pi 5) | the firmware (mailbox: the V3D power domain / `ENABLE_QPU`) **(verify)** |
| speed | 8 QPUs @ ~500 MHz (~32 GFLOPS) | 12 QPUs @ ~300-400 MHz (~24 GFLOPS **(verify)**), shared bandwidth with the CPU on LPDDR2 |

So everything is new: the register map, the packets (but **`tools/v3d/genpackets.py` can be taught Mesa's vc4
packet XML** for the generator part), the QPU assembler and simulator (`tools/qpu`: a third ISA — open references:
Mesa's `vc4_qpu_defines.h` / `vc4_qpu_disasm.c`, Broadcom's published *VideoCore IV 3D Architecture Reference
Guide*, py-videocore, vc4asm), the kernel's six shaders (`kernel/sys/v3d_shaders.qasm`: VS_CLIP, CS_CLIP, FS_COLOR,
FS_COLOR_AT, FS_TEX, FS_TEX_AT), the texture tiling (`kern/v3d_tiling.h`: T / LT), the per-tile rendering list.

### 9.2 Another route: the firmware's GLES 2 (VCHIQ)

Circle's `addon/vc4` (Pi 1-3 only) reaches the firmware's own OpenGL ES 2 / EGL / OpenVG through VCHIQ, with a
Linux-emulation layer. It is a working GL with no driver to write — but closed firmware, its own memory (a larger
`gpu_mem`), a big addition to the kernel, and an API (GLES 2) none of Onyx's GPU kapi calls speaks: `gpu_render`
would become a small GLES 2 client in the kernel. **Not recommended**: the native driver is more work but keeps
Onyx's one GPU model and its tools (the simulator, the tests).

### 9.3 The plan, smallest useful first

| Step | What | Gives |
|---|---|---|
| G0 | `gpu_info` 0 on the Pi 3 (Phase 2) | every app on its CPU path (gpucomp: Jet — out of reach anyway —; teapot, gpudemo, BASIC 3D, planets3d, n64emu's software renderer) |
| G1 | the bring-up: the firmware powers the V3D, IDENT0 read (`'V3D'`, version 2), the interrupt (`ARM_IRQ_3D`), a binning + rendering list that clears the screen | `gpu_info` says "V3D 2.1" |
| G2 | `tools/qpu`: a vc4 mode of the assembler (its encoding, its restrictions: the regfile A/B read ports, the accumulator latency after an SFU / TMU, the thread switch rules) and of the simulator; the six kernel shaders written for it, tested against 4.2's in `kshaders_test` as the 7.1 ones were | the shaders, checked on the PC |
| G3 | `gpu_render` / `gpu_render3` / `gpu_vbuf` on vc4: the per-tile rendering list, the 2.1 shader records, the T-format textures, the alpha test, the depth buffer | teapot, gpudemo, BASIC 3D, planets3d, **n64emu**'s GPU path |
| — | `gpu_program` (the apps' own shaders) | **not done**: gcemu (out of reach on the A53) and v3dprog stay Pi 4 / Pi 5 packages (`boards = pi4 pi5`) |

G1-G3 is the size of the Pi 4's own V3D work (docs/02 §15), not of the Pi 5's port. Worth it mainly for n64emu
(§10) and the 3D demos; the desktop composes on the CPU anyway.

## 10. What runs, what does not

Expect **two to three times the Pi 4's CPU time** (an in-order A53 at 1.2-1.4 GHz, 512 KB of L2 shared, LPDDR2 at
about half the Pi 4's bandwidth **(verify)**). The Pi 4's figures come from `HANDOFF.md`, `PI5-PORT.md` §10,
`docs/daw/PERFORMANCE.md` and `docs/04`.

| | Pi 4 | Pi 3 (estimate) | Verdict |
|---|---|---|---|
| the desktop, Elegant (CPU compositing), the apps, the Control Panel, the Terminal, Ledger, Letters, BASIC | fluid | slower, usable | **yes** |
| GB / GBC / NES / SNES / GBA emulators | full speed with margin | full speed (SNES and GBA to check) | **yes** |
| n64emu (OoT) | 12.3 ms a frame, 50.9 fps | ~25-35 ms, below full speed; worse with the software renderer | **partly** — needs G3 |
| gcemu (GameCube) | ~21 ms a field for a 20 ms budget, both app cores full | out of reach | **no** (`boards = pi4 pi5`) |
| Jet Browser (WebKit) | runs; short of memory on a 1 GB Pi 4 | its 80-100 MB image in a ~250 MB pool, an A53 for WebKit | **no** (maybe on a 3 B+ with §7's split, as a curiosity) |
| Media Player (FFmpeg) | ~720p H.264, 480p VP9 / H.265 | ~480p H.264, 360p others **(verify)**; the Pi 3's hardware H.264 decoder needs MMAL over VCHIQ (not ported) | **partly** |
| PDF Viewer (MuPDF) | fluid | slower renders, big documents press on memory | **yes** |
| Koton | 64 SF2 voices ≈ 5-10 % of an A72 core | ≈ 15-25 % of an A53 core; plugins fewer | **yes**, lighter projects |
| Doom, SuperTuxKart (`stk`) | Doom fine; stk heavy | Doom fine; stk no | Doom **yes** |
| FTP, telnet, VNC, rdpd, OnyxRemote | fine | fine; rdpd's encoding slower | **yes** |
| Telegram, mail, IRC, Lisa, llm helper | fine | fine (TLS without crypto instructions, as the Pi 4) | **yes** |

## 11. The Zero 2 W

The BCM2837 in a smaller package: **512 MB**, a 1 GHz A53, one micro-USB OTG port (a hub for a keyboard and a
mouse), mini-HDMI, no jack, Wi-Fi 2.4 GHz (43430 rev 2 / 43436: Circle says "WLAN unknown for new revision"), no
Ethernet. The same kernel as the Pi 3 (`kernel8.img`, `[pi02]` in `config.txt`); a small-memory profile (§7:
`RAM:` off, the preload off, 1280×720 to save the framebuffer and the canvases), the light apps only. Second
priority, after the 3 B+.

## 12. Effort, compared with the Pi 5

| Part | Pi 5 (done 2026-10-09, not run) | Pi 3 |
|---|---|---|
| Circle | supported, "limited" framebuffer | supported, mature |
| boot blockers | 4 (core number, high RAM twice, 16 GB, V3D) | 3 (the VA window, a link error, Pi-4-only code) — smaller |
| userland | the Pi 4's (A72 code on the A76) | the Pi 4's (A72 code on the A53) |
| distribution | `sdcard5/`, `pi5/` (done) | the same machinery: a copy for `pi3` |
| sound | no jack: HDMI / USB | jack + HDMI, **no USB** |
| network | RP1 Ethernet, Wi-Fi with HDMI delicate | USB Ethernet (3 B / B+), 43430 / 43455 Wi-Fi |
| storage | the EMMC driver (the fork's patches hold) | **SDHOST: the yielding waits to port** |
| memory | 8 GB cap first, 16 GB later | **the split to rework** (§7), the real limit |
| GPU | a port (4.2 → 7.1: the generator, a translator) | **a new driver** (VC4: ISA, lists, tiling, no MMU) — the largest part |
| what it gains | faster: everything, Ethernet, RTC, fan, button | a cheaper board, the light uses |

**In short:** the kernel and the card, about as much as the Pi 5 (Phases 1-5: a few days of work, then the board's
tests); the memory profile a little more (and it helps the 1 GB Pi 4); the GPU a project of its own, to do only if
n64emu and the 3D demos on a Pi 3 are wanted.

## 13. Test plan

| Stage | Check | Tool |
|---|---|---|
| Build | `make BOARD=pi3` links; the Pi 4 and Pi 5 kernels unchanged where nothing was meant to change (objects compared) | `make`, `make BOARD=pi5`, `make BOARD=pi3` |
| Boot | the serial log to the desktop; `ps`; `coretest`, `threadtest`, `futextest`, `ringtest`, `el0test`, `faulttest` (the IPIs through the ARM-local mailboxes, the app cores) | the UART, `user/BinUtils/*test` |
| Memory | `meminfo` / `memmon`: the two pools as §7 says; a long session (the desktop, Koton, an emulator) without "out of memory" | `memmon` |
| SD | an app's start does not freeze the desktop (after the SDHOST waits); copy speed | `fstest`, a stopwatch |
| Sound | the jack, HDMI; Koton at 256 × 2 without underruns; the emulators | `soundconf`, Koton's meter |
| Network | Wi-Fi (43430, 43455); Ethernet `auto` with and without a cable; FTP speed | `ftp`, `ping`, `netstat` (`link`) |
| GPIO | pins, edges, I2C, SPI; the PWM refused while the jack plays | GPIOKit's tests, a LED |
| Heat | 30 min of an emulator at full clock: the throttling log (`CrashLogPower`) | `kmsg` |
| GPU (G1-G3) | `gpu_info` "V3D 2.1", then gpudemo, teapot, BASIC 3D, n64emu | as the Pi 5's ladder (`PI5-PORT.md` §9.3) |

## 14. Open questions (verify on hardware)

1. Circle's Pi 3 IRQ handler reads core 0's `ARM_LOCAL_IRQ_PENDING0` on every core (`circle/lib/interrupt.cpp:308-314`):
   is an IPI on an app core ever mistaken for core 0's timer? (Onyx sends IPIs to cores 0 and 2-3.)
2. Does the Pi 3's RAM survive a watchdog reboot (a crash area kept across it)?
3. `core_freq=300`'s effect on the Pi 3 (VPU, L2, SDRAM) — move it under `[pi4]` regardless.
4. The serial console: which UART Circle's `CSerialDevice` drives on the Pi 3 with Bluetooth on, and whether
   `dtoverlay=disable-bt` is needed.
5. The CRC32 instructions on the BCM2837's A53 (the Pi 4 binaries are built with `+crc`): Linux reports `crc32` on
   a Pi 3 — confirm.
6. The A53's clock under Onyx's pinning: 1.2 / 1.4 GHz held, or the 3 B+'s soft limit at 60 °C.
7. The VideoCore IV's power-up without Linux (the firmware's V3D domain, `ENABLE_QPU`), its clock, the QPU count
   available to the ARM.
8. The largest HDMI mode with the 32-bpp double-height framebuffer and `gpu_mem=64`.
9. The Zero 2 W's newer Wi-Fi revision (43436s): Circle's chip table.
10. USB MIDI on DWHCI without the USB FIQ: are events dropped at Koton's rates?

## 15. Sources

- The Circle fork: `circle/README.md` (the feature table, the Pi 3 / Zero 2 W columns; "USB audio streaming
  devices (RPi 4 only)"), `circle/Rules.mk:100-123`, `circle/include/circle/memorymap64.h`, `circle/include/circle/bcm2835.h`,
  `circle/lib/memory64.cpp`, `circle/lib/translationtable64.cpp`, `circle/lib/interrupt.cpp`, `circle/lib/multicore.cpp`,
  `circle/lib/usb/Makefile`, `circle/lib/sound/`, `circle/addon/wlan/`, `circle/addon/SDCard/` (`sdhost.cpp`,
  `emmc.cpp`), `circle/addon/vc4/`, `circle/boot/Makefile`.
- Onyx: [`PI5-PORT.md`](PI5-PORT.md), docs/02 §3 (memory) and §15 (the V3D), docs/05 (the fork's patches),
  `HANDOFF.md` (the 1 GB Pi 4's memory, gcemu's timings), [`08-WEBKIT-PORT.md`](08-WEBKIT-PORT.md) (Jet's size).
- VideoCore IV: Broadcom, *VideoCore IV 3D Architecture Reference Guide* (2013, public); Mesa
  `src/gallium/drivers/vc4/` (`vc4_packet.h`, `vc4_qpu_defines.h`, `vc4_qpu.c`, `vc4_tiling.c`, `vc4_draw.c`),
  `src/broadcom/cle/v3d_packet_v21.xml`; Linux `drivers/gpu/drm/vc4/` (`vc4_v3d.c`, `vc4_regs.h`, `vc4_render_cl.c`,
  `vc4_validate.c`); py-videocore (<https://github.com/nineties/py-videocore>), vc4asm
  (<http://maazl.de/project/vc4asm/doc/>).
- Raspberry Pi: <https://www.raspberrypi.com/documentation/computers/processors.html> (BCM2837, BCM2837B0,
  BCM2710A1), <https://www.raspberrypi.com/documentation/computers/config_txt.html> (`[pi3]`, `[pi02]`, `gpu_mem`,
  `arm_64bit`).
