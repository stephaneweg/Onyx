# AutoDev round 4 — Technical Analyst: Pinball

Date: 2026-10-06. Inputs: `01-product-manager.md`, `02-product-analysis.md` (the scope, the `.table` format §7.1,
`scores.ini` §7.2, the controls §10, the 37 acceptance criteria — kept as they are; "AC n" below = 02 §12 item n).
Branch `AutoDev`.

Read for this analysis: `user/Apps/games/game.h` (`GameView`, `GameRoot`, `sfx*`, `rng*`, `gtext*`),
`user/Apps/invaders/main.cpp` (the action-game model), `user/Include/gamepad.h` (`pad_buttons`, `pad_read`, the cached
map), `user/Kits/filekit/kvtext.h` + `kvtext.inc` (`fk_kv_*`), `user/Apps/circuits/{circuit.h,circuit.cpp,main.cpp}`
(how a round-2 app reads its packs and progress through `fk_kv`, its error lines, `kapi_get_args`, `fs_basename`,
`open_face`), `user/Kits/uikit/{root.cpp,canvas.h,vpaint.h,paint.h,lang.h}`, `kernel/gui/kwin.cpp`
(`UsageToKey`, `KeyHeldAny`), `user/Makefile` (`APPS`, `FT_APPS`, `FT_EXTRA_*`, `NL_CXXFLAGS`, the FT rule),
`tools/tests/desktop_sim/{fakekapi.cpp,shots.sh,run.sh}`, `tools/tests/run_circuits_test.sh` +
`tools/tests/circuits/circuitstest.cpp`, `tools/lang/check.py`, `tools/pkg/{packages.ini,versions.ini}`,
`sdcard/apps/{circuits,invaders}.app/app.txt`, `sdcard/etc/fileassoc.ini`, round 2's `03-technical-analysis.md`.

**Summary.** Pinball is **an application only: no kapi change, no kernel change, no AppKit change, no new kit, no
kit change** (`kapi_abi.h`, `appkit.h`, `appkit_calls.inc`, `appkit.abi`, every `*.abi` untouched). Everything new
lives beside the app in `user/Apps/pinball/`: a UI-free core (`table`, `physics`, `rules`, `scores` — plain C++ that
the host test links alone) and the window (`main.cpp` + `draw.h`). It is built as a **FreeType app** (`FT_APPS`: UTF-8
French, the hardware FPU, newlib, `-lm`) with AudioKit added for `game.h`'s `sfx_*`. The only change outside the app
is **test tooling**: `fakekapi.cpp` gains `hold` / `release` script steps and answers `kapi_key_held` (§5).

### Environment (checked in this container)

