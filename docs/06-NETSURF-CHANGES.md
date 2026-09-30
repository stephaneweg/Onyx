# Onyx — the NetSurf changes

NetSurf is Onyx's web browser: the upstream NetSurf 3.x core, its framebuffer frontend and
its libraries, vendored in `third_party/` and changed for Onyx. This document lists what
differs from upstream and why, as `05-CIRCLE-CHANGES.md` does for Circle. The aim of the
changes: pages drawn as Chrome draws them (the user compares with Chrome on Windows).

NetSurf is ours to change: the patches are marked `Onyx:` in the sources (a comment on the
line or the block), and each is listed here. The user's guide entry is in
`04-USER-GUIDE.md` (NetSurf), the build in `user/netsurf/README.md`.

## 1. Where things are

| Path | What |
|---|---|
| `third_party/netsurf/` | NetSurf: `content/` (the core: HTML, CSS, layout, redraw), `desktop/`, `frontends/framebuffer/` (the frontend Onyx uses), `include/`, `utils/` |
| `third_party/libcss/` | CSS parser and selection engine (most of the CSS3 work) |
| `third_party/libnsfb/` | the framebuffer library (`user/nsfb/onyx_surface.c`: its Onyx surface) |
| `third_party/freetype-2.14.3/` | FreeType (options and modules: `user/netsurf/freetype/`) |
| `third_party/webref-css-8.7.5/` | the CSS specifications' value grammars (W3C webref, MIT): libcss's grammar tables are made from them (§14) |
| `third_party/brotli-1.1.0/` | Brotli's decoder only (FreeType's WOFF2), MIT |
| `third_party/quickjs-ng-0.17.0/` | QuickJS-ng, the JavaScript engine (ES2023), MIT: the engine alone (`README.onyx`: its patches) |
| `third_party/plutovg-1.3.3/`, `third_party/plutosvg-0.0.8/` | PlutoVG, the vector rasteriser (anti-aliased paths, strokes, gradients, clipping, compositing, TrueType text), and PlutoSVG, the SVG renderer on it, MIT: SVG images, inline `<svg>`, `<canvas>` (§12, §13; PlutoSVG's patches: its `README.onyx`) |
| `third_party/netsurf/content/handlers/javascript/quickjs/` | NetSurf's JavaScript on QuickJS: `qjs.c` (the engine's glue, the natives), `dom.js` (the DOM, in JavaScript), `canvas.js` + `qjs_canvas.c` (canvas, §13), `intl.js` + `qjs_intl.h` (Intl, §15), `html5.js` (the HTML5 DOM: §17) |
| `third_party/libhubbub/`, `third_party/libdom/`, `third_party/libparserutils/` | the HTML parser (the current standard's: §16), the DOM, the input decoding |
| `third_party/cldr-48/` | the locale data of Intl (CLDR 48 through ICU 78; Unicode License v3), made by `tools/tests/netsurf/intl/gendata.js` |
| `third_party/fonts/`, `third_party/dejavu-fonts-ttf-2.37/` | the fonts staged into `SD:/res/fonts` |
| `user/netsurf/` | the Onyx glue: `onyx_chrome.cpp` (the window, its wtk toolbar, the History dialog), `onyx_fetch.c` (HTTP/HTTPS over the Onyx TCP kapis, mbedTLS; each download in a thread of its own), `onyx_main.c`, the makefiles |
| `tools/tests/netsurf/` | the PC test bench: NetSurf built for the PC on the desktop simulator (`host.mk`), a page to a PNG (`shot.sh`), the same page in Chromium (`chrome.sh`), copies of the two sites (`getsites.sh`), the JavaScript regression test (`jstest.sh`, `pages/js-*.html`), the HTTP test (`httptest.sh`: the fetcher over a local HTTP/1.1 server, `httpsrv.py` -- keep-alive, chunked, gzip, a redirect, cookies, the Referer, the page drawn as its file:// copy); `NS_JSDEBUG=1` prints the scripts' errors and `console.log`, `NS_BOXDUMP=<file>` + F5 dumps the box tree, `NS_PERF=1` the timings (§9). `css3test.sh`: css3test.com's score in NetSurf and Chromium; `css-check`: what libcss keeps (`csscheck.c`, `css-values.txt`) (§14). `jstest.sh`: the DOM, the events, a recursion, `fetch` / XHR (file:// and data: URLs), the hover events, CSS `:hover`, `localStorage` kept, the HTML5 pages (`js-html5`, `js-forms`, `js-apis`, `js-ce`); `html5lib.sh` (the parser against the html5lib-tests, its speed: §16), `html5test.sh` (the html5test.co score: §17) |

Build for the Pi: `make -C user/netsurf` (the libraries, their `.a` are committed:
`libquickjs.a` among them), then `make -f user/netsurf/netsurf-app.mk link stage`. A header change needs a clean rebuild of
what includes it (the rules do not track headers): the libcss objects, `/tmp/nsbuild`. The
codegen tools need a host compiler (`BUILD_CC`, `gcc` by default: `build-essential` in WSL; a
WSL without one: `BUILD_CC=$PWD/tools/wsl-build-cc.sh`, the Windows MinGW gcc). GCC 14
turned some warnings NetSurf has into errors: the makefile keeps them warnings.

**The network** (`user/netsurf/onyx_fetch.c`): each download runs in a **thread** of its own
(kernel v67) -- the DNS, the connect, the TLS handshake and every read block there, not the
UI. **HTTP/1.1 with keep-alive**: a response is framed by its `Content-Length` or its chunks
(read to the close only when it has neither), then its connection goes back to a **pool** (8,
per host / port / scheme, kept 10 s idle) for the next request there -- no DNS, TCP or TLS
handshake again; a pooled connection the server closed meanwhile is noticed at its first
request (nothing back) and the request sent again on a new one. **Streaming**: the head goes to
the core as soon as it is in (its headers, a redirect), then the body as it comes, inflated on
the fly (zlib): the HTML parser finds the style sheets, scripts and images while the page still
downloads. The worker posts (`kapi_post`) when the head has come, every 32 KB, and at the end:
the UI thread, waiting in `kapi_pump_wait`, wakes at once. **Cookies**: the jar's
(`urldb_get_cookie`, read on the UI thread when the fetch is set up) sent, and every
`Set-Cookie` of a response -- a redirect's too (a login's session) -- given to NetSurf's jar
(`fetch_set_cookie`). **Referer** as Chrome (strict-origin-when-cross-origin: the whole URL
within its origin, its origin elsewhere, nothing from https to http) and an `Origin` on the
methods other than GET / HEAD (`fetch_get_referer`, an Onyx addition to `content/fetch.c`).
**User-Agent**: a current Chrome's on Android (`utils/useragent.c`; Choices' `user_agent`
replaces it): the big sites send their light mobile pages -- their desktop script applications
(google.com, yahoo.com's Next.js) are too heavy for QuickJS on the Pi; the same in
`navigator.userAgent`; `Accept-Language`: Choices' `accept_language`, else
French then English. No length limit on a URL's path (it was 1024). 8 downloads at once; the
connects one at a time (short now: the kernel caches the DNS answers). An aborted fetch whose
thread still runs is freed by that thread. The request is a GET, a POST (a url-encoded or text
body) or any method a script asks for (`X-Onyx-Method`, taken off), with the caller's headers;
the response goes to the core with its status line and headers (not the cache-control ones,
nor those the inflate makes wrong). Without threads (an older kernel) it is the
one-step-per-poll state machine (HTTP/1.1 with `Connection: close`, its chunks undone, the
response delivered whole). mbedTLS (`user/tls/onyx_tls.hpp`): its session cache is locked (16
hosts, replaced in turn), its receive buffer 16 KB (a whole record: fewer round trips to the
network core). The cache (`content/llcache.c`, Onyx changes): a request's own headers
(`llcache_handle_retrieve_ex`), its HTTP status kept (`llcache_handle_get_http_code`), its
headers in order (`llcache_handle_get_header_at`).

## 2. The window and the frontend

- **A native Onyx window** (`user/netsurf/onyx_chrome.cpp`): the toolbar is wtk's (back,
  forward, reload/stop, home, History, the address field), the frame's close box closes
  NetSurf; no fbtk toolbar (`frontends/framebuffer/gui.c`). Keys: F5, Esc, Alt+Left/Right,
  F6 / Ctrl+L, Ctrl+H.
- **A back buffer** (`user/nsfb/onyx_surface.c`, the libnsfb surface): NetSurf draws into an
  off-screen buffer of the page's size, and `update` copies the rectangle it redrew into the
  window's canvas -- the compositor, the apps being preempted, showed half-drawn redraws (a
  background cleared, then painted: the page flickered at each restyle). Made again at a resize,
  from what the canvas shows. **One present a main-loop iteration**, after all its redraws
  (`onyx_chrome_present_later` / `onyx_chrome_flush`, called at the end of `framebuffer_run`):
  a scroll's moved pixels and its new band are shown together (they were two presents, the first
  with the band not yet drawn).
- **The main loop waits for an event** (`onyx_input` -> `onyx_chrome_pump_wait` ->
  `kapi_pump_wait`), up to the next timer: the pointer, a key, a fetch thread's post or the
  close box wakes it at once -- it slept in blind 20 ms slices.
- **History** — a native dialog (most recent first, Find, Delete, Clear all); the pages
  visited kept in `SD:/apps/netsurf.app/History`, the cookies in `.../Cookies`
  (`ONYX_NS_DATAPATH`, written a few seconds after each page and on exit; never committed).
  The local history's thumbnails are off (`desktop/browser_history.c`).
- **Scrolling**: the mouse wheel; a scroll blits what stays on screen and redraws the strip
  that comes in. A new page opens at its top (`GW_EVENT_NEW_CONTENT`).
