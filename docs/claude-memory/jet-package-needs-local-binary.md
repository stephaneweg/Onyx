---
name: jet-package-needs-local-binary
description: "Jet's 105 MB program is git-ignored; publishing from a clone without it makes an empty jet package (happened 2026-10-04: jet 2.0.2 = 2.6 KB)"
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-04T14:53:34.548Z
---

`sdcard/apps/jet.app/main` (WebKit, 105 MB) is ignored by git and exists only in the WSL clone
`~/src/Onyx` where Jet is built ([[wsl-webkit-build-env]]). `tools/pkg/publish.sh` packages `sdcard/`
as it is staged: run from a clone that lacks that file, it publishes a `jet` package without its
program. On 2026-10-04 another session published `jet 2.0.2` that way (the .opk: 2.6 KB instead of
~43 MB); I republished `jet 2.0.3` from the WSL clone.

**How to apply:** after any publish, look at the size of `pkgs/jet-*.opk` in `~/src/onyx-packages`
(~43 MB); if `jet` was bumped by a publish I did not make, republish it from the WSL clone. Before a
publish, check that the merge of `origin/main` really happened (read its whole output, not its last
line: a failed merge once made my publish drop two new packages — caught before the push).
