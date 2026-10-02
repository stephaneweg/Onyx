#!/bin/sh
# run_image_test.sh -- the kernel's program images of kapi v77 (kernel/proc/image.cpp: the streaming
# loader, the shared image keyed by the program's canonical path, preload / unload, the file layer's
# hook; kernel/proc/elf.cpp: the ELF header checks, LoadELF) on the PC, the kernel around them
# stubbed (tools/tests/image: address spaces as maps of pages, a file as a buffer, the kernel's
# cooperative tasks as threads that run one at a time). docs/02 section 7 "Program images".
# MIT licence (Onyx).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}/onyx_imagetest
rm -rf "$T" && mkdir -p "$T"
INC="-I$HERE/image/stub -I$HERE/ipc/stub -I$ROOT/kernel/include"
FLAGS="-std=gnu++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-parameter"
for f in image elf; do
	g++ $FLAGS $INC -c "$ROOT/kernel/proc/$f.cpp" -o "$T/$f.o"
done
g++ $FLAGS $INC "$HERE/image/imagetest.cpp" "$T/image.o" "$T/elf.o" -o "$T/imagetest" -lpthread
"$T/imagetest"