- **96 dpi, 16 px text** (`gui.c`): a CSS px is a pixel, `medium` is 12 pt = 16 px, the
  default font is serif (Chrome's Times New Roman) — NetSurf had 90 dpi and 12.8 pt.

## 3. Fonts (`frontends/framebuffer/font_freetype.c`)

- **Families by their CSS names.** NetSurf used only the generic family. The card's fonts
  (`SD:/res/fonts`) are found under their names and those of the fonts they stand in for,
  metric for metric (`fb_card_faces`, `fb_font_aliases`):

  | font on the card | stands in for |
  |---|---|
  | Liberation Sans 2.1.5 | Arial, Helvetica, Arimo, Tahoma, Trebuchet MS |
  | Liberation Serif 2.1.5 | Times New Roman, Times, Tinos |
  | Selawik 1.01 (Microsoft) | Segoe UI, system-ui |
  | Gelasio | Georgia (and Georgia's ascent / descent) |
  | DejaVu Sans / Serif / Sans Mono | Verdana, Courier New, Consolas, Menlo... — and the fallback |

  The generic families are Chrome's on Windows: serif → Liberation Serif, sans-serif →
  Liberation Sans, monospace → DejaVu Sans Mono. All are SIL OFL; a missing file is skipped
  (DejaVu Sans is the one required).
- **Matching** as CSS Fonts says: the style first (an italic with no italic face is its
  roman slanted, x += y/4), then the nearest weight (600 and up is bold with two faces).
- **Per-character fallback**: the families of the list in turn, then DejaVu Sans.
- **Advances**: unhinted (fractional), each glyph drawn at its pen position rounded;
  glyphs hinted lightly (`FT_LOAD_TARGET_LIGHT`, vertically only). `letter-spacing`,
  `word-spacing` (new `plot_font_style_t` fields) and `font-variant: small-caps` apply.
- **Vertical metrics** (`fb_font_metrics`, the layout table's `metrics`): as DirectWrite
  reads them — OS/2's typographic metrics when the font sets USE_TYPO_METRICS, else its
  Windows ascent / descent and hhea's line gap; each rounded to whole pixels as Blink does.
- **Web fonts** (`@font-face`, `content/handlers/html/onyx_webfont.c`): once a document's
  style sheets are in, each face's first source FreeType reads (TrueType, OpenType/CFF,
  WOFF, WOFF2) is fetched, handed to the font code for that document only
  (`add_face` / `release_faces` / `set_scope` in the layout table) and the document laid
  out again (font-display: swap), its cached text widths forgotten first. Faces split by
  `unicode-range` are fetched when they cover some of Latin-1; a variable font is set to
  the weight asked (its `wght` axis). `onyx_fetch.c` asks `fonts.googleapis.com` with a
  current browser's user agent (WOFF2 subsets, ~50 KB a weight, rather than 300 KB TTFs).
- FreeType is built with the TrueType and CFF drivers, zlib (WOFF) and Brotli (WOFF2).

## 4. CSS (libcss)

- **Values**: `calc()`, `min()`, `max()`, `clamp()` everywhere a length goes; custom
  properties and `var()` (resolved at cascade time); CSS Color 4/5 (`rgb()`/`hsl()` space
  syntax, `hwb()`, `lab()`/`lch()`/`oklab()`/`oklch()`, `color-mix()`); gradients kept as a
  canonical text (`onyx-gradient:` URLs) for the redraw.
- **Properties added**: `row-gap`/`gap`, the four `border-*-radius`, `box-shadow`,
  `text-shadow`, `background-size`, `background-clip` (with `text`), `text-overflow`,
  `justify-items`/`justify-self`, `aspect-ratio`, `object-fit`/`object-position`,
  `transform`/`translate`/`scale`/`rotate`, the grid properties; logical properties and
  the CSS3 shorthands mapped to their physical longhands.
- **Rules and selectors**: `@supports`, `@layer`, `@container` (their content kept),
  `:is()`, `:where()`, `:focus-within`, `:focus-visible`, `:any-link`.
- **Vendor prefixes**: `-webkit-`, `-moz-`, `-ms-`, `-o-` properties whose standard name
  is known are that property; `-webkit-text-fill-color` is `color`.
- **@font-face**: `format(woff2)`, `unicode-range`, `font-weight` ranges and any weight
  1–1000; `css_stylesheet_font_faces()` lists a sheet's rules.
- **Units**: a length is its unit's exact size times its value (upstream rounded the unit
  to whole pixels first: 1em of a 12.48 px font was 12 px, 1pt was 1px).
- **The specifications' grammars** (§14): every property and descriptor libcss does not
  compute is checked against its official grammar and kept without effect; @keyframes,
  @counter-style, @property, @scope... kept; Selectors 4; the newer units and math functions
  computed; SVG's fill / stroke... computed.
- libcss's own selection tests still pass: `make -f tools/tests/netsurf/host.mk
  libcss-test`.

## 5. Layout (`content/handlers/html/layout*.c`)

- **Flexbox**: inline children blockified; the automatic minimum size; item contributions
  as Flexbox 9.9.1 (the larger of content and definite width, clamped by the flex base);
  `gap`; `justify-content`, `align-content`; `min-height`/`max-height` with auto height.
- **Grid** (`layout_grid.c`, new): templates, `repeat()`, `fr`, `minmax()`, auto tracks,
  placement, areas, `auto-fill`/`auto-fit`, gaps, alignment. A grid container is a
  `BOX_FLEX`/`BOX_INLINE_FLEX` box whose display is grid.
- **Lines on their baseline** (CSS 2.1 10.8): the block's strut, text (its font's ascent
  below the half leading), inline-blocks and buttons (their last / first line's
  baseline), images (their bottom edge), `vertical-align` (sub, super, middle, text-top,
  text-bottom, top, bottom, lengths) — NetSurf put each box at the top of the line and a
  text's baseline three quarters down. `line-height: normal` is the font's spacing (was
  1.3 em); a run of lines keeps its fractions of a pixel.
- `transform`'s translation moves a box as a relative offset (paint and hit-testing).
- The space after an inline-block, inline-flex, image or control is kept.
- `<button>` keeps a flex / grid / block box.

## 6. Painting (`content/handlers/html/redraw.c`, `onyx_paint.c`)

- New plotter operations (`include/netsurf/onyx_paint.h`, `plotters.h`): `onyx_shape`
  (rounded boxes, rings, blurred shadows, gradients), `onyx_round_clip`, `onyx_text_paint`
  (text in a gradient); the framebuffer's in `frontends/framebuffer/onyx_paint.c` —
  anti-aliased by signed distance, Gaussian (erfc) shadows, linear / radial / conic /
  repeating gradients. Made fast (a full redraw of kotonviolins.com: 12.6 ms -> 4.9 ms on the
  PC, the same pixels): the rows' fully covered spans filled without a distance a pixel (a
  shadow's too: 3 sigma inside), the insides of a ring's hole and of a shadow's caster skipped,
  the blur's coverage from a table (`blur_lut`: no `expf` a pixel), a gradient's colours from a
  table of 1025 made once per gradient and kept (`glut_get`, keyed by its stops), a linear
  gradient's position stepped along the row, the blend without a division (`div255`).
- **`background-size`** (`contain`, `cover`, lengths, percentages, one side `auto` from the
  image's ratio; `onyx_background_size` in `redraw.c`): libcss parsed it, the redraw drew
  every background image at its own size -- the SVG backgrounds of the big sites (icons sized
  by CSS) came out wrong.
- `border-radius`, `box-shadow` (outer and inset), gradient backgrounds, rounded clips of
  a box's content; `background-clip: text` paints the descendants' text with the box's
  background.
- Translucent colours are blended; a translucent fill no longer hides what is under it
  (`desktop/knockout.c`); `color: transparent` draws nothing.
- **The painting order of positioned boxes** (CSS 2.1 appendix E, simplified): NetSurf
  painted the boxes in the tree's order, so a menu panel positioned over the page (a
  header's absolute dropdown, a sticky bar with a `z-index`) was drawn under what follows
  it. A positioned box (and a flex / grid item with a `z-index`) is now put off while its
  layer — the page, or the positioned box it is in — is painted, then painted after that
  layer's in-flow content, sorted by `z-index` (auto: 0), each a layer of its own; a
  negative `z-index` is painted in place (`html_redraw_layer_z`, `onyx_layer_*`).
- **The box under the pointer** follows the same order (`interaction.c`, `onyx_hit_*`): the
  last box painted there, then its ancestors' links, controls and titles, root first — a
  click on an open menu reaches its link, not the page under it. A click on a link's text
  is the `<a>`'s (NetSurf gave the block's node: a text box has none).

## 7. JavaScript (QuickJS)

NetSurf's JavaScript was Duktape (ES5) with bindings generated by nsgenbind, and the page
was never laid out again after a script changed it. It is now **QuickJS** (QuickJS-ng
0.17.0, ES2023: classes, `let`/`const`, arrow functions, promises, `async`/`await`,
optional chaining...) with the DOM written in JavaScript:

- **`quickjs/qjs.c`** implements `javascript/js.h` on QuickJS: one runtime per window, one
  context per document, `js_exec` for the scripts NetSurf runs as it parses. libdom's nodes
  are wrapped (one JavaScript object per node, for the document's life) and a small set of
  **natives** is given to the prelude: the tree (parent, children, insert, remove, clone,
  attributes, text), `setHTML` (hubbub's fragment parser: innerHTML), the boxes (a node's
  rectangle, whether it is displayed, a few computed properties), the window (its scroll,
  its size), timers, navigation, history, cookies (NetSurf's urldb), `document.write`
  (while parsing), the form controls (`formValue`, `formChecked`: NetSurf's controls, what
  the user typed) and `submit` (NetSurf sends the form: its method, its encoding).
- **`quickjs/dom.js`** (compiled in as a C string, `qjs_dom_js.h`, made by the makefiles) is
  the DOM on those natives: `Node`, `Element`, `HTMLElement` and ~60 element classes,
  `Document`, events (capture, target, bubble; `on*` properties and attributes;
  `preventDefault`), `querySelector`/`matches`/`closest` (its own selector engine: `:is`,
  `:not`, `:has`, `:nth-*`...), `classList`, `dataset`, `style`, `innerHTML`/`outerHTML`,
  `MutationObserver`, `IntersectionObserver`, `ResizeObserver`, `matchMedia`,
  `getComputedStyle`, `localStorage` (kept: a file per origin next to the cookie file,
  `ls-<origin>.json`, written a turn after a change; `sessionStorage` in memory), `URL`,
  `URLSearchParams`, `FormData`, `TextEncoder` / `TextDecoder` (UTF-8 in C: `N.utf8`), timers,
  `requestAnimationFrame`, `location`, `history`, `navigator`.
- **`fetch` and `XMLHttpRequest`**: `fetch` (a promise of a `Response`: `ok`, `status`,
  `statusText`, `url`, `redirected`, `headers`; `text()`, `json()`, `arrayBuffer()`,
  `bytes()`, `blob()`, `formData()`, `clone()`; an `AbortSignal` cancels it), `Headers`,
  `Request`, `Response` (`Response.json`, `.error`, `.redirect`), and `XMLHttpRequest`
  (`readyState` 1 → 4 with `readystatechange`, `loadstart` / `progress` / `load` / `error` /
  `abort` / `timeout` / `loadend`, `responseType` `''` / `text` / `json` / `arraybuffer` /
  `blob`, `setRequestHeader`, `getResponseHeader`, `getAllResponseHeaders`, `timeout`,
  `abort`). Bodies: a string, `URLSearchParams`, `FormData` (url-encoded), a `Blob`, bytes
  (as text: no NUL). On one native, `request(method, url, body, headers, binary, cb)`
  (`qjs.c`): NetSurf's low-level cache with `LLCACHE_RETRIEVE_FORCE_FETCH`, the callback on the
  page's thread (the promises' jobs run after it, a changed DOM laid out again); the requests
  in flight are cancelled when the document goes. Not yet: synchronous XHR (it runs async),
  multipart bodies, CORS checks (every origin answers); streams, `responseXML` and
  `responseType = 'document'`: html5.js (§17).
- **A changed DOM is laid out again** (`html.c`, `html_script_dom_changed`): once a script
  is done, NetSurf builds the document's boxes again (`dom_to_box_now`, synchronously) and
  lays it out — a menu a script opens, a class toggled, nodes added. The old boxes' objects
  (images) are taken over by the new ones (`html_fetch_object`: no fetch, no flicker), the
  form controls kept (their text areas, their typed text; a select's options read again),
  the focus and the iframes moved to the new boxes. A script asking for a rectangle gets
  the new layout at once (`html_script_layout_now`).
- **The browser's events** reach the scripts (`html_script_event`): `mousedown`, `mouseup`,
  `click` at the element under the pointer — a click a script prevents neither follows its
  link nor sends its form —; the pointer's moves (`onyx:hover` from `interaction.c`, every
  move: dom.js makes `mouseover` / `mouseout` with their `relatedTarget`, `mouseenter` /
  `mouseleave` for each ancestor entered or left, and `mousemove`); **CSS `:hover`** -- the
  node under the pointer is kept (`html_content.hover_node`); libcss asks `node_is_hover`
  (`css/select.c`: that node and its ancestors), and when it changes the styles are made again
  (`html_script_dom_changed`) -- only when the style sheets have `:hover` rules (a `:hover`
  selector was tried: `uses_hover`); `keydown`/`keypress`/`keyup` at the focused control or the
  document (a key prevented is not typed); `input` (typing; deferred a turn: the text area
  is not changed under its feet), `change` (checkboxes, radios, a select's menu), `submit`
  (a submit button, Enter; prevented: not sent); the window's `scroll` (the frontend tells
  the core: `browser_window_scrolled`), `resize`, `DOMContentLoaded` (the document parsed),
  `load` (laid out, its images in; `<body onload>`).
- A script runs at most `script_timeout` seconds (NetSurf's option); its recursion is
  stopped past **4 MB** of stack (a `RangeError`). The Onyx app has an 8 MB stack for it:
  `stack = 8M` in `SD:/apps/netsurf.app/app.txt`, read by the kernel (`AppStackSize`,
  `kernel/kernel.cpp`) — the default is 256 KB. `NS_JSDEBUG=1` (the PC bench) prints the
  scripts' errors and `console.log` on stderr.
- **libdom**: a changed `class` attribute updates the element's classes (`element.c`: they
  were cached when the attribute was made, so a `classList` change did not restyle).
- The Duktape backend (`javascript/duktape`, `user/netsurf/gen/duktape`) is no longer built.

## 9. Performance

- **Timings** (`user/netsurf/onyx_perf.h`): with the file `SD:/apps/netsurf.app/perf` (the PC
  bench: `NS_PERF=1`), each step longer than 1 ms is printed -- `ONYX-PERF rebox:boxes`,
  `rebox:reformat`, `layout`, `redraw WxH`, `hover:restyle`, and why a hover built the boxes
  again (`hover:rebox (<reason>)`), each script run (`js:exec <url> (<size>)`) and each call
  into the scripts (`js:call <event>`) -- on stderr, the kernel log on the Pi. The file
  `SD:/apps/netsurf.app/jsdebug` (the PC: `NS_JSDEBUG=1`) prints the scripts' errors and
  `console.log` there too.
- **The scripts do not keep the window from responding** (`frontends/framebuffer/schedule.c`):
  the scheduler runs its due callbacks (the page's timers among them) for 40 ms at most, then
  hands the main loop back to the events (it goes on at once after). A single script is still
  cut off after Choices' `script_timeout` (10 s). Seen on google.com and yahoo.com with the
  Chrome User-Agent (their full script applications): the window stopped responding.
- **CSS `:hover` without the whole page** (`content/handlers/html/onyx_hover.c`): the
  selection notes each node a `:hover` selector is tried on (`nscss_hover_note`, `select.c`);
  when the node under the pointer changes, only the topmost element that left (entered) the
  hover chain among those has its subtree styled again (`box_style_select`, the construction's
  own), without building the boxes. When the new styles differ only in how boxes are painted
  (libcss's `css_computed_style_paint_only_change`: colours, backgrounds, border colours and
  radii, outline, shadows, text decoration, visibility, opacity, z-index, cursor), the boxes
  take them in place -- an element's own boxes and those without a node (text, `::before`,
  markers, an inline's end) matched by owning element *and* style, libcss interning styles --
  the borders' colours copied again, the anonymous boxes' styles derived again, and only their
  rectangles are redrawn. A change of `transform` / `translate` (a card lifted on hover) moves
  the box (its descendants follow) as the layout would, its ancestors' descendant bounds grown.
  Anything else (a layout property, a pseudo-element appearing, a background image to fetch, a
  `:hover` tried on a sibling, a table's borders) builds the boxes again as before
  (`html_script_dom_changed`). On the two sites every hover is now a restyle: no rebox, the
  pixels those of a rebox (`NS_HOVER_FULL=1`: always the rebox, to compare). The replaced style
  results are kept until the next rebox (a box the walk missed would still point at them).
- **libcss**: `css_computed_style_paint_only_change (a, b, &moved)` (`src/select/arena.c`) copies
  the paint properties' bits and values of b over a copy of a's, then compares the rest as the
  interning does; the bits' positions are in `src/select/onyx_propbits.h`, copied from
  `autogenerated_propget.h` (which `#undef`s them) by `onyx_propbits.py`: run it (after
  `select_generator.py`) whenever `select_config.py` changes the layout.
