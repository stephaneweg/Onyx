# AutoDev — the autonomous development pipeline

The user's request (2026-10-06): a scheduled pipeline, on the branch **`AutoDev`**, that picks the
best application to build next for Onyx, analyses it, designs it, plans it, builds it and tests it
in the PC simulator, then reports. **Nothing goes into `main` until the user has validated it
personally.** One run of this file = one **round** = one application (or one feature).

Each role below is played by a **separate subagent** (the `Agent` tool), started by the round's
session — the *orchestrator* — with a prompt that hands it the previous roles' documents. The
orchestrator does not do the roles' work itself; it chains them, checks their outputs exist and
are complete, and loops when a check fails.

## 0. The round's rules (the orchestrator)

1. **Checkout**: the Onyx repository (`stephaneweg/onyx`), branch `AutoDev`
   (`git fetch origin AutoDev main && git checkout -B AutoDev origin/AutoDev`);
   `git submodule update --init circle` if `circle/` is empty.
2. **Resync with main first**: `git merge origin/main` (resolve conflicts; main has priority
   for anything fixed there). Commit the merge, push `AutoDev`.
3. **The round's number**: read `autodev/STATE.md`. If `rounds_done >= rounds_max`, stop: write
   nothing, say "AutoDev: the rounds are done". If `lock:` holds a round whose session is still
   running, stop (another round runs). Otherwise write `lock: round N, <UTC time>`, commit, push.
4. **The round's folder**: `autodev/rounds/NN-<app-slug>/` holds every role's document (below).
5. **Never** push to `main`, never merge into `main`, **never publish packages** (the
   onyx-packages skill and `tools/pkg/publish.sh` are NOT run in AutoDev, whatever CLAUDE.md says:
   publishing happens after the user's validation). Never delete branches. Commit often, push
   `AutoDev` after each role (`git push -u origin AutoDev`, retry on network errors 2/4/8/16 s).
6. CLAUDE.md's other rules hold: kits first, the documentation kept up to date (docs/04 catalog,
   docs/02/03 for kapi, kit docs regenerated), MIT notices, `python docs/build_docs.py`, the
   screenshots through `tools/tests/desktop_sim/shots.sh`. Docs in English, **the report in French**.
   **Every app is bilingual English / French from its first version** (CLAUDE.md, "every app in English and
   French"): `TR ()` + `lang/fr.txt`, its data texts (levels, help) with French keys, `tools/lang/check.py <app>`
   at 0 missing, the French screenshots checked (`SHOTS_LANG=fr`).
7. At the end: `rounds_done += 1`, the round added to `STATE.md`'s history, `lock:` cleared,
   commit, push.

**No time limit (the user, 2026-10-07)**: a round takes as long as its scope needs. Never cut the scope to
save time; use as many Developer subagents, one after the other, as the plan needs.

## 1. Product Manager — pick the best application

**First read `autodev/QUEUE.md`**: when it lists an item not yet done, that item is the pick
(the user's order); the scoring below then only frames its scope.

Inputs: `IDEAS.md`, `docs/HANDOFF.md` (the priority lists, "Next"), `docs/04-USER-GUIDE.md` §12
(the catalog), `user/Apps/`, `user/BinUtils/`, `autodev/STATE.md` (what earlier rounds did —
**never pick the same thing twice**, and never what the user set aside: the multi-user plan).

Score candidates on: value to the user (an everyday or educational use; the user's priorities in
HANDOFF/IDEAS first), what Onyx already has to build on (kits, drivers), feasibility **within one
round** with a testable result in the PC simulator (no work that needs real hardware to see
anything), risk to the rest of the system (prefer few kernel changes). Write
`01-product-manager.md`: the 5 best candidates with their scores, the pick and why.

## 2. Product Analyst — what the application is

Write `02-product-analysis.md`: the name (Onyx style: a plain, short name), the one-line pitch,
the users and their tasks, the features (must / should / later), the files it reads and writes
and their format, the file associations, how it fits with the existing apps (drag & drop, the
clipboard, notifications, the other apps' files), what it does NOT do, the acceptance criteria
(testable statements).

## 3. Technical Analyst — the gap and the plan

Explore the code (the kits' headers and docs `docs/06`, `docs/10`…`19`, similar apps, the BASIC
if relevant, the kernel only if needed). Write `03-technical-analysis.md`: what exists and can be
reused (by file), what is missing (in which kit, or beside the app), any kapi change (avoid one if
possible), the risks, and a **step-by-step implementation plan** (files to create / change, in
order, each step testable), the tests (unit tests on the PC, the simulator scenario).

## 4. UX Designer — the mockups and the GUI plan

Read docs/11-UIKIT.md, `docs/gui-redesign/README.md` (the desktop's design: a modernised CDE),
and two or three polished apps' code (Letters, Photos, Mail…) to stay consistent. Produce
**mockups** as images rendered with **UIKit itself** when possible (a throwaway gallery program
like `tools/tests/desktop_sim/gallery/studio.cpp`, through `studio.sh` / `shots.sh`), otherwise as
precise ASCII / SVG wireframes — every widget named after its UIKit class. Write
`04-ux-design.md` (+ the pictures beside it): the windows, menus, toolbars, dialogs, keyboard
shortcuts, states (empty, loading, error), and **amend the technical plan** with the GUI steps
(append a "GUI plan" section to `03-technical-analysis.md`, saying what was changed).

## 5. Technical Analyst again — the validation

A fresh Technical Analyst subagent reviews 02 + 03 + 04 together: every acceptance criterion
covered by a step, every widget available in UIKit (or its addition planned in UIKit), every
file / kit / kapi named, the tests defined. Write `05-validation.md`: **GREEN** or the list of
remaining gaps. Not green → back to the role that owns the gap (3 or 4), then validate again
(at most 3 loops; still not green → reduce the scope and say so in the report).

## 6. Developer — the implementation

A Developer subagent (several in sequence if the work is big, one step group each) implements
the plan: the app in `user/Apps/<name>/` (or what the plan says), `user/Makefile`,
`sdcard/apps/<name>.app/app.txt`, its package declared in `tools/pkg/packages.ini` (declared,
not published), the docs (docs/04 catalog + docs/03 if relevant), a `shots.sh` scenario and the
screenshot(s). It builds (`make` from `kernel/` for the apps, or the app's own target) and **must
pass the PC test bench**: the desktop simulator (`tools/tests/desktop_sim/run.sh`, `shots.sh
<name>`, the app's own tests), plus the repo's existing tests that the change touches. A failing
build or test is fixed, never skipped. Write `06-development.md`: what was done, by step, the
commits, the tests run and their results, what could not be done.

## 7. The report

Write `autodev/reports/round-NN-<app-slug>.md`, **in French**, for the user: the application
picked and why, what it does (with the screenshots), what was built (files, kits touched, kapi
changes if any), the tests and their results, what is left / known limits, **what the user must
check by hand** (on the Pi if hardware is involved), and how to try it. Also give this report as
the session's final message.
