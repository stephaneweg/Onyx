# AutoDev round 4 — Development: Pinball

Date: 2026-10-07. Inputs: `02-product-analysis.md` (AC 1–37), `03-technical-analysis.md` (§3 the core, §8 the plan,
§9.1 the host test, the GUI plan), `04-ux-design.md`, `05-validation.md` (its binding notes). Branch `AutoDev`.

## Developer A — steps 1–8 (the core, no UIKit)

Everything below is plain C++ (no STL, no exceptions, no RTTI, fixed arrays, `new` for the big structures, `<math.h>`
for `sqrt` only), compiled by the host test with `-Wall -Wextra -Werror -ffp-contract=off`; every file carries the MIT
notice. No kernel, kapi, AppKit, UIKit or kit change (`git diff origin/main -- kernel user/Kits` is empty).

### What was done, by step

| Step | Files | What |
|---|---|---|
| 1 — scaffold | `user/Apps/pinball/{table,physics,rules,scores}.{h,cpp}`, `tools/tests/pinball/pinballtest.cpp`, `tools/tests/run_pinball_test.sh` | the core's files in `namespace pinball`; the script builds the test twice (ASan/UBSan `-O1` with `--quick`, then `-O2` with the full counts) and compares the two builds' **determinism fingerprint** (the hashes of every table's 2-minute game: the same bits with and without optimisation — the proxy for "the Pi and the PC give the same run") |
| 2 — the loader | `table.h`, `table.cpp` | `load_table (text, Table &, LoadError &)` over FileKit's `fk_kv_parse`: every block and key of 02 §7.1.2 with its defaults and ranges, the hand-written value reader (`number` without exponent or locale, points with spaces and/or one comma, `#RRGGBB`, bool, id), the limits ("too many lanes (max 32)"), the ids unique, the cross-checks (one closed `outline`, 2–3 flippers with a left and a right, one plunger, banks of 2–8, `rotate` and every `[rule]` reference resolved); every error of 02 §7.1.4 with its line; a refused table is cleared. `LoadError` = `line`, `reason` (English, what the tests compare), `fmt` + `arg[2]` (the format and its `%s` arguments, for the window to translate the format: `TR (e.fmt)`); every format and every "too many" word in `// TR:` comments for `check.py`. `inside_outline`, `hit_id`, the core's own `pb_sin` / `pb_cos` (Taylor to x¹¹, < 1e-8) |
| 3 — static physics | `physics.h`, `physics.cpp` | `World`: balls (`B_PLAY`, `B_PLUNGER`, `B_RAMP`, `B_SAUCER`, `B_OFF`), gravity, the speed cap 4000, the fixed frame of 1/60 s in `n = max (8, ⌈travel / (r/2)⌉)` sub-steps; segments (round ends; the side pushed back to = the side at the sub-step's start), arcs (inner / outer face from the start side, range by cross products, round ends), posts; one-way gates (ignored until crossed the allowed way, then a wall from that side — crossed through the segment's own normal, so a gate may slant); the plunger catch; the drain; the bounding-box reject |
| 4 — flippers, plunger | `physics.cpp` | flippers as tapered capsules turning at `speed` between `rest` and `up`, the contact found in the flipper's rotating frame (start position vs start angle, end position vs end angle: a ball the flipper swept past goes back to its side), the contact point's velocity ω × r added (a moving flipper strikes); the press edges (`P_FLIPPER_UP`, once per side); the plunger's pull (full in 60 frames), the launch at `max × pull`, a release within 6 frames or `Input::tap` = `auto` |
| 5 — the toys | `physics.cpp` | bumpers (least outgoing normal speed `kick`, flash 9 frames), slings (kick when hit at ≥ 200, its body lit 6 frames), drop targets (fall, bank cleared → `P_BANK` once, up again after 60 frames), standups (lit), lanes (entry of the centre, lit; a group all lit → `P_LANES`, dark), `rotateLanes` with wrap, ramps (crossing the entry along `pass` → off the table for `time`, back at the path's end with `out`; drawn by `rampPos`), saucers (a ball slower than 1500 caught, held `hold`, ejected with `out`, not caught again until it left), ball–ball (equal masses, e = 0.9), the ball search (still 4 s, not touching a flipper → a 300 units/s kick; `World::searches`), the test hook `World::onSubStep` |
| 6 — the rules | `rules.h`, `rules.cpp` | `Game`: `start`, `frame (Input, nudge)`, `skip`, `fire (Action, rule)`, `handle (PEvent)`; points and bonus (a tenth rounded down to 10; none when tilted), `G_READY → G_PLAY → G_BALLEND → G_OVER`, ball save (from the ball's first launch, once a ball), extra balls, the multiball queue (one ball put in play every 30 frames, refused while 2+ live), tilt (3 nudges in 300 frames; the 2nd a warning), the `[rule]` counters (the whole game; `once`), the lane rotation on the flipper presses; `GEvent`s for the window (sounds, messages, multiball, extra ball, ball saved, tilt warning, tilt, ball end + its bonus, new ball, game over); exposed for the panel (GUI plan): `ballSaveActive ()`, `extraBalls`, `nudges ()`, `ruleCount[]`, `ruleDone[]`, `ruleGoal (r)` |
| 7 — the three tables | `tools/pinball/mktables.py` → `sdcard/apps/pinball.app/tables/{1-space-station,2-haunted-manor,3-volcano}.table` | started from the UX drafts (`mockups/mktables.py`: layouts, palettes, artwork, EN + FR words kept), then **tuned with the host test** (see the deviations) — the generator is kept in `tools/pinball/` so a table is edited there and regenerated |
| 8 — high scores | `scores.h`, `scores.cpp` | the top 5 per table over an `fk_kv` document: `scores_read`, `scores_qualifies`, `scores_add` (equal scores: the older stays above; the lines written again, a broken one replaced), `scores_section` (`1-space-station` / `user.<base>`), `scores_setting` / `scores_set_setting` (`[settings] name`, `sound`, `table`), `scores_clean_name` (`=` and new lines → spaces, trimmed, 16 characters, "Player" if empty). No pinball type in the API (it can move into a kit) |

### Commits (on `AutoDev`)

- `5330e479` Pinball (AutoDev round 4, steps 1–8): the core — table reader, physics, rules, scores — and the three tables
- `8cf71865` Pinball host test: the ball search counted on the no-flipper launches, the two builds' determinism fingerprint compared
- (this document's commit)

### Tests run

| Test | Result |
|---|---|
| `sh tools/tests/run_pinball_test.sh` (AC 1–27, 03 §9.1's table) | **passes**: `ok pinball (516 checks: 3 tables, 210 launches, quick)` (ASan/UBSan, no leak) and `ok pinball (516 checks: 3 tables, 2100 launches)` (-O2); the fingerprint `73999beb7987e858` equal in both builds; ≈ 14 s with the two compilations (the runs themselves ≈ 1.5 s each) |
| mutation checks (by hand, reverted) | flipper collisions switched off → AC 10 and AC 11 fail on every flipper (the swept-contact check is not vacuous); the gate switched off → AC 17 fails |
| `sh tools/tests/run_circuits_test.sh` (FileKit's reader shared) | still passes (582 checks) |
| table statistics (scratch tools, not committed): 500 launches per table, flippers random / never pressed | no ball search needed without flippers; mean ball ≈ 5.5 s without flippers, ≈ 6.5 s with random presses; with random flipper shots the lava ramp is entered ≈ 20 % of shots, the orbits ≈ 2 %, the saucers 6–37 % |

AC → test (03 §9.1): A `test_shipped` (AC 1, plus every `message` ≤ 31 bytes — binding note 2 — and every label's
`text.fr`), `test_example` (AC 2, AC 3: 28 broken files made from the example by one line changed — each reason, line,
`fmt` + args and the table cleared; plus the 64 KB file and "cannot read the file" — AC 4); B `test_physics` (AC 5 the
2-minute game run twice, AC 6 500 launches per table with the outline and the speed checked at every sub-step, AC 7 200
launches without flippers: each ball out within 60 s and no ball search, AC 8 64 shots at 4000 units/s on the example:
no centre path crosses a wall or sling segment, AC 9 the drain between the tips on every table and the example, AC 10
both lower flippers on every table, AC 11 every flipper (Volcano's third too) × 11 press offsets, AC 12 the pulls 60 /
30 / 2 frames, the tap, the full launch through the gate and never back into the lane); C `test_toys` (AC 13–17 on the
example, AC 18–19 on `tools/tests/pinball/rules.table`); D `test_rules` (AC 20–26); E `test_scores` (AC 27).

### Deviations from the plan (and why)

1. **AC 10 — no 1 s settle.** A ball cannot rest on a *lowered* flipper: it slopes 30° down to its tip and the ball
   rolls off in ≈ 0.25 s. The test places the ball at rest touching the lowered flipper at 2/3 of its length and holds
   the flipper from the first frame; the criteria are kept (vy < −1000 within 15 frames, above half height within 90).
2. **AC 11 — what "below" means.** "Never ends below the swept area" is checked at every sub-step as *never crossing
   the flipper's axis from its top side to its bottom side within its length, never with the centre inside the
   capsule*, during the swing (contact + 30 frames). A ball shot up that comes back down may legitimately pass under a
   raised flipper; that is not tunnelling.
3. **The tables' skeleton** (03 §6.1 / the UX drafts) changed where the host test or the play showed a fault:
   - the **inlanes now feed the flippers**: the outlane divider goes on as the inlane guide, `(38,700) → (38,850) →
     (144,894)` and its mirror `(438,700) → (438,850) → (332,894)` (the drafts' inlanes, between the divider and a guide
     at x = 72, led to the drain; the ball also stuck in a pocket between the guide's end and the flipper's pivot);
     the inlane sensors moved to x 55 / 397;
   - the **slingshots** moved up and in, `(96,740) (96,820) (132,830)` and mirror: the drafts' bodies left 20 units
     between them and the guides (a 26-unit ball cannot pass);
   - the **shooter gate slants**, `(476,300) → (518,284)`: a weak launch that just crossed it rested on it for ever;
   - the **orbit channels** (Space Station left, Haunted Manor right) end at y 560, their entries at y 540 (were 640 /
     620): a shot can reach them over the slingshots (still a hard shot, ≈ 2 % of random flipper hits);
   - **Volcano's upper-flipper back wall** starts on the shooter wall, `(476,392) → (452,412) → …`: a ball rested in
     the 24-unit gap between it and the shooter lane.
4. **Haunted Manor's extra ball** (binding note 3): `when = bank bats`, `count = 2`, `once = 1`, `do = extraball`,
   *Extra ball!* / *Bille supplémentaire !* — the format has one event per rule (02 asked "both banks ×2").
5. **Volcano's third flipper** feeds the *Crater* saucer and the bumpers (≈ 37 % of its shots), not the lava ramp:
   the ramp's entry lies below it (03 §6.1 said "feeding the ramp").
6. **Slingshot kicks vary** a little (1–1.15 × `kick`, turned by up to ±10°, the speed along the face halved), from the
   world's seeded generator: two slings otherwise kept a ball bouncing between them for a minute (a stable orbit).
   Still deterministic; AC 14's "≥ kick" holds.
7. **Gates and ramp entries are crossed through their segment's own normal** (oriented by `pass`), so they may slant;
   one nearly parallel to its `pass` (within 30°) is refused, `bad value for pass`.
8. **Error lines** where 02 §7.1.4 leaves the choice: a missing outline / too few flippers / no plunger → the
   `[table]` header's line; a second outline → its `id` line; an outline not closed → its header; a 4th flipper or a
   2nd plunger → its header; a bank of 1 → its first target's `bank` line; a 9th target in a bank → its `bank` line;
   "too many …" → the header of the block over the limit; `format` other than 1 → `format out of range`; a key before
   any block → `[table] must be the first block, once`.
9. **Rules' details**: the ball save starts at the ball's first launch and saves once (a saved ball relaunched does not
   get a new one; a `ballsave` action re-arms it for its seconds); rules do not count while tilted; `P_LANES` fires
   for every lane group (so `lanes in` / `lanes out` rules work), the rotation only for `rotate`; **a score of 0 never
   enters the top 5** (binding note 9).
10. **`LoadError`** has `fmt` + `arg[2]` (strings) rather than one argument ("too many %s (max %s)" has two).
11. `tools/pinball/mktables.py` is a new file (the tuned generator). `example.table` (02 §7.1.3 verbatim) and
    `rules.table` (every toy placed for direct shots, `ballsave = 8`) are in `tools/tests/pinball/`; the broken files
    of AC 3 are made by the test from the example (not files).

### What is left for Developer B (steps 9–14)

- **Step 9** — the simulator's `hold` / `release` script steps and `key_held` in `tools/tests/desktop_sim/fakekapi.cpp`
  (03 §5); `shots.sh invaders` before / after identical.
- **Step 10** — the window (`main.cpp`, `draw.h`, `panel.h`; FT app, `GameRoot` 600 × 680 resizable, min 600 × 680 —
  binding note 1), the frame accumulator (16 667 µs a frame, at most 3 frames a tick: **handle `g.ev[]` after each
  `Game::frame`**, it is cleared by the next), input per 03 §4.3 (`Input::tap` = a plunger key event while
  `kapi_key_held` is false at the next frame — binding note 12), sounds from `GE_SOUND` (`index` = the `PEvKind`), the
  messages (`GE_MESSAGE` → `t.rule[index].message.get (lang)`; `GE_MULTIBALL` with `index < 0` → `TR ("MULTIBALL!")`,
  with `index >= 0` the rule's own message follows as `GE_MESSAGE` — never both, note 10), `--seed N`, `--start
  multiball` (`g.fire ({A_MULTIBALL, 2})` on the first `P_LAUNCH` of `g.w.ev`).
- **Step 11** — the picker, the name entry, `scores.ini` (`fk_kv_load (path, FK_KV_ESCAPES)` or `fk_kv_new`,
  `scores_*`, `fk_kv_save (kv, path, "Pinball -- high scores (written by the game)")`), `[settings] table`; the file
  read by the window (`kapi_open/fsize/read`, a file over `MAXFILE` → `load_error_file (e, true)`).
- **Step 12** — French: `fr.txt` with every `// TR:` line of `table.h` (the formats and the "too many" words) and the
  UI's words; the tables' `.fr` texts through `Text::get (uk_lang () == fr)`.
- **Step 13** — `user/Makefile` (`FT_APPS`, `FT_EXTRA_pinball = Apps/pinball/table.cpp Apps/pinball/physics.cpp
  Apps/pinball/rules.cpp Apps/pinball/scores.cpp lib/audiokit.imp.a`, `pinball.elf: NL_CXXFLAGS += -ffp-contract=off`),
  `app.txt`, `icon.bmp`, `lang/fr.txt`, `fileassoc.ini`, `packages.ini` (declared, not published).
- **Step 14** — screenshots and docs (docs/04: the `.table` format — the reasons and lines of deviation 8 —, the
  controls, the three tables; HANDOFF, IDEAS, `build_docs.py`).

### The core's API (what the window builds on)

```cpp
namespace pinball {
// table.h
struct Vec { double x, y; };
struct Text { char en[164], fr[164]; const char *get (int lang) const; };       // lang 1 = French (falls back to en)
struct Table {
	Text name, goal; double w, h, gravity, ball, ballsave, drain; int balls, rotate; unsigned background;
	Seg seg[]; Wall wall[]; Arc arc[]; Circle circ[] /* posts, bumpers */; Sling sling[]; Target tgt[]; Bank bank[];
	Lane lane[]; char group[][25]; Gate gate[]; Flip flip[]; Ramp ramp[]; Saucer saucer[]; Shape shape[]; Label label[];
	Rule rule[];   /* each with its count n<name> */   Vec plunger; double plungerMax, plungerAuto; Vec outline[]; int noutline;
};
// Wall {first, n, closed, outline, colour, width} -> seg[first .. first+n-1]; Arc {c, r, from, to, span, colour, width};
// Circle {id, c, r, kind C_POST/C_BUMPER, score, colour}; Sling {id, seg, score, kick, colour};
// Target {id, seg, bank, score, drop, colour}; Lane {id, x, y, w, h, group, score, colour}; Gate {a, b, pass};
// Flip {side, pivot, len, rest, up (radians), restDeg, upDeg, r0, r1, speed, bounce, colour};
// Ramp {id, a, b, pass, path[], npath, time, out, score, colour, width}; Saucer {id, c, r, hold, out, score, colour};
// Shape {p[], n, colour}; Label {at, text, size, angle, colour}; Rule {when, ref, count, once, act[], nact, message}
struct LoadError { int line; char reason[128]; const char *fmt; char arg[2][48]; };
bool load_table (const char *text, Table &t, LoadError &e);   // false: t cleared
void load_error_file (LoadError &e, bool big);                // "the file is too big" / "cannot read the file", line 0
bool inside_outline (const Table &t, Vec p);
const char *hit_id (const Table &t, int ref);
double pb_sin (double), pb_cos (double);
enum { MAXFILE = 65536, ... };

// physics.h
enum BallState { B_OFF, B_PLAY, B_PLUNGER, B_RAMP, B_SAUCER };
struct Ball { Vec p, v; int state, ramp, saucer, timer, ignoreSaucer; unsigned lanes; bool live () const; ... };
struct Input { bool left, right, plunger, tap; };
enum PEvKind { P_BUMPER, P_SLING, P_TARGET, P_DROP, P_BANK, P_LANE, P_LANES, P_RAMP, P_RAMP_OUT, P_SAUCER, P_EJECT,
               P_FLIPPER_UP, P_LAUNCH, P_DRAIN, P_PLUNGER, P_BALLHIT, P_SEARCH };
struct PEvent { int kind, index, ball; double speed; };
struct World {
	const Table *t; Ball ball[3]; double flipAng[3], flipW[3]; bool flipperDead; double pull; int pullFrames;
	bool tgtDown[64], tgtLit[64]; int bankTimer[32]; bool laneLit[32]; int bumperFlash[64], slingFlash[16], rampFlash[4];
	PEvent ev[64]; int nev; unsigned rng; long frame, subSteps; int searches;
	void (*onSubStep) (const World &, void *); void *hookData;            // tests only
	void reset (const Table &, unsigned seed); int addBall (Vec p, Vec v); int addOnPlunger (); bool plungerBusy () const;
	bool launch (double speed); void step (const Input &); void rotateLanes (int dir); void nudge ();
	int liveBalls () const; int playBalls () const; unsigned random ();
	Vec flipTip (int f) const; Vec rampPos (int b) const; bool onFlipper (int b) const;
};
// constants: FPS 60, FLASH_FRAMES 9, SLING_FRAMES 6, RAMP_FRAMES 30, BANK_FRAMES 60, MAXSPEED 4000, ...

// rules.h
enum GState { G_READY, G_PLAY, G_BALLEND, G_OVER };
enum GEvKind { GE_SOUND, GE_MESSAGE, GE_MULTIBALL, GE_EXTRABALL, GE_BALLSAVED, GE_TILTWARN, GE_TILT, GE_BALLEND,
               GE_NEWBALL, GE_GAMEOVER };
struct GEvent { int kind, index; long value; };
struct Game {
	World w; const Table *t; int state; long score, bonus, lastBonus; int mult, ball, extraBalls, multiballQueue;
	long frameNo, ballSaveUntil; bool tilted; int ruleCount[32]; bool ruleDone[32]; GEvent ev[64]; int nev;
	void start (const Table &, unsigned seed); void frame (const Input &, bool nudge); void skip ();
	void fire (const Action &, int rule = -1); void handle (const PEvent &);
	bool ballSaveActive () const; int nudges () const; int ruleGoal (int r) const;
};

// scores.h
struct ScoreLine { long score; char name[65]; };   // TOPN 5, NAMEC 16 characters
int  scores_read (const fk_kv *, const char *section, ScoreLine out[5]);
bool scores_qualifies (const fk_kv *, const char *section, long score);
int  scores_add (fk_kv *, const char *section, long score, const char *name);   // rank 0..4, -1
void scores_section (const char *path, bool shipped, char *out, int cap);
const char *scores_setting (const fk_kv *, const char *key, const char *def);
void scores_set_setting (fk_kv *, const char *key, const char *value);
void scores_clean_name (const char *name, char *out);
}
```

`Table` and `Game` are large (≈ 0.43 MB and ≈ 4 KB): allocate them with `new`, never on the stack.
