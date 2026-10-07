# AutoDev round 6 — Validation (Technical Analyst, fresh): Clock + clockd

Date: 2026-10-07. Reviewed together: `02-product-analysis.md` (02 #1–34, files §5, **AC-1…AC-41**),
`03-technical-analysis.md` (§1–§9, facts 1–9, R-1…R-11, steps 0–13 + S1–S3, tests §8, the AC table §9, and its
appended **§10 GUI plan** G1–G8), `04-ux-design.md` (D1–D21, §1–§8) and `mockups/` (`clkmock.cpp`, `mockups.sh`,
`sd/…/fr.txt`, the 28 `clk-*.png`; `clk-alarms.png`, `clk-edit-fr.png` looked at). **`mockups.sh` was run here**: it
builds against today's UIKit and renders the 28 pictures byte-identical to the committed ones (13 s).
`run_kvtext_test.sh` (101 + 115 checks) and `run_notes_test.sh` pass today (the baselines step 0 and step 2 keep).

## Checked against the code

- **UIKit** (`user/Kits/uikit/`): `SegmentedControl (l, t, w, h, labels, n, sel, cb)` + `select`, `setEnabled`,
  `onKey` (Left / Right only); `NumericUpDown (l, t, w, h, lo, hi, val, step, cb)` (`setValue`; **PgUp / PgDn ±10·step**
  exist; it **takes Enter and Esc**, controls.cpp:203–204 — gap 5); `LcdDisplay` (`face`, `centred`, `setText`, 32-byte
  text); `Textbox` (`maxLen`, `changed`, Enter passed on when no `cb`); `ToolButton` (`setGlyph`, `setIcon (ToolIconFn,
  id)`, `setText`, `setToggle`, `filled`, `raised`, `fitWidth`, `setOn`, `setDisabled`); `WKT_PLUS / MINUS / TRASH /
  SEARCH / PLAY / PAUSE / STOP / TO_START / COPY`; `WKG_CHEV_UP / DOWN / CLOSE / CHECK`; `DataGrid` (`setColumns`,
  `column`, `setRows`, `stripes`, `cellDraw`, `emptyText`, `onSelect / onActivate`); `Menu::item (label, text, key, cb)`,
  `UK_CTRL` (= `c & 0x1F`: G4's `^1` = `^Q` is right), `Menu::shortcut` (`modal_open ()` first, then `UK_CTRL ('Q')` →
  `kapi_menu_command (MENU_QUIT)`: G1 and G3 are right); `Widget::modal` (handleKey / handleMouse route to the topmost
  modal child, Tab walks its controls), `hidden`, `disabled`, `removeChild`, `resizeTo` (virtual), `setFocus`;
  `Root::onTick / onResized / setResizable / setMinSize`; `VPath::arc (cx, cy, r, a0, a1, w)`; `uk_switch_mark`,
  `uk_sunken`, `uk_rbox`, `uk_title_strip`, `uk_scroll_bar`, `uk_glyph`, `uk_tone`, `uk_mix`, `uk_bright`,
  `uk_ink_for`; `UkFaceScope` (text.h); `TR / TRC / TRN / uk_lang_init / uk_lang`. **All exist: no UIKit change
  needed** (as 03 §10 says).
- **SystemKit**: `locale.h` (`locale_zone_count / city / summer / offset / utc`, `locale_zone`, `locale_set_zone`,
  `locale_ini_get / set`; the 23 zones of `locale.inc`, no half-hour zone — `+5 h 30` is a synthetic test only, as 03
  says); `notify_action` (payload `title\0text\0action\0`, `NOTIFY_MSG_SHOW` 1 — 03's logged form is right);
  `clip_set_text_n`; `autostart_ensure (cmd, after, comment)`. `systemkit.abi` ends at **75** → 76 / 77 are the next.
- **FileKit** `kvtext.h`: every function 03 uses; `fkkv_add_block_` / `fkkv_insert_` in `kvtext.inc` (the internals
  of §2.2); `filekit.abi` ends at **95** → 96–98 are the next.
- **AudioKit**: `ak_fm_instrument (voice, const kapi_fm_instrument *)`, `ak_fm_start (voice, milli_hz, wave, volume)`,
  `ak_fm_stop`, `ak_fm_silence`, `ak_out_open`; `ak_play_state` / `AK_BUSY` exist **but describe the file only**
  (gap 2).
- **AppKit**: `kapi_get_datetime`, `kapi_get_ticks` (100 Hz), `kapi_clock_info` (`utc_us`, `tz_minutes`,
  `KAPI_CLOCK_REALTIME_VALID`), `kapi_set_timezone (-720..840)`, `kapi_ipc_register / lookup`, `kapi_mailbox_*`,
  `kapi_raise_app`, `kapi_get_modifiers`, `MOD_CTRL`, `lx_launch` (`SD:apps/<n>.app/main` must exist, else
  `lx_launch_dir` → 0: see note 1). **No kapi change** — confirmed (`appkit.h`, `appkit_calls.inc`, `kapi_abi.h`
  untouched by the plan).
- **Kernel** facts 1, 3, 8: `kapi_set_timezone` → `CTimer::SetTimeZone` only; `SD:/etc/clock` stores UTC
  (crashlog.cpp:842/858); the shipped `sdcard/etc/system.ini` has **`timezone=120` and no `zone=`** (gap 1).
- **Simulator** `fakekapi.cpp`: `get_datetime` frozen 12:34:00; `clock_info` only with `SIM_STAT` (12:34 UTC, tz 0);
  `set_timezone` silent; `msleep` = `ms/10 + 1` ticks and one script step (clockd's `msleep (500)` = 51 ticks — 03's
  arithmetic, and the probe's 12:35:41, are right); `SIM_SERVICES` (lookup only the listed names, `register` 1 for any,
  sends logged), `SIM_MBOX` with `@<ticks>:`, `SIM_ARGS`, `SIM_LOG` + `waitlog`, `copy`, `mods`, `key`, `quit`,
  `exit`, `sim: exec / launch / raise_app / menu_command` logged; **`menu_command` only logs** (03 §10.3 already
  falls back to `quit`). `SIM_CLOCK` / `SIM_TZ` do not exist yet — **planned** in step 0.
- **shots.sh**: the existing `if want clock` block (l. 596) makes `screenshots/clock.png` from the **menu bar** — G5 is
  right; `build ()`'s FT `case` list and `APPS` (clock to be added in step 5); `lang`, `sim`, `png`; **`SHOTS_LANG`
  renders the same file names in that language** (gap 4).
- **Build / stage / package**: `FT_APPS`, `FT_EXTRA_*`, the `$(FT_APP_ELFS)` pattern rule on `Apps/%/main.cpp`; the
  `clipd.elf` newlib rule (`lib/systemkit.imp.a`; `pkgman` shows `lib/filekit.imp.a` linked the same way); `kernel/
  Makefile` `stage` copies every `user/*.elf` to `apps/<name>.app/main` (clockd staged with no Makefile change there);
  `sdcard/apps/clipd.app/app.txt` (`category = Shell`); `packages.ini [notes]` (the `[clock]` of 03 §7 has its shape);
  `tools/lang/check.py` scans `user/Apps/<app>/**` (clockd, in `user/Apps/clockd/`, is outside — it has no words, by
  design); `tools/docgen/kitdocs.py`; `tools/icons/*_icon.py`. No name clash (`clock`, `clockd` free in `user/Apps`,
  `BinUtils`, `sdcard/bin`).

## Verdict: **NOT GREEN** — five gaps, all small, all with their fix below

The plan is sound and nearly complete: every AC has a step and a test (03 §9 + §10.5), the core is UI-free and
host-testable, the GUI is proven by the mock-ups on today's UIKit, kits-first is respected (no new loose header; the
model beside the app with its two users only), and the 02 / 03 / 04 differences are recorded (below). What blocks:
one kit addition that would **move users' clocks by an hour**, one state the plan cannot detect as written, one
double-ring path left unspecified, the French screenshots that the amended shots plan would not produce, and the
editor's keys that UIKit does not deliver as 04 describes.

### Gaps

1. **[03 — Technical Analyst] `locale_zone_sync` must act only on an explicit `zone=`; and it must be documented.**
   As specified ("the chosen zone's offset now (`locale_zone ()`…)") it uses `locale_zone ()`, which, without `zone=`,
   **infers** the zone as the first whose *day-judged* offset equals `timezone=` (locale.inc:178–183). The card ships
   `timezone=120` with no `zone=` (and older cards set up before `zone=` existed have the same). On 2026-10-25 local
   (from 2026-10-24 22:00 UTC), Brussels' day-judged offset is 60, so `timezone=120` infers **Helsinki** (std 120);
   `locale_zone_offset_at (Helsinki, 22:00 UTC)` = 180 → clockd sets the clock to **UTC+3** and writes `timezone=180`
   (then nothing infers back: stuck). Fix in §2.1 / step 1b: `locale_zone_sync` reads `zone=` itself
   (`locale_ini_get ("zone")` matched to a city) and returns 0 when it is absent — never the inference; add the case
   to step 1b's test and to `clockd-sync`: "`timezone=120`, no `zone=`, SIM_CLOCK 2026-10-25 00:30 UTC → 0, nothing
   logged, nothing written". **Keep it (not deferred)** with that guard: it is otherwise safe (UTC never goes back, it
   writes `system.ini` only on a change, twice a year; the ringer's backward-jump rule absorbs the hour). Also plan:
   (a) **`tools/docgen/kitdocs.py`: add `"systemkit/locale.h"` to SystemKit's header list** — today it is not there, so
   "kitdocs → docs/12" would document neither new function (docs/12 has no `locale_*` table); (b) docs/04's *Language &
   Region* text and the Clock section: "while clockd runs, the summer time changes the clock by itself (zone chosen in
   Language & Region or Setup)"; docs/06's time-zone paragraph the same.

