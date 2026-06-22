# A multi-process kernel on Circle (Raspberry Pi 4)

A small **preemptive, multi-process kernel** built on top of
[Circle](https://github.com/rsta2/circle) (used as the hardware/driver layer). It
loads **ELF programs from the SD card** and runs them as **EL1 apps, each in its own
page table**, that **call kernel functions directly** (resolved at link time —
SimpleOS-style; apps are isolated from each other, the kernel is not protected — see
ARCHITECTURE.md §11). The showcase is **two windowed graphical demos animating at the
same time**, drawn by a software compositor whose rendering core is ported from the
user's FreeBASIC `SimpleOS` GUI.

See **[ARCHITECTURE.md](ARCHITECTURE.md)** for the full design.

## Status

All milestones implemented and **building** (`aarch64-none-elf`, WSL). Runtime is
validated on real Pi 4 hardware (no QEMU `raspi4b` available here).

| # | Milestone | |
|---|---|---|
| 1 | Skeleton, layout/MMU constants | ✅ |
| 2 | `main()` takes over from Circle's `sysinit`; serial console | ✅ |
| 3 | Preemptive scheduler replacing Circle's (shadow `CScheduler`) | ✅ |
| 4 | Own `VBAR_EL1` vectors, trap frame, syscalls, **preemption** | ✅ |
| 5 | Per-process page tables (`CAddressSpace`), TTBR0/ASID switch | ✅ |
| 6 | ELF64 loader → EL0 process | ✅ |
| 9 | Framebuffer (`C2DGraphics`) + `GImage` rendering core | ✅ |
| 10 | Compositor + window manager | ✅ |
| 7/11 | Two windowed EL0 demos, loaded from SD, running concurrently | ✅ |

## Layout

```
ARCHITECTURE.md   full design + build manifest
kernel/           the kernel (built on Circle; see kernel/README.md)
user/             userland ELF programs (hello, demoA, demoB) + runtime
sdcard/           ready-to-flash boot files for a Pi 4 (see sdcard/README.md)
circle/           upstream Circle clone (not committed; cloned separately)
```

## Build & run (summary)

```sh
# clone Circle next to this tree, then (one-time) renormalize it to LF
git clone --recurse-submodules https://github.com/rsta2/circle.git
git -C circle config core.autocrlf false && git -C circle rm --cached -rq . && git -C circle reset --hard

# build Circle libs (RPi4 / AArch64 / DEPTH=32) + the SD/FatFs addons
cd circle && ./configure -r 4 -p aarch64-none-elf- -d DEPTH=32 -f
(cd lib && make -j4) && (cd lib/sched && make -j4) && (cd lib/fs && make -j4) \
  && (cd addon/SDCard && make -j4) && (cd addon/fatfs && make -j4) && cd ..

# build the kernel; this also exports the kapi_* symbols and then builds the
# userland apps against them (apps must be built after the kernel)
(cd kernel && make)     # -> kernel/kernel8-rpi4.img + user/demoA.elf, demoB.elf
```

Then copy the contents of [`sdcard/`](sdcard/README.md) (plus the freshly built
`kernel8-rpi4.img`, `demoA.elf`, `demoB.elf`) onto a FAT32 SD card and boot a Pi 4
with HDMI + serial (115200). The two demos are **distinct ELF files on the card**
(`demoA.elf`, `demoB.elf`), loaded independently.
