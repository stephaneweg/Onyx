# Jet Browser — removing NetSurf's dead code (task for a Jet session)

> **Status (2026-10-01): §4, §1 and §2 done** (three commits; [`06-JET-BROWSER.md`](06-JET-BROWSER.md)
> §39 has what went and the measures). **Left**: §3 (the dead functions inside live files —
> after the sessions editing `browser_window.c`, `gui.c`, `download.c` and the desktop code are
> done) and `desktop/search.c` + `content/textsearch.c` (find in page), kept pending the user's
> decision on Ctrl+F.
>
> Decided by the user after the licence audit
> ([`LICENSING.md`](LICENSING.md) §5): Jet stays on NetSurf (GPL-2.0), but **the NetSurf code Jet
> does not use goes** — out of the build first, then out of the repository. No behaviour change:
> every page must render and behave exactly as before. Rewriting the live NetSurf code is *not*
> part of this task.

## How the dead code was found

Not by reading: by the linker. The PC build (`tools/tests/netsurf/host.mk`) compiled with
`-ffunction-sections -fdata-sections`, then relinked with `-Wl,--gc-sections
-Wl,--print-gc-sections`: every function the linker drops is reached by nothing — no call, no
function-pointer table. To redo it (≈ 1 min with -j16):

```sh
S=/tmp/nsgc
make -f tools/tests/netsurf/host.mk OUT=$S -j16 \
  BASEF="-O2 -g -fcommon -fno-strict-aliasing -w -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L -DNDEBUG -MMD -MP -ffunction-sections -fdata-sections" > $S.log 2>&1
cmd=$(grep -E "^g\+\+ -o .*/netsurf " $S.log | tail -1)
eval "${cmd/-o $S\/netsurf/-o $S/netsurf.gc -Wl,--gc-sections -Wl,--print-gc-sections}" 2> $S-gc.txt
# then, per object $S/o/_..._third_party_netsurf_*.o: its .text.* sections (size -A) against
# the "removing unused section '.text.X' in file '<object>'" lines of $S-gc.txt
```

**Check the Pi build the same way** (`user/netsurf/netsurf-app.mk`, aarch64 `-Wl,--gc-sections
-Wl,--print-gc-sections`): the host build has `ONYX_HOST_SIM` and the PC's fetch / TLS; a function
dead on the PC may be live on the Pi. Do the same for `pc/Jet/jet.mk` (Windows) before removing a
file all three builds share (`netsurf-src.mk` lists the frontend files for both).

## 1. Files compiled and entirely dead — drop them from the build

> **Done** (2026-10-01), except `desktop/search.c` (kept: find in page). Also found dead and
> removed: the generated internal bitmap font (`font-ns-sans.c`, 49 KB of data: only
> `font_internal.c` read it) and two toolbar bitmaps nothing draws (`reload_g`,
> `history_image_g`). `print.c` defined the three printing flags `html/redraw.c` reads (always
> off): they are defined in `redraw.c` now. The PE linker (Windows) does not drop `.text$`
> sections, so its report finds little: the Windows build was checked by linking without the
> files.

`netsurf-app.mk`, `host.mk` and `pc/Jet/jet.mk` compile `$(wildcard $(NS)/desktop/*.c)`,
`utils/*.c`, `content/*.c`…: add these to a `filter-out` (or list the live files explicitly),
then delete them.

| File | Lines | Note |
|---|---|---|
| `content/fs_backing_store.c` | 2 021 | replaced by `user/netsurf/onyx_cache.c` |
| `desktop/save_complete.c` | 1 348 | "save page complete": Jet has none |
| `desktop/print.c` | 337 | printing |
| `desktop/font_haru.c` | 383 | compiles to nothing (no libharu) |
| `desktop/save_pdf.c` | 996 | only an empty `save_pdf()` stub is live — find its caller, remove both |
| `desktop/search.c` | 53 | find in page (see §3) |
| `desktop/mouse.c` | 34 | |
| `utils/hashmap.c` | 255 | |
| `utils/http/challenge.c`, `utils/http/www-authenticate.c` | ~200 | HTTP auth header parsing, unused |

