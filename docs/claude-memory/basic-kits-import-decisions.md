---
name: basic-kits-import-decisions
description: "BASIC's #import of kits — the user's choices of 2026-10-05 (flat C exposure for C++ kits, nothing checked, the program frees what a kit returns), the agreed order, and what to ask before UIKit"
metadata:
  node_type: memory
  type: project
  originSessionId: ed7ab39a-24d3-47ba-a25b-3ba30b8d7a64
  modified: 2026-10-05T16:46:24.726Z
---

The user asked (2026-10-05) for BASIC to import kits by name, not frozen (`#import uikit`, case-insensitive,
a new kit usable without touching the VM), and accepted my plan and order: (1) `.bi` descriptions +
`#import` + generic call, (2) structures (`TYPE` <-> `struct`), (3) callbacks, (4) UIKit.

Their choices, in their words' sense:
- C kits as they are; **for the C++ kits a flat exposure is fine** ("on manipule principalement des
  handles, pas des objets").
- Callbacks: the kit calls the BASIC SUB through a pointer (done with relays, `ADDRESSOF`).
- Safety: **the runtime checks nearly nothing** ("c'est pareil dans FreeBASIC"); **a text / buffer a kit
  returns is always the developer's to free** — never freed by BASIC behind their back.

Done and in `main` that day: steps 1 and 3 (docs/HANDOFF.md, *the kits by #import*). My own additions,
not asked: qualified names `Kit.name` (prefix stripped, C name also accepted), `BYREF`, `ALLOC` /
`DEALLOC` / `CSTR$` / `PEEKx` / `POKEx`, words gated on the presence of an `#import`.

**Why:** the user wants BASIC to draw on the kits like any program ([[kits-first-rule]]) without a list
of kits compiled into the language.

**How to apply:** keep `user/Libs/basic` free of kit names (only AppKit's fixed table is special-cased in
`baskits.cpp`). Step 4 adds functions to UIKit's append-only table and must coexist with the runtime's
own window: propose the flat layer's shape to the user before writing it. See [[basic-oo-native-decisions]].
