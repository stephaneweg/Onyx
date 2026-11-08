---
name: localization-en-fr
description: Onyx apps are in English + French, the language is the system's (Language & Region); new apps translated from the start (the user, 2026-10-06)
metadata:
  type: feedback
---

The user asked on 2026-10-06 for a system language setting (Control Panel "Language & Region" + Setup's welcome page) and said: every NEW app is translated systematically (English + French); the older apps are translated progressively, as they are worked on. No app keeps its own language toggle on Onyx (Ledger's and Turtle's were removed; the Mac port of Ledger keeps its own, `#ifdef __APPLE__`).

**Why:** the user's words: "au fur et à mesure on traduira les applications dans les 2 langues, les nouvelles feront l'objet d'une traduction systématique".

**How to apply:** the rule and the mechanics are in CLAUDE.md ("every app in English and French") and docs/03: `TR ()`, `sdcard/apps/<app>.app/lang/fr.txt`, `python tools/lang/check.py <app>`, `SHOTS_LANG=fr` renders. Turtle's French BASIC vocabulary was given by the user (AVANCER, SI/SINON/SINON SI, POUR/JUSQUE/SUITE, SUB/FIN SUB, FONCTION, CLASSE, ENTIER, CHAINE); the rest (PAS, RETOUR, COMME, SELON/CAS, JUSQUA = UNTIL...) is my choice, see `user/Apps/turtle/world.h`. Publishing packages from this PC: the WSL clone `~/src/onyx-packages` is stale and dirty — use a fresh clone (I used `~/src/onyx-packages-l10n`). See [[kits-first-rule]].

BASIC's number types (the user, 2026-10-06): two kinds for the beginner -- `INTEGER` (alias of `INTEGER64`; exact to 2^53 is accepted "for now") and `REAL` (double, shown short: 7 digits); `BYTE` for memory / strings / files; sizes only where bytes count: `INTEGER16/32/64`, `REAL32/64`. QBasic's `LONG`/`SINGLE`/`DOUBLE` and suffixes stay accepted. French: ENTIER, REEL, OCTET, CHAINE, ENTIER16/32/64, REEL32/64 (SIMPLE dropped). QBasic never had SHORT.
