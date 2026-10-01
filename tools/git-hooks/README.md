# Git hooks

Version-controlled git hooks for this repo. Activate them once per clone:

```sh
git config core.hooksPath tools/git-hooks
```

## `pre-commit` — keep the Wi-Fi credentials out of git

`sdcard/etc/wpa_supplicant.conf` holds the user's Wi-Fi network and passphrase in clear
text. It is **not tracked** (`.gitignore`) and **never packaged** (`tools/pkg/mkrepo.py`'s
`PRIVATE`): it lives on the SD card only, written by the Wi-Fi settings (`wpaconf`) or the
menu bar's Wi-Fi menu. The hook refuses a commit that stages it anyway (`git add -f`).