2. **[03 — Technical Analyst] "Sound unavailable" cannot be detected the way R-3 / R-4 / step 8 say.**
   `ak_play_state ()` is the **file's** state ("the live notes aside", audiokit.h:111): `player_main` turns a state into
   `AK_BUSY` only when a file is `AK_PLAYING` (akcore.cpp:310–313); with FM voices only it stays `AK_STOPPED`.
   `ak_fm_start` returns `fmsynth::start`'s result (a voice in range), not the output's. So D14 / 02 #25 / AC-25's
   no-sound case would never show. Fix: before a ring (and on ▶ Test) call **`ak_out_open (0, 0)`** — 1: the output is
   the Clock's (the player then mixes the FM voices into it); 0: another program holds it → D14's *Sound unavailable:
   the sound output is busy*; −1: no sound → D14 with *Sound unavailable* (a second wording, in `fr.txt`). Say it in R-3
   / R-4 / step 8, and how the sim checks it (fakekapi's `sound_acquire`: one case where it fails → the card's no-sound
   line logged, e.g. `clock: sound unavailable busy`), or state that the no-sound line is checked only by
   `clk-ring-nosound`'s mock and on the Pi.

3. **[03 — Technical Analyst] The timer handed to clockd, then the Clock reopened before it ends: unspecified.**
   R-1 / G1 write `[timer]` at every exit and clockd rings it; nothing says what the next Clock does with a `[timer]`
   still running. As written, the Timer tab shows the idle duration while clockd later starts `clock --ring timer` — or,
   if the Clock re-runs the timer itself, it rings **twice** (its own *Time's up*, then clockd's `CLOCK_MSG_OPEN
   "--ring timer"`). Specify (§3.6 / step 9): at start the Clock reads `[timer]`; still to come (`end > now`, the stale
   rule of §3.3 passed) → it **takes it back**: the Timer tab running with `end − now`, `[timer]` removed,
   `alarms_save` + `CLOCKD_MSG_RELOAD`; already past → left to clockd (or rung at once as *Time's up*). Add the sim case
   `clock-timer-resume` (fixture `[timer] end = <ticks ahead>`, `SIM_ARGS=timer` → log `timer running 0x:yy`, the
   block gone, `sim: send clockd type 2`) and adjust AC-40's wording (03 §10.5): "…the timer still rings, **and the
   reopened Clock shows it running**".

4. **[04 — UX Designer] §10.4: the French screenshots are not made by `SHOTS_LANG=fr`.**
   "`SHOTS_LANG=fr` renders the same names with `-fr`" is not what `shots.sh` does: `lang "$SHOTS_LANG"` (l. 29)
   renders the **same file names** in that language (and, with `PNG=screenshots`, overwrites the English ones).
   AC-3's French pictures need **explicit lines** in the block, the Critters / Pinball pattern (shots.sh:424, 445–447):
   `kfix; lang fr; sim clock clock-world-fr "…" …; png clock-world-fr; … ; lang "$SHOTS_LANG"`. List them in §10.4 —
   at least `clock-world-fr`, `clock-alarms-fr`, `clock-timer-fr`, `clock-stopwatch-fr`, `clock-ring-fr` (AC-3), plus
   `clock-edit-fr`, `clock-cities-fr`, `clock-timesup-fr` (10.5) — and keep "`SHOTS_LANG=fr` + `SHOTS_PNG=<scratch>`"
   only as the look-at-it-in-French check of CLAUDE.md.

5. **[04 — UX Designer] The editor's Enter / Esc and Ctrl+1…4 over a focused `NumericUpDown`.**
   `Widget::handleKey` (not virtual) gives a key to the focused child first; `NumericUpDown::onKey` **returns true for
   `KEY_ENTER` and Esc (27)** and for any digit (it does not look at Ctrl). So D12's "focus starts on *Hours*; Enter =
   OK, Esc = Cancel" does not happen (both keys are eaten by the spin box), and Ctrl+1…4 (G4, handled in the root's
   `onKey`) type a digit into a Timer spin box that has the focus. Fix in D12 / §7 and 03 §10.2 step 5: a **`Spin :
   NumericUpDown`** beside the app (`ui.h`) whose `onKey` returns false for any key with `MOD_CTRL` held, for Esc, and
   for Enter after `NumericUpDown::onKey (KEY_ENTER)` (the typed digits committed) — used for every spin box of the
   editor and the Timer; and a sim case (`clock-edit-keys`: `key 13` with the focus on *Hours* → the block written;
   `key 27` → nothing written; Timer tab, a spin box clicked, `mods 1;key 50;mods 0` → Alarms tab).