## 2. Files kept alive only by a call that feeds nothing — cut the call, drop the file

> **Done** (2026-10-01): the calls are `static inline` no-ops in `global_history.h`,
> `hotlist.h`, `cookie_manager.h`, `page-info.h`; `treeview.h` went with `treeview.c` (no other
> user). `gui_factory.c` needs none of them. Jet's History reads `urldb` (`urldb_iterate_entries`,
> `gui.c`). No treeview resource file existed to drop (its triangles are drawn in code).

Jet's chrome is Onyx's own (`onyx_chrome.cpp`): NetSurf's tree views (hotlist = bookmarks,
global history, cookie manager, page info) are never shown, but the core still feeds them.

| Live only through | Then dead | Lines |
|---|---|---|
| `browser_window.c:886` `global_history_add (...)` | `desktop/global_history.c` | 1 011 |
| `browser_window.c:983` `hotlist_update_url (...)` | `desktop/hotlist.c` | 1 749 |
| `urldb.c` → `cookie_manager_add / _remove` | `desktop/cookie_manager.c` | 918 |
| `netsurf.c:222` `page_info_init ()` / `page_info_fini ()` | `desktop/page-info.c` | 827 |
| the four above | `desktop/treeview.c` (+ its resources: the triangle bitmaps) | 5 423 |

Replace each call with nothing (or a static inline no-op in the header, so the call sites stay
upstream-shaped), check the frontend tables (`gui_factory.c`) do not require them, drop the files.
**Check first** that Jet's own History / Bookmarks (if any, in `onyx_chrome.cpp` or `onyx_main.c`)
do not read `global_history` / `hotlist` — the grep finds none today; they use `urldb`.

## 3. Dead functions inside live files — remove them

