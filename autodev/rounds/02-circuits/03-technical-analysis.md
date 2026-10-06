# AutoDev round 2 — Technical Analyst: Circuits

Date: 2026-10-06. Inputs: `01-product-manager.md`, `02-product-analysis.md` (the scope, the 20 missions, the file
formats, the 26 acceptance criteria — kept as they are). Branch `AutoDev`.

Read for this analysis: `user/Apps/turtle/world.h` (the pack parser `parse_pack`, `Level`, `Pack`, `Arr<T>`, the
concept cards), `user/Apps/turtle/main.cpp` (its `kv_*` progress store, `load_packs`, `onDrop`, `build_menu`, the
F-keys, `kapi_get_args`), `tools/tests/turtle/turtletest.cpp` + `tools/tests/run_turtle_test.sh`;
UIKit `widget.h` / `widget.cpp` (mouse routing, `catchOutside`), `root.h` (`onTick`, `onDrop`, `onKey`), `vpaint.h`
(`VPath`), `toolbar.h` (`ToolButton`, `ToolIconFn`, `setToggle`), `dialog.h`, `lang.h`, `menu.h`; FontKit
`uikitface.h` (`ft_uikit_install`, `ft_messagebox`); AppKit `appkit.h` (the `.ini` reader, `KEY_*`); FileKit
`filekit.h`, `fsutil.h` / `fsutil.inc` / `fsutil.cpp`, `filekit.abi`; SystemKit `clipboard.h`; `user/Makefile`
(`FT_APPS`, `FT_EXTRA_*`, `FK_OBJ`, the `filekit.so` rules); `tools/libgen/libgen.py`; `tools/docgen/kitdocs.py`;
`tools/lang/check.py`; `tools/pkg/packages.ini`, `versions.ini`; `sdcard/etc/fileassoc.ini`;
`tools/tests/desktop_sim/{shots.sh,run.sh,fakekapi.cpp}`, `tools/tests/run_notes_sim_test.sh`;
`docs/06-KITS-GUIDE.md` §7 and §13; round 1's `03-technical-analysis.md` and `06-development.md` (the host-only
test bench, `libgen --nm nm`, the `.abi` lines "to be confirmed by the user's `make`").

**Summary.** Circuits is an application only: **no kapi change, no kernel change, no new kit, `appkit.abi`
untouched.** One kit addition is decided (§3): a **sectioned key/value text document in FileKit**
(`filekit/kvtext.h`, `fk_kv_*`) that Circuits uses for both its packs and its `progress.ini`; Turtle Quest is not
touched this round (it can move onto it later). Everything else is beside the app: the engine
`user/Apps/circuits/circuit.h` / `circuit.cpp` (no UI, no file I/O), the lesson cards `lessons.h`, the window
`main.cpp`. All building and testing is on the host `g++` (no AArch64 compiler in this container): a host unit test
for the kit, one for the engine and the packs, a scripted simulator test, `shots.sh circuits`.

### Environment (checked in this container)

| Item | State |
|---|---|
| `aarch64-none-elf-g++` | **absent** — `make` / `make stage` for the Pi are deferred to the user (AC 23 is "written, not built here") |
| host `g++`, `python3` + Pillow + numpy | present |
| `sh tools/tests/run_turtle_test.sh` (baseline for AC 26) | **passes**: `ok turtle (27 levels: solved, written back; the errors)`, 43 s |
| `tools/tests/desktop_sim/fakekapi.cpp` | the stand-in kernel: `SIM`, `SIM_ARGS`, `SIM_WRITES`, `SIM_OVERLAY` (files only, not folder listings), `SIM_CLIPFILE`, script steps `down/up/move/rdown/rup/key/menu/drop/wait/dump/exit` |
| `shots.sh` | builds **every** app of its list whatever names are given (~4 min the first time — round 1's R7) |

---

## 1. What exists and is reused (by file)

### 1.1 The model: Turtle Quest

| File | What Circuits takes from it | How |
|---|---|---|
| `user/Apps/turtle/world.h` | the split engine / window; the pack syntax (`[pack]`, `[level]`, `key = value`, `key.fr`, `\|` continuation lines); `Level::titleOf (lang)` fallbacks; `par = <3★> <2★>` and the stars rule; the `Concept` table of lesson cards in EN + FR | **re-implemented** for circuits on the new FileKit reader (§3) — the turtle-specific keys (`map`, `words`, `draw`) do not apply; `world.h` is not included (it pulls Onyx BASIC) |
| `user/Apps/turtle/main.cpp` | the window's anatomy (levels list with stars, lesson card over the board, message bar with stars and a *Next level* button), `load_packs ()` (sorted `*.turtle` of the app's `levels/`), `onDrop` (`DND_FILES`), `kapi_get_args`, `m_open` (`uk_file_open` with `"name\|*.ext\|All files\|*"` filters), `build_menu` (`Menu::menu/item/separator`, `KEY_F1 + n`, `UK_CTRL ('N')`), the progress layout (`[Player]`, `<id>`, `<id>.code`, `seen.<concept>`, top keys `pack`, `level`) | the same patterns, written for Circuits; the progress through `fk_kv_*` instead of a third copy of `kv_*` |
| `tools/tests/turtle/turtletest.cpp`, `run_turtle_test.sh` | the host test's shape: packs given on the command line, every level's `solution` played and won with 3 stars, `CHECK (...)` macro, ASan/UBSan | copied as a pattern for `tools/tests/circuits/circuitstest.cpp` |
| `tools/tests/desktop_sim/shots.sh` (`if want turtle`) + `desktop_sim/turtle/*.ini` | a progress fixture copied into `$OUT/writes/apps/<app>.app/progress.ini`, `lang fr` for the French shot | the same for `circuits` |

### 1.2 The kits

