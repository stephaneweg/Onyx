#!/bin/sh
#
# apply.sh -- apply the Onyx Circle patches to ../circle (relative to the repo root).
# Idempotent: skips a patch that is already applied. Run from the repo root or from
# circle-patches/ -- the circle tree is resolved relative to this script.
#
set -e

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
circle=$(CDPATH= cd -- "$here/../circle" && pwd) || {
	echo "error: ../circle not found -- clone it first (see README.md)"; exit 1; }

echo "Applying Circle patches to: $circle"
for p in "$here"/*.patch; do
	[ -e "$p" ] || continue
	name=$(basename "$p")
	if git -C "$circle" apply --reverse --check "$p" 2>/dev/null; then
		echo "  already applied: $name"
	elif git -C "$circle" apply --check "$p" 2>/dev/null; then
		git -C "$circle" apply "$p"
		echo "  applied:         $name"
	else
		echo "  FAILED (conflict): $name -- resolve manually"; exit 1
	fi
done
echo "Done. Now build Circle (see circle-patches/README.md)."
