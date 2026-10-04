#!/bin/sh
# tools/pkg/publish.sh -- publish the packages (docs/pkg/README.md; the procedure: the skill
# .claude/skills/onyx-packages/SKILL.md). From sdcard/ as it is staged:
#
#   1. the repository stephaneweg/onyx-packages cloned (or brought up to date) beside: $ONYX_PACKAGES_DIR,
#      default ../onyx-packages;
#   2. tools/pkg/mkrepo.py --bump --db --lite sdcard_lite: the packages whose files changed get a new
#      version (tools/pkg/versions.ini), their .opk, the index signed; the card's database
#      (sdcard/var/pkg/db) and sdcard_lite made again;
#   3. the index's signature checked with the cards' public key (sdcard/etc/pkg/onyx.pub): a wrong key
#      stops here (every card would refuse the index);
#   4. the host test of pkg (tools/tests/run_pkg_test.sh);
#   5. onyx-packages committed and pushed (its main: GitHub Pages serves it).
#
# The private key (never in a repository, never in the chat): $ONYX_PKG_KEY_FILE (a path), or
# $ONYX_PKG_KEY (the PEM itself; "\n" escapes or base64 taken too -- the cloud environment's variable),
# or ~/.onyx/pkg-key.pem. Without one: nothing is published (exit 2).
#
#   sh tools/pkg/publish.sh [--no-push]      then commit in onyx: versions.ini, sdcard/var/pkg/db, sdcard_lite
set -e
cd "$(dirname "$0")/../.."
PUSH=1; [ "$1" = "--no-push" ] && PUSH=0
REPO=${ONYX_PACKAGES_DIR:-../onyx-packages}

# ---- the key ----
KEY=""; TMPKEY=""
if [ -n "$ONYX_PKG_KEY_FILE" ] && [ -f "$ONYX_PKG_KEY_FILE" ]; then KEY=$ONYX_PKG_KEY_FILE
elif [ -n "$ONYX_PKG_KEY" ]; then
	TMPKEY=$(mktemp); chmod 600 "$TMPKEY"; trap 'rm -f "$TMPKEY"' EXIT
	python3 - "$TMPKEY" <<'EOF'
import os, sys, base64
v = os.environ["ONYX_PKG_KEY"].strip ()
if "BEGIN" not in v: v = base64.b64decode (v).decode ()
v = v.replace ("\\n", "\n")
if "\n" not in v.strip ():			# (one line: the PEM's lines run together)
	h, rest = v.split ("-----", 2)[1], v.split ("-----", 2)[2]
	body, tail = rest.split ("-----", 1)
	v = "-----%s-----\n%s\n-----%s" % (h, "\n".join (body.split ()), tail)
open (sys.argv[1], "w").write (v.strip () + "\n")
EOF
	KEY=$TMPKEY
elif [ -f "$HOME/.onyx/pkg-key.pem" ]; then KEY=$HOME/.onyx/pkg-key.pem
else
	echo "publish: no signing key (ONYX_PKG_KEY / ONYX_PKG_KEY_FILE / ~/.onyx/pkg-key.pem): nothing published" >&2
	exit 2
fi

# ---- the repository ----
if [ ! -d "$REPO/.git" ]; then git clone -q https://github.com/stephaneweg/onyx-packages "$REPO"; fi
git -C "$REPO" config --get remote.origin.fetch >/dev/null || git -C "$REPO" config remote.origin.fetch '+refs/heads/*:refs/remotes/origin/*'
git -C "$REPO" fetch -q origin 2>/dev/null || true
if git -C "$REPO" rev-parse -q --verify origin/main >/dev/null; then
	git -C "$REPO" checkout -q main 2>/dev/null || git -C "$REPO" checkout -q -b main origin/main
	git -C "$REPO" merge -q --ff-only origin/main
fi

# ---- Jet's program: not in git (100 MB), carried by its package only. A checkout without it would publish
# a Jet without its program (2.0.2 and 2.0.4 were): taken back from the last package, else nothing published.
if [ ! -f sdcard/apps/jet.app/main ]; then
	LAST=$(ls -v "$REPO"/pkgs/jet-*.opk 2>/dev/null | tail -1)
	if [ -n "$LAST" ] && python3 -c "
import sys, zipfile
z = zipfile.ZipFile (sys.argv[1]); open ('sdcard/apps/jet.app/main', 'wb').write (z.read ('apps/jet.app/main'))" "$LAST" 2>/dev/null; then
		echo "publish: Jet's program taken back from $(basename "$LAST")"
	else
		rm -f sdcard/apps/jet.app/main
		echo "publish: sdcard/apps/jet.app/main is missing (build it: tools/webkit/build-web.sh): nothing published" >&2
		exit 2
	fi
fi

# ---- the packages ----
python3 tools/pkg/mkrepo.py --out "$REPO" --key "$KEY" --db --lite sdcard_lite --bump

# ---- the signature, with the cards' key ----
python3 - "$REPO" <<'EOF'
import sys
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
repo = sys.argv[1]
pub = serialization.load_pem_public_key (open ("sdcard/etc/pkg/onyx.pub", "rb").read ())
data = open (repo + "/index.txt", "rb").read ()
sig = bytes.fromhex (open (repo + "/index.sig").read ().strip ())
pub.verify (sig, data, ec.ECDSA (hashes.SHA256 ()))	# (raises: a key that is not the cards')
print ("publish: the index's signature is good for the cards' key")
EOF

# ---- the test ----
sh tools/tests/run_pkg_test.sh | tail -1

# ---- out ----
cd "$REPO"
if [ -n "$(git status --porcelain)" ]; then
	git add -A
	VERS=$(grep -A2 '^\[onyx\]' index.txt | sed -n 's/^version = //p')
	git -c user.name="$(git -C "$OLDPWD" config user.name || echo Onyx)" -c user.email="$(git -C "$OLDPWD" config user.email || echo onyx@localhost)" \
	    commit -q -m "Onyx packages: onyx $VERS ($(grep -c '^file = ' index.txt) packages), from onyx $(git -C "$OLDPWD" rev-parse --short HEAD)"
	if [ $PUSH = 1 ]; then git push -q origin HEAD:main && echo "publish: pushed to stephaneweg/onyx-packages (GitHub Pages: a minute or two)"; fi
else
	echo "publish: nothing changed in the repository"
fi