| Kit / file | Used for |
|---|---|
| **AppKit** `appkit/appkit.h` | `kapi_opendir/readdir/closedir` (the `levels/` folder), `kapi_get_args` (`circuits <path>`), `KEY_F1` (0x110), `KEY_DEL` (0x108), `KEY_BACKSPACE`; the file reads/writes go through FileKit. **Its `.ini` reader is not used**: `INI_MAX` 64 entries, 64-char values, 2 KB, read-only (round 1 §1.3 measured the same limits). |
| **FileKit** `filekit/filekit.h` | `fk_load` (a pack opened, a dropped file), `fk_path_name`, `fk_path_ext`; **new**: `fk_kv_*` (§3). Already linked into every FT app (`lib/filekit.imp.a`). |
| **UIKit** `uikit/uikit.h` | `Root` (`onKey`, `onTick` ~60 Hz for the autosave pause, `onDrop`, `setResizable`, `setMinSize`, `fitWorkArea`), `Widget` subclasses for the custom views (`onDraw`, `onMouse (mx, my, bl, br, bm, wheel)`, `catchOutside`, `tip`), `VPath` (`poly`, `circle`, `hole`, `line`, `polyline`, `arc`, `fill`; 1/16 px, `V ()`), `uk_sin/uk_cos`, `ToolBar` / `ToolButton` (`setToggle`, an app `ToolIconFn` to draw the gate icons, `tip`), `Button`, `Label`, `Splitter` (if the UX wants one), `Menu`, `uk_file_open`, `uk_rbox`, `uk_text*`; **`lang.h`**: `TR`, `TRC`, `TRN`, `uk_lang_init`, `uk_lang ()`. **No UIKit change is needed.** |
| **FontKit** `fontkit/uikitface.h` | `ft_uikit_install ("DejaVu Sans", 13)` before `uk_lang_init ()` (Ledger's order), `ft_messagebox` (errors, About) so dialogs use the same face |
| **SystemKit** `systemkit/systemkit.h` | `clip_set_text` (Copy Truth Table, Copy Circuit); `clip_get_text` for the SHOULD *Paste Circuit*; the language comes through `uk_lang_init` (it reads `locale_language`) |
| **tools** | `tools/lang/check.py circuits` (AC 22), `tools/docgen/kitdocs.py` (docs/14 after the FileKit addition), `tools/libgen/libgen.py --nm nm` (the `.abi` lines from host objects, round 1's method), `tools/icons/*_icon.py` (Pillow → 40×40 BMP, magenta key, `gen_assets`) |

### 1.3 Build and packaging

- `user/Makefile`: Circuits is a plain FreeType app → **`FT_APPS += circuits`** and
  `FT_EXTRA_circuits = Apps/circuits/circuit.cpp` + a dependency line (Notes' pattern, lines 774–779). The generic
  `$(FT_APP_ELFS)` rule already links UIKit, FileKit, SystemKit, NetKit, FontKit.
- `sdcard/apps/<name>.app/app.txt` (`name`, `category`, `opens`, `stack`), `sdcard/etc/fileassoc.ini`
  (`turtle = turtle` at line 104: `circuits = circuits` beside it), `tools/pkg/packages.ini` (`[app.turtle]`'s
  `needs` / `opens`: `[app.circuits]` the same).

---

## 2. What is missing, and where it goes

| Missing | Where | Why there |
|---|---|---|
| A sectioned key/value **text document** that reads and writes: repeated sections, `\|` continuation lines with their line numbers, `\n`/`\\` escapes, unknown keys kept, whole-file save | **FileKit, new subject `filekit/kvtext.h`** (`fk_kv_*`) — §3 | needed by two apps already (Turtle's `kv_*` + `parse_pack`, now Circuits; Notes' `notes.ini` is a third reader): the kits-first rule forbids a third copy |
| The logic engine: parts, wires, evaluation (live, by depth), checking a truth table, stars, circuit text, undo history | `user/Apps/circuits/circuit.h` + `circuit.cpp` (engine, no UI, no file I/O) | one program uses it |
| Level packs: parsing / validating `*.circuits`, levels, packs, unlocking | same engine files (on `fk_kv`) | idem |
| Progress (`progress.ini`) as an object over an `fk_kv` document | same engine files | idem |
| The lesson cards (18 concepts, EN + FR, the gate drawn, its truth table) | `user/Apps/circuits/lessons.h` (data) | idem; compiled in as Turtle's |
| Gate symbols, board, levels list, objective/truth-table panel, message bar, lesson card | `user/Apps/circuits/main.cpp` (+ `board.h` / `gates.h` if the developer splits it) | idem |
| The three packs | `sdcard/apps/circuits.app/levels/{1-gates,2-combining,3-arithmetic}.circuits` | data |
| French UI words | `sdcard/apps/circuits.app/lang/fr.txt` | UIKit `lang.h` format |
| Icon | `sdcard/apps/circuits.app/icon.bmp` from `tools/icons/circuits_icon.py` | round 1's method |
| Tests | `tools/tests/filekit/kvtest.cpp` + `tools/tests/run_kvtext_test.sh`; `tools/tests/circuits/circuitstest.cpp` + `tools/tests/run_circuits_test.sh`; `tools/tests/run_circuits_sim_test.sh`; `tools/tests/desktop_sim/circuits/*.ini` + `tools/tests/desktop_sim/sd/docs/circuits/extra.circuits` (fixtures); a `circuits` block in `shots.sh` | |

**Not needed / not done:** no UIKit widget is missing (the board, the lists and the table are owner-drawn `Widget`s,
as Turtle Quest's). The stars are a 10-point `VPath` polygon drawn by Circuits (as Turtle draws its own); a shared
`WKT_STAR` glyph in UIKit would be a clean-up for both games later, not this round. No new kapi call: files are
read through `fk_load` / `kapi_opendir`, written through `fk_kv_save` (FileKit → AppKit's `kapi_save_file`).

### 2.1 kapi changes

**None.** `kernel/include/kern/kapi_abi.h`, `kernel/sys/kapi*.cpp`, `user/Kits/appkit/appkit.h`,
`appkit_calls.inc` and `appkit.abi` stay untouched (AC 24 checks it with `git diff --stat origin/main -- …`).

---

## 3. Decision on the PM's open question: the shared progress store / pack parser

**Decided: one new FileKit subject, `filekit/kvtext.h` — a sectioned key/value text document — used by Circuits
for its packs *and* its progress. Turtle Quest is not changed this round.**

Why a kit, why FileKit, why this shape:

- Turtle Quest has a private `kv_*` store (main.cpp 43–123) and a private `parse_pack` (world.h 255–337); Notes has
  its own `notes.ini` reader. Circuits needs both functions again — a third and fourth copy is what the kits-first
  rule forbids ("reusable code goes into the adequate kit — not a copy in an app").
- **Not AppKit**, although `.ini` is in its description: AppKit's reader is a fixed static store inside `appkit.so`
  (the library the kernel loads into *every* program; its limits are part of its ABI) and AC 24 asks for
  `appkit.abi` untouched. A growable document with write-back is a file-format helper: **FileKit** ("files"; it
  already has `fsutil.h`'s helpers) is the right place, and its `fsutil.h` pattern gives exactly what the test bench
  needs — **inline code on the PC, the library's code on the Pi** — so no host build script changes.
- One reader serves both files because they are the same syntax: `[section]` headers, `key = value`, `#`/`;`
  comments; packs add **repeated sections** (many `[level]`) and **`|` continuation lines** with **line numbers**
  for the error messages (AC 12); `progress.ini` adds `\n` / `\\` **escapes** and **write-back keeping unknown
  keys** (02 §7.2). The `.fr` suffix stays the caller's business (a key like any other).
- Turtle Quest is left as it is (02 §5 and AC 26: its behaviour unchanged; touching it costs a re-test of its 27
  levels and screenshots for no user-visible gain). Migrating Turtle's `kv_*` and `parse_pack`, and Notes'
  `notes.ini`, onto `fk_kv` is written in `docs/HANDOFF.md` as a follow-up.

### 3.1 The API (`user/Kits/filekit/kvtext.h`, C-compatible, integer only)

```c
// kvtext.h -- FileKit: a text document of [section] headers and "key = value" lines, read and written
// back (a game's progress, a level pack, a settings file bigger than AppKit's .ini reader takes).
typedef struct fk_kv fk_kv;
#define FK_KV_ESCAPES  1   // values may hold \n and \ : written "\n", "\\", read back (progress.ini)
#define FK_KV_PIPES    2   // a value may go on in the lines that follow, each starting with "|" (packs)
fk_kv      *fk_kv_new (int flags);                                   // empty; 0: no memory
fk_kv      *fk_kv_parse (const char *text, int flags);               // never fails on syntax (lines without '=' skipped)
fk_kv      *fk_kv_load (const char *path, int flags);                // 0: no such file / unreadable
void        fk_kv_free (fk_kv *kv);
int         fk_kv_count (const fk_kv *kv);                           // the entries, in the file's order
const char *fk_kv_key (const fk_kv *kv, int i);                      // "" out of range
const char *fk_kv_value (const fk_kv *kv, int i);                    // unescaped / "|" lines joined by '\n'
const char *fk_kv_section (const fk_kv *kv, int i);                  // "" before any header
int         fk_kv_block (const fk_kv *kv, int i);                    // 0 before any header, then 1, 2...: one per "[...]" line
int         fk_kv_line (const fk_kv *kv, int i);                     // 1-based line of the value's FIRST line (the key's
                                                                     //  line, or its first "|" line when "key =" is empty):
                                                                     //  the value's line j is at fk_kv_line + j
int         fk_kv_blocks (const fk_kv *kv);                          // the "[...]" headers read / made
const char *fk_kv_block_name (const fk_kv *kv, int b);               // "level" (b 1-based; "" for 0)
int         fk_kv_block_line (const fk_kv *kv, int b);               // its header's line (0: made, not read)
const char *fk_kv_get (const fk_kv *kv, const char *section, const char *key, const char *def);  // the first match
int         fk_kv_set (fk_kv *kv, const char *section, const char *key, const char *value);   // replaces the first
                                                                     //  match, else appended to that section's first block
                                                                     //  (a new block at the end if none) -> 0 / -1 no memory
int         fk_kv_remove (fk_kv *kv, const char *section, const char *key);                    // 1 removed / 0
// The document as text: an optional first comment line ("# ..."), the entries before any header, then each
// block "[name]" with its entries (ESCAPES: values escaped; PIPES: a multi-line value as "key =" + "| " lines).
// The pointer is the document's, valid until its next change / free; *len its length (may be 0).
const char *fk_kv_text (fk_kv *kv, const char *comment, int *len);
int         fk_kv_save (fk_kv *kv, const char *path, const char *comment);                    // 0 / -1
```

Semantics fixed here (the test checks them): keys and section names trimmed; `key.fr` is a plain key; a blank or
`#` line ends a `|` block; a `\r` before `\n` is dropped; with `FK_KV_PIPES` a `|` line with nothing before it is
ignored; lookups are case-sensitive; no limit on sizes but memory; the text written then parsed gives the same
entries (round-trip). Implementation: `kvtext.inc` (valid C **and** C++, `static inline` helpers named `fkkv_*_`,
`malloc`/`realloc`/`free`; load through `kapi_open`/`kapi_fsize`/`kapi_read`, save through `kapi_save_file` — as
`fsutil.inc` reaches AppKit), compiled into the library by `kvtext.cpp` (`#define FK_KV_IMPL`), inline on the PC:

```c
#if (defined (__aarch64__) && !defined (FK_KV_INLINE)) || defined (FK_KV_IMPL)   // (the library's build, on the Pi or the PC)
#  ifdef __cplusplus
#    define FK_KV_API extern "C"
#  else
#    define FK_KV_API extern
#  endif
#else
#  define FK_KV_API static inline
#endif
...
#if (!defined (__aarch64__) || defined (FK_KV_INLINE)) && !defined (FK_KV_IMPL)
#include "kvtext.inc"
#endif
```

`filekit.h` includes `kvtext.h` after its `extern "C"` block (for C and C++). `user/Makefile`:
`FK_OBJ += lib/fk/kvtext.o` and its rule (copy `lib/fk/fsutil.o`'s). The export regex `'^(fk_|fs_)'` already takes
`fk_kv_*`. **`filekit.abi`**: the 18 names appended **after slot 77, sorted** (what libgen does): obtained by
`tools/libgen/libgen.py --nm nm` on the host-built `kvtext.o` + `fsutil.o` + `fkcore.o` against a copy of the
current `.abi` (round 1's method, 06 §step 1) — to be confirmed by the user's `make` on the Pi. The table grows from
78 to 96 entries → `[app.circuits] needs = filekit >= 1.96` (to be confirmed at publish, as round 1's `needs`);
`tools/pkg/versions.ini` is **not** edited (publishing raises it — PIPELINE §0.5). `kitdocs.py`: add
`"filekit/kvtext.h"` to FileKit's header list → `docs/14-FILEKIT.md` regenerated; `docs/06` §7 gains a short
example and §14's quick reference a row ("read and write a settings / progress file with sections → FileKit →
`fk_kv_load`, `fk_kv_set`, `fk_kv_save`").

---

## 4. The engine (`user/Apps/circuits/circuit.h` + `circuit.cpp`)

Constraints: compiles with the host `g++ -std=c++17` (tests, simulator) **and** the newlib AArch64 C++ build
(`-fno-exceptions -fno-rtti`, no STL — fixed arrays and Turtle-style `Arr<T>` if needed); no UIKit, no AppKit
calls, **no file I/O** (text in, text out; the window loads/saves). Namespace `circuits`. MIT notice. The FPU is
available to FT apps but the engine needs none.

Sketch (names may change; the semantics may not):

```cpp
enum PartType { P_SWITCH, P_LAMP, P_NOT, P_AND, P_OR, P_XOR, P_NAND, P_NOR, P_COUNT_ };   // (SHOULD: P_CHIP after)
enum { MAXIO = 4, MAXGATES = 48, MAXPARTS = 2 * MAXIO + MAXGATES, MAXROWS = 16 };
enum { BOARD_W = 40, BOARD_H = 24 };          // the grid, in cells (the UX may change the numbers, not the idea)
enum Err { E_OK, E_NOT_ALLOWED, E_FULL, E_OVERLAP, E_OUTSIDE, E_FIXED, E_LOOP, E_PIN, E_SAME,
           E_LAMP_OPEN, E_GATE_OPEN, E_SYNTAX, E_NAME, E_TYPE, E_DOUBLE };   // + a message EN/FR per code
enum Lv { L0 = 0, L1 = 1, LX = 2 };           // a signal: 0, 1, unknown (step mode) 
struct Part { char name[9]; unsigned char type; short x, y; short in[2]; };      // in[k]: the source part, -1 none
struct Circuit
{
    Part p[MAXPARTS]; int n, ni, no;          // parts [0, ni) switches, [ni, ni + no) lamps, then the gates
    void setup (const Level &L);              // the level's switches (left column) and lamps (right column), fixed
    int  add (int type, int x, int y, const Level &L, Err *e);    // -> part index, -1 (not allowed, 49th gate, overlap, outside)
    Err  move (int i, int x, int y);          // E_FIXED for a switch / lamp, E_OVERLAP, E_OUTSIDE
    Err  connect (int src, int dst, int pin); // replaces a wire already there; E_LOOP, E_PIN, E_SAME
    void disconnect (int dst, int pin);  void remove (int i);  void clear ();   // remove: its wires too
    int  gates () const;                      // NOT..NOR on the board (unused ones count)
    bool upstream (int a, int b) const;       // does a depend on b (the loop test)
    int  pins (int i) const;                  // inputs of part i: NOT 1, gates 2, lamp 1, switch 0
    // geometry (cells): footprint, input pin k / output pin position, a wire's orthogonal route (for drawing and
    // for hit-testing a click on a wire), the part under a cell
    void box (int i, int &x, int &y, int &w, int &h) const;
    void pinIn (int i, int k, int &x, int &y) const;  void pinOut (int i, int &x, int &y) const;
    int  route (int dst, int pin, int *xy, int cap) const;       // the polyline's points
    int  at (int cx, int cy) const;
};
struct Eval { unsigned char v[MAXPARTS]; bool det[MAXPARTS]; int depth[MAXPARTS]; int maxDepth; };
void evaluate (const Circuit &c, unsigned inputs, Eval &e);              // live: an open input counts 0, det = false downstream
void evaluate_to (const Circuit &c, unsigned inputs, int step, Eval &e); // step mode: gates deeper than step -> LX
struct CheckResult { Err err; int errPart; unsigned got[MAXIO]; unsigned wrong; int nwrong, firstWrong, gates, stars; bool won; };
CheckResult check (const Circuit &c, const Level &L);                    // every row; refuses an open lamp / gate input first
int  stars_for (int gates, int par3, int par2, bool won);                // 0 not won; 3 <= par3; 2 <= par2; else 1
// circuit text (02 §7.3): canonical write (parts in creation order with x y, then wires: for each part, its pins)
int  write_text (const Circuit &c, char *out, int cap);                  // -> length (cap: 8 KB is ample for 48 gates)
bool read_text (Circuit &c, const Level &L, const char *text, Err *e, int *line);   // positions optional: auto-placed by depth
struct History { /* 64 + 1 snapshots of write_text, a ring; push (c), undo (c, L), redo (c, L), canUndo (), canRedo () */ };
int  truth_table_text (const Level &L, const CheckResult *r, char *out, int cap);   // AC 16 (tab-separated)
```

Levels and packs: `struct Level` (`id[24]`, `title/text/hint[2][…]`, `concept[16]`, `ninputs`, `noutputs`,
`inName[4][5]`, `outName[4][5]`, `parts` bit mask, `par3`, `par2`, `want[MAXIO]` (bit r = the output's value on row
r), `solution` (malloc'd text), `chips[...]` (SHOULD)) with `titleOf (lang)` fallbacks; `struct Pack` (`title[2]`,
`path`, `opened` (from a file: all its levels open), levels). `bool parse_pack (Pack &, const char *text, char *why,
int cap)` = `fk_kv_parse (text, FK_KV_PIPES)` then a walk over blocks: `[pack]` keys, each `[level]` block checked —
required keys (error at the block's header line), `inputs`/`outputs` 1–4 names ≤ 4 chars, `parts` names, `par` two
numbers with 3★ ≤ 2★, `table` header names equal to inputs/outputs, exactly 2^n rows, rows in counting order, 0/1
values (error at the row's own line = `fk_kv_line + j`), unknown keys ignored; `id` unique across the loaded packs
(checked when a pack is added: `bool ids_unique (Pack **, int n, char *why)`).

Progress: `struct Progress { fk_kv *kv; … }` over an `fk_kv` with `FK_KV_ESCAPES` — `stars (id)`, `record (id,
stars)` (never lowers), `circuit (id)` / `setCircuit (id, text)`, `seen (concept)` / `setSeen`, `isOpen (id)` /
`open (id)` (the top key `open = id id …`; a missing file → only the first level open), `lastPack/lastLevel`;
`void unlock_after (Progress &, Pack **packs, int n, int p, int l)` (the next level, across built-in packs in
file-name order). The window does `fk_kv_load (PROGRESS, FK_KV_ESCAPES)` (0 → `fk_kv_new`) and
`fk_kv_save (kv, PROGRESS, "# Circuits -- the player's progress (written by the game)")`.

Lessons: `lessons.h` — `struct Lesson { const char *key; int gate; const char *title[2]; const char *text[2]; }` for
the 18 concepts of 02 §4.15, `find_lesson (key)`; the card's truth table is computed from `gate` by the window.
(Compiled in both languages as Turtle's, chosen by `!strcmp (uk_lang (), "fr")`; they are not `TR ()` words, so
`check.py` does not see them — the host test checks both texts are present.)

---

## 5. Technical constraints for the window (the UX Designer decides the layout and the looks)

- **One `Root` window**, resizable, a minimum size the board fits in (Turtle Quest uses `setMinSize (920, 560)`);
  `fitWorkArea ()` at start. Text through FontKit: `ft_uikit_install ("DejaVu Sans", 13)` **then**
  `uk_lang_init ()` **then** the widgets (the language is fixed for the run). Dialogs through `ft_messagebox`,
  `uk_file_open (path, cap, "SD:/docs/circuits", TR ("Circuits levels|*.circuits|All files|*"))`.
- **Custom widgets** are `Widget` subclasses beside the app (`onDraw` into their own `canvas`, `onMouse (mx, my,
  bl, br, bm, wheel)`; `mx < 0` = the pointer left). **No virtual may be added to a UIKit class** and no UIKit change
  is planned; a widget that must follow a drag past its edge sets `catchOutside` while the button is held.
- **Drag from the palette to the board**: UIKit has no in-window drag object — a press on a palette `ToolButton`
  arms the part; the board receives the `move`/`up` events with the button held (routing goes to the widget under
  the pointer) and places the part on release. Click-then-click works the same way. (The system's DND is only for
  files: `onDrop` with `DND_FILES` opens a pack.)
- **Keys** in `Root::onKey`: F5 = `KEY_F1 + 4` (0x114), F7 = 0x116, F8 = 0x117, F9 = 0x118, F1 = 0x110, F2 = 0x111,
  `UK_CTRL ('Z')`, `UK_CTRL ('Y')`, `'N'`, `'P'`, `'O'`, `'Q'`, `KEY_DEL` (0x108), `KEY_BACKSPACE` (8), Esc (27)
  cancels a wire being drawn. Menus with `Menu::item (label, shortcut text, key, fn)` as Turtle's `build_menu`.
- **Drawing**: `VPath` coordinates are integers in 1/16 px; there is no Bézier — the OR/XOR curves are built as
  polygons from `arc ()` or from points computed with `uk_sin`/`uk_cos` (integer). Gates, switches, lamps, wires
  (level colours: lit / dark / grey for undetermined), selection frame, red outline on a refused part, the wrong
  rows of the table. The board's cell size in pixels is the UX's choice (constant or fitted to the widget).
- **Board model ↔ screen**: everything the engine needs is in cells (`Circuit::box`, `pinIn`, `pinOut`, `route`,
  `at`); hit tests on pins and wires are done in cells / pixels by the window from those.
- **Autosave**: after each board change set a "dirty" time; `onTick` saves `<id>.circuit` (+ the undo history is
  *not* saved) after ~1 s of quiet, and on level change / quit (`root.run ()` returning) — Notes' and Turtle's way.
- **Strings**: every UI word in `TR ("...")` / `TRC (...)` (a table of English words: `TRN`); the levels' texts
  through `Level::titleOf (lang)` etc.; engine error messages: an `Err` → English text in a `TRN` table in
  `main.cpp`, translated with `TR` when shown (the engine itself stays language-free).
- **Clipboard**: `clip_set_text` (`systemkit/systemkit.h`).
- **Arguments**: `kapi_get_args (buf, cap)`; a `.circuits` path → added and shown (all its levels open).

---

## 6. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | No AArch64 compiler: `make`, `make stage`, libgen's real `.abi` check and the Pi run cannot be done here | host builds of everything (engine, kit, window); the `.abi` lines from `libgen --nm nm` on host objects; the Makefile rule written after the Notes model; AC 23 stays partly with the user — said in 06 and the report |
| R2 | FileKit is shared by all apps: a bug in `fk_kv_*` can only reach programs that call it (new functions) | append-only `.abi`; nothing existing changes; own unit test with ASan/UBSan |
| R3 | `needs = filekit >= 1.96` and the `.abi` slots are only confirmed by the Pi build / publish | written as "to be confirmed" in packages.ini's comment, as round 1 |
| R4 | `shots.sh` builds every app (~4 min) and rewrites any PNG asked | run `shots.sh circuits` only; commit only `circuits*.png`; `turtle` re-run once for AC 26 |
| R5 | Mouse: no capture in UIKit | §5 (arm on press, place on release; `catchOutside` for drags leaving the board) |
| R6 | Curved gate shapes with integer `VPath` only | polygons from `arc ()`/`uk_sin`; looked at in the screenshots |
| R7 | The simulator lists folders of the card but not of `SIM_OVERLAY` | built-in packs are read from `sdcard/apps/circuits.app/levels/` (the card in the repo); the extra pack of AC 20 is a *file* in the overlay (`SD:/docs/circuits/extra.circuits`) — files are found there |
| R8 | 3.4's minimum (7) is not proven (02 §6.4) | the test checks reference = the Min column (7) and 3 stars, not minimality |
| R9 | Long values in `progress.ini` (48-gate circuits ~1.5 KB each) | no limit in `fk_kv`; 20 levels ≪ 64 KB |
| R10 | `circuits.app/main` cannot be staged here | the folder has `app.txt`, `icon.bmp`, `levels/`, `lang/` committed; `main` comes from the user's `make stage` |
| R11 | Temptation to refactor Turtle onto `fk_kv` | forbidden this round (AC 26); HANDOFF note |
| R12 | A combinational loop through a later edit (move does not wire; only `connect` can) | `connect` is the only place a wire is made — checked there; `read_text` uses `connect` too |

---

## 7. Implementation plan (numbered; each step ends testable)

Conventions: run everything from `/home/user/onyx`; every new source file carries the MIT notice; commit after each
step (`AutoDev` only). `T=${TMPDIR:-/tmp}`.

**Step 0 — baseline.** `sh tools/tests/run_turtle_test.sh` (passes: 27 levels), `python3 tools/docgen/kitdocs.py &&
git status --short docs/` (no diff expected). Keep a copy of `user/Kits/filekit/filekit.abi`.

**Step 1 — FileKit `fk_kv_*` (§3).**
Create `user/Kits/filekit/kvtext.h`, `kvtext.inc`, `kvtext.cpp`; include `kvtext.h` at the end of `filekit.h`
(both languages); `user/Makefile`: `FK_OBJ += lib/fk/kvtext.o` + its rule. Test: `tools/tests/filekit/kvtest.cpp`
+ `tools/tests/run_kvtext_test.sh` — parse/get/set/remove, repeated `[level]` blocks with `fk_kv_block`,
`fk_kv_line` of keys and of `|` values (value line j), escapes round-trip (`a\nb\\c`), unknown keys kept on
write-back, `fk_kv_text` → `fk_kv_parse` identical entries, comment line, `\r\n` input, empty / missing file
(`fk_kv_load` → 0), save/load through the simulator's kapi (`fakekapi.cpp`, `SIM_WRITES=$T/kv_w`). Also compile
`kvtext.inc` as C (`gcc -std=c99 -fsyntax-only -x c` on a 3-line file including `filekit/kvtext.h`).
Then `.abi`: compile `kvtext.cpp` (it defines `FK_KV_IMPL`, which makes the header's `FK_KV_API` `extern "C"` on
the host too — see the macro in §3.1), `fsutil.cpp` and `fkcore.cpp` for the host (`-c`, the include paths of the
tests; a shim for `fsutil.cpp` if its `FS_API` clashes on the host, as round 1 did for SystemKit), run
`python3 tools/libgen/libgen.py --nm nm --name filekit --abi <copy of filekit.abi> --init fk_lib_init --allow-data .
--export '^(fk_|fs_)' --table $T/t.S --stubs $T/s.S --bind $T/b.cpp <objects>` and append exactly the new lines
(78…95, sorted) to `user/Kits/filekit/filekit.abi`. `tools/docgen/kitdocs.py`: `"filekit/kvtext.h"` in FileKit's
list → run it (`docs/14-FILEKIT.md` changes: the new section only). `docs/06` §7 example + §14 row.
*Checks*: `sh tools/tests/run_kvtext_test.sh` → `ok kvtext (N checks)`.

**Step 2 — the engine core (`circuit.h`, `circuit.cpp`): parts, wires, evaluation, check, text, history.**
Everything in §4 except packs/progress. Test: `tools/tests/circuits/circuitstest.cpp` part A (with an inline
`Level` built in the test): AC 4 (stars 3/2/1/0 for `par = 3 5`), AC 5 (`xor` with OR wrong only on row `11`;
`and` with OR wrong on `01`, `10`), AC 6 (open lamp → `E_LAMP_OPEN` naming it; open gate input → `E_GATE_OPEN`,
nothing recorded), AC 7 (self-loop and `g1→g2→g1.1` refused, circuit text unchanged), AC 8 (second wire replaces,
wire count unchanged; one output into 3 inputs accepted), AC 9 (type not allowed refused; NOT 1 pin, others 2; 49th
gate refused), AC 10 (full adder: `maxDepth == 3`; after step 1 only depth-1 gates known; step 3 = live, for all 8
rows), AC 11 (write → read → write identical; unknown part / missing pin / two wires into one input / loop refused
with their line), AC 15 (place, wire, move, delete, clear each undone and redone by circuit text; 64 steps kept, the
65th drops the oldest), AC 16 (`half` table text: `A\tB\tC\tS\n` + 4 rows; after a Check the obtained columns
`C'`, `S'` added — the header suffix `'` is the decided convention).
Script `tools/tests/run_circuits_test.sh` (packs given as arguments, as Turtle's):
```
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I user -I user/Kits -I user/Runtime \
    -I user/Include -I user/Libs -I user/Apps -I kernel/include \
    tools/tests/circuits/circuitstest.cpp user/Apps/circuits/circuit.cpp -o $T/onyx_circuits_test
$T/onyx_circuits_test sdcard/apps/circuits.app/levels/*.circuits
```
(no fakekapi: the engine does no I/O.) *Check*: `ok circuits (…)`, no sanitizer report, no warning in
`user/Apps/circuits/*`.

**Step 3 — packs, progress, lessons; the 20 missions.**
`parse_pack`, `ids_unique`, `Progress`, `unlock_after` in the engine; `lessons.h`; the three packs
`sdcard/apps/circuits.app/levels/1-gates.circuits` (8), `2-combining.circuits` (6), `3-arithmetic.circuits` (6)
exactly as 02 §6 (titles, FR texts, hints, concepts, parts, par = Min/★★, tables, reference solutions with
positions). Test part B: AC 1 (3 packs, 8 + 6 + 6, ids unique, order of §6), AC 2 (each table equals a C++ lambda
per level, every row), AC 3 (each solution parses, uses only the level's parts, wins on every row with 3 stars, gate
count == Min: 0,1,1,1,2,2,2,3 / 3,3,4,2,4,3 / 2,2,5,7,4,4), AC 12 (bad packs as strings in the test: wrong row count,
rows out of order, missing `inputs`, 5 inputs → refused with the right line; an unknown key → loads), AC 13
(Progress round-trip through `fk_kv_text`/`fk_kv_parse`: stars, multi-line `<id>.circuit`, `seen.*`, `open`,
`pack`/`level`; empty → only `wire` open; an unknown key kept), AC 14 (win k opens k+1; last of pack 1 opens
`nandor`; better result raises, worse never lowers), AC 22 second half (every level has `title.fr` and `text.fr`;
every `concept` used has a lesson with EN and FR texts).
*Check*: `sh tools/tests/run_circuits_test.sh` → all green.

**Step 4 — the window, static.** `user/Apps/circuits/main.cpp` (+ `board.h`, `gates.h` if split): faces, language,
packs loaded (`kapi_opendir` of `SD:/apps/circuits.app/levels`, sorted), progress loaded, argument, the levels list,
objective + truth-table panel, palette (`ToolBar` of the level's parts + Select), board drawing (switches, lamps,
the gate symbols, wires coloured by a live `evaluate`), message bar, lesson card, menus (Game / Edit / Simulate /
Levels / Help as 02 §4 and the UX's plan), About. `user/Makefile`: `circuits` in `FT_APPS`,
`FT_EXTRA_circuits = Apps/circuits/circuit.cpp`, a dependency line
`circuits.elf: Apps/circuits/circuit.cpp $(wildcard Apps/circuits/*.h) Kits/filekit/kvtext.h Kits/filekit/kvtext.inc`.
`shots.sh`: `circuits` in `APPS` and in the FT list, `extra=user/Apps/circuits/circuit.cpp` (as `notes`).
*Check*: build as `run_circuits_sim_test.sh` will (below) with `-Wall -Wextra` on `main.cpp` (no warning), one run
`SIM="wait;wait;wait;dump $T/c0.elsm;exit"` → `python3 tools/tests/desktop_sim/shot.py $T/c0.elsm $T/c0.png`
looked at.

**Step 5 — the window, interactive.** Placing (click-click and drag from the palette), wiring (drag pin → pin,
replace, loop refusal message 02 §4.6), select / move / Delete / Backspace / right click, Clear Board, Restart
Level, Undo / Redo (`History`), live switches, step mode (F8, F9, F7, any change leaves it), Check (F5 + the
button: open-input refusal with the red outline, wrong rows marked, first wrong row set on the switches, stars,
"Well done! N gates."), progress recorded + next level opened + autosave (`onTick`) + save on level change and quit,
lesson card first time (`seen.*`) and F1, hint F2, Next / Previous (Ctrl+N / Ctrl+P), Open Level Pack (Ctrl+O,
drop, argument; malformed → `ft_messagebox` with file and line, nothing added), Copy Truth Table / Copy Circuit,
last level shown at start. *Check*: step 7's sim test cases written alongside, run as they become possible.

**Step 6 — French.** Every UI word through `TR`/`TRC`/`TRN`; `python3 tools/lang/check.py --keys circuits` to start
`sdcard/apps/circuits.app/lang/fr.txt`, then translate (the loop message of 02 §4.6 exactly). *Check*:
`python3 tools/lang/check.py circuits` → exit 0, no missing word, no unused line (AC 22 first half).

**Step 7 — scripted simulator test `tools/tests/run_circuits_sim_test.sh`** (Notes' `run_notes_sim_test.sh` as the
model: UIKit/FreeType/fakekapi built once into `$T/onyx_circuits_sim`, `main.cpp` built with `-Wall -Wextra` (no
warning), each case its own `SIM_WRITES`, assertions on the written `apps/circuits.app/progress.ini`, on the log, on
`SIM_CLIPFILE`, and on dump pixels via a small python helper (`shot.py` then numpy on known coordinates the developer
derives from the layout)). Cases: AC 18 (no `progress.ini` → the dump shows the lesson card; then OK and a dump: 1.1
open, the others padlocked — pixel check on the padlock/star column; with fixture `tools/tests/desktop_sim/circuits/
stars.ini` → the stars drawn match it), AC 19 (level `and` from a fixture with its solution saved: dump, click switch
A and B, dump → the lamp pixel goes from dark to lit, and a wire pixel changes), AC 20 (`SIM_ARGS=SD:/docs/circuits/
extra.circuits` from `tools/tests/desktop_sim/sd/docs/circuits/extra.circuits` → its title in the list, its first
level shown, its levels open), AC 21 (on `wire`: drag A's pin to Out's pin, F5 → `progress.ini` has `wire = 3`,
`wire.circuit = wire A Out`, `open` holds `not`), AC 16 in the app (Copy Truth Table → `SIM_CLIPFILE` content), the
loop refusal message shown (log/dump). One line a case `ok …` / `FAIL …`, exit 1 on a failure.
*Check*: `sh tools/tests/run_circuits_sim_test.sh` → `circuits-sim: all N checks passed`.

**Step 8 — the card and the package (declared, not published).** `sdcard/apps/circuits.app/app.txt`
(`name = Circuits`, `category = Programming`, `opens = circuits`, `stack = 4M`), `tools/icons/circuits_icon.py` →
`sdcard/apps/circuits.app/icon.bmp` (a gate with a lit wire), `sdcard/etc/fileassoc.ini`: `# Circuits` +
`circuits = circuits` after Turtle Quest's lines, `tools/pkg/packages.ini`: `[app.circuits]` after `[app.turtle]`
with `needs = uikit >= 1.779, systemkit >= 1.74, filekit >= 1.96` and `opens = circuits` (+ a comment: the filekit
version to confirm at publish). No `publish.sh`, no `versions.ini`, no `sdcard/var/pkg/db`. *Check*:
`grep -n circuits sdcard/etc/fileassoc.ini tools/pkg/packages.ini`; optionally round 1's dry run
`python3 tools/pkg/mkrepo.py --out $T/repo --no-sign --versions <copy of versions.ini> --bump` (nothing pushed).

**Step 9 — screenshots.** Fixtures `tools/tests/desktop_sim/circuits/solving.ini` (a world-3 level, e.g. `full`,
half-built, saved circuit with positions) and `check.ini` (level `xor`, board `OR(A,B) → Out`); a `if want circuits`
block in `shots.sh` after Turtle's: `circuits.png` (solving.ini, a switch or two toggled), `circuits-check.png`
(check.ini, `key 0x114` → "1 row is wrong."), `circuits-fr.png` (`lang fr`, same as the first); `rm -rf` the app's
writes folder after. *Check*: `sh tools/tests/desktop_sim/shots.sh circuits` → the three PNGs; looked at; only
these committed (AC 17).

**Step 10 — documentation.** `docs/04-USER-GUIDE.md`: §12 catalog row (Programming, beside Turtle Quest), a §13
section *Circuits* (the board, the parts, wiring, live / step / check, stars, the three worlds, menus and keys, the
files read/written, the three screenshots), the Language & Region row's translated-apps list (+ Circuits);
`docs/03-DEVELOPER-GUIDE.md`: the translated-apps sentence (line ~2203) and a Circuits paragraph beside Turtle
Quest's (line ~3931: engine / window split, `fk_kv`, the tests); `docs/06` (step 1); `docs/HANDOFF.md`: a Circuits
section (state, files, tests, what is left: SHOULD items, sequential logic, Turtle/Notes onto `fk_kv`, the Pi build
to confirm `filekit.abi` 78–95 and `needs`). Then `python3 docs/build_docs.py` (pandoc; the PDF step may be missing
here — say so). *Check*: the exports regenerated (or the reason they are not).

**Step 11 — regression and rules (AC 24, 26).** `sh tools/tests/run_turtle_test.sh` (unchanged result);
`sh tools/tests/desktop_sim/shots.sh turtle` (its PNGs byte-identical or only pre-existing drift, restored);
`git diff --stat origin/main -- kernel/include/kern/kapi_abi.h kernel/sys user/Kits/appkit user/Apps/turtle` →
empty; `grep -L "MIT License" user/Apps/circuits/* user/Kits/filekit/kvtext.* tools/tests/circuits/* tools/tests/filekit/*
tools/icons/circuits_icon.py` → nothing; `grep -n "kapi_" user/Apps/circuits/*` limited to `kapi_opendir/readdir/
closedir/get_args` (AppKit, allowed); `run_kvtext_test.sh`, `run_circuits_test.sh`, `run_circuits_sim_test.sh` green.

**Step 12 (SHOULD, only if time remains, in this order).** (a) *Paste Circuit* — `clip_get_text` + `read_text`
(the engine already refuses parts not allowed) + a sim case; (b) *chips*: `P_CHIP` with a sub-circuit from
`Progress::circuit ("half"/"full")`, `chips =` key, `chipcount` pack key, evaluation by the sub-circuit, test with
3.3/3.4 solved by chips; (c) sandbox and `.circuit` files (+ `circuit = circuits` association); (d) several players.
Each with its test cases; docs/04 updated for what is built.

---

## 8. Tests — summary and exact commands

| Test | Command | Covers |
|---|---|---|
| FileKit kv unit test (host, ASan/UBSan, fakekapi for load/save) | `sh tools/tests/run_kvtext_test.sh` | §3; AC 13's storage layer |
| Engine + packs unit test (host, ASan/UBSan, no I/O) | `sh tools/tests/run_circuits_test.sh` (→ `g++ … circuitstest.cpp circuit.cpp`, run on `sdcard/apps/circuits.app/levels/*.circuits`) | AC 1–16, 22 (b) |
| Language catalogue | `python3 tools/lang/check.py circuits` | AC 22 (a) |
| Simulator scenario (scripted, asserted) | `sh tools/tests/run_circuits_sim_test.sh` | AC 16 (in app), 18–21 |
| Screenshots | `sh tools/tests/desktop_sim/shots.sh circuits` | AC 17 |
| Kit docs | `python3 tools/docgen/kitdocs.py` then `git diff --stat docs/` (only docs/14 + what was intended) | AC 24 (kits documented) |
| Exports | `python3 docs/build_docs.py` | AC 25 |
| Regression | `sh tools/tests/run_turtle_test.sh`; `sh tools/tests/desktop_sim/shots.sh turtle`; the `git diff` / `grep` of step 11 | AC 24, 26 |
| Pi (deferred to the user) | from `kernel/`: `make` (libgen confirms `filekit.abi` 78–95; `circuits.elf` built), `make stage` (`SD:/apps/circuits.app/main`) | AC 23 (rest) |

## 9. Acceptance criteria → steps

| AC | Step(s) | AC | Step(s) |
|---|---|---|---|
| 1 packs load, 20 levels | 3 | 14 unlocking, stars never lower | 3 (engine), 5 (window) |
| 2 tables = objectives | 3 | 15 undo / redo 64 | 2 (engine), 5 (window) |
| 3 solutions win, 3★, = Min | 3 | 16 Copy Truth Table text | 2 (engine), 5 + 7 (clipboard in the app) |
| 4 stars by count | 2 | 17 three screenshots | 9 |
| 5 wrong rows | 2 | 18 fresh start / fixture stars | 4, 5, 7 |
| 6 incomplete board refused | 2, 5 | 19 live switch → lamp | 5, 7, 9 |
| 7 loops refused | 2, 5 | 20 `circuits <pack>` | 5, 7 |
| 8 one input one wire, fan-out | 2 | 21 progress after a win | 3, 5, 7 |
| 9 parts allowed, pins, 48 max | 2, 4 | 22 French complete, `.fr` keys | 6 (a), 3 (b) |
| 10 step by step depths | 2, 5 | 23 Makefile / card / fileassoc / package | 4, 8 (Pi build: user) |
| 11 circuit text round-trip, errors | 2 | 24 MIT, no kapi, kits only | 1, 11 |
| 12 pack errors with lines | 1 (`fk_kv_line`), 3 | 25 docs, build_docs | 1 (docs/06, 14), 10 |
| 13 progress round-trip | 1, 3 | 26 Turtle unchanged | 0, 11 |

The UX Designer appends the **GUI plan** below (the window's layout, widgets by UIKit class, menus, states, mockups)
and amends steps 4, 5, 9 accordingly; the constraints of §5 hold.
