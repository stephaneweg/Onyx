# Circle patches

Onyx builds on **Circle** (the bare-metal RPi environment) as its HAL. Circle is a
large third-party tree (~256 MB, 1700+ files) cloned **separately** into `../circle`
(git-ignored, see the repo `.gitignore`) — it is **not** vendored into this repo.

We carry a few small local changes to Circle. Rather than fork the whole tree, we
pin the exact upstream commit and keep our changes here as a tracked **patch series**,
so the build is reproducible and our patch history lives in this repo.

## Upstream

| | |
|---|---|
| Repository | https://github.com/rsta2/circle.git |
| Pinned base commit | `6177984e30fac5e65582d171d43f1563368a94ac` |
| Tag | `Step51` |

## Set up `../circle` from scratch

```sh
git clone https://github.com/rsta2/circle.git ../circle
git -C ../circle checkout 6177984e
( cd ../circle && git apply ../zircon/circle-patches/*.patch )   # or: ./circle-patches/apply.sh
# build Circle + the addons Onyx links (incl. the network stack):
( cd ../circle && ./configure -r 4 -p aarch64-none-elf- -f && ./makeall --nosample )
( cd ../circle/lib/net && make )                 # libnet.a
( cd ../circle/addon/wlan && ./makeall --nosample )   # libwlan.a + libwpa_supplicant.a
```

`apply.sh` (next to this file) applies the patches to `../circle` and tolerates an
already-patched tree.

## The patches

| Patch | Files | What / why |
|---|---|---|
| `0001-runtime-keymap-switching.patch` | `include/circle/input/keyboardbehaviour.h`, `include/circle/input/keymap.h`, `include/circle/usb/usbkeyboard.h`, `lib/input/keymap.cpp` | Expose `GetKeyMap()` accessors + a `CKeyMap::LoadMap(locale)` so Onyx can switch the keyboard layout **at runtime** (the `keyb` command, the theme editor). Stock Circle only sets the map at construction. |
| `0002-max-tasks-40.patch` | `include/circle/sysconfig.h` | Raise `MAX_TASKS` 20 → 40: the network stack adds background tasks (net/DHCP/WPA + NTP/IRC). Onyx links its own scheduler, but the constant still sizes its task array. |

> Not patches: the local `Rules.mk.bak` / `wpa_supplicant.conf.bak` files in some
> circle checkouts are stray editor backups, and the sample `wpa_supplicant.conf`
> holds personal Wi-Fi creds — none of these belong in the patch series.

## Updating the patches

After editing files under `../circle`, regenerate from the pinned base:

```sh
git -C ../circle diff 6177984e -- <paths…> > circle-patches/000N-<name>.patch
```

To move to a newer Circle: clone/checkout the new commit, re-apply (fixing any
rejects), update the pinned commit above, and regenerate the `.patch` files.