- The windows' `dom.js` checkout: the makefiles strip the CRs before embedding it (a Windows
  checkout, `core.autocrlf`, broke `qjs_dom_js.h`).

## 10. Scripts of the big sites (yahoo.com's Next.js, 2026-09-30)

Found by running a saved copy of yahoo.com on the PC bench (`NS_PERF=1 NS_JSDEBUG=1`):

- **`<script nomodule>` is not run** (`html/script.c`), as in Chrome: it is for the browsers
  without ES modules. Run, yahoo.com's polyfills replaced `Promise`, and `queueMicrotask`
  (built on `Promise`) and the polyfill called each other for ever: the window froze.
- **`queueMicrotask`** uses the engine's own `Promise`, kept when dom.js starts
  (`NativePromise`), whatever a page's polyfill does to `Promise`.
- **`document.currentScript`**: the `<script>` element whose code runs (`js_set_current_script`,
  called around each run by `html/script.c`; `struct html_script` keeps its element) -- webpack
  finds its chunks' path with it.
- The file fetcher types `.js` / `.mjs` (JavaScript), `.json`, `.webp`, `.ico`, `.woff` /
  `.woff2` / `.ttf` / `.otf`, `.txt` (`frontends/framebuffer/fetch.c`): a page saved with its
  scripts runs from the card (they were typed `text/html`: never run).
- `tools/tests/netsurf/pages/js-reactreveal.html`: React 18's streaming reveal (`$RC` / `$RV`),
  its comments, `insertBefore (x, null)`.

## 11. ES modules, CSS feature detection, a CSSOM (css3test.com: 0% -> 23%)

- **ES modules** (`<script type="module">`, inline or `src`): `html/script.c` hands the element
  to the scripts (`js_module_script` -> the event `onyx:module`). dom.js fetches the module and,
  in parallel, every module its imports name (`import` / `export ... from` / `import(...)` found
  in the source: `modGraph`), gives the sources to qjs.c (`moduleSource`), then `moduleRun`
  compiles the root module; QuickJS's loader (`qjs_mod_loader`, `qjs_mod_normalize`: the
  specifier resolved against the importing module's URL) compiles the imports from the sources
  -- a loader cannot wait for the network -- and a source still missing (an import dom.js did
  not see) comes back in moduleRun's result: fetched, then again. `import.meta.url` is set.
  Deferred as in a browser: after parsing, in the document's order, before `DOMContentLoaded`
  (which waits for them), `load` after them; an `async` module as soon as it is ready. No
  import maps (a bare specifier does not load).
- **`CSS.supports` and `element.style` answer from libcss** (they said yes to everything):
  `cssKept (text, inline)` (qjs.c) -> `nscss_text_kept` (`css/select.c`) parses the text as an
  inline style or a sheet and `css_stylesheet_onyx_kept` (libcss, `src/stylesheet.c`) says what
  was kept (rules, declaration bytecode). A property is known when `prop: inherit` is kept; a
  value valid when `prop: value` is; a selector when `sel { color: red }` keeps a rule.
  `'prop' in element.style` is true for the known properties only; an invalid value is not set
  (the old one stays); `CSS.supports (prop, value)` and `CSS.supports ("(a: b) and (not (c: d))",
  "selector(...)")` are real.
