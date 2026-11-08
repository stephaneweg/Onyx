---
name: webkit-browser-ui-decisions
description: "The user's choices for the WebKit browser's window: Milk theme mock-up approved, no menu button in the toolbar, target=_blank opens a new window (a new launch), autonomy to deliver the browser overnight"
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-03T03:15:40.092Z
---

Decided by the user on 2026-10-03 after seeing the Milk-theme mock-up (a wtk window: back / forward
/ reload / home / history, the address field, the page, a status bar; the Onyx menu bar on top):

- **No menu button in the toolbar** (the Onyx menu bar carries the menus).
- **`target=_blank` / `window.open` open a new window** — a new launch of the browser program
  ("it is already preloaded, so it should not be heavy": the kernel's preload, docs/ELF-LOADER-PLAN.md).
- The name shown in the menu bar ("Jet" in the mock-up) was not decided.
- **Autonomy (2026-10-03, the user going to sleep)**: "do everything up to a deliverable browser";
  treat the coming tests as provisionally valid; the user will test the whole browser on the Pi at
  once when back; add kmsg / log traces to diagnose problems.

- **Next work asked (2026-10-03 morning, then "I let you go on while I sleep")**, in order:
  1. `<select>` drop-down lists; 2. the clipboard with the other apps, + **Ctrl+F** (find in page);
  4. **downloads** with a **small non-modal popup**: each download's progress and, if possible,
  Cancel; plus **right click → "Save as…"** on a link, an image or other; then **the JIT**
  (roadmap step 3), then **the V3D compositor** (step 2). Autonomy as before (test on the Pi
  over telnet/VNC, publish).

**Why:** the user wants one test session of a complete browser rather than step-by-step checks.

**How to apply:** keep going without waiting for Pi tests between steps; make the browser log what
it does (kernel log / a log file) so a failed Pi run can be diagnosed; give the user a clear test
procedure. See [[webkit-roadmap-order]], [[webkit-browser-no-tabs]], [[webkit-graphics-roadmap]].
