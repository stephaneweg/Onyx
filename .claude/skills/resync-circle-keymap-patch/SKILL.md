---
name: resync-circle-keymap-patch
description: >-
  Re-apply Onyx's "keyboard map decoupled from the kernel" patch to the vendored Circle
  fork after the fork is re-synced/rebased/merged/updated against upstream (Step51). USE
  THIS SKILL whenever you pull, merge, rebase, re-clone or otherwise update the nested
  `circle/` repo, OR whenever you notice upstream Circle code has come back in
  `circle/lib/input/keymap.cpp` / `circle/include/circle/input/keymap.h` (telltale signs:
  `s_DefaultMap`, `s_MapDirectory`, `LookupDefaultMap`, `CKeyMap::LoadMap`, `#include
  "keymap_de.h"`, or a `CKeyMap` constructor that `memcpy`s a default map). Onyx ships NO
  compiled-in keyboard maps — layouts are `SD:/etc/keymaps/*.kmap` data files — and an
  upstream sync silently re-introduces the country tables, so this patch must be re-applied
  every time. Also covers moving the `keymap_*.h` sources to `tools/keymaps/maps/` and
  regenerating the `.kmap` files. Do not skip it just because the build still links — the
  kernel would bloat back up and `LoadMap`-by-name behaviour would creep back.
---

# Re-apply the Circle keymap-decoupling patch after an upstream sync

## Why this exists

Onyx deliberately carries **no keyboard map in the kernel**. Keyboard layouts are data files
(`SD:/etc/keymaps/<NAME>.kmap`, format `"OKM1"` + `u16` rows(128)/cols(5) + `u16[128][5]`),
loaded at runtime by the `keyb` tool / `theme` editor via `kapi_set_keymap_data` (ABI v27).
This keeps the kernel small and lets a new layout (e.g. `BE`) ship without a kernel rebuild.