- **A read-only CSSOM**: `<style>.sheet` (`cssRules`: the style rules with their valid
  declarations, `@font-face` with the descriptors libcss reads -- font-family, src, font-style,
  font-weight, unicode-range --, `@page`, `@media` / `@supports` with their rules, `@import`,
  `@namespace`; what libcss drops is not there), `document.styleSheets` (the `<style>`s').
  Since made a real, writable CSSOM with the linked sheets (§14).
- css3test.com (Lea Verou's) now runs -- its engine is a module graph of 156 modules -- and
  scores **23%** (1154 of 6419 tests), honestly (what libcss parses; Chrome ~ 75%). It found a
  double free in libcss's Onyx gradient parser (a prefixed legacy `radial-gradient` such as
  `-webkit-radial-gradient(center, circle, ...)`: its buffers freed, then used and freed again --
  `src/parse/properties/onyx_background.c`), which could crash on real pages too.
- `tools/tests/netsurf/pages/js-module.html` (+ `js-mod-a/b/c.js`), in `jstest.sh`: a module
  graph, `import.meta.url`, `import()`, an inline module, the order, `CSS.supports`,
  `element.style`.

## 12. SVG (images and inline `<svg>`, on PlutoSVG)

NetSurf's own SVG handler (`image/svg.c`) needs libsvgtiny, never vendored: SVG images were
not drawn, inline `<svg>` neither (the logos and icons of google.com, bbc.co.uk, Facebook).

- **The image handler** (`content/handlers/image/onyx_svg.c`, registered in `image.c` for
  `image/svg+xml` and `image/svg`; the file fetcher types `.svg`): the document is parsed once
  by PlutoSVG; its intrinsic size is its `width` / `height`, else its viewBox's, else 300 x 150
  (a width alone: 150 high, as Chrome). It is rasterised -- anti-aliased, into a NetSurf bitmap --
  at the size a redraw asks for and kept for the next redraws (six sizes per image, the least
  recently used replaced): a redraw is a bitmap plot, never a rasterisation. An SVG with a
  viewBox is drawn in a viewport of the drawn size (its `preserveAspectRatio` applies), one
  without is scaled, as browsers do for images; a drawing over 2048 x 2048 pixels is rasterised
  smaller and scaled up. `currentColor` is black in an image. `<img>`, backgrounds (tiled too),
  `list-style-image`, `<object>` and `<embed>` use it; a canvas' `drawImage` and favicons get
  its last size (`get_internal`). A bitmap replaced during a redraw is destroyed once the main
  loop turns: the knockout (`desktop/knockout.c`) may still have its plot queued.
- **Inline `<svg>`** (`content/handlers/html/onyx_svg_inline.c`, one call in `box_special.c`'s
  `convert_special_elements`): hubbub builds an `<svg>` and its descendants in the SVG namespace
  (their camelCase names); an outer `<svg>` becomes a replaced box -- its children are not
  converted -- whose object is the subtree written out as SVG text: the elements (their names
  lower-cased: libdom upper-cases an HTML document's; the camelCase ones restored), their
  attributes, a `<style>`'s text, the element's CSS `color` as the root's `color` (its
  `currentColor`), a `var(--x[, fallback])` in a value replaced by the `<svg>`'s custom property
  (libcss: `css_onyx_node_var` reads it from the node data its selection keeps -- google.com's
  menu icon is `fill: var(--IXoxUe)`), the root's `width` / `height` in `em` / `rem` / `ex`
  written in px with the element's font size (bbc.co.uk's logo is `width="7em"`). The elements
  it names by `href="#id"` / `url(#id)` that are elsewhere in the page (an icon sprite's
  `<symbol>`s, a shared gradient) are copied into a `<defs>` at its end. The text is the box's
  object as a `data:image/svg+xml;base64,...` URL through the usual `html_fetch_object`: the
  low-level cache shares one content between identical icons, and the SVG image handler draws
  it at the box's size (CSS `width` / `height` size it as an image). A script's change builds
  the boxes again (`html_script_dom_changed`): a new text, a new URL; so does a `:hover` that
  changes the colour of an inline `<svg>` (`onyx_hover.c` asks `onyx_svg_box_is_inline`: its
  `currentColor` is in its text, a restyle in place would keep the old one --
  `pages/svg-hover.html`). Costs, kept low for the
  reboxes the scripts cause: an element named by id is looked up (libdom walks the tree) and
  written once per main-loop turn, the URLs are kept by their text (256, 512 KB) -- a page of 300
  sprite icons: its rebox 4.4-6.9 ms -> 3.0-5.0 ms on the PC (2.2 ms without the icons).
  `NS_SVGDEBUG=1` prints each SVG text on stderr (the PC bench).
- **PlutoSVG's Onyx patches** (`third_party/plutosvg-0.0.8/README.onyx`): the `<style>` sheets
  (type, class, id selectors, descendant and child combinators, specificity; below `style=""`,
  above the attributes), `clip-path` (`clipPathUnits`, transforms), `<a>` and `<switch>`,
  quoted `url('#id')`, `em` / `ex` / `rem` lengths, `set_size` / `has_view_box`, and
  **`<text>` / `<tspan>`** (a chart's labels): glyph outlines of the card's fonts
  (`image/onyx_vgfont.c`: a CSS family list to Liberation, Gelasio, Selawik or DejaVu, as
  `font_freetype.c` maps Chrome's; shared with the canvas' text), `font-size`, `font-weight`,
  `font-style`, `x` / `y` / `dx` / `dy`, `text-anchor`, filled and stroked as shapes.
- Built for the Pi by `user/netsurf/Makefile` (`libplutovg.a`, `libplutosvg.a`, committed:
  PlutoVG without the font directory scan, stb_image for PNG / JPEG only) and on the PC by
  `host.mk`. Pages: `tools/tests/netsurf/pages/svg-img.html` (`<img>` at three sizes, a
  viewBox-only icon, tiled and sized backgrounds, a `data:` SVG, list markers, `<object>`,
  `<embed>`), `svg-chart.svg` (`<text>`: anchors, a tspan,
  entities), `svg-inline.html` (a sprite's symbols through `<use>`, `currentColor` from the
  link's colour, a gradient from the sprite, a `<style>`, `clip-path`, `var()`, an `<svg>` made
  by `innerHTML`), each against Chromium (`chrome.sh`).

## 13. Canvas 2D (on PlutoVG)

`<canvas>` was a replaced box drawing nothing and `getContext` answered null.

- **`quickjs/canvas.js`** (compiled in as `qjs_canvas_js.h` by both makefiles, as dom.js; run by
  `qjs_canvas_setup` right after dom.js) is the API and keeps its state (the styles, the font,
  the save stack): `HTMLCanvasElement.getContext('2d')`, `width` / `height` (a change clears the
  canvas and its state), `toDataURL` / `toBlob` (PNG); `CanvasRenderingContext2D` --
  `fillRect` / `strokeRect` / `clearRect`, paths (`moveTo`, `lineTo`, `quadraticCurveTo`,
  `bezierCurveTo`, `arc`, `arcTo`, `ellipse`, `rect`, `roundRect` (one radius), `closePath`),
  `fill` (non-zero, even-odd), `stroke`, `clip`, `isPointInPath` / `isPointInStroke`,
  `fillStyle` / `strokeStyle` (CSS colours given back as Chrome does: `#rrggbb` or `rgba()`),
  `createLinearGradient` / `createRadialGradient` / `createConicGradient` (its middle colour:
  PlutoVG has no conic gradient), `createPattern`, `lineWidth`, `lineCap`, `lineJoin`,
  `miterLimit`, `setLineDash` / `getLineDash` / `lineDashOffset`, `globalAlpha`,
  `globalCompositeOperation` (the Porter-Duff ones and `copy`; the blend modes are kept but
  drawn source-over), `save` / `restore`, `translate` / `rotate` / `scale` / `transform` /
  `setTransform` / `resetTransform` / `getTransform` (a 2D `DOMMatrix`, defined if the page has
  none), `drawImage` (3, 5 and 9 arguments), `createImageData` / `getImageData` / `putImageData`,
  `fillText` / `strokeText` (`textAlign`, `textBaseline`, `maxWidth`) and `measureText` (a
  `TextMetrics` with its bounding boxes); shadows, filters and `imageSmoothing*` are kept, not
  drawn. `Path2D` (the path methods, `addPath`, SVG path data), `ImageData`, `OffscreenCanvas`
  (`getContext`, `convertToBlob`, `transferToImageBitmap`), `createImageBitmap`.
- **`quickjs/qjs_canvas.c`**, the natives (`N.cv*`, added to dom.js' natives): each draws on the
  canvas' PlutoVG surface (premultiplied ARGB, anti-aliased) -- thin, one native per call. The
  path is kept in device space, each point through the transform of the moment it is added
  (the HTML canvas' rule); a fill draws it with the identity (the paint in the current user
  space), a stroke maps it back through the current transform (the line width and dashes scale
  with it). Fonts: the card's (`SD:/res/fonts`: a CSS family to Liberation, DejaVu, Gelasio or
  Selawik, as `font_freetype.c` maps them: `image/onyx_vgfont.c`, shared with SVG `<text>`), read
  once by PlutoVG's stb_truetype. `drawImage` /
  `createPattern` take a canvas (its surface), an `<img>` NetSurf fetched (its content's bitmap,
  converted once and kept: 8 images), an SVG image, or an `Image` a script made and never put in
  the page: `canvas.js` completes `HTMLImageElement` -- its `src` fetches it through the
  high-level cache (`cvLoadImage`), then `load` / `error`, `complete`, `naturalWidth`, `width`.
- **The picture**: the element's node carries a NetSurf bitmap as its user data
  (`__ns_key_canvas_node_data`, which `redraw.c` already plots into the canvas' box, scaled to
  its CSS size); after drawing, the surface is copied into it (straight alpha, the client's
  layout) once a main-loop turn -- a scheduled flush -- and the box redrawn
  (`html__redraw_a_box`): a script drawing a thousand shapes costs one copy and one redraw.
- `qjs.c` (Onyx blocks): the setup call, `qjs_node_of` / `qjs_html_of` / `qjs_invoke` for
  `qjs_canvas.c`, and `qjs_canvas_context_gone` before a document's context is freed (its
  canvases stop flushing, its image loads are released). A canvas is at most 4096 x 4096.
