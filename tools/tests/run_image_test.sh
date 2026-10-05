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
# (v83) the shared libraries: against the real test library (user/Libs/demo), when the cross toolchain is there
P=${PREFIX:-aarch64-none-elf-}
if command -v ${P}g++ >/dev/null 2>&1; then
	CF="-O2 -fPIC -fvisibility=hidden -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -mgeneral-regs-only -I$ROOT/user -I$ROOT/user/Kits -I$ROOT/user/Runtime -I$ROOT/user/Include -I$ROOT/user/Libs -I$ROOT/user/Emulators -I$ROOT/user/Ports -I$ROOT/kernel/include -w"
	${P}g++ $CF -c "$ROOT/user/Libs/demo/demolib.cpp" -o "$T/demolib.o"
	${P}g++ $CF -c "$ROOT/user/Runtime/librt.cpp" -o "$T/librt.o"
	${P}ld -shared -Bsymbolic -z text -z max-page-size=0x10000 --no-undefined --hash-style=sysv --build-id=none \
		-T "$ROOT/user/Runtime/lib.ld" --version-script "$ROOT/user/Runtime/lib.vers" -e onyx_lib_table -o "$T/demo.so" "$T/demolib.o" "$T/librt.o"
	export ONYX_DEMO_SO="$T/demo.so"
fi
"$T/imagetest"
