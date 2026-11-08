---
name: sudo-commands-to-user
description: "For anything needing sudo/root in WSL, give the user the command and let them run it"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-02T16:45:22.241Z
---

For everything that needs `sudo` (apt installs, `/opt` directories…), give the user the exact command
and they run it themselves; then continue once they confirm.

**Why:** the user said so explicitly (2026-10-02, WSL setup for the WebKit port); `sudo` asks for
their password.

**How to apply:** one self-contained command in a `bash` block, checked beforehand (e.g. package
names with `apt-cache policy` — a single unknown package makes apt install nothing). Do not work
around it with `wsl -u root`. See [[wsl-webkit-build-env]].
