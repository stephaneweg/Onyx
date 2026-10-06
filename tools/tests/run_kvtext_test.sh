#!/bin/sh
# run_kvtext_test.sh -- FileKit's key / value text documents (user/Kits/filekit/kvtext.h, fk_kv_*) on the PC:
# tools/tests/filekit/kvtest.cpp, the code inline (as every PC build has it), over the desktop simulator's kapi
# (the card read from sdcard/, what is saved into a folder of its own) with UBSan, and without the files with ASan; then kvtext.inc compiled
# as C (C99) and as the library's object (kvtext.cpp, FK_KV_IMPL: the exported fk_kv_* symbols).
#
#   sh tools/tests/run_kvtext_test.sh          -> "ok   kvtext (N checks)" (exit 0)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/onyx_kvtext_test
rm -rf "$OUT"; mkdir -p "$OUT/w"
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I kernel/include"
# 1. AddressSanitizer + UBSan, everything but the files (fakekapi maps the kapi table at a fixed address, where ASan
#    keeps its shadow memory: the two do not go together)
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Werror $INC -fsanitize=address,undefined -fno-sanitize-recover=undefined -DKV_NO_FILES \
    tools/tests/filekit/kvtest.cpp -o "$OUT/kvtest_asan"
"$OUT/kvtest_asan" > "$OUT/asan.log" || { cat "$OUT/asan.log"; echo "FAIL kvtext (ASan build)"; exit 1; }
sed -n "s/^ok   kvtext (/ok   kvtext, ASan build, no files (/p" "$OUT/asan.log"
# 2. UBSan, everything, the files through the stand-in kernel (fakekapi.cpp: its warnings not ours)
g++ -std=gnu++17 -O1 -g -w $INC -c tools/tests/desktop_sim/fakekapi.cpp -o "$OUT/fakekapi.o"
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Werror $INC -fsanitize=undefined -fno-sanitize-recover=undefined -c tools/tests/filekit/kvtest.cpp -o "$OUT/kvtest.o"
g++ -fsanitize=undefined -o "$OUT/kvtest" "$OUT/kvtest.o" "$OUT/fakekapi.o" -lpthread
# C: the header and its inline code
printf '#include "filekit/filekit.h"\nint kv_c_test (void) { fk_kv *kv = fk_kv_new (FK_KV_PIPES); int n = fk_kv_count (kv); fk_kv_free (kv); return n; }\n' > "$OUT/c.c"
gcc -std=c99 -Wall -Wextra -Werror -fsyntax-only $INC "$OUT/c.c"
# the library's object: the 18 functions exported (libgen's '^(fk_|fs_)'), no helper among them
g++ -std=gnu++17 -O1 -Wall -Wextra -Werror $INC -c user/Kits/filekit/kvtext.cpp -o "$OUT/kvtext.o"
EXP=$(nm -g --defined-only "$OUT/kvtext.o" | awk '{print $3}' | grep -E '^(fk_|fs_)' | sort | tr '\n' ' ')
N=$(echo $EXP | wc -w)
[ "$N" = 18 ] || { echo "FAIL kvtext: kvtext.o exports $N symbols, 18 expected: $EXP"; exit 1; }
SIM_WRITES="$OUT/w" SIM_SD=sdcard "$OUT/kvtest"
