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
| **Compositing: `opacity` on a subtree, stacking of translucent groups** | all | colours' alpha only; a subtree's `opacity` is not a group | paint the stacking context into an off-screen ARGB layer (`onyx_paint.c`), blend it with its alpha; skip when opacity = 1; cache the layer until its subtree changes | M | P1 |
| **`transform` (rotate / scale / skew / matrix, 3D flattened)** | all | translation only | a transformed stacking context painted into a layer then drawn through the matrix (PlutoVG already draws a transformed image); hit-testing through the inverse matrix | M | P1 |
| **`filter` / `backdrop-filter` (blur, brightness, drop-shadow...)** | all | parsed, not drawn | on the layer above: box blur (3 passes), colour matrices; `backdrop-filter` reads the pixels behind | M | P2 |
| Transitions and animations (`transition`, `@keyframes`, Web Animations API) | all | **done** (docs/06 §21): a timeline per content interpolates the computed values each frame (colours, lengths, opacity, transforms, shadows), redraws the animated boxes' rectangles when paint-only, lays out otherwise; the events; `element.animate()` / `Animation` / `getAnimations()` on the same engine; `requestAnimationFrame` paced by the frames (~60 Hz, slower when frames are long, none when idle) | left: `rotate` / `scale` / subtree `opacity` drawn (the compositing layers), pseudo-elements' animations, reversing shortening, keyframes recomputed on a base change, `composite` | S | P2 |
| **Incremental restyle and relayout** | all (dirty bits per node) | a DOM change = full rebox + layout (10 ms after the turn); `:hover` already incremental | dirty flags on nodes; restyle the changed subtrees (libcss selection per node, the hover code's machinery); relayout from the nearest box whose size cannot change (a formatting-context root with fixed size); React pages would stop costing a full layout per update | L | P1 |
| `position: fixed` / `sticky` in every case | all | fixed and sticky exist (z-layers), some cases wrong | layoutdiff pages per case | S | P2 |
| **Text shaping (ligatures, kerning, Arabic, Indic, Thai...)** | HarfBuzz (Chromium, WebKit, Ladybird) | FreeType glyph by glyph | vendor HarfBuzz (C++, ~1 MB, no exceptions needed) in `font_freetype.c`: shape each run, cache shaped words | M | P2 |
| **Bidirectional text (RTL: Arabic, Hebrew)** | ICU / own | none (`direction` parsed) | Unicode bidi algorithm (fribidi, or the small `unicode-bidi` reference) on each line's runs; mirrored layout of the line boxes | M | P3 |
| **Colour emoji** | all | monochrome fallback | FreeType's `FT_LOAD_COLOR` (CBDT / COLRv0-1 via its `FT_Get_Color_Glyph_Layer`), a Noto Color Emoji subset on the card | S | P2 |
| Line breaking by UAX #14, hyphenation (`hyphens: auto`) | all | spaces and a few punctuation cases | libunibreak (small C); hyphenation dictionaries later | S | P3 |
| Vertical writing modes | all | none | after bidi; rarely needed on Western pages | L | P4 |
| Multi-column layout (`columns`) | all | parsed | column balancing in layout.c | M | P3 |
| `@container` queries | all | parsed, not evaluated | evaluate at layout (needs incremental relayout) | M | P3 |
| Subgrid, masonry | Chromium / WebKit | grid without subgrid | layout_grid.c | M | P3 |
| `clip-path` on HTML boxes, `mask` beyond images | all | SVG clip-path; mask-image on images (layout agent) | through the compositing layers above | M | P3 |
| Scrolling: smooth, momentum, `scroll-snap`, `overscroll-behavior`, scroll-driven animations | all | wheel steps, overflow scrollers | a scroll animator per scroller; snap points at the end | M | P3 |
| High-DPI / zoom | all | 1:1 | a device-pixel ratio through layout (CSS px -> device px) | M | P3 |
| Printing, PDF export | all | none | NetSurf had a PDF plotter upstream | M | P4 |

## 2. JavaScript engine

| Gap | Others | Onyx today | How to close it | Cost | Prio |
|---|---|---|---|---|---|
| **WebAssembly** | all | none (QuickJS has no Wasm) -- pages that need it stop (Figma, Google Earth, some players, emulators, some bundles for codecs / crypto) | wasm3 (a fast C interpreter, ~70 KB, MIT) or WAMR's fast interpreter, bound to QuickJS as the `WebAssembly` namespace (`Module`, `Instance`, `Memory` on an ArrayBuffer, `Table`, imports / exports as JS functions, `instantiateStreaming`); an AOT / JIT for AArch64 later (WAMR has one) | M | P1 |
| **JIT (speed)** | V8 / JSC / LibJS (bytecode interpreter + JIT on x86-64 only) | QuickJS-ng bytecode interpreter: 20-50x slower than V8 on heavy scripts (a React hydration: seconds on the Pi) | short term: profile-guided fixes in the natives (dom.js's hot paths in C: the DOM wrappers, the selector engine, innerHTML), QuickJS's inline caches (quickjs-ng has shape caches: keep them warm), avoid megamorphic dom.js code; long term: a baseline JIT for AArch64 in QuickJS (XL) | M / XL | P1 |
| Engine conformance (test262 core) | V8 ~99 %, LibJS ~95 % | QuickJS-ng ~ 99 % of ES2024 | follow quickjs-ng releases | S | P3 |
| Memory: generational GC | V8 / JSC | refcount + cycle collector, 384 MB limit | fine for the Pi; watch leaks | - | P4 |

## 3. DOM and Web APIs

| Gap | Others | Onyx today | How to close it | Cost | Prio |
|---|---|---|---|---|---|
| **Editing: `contenteditable`, `designMode`, `execCommand`, Selection / Range editing, `beforeinput` / `input` events on editable content, IME** | all | text inputs and textareas only | an editing host = a caret in the box tree (inline boxes, the textarea's code as model), insert / delete as DOM mutations, the Selection API's ranges drawn; enough for Gmail compose, Google Docs is out of reach | L | P2 |
| **IndexedDB** | all | none | a key-value store per origin on the card (a B-tree or a simple log file per object store), transactions async, structured clone (exists for workers), indexes | M | P2 |
| **Service Workers, Cache API, offline, Push** | all | none | Cache API first (over the HTTP cache), then SW interception of fetches in the fetcher (a worker context exists) | L | P3 |
| Web Components leftovers | all | shadow DOM done (§20); `exportparts`, form-associated custom elements, `:host` in `matches()` | small items | S | P3 |
| `:active`, `:focus`, `:focus-visible`, `:focus-within` styles | all | answer no | the hover code's incremental restyle, with the focus / press state | S | P2 |
| Clipboard API, drag and drop (HTML5 DnD), File System Access | all | none | Onyx's clipboard (the desktop's); DnD events from the mouse | M | P3 |
| Notifications, Geolocation, Permissions, Vibration, Battery, Gamepad, Web MIDI, Web Serial / USB / Bluetooth / HID | Chromium (most), WebKit (some) | none | Notifications -> the Onyx shell; Gamepad -> the kernel's USB HID pads (Onyx has them for the emulators); the rest P4 | S each | P3 |
| **Web Crypto** (`crypto.subtle`; `getRandomValues` from a real CSPRNG) | all | `subtle` is an empty object; `getRandomValues` / `randomUUID` use `Math.random` (predictable: session tokens, nonces) | mbedTLS is linked: its CTR-DRBG for `getRandomValues`, then digest, HMAC, AES-GCM / CBC, ECDSA / ECDH (P-256), RSA-PSS / OAEP, PBKDF2, HKDF through it, `importKey` / `exportKey` (raw, JWK, SPKI, PKCS#8) | M | P1 (logins, many SPAs call `subtle.digest`) |
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
| **TLS certificate verification** | all | `MBEDTLS_SSL_VERIFY_NONE` (the CA bundle is on the card, the clock is needed for expiry) | verify against `SD:/res/ca-bundle` with the Setup's clock (kernel v69 has it); an error page with "continue anyway" | S | P1 (before any login) |
| **HTTP/2 (and HTTP/3 / QUIC)** | all | HTTP/1.1 keep-alive, 8 connections | nghttp2 (C, the client side ~100 KB) under the fetcher: one connection per origin, multiplexed -- big pages of 100+ resources load much faster; HTTP/3 needs QUIC (ngtcp2 + a TLS 1.3 QUIC stack): P4 | M | P2 |
| Brotli / zstd content encoding | all | the brotli decoder is linked but the fetcher announces `gzip, deflate` only; no zstd | announce and decode `br` in `onyx_fetch.c` (~20 % smaller than gzip); zstd decoder (small) | S | P2 |
| **Same-origin policy, CORS, CSP, cookies' SameSite / partitioning, mixed content** | all | no CORS checks, no CSP, cookies basic | CORS in `n_request` (preflight, response checks), CSP parsing and enforcement for scripts / frames, SameSite in the cookie jar | M | P2 (security of logins) |
| Site isolation, sandboxed renderer processes | Chromium, WebKit, Ladybird (multi-process) | one process, one context per page | Onyx has processes: one NetSurf per tab is already the model; within a page, iframes share the process | XL | P4 |
| HTTP cache on disk, `Cache-Control` / validators, back-forward cache | all | memory cache; the fetcher drops validators | a disk cache on the card (llcache has a backing-store API upstream: `fs_backing_store.c`), 304 support in the fetcher | M | P2 |
| DNS over HTTPS, HSTS preload | all | HSTS headers kept by NetSurf's urldb, no preload list | the preload list for the big sites | S | P3 |
| Downloads manager, `download` attribute, file pickers (`<input type=file>`) | all | ? | the Onyx file dialog (wtk) | S | P2 |
| Password manager, autofill, sync, extensions, devtools | all (Ladybird: devtools starting) | none | a JS console / DOM inspector window would help debugging on the Pi (the bench has NS_JSDEBUG) | M each | P3-P4 |

## 6. Performance on the Pi

- **Where the time goes** (bbc.com on the PC, `NS_PROF`): after the fetcher fix, JS execution
  (QuickJS) and the full rebox after each script turn dominate. On the Pi each is 5-10x
  slower. The big levers, in order: incremental restyle / relayout (§1), the DOM natives' hot
  paths in C (§2), Wasm for the sites that compute in it, HTTP/2 for many small resources.
- **Multi-core**: Chromium runs raster, decode, network and the compositor on other threads.
  Onyx NetSurf already fetches in threads; next: image decoding in a thread (the decoders are
  pure C over a buffer), a raster thread for the compositing layers, workers on their own
  thread (a second QuickJS runtime -- runtimes are independent).
- **GPU compositing**: the V3D driver could blit the layers (the compositor's own is CPU).

## 7. Order of work (what will be done, in parallel where independent)

Wave 1 -- the P1 items, independent areas (an agent each, in worktrees, merged one by one
with the tests: `jstest.sh`, `nettest.sh`, `libcss-test`, `sitesweep.sh`, `layoutdiff.sh`):
1. Compositing layers: `opacity` groups, full `transform`, `filter` / `backdrop-filter`
   (redraw.c, onyx_paint.c).
2. Transitions, `@keyframes`, Web Animations, `requestAnimationFrame` pacing (a new
   `html/onyx_anim.c`, libcss computed-value interpolation) -- done (docs/06 §21).
3. WebAssembly on wasm3 (vendored), bound to QuickJS; Web Crypto on mbedTLS.
4. TLS certificate verification, HTTP/2 (nghttp2), `br` announced, CORS / CSP basics, a disk
   cache with validators.

Wave 2 -- P1 / P2 depending on wave 1:
5. Incremental restyle and relayout (after the layers, since both touch the redraw).
6. Text: HarfBuzz shaping, colour emoji, UAX #14 line breaking, then bidi.
7. Media: audio (`<audio>`, Web Audio basics) on the Onyx sound kapi; AVIF (dav1d).
8. Editing (`contenteditable`) and IndexedDB; `:focus` / `:active` styles.

Wave 3 -- P3: video (VP9 software, then H.264 hardware in the kernel), WebGL on V3D,
Service Workers / Cache API, multi-column, container queries, scroll-snap, zoom / HiDPI, the
JIT study for QuickJS on AArch64.

Each item is measured before and after on the Pi's workload (the bench's `NS_PERF` /
`NS_PROF`, the sweep's 15 sites) -- a feature that slows every page is gated to the pages that
use it.