| Item | State |
|---|---|
| `aarch64-none-elf-g++` | **absent** — `make` / `make stage` for the Pi are deferred to the user (AC 34 is "written, not built here", as rounds 1–3) |
| host `g++` 13.3, `python3` (+ Pillow, numpy) | present |
| `sh tools/tests/run_circuits_test.sh` (the pattern to copy) | **passes** in 5 s: `ok circuits (582 checks …)` |
| `python3 tools/lang/check.py circuits` | `0 missing` — the tool works |
| `SHOTS_TMP=… SHOTS_PNG=<scratch> sh tools/tests/desktop_sim/shots.sh invaders` | **works: 4 min 07 s** (it builds **every** app of its list whatever names are given, round 1's R7) then runs the one asked; the PNG is **byte-identical** (`cmp`) to the committed `screenshots/invaders.png` — the baseline for AC 28 / 37. Use `SHOTS_PNG=<scratch folder>` while developing so `screenshots/` is untouched |

---

## 1. What exists and is reused (by file)

| File | What Pinball takes | Notes |
|---|---|---|
| `user/Apps/games/game.h` | `GameView` (the full-window view: `paint`, `key`, `tick (dt)`, `press`), `GameRoot` (`onTick` → `view->step ()`, keys routed to the view), `sfx` / `sfx_later` / `sfx_win` / `sfx_lose` / `sfx_set_mute` / `g_sfx_mute`, `gms ()`, `gitoa` | **unchanged**. `gtext*` draws with uikit's *bitmap* font (Latin-1): **not used** for words — a FreeType app's words are UTF-8 (`lang.h`): use `uk_text_l / _c / _w` and `FtTextFace` faces (Circuits' `open_face`). `rng*` (a global in a UIKit header) is **not used by the core** (§3.5): the core has its own seeded xorshift so the host test links without UIKit. |
| `user/Apps/invaders/main.cpp` | the shape of an action game: states, `kapi_key_held` polled in `tick`, `hud_item`, `msgbox`, `notice` (theme-drawn panels: copy the three helpers), the *Game* menu (`New Game ^N`, `Pause P`, `Sound On / Off`), `root.view = g_game`, `setFocus` | Invaders is a bitmap-font `APPS` app; Pinball is an `FT_APPS` one (`ft_uikit_install` then `uk_lang_init` first in `main`, as Circuits). |
| `user/Include/gamepad.h` | `pad_buttons (-1)` → `PAD_L/R/L2/R2/LEFT/B/A/Y/START/SELECT/UP/DOWN` | header-only; the map is read once and cached (`g_pad_ncfg`); returns 0 while the window has no keyboard focus. Called once per tick. |
| `user/Kits/filekit/kvtext.h` (+ `kvtext.inc`) | `fk_kv_parse (text, 0)` for a table (repeated blocks, `fk_kv_block`, `fk_kv_block_name`, `fk_kv_block_line`, `fk_kv_line` — the error lines of §7.1.4); `fk_kv_load / get / set / text / save (…, FK_KV_ESCAPES)` for `scores.ini` (unknown sections kept on write — AC 27) | on the PC the code is inline (the host test needs no library); on the Pi it is `filekit.so` (`filekit >= 1.96`, already linked into every FT app). The `.table` reader uses `fk_kv_parse` on text the UI read, so the core does no I/O. |
| `user/Kits/filekit/fsutil.h` | `fs_basename` (the table's file name → its `scores.ini` section), `fs_ci_cmp` (the `.table` ending) | as Circuits |
| `user/Apps/circuits/circuit.cpp` 740–870 | the pattern for walking `fk_kv` blocks and reporting `line <n>: <reason>` with the key's line or the block header's line | copied as a pattern, not included |
| `user/Apps/circuits/main.cpp` | `load_packs` (`kapi_opendir/readdir/closedir` of a folder, sorted), `kapi_get_args` (a file given), `fk_kv_load` + an error message, `open_face` (`gates.h:29`), `ft_messagebox` | patterns |
| **UIKit** `canvas.h`, `vpaint.h`, `paint.h`, `lang.h`, `menu.h`, `root.h` | `Canvas::alloc` + `putOther` (an **offscreen cache** of the static table — §6.3), `VPath` (`poly`, `circle`, `line`, `polyline`, `arc`, `fill` with alpha; 1/16 px ints), `uk_rbox` (gradients), `uk_text_*`, `uk_tone`, `TR` / `uk_lang_init` / `uk_lang ()`, `Menu`, `uk_file_open` (SHOULD 1) | **no UIKit change** |
| **FontKit** `fontkit/uikitface.h` | `ft_uikit_install ("DejaVu Sans", 13)`, `FtTextFace` (big score digits), `ft_messagebox` | |
| **AudioKit** (through `game.h`) | the FM voices 12–15 | `FT_EXTRA_pinball` adds `lib/audiokit.imp.a` (as `fmtracker`, `soundconf`) |
| **AppKit** `appkit.h` | `kapi_key_held` (held keys), `KEY_LEFT/RIGHT/UP/DOWN/ENTER` (0x102/0x103/0x100/0x101/13), `kapi_get_args`, `kapi_opendir…`, `kapi_open/fsize/read` (reading a `.table`'s text; ≤ 64 KB) | existing calls only |
| `tools/tests/run_circuits_test.sh`, `circuits/circuitstest.cpp` | the host test's shape: `g++ -std=c++17 -Werror -fsanitize=address,undefined`, the include paths, the `CHECK (cond, fmt…)` macro, files given on the command line | copied for `run_pinball_test.sh` |
| `tools/tests/desktop_sim/shots.sh` | `build ()` (an FT app + `$extra` sources + `$AK`), `sim APP DUMP "SCRIPT" VARS`, `png`, `lang fr`, fixtures copied into `$OUT/writes/apps/<app>.app/` (read back first by the simulator) | a `pinball` block (§8.2) |
| `tools/lang/check.py` | scans `user/Apps/pinball/*.cpp, *.h` for `TR`/`TRC`/`TRN`, and `// TR: word` comments | the core cannot include `lang.h` → its English reasons are listed in `// TR:` comments (§3.2) |
| `tools/icons/circuits_icon.py` | Pillow → 40×40 BMP icon, magenta key | `tools/icons/pinball_icon.py` |

### 1.1 Facts found that shape the design

1. **Held keys** (`kernel/gui/kwin.cpp` `UsageToKey`, `KeyHeldAny`): only letters (folded to lower case), digits,
   Space, Enter (`'\n'`/`'\r'` → `KEY_ENTER`), Esc and the four arrows can be held. The analyst's controls (02 §10:
   ←/Z, →/M, Space/↓/Enter, ↑/N) all are. Key **events** also arrive for those keys (and may auto-repeat): during
   play the view **consumes** the flipper / plunger key events and acts only on the polled state, except the
   plunger's quick tap (§4.4).
2. **The loop's rate**: `Root::run` is `while (step ()) msleep (16)`; `gms ()` counts in 10 ms ticks (HZ = 100),
   so `GameView::step`'s `dt` is 10, 20 or 30 ms — the Pi's loop runs ≈ 50 Hz, not exactly 60. The world must step
   by a **fixed 1/60 s through an accumulator** (§4.1), never by `dt`.
3. **The simulator's clock** advances only in `msleep` (`g_ticks += ms / 10 + 1` → **20 ms per loop turn**), and each
   loop turn plays **one script step**. So in a `shots.sh` script one `wait` ≈ 1.2 physics frames; 50 `wait`s ≈ 1 s
   of play. Fully deterministic.
4. **FT apps** are compiled with `NL_CXXFLAGS` (`-mcpu=cortex-a72`, no `-mgeneral-regs-only`): floats in hardware,
   newlib, `-lm`. GCC's AArch64 default `-ffp-contract=fast` **fuses multiply-adds**, x86-64's does not → the same
   source gives different bits on the Pi and the PC unless `-ffp-contract=off` (§7, R2).
5. **`gtext` vs `uk_text`**: `gtext` draws uikit's bitmap font (Latin-1); a French word in UTF-8 would print
   garbage there. Pinball draws all its words with `uk_text_*` / `FtTextFace`.
6. **The simulator's files**: a read looks in `SIM_WRITES` first, then `SIM_OVERLAY` (files only), then `sdcard/`.
   A folder is listed from `SIM_WRITES` only when the card has no such folder. If `SD:/docs/pinball/` ships (SHOULD 1's
   sample), a fixture `.table` cannot be *listed* from the writes; it is opened through `SIM_ARGS` instead (AC 30).

---

## 2. What is missing, and where it goes (the kits-first rule)

| Missing | Where | Why there |
|---|---|---|
| 2-D vector maths, collisions (segment / circle / arc / capsule), swept flipper contact, the fixed step | `user/Apps/pinball/physics.{h,cpp}` | one program uses it; no other Onyx program does rigid-body physics (the queued Lemmings-like is pixel terrain). Not a kit. |
| The table model and the `.table` loader / validator (§7.1 of 02) | `user/Apps/pinball/table.{h,cpp}` | a format of this app; reads through FileKit's `fk_kv` (the kit already exists — **no copy of a parser**) |
| The game rules (balls, ball save, bonus × multiplier, tilt, multiball, extra ball, `[rule]` counters and actions, events out) | `user/Apps/pinball/rules.{h,cpp}` | idem |
| The top-5 high scores over an `fk_kv` document | `user/Apps/pinball/scores.{h,cpp}` | 02 SHOULD 2 decided: **beside the app this round**, written as a self-contained module (no pinball type in its API) so it can move into FileKit / SystemKit when a second game wants it — noted in `docs/HANDOFF.md` |
| The window: picker, playfield drawing, panel, name entry, menu, sounds, input | `user/Apps/pinball/main.cpp` + `draw.h` (one translation unit, as Circuits' `gates.h`/`board.h`) | UI |
| 3 tables | `sdcard/apps/pinball.app/tables/{1-space-station,2-haunted-manor,3-volcano}.table` | data |
| French | `sdcard/apps/pinball.app/lang/fr.txt` | `lang.h` catalogue |
| `app.txt`, `icon.bmp` | `sdcard/apps/pinball.app/`; `tools/icons/pinball_icon.py` | |
| Held keys in the simulator | `tools/tests/desktop_sim/fakekapi.cpp` (`hold`/`release`, `key_held`) | test-only (§5) |
| Tests | `tools/tests/pinball/pinballtest.cpp`, `tools/tests/run_pinball_test.sh`, `tools/tests/run_pinball_sim_test.sh`, fixtures in `tools/tests/desktop_sim/pinball/` and `tools/tests/desktop_sim/sd/docs/pinball/` | |

**Not needed:** no UIKit widget (the playfield and the picker are drawn in the `GameView`; the name entry is a few
keys handled by the view, or UIKit's `TextBox` in a small dialog — the UX designer decides); no `game.h` change (if the
developer wants a helper there, additions only — AC 37); no change to any kit, so **no `.abi`, no `kitdocs.py`, no
version raise of a kit**.

### 2.1 kapi changes

**None.** `kernel/include/kern/kapi_abi.h`, `kernel/sys/kapi*.cpp`, `user/Kits/appkit/appkit.h`,
`appkit_calls.inc`, `appkit.abi` stay untouched (AC 35: `git diff --stat origin/main -- kernel user/Kits` must list
nothing). Left/Right Shift as flippers would need the held-key table to learn modifiers — deferred (02 LATER).

---

## 3. The core (no UIKit, no I/O) — `namespace pinball`

Constraints for every core file: compiles with host `g++ -std=c++17 -Wall -Wextra -Werror` **and** the Pi's newlib
`-fno-exceptions -fno-rtti`; **no STL**, fixed arrays sized by the limits of 02 §7.1.2 (the `Table` and the `World`
are allocated once with `new`, never on the stack); `<string.h>`, `<stdlib.h>`, `<stdio.h>` (`snprintf`) and
`<math.h>` (`sqrt` only, §3.3) allowed; MIT notice at the top. Includes: `table.h` includes `filekit/filekit.h`
(for `fk_kv`) — nothing else from the kits.

### 3.1 `table.h` — the model

```cpp
namespace pinball {
struct Vec { double x, y; };
enum { MAXSEG = 2000, MAXCIRC = 64, MAXARC = 32, MAXTGT = 64, MAXLANE = 32, MAXGATE = 16, MAXRAMP = 4,
       MAXFLIP = 3, MAXRULE = 32, MAXSHAPE = 128, MAXLABEL = 64, MAXPTS = 64 /* per path / shape */, IDL = 25 };
struct Text { char en[164], fr[164]; const char *get (int lang) const; };   // lang 1 = fr, falls back to en
enum SegKind { S_WALL, S_SLING, S_TARGET };          // gates and ramp entries are their own arrays
struct Seg   { Vec a, b; double bounce, friction; int kind, elem /* index in slings/targets, -1 */, wall /* drawing */; };
struct Circle{ Vec c; double r, bounce, kick; int kind /* C_POST, C_BUMPER */, score; unsigned colour; char id[IDL]; };
struct Arc   { Vec c; double r, from, to /* degrees, clockwise (y down) */, bounce, friction; unsigned colour; int width; };
struct Wall  { int first, n /* segments */; unsigned colour; int width; bool outline; };
struct Sling { char id[IDL]; int seg, score; double kick; unsigned colour; };
struct Target{ char id[IDL]; int seg, bank /* -1 standup */, score; unsigned colour; };
struct Bank  { char id[IDL]; int first[8], n; };
struct Lane  { char id[IDL]; double x, y, w, h; int group /* -1 */, score; unsigned colour; };
struct Gate  { Vec a, b, pass /* unit vector of the allowed crossing */; };
struct Flip  { int side /* 0 left, 1 right */; Vec pivot; double len, rest, up /* radians, already mirrored */,
               r0, r1, speed /* rad/s */, bounce; unsigned colour; };
struct Ramp  { char id[IDL]; Vec a, b, pass; Vec path[MAXPTS]; int npath; double time; Vec out; int score; unsigned colour; int width; };
struct Saucer{ char id[IDL]; Vec c; double r, hold; Vec out; int score; unsigned colour; };
struct Shape { Vec p[MAXPTS]; int n; unsigned colour; };
struct Label { Vec at; Text text; int size, angle; unsigned colour; };
enum EvKind { EV_BANK, EV_LANES, EV_RAMP, EV_SAUCER, EV_HIT };               // a [rule]'s "when"
enum ActKind { A_SCORE, A_BONUS, A_MULT, A_MULTIBALL, A_EXTRABALL, A_BALLSAVE };
struct Action { int kind; long n; };
struct Rule  { int when, ref /* bank / group / ramp / saucer index, or an element key for EV_HIT */; int count; bool once;
               Action act[8]; int nact; Text message; };
struct Table {
	Text name, goal; double w, h, gravity, ball, ballsave, drain; int balls, rotate /* group, -1 */; unsigned background;
	Seg seg[MAXSEG]; int nseg; Wall wall[...]; Circle circ[MAXCIRC]; ... ; Vec plunger; double plungerMax, plungerAuto;
	char groups[MAXLANE][IDL]; int ngroups;                                  // lane group names
	Vec outline[MAXSEG]; int noutline;                                       // the outline polygon (the containment test)
};
struct LoadError { int line; char reason[96]; };                             // English, "unknown block [bumber]"
bool load_table (const char *text, Table &t, LoadError &e);                  // false: t unusable (cleared)
bool inside_outline (const Table &t, Vec p);                                 // point-in-polygon (AC 6)
}
```

- `load_table` = `fk_kv_parse (text, 0)`, then **one pass per block in file order** (`fk_kv_blocks`,
  `fk_kv_block_name`; entries by `fk_kv_block (doc, i) == b`), then the cross-checks (outline, flippers, plunger,
  banks, rule references) and `fk_kv_free`. A file > 64 KB is refused by the caller before parsing (`line 0: the file
  is too big`); the UI reads the file with `kapi_open/fsize/read` (FileKit's `fk_load` is **not** used: on the PC it lives in
  `fkcore.cpp`, which needs zlib — round 2's gap).
- **Values** are parsed by a small hand-written reader (`number := ["-"] digits ["." digits]`, no `strtod` — its
  locale and exponent rules are not the format's): `read_num`, `read_point`, `read_points` (spaces and/or one comma
  between points), `read_colour` (`#RRGGBB`), `read_bool`, `read_id` (`[a-z0-9_-]{1,24}`). Keys ending in `.fr` fill
  `Text::fr`. Unknown keys inside a known block are skipped (AC 4); an unknown block is an error.
- **Every error** of 02 §7.1.4 is produced with `snprintf (e.reason, …)` — the English reason; `e.line` = the
  offending entry's `fk_kv_line`, or the block's `fk_kv_block_line` for a missing key; 0 for file errors. The reasons'
  format strings are listed in a comment block `// TR: unknown block [%s]` … so `check.py` asks for their French and
  the UI translates **the format** before filling it: the loader keeps `e.fmt` (the English format, a pointer to a
  static string) and `e.arg` (the one `%s` / `%d` argument) besides `e.reason`; the UI does
  `snprintf (buf, n, TR ("line %d: "), e.line)` + `snprintf (…, TR (e.fmt), e.arg)`. The tests compare `e.reason`.
- Flipper angles: the file's `rest` / `up` are degrees **below the horizontal pointing to the table's centre**; the
  loader turns them into the world's angle (left flipper: θ = +deg, pointing +x; right: θ = 180° − deg, pointing −x;
  y down) in radians, once, with `sin`/`cos` from the core's own polynomial (§3.3).
- Arcs are kept as arcs (exact circle collision restricted to the angular range), not flattened.
- An **AABB** is stored for every collider (segment, circle, arc, flipper sweep) for a cheap reject (§7, R4).

### 3.2 `physics.h` — the world

```cpp
namespace pinball {
enum { MAXBALL = 3 };
struct Ball  { Vec p, v; bool live, onPlunger, onRamp, inSaucer; int ramp, saucer; double t /* ramp/saucer timer */;
               bool inLane[MAXLANE]; };
struct Input { bool left, right, plunger /* held */, tap /* a plunger key event with no hold seen */; };
struct PEvent { int kind; int index; double speed; };   // P_BUMPER, P_SLING, P_TARGET, P_DROP, P_BANK, P_LANE, P_LANES,
                                                         // P_GATE, P_RAMP, P_SAUCER, P_EJECT, P_FLIPPER_UP, P_LAUNCH, P_DRAIN, P_BALLHIT
struct World {
	const Table *t;
	Ball ball[MAXBALL]; double flipAng[MAXFLIP], flipW[MAXFLIP] /* rad/s now */;
	bool flipperDead;                       // tilt
	double pull; int pullFrames;            // the plunger
	bool tgtDown[MAXTGT]; double bankReset[MAXTGT /* per bank */]; bool laneLit[MAXLANE]; double bumperFlash[MAXCIRC];
	PEvent ev[64]; int nev;                 // this frame's events (cleared at the start of step)
	unsigned rng;                           // xorshift32, seeded by the Game
	long subSteps;                          // statistics (the tests' budget)
	void reset (const Table &t, unsigned seed);
	int  addBall (Vec p, Vec v);            // -> index, -1 full (tests place balls directly)
	void placeOnPlunger (int i);
	void step (const Input &in);            // ONE frame = 1/60 s
	void rotateLanes (int dir);             // the rotate group's lit lamps shifted (-1 left, +1 right), with wrap
	void nudge ();                          // every live ball's v += 150 * (±0.6, -0.8), side from rng
	int  liveBalls () const;
};
}
```

**`World::step (in)`** — one frame, in this order (the order is part of determinism):

1. Flippers: target angle = `up` if (held and not `flipperDead`) else `rest`; a press edge emits `P_FLIPPER_UP`
   (the Game rotates the lanes on it, 02 #10). The angular speed is `speed` towards the target, 0 at the end stop.
2. Plunger: held → `pullFrames++`, `pull = min (1, pullFrames / 60)`; released after > 6 frames → the ball resting on
   the plunger gets `v = (0, −max × pull)`; released after ≤ 6 frames, or `in.tap` → `v = (0, −auto)`; emits
   `P_LAUNCH` with the speed (AC 12: measured from the event, before gravity acts).
3. **Sub-steps**: `n = max (8, ceil (maxTravel / (0.5 × ballRadius)))` where `maxTravel = (|v|max + |ω|max × len +
   gravity / 60) / 60` over the live balls and moving flippers; `h = (1/60) / n`. For each sub-step:
   a. flippers advance by `ω h` (clamped at their stop);
   b. each live ball not on a ramp / in a saucer / on the plunger: `v.y += g h`; speed capped at 4000; `p += v h`;
   c. collisions, for each ball, in a fixed order (segments, arcs, circles, flippers, gates, ball–ball), each
      resolved at once (position pushed out along the normal, velocity reflected): §3.3;
   d. sensors on the centre's path from the sub-step's start to its end: lanes (entry into the rectangle), ramp
      entries (crossing in `pass` direction), saucers (centre within `r` and speed < 1500 and the saucer empty →
      caught), the plunger zone (a ball falling back onto the plunger is caught: `p = plunger`, `v = 0`), the drain
      (`p.y > drain` → `live = false`, `P_DRAIN`).
4. Timers in frames: bumper flash (9 frames = 0.15 s), bank reset (60 frames: the bank's targets rise), saucer
   hold (`hold × 60` frames → eject: `p = c`, `v = out`, `P_EJECT`), ramp transit (`time × 60` frames → the ball
   reappears at `path[n−1]` with `v = out`).

**Events**, not scores, leave the world: `PEvent` carries what was hit; the Game (rules) decides points (a tilted ball
scores nothing — AC 26).

### 3.3 Collisions — the method (tunnelling-proof, deterministic)

- **Numbers**: `double` throughout (the A72 does scalar double at full speed; it removes most precision worries at
  4000 units/s). Only `+ − × ÷` and `sqrt` (IEEE-exact on both machines) in the step. **No libm transcendental in the
  step or the loader**: `sin`/`cos` are the core's own (`pb_sin`/`pb_cos`: range-reduced, a degree-9 polynomial —
  enough for a flipper), `atan2` is never needed (the arc's range test uses cross products against the precomputed
  `from`/`to` unit vectors). With `-ffp-contract=off` on both builds (§7), a run is the same bit for bit on the PC and
  the Pi — a recorded input script replays identically (a debugging gift, not required by an AC).
- **Segment** (walls, slings, standup and up drop targets): closest point `q` on `[a,b]` to the centre; if
  `|p − q| < r`: **the normal side is decided from the centre's position at the sub-step's start** (its signed
  distance to the segment's line), not the end — a centre that crossed the line inside one sub-step is pushed back to
  the side it came from. Endpoints are round caps (the closest point is an endpoint). Response: `vn = v·n`; if
  `vn < 0`: `v −= (1 + e) vn n`, and the tangential part scaled by `(1 − friction)` per contact (not per sub-step:
  only on an approaching contact, `vn < −5`), `e = 0` when `|vn| < 30` (resting contact: no jitter).
- **Circle** (posts, bumpers): the same against the centre. A **bumper** sets the outgoing normal speed to
  `max (e |vn|, kick)` and emits `P_BUMPER` (flash on); a **sling** (a segment) does so only when `|vn| ≥ 200`
  (`P_SLING`), else a plain bounce (AC 14).
- **Arc**: a circle of radius `R`; the ball collides with its inner face if `R − |p − c| < r` and with its outer face if
  `|p − c| − R < r`, only when the contact direction is within `[from, to]` (cross products); the side from the
  sub-step's start, as segments; the arc's ends are round caps.
- **Gate** (one-way): with `d = (p − a)·pass` the signed distance along the allowed direction, the gate is **ignored
  while `d_start < 0`** (the ball has not crossed); when `d_start ≥ 0` and the ball comes back (`v·pass < 0`) within
  `r` of the segment, it is a wall seen from the pass side. A ball can go through one way only (AC 17); the shooter
  gate keeps balls out of the lane from above (AC 12).
- **Flipper** (a capsule: a segment from the pivot to the tip, radius interpolated `r0 → r1`): the contact is found in
  the **flipper's rotating frame**: the ball's centre is expressed relative to the pivot in the flipper's frame at
  the sub-step's start angle and end angle; if the side (above / below the capsule's axis) changed, or the distance
  to the axis is below `r + rLocal`, it collides **from the start side** — so a fast flipper never passes through a
  ball (AC 11). The contact point's velocity `u = ω × (q − pivot)` is used: `vrel = v − u`; `vrel·n < 0` →
  `v = u + vrel − (1 + e)(vrel·n) n` — a moving flipper strikes (AC 10), a still one is a ramp.
- **Ball–ball** (multiball): equal masses, `e = 0.9`, positions separated half each.
- **Drop target down**: its segment is skipped. **Lanes, ramp entries, saucers** are sensors (no collision).
- **Dead spots**: a ball can come to rest only on the plunger, in a saucer, or cradled on a raised flipper. The game
  adds a **ball search** (real machines have one): a live ball moving less than 2 units in 4 s outside those three
  places gets a kick of 300 units/s in a seeded direction. AC 7 checks the tables need it rarely; the counter
  `World::searches` lets the test assert it stays 0 on the shipped tables (the tables are fixed, not the rule
  loosened, if it fires).

### 3.4 `rules.h` — the game

```cpp
namespace pinball {
enum GState { G_READY /* ball on the plunger */, G_PLAY, G_BALLEND /* bonus counted 1.5 s */, G_OVER };
enum GEvKind { GE_SOUND, GE_MESSAGE, GE_MULTIBALL, GE_EXTRABALL, GE_BALLSAVED, GE_TILTWARN, GE_TILT, GE_BALLEND, GE_GAMEOVER };
struct GEvent { int kind; int index /* PEvent kind for sounds, rule for messages */; long value; };
struct Game {
	World w; const Table *t;
	GState state; long score, bonus, lastBonus; int mult, ball /* 1-based */, extraBalls, multiballQueue;
	int tiltFrames[3]; int nudges; bool tilted; long frame, launchFrame; double ballSaveUntil;
	int ruleCount[MAXRULE]; bool ruleDone[MAXRULE];
	GEvent ev[64]; int nev;                 // this frame's, for the UI (sounds, messages)
	void start (const Table &t, unsigned seed);
	void frame (const Input &in, bool nudge); // one 1/60 s frame: inputs → World::step → events → rules → timers
	void skip ();                              // the bonus count skipped (any key)
	void fire (const Action &a);               // used by rules; public for the tests and the sim's --start
};
}
```

- Scoring (`P_BUMPER`, `P_SLING`, `P_TARGET`, `P_DROP`, `P_LANE`, `P_RAMP`, `P_SAUCER`): `score += s` and
  `bonus += (s / 10) / 10 * 10` unless tilted (AC 13, 22). `P_BANK`, `P_LANES`, `P_RAMP`, `P_SAUCER` and every hit
  (`EV_HIT` by element) advance the matching `[rule]` counters; at `count` → actions in order, `GE_MESSAGE`, the
  counter back to 0; `once` → `ruleDone` (AC 23). Counters live for the whole game.
- `P_FLIPPER_UP` (not tilted) → `w.rotateLanes (side ? +1 : −1)`; `P_LANES` darkens the group (AC 16).
- Drain logic (`P_DRAIN`): if other balls live → nothing more (multiball end when one is left: AC 24); if the last:
  ball save active (`frame − launchFrame < ballsave × 60` and not in multiball) → `GE_BALLSAVED`, a ball back on the
  plunger, `ball` unchanged (AC 21); else `lastBonus = tilted ? 0 : bonus × mult`, `score += lastBonus`,
  `G_BALLEND` for 90 frames (or `skip ()`), then: `extraBalls > 0` → `extraBalls−−`, same ball number (AC 25); else
  `ball++`; `ball > balls` → `G_OVER` (`GE_GAMEOVER`) (AC 20). A new ball resets `bonus = 0`, `mult = 1`,
  `tilted = false`, the flippers alive.
- Multiball `n`: refused while 2+ balls live; else `multiballQueue = n − live`; a queued ball is put on the plunger
  and auto-launched (`auto` speed) every 30 frames (AC 24). The ball save does not apply while 2+ balls play.
- Nudge (`nudge` true, an edge): `w.nudge ()`; the frames of the last nudges within 300 frames counted: 2 →
  `GE_TILTWARN`, 3 → `GE_TILT`, `tilted = true`, `w.flipperDead = true` (AC 26).

### 3.5 `scores.h` — top 5 over `fk_kv`

```cpp
namespace pinball {
struct ScoreLine { long score; char name[17]; };
int  scores_read (const fk_kv *kv, const char *section, ScoreLine out[5]);   // -> count; broken lines skipped
int  scores_add (fk_kv *kv, const char *section, long score, const char *name); // -> rank 0..4, -1 not in the top 5
bool scores_qualifies (const fk_kv *kv, const char *section, long score);
void scores_section (const char *path, bool shipped, char *out, int cap);    // "1-space-station" / "user.my-table"
const char *scores_setting (const fk_kv *kv, const char *key, const char *def); // [settings] name, sound
void scores_set_setting (fk_kv *kv, const char *key, const char *value);
}
```

Equal scores: the new entry goes **below** the older ones (AC 27); `=` and new lines in a name become spaces; 1…16
characters. The UI loads `SD:/apps/pinball.app/scores.ini` with `fk_kv_load (…, FK_KV_ESCAPES)` (a missing file →
`fk_kv_new (FK_KV_ESCAPES)`) and saves with `fk_kv_save (kv, path, "Pinball -- high scores (written by the game)")`
after each new entry and each sound toggle.

---

## 4. The window — `main.cpp` + `draw.h`

### 4.1 The frame loop

`class PinballView : GameView` with `tick (dt)`: `acc += dt` (ms); `while (acc >= 50/3 && n < 3) { game.frame
(input, nudgeEdge); acc −= 50/3; n++; }` — integer arithmetic in thousandths of a ms (`acc_us += dt*1000; step =
16667`) so it is deterministic; more than 3 steps late → `acc = 0` (the game slows rather than jumps, 02 #3). Then the
`GEvent`s → `sfx` and the message line; `redraw ()`. Pause: no frames.

### 4.2 States of the view

`V_PICKER` (the list of tables, thumbnail, goal, top 5; ↑/↓/Enter/Space/A; a click chooses, a double click plays) →
`V_PLAY` (with `Game::state` inside) → `V_NAME` (game over with a top-5 score: the name field, Enter/A keeps it) →
`V_SCORES` (the top 5 with the new line highlighted; any key → picker). `V_PAUSED` overlays play; Esc in play → a
*Resume / Back to tables* notice. `pinball <file>` (`kapi_get_args`) starts that table at once (AC 30).

### 4.3 Input per tick

```
left  = held (KEY_LEFT) || held ('z') || pad & (PAD_L | PAD_L2 | PAD_LEFT)
right = held (KEY_RIGHT) || held ('m') || pad & (PAD_R | PAD_R2 | PAD_B)
plunger = held (' ') || held (KEY_DOWN) || held (KEY_ENTER) || pad & PAD_A
nudge edge = key event ↑ / 'n' / 'N', or the rising edge of pad & PAD_Y
```

The pad's Start (pause), Select (back to the tables) and its d-pad in the picker are edge-detected from
`pad_buttons (-1)`. **`key ()` during play returns true for ←, →, z, m, Space, ↓, Enter without acting**, except: a
Space/↓/Enter event while no plunger hold has been seen since the last frame sets `input.tap` (a press shorter than a
tick still launches; the simulator's `key 32` too). `s` toggles the sound (`sfx_set_mute`, saved in `[settings]`), `p`
pauses, Ctrl+N new game, Esc as above.

### 4.4 Drawing (`draw.h`) — performance

- **Scale**: `s = min (fieldW / t.w, fieldH / t.h)`, the playfield centred in its area (any table size of 200…2000
  units fits, letterboxed). Units → 1/16 px for `VPath`: `V = (int) (u × s × 16)`.
- **The static layer is drawn once** into an offscreen `Canvas` (`alloc (fieldW, fieldH)`) when the table is chosen:
  the background, `[shape]`s, walls (`polyline`), arcs, posts, slings' bodies, the lanes' guides, the ramps' bands,
  `[label]`s (`uk_text_*`; `angle 90` drawn into a small canvas and rotated by hand, or limited to 0 if the developer
  finds it costly — a UX choice). **Each frame**: `canvas.putOther (staticLayer, x + shake, y, false)` (one memcpy a
  row), then the dynamic parts with `VPath`: bumpers (flash), targets (up / down), lane lamps, flippers (a capsule
  `line` with round ends at the true angle), the plunger's spring, the balls (a disc + a highlight), a ball on a ramp
  drawn along its path; then the panel (score with a large `FtTextFace`, ball n/3, ×mult, best, bonus, message).
  A 600 × 700 client is ~420 k pixels: one blit and a few dozen small anti-aliased shapes per frame — the same order
  as Invaders, which redraws everything each tick.
- **The picker's thumbnails**: the same static-layer function at a small scale, cached per table (≤ 16 tables cached;
  the rest drawn on demand).
- **Window size**: the UX designer fixes it; the analysis recommends a fixed client of about **600 × 700**
  (playfield ≈ 345 × 690 at s ≈ 0.66 for 520 × 1040 tables, panel ≈ 230 wide) — with the frame and the menu bar
  ≈ 760 px tall, so a 1280 × 800 screen fits too (02 §4.4).

---

## 5. The simulator change (test tooling only)

`tools/tests/desktop_sim/fakekapi.cpp`:

```cpp
static unsigned g_held[(0x200 + 31) / 32];                // the keys the script holds
static int held_key (const char *arg)                     // as "key": a code (0x102) or a character
{ long k = arg[1] ? strtol (arg, 0, 0) : arg[0];
  if (k >= 'A' && k <= 'Z') k += 'a' - 'A';               // (kwin.cpp KeyHeldAny's folding)
  if (k == '\n' || k == '\r') k = KEY_ENTER; return (int) k; }
...
else if (!strcmp (cmd, "hold") || !strcmp (cmd, "release"))
{
	sscanf (st.c_str (), "%*s %255s", arg); int k = held_key (arg);
	if (k > 0 && k < 0x200) { if (cmd[0] == 'h') g_held[k >> 5] |= 1u << (k & 31); else g_held[k >> 5] &= ~(1u << (k & 31)); }
	if (cmd[0] == 'h' && g_key) g_key (0, GUI_EVENT_KEY, k);   // the kernel sends the press event too
}
...
static int key_held (int k) { if (k >= 'A' && k <= 'Z') k += 'a' - 'A'; if (k == '\n' || k == '\r') k = KEY_ENTER;
                              return k > 0 && k < 0x200 && ((g_held[k >> 5] >> (k & 31)) & 1); }
```

Plus the two lines in the script's comment at the top (`hold KEY` / `release KEY`). Nothing changes while no script
uses them → every existing screenshot is the same (AC 28, 37 — checked with `shots.sh invaders` before/after: the
PNGs compare equal).

---

## 6. The data

### 6.1 The three tables

`sdcard/apps/pinball.app/tables/1-space-station.table`, `2-haunted-manor.table`, `3-volcano.table` (02 §6: their
elements, goals and rules), 520 × 1040 units each, every `.fr` key present. **A common skeleton** (the developer and the
UX designer may move things, the tests decide; the playfield's mirror is `x' = 476 − x`):

```
outline    0 0, 520 0, 520 1040, 0 1040 (closed)       arc  centre 260 260, radius 258, from 180 to 360
shooter    wall 476 1040 → 476 300; gate 476 300 → 518 300 pass up; plunger at 498 980
outlanes   left between the outline (x 0) and a divider wall 38 700 → 38 880 (a post r 6 on its top);
           right between 438 700 → 438 880 and the shooter wall (x 476)
inlanes    left between the divider and the guide 72 740 → 72 850 → 140 896; right 404 740 → 404 850 → 336 896
flippers   left pivot 150 905, right 326 905, length 70, rest 30, up −25 (the tips 55 apart at rest: a ball drains)
slings     faces 92 760 → 130 850 and 384 760 → 346 850 (each the hypotenuse of a closed triangular [wall] body)
drain      1010
lanes      inlanes / outlanes as [lane] sensors (groups in / out); the top lanes (group top) under the arc, separated by posts
```

The ball's diameter is 26: every channel ≥ 30 wide, every gap the ball must not pass < 24. *Volcano*'s third flipper
(`side = right`) on the upper right feeding the ramp.

### 6.2 Fixtures for the tests

- `tools/tests/pinball/` — `example.table` (02 §7.1.3 verbatim, AC 2, 8, 16, 23), `broken/*.table` **generated by the
  test** from the example (one line changed per §7.1.4 case — AC 3; generating them in code keeps them in sync), a
  `rules.table` if needed (a tiny table with a sling, a bumper, a gate, a ramp, a saucer placed for the element tests
  13–19 so each can be shot at directly).
- `tools/tests/desktop_sim/pinball/scores.ini` — the picker's top 5 for the three tables (AC 29).
- `tools/tests/desktop_sim/sd/docs/pinball/broken.table` (a bad block on a known line) and `quick.table`
  (`balls = 1`, `ballsave = 0`) — opened with `SIM_ARGS` (AC 30, 31).
- SHOULD 1: `sdcard/docs/pinball/my-first-table.table` (the example, commented) if the checker is done.

---

## 7. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | **Tunnelling** through walls (zero-thickness segments) and flippers | adaptive sub-steps (≤ half a ball radius per sub-step, ≥ 8), the side taken from the sub-step's **start** position, the flipper tested in its rotating frame, speed cap 4000; AC 6, 8, 11 hunt for it (500 launches × 3 tables, 64 angles at full speed, frame offsets 0…10) |
| R2 | **Floating-point determinism** (AC 5) | the same binary is deterministic by construction (no clock, no uninitialised data, one seeded rng in `World`, fixed iteration order, fixed 1/60 frame). Cross-machine: `-ffp-contract=off` for the app (`pinball.elf: NL_CXXFLAGS += -ffp-contract=off` — the FT rule compiles the sources in one command; a target-specific variable reaches them all) and in `run_pinball_test.sh`; no libm transcendental (own `pb_sin/pb_cos`); never `-ffast-math`. The UI's frame accumulator is integer. |
| R3 | **Feel** (too floaty / too fast, balls stuck, flippers too weak) | the constants are the table's (`gravity`, `bounce`, `kick`, flipper `speed`, plunger `max`/`auto`), tuned on the PC with the simulator; AC 7 (no dead spot), 9 (drain between the flippers), 10 (a flipper shot reaches the upper half) pin the minimum; final tuning on the Pi is the user's (02 LATER) |
| R4 | **Performance at 60 Hz on the Pi** | physics: ≤ 3 balls × ~10 sub-steps × (a few hundred colliders after an AABB reject) ≈ 10⁴ cheap tests a frame — well under 1 ms on an A72. Drawing: the static layer cached (§4.4), one blit + small shapes a frame. The real loop runs ≈ 50 Hz (`msleep (16)` on 10 ms ticks): the accumulator keeps the physics at 60 steps/s. If a frame is slow, at most 3 steps catch up. |
| R5 | **Host test run time** (500 launches × 3 tables × up to 120 s, AC 6) | the test is built twice by the script: **ASan/UBSan at `-O1`** running every case with reduced counts (`--quick`: 50 launches) and **`-O2` without sanitizers** running the full counts. Budget: < 60 s total; report the sub-steps simulated. A launch ends at the drain, or when the ball sits on the plunger (a weak launch that fell back — relaunched by the test with `tap`, counted), or at 120 s. |
| R6 | **Portrait table in a window** (scaling, rounding) | one scale factor from the table's size; integer 1/16-px `VPath` coordinates; a table of any aspect letterboxed; the thumbnail from the same code. A label's `size` maps to faces of fixed pixel sizes (no scaling of text); 02 says text fits — checked on the French shot. |
| R7 | **Multiball / ramp / saucer edge cases** (a ball reappearing onto another, two balls in one saucer, a ball held during tilt) | a saucer holds one ball (the next rolls over); ball–ball separation resolves overlaps; a tilted ball in a saucer is still ejected; each covered by an E test (18, 19, 24) |
| R8 | **Key auto-repeat** sending flipper keys as events | play ignores those events (§4.3); lane rotation uses the polled press edge |
| R9 | The sim builds every app for each `shots.sh` call (minutes) | develop with the host test; run `shots.sh pinball` a few times only, `SHOTS_PNG=<scratch>` until final |
| R10 | `-Werror` on the PC vs newlib on the Pi (e.g. `snprintf` truncation warnings) | sizes chosen with margin; the test script compiles the core with `-Wall -Wextra -Werror` (Circuits' rule) |

---

## 8. Step-by-step implementation plan (each step testable, in order)

> Build checks along the way: `sh tools/tests/run_pinball_test.sh` (the core), `python3 tools/lang/check.py pinball`,
> `SHOTS_PNG=$SCRATCH sh tools/tests/desktop_sim/shots.sh pinball` (the window). The Pi build (`make` from `kernel/`)
> cannot run here: write the Makefile lines carefully and say so in `06-development.md`.

**Step 1 — scaffolding.** `user/Apps/pinball/{table.h,table.cpp,physics.h,physics.cpp,rules.h,rules.cpp,scores.h,
scores.cpp}` with the MIT notice and empty bodies; `tools/tests/pinball/pinballtest.cpp` with `CHECK` and a `main`
taking table files; `tools/tests/run_pinball_test.sh`:

```sh
set -e; cd "$(dirname "$0")/../.."
SRC="user/Apps/pinball/table.cpp user/Apps/pinball/physics.cpp user/Apps/pinball/rules.cpp user/Apps/pinball/scores.cpp"
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Apps -I kernel/include"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    $INC tools/tests/pinball/pinballtest.cpp $SRC -o "${TMPDIR:-/tmp}/onyx_pinball_asan"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -ffp-contract=off $INC tools/tests/pinball/pinballtest.cpp $SRC -o "${TMPDIR:-/tmp}/onyx_pinball"
T=${*:-sdcard/apps/pinball.app/tables/*.table}
"${TMPDIR:-/tmp}/onyx_pinball_asan" --quick $T && "${TMPDIR:-/tmp}/onyx_pinball" $T
```

Test: the script compiles and prints `ok pinball (0 checks)`.

**Step 2 — the loader** (`table.cpp`): values, every block of §7.1.2, defaults, limits, cross-checks, every error of
§7.1.4 with its line, `inside_outline`, the AABBs, `pb_sin/pb_cos`. Test: AC 2 (the example loads; counts of each
element), AC 3 (every broken case: the exact reason and line; a refused load leaves `nseg == 0` etc.), AC 4.

**Step 3 — the static physics** (`physics.cpp`): ball, gravity, sub-steps, segments, arcs, circles (posts), gates,
the plunger catch, the drain, the outline invariant. Test: AC 8 (64 angles at 4000), AC 9 (drain between the flippers
at rest — flippers as static capsules at `rest`), AC 17 (gate), a ball dropped in the shooter lane comes to rest on the
plunger.

**Step 4 — flippers and plunger.** Rotating capsules, swept contact, ω × r; the plunger's pull, launch and tap.
Test: AC 10 (left and mirrored right), AC 11 (frame offsets 0…10), AC 12 (pull 1 s → `max` ± 1 %, 0.5 s → half ± 2 %,
tap → `auto`; the ball passes the gate and never comes back into the lane).

**Step 5 — the elements**: bumpers (kick, flash 9 frames), slings (≥ 200 threshold), targets (drop / standup, banks,
the reset after 60 frames), lanes (entry, lit, `rotateLanes` with wrap, the group complete), ramps (transit, `out`),
saucers (catch < 1500, hold, eject, one ball), ball–ball, ball search. Test: AC 13–19 on the example / `rules.table`,
each by placing a ball with a chosen velocity (`World::addBall`).

**Step 6 — the rules** (`rules.cpp`): `Game::start/frame/skip/fire`, scoring and bonus, drains and the ball count,
ball save, extra ball, multiball queue, tilt, `[rule]` counters. Test: AC 20–26 (drive `Game::frame` with scripted
`Input`s; place balls straight into the drain to make drains happen at chosen frames).

**Step 7 — the three tables.** Write them on the skeleton of §6.1 (positions agreed with the UX mock-ups), with every
`.fr` key. Test: AC 1, then the statistical ones on each: AC 5 (a scripted 2-minute game twice: identical score and a
hash of every ball position per frame), AC 6 (500 launches: inside the outline at every sub-step, speed ≤ 4000), AC 7
(200 launches without flippers: drained within 60 s; `searches == 0`). **Iterate on the tables (not the physics'
guarantees) until they pass**; record the run time.

**Step 8 — high scores** (`scores.cpp`). Test: AC 27 (6 games 10, 50, 30, 50, 20, 5 → 50, 50, 30, 20, 10, the first 50
first; written with `fk_kv_text`, re-parsed, same lines; `=` in a name; a broken line skipped; an unknown section
kept). → **`run_pinball_test.sh` is complete (AC 32)**.

**Step 9 — the simulator's held keys** (§5). Test: `shots.sh invaders` before and after: identical PNGs (compare with
`cmp`); a 3-line script with `hold 0x102` in a throw-away check (or the next step's shot) shows `key_held` true.

**Step 10 — the window, play first** (`main.cpp`, `draw.h`): `FT_APPS` app — `ft_uikit_install`, `uk_lang_init`,
`GameRoot` + `PinballView`, `pinball <file>` plays at once, the static layer cache, the dynamic drawing, the panel, the
accumulator, input (§4.3), sounds per `GEvent` (bumper, sling, flipper, target / drop, lane, ramp, saucer catch /
eject, drain; `sfx_win` on multiball / extra ball, `sfx_lose` at game over), pause, the menu. Add to `shots.sh`:
`[ "$1" = pinball ] && extra="user/Apps/pinball/table.cpp user/Apps/pinball/physics.cpp user/Apps/pinball/rules.cpp
user/Apps/pinball/scores.cpp"`, `pinball` in the FT `case` list and in `APPS`. Test: a shot of *Volcano* in play
(`SIM_ARGS=SD:/apps/pinball.app/tables/3-volcano.table`, `key 32`, waits, `hold 0x102`, waits, dump).

**Step 11 — the picker, the name entry, the scores file.** The tables' list (shipped in order, then
`SD:/docs/pinball/*.table` by name; a broken one greyed with its error), thumbnail, goal, top 5; game over → name entry
(`[settings] name` default; "Player" / "Joueur" by `TR`) → top 5; `scores.ini` saved. The `--start multiball` switch
(below). Test: the picker shot with the fixture `scores.ini`; AC 30 with `broken.table`; AC 31 with `quick.table`.

**Step 12 — French.** Every word in `TR` (the loader's reasons via `// TR:` comments and `TR (e.fmt)`), the tables'
`.fr` texts by `uk_lang ()`, `sdcard/apps/pinball.app/lang/fr.txt`. Test: `python3 tools/lang/check.py pinball` → 0
missing (AC 33); the French picker shot checked by eye (AC 29).

**Step 13 — the card, the build, the package.** `user/Makefile`:

```make
FT_APPS = … circuits pinball
# Pinball (AutoDev round 4): its core (the table loader, the physics, the rules, the scores) beside the window; draw.h
# is the window's own header. The same floating-point results as the host test: no fused multiply-add.
FT_EXTRA_pinball = Apps/pinball/table.cpp Apps/pinball/physics.cpp Apps/pinball/rules.cpp Apps/pinball/scores.cpp lib/audiokit.imp.a
pinball.elf: Apps/pinball/table.cpp Apps/pinball/physics.cpp Apps/pinball/rules.cpp Apps/pinball/scores.cpp $(wildcard Apps/pinball/*.h) \
	     Apps/games/game.h Include/gamepad.h lib/audiokit.imp.a Kits/audiokit/audiokit.h Kits/filekit/kvtext.h Kits/filekit/kvtext.inc
pinball.elf: NL_CXXFLAGS += -ffp-contract=off
```

`sdcard/apps/pinball.app/app.txt` (`name = Pinball`, `category = Games`, `opens = table`, `stack = 4M`), `icon.bmp`
(`tools/icons/pinball_icon.py`), `tables/`, `lang/fr.txt`; `sdcard/etc/fileassoc.ini` `# Pinball` / `table = pinball`;
`tools/pkg/packages.ini`:

```ini
[app.pinball]
needs    = uikit >= 1.781, audiokit >= 1.232, filekit >= 1.96, fontkit >= 1.135
opens    = table
```

(the versions in `tools/pkg/versions.ini` today; **declared, not published** — PIPELINE §0.5; `versions.ini` not
edited). Test: AC 34's file list present (the `make` itself is the user's).

**Step 14 — screenshots and docs.** `shots.sh pinball` (§9.2) → `screenshots/pinball*.png`; docs/04 §12 (catalog row
+ a *Pinball* section: controls, rules, the three tables, the files, the `.table` format with the example and the
errors, the screenshots; the translated-apps list), docs/03 if relevant (a line in the apps list: "physics core
testable on the PC"), `docs/HANDOFF.md` (what is done, the high-score helper to move into a kit when a 2nd game wants
it, Shift flippers need the kernel), `IDEAS.md`'s *Pinball* row, `python docs/build_docs.py`. Test: AC 36, 37 (the
other game shots unchanged).

**SHOULD, if time remains (in 02's order):** the checker (`pinball --check <file>`: the static layer with every
element's id and the walls' numbered points, all errors listed — the loader gains a "collect all errors" mode) + the
sample `SD:/docs/pinball/my-first-table.table`; attract mode on the picker; the skill shot.

### 8.1 The test-only start state

`pinball --seed N <file>` fixes the game's seed (otherwise `gms ()`); `pinball --start multiball <file>` starts the
table and fires `multiball 2` after the first launch (it calls `Game::fire ({A_MULTIBALL, 2})` on the first `P_LAUNCH`)
— the documented way to reach the multiball screenshot (AC 29) without a 2-minute script. Both are parsed from
`kapi_get_args`, mentioned in docs/04 as "for tests".

---

## 9. The tests

### 9.1 Host test — `tools/tests/pinball/pinballtest.cpp` (`sh tools/tests/run_pinball_test.sh`)

Prints `ok   pinball (N checks: 3 tables, 1500 launches, …, X sub-steps)` and exits 0; any failure prints
`FAIL file:line …` and exits 1. Seeds fixed (`0x5EED0001 + k`). Mapping to 02 §12:

| Part | Asserts | AC |
|---|---|---|
| A. tables | the 3 shipped tables load; per table: 2–3 flippers (Volcano 3), 1 plunger, ≥ 3 bumpers, 2 slings, ≥ 3 lanes in the `rotate` group, ≥ 1 bank, ≥ 1 ramp, ≥ 1 saucer, a rule with `multiball`, `name.fr`, `goal.fr`, every `message.fr` non-empty | 1 |
| | the embedded example loads (element counts) | 2 |
| | each §7.1.4 case, made from the example by one line changed: `load_table` false, `e.reason` equal to the stated English reason, `e.line` the expected line; the `Table` cleared | 3 |
| | `sparkle = 1` inside `[bumper]` loads | 4 |
| B. physics | a scripted 2-minute game (launches, flippers, nudges per frame) run twice: equal score and equal FNV hash of `(p, v)` of every ball at every frame | 5 |
| | 500 launches per table (pull 0.2…1.0, random flipper presses): every ball centre inside `outline` at every sub-step (a hook in `World` for the test: `World::onSubStep` callback, null in the game), speed ≤ 4000 | 6 |
| | 200 launches per table, flippers never pressed: drained within 3600 frames (multiball balls too), `searches == 0` | 7 |
| | example table: 64 angles × 4000 units/s from the centre: for every wall segment, the ball's side at the end equals its side at the start whenever it came within `r` (no crossing) | 8 |
| | a ball at rest midway between the two tips, 120 above, flippers down: drained within 120 frames, `x` between the tips | 9 |
| | ball settled 60 frames on the lowered left flipper at 2/3 length, then held: within 15 frames `vy < −1000`, within 90 frames `y < h/2`; mirrored right | 10 |
| | balls dropped onto each flipper with the press at frame offsets 0…10 before contact: never below the swept area with the centre inside the capsule | 11 |
| | pull 60 frames → launch speed `max` ± 1 %; 30 frames → `max/2` ± 2 %; release at frame 3 → `auto`; the ball passes the gate and is never seen again in the shooter lane above the gate | 12 |
| C. elements | bumper: score `+score`, outgoing speed ≥ `kick`, flash for 9 frames | 13 |
| | sling at 250 → kick ≥ `kick`, scores; at 100 → bounce, no score | 14 |
| | drop targets: each falls and scores once, a fallen one is crossed, the last → one `P_BANK`, all up 60 frames later | 15 |
| | lanes: crossing lights and scores; lit {1,2} + left press → {1,3}… exact sets both ways with wrap; all lit → one `P_LANES`, dark | 16 |
| | gate both ways | 17 |
| | ramp: entry in `pass` → off the playfield, ramp score, back after `time × 60` ± 1 frames at the path's end with `out`; the other way → nothing | 18 |
| | saucer: slow ball held `hold × 60` ± 1 frames, one score, ejected with `out`; at 1600 → rolls over | 19 |
| D. rules | 3 drains → `G_OVER`; the score changed only by the bonus | 20 |
| | drain at 3 s (ballsave 8) → saved, same ball; at 9 s → next ball | 21 |
| | bonus 1/10 rounded to 10; drain adds `bonus × mult`; next ball 1 / 0; mult ≤ 5 | 22 |
| | example: `bank trio` × 2 → `multiball 2` once; `lanes top` → mult + 1 and 1 000; `once = 1` fires once in 3 × count; counters survive drains | 23 |
| | multiball: a 2nd ball auto-launched; one lost keeps the ball; both lost → ball over (bonus once); `multiball 3` → 3; during multiball → none added | 24 |
| | extra ball: the next drain keeps the ball number, once per award | 25 |
| | tilt: 2 nudges in 5 s → warning only; 3 → flippers dead, bumpers 0, no bonus; next ball normal; 3 over 15 s → no tilt; a nudge changes `|v|` by 150 ± 1 | 26 |
| E. scores | the six games → `50, 50, 30, 20, 10` in order (the older 50 first), a 6th lower not added, written and re-read equal, `=` → space, a broken line skipped, an unknown section kept | 27 |

Also run in the same script (AC 35): `git diff --quiet origin/main -- kernel user/Kits` is **not** put in the test
(the branch may lag); the Reviewer checks it.

### 9.2 Simulator scenario — `shots.sh pinball`

```sh
if want pinball; then			# (Pinball, AutoDev round 4: the scores of desktop_sim/pinball/scores.ini in the writes' folder;
					#  one script step = 20 ms of the game -- 50 waits ≈ 1 s)
	PQ="$OUT/writes/apps/pinball.app"; PB=SIM_POS=60,30; T=SD:/apps/pinball.app/tables
	WS=$(printf 'wait;%.0s' $(seq 1 50))
	mkdir -p "$PQ"; cp $D/pinball/scores.ini "$PQ/scores.ini"
	sim pinball pinball "wait;wait;key 0x101;$W" $PB; png pinball                     # the picker: 2nd table chosen, its top 5
	sim pinball pinball-play "wait;key 32;${WS}${WS}hold 0x102;wait;wait;wait" $PB "SIM_ARGS=--seed 7 $T/3-volcano.table"; png pinball-play
	sim pinball pinball-multiball "wait;key 32;${WS}${WS}" $PB "SIM_ARGS=--seed 7 --start multiball $T/1-space-station.table"; png pinball-multiball
	lang fr; sim pinball pinball-fr "wait;wait;$W" $PB; png pinball-fr; lang "$SHOTS_LANG"
	rm -rf "$PQ"
fi
```

(the frame counts are the developer's to adjust so the shots show what AC 29 lists: a raised flipper, lit lamps, 2+
balls with *MULTIBALL!*.) Plus `pinball` in `build ()`'s `extra`, its FT `case` list and `APPS` (§8 step 10).

### 9.3 Simulator test — `tools/tests/run_pinball_sim_test.sh` (AC 30, 31)

Builds the app as `shots.sh` does (or reuses `$OUT/pinball` from `SHOTS_TMP`), then:
1. `SIM_ARGS=SD:/docs/pinball/broken.table` (overlay fixture), `dump` → the picker is shown (a log line
   `pinball: refused broken.table: line 12: unknown block [bumber]` printed by the app to stdout → `SIM_LOG`, asserted
   with `grep`), and the picture written for review.
2. `SIM_ARGS=SD:/docs/pinball/my-first-table.table` (the example via the overlay) → play starts at once (log line
   `pinball: playing My First Table`).
3. `SIM_ARGS=SD:/docs/pinball/quick.table`, a script: `key 32`, ~600 waits (the ball drains), the bonus, `key 13` at
   the name entry → `$SIM_WRITES/apps/pinball.app/scores.ini` exists with `[user.quick]` and `1 = <n> Player`
   (`grep`).
Prints `ok   pinball sim (3 scenarios)`.

### 9.4 Other checks

`python3 tools/lang/check.py pinball` → 0 missing (AC 33); `shots.sh invaders` (and `pipes`, `solitaire`) before /
after the `fakekapi.cpp` change → identical PNGs (AC 28, 37); `sh tools/tests/run_circuits_test.sh` still passes (the
FileKit reader is shared, unchanged).

---

## 10. Files — the complete list

**New:** `user/Apps/pinball/{main.cpp,draw.h,table.h,table.cpp,physics.h,physics.cpp,rules.h,rules.cpp,scores.h,
scores.cpp}`; `sdcard/apps/pinball.app/{app.txt,icon.bmp,lang/fr.txt,tables/1-space-station.table,
tables/2-haunted-manor.table,tables/3-volcano.table}`; `tools/icons/pinball_icon.py`; `tools/tests/pinball/
{pinballtest.cpp,example.table,rules.table}`; `tools/tests/run_pinball_test.sh`; `tools/tests/run_pinball_sim_test.sh`;
`tools/tests/desktop_sim/pinball/scores.ini`; `tools/tests/desktop_sim/sd/docs/pinball/{broken,quick,my-first-table}.table`;
`screenshots/pinball{,-play,-multiball,-fr}.png`. SHOULD: `sdcard/docs/pinball/my-first-table.table`.

**Changed:** `user/Makefile` (FT_APPS, FT_EXTRA_pinball, deps, `-ffp-contract=off`); `tools/tests/desktop_sim/
fakekapi.cpp` (`hold`/`release`, `key_held`); `tools/tests/desktop_sim/shots.sh` (build + scenario);
`sdcard/etc/fileassoc.ini`; `tools/pkg/packages.ini`; `docs/04-USER-GUIDE.md`, `docs/03-DEVELOPER-GUIDE.md` (if
relevant), `docs/HANDOFF.md`, `IDEAS.md`, `docs/exports/*` (by `build_docs.py`).

**Untouched (checked by the Reviewer):** `kernel/`, `user/Kits/` (all kits and their `.abi`), `user/Apps/games/game.h`
(or additions only), `user/Include/gamepad.h`, `tools/pkg/versions.ini`, the other apps.
