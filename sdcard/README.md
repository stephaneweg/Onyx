# Bootable SD card (Raspberry Pi 4)

Copy **all files in this directory** onto a **FAT32**-formatted SD card (root of
the first partition), insert it into a Raspberry Pi 4, and power on.

## Files

| File | Role |
|---|---|
| `start4.elf`, `fixup4.dat` | Raspberry Pi 4 GPU firmware |
| `bcm2711-rpi-4-b.dtb` | device tree for the Pi 4 |
| `armstub8-rpi4.bin` | Circle's ARM stub (EL setup / FIQ) |
| `config.txt` | boots `kernel8-rpi4.img` in 64-bit mode on `[pi4]` |
| `kernel8-rpi4.img` | **our kernel** |
| `demoA.elf`, `demoB.elf` | the two windowed EL0 demo programs, loaded from the card |

If `demoA.elf` / `demoB.elf` are missing, the kernel falls back to copies embedded
in `kernel8-rpi4.img`, so it still runs.

## What you should see

- **Serial console** (GPIO14 TXD / GPIO15 RXD, 3.3 V, **115200 8N1**): boot log —
  machine/RAM, scheduler start, `SD card mounted`, the syscall self-test, and
  `demoA`/`demoB` entering EL0.
- **HDMI**: a desktop with **two windows** side by side, both animating at the same
  time — demoA a bouncing box, demoB a moving colour field. They are two separate,
  isolated EL0 processes; the kernel preempts between them and a compositor draws
  both.

## Rebuilding

Rebuild the kernel and demos (see `../ARCHITECTURE.md` §9), then re-copy
`kernel8-rpi4.img`, `demoA.elf`, `demoB.elf` here. The firmware files rarely change.

## Notes

- First runtime test of this kernel — if it hangs, capture the serial output; the
  exception handler dumps registers there on a fault.
- Screen is 640×480 (set in `kernel/kernel.h`). A `cmdline.txt` with
  `width=… height=…` is honored by Circle's framebuffer if you want a different size.
