#
# Makefile -- /bin console tools. Same EL1 app model as the GUI apps but they read
# stdin / write stdout / exit (no window). Built with the shared crt0.S + user.ld +
# kapi.h from the parent dir; staged to SD:/bin/<name>.elf.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++

CFLAGS  = -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only \
	  -O2 -Wall -Wextra -fno-stack-protector -I.. -I../../kernel/include
# Freestanding C++ console tools (e.g. httpget on the reusable HttpClient, ../http.hpp).
# Same integer-only base as CFLAGS; no exceptions/RTTI/STL (none needed).
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
# Floating-point variant: WITHOUT -mgeneral-regs-only so GCC may emit hardware
# FP/SIMD (-mcpu=cortex-a72 = the Pi 4 core). Safe in a userland app -- the kernel
# saves/restores the full FP/SIMD state on every trap (kern/trapframe.h) and FP is
# enabled at EL0 (CPACR_EL1). Opt a tool in via a target-specific CFLAGS override
# below; everything else stays integer-only.
CFLAGS_FP = -ffreestanding -nostdlib -fno-pic -fno-pie -mcpu=cortex-a72 \
	  -O2 -Wall -Wextra -fno-stack-protector -I.. -I../../kernel/include
# newlib (libc + libm) variant: NOT freestanding/-nostdlib -- we want the real
# <stdio.h>/<stdlib.h>/<string.h>/<math.h> from the toolchain's newlib. -nostartfiles
# keeps OUR entry (crt0libc.S, which calls exit() so stdio is flushed). FP is on. The
# kapi syscall layer (../libc/onyx_syscalls.c) maps newlib's _sbrk/_read/_write/... to
# kapi; malloc owns the per-process heap, so do NOT also link umm.h here.
CFLAGS_LIBC = -mcpu=cortex-a72 -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie \
	  -nostartfiles -I.. -I../../kernel/include
# C++ newlib variant (for TLS apps: mbedTLS is C, the app + http.hpp are C++).
CXXFLAGS_LIBC = $(CFLAGS_LIBC) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
LIBC_SRC  = ../libc/crt0libc.S ../libc/onyx_syscalls.c
LIBC_DEP  = ../libc/crt0libc.S ../libc/onyx_syscalls.c ../kapi.h ../user.ld
LDFLAGS = -Wl,-T,../user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none

# HTTPS apps need a cross-built mbedTLS. It is now vendored in the repo at
# third_party/mbedtls-3.6.3, so they build by default. Pass MBEDTLS_DIR= (empty) to skip
# them, or MBEDTLS_DIR=/path to build against a copy elsewhere.
MBEDTLS_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../third_party/mbedtls-3.6.3)
TLS_PROGS =
ifneq ($(strip $(MBEDTLS_DIR)),)
TLS_PROGS = httpsget.elf
endif

# Image apps need the cross-built codecs (zlib/libpng/libjpeg, see ../img/README.md).
# OPT-IN: pass IMG_DIR to enable them, e.g.  make IMG_DIR=../img  (after: make -C ../img)
IMG_DIR ?=
IMG_PROGS =
ifneq ($(strip $(IMG_DIR)),)
IMG_PROGS = imgtest.elf
endif

# libnsfb demo needs the cross-built libnsfb (see ../nsfb/README.md). OPT-IN via NSFB_DIR:
#   make NSFB_DIR=../nsfb  (after: make -C ../nsfb)
NSFB_DIR ?=
NSFB_PROGS =
ifneq ($(strip $(NSFB_DIR)),)
NSFB_PROGS = nsfbdemo.elf
endif

PROGS   = ls.elf cat.elf echo.elf grep.elf wc.elf page.elf \
	  rm.elf mkdir.elf touch.elf cp.elf mv.elf ps.elf kill.elf \
	  run.elf keyb.elf cmd.elf kmsg.elf verbose.elf net.elf wget.elf heaptest.elf \
	  fptest.elf init.elf

# Console tools linked against newlib (real libc/libm). Add a tool here, not to the
# integer-only PROGS pattern rule above.
LIBC_PROGS = libctest.elf

