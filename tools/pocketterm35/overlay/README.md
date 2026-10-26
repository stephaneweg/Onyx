# Onyx for the Waveshare PocketTerm35 (Raspberry Pi 4)

This folder is a ready-to-boot Onyx card for the **Waveshare PocketTerm35** with a **Raspberry Pi 4** inside.

1. Format a micro-SD card in **FAT32**.
2. Copy **all the files in this folder** to the root of its first partition.
3. Put the card in the PocketTerm35 and turn it on.

The first boot runs **Setup**, the first-run wizard, in the pocket look. It asks for the language, the country and
time zone, the Wi-Fi network and the device's name. Everything can be changed later in the Control Panel. The full
system is on the card, every app and game included, so nothing needs to be downloaded.

## What is set for the device

| What | Where | Setting |
|---|---|---|
| The screen | `cmdline.txt` | `width=800 height=600`. For the panel's own 640 x 480, write `width=640 height=480`. |
| The keyboard | `cmdline.txt` | `usbc=1`: the Pi's USB-C socket as a USB host, where the keyboard's RP2040 is wired. |
| The keyboard's layout | `cmdline.txt` | `keyboard=US`: Setup proposes US (QWERTY) and keeps it whatever the country. |
| The touch screen | `cmdline.txt` | `touch=1`: the GT911 touch panel as the pointer. |
| The interface | `etc/system.ini` | `shell=pocket`: the pocket mode, one app at a time, made for a small screen. |
| Waveshare's overlays | `config.txt`, `overlays/` | For Linux. Onyx reads no overlay, but they do no harm. |

### The touch screen

- **Tap**: a click.
- **Slide up or down**: scroll, the page following the finger.
- **Slide sideways, or hold 0.4 s then move**: drag with the button held (a slider, a scroll bar, a selection).
- **Two fingers up or down**: scroll too.

### The keys

The game buttons are keys of the keyboard: **A** `a`, **B** `b`, **X** `x`, **Y** `y`, **L** `l`, **R** `r`,
**Select** Print Screen, **Start** Pause. In the pocket mode:
- **Tab** steps the home's categories and a window's tabs (Shift+Tab back);
- **Shift+Select** takes a screenshot;
- **Super** (CMD) is the Menu; held half a second, it is Home.

The Gamepad applet (Control Panel) maps the buttons for the emulators.

### Sound

The speaker and the 3.5 mm output are fed by the Pi's headphone output: the Sound applet's **Automatic** plays
there. The device's Fn + W / E / R keys mute and set the volume.

## More

The user guide is in `docs/04-USER-GUIDE.md` of the Onyx sources. `cmdline.txt`'s options are in its section 3.

Onyx is MIT-licensed. The kernel is GPL-3.0, being built on the Circle library. The licences of everything on this
card: `docs/LICENSING.md`.
