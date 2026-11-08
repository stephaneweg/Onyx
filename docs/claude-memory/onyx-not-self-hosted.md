---
name: onyx-not-self-hosted
description: "Onyx will not be self-hosted — no GCC / binutils / Clang port; development stays cross from a PC (the user, 2026-10-05)"
metadata:
  node_type: memory
  type: project
  originSessionId: f3dcc179-db32-4457-8d9e-aeb35864c22f
  modified: 2026-10-05T15:55:24.009Z
---

The user decided on 2026-10-05 (worded "je crois qu'on va acter") that Onyx will **not be self-hosted**: no port of
GCC, binutils or Clang to Onyx. Onyx stays a simple OS with "limited" resources; programs are built on a PC with the
cross toolchain.

**Why:** the user's reasoning — it keeps the OS simple, and not being self-hosted does not make Onyx less advanced
than SerenityOS. It came after a discussion of what a GCC port would cost (sub-process spawning for the driver,
binutils, a canadian-cross build, cc1plus's memory, FAT).

The user's further points the same day: the cross toolchain already proves apps can be ported to Onyx; ordinary
users never compile anything; BASIC's role is the on-device language that is simple to learn for people who are not
programmers to begin with (programmers compile on big machines).

**How to apply:** keep BASIC easy to learn for non-programmers when designing its features. do not start or re-propose a native C/C++ toolchain port. On-device programming is BASIC
([[basic-oo-native-decisions]]). TinyCC was mentioned as a small alternative and the user did not take it up — my
reading is that it is covered by the same decision; ask before proposing it again. Same kind of scoping decision as
[[onyx-stays-single-user]].
