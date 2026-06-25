#
# Makefile -- cross-build mbedTLS for Onyx (aarch64-none-elf + newlib) as a bare-metal
# TLS 1.2 client. Produces $(MBEDTLS)/library/{libmbedcrypto,libmbedx509,libmbedtls}.a,
# which the HTTPS apps link (see ../bin/Makefile, MBEDTLS_DIR). The Onyx-side glue is
# header-only in onyx_tls.hpp; this only builds the upstream library.
#
# Prereqs: the mbedTLS source under ./mbedtls (a git submodule pinned to v3.6.2 -- see
# README.md) and python3 (for scripts/config.py). Then:  make -C user/tls
#
PREFIX  ?= aarch64-none-elf-
# mbedTLS source + built libs are vendored in the repo at third_party/mbedtls-3.6.3.
MBEDTLS ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../third_party/mbedtls-3.6.3)
PY      ?= python3

# Same world as the newlib apps: hardware FP (-mcpu=cortex-a72), PIC/SSP off. NOT
# -nostdlib/-ffreestanding -- mbedTLS uses newlib (malloc/snprintf/memcpy).
CFLAGS_TLS = -mcpu=cortex-a72 -O2 -fno-pic -fno-pie -fno-stack-protector \
	     -ffunction-sections -fdata-sections -I$(abspath $(MBEDTLS))/include

# Strip the platform couplings from the upstream default config: no OS sockets / files
# / timing / platform entropy, and TLS 1.2 only (TLS 1.3 would pull the PSA RNG setup).
# Entropy is supplied at runtime by onyx_tls.hpp; see its security note.
#
# CRUCIAL for the Pi 4: disable the ARMv8 hardware-crypto paths (AESCE / SHA A64 crypto).
# The Pi 4's Cortex-A72 does NOT implement the ARMv8 Crypto Extensions, so an AES/SHA
# instruction there is UNDEFINED -> it traps and halts the kernel (looks like a freeze,
# e.g. inside mbedtls_ctr_drbg_seed's AES key schedule). Force pure software crypto.
CONFIG_UNSET = MBEDTLS_NET_C MBEDTLS_TIMING_C MBEDTLS_FS_IO \
	       MBEDTLS_PSA_ITS_FILE_C MBEDTLS_PSA_CRYPTO_STORAGE_C \
	       MBEDTLS_HAVE_TIME MBEDTLS_HAVE_TIME_DATE \
	       MBEDTLS_SSL_PROTO_TLS1_3 \
	       MBEDTLS_AESCE_C MBEDTLS_AESNI_C MBEDTLS_PADLOCK_C \
	       MBEDTLS_SHA256_USE_A64_CRYPTO_IF_PRESENT MBEDTLS_SHA256_USE_A64_CRYPTO_ONLY \
	       MBEDTLS_SHA512_USE_A64_CRYPTO_IF_PRESENT MBEDTLS_SHA512_USE_A64_CRYPTO_ONLY \
	       MBEDTLS_SHA256_USE_ARMV8_A_CRYPTO_IF_PRESENT MBEDTLS_SHA256_USE_ARMV8_A_CRYPTO_ONLY \
	       MBEDTLS_SHA512_USE_ARMV8_A_CRYPTO_IF_PRESENT MBEDTLS_SHA512_USE_ARMV8_A_CRYPTO_ONLY

.PHONY: all config libs clean distclean

all: libs

config:
	@test -d $(MBEDTLS)/library || { echo "mbedTLS source missing at $(MBEDTLS)/ -- see README.md"; exit 1; }
	@for u in $(CONFIG_UNSET); do $(PY) $(MBEDTLS)/scripts/config.py unset $$u; done
	$(PY) $(MBEDTLS)/scripts/config.py set MBEDTLS_NO_PLATFORM_ENTROPY

libs: config
	$(MAKE) -C $(MBEDTLS)/library CC=$(PREFIX)gcc AR=$(PREFIX)ar CFLAGS="$(CFLAGS_TLS)"
	@echo "TLS libs: $(MBEDTLS)/library/libmbed{crypto,x509,tls}.a"

clean:
	-$(MAKE) -C $(MBEDTLS)/library clean

distclean: clean
