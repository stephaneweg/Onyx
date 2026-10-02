#!/bin/sh
# build-taatu.sh -- build the support libs then the TAATU Onyx app (taatu.elf).
# Run after get-toolchain.sh and `export PATH=.../arm-gnu-toolchain-13.3/bin:$PATH`.
set -eu

cd "$(dirname "$0")/../.."           # repo root
ONYX="$PWD"
U="$ONYX/user"
PREFIX="${PREFIX:-aarch64-none-elf-}"

if ! command -v "${PREFIX}gcc" >/dev/null 2>&1; then
    echo "ERROR: ${PREFIX}gcc not on PATH. Run get-toolchain.sh and export its bin/ first." >&2
    exit 1
fi

echo "== support libraries =="
( cd "$U" && make wtk/libwtk.a ft/libft.a ft/fonts.h libc/crt0libc.o libc/onyx_syscalls.o )

if [ ! -f "$ONYX/third_party/mbedtls-3.6.3/library/libmbedtls.a" ]; then
    echo "== mbedTLS (not prebuilt) =="
    ( cd "$U" && make -C tls )
fi

echo "== taatu.elf =="
( cd "$U" && make taatu.elf )

echo "== stage out/taatu.app/ =="
OUT="$ONYX/third_party/toolchain-aarch64/out/taatu.app"
mkdir -p "$OUT"
cp "$U/taatu.elf" "$OUT/main"
cp "$ONYX/user/Apps/taatu/app.txt" "$OUT/app.txt"
[ -f "$ONYX/user/Apps/taatu/icon.bmp" ] && cp "$ONYX/user/Apps/taatu/icon.bmp" "$OUT/" || true
echo "Done -> $OUT (copy to SD:/apps/ on an Onyx card)."