### Notes for the developer (not blocking)

1. **`clock-clockd`** (AC-27): `lx_launch ("clockd", 0)` needs `SD:apps/clockd.app/main` to exist (`lx_exists`), else
   nothing is logged — put a placeholder in the case's `SIM_WRITES/apps/clockd.app/main` (as 03 §4 does for
   `clock.app/main`); the log is then `sim: launch clockd` (no arguments → `kapi_launch`).
2. **Step 0**: keep every new simulator behaviour behind `SIM_CLOCK` — `clock_info` answering and `set_timezone`
   moving the wall time included (Setup and Language & Region call `kapi_set_timezone` in their shots); only the log
   line may be unconditional. Then AC-5's "other screenshots unchanged" holds; check it with a full `shots.sh` run.
3. **No zone in the simulator**: `get_datetime` stays 12:34:00, so the real *Time zone not set* screen shows
   `12:34:00` (the wall time), not the mock's `10:34:00`; fine (no shot of it is planned) — do not "fix" the HereCard
   to UTC for it.
4. **`[timer]` across a reboot**: an `end` in ticks set within ~1 min of a boot can pass §3.3's stale check in the
   next boot. Store the end's UTC too (`kapi_clock_info`, when valid) and require both to agree.
5. **Makefile**: `clockd.elf` also goes on the order-only `| lib/appkit_stubs.o` line (user/Makefile:52) beside
   `clipd.elf`; its recipe names `alarms.cpp clocktime.cpp` after `$<` and links `lib/filekit.imp.a
   lib/systemkit.imp.a` (as pkgman does). `shots.sh` `build ()`: `clock` in the FT `case` list, its `extra` =
   `alarms.cpp clocktime.cpp` + `$AK`.
