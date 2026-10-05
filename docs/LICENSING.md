# Licensing — what Onyx contains, and under which licence it can be distributed

> **Status (2026-10-01): an audit, not legal advice.** Asked by the user before the Archiver takes
> a RAR reader: what licence constraints the whole project carries, and under which licence it can
> be distributed. Today the repository has **no `LICENSE` file at its root** — so the Onyx code is
> "all rights reserved", while the card (`sdcard/`) and `pc/dist/` already ship GPL binaries
> (the kernel with Circle; then, Jet with NetSurf) without the GPL's text. Both need fixing.
>
> **2026-10-04: Jet is no longer NetSurf.** The NetSurf browser and its libraries left the repository; Jet
> Browser is now the WebKit port (LGPL-2.1+). What this file said of NetSurf's GPL-2.0-only is history.

## 1. The verdict

| Part | Must be distributed under | Why |
|---|---|---|
| **The kernel image** (`kernel8-rpi4.img`) | **GPL-3.0-or-later** | Circle (our fork, statically linked) is GPL-3.0-or-later. |
| **Jet Browser** (`jet.app`) | **LGPL-2.1-or-later** | It is the WebKit port: WebCore and JavaScriptCore are LGPL-2.1+ (the rest of WebKit BSD-2-Clause); statically linked, so its complete corresponding source is published — WebKit's pinned revision, `tools/webkit/patches/`, the ports in `tools/ports/`, `user/Apps/jet/` (MIT, ours) — and it can be relinked (`build-webkit.sh`, `build-web.sh`). mbedTLS taken under Apache-2.0, FreeType under the FTL. |
| **Doom** (`doom.app`) | GPL-2.0-or-later (→ GPLv3 fine) | doomgeneric: "version 2 … or any later version". |
| **Media Player** (`media.app`) | **GPL-2.0-or-later** | It links **FFmpeg** built with `--enable-gpl` (GPL-2.0-or-later): H.264, H.265, AAC, AVI, MPEG-TS... Decided with the user (2026-10-02: "on passera le Media Player en GPL-2"). Its own files stay MIT; its source is the repository's. |
| **PDF Viewer** (`pdf.app`) | **AGPL-3.0** | MuPDF (and its jbig2dec) is AGPL-3.0 (or Artifex's paid licence). AGPL-3.0 and GPL-3.0 combine (GPLv3 §13): the app is AGPL, the rest of Onyx unchanged; its source is the repository's. Decided with the user (2026-10-01). |
| **Photos** (`photos.app`) | **MIT** (ours) | Its EXIF reader, library, editing and slideshow are ours; it links FreeType (FTL), stb_image and simplewebp (public domain, BSD-3), our PNG / JPEG / PDF writers (MIT): all permissive. |
| **Mail** (`mail.app`) | **MIT** (ours) | Its protocols and its HTML renderer are ours (`user/mail/`, MIT); it links mbedTLS (Apache-2.0), FreeType (FTL), stb_image (public domain): all permissive. |
| **Every other app, the tools** | **MIT** (ours; the user's decision, below) | Only permissive libraries (MIT, BSD, zlib, ISC, public domain, FTL, IJG) and newlib (BSD-like). |
| **Data** (fonts, sound font, Freedoom, CLDR) | Their own licences, unchanged | OFL / Bitstream Vera, GeneralUser GS licence, BSD-3, Unicode v3 — fine to ship beside GPL code. |
| **Firmware blobs** | Their own licences, unchanged | Raspberry Pi boot firmware (Broadcom, binary redistribution for Raspberry Pi use), the Wi-Fi firmware (Cypress / Synaptics, binary). Not GPL, not ours: "mere aggregation". |

**Recommendation:** distribute Onyx under **GPL-3.0-or-later** (forced anyway for the kernel by
Circle), with **Jet Browser under LGPL-2.1-or-later** (WebKit), the **Media Player under GPL-2.0-or-later** (FFmpeg) and the **PDF Viewer under AGPL-3.0** as documented
exceptions. A purely permissive licence (MIT) for the whole is **not possible**: the kernel is GPLv3
because of Circle.

**Decided by the user (2026-10-01): every piece of Onyx's own software that can be is under the MIT
licence.** That is every app and tool that links only permissive libraries (the table's *Every other
app*), and our own code everywhere — MIT is compatible with the GPL and the AGPL, so the kernel's,
Jet's, Doom's and the PDF Viewer's Onyx files stay MIT as files, while the program built from them
is distributed under its GPL / AGPL. New code is written MIT (e.g. `user/pdf/pdfwrite.h`, the PDF
export of Letters and the Spreadsheet, carries the MIT notice); a library that would force another
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
kernel with Circle, Jet with WebKit. So: *"Onyx is distributed under GPL-3.0-or-later; the
components listed in LICENSING.md keep their own licences"* is the usual, correct statement.

## 2. The grey zone: apps and the kernel

A kernel under GPLv3 (Circle) and programs under other licences (WebKit's LGPL, FFmpeg's GPL-2.0-or-later,
MuPDF's AGPL) are not **one** work. Onyx keeps them in
**separate programs**: the kernel, and each app — `jet.app/main` for one —, a separate ELF loaded into its own address
space (its own `TTBR0` / ASID), which calls the kernel only through the `kapi` table at a fixed
address — never linked against kernel symbols. That is an operating system's call interface, the
same seam as Linux's system calls, so the two are an aggregate, not one work.

Two weaknesses, worth fixing:

1. **Settled by the move to EL0 (kapi v74, 2026-10-02).** The apps used to run at **EL1** in the
   kernel's address space and call the `kapi` as plain indirect calls — what the FSF reads as
   "function calls in a shared address space". Since v74 every app runs at **EL0**, out of the
   kernel's memory, and each kapi entry is a **system call** (`svc`) through a stub
   (docs/EL0-PROTECTED-MODE.md, docs/02 §6): the same seam as Linux's system calls. Still worth
   saying explicitly in the future `LICENSE`, as Linux's syscall note does — *"Programs that use
   the Onyx kernel only through the kapi table (`user/kapi.h`) are not derived works of the
   kernel."* You can grant it for your own kernel code; Circle's author grants nothing, but your
   code is the only part the apps call.
2. `user/kapi.h` (included by every app) must be under a licence every app's licence can take: put it
   (and `user/libc`, `crt0*.S`, `user.ld`) under **MIT** or GPL-2.0-or-later.

## 3. The inventory

| Component | Where | Licence | Obligation |
|---|---|---|---|
| Circle (fork, branch `onyx`) | `circle/` (submodule), the kernel | GPL-3.0-or-later | The kernel's source (ours + the fork) available; the GPL's text |
| doomgeneric | `third_party/doomgeneric` | GPL-2.0-or-later | Source available |
| FFmpeg 7.1.2 (libavcodec, libavformat, libavutil, libswscale, libswresample; `--enable-gpl`, no external library) | `third_party/ffmpeg-7.1.2`, the Media Player (`user/av/av_ffmpeg.c`, `av_lavf.c`) | **GPL-2.0-or-later** | The Media Player under GPL-2.0-or-later, its source available (the repository: `onyx/build.sh` makes the libraries); keep `COPYING.GPLv2`, `LICENSE.md`. Some formats it decodes are patented in some countries (H.264, H.265, AAC...): FFmpeg's own note in `LICENSE.md` |
| mbedTLS 3.6.3 | `third_party/mbedtls-3.6.3` | Apache-2.0 **or** GPL-2.0-or-later | Taken under Apache-2.0 |
| FreeType 2.14.3 | `third_party/freetype-2.14.3` | FTL **or** GPL-2.0-or-later | FTL: a credit in the docs ("Portions of this software are copyright © The FreeType Project") |
| zlib, libpng | `third_party/` | zlib / libpng licence | — |
| libjpeg 9f | `third_party/jpeg-9f` | IJG | Docs: "This software is based in part on the work of the Independent JPEG Group" |
| libwebp, brotli | `third_party/` | BSD-3 / MIT | Keep the notices |
| nghttp2 (curl's HTTP/2) | `third_party/nghttp2-1.70.0` | MIT | Keep the notice |
| ns-sans glyphs (the system's bitmap font is generated from them: `tools/fonts/gen_nssans.py`) | `third_party/fonts/ns-sans` (from NetSurf's framebuffer front end; Tim Tyler, Michael Drake) | MIT | Keep the notice |
| stb_image | `user/img` | Public domain / MIT | — |
| simplewebp | `user/img` | BSD | Keep the notice |
| MuPDF 1.28.5 (fitz, pdf; the URW base-14 fonts) | `third_party/mupdf-1.28.5`, the PDF Viewer | **AGPL-3.0** (Artifex) | The PDF Viewer under AGPL-3.0, its source available (the repository); keep `COPYING` |
| jbig2dec | `third_party/mupdf-1.28.5/thirdparty/jbig2dec`, the PDF Viewer | AGPL-3.0 | As MuPDF |
| OpenJPEG | `third_party/mupdf-1.28.5/thirdparty/openjpeg`, the PDF Viewer | BSD-2 | Keep its `LICENSE` |
| pdfwrite (Onyx) | `user/pdf/pdfwrite.h`, Letters, the Spreadsheet, Slides | **MIT** (ours) | — |
| The print system (Onyx) | `user/printerkit/` (`SD:/lib/printerkit.so`), `user/Apps/printd`, `user/Apps/printconf`, `/bin/ipp`: the Print dialog, the jobs, the page rasteriser, PWG Raster, the IPP client | **MIT** (ours) | — |
| python-pptx's default template | `tools/tests/slides/powerpoint.pptx` (a test deck, not on the card; made by `make_pptx.py`) | MIT (python-pptx) | — |
| **FileKit** (Onyx) | `user/filekit/` (`SD:/lib/filekit.so`): compression, ZIP archives, files and trees, paths — with zlib and the Archiver's ZIP engine (Onyx, MIT) inside | MIT (zlib: the zlib licence, below) | Keep the notices |
| **AudioKit** (Onyx) | `user/audiokit/` (`SD:/lib/audiokit.so`): the files, the background player, mixing, notes, WAV — with MeltySynth and the four decoders below inside it | **MIT** (ours); what it contains: MIT, CC0, public domain — nothing that binds the programs using it. FFmpeg (GPL) is **not** in it and must never be | Keep the notices |
| MeltySynth (C++ port) | `user/Apps/koton/synth` (in AudioKit: Koton, Media Player, BASIC) | MIT | Keep the notice |
| minimp3 | `third_party/minimp3`, AudioKit (the Media Player, `/bin/play`, BASIC), Jet Browser (`user/av/av_mp3.c`) | CC0 (public domain) | — |
| libvpx 1.15.2 (VP8 / VP9 decoders) | `third_party/libvpx-1.15.2`, Jet Browser and the Media Player's videos (`user/av/av_vpx.c`) | BSD-3-Clause + Google's VP8/VP9 patent grant (`PATENTS`) | Keep `LICENSE` and `PATENTS` |
| dav1d 1.5.1 (AV1 decoder) | `third_party/dav1d-1.5.1`, Jet Browser and the Media Player's videos (`user/av/av_dav1d.c`) | BSD-2-Clause | Keep `COPYING` |
| libopus 1.5.2 (Opus) | `third_party/opus-1.5.2`, Jet Browser and the Media Player's videos (`user/av/av_opus.c`) | BSD-3-Clause (royalty-free patent licences listed in `COPYING`) | Keep `COPYING` |
| stb_vorbis | `third_party/stb_vorbis`, AudioKit | Public domain / MIT | — |
| dr_flac, dr_wav | `third_party/dr_libs`, AudioKit | Public domain / MIT-0 | — |
| ares (PI DMA, ported) | `user/n64/n64_bus.cpp` | ISC | The notice is in the file — keep it |
| Mesa (V3D / QPU headers) | `tools/qpu/mesa` | MIT | Keep the notices |
| libonyxposix (the POSIX layer) and its sysroot files | `user/libc/posix`, `tools/onyx-toolchain.cmake`, `tools/cmake`, `tools/ports`, `/bin/posixtest`, `/bin/posixtest-cxx` | **MIT** (ours) | — |
| SQLite 3.50.4 (the amalgamation and its shell) | `third_party/sqlite-3.50.4`, `/bin/sqlite3` (the POSIX ports, not on the card yet) | Public domain | — |
| libxml2 2.13.8 | `third_party/libxml2-2.13.8`, `/bin/xmllint` (the POSIX ports) | MIT | Keep `Copyright` |
| curl 8.16.0 (libcurl and the tool) | `third_party/curl-8.16.0`, `/bin/curl` (the POSIX ports; with mbedTLS taken under Apache-2.0, nghttp2, zlib, brotli) | curl licence (MIT-like) | Keep `COPYING` |
| ICU 78.3 (libicuuc, libicui18n, the filtered data) | `third_party/icu-78.3`, `/bin/icutest` (the POSIX ports for WebKit, not on the card yet) | Unicode License V3 (SPDX Unicode-3.0), with the third-party notices in its `LICENSE` | Keep `LICENSE` |
| HarfBuzz 14.5.1 | `third_party/harfbuzz-14.5.1`, `/bin/hbtest` (the POSIX ports for WebKit) | "Old MIT" | Keep `COPYING` |
| Skia (milestone 154: WebKit's copy) | `third_party/skia-m154`, `/bin/skiatest`, `/bin/skiademo` (the POSIX ports for WebKit) | BSD-3-Clause | Keep `LICENSE` |
| libjpeg-turbo 3.1.4 | `third_party/libjpeg-turbo-3.1.4` (the POSIX ports for WebKit: Skia's and WebKit's JPEG) | IJG + BSD-3-Clause + zlib (`LICENSE.md`) | Docs: "This software is based in part on the work of the Independent JPEG Group"; keep `LICENSE.md` |
| WebKit (pinned revision `b8a7a626`, not in this repository: fetched by `tools/webkit/fetch.sh`): WTF, JavaScriptCore, bmalloc's headers | `/bin/jsc` (the shell of JavaScriptCore: step 1 of the WebKit port, with ICU linked in) | JavaScriptCore: **LGPL-2.1-or-later** (parts BSD-2-Clause); WTF, bmalloc: BSD-2-Clause | `/bin/jsc` is distributed under LGPL-2.1+: its sources are WebKit's at that revision **plus Onyx's patches, published in `tools/webkit/patches/`** (with `revision.sh` and the build script: the complete corresponding source). Statically linked: the objects to relink it are rebuilt from those sources by `tools/webkit/build-jsc.sh`. Keep WebKit's notices |
| WebKit (the same revision): WebCore, PAL, WebKit2, with WTF, JavaScriptCore | **Jet Browser** (`apps/jet.app/main`, its package `jet`; built by `tools/webkit/build-web.sh`, not in this repository) | WebCore, WebKit2: **LGPL-2.1-or-later** and BSD-2-Clause (per file) | `apps/jet.app/main` is distributed under **LGPL-2.1+**: the complete corresponding source is WebKit's revision plus `tools/webkit/patches/`, the ports in `tools/ports/`, and `user/Apps/jet/` (MIT, ours); statically linked: rebuilt and relinked by `build-webkit.sh` then `build-web.sh`. Keep WebKit's notices |
| Onyx's WebKit port files | the patches' new files inside WebKit's tree (`Source/cmake/OptionsOnyx.cmake`, `Platform*Onyx.cmake`, `wtf/onyx/`): BSD-2-Clause, WebKit's usual header ("Onyx contributors"); `tools/webkit/*.sh`, `smoke.js`, `bench.js` | BSD-2-Clause / **MIT** (ours) | — |
| Onyx's Skia font manager, the WebKit ports' build files | `tools/ports/skia/SkFontMgr_onyx.*`, `tools/ports/{icu,harfbuzz,freetype,libpng,libjpeg-turbo,libwebp,skia}` | **MIT** (ours) | — |
| newlib (libc of the C/C++ apps) | the toolchain | BSD-like (several) | Ship newlib's `COPYING.NEWLIB` notices with the binaries |
| libgcc / libstdc++ | the toolchain | GPLv3 + **GCC Runtime Library Exception** | None for our binaries |
| **The `aarch64-onyx-elf` toolchain** (WP-TC: GCC 14.2.0, binutils 2.43, newlib 4.4.0.20231231, GMP 6.3.0, MPFR 4.2.1, MPC 1.3.1) — its build script, patch and fetch script are ours | `tools/toolchain/` (MIT: `build-onyx-toolchain.sh`, `fetch.sh`, the patch file's own text); the binaries: the repository `stephaneweg/onyx-toolchain` (`aarch64-onyx-elf-14.2/`), not in Onyx | GCC, binutils: GPL-3.0-or-later; GMP, MPC: LGPL-3.0-or-later; MPFR: LGPL-3.0-or-later; newlib: BSD-like; libgcc / libstdc++: GPL-3.0 + **GCC Runtime Library Exception** | **The binaries must travel with the corresponding sources**: `onyx-toolchain` carries, beside the tarball, the six pinned source tarballs, `gcc-14.2.0-libstdcxx-onyx.patch` and the build script (`sources/`; GPLv3 §6(d): the binaries and their sources offered from the same place). Programs compiled with it are **not** affected: the Runtime Library Exception lets libgcc / libstdc++ / libsupc++ be linked into an Eligible Compilation (any program compiled by GCC, without a GPL-incompatible plug-in) under the program's own licence — `/bin/posixtest-cxx`, the ports, WebKit later keep theirs. The toolchain's installed `share/onyx-toolchain/` holds the patch and the script. |
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

## 5. (removed)

This section measured how much of the NetSurf Jet was still NetSurf's (2026-10-01). NetSurf left
the repository on 2026-10-04: Jet is the WebKit port.

## 6. To do (when the user decides)

1. A root **`LICENSE`**: GPL-3.0-or-later's text, the kapi exception (§2), the list of the parts
   under other licences (this file's §3), Jet's LGPL-2.1+.
2. WebKit's LGPL-2.1 text and notices beside Jet (`sdcard/apps/jet.app/`, or `SD:/docs/licences`).
3. A **`CREDITS`** / *About Onyx* listing every third-party component with its notice (FreeType's and
   IJG's credit lines are required).
4. The firmware licences on the card (`LICENCE.broadcom`, the Wi-Fi's).
5. SPDX headers in our own files (`// SPDX-License-Identifier: MIT`: the user's decision above),
   `user/kapi.h` and the app runtime first; a `LICENSE` (MIT) beside each app built only from them.
   The new files already carry the MIT notice — the EL0 work (`kernel/arch/aarch64/el0.S`,
   `el0blob.S`, `kernel/sys/el0.cpp`, `sys/uaccess.cpp`, `kern/handle.h`, `kern/uaccess.h`,
   `tools/el0scan.sh`, `tools/gen_kapi_names.py`, `/bin/sysstat`, `el0test`, `faulttest`), Mail
   (`user/mail/`), Photos, `user/pdf/pdfwrite.h`…; `user/kapi.h`, `user/kapi_names.h` (generated)
   and most older files do not yet.
6. The FM Song covers out of the public distribution.

## Decision (2026-10-02): the WebKit browser

The browser on **WebKit** is distributed under **LGPL-2.1+** (WebCore and JavaScriptCore; the rest of
WebKit is BSD-2), with the user's agreement; Onyx's own files in it stay under MIT; WebKit's sources
and Onyx's patches to them are published. Since 2026-10-04 it is **Jet Browser** (the NetSurf Jet,
GPL-2.0-only, was removed with its libraries).
