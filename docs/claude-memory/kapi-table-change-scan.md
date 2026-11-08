---
name: kapi-table-change-scan
description: "Before shipping a change of the kernel's kapi table, scan the card's programs for direct reads of KAPI_TABLE_VA — AppKit only protects the programs really built through it"
metadata:
  node_type: memory
  type: project
  originSessionId: 49f841bb-7dec-4714-9ecb-a702f4092cfe
  modified: 2026-10-05T09:42:12.505Z
---

"No program is rebuilt when the kernel's table changes" (AppKit, kapi v86) holds only for programs
that reach the kernel through AppKit's stubs. On 2026-10-05, while compacting the table (kapi v87,
branch `kapi-compact`), 14 programs staged in `sdcard/` still read the kernel's table themselves:
the POSIX ports of `/bin` (curl, zip, unzip, sqlite3, xmllint, jsc, skiademo, skiatest, hbtest,
icutest, imgtest, stkpoc: linked with a libonyxposix older than AppKit), `jet.app/main`, and
`pdf.app/main` (a stale `onyx_mucompat.o`). `el0test`, `faulttest` (KAPI_INLINE) and `appkit.so`
do it on purpose.

**Why:** a moved slot makes such a program call the wrong system call, silently.

**How to apply:** before publishing a kernel whose table moved, scan every ELF of `sdcard/` for
`mov xN, #0x380000000`, which gcc emits as `orr xN, xzr, #0x380000000` = the word `0xB2610BE0 | N`
(little-endian bytes). AppKit's stubs use movz/movk to 0x380008000 and do not match. Anything
that matches besides appkit.so, el0test and faulttest must be rebuilt (clean objects) first.
Related: [[jet-package-needs-local-binary]], [[wsl-webkit-build-env]].

**Refinement (2026-10-05, kapi v90, branch UserSpaceElegant):** the word alone gives false positives.
`mov xN, #0x380000000` followed by `movk xN, #0x8000` (word `0xF2900000 | N`) is AppKit's own table
(0x380008000: BASIC's `onyxKitOpen`, fine); followed by `ldr xM, [xN, #off]` it is a real read of the
kernel's table (slot off / 8). A worktree's `sdcard/apps/jet.app/main` can be an old link (the one of
04:54 read 118 slots; the one of 13:54 in `C:\Projects\Onyx` reads none): scan the Jet that will be
packaged, not just any copy.
