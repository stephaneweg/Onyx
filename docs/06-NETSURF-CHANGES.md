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
| `third_party/brotli-1.1.0/` | Brotli's decoder only (FreeType's WOFF2), MIT |
| `third_party/quickjs-ng-0.17.0/` | QuickJS-ng, the JavaScript engine (ES2023), MIT: the engine alone (`README.onyx`: its patches) |
| `third_party/netsurf/content/handlers/javascript/quickjs/` | NetSurf's JavaScript on QuickJS: `qjs.c` (the engine's glue, the natives), `dom.js` (the DOM, in JavaScript) |
| `third_party/fonts/`, `third_party/dejavu-fonts-ttf-2.37/` | the fonts staged into `SD:/res/fonts` |
| `user/netsurf/` | the Onyx glue: `onyx_chrome.cpp` (the window, its wtk toolbar, the History dialog), `onyx_fetch.c` (HTTP/HTTPS over the Onyx TCP kapis, mbedTLS; each download in a thread of its own), `onyx_main.c`, the makefiles |
| `tools/tests/netsurf/` | the PC test bench: NetSurf built for the PC on the desktop simulator (`host.mk`), a page to a PNG (`shot.sh`), the same page in Chromium (`chrome.sh`), copies of the two sites (`getsites.sh`), the JavaScript regression test (`jstest.sh`, `pages/js-*.html`), the HTTP test (`httptest.sh`: the fetcher over a local HTTP/1.1 server, `httpsrv.py` -- keep-alive, chunked, gzip, a redirect, cookies, the Referer, the page drawn as its file:// copy); `NS_JSDEBUG=1` prints the scripts' errors and `console.log`, `NS_BOXDUMP=<file>` + F5 dumps the box tree, `NS_PERF=1` the timings (§9). `jstest.sh`: the DOM, the events, a recursion, `fetch` / XHR (file:// and data: URLs), the hover events, CSS `:hover`, `localStorage` kept |

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
  streams (`body` is null), multipart bodies, `responseXML`, CORS checks (every origin
  answers).
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
  `autogenerated_propget.h` (which `#undef`s them): make it again if `select_config.py`
  changes the layout.
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
  `@namespace`; what libcss drops is not there), `document.styleSheets` (the `<style>`s'). No
  `insertRule`, no linked sheets yet.
- css3test.com (Lea Verou's) now runs -- its engine is a module graph of 156 modules -- and
  scores **23%** (1154 of 6419 tests), honestly (what libcss parses; Chrome ~ 75%). It found a
  double free in libcss's Onyx gradient parser (a prefixed legacy `radial-gradient` such as
  `-webkit-radial-gradient(center, circle, ...)`: its buffers freed, then used and freed again --
  `src/parse/properties/onyx_background.c`), which could crash on real pages too.
- `tools/tests/netsurf/pages/js-module.html` (+ `js-mod-a/b/c.js`), in `jstest.sh`: a module
  graph, `import.meta.url`, `import()`, an inline module, the order, `CSS.supports`,
  `element.style`.

## 8. Known gaps

- JavaScript: no `canvas`; no streams (`fetch`'s `body`), synchronous XHR (runs async),
  multipart request bodies, `responseXML`; CSS `:active` / `:focus`; a form control
  outside a form is made again at each layout the scripts cause (its old one leaks).
- SVG (inline `<svg>` and `.svg` images), `opacity`, filters, animations and transitions.
- A face split by `unicode-range` outside Latin-1 falls back to the card's fonts.
