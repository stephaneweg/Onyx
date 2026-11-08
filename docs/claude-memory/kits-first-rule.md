---
name: kits-first-rule
description: "Standing rule (the user, 2026-10-05) — one kit per domain; put reusable code in the right kit (create one if needed) and build apps on the kits, not on loose headers or the kernel"
metadata:
  node_type: memory
  type: feedback
  originSessionId: df787827-5be4-4157-830a-70b9c26f7815
  modified: 2026-10-05T12:31:26.910Z
---

The user's standing rule for all development to come, said on 2026-10-05 after the kits were put in place:

"au lieu d'avoir une pléthore de .h et de librairies potentiellement à lier aux apps, et plutôt que d'avoir un couplage fort avec le noyau, on va avoir un Kit par domaine (UI, police, système, appli, son, réseau, etc.) […] il faudra essayer d'intégrer un maximum de code réutilisable dans le kit adéquat (en créer un si besoin) et essayer de puiser au max dans ces kits."

- **One kit per domain**: AppKit (make a program run: the link to the kernel, strings, console, ini, program starting), UIKit (widgets, icons), SystemKit (talk to the system / other apps), NetKit (network), FileKit (files, archives), ImageKit (pictures), AudioKit (sound), FontKit (fonts), PrinterKit (printing); DocumentKit to come. See [[user-tree-layout-and-headers]].
- **New reusable code goes into the adequate kit** — not a new shared header in `user/Include`, not a copy in an app. No kit fits: propose / create one.
- **Apps draw on the kits as much as possible** and never couple to the kernel (no `KT`, no table reads; only AppKit does).
- A helper used by one program stays beside that program.

**Why:** fewer loose headers and libraries to link, no tight coupling to the kernel, one place per domain.
**How to apply:** before writing a helper, look for it in the kits; when adding a feature that two programs could use, add it to the kit (header = declarations, code in the library, `.abi` append-only, package version raised) and say which kit in the docs (docs/03 §5.6–5.10). My reading, not the user's words: C console tools link the kits through `lib/<kit>.imp_c.a`.
