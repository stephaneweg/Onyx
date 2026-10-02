#!/bin/sh
# tc.sh -- WP-TC's toolchain on the posixsim bench (docs/POSIX-PLAN.md "WP-TC resolutions";
# docs/03 §5.4): posixtest and posixtest-cxx built with aarch64-onyx-elf (native TLS, newlib's
# errno per thread, real gthreads), run under qemu-user on the v75 calls and on the v74 fallbacks,
# then `posixtest cxx` (posixtest running posixtest-cxx as a child process).
#
#   sh tools/tests/posixsim/tc.sh             # every run; the exit status: the failed runs
#   PREFIX=<other>- sh tools/tests/posixsim/tc.sh
#
# Needs the toolchain (/opt/toolchains/aarch64-onyx-elf-14.2 or on the PATH: tools/toolchain) and
# qemu-aarch64-static. The ports with this toolchain: sh tools/tests/posixsim/ports.sh (after
# sh tools/ports/build-all.sh, which picks aarch64-onyx-elf when it is installed).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../../.." && pwd)
: "${PREFIX:=aarch64-onyx-elf-}"
export PREFIX
bad=0
step ()
{
	label=$1
	shift
	out=$("$@" 2>&1)
	st=$?
	echo "$out" | grep -E '^FAIL'
	sum=$(echo "$out" | grep -E '^posixtest(-cxx)?: [0-9]+ passed' | tail -n 1)
	if [ $st -eq 0 ] && [ -n "$sum" ]; then echo "PASS  $label: $sum"; else echo "FAIL  $label: ${sum:-no summary} (exit $st)"; bad=$((bad + 1)); fi
}
CXX_SRC=$ONYX/user/bin/posixtest-cxx.cpp
step "posixtest (v75)" sh "$HERE/run.sh"
step "posixtest (v74 fallbacks)" env POSIXSIM_LEVEL=74 sh "$HERE/run.sh"
step "posixtest-cxx (v75)" env PROG="$CXX_SRC" sh "$HERE/run.sh"
step "posixtest-cxx (v74 fallbacks)" env POSIXSIM_LEVEL=74 PROG="$CXX_SRC" sh "$HERE/run.sh"
# posixtest's cxx group: posixtest-cxx (installed by the run above) spawned from posixtest
step "posixtest cxx (the child)" sh "$HERE/run.sh" cxx
echo "tc.sh: $bad run(s) failed"
exit $bad
