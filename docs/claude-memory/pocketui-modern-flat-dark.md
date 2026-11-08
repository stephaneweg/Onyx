---
name: pocketui-modern-flat-dark
description: "PocketUI is to be modernised flat and dark for the Waveshare PocketTerm35 (640x480, 3.5\"); the user's requirements; mock-ups before code"
metadata:
  node_type: memory
  type: project
  originSessionId: 5b504b96-f714-41fd-aa65-b9c7c658ec73
  modified: 2026-10-11T18:29:48.956Z
---

On 2026-10-10 the user dropped the home-made wooden "devterm" (studies and three CAD models left uncommitted in
`hardware/ardoise/`) and chose to buy a **Waveshare PocketTerm35**: Pi 4 or Pi 5 inside (both frames supplied; he
starts with a Pi 4), 3.5" IPS 640 x 480 said HDMI, D-pad + A/B/X/Y + L/R + Start/Select, a 67-key QWERTY thumb
keyboard, touch (no Onyx driver). Not received yet: how its keys and buttons enumerate over USB is unknown.

His decisions for PocketUI on it (pocket mode, not console mode):
- a **modern look: flat and dark**;
- the **Select button puts the focus on the menu bar**, the menus are then walked with the arrows;
- a **toolbar must be responsive** when it does not fit the window's width (Letters at 640 px is clipped today).

Later the same day and on 2026-10-11 (direction 1 "Onyx Noir" built on the branch `PocketTerm`, worktree
`C:\Projects\Onyx-PocketTerm`, with a Windows test client `pc/PocketTermSim` over rdpd; nothing merged into main,
no package published, nothing run on a Pi):
- Start and Select must stay free for games: **the CMD key opens the app's menu**; Start / Select go through the
  gamepad path, the shell binds nothing to them.
- When the device arrives, **he wants a live debug session** to see how the top controls (D-pad, A/B/X/Y, L/R,
  Start, Select) reach Onyx: through the keyboard driver or as a (virtual) USB gamepad. Decide the mapping then.
- The device ships without its battery (the seller): a 3.7 V 5000 mAh 955565 LiPo with a JST PH 2.0 plug; check the
  plug's polarity before connecting one.

- **The desktop mode must stay exactly as it is** (the user, 2026-10-11): what this work changes is the pocket
  display only -- every change conditional on the pocket mode / size class, and proved by comparing desktop pictures.
- Trap found on his Pi 400 that day: code under `#ifdef UK_PORT_POCKET` is NOT in the device's `lib/pocket/uikit.so`
  (it shares every object but `port.o` with `lib/uikit.so`); the simulator compiles everything with the flag and
  hides it -- decide by the server's mode at run time, and try on the Pi (192.168.0.10, telnet + ftpd + VNC capture).
- Without a mouse (built 2026-10-11): Alt + Up / Down scrolls, F6 rings the toolbar, Ctrl + PgUp / PgDn (L / R) step
  tabs or a SidePanel's places one by one, CMD or F10 the Menu. Apps are to be made responsive at 640 (Mail in three
  screens, Circuits, GPIO Lab, Turtle Quest; an audit of the others; a UX designer's mock-ups where a redesign is needed).

**Why:** the Milk light chrome does not match the black device, and at ~229 ppi the 1x UI is tiny; Letters under
PocketUI at 640 x 480 (simulator, `~/p7/sim640` in WSL) shows a clipped format toolbar and a page wider than the screen.

**How to apply:** mock-ups first ([[mockup-first-for-apps]]) in `docs/compact-shell/mockups/pocketterm/`; no OS code
until he picks a direction. Prefer changes every app inherits (UIKit theme, ToolBar overflow) over per-app work.
See [[pocketui-p7-local-setup]] for the simulator.
