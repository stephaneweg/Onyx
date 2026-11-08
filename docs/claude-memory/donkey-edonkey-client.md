---
name: donkey-edonkey-client
description: Donkey (eDonkey client, 2026-10-08) -- the user's choices, what is waiting on him (live-network tests, Kad, embedded player, signing key)
metadata:
  type: project
---

Donkey, the eDonkey2000 client asked on 2026-10-08 (docs/donkey/README.md, docs/HANDOFF.md). The user's choices: the name
"Donkey"; Kad wanted from the first version (his "oui" to "Kad dès la première version ?" -- my reading); obfuscation
left to me; protocol formats in NetKit, app in user/Apps/donkey; eMule-like tile bar. Asked while I built it: preview of a
video as soon as enough is downloaded, minimise to a menu-bar icon, a `/boot` option (start with Onyx minimised,
reconnect, resume downloads) -- all three done.

**Why:** Kad is still a stub and the engine has only talked to itself (PC test) and been started on the Pi.

**How to apply:** before any test on the real eDonkey / Kad network, ask the user (his home address joins a public P2P
network; which server.met / nodes.dat) -- asked on 2026-10-08, check whether he answered. Also asked: Preview opens the
Media Player (a separate program); a player *inside* Donkey would link FFmpeg and put Donkey under GPL-2.0 -- his call.
That session had no package signing key ([[pi-package-update]]): donkey / netkit 1.49 were committed but not published.
