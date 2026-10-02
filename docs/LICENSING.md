# Licensing — what Onyx contains, and under which licence it can be distributed

> **Status (2026-10-01): an audit, not legal advice.** Asked by the user before the Archiver takes
> a RAR reader: what licence constraints the whole project carries, and under which licence it can
> be distributed. Today the repository has **no `LICENSE` file at its root** — so the Onyx code is
> "all rights reserved", while the card (`sdcard/`) and `pc/dist/` already ship GPL binaries
> (the kernel with Circle, Jet with NetSurf) without the GPL's text. Both need fixing.

## 1. The verdict

| Part | Must be distributed under | Why |
|---|---|---|
| **The kernel image** (`kernel8-rpi4.img`) | **GPL-3.0-or-later** | Circle (our fork, statically linked) is GPL-3.0-or-later. |
| **Jet Browser** (`jet.app`, `pc/dist/Jet`) | **GPL-2.0** (only) | NetSurf is **GPL-2.0-only** ("version 2 of the License", no "or later") — incompatible with GPLv3. Its Onyx code must stay GPLv2-compatible (GPL-2.0-or-later or permissive), mbedTLS taken under its GPL-2.0-or-later option (not Apache-2.0), FreeType under its GPLv2 option. |
| **Doom** (`doom.app`) | GPL-2.0-or-later (→ GPLv3 fine) | doomgeneric: "version 2 … or any later version". |
| **PDF Viewer** (`pdf.app`) | **AGPL-3.0** | MuPDF (and its jbig2dec) is AGPL-3.0 (or Artifex's paid licence). AGPL-3.0 and GPL-3.0 combine (GPLv3 §13): the app is AGPL, the rest of Onyx unchanged; its source is the repository's. Decided with the user (2026-10-01). |
| **Every other app, the tools** | Your choice | Only permissive libraries (MIT, BSD, zlib, ISC, public domain, FTL, IJG) and newlib (BSD-like). |
| **Data** (fonts, sound font, Freedoom, CLDR) | Their own licences, unchanged | OFL / Bitstream Vera, GeneralUser GS licence, BSD-3, Unicode v3 — fine to ship beside GPL code. |
| **Firmware blobs** | Their own licences, unchanged | Raspberry Pi boot firmware (Broadcom, binary redistribution for Raspberry Pi use), the Wi-Fi firmware (Cypress / Synaptics, binary). Not GPL, not ours: "mere aggregation". |

**Recommendation:** distribute Onyx under **GPL-3.0-or-later** (forced anyway for the kernel by
Circle), with **Jet Browser under GPL-2.0** and the **PDF Viewer under AGPL-3.0** as documented
exceptions. A purely permissive licence (MIT) for the whole is **not possible**: the kernel is GPLv3
because of Circle.

**Decided by the user (2026-10-01): every piece of Onyx's own software that can be is under the MIT
licence.** That is every app and tool that links only permissive libraries (the table's *Every other
app*), and our own code everywhere — MIT is compatible with the GPL and the AGPL, so the kernel's,
Jet's, Doom's and the PDF Viewer's Onyx files stay MIT as files, while the program built from them
is distributed under its GPL / AGPL. New code is written MIT (e.g. `user/pdf/pdfwrite.h`, the PDF
export of Writer and the Spreadsheet, carries the MIT notice); a library that would force another
licence on an app is chosen only with the user (as MuPDF was).

### Distributing the whole: an aggregate, as Linux distributions do

Nothing forbids shipping everything **together**, on one card or in one download: GPLv2 §2 and
GPLv3 §5 call it an *aggregate* ("mere aggregation" of separate programs on a storage medium) and
let each program keep its licence. What is impossible is to **relicense** others' code: one licence
can only cover the parts one owns. Linux distributions work that way: the kernel is GPL-2.0-only
(with its syscall note: user programs are not covered), the userland is GPL, LGPL, MIT, BSD,
Apache, MPL… and even proprietary, the firmware has its own licences (`linux-firmware`'s `WHENCE`);
each package lists its own (Debian's `/usr/share/doc/<package>/copyright`, Fedora's SPDX `License:`
tag). Only what is **combined into one program** must have compatible licences — for Onyx, the
kernel with Circle, Jet with NetSurf. So: *"Onyx is distributed under GPL-3.0-or-later; the
components listed in LICENSING.md keep their own licences"* is the usual, correct statement.

## 2. The grey zone: apps and the kernel

GPLv3 (Circle) and GPLv2-only (NetSurf) cannot be combined into **one** work. Onyx keeps them in
**separate programs**: the kernel, and `jet.app/main`, a separate ELF loaded into its own address
space (its own `TTBR0` / ASID), which calls the kernel only through the `kapi` table at a fixed
address — never linked against kernel symbols. That is an operating system's call interface, the
same seam as Linux's system calls, so the two are an aggregate, not one work.

Two weaknesses, worth fixing:

1. The apps run at **EL1** in the kernel's address space (identity region mapped RW) and call the
   `kapi` as plain indirect calls, not traps. The FSF reads "function calls in a shared address
   space" as one program. The interface is still a stable, documented ABI, but **say so
   explicitly**: add to `LICENSE` a **kapi exception**, like Linux's syscall note — *"Programs that
   use the Onyx kernel only through the kapi table (`user/kapi.h`) are not derived works of the
   kernel."* You can grant it for your own kernel code; Circle's author grants nothing, but your
   code is the only part the apps call. The move to EL0 (docs/EL0-PROTECTED-MODE.md) would make the
   separation plain.
2. `user/kapi.h` (included by every app, Jet too) must be under a licence GPLv2 can take: put it
   (and `user/libc`, `crt0*.S`, `user.ld`) under **MIT** or GPL-2.0-or-later.

## 3. The inventory

| Component | Where | Licence | Obligation |
|---|---|---|---|
| Circle (fork, branch `onyx`) | `circle/` (submodule), the kernel | GPL-3.0-or-later | The kernel's source (ours + the fork) available; the GPL's text |
| NetSurf | `third_party/netsurf`, Jet | **GPL-2.0-only** | Jet under GPLv2, source available (also for `pc/dist/Jet/Jet.exe`) |
| talloc (Samba) | `third_party/netsurf/utils/talloc.c`, Jet | LGPL-2.1-or-later | Fine in a GPL program; in a permissive Jet it would have to go (or be relinkable) |
| libcss, libdom, libhubbub, libnsbmp, libnsfb, libnsgif, libnslog, libnsutils, libparserutils, libwapcaplet, nsgenbind | `third_party/` | MIT | Keep the notices |
| doomgeneric | `third_party/doomgeneric` | GPL-2.0-or-later | Source available |
| mbedTLS 3.6.3 | `third_party/mbedtls-3.6.3` | Apache-2.0 **or** GPL-2.0-or-later | Jet: take the GPL option |
| FreeType 2.14.3 | `third_party/freetype-2.14.3` | FTL **or** GPL-2.0-or-later | FTL: a credit in the docs ("Portions of this software are copyright © The FreeType Project") |
| zlib, libpng | `third_party/` | zlib / libpng licence | — |
| libjpeg 9f | `third_party/jpeg-9f` | IJG | Docs: "This software is based in part on the work of the Independent JPEG Group" |
| libwebp, zstd, brotli | `third_party/` | BSD-3 / BSD-3 (or GPLv2) / MIT | Keep the notices |
| nghttp2, quickjs-ng, wasm3, plutovg, plutosvg, webref-css, expat (Jet Browser's XML parser: `third_party/expat-2.7.1`, its `COPYING`) | `third_party/` | MIT | Keep the notices |
| CLDR 48 | `third_party/cldr-48` | Unicode License v3 | Keep the notice |
| stb_image | `user/img` | Public domain / MIT | — |
| simplewebp | `user/img` | BSD | Keep the notice |
| MuPDF 1.28.5 (fitz, pdf; the URW base-14 fonts) | `third_party/mupdf-1.28.5`, the PDF Viewer | **AGPL-3.0** (Artifex) | The PDF Viewer under AGPL-3.0, its source available (the repository); keep `COPYING` |
| jbig2dec | `third_party/mupdf-1.28.5/thirdparty/jbig2dec`, the PDF Viewer | AGPL-3.0 | As MuPDF |
| OpenJPEG | `third_party/mupdf-1.28.5/thirdparty/openjpeg`, the PDF Viewer | BSD-2 | Keep its `LICENSE` |
| pdfwrite (Onyx) | `user/pdf/pdfwrite.h`, Writer, the Spreadsheet | **MIT** (ours) | — |
| MeltySynth (C++ port) | `user/Apps/koton/synth` (Koton, Media Player) | MIT | Keep the notice |
| minimp3 | `third_party/minimp3`, Media Player, Jet Browser (`user/av/av_mp3.c`) | CC0 (public domain) | — |
| libvpx 1.15.2 (VP8 / VP9 decoders) | `third_party/libvpx-1.15.2`, Jet Browser (`user/av/av_vpx.c`) | BSD-3-Clause + Google's VP8/VP9 patent grant (`PATENTS`) | Keep `LICENSE` and `PATENTS` |
| dav1d 1.5.1 (AV1 decoder) | `third_party/dav1d-1.5.1`, Jet Browser (`user/av/av_dav1d.c`) | BSD-2-Clause | Keep `COPYING` |
| libopus 1.5.2 (Opus) | `third_party/opus-1.5.2`, Jet Browser (`user/av/av_opus.c`) | BSD-3-Clause (royalty-free patent licences listed in `COPYING`) | Keep `COPYING` |
| stb_vorbis | `third_party/stb_vorbis`, Media Player | Public domain / MIT | — |
| dr_flac, dr_wav | `third_party/dr_libs`, Media Player | Public domain / MIT-0 | — |
| ares (PI DMA, ported) | `user/n64/n64_bus.cpp` | ISC | The notice is in the file — keep it |
| Mesa (V3D / QPU headers) | `tools/qpu/mesa` | MIT | Keep the notices |
| React 18.3.1, React DOM (their production builds, test pages only: not on the card) | `tools/tests/netsurf/pages/react` | MIT | Keep the notice (its `LICENSE` is there) |
| newlib (libc of the C/C++ apps) | the toolchain | BSD-like (several) | Ship newlib's `COPYING.NEWLIB` notices with the binaries |
| libgcc / libstdc++ | the toolchain | GPLv3 + **GCC Runtime Library Exception** | None for our binaries |
| DejaVu | `third_party/dejavu-*`, `sdcard/res/fonts` | Bitstream Vera + public domain | Keep the licence; fonts not sold alone |
| Liberation, Gelasio, Selawik | `sdcard/res/fonts`, `third_party/fonts` | SIL OFL 1.1 | Keep the licences (they are there) |
| GeneralUser GS 2.0.3 | `sdcard/res/soundfonts` (the package `generaluser-gs`) | Its own, free (also commercial) | Keep the licence (it is there) |
| Freedoom | `sdcard/doom` | BSD-3 | Keep `FREEDOOM-COPYING.txt` (it is there) |
| Raspberry Pi firmware (`start4.elf`, `fixup4.dat`, the `.dtb`) | `sdcard/` | Broadcom's redistributable binary licence; the `.dtb` from Linux's device trees (GPL-2.0, most also MIT) | **Add `LICENCE.broadcom`** to the card |
| `armstub8-rpi4.bin` | `sdcard/` | BSD-3 (raspberrypi/tools) | Add its notice |
| Wi-Fi firmware (`brcmfmac4345*`) | `sdcard/firmware` | Cypress / Synaptics binary licence | **Add its licence file** (`LICENCE.cypress`) |
| SuperTuxKart (not vendored yet) | outside the repo | GPL-3.0-or-later, its art CC-BY-SA / GPL | Fine under GPLv3; keep the credits when vendored |

### Emulators — clean-room, keep them that way

The NES, SNES, Game Boy, GBA, N64 and GameCube emulators are written for Onyx. Their comments
cite the behaviour documented by others (FCEUX's palette, bsnes, Dolphin's DSP HLE and its
microcode CRCs, mupen64plus-rsp-hle's audio — GPL-2.0 — "not copied") — knowledge, not code:
fine. The one ported code (ares, ISC) carries its notice. Keep it so: **never paste code from
Dolphin / mupen64plus / mGBA (GPL) or Snes9x (non-commercial: incompatible with the GPL)**. No
BIOS, IPL or ROM is shipped (`sdcard/roms/README.txt` says so) — keep it so.

### Content to look at

- **`sdcard/music/fms`** — 59 FM Song tunes, many of them **covers of copyrighted music**
  (Airwolf, Final Fantasy IX, Donkey Kong Country, Sonic, Lemmings, Jurassic Park, Tubular Bells,
  Mortal Kombat, Ocarina of Time…). A transcription is a derived work of the composition: not ours to
  license. Keep only your own tunes in a public distribution (or a few clearly public-domain ones).
- The wallpapers, icons and manuals — assumed yours; say so in `LICENSE` / the credits.

## 4. For the Archiver's formats

| Library | Licence | In a GPL app |
|---|---|---|
| zlib (ZIP's Deflate) | zlib | Yes |
| LZMA SDK (7z, xz) | Public domain | Yes |
| zstd | BSD-3 | Yes |
| **libarchive** (its `rar` and `rar5` readers) | **BSD-2** | **Yes — the choice** |
| unRAR (RARLAB's source) | Its own: free to use, but forbids re-creating the RAR compressor; the FSF calls it non-free | **No** — not compatible with the GPL |
| 7-Zip (the program's own code) | LGPL-2.1 + the unRAR restriction for its RAR part | Avoid; take the LZMA SDK instead |

So the Archiver reads RAR through libarchive's readers (BSD): RAR 4 and RAR 5, solid archives;
**not** encrypted RAR (libarchive does not decrypt it), never writing RAR (no free encoder, and its
licence forbids it).

## 5. Jet without NetSurf — what is still NetSurf's

Measured 2026-10-01: each file of the NetSurf tree that Jet compiles, compared line by line with
upstream NetSurf (`github.com/netsurf-browser/netsurf`, master).

| | Files | Lines | Still identical to upstream |
|---|---|---|---|
| Upstream files compiled | 149 | 123 000 | **~112 000 (91 %)** |
| Files written for Onyx in the NetSurf tree (`qjs*.c`, `onyx_*.c`, `layout_grid.c`) | 22 | 28 000 | — |
| Onyx glue (`user/netsurf/*.c, *.cpp`) | | 7 600 | — |

So Jet is not a NetSurf skeleton: the engine's skeleton *and most of its organs* are still
NetSurf's. What Onyx changed most is in the MIT libraries (libcss, libdom, hubbub: no licence
issue) and in Onyx's own files. What would have to be replaced, by block (identical lines):

| Block | Lines (identical) | What it is | Effort |
|---|---|---|---|
| `desktop/` | 30 100 (97 %) | `browser_window.c`, frames, history, `textarea.c` (form fields), scrollbars, selection, search; plus parts Jet does not use (treeview, hotlist, cookie manager, global history, page info, save as PDF / text / complete, print) — dead weight to drop first | large (the used half) |
| `content/` core | 16 100 (98 %) | `llcache` / `hlcache` (the caches), `urldb` (cookies, visited), `fetch.c`, `fs_backing_store`, `content.c`, mime sniffing | medium |
| `content/handlers/html` | 34 000 (80 %) | the heart: `box_construct`, `layout.c` (block, inline, tables), `redraw.c`, `interaction.c`, forms, `html.c` (the document's load, the scripts), tables, imagemaps | **the largest and hardest** |
| `utils/` | 14 000 (97 %) | `nsurl`, hashtables, options, messages, utf8, time, idna, punycode, log, talloc (LGPL) | small to medium |
| `content/handlers/css` | 7 000 (89 %) | the libcss glue: `select.c` (the cascade's callbacks), hints | medium |
| `content/fetchers` | 6 600 (96 %) | about:, file:, data:, resource: (http is Onyx's) | small |
| framebuffer frontend + fbtk | 9 700 (77 %) | `gui.c`, `framebuffer.c`, `font_freetype.c`, the fbtk toolkit — replaceable by a wtk frontend | small to medium |
| image, text handlers | 5 200 (99 %) | thin wrappers over libnsgif / libpng / libjpeg / libwebp, plain text | small |
| `resources/` | — | `default.css`, Messages, `credits.html`, `licence.html`, icons (GPL content) | small (the UA stylesheet can come from the HTML Standard, CC-BY 4.0) |

The Onyx files in the NetSurf tree carry NetSurf's GPL header but are the user's own: their
author can relicense them (MIT) — as long as they were not translated from NetSurf code.

A rewrite must be one, not a translation: written from the specifications (HTML, CSS 2.1 / 3,
Fetch, URL), with its own structures, NetSurf's files not open beside it — otherwise the result
stays a derived work under GPLv2. The permissive engines that could replace NetSurf whole —
litehtml (BSD-3, no JavaScript, simpler layout), Ladybird's LibWeb (BSD-2, far too large for
Onyx), Servo / Blitz (Rust) — would lose much of what Jet does today.

## 6. To do (when the user decides)

1. A root **`LICENSE`**: GPL-3.0-or-later's text, the kapi exception (§2), the list of the parts
   under other licences (this file's §3), Jet's GPL-2.0.
2. `COPYING.GPL2` beside Jet (`sdcard/apps/jet.app/`, `pc/dist/Jet/`) — NetSurf's own
   `res/licence.html` is already shown in the browser.
3. A **`CREDITS`** / *About Onyx* listing every third-party component with its notice (FreeType's and
   IJG's credit lines are required).
4. The firmware licences on the card (`LICENCE.broadcom`, the Wi-Fi's).
5. SPDX headers in our own files (`// SPDX-License-Identifier: MIT`: the user's decision above),
   `user/kapi.h` and the app runtime first; a `LICENSE` (MIT) beside each app built only from them.
6. The FM Song covers out of the public distribution.
