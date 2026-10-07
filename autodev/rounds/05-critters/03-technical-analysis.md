# AutoDev round 5 — Technical Analyst: Critters

Date: 2026-10-07. Inputs: `01-product-manager.md`, `02-product-analysis.md` (the scope, the `.level` format §7.1,
`progress.ini` §7.2, the `.sol` format §7.3, the controls §10, the 37 acceptance criteria — kept as they are; "AC-n"
below = 02 §12 item n, "02 #n" = 02 §4 item n). Branch `AutoDev`. The shape follows round 4's
`autodev/rounds/04-pinball/03-technical-analysis.md`.

Read for this analysis: `user/Apps/pinball/` as built (`table.{h,cpp}` — the `fk_kv` reader, `LoadError` with
`fmt` + `arg[2]`, the `// TR:` reasons; `scores.{h,cpp}`; `main.cpp` — `load_entry` reading a file with
`kapi_open/fsize/read`, `scan_dir`, `kapi_get_args`, the `pinball: …` log lines the simulator tests wait for;
`picker.h`, `panel.h`, `draw.h`), `autodev/rounds/04-pinball/06-development.md` (what deviated, what was learnt),
`tools/tests/run_pinball_test.sh` + `tools/tests/pinball/pinballtest.cpp`, `tools/tests/run_pinball_sim_test.sh`,
`tools/pinball/mktables.py`, `user/Apps/circuits/circuit.h` (packs, `Progress`, `level_open`, `unlock_after`),
`user/Apps/games/game.h` (`GameView`, `GameRoot`, `sfx*`, `gms`), `user/Kits/filekit/kvtext.h` (`fk_kv_*`),
`user/Kits/filekit/fsutil.h` (`fs_basename`, `fs_ci_cmp`), `user/Kits/uikit/{canvas.h,paint.h,lang.h,widget.cpp,
bmp.h}`, `user/Kits/appkit/appkit.h` (`KEY_*`), `user/Makefile` (`FT_APPS`, `FT_EXTRA_*`, the FT rule),
`tools/tests/desktop_sim/{shots.sh,run.sh,fakekapi.cpp}`, `tools/lang/check.py`, `tools/pkg/{packages.ini,
versions.ini}`, `sdcard/apps/pinball.app/{app.txt,lang/fr.txt}`, `sdcard/etc/fileassoc.ini`, `docs/04-USER-GUIDE.md`
(§12 catalog row and the *Pinball* section, the translated-apps list), `docs/06-KITS-GUIDE.md`, `IDEAS.md`.

**Summary.** Critters is **an application only: no kapi change, no kernel change, no AppKit change, no new kit, no
kit change** (`kapi_abi.h`, `appkit.h`, `appkit_calls.inc`, every `*.abi` untouched), **no simulator change** (round 4
already added `hold` / `release`; `click` is `down` + `up`; `waitlog` exists). Everything new lives beside the app in
`user/Apps/critters/`: a UI-free, **integer-only** core (`terrain`, `level`, `world`, `solution`, `progress` — plain
C++ the host test links alone) and the window (`main.cpp` + its own headers). It is an **FT app** (`FT_APPS`: UTF-8
French) with AudioKit added for `game.h`'s `sfx_*`, exactly as Pinball. Two **host tools** are new: a level generator
(`tools/critters/mklevels.py`, as Pinball's `mktables.py`) and a headless runner (`tools/critters/crsim.cpp`: trace,
picture, replay check, and a small search) — the way the 12 recorded solutions are produced and kept valid.

### Environment (checked in this container)