- `tools/tests/netsurf/jstest.sh` runs `pages/canvas-api.html` (40 checks: the state, the
  colours' text, pixels after fills / clears / alpha / transforms / gradients, paths, Path2D,
  image data, text metrics, `toDataURL`, an OffscreenCanvas, a pattern, the reset on `width`,
  `drawImage` of the page's SVG `<img>` and of an `Image` loaded by the script);
  `pages/canvas-draw.html` is drawn against Chromium.

## 14. CSS by the specifications' grammars, a real CSSOM, SVG's properties (css3test.com: 23% -> 81%)

css3test.com asks, for 6419 tests over 1262 features, whether the browser *recognizes* a
property, a value, a selector, an at-rule, a descriptor, a media query or a CSSOM interface.
NetSurf now recognizes what the specifications define -- and checks it as a browser does:
an invalid value is still dropped.

- **The value grammars** (`src/parse/onyx_grammar*.c`, `onyx_grammar_gen.py`): the grammar of
  every CSS property and at-rule descriptor ("Value Definition Syntax"), from the W3C's webref
  consolidation of the specifications (`third_party/webref-css-8.7.5`, MIT; `css-grammar.json`
  trimmed from the npm package by `trim.py`), is compiled by `onyx_grammar_gen.py` into one
  node graph (`onyx_grammar_tables.c`, committed: 816 properties, 48 descriptors, ~70 KB of
  tables). The types are shared nodes; the numeric and token types (`<length>`, `<number>`,
  `<custom-ident>`, `<url>`...) are the matcher's primitives. The matcher (`onyx_grammar.c`)
  is a backtracking matcher in continuation-passing style: juxtaposition, `&&`, `||`, `|`,
  `[ ]`, `* + ? {A,B} # !` exactly as the syntax defines them, the omissible commas of CSS
  Values 2.6, a step bound (a pathological value is refused, not a hang). The numeric
  primitives take the math functions, type-checked (a length is not an angle; `px * px` is
  not a value). A function left open at the end of a value is closed there (CSS Syntax), a
  relative colour's channel keywords are numbers (`rgb(from x r g b)`), `calc-size()`'s `size`
  a length. The generator's `FUNC_FIXES` / `PROP_FIXES` fill webref's gaps (circle() takes a
  percentage, `anchor()` in the logical insets, `anchor-center` in the `*-items`, the engines'
  `x-self-start` / `anchors-visible` names, `url-set`).
- **How libcss uses them** (`parseProperty`, `language.c`): a property libcss computes is
  parsed by its own parser as before; if that parser refuses the value, or the property is one
  libcss does not compute (anchor-name, scroll-snap-type, mask-image, text-wrap, the
  `transition-*` / `animation-*` longhands...), the value is checked against the property's
  grammar: valid, it is kept as `CSS_ONYX_OP_GENERIC` (one bytecode word, the grammar's index;
  its cascade does nothing -- an earlier declaration libcss computes keeps applying), invalid,
  it is dropped. A CSS-wide keyword is valid everywhere, a value with `var()`, `env()`,
  `attr()` or `if()` too (substitution functions). `@supports` asks the same thing
  (`css__onyx_declaration_valid`), `selector()` really parses. So `CSS.supports`,
  `element.style` and `@supports` answer as a browser does -- a page's feature detection now
  takes its modern branch where the property exists in the specifications (NetSurf may not
  draw it: that is the price of an honest "recognized").
- **At-rules kept without effect** (`onyx_atrules.c`): `@keyframes`, `@counter-style`,
  `@property`, `@font-feature-values` (and its `@styleset`...), `@font-palette-values`,
  `@position-try`, `@view-transition`, `@scope`, `@starting-style`: their prelude checked,
  kept as a media rule that never matches (`css_rule_media.onyx_kind`), their descriptors
  checked against the descriptor grammars and counted (`css_stylesheet.onyx_desc_words`, read
  by `css_stylesheet_onyx_kept`); @font-face's and @page's descriptors libcss does not read
  (font-display, size-adjust, size, marks...) checked the same way.
- **Selectors 4** (`parsePseudo`, `onyx_parse_extra_pseudo`; matching in `select.c`): the
  pseudo-classes and pseudo-elements of Selectors 4, CSS Pseudo 4 and the other specifications
  are parsed and checked, their arguments too (`:has()`'s relative selectors, `::part()`,
  `::view-transition-*()`'s `<pt-name-selector>`...). Matched when libcss can say:
  `:nth-child(An+B of S)`, `:read-only` / `:read-write`, `:required` / `:optional`,
  `:placeholder-shown`, `:defined`, `:scope`, `:dir(ltr)`; the others (`:has()`, `:host()`,
  `:invalid`..., every pseudo-element) never match -- a list `a:has(b), c` keeps its `c` (the
  whole rule was dropped). Also `*|` namespaces and the `[a=b i]` / `s` flags; the common
  `::-webkit-scrollbar...` pseudo-elements (pages hide scrollbars in lists with them).
- **Units and math functions computed** (`parse/properties/utils.c`): `svh` / `lvh` / `dvh`
  (and `w`, `i`, `b`, `min`, `max`) are the viewport's units; `cqw`... the small viewport's
  (the spec's fallback: containers are not modelled); `cap`, `ic`, `rex`, `rch`, `rcap`,
  `ric`, `rlh` from em / rem with libcss's ratios; `x` is `dppx`. `round()`, `mod()`,
  `rem()`, `abs()`, `sign()`, `sin()`... `atan2()`, `pow()`, `sqrt()`, `hypot()`, `log()`,
  `exp()`, `e`, `pi`, `infinity` are folded when parsed (numbers, angles, or lengths of one
  unit), alone or inside `calc()`. `height: 100dvh` now sizes a box.
- **SVG's presentation properties** (`parse/properties/onyx_svg.c`,
  `select/properties/onyx_svg.c`): `fill`, `stroke` (none, a colour, `currentColor` kept as
  such, `url()` and its fallback, `context-fill` / `-stroke`), `stroke-width`,
  `stroke-dashoffset`, `stroke-miterlimit`, `stroke-dasharray` (a canonical text),
  `fill-rule`, `stroke-linecap`, `stroke-linejoin` (inherited), `stop-color`, `stop-opacity`
  are computed (`select_config.py`; the autogenerated headers made again; `onyx_propbits.py`
  now makes `onyx_propbits.h`) and read by `css_computed_fill()`... (`computed.h`), so a
  page's `.icon { fill: currentColor }` reaches an inline `<svg>`. An SVG element's
  presentation attributes (`fill="#f00"`, `stroke-width="3"`...) are NetSurf hints
  (`css/hints.c`, `css_hint_svg`): the lowest author precedence, as in browsers (a colour
  attribute in `rgb()` is not read: `nscss_parse_colour`'s forms only). `getComputedStyle`
  answers `fill`, `stroke`, `stroke-width`.
- **A real CSSOM** (dom.js, "the CSSOM" block): `CSSRule` (its type constants) and its
  classes as globals -- `CSSStyleRule`, `CSSMediaRule`, `CSSSupportsRule`, `CSSContainerRule`,
  `CSSLayerBlockRule` / `StatementRule`, `CSSScopeRule`, `CSSStartingStyleRule`,
  `CSSImportRule`, `CSSNamespaceRule`, `CSSFontFaceRule`, `CSSPageRule` / `MarginRule`,
  `CSSKeyframesRule` / `KeyframeRule`, `CSSCounterStyleRule`, `CSSPropertyRule`,
  `CSSFontFeatureValuesRule`, `CSSFontPaletteValuesRule`, `CSSPositionTryRule`,
  `CSSViewTransitionRule`, `CSSNestedDeclarations`, `CSSGroupingRule` / `CSSConditionRule` --
  `StyleSheet` / `CSSStyleSheet` (constructible; `replace` / `replaceSync`, `insertRule` /
  `deleteRule` / `addRule` / `removeRule`), `StyleSheetList`, `CSSRuleList`, `MediaList`;
  `rule.style` a `CSSStyleDeclaration` over the rule's valid declarations (or descriptors).
  A sheet's rules are read from its text when first asked for, as libcss keeps them (each rule,
  declaration and descriptor asked of `N.cssKept`); a change is written back to the text -- a
  `<style>`'s: NetSurf styles the page again. `document.styleSheets` holds the `<link>` sheets
  too (qjs.c `sheetText`: the loaded sheet's text and URL; `html/css.c` keeps a `<link>`'s node
  with its sheet); `document.adoptedStyleSheets` realizes each constructed sheet as a
  `<style data-onyx-adopted>` at the end of the `<head>`. (google.com's script stopped at
  `x instanceof CSSStyleRule`.)
- **Two bugs found on the way**: libparserutils' own charset filter lost the characters its
  codec had decoded but the pivot had no room for when the input was all read -- the last
  character of a 65-byte inline style (a `)`: the declaration dropped), possibly a sheet's
  last bytes (`src/input/filter.c`); and the input stream reported the end while the filter
  still held converted data (`inputstream.c`, `parserutils__filter_pending`). libcss's parser
  now closes a function or bracket left open at the end of the input (`parse.c`: the
  declaration is kept, as CSS Syntax says).
- **The score** (`tools/tests/netsurf/css3test.sh [netsurf|chrome|both]`: fetches the site's
  sources -- git branch v1 of github.com/LeaVerou/css3test, what css3test.com serves -- into
  `$OUT/css3test`, runs them in NetSurf and in Chromium, prints the score and a line per spec
  and per failed test): **81%** (5150 of 6419 tests; 23% before); Chromium 139 headless 71%
  on the same copy. NetSurf passes 1245 tests Chromium fails -- features of drafts no browser
  ships (css-speech, css-borders-4's corner shapes, fill-stroke-3...) whose grammar is in the
  specifications -- and fails 471 Chromium passes (the Typed OM's interfaces, Web Animations,
  CSSOM View: scripts' APIs, not parsing). Counting only what Chromium also passes, NetSurf
  scores 64% (Chromium 71%).
- **Cost**: parsing a big sheet takes 0.3% (bulma, 763 KB), 2.7% (bootstrap, 281 KB) to 8%
  (material-components-web, 624 KB: many properties libcss does not compute, now checked and
  kept) more instructions (`csscheck -b`, callgrind); libcss.a's code +114 KB on the Pi (the
  tables ~70 KB). kotonviolins.com and kotonstudio.com draw pixel-identical.
- **Tests**: `make -f tools/tests/netsurf/host.mk css-check` (`csscheck.c` on
  `css-values.txt`: 157 declarations, sheets and descriptors kept or dropped; `csscheck -b N
  sheet.css`: a parse-speed bench); `jstest.sh`: `js-cssom.html` (26 CSSOM checks),
  `css-svgprops.html`, `css-selectors.html`, `css-math.html`.
- **Not done**: the Typed OM (`CSSStyleValue`, `attributeStyleMap`...), Web Animations,
  CSSOM View's interfaces; `:has()` matching (libcss's handler has no child walk);
  `@container` against a real container; the pseudo-elements' drawing (`::marker`,
  `::placeholder`...); a math function over relative lengths (`round(1em, 3px)`) computed.
## 15. Intl (ECMA-402) and the locale built-ins

QuickJS-ng is built without `Intl`: bbc.co.uk's and bbc.com's Next.js applications stopped on
"Intl is not defined" (their React tree crashed, the page went blank), youtube.com too. NetSurf
now has its own implementation, in JavaScript:

- **`quickjs/intl.js`**: `Intl.DateTimeFormat` (`dateStyle` / `timeStyle`, every component option,
  `hour12` / `hourCycle`, `timeZoneName` in its six forms, `format`, `formatToParts`,
  `formatRange`, `formatRangeToParts`, `resolvedOptions`), `Intl.NumberFormat` (decimal, percent,
  currency with symbol / narrow symbol / code / name and accounting, unit with the simple units
  and their `-per-` compounds; integer, fraction and significant digits, the nine rounding modes,
  rounding increments and priorities, `trailingZeroDisplay`; grouping; standard, scientific,
  engineering, compact short and long notations; `signDisplay`; strings and BigInts exactly;
  `formatRange` with CLDR's collapsing), `Intl.PluralRules` (cardinal and ordinal, `selectRange`),
  `Intl.RelativeTimeFormat`, `Intl.ListFormat`, `Intl.Collator` (a simplified Unicode collation:
  base letters with accents folded at the primary level, accents, case; `numeric`, `caseFirst`,
  `sensitivity`, `ignorePunctuation`; Spanish ñ and the Nordic letters after z), `Intl.Segmenter`
  (graphemes with the Unicode properties of QuickJS's regular expressions; words and sentences
  approximated, Chinese / Japanese / Thai a character at a time), `Intl.DisplayNames` (languages,
  regions, scripts, currencies, calendars, date fields), `Intl.Locale` (with `maximize` /
  `minimize` from CLDR's likely subtags), `Intl.getCanonicalLocales`, `Intl.supportedValuesOf`.
  Language tags are parsed and canonicalised as UTS 35 says (aliases, extensions); the legacy
  constructor behaviour (`Intl.NumberFormat.call (obj)`) is there.
- **The built-ins** take their ECMA-402 versions: `Number.prototype.toLocaleString`,
  `BigInt.prototype.toLocaleString`, `Date.prototype.toLocaleString` / `toLocaleDateString` /
  `toLocaleTimeString`, `Array.prototype.toLocaleString` and the typed arrays', `String.prototype.
  localeCompare`, `toLocaleLowerCase` / `toLocaleUpperCase` (Turkish, Azeri, Lithuanian). The
  formatter of the calls without arguments is kept (a table of numbers formats fast).
- **The locales**: English (US, GB, AU, CA, IN, IE, NZ, ZA; the other English regions as GB),
  French (FR, CA, BE, CH), German (DE, AT, CH), Spanish (ES, MX, US, 419, AR; the other
  Latin-American regions as 419), Italian, Dutch (NL, BE), Portuguese (BR, PT; the other regions as
  PT), and, for formatting only (their units, display names and time zone names are English's):
  Japanese, Chinese (simplified, traditional), Korean, Russian, Polish, Swedish, Danish, Norwegian
  Bokmål, Finnish, Turkish, Czech. Any region of these languages is accepted (`fr-LU`).
  Another language resolves to the default locale, as the specification says.
- **The default locale is `navigator.language`** (dom.js: `fr-FR`), as in Chrome for a French
  user. A page that formats without a locale gets French, as it would in Chrome in France.
- **The data** (`third_party/cldr-48/intl-data.txt`, 450 KB, Unicode License v3) is CLDR 48 as
  ICU 78 has it: `tools/tests/netsurf/intl/gendata.js` reads every pattern and name back from
  Node.js's own `Intl` (full ICU, Chrome's data) by formatting probe values -- the date patterns
  of ~160 option combinations per locale, the number templates, the compact and unit patterns
  per plural category, the relative-time and list patterns, the display names, the time zone
  names; a regional locale keeps only what differs from its language. CLDR's narrow no-break
  space in times is a plain space, as Chrome shows it. The plural rules are code (`PLURAL`).
- **Time zones**: every IANA zone (418, and the common links) with its standard offset and its
  current daylight-saving rule (EU, US, Australian, New Zealand, Chilean, or the zone's own:
  Cairo, Jerusalem, Havana...) found by the generator from the 2026-2028 transitions --
  historical changes are not kept (a 1990 date gets today's rule). Offset time zones (`+05:30`),
  `Etc/GMT±N`; a zone name is kept as written (case-normalised). **The default time zone**: the
  host's name (the PC: `TZ`, `/etc/localtime`) when the `Date` agrees with it, else the zone of
  the locale's region whose offset matches `Date`'s (`fr-FR`: Europe/Paris), else a popular one,
  else `Etc/GMT±N`: `resolvedOptions ().timeZone` always agrees with `getTimezoneOffset ()`.
- **Onyx's clock**: `gettimeofday` gives the local time (the kernel's clock is UTC + the
  `timezone=` of `SD:/etc/system.ini`), so `Date.now ()` was off by that offset and
  `getTimezoneOffset ()` was 0. `quickjs.c` (under `__ONYX__`, README.onyx) now takes
  `js_onyx_utc_offset_min` off `Date.now` and answers it in `getTimezoneOffset`; `qjs_intl.h`
  reads `timezone=` once. Onyx has no daylight-saving rule of its own: the offset is the one the
  Setup wrote (a date in the other season is still shown at today's offset by `Date`; `Intl`
  shows it with the zone's rule).
- **Lazily loaded, compiled once**: `intl.js` has two parts, split at the line `//@@INTL-IMPL@@`
  by `qjs_intl.h` (included by `qjs.c`; `qjs_intl_init (ctx)` before dom.js). The boot part runs
  in every document's context: the `Intl` object, whose members are accessors that load the
  implementation at their first use and then become ordinary data properties, and the
  built-ins. Both parts are compiled once per process and kept as bytecode (`JS_WriteObject`,
  the process's heap), read back in each context. The locale data stays a C string in the
  binary: a record (`"en-GB/d"`) is found and `JSON.parse`d when a formatter first needs it.
  Cost on the PC (`tools/tests/netsurf/intl/qjsintl -m`): a context 0.12 ms, + 0.09 ms for the
  boot; the first `Intl` use in a context 2 ms (12 ms the very first time: compiling). The binary
  grows by ~575 KB (the data and intl.js as strings).
- **Tests**: `tools/tests/netsurf/pages/js-intl.html` (made by `intl/mkpage.js` from
  `intl/cases.js`: 233 expressions with Node's answers as the expected values; in `jstest.sh`),
  `intl/compare.sh` (the same cases in Node and in `qjsintl`, QuickJS with the Intl on the PC:
  `intl/build.sh`), `intl/test262.js` (test262's intl402 in `qjsintl`: 1061 of the 1146 tests
  run pass, 92.6%; Temporal, DurationFormat and a few features are skipped). Live: bbc.co.uk,
  bbc.com and youtube.com no longer stop on `Intl`.

## 16. The HTML parser (libhubbub, libdom's binding, libparserutils)

NetSurf's parser, hubbub, followed the HTML5 drafts of about 2008: no `<template>`, a
different `<select>` and `<table>` handling, the character references of that time, no
fragment parsing in a context element. It now follows the current standard (the WHATWG
tokenizer and tree construction), measured by the **html5lib-tests** (the test suite the
browsers' parsers share; `tools/tests/netsurf/html5lib.sh`):

| | Before | After |
|---|---|---|
| Tokenizer (`tokenizer/*.test`) | 6739 / 7032 (95.8%) | 7024 / 7028 (99.9%) |
| Tree construction (`tree-construction/*.dat`, documents and fragments, scripting on and off) | 930 / 1716 (54.2%) | 1792 / 1792 (100%) |

(The old count missed the tests with a NUL in them: 1716 of the 1792. The 4 tokenizer tests
left start with a U+FEFF that libparserutils' decoder takes off as a byte order mark, as a
browser does for a document; `xmlViolation.test` is for XML-style parsers and is skipped. The
tree tests also pass fed in chunks of 1, 2, 3, 7 and 64 bytes, and under valgrind.)

- **The tokenizer** (`libhubbub/src/tokeniser/tokeniser.c`) is written again from the
  standard's state machine: every state, the character reference states with the standard's
  **2231 named references** (`onyx_entities.inc`, generated by `build/make-onyx-entities.py`
  from the standard's `entities.json`; searched by bisection -- the old trie, `entities.c`,
  and its Perl generator are gone), the numeric references' replacement table, CR / CRLF
  made LF, the script data escape states, CDATA sections in foreign content. The text runs
  between markup are handed on without a copy (a table of the bytes that stop each text
  state). New options: `HUBBUB_TOKENISER_LAST_START_TAG` (an appropriate end tag in RCDATA /
  RAWTEXT / script data when a fragment starts in such an element) and the content models
  `SCRIPTDATA` and `CDATA_SECTION` (`include/hubbub/types.h`).
- **The tree builder** (`src/treebuilder/treebuilder.c`, one file: the old per-mode files,
  `element-type.*`, `internal.h`, `modes.h` are gone) has every insertion mode of the
  standard: the stack of open elements and the list of active formatting elements (with the
  Noah's ark clause), the adoption agency algorithm, foster parenting, `<template>` (its own
  insertion modes; its children go to the template's contents fragment), foreign content
  (SVG and MathML, their attribute and tag name adjustments, the integration points), the
  "in select" handling of the current standard (the relaxed `<select>` parser: other
  elements allowed in a select) with **`<selectedcontent>`** (a copy of the selected
  option's contents), frameset, quirks mode from the doctype, and **fragment parsing** in
  a context element (innerHTML, `createContextualFragment`...). Consecutive characters are
  gathered and inserted as one text node (`insert_text`, not one call per character).
- **The API** (`include/hubbub/tree.h`, `parser.h`; appended, the old fields kept): the tree
  handler's `template_content` (a template's contents fragment) and `insert_text` (append to
  the preceding text node); the parser options `HUBBUB_PARSER_LAST_START_TAG` and
  `HUBBUB_PARSER_FRAGMENT_CONTEXT` (the context element: namespace, name, node, form
  element, whether it is an HTML integration point, the quirks mode);
  `hubbub_treebuilder_flush` (the text gathered so far inserted -- `parser.c` calls it at the
  end of each chunk and when a script pauses the parser).
- **libdom's binding** (`libdom/bindings/hubbub/parser.c`, its copy in
  `libdom/include/dom/bindings/hubbub/`): `add_attributes` adds only the missing ones (the
  standard's `<html>` / `<body>` merge); the templates' contents are a document fragment held
  as the element's user data (`dom_hubbub_template_content`); `insert_text`;
  `dom_hubbub_fragment_parser_create_ctx` (a fragment parser in a context element: qjs.c's
  `setHTML`); the elements made as the parser names them (`_dom_html_document_create_element_parser`:
  no prefix split of `xlink:href`-like names; attribute names that are not XML names -- the
  standard allows them -- are kept, `_dom_element_parser_attrs`).
- **libdom**: an element in another namespace than HTML keeps its name's case (`viewBox`,
  `foreignObject`: they were made upper case) and has no HTML type (only `<style>` keeps
  its type: its sheet); attribute names are lower-cased on HTML elements only; the document
  element and the doctype can be removed (`node.c`).
- **libparserutils**: the input filter emptied its decoder after a full output buffer (the
  last character of a 65-byte input was lost: `filter.c`, `inputstream.c`); U+FFFE / U+FFFF
  are valid UTF-8 (`utf8impl.h`).
- **Speed** (the PC, `html5lib.sh time <page>`, best of 20, fed in 32 KB chunks as the
  fetcher gives them): bbc.co.uk (668 KB) 15.7 -> 10.7 ms, bbc.com/news 8.1 -> 6.2 ms,
  a Wikipedia article 24.1 -> 17.5 ms, google.com 3.0 -> 2.2 ms.
- `html/box_construct.c`: the text of a closed `<details>` other than its `<summary>` makes
  no boxes (`onyx_in_closed_details`).
- The PC tests (`tools/tests/netsurf/`): `html5lib.sh [tree|tok|time <page>] [-v] [files]`
  (the tests are cloned once, at the last html5lib-tests commit that has the
  tree-construction tests); the drivers `html5lib_tree.c` (`-c N`: fed N bytes at a time),
  `html5lib_tok.c` + `html5lib_tok.py`, `html5lib_time.c`; `html5lib.mk` builds them.

## 17. The HTML5 DOM (`quickjs/html5.js`)

**`html5.js`** is a second prelude, run after dom.js (compiled in the same way:
`qjs_html5_js.h`); qjs.c calls it with the natives and dom.js's table of element classes
(`TAGS`). It extends and corrects dom.js's classes, and lists what it does in its header.
Measured by **html5test.co** (Niels Leenheer's test, `tools/tests/netsurf/html5test.sh`: its
engine run in the PC NetSurf, the score and every missing feature printed): **252 -> 314 of
588** (Chrome about 530). Every feature it counts works; nothing answers "supported" without
doing it. dom.js changed in two places only: `N.internals` (its mutation queue, observers,
dispatch, for html5.js) and its selector engine asking `N.internals.pseudo` for the
pseudo-classes it does not know.

- **Namespaces**: `namespaceURI`, `localName`, `tagName`, `prefix`, the `*NS` methods; SVG
  and MathML elements get their own classes (qjs.c's `qjs_proto_for` looks up `svg:<name>`,
  `svg:*`, `math:*`), an unknown HTML name is an `HTMLUnknownElement`, a valid custom
  element name an `HTMLElement` (`*custom`); attribute names lower-cased on HTML elements only.
- **Fragments on the new parser**: `innerHTML` (qjs.c's `setHTML`: parsed in its context
  element -- a `<tr>` into a `<tbody>`, text into a `<textarea>` or `<title>` decoded, a
  `<script>` or `<style>` raw), `outerHTML`, `insertAdjacentHTML`, `setHTMLUnsafe`,
  `getHTML`, `Range.createContextualFragment`; the standard's serialization (escaping,
  void elements, raw text); `<template>`'s `content` (cloned and imported with the
  template); `DOMParser` (HTML, and XML with a small parser: `parsererror` on bad XML),
  `XMLSerializer`, `Document.parseHTMLUnsafe`, `document.implementation.createHTMLDocument`
  / `createDocument`; `ownerDocument`, `importNode`, `adoptNode`, `cloneNode` across
  documents (qjs.c: `createDocument`, `createIn`, `importTo`, `parseDocument`).
- **Messaging**: `MessageChannel` / `MessagePort` (`port1` / `port2`, `postMessage`,
  `onmessage`, `start`, `close`), `window.postMessage` with a `MessageEvent` (`origin`,
  `source`), `BroadcastChannel` -- delivered in a task, not a microtask -- and
  `structuredClone` (the structured clone algorithm: cycles, `Map`, `Set`, `Date`, `RegExp`,
  typed arrays, errors; a `DataCloneError` for functions and nodes).
- **Forms**: the input types (email, url, tel, search, number, range, date, month, week,
  time, datetime-local, color) with the standard's value sanitization, `valueAsNumber`,
  `valueAsDate`, `stepUp` / `stepDown`; the constraint validation API (`validity` with every
  flag, `willValidate`, `checkValidity`, `reportValidity`, `setCustomValidity`,
  `validationMessage`, the `invalid` event); an invalid form is not sent (a capture listener
  on `submit`; `novalidate`, `formnovalidate`); the `form` attribute; `<output>`,
  `<fieldset>` `elements`, `<datalist>`, `<meter>`, `<progress>`; the pseudo-classes
  `:valid`, `:invalid`, `:in-range`, `:out-of-range`, `:read-write`, `:read-only`,
  `:indeterminate`, `:default`, `:open`, `:modal`, `:defined`.
- **`<dialog>`, `<details>`, `hidden`**: `show`, `showModal`, `close`, `returnValue`, a
  `<form method="dialog">` closing its dialog, the `close` / `cancel` / `toggle` events;
  `<details>` `open` with its `toggle` event; the global attributes (`hidden`, `translate`,
  `accessKey`, `contentEditable`, `isContentEditable`, `draggable`, `spellcheck`; the
  `popover` attribute reflected, no popover behaviour). The drawing: `default.css`'s block marked "Onyx HTML5" (`[hidden]`,
  `<template>`, `<datalist>` not drawn; a closed `<dialog>` not drawn, an open one centred;
  a closed `<details>` shows its summary only); `getComputedStyle` of an element without a
  box (display: none, a hidden subtree, a script's reading before the first layout) is its
  selected style (qjs.c: `qjs_unboxed_style`, with the style sheets already loaded --
  `html_css_new_selection_context` skips those still coming); a script's reading of a size
  lays out its changes first (`qjs_layout_now`, 16 times a turn at most).
- **Custom elements**: `customElements.define` / `get` / `getName` / `whenDefined` /
  `upgrade`; the element's constructor runs on `createElement` and `new`, and on the elements
  already there once they are in the document (an upgrade); `connectedCallback` /
  `disconnectedCallback` on insertion and removal, `attributeChangedCallback` for the
  `observedAttributes`; `attachInternals` (`ElementInternals`: a value, a validity). The
  parser's elements: libdom's `DOMNodeInserted` calls `js_handle_new_element`, which keeps
  the element (the DOM is read-only during a mutation event) for html5.js' hook
  (`N.ceHook`), run before the next script or from the scheduler; the scripts' insertions are
  seen by html5.js itself (it wraps the natives `insert`, `remove`, `setHTML`, `setText`).
  Customized built-in elements (`{ extends }`) are recorded but not upgraded, as in Safari.
- **The session history**: `history.pushState` / `replaceState` change the document's URL
  (qjs.c's `setURL`: `location` and the address bar show it; no navigation), `back` /
  `forward` / `go` between the entries the page made fire `popstate` (and `hashchange`),
  past them NetSurf's own history; `location.hash` set makes an entry and scrolls.
- **Streams**: `ReadableStream` (its reader, a BYOB reader -- a byte stream reads as a
  default one and the BYOB reader copies --, `tee`,
  `pipeTo`, `pipeThrough`, async iteration, `ReadableStream.from`), `WritableStream`,
  `TransformStream`, the queuing strategies, `TextEncoderStream` / `TextDecoderStream`,
  `Response.body` and `Blob.stream()`.
- **Files**: `Blob` holds bytes (`size`, `type`, `slice`, `text`, `arrayBuffer`, `bytes`,
  `stream`), `File`, `FileReader` (text, data URL, array buffer, binary string, its events),
  `FileList`, `URL.createObjectURL` / `revokeObjectURL` (`fetch` reads `blob:` URLs).
- **Smaller APIs**: microdata (`itemScope`, `itemProp`, `itemValue`, `properties`,
  `document.getItems`), `<ol reversed>` (NetSurf's layout already numbers it backwards),
  `performance.mark` / `measure` / `getEntries*` and `PerformanceObserver`,
  `XMLHttpRequest`'s `responseType = 'document'` and `responseXML`.
- `tools/tests/netsurf/pages/js-html5.html`, `js-forms.html`, `js-apis.html`, `js-ce.html`
  (in `jstest.sh`): 44 + 45 + 26 + 17 checks. `html5test.sh` starts the run at the load, as
  html5test.co does (it waits for its browser detection script).
- Not done (html5test counts them): Web Workers, EventSource and WebSocket (the requests are
  delivered whole), IndexedDB, the editing APIs (`designMode`, `execCommand`), a real shadow
  tree (`attachShadow` returns the host, whose children are drawn), `<input type="image">`'s
  sizes, and what is out of this work: canvas, SVG, audio and video, WebRTC, WebGL.

## 18. The big sites: memory, requests, events, early layout (bbc.co.uk, google.com, m.facebook.com)

Found on the live sites with the PC bench, which now reaches the network (https through the
PC's OpenSSL, an HTTPS proxy's CONNECT, JPEG and WebP decoded: `site.sh` draws a live site in
NetSurf and in Chromium, `layoutdiff.sh` compares their boxes, `sitesweep.sh` loads a list of
sites and counts crashes and script errors, `NS_MEMSTAT` prints the heap in use, `NS_JSDUMP`
writes the scripts that failed, `NS_INJECT` + F5 runs a script in the page).

- **bbc.co.uk's "out of memory"**: its consent script bundles core-js, which replaced the
  engine's `Promise` because `PromiseRejectionEvent` was missing, and its polyfill queued
  microtasks for ever -- 1.5 GB in a minute on the PC. `PromiseRejectionEvent` exists; the
  promises' jobs run 200 ms at most per turn and the rest a turn later (a runaway chain no longer
  holds the window, `qjs_leave`); the scripts' heap is limited to 384 MB (an allocation past it
  throws in the script instead of stopping the app). bbc.com now takes ~170 MB on the PC,
  steady.
- **The requests' Fetch Metadata** (`onyx_fetch.c`, `onyx_add_fetch_metadata`): Chrome's
  `Sec-Fetch-Site` / `-Mode` / `-Dest` / `-User`, `Upgrade-Insecure-Requests`, an `Accept` by
  destination and, on https, the User-Agent client hints (`sec-ch-ua`, `-mobile`,
  `-platform`). m.facebook.com answered a navigation without `Sec-Fetch-Mode` with a 400 ("Sorry,
  something went wrong"). The destination comes from what the caller accepts (`hlcache.c`: an
  `X-Onyx-Dest` header the fetcher takes off; web fonts and script requests say theirs).
- **Load and error events** of `<script src>` (run or failed, inserted ones too), `<link>` /
  `<style>` sheets and `<img>` (NetSurf's picture); the window's `load` waits for the fetches
  still going (an inserted script delays it) and an element's `load` does not reach the window
  (duckduckgo.com's capturing window listener re-added itself at each image: a loop).
  `<img>.complete` / `naturalWidth` from NetSurf's picture (`N.image`); a script's own images
  (`new Image()`) are canvas.js's (fetched, decoded, `load` / `error`). Facebook's bootloader
  waits for its modules' load events.
- **A script inserted by a script** runs outside libdom's mutation-event guard (libdom refused
  every change it made: yahoo.com's loaders' "bad attribute"; `dom_document_onyx_mutation_guard`).
  `innerHTML` of a raw text element (`script`, `style`...) is its text, of `textarea` / `title`
  its decoded text (a consent stub inserted as `script.innerHTML` lost every `<...>`).
- **Form controls outside any form** are kept for the document's life (`orphan_controls`): a
  rebox found them no more -- what the user typed in a React app's input (Facebook's login)
  was lost at each script change.
- **Early layout** (`html_early_layout`): a script that asks for a geometry while the document
  is still parsed gets boxes built and laid out from the nodes parsed so far (no object
  fetched), made again when the DOM changed, given up at the conversion. google.com's footer
  script set `#gb-main`'s min-height from its `scrollHeight` (0 without boxes): the footer
  covered the search box. **Script-blocking style sheets**: a parser-inserted inline script
  waits (the parser paused) while a style sheet -- a `<style>` still to be converted too -- is
  fetched, as a browser's do (`html_script_sheets_arrived`).
- **A style attribute** is the author's with the specificity (1,0,0,0) (libcss `select.c`): it
  kept the last sheet's origin and selector's specificity, and lost to an `<img width>`'s
  presentational hint after the user sheet (bbc's images kept their attribute size).
- **URL / URLSearchParams** after the WHATWG URL Standard (dom.js: the basic URL parser's state
  machine, IPv4 / IPv6 / IDN hosts, the percent-encode sets, every setter; the WPT's
  urltestdata 896/896, setters 278/278, toascii 72/87 -- `urltest.sh`).
- **Dynamic `import()`** of a URL computed at run time: rewritten at compile time to
  `__onyxImport(base, x)` (`qjs_rewrite_import`: strings, comments and methods named import
  skipped), which fetches the module graph then calls the engine's import (QuickJS's loader
  cannot wait for the network: developer.mozilla.org's chunks).
- **SVG made by scripts** (React): `createElementNS` creates the element in its namespace, the
  prototypes are chosen by namespace, the names keep their case (`N.name`).
- **Web fonts** of the sheets added after the conversion (Facebook's bootloader adds its sheets:
  Optimistic 95 was never fetched), `format("woff2-variations")` read as WOFF2 (libcss), and the
  **CSS Font Loading API**: `FontFace` (a URL or bytes) and `document.fonts` (a `FontFaceSet`:
  `add`, `ready`, `load`, `loading` / `loadingdone`), a script's faces given to NetSurf's font
  code (`N.addFontFace` -> `onyx_webfont_add_script_face`) and the page laid out again.
- **Style sheets after the layout**: a `<style>` or `<link>` a script adds, changes or takes
  out once the page is laid out now restyles it -- NetSurf ignored them ("NS layout is
  static"): the selection context is made again from the sheets there now and the boxes built
  again (`html_css_restyle`, css.c; `html_css_node_removed` from `DOMNodeRemoved`,
  dom_event.c). Facebook's Bloks screens (`.wbloks_1 { display: flex }`, a `<style>` added by
  the script) were laid out as blocks. The UA sheet hides `link`, `meta`, `base`, `area`,
  `param` too (a `<link>` in the body made a line). Test: js-latesheets.
- **The body's overflow is the viewport's** (CSS Overflow 3, 3.3; libcss `select.c`): when
  the root's overflow is visible, the body's computed overflow is made visible (NetSurf's
  window scrolls) -- `body { height: 100%; overflow-y: scroll }` (en.wikipedia.org's Minerva)
  was a scroller of the window's height inside the window, `body { overflow: hidden }` cut the
  page. (getComputedStyle shows the used value, visible.) Test: css-bodyoverflow.
- **The box tree is built in slices of 15 ms**, not of 10 elements (box_construct.c): the
  scheduler runs the next slice at the main loop's next turn only, and a 5000-element page
  (Wikipedia) took hundreds of turns -- the throbber turned for seconds, the early boxes shown.
- **The connects wait asleep** (onyx_fetch.c): the downloads' connects are made one at a time
  and the other workers spun on `kapi_lock` meanwhile (a connect: DNS + TCP, 100 ms and more)
  -- on bbc.com 60 % of the process's CPU; they sleep 5 ms at a time now (the same run: 4080
  -> 1335 ms of CPU). Found with the bench's sampling profiler: `NS_PROF=<file>` (host_stubs.c,
  SIGPROF + backtrace) then `sh tools/tests/netsurf/prof.sh <file>` (self and total per function).
- **A context's prelude compiled once**: dom.js, html5.js and canvas.js are compiled by the
  first context of the process, their bytecode kept and read back in the next ones
  (`qjs_eval_cached`, as Intl's): on the PC a context's prelude went from ~37 ms to ~5 ms
  (a page's iframes, each navigation). Timed as `js:prelude` (NS_PERF).
- Smaller: `DOMStringMap`; `addEventListener` & co called unbound are the window's;
  `localStorage`'s Proxy keeps the Proxy invariants (`Object.keys(localStorage)` threw);
  inline scripts are named by their first characters in errors and timings.
- Tests: `jstest.sh` gained js-microloop, js-rawtext, js-loadevents, js-url, js-dynimport,
  js-svgns, js-fontface.

## 8. Known gaps

- JavaScript: synchronous XHR (runs async), multipart request bodies; no Workers,
  EventSource, WebSocket, IndexedDB, editing APIs, shadow trees (§17); CSS `:active` /
  `:focus`.
- `opacity`, filters, animations and transitions.
- SVG: no `<mask>`, `<pattern>`, `<marker>`, filters, SMIL animations, `<textPath>`, per-glyph
  position lists, the page's web fonts in `<text>`; the page's
  CSS `fill` / `stroke` (`.icon path { fill: red }`) do not reach an inline `<svg>` -- libcss
  has no `fill` / `stroke` properties (only `fill-opacity` / `stroke-opacity`): with them
  (inherited, as text), `onyx_svg_inline.c` would write the `<svg>`'s computed ones on its
  root; its `color`, its custom properties and the SVG's own attributes and `<style>` do. An
  `<svg>` / SVG `<img>` with only a viewBox and no CSS size takes the viewBox's size (Chrome:
  the containing block's width); a `list-style-image` SVG without a size is drawn at its
  viewBox's size (Chrome: small). The scripts' SVG DOM is dom.js' generic one (`namespaceURI`,
  `getBBox` from the box).
- Canvas: no shadows, filters, blend modes (source-over), conic gradients (a solid colour),
  per-corner `roundRect` radii; `drawImage` scales with the nearest pixel (PlutoVG's
  textures), and a canvas shown at another CSS size too (the framebuffer's bitmap plot);
  no `getContext('webgl')`, `captureStream`, video frames; the page's web fonts are not used
  (the card's fonts by family).
- A face split by `unicode-range` outside Latin-1 falls back to the card's fonts.
- Intl (§15): no `Intl.DurationFormat`, no Temporal; the Gregorian calendar only (another
  `calendar` falls back to it, no `relatedYear` / `yearName`); a date range's CLDR interval
  patterns are approximated (the shared fields, the locale's dash); no collation tailorings
  but Spanish and Nordic, no `co` types (phonebook, pinyin...); word and sentence segmentation
  without dictionaries; time zones with their current rules only (no history), four zones with
  irregular rules (Casablanca, El Aaiun, Gaza, Hebron) at a fixed offset; the lean languages
  (ja, zh, ko, ru, pl, sv, da, nb, fi, tr, cs) have English units, display names and zone names.
