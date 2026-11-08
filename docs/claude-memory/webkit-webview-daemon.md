---
name: webkit-webview-daemon
description: "The user wants, at a later stage, a web-view daemon started at boot that launches the UI / web / network roles when a window asks for a web view"
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-02T21:11:21.637Z
---

Asked by the user on 2026-10-02 ("en étape ultérieure"): a **daemon started at boot** that does
nothing until a window asks for a web view, then starts the UI, web and network roles for that
window.

**Why:** the 80 MB engine image takes about 5 s to read from the SD card; the user wants a web
view (browser, Mail) to open at once.

**How to apply:** it belongs to roadmap step 5 (host + web view) or after — not before; see
[[webkit-roadmap-order]] and [[webkit-webview-reusable]]. It relies on the kernel's shared
read-only image (docs/ELF-LOADER-PLAN.md stage c): the daemon holds the image so later processes
map it without reading the card. Natural extras: one network process shared by all views, one
prewarmed spare web process. Cost: ~80 MB held from boot (make it optional on a 1 GB Pi; stage d
gives the same from the second start). Onyx has no `fork` today; **the user decided
(2026-10-02, "on implémentera le fork nécessaire") that a kernel `fork` will be implemented for
this**, making the daemon a zygote (engine initialised once, each role a clone). Needs: shared
read-only image, copy (or copy-on-write) of data / heap / the calling stack, descriptors
duplicated, an append-only kapi addition; constraint: fork before the zygote starts any thread.
Measure first how much start-up is initialisation a zygote can do ahead. Recorded in
docs/08-WEBKIT-PORT.md, roadmap step 5.
