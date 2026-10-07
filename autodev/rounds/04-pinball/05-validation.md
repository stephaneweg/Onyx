# AutoDev round 4 — Validation (Technical Analyst, fresh): Pinball

Date: 2026-10-06. Reviewed together: `01-product-manager.md`, `02-product-analysis.md` (features §4, the `.table`
format §7.1, `scores.ini` §7.2, controls §10, French §11, **AC 1–37**), `03-technical-analysis.md` (§1–§10, R1–R10,
steps 1–14, tests §9, and its appended **GUI plan** + R11), `04-ux-design.md` (D1–D20, §1–§9) and `mockups/`
(`pinmock.cpp`, `mktables.py`, `mockups.sh`, `sd/`, the 19 `pb-*.png`; `pb-play.png` and `pb-picker-fr.png` looked at).

Checked against the code:

- **UIKit** (`user/Kits/uikit/`): `LcdDisplay` (`lcd.h`: `face`, `smallFace`, `ink`, `centred`, `setText` / `setSub`
  / `setCaption`, repainted only on change; **text buffer 32 bytes**), `ToolButton` (`toolbar.h`: `setGlyph`,
  `setText`, `filled`, `raised`, `setOn`, `setDisabled`; takes no focus), `WKT_PLAY`, `Button` (`button.h`),
  `Textbox` (`textbox.h`: `maxLen`; Enter handled only when a callback is given — `textbox.cpp` l. 152/236),
  `VPath` (`vpaint.h`), `Canvas` + `putOther` (`canvas.h`), `uk_text_l / _c / _w`, `uk_text_wrap`, `uk_text_fit`,
  `uk_rbox`, `uk_rline`, `uk_raised`, `uk_sunken`, `uk_hilite`, `uk_title_strip`, `uk_etch_h`, `uk_tone`, `uk_mix`,
  `uk_scroll_bar`, `UkFaceScope`, `Root::setResizable / setMinSize / fitWorkArea / onResized` (`root.h`),
  `uk_file_open`, `TR` / `TRC` / `TRN` / `uk_lang_init` / `uk_lang ()` (`lang.h`). **All exist.**
- **FontKit** `fontkit/uikitface.h`: `FtTextFace`, `ft_uikit_install`, `ft_messagebox`, `ft_file_open (out, cap,
  dir, filters)`. Circuits' `open_face` (`circuits/gates.h:29`).
- **FileKit** `filekit/kvtext.h` (included by `filekit.h`): `fk_kv_new / parse / load / free / count / key / value /
  section / block / line / blocks / block_name / block_line / get / set / remove / text / save`, `FK_KV_ESCAPES`,
  `FK_KV_PIPES`; inline on the PC (no library for the host test). `filekit/fsutil.h`: `fs_basename`, `fs_ci_cmp`.
  `lib/filekit.imp.a` already linked by the FT rule.
- **game.h**: `GameView (l, t, w, h)` (a child of any size: the playfield-only view of D4 works), `paint / press /
  release / move / key / tick`, `step ()`; `GameRoot::onTick`, `GameRoot::onKey` (= `view->key (k)` when the view lacks
  the focus — the R11 routing 03 describes is right); `sfx`, `sfx_later`, `sfx_win`, `sfx_lose`, `sfx_set_mute`,
  `g_sfx_mute`, `gms`, `rng_*`. Invaders' menu uses `UK_CTRL ('N')` (a code distinct from `'n'`: no nudge clash).
