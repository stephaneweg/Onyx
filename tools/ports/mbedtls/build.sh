#!/bin/sh
# build.sh -- mbedTLS for the POSIX sysroot (third_party/mbedtls-3.6.3, Apache-2.0): curl's TLS.
#
# A configuration of its own, made from the in-tree one (user/Libs/tls/Makefile's: no hardware AES /
# SHA -- the Pi 4's A72 has no crypto extensions --, TLS 1.2 + 1.3) with what POSIX now gives:
# MBEDTLS_FS_IO (CA bundle files), MBEDTLS_HAVE_TIME / HAVE_TIME_DATE (certificate dates), PSA's
# random from the entropy module, fed by getrandom (MBEDTLS_ENTROPY_HARDWARE_ALT,
# onyx_entropy.c). Built out of tree with CMake and tools/onyx-toolchain.cmake; the vendored tree
# and its prebuilt libraries (the newlib apps') are not touched. Installs the headers (with this
# configuration as include/mbedtls/mbedtls_config.h) and libmbed{tls,x509,crypto}.a in the sysroot.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/mbedtls-3.6.3
B=$PORTS_OUT/build/mbedtls
mkdir -p "$B"
CFG=$B/onyx_posix_mbedtls_config.h
cp "$SRC/include/mbedtls/mbedtls_config.h" "$CFG"
for s in MBEDTLS_FS_IO MBEDTLS_HAVE_TIME MBEDTLS_HAVE_TIME_DATE MBEDTLS_ENTROPY_HARDWARE_ALT \
	 MBEDTLS_NO_PLATFORM_ENTROPY MBEDTLS_SSL_PROTO_TLS1_3; do
	python3 "$SRC/scripts/config.py" -f "$CFG" set $s
done
for u in MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG MBEDTLS_NET_C MBEDTLS_TIMING_C MBEDTLS_PSA_ITS_FILE_C \
	 MBEDTLS_PSA_CRYPTO_STORAGE_C MBEDTLS_AESCE_C MBEDTLS_AESNI_C MBEDTLS_PADLOCK_C \
	 MBEDTLS_SHA256_USE_A64_CRYPTO_IF_PRESENT MBEDTLS_SHA512_USE_A64_CRYPTO_IF_PRESENT \
	 MBEDTLS_SHA256_USE_ARMV8_A_CRYPTO_IF_PRESENT MBEDTLS_SHA512_USE_ARMV8_A_CRYPTO_IF_PRESENT; do
	python3 "$SRC/scripts/config.py" -f "$CFG" unset $u
done

echo "mbedtls: configure (CMake, the Onyx toolchain file)"
onyx_cmake -S "$SRC" -B "$B/build" -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DGEN_FILES=OFF \
	-DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON -DMBEDTLS_FATAL_WARNINGS=OFF \
	-DMBEDTLS_CONFIG_FILE="$CFG" >"$B/configure.log" || { tail -30 "$B/configure.log"; exit 1; }
echo "mbedtls: build"
cmake --build "$B/build" -j "$JOBS" >"$B/build.log" || { grep -B2 -A8 "error" "$B/build.log" | head -60; exit 1; }

$CC $CFLAGS -c "$(dirname "$0")/onyx_entropy.c" -o "$B/onyx_entropy.o"
$AR rs "$B/build/library/libmbedcrypto.a" "$B/onyx_entropy.o"

inc=$ONYX_SYSROOT/include
rm -rf "$inc/mbedtls" "$inc/psa"
cp -r "$SRC/include/mbedtls" "$SRC/include/psa" "$inc/"
cp "$CFG" "$inc/mbedtls/mbedtls_config.h"
cp "$B/build/library/libmbedtls.a" "$B/build/library/libmbedx509.a" "$B/build/library/libmbedcrypto.a" "$ONYX_SYSROOT/lib/"
for l in mbedcrypto mbedx509 mbedtls; do
	req=""
	[ $l = mbedx509 ] && req="Requires.private: mbedcrypto"
	[ $l = mbedtls ] && req="Requires.private: mbedx509"
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: %s\nDescription: Mbed TLS (Onyx POSIX build)\nVersion: 3.6.3\n%s\nLibs: -L${libdir} -l%s\nCflags: -I${includedir}\n' \
		"$ONYX_SYSROOT" "$l" "$req" "$l" > "$ONYX_SYSROOT/lib/pkgconfig/$l.pc"
done
echo "mbedtls: libmbedtls.a libmbedx509.a libmbedcrypto.a installed"
ls -la "$ONYX_SYSROOT/lib/"libmbed*.a
