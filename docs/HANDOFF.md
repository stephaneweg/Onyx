# Handoff — where the work stands (for the next session)

Written at the end of a long cloud session so that a new session (e.g. a local one on the
user's Windows PC) can continue. Read `CLAUDE.md` first, then this. The user writes in French;
answer in French. The docs stay in English.

## Working conventions (keep them)

- Repo `stephaneweg/Onyx`; the cloud session worked on branch `claude/kind-rubin-vddz2w` and
  always pushed it to `main` as well. Circle is a submodule (`circle/`, fork
  `stephaneweg/circle`, branch `onyx`): commit there with
  `git -C circle -c user.name="stephaneweg" -c user.email="steph.wegener@gmail.com" commit ...`,
  push `git -C circle push origin HEAD:onyx`, then commit the submodule pointer in Onyx. Never
  modify the nested `circle/addon/wlan/hostap` (upstream, not pushable).
- The user never compiles for the Pi: they test the staged `sdcard/` on the Pi and `pc/dist` on
  Windows. `pc/dist` and `sdcard/` binaries are committed.
- Never commit: ROMs / ISOs / saves (`.z64 .n64 .v64 .sfc .smc .nes .gb* .sav .iso .gcm .wav`),
  `sdcard/etc/wpa_supplicant.conf` (psk REDACTED, pre-commit hook + skip-worktree — never defeat
  it), `sdcard/etc/ftpfs.ini`, `shelf.ini`, `SD:/apps/lisa.app/config.ini` (Groq key),
  `sdcard/etc/clock`. Check before each commit:
  `git diff --cached --name-only | grep -i -E "\.sfc$|\.smc$|\.nes$|\.gb|\.sav$|\.z64$|\.n64$|\.v64$|\.wav$|\.iso$|\.gcm$|wpa_supplicant|ftpfs.ini|shelf.ini|lisa.app/config"`
  must print nothing. Do not download commercial ROMs; the user's own ISO/ROMs stay local.
- NetSurf is ours to change (the user lifted the old "do not modify NetSurf" rule): mark each
  patch `Onyx:` in the source and list it in `docs/06-JET-BROWSER.md`; check a change on the
  PC bench (`tools/tests/netsurf/shot.sh` / `chrome.sh`) against Chromium before staging it. No
  model identifiers in code or commits. Commits end with a `Co-Authored-By:` line.
- Docs rule (CLAUDE.md): kapi / app changes → docs 02 / 03 / 04 (+ 05 for Circle patches),
  screenshots via `sh tools/tests/desktop_sim/shots.sh [name ...]` (the real apps on the PC);
  the user runs `python docs/build_docs.py`.
- A rebuild of the Pi apps changes every `sdcard/apps/*/main` a little (build noise): commit only
  the apps that really changed (`git checkout -- sdcard/...` the others).

## Builds

- **Pi (Linux + AArch64 toolchain):** `export PATH=<arm-gnu-toolchain aarch64-none-elf>/bin:$PATH`;
  `cd kernel && (make -j8 || make -j1) && make stage` (the -j8 link race on crt0libc.o is known,
  the -j1 retry works). Circle libs: `make -C circle/lib`, `-C circle/lib/net`,
  `-C circle/addon/fatfs`, `-C circle/addon/wlan`, `(cd circle/addon/wlan/hostap/wpa_supplicant &&
  make -f Makefile.circle)`. After any `ffconf.h` change: rebuild libfatfs, libwlan,
  libwpa_supplicant and the kernel clean (the `FIL` layout).
- **Windows tools:** `sh pc/build.sh` → `pc/dist` (Koton: `sh pc/Koton/build.sh` → `pc/dist/Koton`, MinGW-w64 only; nemucore.dll with MinGW-w64 `g++`,
  `-lwinmm -lopengl32 -lgdi32`; the .NET Framework 4.8 WinForms exes with the .NET SDK). On
  Windows: MSYS2 (or WSL) with MinGW-w64 + the .NET SDK. The script rebuilds every exe; restore
  the unchanged ones (`git checkout pc/dist/<file>`) before committing.
- **Host tests:** `tools/tests/run_*.sh` (fs, v3d clip, gamepad, gc, nemu...). N64 headless:
  `g++ -std=c++17 -O2 [-DN64_TRACE] -I user -I user/basic tools/tests/n64/n64test.cpp user/n64/*.cpp`
  then `n64test <rom> <frames>` with `N64_SAV`, `N64_INPUT="f0-f1:hex;..."`, `N64_SNAP=1`,
  `N64_GFX=prefix N64_GFXEVERY=n`, `N64_FRAMELOG=f`, `N64_CIMG=f` (see the file's header). With
  the user's OoT ROM, the pause menu is reached with the input script: Start at 1000, A at 1200,
  1450, 1550, 1650, then A every 80 frames from 1800 to 16000, Start at 16500.

## Jet Browser: find in page, copy and paste, the context menu (2026-10-01, PC bench only)

- **Find** (docs/06 §40): Ctrl+F / Edit ▸ Find in Page... -- a find bar above the status bar ("3 of 17",
  ^ v, Match case, x); typed words searched as they come (after a 300 ms pause once a search took
  over 30 ms), every match yellow, the current orange, scrolled into the middle when out of view;
  Enter / Shift+Enter, F3 / Shift+F3, Ctrl+G; Esc clears. content/textsearch.c rewritten: an array
  of matches, a binary search a painted text (was O(boxes x matches) a search and a paint), literal,
  case and accents folded, wraps, found again after a layout (`layout_gen`).
- **Clipboard**: frontends/framebuffer/clipboard.c on the kernel's clipboard (UTF-8, 64 KB) both ways:
  Ctrl+C of a selection, Ctrl+V / X / A in fields; Edit ▸ Cut / Copy / Paste / Select All.
- **Context menu** (right click; frontends/framebuffer/onyx_edit.c + wtk's PopupMenu): link (Open,
  Save As, Copy Address), image (Open, Save As, **Copy Image** -> `RAM:/jet/clip/image-1|2.png` +
  CLIP_FILES: Paint's Ctrl+V pastes it; Windows: CF_DIB), field (Cut, Copy, Paste, Select All),
  selection (Copy), Back / Forward / Reload, Select All, Find.
- Fixed: the horizontal scroll bar's arrows left unpainted when the page got shorter (status bar /
  find bar shown: the compositor's stale hole). Menus: Edit inserted after File (View's items moved:
  `menu 12` is Hide Status Bar now).
- Tests: **`tools/tests/netsurf/findtest.sh`** (new; Paint pasting in the simulator), fakekapi's clipboard
  is real (`SIM_CLIP`, `SIM_CLIPFILE`). Screenshots: `shots.sh jet` (jet-find.png, jet-context.png).
- **Needs a rebuild for the Pi** (Jet, `sdcard/apps/jet.app/main`) and Windows (`sh pc/Jet/build.sh`).
  **To try on the Pi**: Ctrl+F on bbc.co.uk (typing speed, the count), a copied image pasted into
  Paint, text copied into the Text Editor and back into a page's field.

## Jet Browser: Acid2 and Acid3 (2026-10-01, PC bench only)

- **Acid2 identical** to its reference (the face pixel by pixel, composited, CPU-painted and after
  a scroll); **Acid3 51 -> 94 / 100** (docs/06 §37: what was fixed -- `position: fixed`, the
  Appendix E paint order, `<object>` fallback, selectors, media query lists, DOM Range /
  NodeIterator, `document.open`, the table API, sheets and `data:` images in the script's turn).
- Bench: `OUT=/tmp/nsbench PORT=8160 sh tools/tests/netsurf/acidtest.sh [acid2|acid3]` (Acid3 over
  `acidsrv.py`, fails below `ACID3_MIN`=94). Left: tests 69, 74, 75, 77, 79, 80 = XML / SVG
  documents in frames and the SVG DOM (docs/07 §3). **The Pi's `libcss.a`, `libdom.a`,
  `libhubbub.a`, libnsfb and the Jet app need a rebuild** (not done here).

## Jet Browser: the page zoom, the status bar, downloads (2026-10-01, PC bench only)

- **The zoom** (docs/06 §38): the toolbar's **"-  100%  +"** (right of the pill), **Ctrl+- / Ctrl++
  (Ctrl+=, keypad +) / Ctrl+0**, Ctrl+wheel, View ▸ Zoom In / Out / Actual Size -- Chrome's steps
  (25..500 %), `browser_window_set_scale`; kept per host in **`SD:/apps/jet.app/view`** (`zoom <site>
  <pct>`), applied before a new page's first layout (core hook `onyx_zoom_hook`,
  `netsurf/onyx_jet.h`); the scripts see CSS px (`devicePixelRatio`, `innerWidth`, `clientX`...).
  **Kernel change** (kernel.cpp `SetKeyMapData`): Ctrl + the `-` / `=` `+` / `0` keys now reach the
  apps (the empty Ctrl column filled with the keypad's keys) -- docs/02.
- **The status bar** (bottom, 22 px; View ▸ Hide / Show Status Bar, kept in `view`): Loading... (n of
  m fetches), Ready, "404 Not Found" / "500 ..." / "Error: Connection failed" in red, the link under
  the pointer; a download's progress on the right.
- **Downloads**: unknown types, `Content-Disposition: attachment` (any type), `<a download[="name"]>`
  (also `blob:` / `data:`, a script's `a.click()`): the Save dialog in **`SD:/Downloads`** (made if
  missing), the name pre-filled (filename*, filename, the link's, the URL's; ASCII-safe for FAT),
  written by a writer thread as it arrives (`frontends/framebuffer/onyx_download.c`), the toolbar's
  downloads button + menu (progress, cancel, clear), File ▸ Downloads..., a notification at the end.
  Windows: `SD:/Downloads` = `%USERPROFILE%\Downloads`.
- Tests: **`tools/tests/netsurf/dltest.sh`** (new), jstest / uatest / httptest / gputest adjusted
  (the page 22 px shorter; the pill moved left) and green. Screenshots: `shots.sh jet` (jet.png,
  jet-menu.png, new jet-save.png, jet-downloads.png).
- **Needs a rebuild for the Pi**: the kernel (the Ctrl keys), wtk (FileDialog's Enter / Esc) and Jet
  (`sdcard/apps/jet.app/main`); Windows: `sh pc/Jet/build.sh` (built and checked here, `pc/dist` not
  committed). **To try on the Pi**: Ctrl+- / Ctrl+= / Ctrl+0 on a US and a French keyboard, the
  zoom on bbc.co.uk (text sharp, clicks where expected, scrolling), a download of a big file (a
  Linux ISO's checksum file, a ZIP) -- the desktop must stay smooth while it writes --, cancel one,
  the status bar's 404 on a missing page.

## Jet Browser: the quadratic audit (2026-10-01, PC bench only)

- What grew faster than a page (docs/06 §36, its table): child lists / `getElementsByTagName` /
  `select.options` / `form.elements` read in loops, `getElementById` (libdom walked the tree: now an
  index), `compareDocumentPosition`, listeners, mutation observers, `document.styleSheets`, NetSurf's
  scheduler (a heap now; **timers due together ran newest first** — fixed), the inline-sheet fetcher
  (**one `<style>` converted per 10 ms** — fixed), libcss's selector hash (fixed 64 slots, chain
  walks), the sheet-list comparisons, llcache's cached-object search. libdom has change counters
  (`dom_onyx_tree_generation` / `dom_onyx_attr_generation`, `N.treeGen` / `N.attrGen` in dom.js).
- Bench: `sh tools/tests/netsurf/quadtest.sh [cases]` (`pages/perf-quadratic.html`), behaviour:
  `pages/js-quadratic.html` in `jstest.sh`. **The Pi's `libdom.a`, `libcss.a` and the Jet app need a
  rebuild** (not done here). Left: §36's "Not fixed" list (a per-turn inline-style write-back,
  llcache's catch-up walk, QuickJS's `shift`).

## Jet Browser: the padlock and the site's version in the toolbar (2026-10-01, not yet tried on the Pi)

- **The padlock** (left of the address field, a half pill joined to it): green for a verified https
  page, red past a certificate warning ("Proceed": NetSurf's `PAGE_STATE_SECURE_OVERRIDE`), grey and
  struck for http, none for `file:` / `about:`; a click opens the certificate viewer with the page's
  host's chain -- the fetcher now keeps every checked connection's chain (`onyx_chain_keep`,
  `onyx_fetch_cert_url`; `onyx_nstls_connect` fills `*chain` on success too: callers free it).
- **The site's version** (right of the field, a blue half pill): Standard / Mobile / Desktop per
  site (registrable domain), its menu on a press (Navigate > Site Version...), the page reloaded; kept
  in `SD:/apps/jet.app/site-modes` ("site mode" lines; the old `desktop-sites` read until the first
  change); jet.ini's `[sites]` wins ("Custom"), jet.ini has a new `[user_agent] mobile`. The disk
  cache keys `D|` / `M|` / `C|` and stores an object under the version it was fetched as
  (`onyx_cache_fetched_as`). Tab / Shift+Tab from the field reach the pill / the padlock.
- docs/06 §35, docs/04 (Jet), `screenshots/jet.png`, `jet-menu.png` (`shots.sh jet`: a local https
  page). Tests: `tools/tests/netsurf/uatest.sh` (new: the User-Agent per version, the cache keys,
  `desktop-sites`, jet.ini Custom), `tlstest.sh` (the padlock's colours and its viewer).
- **To try on the Pi**: the pill on bbc.co.uk / google.com (Mobile vs Standard pages), the padlock
  on a site past "Proceed" (self-signed.badssl.com), the viewer from a resumed TLS session (the
  server's certificate only).

## Jet Browser for Windows (2026-10-01, tried under Wine only)

- **`pc/dist/Jet/Jet.exe`** (+ `pc/dist/Jet.zip`): the browser's Onyx sources built with MinGW-w64
  (`sh pc/Jet/build.sh`, `pc/Jet/jet.mk`, also run by `pc/build.sh`) over `pc/Jet/winkapi.cpp` (the
  kapi on Win32, grown from Koton's). The same engine, network code, TLS (mbedTLS + `res\ca-bundle`)
  and caches as the Pi; no JIT, no GPU. Data in `data\` beside the exe (`RAM:` = `data\ram`), the log
  in `data\jet.log` (`--console`: a console), `--perf` / `--jsdebug` / `--netdebug` (or empty files
  beside the exe). docs/06 §34, docs/03, `pc/Jet/README.txt`.
- **Tested under Wine** (Xvfb + xdotool): pages, a local file, HTTPS (h2, TLS 1.3, brotli), css3test.com
  83 % and responsive after its run, resize, typing, Alt+Left, Alt+F4. **To try on a real Windows 10 /
  11**: the same, plus a scaled display (`--sharp`), the menus, drag and drop of an .html file.
- **Next ideas**: the css3test.com hang on the Pi compared with this build's `--perf` log; Windows'
  certificate store as an option; a proxy setting.

## RAM:, a volume in memory; Jet Browser's caches there (2026-10-01, kernel v71, not yet tried on the Pi)

- **Kernel `RAM:`** (`kernel/sys/ramfs.cpp`, `kern/ramfs.h`; docs/02 §16): a file system in memory,
  reached by the same file kapis as the card (open/read/fsize/seek/close, save_file, file_in/file_out
  with append, opendir/readdir, mkdir/remove/rename, chdir) -- `kapi.cpp` resolves the path first,
  `RAM:` goes to ramfs, `FTP:` to vfs, the rest to FatFs; handles told apart by address. Folders and
  files are heap records of one size; the bytes are in 64 KB pages of `palloc_high` cut into 256 B ..
  32 KB chunks (a small file takes its size rounded to 256 B), given back when nothing in a page is
  used. One re-entrant sleeping lock; 1 MB slices with a Yield between them; a save is no-kill.
  Size: `system.ini` **`ramfs=`** (MB or `N%`; 0 = none; default 128 MB, at most a quarter of the
  free page memory), 32 MB of pages always left to the apps, 16384 nodes. Lost at a restart.
- **Kapi v71 `vol_info`** (`struct kapi_vol_info`): total / free / used / type of `SD:`, `SD1:`..,
  `RAM:`. New `/bin/df` and `/bin/ramtest`.
- **Jet Browser**: the disk cache in `RAM:/jet/cache`, the code cache in `RAM:/jet/jscache/` by
  default (stored at first sight, not paced, 2 MB objects, half / a quarter of `RAM:` at most);
  Choices' **`cache_on_card:1`** puts them back on the card with §32's rules. History and Cookies
  stay on the card. docs/06 §33. The code cache now does its file I/O through the kapi.
- **Tests**: `sh tools/tests/run_ramfs_test.sh` (ramfs.cpp on the PC under ASan, against a model);
  `httptest.sh` (RAM: one `SIM_RAM` = one boot; a restart; `cache_on_card:1` over three launches),
  jstest / nettest / fxtest / iframetest / gputest pass. The desktop simulator maps `RAM:` to
  `SIM_RAM` (else a temporary folder per run).
- **To try on the Pi**: `ramtest` (ALL PASS; its 16 MB times) and `ramtest full`; `df`; `ls RAM:`,
  `cp`, `rm`, `cd RAM:`; Jet Browser: a site, Jet closed and opened, the site again (from `RAM:`:
  `ls RAM:/jet/cache`), no `stall: jet:cache` in `kmsg`; after a restart `RAM:` empty; the boot log's
  `ramfs: RAM: volume, up to ... MB`. A 1 GB Pi: the size (~60 MB) and the apps still fine.
- **Next ideas**: `RAM:` in the File Viewer's Computer places and wtk's file dialog volume list;
  `mv` across volumes (copy + remove); a `ramfs` line in the Control Panel.

## TCP, SD and save fixes (2026-10-01, not yet tried on the Pi)

- **Circle's TCP** (docs/05 §20–22, docs/02 §11; host test `sh tools/tests/run_circlenet_test.sh`,
  the fork's real TCP code against stub headers): only real duplicate ACKs count (the remote
  desktop client's input messages started spurious fast retransmits: the Pi's sending throttled
  while the mouse moved); RTO 1 s initial (was 3 s), the **1 s minimum kept** (200 ms was tried and
  undone: spurious timeouts over Wi-Fi to Windows, the remote desktop ~50x slower), Karn after
  a fast retransmit; `CSocket::Send` answers the bytes queued when a later chunk times out (it answered the
  error: an app resending "the rest" duplicated bytes). rdpd now sends the rest after a short
  count instead of ending the session.
- **SD writes no longer hold core 0**: many short waits in a row (a long multi-block write)
  yield after 10 ms (`OnyxDriverPoll`, a second weak hook: docs/05 §7b, docs/02 §5).
- **`kapi_save_file` returns the bytes written**: Koton's `Doc::save` (every save "failed" on the
  Pi) and `fsutil.h`'s copy (an empty file) fixed; the desktop simulator's fake answers the length
  (it answered 0), so does `pc/Koton/winkapi.cpp` (pc/dist/Koton not rebuilt: no MinGW here).
- **To try on the Pi**: Onyx Remote while moving the mouse over a busy window (the updates a
  second should not drop; `rdpd` lines in `kmsg`: `partial`, the send times); a big file copied
  or saved (File Viewer copy of a 20 MB file, Jet's cache) with no `stall: ... TimeoutWait` lines
  in `kmsg`; Koton: save a song (no error); `fsbench` (the read speed should be about as before).
- The NetSurf bench `httptest.sh`'s two "launched again" disc-cache checks: fixed by the caches
  in `RAM:` (below; the card's "second sight" rule needed a third launch).

## The screen's resolution, changed while running (2026-09-29, not yet tried on the Pi)

- **Kernel v66 `screen_set`** (`ScreenResizeRequest`, kernel.cpp): the compositor task calls
  Circle's `C2DGraphics::Resize` between two frames (the firmware makes a frame buffer of the new
  size), then `CWindowManager::OnScreenResized` keeps the windows on the screen and sends every
  window **`GUI_EVENT_DISPLAY_RESIZE`** (19). Refused while a full-screen app owns the display.
  The wallpaper buffer is made again at the new size (the old one leaked on purpose: it may still
  be mapped). A window may be as big as the screen (was 1024 x 768).
- **wtk**: `Root::onDisplayResize (w, h)`; ~0.3 s later a maximised window fills the new work
  area, another is moved / shrunk into it (`displayTick`, `fitWorkArea`); borderless ones place
  themselves. The menu bar, the dock and notifyd do; vncd (VNC DesktopSize, else the session
  closed), rdpd (`SCREEN` message) and Onyx Remote (its view of the Pi's screen resized) follow.
- **The Control Panel's Display applet** (`displayconf`, `15-display.lnk`): a list of sizes,
  Apply: at once + `SD:/cmdline.txt`; voronoy paints the wallpaper again.
- **To try on the Pi**: bigger and smaller, with maximised windows, the Spreadsheet, a VNC viewer
  and Onyx Remote connected. Not handled: an app's own dialogs placed by the old size.

## Threads (2026-09-29, kernel v67, not yet tried on the Pi)

- **The scheduler has no task limit**: a circular linked list of `TSchedNode`s (the per-task
  scheduler state lives there: `CTask`'s layout is Circle's); `MAX_TASKS` and its `LogPanic` are
  gone. docs/02 §5.
- **Threads** (`kernel/sys/thread.cpp`, `kern/thread.h`): `kapi_thread_create / exit / join /
  self`, mutex / event (manual, auto) / barrier, `kapi_post` (a call run by the process's pump)
  and `kapi_pump_wait` (a pump that sleeps until an event or a post). A thread is a task sharing
  the app's `CAddressSpace`; the process ends with its main task, a kill takes them all
  (`TerminateGroup`), and the reaper frees a killed task only with its whole group (it may be on
  a wait list of the process). 32 threads per process. docs/02 §7, docs/03 §5.2.
- **User side**: `umm` and newlib (its retargetable locks, in `libc/onyx_syscalls.c`) are
  thread-safe; `kapi_lock` / `kapi_unlock`. `errno` is still shared (per-thread would need TLS:
  `TPIDR_EL0` saved at each switch).
- **To try on the Pi**: `threadtest` in a terminal (PASS; the prompt comes back although a thread
  still runs), then kill it from the task manager in the middle; the usual apps (nothing should
  change for them: one task each). Watch `stall:` lines in `kmsg`.
- **Tried on the Pi (2026-09-30)**: `threadtest` PASS, killed mid-run cleanly (after the
  `task.cpp` FIQ fix: a thread first run after a preemption halted on Circle's EnterCritical).
- **Users**: NetSurf's downloads (a thread each: `user/netsurf/onyx_fetch.c`, docs/06 §1 --
  the connects one at a time: several at once all failed, a page's style sheet among them);
  `telnetd` (a session per thread, 8 at once); SuperTuxKart's `stkpoc` (`std::thread` on them:
  `user/stk`, docs/SUPERTUXKART-PORT.md -- PASS on the Pi).
- **Next**: asynchronous kapi calls (a file read, a connect) with a completion posted
  to the pump; `errno` per thread; threads in the BASIC VM (an idea, written down in
  `docs/BASIC-VM-THREADS.md`).

## The shared clipboard (2026-10-01, not yet tried on the Pi)

- **`user/Apps/clipd`** (the service, IPC "clipboard": a ring of 10 typed copies, a cursor), **`user/clipboard.h`**
  (the apps' side, its old functions kept + images and formats), **`user/clipproto.h`** (messages by mailbox,
  bytes by `RAM:/clip` files), **`user/Apps/clipboard`** (the widget, the dock's new button; the dock's small
  buttons now: lock / gear at the left, power / clipboard at the right). Every wtk app gets it through
  `textbox.cpp` / `textarea.cpp`: all the apps were rebuilt and staged. `autostart` runs clipd.
- **Tested on the PC**: `sh tools/tests/run_clipboard_test.sh` (21 checks); the simulator has in-process
  mailboxes (`SIM_IPC=1`) and the magenta key of `WIN_FLAG_TRANSPARENT` windows in its dumps.
- **To do**: Jet's copy / paste through clipboard.h (docs/clipboard/README.md, *Still to do*); try on the
  Pi: a copy in tinypad, the notification, the widget above the dock, an image (when an app copies one).

## Archiver, the archive manager -- version 1, ZIP (2026-10-01, not yet tried on the Pi)

- **`user/Apps/archiver`** (docs/04 §9 *Archiver*, docs/03 *The Archiver*, the plan and the user's
  decisions in `docs/archiver/README.md`): ZIP opened (zip64, CP437 / UTF-8 names, self-extractors,
  ZipCrypto), browsed as folders, extracted (the selection or all; the archive's folders kept, from
  the current folder down, or flat; Ask / Replace / Skip / Keep both), changed by a rewrite into a
  new copy swapped in (add, delete, rename, new folder). Files **dropped** from the File Viewer go
  straight into the folder under the pointer; rows dragged out are extracted to `RAM:` and handed
  over; a file opened (extracted to `RAM:`) and saved is put back. Jobs on a **thread**. A newlib
  app (FreeType) with zlib (`user/zlib/libz.a`). Built here with the Arm GNU toolchain 13.3.
- **Tested on the PC**: `sh tools/tests/run_archiver_test.sh` (the engine, 51 checks against
  `zipfile` and `unzip -t`) and the app in the desktop simulator (which now has `kapi_file_in /
  file_out` streams, remove / rename of the files an app wrote, and the script's `dragover` / `drop`).
- **To try on the Pi**: a big archive (hundreds of MB: the reads go through `kapi_seek`, the
  central directory is read whole), extracting to the card while it writes slowly, a drop from the
  File Viewer, Background / Cancel during a long add, a `.zip` opened from the File Viewer.
- **Next**: 7z (the LZMA SDK), tar / .tar.gz / .tar.zst / .tar.xz, RAR read only through
  libarchive's readers (BSD; unRAR's licence is not GPL-compatible: `docs/LICENSING.md` §4).

## Courier, the HTTP client -- Postman for Onyx (2026-09-30, not yet tried on the Pi)

- **`user/Apps/courier`** (docs/04 *Courier, the HTTP client*, docs/03 after `http.hpp`): requests
  with params / auth / headers / bodies (raw, urlencoded, multipart with files, binary), `{{vars}}`
  (environment > collection > globals, `{{$guid}}`...), collections with folders, environments, the
  history, a cookie jar, tests + captures, code snippets, Postman v2.1 import / export, cURL import.
  A newlib app (FreeType) linking mbedTLS; each request on a **thread** (kapi v67) with `kapi_post`.
  `SD:/courier/` holds its files; the card ships `courier/collections/courier-examples...json`
  (JSONPlaceholder + httpbin over https).
- **Tested on the PC**: the desktop simulator now has **threads** (pthreads, `kapi_post`) and
  **`SIM_REALNET=1`** (real TCP): the engine was checked against a local echo server (gzip,
  redirects, cookies, multipart, auth, tests, Postman round trip, cURL). HTTPS only on the Pi.
- **To try on the Pi**: the examples collection (https, a thread doing TLS: its 512 KB stack), a
  big response, Cancel during a slow request, the window maximised, import of a real Postman export.

## Setup, the first-run wizard; the settings in FreeType (2026-09-30, kernel v69, not yet tried on the Pi)

- **The rule now**: every new or redesigned app draws its text with **FreeType** (DejaVu Sans
  through wtk's face) unless the user says otherwise — `FT_APPS` in `user/Makefile` (and the same
  list in `shots.sh`'s `build`); docs/03 after `ft_wtk_install`. Moved to it: the Control Panel,
  its 8 applets, the Game Library and (2026-10-01) the menu bar (text measured in pixels, `drawFont` gone).
- **`user/Apps/setup`** (docs/04 §4 *Setup*): 7 pages in wtk's theme (the user's validated mock-up:
  `screenshots/setup-*.png`, `shots.sh setup`) — country / keyboard / time zone, Wi-Fi, resolution
  with "Keep this resolution?", colour + wallpaper + 32 tints, host name + remote services, a
  summary. `system.h`: what it writes (system.ini, wpa_supplicant.conf, cmdline.txt, theme.txt,
  wallpaper.ini, the autostart). The card's autostart starts it (`run setup`) and holds back the
  menu bar, the dock and the agenda (`#setup: ` lines, given back and started at the end).
- **Kernel v69**: `WIN_FLAG_FIXED` (not movable, no title buttons, re-centred on a resolution
  change), `screen_native` (EDID), `set_timezone`; `system.ini` `hostname=` (Circle patch 17:
  `CNetSubSystem::SetHostname`) and `ntp=off`. `/bin/verbose` no longer erases system.ini.
- **To try on the Pi**: the EDID size (an HDMI monitor, a TV), Try it / Revert / the 15-s timeout
  and the window re-centred, joining a network (and a wrong password: 30 s), the host name seen by
  the router after a restart, the services started / stopped at Start Onyx.
- **Next (asked), in this order**: **PRIORITY -- apps in EL0** (protected mode: a faulting app killed, not the OS; plan in docs/EL0-PROTECTED-MODE.md; trigger: a panic closing Ledger, not reproduced -- the exception report now names EC/ELR/FAR/task); then redesign every app icon; then games and emulators with 1- or 2-stick pads.

## GPU compositing, stage 1 -- the service (2026-09-30, kernel v70, not yet tried on the Pi)

- `user/gpucomp/gpucomp.{h,c}` (+ `libgpucomp.a`): layers (premultiplied ARGB textures, tiled past
  2048) composited by the V3D into a canvas -- affine matrix, clip, opacity, source-over, bilinear,
  scrolling by the source rectangle -- or by the CPU (NEON loops) with the same API and pixels.
  Kernel v70: `gpu_texture_rect` (damaged rectangles only), `KAPI_GPU_F_ALPHA` (ARGB targets), fair
  shares of the GPU's handles between programs (1024 textures / 512 a program; 32 vbufs / 8 a
  program). docs/02 §15 (*Sharing the GPU*, *The compositing service*), docs/03 *GPU compositing*,
  docs/07 §6 (the stage 2 plan: NetSurf's layers into it).
- Tests: `sh tools/tests/run_gpucomp_test.sh` (the PC: CPU and software-V3D paths against a
  reference, tiles, updates, a refused texture, the GPU lost, qemu-aarch64 NEON = PC bit for bit).
- **On the Pi, first**: `gpcdemo test` (must end `ALL PASS`), `gpcdemo bench` (write the numbers
  into docs/07 §6), `gpcdemo` (the window) -- also while gcemu runs (the sharing), and `v3dprog`
  (must still pass: the kernel's GPU paths were touched for F_ALPHA). kmsg after a failure.
- Stage 2 done (docs/06 §25, docs/07 §6: the page in a band, retained layers, composite-only
  animations, `gpu_compositing` in Choices; `tools/tests/netsurf/gputest.sh`). The V3D's target
  load packet was wrong (stride / 8: frames drawn over the target) -- fixed, `run_v3d_cl_test.sh`.
  Next: the Pi's numbers (`gpcdemo test` / `bench`, NetSurf's `ONYX-SCROLL` with
  `gpu_compositing` 0 and 1), overflow scrollers and fixed boxes as layers, groups holding layers.

## Done recently (all pushed)

- **The random N64 freeze on the Pi — fixed.** Root cause (found with the crash record):
  `CScheduler::WakeTasks` asserted on a task whose `WaitWithTimeout` had just expired (Ready but
  still on the wait list) when the V3D frame-done interrupt set the event → Circle halted all
  cores. `kernel/sched/scheduler.cpp` now only unlinks such a task. The user played 15+ minutes
  without a freeze afterwards. The same bug is in upstream Circle: see below.
- **Crash record** (`kernel/sys/crashlog.cpp`, docs/02 §13): log tail, core-0 IRQ samples,
  breadcrumbs (GPU, display), power/temperature, panic line, in 64 KB of RAM kept out of the heap
  (top of the RAM above 4 GB on the user's 8 GB Pi — it survives the watchdog reset); the hardware
  watchdog (`hangreboot=`, 15 s); core 1 writes a report into `SD:/etc/crashdump.txt` sectors when
  core 0 stops (LED signs); next boot → `SD:/etc/lastcrash.txt`. `hangtest` freezes core 0 on
  purpose. `SD:/etc/clock` keeps the time across boots (files written before NTP get a date).
- **Crash record, round 2 (2026-09-29, for the Spreadsheet's freeze: a long hang, then a restart,
  nothing on the card):** a Circle panic (assertion, kernel heap "Out of memory") halted every
  core, core 1 too — no report; now the logger's panic handler has core 1 write it first. The
  free memory (heap, kernel pages, app pages) every second in the record; the return addresses
  on a faulting stack; the **app watchdog**: a frozen app watched (its task's PCs, its stack) and
  `SD:/etc/apphang.txt` rewritten every 2 s, merged into `lastcrash.txt` if the Pi restarts
  (docs/02 *The crash record*). **Not yet tried on the Pi**: next, reproduce the Spreadsheet's
  freeze and read `lastcrash.txt` (addresses → `addr2line -e user/sheet.elf`).
- **The kernel's size limit:** image + BSS must end below `0x280000` (0x80000 + Circle's
  `KERNEL_MAX_SIZE`, 2 MB) — past it the BSS runs over the kernel's stacks and the Pi does not
  boot, without a message (a 32 KB static buffer did it). `make` / `make stage` now check `_end`
  in the map (`sizecheck`) and delete an image too big. 128 KB left: big buffers go on the heap.
- **N64 (task done):** OoT pause background (the copy into the z-buffer drawn by the CPU into
  RDRAM, the host's frame written back as the framebuffer: `Machine::fbSnapshot`), the 8-bit
  coverage copy written as full (menu opens in ~1 s instead of ~4), decal z bias (z-fighting),
  V3D CPU clipping, polled display DMA (`dispdma`).
- **Onyx Remote:** telnet **Console** window; window fixed to the Pi's screen size when
  connected; the desktop no longer tiles.
- **Circle upstream contributions:** 5 clean branches on the fork (`pr/scheduler-waketasks-timeout`,
  `pr/emmc-high-speed-fixes`, `pr/heap-large-block-reuse`, `pr/2d-partial-update`,
  `pr/dhcp-restart`), based on upstream `develop`; texts, patches and an issue draft (RAM above
  3 GB on the Pi 4) in `docs/circle-upstream/`. The user opens the pull requests.

## The desktop redesign -- a modernised CDE: implemented, in `main`

- **Designed with the user on 2026-09-28 and implemented the same day** on the branch
  `Elegant-UI`, merged into `main` once the user had seen it running (through Onyx Remote).
  `docs/gui-redesign/README.md` has the decisions, the mock-ups and §5 *where the work landed*;
  the user guide (`docs/04` §4-§6, §11) describes the result; `screenshots/` are the real apps
  (`sh tools/tests/desktop_sim/shots.sh`).
  In short: wtk's procedural painter (`user/wtk/paint.h`) and the theme's colours as variables
  (`theme.txt`: theme Peach / Steel / Sage / Brick / Slate or a colour, inactive, face, accent,
  outline, dock), every wtk widget restyled (the user's framed button), the window frames drawn
  by wtk (title 28, border 4, rounded corners r 8, the window menu / minimise / maximise / close
  buttons), kapi **v64** (`win_minimise`, `win_geometry`, `resize_window2`), the **dock**
  (`user/Apps/dock`: categories + drawers, the Shelf's tabs as its switcher, lock / gear / power,
  Terminal, File Viewer, Trash) instead of the Shelf and the panel, the see-through agenda, the
  menu bar restyled (its time opens a calendar), the **lock** screen, the **Theme** app
  rewritten, every app's hard-coded dark colours converted.
- The emulators' fast path is intact: an app's present damages only its client area unless its
  frame changed, `CoversOpaque` less the corners' see-through pixels only
  (`tools/tests/desktop_sim/wmtest.cpp` checks it); the V3D, `gpudirect`, `dispdma`,
  `fullscreen_direct` paths untouched.
- **Next ideas** (none started): anti-aliased text (the `.aaf` fonts and `user/elegant.h` on
  **`archive/elegant-ui-2026-09-28`**); resizing a window by dragging its border (today:
  maximise / restore; a resizable app says so with `Root::setResizable`); a theme change applied
  to the open apps live (today: at their next start; the shell apps are restarted); the dock's
  drawers by drag & drop (an app to a drawer); the Shelf / panel apps removed once the dock is
  proven on the Pi. (The second round below did the workspaces, the Control Panel and the
  dock's drawers.)
- The phone-like "elegant layout" the branch held first is on **`archive/elegant-ui-2026-09-28`**.

## The desktop's second round (2026-09-29, branch `claude/happy-wright-wg38ez`)

Asked by the user after trying the modernised CDE (not merged into `main` yet: the user tests
the staged `sdcard/` and `pc/dist/OnyxRemote.exe` first).

- **Workspaces** (kapi **v65** `desk` / `win_desk`, `kernel/gui/window.cpp`: `SetDeskLocked`,
  `Hidden ()` = minimised or off-desk everywhere the compositor / hit-testing / focus look,
  Ctrl+Alt+Left / Right in `OnKey`); the dock's pager (4 squares by default, the windows drawn
  small), the window menu's Move to / On All Workspaces. `wmtest.cpp` checks them.
- **The dock** rewritten (`user/Apps/dock`): drawers = a group + its main app (the icon starts
  the app, the strip above opens the drawer, as Xfce), launchers, the pager, lock / gear
  (Control Panel) / power, the Trash (a click opens it in the File Viewer: `fileviewer trash`;
  a drop trashes). The Shelf's switcher is gone. `SD:/etc/dock.ini` (`user/dockconf.h`).
- **The Control Panel** (`user/Apps/control`): applets drawn inside its window through a shared
  surface (`user/applet_proto.h`; wtk's `Root` has an applet mode: `wk_applet ()`, `wk_pump`,
  `wk_present`, `wk_quit`), listed by link files (`sdcard/apps/control.app/applets/*.lnk`).
  Applets: Theme (rewritten: a Windows-98-like desktop preview, a colour per part — frames,
  content, buttons, fields, selection, menu bar, dock — the wallpaper's modes, `user/wallpaper.h`
  painted by `voronoy`), Panel (`dockconf`), Sound (`soundconf`), Keyboard & Mouse (`keyconf`),
  Gamepad, Wi-Fi, App Settings (`config`). Kernel surfaces are now counted per user (an applet's
  surface outlives its host or itself safely).
- **The categories**: the System group gone (its apps in Productivity / Graphics / Settings),
  `Settings` and `Emulators` left out of the menus (the emulators through the Game Library).
- **The File Viewer**: a sidebar (Personal: pinned folders under a name, the Trash; Computer:
  the partitions; Network: the FTP servers connected once, under a name — a click reconnects),
  an elementary-style path bar, padded columns. `SD:/etc/places.ini`.
- **Onyx Remote**: the borderless windows above the framed ones (the menu bar's drop-downs,
  the dock, the notifications, the Wi-Fi menu) are layered windows with per-pixel alpha
  (`pc/OnyxRemote/Overlay.cs`) — no more black under a popup; hidden windows not shown; the
  Onyx frames' title buttons pressed on the Pi. rdpd sends the ALPHA windows and the frames in 32
  bits. The kernel bumps a window's ChromeGen again at its first present after a frame redraw
  (rdpd could read a half-drawn frame: a title bar without buttons). `eyes` presents now.
- **The Game Library** redone as the File Viewer (a sidebar: All Games, the systems with their
  icons and counts, the folders; a path bar; the cards filtered on the system chosen).
- **Wallpaper patterns**: eight abstract grey pictures (`sdcard/wallpapers/*.png`, 1024 x 768,
  made by `tools/gen_wallpapers.py`) that the Theme applet colours (`mode = pattern`: the grey
  multiplies the gradient of the two colours; `wallpaper.h` `wp_grey_cover` / `wp_multiply`,
  painted by `voronoy`). wtk's `Dropdown` opens upward when it must.
- **Open question for the user**: *Gamelib without its title buttons* — not reproduced (its
  frame is drawn like every Root app's, in the simulator too); the ChromeGen fix above covers
  the Onyx Remote case (a frame read while being drawn). Ask where it showed (the Pi's screen
  or Onyx Remote, with or without Onyx frames) if it comes back.
- **Next ideas**: drag an app onto a drawer to add it; the applets' own help; a wallpaper
  slideshow; the workspaces' windows moved by drag & drop onto the pager.

## Writer, a word processor (2026-09-29, same branch, pushed to `main`)

Asked by the user: Writer "toward AbiWord", no printing, FreeType from the NetSurf work, drawn by
Writer itself (no RichTextBox). Done:

- **The apps' FreeType** (`user/ft/`): TrueType only, auto-hinted, anti-aliased, kerned; built by
  `user/Makefile` into `ft/libft.a` (NetSurf keeps its own). `ft/fonts.h`: the card's families
  (`SD:/res/fonts`, `SD:/fonts`), sized fonts, a glyph cache at quarter pixels, the drawing.
- **Writer** (`user/Apps/writer/`, a newlib app now: `writer.elf` rule): pages (A4, margins,
  page numbers), styles, fonts, sizes, B/I/U/S, super/subscript, colours, highlights,
  alignments, indents (the ruler's markers dragged), spacing, lists, page breaks, images (PNG /
  JPEG / BMP / GIF / WebP, resized with a handle), undo / redo, rich copy / paste, Find and
  Replace, Special Character, Date and Time, Word Count, Page Setup, zoom, formatting marks;
  RTF read / written with all of it, text, HTML export; a recovered document after a close with
  unsaved changes. `.rtf` files now open in Writer (`sdcard/etc/fileassoc.ini`). The docs:
  `docs/04` *Writer, the word processor*, `docs/03` (TrueType text, `VPath`, Writer's pieces).
- **wtk**: `wtk/vpaint.h` (`VPath`: anti-aliased vector shapes, integer); `img_load_mem`.
- **Sample**: `sdcard/docs/writer-tour.rtf` (`tools/gen_writer_sample.py`); the screenshot
  `screenshots/writer.png`. The desktop simulator's script has `mods N` (modifier keys).
- **Next ideas**: done since — tables, headers / footers, fields, tab stops, a table of contents,
  `.docx` / `.odt`, the mail merge: see *Writer as Word* below.

## Paint, as Windows 11's, with layers (2026-09-29, same branch, pushed to `main`)

Asked by the user right after Writer. `user/Apps/paint/` rewritten (still a freestanding integer
app): the ribbon (Edit, Image — select, crop, resize / canvas size, rotate / flip —, Tools — pencil,
fill, eraser, colour picker, magnifier, brush —, fifteen Shapes inscribed in their box's ellipse
with outline / fill, Size, Colours — 1 and 2, twenty, ten custom, Edit —, View — the pixel **Grid**
toggle, Fit), transparent **layers** (eye, opacity, add, duplicate, delete, move, merge, flatten),
a floating selection (moved, nudged, turned, flipped; a click outside puts it down), zoom 12 % –
3200 %, undo by tiles. **Save** = OpenRaster (`.ora`, the layers; GIMP / Krita read it); **Open**:
`.ora`, PNG, JPEG, BMP, GIF (WebP, PCX); **Export**: PNG, JPEG, BMP, GIF (flattened) — the writers
in `user/img/pngsave.hpp` (deflate, PNG, JPEG, GIF, BMP, ZIP), `img_inflate` in imgload.hpp. A
closed-unsaved picture is recovered. Screenshots `paint.png`, `paint-grid.png`; docs 04 *Paint*,
03. **Next ideas**: a text tool (it would make Paint a newlib app: `ft/fonts.h`), free-form
selection, a selection resized by handles, brushes with soft edges, a gradient fill.

## Cardfile, a small database (2026-09-29, same branch, pushed to `main`)

Asked by the user ("a simple Access without SQL"). Done (the user guide: docs/04 *Cardfile, a
small database*; the pieces: docs/03):

- **Cardfile** (`user/Apps/cardfile/`, integer only): one `.card` file = a **form** (a title, a
  description, fields: display name, column name, type — one-line text, multi-line text, integer,
  decimal with its decimals, date, colour, yes / no, a choice list with its choices) and its
  **records**; the format (text, INI-like head, the records tab-separated) is at the top of
  `main.cpp`. Three views: **Form** (an index card, an editor a field, the navigator, validation
  when a record is left), **List** (a grid: sort by a title, again the other way; columns widened
  by their edge; double-click → the form), **Design** (fields added / removed / moved / named /
  typed; a new type converts the values, asked first when some would be emptied). Search (every
  word, any field), Undo / Redo (the whole document kept before each change), CSV export and import
  (the types guessed), a document kept at a close with unsaved changes (`recovered.card`).
- **wtk**: `wtk/datagrid.h` — `DataGrid`, a virtual table (docs/03).
- Samples `SD:/docs/books.card` (every type) and `contacts.card`; `card = cardfile` in
  `fileassoc.ini`; the icon by `tools/gen_assets.py cardfile`; screenshots `cardfile.png`,
  `cardfile-list.png`, `cardfile-design.png`; host test `sh tools/tests/run_cardfile_test.sh`.
- **Next ideas**: a cell edited in place in the grid (Access's datasheet); the form's layout
  (two columns, a field's width, a memo's height); a default value per field, required fields;
  computed fields; an image field; printing / a report; a lookup into another `.card`.

## The Spreadsheet, as LibreOffice Calc / Gnumeric (2026-09-29, same branch, pushed to `main`)

Asked by the user ("un peu plus poussé comme gcalc": read as LibreOffice Calc / Gnumeric). The old
`sheet` rewritten from scratch (the user guide: docs/04 *The Spreadsheet*; the pieces: docs/03):

- **`user/Apps/sheet/`**, a **newlib** wtk app (`sheet.elf` rule, FreeType), `stack = 4M` in its
  `app.txt`. The engine (plain C++, the same on the PC): workbooks of sheets of 1 048 576 × 16 384
  cells in a hash of the used ones; Excel's formula syntax (references relative / absolute, to other
  sheets, whole rows / columns, arrays, names), **237 functions**, full recalculation at each
  change (on demand, an explicit stack for deep chains, `#CIRC!`), Excel's number formats, typed
  entries read as Calc does (numbers, %, amounts, dates day first, times), interned styles, merged
  cells, frozen panes, rows / columns inserted / deleted with every reference moved, copy / cut /
  paste (Paste Special), fill series, sort (3 keys), Find and Replace, **defined names**,
  **conditional formatting** (value, text, top / bottom, average, duplicates, formula; colour
  scales, data bars), the **AutoFilter**, **charts** (column, bar, line, area, pie, scatter,
  anchored to a cell), undo / redo (100 steps).
- **Files**: `.xlsx` read and written (styles, formats, merges, sizes, panes, charts, conditional
  formats with their dxfs, AutoFilter, names — LibreOffice opens them as written); `.ods` read
  (LibreOffice's styles, number styles, charts, calcext conditional formats, database-range
  filters, named ranges); CSV / TSV read and written. `xlsx`, `ods`, `csv`, `tsv` = `sheet` in
  `fileassoc.ini` (**`.csv` used to open in the text editor**). A workbook closed unsaved is
  kept as `SD:/apps/sheet.app/recovered.xlsx`.
- **Sample** `SD:/docs/cafe-2026.xlsx` (made by `tools/tests/sheet/make_sample.cpp`); screenshots
  `sheet.png`, `sheet-filter.png`, `sheet-loan.png` (the `sheet` scenario of `shots.sh`); host
  tests `sh tools/tests/run_sheet_test.sh` (328 engine checks, 122 file checks; LibreOffice round
  trips when `soffice` is installed).
- **The euro sign**: wtk's font has it in slot 0x80 (Windows-1252's; `gen_nssans.py` `EXTRA`);
  **AltGr+E** now types it on the FR, BE, DE, ES keymaps (AltGr+4 on UK; IT and US had it) —
  `tools/keymaps/maps/*.h` → `genkeymaps.py`. The Spreadsheet takes it; wtk's `Textbox` /
  `Textarea` still ignore 0x80 (they accept 0x20–0x7E, 0xA0–0xFF).
- **Next ideas**: spilled dynamic arrays (and with them SORT, UNIQUE, FILTER, SEQUENCE); copy /
  paste does not carry conditional formats, and a cut / paste moves the cells' formulas but not the
  names' nor the rules' (rows / columns inserted or deleted move all of them); comments; data
  validation (drop-down lists); pivot tables; `.ods` writing; printing / PDF; the € in wtk's
  text boxes.
- **The freeze at its first start on the Pi (fixed, `73a1eb05`).** Its window was 1060 pixels
  wide; the kernel then made none over 1024 × 768 (`CreateWindow`, `sys/kapi.cpp`; since v66, none
  bigger than the screen) and returns a
  null canvas, which wtk drew into: an app runs at EL1 with the kernel's identity mapping, so the
  first frame overwrote the kernel at address 0 — the Pi froze, nothing in `kmsg`, no
  `lastcrash.txt` (a Pi without RAM above 3 GB keeps no record, and a panic halts core 1 too), the
  watchdog restarted it. Now 1000 pixels; wtk's `Root` stops an app the kernel gives no window;
  the desktop simulator refuses windows over 1024 × 768 (its screen) as the kernel does. How it was found, and
  worth reusing: the Pi binary itself run under **qemu-aarch64** with the simulator's kapi (a
  loader mapping the ELF's segments, the kapi table at `KAPI_TABLE_VA`, a 4 MB stack with a guard
  page), valgrind and ASan on the simulator build (for ASan, a copy of `kern/kapi_abi.h` with
  `KAPI_TABLE_VA` moved out of its shadow). **Kernel follow-ups worth doing**: unmap the first
  pages of the identity map in the apps' address spaces (a null pointer would fault instead of
  writing over the kernel), and stop a faulting app instead of the kernel panic (any app fault
  takes the whole Pi down today).

## Writer as Word: tables, pages, fields, .docx / .odt; Cardfile's mail merge (2026-09-29, `claude/happy-wright-wg38ez`, pushed to `main`)

Asked by the user: Writer pushed further (".odt, .docx, tables, a table of contents, headers and
footers, pagination") and, in Cardfile, a mail merge with a Writer letter (one record → one
document; all the records → a series of documents). Done (the user guide: docs/04 *Writer* —
*Tables*, *Pages*, *The mail merge* — and *Cardfile*; the pieces: docs/03):

- **The model** (`doc.h`): stories (the body, the header, the footer, the first page's own; one
  edited at a time), tables (a run of paragraphs saying their cell; the `Table` — widths, heights,
  spans, shading, lines, heading row — a value replaced whole by an edit, so undo covers it), fields
  (`FIELD_CHAR` + a `Field`: page, pages, date / time with a picture, merge field), tab stops with
  alignments and leaders, keep with next / lines together / widow control, TOC and header / footer
  styles. **The layout** (`layout.h`): boxes (the text's width or a cell's), the headers and
  footers first (the body's top follows the header's height), a `Pager` (headings kept with their
  next line, widows / orphans, a table broken between its rows with its heading row repeated).
  **Editing** (`edit.h`, `view.h`): the table commands (rows, columns, merge, split, even widths,
  properties), Tab from cell to cell, a column border dragged (page or ruler), a double click on a
  header / footer (the body greyed), Insert ▸ Page Numbers / Field / Table of Contents, Tools ▸
  Update Table of Contents, Format ▸ Tabs.
- **Files**: RTF extended (tables, headers, fields, tabs, the TOC's field); **`docx.h`**
  (WordprocessingML) and **`odt.h`** (ODF) read and written, over `xml.h` (a pull reader on a zip
  entry; the zip written with `pngsave.hpp`'s deflate). **Tested** by `sh tools/tests/run_writer_test.sh`
  (14936 checks: a document with all of it through RTF, .docx and .odt, and LibreOffice's
  conversions of ours when `soffice` is installed — `apt install libreoffice-writer` in the cloud
  container; valgrind clean with `VG=1`). `tools/tests/writer/conv.cpp` converts a file.
- **The mail merge**: Writer's `merge.h` (the data read with Cardfile's own `model.h`; Tools ▸ Mail
  Merge: fields inserted, values previewed, merged to a new document or to files named after a
  field) and `writer --merge JOB` for Cardfile's **Record ▸ Mail Merge** (`MergeBox`: this record or
  all those shown; one document or files; the form's `merge` key remembers the letter).
- **Fixed on the way**: a question asked at an app's start (the recovered document of Writer,
  Cardfile, Paint, the Spreadsheet; a merge's end) got no click nor key — `Root::run` hooked the
  pointer and the keys; they are hooked (`root.attach ()`) right after the `Root` now. **Keep it
  so in a new app that asks something before `run ()`**: the kernel drops a window's events while
  it has no handler. `xml.h`: `XBuf::str ()` of an empty value was not ended (valgrind).
- **Samples**: `SD:/docs/writer-tour.rtf` (two pages: a TOC, a header / footer — the title page's
  own —, a table) and `SD:/docs/new-year-letter.rtf` (the Contacts form's letter; `contacts.card`
  names it), both by `tools/gen_writer_sample.py`; `.docx` / `.odt` open in Writer
  (`fileassoc.ini`). Screenshots `writer.png`, `writer-table.png`, `writer-merge.png`,
  `cardfile-merge.png`.
- **Next ideas**: a table's rows split across pages (a row taller than a page runs over its foot
  today); text boxes and shapes (dropped when read); comments, tracked changes (read accepted);
  columns (newspaper); footnotes; sections with their own page setups; a mail merge's conditions
  (IF fields) and a filter on the records; printing / PDF.

## Ledger, Belgian accounting (2026-09-29, `claude/happy-wright-wg38ez`, pushed to `main`)

Asked by the user: "un logiciel de comptabilité soigné, professionnel et utilisable, pour une PME ou
un indépendant, avec le PCMN belge et la déclaration TVA XML (Intervat)" — customers / suppliers,
purchases / sales, misc. operations, general ledger and journal; GnuCash as the reference for the
look, BOB 50 for the features; then documents from templates (quotes, orders, delivery notes) and the
general ledger, income statement and balance sheet in Writer or the Spreadsheet. Done (the user
guide: docs/04 *Ledger, the accounts*; the pieces: docs/03):

- **`user/Apps/ledger/`** (integer only; the engine plain C++, tested on the PC): the PCMN (French /
  Dutch), parties (VAT numbers and IBANs checked, a VAT situation choosing the codes, a language for
  the documents), sales / purchase invoices and credit notes (Odoo's Belgian VAT codes, reverse
  charges, half-deductible cars, the entry shown as typed, a sale's structured communication), bank
  and cash statements (open items ticked, matched), misc. operations, matching, the fiscal years
  (closed: the result appropriated), the VAT grids and Intervat's checks, the return's XML, the
  settlement (451200 / 411200), the customer and intra-Community listings, reports (journals, general
  ledger, trial balance, balance sheet, income statement, balances, ages, a party's account, VAT
  detail) to Writer (RTF), the Spreadsheet (.xlsx) or CSV.
- **Quotes, orders, delivery notes, purchase orders** (`commerce.h`, `commerce_ui.h`): numbered by
  kind and year, each becomes the next, then the invoice (posted, the document marked invoiced).
- **Printing from templates** (`print.h`): the data written as Cardfile forms (the document's, its
  lines'), then `writer --merge`: Writer's merge job has a new key, **`lines`** — the template's table
  row holding `Line...` fields repeated per line (`merge.h`, `merge_lines`). The templates (French,
  `nl/`, `en/`, and `fields.card`) by `tools/ledger/gen_templates.py`; Settings ▸ Printing edits them.
  Writer's RTF reader now tells a table's lines apart (rows only → `TB_ROWS`; test added).
- **CODA import** (`coda.h`): the bank's statements, their parties and invoices found; the statements
  shown one after the other to complete (`main.cpp`'s queue, `StatementPage::loadImport`).
- **SEPA payments** (`sepa.h`, `payui.h`): pain.001.001.09 (hybrid addresses, as Febelfin asks from
  November 2026), validated against ISO's schema; the invoices flagged `P` (Transfer sent).
- **Demo** `SD:/docs/demo-company.ledger` + `SD:/docs/demo-bank-statement.cod` (made by
  `tools/ledger/make_demo.cpp` through the engine: 2025 closed, 2026 to September, quotes and orders);
  `ledger = ledger` in `fileassoc.ini`; the icon (`tools/gen_assets.py ledger`); screenshots
  `ledger*.png` (the `ledger` scenario of `shots.sh`, `ledger-print` through Writer); host test
  `sh tools/tests/run_ledger_test.sh` (200 checks; the XML validated when `xmllint` is installed).
- **The manual** (asked: "un manuel pour le logiciel de comptabilité, avec captures, en .md et en pdf;
  tout gros logiciel fera l'objet d'un manuel"): `sdcard/manuals/ledger/Ledger.md` + `Ledger.pdf` (55
  pages) + `images/` (43 pictures: `sh tools/manuals/ledger_shots.sh`; the PDF:
  `python tools/manuals/build_manuals.py`) — docs/03 *Manuals*. On the way: an opened statement or
  invoice saved again kept its matchings no more (`entry_save` now carries them; test added), a saved
  statement's movements show what they paid, a new statement proposes the bank's next number, the
  reports' and VAT page's columns fit, the demo's "Flémalle" in Latin-1. Then (asked) the manual in
  French and Dutch too: `Ledger.fr.md` / `.pdf` (59 pages), `Ledger.nl.md` / `.pdf` (60). **Next**: a manual reader app
  on Onyx (the same Markdown subset), then a manual for each big app (Writer, the Spreadsheet...).
- **Next ideas**: **e-invoicing** — Belgium requires structured B2B invoices through **Peppol** from
  2026: a sales invoice as UBL (Peppol BIS Billing 3.0) and a purchase UBL read would be the most
  useful next step; CAMT.053 statements (the XML successor of CODA); payment reminders from the
  overdue invoices (a template like the others); an articles catalogue for the quotes' lines (their
  prices, units); recurring invoices; invoices typed as quantity × price like the quotes; a party's
  statement printed; analytic codes; foreign currencies; the annual accounts (NBB) not done.

## The GameCube on the Pi -- the TEV renderer (the black screen: fixed; next: the speed)

- **Done (cloud session), all pushed:** option A of `docs/GC-WINDOWS-REPORT.md` §5 -- the GX on the
  V3D with generated QPU shaders:
  - **Kernel ABI v61** `gpu_program` / `gpu_render2` (`kernel/sys/v3d.cpp`): the app's own
    vertex / coordinate / fragment shaders, batches with uniform ranges, up to 8 textures, blend
    factors, write mask, scissor; generic CPU clipping (`V3DClipTriangleN`).
  - **QPU toolchain**: `user/v3d/qpu.h` (C++ instruction builder over Mesa's packer),
    `user/v3d/shaders.h` (pass-through VS / CS, simple FS), `tools/qpu/qpulib` (instruction
    restrictions checker), `tools/qpu/qpusim` (fragment-shader simulator).
  - **TEV generator** `user/v3d/gxtev.{h,cpp}`: a TEV configuration -> fragment shader in
    integers as the hardware; `user/v3d/gxtev_ref.h` = gxgl.cpp's GLSL TEV in C++. Checked:
    `tools/tests/run_qpu_test.sh` (thousands of random configs in the simulator, exact) and on
    the Pi: `/bin/v3dprog` -> **ALL PASS 24/24** (incl. 11 TEV configs, Wind Waker's first two).
  - **gcemu backend** `user/Apps/gcemu/gxv3d.h` (`Rec`: gc::GxGpu on the app core -- vertices
    through gxgl.cpp's VS ported to C++, TEV program cached by key, uniforms, state; `Out`: main
    thread -> gpu_program / gpu_texture / gpu_render2). View > TEV Shaders On / Off.
  - **PC harness** `tools/tests/gc/gcv3d.cpp`: runs a `.dol` / `.iso` with the same `Rec` and
    draws the last frame with a software V3D (the shaders in qpusim) -> `.ppm`. Build line in
    `tools/tests/run_gc_test.sh` (on Windows: WinLibs MinGW `g++`, see its line); options in its
    header (`GCV3D_EVERY`, `GCV3D_DUMP=1/2`, `GCV3D_SAVE`, `--replay`).
- **The black screen -- fixed (local session, tested on the user's Pi with the ISO).** Three bugs,
  one behind the other:
  1. **The GPU hung** on the first TEV frame (`gpu_render2` -3, kmsg "rendering timed out ... GPU
     left off", then -1 until a reboot). The TEV shaders without texture lookups were flagged
     `KAPI_GPU_P_FS_FINAL` (start in the final thread section). `v3dprog ww` (a w x h target, one
     quad with WW's first TEV program) showed it: hangs now and then on targets >= 512 pixels wide
     (5 runs of 7, mostly the first frame after boot), never at 64 x 64 (the v3dprog tests). Now
     `gxtev` always ends with the last-segment pair (as Mesa): no hang since, in hours of frames.
     docs/02 §15.
  2. **The window was never redrawn** once the game ran slower than real time: gcemu's loop asked
     for the next field before looking for the new picture (`ec_pending` then never 0 there, the
     F12 line showed `draw 0.0 ms`). The picture is now taken first. **n64emu has the same loop**
     (a separate task was suggested to the user: same fix).
  3. **No texture reached the GPU**: `Machine::gxTexture` allocated the texture pixels with `new`
     on the app core -- `umm` is not safe across cores and `kapi_sbrk` from core 2 grows the heap of
     the task core 0 happens to run (seldom gcemu): `px` null (and the decoding wrote through it).
     Now a pool of `TEX_POOL` texels (64 MB) made with the machine; full, every texture is
     forgotten (`texFlush`, the draws re-resolve). docs/03 (the app-core rules).
  And `Out::render` kept only half of `KAPI_GPU_MAX_VERTS`: the island flyover (147 K vertices)
  lost its last 250 batches (the logo, the sea); now 7/8 of it (the rest: the clipping's room).
- **State:** The Wind Waker runs with the TEV on the Pi -- the title (island flyover, logo, "Press
  Start"), the intro story; frames dumped on the Pi and replayed on the PC give the same picture
  (<= 2 % of the pixels differ, on edges). Diagnostics for the next bugs: F12's third line, F9 /
  `--diag[=FTP:<pc>:<port>]` dumps, `gcv3d --replay` (docs/03).
- **The speed (local session, first pass).** F12's fourth line / `--diag` measure it (docs/03).
  The island flyover (~370 batches, 147 K vertices a frame, ~4800 draws of ~5 vertices a field)
  went from ~7 to ~12 fields/s, lighter scenes 20-33 (60 is real time):
  - the drawing overlaps the next field (`Out::prepare` while the machine waits, `Out::submit`
    after the request), and a frame already in the window is not drawn again (30 fps: half);
  - the recorder: only the colour channels the program reads, the alpha from the colour's
    lighting when the controls match, the lights read once a draw, the last TEV configuration
    matched without its hash -- 366 -> 271 cycles a vertex on the PC, exact (the pictures
    identical, `gcv3d`);
  - `gpuTexture` remembers each map's answer within a field (`texMemo`, `texEpoch`: a field, an
    EFB copy, a TLUT load, a DMA, the pool emptied): 7.7 -> 0.4 ms a field.
  Where a heavy field's ~75 ms go now (app core): the recorder ~29, the vertices decoded + the GX
  state ~7.5, the FIFO ~2.5, the textures 0.4, **the CPU (JIT) and the rest ~36**; the main thread
  (~9 ms copy + ~43 ms `gpu_render2` a frame, the kernel's CPU clipping mostly) now runs beside.
  Next, by gain / effort:
  1. **The CPU**: profile the JIT on the Pi (`jitProfile` / `GC_PROFILE` in gctest: the hot
     blocks; the instructions still interpreted, the FPR loads / stores per instruction, the
     dispatch) -- 36 ms a field alone is twice real time.
  2. **The vertex stage on the GPU**: a generated vertex shader (the XF transform, the lighting,
     the texgens: `Rec::record`'s work) fed with the decoded vertices and the matrices / lights as
     uniforms -- removes most of the recorder's ~29 ms; the per-draw setup (~22 % of it) could
     meanwhile be cut by reusing the last draw's program / uniforms when `GxState::serial` has not
     changed.
  3. A fast path in the kernel's `V3DClipTriangleN` for the triangles fully inside (most): the
     main thread's ~43 ms (not on the critical path any more, but it is the display's latency).
  4. Dual-issue scheduling of the generated TEV code.
  Then stage 4: EFB copies to textures (`rec skipped` counts the draws reading one: the heat haze,
  the bloom...), the fog, the indirect textures.
- **The speed, second pass (local session, 2026-09-28).** `netcore=0` on the user's Pi (both app
  cores free): the GX runs on core 3 while the machine runs on core 2 (docs/03, "The GX on its
  own core"). A command-by-command copy of the FIFO near its end made that slower at first (4 fps):
  the commands are now read in place. The Wind Waker (PAL, 50 fields/s) on the Pi: the title's
  heavy flyover ~20 fields/s (the GX core ~90 % busy, ~45 ms a field: the recorder ~32), the
  lighter scenes 35-50, the user's first steps on Outset ~15 fps (60-65 %). JIT (docs/03): the
  memory fast path (one EOR check, loads into the destination), compare + branch fused on the
  host's flags (`crDead`); `gctest fuzz` passes (GC_FUZZC too). The JIT profile (--jitprof,
  200 s): 7.1 host instructions a guest one; lwz / stw 8.5, bc 7.7, b 9.2, lfs 12.3, cmpli 9.4
  before those two changes; a block run ~10 guest instructions; the CPU part is ~90 % JIT code,
  IPC ~0.85 (L2 refills ~4 a thousand instructions).
  **Fixed: the camera "in Link's head"** (the user's report; on the PC it was right): not the
  camera -- the recorder framed the vertices with the copy registers of the draw's time, and a
  half-size EFB copy to a texture in the middle of the frame made the rest a 2x zoom of the top
  left quarter (the title screen too). Found by comparing `gcrun GC_GL=1` and `gcv3d` on the same
  scripted game (`GC_PAD`, `GC_CARD`, `GC_HASH`: the same MEM1, so the drawing). Now the EFB's
  space + `Frame::rect` (docs/03).
  Then (same day): the JIT's base pointers (a D-form access through a register found in MEM1:
  one load / store; little measured gain -- the CPU part is memory-bound), the code buffer's
  hot / cold chunks (L1I refills 20 -> 17.5 a thousand instructions, -7 % cycles), the
  recorder's and the vertex decoder's per-draw plans (the recordings bit for bit the same:
  `GCV3D_VHASH`). The CPU-bound scene of the intro: 20.4 -> 17.0 ms a field (50 fields/s, full
  speed); the user saw 15-20 fps in the game before the GX plans.
  Next, by gain: the JIT's hot code smaller still (the not-taken paths of the branches the
  compiler hints as unlikely out of line; the FP compares fused), a second, optimising tier for
  the hot code (whole functions, the registers kept across blocks, the hot code packed), then the
  display's `gpu_render2` (~20-45 ms a frame on core 0: the kernel's clipping and copies).
- **The speed, third pass (local session, 2026-09-28 evening).** Measured in the game (Outset's
  bridge, ~96k vertices a frame) with `--statlog=FTP:<pc>:2121` (F12's lines whole, every
  second; docs/03): 30.6 -> **~38 fields/s** (~19 fps of 25; F10 said 14-15 fps, 60 %, before).
  - LR, CR, r3, r0 kept in host registers across the blocks (the JIT's SRA), a call counting as
    setting CR0 (`crDead`), `gctest jitsize` (6.56 -> 5.85 host instructions a guest one on the
    Pi profile's hot blocks); `gctest fuzz` (GC_FUZZC, GC_FUZZ2 too) passes.
  - gcemu's loop: the TEV frame prepared and drawn after the next field is asked for, two fields
    asked for when a frame is drawn and the game is behind (the machine, its field done, idled
    ~13 ms each frame while `gpu_render2` ran), the main thread naps while the field's end is
    far (a yield in a loop: core 0 busy, heat).
  - The kernel's `ClipFrame2`: the triangles inside copied once, straight into the GPU's buffer
    (were four copies): `gpu_render2` 34 -> 25 ms a frame; its split in kmsg (docs/02).
  - Where it stands: the machine (core 2) ~26 ms a field, busy ~99 % — the bottleneck; the GX
    (core 3) ~22-24 ms, ~87 %; the main thread ~32 ms a frame (prep 6.8, clip 9.8, GPU 14).
  - **The Pi was throttling**: kmsg `power: SoC 81-84 C ... soft temp limit NOW`, the cores
    ~1.3 GHz (the PMU's cycles against the time) instead of 1.5: a fan / heatsink is ~+15 %.
  - Measures (`--pmu=08,01,52,53`, docs/03): the machine's IPC 0.84, L1I refills 11.4 and L2
    read refills 3.2 a thousand instructions — its data from the RAM. `--nodraw` (the frames
    recorded, not drawn): the machine 37 -> 30 M cycles a field (IPC 1.02), ~45 fields/s, the GX
    core then 98 % busy: **the display's copies (core 0) cost the machine ~18 %** through the
    shared L2 / RAM, and the GX is right behind.
  - The JIT profile (`--jitprof`, 180 s, the game): 5.36 host instructions a guest one on the
    main paths; the branches ~33 % of them (bc 12.7, bclr 8.8 -- plus the dispatcher's --, b / bl
    7.7, bcctr 3.4), the loads / stores ~42 % (lwz 13.4 at 5.7 each, lfs 9.6 at 10.1), fcmpo 4.5 %
    at 14.8 each. The hottest loop (~17 %): a linked list searched through two function pointers
    a node (`8024A118..8024A150`: bctrl -> `8024A7E0` -> bctrl -> `80043E1C` -> blr -> blr), 22k
    nodes a field: four trips through the dispatcher a node.
  Then (same evening): the recorder leaves out the triangles behind the eye (~10 % in the game:
  85k vertices a frame for 96k, prep 6.8 -> 5.8 ms, the pictures the same), and **the JIT's calls
  / returns on the host's `bl` / `ret`** (a stack of pairs {landing, return address}: docs/03;
  `gctest calltest`, the fuzz pass): branch mispredictions 5.6 -> 2.0 a thousand instructions,
  **~42 fields/s** (21 fps) on Outset. Both app cores are now full: the machine ~23 ms a field,
  the GX ~22 (the recorder ~15.7 of it); core 0 ~29 ms a frame (prep 5.8, gpu_render2 23).
  Then the recorder's plans kept from a draw to the next (the draws: 4.6 vertices, ~90 % of them
  with the last one's state): the recorder 15.7 -> 8.2 ms a field, the GX core ~61 % busy (docs/03).
  **Only the machine is full now** (~23 ms a field: ~43 fields/s). Seen meanwhile: after a few
  hundred FTP transfers (`--statlog=FTP:...` every 5 s over long runs) the Pi's `ftpfs` no longer
  opened its passive data connections ("Passive data channel timed out" on the PC; a failed
  `cat FTP:... > file` then leaves cat's error in the file) until a reboot -- not investigated;
  `--statlog` on `SD:/gcdump`, read with `cat` over telnet, avoids it.
  Then **kapi v62 `gpu_render3`** (the kernel reads the recorder's frame where it is: each batch's
  offset and stride, framed on the way) and the recorder's three frames (the one drawn is never
  built into): `Out::prepare` 5.9 -> 0.1 ms, the machine 33.5 -> 29.9 M cycles a field, **~46-47
  fields/s (~23 fps, 94 %)** on Outset -- measured: without the display at all (`--nodraw`) the
  machine runs at full speed (26.8 M cycles, IPC 1.14); `--noren` (prepared, not drawn) said the
  copy in `Out::prepare` cost it ~2.9 M cycles, the kernel's copy + the GPU ~3.8 M. The machine's
  time is 98 % in the JIT's code (--pmu: `in JIT`), its runs end ~310 times a field on VI lines
  and ~80 on the audio DMA, the JIT is entered ~2000 times (mtmsr / mtspr / rfi / sc leave it).
  Then **kapi v63 `gpu_vbuf`**: the recorder's frames in GPU-visible memory, `gpu_render3` draws
  them in place (framed in place, runs of the inside triangles, the clipped ones into the
  buffer's end): no copy left (docs/02). The user's game then: ~48-49 fields/s (~24 fps, 92-98 %)
  in the scenes played, the GX core ~44 % busy. Not yet measured against v62 on the same spot:
  the kernel's pass still reads every vertex's position and writes its x / y back (the framing),
  ~85 ns a vertex against ~105 for the copy.
  What is left, estimated: the next section.
- **gcemu's speed: what is left, estimated (2026-09-28, end of the day).** Where it stands (The Wind
  Waker PAL, Outset; the Pi throttling at 80-83 °C): **~46-49 fields/s of 50 (92-98 %), ~23-24
  fps of 25** -- "perfectly smooth" on the user's TV. The machine (core 2) is the only full core: ~29-30 M cycles, ~21 ms a field; the
  GX core ~44-69 % busy (9-14 ms a field); core 0 ~17-21 ms a frame (the kernel's pass ~3.5 ms,
  the GPU ~13 ms). Full speed everywhere needs the machine at <= 20 ms a field: its cycles down
  by ~5-10 % here, more in heavier scenes. The gains below are on the machine's time unless said,
  from the measures of this session (`--pmu`, `--nodraw`, `--noren`, `--jitprof`); they do not
  simply add up.

  | # | What | Estimated gain | Effort, risk |
  |---|---|---|---|
  | 1 | A fan / heatsink on the Pi (kmsg `power: SoC 80-83 C ... soft temp limit NOW`: the cores ~1.3-1.4 GHz instead of 1.5) | +7-15 % on every core | none (hardware) |
  | 2 | Then an overclock (`arm_freq` 1750-2000 + `over_voltage` in config.txt, with the cooling) | +15-30 % more | the user's call (boot config) |
  | 3 | The display's last traffic: the XFB framing in the vertex / coordinate shaders (`user/v3d/shaders.cpp`: 4 more uniforms, 2 fmul + fadd a coordinate) so the kernel only reads the positions -- or the recorder flagging the batches wholly inside (it has the positions; a conservative guard band) so the kernel skips them. The display still costs the machine ~9 % (`--nodraw`: 26.8 against 29.3 M cycles) | -3-5 % (the kernel's pass 3.5 -> < 1 ms a frame) | medium (QPU code, v3dprog) |
  | 4 | JIT, small: the call landing's cycle check (its return checked them: -2 instructions a call), `and` / `orr` immediates on blr / bctrl (-1 each), the CR field from an NZCV table (`mrs nzcv` + `ldrb`: 7 -> 5 instructions a materialized compare, the same results), fcmpo / fcmpu fused with their branch like the integer compares (14.4 host instructions each, ~4 % of the hot code) | -2-4 % | small, fuzz + calltest under qemu |
  | 5 | JIT: mtmsr / mfmsr native (OSDisable / RestoreInterrupts: ~550 of the ~2000 exits to C a field; each one also empties the call / return pairs): exit only when EE comes on with an interrupt pending, or IR / DR change | -1-2 % | small-medium |
  | 6 | Fewer run ends: a VI line (~310 a field) only when a VI interrupt can fire on it; the audio DMA's ~80 | -0.5-1 % | small |
  | 7 | Fastmem: MEM1 mapped so that a load / store needs no address test (`and` + `ldr` + `rev`), the kernel forwarding an app core's data abort (`AppCoreOnFault`) to a handler that patches the site to its slow path (Dolphin's backpatching): -3 instructions an access, ~15 % less host code (the L1I: 11 refills a thousand instructions) | -5-10 % | large (kernel + JIT + tests) |
  | 8 | A second JIT tier for the hottest code (whole functions / traces, the registers kept across their blocks, the code packed): the hot 80 blocks are ~47 % of the time | -10-20 % | large |
  | 9 | The GX (not the bottleneck now): the recorder's vertex loop on NEON, four vertices at once (-30-50 % of its 5-8 ms); indexed geometry / packed attributes (less memory traffic: the machine -1-3 %) | GX only, machine -1-3 % | medium-large |

  Likely path: 3 + 4 + 5 give ~50 fields/s in the scenes measured; with 1 there is a margin; 7 and
  8 are for heavier scenes and games. Not worth it: a degraded display (the sea left out...) --
  the machine emulates the game's CPU whatever is drawn; SVE / SVE2 -- the Cortex-A72 has NEON
  only, and the JIT already keeps the FPRs and paired singles in NEON registers.
- **Testing on the Pi yourself** (on the user's network; ask its IP -- it was 192.168.0.7):
  - a console: `telnet <pi-ip>` (telnetd, port 23; or OnyxRemote's Console button). If telnetd
    stops answering (a process spinning, see below): in the Pi's Terminal `ps`, then
    `kill <pid> --force`; or `reboot`.
  - files: an anonymous FTP server on the PC (e.g. Python's `pyftpdlib`, port 2121) and on the Pi
    `cat FTP:<pc-ip>:2121/<file> > SD:/<path>` (ftpfs; `cp` does not go through it); gcemu's
    `--diag=FTP:<pc-ip>:2121` writes its log and dumps straight there. Or `ftpd SD:/` on the Pi
    (port 21). `apps/gcemu.app/main` and `bin/*` need no reboot, `kernel8-rpi4.img` does.
  - run: `run gcemu SD1:/roms/ZeldaWIndWaker/ZeldaWIndWaker.iso --diag=...` (the second
    partition); `v3dprog` must pass after any GPU change; a GPU hang leaves the GPU off until a
    reboot (`v3dprog` then says `no GPU: V3D: stopped`).
  - pitfalls: `grep` / `wc` read stdin only (`wc < file`; with a file argument they wait, and
    once the telnet session is gone they spin on stdin's end -- an open bug, cmd too sometimes);
    `kmsg` streams until Ctrl+C and consumes the log (each line is read once).
  - the picture: `OnyxRemote.exe` (`pc/dist/`, rdpd port 3390); or VNC (vncd, no password):
    `python -m vncdotool.command -s <pi-ip> capture x.png`, `... key p` (a key).

## NetSurf -- "as in Chrome" (kotonviolins.com, kotonstudio.com)

The goal, in the user's words: the two sites (theirs) drawn "comme sur Chrome" -- Chrome on
Windows, which they compare with on the Pi (portrait and landscape screens) -- and JavaScript
for the forms *and* the DOM. Every NetSurf patch is marked `Onyx:` in the source and listed in
`docs/06-JET-BROWSER.md` (read it first: it is the map of what changed and why).

**Done (2026-10-01): the browser is renamed "Jet Browser"** (decided by the user on
2026-09-30; docs/06 §31). The app is `sdcard/apps/jet.app` (`name = Jet Browser`), launched as
`jet` -- no `netsurf` alias, `run netsurf` no longer exists; the window is titled "Jet";
Help > About Jet Browser... reads "Jet Browser -- the Onyx web browser, based on NetSurf" and
credits NetSurf (GPL v2, its copyright) and the libraries' licences. The dock's default
Internet button launches `jet` (`user/dockconf.h`, `user/Apps/setup/main.cpp`,
`sdcard/etc/dock.ini`, `sdcard/etc/quicklaunch.txt`; dock / dockconf / theme / setup
restaged). The user's files are under `SD:/apps/jet.app/` (`ONYX_NS_DATAPATH`); at start,
`Cookies`, `History` and `desktop-sites` missing there are copied once from
`SD:/apps/netsurf.app/` (`gui.c`, `onyx_carry_old_data`). docs/06 is now
`docs/06-JET-BROWSER.md`. The source paths (`third_party/netsurf/`, `user/netsurf/`), the
binary's build name (`netsurf.elf`), the PC bench (`tools/tests/netsurf/`, `build/netsurf`)
and the kernel log's `netsurf:` lines keep NetSurf's name. On the Pi: copy
`apps/jet.app/` to the card (main, app.txt, icon.bmp) and `apps/dock.app/main`; the old
`SD:/apps/netsurf.app/` can be deleted after a first start of `jet`. In the history below,
`netsurf.app` and `run netsurf` are the names of the time.

**Done (2026-10-01): an idle Jet Browser no longer slows the Pi (docs/06 §32).** The user's
"when Jet is running, the whole system gets slow": (1) kotonstudio.com's pulsing dot (a
`box-shadow` animation: paint, never composite-only) ran ~60 frames a second, each walking the
box tree twice and compositing the whole view -- now ~30 Hz (15 for a change of a few px, half
unfocused), none while the window is hidden (minimised / other workspace / covered, from
`kapi_win_geometry` + `kapi_win_list`: `visibilitychange`, timers >= 1 s, GIFs stopped, nothing
painted), the restyle walks only the animated subtrees, only the changed rectangle is
composited; idle 6.5 / 13.7 % -> ~1.8 % of a PC core (floor 0.8 %), `tools/tests/netsurf/
idlecpu.py`. (2) the SD write stalls: the disk cache and the code cache store on second sight,
no body over 512 KB, 16 KB pieces with a sleep (`user/netsurf/onyx_io.h`), waiting while the user
acts / a page loads; the disk cache was **write-only on the Pi** (`kapi_save_file` answers the
bytes written, it checked `== 0`) -- fixed. google.com: 10.4 MB written at the first visit ->
5 KB. Kernel side left for another session: the EMMC driver's busy wait (`TimeoutWait`) should
yield; `kapi_present` has no rectangle. Pi app staged.

**Feature tests on the Pi (2026-09-30 late, docs/06 §23):** the Pi's css3test.com "100 %" /
browserscore.dev "0 %" were the script time limit (10 s) cutting both test runs off on the
slower CPU (css3test's 100 % is its CSS 2.2 / 2007 / 2010 filter, kept in localStorage) -- now
60 s (`script_timeout`) and the runs 2x faster; the detection is honest (`js-cssdetect.html`
against Chromium). PC bench: css3test 83 %, browserscore 86 % (Chromium 71 %, 75 %). Also the
Popover API (`:popover-open` / `:modal` in libcss), matchMedia by libcss (aspect-ratio,
orientation, hover...), `NS_JSPROF` + `jsprof.py` (a sampling profiler of the scripts). The
Pi's `libcss.a` rebuilt; the NetSurf binary for the card NOT restaged by this work.
**Then (2026-10-01, docs/06 §28)**: browserscore.dev still "0 of 0" on the Pi -- Vue's first
render job hit the 60 s limit with the `jsdebug` log on. The console's formatting bounded hard,
Map / WeakMap object keys hashed properly in QuickJS (Pi `libquickjs.a` rebuilt), `new URL`'s
cache, no rebox for `getComputedStyle('--x')`, a native `getElementsByClassName`: the job 8.5 ->
6.4 s (log on), 7.0 -> 6.5 s (off) on the PC; the rest is the interpreter. And the time limit now
spares a script still changing the page (up to 4x the limit). To try on the Pi: browserscore.dev
with `jsdebug` on (kmsg: `JS: a script past 60 s still changing the page` if it runs that long).
Not restaged either.

**START HERE -- 2026-09-30 evening, branch `claude/busy-ramanujan-5enakb` ("improve NetSurf as
far as conceivable, keeping the speed": css3test >= 50 %, google and facebook usable, no more
out of memory on bbc.co.uk, HTML5).** Built and tried on the PC bench only; the Pi binaries
staged at the end of the session (`sdcard/apps/netsurf.app/main`, NOT yet tried on the Pi).
docs/06 §12-§19 describe each piece; read them first. Where it stands:
- **css3test.com 23 % -> 81 %** (Chromium 71 % on the same copy): libcss parses by the specs'
  grammars, a real CSSOM, SVG's properties (§14). **html5test 252 -> 369 / 588**. The HTML
  parser passes html5lib's tree construction 100 %, its tokenizer 99.9 % (§16); the HTML5 DOM
  (§17), Intl on ECMA-402 (§15, 92.6 % of test262's intl402 subset), SVG images and inline SVG
  and canvas 2D on PlutoSVG / PlutoVG (§12, §13), a real shadow DOM with style scoping,
  WebSocket, EventSource, streamed fetch / XHR, Workers (§19).
- **bbc.co.uk's out of memory**: fixed (§18: its root cause and the JS heap limit); bbc.com
  ~170 MB steady on the PC. **google.com**: the logo, the footer (early layout, script-blocking
  sheets) -- the results page is untested (Google answers a captcha to this container's IP: try
  it on the Pi). **m.facebook.com**: the login form as in Chrome (Fetch Metadata headers, late
  style sheets restyle, mask-image icons, aspect-ratio); typing kept; not logged in yet.
- **Speed**: the JS preludes compiled once per process (a page's context 37 -> 5 ms), the box
  tree built in 15 ms slices, the fetch workers sleep instead of spinning (60 % of the CPU on
  bbc.com), job slicing of the microtasks. React hydration is still the big cost (bbc.com: a
  1.4 s script on the PC -- on the Pi several seconds).
- **The bench** (`tools/tests/netsurf/`): `jstest.sh` (all the JS / DOM / CSS regression
  pages), `nettest.sh` (WebSocket / SSE / workers), `sitesweep.sh` (15 live sites: crashes and
  script errors -- run it after any core change), `site.sh <url> <name>` (NetSurf and Chromium
  side by side), `layoutdiff.sh` (box by box against Chromium), `prof.sh` (a sampling profiler:
  `NS_PROF=<file>`), `NS_BOXDUMP=<file>` / `NS_INJECT=<file.js>` then F5 (`key 276`),
  `html5lib.sh`, `css3test.sh`, `html5test.sh`, `urltest.sh`, `wpt.sh`, `iframetest.sh` (iframes,
  postMessage, MessagePort across frames, a local reCAPTCHA v2 mimic on two origins: docs/06 §26). The container reaches the
  web through a proxy (`fakekapi.cpp` tunnels with CONNECT); OpenSSL gives the bench https.
- **To try on the Pi first**: kotonviolins.com / kotonstudio.com (regressions), bbc.co.uk (the
  memory), google.com (search, results), m.facebook.com (log in), en.wikipedia.org, a
  WebSocket echo. Watch `kmsg` for `app:` lines and `SD:/etc/apphang.txt`.
- **Layout / rebox performance (docs/06 §26)**: the flex layout memo (m.facebook.com's
  layout pass 170 ms -> under 1 ms on the PC; its cookie dialog took 3.5 s a pass on the Pi),
  the style selections kept between box trees (github.com's rebox 150 ms -> 6-20 ms),
  attribute-only changes restyled in the boxes, reboxes coalesced / throttled.
  `NS_RESTYLE_CHECK=1` / `NS_NORESTYLE=1` / `NS_NOINPLACE=1` on the bench to check them.
- **Iframes as windows (docs/06 §29)**: postMessage, MessagePort across frames: Google's captcha
  page (www.google.com/sorry/, reCAPTCHA's "I'm not a robot") -- the bench's mimic of its frames
  passes; the real one is to be tried on the Pi by the user, not by the tests.
- **Next**: an incremental layout and box construction (06 §26 "Left"), a worker thread for
  Workers, Google's results on the user's network.

**Done, in `main`, staged on the card:** CSS3 (calc / var / grid / flex / gradients /
shadows / radii / background-clip: text / vendor prefixes), Chrome's Windows fonts
(metric-compatible stand-ins, web fonts with WOFF2 and variable fonts, baseline alignment),
**JavaScript on QuickJS** (ES2023; the DOM in JavaScript; the page laid out again after a
script's changes; clicks / keys / typing / submit / scroll / load to the scripts), the
**painting order of positioned boxes** (z-index layers) and a hit test in that order. The
hamburger menus of both sites open and their links work; kotonstudio's scroll reveals run
(IntersectionObserver).

**Done 2026-09-30 (tried on the Pi, kotonviolins.com drawn as before, no failed fetch):** each
download in a **thread** of its own (kernel v67; the connects one at a time -- several at once
all failed and the page was laid out without its style sheet); **`fetch`** (`Response`,
`Headers`, `Request`, `AbortSignal`) and **`XMLHttpRequest`** on a native `request()` over the
low-level cache (which now takes a request's own headers and keeps the HTTP status and the
headers); POST / any method and the request's headers in the Onyx fetcher; **`localStorage`
kept** (a file per origin); the **hover events** (`mouseover` / `mouseenter`... from
`onyx:hover`), **CSS `:hover`** (the styles made again when the node under the pointer changes,
if the page has `:hover` rules; tried on the Pi: kotonviolins' buttons), and a **back buffer**
(`user/nsfb/onyx_surface.c`: NetSurf draws off screen, `update` copies the rectangle redrawn into
the window's canvas -- the compositor showed half-drawn redraws: the page flickered at each
restyle; deployed on the Pi, the flicker not yet confirmed gone by the user). The PC bench builds
in WSL again (`build-essential`, `libpng-dev`, `zlib1g-dev`); `jstest.sh` covers them all
(js-fetch, js-hover, js-hovercss, js-storage).

**The state before that session (2026-09-30; all in `main`, pushed; the Pi binaries
staged: `sdcard/kernel8-rpi4.img`, `sdcard/apps/netsurf.app/main`).** Tried by the user on the
Pi: much faster (kotonstudio with all its images almost at once), hovers fine. Since then (the
last build, NOT yet tried on the Pi): the Android Chrome User-Agent by default, the yahoo.com
freeze fixed, ES modules, honest `CSS.supports` / `element.style`, a CSSOM. The docs: docs/06
§9-§11 (performance, the big sites' scripts, modules / CSS detection), docs/05 §18-19 (DNS,
TCP window), docs/02 (the kernel's sockets).

The next session's goals, in the user's words and order:
1. **DOM levels**: check which DOM Level (1/2/3, and the WHATWG DOM) features NetSurf supports
   -- `dom.js` + `qjs.c` natives over libdom -- write the table down (docs/06 §7), and fill
   the gaps (Range / Selection, TreeWalker / NodeIterator, `DOMParser`, `XMLSerializer`,
   Shadow DOM, `customElements` upgrades, `MutationObserver` details, events' fine points...).
   A regression page per area in `tools/tests/netsurf/pages/` (see `jstest.sh`).
2. **An HTML5-compliant parser**: hubbub (`third_party/libhubbub`) is an old HTML5 tokenizer /
   tree builder: check it against the html5lib-tests (tree construction, tokenizer), fix what
   fails (`<template>`, foster parenting, the adoption agency, `<svg>` / `<math>` foreign
   content, the insertion modes of tables and `<select>`, entity names), and innerHTML's
   fragment parsing (qjs.c saves/restores the quirks mode around it).
3. **css3test.com: aim at 50%** (23% now, 1154 of 6419 tests; the page copy: `wget -p -k -E -H
   -D css3test.com https://css3test.com/`, its 156 test modules fetched by following the
   imports, `bliss.js` from cdnjs put beside it -- run it with `NS_JSDEBUG=1 NS_PERF=1`). The
   score is what libcss parses (docs/06 §11): each new property / value / selector / at-rule
   parsed by libcss counts -- the cheap wins are the properties NetSurf already draws or can
   ignore (`opacity`, `transform` functions, `filter`, `mask-*`, `inset`, `aspect-ratio`
   variants, logical properties `margin-inline` ..., `place-*`, `gap` forms, `color-mix()`,
   `oklch()` / `lab()` / `lch()` / `hwb()`, `@layer`, `@container`, `@property`, `:is()` /
   `:where()` / `:has()` / `:not(list)`, `::marker`...). A property parsed but not drawn is
   still a win for pages (their other declarations are kept). browserscore.dev (the site's new
   version) is the same idea and worth running too.
4. **bbc.co.uk: "out of memory"** (the user, on the Pi): find why -- `SD:/apps/netsurf.app/perf`
   and `.../jsdebug` on, `kmsg` (the `app:` lines), `SD:/etc/apphang.txt` / `lastcrash.txt`;
   suspects: a huge image decoded whole (a `srcset` / `<picture>` choosing the largest), the
   whole-response buffers (a big JSON / script), QuickJS's heap (no GC threshold set: qjs.c
   `js_newheap`), the hover's kept style results (`HV_KEEP_MAX`), a loop allocating. The app's
   memory limit: the kernel's app pages (`ps` shows PAGES / MEM).
5. Then Facebook (m.facebook.com with the mobile UA: the consent loop is gone; it showed "Sorry,
   something went wrong" with the desktop UA -- look at `jsdebug`), SVG (logos and icons:
   libsvgtiny is not vendored), `opacity`, `position: fixed` on the viewport, the rest of the
   "Facebook" list below.

Checking on the Pi from the PC (the user's rule: ask before scanning the network -- never scan
it; they give the address): telnet (`kmsg`, `ps`, `cat SD:/etc/apphang.txt`), VNC captures
(`python -m vncdotool.command -s <ip> capture x.png`). The PC bench: `tools/tests/netsurf/`
(`jstest.sh`, `httptest.sh`, `shot.sh`; `SITES=$HOME/nssites`, `OUT=$HOME/nsbench` on this PC).
**Build traps met** (see the memory): after `make kernel8-rpi4.img` run `make sizecheck` (a
kernel past 2 MB does not boot); before `make -C user/netsurf`, delete the objects of a library
you changed (`find third_party/libcss -name '*.o' -delete`: old objects were mixed in and the Pi
build crashed); the NetSurf app build does not track headers (`rm -rf $HOME/nsbuild` after a
header change).

**Performance and the network (2026-09-30, tried on the Pi since: faster; at the time the Pi did
not answer; deploy `sdcard/kernel8-rpi4.img` (kernel + Circle changed) and
`sdcard/apps/netsurf.app/main`, update the card's `SD:/res/Choices` to `max_fetchers:8` /
`max_fetchers_per_host:6` -- staging keeps the card's own file):** measured first (docs/06 §9:
`onyx_perf.h`, the file `SD:/apps/netsurf.app/perf` logs the timings) -- the painting was the
wall (a full redraw 12.6 ms on the PC), now 4.9 ms (`onyx_paint.c`: spans, tables, the same
pixels); **CSS `:hover` restyles only what changed** and repaints only those boxes
(`onyx_hover.c`; on both sites every hover is a restyle, pixel-identical to a rebox); one
present per main-loop iteration; the loop waits on `kapi_pump_wait`. **The fetcher** rewritten:
HTTP/1.1 keep-alive pool, chunked, streaming, **cookies sent and stored** (the old one sent none
and dropped every Set-Cookie: the user's m.facebook.com consent page came back for ever),
Referer / Origin, a Chrome User-Agent (Choices `user_agent`), no path limit
(`tools/tests/netsurf/httptest.sh` checks it on the PC). **Kernel / Circle**: the socket slot
race of concurrent connects (the "several connects fail" bug) fixed, a DNS cache, 32 KB recv
gather; Circle's DNS polled (it slept 1 s per lookup), TCP window 64 KB (docs/05 §18-19). To try
on the Pi: page load times (kotonviolins, kotonstudio), hovers, scrolling, m.facebook.com's
cookie consent. Found on the PC and not fixed: a full rebox lays kotonstudio's hero button out
3 px lower than the first layout (a jump when a script changes the DOM); a scroll can leave a
100 px band blank now and then (a race, seen once in a capture before these changes).

**Facebook, the user's next goal ("afficher facebook et que ça soit confortable")** -- the gaps
(audit 2026-09-30), in order: (1) done: cookies, UA, Referer, long URLs; (2) certificate checks
(`MBEDTLS_SSL_VERIFY_NONE` in `onyx_tls.hpp`: `SD:/res/ca-bundle` is on the card, needs a clock
for expiry) -- before typing a password; (3) `history.pushState` / `replaceState` changing the
URL + `popstate` (dom.js only stores the state); (4) an `Intl` subset (QuickJS-ng is built
without it); (5) inline SVG and `.svg` images (libsvgtiny is not vendored: Facebook's icons);
(6) incremental relayout for script DOM changes (today a full rebox 10 ms after each script
turn: React updates constantly) -- the hover restyle's machinery (`onyx_hover.c`) is the start:
restyle the changed subtree, relayout only when a layout property changed; (7) `position: fixed`
pinned to the viewport, `opacity`, `mask-image`; (8) WebSocket (chat) and brotli (cheap: the
decoder is linked). `CSS.supports` answers true for everything: make it honest.

**Open bug (2026-09-30, start here):** on the Pi, kotonviolins' header line turned **opaque
brown** (90, 62, 43) where it was light grey (225, 219, 211) -- seen after the back-buffer build
was deployed (the capture just before it, CSS `:hover` build, was right). The rule:
`.site-header { position: sticky; top: 0; z-index: 20; backdrop-filter: blur(12px);
background: rgba(247, 244, 238, 0.82); border-bottom: 1px solid var(--line) }` with
`--line: rgba(90, 62, 43, 0.14)` -- a translucent colour drawn again and again over itself
(no background repainted under it) tends to the opaque colour: suspect a redraw of the sticky
header's layer (`html_redraw_layer_z` / the sticky code in `redraw.c`) painting over pixels
already there, which the back buffer now keeps (the canvas was maybe cleared before). Not
reproduced on the PC bench (`pages/alpha-hover.html`: a sticky translucent header, repeated
hovers -- stays right). To do: the Pi's `SD:/apps/netsurf.app/main.old` is the build before
threads (compare); make the bench page closer (backdrop-filter, scrolled, a web font arriving
late); log the redraw rectangles; check `onyx_paint.c`'s effects (they read the buffer:
`nsfb_get_buffer`). This is NOT `opacity` (unsupported, a separate item).

### Where the code is
- CSS: `third_party/libcss`. A new property touches `src/parse/propstrings.*`,
  `src/parse/properties/properties.gen` (+ its parser), `src/bytecode/opcodes.h`,
  `include/libcss/properties.h`, `src/select/select_config.py` + `select_generator.py` (the
  computed style's layout), `src/select/dispatch.c`, `src/select/properties/<name>.c` (its
  cascade) and `src/select/computed.c` -- the ones Onyx added are parsed in
  `src/parse/properties/onyx_*.c` and cascaded in `src/select/properties/onyx_css3.c`;
  `make -f tools/tests/netsurf/host.mk libcss-test` must still pass.
- Layout `content/handlers/html/layout*.c` (`layout_grid.c`); painting `redraw.c` (+ the
  framebuffer's `frontends/framebuffer/onyx_paint.c`; the layers: `onyx_layer_*`,
  `html_redraw_layer_z`); fonts `frontends/framebuffer/font_freetype.c`, web fonts
  `html/onyx_webfont.c`.
- JavaScript: `content/handlers/javascript/quickjs/qjs.c` -- a native is an `n_*` function
  plus a line in the `qjs_natives` table, called from dom.js as `N.name(...)`;
  `quickjs/dom.js` -- events, collections, Node, the selector engine, Element / HTMLElement,
  the element classes (`TAGS`), Document, the window's objects, `browserDispatch` (the
  browser's events). Re-layout: `html.c` (`html_script_dom_changed`, `html_rebox`,
  `html_script_layout_now`); events: `html_script_event` (html.c), called from
  `interaction.c` (mouse, keys), `box_textarea.c` (typing, Enter), `form.c` (a select's menu).

### The PC bench (`tools/tests/netsurf/`, no Pi needed)
- `sh tools/tests/netsurf/getsites.sh` -- copies of the two sites in `/tmp/nssites` (the
  bench's NetSurf has no https): `kotonviolins.com/index.html`, `kotonstudio.com/fr/index.html`.
- `sh tools/tests/netsurf/shot.sh <file|url> <png> [WxH] [waits]` builds (host.mk, OUT
  `/tmp/nsbench`) and screenshots a page: portrait `700x1200` (a 618 px page), landscape
  `1600x1000` (1262 px). `chrome.sh <file> <png> <w> <h>` draws it in Chromium. For Windows'
  look, give it `FONTCONFIG_FILE=` a fonts.conf whose `<dir>` holds Georgia and Segoe UI
  (Microsoft's fonts: never in the repo); without them Chromium uses the same stand-ins as
  NetSurf (Liberation, Selawik, Gelasio).
- `sh tools/tests/netsurf/jstest.sh` -- the JavaScript regression test (26 DOM checks, 12
  event checks through simulated clicks and keys, a runaway recursion, 19 fetch / XHR checks,
  the hover events, CSS `:hover`, `localStorage` kept over two runs): run it after any change
  to the JS, the events, the layout or the painting order.
- **On this PC (WSL)**: WSL's `/tmp` is wiped when the distribution stops (the bench was built
  again from nothing each time): `export OUT=$HOME/nsbench` first. WSL's python has no numpy /
  PIL: convert a dump with Windows' python (`python tools/tests/desktop_sim/shot.py f.elsm f.png`,
  the `.elsm` copied to a Windows folder). Run NetSurf itself for a custom script:
  `SIM_SCREEN=800x600 SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS=file://... SIM="wait;...;dump f.elsm;exit"
  $HOME/nsbench/build/netsurf`.
- The simulator's script (`SIM=` in run.sh / jstest.sh): `wait`, `move x y`, `down x y`,
  `up x y` (a `move` first), `wheel x y d` (negative: down), `key k` (a character or a code:
  13 Enter, 27 Esc, 276 F5), `dump f.elsm` (`python3 tools/tests/desktop_sim/shot.py f.elsm
  f.png`), `exit`. Coordinates are the window's client area: the page's y + 40 (the toolbar).
  kotonviolins' hamburger at 700x1200: `(578, 82)`.
- `NS_JSDEBUG=1`: the scripts' errors and `console.log` on stderr. `NS_BOXDUMP=<file>` then
  F5 (`key 276`): the box tree (positions, sizes, styles) written to the file.

### Building for the Pi
- **On this PC (2026-09-30)**: the Arm GNU toolchain 14.2 in WSL builds it all (see the memory /
  docs/03; GCC 14's new errors are kept warnings in `netsurf-app.mk`). From the repo, in WSL, with
  the toolchain on the PATH: `make -C user/nsfb NSFB=$PWD/third_party/libnsfb` (only after
  `onyx_surface.c` changes: the default NSFB path is wrong), then
  `make -f user/netsurf/netsurf-app.mk OUT=$HOME/nsbuild -j8 link` and `... OUT=$HOME/nsbuild
  stage` (OUT outside `/tmp`, as above). The rules do not track headers, but `qjs_dom_js.h` is
  made again from `dom.js` (check with `grep -c <a new name> $HOME/nsbuild/qjsgen/qjs_dom_js.h`).
- NetSurf and its libraries (the cloud container): GCC 10.3 (`gcc-arm-10.3-2021.07`, aarch64-none-elf; the cloud
  container had it in `/home/user/toolchain`). `make -C user/netsurf` (the `.a` are
  committed; after a libcss / libdom header change delete their `.o`), then
  `rm -rf /tmp/nsbuild && make -f user/netsurf/netsurf-app.mk -j$(nproc) link` and
  `make -f user/netsurf/netsurf-app.mk stage` (the ELF, its app.txt, `SD:/res`).
- The kernel (only when `kernel/` changes): the Arm GNU toolchain 13.3.rel1
  (`https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf.tar.xz`;
  GCC 10 cannot build `sys/v3d.cpp`), `git submodule update --init circle` then
  `git -C circle submodule update --init addon/wlan/hostap`, Circle built as docs/03 §2 says,
  `make -C kernel kernel8-rpi4.img`, copied to `sdcard/`. NetSurf's `app.txt` (`stack = 8M`)
  needs this kernel (the default stack is 256 KB; QuickJS may use 4 MB).

### Trying it on the Pi (from this PC)
- The Pi answers at 192.168.0.10 (telnet 23, VNC 5900; telnetd serves several sessions now).
  Deploy: an FTP server on the PC (`pyftpdlib`, port 2121, the repo's `sdcard/` as its root,
  allowing only the Pi), then in a telnet session `cat FTP:192.168.0.9:2121/apps/jet.app/main
  > SD:/apps/jet.app/main.new` (7 MB: wait ~45 s before the next command, or the transfer is
  cut), `wc -c < SD:/apps/jet.app/main.new` (the size), `cp` it over `main`. No reboot
  needed for an app.
- **Its messages**: an app's stdout without a terminal goes to the kernel log as `app:` lines --
  NetSurf's `ONYX-FETCH FAIL <url> err=...`, `ONYX-CSS-ERR ...`, `ONYX-HLC type=... url=...`.
  One telnet session runs `kmsg` (it streams for ever: stop it with Ctrl-C, `\x03`; never pipe
  it into `grep`: that never ends and freezes the session), a second one runs
  `run jet https://kotonviolins.com` (`run` passes the URL; a Jet Browser already open is kept:
  close it first for a new build). Screenshots: `python -m vncdotool.command -s 192.168.0.10
  capture x.png`; at 1024 x 768 (the user's setting now) `move x y` lands where asked (hover
  tests: compare pixels before / after).

### Next, with where to start
0. **The open bug above** (the opaque header line), then commit nothing more on top of it until
   it is understood.
1. **Hover without the whole page** -- CSS `:hover` makes the document's boxes again
   (`html_script_dom_changed` -> a full rebox + layout + redraw) at each change of the node
   under the pointer. As the big engines do: (a) restyle only the nodes whose hover state
   changed (the old and new hover chains: their subtrees) -- `box_get_style` again for their
   boxes, the text / anonymous boxes that point at the old style updated too; (b) compare the old
   and new computed styles (libcss interns them: the same pointer = no change) and if only paint
   properties differ (colours, backgrounds, border colours, outline, shadows, visibility)
   swap the styles and redraw the boxes' rectangles only (`html__redraw_a_box`), no layout;
   else the rebox as now. Same for `:active` / `:focus` (`node_is_active` / `node_is_focus`
   in `css/select.c` answer no).
2. **`opacity`** -- kotonstudio's scroll reveal (`.feature-card { opacity: 0 }` until the
   IntersectionObserver adds `.is-visible`: the JS part works) and the hamburger's middle bar.
   `redraw.c`: paint the box's subtree into an off-screen bitmap and blend it with its alpha
   (a new plotter operation, framebuffer side in `onyx_paint.c`); a cheaper first step for a
   subtree with no overlap: multiply its colours' and images' alpha.
3. **Cookies in `user/netsurf/onyx_fetch.c`** (logins): send `urldb_get_cookie`, give each
   `Set-Cookie` to NetSurf's cookie handling (the fetcher leaves `Set-Cookie` out of the
   headers it hands the core today). Mind the worker threads: urldb is not thread-safe -- read
   the cookie on the UI thread (at `fetch_onyx_setup` / job start) and hand the `Set-Cookie`
   lines back with the response.
4. **The fetch / XHR gaps** (docs/06 §7): streams (`Response.body`), synchronous XHR (runs
   async), multipart bodies (FormData with files), `responseXML`, CORS checks (none: every
   origin answers), cookies on script requests (with 3).
5. **The UI thread's wait**: `onyx_input` (`user/nsfb/onyx_surface.c`) still sleeps
   `kapi_msleep(<=20)`; `kapi_pump_wait` would wake it at once on an event (and on a post, if
   the fetch workers `kapi_post` their completion instead of the 10 ms poll).
6. **Media queries on a resize** (responsive): a new window size -> `html_rebox` (the boxes'
   styles are selected again) -- check which width the selection's media sees.
7. **CSS transitions / animations, SVG (libsvgtiny, not vendored), `canvas`** -- the larger
   gaps to Ladybird / Chrome.
8. Form controls outside any form are made again at each rebox (the old one leaks):
   `html_forms_get_control_for_node` only searches the forms -- keep them in a list per
   document. NetSurf's `default.css` gives inputs and buttons `margin: 1px` (Chrome: 0):
   compare before changing.

### Pitfalls met (keep them in mind)
- An app's stack must stay in kernel memory: `Yield` activates the next task's address space
  before it leaves the old stack. A bigger stack only through `app.txt` (`stack =`).
- libdom caches an element's classes: now updated by `setAttribute` (element.c); a path that
  changes an `Attr` directly would still miss it.
- hubbub's fragment parser (innerHTML) sets its document's quirks mode: qjs.c saves and
  restores it.
- `textarea_set_text` reports `TEXTAREA_MSG_TEXT_MODIFIED`: the control's `syncing` flag keeps
  a script's value from becoming an `input` event (a loop otherwise).
- Events are dispatched with the boxes held (`script_hold`): their callers keep box pointers
  (`mouse_action_state`). A changed DOM is laid out 10 ms later, or at once when a script asks
  for a rectangle outside an event.
- A rebox takes the old boxes' objects over by URL (`html_fetch_object`); objects that arrive
  after the page is done cause a reformat (object.c).
- **The connects one at a time** (`onyx_connect` in `onyx_fetch.c`): several threads connecting
  at once (DNS + TCP on the network core) all failed ("Connection failed"), and a page was laid
  out without its style sheet -- a failed style sheet does not hold the layout back.
- The fetch threads touch plain copies only (the URL as a string, the response bytes); every
  NetSurf call (nsurl, llcache, `fetch_send_callback`) stays on the UI thread.
- Script requests go through `llcache_handle_retrieve_ex` with `LLCACHE_RETRIEVE_FORCE_FETCH`
  (a cached object would not be fetched again with the request's headers); the Onyx fetcher
  leaves the cache-control response headers out (the cache behaves as when only Content-Type
  came) and drops `If-None-Match` / `If-Modified-Since` (a 304 would need FETCH_NOTMODIFIED).

## Koton, the studio -- a DAW (2026-09-29: implemented, not yet run on the Pi)

Koton Studio (the user's C# DAW, `github.com/stephaneweg/MusicTracker`) made again for Onyx, no
score view: **`docs/daw/README.md`** (the study, the plan, the user's decisions in §8, **where it
stands and what to test on the Pi in §9**). Code: `user/Apps/koton` (engine/, synth/, plug/, ui/,
main.cpp), `user/kplug*.h` + `user/Apps/kp_*` (the plugins), `user/bin/llm.cpp` (the AI's HTTPS
helper), wtk's text face (`user/ft/wtkface.h`) and studio widgets. Docs: docs/04 *Koton, the
studio*, docs/03 *A large app: Koton*, *Koton's plugins*, the `/bin/llm` section. Tests:
`sh tools/tests/koton/{synth,engine,ai,plug,plug_host}_run.sh`; the app on the PC:
`sh tools/tests/desktop_sim/shots.sh koton`. The card carries the GeneralUser GS SoundFont
(`sdcard/koton/soundfonts`, licence beside it).

**Koton for Windows (2026-09-30):** `pc/dist/Koton` (`sh pc/Koton/build.sh`, MinGW-w64): the Onyx
sources unchanged over `pc/Koton/winkapi.cpp`, the kernel's table on Win32 -- docs/03 *Koton for
Windows*. Checked under Wine (no sound card there: the silent drain); to try on a real Windows: the
sound (WASAPI), a USB MIDI keyboard, the window's resize by hand.

**User manual (2026-09-30):** `sdcard/manuals/koton/Koton.md` + `Koton.fr.md` and their PDFs
(`python tools/manuals/build_manuals.py <the .md>`), 25 pictures in `images/` by
`sh tools/manuals/koton_shots.sh` (clicks at fixed places on the demo song: move them if the layout
changes) -- docs/03 *Manuals*. Keep it in step when Koton changes.

**Performance** (why there is no audible latency, why the UI stays fluid, the rules not to
break): **`docs/daw/PERFORMANCE.md`**. Its TODO (none started): the arrangement's playhead drawn
over a cached canvas instead of the whole view redrawn at each tick while playing, redraw only
when the playhead moved a pixel, a *Low latency* setting (128 × 2 in the kernel, a 512-frame ring
≈ 20 ms) for live MIDI.

## End-user apps roadmap (decided with the user, 2026-09-30; none started)

Every new app: FreeType text through wtk's face, polished, its catalog entry in docs/04 and a
`shots.sh` scenario. In the user's priority order:

**Priority 1**
- **Music library** (audio player as a polished library app, in the way of iTunes / Rhythmbox):
  MP3, OGG, FLAC, WAV **and MIDI** (`.mid` played through MeltySynth + a SoundFont -- the synth
  is in `user/Apps/koton/synth/`, to share rather than copy); artists / albums / playlists,
  tags and cover art, a now-playing view, file associations.
- **Mail client**, as user-friendly as possible: IMAP / SMTP over TLS, an account wizard
  (well-known providers pre-filled), threads, attachments, drafts. **Contacts** = a Cardfile
  form: the mail client creates the `.card` structure, reads / writes it (address completion,
  "add sender"), and the file opens in Cardfile too.
- **PDF viewer** (MuPDF-like, FreeType) + **PDF export** in Writer and the Spreadsheet.
- **Screenshot** tool (screen / window / area; Print Screen key).
- **Clock**: alarms, timer, stopwatch, world clocks (notifications through notifyd).

**Priority 2**
- **Localisation** screen: the Keyboard applet becomes *Region & Keyboard* -- the country /
  time zone next to the layout (the time itself stays set by NTP automatically).
- **About / System**: a fuller successor to memmon (version, kernel, CPU, temperature, RAM,
  uptime, network, processes).
- **Presentations** (as Impress), to complete Writer / Sheet / Cardfile.
- **Quick notes** with a desktop widget that can be shown or hidden.
- (Storage applet: not for now. Updates: part of the future package manager / app store.)

**Priority 3–4**: **video player**; the **app store / package manager** (see IDEAS.md below).

**Priority 5**: a global **key vault** (encrypted secrets store) with seamless integration in
the apps that hold secrets (Wi-Fi, Lisa / Groq keys, mail passwords, Courier, ftpfs...).

## Other open items

- **gcemu, The Wind Waker: Link's eyes are missing** (the user, on the TV, 2026-09-28; to look at
  after the GUI work). Leads: the game draws the eyes and eyebrows after the hair with their own
  depth compare so they show through it, and uses the EFB's alpha around them -- a draw left out
  (`GCV3D_SKIPLOG` / F12's third line: the reason), the depth function or its precision (the V3D's
  against the EFB's 24 bits: z-fighting on the face), or the destination alpha (`dstAlpha`, the
  EFB's RGBA6 / RGB8 format) not kept. To start: F9 on a close view of Link's face, then
  `gcv3d --replay` (docs/03) -- the recorder's batches for the eyes, their state and textures.
- VNC (`vncd`): the image froze while the sound went on, OnyxRemote (rdpd) kept working —
  not investigated yet.
- Ideas (IDEAS.md): an ISO9660 driver + `mount` of ISO / disk / partition images as volumes
  `VD0:`, `VD1:`…; an mstsc-compatible RDP server (~3000–4500 lines, TLS without NLA); the AX
  microcode's audio for the GameCube (Dolphin's `AXUCode`); NintendoEMU keyboard remapping;
  a task scheduler (as Windows': at boot, every x minutes / hours / days, on given weekdays;
  runs any executable -- an app, `bin/`, a `.bas`); packages as archives mirroring the card's
  root (`apps/calc.app/…`, `bin/…`, even `kernel8-rpi4.img`: one format for apps and system
  updates) with a manifest each, in a public repository with an index; a package manager (an
  applet of the Settings app: new apps, available updates, manual / automatic per app) and an
  update daemon run by the scheduler (updates the "automatic" ones) or once from the applet (a
  checklist of what to update); a `pkg` command (`add` / `delete` / `update [-a]` / `upgrade` /
  `list [-a]`). Details in IDEAS.md.
