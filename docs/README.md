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
| 01 | **[Project Overview](01-PROJECT-OVERVIEW.md)** | everyone | vision, architecture, features, execution model, repository structure |
| 02 | **[Kernel Internals](02-KERNEL-INTERNALS.md)** | to understand the kernel | boot, memory/MMU/ASID, scheduling, exceptions, ELF loader, kapi ABI, streams, GUI (excluding Circle) |
| 03 | **[Developer Guide](03-DEVELOPER-GUIDE.md)** | to build / extend | toolchain, build, app model, writing an app/tool, extending the ABI, conventions, debugging, pitfalls |
| 04 | **[User Guide](04-USER-GUIDE.md)** | to use it | SD card, boot, Onyx desktop, terminal, files, customization, app catalog |

## Formats

- **Markdown** (this folder) — for viewing on GitHub.
- **Word / PDF** — in [`exports/`](exports/), one `.docx` + one `.pdf` per document,
  generated from the `.md` files by [`build_docs.py`](build_docs.py)
  (`python docs/build_docs.py`). The shared visual signature (the Onyx theme) is defined by
  [`assets/make_reference.py`](assets/make_reference.py), which builds the pandoc
  `reference.docx`.

Screenshots (simulated, faithful to the real skins/font/icons) live in
[`../screenshots/`](../screenshots/) and are produced by
[`tools/screenshot/render.py`](../tools/screenshot/render.py).

## A note on the legacy docs

At the repository root, `ARCHITECTURE.md`, `README.md`, `kernel/README.md`, and
`sdcard/README.md` describe **older states** of the project (EL0 processes, preemptive
scheduling, 640×480, "two demos") and still use the legacy name *Zircon*. Where they
conflict, **this documentation (`docs/`) is authoritative** for the current state.
`ARCHITECTURE.md` §11–§12 remains the best historical reference for *why* the "Option C"
model (EL1 apps) and cooperative scheduling were chosen.