6. `ToolButton::setIcon` takes a `ToolIconFn` (toolbar.h:35): a 3-line drawer calling `uk_glyph (WKG_CHEV_UP / DOWN)`
   in `ui.h` for D5's ▲ / ▼. The presets' double-click (02 #27, D17) is the app's: two clicks within ~400 ms (ToolButton
   has no double-click).
7. **The dock's label stays "Clock" in French**: the dock shows `app.txt`'s `name` untranslated (dock main.cpp:119; no
   app's label is translated today). 02 §1's "the dock's label through `lang/fr.txt`" is not possible this round — do
   not claim it in docs/04; the window's title is *Horloge*.
8. `kapi_get_datetime` returning 0 (no real date yet, before NTP on a card without `SD:/etc/clock`): the HereCard
   shows the time since boot — add a dim *Clock not set yet* line (one `TR` word) rather than a wrong date.
9. `clockd-reload`: `SIM_MBOX`'s `@<ticks>` counts the fake kernel's ticks — 51 per clockd step — so put the `copy`
   step before that tick; `waitlog` counts steps.
10. docs/06: one sentence in the FileKit `kvtext` paragraph (l. 574–590) for the blocks now written
    (`fk_kv_block_new / _set / _get`), beside the SystemKit time-zone one of step 1a.

## 02 / 03 / 04 agree, the differences recorded

- **"Quit anyway?"** (02 #34, AC-40): 03 R-1 kept it on Ctrl+Q only; 04 D20 / G1 removes it entirely (Ctrl+Q is
  `MENU_QUIT` inside `Menu::shortcut` before the app sees it — verified) and every exit hands over. 03 §10 supersedes
  step 11 and the `clock-quit` test (→ `clock-quit-hand`). Consistent; gap 3 completes the hand-over.
- **Screenshot names**: 03 §8.3's `clock.png …` superseded by G5 (`clock-world.png` …; `clock.png` is the menu bar's,
  verified) — consistent; gap 4 for the French ones.
