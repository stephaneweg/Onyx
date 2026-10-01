# Onyx NetSurf against Ladybird, Chromium (Blink), WebKit (Safari) and Opera

What the Onyx browser still lacks next to the big engines, why, how to close each gap, its
cost and its order. Opera is Chromium (Blink + V8) with its own shell; "Chromium" below stands
for Chrome, Edge, Opera, Brave and Vivaldi. Ladybird is the independent engine (LibWeb +
LibJS) written from the specifications since 2022. State of 2026-09-30 (the end of the
"improve NetSurf" session, docs/06 §12-§20).

Scores on the same page copies, PC bench: css3test.com 81 % (Chromium 71 % on that copy --
the test counts what is parsed); html5test 369 / 588 (Chromium ~ 530); html5lib tree
construction 100 %, tokenizer 99.9 %; WPT url 896 / 896; WPT shadow-dom ~ 60 %; test262
intl402 subset 92.6 %.

Cost: S = days, M = a week or two of focused work, L = a month and more, XL = a project of
its own. Priority for Onyx (a Raspberry Pi 4, 4 cores, 4-8 GB, the framebuffer): P1 = pages
break without it, P2 = pages look or feel wrong, P3 = missing features, P4 = not worth it now.

## 1. Rendering and CSS

| Gap | Ladybird / Chromium / WebKit | Onyx today | How to close it | Cost | Prio |
|---|---|---|---|---|---|
| ~~Compositing: `opacity` on a subtree, stacking of translucent groups~~ | all | **done (docs/06 §21)**: a group, a stacking context, blended in place (exact, one pass); nested, positioned, inline, the hover's partial redraws. Left: a layer kept between redraws | cache the layer until its subtree changes | S | P3 |
| ~~`transform` (rotate / scale / skew / matrix, 3D flattened)~~ | all | **done (§21)**: painted apart (black and white passes, one when opaque), drawn through the matrix, bilinear; `transform-origin`, the individual properties, 3D flattened; hit test and `getBoundingClientRect` through it; translations still the layout's. Left: a real `perspective` | a projective composite (the layer mapped through a 3x3 homography) | S | P3 |
| ~~`filter` / `backdrop-filter` (blur, brightness, drop-shadow...)~~ | all | **done (§21)**: every filter function but `url()`, `backdrop-filter`, the separable `mix-blend-mode`s; a large blur on a reduced layer. Left: `filter: url()`, the non-separable blend modes, `isolation` | SVG filter primitives on the layer | M | P3 |
| ~~Transitions and animations (`transition`, `@keyframes`, Web Animations API)~~ | all | **done (docs/06 §22)**: a timeline per content interpolates the computed values each frame (colours, lengths, opacity, transforms, shadows), redraws the animated boxes' rectangles when paint-only, lays out otherwise; the events; `element.animate()` / `Animation` / `getAnimations()` on the same engine; `requestAnimationFrame` paced by the frames (~60 Hz, slower when frames are long, none when idle) | left: pseudo-elements' animations, reversing shortening, keyframes recomputed on a base change, `composite` | S | P2 |
| **Incremental restyle and relayout** | all (dirty bits per node) | **restyle done (docs/06 §26)**: the style selections kept from one box tree to the next (marks from the mutation events, sheets added or taken out probed, a check mode), attribute-only changes restyled in the boxes (redrawn, or laid out without a rebox), reboxes coalesced / throttled, none for changes in `display: none` subtrees; the flex layout memo (nested flex: once per inputs instead of 2^depth). Left: a rebox still builds every box (~3 µs an element on the PC), a layout still lays out the whole tree | incremental box construction (the changed subtrees' boxes re-attached: anonymous boxes and inline containers make it delicate); relayout from the nearest box whose size cannot change (a formatting-context root with a fixed size), the dirty boxes and their ancestors only | M | P1 |
| `position: fixed` / `sticky` in every case | all | fixed and sticky exist (z-layers), some cases wrong | layoutdiff pages per case | S | P2 |
| **Text shaping (ligatures, kerning, Arabic, Indic, Thai...)** | HarfBuzz (Chromium, WebKit, Ladybird) | FreeType glyph by glyph | vendor HarfBuzz (C++, ~1 MB, no exceptions needed) in `font_freetype.c`: shape each run, cache shaped words | M | P2 |
| **Bidirectional text (RTL: Arabic, Hebrew)** | ICU / own | none (`direction` parsed) | Unicode bidi algorithm (fribidi, or the small `unicode-bidi` reference) on each line's runs; mirrored layout of the line boxes | M | P3 |
| **Colour emoji** | all | monochrome fallback | FreeType's `FT_LOAD_COLOR` (CBDT / COLRv0-1 via its `FT_Get_Color_Glyph_Layer`), a Noto Color Emoji subset on the card | S | P2 |
| Line breaking by UAX #14, hyphenation (`hyphens: auto`) | all | spaces and a few punctuation cases | libunibreak (small C); hyphenation dictionaries later | S | P3 |
| Vertical writing modes | all | none | after bidi; rarely needed on Western pages | L | P4 |
| Multi-column layout (`columns`) | all | parsed | column balancing in layout.c | M | P3 |
| `@container` queries | all | parsed, not evaluated | evaluate at layout (needs incremental relayout) | M | P3 |
| Subgrid, masonry | Chromium / WebKit | grid without subgrid | layout_grid.c | M | P3 |
| `clip-path` on HTML boxes, `mask` beyond images | all | SVG clip-path; mask-image on images (layout agent) | through the compositing layers (docs/06 §21: a group's layer multiplied by the clip's coverage before it is drawn) | M | P3 |
| Scrolling: smooth, momentum, `scroll-snap`, `overscroll-behavior`, scroll-driven animations | all | wheel steps, overflow scrollers | a scroll animator per scroller; snap points at the end | M | P3 |
| High-DPI / zoom | all | 1:1 | a device-pixel ratio through layout (CSS px -> device px) | M | P3 |
| Printing, PDF export | all | none | NetSurf had a PDF plotter upstream | M | P4 |

## 2. JavaScript engine

| Gap | Others | Onyx today | How to close it | Cost | Prio |
|---|---|---|---|---|---|
| ~~**WebAssembly**~~ -- done (docs/06 §27) | all | the standard API on wasm3 (an interpreter, bound to QuickJS: `Module`, `Instance`, `Memory` over the linear memory, `Table`, `Global`, imports / exports, i64 as BigInt, traps as RuntimeError, the streaming forms), in pages and workers; 3.5-9x slower than V8 on the PC. Left: SIMD, threads / shared memory, `WebAssembly.Tag` / `Exception`, JSPI | speed: an AOT / JIT for AArch64 (WAMR fast-JIT / AOT, or a template JIT of wasm3's operations) -- see the JIT row | M | P2 |
| **JIT (speed)** | V8 / JSC / LibJS (bytecode interpreter + JIT on x86-64 only) | QuickJS-ng bytecode interpreter: 20-50x slower than V8 on heavy scripts (a React hydration: seconds on the Pi) | short term: profile-guided fixes in the natives (dom.js's hot paths in C: the DOM wrappers, the selector engine, innerHTML), QuickJS's inline caches (quickjs-ng has shape caches: keep them warm), avoid megamorphic dom.js code; long term: a baseline JIT for AArch64 in QuickJS (XL) | M / XL | P1 |
| Engine conformance (test262 core) | V8 ~99 %, LibJS ~95 % | QuickJS-ng ~ 99 % of ES2024 | follow quickjs-ng releases | S | P3 |
| Memory: generational GC | V8 / JSC | refcount + cycle collector, 384 MB limit | fine for the Pi; watch leaks | - | P4 |

## 3. DOM and Web APIs

| Gap | Others | Onyx today | How to close it | Cost | Prio |
|---|---|---|---|---|---|
| ~~Iframes as browsing contexts, cross-document messaging (`postMessage`, `MessagePort` across frames, `BroadcastChannel`)~~ | all | **done (docs/06 §29)**: each `<iframe>` (src, srcdoc, about:blank, hidden, a script's) a window kept by its element across reboxes, `load` events, `contentWindow` / `contentDocument`, `parent` / `top` / `frames` / `frameElement`, a WindowProxy with the cross-origin limits, the structured clone into the receiver's realm, ports transferred between realms, `sandbox`; a reCAPTCHA v2 mimic passes (`iframetest.sh`). Left: the initial about:blank synchronously, `document.domain`, focus between frames by script, site isolation (below) | a synchronous about:blank document (a content made without a fetch) | S | P2 |
| **Editing: `contenteditable`, `designMode`, `execCommand`, Selection / Range editing, `beforeinput` / `input` events on editable content, IME** | all | text inputs and textareas only | an editing host = a caret in the box tree (inline boxes, the textarea's code as model), insert / delete as DOM mutations, the Selection API's ranges drawn; enough for Gmail compose, Google Docs is out of reach | L | P2 |
| **IndexedDB** | all | none | a key-value store per origin on the card (a B-tree or a simple log file per object store), transactions async, structured clone (exists for workers), indexes | M | P2 |
| **Service Workers, Cache API, offline, Push** | all | none | Cache API first (over the HTTP cache), then SW interception of fetches in the fetcher (a worker context exists) | L | P3 |
| Web Components leftovers | all | shadow DOM done (§20); `exportparts`, form-associated custom elements, `:host` in `matches()` | small items | S | P3 |
| `:active`, `:focus`, `:focus-visible`, `:focus-within` styles | all | answer no | the hover code's incremental restyle, with the focus / press state | S | P2 |
| Clipboard API, drag and drop (HTML5 DnD), File System Access | all | none | Onyx's clipboard (the desktop's); DnD events from the mouse | M | P3 |
| Notifications, Geolocation, Permissions, Vibration, Battery, Gamepad, Web MIDI, Web Serial / USB / Bluetooth / HID | Chromium (most), WebKit (some) | none | Notifications -> the Onyx shell; Gamepad -> the kernel's USB HID pads (Onyx has them for the emulators); the rest P4 | S each | P3 |
| ~~**Web Crypto**~~ -- done (docs/06 §27) | all | `getRandomValues` / `randomUUID` from a CTR-DRBG seeded by the hardware RNG; `crypto.subtle` on mbedTLS: SHA-1/2, HMAC, AES-GCM / CBC / CTR / KW, ECDSA / ECDH P-256/384/521, RSASSA-PKCS1-v1_5, RSA-PSS, RSA-OAEP, PBKDF2, HKDF; raw / JWK / SPKI / PKCS#8; in workers. Left: Ed25519 / X25519 (not in mbedTLS 3.6), RSA key generation blocks the window | Ed25519 / X25519 from another small library (monocypher, TweetNaCl) | S | P3 |
| `Intl` leftovers | all | §15 (Temporal, DurationFormat, other calendars) | quickjs-ng's Temporal work, CLDR data | M | P3 |
| Accessibility tree (ARIA) | all | none | a screen reader is not in Onyx; P4 | L | P4 |

## 4. Media and graphics

| Gap | Others | Onyx today | How to close it | Cost | Prio |
|---|---|---|---|---|---|
| **`<video>` / `<audio>`, Media Source Extensions** | all (H.264, VP9, AV1, AAC, Opus) | none | audio first: the Onyx sound kapi exists (Koton, the emulators); decoders: Opus (libopus), Vorbis (stb_vorbis), MP3 (minimp3), AAC (fdk-aac is heavy); video: VP8/VP9 via libvpx (NEON-optimised, 720p software on a Pi 4 is borderline), H.264 through the Pi's hardware decoder needs a V4L2-like kernel driver (the Pi 4 has H.264 hw decode only; HEVC hw) -- a kernel project; MSE on top for YouTube; EME / DRM (Widevine) impossible | XL | P2 (audio), P3 (video) |
| **WebGL / WebGL2** | all | none | Onyx has a V3D driver (`kernel/sys/v3d.cpp`, the GameCube renderer): a GLES2 subset on it, WebGL bound to it; or a software rasteriser (slow) | XL | P3 |
| WebGPU | Chromium, WebKit (partial), Ladybird (starting) | none | after WebGL | XL | P4 |
| Canvas leftovers | all | §13: no shadows, filters, blend modes, conic gradients, smooth image scaling | PlutoVG extensions (blend modes, conic), a blur for shadows, bilinear texture sampling | M | P2 |
| SVG leftovers | all | §12: no mask / pattern / marker / filters / SMIL / textPath; CSS `fill` / `stroke` on inline SVG | libcss `fill` / `stroke` (the agent's §14 added them for the CSSOM: wire them to `onyx_svg_inline.c`); PlutoSVG extensions | M | P2 |
| Image formats: AVIF, JPEG XL, animated WebP / APNG | Chromium (AVIF, animated WebP), WebKit (+JXL) | PNG, JPEG, GIF (animated), BMP, WebP (still), SVG | libavif + dav1d (AV1 decode, NEON) -- AVIF is common now (Netflix, Shopify CDNs); animated WebP via libwebp's demux | M | P2 |
| Colour management (ICC, wide gamut, `color(display-p3)` drawn) | all | sRGB only | P4 on the Pi's screens | M | P4 |

## 5. Network and security

| Gap | Others | Onyx today | How to close it | Cost | Prio |
|---|---|---|---|---|---|
| ~~TLS certificate verification~~ (done, 06 §24) | all | the chain against `SD:/res/ca-bundle`, the host name, the dates against the Onyx clock; NetSurf's "Privacy error" page with "Proceed", `about:certificate`; WebSocket the same | left: OCSP / CRL revocation, Certificate Transparency | S | P3 |
| ~~TLS 1.3~~ (done, 06 §24) | all | TLS 1.2 and 1.3 (mbedTLS on PSA crypto, the app's random generator), Chrome's suites / groups / signature algorithms, 1.3 tickets resumed | left: GREASE and Chrome's other ClientHello extensions, the post-quantum X25519MLKEM768 key share | S | P3 |
| ~~HTTP/2~~ (done, 06 §24) / HTTP/3 (QUIC) | all | nghttp2: one connection per origin by ALPN, streams, Chrome's settings, fallback to HTTP/1.1 | HTTP/3 needs QUIC (ngtcp2 + a TLS 1.3 QUIC stack) | L | P4 |
| ~~Brotli / zstd~~ (done, 06 §24) | all | `gzip, deflate, br, zstd` decoded as they come | -- | -- | -- |
| **Same-origin policy, CSP, cookies' SameSite / partitioning, mixed content** | all | CORS done for fetch / XHR (06 §24: preflight, the Allow-Origin / -Credentials / -Headers / -Methods checks, modes, credentials, opaque responses); no CSP, no SameSite, no CORS for EventSource / fonts / `<img crossorigin>` | CSP parsing and enforcement for scripts / frames; SameSite in the cookie jar; CORS in the core's loads | M | P2 (security of logins) |
| Site isolation, sandboxed renderer processes | Chromium, WebKit, Ladybird (multi-process) | one process; a tab's frames share one QuickJS runtime, a realm per document (docs/06 §29: cross-origin frames reach each other only through the WindowProxy's allowed fields and the serializer) | Onyx has processes: one NetSurf per tab is already the model; within a page, iframes share the process | XL | P4 |
| ~~HTTP cache on disk, validators~~ (done, 06 §24) / back-forward cache | all | `SD:/apps/netsurf.app/cache` (64 MB, LRU), ETag / Last-Modified revalidated (304), max-age honoured; TLS sessions kept across launches; preconnect / dns-prefetch | a back-forward cache; the cache partitioned by site; preload hints (`rel=preload`, 103 Early Hints) | M | P3 |
| DNS over HTTPS, HSTS preload | all | HSTS headers kept by NetSurf's urldb, no preload list | the preload list for the big sites | S | P3 |
| Downloads manager, `download` attribute, file pickers (`<input type=file>`) | all | ? | the Onyx file dialog (wtk) | S | P2 |
| Password manager, autofill, sync, extensions, devtools | all (Ladybird: devtools starting) | none | a JS console / DOM inspector window would help debugging on the Pi (the bench has NS_JSDEBUG) | M each | P3-P4 |

## 6. Performance on the Pi

- **Where the time goes** (bbc.com on the PC, `NS_PROF`): after the fetcher fix, JS execution
  (QuickJS) and the full rebox after each script turn dominated. Since docs/06 §26 (the kept
  style selections, the flex layout memo, the in-place restyles) the reboxes and layouts are
  small next to the scripts (bbc.com: 86 % of the samples in QuickJS; m.facebook.com's layout
  pass 170 ms -> under 1 ms, github.com's rebox 150 ms -> 6-20 ms on the PC). On the Pi each is
  5-20x slower. The big levers now, in order: the DOM natives' hot paths in C (§2), an
  incremental layout and box construction for the big pages (§1: wikipedia's layout ~45 ms on
  the PC), Wasm for the sites that compute in it.
- **Multi-core**: Chromium runs raster, decode, network and the compositor on other threads.
  Onyx NetSurf already fetches in threads; next: image decoding in a thread (the decoders are
  pure C over a buffer), a raster thread for the compositing layers, workers on their own
  thread (a second QuickJS runtime -- runtimes are independent).
- **GPU compositing** -- stage 1 done: the **compositing service** `user/gpucomp` (kapi v70;
  docs/02 §15, docs/03 *GPU compositing*, `/bin/gpcdemo`). An app uploads layers (premultiplied
  ARGB, any size: tiles of 2048 with a seamless border past that) once, then has a list of them
  composited by the V3D into its canvas: per layer a 2D affine matrix (CSS `matrix(a..f)`), a clip
  rectangle, an opacity, premultiplied source-over, bilinear filtering; scrolling = the layer's
  source rectangle moved (fractions: smooth), nothing uploaded again; a damaged rectangle =
  `gpc_tex_update` (kapi `gpu_texture_rect`: only those texels re-tiled). The same API runs on the
  CPU (NEON loops) when there is no GPU or it stopped (`GPC_LOST`), so the browser never depends
  on it. What it costs: each composite is one `gpu_render` frame straight into the canvas (the
  kernel cleans / invalidates the target's span before and after); its GPU time is shared with any
  other program using the V3D (frames are served one at a time). Measured on the Pi by
  `gpcdemo bench` (1920 x 1080, GPU vs CPU) -- to be filled in from the first run.

  **Stage 2 -- done** (docs/06 §25; `gpu_compositing` in Choices, default on): the page is
  kept in a band three views high (a ring of rows), repainted where damaged, uploaded where
  painted; opacity / transform groups are retained layers (`gpc_tex`, damage-driven uploads,
  demoted to in-place painting when something painted after them covers them or when their
  pixels change frame after frame); a frame is one `gpc_composite` into the canvas; **a scroll
  paints nothing** already in the band (rows ahead painted in idle turns); animated / hovered
  `transform` and `opacity` are composite-only frames (`html_redraw_layer_update`); a startup
  self-test (the band, the layers, and drawing over the target -- the no-clear path, wrong on
  the Pi until the load packet was fixed) falls back to the CPU path; the CPU painting stays
  (`gpu_compositing:0`). Checked by `tools/tests/netsurf/gputest.sh` (composited = CPU painting
  on the PC, gpucomp's CPU path and the software V3D). Of the plan below, not done: steps 1's
  fixed / sticky boxes and overflow scrollers as layers (fixed boxes scroll with the page in
  NetSurf: they are in the band; a scroller's scroll repaints its box), `will-change`, step 5's
  `GPC_F_ASYNC`, step 6 (groups holding layers: the CPU paints what such a group holds).

  **Stage 2 -- the plan** (after the compositing-layers work in
  `content/handlers/html/redraw.c` / `frontends/framebuffer/onyx_paint.c` is merged; nothing of
  it is touched by stage 1):
  1. *A layer = a `gpc_tex`.* Each stacking context the redraw promotes to a layer (opacity < 1, a
     non-translation `transform`, `filter`, `will-change`, a fixed / sticky box, an overflow
     scroller) keeps its off-screen ARGB buffer -- premultiplied, as `gpucomp` wants it (convert
     where the painter keeps straight alpha) -- and a `gpc_tex` of the same size. Painting a
     layer stays the CPU plotters' job (text, borders, images, gradients: `onyx_paint.c`); what
     changes is who assembles them.
  2. *Uploads follow the damage.* The redraw's dirty rectangles of a layer become
     `gpc_tex_update (g, tex, x, y, w, h, buf + y * stride + x, stride)` calls; a layer whose
     content did not change uploads nothing. A resized layer: `gpc_tex_destroy` + `gpc_tex_create`.
  3. *The layer list at present time.* In paint order (the root / page layer first), each
     `gpc_layer`: `m` = the layer's transform in page pixels (the CSS `transform` about its
     `transform-origin`, times its offset in the page, minus the scroll -- the same matrix the
     hit-testing inverts: `gpc_matrix_invert`), `clip` = the ancestors' overflow clip in the page
     area, `opacity` = the effective opacity, `GPC_L_OPAQUE` for layers known opaque (the root's
     background). Then one `gpc_composite` into the page area of the canvas
     (`onyx_chrome_page`'s pixels and stride: the GPU writes the canvas directly -- the back
     buffer's copy to the canvas in `onyx_surface.c` goes away for composited frames).
  4. *Scrolling without repainting.* The root layer is a band of the page taller than the view
     (say 3 viewports: 1920 x 3240 = two textures high); scrolling moves `src_y` (fractions for
     smooth scrolling); when the view nears the band's edge the band is re-centred (repaint + a
     band upload, spread over frames). Fixed elements are layers of their own (they do not move).
     Overflow scrollers: the same, one layer each.
  5. *Animations are composites.* `transform` / `opacity` transitions and `@keyframes` (wave 1
     item 2) change `m` / `opacity` only: a frame is one `gpc_composite` (milliseconds on the
     GPU), no layout, no paint, no upload -- the payoff of the whole design. `requestAnimationFrame`
     paces them; `GPC_F_ASYNC` + `gpc_submit` lets the JS / layout of the next frame run while
     the GPU composes this one (call `gpc_wait` before touching the canvas or the layers).
  6. *Groups.* A layer with children layers and opacity < 1 (or a filter) cannot be applied to
     each child (overlaps would show through): composite the children into the group's own ARGB
     target (`GPC_T_ALPHA`, from `gpc_target_alloc` so the GPU renders it directly), then
     `gpc_tex_update` the group's texture from it and draw the group with its opacity. (The GPU
     cannot sample what it just rendered without that re-upload: the texture unit reads tiled
     layouts only -- a render-to-texture path is a later kernel addition.)
  7. *Budget.* Handles: 512 textures a program (a 2048-tile each); memory: the GPU's copies live
     below 1 GB (`HEAP_LOW`), shared with the kernel -- keep the layers' textures to ~128 MB (drop
     the off-screen ones first; a layer whose `gpc_tex_create` fails is drawn by the CPU, in
     order, automatically). `GPC_LOST`: re-create the textures (the CPU path from then on).
  8. *Checks.* `gpcdemo test` first on each Pi; then NetSurf's `NS_PERF` / `NS_PROF` before /
     after on the sweep's sites, and the layoutdiff pages (the composited frame must match the
     CPU one: `gpc_create` with `GPC_F_CPU` gives the reference on the same machine).
  The compositing layers themselves (docs/06 §21) are painted by the CPU: an effect costs its
  layer's pixels when it is painted (a large blur is done at a reduced size; a page without
  effects pays nothing) -- with stage 2 once, not at each scroll; opacity and transforms are then
  assembled by the GPU.

## 7. Order of work (what will be done, in parallel where independent)

Wave 1 -- the P1 items, independent areas (an agent each, in worktrees, merged one by one
with the tests: `jstest.sh`, `nettest.sh`, `libcss-test`, `sitesweep.sh`, `layoutdiff.sh`):
1. ~~Compositing layers: `opacity` groups, full `transform`, `filter` / `backdrop-filter`
   (redraw.c, onyx_paint.c).~~ Done (docs/06 §21; left: a real perspective, a layer kept
   between redraws, clip-path / mask on it).
2. Transitions, `@keyframes`, Web Animations, `requestAnimationFrame` pacing (a new
   `html/onyx_anim.c`, libcss computed-value interpolation) -- done (docs/06 §22).
3. ~~WebAssembly on wasm3 (vendored), bound to QuickJS; Web Crypto on mbedTLS~~ -- done (docs/06 §27).
4. TLS certificate verification, HTTP/2 (nghttp2), `br` announced, CORS / CSP basics, a disk
   cache with validators -- done but CSP (06 §24: also zstd, TLS sessions kept across
   launches, preconnect, per-connection timings, TLS 1.3); left: CSP, SameSite.

Wave 2 -- P1 / P2 depending on wave 1:
5. Incremental restyle and relayout (after the layers, since both touch the redraw) --
   restyle done (06 §26: kept selections, in-place restyles, the flex layout memo); left:
   incremental box construction and layout.
6. Text: HarfBuzz shaping, colour emoji, UAX #14 line breaking, then bidi.
7. Media: audio (`<audio>`, Web Audio basics) on the Onyx sound kapi; AVIF (dav1d).
8. Editing (`contenteditable`) and IndexedDB; `:focus` / `:active` styles.

Wave 3 -- P3: video (VP9 software, then H.264 hardware in the kernel), WebGL on V3D,
Service Workers / Cache API, multi-column, container queries, scroll-snap, zoom / HiDPI, the
JIT study for QuickJS on AArch64.

Each item is measured before and after on the Pi's workload (the bench's `NS_PERF` /
`NS_PROF`, the sweep's 15 sites) -- a feature that slows every page is gated to the pages that
use it.
