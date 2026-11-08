---
name: webkit-graphics-roadmap
description: "The user's decided order for the WebKit browser's graphics — CPU first, then an Onyx V3D compositor, WebGL later through an ANGLE back end of our own (not a full Mesa port)"
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-02T18:42:57.147Z
---

Decided by the user on 2026-10-02, for the WebKit port (see `docs/08-WEBKIT-PORT.md`):

1. **First a browser rendered on the CPU, without GL** (Skia raster; no Mesa, no EGL).
2. **Once that has worked well for a few days of use: a compositor on the V3D** (Onyx's own kernel
   V3D driver, as Jet does with `user/gpucomp`) — not before.
3. **WebGL will become a goal eventually. The user's choice of route: an ANGLE back end of our own
   driving Onyx's V3D kernel driver directly, with only Mesa's shader compiler library
   (SPIR-V → NIR → QPU) behind it** — NOT a full Mesa port (no Gallium `v3d` driver, no emulation
   of Linux's DRM UAPI, no Mesa EGL). Keep it "in the boxes": do not start it, but do not make
   choices that close the door on it.

**Why:** the user wants the fewest interfaces and the most direct path ("réduire au maximum les
interfaces, le plus direct"), even though it means writing the GL-state → control-list driver
ourselves. ANGLE alone never produces QPU code, so Mesa's compiler is still needed. Mesa / GPU work
can only be debugged on the real Pi (through the user) and is not needed to get pages on screen.

**How to apply:** in step 2 and after, configure WebCore without TextureMapper / EGL / libepoxy;
write the docs' plan in that order; when a design choice would make a later GL back end harder
(e.g. hard-wiring CPU-only paths where WebKit has a GL abstraction), prefer the one that keeps both
possible, and say so. When WebGL starts: study ANGLE's back-end interface (`libANGLE/renderer`,
the null back end as the skeleton) and how to build Mesa's `src/compiler` + `src/broadcom/compiler`
alone, before any code. More generally the user prefers direct designs over emulation layers.
