---
name: webkit-browser-no-tabs
description: The Onyx WebKit browser must not have tabs — one page per browser window
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-02T20:46:44.400Z
---

The user does not want several tabs in the browser (said 2026-10-02, while step 3 of the WebKit
port — WebKit2 — was being planned): one page per browser window.

**Why:** the user's product choice (stated without further reason; it came up when discussing that
WebKit2 starts one web process per tab / site and that each 80 MB process image is slow to load
and costly in memory on the Pi).

**How to apply:** design the browser UI and the WebKit2 embedding for a single page view per
window: no tab strip, no tab model, no per-tab process bookkeeping; "open in new tab" /
`window.open` / `target=_blank` need another answer (same view, or a new browser window — ask
when it comes up). Budget processes as one UI + one web + one network process per browser. Do not
propose tabs as a feature. See [[webkit-graphics-roadmap]] for the other decided constraints.