> **To do**, after the sessions editing `browser_window.c`, `gui.c`, `download.c` and the
> desktop code are done. Regenerate the list: §1 and §2 changed it (e.g. `print.c`'s and
> `treeview.c`'s users are gone).

From the same linker report (functions never reached). Remove them with their declarations;
leave a file's structure otherwise alone (later merges from upstream stay readable).

| File | Dead |
|---|---|
| `utils/talloc.c` (LGPL) | 71 of 91 functions (refcounts, names, autofree, unlink…) — or replace talloc's last users (`box_construct.c`) with an arena of our own and drop it whole |
| `desktop/save_text.c` | `save_as_text`, `extract_text` (only `save_text_solve_whitespace` is used: move it to its caller, drop the file) |
| `content/textsearch.c` | `content_textsearch`, `content_textsearch_clear`, `content_textsearch_step` — **find in page**: Jet has no Ctrl+F today. Keep this file and `desktop/search.c` instead if Ctrl+F is planned (ask the user) |
| `desktop/browser_window.c` | 24 functions (`browser_window_get_name / set_name`, `_debug`, `_get_selection`, `_can_search`, `_is_frameset`, `_refresh_url_bar`, `_up_available`…) |
| `desktop/browser_history.c` | the enumerate / thumbnail functions (8) |
| `content/urldb.c` | `urldb_dump*`, `urldb_iterate_partial*`, `urldb_iterate_cookies`, `urldb_delete_cookie*`, `urldb_set_url_persistence` (9) |
| `content/content.c` | 13 getters (`content__get_title`, `content_debug`, `content_exec`…) |
| `content/handlers/html/html.c` | 6 (`html_get_document`, `html_get_iframe`, `html_get_base_url`, `fire_dom_keyboard_event`…) |
| `desktop/searchweb.c` | the provider icon / iteration functions (4) |
| `utils/` | `utf8.c` (5: `utf8_to_html`, `utf8_save_text`…), `punycode_encode`, `bloom.c` (3), `libdom.c` (4), `utils.c` (3), `hashtable.c` (3), `file.c` (3), `filepath.c` (2), `nsoption.c` (`nsoption_write`, `_dump`, `_generate`), `messages.c` (2), `useragent.c` (2), `log.c` (1) |
| others (1–3 each) | `fetch.c`, `hlcache.c`, `image_cache.c`, `local_history.c` (desktop and framebuffer), `selection.c`, `textarea.c`, `box_manipulate.c`, `imagemap.c` (`imagemap_dump`), `form.c`, `css.c`, `dirlist.c`, `ssl_certs.c`, `framebuffer/schedule.c` (`list_schedule`), `framebuffer/gui.c`, `fbtk/text.c`, `corewindow.c`, `download.c` (5 getters) |

The full list (function by function, with sizes) is what the linker prints; regenerate it rather
than trusting this table after other changes.

## 4. In the repository, never compiled — delete

> **Done** (2026-10-01): 1 893 files, 600 500 lines, 22 MB; the Pi's linked `netsurf.elf`
> byte-identical before and after. Also: the framebuffer's `fb_search.c` (empty stubs) and
> `font_internal.c/.h`, NetSurf's `tools/` (but `convert_font.c`, `convert_image.c`), every
> NetSurf `Makefile*` (not only the root's). Note: no build runs `convert_font` any more (§1);
> it stays, with `glyph_data` (which `tools/fonts/gen_nssans.py` reads).

| Path | Size | Note |
|---|---|---|
| `third_party/netsurf/frontends/{amiga,atari,beos,gtk,monkey,qt,riscos,windows}` | ~207 000 lines, ~15 MB | only `frontends/framebuffer` is built (Jet for Windows is `pc/Jet`, Onyx's own) |
| `third_party/netsurf/content/handlers/javascript/duktape`, `…/WebIDL` | 4.2 MB | the Duktape backend is no longer built (`netsurf-app.mk:71`) |
| `user/netsurf/gen/duktape`, `user/netsurf/gen-duktape.sh` | 3.4 MB | its generated bindings |
| `third_party/nsgenbind` | 0.8 MB | only `gen-duktape.sh` used it (MIT: no licence gain, just weight) |
| `content/handlers/javascript/none` | | the "no JavaScript" backend |
| `content/handlers/image/{jpegxl,nssprite,rsvg,rsvg246,svg,video}.c/.h` | | not in the image list Jet compiles (`onyx_svg.c` replaced `svg.c`) |
| `content/fetchers/curl.c` | | filtered out (Onyx's `onyx_fetch.c` fetches) |
| `third_party/netsurf/test` | 0.7 MB | NetSurf's unit tests; no Onyx script uses them (check `tools/tests/netsurf/*.sh` again) |
| `third_party/netsurf/docs`, `Makefile*` at its root | | NetSurf's own build is bypassed |
| `third_party/netsurf/resources` | | **keep** what `netsurf-app.mk` `stage` copies (default.css, quirks.css, adblock.css, internal.css, FatMessages, en/welcome / credits / licence.html, a few icons, ca-bundle…) and what `host.mk` / `jet.mk` stage; the rest (other languages' pages, other frontends' icons) can go |

Keep `third_party/netsurf/tools/convert_font.c` and `convert_image.c`: all three builds run them.
Keep `COPYING` (and `licence.html` on the card): Jet stays GPL-2.0.

## Order, and how to check

1. §4 (nothing compiled changes) → build the three targets (Pi `make -C user/netsurf -f
   netsurf-app.mk`, the PC `host.mk`, Windows `pc/Jet`) — identical binaries expected for the Pi.
2. §1, then §2 (one commit each) → the three builds, then the usual Jet checks
   (`tools/tests/netsurf/`: `layouttest.sh`, `jstest.sh`, `html5test.sh`, `css3test.sh`, the
   screenshots of `tools/tests/desktop_sim/shots.sh jet`): no difference allowed.
3. §3 → the same checks. Then rerun the linker report: what is left dead is what a later
   pass decides on.
4. Update `docs/06-JET-BROWSER.md` (what was removed, why), `docs/LICENSING.md` §5 (the new line
   counts: rerun the comparison with upstream described there), and regenerate the exports
   (`python docs/build_docs.py`).

Measured on the PC build, 2026-10-01: §1 + §2 ≈ 15 000 lines out of the build; §3 ≈ 400
functions; §4 ≈ 220 000 lines and ~25 MB out of the repository.
