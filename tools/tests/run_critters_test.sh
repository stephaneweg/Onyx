#!/bin/sh
# run_critters_test.sh -- Critters' core on the PC (tools/tests/critters/critterstest.cpp; user/Apps/critters/terrain.cpp,
# level.cpp, world.cpp, solution.cpp, progress.cpp): the level reader and its errors, the terrain, every rule of the
# creatures and the roles, the end, determinism, the clock, the .sol reader, the progress, and the 12 shipped levels --
# each won by its recorded solution, each lost when nothing is done -- 02 §12 AC 1-24, 29-31.
# Built twice: ASan / UBSan at -O1, then -O2; the two builds' checksum streams of every solution must give the same
# bits (the "determinism fingerprint"). No warning allowed in the core. The core: integers only, no random number,
# no clock, no kapi, no UIKit (checked here by grep, comments stripped). crsim (tools/critters/crsim.cpp) is built too,
# so it never rots.
#
#   sh tools/tests/run_critters_test.sh [file.level ...]     -> "ok   critters (N checks ...)" (exit 0)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}
CORE="terrain level world solution progress"
SRC=$(for f in $CORE; do printf 'user/Apps/critters/%s.cpp ' $f; done)
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Apps -I kernel/include"
# the core: integers only, no rng / clock / kapi / UIKit (AC-20, AC-34) -- comments stripped first (a comment may say "no float")
for f in $CORE; do
	if sed 's,//.*,,' user/Apps/critters/$f.h user/Apps/critters/$f.cpp | grep -nE '\b(float|double)\b|\brng|gms *\(|clock *\(|time *\(|kapi_|uikit/|rand *\('; then
		echo "FAIL critters: $f uses a float, a random number, a clock, kapi or UIKit"; exit 1
	fi
done
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined $INC \
    tools/tests/critters/critterstest.cpp $SRC -o "$OUT/onyx_critters_asan"
g++ -std=c++17 -O2 -Wall -Wextra -Werror $INC tools/tests/critters/critterstest.cpp $SRC -o "$OUT/onyx_critters"
g++ -std=c++17 -O2 -Wall -Wextra -Werror $INC tools/critters/crsim.cpp $SRC -o "$OUT/crsim"
if [ $# -gt 0 ]; then T="$*"; else T=$(ls sdcard/apps/critters.app/levels/*.level 2>/dev/null || true); fi
"$OUT/onyx_critters_asan" $T > "$OUT/onyx_critters_asan.out" || { cat "$OUT/onyx_critters_asan.out"; exit 1; }
cat "$OUT/onyx_critters_asan.out"
"$OUT/onyx_critters" $T > "$OUT/onyx_critters.out" || { cat "$OUT/onyx_critters.out"; exit 1; }
cat "$OUT/onyx_critters.out"
# the same runs give the same bits at -O1 + sanitizers and at -O2
A=$(grep fingerprint "$OUT/onyx_critters_asan.out"); B=$(grep fingerprint "$OUT/onyx_critters.out")
if [ "$A" != "$B" ]; then echo "FAIL critters: the two builds differ ($A / $B)"; exit 1; fi
