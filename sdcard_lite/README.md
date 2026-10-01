# Bootable SD card (Raspberry Pi 4)

Copy **all files in this directory** onto a **FAT32**-formatted SD card (root of
the first partition), insert it into a Raspberry Pi 4, and power on.

## Files

| File | Role |
|---|---|
| `start4.elf`, `fixup4.dat` | Raspberry Pi 4 GPU firmware |
| `bcm2711-rpi-4-b.dtb`, `bcm2711-rpi-400.dtb` | device trees for the Pi 4 B and the Pi 400 |
| `firmware/` | Wi-Fi firmware: `brcmfmac43455-sdio.*` (Pi 4 B), `brcmfmac43456-sdio.*` (Pi 400) |
| `armstub8-rpi4.bin` | Circle's ARM stub (EL setup / FIQ) |
| `config.txt` | boots `kernel8-rpi4.img` in 64-bit mode on `[pi4]` |
| `kernel8-rpi4.img` | **our kernel** |
| `cmdline.txt` | kernel options (`width=`/`height=`, `init=SD:/bin/init`) |
| `apps/<name>.app/main` | the **applications** (one per `.app` folder) |
| `bin/<tool>` | the terminal **command-line tools** (incl. `init`) |
| `etc/` | system configuration (`autostart`, `system.ini`, `theme.txt`, keymaps, …) |

Onyx executables carry **no extension** (`main`, `bin/ls`, …); only the Pi firmware
keeps its own name (`start4.elf`).

## What you should see

- **Serial console** (GPIO14 TXD / GPIO15 RXD, 3.3 V, **115200 8N1**): boot log —
  machine/RAM, scheduler start, `SD card mounted`, the syscall self-test, and
  `demoA`/`demoB` entering EL0.
- **HDMI**: a desktop with **two windows** side by side, both animating at the same
  time — demoA a bouncing box, demoB a moving colour field. They are two separate,
  isolated EL0 processes; the kernel preempts between them and a compositor draws
  both.

## Rebuilding

Rebuild the kernel and apps, then run `make stage` from `kernel/`: it copies
`kernel8-rpi4.img`, `apps/<name>.app/main` and `bin/<tool>` here. The firmware files rarely change.

## Notes

- First runtime test of this kernel — if it hangs, capture the serial output; the
  exception handler dumps registers there on a fault.
- Screen is 640×480 (set in `kernel/kernel.h`). A `cmdline.txt` with
  `width=… height=…` is honored by Circle's framebuffer if you want a different size.