Upstream Circle (our fork's base is `Step51`) does the opposite: it bakes the country maps
into the binary and selects one at boot. So **every time the `circle/` fork is re-synced with
upstream, the upstream keymap code returns and our decoupling is lost.** This skill re-applies
it. The authoritative description of the patch lives in
[`docs/05-CIRCLE-CHANGES.md`](../../../docs/05-CIRCLE-CHANGES.md) §1 — keep that in sync if the
patch shape changes.

`circle/` is a **nested git repo** (it shows as `m circle` / untracked in the Onyx repo). Its
own working tree carries these edits; commit them in the `circle/` repo, not the Onyx one.

## When to run

Run the whole procedure if **any** of these is true after touching the fork:

```bash
# 1) Compiled-in maps came back in Circle?
grep -nE "s_DefaultMap|s_MapDirectory|LookupDefaultMap|CKeyMap::LoadMap" circle/lib/input/keymap.cpp circle/include/circle/input/keymap.h
# 2) The country tables are back inside the Circle tree?
ls circle/lib/input/keymap_*.h 2>/dev/null
# 3) Our maps moved out — are the sources still where genkeymaps expects them?
ls tools/keymaps/maps/keymap_*.h
```

If (1) or (2) print anything, the patch was reverted — do steps 1–4. If only (3) is empty,
just the maps move/regen is needed (steps 3–4).

## Step 1 — Empty the `CKeyMap` table (no compiled default)

`circle/lib/input/keymap.cpp`: delete the `C(chr)` macro, the `s_DefaultMap[][...]`
definition (the block of `#include "keymap_xx.h"`), the `s_MapDirectory[]` definition, and
the `LookupDefaultMap()` definition. Replace the constructor so it just zeroes the table:

```cpp
// Zircon: the kernel compiles in NO country maps -- keyboard layouts ship as
// SD:/etc/keymaps/*.kmap data files and are loaded at runtime through ClearTable/SetEntry
// (see kapi_set_keymap_data). So a fresh keymap starts empty (every entry KeyNone); the
// kernel fills it from a .kmap as soon as the keyboard has enumerated.
CKeyMap::CKeyMap (void)
:	m_bCapsLock (FALSE),
	m_bNumLock (FALSE),
	m_bScrollLock (FALSE)
{
	memset (m_KeyMap, 0, sizeof m_KeyMap);		// KeyNone == 0
}
```

Also delete the `CKeyMap::LoadMap(const char *)` definition entirely. Leave `ClearTable`,
`SetEntry`, `Translate`, `GetString`, `GetLEDStatus` untouched — those are what the runtime
loader uses. (Unused `#include`s for `koptions.h`/`sysconfig.h`/`assert.h` are harmless; the
build has no `-Werror`, so there's no need to prune them.)

`circle/include/circle/input/keymap.h`: remove the `LoadMap` declaration, the
`LookupDefaultMap` declaration, and the `s_DefaultMap` / `s_MapDirectory` static members.
Keep `m_KeyMap`, the lock flags, and `s_KeyStrings`.

## Step 2 — Dependency check: the `GetKeyMap()` accessors must survive

The kernel reaches the live `CKeyMap` through `GetKeyMap()` accessors added to
`circle/include/circle/input/keyboardbehaviour.h` and `circle/include/circle/usb/usbkeyboard.h`
(part of Circle patch #1 in `docs/05`). A hard re-sync wipes these too. Verify:

```bash
grep -n "GetKeyMap" circle/include/circle/input/keyboardbehaviour.h circle/include/circle/usb/usbkeyboard.h
```

If missing, re-add the accessor that returns the underlying `CKeyMap *` up the device stack
(see `docs/05-CIRCLE-CHANGES.md` §1). Without it, `kernel.cpp` won't compile.

## Step 3 — Keep the layout sources in `tools/keymaps/maps/` (not in Circle)

All 8 layout tables (`keymap_be/de/dv/es/fr/it/uk/us.h`) live in **`tools/keymaps/maps/`**,
tracked in the Onyx repo. If an upstream sync re-created them under `circle/lib/input/`,
remove those copies so the Circle tree carries none and the only sources are ours:

```bash
# Our 8 sources should already exist here (BE is custom; the 7 others were lifted from
# upstream Circle once and now live with us):
ls tools/keymaps/maps/keymap_*.h
# Drop any that upstream re-created inside Circle, plus the stale dep/obj that would list
# the (now-removed) headers as prerequisites and break the build:
rm -f circle/lib/input/keymap_*.h circle/lib/input/keymap.d circle/lib/input/keymap.o
```

If for some reason a stock source is genuinely missing from `tools/keymaps/maps/`, recover it
from the upstream Circle table before deleting the Circle copy (`git show` in the `circle/`
repo, or copy it across) — don't lose a layout.

`tools/keymaps/genkeymaps.py` finds each table via `src_path()` (Circle dir first, then
`tools/keymaps/maps/`); with the Circle copies gone it picks ours up. No script change needed.

## Step 4 — Regenerate the `.kmap` files and verify

```bash
python tools/keymaps/genkeymaps.py            # writes sdcard/etc/keymaps/*.kmap (8 files)
# Sanity: no dangling references to the removed symbols anywhere in source:
grep -nE "s_DefaultMap|s_MapDirectory|LookupDefaultMap|CKeyMap::LoadMap|GetKeyMap \(\)->LoadMap" \
  circle/lib/input/keymap.cpp circle/include/circle/input/keymap.h kernel/kernel.cpp
```

The generator prints `wrote <X>.kmap (1288 bytes)` for each of BE/DE/DV/ES/FR/IT/UK/US. The
grep should print nothing.

**Rebuild:** the Circle change means `libinput` must be rebuilt; from `kernel/` run `make`
(it rebuilds the affected Circle lib + the kernel) then `make stage`. The `.kmap` files are
data on the card — already staged by living in `sdcard/etc/keymaps/`.

## What stays on the Onyx side (verify, don't re-do)

These pieces live in the **Onyx** repo, so a *Circle*-only sync does not touch them — but
confirm they're still present, since they're what makes the empty-kernel design work:

- `kernel/kernel.cpp`: the persistent `g_KeyMap` RAM table + `LoadKeyMapTable()` helper;
  `CInputTask::SetKeyMapData` copies the `.kmap` payload into `g_KeyMap` then onto the live
  keyboard via `SetEntry`; `Detect()` re-applies `g_KeyMap` on every keyboard (re-)attach
  (so a hot re-plug keeps the layout); `KernelSetKeyMap` returns `FALSE` (by-name loading is
  gone — the ABI slot stays for append-only compatibility).
- `user/applib.h`: `ax_load_keymap(name)` (file-first, then the now-inert `kapi_set_keymap`).
- `user/theme.c`: `scan_keymaps()` fills the keymap dropdown from `SD:/etc/keymaps/*.kmap`.
- `user/bin/keyb.c`: lists `US UK DE FR BE ES IT DV` and uses `ax_load_keymap`.

If a full Onyx-branch rebase also clobbered these, re-derive them from
`docs/05-CIRCLE-CHANGES.md` §1 and the kernel-internals ABI notes (`docs/02`).
