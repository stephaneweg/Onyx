---
name: pi-package-update
description: "Updating the user's Pi by packages - pkg check first (refreshes the index), pkg update -a, then reboot for onyx; allowed by the user on 2026-10-05"
metadata:
  node_type: memory
  type: project
  originSessionId: df787827-5be4-4157-830a-70b9c26f7815
  modified: 2026-10-05T02:22:54.611Z
---

To bring the user's Pi (192.168.0.7, [[pi-remote-access]]) up to date after a publish:

1. `pkg check` — refreshes the index (without it `pkg update -a` works from a stale index: "everything is
   up to date", or `pkgs/onyx-<old>.opk: HTTP 404` when a newer publish replaced the file).
2. `pkg update -a` — installs everything; the `onyx` package is only **staged** ("restart to finish").
3. `reboot`, wait ~100 s, then `pkg check` again ("everything is up to date").

**Why:** on 2026-10-05 the Pi's `onyx` package had stayed at 2026.10.59 for hours while I published .60 to
.66: `/bin` and the Control Panel applets were old (the printer applet still looked for `print.so`, which
the `printerkit` package had removed). Files I had sent by FTP for my own tests hid the problem.

**How to apply:** after publishing something the user will try on the Pi, do the three steps (the user
asked for the full update through the package manager on 2026-10-05; a reboot interrupts what they are
doing on the Pi — say so when they are clearly using it). Check `pkg list` shows the versions just
published, not only that my FTP-sent test binaries work.
