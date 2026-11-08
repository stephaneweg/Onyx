---
name: webkit-roadmap-order
description: "WebKit port order and where it stands — browser, JIT, V3D compositor (default since Web 1.0.6) and multi-core tile raster done on 2026-10-03; next video, host+webview, lazy loading"
metadata:
  node_type: memory
  type: project
  originSessionId: 001d9be7-ac4e-42a7-a2f2-d967f5762ebd
  modified: 2026-10-03T16:19:34.094Z
---

The user's order for the WebKit port: 1) a usable browser (kotonstudio.com near-instant on the Pi); 2) the compositor on the V3D; 3) the JIT; 4) video; 5) host window + reusable web view; 6) lazy loading.

State on 2026-10-03 (in `main`, published, on the Pi):
- Step 1 done: Web with select lists, clipboard, find, downloads window, context menu; Mail hosts the web view.
- JIT done: kernel v78 `PROT_EXEC`, jsc 1.1.0 and Web with Baseline + DFG. The shipped Web is linked from `~/webkit-build/webkit-jit` (`BUILD=… sh tools/webkit/build-web.sh`); `~/webkit-build/webcore` is the non-JIT tree for quick compile checks.
- Compositor done (WebKit patches 0018–0020: own GraphicsLayer on gpucomp, animated opacity/transform layers, groups, tiles rasterised on the app cores). **The user asked for the GPU compositor to be the default** ("peut tu mettre gpu par défaut ?", 2026-10-03): Web 1.0.6; `web --nogpu` or the file `SD:/etc/web-nogpu` goes back to the software path; Mail's web view stays on the software path.
- Web's link now uses newlib's malloc everywhere (`ONYX_HOSTED_NEW`): wtk's weak malloc and onyxpp.hpp's operator new used to win, which caused the old Skia heap corruption.

**Why:** the user judged the compositor good enough after the multi-core raster made scrolling on kotonstudio.com 3–4 times faster.

**How to apply:** next in the user's order is video (step 4: `MediaPlayerPrivate` on `user/av`, MSE), then host window + web view, then lazy loading. Open compositor items in docs/08: the window canvas shared with the web process (kernel addition), 3D matrices in gpucomp, composited filters/masks; open JIT items: FTL / WebAssembly JITs, the JSON + OSR-entry slowdown in bench.js. See [[webkit-graphics-roadmap]] and [[webkit-jit-before-video]].
