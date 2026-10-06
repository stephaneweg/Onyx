#!/bin/sh
# run_gpiokit_test.sh -- GPIOKit on the PC: its simulator (gpiokit/gktest.cpp, under AddressSanitizer + UBSan),
# its header in C, and the BASIC runtime's places in GPIOKit's table (user/Libs/basic/runtime.cpp GKF_* =
# user/Kits/gpiokit/gpiokit.abi). The hardware side (kernel/sys/gpio.cpp) is the Pi's: docs/HANDOFF.md.
#   sh tools/tests/run_gpiokit_test.sh
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${TMPDIR:-/tmp}/onyx_gktest
INC="-I$ROOT/user -I$ROOT/user/Kits -I$ROOT/user/Runtime"
g++ -std=gnu++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -DGK_STANDALONE $INC \
    "$ROOT/user/Kits/gpiokit/gkcore.cpp" "$HERE/gpiokit/gktest.cpp" -o "$BIN"
"$BIN"
# the header in C
printf '#include "gpiokit/gpiokit.h"\nint main (void) { gk_event e; gk_pin_info p; (void) e; (void) p; return gk_available (); }\n' > "${BIN}_c.c"
gcc -std=c99 -Wall -Wextra -pedantic -fsyntax-only $INC "${BIN}_c.c" && echo "gpiokit.h: C ok"
# the BASIC runtime's places in the table
python3 - "$ROOT" <<'PY'
import re, sys
root = sys.argv[1]
abi = {}
for l in open (root + "/user/Kits/gpiokit/gpiokit.abi"):
	f = l.split ()
	if len (f) == 2 and f[0].isdigit (): abi[f[1]] = int (f[0])
bad = 0
for m in re.finditer (r"#define GKF_\w+\s+GK \((\d+),.*?, (gk_\w+)\)", open (root + "/user/Libs/basic/runtime.cpp").read ()):
	slot, name = int (m.group (1)), m.group (2)
	if abi.get (name) != slot: print ("FAIL runtime.cpp: %s at %d, gpiokit.abi says %s" % (name, slot, abi.get (name))); bad = 1
print ("runtime.cpp: GPIOKit's places ok" if not bad else "runtime.cpp: GPIOKit's places WRONG")
sys.exit (bad)
PY
