#!/bin/sh
# run_cmd_test.sh -- the shell's command-line parser (user/bin/cmdparse.h, used by /bin/cmd) on the
# PC: words, "..." and '...' quotes, backslash escapes, pipes and redirections, the syntax errors,
# a 2 KB line, the command lists (; && ||, comments) and the script variables ($1, $#, $?...); and
# the consoles' line editor (user/lineedit.h: the cursor, the history). docs/04 *Terminal & shell*.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-${TMPDIR:-/tmp}/onyx_cmdtest}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
	"$ROOT/tools/tests/cmd/cmdparse_test.c" -o "$OUT/cmdparse_test"
gcc -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
	"$ROOT/tools/tests/cmd/lineedit_test.c" -o "$OUT/lineedit_test"
"$OUT/cmdparse_test" && "$OUT/lineedit_test" && echo "all passed"
