---
name: pi-4k-cmdline-trap
description: "Setting width=3840 height=2160 in the Pi's cmdline.txt and rebooting left it off the network (2026-10-27): test a resolution live, never by cmdline + reboot"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 9e606274-0f6e-4c23-beac-7cc8ba27e7ec
  modified: 2026-10-27T09:55:44.219Z
---

On 2026-10-27 the user asked for desktop captures "en mode 4K" on their Pi (192.168.0.16, a Pi 4 in the
PocketTerm35 setup, pocket mode at 640x480). I backed up `SD:/cmdline.txt` to `SD:/cmdline.pocket`, wrote
`width=3840 height=2160` and `shell=desktop` in `system.ini`, and rebooted: the Pi never came back (no telnet,
no VNC, not in arp) -- most likely the firmware could not give a 4K framebuffer, and without one Onyx starts no
userland (config.txt's comment). In fact (the user, watching the screen): it booted very slowly and restarted
once the desktop showed, over and over. The user put the card in the PC (drive E:) and I set 1280x1024.

**Why:** a cmdline.txt change has no way back from afar; the Display applet's live change has a 15 s revert.

**How to apply:** change a resolution live (the Display applet / SystemKit's display call, with its Keep
prompt) before writing it for the next boot; keep a backup of cmdline.txt; tell the user before a reboot that
may not come back. See [[pi-remote-access]], [[pi-net-trial]].
