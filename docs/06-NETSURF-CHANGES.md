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
| `third_party/fonts/`, `third_party/dejavu-fonts-ttf-2.37/` | the fonts staged into `SD:/res/fonts` |
| `user/netsurf/` | the Onyx glue: `onyx_chrome.cpp` (the window, its wtk toolbar, the History dialog), `onyx_fetch.c` (HTTP/HTTPS over the Onyx TCP kapis, mbedTLS), `onyx_main.c`, the makefiles |
| `tools/tests/netsurf/` | the PC test bench: NetSurf built for the PC on the desktop simulator (`host.mk`), a page to a PNG (`shot.sh`), the same page in Chromium (`chrome.sh`) |

Build for the Pi: `make -C user/netsurf` (the libraries, their `.a` are committed), then
`make -f user/netsurf/netsurf-app.mk link stage`. A header change needs a clean rebuild of
what includes it (the rules do not track headers): the libcss objects, `/tmp/nsbuild`.

## 2. The window and the frontend

- **A native Onyx window** (`user/netsurf/onyx_chrome.cpp`): the toolbar is wtk's (back,
  forward, reload/stop, home, History, the address field), the frame's close box closes
  NetSurf; no fbtk toolbar (`frontends/framebuffer/gui.c`). Keys: F5, Esc, Alt+Left/Right,
  F6 / Ctrl+L, Ctrl+H.
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
  repeating gradients.
- `border-radius`, `box-shadow` (outer and inset), gradient backgrounds, rounded clips of
  a box's content; `background-clip: text` paints the descendants' text with the box's
  background.
- Translucent colours are blended; a translucent fill no longer hides what is under it
  (`desktop/knockout.c`); `color: transparent` draws nothing.

## 7. Known gaps

- JavaScript: Duktape runs scripts, but a change of the DOM does not lay the page out again
  (NetSurf's layout is static): menus opened by a script, `classList` toggles.
- SVG (inline `<svg>` and `.svg` images), `opacity`, filters, animations and transitions.
- A face split by `unicode-range` outside Latin-1 falls back to the card's fonts.
