# Bootable SD card (Raspberry Pi 4)

This folder is a ready-to-boot Onyx card. Copy **all files in this directory** onto a
**FAT32**-formatted SD card (root of the first partition), insert it into a Raspberry Pi 4
(or a Pi 400), and power on. The first boot runs **Setup**, the first-run wizard (country,
keyboard, time zone, Wi-Fi, resolution, colours). Details: [`docs/04-USER-GUIDE.md`](../docs/04-USER-GUIDE.md)
§2–§4.

## Files

| File | Role |
|---|---|
| `start4.elf`, `fixup4.dat` | Raspberry Pi 4 GPU firmware |
| `bcm2711-rpi-4-b.dtb`, `bcm2711-rpi-400.dtb` | device trees for the Pi 4 B and the Pi 400 |
| `firmware/` | Wi-Fi firmware: `brcmfmac43455-sdio.*` (Pi 4 B), `brcmfmac43456-sdio.*` (Pi 400) |
| `armstub8-rpi4.bin` | Circle's ARM stub (EL setup / FIQ) |
| `config.txt` | boots `kernel8-rpi4.img` in 64-bit mode on `[pi4]` |
| `kernel8-rpi4.img` | **the Onyx kernel** (kapi ABI v74: every app at EL0) |
| `cmdline.txt` | kernel options: `width=`/`height=`, `init=SD:/bin/init`, `netcore=1` (the network on core 3), … (docs/04 §3) |
| `apps/<name>.app/` | the **applications** (one per `.app` folder): `main` (the ELF), `app.txt`, icons, resources |
| `bin/<tool>` | the terminal **command-line tools** (incl. `init`, the first program, and `cmd`, the shell) |
| `etc/` | system configuration (`autostart`, `system.ini`, `theme.txt`, `dock.ini`, `fileassoc.ini`, keymaps, …) |
| `var/pkg/db` | the installed packages (the Package Manager, `pkg`) |
| `res/` | shared resources: fonts, icons, SoundFonts, Jet Browser's files |
| `koton/`, `basic/`, `docs/`, `music/`, `courier/`, `manuals/`, `doom/`, `roms/`, `wallpapers/`, `fonts/` | Koton's plugins and songs, BASIC samples, sample documents, Freedoom, a place for your ROMs, wallpapers |

Onyx executables carry **no extension** (`main`, `bin/ls`, …); only the Pi firmware keeps its
own name (`start4.elf`). Never committed here: your Wi-Fi (`etc/wpa_supplicant.conf`, written
by Setup or the Wi-Fi menu), `etc/clock`, ROMs and saves.

## What you should see

- **HDMI**: the boot log, then the **Onyx desktop** — the menu bar at the top, the dock at the
  bottom, the wallpaper, the agenda (on the very first boot, Setup over the wallpaper first).
- **Without a screen**: the green ACT LED blinks slowly (1 s) while the network is down, fast
  (0.2 s) once it is up (`telnetd` on port 23, `vncd` on 5900, `rdpd` on 3390); SOS = a kernel
  panic, reported in `etc/lastcrash.txt` at the next boot.
- **Serial console** (GPIO14 TXD / GPIO15 RXD, 3.3 V, **115200 8N1**): the same boot log.

## Rebuilding

Rebuild the kernel and the apps, then run `make stage` from `kernel/`: it copies
`kernel8-rpi4.img`, `apps/<name>.app/main`, `bin/<tool>` and Koton's plugins here
(docs/03 §3–§4). The firmware files rarely change. A card already in use updates itself from
the package repository (`pkg`, the Package Manager: `docs/pkg/README.md`).
