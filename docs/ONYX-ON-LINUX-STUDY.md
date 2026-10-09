# Onyx on the Linux kernel — a study and a plan

> **Status (2026-10-09): a study only, for later. Nothing is built, nothing is decided.** The user asked
> for a plan "to think about for the future". Onyx native (Circle + the Onyx kernel) stays **the**
> Onyx; this page describes a possible **second target**, *Onyx/Linux*: the Linux kernel alone — no
> distribution, no glibc, no systemd, no X11/Wayland, no shell of GNU — with Onyx's init, AppKit,
> Elegant, the kits and the apps on top. Do not start it unasked.
> Answer the user in French; this page stays in English.

## 1. The idea

The Linux kernel needs no distribution: once booted it starts **one program** (`/sbin/init`, or the
`init=` of the command line, PID 1) and nothing else. Android (its own init, its own libc, no GNU tools)
and LibreELEC (Linux + Kodi) are built this way. Onyx could be too.

What makes it cheap for Onyx is the rule **kits first**: a program never reaches the kernel; **only
AppKit** does (`user/Kits/appkit/appkit_calls.inc`, ~240 live `kapi_*` calls). Re-writing AppKit's
bodies over Linux's system calls, and giving a home in user space to what the Onyx kernel does beside
the system calls (the mixer, the input, the display), would leave the kits, Elegant and the apps
largely unchanged.

There is already a proof that the apps' own binaries run on Linux: the desktop simulator's
**`tools/tests/desktop_sim/elfrun.cpp`** maps a Pi app's ELF (newlib and all) at its addresses on Linux
AArch64 and runs it, its kapi table filled by `fakekapi.cpp`. Onyx/Linux is that, made real.

### 1.1 What it would bring

- **All the hardware, at once**: the V3D GPU through the kernel's `v3d` DRM driver and Mesa (MIT); the
  Wi-Fi (`brcmfmac`), the Bluetooth (HCI sockets: A2DP, docs/BLUETOOTH-AUDIO-STUDY.md), every USB class,
  the HDMI audio, the RP1 of the Pi 5 (docs/PI5-PORT.md: the GPIO, the Ethernet) — maintained by others.
- **The Pi 5 (and later boards) from day one**: one `.dtb` and a kernel config.
- Mature memory management, preemptive SMP on all cores, file systems (vfat, exFAT, ext4), USB mass
  storage hot-plug, power management, the CPU's frequency.
