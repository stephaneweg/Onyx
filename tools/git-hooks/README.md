# Git hooks

Version-controlled git hooks for this repo. Activate them once per clone:

```sh
git config core.hooksPath tools/git-hooks
```

## `pre-commit` — redact the Wi-Fi PSK

`sdcard/wpa_supplicant.conf` is tracked so the SSID and structure are versioned, but
its passphrase must never enter git history. Two layers keep it out:

1. **`skip-worktree`** on the file — git ignores local edits, so the real key in the
   working copy (needed for the SD card) is never staged and `git status` stays clean:

   ```sh
   git update-index --skip-worktree sdcard/wpa_supplicant.conf
   ```

   To intentionally change what is committed (e.g. the SSID), temporarily clear it:
   `git update-index --no-skip-worktree sdcard/wpa_supplicant.conf`, edit/commit (the
   hook still redacts the psk), then set `--skip-worktree` again.

2. **`pre-commit` hook** — if the file ever does get staged, it rewrites only the
   staged blob so the committed `psk` is `"REDACTED-set-on-the-SD-card"`. The working
   tree (real key) is untouched.

So: the working copy / SD card has the real key; the committed copy never does.
