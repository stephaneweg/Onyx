#!/bin/sh
# get-toolchain.sh -- download + extract Arm GNU Toolchain 13.3.rel1 (aarch64-none-elf).
# Linux x86-64 host (cloud session / WSL / CI). Not committed to the repo (too large).
set -eu

VER=13.3.rel1
HOST=x86_64
TARGET=aarch64-none-elf
TARBALL="arm-gnu-toolchain-${VER}-${HOST}-${TARGET}.tar.xz"
URL="https://developer.arm.com/-/media/Files/downloads/gnu/${VER}/binrel/${TARBALL}"
DEST="arm-gnu-toolchain-13.3"

cd "$(dirname "$0")"

if [ -x "${DEST}/bin/${TARGET}-gcc" ]; then
    echo "Toolchain already present in ${DEST}/ -- nothing to do."
    "${DEST}/bin/${TARGET}-gcc" --version | head -1
    exit 0
fi

echo "Downloading ${TARBALL} ..."
curl -fL --retry 3 -o "${TARBALL}" "${URL}"

# Optional integrity check: put the expected hash in SHA256SUMS (one line:
# "<sha256>  <tarball>"). Without it we only warn -- Arm does not publish a stable
# checksum URL next to the file, so fill it once you trust a download.
if [ -f SHA256SUMS ]; then
    echo "Verifying checksum ..."
    sha256sum -c SHA256SUMS
else
    echo "WARNING: no SHA256SUMS file -- skipping integrity check."
    echo "         computed: $(sha256sum "${TARBALL}" | cut -d' ' -f1)"
fi

echo "Extracting ..."
rm -rf "${DEST}"
mkdir -p "${DEST}"
tar -xJf "${TARBALL}" --strip-components=1 -C "${DEST}"

echo "Done. Add to PATH:"
echo "  export PATH=\"$PWD/${DEST}/bin:\$PATH\""
"${DEST}/bin/${TARGET}-gcc" --version | head -1
