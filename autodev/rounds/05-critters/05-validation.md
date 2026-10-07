# AutoDev round 5 — Validation (Technical Analyst, fresh): Critters

Date: 2026-10-07. Reviewed together: `02-product-analysis.md` (features §4, the `.level` format §7.1, `progress.ini`
§7.2, the `.sol` format §7.3, controls §10, French §11, **AC-1…AC-37**), `03-technical-analysis.md` (§1–§11, R1–R11,
steps 1–14, tests §9, the AC → step table §10, and its appended **GUI plan** G1–G10), `04-ux-design.md` (D1–D24,
§1–§9) and `mockups/` (`crmock.cpp`, `mklevels.py`, `mockups.sh`, `sd/` with the 12 draft levels and `fr.txt`, the
26 `cr-*.png`; `cr-play-fr.png` and `cr-picker.png` looked at).

Checked against the code:

- **UIKit** (`user/Kits/uikit/`): `LcdDisplay` (`lcd.h`: `face`, `smallFace`, `ink`, `centred`, `setText` / `setCaption`
  / `setSub`; **text buffers 32 bytes**), `ToolButton` (`toolbar.h`: `setGlyph`, `setText`, `setToggle`, `filled`,
  `raised`, `setOn`, `setDisabled`, tooltip as the constructor's text), `WKT_PLAY / PAUSE / FORWARD / PLUS / MINUS`,
  `uk_tool_glyph`, `Button` (`button.h`), `Menu` (`menu.h`, `UK_CTRL`), `VPath` (`vpaint.h`: `ellipse`, `line`, `rrect`,
  `poly`, `arc`, `fill (cv, colour, alpha, dx, dy)` — 1/16-px integer coordinates), `Canvas` (`px / w / h / stride`,
  `alloc`, `fillRect`, `pixel`, `putOther (src, dx, dy, transparent)` — magenta key, **no alpha blit, no scaled blit**,
  as 03 says), `uk_rbox`, `uk_rline`, `uk_raised`, `uk_sunken`, `UK_PRESSED`, `uk_tone`, `uk_mix`, `uk_bright`,
  `uk_hilite`, `uk_title_strip`, `uk_etch_h / _v`, `uk_scroll_bar`, `uk_glyph` + `WKG_LOCK / CHECK / HISTORY`,
  `uk_text_l / _c / _w / _fit / _wrap`, `uk_fh`, `UkFaceScope`, `C_DIS`, `C_FRAME_ACTIVE`, `canFocus`, `Root (w, h,
  title)`, `setResizable`, `setMinSize`, `bmp_decode`, `TR` / `TRC` / `TRN` / `uk_lang_init` / `uk_lang ()` (returns
  `"en"` / `"fr"`). **All exist.** Every widget 04 adds (`SkillSlot`, `StatusLine`, `MiniMap`, `LevelList`, `Preview`,
  `LevelInfo`, `Legend`, `SkillBar`'s face) is a drawn `Widget` beside the app (`bar.h`, `picker.h`), as Pinball's /
  Circuits' — no UIKit addition needed.
- **game.h**: `GameView (l, t, w, h)` (`canFocus`, `setFocus` on a left press; `paint / press (x, y, right) / release /
  move / key (long) / tick (unsigned dt)`, `step ()` capping `dt` at 100), `GameRoot (w, h, title)` (`onTick` →
  `view->step ()`, `onKey` → `view->key` when the view lacks the focus), `sfx`, `sfx_later`, `sfx_win`, `sfx_lose`,
  `sfx_set_mute`, `g_sfx_mute`, `gms ()`. `GameView::onMouse` does **not** pass the wheel on (see note 6).
- **FileKit** `kvtext.h`: `fk_kv_new / parse / load / free / count / key / value / section / block / line / blocks /
  block_name / block_line / get / set / remove / text / save (kv, path, comment)`, `FK_KV_ESCAPES` (a flag of
  `new` / `parse` / `load`, not of `save`); `fsutil.h`: `fs_basename`, `fs_ci_cmp`.
- **FontKit** `uikitface.h`: `ft_uikit_install`, `FtTextFace`, `ft_uikit_face`, `ft_messagebox (title, text, buttons)`,
  `ft_file_open (out, cap, dir, filters)`; `MB_OK` in `appkit.h`.
- **AppKit** `appkit.h`: `kapi_get_modifiers`, `MOD_CTRL` 1, `MOD_SHIFT` 2, `KEY_TAB` 9, `KEY_ENTER` 13, `KEY_LEFT /
  RIGHT` 0x102 / 0x103, `KEY_HOME / END / PGUP / PGDN` 0x104–0x107, `KEY_F1` 0x110, `kapi_key_held`, `kapi_get_args`,
  `kapi_open / read / fsize / close`, `kapi_opendir / readdir / closedir`, `kapi_get_ticks`. **gamepad.h**:
  `pad_buttons`, `PAD_A / B / X / L / R / START / SELECT` and the d-pad.
- **Simulator**: `fakekapi.cpp` `g_ticks += ms / 10 + 1` (20 ms a loop turn: 03 §1.1's 2.5 waits a step is right), the
  script's `down / up / rdown / rup / wheel / key / hold / release / mods / copy / waitlog`, `SIM_ARGS`, `SIM_POS`,
  `SIM_LOG`, `SIM_MENU`, `SIM_WRITES`, `SIM_OVERLAY` (files only; writes first). `shots.sh`: `D=`, `want`, `build ()`'s
  `extra` (the pinball line to copy), the FT `case` list, `APPS`, `sim` (with `SIM_OVERLAY=$D/sd`), `png`, `lang`, `$W`.
  `run_pinball_test.sh` / `run_pinball_sim_test.sh` exist as patterns.
- **Build / packaging**: `user/Makefile` `FT_APPS` (… `circuits pinball`), `FT_EXTRA_pinball` + its `pinball.elf` deps
  (03 step 13 is the same shape); FT apps build with `-mcpu=cortex-a72` (floats allowed in the **window** code, so the
  mock's `double` drawing helpers can be lifted; the core stays integer by its own rule). `tools/pkg/versions.ini`:
  uikit 1.781, audiokit 1.232, filekit 1.96, fontkit 1.135 — 03's `needs` match `[app.pinball]`'s. `fileassoc.ini`:
  `.level` free, the `# Pinball` / `table = pinball` pattern. `app.txt` pattern (`name / category / opens [/ stack]`).
  `tools/icons/pinball_icon.py`, `tools/pinball/mktables.py`, `tools/lang/check.py` (`TR`, `TRC`, `TRN`, `// TR:`),
  `IDEAS.md`'s *Lemmings-like* row, docs/04's translated-apps list (l. 1658) and the *Pinball* section — all present.
  `git diff --stat origin/main -- kernel user/Kits` is empty today (AC-34's check is meaningful).

## Verdict: **GREEN**

- **Every acceptance criterion is covered by a plan step and a test** (03 §10): AC-1…AC-24 and AC-30…AC-31 by steps
  2–9 and the host test's table (03 §9.1, one row per AC); AC-25 by step 14 + §9.2's six shots (screens from G1–G10);
  AC-26…AC-28 by step 11 and `run_critters_sim_test.sh` cases 1–4 (§9.3); AC-29 by step 9; AC-32 by step 12 / G10;
  AC-33 by step 13 (written; the Pi `make` is the user's — no AArch64 compiler here, as rounds 1–4); AC-34 by step 1's
  grep guard + step 13; AC-35 by step 14; AC-36 by §9.4 / step 14; AC-37 by D19 / D21 (sprites drawn in code) + step
  14's grep.
- **Every widget, file, kit function and call named exists** with that name (above), or is a new file listed in 03 §11.
  **No UIKit, kit, AppKit, kapi, kernel or simulator change**: no `.abi`, no `kitdocs.py`, no version raised.
- **02 / 03 / 04 agree**, the differences being resolved explicitly in 03's GUI plan table:
  - **the nuke's confirmation**: 02 §10's "N, then N within 2 s" is **superseded by 04 D12** (a modal card that pauses
    the world; N / Enter confirms, Esc / Cancel / a click outside cancels; no timer). No AC tests the timer; docs/04
    describes the card. 02 #17's "asked to confirm" is met.
  - **the blocker / nuke rule** (03 fact 7, R6, 04 D13): rules unchanged; every `.sol` leaving a blocker ends with
    `nuke`; the UI tells the player (amber status line, the pulsing N slot, `World::onlyBlockersLeft ()` — an accessor,
    no rule change).
  - **the step timing**: 02 #2 (50 ms a step, ×3 fast, ≤ 4 caught up) = 03 §3.6's `Clock` (clarified during validation,
    below); the simulator's 2.5 waits a step and the `--until` jump (03 §1.1, §5.5) keep scripts short.
  - **the `.sol` format**: 02 §7.3 = 03 §3.4 (`--until end` is 03's addition, needed for AC-25 / 27).
  - **the window** (fixed 800 × 448, fits 1024 × 768), **the rate step** (±5, 04 D5), **the scroll speed** (4 logical px a
    frame, 04 §4.1) and **the minimap's scale** (D8) are UX sizings that 02 left open.
- **Bilingual EN/FR covered**: `TR` / `TRN` everywhere, `uk_lang_init` after `ft_uikit_install`, the loader's English
  reasons in `// TR:` comments and translated through `LoadError::fmt`, the levels' `.fr` keys (AC-1 asserts them), the
  starting `fr.txt` in `mockups/sd/`, `check.py critters` at 0 (AC-32), French mock-ups for every screen and two French
  shots (AC-25).
- **Scope buildable in one round**: the engine is small integer code with a pixel-exact test per rule; the real risk
  (12 winnable levels with valid solutions) is handled by the `crsim` loop (03 §4) and guarded by AC-30 / 31; the UI
  reuses round 4's patterns (picker, overlays as root children, sim test, shots). SHOULD items stay optional.
- CLAUDE.md / PIPELINE respected: one-program code beside the app (kits-first; 03 §2.2 argues why no FileKit progress
  helper), MIT notices, docs/04 + HANDOFF + IDEAS + `build_docs.py` planned, the package **declared, not published**,
  branch `AutoDev` only.

### Fixed during validation (in `03-technical-analysis.md`)

1. **§3.6 `Clock`**: the field was declared `acc` but used as `acc3`, and the cost per step was ambiguous. Now: `acc3`
   in thirds of a ms, a step costs 150 (normal) or 50 (fast), at most 4 (fast 12) a call, the excess dropped (the game
   slows, never skips — 02 #2).
2. **§1 FileKit row**: `fk_kv_save (…, FK_KV_ESCAPES)` does not exist — the flag belongs to `fk_kv_load` / `fk_kv_new`;
   `fk_kv_save (kv, path, comment)` returns 0 / −1 (as Circuits).
3. **§3.3 `S_CLIMB`**: 02 #12 turns a **climbing** creature at a blocker too; the step listed the check only for
   `S_WALK`. Added: a climber within reach of a blocker lets go, turns and falls.
4. **§3.4 `load_solution`**: 02 §7.3's example has **trailing `#` comments** and may give two roles in one step; now
   stated: a `#` ends a line, equal steps allowed ("bad step" = lower than the previous), and > 1024 actions refused
   (`too many actions (max 1024)`).
5. **§3.2 `Text::get (int lang)`**: `uk_lang ()` returns a string; the UI passes `!strcmp (uk_lang (), "fr")` (the core
   cannot include `lang.h`).

No gap sends the round back to 03 or 04. The notes below are **binding for the Developer** (step numbers are 03 §8's).

## Binding notes for the Developer

1. **Sprites with an opacity (04 D21 / §5.5).** UIKit has no ARGB canvas and no alpha blit: `VPath::fill` blends a
   colour into an RGB `Canvas`. To pre-render frames "colour + opacity", render each frame **twice**, on black and on
   white (`Canvas::clear (0)` / `clear (0xFFFFFF)`, then the same `VPath` calls); per pixel `a = 255 − (W − B)` (per
   channel, the green one is enough) and colour = `B × 255 / a`; then blend the frames with a 10-line loop of your own in
   `draw.h`. (Simpler fall-back, if time is short: draw the creatures live with `VPath` each frame as `crmock.cpp` does —
   measure: ≤ 80 creatures × ≈ 15 fills — and say so in `06-development.md`.) Keep the drawing code out of the core.
2. **The shots in play must not show the pause banner.** 03 §5.1 makes `--until N` stop **paused** — the honest state for
   a user — so D10's dimming and banner would be in `critters-play.png` / `critters-build.png`. In §9.2's scenario,
   after the start send **`key p`** (resume) and then the waits (deterministic: the simulator's clock is fixed), or wait
   with the world paused and accept the banner nowhere but in a pause shot. Also: `critters-play` needs **a role chosen**
   (`key 5`, *Digger*, as `cr-play.png`) and preferably a `move` onto a creature so the brackets show (its position from
   `crsim --where`, turned into client coordinates by `(x − vx) × 2`, `y × 2`); `critters-build` the `key 4` 03 states.
3. **No sounds during a headless jump**: `--until` runs many steps at once — drop the events' sounds while catching up
   (play only the step's last events, or none) so the simulator / Pi does not queue hundreds of `sfx_later` notes.
4. **`critters.png` and *My levels***: `shots.sh` runs every app with `SIM_OVERLAY=$D/sd`, whose folders are **not listed**.
   To show *My levels* as `cr-picker.png` does, copy fixtures into `$OUT/writes/docs/critters/` in `cfix` (works while
   the card has no `sdcard/docs/critters/`; if SHOULD 2 ships that folder, the listing comes from the card and the shot
   shows the sample only). AC-25 does not require *My levels*: either way is acceptable — say which in `06`.
5. **Levels kept for the picker**: a `Level` is ≈ 120 KB (`Text` of 2 × 644 bytes for each of 32 labels, 256 shapes).
   Keep one full `Level` for the level being played or previewed and only the picker's facts (names, counts, roles,
   time, the error) per entry — not 12 + N full `Level`s.
6. **The wheel**: `GameView::onMouse` drops the wheel argument; `CrittersView` overrides `onMouse` (call
   `GameView::onMouse` first, then use the 6th argument for 04 §4.1's 24 px a notch). The bar's "wheel = previous / next
   role" is `SkillBar`'s (or each `SkillSlot`'s) `onMouse`. The edge scroll runs only while the pointer is inside the
   view (`onMouse` with `x < 0` = the pointer left; there is no "window active" call to test).
7. **Tab and the focus**: a left press gives the `GameView` the focus (`setFocus` in `GameView::onMouse`), after which
   keys reach `CrittersView::key` directly; `GameRoot::onKey` covers the other case. Return **false** for Tab / Enter /
   Esc while a card with `Button`s shows (R9 / round 4's R11), true for Tab otherwise (02 #21).
8. **AC-28's click**: in case 4 of §9.3 prefer reaching the creature by `--replay` of an empty / partial `.sol` with
   `--until N` + `key p` (a known step) over counting waits from the card; then click within its body box (±4 logical px
   = ±8 screen px). T1 is exactly 400 px wide: no letterbox, `vx = 0`, so client x = `2 × x`.
9. **The `.sol` replay counts as play**: AC-27 wants `--replay … --until end` to write `progress.ini`; keep it (the
   simulator's writes go to `SIM_WRITES`). A replay's result is a real run (determinism), so this is honest.
10. **The constants**: put 02 §4.2–4.3's numbers in the one `enum` of `world.h` (03 §3.3) and freeze them before the
    solutions are recorded (R3); a later tweak re-runs `crsim --search` on every `.sol` and AC-30 must still pass.
11. **`--until` vs the start card**: with `--replay` there is no start card and the hatch is open from step 0
    (`E_HATCH_OPEN`); without `--replay`, the world does not tick until the card is dismissed (AC-6's step 40 counts from
    the first tick, not from the window's opening).
12. **Not done here, by rule**: the card build (`make` from `kernel/`) is left to the user; no publishing
    (`tools/pkg/versions.ini` unchanged; `[app.critters]` declared only); `mockups/` is never built nor shipped — lift
    `critter`, `hatch`, `portal`, `burst`, `brackets`, `role_icon`, `nuke_icon`, `digit` and the drawn widgets by hand;
    the draft levels of `mockups/mklevels.py` are a start for `tools/critters/mklevels.py`, re-solved by `crsim` (their
    geometry is not solved). Docs per step 14 + the GUI plan (the controls of 04 §7, the screens of 04 §2–§4, the nuke
    card instead of 02 §10's 2-second rule, "All explode ends a level a blocker keeps alive", the `.level` and `.sol`
    formats, `--replay` / `--until` "for tests and makers"), then `python docs/build_docs.py`.