- **The notification** (02 #17 "Clock — 07:00 School"): title *Clock*, text `07:00 School`, sent by the ringing Clock
  (03 §3.5, 04 §5) — the same bubble; AC-24 marked changed. *Laps copied* in the footer (clipd already bubbles).
- **The sounds** on the FM voices (R-3), *Missed alarm* kept on the row (R-2): reductions stated, AC-29 / AC-30 rewritten.
- **The ring started alone closes itself** (G2 / D15), the editor's padded `LcdDisplay` (G6), overlays as `Veil`s (G3):
  UX decisions within 02's must list.

## What the next pass checks

The five gaps closed in 03 (§2.1, step 1b and its test, kitdocs, R-3 / R-4 / step 8, §3.6 / step 9, §10.5) and 04
(§10.4's French lines, D12 / §7 and §10.2's `Spin`). Nothing else needs redoing: the mock-ups stand.

---

## Validation 2

Date: 2026-10-07 (a fresh Technical Analyst). Read: the five gaps above, `03-technical-analysis.md` (whole, with its
*Changes after validation 1*), `04-ux-design.md` (D12, D14, §7 and its *Changes after validation 1*), commits
`bc7bc181`, `5e0f91c7`, `b094c417`. Re-checked against the code: `user/Kits/uikit/controls.cpp:195–220`
(`NumericUpDown::onKey`), `widget.cpp:143–158` (`Widget::handleKey`), `user/Kits/audiokit/akcore.cpp:231–310`
(`out_acquire`, `ak_out_open`, `player_main`), `audiokit.h:133`, `tools/tests/desktop_sim/fakekapi.cpp:1041–1052`
(`sound_acquire`, `SIM_SOUND`), `user/Kits/systemkit/locale.inc:173–195`, `sdcard/etc/system.ini`,
`tools/docgen/kitdocs.py:36–38`, `tools/tests/desktop_sim/shots.sh:28–29, 424, 445–447, 596`, `user/Makefile:50–52,
552`. Run here: `run_kvtext_test.sh` (101 + 115) and `run_notes_test.sh` pass; `kitdocs.py` runs and is idempotent
today (no diff).

### The five gaps

1. **Closed.** §2.1 says `locale_zone_sync` reads `zone=` itself and never uses `locale_zone ()`'s inference
   (locale.inc:178–183 confirmed: that is where the guess is); step 1b's test and `clockd-sync-nozone` add the
   `timezone=120`, no `zone=` case at 2026-10-25 00:30 UTC; kitdocs gets `"systemkit/locale.h"` (it is indeed absent
   from SystemKit's list today); docs/04 (*Language & Region*, Clock) and docs/06 are in §2.1, §7 and step 13.
2. **Closed.** R-3 / R-4 / step 8 / §1 use `ak_out_open (0, 0)`: akcore.cpp confirms 1 (ours, `s_out = 1`, the player
   thread then mixes the FM voices into it and closes it after ~60 × 10 ms idle), 0 (busy), −1 (no sound:
   `kapi_sound_acquire`'s). In the simulator `sound_acquire` returns −1 without `SIM_SOUND` and logs
   `sim: sound acquired` with it — `clock-nosound` is runnable as written; the busy case is mock + Pi, stated.
3. **Closed as asked** (the take-back: §3.6, step 9, `clock-timer-resume` — its numbers check: `end` = 13 000 at
   tick ≈ 1 000 passes the stale rule, 120 s left, `02:00` rounded up; AC-40 in §9 and §10.5). **But see new gap 1
   below**: the other half of the timer's life — after it has rung — is not specified.
4. **Closed.** §10.4 has the explicit `lang fr … -fr … lang "$SHOTS_LANG"` lines (eight French shots, the Critters /
   Pinball pattern, verified at shots.sh:424, 445–447); `lang fr` writes the card's `system.ini` + `language=fr`
   (shots.sh:28), so the French shots have the same inferred zone as the English ones, as §10.4 says.
5. **Closed.** `Spin : NumericUpDown` in `ui.h` (03 G9, §10.2 step 5 / 7 / 9; 04 D12, §7's table): Ctrl keys passed on
   before the base (no digit typed), Enter after the base's commit, Esc after the base's drop — exactly what
   `NumericUpDown::onKey` + `Widget::handleKey` need (the focused child returns false → the veil's / root's `onKey`).
   `clock-edit-keys` (a)–(c) is runnable (`mods`, `key`, `down`/`up` exist in fakekapi).

### Verdict: **NOT GREEN** — two small gaps (both 03, the Technical Analyst; no mock-up or UX change needed)

1. **[03 — Technical Analyst] The `[timer]` after it has rung: who removes it, and clockd ringing it once.**
   The ringer's "exactly once" is the minutes' `last` (§3.3 rule 4); the `[timer]` entry is due on **ticks** ("due when
   `kapi_get_ticks () >= end` and `end − now <= set·100 + 100`"), a condition that **stays true** after `end` (`end −
   now` is negative) for the rest of the boot. clockd never writes `alarms.txt` (§3.2), and nothing says the Clock
   removes `[timer]` when it rings it. As written, clockd sends `CLOCK_MSG_OPEN "--ring timer"` (or `exec`s the Clock)
   **every 0.5 s** from `end` on; and gap 3's new rule "already past → left to clockd" makes a Clock started by that
   very ring leave the block in place. Fix (§3.3, §3.6, step 4 / 9, §8.1, §8.4): (a) the `Ringer` remembers the `end`
   it rang (`timer_rung`, kept across a reload like `last`): the same `end` is due **once**; (b) the Clock, on
   `--ring timer` (argument or message), shows *Time's up*, **removes `[timer]`**, `alarms_save` + `CLOCKD_MSG_RELOAD`
   (*+1 min* then runs it in the Clock as a fresh timer, handed over again at exit); (c) tests: alarm_test 19 extended
   ("stepped from T − 100 to T + 1 000: due exactly once; reloaded with the same `end`: not again"), a clockd case
   `clockd-timer` (`[timer] end` 300 ticks ahead, placeholder `clock.app/main`, 200 steps → exactly one
   `sim: exec SD:apps/clock.app/main --ring timer`), and `clock-timesup-hand` (`SIM_ARGS="--ring timer"`, fixture with
   a past `[timer]` → *Time's up* logged, the block gone from the written `alarms.txt`, `sim: send clockd type 2`).

2. **[03 — Technical Analyst] Superseded text still contradicting §10 in places the developer reads first.**
   The §9 AC-40 row was edited in this pass (now naming `clock-quit-hand`, which asserts *no* question) yet still says
   "**Ctrl+Q asks**; the close box hands over" — G1 says no question at all. Likewise still live: R-1 (a), step 11
   ("Ctrl+Q … *Quit anyway?* overlay"), §8.4's `clock-quit` row, §8.3's old block (`clock.png`, `clock-fr`) and its
   sentence "`SHOTS_LANG=fr …` renders every one of them in French as well — AC-3", and step 12's "then
   `SHOTS_LANG=fr …`". Fix: strike or mark each "superseded by §10 (G1 / G5 / §10.4)" — AC-40's row to read
   "**reduced** (R-1, G1): no question; every exit hands over; the reopened Clock shows the timer running"; step 11 to
   "keys of 04 §7, the hand-over at every exit (G1)"; `clock-quit` replaced by `clock-quit-hand`; §8.3's block replaced
   by a pointer to §10.4. Text only.

### Notes for the developer (not blocking)

1. **`ak_out_open (0, 0)` and the player.** audiokit.h:132 says "not together with the player: both want the output";
   it works here only because `s_out` is shared in the process (the player sees `s_out == 1` and does not re-acquire),
   but then the output keeps its current configuration instead of the player's low-latency `kapi_sound_config
   (512, 3)`. If the FM sound lags on the Pi, use the call as a probe: `r = ak_out_open (0, 0); if (r == 1)
   ak_out_close ();` then start the voices (the player re-acquires with its own settings; the race with another
   program in between is negligible). Do not hold the output when the ring card closes without sound.
2. **D14's second wording.** 03 R-3 has two wordings (−1: *Sound unavailable*; 0: *…the sound output is busy*); 04 D14
   and the mock's `fr.txt` have only the busy one. Add `Sound unavailable	Son indisponible` to `fr.txt` (check.py will
   ask anyway); the simulator's `clock-nosound` shows the plain one.
3. **04 §7: letters over a focused spin box.** The prose says "the letter keys act only when no `Textbox` / spin box has
   the focus", the new `Spin` table says Space / letters are passed on (Space starts the timer, R resets it). Follow
   the table (simpler: the root acts on what the spin box refuses) and keep only the `Textbox` exception.
4. **The HereCard on a card with no `zone=`** uses `locale_zone ()`'s guess for the city name and
   `locale_zone_offset_at (guess, utc)` for `UTC±h` / *Summer time* / the cities' differences: in the hours around a
   change night (the day-judged guess vs the instant) it can say `UTC+3` for a UTC+2 wall clock. Take the here offset
   from `kapi_clock_info`'s `tz_minutes` when valid (else `timezone=`), and the guess only for the city's name; the
   cities' times stay from UTC.
5. **`clockd-sync-nozone`** says "`system.ini` not in the writes": that holds if the case uses the card's own
   `sdcard/etc/system.ini` (`timezone=120`, no `zone=`, as shipped). If it puts a fixture in `SIM_WRITES/etc/`,
   assert "byte-identical to the fixture" instead.
6. **The take-back's tab.** A `[timer]` taken back with no argument (the last tab) — open on the Timer tab, or show
   the footer hint; 03 does not say: choose the Timer tab (the user would otherwise not see it running).

### What the next pass checks

New gaps 1 and 2 in 03 (§3.3, §3.6, steps 4 / 9 / 11 / 12, §8.1 case 19, §8.4, §9 row 40, R-1, §8.3). 04 and the
mock-ups stand.
