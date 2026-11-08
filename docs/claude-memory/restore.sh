#!/bin/sh
# Restore Claude Code's project memory from this backup (MIT).
# Run from Git Bash (Windows) or a Linux / macOS shell, wherever the repository is cloned:
#   sh docs/claude-memory/restore.sh
# The memory folder is named after the checkout's path: C:\Projects\Onyx -> C--Projects-Onyx.
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
if command -v cygpath >/dev/null 2>&1; then repo=$(cygpath -w "$repo"); fi
name=$(printf '%s' "$repo" | sed 's/[^A-Za-z0-9]/-/g')
dest="$HOME/.claude/projects/$name/memory"
mkdir -p "$dest"
for f in "$here"/*.md; do
  [ "$(basename "$f")" = README.md ] || cp "$f" "$dest/"
done
echo "Memory restored into $dest ($(ls "$dest" | wc -l) files)"
