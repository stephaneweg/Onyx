---
name: uikit-rename
description: "wtk was fully renamed UIKit on 2026-10-05 (the user's choice: 'kits' by use, as BeOS): user/uikit, namespace uikit, uk_*, uikit.so"
metadata:
  type: project
---

The widget toolkit **wtk is now UIKit** (the user, 2026-10-05: "kits" by use, like BeOS's): `user/uikit/`
(`uikit.h`), `namespace uikit`, functions `uk_*`, macros `UK_*`, types `Uk*`, `ft/uikitface.h`,
`SD:/lib/uikit.so`, package `uikit` (replaces `wtk`). Siblings: **AudioKit** (`user/audiokit`, `ak_*`),
ImageKit planned (`ik_*`). Older memories, commits and notes say wtk / wk_.

**How to apply:** use the new names; a new shared library is a "<Use>Kit". Jet's WebKit side keeps its own
`WK*` names (WebKit's, not ours). The demo app is still called `wtkdemo`.