- **Licence**: the Linux kernel is GPL-2.0, but its system-call boundary does not extend it to user space
  (the kernel's `COPYING`, "syscall exception"). Without Circle, *nothing* of Onyx would carry the
  GPL-3.0: AppKit, Elegant, the kits and the apps stay MIT (docs/LICENSING.md would gain a column).

### 1.2 What it would cost

- **Onyx's kernel is not used** on that target: the scheduler, the address spaces, the kapi — the heart
  of the project (and of the *Programmez!* article). That is why it is a second target, not a
  replacement.
- **Boot time**: 2 to 5 s for a trimmed kernel, against the native kernel's near-instant start.
- **Size**: a kernel image of 10–20 MB (modules built in), plus the Wi-Fi/BT firmware (already on the card).
- **The build**: cross-compiling a Linux kernel (the Raspberry Pi tree `raspberrypi/linux`, or mainline),
  a `defconfig` of our own; a C library for the native bits.
- **Two targets to keep working**: every new kapi call needs a Linux body (or `-KAPI_ENOSYS`).

## 2. What the card would hold

```
SD:/ (FAT, the boot partition — as today)
  start4.elf, fixup4.dat, config.txt   the firmware (kernel=Image, arm_64bit=1)
  Image                                the Linux kernel (our config, everything built in, no modules)
  bcm2711-rpi-4-b.dtb, overlays/       the device tree (from the Raspberry Pi tree)
  cmdline.txt                          root=/dev/mmcblk0p1 rootfstype=vfat init=/bin/onyxinit quiet
  bin/onyxinit                         Onyx's PID 1 (§4.1)
  bin/elegant, bin/*, apps/, lib/, etc/, fonts/ ...   Onyx, as today
```

**One FAT partition, as today** (Linux can boot with its root on vfat: no symbolic links, no Unix
rights — Onyx needs neither). `SD:/` maps to `/`; AppKit translates `SD:/x` to `/x` (and the other
volumes `USB0:/` … to their mount points, §4.7). An initramfs is not needed.

## 3. Where each part of today's kernel goes

| Today (the Onyx kernel, docs/02) | On Linux | Who does it |
|---|---|---|
| Processes, threads, the scheduler, address spaces (§3–§7) | Linux | `clone`, `mmap`, `futex` |
| ELF loader, program images (§7) | A **user-space loader** (`onyxld`, from `elfrun.cpp`): maps the app at its addresses (`MAP_FIXED_NOREPLACE`) | AppKit / `onyxld` |
| Shared libraries, export tables, `lib_open`, `lib_open_as` (§7, v83/v97) | The same `.so` format, mapped by `onyxld` in [16 GB, 32 GB), relocated, bound | `onyxld` |
| The kapi table at `KAPI_TABLE_VA` (§8) | `onyxld` maps AppKit/Linux and fills `APPKIT_TABLE_VA`; **the apps' binaries are unchanged** | AppKit |
| Files, directories, streams, pipes (§9) | `open`, `read`, `pipe2`, `getdents64`… | AppKit |
| Sockets, `poll` (v75) | Linux sockets, `ppoll` | AppKit |
| IPC services, mailboxes (v40) | Abstract Unix sockets (`\0onyx/<service>`) | AppKit |
| Shared surfaces, `shm_*` (v35/v76) | `memfd_create` + `SCM_RIGHTS` | AppKit |
| Handles passed at spawn (`spawn_ex2`) | Inherited file descriptors | AppKit |
| Elegant's display (`ws_ctl` DISPLAY / PRESENT, §10) | **DRM/KMS**: dumb buffers + page flip (later GBM/EGL) | Elegant |
| Elegant's input (`ws_ctl` INPUT, §10.3), the keymaps | **evdev** (`/dev/input/event*`); the `.kmap` files applied by Elegant | Elegant |
| Full-screen programs (`fullscreen_*`, `present_fb`) | Through Elegant (a surface covering the screen), or DRM master handed over | Elegant |
| The mixer, `sound_*` (§12) | An **audio server** `audiod` (the mixer moved out of the kernel) over **ALSA** (`/dev/snd/pcmC*`, ioctls, no alsa-lib) | new `bin/audiod` |
| MIDI (`midi_read`) | ALSA raw MIDI (`/dev/snd/midiC*`) | AppKit / `audiod` |
| Game pads (`pad_state`) | evdev joysticks | Elegant or AppKit |
| App cores (§14, `core_acquire` / `core_run`) | `sched_setaffinity` + `isolcpus=2,3` on the command line | AppKit |
| The V3D (§15, `gpu_*`) | The `v3d` DRM driver: `DRM_IOCTL_V3D_SUBMIT_CL`, BOs; later Mesa | AppKit (then a GL kit) |
| The network stack, Wi-Fi association, NTP (§11) | Linux TCP/IP; **`wpa_supplicant`** (BSD) or a small nl80211 client; an SNTP client | `onyxinit` / `netd` |
| `wlan_scan`, `net_info`, `net_stats` | nl80211, `getifaddrs`, `/sys/class/net` | AppKit / `netd` |
| User file systems (`vfs_register`: ftpfs) | **FUSE** (`/dev/fuse`, its protocol spoken directly) | NetKit |
| Volumes: mount, eject, format (§18) | `mount(2)`, netlink uevents for hot-plug, `mkfs` of our own | `onyxinit` / `vold` |
| The RAM: volume (§16) | `tmpfs` | `onyxinit` |
| GPIO (§17, `gpio_ctl`) | `/dev/gpiochip*` (the character device), `/dev/i2c-*`, `/dev/spidev*` | GPIOKit's backend |
| Crash log (§13, `lastcrash.txt`) | Signal handlers in AppKit; the kernel's own panics in `/dev/kmsg` → `pstore` | AppKit |
| `klog_read`, `kernel_info`, `cpu_stats`, `meminfo` | `/dev/kmsg`, `uname`, `/proc/stat`, `/proc/meminfo` | AppKit |
| `shutdown`, `reboot` | `sync` + `reboot(2)` (from PID 1) | `onyxinit` |
| `random` | `getrandom` | AppKit |

## 4. The new pieces

### 4.1 `onyxinit` — PID 1

A small static program (MIT): mounts `/proc`, `/sys`, `/dev` (devtmpfs), `/tmp` and `RAM:` (tmpfs);
reads `SD:/etc/system.ini`; starts `audiod`, the network, then the graphics server (`elegant`, or
`pocketui` for `shell = pocket / console`, as the native kernel does, v97) and the startup programs;
reaps orphans (`wait`); on shutdown, ends the programs, `sync`, `reboot(2)`. It never exits (PID 1
dying is a kernel panic).

### 4.2 `onyxld` — the program loader

From `elfrun.cpp`: the loader every Onyx program is started through (`onyxld APP [args]`, or `binfmt_misc`
so that `exec` of an Onyx ELF goes to it). It maps the program's `PT_LOAD` segments at their addresses,
maps `SD:/lib/appkit.so` (the Linux one) and fills the table AppKit's programs read, then the kits on
`lib_open` (the same arena, the same relocation, the same export tables as docs/02 §7), sets up the
stack and jumps. **Same binaries on both targets** is the goal (§6, decision L2).

Points to verify:
- **The core number**: `kapi__core ()` (inline in `appkit/appkit.h`) reads `TPIDRRO_EL0`, which Linux keeps
  at 0 for a native task. Every program would see "core 0": `lock_me` (`user/Runtime/libc/onyx_syscalls.c`)
  then uses the thread's id — correct — and `on_app_core` says no, which is right while the app cores'
  RPC is not used. To check when the app cores are mapped (§3).
- **The addresses**: the user window up to 60 GB (`USER_WS_BASE`) needs Linux's 39-bit or 48-bit VA:
  both fit (512 GB / 256 TB); check the Raspberry Pi config's `ARM64_VA_BITS`.
- **ID registers**: the native kernel emulates `MRS` of ID registers (`EmulateMrs`); Linux does too
  (`arm64 cpu-feature-registers`), with sanitised values.

### 4.3 AppKit/Linux

The same `appkit.h`, the same `appkit.abi`; a second body, `appkit_linux.c`, one function per kapi call
over Linux's system calls (raw `svc #0`: no libc needed in AppKit itself, as today). The calls with no
Linux meaning answer `-KAPI_ENOSYS`, as an older kernel would. Programs keep newlib: its glue
(`user/Runtime/libc/onyx_syscalls.c`) already goes through kapi.

### 4.4 Elegant on DRM/KMS and evdev

Elegant composes in user space already (docs/02 §10, `user/Servers/elegant`); only its two ends change:
- **out**: instead of `KAPI_WS_DISPLAY` / `KAPI_WS_PRESENT`, a DRM dumb buffer (two, flipped), the
  mode read from the connector (the native `screen_set` → a KMS mode set);
- **in**: instead of `KAPI_WS_INPUT`, the evdev devices (keyboard, mouse, touch, pads), hot-plugged
  through inotify on `/dev/input`; Onyx's keymaps (`SD:/etc/keymaps/*.kmap`) applied by Elegant.
- The programs' side (`KAPI_WS_CALL`, the buffers) over a Unix socket and memfd surfaces.

Later, with Mesa: Elegant composing on the GPU (GBM + EGL/GLES), the apps' 3D (`gpu_*` → GL).

### 4.5 `audiod` — the mixer

Today the mixer is in the kernel (docs/02 §12, v85: clients, volumes, outputs). On Linux it becomes a
server: clients write into a shared ring (memfd), `audiod` mixes and writes to ALSA (jack, HDMI, USB,
Bluetooth later). AudioKit is unchanged; AppKit's `sound_*` speak to `audiod`.

### 4.6 The network

The Linux stack does TCP/IP, DHCP aside: a small DHCP client and an SNTP client (in `netd`, MIT, or
from BSD sources). The Wi-Fi: `brcmfmac` + its firmware (as today: `SD:/firmware`), the WPA handshake by
**`wpa_supplicant`** (BSD licence: fine) driven from `netd`, which keeps `SD:/etc/wpa_supplicant.conf`
and the Control Panel's applet as they are.

### 4.7 The volumes

`vold` (or `onyxinit`): listens to the kernel's uevents (netlink), mounts a USB stick's partitions as
`USB0:`… (vfat / exFAT), unmounts on eject; `vol_format` with our own FAT formatter (FileKit).

## 5. The plan — phases

| Phase | What | Done when |
|---|---|---|
| **L0** | The kernel: a `defconfig` for the Pi 4 (everything built in: vfat, evdev, DRM vc4 + v3d, ALSA bcm2835 + HDMI, brcmfmac, USB HID/storage, FUSE, tmpfs), the card layout of §2, `tools/linux/build.sh` | Linux boots from the Onyx card to a static "hello" as PID 1, on HDMI's text console |
| **L1** | `onyxld` (from `elfrun.cpp`) + **AppKit/Linux**: files, processes, pipes, time, memory, threads, sync | `/bin` tools run (`ls`, `cat`, the shell) on a serial/HDMI console |
| **L2** | `onyxinit` (mounts, `system.ini`, startup, reaping, shutdown) | the console shell started at boot, `reboot` works |
| **L3** | Elegant on DRM/KMS + evdev; the window protocol over Unix sockets + memfd | the desktop, the Files, the Terminal, the Control Panel |
| **L4** | The kits' libraries through `onyxld` (`lib_open`, `lib_open_as`), IPC services, the clipboard, the notifications | every UIKit app starts |
| **L5** | `audiod` + ALSA | the Media Player, the emulators' sound |
| **L6** | The network: Ethernet, DHCP, DNS, SNTP; Wi-Fi with `wpa_supplicant`; FUSE for ftpfs | `pkg update`, Jet, Mail |
| **L7** | Volumes, GPIO, game pads, MIDI, crash logs, `kernel_info`'s `board` | parity with the native target, as far as useful |
| **L8** | The GPU: `gpu_*` over the `v3d` DRM driver, then Mesa (GLES) for Elegant and the 3D apps | Elegant composes on the GPU |
| **L9** | The Pi 5: its `.dtb`, its config, a card of its own (docs/PI5-PORT.md §4.4) | the same Onyx/Linux on a Pi 5 |
| **L10** | Bluetooth: HCI sockets, A2DP sink/source (docs/BLUETOOTH-AUDIO-STUDY.md) | sound to a headset |

L0 to L3 are the proof of the idea: if the desktop runs there, the rest is filling calls.

## 6. Decisions for the user

- **L1 — a second target or never?** This page assumes a second target beside the native one, never a
  replacement.
- **L2 — same binaries or rebuilt?** Same ELF on both targets (only AppKit and the loader differ: the
  packages stay one set) — the goal; or a separate build (`BOARD=linux`), as the Pi 5 has its own
  distribution, if a difference forces it.
- **L3 — the card**: one FAT partition (§2, simplest), or a FAT boot partition + an ext4 root (Unix rights,
  links — not needed today).
- **L4 — which kernel tree**: the Raspberry Pi tree (`raspberrypi/linux`, every Pi driver, the firmware's
  overlays) or mainline (cleaner, some Pi drivers missing). The Raspberry Pi tree is the safer start.
- **L5 — the libc of the native pieces** (`onyxinit`, `onyxld`, `audiod`, `netd`): newlib with our glue
  (one runtime for all) or **musl** (MIT, small, made for static Linux programs).
- **L6 — the packages**: a `linux/` folder in `onyx-packages` (as `pi5/`), or the same packages if L2 holds
  (only the kernel and AppKit packages differ).

## 7. Licences

| Part | Licence | Effect |
|---|---|---|
| Linux kernel | GPL-2.0 | Distributed with its source (or an offer); does not reach user space |
| Firmware blobs (`start4.elf`, `brcmfmac` firmware) | Broadcom / Cypress redistributable | As today |
| `onyxinit`, `onyxld`, AppKit/Linux, `audiod`, `netd`, Elegant, the kits, the apps | MIT | Ours |
| `wpa_supplicant` | BSD-3-Clause | Fine |
| Mesa (L8) | MIT | Fine |
| musl (if chosen, L5) | MIT | Fine |

The programs that link a copyleft library keep their licence as today (docs/LICENSING.md): Jet LGPL-2.1,
the Media Player GPL-2.0, the PDF Viewer AGPL-3.0, Doom GPL. **Never copy Linux or BlueZ code into MIT
pieces**: read them for understanding only.

## 8. Risks and unknowns

- The fixed user addresses (8–60 GB) must not collide with what Linux places (the vDSO, the stack: both
  high, above 256 GB with 48-bit VA; check with 39-bit).
- Programs that use the native kernel's specifics outside AppKit: none should (the rule), but inline helpers
  in `appkit.h` (`kapi__core`) are compiled into them — audit `appkit.h` for every inline that reads a
  register or a fixed address.
- The emulators' JIT (`code_alloc`, `vm_protect` PROT_EXEC): `mmap` + `mprotect` + cache maintenance —
  Linux allows user-space `DC`/`IC` (as the native kernel does, `SCTLR_EL1.UCI`).
- The latency of a user-space mixer against the in-kernel one: an `audiod` thread at `SCHED_FIFO`.
- Boot time against the native kernel: measure at L0 with a trimmed config.
- Keeping two AppKits in step: a test (`tools/tests`) that runs the same programs on both, using the
  simulator's scripts.