# Freestanding C++ console tools (built from <name>.cpp).
CXX_PROGS = httpget.elf

all: $(PROGS) $(LIBC_PROGS) $(CXX_PROGS) $(TLS_PROGS) $(IMG_PROGS) $(NSFB_PROGS)

# fptest exercises hardware float across context switches -> build it with FP on.
fptest.elf: CFLAGS := $(CFLAGS_FP)

%.elf: %.c ../crt0.S ../user.ld ../kapi.h ../applib.h ../httpc.h ../umm.h
	$(CC) $(CFLAGS) $(LDFLAGS) ../crt0.S $< -o $@

# newlib apps: crt0libc + the kapi syscall layer + the app, linked with -lm (libc is
# pulled automatically since this is not -nostdlib).
$(LIBC_PROGS): %.elf: %.c $(LIBC_DEP)
	$(CC) $(CFLAGS_LIBC) $(LDFLAGS) $(LIBC_SRC) $< -lm -o $@

# Freestanding C++ tools: our crt0.S + the .cpp (uses kapi + ../http.hpp only).
$(CXX_PROGS): %.elf: %.cpp ../crt0.S ../user.ld ../kapi.h ../http.hpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) ../crt0.S $< -o $@

# HTTPS apps (newlib C++ + mbedTLS). Built only when MBEDTLS_DIR is set. The C startup
# + syscalls are compiled as C objects (gcc), then linked with g++ alongside the libs.
ifneq ($(strip $(MBEDTLS_DIR)),)
$(TLS_PROGS): %.elf: %.cpp $(LIBC_DEP) ../http.hpp ../tls/onyx_tls.hpp
	$(CC)  $(CFLAGS_LIBC) -c ../libc/crt0libc.S -o crt0libc.o
	$(CC)  $(CFLAGS_LIBC) -c ../libc/onyx_syscalls.c -o onyx_syscalls.o
	$(CXX) $(CXXFLAGS_LIBC) -I$(MBEDTLS_DIR)/include $(LDFLAGS) crt0libc.o onyx_syscalls.o $< \
	  -L$(MBEDTLS_DIR)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@
	@rm -f crt0libc.o onyx_syscalls.o
endif

# Image apps (newlib C++ + the codecs). Built only when IMG_DIR is set. Same crt0 split
# as the TLS apps: C startup + syscalls as C objects (gcc), then linked with g++.
ifneq ($(strip $(IMG_DIR)),)
$(IMG_PROGS): %.elf: %.cpp $(LIBC_DEP) ../img/image.hpp
	$(CC)  $(CFLAGS_LIBC) -c ../libc/crt0libc.S -o crt0libc.o
	$(CC)  $(CFLAGS_LIBC) -c ../libc/onyx_syscalls.c -o onyx_syscalls.o
	$(CXX) $(CXXFLAGS_LIBC) -I$(IMG_DIR)/libpng -I$(IMG_DIR)/zlib -I$(IMG_DIR)/libjpeg \
	  $(LDFLAGS) crt0libc.o onyx_syscalls.o $< \
	  -L$(IMG_DIR)/libpng -L$(IMG_DIR)/zlib -L$(IMG_DIR)/libjpeg -lpng -ljpeg -lz -lm -o $@
	@rm -f crt0libc.o onyx_syscalls.o
endif

# libnsfb demo (newlib C + libnsfb). Built only when NSFB_DIR is set. The Onyx surface
# registers via a constructor, so libnsfb.a is pulled with --whole-archive (otherwise the
# linker would drop the un-referenced surface object and "onyx" would never register).
ifneq ($(strip $(NSFB_DIR)),)
$(NSFB_PROGS): %.elf: %.c $(LIBC_DEP) ../nsfb/onyx_surface.c
	$(CC) $(CFLAGS_LIBC) -I$(NSFB_DIR)/libnsfb/include $(LDFLAGS) $(LIBC_SRC) $< \
	  -Wl,--whole-archive -L$(NSFB_DIR)/libnsfb -lnsfb -Wl,--no-whole-archive -lm -o $@
endif

clean:
	rm -f *.elf *.o
