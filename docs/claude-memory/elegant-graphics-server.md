---
name: elegant-graphics-server
description: "Elegant = the graphics server in a user process (the user's name for it); branch UserSpaceElegant, the user's decisions, how it is deployed and tried on the Pi"
metadata:
  node_type: memory
  type: project
  originSessionId: 5cd9c338-7aa0-48a6-8ac0-3b34e9a3d68b
  modified: 2026-10-05T18:30:35.287Z
---

**Elegant** is the graphics / window server moved out of the kernel into a user process (`SD:/bin/elegant`,
`user/Servers/elegant/`). The user named it and asked for the work on 2026-10-05. Where it stands, the
design and the stages: the first section of `docs/HANDOFF.md` (read it first).

The user's decisions (2026-10-05):
- Work **only on the branch `UserSpaceElegant`**; merge it into `main` only when the user says so.
- The protocol AppKit <-> Elegant is **private to AppKit** (`user/Kits/appkit/elegant.h`); what Elegant
  brings that is new is shown to programs by UIKit. (The user first leaned towards UIKit, then chose AppKit.)
- The kernel keeps a minimal console as fallback and restarts Elegant if it dies; the whole window
  manager leaves the kernel.
- Migration behind a **one-boot trial file** (`SD:/etc/elegant.trial`), then default, then removal.
- The per-process event queue stays in the kernel (Elegant pushes into it).
- `rdpd` (Onyx Remote) stays a separate process, with access to the windows' content and to the injection
  of clicks and keys; Onyx Remote must keep working unchanged.

**How to apply:** my own choice, not the user's: no package is published from that branch (it would put
branch code in the public repository) -- test builds go to the Pi with
`python tools/tests/elegant/pi_deploy.py [--trial]` (kernel built in WSL `~/src/Onyx-elegant`). Putting a
kernel on the Pi was refused by the session's auto-mode classifier until the user switched the session to
bypass-permissions mode; expect to need that again. See [[pi-remote-access]], [[wsl-webkit-build-env]].