- **gamepad.h**: `pad_buttons`, `PAD_UP/DOWN/LEFT/A/B/Y/L/R/L2/R2/SELECT/START`. **appkit.h**: `kapi_key_held`,
  `KEY_ENTER/UP/DOWN/LEFT/RIGHT/HOME/END`, `kapi_get_args`, `kapi_opendir/closedir`, `kapi_fsize`. `kernel/gui/kwin.cpp`
  `UsageToKey` / `KeyHeldAny` present (02 §4.3's finding).
- **Simulator**: `fakekapi.cpp` `key_held` returns 0 (l. 1311) and `T->key_held = key_held` (l. 1511); the script's
  `key` / `waitlog` / `SIM_ARGS` / `SIM_LOG` / `SIM_OVERLAY` (files only) / writes-first lookup exist as 03 §1.1 says.
  `shots.sh`: `build ()`'s `extra`, the FT `case` list, `APPS`, `sim`, `png`, `lang`, `$W`, `$AK` (AudioKit) exist.
- **Build / packaging**: `user/Makefile` `FT_APPS`, `FT_EXTRA_<app>`, `NL_CXXFLAGS`, the Circuits pattern
  (l. 785–786); `tools/pkg/versions.ini` (uikit 1.781, audiokit 1.232, filekit 1.96, fontkit 1.135 — 03's `needs`
  match); `[*apps]` packaging; `tools/icons/circuits_icon.py`, `tools/tests/run_circuits_test.sh` (patterns);
  `tools/lang/check.py` takes `TR`, `TRN`, `TRC` and `// TR:` comments; `IDEAS.md` has the *Pinball* row.

## Verdict: **GREEN**

- **Every acceptance criterion is covered by a plan step and a test**: AC 1–27 by steps 2–8 and the host test's
  table (03 §9.1, one row per AC), AC 28 by step 9 + §9.4, AC 29 by §9.2 (+ `pinball-broken.png` from the GUI plan),
  AC 30–31 by `run_pinball_sim_test.sh` (§9.3), AC 32 by the script, AC 33 by step 12, AC 34 by step 13 (written, the
  Pi `make` left to the user — no AArch64 compiler, as rounds 1–3), AC 35 by step 1 + §2.1 + §10's untouched list,
  AC 36 by step 14, AC 37 by §9.4.
- **Every widget named in 04 exists in UIKit** (above) or is a drawn `Widget` subclass planned beside the app
  (`Heading`, `Stats`, `RuleList`, `Legend`, `TableList`, `Thumb`, `ScoreList` in `panel.h` / `picker.h`, as Circuits'
  `LevelList`). **No UIKit, kit, AppKit, kapi or kernel change** — no `.abi`, no `kitdocs.py`, no ABI question.
- **Every file, kit function and call named exists** (above); the new files are listed (03 §10 + the GUI plan).
- **02 / 03 / 04 agree** on the keys (02 §10 = 03 §4.3 = 04 §7; 04 merges the two pause overlays and adds Home/End,
  Start-to-play, Ctrl+O, Ctrl+Q — additions, no conflict), the window (600 × 680 client, ≤ 760 tall with the frame:
  02 §4.4 met; the GUI plan supersedes 03 §4.4's "≈ 600 × 700"), the file names (`tables/1-…3-….table`, `scores.ini`,
  `SD:/docs/pinball/`, `lang/fr.txt`), the menu (02's three items + 04's Open / Quit), the extra `[settings] table` key
  (02 §7.2 keeps unknown keys).
- **Bilingual EN/FR covered**: `TR` / `TRC` everywhere, `uk_lang_init` after `ft_uikit_install`, the loader's English
  reasons listed in `// TR:` comments and translated as formats, the tables' `.fr` keys (AC 1 asserts them), the
  starting `fr.txt` in `mockups/sd/`, `check.py pinball` at 0 (AC 33), French mock-ups and the French picker shot
  (AC 29). Thousands separator `TRC ("digits", ",")` → U+202F.
- CLAUDE.md / PIPELINE respected: everything one program uses stays beside it (kits-first); MIT notices; docs/04,
  HANDOFF, IDEAS, `build_docs.py` planned; package **declared, not published**; branch `AutoDev` only.

No gap sends the round back to 03 or 04. The notes below are **binding for the Developer** (step numbers are 03 §8's).

## Binding notes for the Developer

1. **The picker at the minimum window size (04 owner, resolved here).** 04 §2.1 places the picker's right column at
   fixed x 314–588 and *Play* at y 600, which needs a client of at least 600 × 680, but D1 allows `setMinSize (480,
   560)`. Resolve it with **`setMinSize (600, 680)`** (the default size: it still fits 1280 × 800, D1). Keep the
   Legend / RuleList hiding rules (they then only matter for a future smaller minimum). A developer who prefers a
   picker that reflows may keep 480 × 560, but must then check a picker shot at that size (scratch folder) and say so
   in `06-development.md`.
2. **The message LCD holds 31 bytes, not 32 characters** (`LcdDisplay`'s `m_text[32]`, `lcd_set` cuts at 31 bytes
   and may cut a UTF-8 sequence in two). Before `setText`, cut a message to ≤ 31 bytes **at a UTF-8 character
   boundary** (then D6's 13-px fallback for width). The shipped messages are all ≤ 31 bytes (longest: *"Chauves-souris
   dispersées !"*, 28 bytes) — keep them so. 02's "≤ 32 characters" limit in the loader stays as written.
3. **Haunted Manor's extra ball.** 02 §6 asks *"both banks ×2 → extra ball (once)"*, which the format cannot say (one
   event per `[rule]`; conditions are LATER) and `mktables.py` dropped it. Add an expressible rule — e.g. `when = bank
   bats`, `count = 2`, `once = 1`, `do = extraball`, *Extra ball!* / *Bille supplémentaire !* — and note the
   approximation in `06-development.md`.
4. **AC 16's example**: 02 writes *"lanes 1 and 2 lit, a left press gives {2, 3}"*, which contradicts its own rule
   "left flipper → left". Follow 03 §9.1: left shift with wrap, {1, 2} → **{1, 3}**; right shift {1, 2} → {2, 3}.
5. **AC 26's nudge**: assert **|Δv| = 150 ± 1** (the velocity's change), not a change of |v| (03 §9.1's wording).
6. **Members 03 uses but its sketches omit**: `World::searches` (AC 7), the test hook `World::onSubStep` (AC 6, null
   in the game), `LoadError::fmt` / `arg` (the translated reasons), `Game::ballSaveActive ()` and the nudge count (GUI
   plan). Add them.
7. **The picker's *Play* `ToolButton`**: `filled` paints only while `on` — `setOn (ok)` + `setDisabled (!ok)` as the
   mock does (`pinmock.cpp` l. 717–718). It takes no keyboard focus: Enter / Space / A are handled by `TableList` / the
   view.
8. **The name `Textbox` needs its callback** (`new Textbox (…, name, on_name_ok)`): without one, Enter returns false,
   goes to `GameRoot::onKey` → `view->key`, which ignores keys while an overlay is up (R11) — the name could not be
   confirmed with the keyboard. The pad's A / B / d-pad during an overlay are delivered to the focused widget's
   `onKey` (`KEY_ENTER`, 27, ↑ ↓).
9. **AC 31's sim scenario** (03 §9.3 step 3): build `quick.table` so the launched ball **scores** (crosses a lane or
   hits a bumper) and **drains quickly** without flippers, and wait with `waitlog N <text>` on a log line the app
   prints (e.g. `pinball: game over`) rather than a fixed ~600 waits. State the rule for a score of 0 (suggested: not
   entered in the top 5; AC 27's scores are all > 0).
10. **`--start multiball`** calls `Game::fire` with no rule: on `GE_MULTIBALL` the UI shows `TR ("MULTIBALL!")` (02
    #17) unless the firing rule has its own message — never both.
11. **Player tables in the picker shot**: the simulator lists a folder from `SIM_WRITES` only (overlay folders are not
    listed). To make `pinball.png` look like `pb-picker.png` with *Your tables*, copy the fixtures into
    `$OUT/writes/docs/pinball/` (works while the card has no `sdcard/docs/pinball/`; if SHOULD 1 ships that folder,
    accept the shot without them — AC 29 needs only the three tables). The mock's FR picker shows *Record 412 000* in
    the list but an empty top 5 for *Manoir hanté* — a mock artefact: in the app both come from `scores.ini`.
12. **Keys**: Ctrl+N arrives as `UK_CTRL ('N')` (the menu's shortcut), so plain `'n'` / `'N'` only nudge; keep Invaders'
    pattern. During play, `key ()` consumes flipper / plunger events (03 §4.3); the `hold` step of the simulator also
    sends a press event — make sure a `hold 32` is not also counted as a tap (tap = an event while `kapi_key_held` is
    false at the next frame).
13. **Not done here, by rule**: the card build (`make` from `kernel/`) is left to the user; no publishing
    (`tools/pkg/versions.ini` unchanged; `[app.pinball]` declared only); `mockups/` is never built nor shipped — lift
    `draw_static`, `draw_dynamic`, `lamp`, `pill`, `ball`, `flipper`, `words` and the drawn widgets by hand; the
    draft tables from `mktables.py` are a start, tuned until AC 6 / 7 / 9 pass (positions, not the physics'
    guarantees). Docs per step 14 + the GUI plan (the controls of 04 §7, the screens of 04 §2–§4, the `.table`
    format, the `[settings] table` key, `--seed` / `--start` "for tests"), then `python docs/build_docs.py`.
