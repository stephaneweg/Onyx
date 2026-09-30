# Onyx on the Raspberry Pi 5 — the porting plan, A to Z

> **Status: a plan, nothing done yet (written 2026-09-30).** It comes from a read-only audit of
> the tree at `fb2b27a1` (the Circle fork: `Step51` + 24 Onyx commits) and of the public sources
> (Linux `drm/v3d`, the Raspberry Pi device trees, Mesa's `src/broadcom`). Line numbers drift:
> search for the symbol when a reference is off. Items marked **(verify)** could not be settled
> without a Pi 5 on the desk.
>
> **Decision already taken:** Onyx may ship **two binary distributions**, one for the Pi 4
> (`kernel8-rpi4.img`) and one for the Pi 5 (`kernel_2712.img`), each with its own apps build if
> needed. The kapi ABI stays append-only *within* each distribution.

## Contents

1. [The Pi 5 in one page](#1-the-pi-5-in-one-page)
2. [Verdict: what breaks, what carries over](#2-verdict-what-breaks-what-carries-over)
3. [Phase 0 — prerequisites and decisions](#3-phase-0--prerequisites-and-decisions)
4. [Phase 1 — build and SD card](#4-phase-1--build-and-sd-card)
5. [Phase 2 — the four boot blockers](#5-phase-2--the-four-boot-blockers)
6. [Phase 3 — first boot to the desktop](#6-phase-3--first-boot-to-the-desktop)
7. [Phase 4 — sound](#7-phase-4--sound)
8. [Phase 5 — network](#8-phase-5--network)
9. [Phase 6 — the GPU: V3D 4.2 → 7.1, shaders included](#9-phase-6--the-gpu-v3d-42--71-shaders-included)
10. [Phase 7 — taking advantage of the Pi 5](#10-phase-7--taking-advantage-of-the-pi-5)
11. [kapi additions (append-only)](#11-kapi-additions-append-only)
12. [Text, docs and tools to update](#12-text-docs-and-tools-to-update)
13. [Test plan](#13-test-plan)
14. [Open questions (verify on hardware)](#14-open-questions-verify-on-hardware)
15. [Sources](#15-sources)

---

## 1. The Pi 5 in one page

| | Raspberry Pi 4 (today) | Raspberry Pi 5 |
|---|---|---|
| SoC | BCM2711 | BCM2712 + **RP1** south bridge on PCIe (GPIO, USB, Ethernet, UARTs, I2C/SPI, PWM, I2S) |
| CPU | 4× Cortex-A72 (ARMv8.0) @ 1.5 GHz | 4× **Cortex-A76 (ARMv8.2)** @ 2.4 GHz, 512 KB L2 / core, 2 MB L3 |
| ISA extras | CRC32 | + **LSE atomics, RCpc, fp16, dotprod, AES/PMULL/SHA1/SHA2**, PAN, UAO. No SVE. |
| MPIDR | core number in Aff0 | **MT = 1, core number in Aff1** (bits 15:8) |
| GPU | V3D **4.2** (VideoCore VI), 8 QPUs @ ~500 MHz | V3D **7.1** (VideoCore VII), 12 QPUs @ ~800 MHz (verify), no accumulators |
| RAM | 1–8 GB | 2, 4, 8, 16 GB |
| Peripherals | `0xFE00_0000` (`ARM_IO_BASE`) | `0x10_7C00_0000`; AXI at `0x10_0000_0000`; RP1 at `0x1F_0000_0000` |
| Interrupts | GIC-400, IRQ + FIQ | GIC-400, **IRQ only** (no Circle armstub → no FIQ) |
| Core start | spin table (armstub) | **PSCI `CPU_ON`** via the firmware's BL31 |
| Audio | 3.5 mm jack (PWM) | **no jack**: HDMI, USB, I2S HAT, or PWM on GPIO12/13 with an external filter |
| Ethernet | BCM54213 (not supported by Circle) | **RP1 MACB/GEM, supported by Circle** |
| Extras | — | **RTC** (in the PMIC, battery optional), **power button**, 4-pin **fan** header, **PCIe NVMe**, 3-pin **debug UART**, dual 4Kp60 HDMI |
| Boot files | `start4.elf`, `fixup4.dat`, armstub, `bcm2711-*.dtb` | firmware in **EEPROM**; the card needs only the kernel, `config.txt`, the **`bcm2712*.dtb`** and `overlays/bcm2712d0.dtbo` |

**Circle already supports the Pi 5** (`circle/README.md` §feature table, "Tested" with the C1 and
D0 steppings): build, MMU, GIC, PSCI multicore, timer, CPU clock/temperature/fan, the firmware
framebuffer ("limited"), xHCI USB through RP1, SD card, WLAN, MACB Ethernet, HDMI / I2S / USB /
RP1-PWM sound, RNG200, watchdog, NVMe (experimental), the firmware RTC (addon). So the work is
almost entirely **on the Onyx side**.

## 2. Verdict: what breaks, what carries over

### Blockers (the Pi 5 kernel will not boot, or will corrupt memory)

| # | Problem | Where | Phase |
|---|---|---|---|
| B1 | **Core number read from Aff0**: `mpidr & 3` is 0 on every A76 core → the four per-core schedulers collide, the app cores and the network core misbehave, user spinlocks call `yield` from an app core | `kernel/compat/circle/sched/scheduler.h:207-212`, `user/kapi.h:462-468` (`kapi__core`, used by `kapi_lock` ~`:476-484`), `user/libc/onyx_syscalls.c:107-112` (`on_app_core`) | 2 |
| B2 | **High RAM registered twice on an 8 GB Pi 5**: `SetupHighMemAbove4G`'s fallback maps [4 GB, 8 GB) again although seg0 already covers [1 GB, 8 GB) → two allocators hand out the same frames | `circle/lib/memory64.cpp:181-296` (the Onyx patch, commit `24842f27`) | 2 |
| B3 | **16 GB Pi 5: RAM above 8 GB lands in the user VA window** [8 GB, 60 GB) (code 8 GB, heap 10 GB, canvas 12 GB, kapi table 14 GB, stack top 16 GB) | `kernel/include/kern/layout.h:40-106`, `kapi_abi.h:19` | 2 |
| B4 | **The V3D driver pokes Pi 4 addresses** (`ARM_IO_BASE + 0xC00000` is not the V3D on the Pi 5; `PM_GRAFX`/ASB do not exist) and connects `GIC_SPI(74)` → a fault or an assert the first time an app calls a GPU kapi | `kernel/sys/v3d.cpp:43-90, 196-265, 300-307` | 2 (guard), 6 (port) |

### Breaks (a feature fails)

| Problem | Where | Phase |
|---|---|---|
| No sound: PWM goes to GPIO12/13 only, and RP1-PWM **stops** when the device gets a short chunk (Onyx's low-latency trick) | `kernel/sys/sound.cpp:13-20, 323-371, 397-420`; `circle/lib/sound/pwmsoundbasedevice-rp1.cpp:320-325` | 4 |
| `kapi_wait_word` refuses words at physical ≥ 4 GB (`KERNEL_IDENTITY_END`), and app frames come from [1 GB, 8 GB) on the Pi 5 | `kernel/sys/thread.cpp:575-592` | 2 |
| Crash record not kept across a reboot: the crash area is taken only from RAM above `MEM_HIGHMEM_END` (8 GB on the Pi 5) | `circle/lib/memory64.cpp:247-253, 276-280`, `kernel/sys/crashlog.cpp:283` | 3 |
| Wi-Fi: the Pi 5 needs **other firmware file names**, and the **device tree** (the MAC is read from `/axi/mmc@1100000/wifi@1`) | `circle/addon/wlan/ether4330.c:300-306`, `bcm4343.cpp:61-79`, `sdcard/firmware/` | 1, 5 |
| 16-bpp screen unless `framebuffer_depth=32` + `framebuffer_ignore_alpha=1` (Onyx writes alpha 0) | `sdcard/config.txt:47-50` | 1 |
| GPU apps: `gpu_program` / `gpu_render2/3` take **V3D 4.2 QPU binaries** from apps | `kapi_abi.h:377-398, 941-962`, `user/v3d/*`, `user/Apps/gcemu/gxv3d.h` | 6 |

### Carries over unchanged (board-independent)

Trap frame, vectors, FP/NEON save, the syscall path and the preemption trampoline
(`kernel/arch/aarch64/`), the per-process address spaces (64 KB granule, L2/L3 math, 8-bit ASIDs,
TLBI, teardown — only the VA *constants* depend on the RAM), the scheduler logic, threads, futex
tick, IPC, VFS, streams, the ELF loader, the window manager and wtk, the kapi table mechanism, USB
HID / gamepads / MIDI, TCP/IP, NTP, FatFs and the Onyx FatFs patches, the FTP / telnet / VNC / rdpd
daemons, BASIC, NetSurf, Writer, Ledger, the emulators' CPU cores **and the GameCube JIT** (plain
ARMv8.0 code; it flushes with `dc cvau`/`ic ivau` using `CTR_EL0` line sizes). **Every timing path
reads `CNTFRQ_EL0`** (no 54 MHz or 1.5 GHz constant in code). **A72-tuned binaries run unchanged
on the A76.** Onyx uses no FIQ (it only masks it). The panic screen, ACT LED, watchdog and
reboot go through Circle and work on the Pi 5.

## 3. Phase 0 — prerequisites and decisions

1. **Hardware:** a Pi 5 (8 GB is the easy target; 4 GB also fine), the **official 27 W supply**,
   the **Active Cooler** (Onyx pins the CPU at max clock — without cooling the firmware throttles),
   a USB-to-3.3 V UART adapter with a JST-SH 3-pin plug for the **debug UART** (the kernel log goes
   there by default, `SERIAL_DEVICE_DEFAULT` = 10), a spare SD card.
2. **Decide the RAM policy** (drives Phase 2):
   - **(A) Cap Onyx at 8 GB on the Pi 5** — no layout change, the same user VA and kapi table
     address as the Pi 4, so the same app binaries (after the B1 fix) run on both. **Recommended
     first.** A 16 GB board then runs with 8 GB.
   - **(B) A Pi 5 layout** — move the user window above 16 GB (under Circle's `T0SZ_128GB`: free
     VA ≈ [16 GB, 64 GB), AXI I/O starts at 64 GB). `USER_VA_BASE`, `KAPI_TABLE_VA` and `user.ld`
     change → **apps are rebuilt for the Pi 5 distribution**. Allowed by the two-distributions
     decision; do it later, if 16 GB is ever needed.
3. **Decide the userland build**:
   - **Phase 1-6: one userland** built as today (`-mcpu=cortex-a72`, ARMv8.0) — runs on both.
     Only change: the B1 core-number fix, which must work on both boards.
   - **Phase 7 (optional): a Pi-5-tuned userland** (`-mcpu=cortex-a76`: LSE atomics, crypto,
     dotprod, fp16). Never ship those binaries to a Pi 4 (they fault on ARMv8.0).
4. **Keep two Circle build trees** (the Circle libs are per-`RASPPI`): e.g. a second worktree of
   the fork (`circle-rpi5/`) configured once, or a `make clean` + `./configure` switch script.
   Two trees avoid rebuilding everything at each switch.

## 4. Phase 1 — build and SD card

### 4.1 Circle for the Pi 5

- `./configure -r 5 -p aarch64-none-elf- -d DEPTH=32 -f`, then `make` in `lib/`, `lib/usb`,
  `lib/input`, `lib/fs`, `lib/net`, `lib/sched`, `lib/sound`, `addon/SDCard`, `addon/fatfs`,
  `addon/wlan` (and later `addon/rtc`, `addon/nvme`).
- `circle/Rules.mk:111-118` then gives `-mcpu=cortex-a76` and **`TARGET=kernel_2712`**;
  `LOADADDR` stays `0x80000`, `KERNEL_MAX_SIZE` 2 MB (the Onyx kernel is ~1.06 MB today).
- Built only for the Pi 5: `bcmpciehostbridge`, `southbridge`, `dmachannel-rp1`, `gpiomanager2712`,
  `gpiopin2712`, `gpioclock-rp1`, `pwmoutput-rp1`, `i2cmaster-rp1`, `spimaster-rp1`, `macb`, the
  xHCI `usbsubsystem`. **Not built** on the Pi 5: `CUserTimer`, FIQ GPIO, I2C slave, SMI, AUX SPI,
  `bcm54213`, `dmasoundbuffers`, the USB gadgets, VCHIQ/`vc4` — check the Onyx kernel links none.

### 4.2 The Onyx kernel

- `kernel/Makefile` already uses `$(TARGET)` for `sizecheck` (`:85-93`) and `stage` (`:116-141`),
  so it produces `kernel_2712.img`. Add a **board switch** (e.g. `make BOARD=pi5`) that points at
  the Pi 5 Circle tree and a `sdcard-pi5/` staging directory (or a `stage-pi5` target).
- `sys/v3d.o` is compiled unconditionally (`kernel/Makefile:38, 73`): keep it, but Phase 2 makes it
  refuse to start on the Pi 5 until Phase 6.
- Hard-coded image names to make `$(TARGET)`-driven: `kernel/sys/crashlog.cpp:420` and
  `kernel/kernel.cpp:503` (the addr2line hints), `tools/tests/fs/fstest.cpp:64, 79`,
  `tools/tests/run_gui_test.sh:9` (`-DRASPPI=4`).

### 4.3 The SD card for the Pi 5 (`sdcard-pi5/` or a shared card)

| File | Source | Note |
|---|---|---|
| `kernel_2712.img` | the Pi 5 build | `sdcard/config.txt:47-48` already has `[pi5] kernel=kernel_2712.img` |
| `bcm2712-rpi-5-b.dtb`, `bcm2712d0-rpi-5-b.dtb`, `bcm2712-rpi-500.dtb` (+ CM5 dtbs if wanted) | `circle/boot/` (its Makefile downloads them) | **the DT is mandatory** on the Pi 5 (WLAN MAC, `CMachineInfo`, DMA channel mask) |
| `overlays/bcm2712d0.dtbo` | `circle/boot/` | for the D0 stepping |
| `config.txt` | extend `sdcard/config.txt` | in `[pi5]`: **`framebuffer_depth=32`**, **`framebuffer_ignore_alpha=1`**, `arm_64bit=1`, `kernel_address=0x80000`; re-check `hdmi_force_hotplug`, `enable_uart` (harmless), `max_framebuffers` (only set under `[pi4]` in Circle's template) |
| `firmware/brcmfmac43455-sdio.raspberrypi,5-model-b.{bin,txt,clm_blob}` | `circle/addon/wlan/firmware/Makefile` target `firmware5` (cypress `cyfmac43455`) | the Pi 4 names in `sdcard/firmware/` are not picked up on the Pi 5 |
| no `start*.elf`, `fixup*.dat`, `bootcode.bin`, armstub | — | the Pi 5 firmware and BL31 are in EEPROM |

A single card can boot both boards: the `[pi4]` / `[pi5]` sections of `config.txt` pick the kernel,
the two sets of DTBs coexist, the apps are shared (Phase 1-6 userland). The apps directory is the
same; only the kernel differs.

**EEPROM (optional):** `POWER_OFF_ON_HALT=1` makes a halt cut the 3.3 V rail (~0.01 W instead of
~1.2 W) — see Phase 7 for a clean power-off.

## 5. Phase 2 — the four boot blockers

### 5.1 B1 — the core number (do this first; it also prepares EL0 mode)

Circle already does it right (`circle/include/circle/multicore.h:71-73`: `#if RASPPI >= 5
nMPIDR >>= 8`). Onyx has three copies of its own:

1. **Kernel** — `kernel/compat/circle/sched/scheduler.h:207-212` (`ThisCore()`, used by `IsActive()`
   `:204`, `scheduler.cpp:113` `m_nCore`, `:1048` `Get()`, `net.cpp:678`): add the `>> 8` under
   `RASPPI >= 5`, or simply call `CMultiCoreSupport::ThisCore ()`.
2. **User side** — `user/kapi.h:462-468` (`kapi__core`) and `user/libc/onyx_syscalls.c:107-112`
   (`on_app_core`, which tests `(m & 0xFF) != 0`). These are **inline in every app**, so the fix
   must work on both boards with the same binary. Two options:
   - **Board-independent decode:** `(m >> ((m >> 24) & 1 ? 8 : 0)) & 3` (MPIDR.MT, bit 24,
     says which affinity level holds the core number). Cheap, no kernel change.
   - **Better — the kernel publishes the core number in `TPIDRRO_EL0`** at each switch (step 0 of
     `docs/EL0-PROTECTED-MODE.md` §4.8, which needs it anyway because `MPIDR_EL1` is not readable
     at EL0). Apps read `mrs x0, tpidrro_el0`. Board-independent, EL0-ready.
3. **Rebuild every app** afterwards (the helpers are inline). Apps built before the fix still work
   on the Pi 4 but not on the Pi 5 — one more reason for a Pi 5 distribution.

### 5.2 B2 / B3 — high memory and the user window

In the fork's `circle/lib/memory64.cpp` (Onyx commit `24842f27`, `docs/05-CIRCLE-CHANGES.md`):

- `SetupHighMem` (`:130-178`) already makes seg0 = [1 GB, `MEM_HIGHMEM_END`] = [1 GB, 8 GB) on the
  Pi 5. **`SetupHighMemAbove4G` (`:181-296`) must be skipped when `MEM_HIGHMEM_END` ≥ 4 GB**, or
  at least its fallback (B, `:261-283`) must not register [4 GB, 8 GB) again. Rewrite the
  "3 GB" comments (`:159-163, 209, 220-223`) in terms of `MEM_HIGHMEM_END`.
- The DT `/memory` parser assumes 16-byte entries (`:213`, `nLen % 16`), while both shipped DTBs
  declare root `#address-cells = 2`, `#size-cells = 1` (12-byte entries). It works on the Pi 4
  today, so the firmware probably patches the cells at boot **(verify)** — but read
  `#address-cells` / `#size-cells` from the root node instead of assuming.
- **Policy (A), 8 GB cap:** nothing else to do: Circle's Pi 5 `CTranslationTable` maps RAM only
  below 8 GB (`circle/lib/translationtable64.cpp:66-75`), exactly up to `USER_VA_BASE` (8 GB). Set
  a guard so RAM above 8 GB is never mapped or handed out.
- **Policy (B), 16 GB:** map [8 GB, 16 GB) for the kernel, move the user window above it
  (`layout.h:50-106`: `USER_VA_BASE`, heap, canvas, wallpaper, surfaces, `KAPI_TABLE_VA`, code
  arena, fullscreen canvas, stack), `user/user.ld`, `user/kapi.h:16`, keep everything below the
  AXI I/O at 64 GB. Apps rebuilt for the Pi 5.
- `layout.h:41-52`: `KERNEL_IDENTITY_END` (4 GB) and `USER_VA_CEILING` ("T0SZ_64GB hard limit on
  RPi 4") become per-board (`RASPPI`). On the Pi 5, Circle uses `IPS_1TB` + `T0SZ_128GB`
  (`memory64.cpp:328-353`) and a 256-entry L2 table; the per-process L2 copy
  (`addrspace.cpp:96-100`) copies it whole and needs no change.
- **`kapi_wait_word`** (`kernel/sys/thread.cpp:586`): compare against the real identity top
  (8 GB on the Pi 5, a runtime value) instead of `KERNEL_IDENTITY_END`. (The 8 GB Pi 4 has the same
  latent limit for its reclaimed frames above 4 GB — fix it for both.)
- `kernel/mm/addrspace.cpp:394-399` comment: the high zone is 1-8 GB on the Pi 5.

### 5.3 B4 — fence the V3D until Phase 6

In `kernel/sys/v3d.cpp`: under `RASPPI >= 5`, make the power-up (`PowerOn` / `Up`) return
"no GPU" **before** touching any register or connecting the IRQ. `gpu_info` then returns 0 and the
apps take their software paths (teapot, BASIC 3D, planets3d have one; **gcemu and n64emu need
`gpu_info` checked and a clear message** — verify they degrade instead of hanging).

## 6. Phase 3 — first boot to the desktop

Aim: the desktop on HDMI, USB keyboard and mouse, the SD card, the terminal, no sound, no network.

1. **Serial log first:** the debug UART (JST) gets the kernel log automatically
   (`m_Serial.Initialize (115200)`, `kernel/kernel.cpp:1637`). Boot with a minimal `cmdline.txt`
   (`netcore=0`, no `sdhs`) to reduce the unknowns.
2. **Framebuffer:** `CBcmFrameBuffer` has no Pi-5 branch; the limits are in `C2DGraphics` (no VSync,
   no double buffer on the Pi 5, `2dgraphics.cpp:140-184, 542-629`) — Onyx already builds it with
   VSync off (`kernel/kernel.cpp:1248`) and draws its own cursor (no hardware cursor on the Pi 5).
   Check:
   - 32 bpp with `framebuffer_ignore_alpha=1` (else a black or transparent screen);
   - the framebuffer address is masked with `& 0x3FFFFFFF` (`bcmframebuffer.cpp:176`): fine if the
     firmware puts it below 1 GB;
   - **the compositor's 2D DMA** (`kernel/kernel.cpp:299-323`, the Onyx `UpdateDisplayStart/Poll`
     patches): on the Pi 5 the "normal" DMA channel is a **DMA4** channel
     (`machineinfo.h:146-149`, channels 6-11 filtered by the DT mask), so the Onyx additions to
     `circle/lib/dma4channel.cpp` (`:363ff, 544ff`) are the ones in use. Untested → if the screen
     tears or hangs, boot with **`dispdma=0`** (synchronous copy) and debug later;
   - **the runtime resolution change** (`ScreenResizeRequest`, `kernel/kernel.cpp:332-357, 429-449`,
     the `displayconf` app): re-allocating the framebuffer on the Pi 5 is untested (Circle says the
     Pi 5 mailbox implements a "subset of functions"). If it fails, pin the resolution in
     `config.txt` / `cmdline.txt` and grey out the choice in `displayconf` on the Pi 5.
3. **USB:** `CUSBHCIDevice` is `CUSBSubSystem` on the Pi 5 (RP1 + two xHCs), same constructor
   (`kernel/kernel.cpp:1250`). Nothing to change; hub support is USB 2.0 only.
4. **SD card:** `CEMMCDevice` at `0x10_00FF_F000`, 200 MHz base clock, PIO. The Onyx High Speed patch
   (`circle/addon/SDCard/emmc.cpp:2176-2220`, `CONTROL0` bit 2) is generic SDHCI (verify `sdhs=1`).
5. **SMP:** after B1, the per-core roles hold (core 0 kernel + processes + compositor, core 1 sound
   and the crash watchdog, cores 2-3 app cores, core 3 network with `netcore=1`). Circle starts the
   secondaries by PSCI.
6. **Watchdog and crash log:** `crashlog.cpp:555-560` writes `ARM_PM_WDOG` / `ARM_PM_RSTC`
   directly — valid at the Pi 5's `ARM_PM_BASE`. The **crash area** needs RAM that survives a
   reboot and is outside the allocators: on the Pi 5 (≤ 8 GB) take a fixed region at the top of
   seg0 (e.g. the last 64 KB below 8 GB, removed from seg0) instead of "above `MEM_HIGHMEM_END`".
   Whether RAM survives a watchdog reset through the EEPROM bootloader is **(verify)**.
7. **CPU clock and heat:** `CCPUThrottle (CPUSpeedMaximum)` gives 2.4 GHz. **Add `gpiofanpin=45`**
   to the Pi 5 `cmdline.txt` (active-low, handled by `circle/lib/cputhrottle.cpp:53-68`) and call
   **`m_CPUThrottle.Update ()` once a second** (the GUI watchdog that runs `CrashLogPower`) so the
   fan and the soft temperature limit act. Today Onyx never calls `Update()`.

## 7. Phase 4 — sound

The Pi 5 has **no 3.5 mm jack**. `COnyxSoundDevice : CPWMSoundBaseDevice` (`kernel/sys/sound.cpp:323`)
becomes the RP1 PWM driver on GPIO12/13 — silent on a stock board.

**Target: HDMI audio** (`CHDMISoundBaseDevice`, supported on the Pi 5, HDMI0 only):

- Constructor `(pInterrupt, nSampleRate = 48000, nChunkSize = 384 * 10)`; the chunk is a multiple
  of **384**, samples are **IEC958 frames** (`ConvertIEC958Sample`) — a different `GetChunk` format
  than PWM.
- Onyx's mixer stays: core 1 still renders chunks ahead into the ring; only the device class and
  the sample packing change. Keep `SND_RATE` 44100 by resampling in the mixer, or move to 48000
  (the mixer's voices and the PCM ring are rate-parametric — check `sound.cpp:43-47` and the apps
  that assume 44100: emulators `emucore.h`, Koton `SOUND_RATE`).
- **Low latency:** the Onyx "return a shorter chunk" trick (`sound.cpp:13-20, 331-334, 369`)
  must go on the Pi 5 — the RP1 PWM driver treats a short return as end-of-stream and stops
  (`pwmsoundbasedevice-rp1.cpp:320-325`); do not rely on it for HDMI either. Implement
  `kapi_sound_config` by **choosing the device chunk size** at (re)creation instead. Measure the
  HDMI minimum latency (the firmware/HDMI pipeline adds some); Koton's 256 × 2 target
  (`docs/daw/PERFORMANCE.md` §1.2) may need 384 × 2.
- **Alternatives** (a `soundout=` option in `cmdline.txt` or `system.ini`, chosen in `soundconf`):
  **USB audio** (`CUSBSoundBaseDevice`, 48 kHz — best quality, a USB DAC), **I2S HAT**
  (`CI2SSoundBaseDevice`, RP1 version), **PWM on GPIO12/13** (with an RC filter, for the curious).
- Texts to update: `sound.h:3, 11`, `kapi_abi.h:819` ("3.5 mm jack by PWM"), `user/kapi.h:303`,
  `sound.cpp:410, 415`, `user/Apps/soundconf/main.cpp:94` ("headphone jack (3.5 mm)"),
  `docs/daw/PERFORMANCE.md:12, 106`.

## 8. Phase 5 — network

1. **Wi-Fi** (the Pi 4 path, `CBcm4343Device`, `kernel/sys/net.cpp:352, 430`): works once the Pi 5
   firmware files and the DT are on the card (Phase 1). The SDIO controller is PIO-only on the
   Pi 5. **Known issue:** Circle notes that "the WLAN connection is delicate on the Raspberry Pi 5
   when a HDMI display is used at the same time" (`circle/doc/issues.txt:51`) — a desktop always
   has HDMI, so expect trouble and prefer:
2. **Gigabit Ethernet (new, recommended on the Pi 5):** Circle's `CMACBDevice` (RP1 MACB/GEM) is
   created automatically by `CNetDeviceLayer::Initialize` (`circle/lib/net/netdevlayer.cpp:56`).
   In `kernel/sys/net.cpp` / `kernel/kernel.cpp:1149-1252` add a wired path: bring up
   `CNetSubSystem` on `NetDeviceTypeEthernet` when a link is up, fall back to WLAN otherwise; a
   `net=` option (`auto` / `eth` / `wlan`). Route the MACB IRQ to the network core (core 3) with
   `netcore=1`. Gains: FTP deploy (7 MB ≈ 45 s over Wi-Fi today, `HANDOFF.md:725`), VNC/rdpd,
   OnyxRemote, NetSurf.
3. **Apps:** `wifimenu` and `wpaconf` assume Wi-Fi (`kapi_wlan_reconnect`, `user/kapi.h:373`):
   show an "Ethernet connected" state; this needs a small kapi (§11).
4. The FTP / telnet / VNC / rdpd daemons are pure kapi: no change.

## 9. Phase 6 — the GPU: V3D 4.2 → 7.1, shaders included

### 9.1 What Onyx has today (V3D 4.2)

| Piece | File | Lines |
|---|---|---|
| Kernel driver (power, submission, IRQ, textures, clipping glue) | `kernel/sys/v3d.cpp` | 1623 |
| Control-list packets (from Mesa `v3d_packet_v42.xml`) | `kernel/include/kern/v3d_cl.h` | 935 |
| CPU triangle clipper (near plane, 4× guard band) | `kernel/include/kern/v3d_clip.h` | 148 |
| Texture layouts LT / UBLINEAR / UIF | `kernel/include/kern/v3d_tiling.h` | 59 |
| Kernel shaders (hand-written QPU asm: `VS_CLIP`, `CS_CLIP`, `FS_COLOR`, `FS_TEX`, `FS_COLOR_AT`, `FS_TEX_AT`) | `kernel/sys/v3d_shaders.qasm` → `.inc` | 288 → 267 |
| Run-time QPU builder (fills Mesa `v3d_qpu_instr`, `devinfo.ver = 42`) | `user/v3d/qpu.h` / `qpu.cpp` | 99 / 109 |
| Stock shaders (passVS / passCS / flatFS / varyFS / texFS) | `user/v3d/shaders.cpp` | 144 |
| GameCube TEV → QPU generator | `user/v3d/gxtev.cpp` (+ `.h`, `gxtev_ref.h`) | 728 |
| Host assembler / CLI, QPU simulator | `tools/qpu/qpulib.c`, `qpuasm.c`, `qpusim.cpp` | 447, 69, 435 |
| Vendored Mesa packer (MIT) | `tools/qpu/mesa/broadcom/qpu/*` | 4883 |
| Users | `teapot` (v52), `gpudemo` (v53), BASIC 3D / `planets3d`, `n64emu` (`gpu_render`), `gcemu` (v61-v63: TEV shaders, `gpu_vbuf`, `gpu_render3`), `bin/v3dprog` | |

Design (`docs/02-KERNEL-INTERNALS.md` §15): **no V3D MMU** (physical = bus addresses, buffers from
`HEAP_LOW` below 1 GB), bin → render one frame at a time, one 64×64-tile RGBA8 render target,
triangle lists only, depth / cull / blend / scissor / alpha test, RGBA8 textures (nearest/linear,
no mipmaps), up to 8 textures a batch, no compute (CSD), no TFU. Overflow pool 16 × 256 KB, IRQ
completion with a 2 ms polling fallback and a 0.5 s timeout. The QPU code leans on the
**accumulators r0-r5** (the `.qasm` uses r0 162×, r1 120×, r5 69×; `gxtev` uses r0-r3 as
temporaries, r5 via `ldunif`, r4 for `recip`, W in rf0).

**The biggest lever:** the vendored Mesa packer **already contains the V3D 7.1 encoding**
(`qpu_pack.c`: 12 `ver >= 71` paths; `qpu_instr.c:40-43, 1028-1036`). The assembler, builder and
disassembler need a version switch, not a new encoder.

### 9.2 What changes in V3D 7.1

**SoC integration** (RPi `bcm2712-ds.dtsi`, `compatible = "brcm,2712-v3d"`):

| | Pi 4 (V3D 4.2) | Pi 5 (V3D 7.1) |
|---|---|---|
| hub registers | `0xFEC0_0000` | **`0x10_0200_0000`** (0x4000) |
| core 0 registers | hub + 0x4000 | **hub + 0x8000** = `0x10_0200_8000` (0x6000) |
| SMS block | — | **`0x10_0203_0800`** (0x700), new |
| IRQ | `GIC_SPI(74)`, shared | **two lines: SPI 250 = hub, SPI 249 = core** |
| power / reset | mailbox domain 10 + clock 5, `PM_GRAFX` (0x10C) `V3DRSTN`, **ASB** master/slave bridges | mailbox clock 5 (same id); `PM_GRAFX_2712` = **`0x304`** in the PM block at `0x10_7D20_0000` (same `V3DRSTN` = bit 6); **no ASB**; then **SMS resume** (`TEE_CS` 0x400 ← `CLEAR_POWER_OFF` bit 29, wait `STATE` = IDLE) and **SMS reset** (`REE_CS` 0x000 ← state 4, wait) |
| bus addressing | physical = bus | identity `dma-ranges` on the AXI bus: physical = bus still holds |
| `HUB_IDENT1` TVER | 4 | **7** (REV 1; "7.1.6" on C1, "7.1.10" on D0) |

**Registers Onyx uses — unchanged:** `HUB_IDENT0-3`, the CLE `CT0/CT1 QBA/QEA/QMA/QMS/QTS`,
`BFC`/`RFC`, `PTB_BPOA/BPOS`, `CTL_INT_STS/CLR/MSK_*` (FRDONE/FLDONE/OUTOMEM bits 0/1/2), `SLCACTL`,
`L2TCACTL/L2TFLSTA/L2TFLEND`, the MMU registers. **Moved (unused today, needed for compute/TFU):**
TFU 0x400 → 0x700 (+ IOC 0x71c), GMP 0x800 → 0x600 (hub level), CSD `QUEUED_CFG0-6` 0x904 → 0x930
(+ `CFG7` = 0), `CURRENT_CFG` 0x920 → 0x958, `INT_CSDDONE` bit 7 → 6, `INT_PCTR` 6 → 5,
`MMUC_CONTROL_CLEAR` bit 3 → 11. On 7.1.6+ the CSD batch count is not "minus one".

**Control-list packets** (Mesa's unified `v3d_packet.xml`, `min_ver`/`max_ver`):

| Packet | Change in 7.1 | Onyx site |
|---|---|---|
| `Tile Binning Mode Cfg` (120) | "Maximum BPP" and "Number of RTs" gone; **explicit Log2 tile width/height** (64 → 3/3) | `Draw`, `Render`, `Render2` |
| `Tile Rendering Mode Cfg (Common)` (121/0) | explicit Log2 tile size; **Early Depth/Stencil Clear** bit | `BuildRCL` `:437-484` |
| `(Color)` sub-packet | **removed** | `:443` |
| `(Clear Colors Part1/2/3)` | replaced by **`(Render Target Part1/2/3)`**: clear colour, internal type + clamp (RGBA8 = 8), internal BPP, **stride in 128-bit units − 1** (64×64 RGBA8 → 31), **base in the tile buffer** (0), RT number | `BuildRCL` |
| `(ZS Clear Values)` | re-laid out | `BuildRCL` |
| `Clear Tile Buffers` (25, Z/RT bits) | **`Clear Render Targets`** (25, no fields); Z/S via Early Depth/Stencil Clear | `:458, :476` |
| `Clipper XY Scaling` (110) | units **1/256 → 1/64 pixel** — and the same factor in the **VS viewport uniforms** (only uniform values change) | `v3d.cpp:537-539, 598, 746, 1343`, `shaders.cpp:139-140`, gcemu / n64emu if they compute scales |
| `Cfg Bits` (96) | "Early Z enable/updates" gone; Z clipping mode, Z clamp, depth bounds added | `Draw`/`Render*` |
| `Blend Cfg` (84) | RT mask 4 → 8 bits | idem |
| **`GL Shader State Record`** | **"Address of default attribute values" removed → every later field moves 32 bits earlier**; "separate VPM" bits gone; "Never defer FEP depth writes" added | `:551, :823, :1413` |
| unchanged | `Vertex Array Prims`, `GL Shader State`, attribute record, supertiles, `Tile List Initial Block Size`, `Viewport Offset`, `Clip Window`, `Sample State`, `Sampler State`, `Store/Load Tile Buffer General` (RT numbers 4-7 added), the GFXH-1742 double dummy store, `SET_INSTANCEID 0` | |
| `Texture Shader State` | **every field Onyx writes keeps its bit position** (width 58, height 72, depth 86, type 100, swizzles 108-117, extended 107, UB_PAD 128, XOR 132, strictly-UIF 134); new **R/B swap bit at 32** (array stride → 33) | `:626-709` |

The 64×64 RGBA8 + 32-bit Z tile still fits the 7.1 tile buffer (16 KB colour + 16 KB depth).

**QPU ISA** (Mesa `qpu_instr.h`, `qpu_pack.c`, `qpu_schedule.c`, `nir_to_vir.c`):

- **No accumulators** (`has_accumulators = ver < 71`): r0-r5 are gone (the r5/r5rep magic
  addresses become `quad`/`rep`); every operand is a register-file register.
- **rf0 is the implicit destination** of `ldvary`, `ldunif`, `ldunifa` → keep rf0 free; `ldvary`'s
  C coefficient lands in rf0 on the next instruction.
- **Four read addresses** `raddr_a/b/c/d` (one per ALU operand), each may be a small immediate
  (one per ALU) — the 4.2 "two rf reads per instruction" limit is gone.
- **SFU as ALU ops** (`recip rfN, rfM`, result in any rf) instead of the magic write + r4 three
  instructions later. Whether 7.1 still accepts magic-write SFU: assume **not**.
- `mov`/`fmov` on either ALU; new packing ops.
- **Fragment payload moves: W is rf3** (rf0 on 4.2); centroid W rf1, Z rf2 unchanged. Compute
  payload rf3/rf2.
- Scheduling: `ldvary` allowed in the thrsw delay slots except the 2nd; thread end must not write
  the rf through the ALUs; rf2/rf3 may be clobbered in the last thrsw's delay slots; the "rf0
  flops" rule. **Take the exact latencies from Mesa's `qpu_schedule.c` / `qpu_validate.c` (v71)
  before writing any shader.**
- TMU magic writes (`tmus/tmut/tmud/tmua`, `wrtmuc`, `ldtmu`) unchanged.
- **Not binary-compatible, but semi-mechanical at the instruction-model level:** map each
  accumulator to a reserved rf register, fix the implicit writes (r5 → rf0, r4 → an explicit SFU
  destination), move W to rf3, keep rf0 free, re-check timing.

### 9.3 The GPU plan, step by step (one code base, v42 and v71 paths)

1. **Version selection.** Kernel: compile-time `RASPPI` for addresses / power / IRQs, plus a
   run-time check of `HUB_IDENT1` (TVER 4 or 7) kept in `s_nVer` (42 / 71) that picks the packet
   emitters; relax the `nTver != 4` check (`v3d.cpp:234-243`). User side: apps learn the version
   at run time (§11 `gpu_version`) and pass it to Mesa's `v3d_device_info` (`qpu.cpp:19`,
   `qpulib.c:21`); `kapi_gpu_program` gets a version flag so the kernel refuses a 4.2 program on a
   7.1 GPU and vice versa.
2. **Kernel bring-up** (`v3d.cpp` `PowerOn`/`Up`): hub `0x10_0200_0000`, core0 `+0x8000`, SMS
   `0x10_0203_0800`, `PM_GRAFX_2712` `0x10_7D20_0304`, IRQ core SPI 249 (connect the core line;
   the hub line only if GMP/TFU interrupts are used). Power: mailbox clock 5 on + max rate (keep),
   `V3DRSTN` with the clock off/on around it as Linux does, **no ASB**, SMS resume then SMS reset,
   init `L2TFLSTA/END`. Map the V3D window (it is in Circle's AXI identity region,
   `translationtable64.cpp:66`, `memorymap64.h:113-120`).
3. **The V3D MMU — the first risk to clear.** Try **MMU off** first (as on the Pi 4: physical
   addresses, buffers below 1 GB). If 7.1 refuses, add an identity page table (4 KB pages,
   PTE = PFN | valid | writable, `V3D_MMU_PT_PA_BASE`, `MMU_CTL_ENABLE`), modelled on Linux
   `v3d_mmu.c` (~160 lines).
4. **Control lists:** add `*_v71` structs to `v3d_cl.h` — better **generated from Mesa's
   `v3d_packet.xml`** than hand-written (silent bit-layout mistakes are the main risk). Put the
   emitters behind `EmitBinCfg` / `EmitRcl` / `EmitShaderRecord` helpers switching on `s_nVer`, so
   `Draw` / `Render` / `Render2` share the rest. Clipper scale ×64. Reuse unchanged: the CPU clipper,
   the tiling, the texture state, the sampler, the direct-target mode, the overflow pool, the vbufs,
   program ownership. Optional: the texture R/B-swap bit instead of the CPU swap.
5. **Assembler and simulator:** `qpulib` / `qpuasm` get a `-v71` mode (`devinfo.ver = 71`, reject
   r0-r5, the v71 validator rules); `qpusim` gets 7.1 semantics (no accumulators, rf0 implicit
   writes, the payload registers) so `tools/tests/run_qpu_test.sh` covers both ISAs **before**
   touching the hardware.
6. **Kernel shaders:** a v71 copy `v3d_shaders71.qasm` → `.inc`: `ldunif` → rf0 (or `ldunifrf.rfN`),
   `recip` as an ALU op, `ldvary` C in rf0, W from rf3, r0-r3 → spare rf. Drop the v52 Mesa
   binaries on 7.1 (`gpu_draw` can use `VS_CLIP` / `FS_COLOR`).
7. **Builder and stock shaders:** in `user/v3d/qpu.h`, in v71 mode map `r0..r5` onto reserved rf
   registers (e.g. rf4-rf9); `I::sfu(op, dst, src)` emits a magic write on 4.2, an ALU op on 7.1;
   `ldunif()` → rf0; `ldvary(d)` → C in rf0. `shaders.cpp` then ports with small edits.
8. **`gxtev` (the most work):** a version-aware register budget (rf0 implicit, rf1-3 payload,
   4 pseudo-accumulators, 32 registers at 2 threads), `accBusy` / `needR5` and W = rf0 made
   version-aware; validate bit-exact against `gxtev_ref.h` in `qpusim`. If some TEV configurations
   no longer fit, fall back to 1 thread (64 registers).
9. **Docs and ABI:** `docs/02` §15, the ABI table, `docs/03` GPU chapter, `KAPI_ABI_VERSION`.

**Size (estimate, uncertain):** kernel ~400-600 lines, `v3d_cl.h` +300-400, `v3d_shaders71.qasm`
~300, `qpulib` ~200, `qpusim` ~200-300, `qpu.h`/`shaders.cpp` ~150, `gxtev` ~200-300 + tuning —
**about 2-2.5 k lines, several weeks.**

**On-device test ladder** (each step with `v3dprog` / `gpudemo`, the log of `HUB_IDENT`, a VNC
screenshot):

1. `gpu_info`: power-up, IDENT reads 7.1, SMS idle, IRQ connected.
2. **Empty frame:** an RCL-only clear (a binning list with no primitive) → a solid window. Exercises
   the tile config, RT Part1, `Clear Render Targets`, the store, FRDONE/FLDONE, the IRQs.
3. **Flat triangle** (`gpu_draw`, v71 `VS_CLIP`/`CS_CLIP`/`FS_COLOR`): the 1/64 scale, the shader
   record, `ldvary`/rf0.
4. Depth + culling + blending: `gpudemo`.
5. Textures (`FS_TEX`): the TMU path, the three layouts (the tiling test sizes).
6. `KEEP` loads, direct-target mode, scissor, write mask.
7. App shaders (v61): the `v3dprog` suite, then gcemu's TEV (Wind Waker), then n64emu.
8. Compute (new): a CSD dispatch with the v71 offsets and `CFG7` = 0 (e.g. a vector add as in
   py-videocore7).
9. Stress: 1080p frames, the overflow pool, the 0.5 s timeout path, reset recovery (PM + SMS reset).

### 9.4 What the Pi 5 GPU allows beyond the Pi 4

- **~2.4× shader throughput** (12 vs 8 QPUs, higher clock; ~77 vs ~32 GFLOPS per py-videocore7)
  and more instruction-level parallelism: gcemu's TEV fragment shaders (Wind Waker ≈ 14 ms of GPU
  time a frame on the Pi 4).
- **GPU compositing of the desktop:** windows as textures blended into the framebuffer (the R/B
  swap bit samples BGRA canvases without a CPU swizzle); the headroom for 4K / two displays. Also
  possible on the Pi 4, but the Pi 5 has the margin.
- **NetSurf:** GPU blits, scaling and alpha compositing of page layers and images.
- **Emulators:** depth clamp (N64), up to 8 render targets and the TFU (GameCube EFB copies,
  texture conversion, mipmaps), depth bounds.
- **Compute (CSD):** Koton DSP, image filters, the `llm` helper — new for Onyx on either board.

## 10. Phase 7 — taking advantage of the Pi 5

In rough order of value for effort.

1. **CPU speed, for free** (~2-3× per core). What gains:
   - gcemu: the machine core needs ≤ 20 ms a field and takes ~21 ms on a throttled Pi 4
     (`HANDOFF.md:566-590`) → full speed with margin; fastmem and the second JIT tier become less
     urgent. Re-measure with the PMU counters (`user/gc/gc.h:103-131`, architectural events) before
     trusting the Pi 4 tuning notes.
   - n64emu (interpreter, OoT 50.9 fps / 12.3 ms a frame on the Pi 4): "recompiler?" (`IDEAS.md:45`)
     becomes optional.
   - NetSurf layout / JS / restyle on hover (core 0), Koton polyphony and plugins (64 SF2 voices ≈
     5-10 % of an A72 core), the software compositor, Mandelbrot, BASIC, image codecs, the
     software 3D fallback.
   - Cheap re-tunes: `voronoy` `div = 1` instead of 2 (`voronoy/main.cpp:41`); Koton's
     `kapi_sound_config (128, 2)` "low latency" (`PERFORMANCE.md` §3).
2. **TLS with the crypto extensions:** `user/tls/Makefile:24-35` disables `MBEDTLS_AESCE_C` and the
   `*_USE_A64_CRYPTO_*` paths ("CRUCIAL for the Pi 4"), and `onyx_tls.hpp:180-183` prefers
   ChaCha20-Poly1305. On the Pi 5, enable AES-CE / SHA-CE (compile-time in a Pi-5 build, or
   run-time detection) and prefer AES-GCM: much faster HTTPS in NetSurf, lisa, llm, wget.
3. **Gigabit Ethernet** (Phase 5).
4. **The RTC:** Circle's `CFirmwareRTC` (`circle/addon/rtc/firmwarertc.*`, mailbox
   `GET/SET_RTC_REG`, `RTC_REGISTER_TIME` = seconds since 1970) — set the clock at boot before NTP
   (today `SD:/etc/clock`, `crashlog.cpp:835-866`, "no battery-backed clock"), write it back after
   an NTP sync. A battery (the official RTC battery) keeps it across power-off. Unblocks dated tasks
   for the planned task scheduler (`IDEAS.md:52`).
5. **The power button and a real power-off:** `is_power_button_pressed ()` (Circle, GPIO1 bank bit
   20, debounced, polling — `circle/lib/sysinit.cpp:258`): poll it from the 1 Hz GUI watchdog and
   post the existing session-end flow (menu Onyx ▸ Shut Down…), then `kapi_shutdown`. For a true
   power-off use PSCI `SYSTEM_OFF` (`sysinit.cpp:245-256`, `EXIT_POWER_OFF`) + `POWER_OFF_ON_HALT=1`
   in the EEPROM; today `halt()` leaves the board powered.
6. **Fan and temperature:** `gpiofanpin=45` + `CCPUThrottle::Update ()` (Phase 3); a temperature and
   clock readout in the menu bar or `memmon`/`taskman` (`PROPTAG_GET_TEMPERATURE`, `GET_THROTTLED`,
   already used by `CrashLogPower`).
7. **Storage:** **NVMe** (`circle/addon/nvme`, experimental, FatFs volume `NVME` / `nvme1`) as a new
   `NVMe:` volume (game ISOs, SoundFonts); **USB 3** mass storage completes the `USB:` volume
   (`IDEAS.md:48`). SDR104 is not in Circle (`SD_CLOCK_100/208` defined but unused).
8. **ARMv8.2 in the kernel and a Pi-5-tuned userland:**
   - **LSE atomics** (`swpal`, `casal`, `ldadd`) for spinlocks, the futex, the netcore mailbox,
     `emucore` `ec_xchg` (`user/emucore.h:59-67`), `kapi.h:455`: modest gains under contention; in
     a shared binary select at run time (a CPU-feature word, §11).
   - **RCpc (`ldapr`)** for the emucore ring acquire loads.
   - **CRC32 instructions** (ARMv8.0 already, unused): `user/img/pngsave.hpp:52-58`,
     `user/n64/n64_bus.cpp:39`, zlib.
   - **PAN / UAO** for `docs/EL0-PROTECTED-MODE.md`: with PAN, stray kernel accesses to user pages
     fault while `LDTR`/`STTR` copies still work. The A72 lacks PAN → keep it optional (feature
     check). Also add `SCTLR_EL1.UCT` to that doc's §4.8 table: `user/gc/gc_jit.cpp:54` reads
     `CTR_EL0`, which traps at EL0 without it.
   - **The JIT flush:** skip the `dc cvau` loop when `CTR_EL0.IDC = 1` and the `ic ivau` loop when
     `DIC = 1` (`gc_jit.cpp:52-61`) — **(verify)** the BCM2712's values.
   - A Pi-5 userland with `-mcpu=cortex-a76` everywhere (the Makefiles below) once the Pi 5
     distribution is separate. `-mno-outline-atomics` (`user/Makefile:176, 182`) becomes moot
     (GCC emits inline LSE).
9. **16 GB** (RAM policy B, §5.2): larger NetSurf caches, whole GameCube ISOs in RAM, Koton sample
   libraries.
10. **Dual 4K HDMI:** `SCREEN_MAX_W/H` is 2560×1600 (`kernel/include/kern/gui/window.h:37-40`,
    `kapi_abi.h:982`, `displayconf/main.cpp:33-35`); raise it on the Pi 5 (memory for the canvases
    and the compositor's bandwidth). A second screen needs a second compositor target: large, low
    priority, depends on Circle's limited Pi 5 framebuffer.
11. **Hardware RNG:** the BCM2712's RNG200 (`CBcmRandomNumberGenerator` works on the Pi 5) could
    seed `kapi_random` (`kapi.cpp:1181-1210`, software splitmix64 today) and mbedTLS.

## 11. kapi additions (append-only)

Proposals — each one updates `kapi_abi.h`, `sys/kapi.cpp`, `sys/kapitable.cpp`, `user/kapi.h`, the
ABI table in `docs/02`, `docs/03` and the `KAPI_ABI_VERSION` history (CLAUDE.md rule).

| kapi | Purpose |
|---|---|
| `board_info (struct *)` | board (Pi 4 / Pi 400 / Pi 5 / Pi 500), SoC, RAM, CPU features word (LSE, AES, SHA, CRC32, PAN, dotprod, fp16), V3D version, sound outputs present, Ethernet present. Lets one app binary adapt at run time. |
| `gpu_version ()` (or a field in `board_info`) | 42 / 71 for the QPU builders |
| a version flag in `kapi_gpu_program` | refuse a program built for the other ISA |
| `net_link (…)` / an Ethernet state in `net_status` | `wifimenu` and the network applet show Ethernet |
| `sound_outputs` / a `soundout` choice | `soundconf` lists HDMI / USB / I2S / PWM |
| `power_event ()` or a posted message | the power button starts the session-end flow |
| (EL0 prep) the core number in `TPIDRRO_EL0` | not a kapi call: a documented register the kernel maintains (B1) |

## 12. Text, docs and tools to update

- **Compiler flags** `-mcpu=cortex-a72` (fine for a shared userland; switch only for a Pi-5-tuned
  build): `user/Makefile:5, 60, 64, 79, 176, 182`, `user/bin/Makefile:16, 20, 27`,
  `user/doom/Makefile:13, 16`, `user/netsurf/Makefile:44`, `user/netsurf/netsurf-app.mk:47, 147,
  183-187, 246`, `user/img/Makefile:25, 27`, `user/tls/Makefile:15, 17`, `user/stk/Makefile:29`,
  `user/nsfb/Makefile:27`, `third_party/libwebp-1.4.0/src/**/Makefile`,
  `tools/tests/koton/{synth,plug,engine,ai}_run.sh`.
- **The QPU toolchain** pinned to 42: `tools/qpu/qpulib.c:19-21`, `tools/qpu/qpusim.cpp:17`,
  `user/v3d/qpu.cpp:19`.
- **Pi-4 strings and comments:** `kernel/kernel.h:14, 65` and `kernel/kernel.cpp:1244, 1653-1654`
  ("A72", "1.5 GHz", "~1500 MHz on a Pi 4"), `kernel/sys/crashlog.cpp:283, 420`,
  `kernel/kernel.cpp:503`, `user/Apps/irc/main.cpp:908` (CTCP VERSION "Raspberry Pi 4"),
  `user/Apps/lisa/main.cpp:349`, `user/Apps/teapot/main.cpp:2`, `user/Apps/memmon/main.cpp:114`
  ("Above 4G"), `user/Apps/soundconf/main.cpp:94`, `user/tls/onyx_tls.hpp:13, 180`,
  `user/tls/README.md:57-58`, `kapi_abi.h:661-666, 670, 819`, `user/kapi.h:303, 563`,
  `tools/screenshot/render.py:361`, `tools/tests/desktop_sim/shots.sh:136`,
  `user/Apps/koton/ui/audio.h:138` and `ui/chrome.h:161, 465` ("CORE 2" hard-coded even when
  another core was acquired).
- **Docs:** `ARCHITECTURE.md:51-66` ("RPi 5 deferred"), `:245-279, 363-392`,
  `docs/01-PROJECT-OVERVIEW.md:152`, `docs/03-DEVELOPER-GUIDE.md` (build: the second board,
  `./configure -r 5`), `docs/04-USER-GUIDE.md` (the Pi 5 card, sound outputs, Ethernet, the power
  button), `docs/05-CIRCLE-CHANGES.md` (the high-memory patch reworked, `:588`),
  `docs/02-KERNEL-INTERNALS.md` §15 (V3D 7.1), `docs/daw/PERFORMANCE.md`, the website
  (`docs/website/index.html`, "Pi 5 not supported"), then `python docs/build_docs.py`.

## 13. Test plan

| Stage | Check | Tool |
|---|---|---|
| Build | both images build from a clean tree; `sizecheck` passes; the Pi 4 image is unchanged byte-for-byte except intended changes | `make`, `make BOARD=pi5` |
| Pi 4 regression | after every phase, the Pi 4 still boots and passes the usual tests (B1 and the memory rework touch both) | the Pi-4 workflow (telnet, FTP, vncdotool) |
| Boot | serial log to the desktop; `ps` shows the per-core tasks; `coretest`, `threadtest`, `futextest`, `ringtest` pass (B1) | debug UART, `user/bin/*test` |
| Memory | `meminfo` reports the RAM once; a long NetSurf + gcemu session with no corruption; `heaptest` | `memmon`, `kapi_meminfo` |
| Display | 32 bpp colours right; compositor DMA on and `dispdma=0`; the resolution change | VNC screenshots |
| Sound | HDMI tone; Koton at 256 × 2 and 384 × 2 without underruns; the emulators' audio | `soundconf`, Koton meter |
| Network | Ethernet DHCP + FTP deploy speed; Wi-Fi with HDMI connected | `ftp`, `ping`, `netstat` |
| GPU | the §9.3 ladder | `v3dprog`, `gpudemo`, gcemu |
| Thermal | 30 min of gcemu with the Active Cooler: clock stays at 2.4 GHz, the fan switches | `CrashLogPower` log |
| Crash log | a forced fault, watchdog reboot, `lastcrash.txt` kept | `hangtest` |

## 14. Open questions (verify on hardware)

1. Does the firmware patch the DT root `#size-cells` so the fork's 16-byte `/memory` parse holds?
2. Does RAM survive a watchdog reboot through the EEPROM bootloader (the crash area)?
3. Does the Onyx 2D-DMA present path work on the Pi 5's DMA4 channels (else `dispdma=0`)?
4. Can the framebuffer be re-allocated at run time (the Display app), and are `SetVirtualOffset` /
   two displays honoured by the Pi 5 firmware?
5. HDMI audio minimum chunk / latency; USB audio on the Pi 5 (README ambiguous).
6. Wi-Fi stability with HDMI in use.
7. SD High Speed (`sdhs=1`) on the Pi 5 controller.
8. V3D 7.1 with its MMU off; whether the firmware already powers the SMS; the exact V3D clock;
   whether the "domain 10" mailbox call is harmless; the exact `ldunif`/`ldvary` → rf0 latencies.
9. `CTR_EL0.IDC` / `DIC` on the BCM2712 (JIT flush shortcut).
10. How gcemu and n64emu behave when `gpu_info` returns 0 (must degrade, not hang).

## 15. Sources

- Circle fork: `circle/README.md` (feature table, Pi 5 column), `circle/doc/memorymap.txt` (Pi 5
  section), `circle/doc/issues.txt:51`, `circle/boot/README`, `circle/Rules.mk:100-123`,
  `circle/include/circle/memorymap64.h`, `circle/lib/translationtable64.cpp`,
  `circle/lib/memory64.cpp`, `circle/lib/multicore.cpp`, `circle/lib/sysinit.cpp`,
  `circle/lib/sound/*`, `circle/addon/wlan/`, `circle/addon/rtc/`, `circle/addon/nvme/`.
- Linux DRM v3d: <https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/v3d>
  (`v3d_regs.h`, `v3d_drv.c`, `v3d_gem.c`, `v3d_irq.c`, `v3d_power.c`, `v3d_sched.c`, `v3d_mmu.c`).
- Raspberry Pi device trees: <https://github.com/raspberrypi/linux/blob/rpi-6.12.y/arch/arm64/boot/dts/broadcom/bcm2712-ds.dtsi>,
  `bcm2712.dtsi`; the PM driver <https://github.com/raspberrypi/linux/blob/rpi-6.12.y/drivers/pmdomain/bcm/bcm2835-power.c>.
- Mesa: <https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/broadcom/cle/v3d_packet.xml>,
  `src/gallium/drivers/v3d/v3dx_rcl.c`, `v3dx_emit.c`, `v3d_uniforms.c`,
  `src/broadcom/common/v3d_util.c`, `src/broadcom/compiler/qpu_schedule.c`, `nir_to_vir.c`;
  <https://docs.mesa3d.org/drivers/v3d.html>.
- Igalia, "Raspberry Pi 5 / V3D 7.1" (EOSS 2024):
  <https://static.sched.com/hosted_files/eoss24/78/2024-04-eoss-apinheiro-rpi5.pdf>;
  <https://blogs.igalia.com/apinheiro/>.
- Phoronix: <https://www.phoronix.com/news/Mesa-RPi-5-VideoCore-7.1.x>,
  <https://www.phoronix.com/review/raspberry-pi-5-benchmarks>.
- py-videocore7 (QPU counts, GFLOPS): <https://github.com/Idein/py-videocore7>.
- BCM2712 / A76: <https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/processors/bcm2712.adoc>;
  the MPIDR layout: <https://github.com/hatter6822/rpi5_machine/pull/3>.
- Power button, `POWER_OFF_ON_HALT`, RTC: <https://www.raspberrypi.com/documentation/computers/raspberry-pi.html>,
  <https://www.jeffgeerling.com/blog/2023/reducing-raspberry-pi-5s-power-consumption-140x/>.
- Caches and self-modifying code (`CTR_EL0.IDC/DIC`):
  <https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/caches-self-modifying-code-implementing-clear-cache>.
