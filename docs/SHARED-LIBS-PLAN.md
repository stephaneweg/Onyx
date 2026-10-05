# Onyx: shared libraries behind an export table — the decided design and the plan

*Status (2026-10-05): **built, on the Pi.** The kernel loads shared libraries (kapi v83
`lib_open`), `SD:/lib/ft.so` (FreeType) and `SD:/lib/uikit.so` (the toolkit) exist, and **every app of
`user/` is built against them** (`lib/uikit.imp.a`, `lib/ft.imp.a`). What was built, where it departs
from the plan below, and what is tested: **section 0**. The reference documentation is docs/02 §7
*Shared libraries* (the kernel) and docs/03 §5.6 *Shared libraries* (writing and using one); the rules
that keep old programs working are in `user/uikit/abi.h`. The study that led here, and the user-space
GUI it prepares: `docs/GUI-USERSPACE-STUDY.md` (§3.3). Answer the user in French; this page stays in
English.*

## 0. As built (2026-10-04 / 05)

| Step | What | Where |
|---|---|---|
| 1a | The kernel: a library is an image (`ET_DYN` at 0, `ELF_KIND_LIB`), placed once in the arena 16 GB..32 GB (`LibPlace`), its `R_AARCH64_RELATIVE` relocations applied once to the data's copy (`LibRelocate`), up to 16 per address space (`ImageMapLib`), `kapi_lib_open` (slot 261), `KAPI_IMG_LIB`, preload of a library | `kernel/proc/elf.cpp`, `proc/image.cpp`, `kernel.cpp` `LibraryOpen`, `sys/kapi.cpp` |
| 1a | The user side: `user/lib.h` (`TLibImports`, `TLibHeader`, `lib_bind`), `user/librt.cpp` (a library's runtime), `user/lib.ld`, `lib.vers`; the test library `user/demo`, `/bin/libtest` | |
| 1b | The generator `tools/libgen/libgen.py`; `ft.so` (FreeType's public API: 135 entries, `user/ft/ft.abi`; `user/ft/ftso.c`) | `user/Makefile` |
| 1c | `uikit.so` (683 entries, `user/uikit/uikit.abi`), the globals shared with the programs (`uikit/globals.inc`, `globals.cpp`, `global.h`), the reserve (`Widget`, `Canvas`, `Root`), the layout lock (`uikit/layout_lock.cpp`, `tools/libgen/layout.py`), the rules (`uikit/abi.h`); every app, Doom, BASIC's runtime and Koton's plugins relinked | `user/Makefile`, `user/doom/Makefile` |
| + | `printerkit.so` (33 entries, `user/printerkit/printerkit.abi`): printing (docs/03 §5.7) — the first library that uses others (`ft.so`, `uikit.so`: their import stubs linked in, opened on demand; uikit's variables through the importer's table) | `user/Makefile` |
| | Packages `uikit` and `ft` (required), `needs = uikit >= 1.683, ft` on `onyx` and on every app | `tools/pkg/packages.ini` |

**Where it departs from the plan below — the mechanism of sections 4.3 and 5.1–5.2 (the defaults
"reasoned, not compiled", not the user's decisions D1–D8).** The plan had the generator write, from a
hand-kept list, header thunks (`UIKIT->...`) and `init_` / `fini_` bodies for every constructor. What
was built instead needs **no change to the apps' sources nor to uikit's headers**:

- **Import stubs, not header thunks.** The table's entries are the library's global functions *under
  their own (mangled) names*; the program side is one four-instruction stub per entry, with the same
  name, that jumps through the table (`adrp x16, onyx_uikit_table; ldr; ldr x16, [x16, #slot]; br x16`).
  The linker binds the app's calls — `uikit::Widget::invalidate`, `FT_Load_Glyph`, a constructor — to
  the stubs. Still D3 (a table of pointers filled at load, called as `KT`), D4 (append-only,
  versioned), D6 (no ELF dynamic linker: nothing is looked up by name at run time; the apps stay
  non-PIC and static).
- **The list is generated**: `<lib>.abi` is kept in git but written by the tool from the objects'
  symbols (`nm`), append-only (a slot number on each line: a line moved or removed fails the build).
  The table's **version is its number of entries**.
- **Whole constructors are entries** (not `init_` / `fini_` bodies): the library's constructor builds
  the bases and the members with the library's layout, which is the same text — and the reserve is
  initialised by it, so a later library can use it in objects made by old programs.
- **Vtables**: a class whose vtable is emitted only with its key function (Root, Modal, Panel,
  Checkbox, Splitter…) is referenced by the apps: the program side gets a **copy** of each vtable
  (`--vtables`), its slots resolved by the stubs.
- **Globals** are the *program's* variables, handed to the library by address (`TLibImports.data`):
  the apps' code is unchanged (`C_BG` is a plain variable for them); in the library they are
  references bound at `init`. (The plan had them in the library, reached by macros in the apps.)
- **P3**: `TLibImports` has `alloc` / `free` (no `realloc`: FreeType's is made of them in
  `ftso.c`) and `data` / `ndata`.

**Tested** — PC: `sh tools/tests/run_image_test.sh` (554 checks; the loader on the real `demo.so`),
`sh tools/tests/shlib/check_pic.sh`. Pi 4 (2026-10-04): `/bin/libtest` (17 checks: section 6, step
1a, all of its ten points); the apps started one by one on the libraries
(`python tools/tests/shlib/pi_apps.py <pi-ip>`); **the compatibility test of D5**
(`sh tools/tests/shlib/compat.sh` + `python tools/tests/shlib/compat_pi.py <pi-ip> out/shlib-compat`):
a program built against uikit N, not rebuilt, on N+1 — see section 10 for the results.

**Not done** (sections 5.3 and 5.5's refinements): the inline code with logic of uikit's headers was
*not* moved into the library — it is compiled into the apps as before, so a fix to it needs the apps
rebuilt (the rule is written in `uikit/abi.h`; move a piece when it has to change). Spare slots exist
in `Widget` and `Root` only (a new overridable goes there). Jet's hosted build stayed static at first (P6); since 2026-10-05 it links the library's import side too.
`sdcard/etc/preload.ini` is unchanged: the desktop's own processes keep both libraries in memory.

## 1. What the user decided (2026-10-04)

| # | Decision | Notes |
|---|---|---|
| D1 | **The apps stop carrying a static copy of uikit (then FreeType, then others): shared libraries, one physical copy in memory, mapped into every process that uses them.** | the user's goal |
| D2 | **No library at a link-time fixed address.** An app is never bound to one binary of a library. | the user ruled out the a.out / Windows-base style |
| D3 | **A library is PIC code that publishes its entry points in a table of pointers, filled when it is loaded; an app finds the table and calls through it — exactly as it uses `kapi` (`KT->x (...)`).** | the user's design |
| D4 | **The table is append-only, versioned like `kapi`** (`version`, `size` first; never reorder, never remove, never change a signature: a new entry `foo2` instead). | as `kern/kapi_abi.h` |
| D5 | **A bug fix or an added function in a library ships without rebuilding any app.** This is the point of the work; the rules of §5 exist to keep it true. | asked and confirmed by the user |
| D6 | **No ELF dynamic linker** (`ld.so`, symbol lookup by name, PLT/GOT in the apps). The apps stay non-PIC, static, at 8 GB (`user/user.ld` unchanged). | table-based dynamic linking (the AmigaOS `OpenLibrary` model), not ELF's |
| D7 | **Not** uikit inside the window server, **not** uikit in a process of its own (304 `onDraw` overrides, 418 classes derived in the apps). | study §3.1–3.2 |
| D8 | Order: the kernel and a demo library → **FreeType** (a C library: no class layouts) → **uikit** → others (mbedTLS, newlib, FFmpeg) as wanted. The user-space GUI (`wsd`, `libgui`) comes after, on this mechanism. | |

**Defaults taken with the design (proposed in the study, not contradicted; change them here if the
user does):**

| # | Default | Why |
|---|---|---|
| P1 | The kernel places a library **once for the whole system** when it loads its image (a free slot in [16 GB, 32 GB)), and applies its relocations **once**, to the image's copy of the data. Not a link-time address: another build, another boot, another place. | no relocation work per process; the relocated vtables / table are identical everywhere; the kernel can point `kapi` slots into a library (`libgui`, later) |
| P2 | An app gets a library with a **call**, `kapi_lib_open (name, min_version)` (kapi v83), made by a small bind object linked into the app (a constructor of priority 101: before the app's own). An ELF note naming the libraries (mapped before `_start`) may come later; same table. | simple, explicit, testable |
| P3 | **The importer's allocator**: the app passes `alloc` / `free` to the library's `init` in a `TLibImports` table; the library's `operator new` / `delete` (and `malloc` if it needs one) go through it. | one allocator per process, whether the app is freestanding (`umm`) or newlib (`malloc`) |
| P4 | Libraries live in **`SD:/lib/<name>.so`** (ELF `ET_DYN` — not loadable as a program); one package per library (`needs = uikit`). | |
| P5 | A layout break of a C++ library = **a new name** (`uikit2`), beside the old one. | the escape hatch, not the rule |
| P6 | Jet's hosted build of uikit (`tools/webkit/build-web.sh`, `-DONYX_HOSTED_NEW`) **stayed static** at first; **done 2026-10-05**: the script compiles the import side (`lib/uikit_stubs.S`, `lib/uikit_bind.cpp`, `uikit/globals.cpp`) with the POSIX toolchain and Jet opens `SD:/lib/uikit.so` like every app (its allocations through the program's `operator new`: newlib's malloc). | another toolchain, another runtime |

## 2. The library format (step 0: done, `tools/tests/shlib/`)

Built and checked with the Arm GNU Toolchain 14.2.rel1 `aarch64-none-elf` (the one of docs/03):

```sh
aarch64-none-elf-g++ -O2 -fPIC -fvisibility=hidden -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics -fno-use-cxa-atexit -mgeneral-regs-only -c lib.cpp -o lib.o
aarch64-none-elf-ld -shared -Bsymbolic -z text -z max-page-size=0x10000 --no-undefined --hash-style=sysv \
    -T lib.ld --version-script lib.vers -e onyx_lib_table -o name.so lib.o
```

- `lib.ld` (`tools/tests/shlib/lib.ld`, to move to `user/lib.ld`): linked at 0; **two `PT_LOAD`**, as
  `user.ld` — RX (headers, `.dynsym`, `.rela.dyn`, `.text`, `.rodata`) and RW (`.data.rel.ro`,
  `.init_array`, `.dynamic`, `.got`, `.data`, `.bss`) on separate 64 KB pages — + `PT_DYNAMIC`;
  `__lib_init_array_start/end` for the library's constructors; `.eh_frame` discarded.
- `lib.vers`: `{ global: onyx_lib_table; local: *; };` — **one exported symbol**, the table.
- **`-e onyx_lib_table`: the ELF entry point is the table's offset.** The kernel finds the table with
  no symbol table at all.
- `-z text`: the link fails if code needs patching. `--no-undefined`: the library imports nothing by
  name (it reaches the kernel through `KT`, the app through `TLibImports`, other libraries through
  their tables).
- **Result on the demo** (a C++ class with virtuals, a static constructor, a table of strings, the
  allocator imported): **14 relocations, all `R_AARCH64_RELATIVE`, all in the RW segment**; the RX
  segment is never written — **shared as is by every process**.

`sh tools/tests/shlib/check_pic.sh` re-checks all of that (run it after any toolchain or flag change).
Note: a C++ `const` object at namespace scope has internal linkage — the table needs an `extern`
declaration before its definition (as in `demolib.cpp`).

## 3. The kernel (step 1a)

Built on the v77 program images (`kernel/proc/image.cpp`, `kern/image.h`, docs/02 §7, the host test
`tools/tests/image/imagetest.cpp`). A library **is** a `TImage`: read once, its RX frames mapped
*not owned* into each user, its RW bytes kept in the object (`TImgSeg::pInit`) and copied into private
pages per process, reference counted, preloadable, dropped when its file changes
(`ImageFileChanged`). What changes:

1. **`ElfReadPlan` (`kernel/proc/elf.cpp:31`) with a library flag**: accept `ET_DYN` linked at 0
   (today: segments must lie in the user range), require exactly the shape of §2 (two `PT_LOAD`, one
   `PT_DYNAMIC` inside the RW one, the entry inside the RW segment), and return the dynamic section's
   place. A program (`ET_EXEC` at 8 GB) is refused as a library and a library as a program.
2. **Placement (P1)**: a library arena `USER_LIB_BASE` 16 GB .. `USER_LIB_END` 32 GB in `kern/layout.h`
   (the hole noted at `layout.h:109`); a system-wide first-fit allocator of 64 KB-aligned ranges; an
   image gets its base `ulBase` when its load begins, gives it back when it is freed. A process still
   running an old build keeps its range busy (reference count): no overlap ever.
3. **Relocation, once, at load** (in `Load`, after the segments are read): walk `DT_RELA` /
   `DT_RELASZ` / `DT_RELAENT` (in the RX segment's frames: readable by the kernel through the identity
   map); for each entry: **type must be `R_AARCH64_RELATIVE` (1027)** — anything else fails the load
   with the type in the reason; the offset must fall in the RW segment's file bytes; write
   `ulBase + addend` (u64) into that segment's `pInit` copy. Refuse `DT_TEXTREL`. Every process then
   copies already relocated bytes (`ImageMap`'s private branch, unchanged).
4. **Several images per address space**: `CAddressSpace` keeps the program's `m_pImage` and **a small
   array of libraries** (`m_pLib[LIB_MAX]`, 16), each released by the destructor (`addrspace.cpp:230`,
   `:308`). `ImageMap` (`image.cpp:447`, which refuses a second image today) gets a library variant
   that maps at `ulBase + p_vaddr` and records the image in that array.
5. **`kapi_lib_open` — kapi v83, slot 261** (after `win_resizable`, v82, slot 260):
   `const void *lib_open (const char *name, unsigned min_version, int *err)`:
   - `name`: a bare name (`"uikit"`) → `SD:/lib/uikit.so` (P4); a path is accepted too (tests);
   - already mapped in the caller → the same table (idempotent);
   - else find the named image (no card access) or load it (the calling task streams it, as a program
     start), map it, and return `ulBase + e_entry`;
   - **version check in the kernel**: the table's first `u32` (read in the relocated `pInit`) must be
     `>= min_version`, else 0 and `*err = -KAPI_ENOTSUP` (too old) — the app says so and exits;
   - 0 and `-KAPI_ENOENT` (no file) / `-KAPI_EINVAL` (not a library: format, a relocation other than
     `RELATIVE`) / `-KAPI_ENOMEM` (memory, or no room in the arena) otherwise. (No `ENOEXEC` /
     `EVERSION` exist in `kapi_abi.h`; appending them is possible but not needed.)
   No `lib_close`: a library stays mapped until the process ends.
6. **`image_list` / `/bin/preload` / `/bin/unload`** work on libraries unchanged (they are images):
   add `KAPI_IMG_LIB` (8) to `kapi_image_info.flags` so `preload` lists them apart; `preload
   SD:/lib/uikit.so` in `SD:/etc/preload.ini` keeps it loaded from the boot.
7. **Log line** per library load, as for programs (`kmsg`): `lib uikit.so: loaded in N ms at 0x4_0000_0000,
   R relocations, X KB shared, Y KB private`.
8. **Docs at the same time** (CLAUDE.md rule): docs/02 (the ABI table, v83 in the version history, §7
   *Program images* → libraries, the VA map), docs/03 (writing and using a library), `kapi_abi.h`'s
   history comment, `user/kapi.h` wrapper, `kapi_names.h`.

**Not changed**: the fault paths, the apps' link, `user.ld`, the EL0 blob, the scheduler.

## 4. The user side (steps 1a–1b)

### 4.1 Headers

- `user/lib.h` (new, MIT): `TLibImports { unsigned size; void *(*alloc) (size_t); void (*free) (void *);
  void *(*realloc) (void *, size_t); }`, the `kapi_lib_open` wrapper, and a helper
  `lib_bind (name, min_version, &table)` that opens, calls `table->init (&imports)`, and on failure
  shows *"<app> needs library <name> version ≥ N"* (kmsg + `ask`-style box if a window exists) then
  exits.
- Every library's table starts with the same three fields:
  `unsigned version; unsigned size; int (*init) (const TLibImports *);`.
- `init` is called by **each** process (it is per process: the RW segment is private); it is
  idempotent (a library opened by the app and by another library); it runs the library's
  `__lib_init_array` once, then opens the libraries it depends on (uikit → `lib_open ("ft")`).

### 4.2 The bind object

Per library, a generated `user/<lib>/<lib>_bind.cpp`, linked **statically** into the apps that use the
library: `const T<Lib>Table *g_<lib>;` and a constructor `__attribute__ ((constructor (101)))` that
calls `lib_bind`. `crt0.S` / `crt0libc.S` already run `.init_array` sorted by priority, so the
library is bound before any app constructor (`static Menu menu;` in apps touches uikit at construction).

### 4.3 The generator (step 1b)

`tools/libgen/libgen.py` (new): **one list per library** — `user/<lib>/<lib>.abi`, append-only like
the table it describes — produces:

1. `<lib>_abi.h`: the table struct, the version, `KAPI_STATIC_ASSERT`-style checks of its size;
2. the app-side inline thunks (`UIKIT->...` calls) — for C: inline functions / macros with the original
   names (`FT_Load_Glyph` stays the name the app source uses);
3. the library-side table definition (`onyx_lib_table`) and, for C++, the trampolines from table
   entries to member functions (`static void t_widget_invalidate (Widget *w, bool r)
   { w->Widget::invalidate (r); }` — qualified: non-virtual);
4. a **frozen snapshot** (`<lib>.abi.lock`): the entries of the last published version; the build
   fails if an entry of the snapshot moved, vanished or changed its signature.

### 4.4 FreeType first (step 1b)

`ft/libft.a` (FreeType) becomes `SD:/lib/ft.so`: the FreeType calls Onyx's code uses (count them from
`ft/fonts.h`, `ft/uikitface.h`, the apps — `grep -ho "FT_[A-Za-z_]*" user` gives the list) in its table,
its memory through `FT_Memory` over the importer's allocator, its file access through the kapi. The
header-only `ft/fonts.h` / `ft/uikitface.h` stay in the apps at this step (they call FreeType through the
table). C: no layouts beyond FreeType's own public structs, which FreeType keeps stable.

## 5. uikit as a library: the C++ rules (step 1c)

The table says *where the functions are*. C++ also exposes **the layout of the classes** (apps
allocate and subclass them, and read / write `left`, `width`, `canvas.px`, `valid`, `hidden`...) and
**the order of the virtual functions** (apps' vtables are built by the apps' compiler). So uikit's ABI is
**the table + the layouts + the virtual order**, and all three follow the append-only rule.

### 5.1 One header, two builds

The public headers are compiled twice: in the library (`-DUIKIT_BUILDING_LIB`: methods declared,
defined in the `.cpp` as today) and in the apps (each non-inline method defined **inline** as a thunk
to the table, generated). The **class definitions — fields and virtuals — are the same text on both
sides**: the vtable slot order and the offsets match by construction.

### 5.2 Constructors and destructors: bodies only

A complete constructor or destructor also constructs / destroys the bases and the members, and each
side's compiler already does that. So the library exports **only the bodies**:

- every exposed class gets `void init_ (args)` (what the constructor did: its member-initialiser list
  turned into assignments, then its body) and `void fini_ ()` (the destructor's body);
- in the library: `Widget::Widget (int l, int t, int w, int h) { init_ (l, t, w, h); }`,
  `Widget::~Widget () { fini_ (); }`;
- in the apps: the same, with `init_` / `fini_` thunks to the table.

The compiler of whichever side creates the object then runs the chain once: base `init_`s, member
constructors (`Canvas` is a member of `Widget` with its own constructor and destructor: they are
exposed the same way), the class's `init_`. **Rule: no logic in member-initialiser lists of exposed
classes; no complete constructor or destructor in the table.**

Today's example (`user/uikit/widget.cpp:9-19`): the 25 member initialisers of `Widget::Widget` move into
`Widget::init_`; `canvas.alloc (w, h)` stays in it.

### 5.3 Virtuals

- An app class derived from `Button` gets its vtable from the app's compiler; the slots it does not
  override point to the app-side thunks of the base methods (emitted out of line because their address
  is taken) → the table → the library's code. No library address in the app.
- The library calls `w->onDraw ()` through the object's vtable: the app's for an app-made widget, the
  library's for one it made (a dialog's buttons). Same slot order (§5.1): both work.
- Base defaults written inline today (`virtual void onDraw () {}`, `bgColor ()`, `isField ()`...) are
  compiled into the apps: **a later change to such a default reaches only rebuilt apps** — move every
  default with logic into the library (an out-of-line method with a table entry).

### 5.4 The reserve (to put in BEFORE the first release of the library)

So that additions keep old apps working (D5):

- **spare virtual slots** at the end of the virtuals of every class apps derive from (`Widget`, `Root`,
  `Modal`, and the controls apps subclass — list them from the 418 derived classes:
  `grep -rhoE "class \w+ *: *(public )?\w+" user/Apps | awk '{print $NF}' | sort | uniq -c | sort -rn`).
  Proposed: 8 for `Widget`, 8 for `Root`, 4 for the others. A spare is declared
  `virtual void reserved_N ();` with a library default that does nothing; giving it a meaning later
  (renaming it, a default in the library) keeps the slot: old apps' vtables point to the thunk → the
  table → the new default;
- **spare bytes** at the end of every exposed class (proposed: 32 in `Widget`, 16 in the others) and a
  **`void *ext`** that the library may allocate for state that does not fit;
- **new fields go into the reserve or behind `ext`; new virtuals into a spare slot** — never in the
  middle of a class;
- **a check at build time**: the generator writes `uikit.layout.lock` — for each exposed class its
  `sizeof`, the `offsetof` of each field, and its virtuals in order (from the header) — and a host test
  compiles a probe against the current headers and compares. A difference fails the build unless the
  lock is updated deliberately (and that means `uikit2`, P5).

### 5.5 What else moves

- **Globals**: the 22 `extern` declarations of the headers (the theme colours `C_BG`..., `uk_face_`,
  `uk_face_fw_`...) become fields of one `UIKitGlobals` struct in the library's RW data, reached through
  `UIKIT->globals`; the old names stay, as macros or inline references (`#define C_BG
  (g_uikit->globals->c_bg)`), so the apps' source does not change. Assignments keep working (lvalues).
- **Statics in headers**: the class statics (`Menu::current`, `Font::Sans`, `Dialog::titleH`,
  `Calendar::daysIn`...) are functions: table entries like the methods.
- **Inline code with logic** (`widget.h:21-104`: `uk_fw` / `uk_fh`, the thumb arithmetic,
  `UkBarDrag`; `text.h` `UkFaceScope`; `skin.h` `uk_tint`; inline accessors of `root.h`): into the
  library unless trivial — what stays inline is frozen in the apps.
- **`ft/fonts.h`, `ft/uikitface.h`** (header-only FreeType glue): into `ft.so` or `uikit.so` (to decide
  then; uikit's text face is the natural owner).
- **`onyxpp.hpp`**: the apps keep defining `operator new` / `delete`; the bind object passes them to
  `init` (P3). The library defines its own `operator new` / `delete` over `TLibImports`.
- **The canvas's NEON** (`uikit/canvas.o`, `imgload.o` built without `-mgeneral-regs-only`): same flags
  in the library build.
- **Callbacks** (`Action`, `MenuAction`: plain function pointers into the app): unchanged — absolute
  addresses in the app, called by the library.
- **The Makefile**: `uikit.so` built with `-fPIC -fvisibility=hidden` (§2); the apps drop `uikit/libuikit.a`
  for `uikit/uikit_bind.o`; `make stage` copies `user/lib/*.so` to `sdcard/lib/`. The `kp_*` Koton plugins
  link `libuikit.a` without using it: drop it. Doom (`doom/doom_uikit.cpp`), BASIC
  (`basic/runtime.cpp`), the games (`game.h`) use uikit like any app.
- **Every app rebuilt once** on the library (the last time for a compatible uikit change), every app
  started on the Pi (§6).

## 6. Tests — to automate on the Pi

Each Pi test is a `/bin` tool or an app mode that prints `PASS name` / `FAIL name: reason` lines and
exits 0 only if all passed (the style of `el0test`, `faulttest`), so a script can run them all and
collect the log.

### Host (PC), every build

- `tools/tests/shlib/check_pic.sh` — the format (§2), for each real library too (`check_pic.sh
  user/lib/uikit.so` once the script takes an argument).
- `tools/tests/image/imagetest.cpp` extended: a library image from `demo.so` — placed in the arena,
  relocations applied once (compare the table's pointers with `base + addend`), two address spaces
  sharing the RX frames (same physical frames, different private RW), refusal of a non-`RELATIVE`
  relocation (a hand-made `.so`), of `TEXTREL`, of a program as a library and a library as a program,
  arena reuse after the last reference, `ImageFileChanged` on a library path, version too old.
- The generator: the `.abi.lock` check (an entry moved → failure), the layout lock (a field added to
  `Widget` outside the reserve → failure).

### Pi, step 1a — `/bin/libtest` + `SD:/lib/demo.so`

1. open `demo` → a table; `version`, `size` as built; `init` twice → 0 both (idempotent);
2. `ctor_ran () == 42` (the library's `.init_array` ran); `name (2)` → `"two"` (relocated pointers);
3. `make_rect (3, 4)` → `area_of` → 12 (a library vtable); an app subclass of `Shape` overriding
   `area` passed to `area_of` → the app's value (an app vtable called by the library);
4. memory: `new` in the library, `delete` in the app and the reverse (one allocator, P3);
5. `lib_open ("demo", 99)` → 0, `-KAPI_ENOTSUP`; `lib_open ("nosuch")` → `-KAPI_ENOENT`;
6. two `libtest --child` processes at once: `image_list` shows `demo.so` **once**, refs 2, size =
   its RX pages (not twice);
7. a child that faults inside a library function: only it dies, the library stays for the others;
8. replace `demo.so` while a child runs (copy another build over it): the running child keeps working
   on the old one, a new child gets the new one (`image_list`: the old one `UNNAMED`); after the old
   child ends, the old range of the arena is free again;
9. `preload SD:/lib/demo.so`, then a start: the log line says `shared`, no card read;
10. 500 starts / ends of a child in a loop: no leak (`image_list` sizes, `vm_stats`, free memory equal
    before and after).

### Pi, step 1b — FreeType

The FreeType apps (Letters, Calendar, Sheet, Mail, the Control Panel's applets...) draw their text as
before (compare screenshots: `screen_grab` through `/bin/screenshot`, or `shots.sh` on the PC);
`image_list`: `ft.so` once; the binaries' sizes go down by ~92 KB each.

### Pi, step 1c — uikit

- **Every app starts and draws** (a script launches each `SD:/apps/*.app`, waits, grabs the screen,
  closes it; a crash or a "needs library" box is a failure) — the ~88 uikit apps, BASIC programs, Doom,
  the emulators, the Koton plugins.
- **The compatibility test of D5 — the reason for the work**: build `wtkdemo` (or `widgets`) against
  uikit version N; then, **without rebuilding it**: (a) fix a bug in a widget's drawing → the old app
  shows the fix; (b) append a function and a new widget class → the old app still runs; (c) use one
  spare byte and one spare virtual slot in `Widget` → the old app still runs, a rebuilt app uses them;
  (d) start an app built against N+1 with the library N → refused cleanly ("needs uikit ≥ N+1").
- Memory: `image_list` shows `uikit.so` once; resident memory with 15 GUI apps open, before / after.

## 7. Packages (CLAUDE.md rule: publish what changes on the card)

- `tools/pkg/packages.ini`: a package per library (`uikit`, `ft`, `demo` only if wanted), files
  `lib/<name>.so`; every app package that uses one gets `needs = uikit` (and `ft`). A library package's
  version is its table version + a build number.
- `pkg` already `unload`s / `preload`s a program it replaces (`pkglib.h` `move`): the same for a
  library (the kernel's file hook also drops the name).
- The onyx package (the kernel, v83) before any app that needs `lib_open`: an app's `needs` include the
  onyx version with v83.
- `sdcard/etc/preload.ini` (the Preload applet): propose `SD:/lib/uikit.so` and `SD:/lib/ft.so`.

## 8. Licences

Libraries of ours: MIT (docs/LICENSING.md). FreeType is FTL / GPLv2 (dual): as a shared library it is
used under the FTL, as today statically — no change for the apps. Later libraries: FFmpeg and MuPDF
(LGPL / AGPL) shared change nothing for Onyx's own code (LGPL prefers a shared library); docs/LICENSING.md
to update when each one becomes a library.

## 9. Later, on the same mechanism (not in this plan)

- **`libgui` and the user-space window server `wsd`** (`docs/GUI-USERSPACE-STUDY.md` §2): `libgui` is a
  library loaded into every process; because of P1 the kernel can write its entry addresses into the
  GUI slots of the `kapi` table, so existing apps talk to `wsd` without a rebuild.
- An ELF note listing an app's libraries, mapped before `_start` (P2's alternative).
- Sharing the relocated `.data.rel.ro` (the vtables, the tables) read-only between processes: a third
  segment in `lib.ld`, identical in every process thanks to P1.
- Full ELF dynamic linking (`ld.so`), only if unmodified ports need `.so` files as they are.

## 10. Results, and what is still not verified

**On the Pi 4 (2026-10-05, kernel kapi 83, uikit 683 entries, ft 135):**

- `libtest 200`: **17 checks passed** (section 6, step 1a: every point).
- `pi_apps.py`: **84 apps started one by one, 0 failed** — every app of the card built on the libraries
  (the desktop's own processes run on them since the boot: menubar, dock, notifyd, agenda; Doom, the
  BASIC apps through `/bin/basic`, the emulators, Koton, Letters, the Spreadsheet, Slides, Mail, Paint,
  Photos, the Media Player, the PDF Viewer, the Control Panel and its applets; Jet, static, beside them).
- **The compatibility test (D5): 7 checks passed.** A program built against uikit N, **not rebuilt**, on
  the library N+1: (a) a fix in a library function reaches it; (b) a function appended and (c) a
  reserved virtual slot given a meaning and a reserved field written by `Widget`'s constructor do not
  disturb it (its widgets, its derived class, its callback, the same layout); a program built against
  N+1 uses them; (d) the program built against N+1 is refused by the library N, with *needs the shared
  library "uikitc" (version 684 or later): the one installed is older*.
- Sizes: `uikit.so` 320 KB of code shared + 64 KB of data per process, `ft.so` 192 KB + 64 KB. An app's
  file: Calendar 862 KB → 246 KB, wtkdemo 229 KB → 195 KB; `sdcard/apps` as a whole 74 MB → 58 MB.

**Found on the way:** `user.ld` did not order the constructors' priorities across files (a static
`Menu` of the Control Panel ran before the bind constructor: a fault at its start) — fixed, with
`lib.ld`. `bin/Makefile`'s default goal. The telnet shell no longer stops `kmsg` on Ctrl+C (the tests
keep a second session in it).

**Not verified / not done:** resident memory with 15 GUI apps open, before / after (not measured);
the screenshots were looked at, not compared pixel by pixel with the static builds; the inline code
of uikit's headers is still compiled into the apps (section 0); `docs/exports` were not regenerated (no
pandoc on this machine: `python docs/build_docs.py`).

