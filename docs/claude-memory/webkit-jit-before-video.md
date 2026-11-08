---
name: webkit-jit-before-video
description: "In the WebKit port, the JavaScript JIT comes before video / media (the user's ordering)"
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-02T20:59:44.954Z
---

The user prefers that the **JavaScriptCore JIT be done before video** in the WebKit port (said
2026-10-02, after hearing that YouTube's pages would be slow without a JIT and that video is a
later step).

**Why:** without a JIT, heavy sites (YouTube's several MB of script) start slowly and feel sluggish
on the Pi 4; the user wants JavaScript speed first, media after.

**How to apply:** order the work after WebKit2 (step 3) as: JIT, then media
(`MediaPlayerPrivate` on `user/av`, MSE). The JIT needs kernel work first — executable memory for
user mappings (`PROT_EXEC` through `vm_map` / `vm_protect`, W^X, instruction-cache flush from EL0:
a kapi change to validate on the Pi) — then `ENABLE_JIT` in `OptionsOnyx.cmake` (the Baseline tier
first), JSC's executable allocator on it, and a check of what the JIT tiers need from thread
suspension / signals, which Onyx does not have (`Thread::suspend` is fatal there; see
`docs/08-WEBKIT-PORT.md`, "Two things Onyx does differently"). The V3D compositor comes BEFORE the JIT
(the user's full order, given later the same day: [[webkit-roadmap-order]]). YouTube is the reference site for these steps.
See [[webkit-graphics-roadmap]].