| Item | State |
|---|---|
| `aarch64-none-elf-g++` | **absent** — `make` / `make stage` for the Pi are the user's (AC-33 "written, not built here", as rounds 1–4) |
| host `g++`, `python3` (+ Pillow) | present |
| `sh tools/tests/run_pinball_test.sh` (the pattern to copy) | **passes in 16 s**: `ok pinball (516 checks …)`, the determinism fingerprint equal at `-O1`+ASan and `-O2` |
| `shots.sh` | builds **every** app of its list whatever names are given (≈ 4 min, round 4's R9); develop with `SHOTS_PNG=<scratch>` so `screenshots/` stays untouched until the final run |

---

## 1. What exists and is reused (by file)

| File | What Critters takes | Notes |
|---|---|---|
| `user/Apps/games/game.h` | `GameView` (`paint`, `press/release/move` with `lb`/`mx`/`my`, `key`, `tick (dt)`), `GameRoot` (`onTick` → `view->step ()`; keys the focused widgets did not take go to the view), `sfx` / `sfx_later` / `sfx_win` / `sfx_lose` / `sfx_set_mute`, `gms ()` | **unchanged** (AC-36). `gtext*` (bitmap font, Latin-1) not used for words — UTF-8 French needs `uk_text_*`. `rng*` not used by the core (02 #2: no random number in the rules). |
| `user/Apps/pinball/table.{h,cpp}` | **the pattern** of the reader: `fk_kv_parse (text, 0)`, one pass per block in file order (`fk_kv_blocks`, `fk_kv_block_name`, `fk_kv_block_line`, entries by `fk_kv_block (kv, i) == b`), the hand-written value readers (no `strtod`), `Text { en, fr; get (lang) }`, `LoadError { line; reason[96]; fmt; arg[2] }` (the UI translates `TR (e.fmt)` then fills it; the tests compare `reason`), the reasons listed in `// TR:` comments for `check.py`, a file over the limit refused by the caller (`line 0`) | copied as a pattern, **not included** (pinball's types are not Critters'). |
| `user/Apps/pinball/main.cpp` | `load_entry` (read a file: `kapi_open/fsize/read`, size check), `scan_dir` (`kapi_opendir/readdir/closedir`, sorted), `kapi_get_args` parsing (`--seed`, a path), `fs_basename` → the progress section, the log lines `pinball: playing …` / `refused <file>: line N: …` / `picker (N …)` that the sim tests `grep` and `waitlog` | patterns |
| `user/Apps/pinball/picker.h`, `panel.h` | a drawn `Widget` list with a greyed refused row and its reason, a preview `Widget`, the *Play* `ToolButton`; overlays drawn by the view with real widgets as root children (round 4's R11 focus rule) | patterns for the UX designer's picker and end screen |
| `user/Apps/pinball/scores.{h,cpp}` | `scores_section (path, shipped, …)` (`"1-space-station"` / `"user.my-table"`), `scores_setting` / `scores_set_setting` over `[settings]` | Critters' `progress.cpp` has the same two helpers (copied, 20 lines) — §2.2 on why no kit |
| `user/Apps/circuits/circuit.h` | the idea of levels opened one after the other (`level_open`, `unlock_after`), `progress.ini` with `FK_KV_ESCAPES`, unknown sections kept on write | Critters' rule is simpler: one chain over the 12 shipped levels (02 #27: *Expedition* 1 opens after *Training* 6 = the next in the chain) |
| `user/Kits/filekit/kvtext.h` (+ `kvtext.inc`) | `fk_kv_parse` (levels), `fk_kv_load (path, FK_KV_ESCAPES)` / `fk_kv_new (FK_KV_ESCAPES)`, `fk_kv_get / set / text`, `fk_kv_save (kv, path, comment)` → 0 / −1 (progress; the flag is the document's, given at load / new — as Circuits) | on the PC the code is inline (the host test needs no library); on the Pi `filekit.so` (`filekit >= 1.96`, already linked by every FT app). The core does no I/O: it parses text the UI read. |
| `user/Kits/filekit/fsutil.h` | `fs_basename`, `fs_ci_cmp` (the `.level` ending) | as Pinball |
| **UIKit** `canvas.h` | `Canvas::px / stride / w / h` (direct pixel writes: the terrain ×2 blit, §5.3), `alloc` (the minimap and the picker's thumbnails as owned canvases), `putOther` (blit them, magenta transparency for sprites), `fillRect`, `pixel` | **no UIKit change**. There is **no scaled blit** in UIKit (checked: `canvas.h`, `imagebox.h` only fits a picture into a box) — the ×2 is a 15-line loop in the app (§5.3). |
| **UIKit** `paint.h`, `lang.h`, `menu.h`, `root.h`, `toolbar.h`, `bmp.h` | `uk_text_l / _c / _w`, `uk_rbox`, `uk_tone`, `TR` / `TRC` / `TRN` / `uk_lang_init` / `uk_lang ()`, `Menu`, `ToolButton`, `Button`; `bmp_decode` if the UX designer prefers a BMP sprite sheet to sprites drawn in code | |
| **FontKit** `fontkit/uikitface.h` | `ft_uikit_install ("DejaVu Sans", 13)`, `FtTextFace` (the countdown digits, the end screen's big numbers), `ft_messagebox`, `ft_file_open` (*Open a Level File…*) | |
| **AudioKit** (through `game.h`) | the FM voices | `FT_EXTRA_critters` adds `lib/audiokit.imp.a` (as Pinball) |
| **AppKit** `appkit.h` | `kapi_key_held (KEY_LEFT / KEY_RIGHT)` (scroll), `kapi_get_modifiers` (`MOD_CTRL` for Ctrl+R, `MOD_SHIFT` for Shift+Tab), `KEY_TAB` 9, `KEY_ENTER` 13, `KEY_F1` 0x110, `kapi_get_args`, `kapi_open/fsize/read/close`, `kapi_opendir…` | existing calls only |
| `user/Include/gamepad.h` | `pad_buttons (-1)` (SHOULD 3) | header-only |
| `tools/tests/run_pinball_test.sh`, `pinball/pinballtest.cpp` | the host test's shape: two builds (`-O1 -fsanitize=address,undefined`, then `-O2`), `-Wall -Wextra -Werror`, the include paths, the `CHECK` macro, the fingerprint compared between the builds | copied for `run_critters_test.sh` |
| `tools/tests/run_pinball_sim_test.sh` | the scripted window checks: the app built for the PC with `fakekapi.cpp`, a fresh `SIM_WRITES` per case, `SIM_LOG` + `waitlog`, asserts on log lines and on the written `.ini` | copied for `run_critters_sim_test.sh` (AC-26, 27, 28) |
| `tools/tests/desktop_sim/shots.sh` | `build ()` (`extra` = the core's sources, the FT `case` list, `APPS`), `sim APP DUMP "SCRIPT" VARS`, `png`, `lang fr`, fixtures copied into `$OUT/writes/apps/<app>.app/` | a `critters` block (§9.2) |
| `tools/pinball/mktables.py` | a Python generator writing the shipped data files with every `.fr` key | model for `tools/critters/mklevels.py` |
| `tools/icons/pinball_icon.py` | Pillow → 40 × 40 BMP icon, magenta key | `tools/icons/critters_icon.py` |
| `tools/lang/check.py` | scans every `*.cpp/*.h` under `user/Apps/critters/` (`TR`, `TRC`, `TRN`, `// TR:` comments) | the core cannot include `lang.h`: its error reasons go in `// TR:` comments |

### 1.1 Facts found that shape the design

1. **The loop's rate**: `Root::run` is `while (step ()) msleep (16)`; `gms ()` counts 10 ms ticks, so `GameView`'s `dt`
   is 10, 20 or 30 ms (≈ 50 Hz on the Pi, capped at 100). The world advances by a **fixed 50 ms step through an
   integer accumulator** (§3.6), never by `dt`.
2. **The simulator's clock** advances only in `msleep`: `g_ticks += ms / 10 + 1` → **20 ms per loop turn**, one
   script step per turn. One world step (50 ms) = **2.5 script `wait`s**; a 3:00 level = 3 600 steps = 9 000 waits; the
   7:00 *Grand Tour* = 21 000. Scripts must not play levels on the clock: they jump with `--until` (§5.5).
3. **Held keys** (`kernel/gui/kwin.cpp` `UsageToKey`, checked by 02): only ← / → are held in Critters (scroll), both
   reportable; the simulator's `hold 0x102` / `release 0x102` drive them.
4. **Tab reaches the view**: `Widget::handleKey` gives a key to the focused child first; Tab moves the focus only when
   nobody took it. `CrittersView::key` returns true for `KEY_TAB` → Tab / Shift+Tab are free for 02 #21's keyboard
   picking (Shift read with `kapi_get_modifiers () & MOD_SHIFT`). When an overlay with widgets is up, the view must
   return **false** for Tab so the overlay's buttons get it (round 4's R11).
5. **`char` is unsigned on AArch64, signed on x86-64**: the core uses `uint8_t` / `int8_t` / `int16_t` explicitly,
   never plain `char` for numbers (determinism, R2).
6. **The simulator's files**: a read looks in `SIM_WRITES`, then `SIM_OVERLAY` (files only), then `sdcard/`; a folder
   is listed from `SIM_WRITES` only when the card has none. `SD:/docs/critters/` does not ship (unless SHOULD 2's
   sample is done), so fixture levels copied to `$OUT/writes/docs/critters/` **are** listed (*My levels*), and a
   broken one is opened with `SIM_ARGS` (AC-26) as Pinball did.
7. **A blocker never ends a level by itself**: 02 #24 ends a level when every creature is saved or dead; a blocker
   stays alive until an explosion. So a level with a blocker left ends only by the clock or by *All explode*. Rules
   unchanged (it is the genre's convention), but: every `.sol` whose run leaves a blocker **ends with `nuke`**
   (else AC-30's "before the time limit" fails), and the UX designer should make *All explode* obvious when only
   blockers remain (e.g. the HUD's *Out* count flashing, or a hint line — §GUI plan).
8. **No `.level` consumer exists** (`sdcard/etc/fileassoc.ini`: free — 02 §8).

---

## 2. What is missing, and where it goes (the kits-first rule)

| Missing | Where | Why there |
|---|---|---|
| The pixel terrain (materials + colour layer, edges, dig / fill / disc, dirty rectangle, incremental hash) | `user/Apps/critters/terrain.{h,cpp}` | one program uses a destructible pixel terrain. Not a kit. |
| The level model, the `.level` reader / validator, the terrain building from shapes (rect / scanline polygon / circle, textures) | `user/Apps/critters/level.{h,cpp}` | a format of this app; reads through FileKit's `fk_kv` (**no copy of a parser**) |
| The world: release, creatures' state machine, the six roles (+ basher / miner slots), giving a role, the cursor's pick, the nuke, the end and the result, events out, the per-step checksum, the clock accumulator | `user/Apps/critters/world.{h,cpp}` | idem |
| The `.sol` reader, the replay driver, the recorder, `run_solution` | `user/Apps/critters/solution.{h,cpp}` | used by the test, `crsim`, and the window (`--replay`, SHOULD 4) |
| The progress over `fk_kv` (solved, best saved, best time, the chain, `[settings]`) | `user/Apps/critters/progress.{h,cpp}` | §2.2 |
| The window: picker, play view (terrain, sprites, particles, skill bar, minimap, HUD), hint card, pause, end screen, menus, sounds, input | `user/Apps/critters/main.cpp` + `draw.h` (terrain, sprites, minimap), `bar.h` (skill bar + HUD), `picker.h` — one translation unit, as Pinball | UI; the exact split is the UX designer's / developer's |
| The 12 levels | `sdcard/apps/critters.app/levels/*.level`, generated by `tools/critters/mklevels.py` | data |
| The recorded solutions | `tools/tests/critters/solutions/<base>.sol` (not on the card — 02 §7) | test data |
| A headless runner to author and check solutions | `tools/critters/crsim.cpp` (+ built by the test script so it never rots) | host tool (§4) |
| French | `sdcard/apps/critters.app/lang/fr.txt` | `lang.h` catalogue |
| `app.txt`, `icon.bmp` | `sdcard/apps/critters.app/`; `tools/icons/critters_icon.py` | |
| Tests | `tools/tests/critters/critterstest.cpp`, `tools/tests/run_critters_test.sh`, `tools/tests/run_critters_sim_test.sh`, fixtures in `tools/tests/critters/` and `tools/tests/desktop_sim/{critters/,sd/docs/critters/}` | |

**Not needed:** no UIKit widget (the play area, the skill bar and the picker are drawn widgets of the app, as Pinball's
`TableList` / Circuits' `LevelList`); no `game.h` change (additions only if any — AC-36); no simulator change; no kit
change, so **no `.abi`, no `kitdocs.py`, no kit version raised**.

### 2.1 kapi changes

**None.** `kernel/include/kern/kapi_abi.h`, `kernel/sys/kapi*.cpp`, `user/Kits/appkit/{appkit.h,appkit_calls.inc,
appkit.abi}` stay untouched (AC-34: `git diff --stat origin/main -- kernel user/Kits` lists nothing — the Reviewer
checks it).

### 2.2 A shared progress helper in FileKit? (02 SHOULD 5) — **no, beside the app**

The three users would share only what `fk_kv` already gives (an int read / written by section and key):
Pinball keeps a **top 5 with names**, Circuits **packs with per-pack opening and lessons**, Critters **solved + best
saved + best time and a linear chain**. A common `fk_progress_*` would be either too thin to matter or shaped by one
game. `progress.{h,cpp}` is written self-contained (no Critters type in its API: sections, ints, an ordered list of
section names) so it can move to FileKit when a fourth game wants the same — noted in `docs/HANDOFF.md`.

---

## 3. The core (no UIKit, no I/O, no float) — `namespace critters`

Constraints for every core file: host `g++ -std=c++17 -Wall -Wextra -Werror` **and** the Pi's newlib
`-fno-exceptions -fno-rtti`; **no STL**, **no `float` / `double`** (checked by `grep` in the test script), no
`rng`, no clock, no `kapi_*` (checked likewise); fixed arrays sized by 02 §7.1.2's limits; the `Level`, the `World`
and the terrain buffers allocated with `new` (never on the stack); `<stdint.h>`, `<string.h>`, `<stdlib.h>`,
`<stdio.h>` (`snprintf`) only; MIT notice at the top. `level.h` and `progress.h` include `filekit/filekit.h` (for
`fk_kv`) — nothing else from the kits. Overflow-free arithmetic (`int32` with stated bounds, `int64` for the
polygon's cross products; no signed overflow: UBSan in the test).

### 3.1 `terrain.h`

```cpp
namespace critters {
enum Mat : uint8_t { M_EMPTY = 0, M_EARTH, M_STEEL, M_WATER, M_LAVA };
struct Rect { int x0, y0, x1, y1; };                 // inclusive; empty when x1 < x0
struct Terrain {
	int w, h;                                         // 320..1600 x 100..160
	uint8_t  *m;                                      // w*h materials
	uint32_t *col;                                    // w*h colours 0x00RRGGBB (the drawing's layer: Canvas's format)
	uint32_t bg, brick;                               // the level's background, the bricks' colour
	uint64_t hash;                                    // kept up to date on every change (below)
	Rect dirty;                                       // changed since the UI last took it (take_dirty)
	bool alloc (int w, int h); void free ();
	int  at (int x, int y) const;                     // outside: x<0 / x>=w / y<0 -> M_STEEL; y>=h -> M_EMPTY (the void)
	bool solid (int x, int y) const;                  // M_EARTH or M_STEEL (hazards are not solid)
	bool hazard (int x, int y) const;
	void set (int x, int y, uint8_t mat, uint32_t colour);   // the one writer: hash, dirty
	int  dig_rect (int x0, int y0, int x1, int y1);   // earth -> empty (bg); returns earth pixels removed; steel/hazard kept
	bool any_steel (int x0, int y0, int x1, int y1) const;
	int  dig_disc (int cx, int cy, int r);            // the exploder's disc
	int  brick (int x0, int y, int dir);              // a 6 x 2 brick; only empty pixels filled (brick colour, light top row)
	Rect take_dirty ();
	uint64_t full_hash () const;                      // the same value as `hash`, recomputed (the test compares)
};
}
```

- **The incremental hash** (AC-20 needs a checksum per step; hashing 256 KB each step × 8 400 steps × 12 levels × 2
  runs would cost seconds under ASan): `hash = XOR over pixels of mix64 (i * 8 + m[i])` (`mix64` = splitmix64's
  finaliser). `set` does `hash ^= mix64 (i*8 + old) ^ mix64 (i*8 + new)`; `full_hash` recomputes it (checked equal at
  the end of every replay). Colours are not hashed (they follow from the materials and the level).
- **Edges** (02 #3): columns left of 0 and right of `w−1` are steel, the row above 0 steel, below `h−1` the void.

### 3.2 `level.h`

```cpp
namespace critters {
enum { MAXW = 1600, MAXH = 160, MINW = 320, MINH = 100, MAXCRIT = 80, MAXSHAPE = 256, MAXPTS = 64,
       MAXHATCH = 4, MAXEXIT = 4, MAXLABEL = 32, NROLES = 8, MAXFILE = 65536, NAMEL = 132, HINTL = 644 /* UTF-8 */ };
enum Role { R_CLIMBER, R_FLOATER, R_BLOCKER, R_BUILDER, R_DIGGER, R_EXPLODER, R_BASHER, R_MINER };
extern const char *const ROLE_WORD[NROLES];        // "climber" ... the file / .sol words (never translated)
enum ShapeKind { SH_RECT, SH_POLY, SH_CIRCLE };
enum Texture { TX_PLAIN, TX_SPECKLE, TX_STRIPES, TX_BRICKS };
struct Text { char en[HINTL], fr[HINTL]; const char *get (int lang) const; };     // lang 1 = fr, falls back to en
                                                    // (the UI passes !strcmp (uk_lang (), "fr"): uk_lang () returns "en" / "fr")
struct Shape { uint8_t kind, mat /* M_*, or 255 = erase */, tex; uint32_t colour, colour2;
               int16_t p[2 * MAXPTS]; int n; /* rect: x y w h; circle: cx cy r; poly: n points */ int x0, y0, x1, y1; /* bbox */ };
struct Point { int16_t x, y; };
struct Hatch { Point at; int8_t dir; };
struct Label { Point at; Text text; uint32_t colour; };
struct Level {
	Text name, hint; int w, h, count, save, timeSec, rate, roles[NROLES], start; uint32_t bg, brick;
	Shape shape[MAXSHAPE]; int nshape; Hatch hatch[MAXHATCH]; int nhatch; Point exit[MAXEXIT]; int nexit;
	Label label[MAXLABEL]; int nlabel;
};
struct LoadError { int line; char reason[112]; const char *fmt; char arg[2][40]; };  // English; fmt for TR ()
bool load_level (const char *text, Level &lv, LoadError &e, bool withDiggers2 = false /* basher/miner built */);
void load_error_file (LoadError &e, bool tooBig);                     // "the file is too big" / "cannot read the file"
int  material_at (const Level &lv, int x, int y);                    // the shapes evaluated at one pixel (validation)
void build_terrain (const Level &lv, Terrain &t);                     // 02 §7.1.3, the whole map
}
```

- `load_level` = `fk_kv_parse (text, 0)`, one pass per block, then the cross-checks (1…4 hatches / exits, `save ≤
  count`, the points in the map, **a hatch / an exit inside solid terrain** — computed with `material_at` (the shapes
  evaluated at that one pixel, in order) so the picker validates every level **without building its 1 MB terrain**),
  then `fk_kv_free`. Every reason of 02 §7.1.5 with its line (the key's `fk_kv_line`, the block's
  `fk_kv_block_line` for a missing key, `[level]`'s for the hatch / exit counts, 0 for file errors). Unknown keys
  skipped (AC-4), unknown blocks refused. Values: `read_int`, `read_points` (spaces and/or one comma), `read_colour`,
  `read_word`; `name.fr` / `hint.fr` / `text.fr` fill `Text::fr`. The `basher` / `miner` keys are read from the start;
  > 0 is refused with `role <name> is not available` until SHOULD 1 is built (the flag flips then).
- **`build_terrain`** — all `M_EMPTY` / `bg`; each shape in order over its clipped bbox:
  - rect: the pixels `x…x+w−1, y…y+h−1`;
  - **polygon by scanlines** (not a per-pixel point-in-polygon: 256 shapes × 64 edges × 256 k pixels would be
    seconds): for each row `py`, the crossings of the edges with the line `y = py + ½` computed in **doubled integer
    coordinates** (`2y+1` against edges scaled ×2; `int64` products), sorted (≤ 64), even-odd spans filled for the
    pixel centres `2x+1` inside → exactly the even-odd rule at pixel centres (AC-5);
  - circle: `dx² + dy² ≤ r²`;
  - texture: `speckle` = `colour2` where `mix32 (x, y, shapeIndex) % 6 == 0` (a fixed integer hash, not `rng`),
    `stripes` every 4th row (`y % 4 == 3`), `bricks` = mortar where `y % 4 == 3` or `(x + (y / 4 % 2) * 4) % 8 == 7`;
    `colour2` default = `colour` darkened by a quarter (integer per channel: `c − c / 4`).
  Built once per level start (≈ 10–30 ms on the Pi for a 1600 × 160 level of 60 shapes; measured by the test on the
  PC and printed).

### 3.3 `world.h`

```cpp
namespace critters {
enum State : uint8_t { S_FALL, S_WALK, S_CLIMB, S_BLOCK, S_BUILD, S_SHRUG, S_DIG, S_BASH, S_MINE,
                       S_EXIT /* entering, 8 steps */, S_SPLAT, S_DROWN, S_BURN, S_BURST /* dying anims ≤ 16 steps */,
                       S_SAVED, S_DEAD };
enum { F_CLIMBER = 1, F_FLOATER = 2 };
struct Critter {
	int16_t x, y;          // the feet point (02 #4)
	int8_t  dir;           // -1 left, +1 right
	uint8_t state, flags;
	int16_t fallFrom;      // y where the fall started
	int16_t timer;         // the state's step counter (build, dig, shrug, exit, dying)
	int8_t  bricks;        // a builder's bricks left
	int16_t fuse;          // exploder countdown in steps, -1 none
	uint16_t frame;        // animation (drawing only; part of the checksum all the same)
};
enum EvKind { E_HATCH_OPEN, E_OUT, E_ROLE, E_REFUSE, E_BRICK_WARN, E_SPLAT, E_DROWN, E_BURN, E_TICK, E_BURST,
              E_SAVED, E_END };
struct Event { uint8_t kind; int8_t role; int16_t who, x, y; };
enum Refusal { OK = 0, NO_COUNT, NOT_ALIVE, LEAVING, ALREADY, NOT_ON_GROUND, IS_BLOCKER, COUNTING_DOWN };
enum Result { PLAYING, WON, LOST };
struct World {
	const Level *lv; Terrain t;
	Critter c[MAXCRIT]; int nout /* released */, saved, dead;
	int step /* steps run */, rate, nextRelease, hatchTurn, roles[NROLES];
	bool nuking; int nukeNext; int result; int endStep;
	Event ev[256]; int nev;                        // this step's (cleared at the start of step ())
	bool reset (const Level &lv);                   // builds the terrain; false: no memory
	void tick ();                                   // ONE step (1/20 s): 02 #2
	int  assign (int who, int role);                // -> Refusal; OK takes one from the count (E_ROLE / E_REFUSE)
	int  can_take (int who, int role) const;        // -> Refusal, without acting (02 #16)
	int  pick (int x, int y, int role) const;       // 02 #21's cursor rule -> creature index, -1 none
	void set_rate (int r);                          // clamped to [lv->rate, 99]
	void nuke ();
	int  inPlay () const;                           // out and not saved / dead
	uint64_t checksum () const;                     // t.hash mixed with every field of every creature and the counters
};
int interval_for (int rate);                        // 4 + (99 - rate) * 40 / 98
}
```

**`World::tick ()` — one step, in this order** (the order is part of determinism and of the tests):

1. If `result != PLAYING` → nothing. `nev = 0`.
2. **Release**: at `step == 0` emit `E_HATCH_OPEN`; if `!nuking && nout < count && step >= nextRelease` (first
   `nextRelease = 40`): a creature at `hatch[hatchTurn].at`, `S_FALL`, `fallFrom = y`, facing the hatch's `dir`;
   `hatchTurn = (hatchTurn + 1) % nhatch`; `nextRelease = step + interval_for (rate)` (a rate change takes effect from
   the next release). → AC-6's 40, 64, 88, 112, 136.
3. **Nuke**: if `nuking`, the next creature in release order that is in play and has no fuse gets `fuse = 100`
   (one a step; 02 #17).
4. **Each creature `i = 0 … nout−1` in order**: its fuse (−1 a step; `E_TICK` at 100, 80, 60, 40, 20; at 0 → burst:
   `t.dig_disc (x, y−4, 12)`, `S_BURST`, `E_BURST`), then its state:
   - `S_WALK`: blockers first (any `S_BLOCK` creature `b ≠ i` with `|x_b − x| ≤ 6`, `|y_b − y| ≤ 10`, and `x_b − x`
     having the sign of `dir` → turn); then one pixel ahead: if solid at the feet height, look up to 6 px for the first
     empty → step up; none → climber: `S_CLIMB`, else turn; else if the ground falls away: down up to 3 px → step down;
     deeper → `S_FALL` (`fallFrom = y`);
   - `S_FALL`: up to 3 px (a floater: 3 px for the first 12, then 1 px), **pixel by pixel** (a 2-px brick is never
     tunnelled through); landing → fall `y − fallFrom > 60` and not a floater → `S_SPLAT`; else `S_WALK`;
   - `S_CLIMB`: a blocker within reach as for `S_WALK` (02 #12 says "walking or climbing") → let go, turn, `S_FALL`
     *(added during validation)*; solid above the head (`y − 12`) → let go, turn, `S_FALL`; else up 1 px; the wall's top reached
     (the column ahead empty at the feet) → step over, `S_WALK`;
   - `S_BUILD` (every 8 steps): head-height check (column `x + 3·dir`, rows `y−4 … y−9` solid → turn, `S_WALK`);
     else `t.brick (…)` + `x += 3·dir`, `y −= 2`, `bricks−−` (`E_BRICK_WARN` at 3, 2, 1); at 0 → `S_SHRUG` (10 steps)
     → `S_WALK`. **The brick's exact pixels** (proposed, pinned by AC-14's test): columns `x − 1·dir … x + 4·dir`
     (6 px), rows `y − 1 … y` — the next brick overlaps the last by 3 columns, a stair rising 2 every 3;
   - `S_DIG` (every 2 steps): `any_steel (x−4, y+1, x+4, y+1)` → `S_WALK`; no earth there → `S_FALL`; else
     `dig_rect` that row, `y += 1`;
   - `S_BLOCK`: no ground under it → `S_FALL` (a walker on landing — 02 #12);
   - `S_EXIT`: 8 steps → `S_SAVED`, `saved++`, `E_SAVED`; dying states: ≤ 16 steps → `S_DEAD`, `dead++`;
   - after the move, for any living, not-yet-leaving creature: **hazard** (`at (x, y)` or `at (x, y+1)` water /
     lava) → `S_DROWN` / `S_BURN`; `y ≥ h` → `S_DEAD` at once (the void); **exit** (`|x − ex| ≤ 2`, `|y − ey| ≤ 4`,
     not a blocker, not bursting) → `S_EXIT`.
5. `step++`. **End** (02 #24): `nout == count` (or `nuking`) and `inPlay () == 0` → `result = saved >= save ? WON :
   LOST`; else `step >= timeSec × 20` → every creature in play counted lost, judged the same; `E_END`, `endStep`.

**Giving a role** (`can_take`, 02 #16): `NO_COUNT` (count 0), `NOT_ALIVE` (dead / dying / saved), `LEAVING`
(`S_EXIT`), climber / floater: `ALREADY` if the flag is set; blocker / builder / digger (basher, miner): `NOT_ON_GROUND`
unless `S_WALK`, `S_BUILD`, `S_DIG`, `S_BASH`, `S_MINE` (or `S_SHRUG` for builder), `IS_BLOCKER` if `S_BLOCK`,
`ALREADY` for the same working role (except builder: given again, 12 more — 02 #13); exploder: `COUNTING_DOWN` if
`fuse ≥ 0`. **`pick (x, y, role)`**: among creatures whose box `x−4…x+4, y−11…y` holds the point, the first in release
order with `can_take == OK`, else the one nearest (Manhattan, then lowest index) — deterministic (AC-17).

**Constants**: every number of 02 §4.2–4.3 lives in one `enum` at the top of `world.h` (`WALK_UP = 6, WALK_DOWN = 3,
FALL_SPEED = 3, FALL_KILL = 60, FLOAT_FAST = 12, BLOCK_DX = 6, BLOCK_DY = 10, BRICK_EVERY = 8, BRICKS = 12,
SHRUG = 10, DIG_EVERY = 2, FUSE = 100, BURST_R = 12, EXIT_DX = 2, EXIT_DY = 4, EXIT_STEPS = 8, FIRST_OUT = 40`) so
the tests read the same names; 02 says a change is made in the table and the tests together.

### 3.4 `solution.h`

```cpp
namespace critters {
enum ActKind { A_ROLE, A_RATE, A_NUKE, A_PAUSE, A_FAST };
struct Action { int step; uint8_t kind; int8_t role; int16_t arg /* creature, or rate */; int line; };
struct Solution { Action a[1024]; int n; };
bool load_solution (const char *text, Solution &s, LoadError &e);   // 02 §7.3: "bad step" (also a step LOWER than the line before:
                                                                    // equal steps are allowed -- two roles in one step), "bad action",
                                                                    // "bad creature"; a "#" ends a line (02's example has trailing
                                                                    // comments); more than 1024 actions -> "too many actions (max 1024)"
struct Replay { const Solution *s; int next; int refused;            // applied before each tick
	void apply_due (World &w); };                               // every action with step == w.step (A_PAUSE / A_FAST ignored)
struct Recorder { Solution s; void add (int step, int kind, int role, int arg); int text (char *out, int cap) const; };
struct RunResult { int result, saved, needed, endStep, refused; uint64_t lastChecksum; };
// A level run headless with a solution to its end; perStep (may be 0) gets the checksum after every tick
RunResult run_solution (const Level &lv, const Solution *s, void (*perStep) (int step, uint64_t sum, void *u), void *u);
}
```

The window records the player's actions with `Recorder` (a role given by creature index, a rate change, the nuke) —
the base of SHOULD 4 (`replay` in `progress.ini`) and of a `--record <file.sol>` switch (cheap, useful to the user
for new levels on the Pi; **SHOULD**, not an AC).

### 3.5 `progress.h`

```cpp
namespace critters {
struct Best { bool solved; int saved, timeSec; };
void progress_section (const char *path, bool shipped, char *out, int cap);  // "training-01-straight-down" / "user.<base>"
Best progress_get (const fk_kv *kv, const char *section);                    // a line that does not read: as absent
bool progress_won (fk_kv *kv, const char *section, int saved, int timeSec); // solved=1, saved=max, time=min -> true: a new best
bool progress_open (const fk_kv *kv, const char *const *chain, int n, int k); // k = 0 open; else chain[k-1] solved
const char *progress_setting (const fk_kv *kv, const char *key, const char *def);   // [settings] sound, last
void progress_set_setting (fk_kv *kv, const char *key, const char *value);
}
```

A lost run writes nothing (AC-23). The chain is the 12 shipped sections in picker order (training 01…06, expedition
01…06) — 02 #27's rule is exactly "each opens when the one before it is solved"; the player's levels are always open.

### 3.6 The clock — `world.h`, used by the window and tested (AC-21)

```cpp
struct Clock { int acc3; bool fast, paused;                   // time owed, in thirds of a millisecond
	int due (unsigned dt); };                             // -> steps to run now. paused: 0 and acc3 = 0. Else acc3 += dt * 3;
	                                                      //    one step costs 150 (50 ms) normally, 50 (16.7 ms) when fast;
	                                                      //    n = acc3 / cost, capped at 4 (fast: 12 = 4 frames' worth),
	                                                      //    acc3 -= n * cost; capped -> acc3 = 0 (the owed time dropped:
	                                                      //    the game slows, a step is never skipped -- 02 #2)
```

*(Fixed during validation: the sketch named the field `acc` but used `acc3`, and the cost per step was ambiguous.
02 #2's "at most 4 steps caught up per frame" is read for the normal speed; fast-forward allows 12, three times as
much, so it is not throttled by a 100 ms frame.)*

The window does `for (n = clock.due (dt); n > 0; n--) { replay.apply_due (w); w.tick (); events → sounds; }`. Pause
and fast-forward change only **when** steps run, never **what** a step does; AC-21's test drives the same `Solution`
through `Clock` with random `dt`s, pauses and fast toggles and compares the final checksum with `run_solution`'s.

---

## 4. Host tools — making the 12 levels and their solutions

The risk of the round is not the engine but **12 levels that are each winnable, each lost when nothing is done, and
whose recorded solutions stay valid while the engine is tuned**. Nobody can play the game interactively in this
container (the simulator runs scripts), so the levels are made **by script** and solved **by trace**:

### 4.1 `tools/critters/mklevels.py`

Writes the 12 `.level` files of 02 §6 into `sdcard/apps/critters.app/levels/` (geometry as Python data: rectangles,
polygons, circles, materials, textures, palettes; `name.fr`, `hint.fr`, `[label]` `text.fr`), so a geometry tweak
is one number and a re-run. The UX designer's palettes go there. The header comment of each file says "generated by
tools/critters/mklevels.py — edit the script, not the file".

### 4.2 `tools/critters/crsim.cpp` — the headless runner (links the core only)

```
crsim LEVEL [SOL]                       run to the end (no SOL: nothing done); print "won|lost saved/needed time step refused"
crsim LEVEL [SOL] --trace               + one line per event: "412 c3 lands 130,119" "430 c3 wall-turn" "511 c0 saved" ...
crsim LEVEL [SOL] --at STEP --ppm F     the terrain + creatures (numbered) at that step as a PPM (×2), for a look
crsim LEVEL [SOL] --where STEP          every creature's index, state, x, y at that step (to aim an assignment)
crsim LEVEL SOL --search LINE A B       LINE's step tried from A to B (e.g. "builder 0"): prints the steps giving the best result
crsim --check LEVEL...                  load only: OK or "line N: reason"
```

The authoring loop for a level: `mklevels.py` → `crsim L --trace` (where do they go when nothing is done? it must
say *lost*) → `crsim L --where 120` (who is where) → write `<base>.sol` lines → `crsim L S` (won? refused 0?) →
`--search` to find a robust step window (prefer a step in the **middle** of a winning window, so a small engine tweak
keeps it valid) → `--ppm` to look. `run_critters_test.sh` compiles `crsim` too (so it builds whenever the core
changes) and the test asserts on the same `run_solution` (AC-30, 31). The `--search` brute force is cheap: one run
of an 8 400-step level with 60 creatures is ≈ 1–2 ms at `-O2`.

---

## 5. The window — `main.cpp` + `draw.h`, `bar.h`, `picker.h`

### 5.1 Start-up and arguments

`ft_uikit_install ("DejaVu Sans", 13)`, `uk_lang_init ()`, then `kapi_get_args`:
`critters` → picker; `critters <file.level>` → that level (hint card), also listed on the picker for the run;
`critters <file.level> --replay <file.sol> [--until <step|end>]` → the level with the solution applied (no hint
card); **`--until`** runs the world **headless, instantly** to that step (no wall time — the result is the same bits,
the world being deterministic) and pauses there; `--until end` runs to the end → the end screen and the progress
written (AC-25 `critters-end`, AC-27). A bad `.sol` → `ft_messagebox` with its line, then the level normally.
SHOULD 2: `--check <file>`. Log lines to stdout for the sim tests: `critters: picker (N levels)`, `critters: playing
<name en>`, `critters: refused <file>: line N: <reason>`, `critters: role <word> c<i> ok|refused (<count> left)`,
`critters: end won|lost <saved>/<needed> <sec>s`, `critters: progress written`, `critters: overlay <name>`.

### 5.2 States

`V_PICKER` → `V_CARD` (the hint card, paused) → `V_PLAY` (+ overlays: pause menu on Esc, nuke confirmation) →
`V_END` (Retry / Next / Levels). The world ticks only in `V_PLAY` without an overlay (R11 of round 4).

### 5.3 Drawing and its cost (R1)

- **Terrain**: the core's `col[]` is already `0x00RRGGBB`. Each frame the visible window of it (at ×2 suggested:
  a play area of e.g. 640 × 320 screen px = 320 × 160 logical px) is blitted scaled: per logical row, build the doubled
  row once (`dst[2i] = dst[2i+1] = src[i]`), `memcpy` it to the two screen rows → ≈ 51 k reads + 205 k writes a frame
  ≈ 1 ms on the A72; no full-level ×2 cache (it would be 4 MB). Integer scroll in logical pixels (the view's left edge
  `vx`, 4 px a step held key or edge-scroll — the UX sizes it).
- **Creatures**: ≤ 80 small sprites (≈ 10 × 12 logical, drawn ×2) from a palette-indexed table in `draw.h` (drawn in
  code: no file, originality obvious — AC-37) or a BMP sheet via `bmp_decode` (UX choice); explosion particles (≤ 64,
  integer motion, **drawing-only**, seeded from the burst's position — they never touch the world); the countdown
  digit over an exploder.
- **Minimap** (02 #19): an owned `Canvas` (e.g. 160 × 16 for a 1600-wide level: one sample per 10 × 10 block, the
  block's most common non-empty material colour) **rebuilt only when `t.take_dirty ()` is non-empty, at most every
  10 steps**; each frame: blit it, the creatures as dots, the view frame.
- **Picker thumbnails**: `build_terrain` into a scratch `Terrain` on selection (≤ 30 ms), scaled down into a cached
  `Canvas` per level (≤ 32 cached); the 1.25 MB scratch reused.
- **Labels** (`[label]`, ≤ 32): drawn by the UI into the colour layer once after `build_terrain` (only over earth /
  steel pixels: they erode with the earth) — or over the terrain each frame; the UX designer decides. Text through
  `uk_text_*` into a small canvas, copied onto `col[]` (the core's materials untouched).
- The whole view is redrawn each tick (Invaders' and Pinball's way); the skill bar's counts and the HUD only when a
  value changed.

### 5.4 Input

- Mouse: a press on a slot chooses the role; a press in the play area → `w.pick (lx, ly, role)` → `w.assign` (logged,
  recorded, sound); the creature under the pointer highlighted each frame (`pick` with the chosen role); a press /
  drag on the minimap scrolls; the pointer within 8 px of the play area's left / right edge scrolls.
- Keys (02 §10): `1`…`8`, `Tab` / Shift+`Tab` (fact 4), `Enter`, `-` / `+` / `=`, `p` / Space, `f`, `n` (twice within
  2 s, or Enter), ← / → held (`kapi_key_held` polled in `tick`; their events consumed), Ctrl+R (`kapi_get_modifiers ()
  & MOD_CTRL`), Esc, `m`, F1 (`KEY_F1`). Keys are acted on **between** steps (the world is never changed mid-step).
- SHOULD 3: `pad_buttons (-1)` edges.

### 5.5 Simulator facts for the scripts

One `wait` = 20 ms; one world step = 2.5 waits. A shot "a few seconds in" = `--replay S --until N` + a few `wait`s
(the sprites' animation runs, the world paused) — no 9 000-wait scripts. A `click` at a creature: its logical position
at step N comes from `crsim --where N` (deterministic), turned into client coordinates by the view's origin and ×2;
the log line `critters: role digger c0 ok (2 left)` is what AC-28 asserts (no pixel check).

---

## 6. The data

### 6.1 The 12 levels (02 §6)

All 160 high, generated by `mklevels.py`; each level's design must make the taught role **necessary** (AC-31) and
leave a **winning window** wide enough to survive tuning:

| Level | Why nothing-done loses (AC-31) | Watch out |
|---|---|---|
| T1 straight down | they walk to and fro on the upper floor until the clock | a steel-free floor ≥ 20 px under the hatch; the exit under it reachable after the shaft |
| T2 mind the gap | the ravine is a > 60 px drop (or lava) | gap ≤ 30 px so 1–2 builders bridge it (AC-14's 30 px case is the model) |
| T3 hold the line | the hatch faces the lake; the exit is behind them | blockers never end the level: the `.sol` ends with `nuke` once 6 are saved (fact 7) — or the digger frees the blocker (dig its floor) |
| T4 up the wall | wall > 6 px everywhere before the exit | climbers must get over: no overhang on the wall's top |
| T5 soft landing | the drop to the exit > 60 px | the landing zone flat, the exit reachable by walking after it |
| T6 blast through | an earth wall > 6 px high, too thick to climb (no climbers given anyway) | the burst disc (r 12 at y−4) must open a passage ≤ 6 px step: the wall's base thickness ≤ 2 × 12 and the floor flat — check with `--ppm` |
| E1–E6 | mixed; each checked by `crsim L` = *lost* | E5 / E6 have tunnels and caves: check no creature can get stuck in a 1-px pit (they turn forever: lost by the clock, fine, but the solution must avoid it) |

### 6.2 Fixtures for the tests

- `tools/tests/critters/example.level` — 02 §7.1.4 verbatim (AC-2; the broken cases of AC-3 are **made by the test
  from it**, one line changed each, as Pinball).
- `tools/tests/critters/solutions/<base>.sol` × 12 (AC-30), and `example.sol` if useful.
- Rule levels **built in the test as text** (a 320 × 100 flat floor, a wall of 7 / 40 px, a 3 / 4 / 6 px step, a 60 /
  61 px drop, a 150 px drop, water, lava, earth-over-steel, earth-over-cave, a 30-px gap over 100 px) — AC-5…AC-19.
- `tools/tests/desktop_sim/critters/progress.ini` — some levels solved (the picker's ticks and bests, AC-25).
- `tools/tests/desktop_sim/sd/docs/critters/broken.level` (a bad block on a known line) and `quick.level` (5
  creatures, save 1, a hatch above the exit — ends in a few seconds; AC-26, 27).

---

## 7. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | **Performance on the Pi**: a 1600 × 160 terrain (256 KB materials + 1 MB colours), 80 creatures, 50 Hz drawing | simulation: 80 creatures × a few pixel probes + ≤ 80 blocker checks each ≈ 10⁴ ops a step at 20 steps/s — negligible; digging / bursts touch ≤ 25 × 25 px. Drawing: only the visible 320 × 160 logical window scaled (≈ 1 ms, §5.3), the minimap rebuilt on dirty only, thumbnails cached; terrain built once per start with scanline polygons (≈ 10–30 ms). Memory ≈ 1.6 MB a level, allocated once. Real frame rate checked by the user on the Pi (02 LATER). |
| R2 | **Determinism** (AC-20, 21, and every recorded solution) | integers only (grep-checked), no `rng`, no clock, no uninitialised fields (`reset` clears everything with `memset`), creature order fixed (release order), the step's sub-order fixed (§3.3), assignments only between steps, `uint8_t`/`int8_t` instead of `char` (fact 1.1-5), no signed overflow (UBSan), the hash over explicit fields (no struct bytes / padding). The same source gives the same bits on PC and Pi — no `-ffp-contract` concern (no float). The test compares the per-step checksum stream of two runs **and** of the `-O1`+ASan and `-O2` builds (a fingerprint, as Pinball). |
| R3 | **Solutions break when a constant or a level changes** | the test is the guard (AC-30); `crsim --search` re-finds a window in seconds; prefer steps in the middle of winning windows (§4.2); the constants are frozen once the levels are solved (a later change re-runs `--search` for each `.sol`) |
| R4 | **Unwinnable / trivially winnable levels** | AC-30 + AC-31 per level (won by the `.sol`, lost with nothing, Training lost without the taught role) |
| R5 | **Simulator time**: 2.5 waits per step, levels of 3–7 min | `--until` jumps headless (instant); `waitlog` for ends; `quick.level` for an end on the clock (AC-27 can use either); `shots.sh` itself ≈ 4 min (builds every app) — develop with the host test, run shots a few times only |
| R6 | **Blockers keep a level alive** (fact 7) | solutions end with `nuke`; the UX makes *All explode* discoverable; docs/04 says it |
| R7 | **Tunnelling** through 2-px bricks or thin walls (fall 3 px, floater, walk step-up) | every move pixel by pixel; AC-14's gap crossing and AC-8's exact 60/61 px cases pin it |
| R8 | **Pixel-rule ambiguity** (where exactly a brick, a dig row, a burst disc lands) | exact formulas in `world.cpp` comments (§3.3), the tests check pixel by pixel (AC-14, 15, 16), docs/04 does not promise pixels |
| R9 | **Focus between the view and the overlays** (Tab, Enter on the end screen / pause menu) | round 4's R11 rule: the view ignores keys while an overlay with widgets is up; Tab returned false then (fact 4) |
| R10 | **French widths** (skill bar, HUD) | the bar shows icons + counts (no words); names in the HUD / tooltips; checked on the French shots (AC-25) |
| R11 | `-Werror` on the PC vs newlib (`snprintf` truncation warnings) | sizes with margin; the sim test builds the app's sources with `-Wall -Wextra -Wno-format-truncation`, the host test the core with `-Werror` (Pinball's rule) |

---

## 8. Step-by-step implementation plan (each step testable, in order)

> Checks along the way: `sh tools/tests/run_critters_test.sh` (the core, the levels, the solutions),
> `python3 tools/lang/check.py critters`, `sh tools/tests/run_critters_sim_test.sh` (the window's scripted cases),
> `SHOTS_PNG=$SCRATCH sh tools/tests/desktop_sim/shots.sh critters` (the look). The Pi build (`make` from `kernel/`)
> cannot run here: the Makefile lines are written carefully and said so in `06-development.md`.

**Step 1 — scaffolding.** `user/Apps/critters/{terrain,level,world,solution,progress}.{h,cpp}` with the MIT notice
and empty bodies; `tools/tests/critters/critterstest.cpp` (`CHECK`, `main` taking level files); `tools/tests/
run_critters_test.sh`:

```sh
set -e; cd "$(dirname "$0")/../.."; OUT=${TMPDIR:-/tmp}
CORE="terrain level world solution progress"; SRC=$(for f in $CORE; do printf 'user/Apps/critters/%s.cpp ' $f; done)
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Apps -I kernel/include"
# the core: integers only, no rng / clock / kapi / UIKit (AC-20, AC-34)
for f in $CORE; do sed 's,//.*,,' user/Apps/critters/$f.[ch]* | grep -nE '\b(float|double)\b|\brng|gms *\(|kapi_|uikit/' && { echo "FAIL critters: $f"; exit 1; }; done
# (comments stripped first: a comment may say "no float")
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined $INC \
    tools/tests/critters/critterstest.cpp $SRC -o "$OUT/onyx_critters_asan"
g++ -std=c++17 -O2 -Wall -Wextra -Werror $INC tools/tests/critters/critterstest.cpp $SRC -o "$OUT/onyx_critters"
g++ -std=c++17 -O2 -Wall -Wextra -Werror $INC tools/critters/crsim.cpp $SRC -o "$OUT/crsim"       # (kept building)
T=${*:-$(ls sdcard/apps/critters.app/levels/*.level 2>/dev/null)}
"$OUT/onyx_critters_asan" $T > "$OUT/c1.out" || { cat "$OUT/c1.out"; exit 1; }; cat "$OUT/c1.out"
"$OUT/onyx_critters" $T > "$OUT/c2.out" || { cat "$OUT/c2.out"; exit 1; }; cat "$OUT/c2.out"
[ "$(grep fingerprint "$OUT/c1.out")" = "$(grep fingerprint "$OUT/c2.out")" ] || { echo "FAIL critters: builds differ"; exit 1; }
```

Test: the script runs and prints `ok   critters (0 checks)`.

**Step 2 — the terrain** (`terrain.cpp`): `alloc`, `at` with the edges, `set` (hash + dirty), `dig_rect`, `dig_disc`,
`brick`, `any_steel`, `take_dirty`, `full_hash`. Test: edges (steel left / right / top, void below); dig never
removes steel / hazards; `hash == full_hash ()` after random edits.

**Step 3 — the level reader** (`level.cpp`): values, every block and key of 02 §7.1.2, defaults, limits, every error of
§7.1.5 with its line, `material_at`, `// TR:` comments for the reasons. Test: **AC-2** (the example: counts, names,
defaults), **AC-3** (each case made from the example: exact reason, exact line, the level cleared), **AC-4**.

**Step 4 — the terrain building** (`build_terrain`): rect, scanline polygon, circle, erase / steel / hazards, clipping,
textures, colours, bricks' colour. Test: **AC-5** (150 earth pixels; 317 for r 10; the triangle against a brute-force
even-odd count at pixel centres written in the test; erase; steel over earth; clipping), `material_at` equal to
`build_terrain` on every pixel of the example; the build time printed.

**Step 5 — the world, walking and falling** (`world.cpp`: `reset`, release, `S_FALL`, `S_WALK`, hazards, the void,
exits, the end, `checksum`, `Clock`). Test: **AC-6** (40, 64, 88, 112, 136; rate 99 → 4, rate 1 → 44; two hatches
alternate; `set_rate` clamps), **AC-7**, **AC-8**, **AC-10**, **AC-11**, **AC-19** (end and win / lose at
`save − 1 / save / save + 1`, time-out).

**Step 6 — the roles** (climber, floater, blocker, builder, digger, exploder, `can_take`, `assign`, `pick`, `nuke`).
Test: **AC-9**, **AC-12**, **AC-13**, **AC-14** (bricks pixel by pixel, the early stop, the 30-px gap crossed by the
followers alive), **AC-15**, **AC-16**, **AC-17**, **AC-18**.

**Step 7 — solutions and replay** (`solution.cpp`: `load_solution`, `Replay`, `Recorder`, `run_solution`). Test:
**AC-22** (the §7.3 example reads; bad step / action / creature; out of order), recorder text → `load_solution` →
same actions; **AC-21** on a rule level (Clock with random `dt`, pauses, fast → same final checksum).

**Step 8 — progress** (`progress.cpp`). Test: **AC-23** (8 / 120 then 9 / 140 then lost → `solved 1, saved 9, time
120`; written with `fk_kv_text`, re-parsed equal; unknown sections and keys kept; a broken line ignored), **AC-24**
(the chain; user levels open).

**Step 9 — the tools and the 12 levels** (`tools/critters/crsim.cpp`, `tools/critters/mklevels.py`,
`sdcard/apps/critters.app/levels/*.level`, `tools/tests/critters/solutions/*.sol`). Build T1…T6 first, then E1…E6,
each with the loop of §4.2. Test: **AC-1** (12 load; `name.fr`, `hint.fr`; counts and sizes; 6 + 6 prefixes; order by
`<nn>`), **AC-30** (each won by its `.sol`, refused 0, saved / needed / time printed), **AC-31** (each lost with
nothing; each Training level lost without its role's assignments and with its count at 0), **AC-20** (each `.sol`
run twice: equal checksum streams; `hash == full_hash ()` at the end; the fingerprint = a hash of all 12 streams). →
**`run_critters_test.sh` complete (AC-29)**; record its time (target < 30 s).

**Step 10 — the window, play first** (`main.cpp`, `draw.h`, `bar.h`): `FT_APPS` app, `GameRoot` + `CrittersView`,
`critters <file>` plays at once (hint card), `--replay` / `--until`, the clock, the terrain ×2 blit, sprites, particles,
countdowns, skill bar with counts and the chosen role, HUD, minimap, scrolling (keys, edges, minimap), giving roles
(mouse and Tab / Enter), pause, fast, nuke + confirmation, sounds per `Event` (+ `sfx_win` / `sfx_lose`), the log
lines. Add to `shots.sh`: `[ "$1" = critters ] && extra="user/Apps/critters/terrain.cpp … progress.cpp"`, `critters`
in the FT `case` list and in `APPS`. Test: a shot of T2 at `--until 400` with its `.sol` (a builder at work).

**Step 11 — the picker, the end screen, the progress file.** The packs (shipped in order), *My levels*
(`SD:/docs/critters/*.level`, a refused one greyed with its reason), the lock / tick / best, the preview, the roles,
*Play*; the end screen (*Level complete!* / *Not enough critters saved*, saved / %, needed, time, *New best!*, Retry /
Next / Levels); `progress.ini` load / save (`FK_KV_ESCAPES`, at each won level and on the sound toggle; `[settings]
last`); the menus (*Game*: Restart Level ^R, Pause P, Fast Forward F, All Explode N, Levels… Esc, Open a Level File… ^O,
Sound On / Off M, Quit ^Q; *Help*: How to Play F1, About Critters). Test: `tools/tests/run_critters_sim_test.sh`
(§9.3): **AC-26, AC-27, AC-28**.

**Step 12 — French.** Every word in `TR` (the loader's reasons via `// TR:` + `TR (e.fmt)`), the levels' `.fr` texts
by `uk_lang ()`, `sdcard/apps/critters.app/lang/fr.txt` (02 §11's words). Test: `python3 tools/lang/check.py critters`
→ 0 missing (**AC-32**); the French shots by eye.

**Step 13 — the card, the build, the package (declared).** `user/Makefile`:

```make
FT_APPS = … circuits pinball critters
# Critters (AutoDev round 5): its core (the terrain, the levels, the world, the solutions, the progress -- integers
# only, tools/tests/run_critters_test.sh) beside the window; draw.h, bar.h, picker.h are the window's own headers.
FT_EXTRA_critters = Apps/critters/terrain.cpp Apps/critters/level.cpp Apps/critters/world.cpp Apps/critters/solution.cpp Apps/critters/progress.cpp lib/audiokit.imp.a
critters.elf: Apps/critters/terrain.cpp Apps/critters/level.cpp Apps/critters/world.cpp Apps/critters/solution.cpp Apps/critters/progress.cpp \
	      $(wildcard Apps/critters/*.h) Apps/games/game.h Include/gamepad.h lib/audiokit.imp.a Kits/audiokit/audiokit.h Kits/filekit/kvtext.h Kits/filekit/kvtext.inc Kits/uikit/toolbar.h
```

`sdcard/apps/critters.app/app.txt`:

```
# Onyx application metadata -- read by the category launcher (key=value, no section).
name = Critters
category = Games
opens = level
```

(`stack` not needed: the big buffers are on the heap; add `stack = 4M` as Pinball only if the developer keeps large
locals.) `icon.bmp` (`tools/icons/critters_icon.py`: a round creature, our own), `levels/` (12), `lang/fr.txt`;
`sdcard/etc/fileassoc.ini` `# Critters` / `level = critters`; `tools/pkg/packages.ini`:

```ini
[app.critters]
needs    = uikit >= 1.781, audiokit >= 1.232, filekit >= 1.96, fontkit >= 1.135
opens    = level
```

(the versions in `tools/pkg/versions.ini` today; `apps/critters.app/` packaged whole by the `[*apps]` rule; no user
file shipped; **declared, not published** — PIPELINE §0.5; `versions.ini` not edited.) Test: **AC-33**'s file list
present; **AC-34** (MIT notices: `grep -L "MIT License" user/Apps/critters/* tools/critters/* tools/tests/critters/*.cpp`
empty; `git diff --stat origin/main -- kernel user/Kits` empty).

**Step 14 — screenshots and docs.** `shots.sh critters` (§9.2) → `screenshots/critters*.png`; docs/04 §12: the
catalog row (Games) and a *Critters* section (rules, roles, controls of 02 §10, the levels, files, the `.level` format
with the example and the errors, the `.sol` format, the screenshots, "All explode ends a level a blocker keeps
alive"), the translated-apps list (line ≈ 1658: "…, Pinball, Critters and the dialogs…"); docs/03 if relevant (one line
in the apps list: "integer core testable on the PC, solutions replayed"); `docs/HANDOFF.md` (a *Critters* section:
done, not in `main`, not published; the progress helper that could move to FileKit; SHOULDs left); `IDEAS.md`'s
*Lemmings-like* row → done (named Critters); `python docs/build_docs.py`. Test: **AC-25**, **AC-35**, **AC-36** (the
other games' shots compare equal: `shots.sh pinball invaders` with `SHOTS_PNG=<scratch>` then `cmp` with
`screenshots/`), **AC-37** (`grep -ri lemming user/Apps/critters sdcard/apps/critters.app tools/critters
tools/tests/critters` empty).

**SHOULD, if time remains (02's order):** 1. basher / miner (`S_BASH`, `S_MINE`, the slots lit, the `withDiggers2` flag
on; tests in the AC-15 style); 2. `critters --check <file>` + *Game ▸ Check a Level…* (the loader's "collect all errors"
mode; a `<file>.sol` beside it replayed with `run_solution`) + `sdcard/docs/critters/my-first-level.level` (then the
sim fixtures must move from listing to `SIM_ARGS` — fact 6); 3. gamepad; 4. the best run's `replay` in `progress.ini`
and *Watch the best* (the `Recorder` exists from step 7); 5. — (judged: no kit, §2.2).

---

## 9. The tests

### 9.1 Host test — `tools/tests/critters/critterstest.cpp` (`sh tools/tests/run_critters_test.sh`)

Prints `ok   critters (N checks: 12 levels, 12 solutions, M steps)` and a `determinism fingerprint <hex>` line; any failure
prints `FAIL file:line …` and exits 1. Small levels are made **in the test as text** (a helper `lvl ("size = 320 100",
"[shape] rect = 0 80 320 20", …)`) and run with `World` directly, placing creatures where needed (`World` exposes
`c[]`; a test helper `drop (w, x, y, dir)` adds one without the hatch).

| Part | Asserts | AC |
|---|---|---|
| A. levels | 12 shipped load; `name.fr`, `hint.fr`; 1…4 hatches / exits; `save ≤ count ≤ 80`; width 320…1600; height ≤ 160; 6 `training-`, 6 `expedition-`; order by `<nn>` | 1 |
| | the example loads (its counts, defaults) | 2 |
| | each §7.1.5 case from the example, one line changed: refused, exact `reason`, exact `line`, the level cleared; > 64 KB and unreadable via `load_error_file` | 3 |
| | `sparkle = 1` in `[shape]` loads | 4 |
| | rect 150 px; circle r 10 → 317 px; a triangle = brute-force even-odd count; erase; steel over earth; clipping; `material_at` = built map | 5 |
| B. creatures | release steps; rate 99 / 50 / 1 intervals; two hatches alternate; `set_rate` clamps | 6 |
| | walk 1 px / step; up 3 and 6 px; turn at 7; down 3 walking; 4 → falls | 7 |
| | 60 px fall alive, 61 dies; 3 px / step | 8 |
| | floater 150 px alive; 3 px / step for 12 px then 1 | 9 |
| | water, lava, the void, the map's edges turn; dead count lost | 10 |
| | exit within 8 steps; 3 px beside on a shelf 5 px above: not | 11 |
| C. roles | climber climbs a 40 px wall at 1 px / step, walks the top; overhang → falls back turned; a walker turns | 12 |
| | blocker: both sides turn within 6 px; none passes in 600 steps; floor dug → falls; exploder frees | 13 |
| | builder: 12 bricks, 1 per 8 steps, 3 px further / 2 px higher, pixels checked; early stop at a wall; 30-px gap over 100 px crossed by followers alive | 14 |
| | digger: 1 px / 2 steps, 9 px shaft, stops on steel intact; through to a cave → falls | 15 |
| | exploder: 100 steps; disc r 12 at (x, y−4) earth removed, steel / hazard kept; dies; in the air too | 16 |
| | counts −1; 0 refused; digger on a faller, builder on a blocker, climber twice, exploder twice refused, counts unchanged; climber + floater hold; `pick` among overlapping | 17 |
| | nuke: no more release; one fuse a step in release order; level ends after the last burst; no exploder count taken | 18 |
| D. rules | end when all out and saved / dead; won iff `saved ≥ save` (save − 1, save, save + 1); time-out ends at once, the rest lost | 19 |
| | each `.sol` run twice → equal checksum per step; `hash == full_hash` at the end; the fingerprint | 20 |
| | `Clock` with random `dt`, pauses, fast → same final checksum as `run_solution` | 21 |
| | `.sol` reader: the example; bad step / action / creature; out of order | 22 |
| E. progress | 8 / 120, 9 / 140, lost → 1 / 9 / 120; round trip; unknown kept; broken line ignored | 23 |
| | the chain; user levels open | 24 |
| F. shipped levels | each won by its `.sol`, 0 refused, saved / needed / time printed | 30 |
| | each lost with nothing; each Training lost without its role | 31 |

### 9.2 Simulator scenario — `shots.sh critters`

```sh
if want critters; then			# (Critters, AutoDev round 5: desktop_sim/critters/progress.ini in the writes' folder; one
					#  script step = 20 ms, a world step = 2.5 steps: mid-level states via --replay … --until)
	CQ="$OUT/writes/apps/critters.app"; L=SD:/apps/critters.app/levels; S=SD:/tmp/critters
	CB="SIM_POS=60,30"; cw () { printf 'wait;%.0s' $(seq 1 $1); }
	cfix () { rm -rf "$CQ" "$OUT/writes/tmp/critters"; mkdir -p "$CQ" "$OUT/writes/tmp/critters"
	          cp $D/critters/progress.ini "$CQ/progress.ini"; cp tools/tests/critters/solutions/*.sol "$OUT/writes/tmp/critters/"; }
	cfix; sim critters critters "wait;wait;$W" $CB; png critters                                   # the picker
	cfix; sim critters critters-play "$(cw 10)" $CB "SIM_ARGS=$L/expedition-01-two-ways.level --replay $S/expedition-01-two-ways.sol --until 500"; png critters-play
	cfix; sim critters critters-build "$(cw 10)" $CB "SIM_ARGS=$L/expedition-02-steel-floor.level --replay $S/expedition-02-steel-floor.sol --until <N>"; png critters-build
	cfix; sim critters critters-end "$(cw 5)" $CB "SIM_ARGS=$L/training-02-mind-the-gap.level --replay $S/training-02-mind-the-gap.sol --until end"; png critters-end
	cfix; lang fr; sim critters critters-fr "wait;wait;$W" $CB; png critters-fr
	          sim critters critters-play-fr "$(cw 5)" $CB "SIM_ARGS=$L/training-01-straight-down.level"; png critters-play-fr   # the hint card
	lang "$SHOTS_LANG"; rm -rf "$CQ"
fi
```

(The simulator maps **every** path to the card — `fakekapi.cpp` `relpath` / `sdpath`: `SIM_WRITES`, then
`SIM_OVERLAY`, then `sdcard/`; a host path like `tools/tests/…` would become `sdcard/tools/tests/…` and not be found —
so the solutions are copied into the writes' folder and passed as `SD:/tmp/critters/<base>.sol`; the same in the sim
test. `<N>` chosen so a builder's
stair and a digger's shaft are both visible; the chosen role outlined by a `key 4` before the dump.) Plus `critters`
in `build ()`'s `extra`, the FT `case` list and `APPS` (step 10). AC-25's six PNGs.

### 9.3 Simulator test — `tools/tests/run_critters_sim_test.sh` (AC-26, 27, 28)

Builds the app as `run_pinball_sim_test.sh` does (the app's sources at `-Wall -Wextra -Wno-format-truncation`, no
warning), then, each case in a fresh `SIM_WRITES`:
1. `SIM_ARGS=SD:/docs/critters/broken.level` → log `critters: refused broken.level: line <n>: <reason>`, no
   `playing`, the picker shown (dump looked at). **AC-26**
2. `SIM_ARGS=SD:/docs/critters/my-first-level.level` (02's example as an overlay fixture) → `critters: playing My First
   Level` and `overlay card`. **AC-26**
3. `SIM_ARGS=… quick.level --replay quick.sol` with `waitlog 2000 critters: end` (or `--until end`) → `end won`,
   `$SIM_WRITES/apps/critters.app/progress.ini` has `[user.quick]` `solved = 1`, `saved`, `time`, `[settings] last`.
   **AC-27**
4. `SIM_ARGS=SD:/apps/critters.app/levels/training-01-straight-down.level`, `key 13` (the card), waits until creature 0
   stands on the floor (its step and position from `crsim --where`), `down/up` on the *Digger* slot, `down/up` on
   creature 0 → log `critters: role digger c0 ok (2 left)` (and a dump before / after). **AC-28**
5. In French: case 1 again → the reason in French in the dump (by eye).
Prints `critters-sim: all N checks passed`.

### 9.4 Other checks

`python3 tools/lang/check.py critters` → 0 missing (AC-32); `shots.sh pinball invaders` (scratch PNGs) equal to the
committed ones (AC-36); `sh tools/tests/run_pinball_test.sh` and `run_circuits_test.sh` still pass (FileKit's reader
shared, unchanged).

---

## 10. Acceptance criteria → steps

| AC | Step | AC | Step | AC | Step |
|---|---|---|---|---|---|
| AC-1 | 9 | AC-14 | 6 | AC-27 | 11 (sim test 3) |
| AC-2 | 3 | AC-15 | 6 | AC-28 | 11 (sim test 4) |
| AC-3 | 3 | AC-16 | 6 | AC-29 | 9 (test complete) |
| AC-4 | 3 | AC-17 | 6 | AC-30 | 9 |
| AC-5 | 4 | AC-18 | 6 | AC-31 | 9 |
| AC-6 | 5 | AC-19 | 5 | AC-32 | 12 |
| AC-7 | 5 | AC-20 | 9 (checksum from 5) | AC-33 | 13 |
| AC-8 | 5 | AC-21 | 7 | AC-34 | 1 (grep guard), 13 |
| AC-9 | 6 | AC-22 | 7 | AC-35 | 14 |
| AC-10 | 5 | AC-23 | 8 | AC-36 | 14 |
| AC-11 | 5 | AC-24 | 8 | AC-37 | 10 (sprites), 14 (grep) |
| AC-12 | 6 | AC-25 | 14 (shots; screens from 10–12) |  |  |
| AC-13 | 6 | AC-26 | 11 (sim test 1, 2) |  |  |

---

## 11. Files — the complete list

**New:** `user/Apps/critters/{main.cpp,draw.h,bar.h,picker.h,terrain.h,terrain.cpp,level.h,level.cpp,world.h,world.cpp,
solution.h,solution.cpp,progress.h,progress.cpp}`; `sdcard/apps/critters.app/{app.txt,icon.bmp,lang/fr.txt,levels/
(12 × .level)}`; `tools/critters/{mklevels.py,crsim.cpp}`; `tools/icons/critters_icon.py`; `tools/tests/critters/
{critterstest.cpp,example.level,solutions/*.sol}`; `tools/tests/run_critters_test.sh`, `tools/tests/
run_critters_sim_test.sh`; `tools/tests/desktop_sim/critters/progress.ini`; `tools/tests/desktop_sim/sd/docs/critters/
{broken,quick,my-first-level}.level` (+ `quick.sol`); `screenshots/critters{,-play,-build,-end,-fr,-play-fr}.png`.
SHOULD: `sdcard/docs/critters/my-first-level.level`.

**Changed:** `user/Makefile` (`FT_APPS`, `FT_EXTRA_critters`, deps); `tools/tests/desktop_sim/shots.sh` (build +
scenario); `sdcard/etc/fileassoc.ini`; `tools/pkg/packages.ini`; `docs/04-USER-GUIDE.md`, `docs/03-DEVELOPER-GUIDE.md`
(if relevant), `docs/HANDOFF.md`, `IDEAS.md`, `docs/exports/*` (by `build_docs.py`).

**Untouched (the Reviewer checks):** `kernel/`, `user/Kits/` (every kit and `.abi`), `tools/tests/desktop_sim/
fakekapi.cpp`, `user/Apps/games/game.h` (or additions only), `user/Include/gamepad.h`, `tools/pkg/versions.ini`, the
other apps and their screenshots.

---

## GUI plan

*(The UX Designer, 2026-10-07 — `04-ux-design.md` "D n" / "§ n", the pictures in `mockups/` rendered by UIKit through
the desktop simulator: `sh autodev/rounds/05-critters/mockups/mockups.sh`.)*

### What the design changes in this plan

| Where in this plan | Was | Now (04) |
|---|---|---|
| §5.3 the play area | "e.g. 640 × 320 screen px = 320 × 160 logical", window size open | **fixed `Root (800, 448)`**; the `GameView` is **800 × 320 = 400 × 160 logical at ×2**; a level narrower than 400 is centred with bars `uk_tone (bg, 70)` (D1, D2) |
| §5.3 the minimap | "160 × 16 for a 1600-wide level, one sample per 10 × 10 block" | a **182 × 92** widget: `sx = 174 / w`, `sy = min (84 / h, 2·sx)`, one sample per `1/sx × 1/sy` block (the first non-empty pixel's colour), the creatures as 2 × 2 dots, exits and hatches marked, the view frame (D8); still rebuilt on `take_dirty`, ≤ every 10 steps |
| §5.3 sprites | "a palette-indexed table … or a BMP sheet (UX choice)" | **vector drawings (`VPath`) pre-rendered once at start into ARGB frames** (colour + opacity), both directions, the climber / floater marks as overlay frames, blended per frame (04 §5.5; ≈ 90 frames, ≈ 400 KB, ≈ 20 ms once) — no BMP |
| §5.3 labels | "into the colour layer … or over the terrain each frame; the UX decides" | **painted into the colour layer once, over earth / steel only** (DejaVu Sans Bold 9 px at ×1): they erode with the earth (D22) |
| §5.2 states | `V_PICKER → V_CARD → V_PLAY (+ pause menu, nuke) → V_END` | the same, with the overlays named: the **pause banner** (P / Space: not modal, scroll and choosing allowed), the **Esc card** (modal: Resume / Restart Level / Back to the levels), the **nuke card** (modal), **How to play** (F1, modal); the world ticks only in `V_PLAY` with no modal card |
| 02 §10 / §5.4 the nuke's keys | "N, then N again (or Enter) within 2 s" | **N opens a modal card that pauses the world; N again or Enter confirms, Esc / Cancel cancels — no timer** (D12) |
| §5.4 the rate keys | − / + (step unstated) | **±5 a press or click**, clamped to [level's `rate`, 99]; the `ToolButton`s disabled at the ends (D5) |
| §5.4 scrolling | "4 px a step held key or edge-scroll — the UX sizes it" | **4 logical px a frame** (Shift: 12), edge zone 8 screen px, **Home / End** jump to the ends, the wheel 24 px a notch, a **right-button drag** grabs the terrain, the minimap click / drag (04 §4.1) |
| fact 7 / R6 "make All explode obvious" | — | when all are out (or nuking) and every creature in play is a blocker: the status line's amber message, *Out* amber, the **N slot pulses** (D13) — `World` gets an accessor `onlyBlockersLeft ()` (a scan of the creatures in play; **no rule change**) |
| §5.1 log lines | — | add `critters: overlay card|pause|menu|nuke|end|help` when one shows (the sim tests wait for them) |
| §4.1 `mklevels.py` | "the UX designer's palettes go there" | the five palettes of 04 §5.3 (soil, moss, sand, ice, cave + steel, water, lava, bricks), a 3-px cap on every ground; the **steel's rivets** are a drawing rule of `build_terrain`'s colour layer (steel + `bricks` texture: a light pixel at each plate's corner — materials unchanged, AC-5 unaffected); polygons ≤ 64 points (02's limit — the mock's first ground waves broke it) |
| `fr.txt` | 02 §11's words | + 04 §8's (the status line, the cards, the picker, the help card) |
| AC-25 shots | the six PNGs | unchanged, each matching a mock scene (04 §9) |

Nothing changes in the core's rules, the formats, the tests' cases or the file list (§11) — `bar.h` and `picker.h` hold
the drawn widgets below.

### The GUI steps (within steps 10–14; each one checked by a scratch shot `SHOTS_PNG=$SCRATCH sh tools/tests/desktop_sim/shots.sh critters`, or faster by a throwaway run like `mockups.sh`)

**G1 — the frame (step 10, first).** `Root (800, 448, TR ("Critters"))`, not resizable; `CrittersView : GameView` at
(0, 0, 800, 320); `StatusLine` (0, 320, 800, 24); the `SkillBar` face (0, 344, 800, 104) with its etched separators at
x 362, 460, 602; the widgets placed as 04 §2.1's table. Test: a shot of T1 at `--until 0`: the layout of `cr-card.png`
without the card.

**G2 — the terrain view (step 10).** The ×2 blit of the visible 400 columns (03 §5.3), the letterbox, `vx` clamped,
scrolling by ← / → (Shift), Home / End, edge zone, wheel, right drag. The labels painted into `col[]` after
`build_terrain` (D22), the steel rivets. Test: shots of T1 (whole, 400 px), E6 at `vx` = 0 and at the right end.

**G3 — the sprites (step 10).** `draw.h`: the `VPath` drawings of 04 §5.1 / §5.2 (lift `critter`, `hatch`, `portal`,
`burst`, `brackets`, `digit` from `mockups/crmock.cpp`), pre-rendered at start into ARGB frames (04 §5.5) and blended; the
animation frames from `Critter::frame`; the countdown digit (13 px `FtTextFace`); the particles (drawing only, seeded
from the burst's position); the exit's pulse; the hatch's door (closed until the start card is dismissed, then open).
Test: a shot of E2 with its `.sol` at the step where a builder, a digger and an exploder are all at work — compare with
`cr-build.png`; and the style sheet of states by a test scene if useful.

**G4 — the skill bar and the status line (step 10).** `SkillSlot` (D4, D6; 11 instances: 8 roles + P, F, N), the rate
`LcdDisplay` + two `ToolButton`s (D5), `StatusLine` (D7, D13 — with `World::onlyBlockersLeft ()`), the hover text of a
slot. Repaint on change only (`LcdDisplay::setText` already does; the slots and the line keep their last values).
Test: AC-28's sim case (click the Digger slot, click creature 0 → `critters: role digger c0 ok (2 left)`) and a shot
like `cr-play.png`; the French shot like `cr-play-fr.png` (*DÉBIT*, *Bâtisseur encore 2* fit).

**G5 — giving roles and the highlight (step 10).** `World::pick` under the pointer each frame (the brackets white / red,
the status line's state and refusal words), Tab / Shift+Tab / Enter with the accent triangle (fact 4), 1…8 with the blip
on an empty slot, a click while paused blips. Test: a sim case Tab, Tab, Enter → the log's `role … ok`; a shot with the
red brackets like `cr-refuse.png`.

**G6 — the minimap (step 10).** `MiniMap` (D8): its own `Canvas` at the D8 scale, rebuilt from the terrain on dirty at
most every 10 steps; dots, marks, the frame; click / drag scrolls. Test: shots of E6 (1600 px) and T1 (400 px).

**G7 — the overlays (steps 10–11).** The start card (D14), the pause banner (D10), the fast pill (D6), the Esc card
(D11) and the nuke card (D12) with their `Button`s as root children shown with them (Tab / Enter / Esc to the focused
button, the view returns false for Tab), *How to play* (F1), the end card (D15: Next focused when won and a next level
exists, Retry focused when lost, Esc = Levels), the dimming. The log line `critters: overlay <name>`. Test: the sim
test's case 2 (`overlay card`), case 3 (`end won`), shots like `cr-card`, `cr-menu`, `cr-nuke`, `cr-won`, `cr-lost`.

**G8 — the picker (step 11).** `LevelList` (D16: groups, badges, ✓ + best, *new*, lock, the broken 40-px row, scrolling
to the chosen row, the wheel), `Preview` (cached thumbnails; dimmed + lock when locked, the warning card when broken),
`LevelInfo`, `Legend`, the *Play* `ToolButton` (disabled when locked / broken); the keys of 04 §3; `[settings] last`
chosen at start; the empty state's message box. Test: AC-25's `critters.png` from the fixture `progress.ini` (≈
`cr-picker.png`), AC-26's broken argument (≈ `cr-picker-broken.png`), a locked level chosen (≈ `cr-picker-locked.png`).

**G9 — the menus (step 11).** *Game* and *Help* of 04 §6 (Ctrl+R, P, F, N, Esc, Ctrl+O, M, Ctrl+Q, F1, *About
Critters*), disabled items on the picker. Test: `SIM_MENU` dump shows the items; Ctrl+R restarts (the log's `playing`
again).

**G10 — French and the final shots (steps 12, 14).** `fr.txt` from `mockups/sd/apps/critters.app/lang/fr.txt` + the menus
and the load errors; `check.py critters` → 0 missing; the French shots of AC-25 checked against 04 §8.1's list (the
rate's *DÉBIT*, the legend, the end card's buttons, the status line's amber message). Optional (G3's style sheet):
`screenshots/critters-sheet.png` is **not** an AC — skip it unless the docs want the roles' pictures (docs/04 could
reuse `cr-help.png`'s card instead: the *How to play* card shot in the *Critters* section).
