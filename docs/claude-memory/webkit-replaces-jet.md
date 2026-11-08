---
name: webkit-replaces-jet
description: "Done 2026-10-04 — NetSurf's Jet removed, the WebKit browser is Jet (user/Apps/jet, package jet 2.0); what keeps the old \"web\" name"
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-04T10:19:27.301Z
---

Done on 2026-10-04 (Onyx commit `8bfd7811`, the user's explicit go-ahead): NetSurf's Jet, its libraries
(`third_party/netsurf`, libcss, libdom, quickjs-ng…), its PC bench, Jet for Windows and docs 06/07 were
removed; the WebKit browser, first called Web, is now **Jet**: `user/Apps/jet`, `SD:/apps/jet.app`,
package `jet` 2.0.x, `run jet`. The package `web` is no longer made.

Kept on purpose under the old name: the log prefixes `web:` / `webview:`, the switch files
`SD:/etc/web-*`, the script `tools/webkit/build-web.sh` and its output `$BUILD/bin/web`, WebKit's user
agent. The codecs (libvpx, dav1d, opus) are built by `user/av/Makefile`.

**How to apply:** do not look for NetSurf code or docs/06, docs/07; the browser's document is
`docs/08-WEBKIT-PORT.md`. The last commit with NetSurf in the tree is `3e2eb270`.
See [[webkit-roadmap-order]].
