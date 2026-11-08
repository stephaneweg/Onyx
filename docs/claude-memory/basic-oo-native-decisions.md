---
name: basic-oo-native-decisions
description: "Onyx BASIC — the user's decisions of 2026-10-05 on objects (CLASS / TYPE) and on the native AArch64 back end (form, activation, the \"Managé\" option)"
metadata:
  node_type: memory
  type: project
  originSessionId: 74344419-3955-4412-8f40-4bfd205d753d
  modified: 2026-10-05T02:00:52.175Z
---

Decided by the user on 2026-10-05 (session on `user/basic`):

- **Objects**: `CLASS` is a reference type (C#'s class), `TYPE` stays a value **and only holds data — no
  methods on a TYPE** (asked explicitly: "TYPE ne servant qu'à définir des structures de données").
  Methods are written outside the block (`VIRTUAL` / `OVERRIDE` / `ABSTRACT SUB Class.Name`).
  Options taken: `ABSTRACT`, destructor (`SUB Class.delete`). Not taken: `PRIVATE`.
- **Native execution (phase B)**: form **(a) then (c)** — first native code produced at load by
  `/bin/basic`, then "Make App" producing a standalone executable (`apps/<name>.app/main` = the
  pre-linked runtime + the program attached). Form (b), native code saved in a compiled file, was
  not chosen.
- **Activation**: native **by default at once**, the VM as the fallback for what the translator
  cannot do; an option **`-m`** runs on the VM, shown in the compile dialogs as **"Managed"**
  (English, like the rest of the UI: the user confirmed on 2026-10-05 that the French word is not
  needed — a localisation project will come later, opened by the user).
- Acceptance tests, in this order: one demo (a simple example or `basicdemo`), then Arkanoid.
- **Done since (2026-10-05, asked by the user)**: SUB / FUNCTION calls, returns, by-reference
  arguments and the numeric functions in machine code.
- **Strings stay on the VM for now** (the user, 2026-10-05: "garde juste en mémoire" — an idea kept,
  not a task; do not start it nor measure it unasked). What I told the user: there is no GC, strings
  are reference-counted in the runtime; their work is native C++ already, the VM only adds its
  dispatch (~35 ns), so the gain would be small (my estimate ×1.3 to ×2, not measured) and a
  reference-count bug in machine code is costly. If a real program needs it one day: keep the
  runtime's C++ string routines and call them directly from the machine code (as for SUB calls),
  after a string benchmark on the Pi in both modes. Also still on the VM: GOSUB, virtual and
  interface calls.

**Why:** the user wants a compiled BASIC beside the VM; measured the same day: 35 ns per VM
instruction on the Pi 4, pure computing 98 % in the VM, Arkanoid only ~50 000 instructions per
second (native will not show there — said to the user, who still chose to go on).

**How to apply:** do not re-ask these; do not re-add methods to TYPE; keep the VM working (`-m`).
See [[pi-remote-access]] for testing on the Pi (another session may be using it at the same time).
