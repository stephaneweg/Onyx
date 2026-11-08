---
name: windows-checkout-source-of-truth
description: "Edit in C:\Projects\Onyx (the user's rule, 2026-10-04); other sessions may edit the same checkout at the same time — commit only my own files"
metadata:
  type: feedback
---

The user asked (2026-10-04, during the shared-libraries work): every change must be in sync in
`C:\Projects\Onyx` — edit there, not in a WSL clone. Other Claude sessions / the user work in that
same checkout at the same time (that day: the shell's rework, branch `term_updates`).

**Why:** the user follows the work from that folder; a second session's uncommitted edits sit beside mine.

**How to apply:** edit in `C:\Projects\Onyx`; build the apps from it in WSL (`make -C /mnt/c/Projects/Onyx/user`);
for the kernel, rsync `kernel/` into a WSL build tree that has Circle built (`~/src/Onyx-shlib`, see
[[wsl-webkit-build-env]]) and copy the image back. Before a commit: `git status`, add only my own files
(a shared file like `user/bin/Makefile` may carry someone else's hunk: commit my hunk only). When the
user asks to "integrate branch X" whose commits were made from this same worktree, the working copies are
usually identical to the branch: check with `git hash-object`, restore them, then fast-forward.
