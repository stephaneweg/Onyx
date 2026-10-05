<div align="center">

# Onyx

**A multi-process operating system for the Raspberry Pi 4**, written from scratch on top
of the **Circle** bare-metal framework.

<img src="../screenshots/desktop.png" alt="The Onyx desktop" width="760">

</div>

> **Name.** *Onyx* is the name of the OS, its kernel, and its GUI. The repository folder is
> historically named `Zircon` (a legacy name; no relation to Google/Fuchsia's *Zircon*
> microkernel).

## Documents

| # | Document | For | Contents |
|---|---|---|---|
| 01 | **[Project Overview](01-PROJECT-OVERVIEW.md)** | everyone | vision, architecture, features, execution model (apps at EL0), the cores, repository structure |
| 02 | **[Kernel Internals](02-KERNEL-INTERNALS.md)** | to understand the kernel | boot, memory/MMU/ASID, scheduling, exceptions and system calls, ELF loader, threads, kapi ABI, streams, GUI, network, sound, app cores, GPU, `RAM:` (excluding Circle) |
| 03 | **[Developer Guide](03-DEVELOPER-GUIDE.md)** | to build / extend | toolchain, build, app model, writing an app/tool, uikit, the big apps' code, extending the ABI, conventions, debugging, pitfalls |
| 04 | **[User Guide](04-USER-GUIDE.md)** | to use it | SD card, boot options, Onyx desktop, terminal, `/bin` tools, files, Control Panel, app catalog, BASIC |
| 05 | **[Circle Changes](05-CIRCLE-CHANGES.md)** | HAL maintainers | the patches in our Circle fork vs upstream `Step51` |
| 06 | **[The Kits](06-KITS-GUIDE.md)** | to write a program | one kit per domain (AppKit, UIKit, SystemKit, NetKit, FileKit, ImageKit, AudioKit, FontKit, PrinterKit): how a program uses them, an example for each, adding to a kit |
| 08 | **[Jet Browser: the WebKit port](08-WEBKIT-PORT.md)** | the browser | the port of WebKit to Onyx (Jet Browser since 2026-10-04; the NetSurf Jet and its documents 06 and 07 were removed): where it stands, the patch series, the builds, the media engine, the compositor |
| 10 | **[AppKit reference](10-APPKIT.md)** | to write a program | every operation of AppKit, made from its headers (`tools/docgen/kitdocs.py`) |
| 11 | **[UIKit reference](11-UIKIT.md)** | to write a program | every operation of UIKit, made from its headers (`tools/docgen/kitdocs.py`) |
| 12 | **[SystemKit reference](12-SYSTEMKIT.md)** | to write a program | every operation of SystemKit, made from its headers (`tools/docgen/kitdocs.py`) |
| 13 | **[NetKit reference](13-NETKIT.md)** | to write a program | every operation of NetKit, made from its headers (`tools/docgen/kitdocs.py`) |
| 14 | **[FileKit reference](14-FILEKIT.md)** | to write a program | every operation of FileKit, made from its headers (`tools/docgen/kitdocs.py`) |
| 15 | **[ImageKit reference](15-IMAGEKIT.md)** | to write a program | every operation of ImageKit, made from its headers (`tools/docgen/kitdocs.py`) |
| 16 | **[AudioKit reference](16-AUDIOKIT.md)** | to write a program | every operation of AudioKit, made from its headers (`tools/docgen/kitdocs.py`) |
| 17 | **[FontKit reference](17-FONTKIT.md)** | to write a program | every operation of FontKit, made from its headers (`tools/docgen/kitdocs.py`) |
| 18 | **[PrinterKit reference](18-PRINTERKIT.md)** | to write a program | every operation of PrinterKit, made from its headers (`tools/docgen/kitdocs.py`) |
| | **[EL0 protected mode](EL0-PROTECTED-MODE.md)** | the execution model | how the apps moved from EL1 to EL0 (system calls), the design, the steps |
| | **[Licensing](LICENSING.md)** | distributors | the licences of everything Onyx contains; under which licence it can be distributed |
| | **[Handoff](HANDOFF.md)** | the next session | where the work stands, the conventions, the next tasks |

**Plans and studies** (each with its status at the top): [PI5-PORT](PI5-PORT.md) (the Raspberry Pi 5
port, a plan), [SUPERTUXKART-PORT](SUPERTUXKART-PORT.md) (paused), [BASIC-VM-THREADS](BASIC-VM-THREADS.md)
(an idea), [GC-WINDOWS-REPORT](GC-WINDOWS-REPORT.md) (the GameCube emulator on Windows),
[JET-DEAD-CODE](JET-DEAD-CODE.md) (NetSurf's dead code removed). The apps' studies and mock-ups, the
user's decisions: [gui-redesign](gui-redesign/README.md) (the modernised CDE desktop),
[pkg](pkg/README.md) (packages and updates), [mail](mail/README.md), [pdf](pdf/README.md),
[photos](photos/README.md), [paint](paint/README.md), [media](media/README.md),
[screenshot](screenshot/README.md), [clipboard](clipboard/README.md), [archiver](archiver/README.md),
[daw](daw/README.md) (Koton, and its [performance notes](daw/PERFORMANCE.md)), [slides](slides/README.md) (the presentation program), [qbstudio](qbstudio/README.md) (QBStudio, the IDE for desktop apps in BASIC),
[circle-upstream](circle-upstream/README.md) (the fork's changes offered to upstream Circle).

## Formats

- **Markdown** (this folder) — for viewing on GitHub.
- **Word / PDF** — in [`exports/`](exports/), one `.docx` + one `.pdf` per document,
  generated from the `.md` files by [`build_docs.py`](build_docs.py)
  (`python docs/build_docs.py`). The shared visual signature (the Onyx theme) is defined by
  [`assets/make_reference.py`](assets/make_reference.py), which builds the pandoc
  `reference.docx`.

Screenshots (the real apps' windows) live in
[`../screenshots/`](../screenshots/) and are produced by
[`tools/tests/desktop_sim/shots.sh`](../tools/tests/desktop_sim/shots.sh) (the real apps, run on
a PC against a stand-in kernel).

## A note on the legacy docs

At the repository root, `ARCHITECTURE.md` is the **original design record**: it describes the
first execution model ("Option C": apps at EL1 calling the kernel directly) and cooperative
scheduling; a banner at its top says what changed. Where it conflicts, **this documentation
(`docs/`) is authoritative**. Its §11–§12 remain the historical reference for *why* Option C was
chosen; apps run at EL0 since kapi v74 ([EL0-PROTECTED-MODE.md](EL0-PROTECTED-MODE.md)). The
in-OS strings and some screenshots may still say *Zircon* (the rename is a separate task).
