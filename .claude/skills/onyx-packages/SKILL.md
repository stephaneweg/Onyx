---
name: onyx-packages
description: >-
  Package and publish Onyx's software to the package repository stephaneweg/onyx-packages
  (GitHub Pages, read by `pkg`, the Package Manager pkgman and the update daemon pkgd on the
  Pi). USE THIS SKILL, without being asked, at the end of ANY work that changes what is on the
  card: a NEW app (user/Apps/<name>, sdcard/apps/<name>.app), an app or a /bin tool rebuilt
  and staged (sdcard/apps/*/main, sdcard/bin/*), the kernel image, sdcard/etc/*, the firmware,
  fonts, res/, samples (sdcard/docs, basic/, music/...). It covers declaring a new app's
  package (tools/pkg/packages.ini: its extra files, its needs, its user files, its samples;
  an emulator's app.txt `games =`), raising versions, building the .opk archives, signing
  and publishing the index (tools/pkg/publish.sh), and committing what changes in onyx
  (tools/pkg/versions.ini, sdcard/var/pkg/db, sdcard_lite). Do not skip it because "only one
  app changed": a card updates only from the repository.
---

# Publishing Onyx's packages (onyx-packages)

The design and formats: `docs/pkg/README.md`. In short: every file of `sdcard/` belongs to one
package (`tools/pkg/packages.ini`); `tools/pkg/mkrepo.py` makes a `.opk` (a ZIP of the card's tree +
`PKG/manifest.ini`) for each, an `index.txt` signed into `index.sig` (ECDSA P-256) with the user's
private key; the cards check it with `sdcard/etc/pkg/onyx.pub`. `tools/pkg/publish.sh` does the
whole procedure. **When the work changed the card, publishing is part of the work.**

## 1. When

At the end of a task, after the build, `make stage` (or the copies into `sdcard/`), the tests and
the documentation (CLAUDE.md's rule), whenever `git status` shows changes under `sdcard/` (other than
`sdcard/var/pkg/db` and `sdcard_lite`, which this procedure itself rewrites) or in
`tools/pkg/packages.ini`. Not for a change that stays in the sources (docs, tests, tools).

## 2. A new app: declare its package

`[*apps]` already makes every `apps/<name>.app` a package of its own (title and category from its
`app.txt`; **give it a `category`**: Shell and Settings apps go into the system package `onyx`
automatically). Then decide, and write in `tools/pkg/packages.ini`:

- **Files outside its bundle** (`res/…`, `koton/…`, `manuals/<app>/…`): `[app.<name>]` `files = …`
  — a file belongs to the first section that names it; mkrepo prints the files left in no package:
  **there must be none**.
- **Needs**: an app it cannot work without (an emulator → `gamelib`; Letters / Sheet / Ledger →
  `cardfile`): `[app.<name>]` `needs = gamelib`. A mere link ("Open in Letters") is not a need.
  Something the system itself needs (the wallpaper's painter, the text editor other apps open) goes
  into `[onyx]`'s `files` instead.
- **The user's files** (a `config.ini` shipped with defaults, documents): `config = <paths>` — never
  overwritten once the user changed them (the new one written beside as `.new`).
- **Samples** (documents to try the app with): a section `[<app>-samples]` (`files`, the same paths
  as `config`, `needs = <app>`), as `letters-samples`, `basic-samples`.
- **An emulator**: its `app.txt` says what it plays — `category = Emulators`,
  `games = <System>: <ext> <ext>; <System>: <ext>`, `order = <n>` (and `opens = <ext>` for files that
  are not games) — and `needs = gamelib`. The Game Library and `launch.h` find it from there; never
  add its extensions to `etc/runners.ini`.
- **A file type it opens** (not a game): `opens = <ext>` in its `app.txt`, or `etc/fileassoc.ini`.

## 3. Publish

1. The repository, writable in this session: call `add_repo` for **`stephaneweg/onyx-packages`**
   with `access: "push"`, clone it as its result says (e.g. `/home/user/onyx-packages`), then
   `register_repo_root`. (Locally on the user's PC: a clone beside `onyx`, `../onyx-packages`.)
2. Run, from the onyx checkout:
   ```
   ONYX_PACKAGES_DIR=/home/user/onyx-packages sh tools/pkg/publish.sh
   ```
   It brings the clone up to date, runs `mkrepo.py --bump --db --lite sdcard_lite` (a package whose
   files changed gets its version's last number raised in `tools/pkg/versions.ini`; to give a
   bigger version, write it in `versions.ini` first), **checks the new index's signature with the
   cards' public key**, runs `tools/tests/run_pkg_test.sh`, then commits and pushes `onyx-packages`
   (its `main`: GitHub Pages serves it). "nothing changed in the repository": nothing to publish.
3. **The key**: `publish.sh` reads the user's private key from the environment variable
   **`ONYX_PKG_KEY`** (set by the user in the cloud environment's settings; or `ONYX_PKG_KEY_FILE`,
   or `~/.onyx/pkg-key.pem` on their PC). If it exits with *no signing key*: **stop the publishing
   there** and tell the user, in one line, to add `ONYX_PKG_KEY` (the content of their
   `pkg-key.pem`) to the environment's variables — never ask for the key in the chat.
4. Commit in **onyx** what the procedure changed: `tools/pkg/versions.ini`, `sdcard/var/pkg/db/`,
   `sdcard_lite/` (and `tools/pkg/packages.ini`, the app's `app.txt`), with the new versions in the
   commit message; push as usual.
5. Tell the user the packages published and their versions (mkrepo's "bumped: …" line). GitHub
   Pages serves the new index a minute or two later:
   `curl -s https://stephaneweg.github.io/onyx-packages/index.txt | grep -A2 '^\[<name>\]'`.

- **Jet's program** (`sdcard/apps/jet.app/main`, 100 MB) is not in git: a fresh checkout lacks it.
   `publish.sh` takes it back from the last `jet-*.opk` of the repository before packaging (else it stops),
   so `jet` is never published without its program again (2.0.2 and 2.0.4 were). It notes the package it took
   it from in `.jet-from-package` (not in git) and takes it again when a newer `jet-*.opk` came since -- unless
   the program was built here after (2.0.7 was published with 2.0.5's program, left on the disk by an earlier
   publish; 2.0.8 put 2.0.6's back).

## Never

- Never run `tools/pkg/keygen.py` to "fix" a missing key, never sign with another key: every card
  would refuse the index (the cards hold the user's public key).
- Never write the private key into a file of a repository, a commit, a log or the chat.
- Never edit `index.txt` / `index.sig` / the `.opk` by hand, never push to `onyx-packages` other than
  through `publish.sh`; never lower a version.
- Never publish from a card that was not staged from the current build (`make stage` first).
- Never remove a package from `packages.ini` silently: the cards that have it keep it, without
  updates — say so to the user.
