#!/bin/sh
# run_telnetd_test.sh -- the end of a remote shell session (user/bin/telnetd.c, user/bin/shellend.h)
# on the PC: telnetd's own session code against a mock kapi (tools/tests/telnetd/: a clock, the
# kernel's pipes, a scripted client and a model of /bin/cmd). A client that closes or vanishes,
# at the prompt or while a program runs, must leave no shell behind; a silent client stays.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-${TMPDIR:-/tmp}/onyx_telnetdtest}
mkdir -p "$OUT"
# (telnetd.c as it is, but for its main and an AArch64 barrier)
sed -e 's|^int main (void)|static int telnetd_main (void)|' -e 's|__asm__ volatile ("dmb ish" ::: "memory");||' \
	"$ROOT/user/bin/telnetd.c" > "$OUT/telnetd.c"
gcc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-function -fno-builtin-log2 -fsanitize=address,undefined \
	-I"$ROOT/tools/tests/telnetd" -I"$OUT" -I"$ROOT/user/bin" -I"$ROOT/user" \
	"$ROOT/tools/tests/telnetd/telnetd_test.c" -o "$OUT/telnetd_test"
"$OUT/telnetd_test"
