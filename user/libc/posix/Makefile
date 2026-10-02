#
# Makefile -- libonyxposix, the POSIX layer of Onyx (docs/POSIX-PLAN.md §3.4; docs/03 §5.4).
#
#   make                          build/libonyxposix.a, build/crt0posix.o, build/onyx.specs (for
#                                 the in-tree programs: user/bin's posixtest and the ports' tools)
#   make install SYSROOT=<dir>    the sysroot third-party code builds against (default
#                                 ../../../out/sysroot, git-ignored): include/, lib/libonyxposix.a,
#                                 lib/crt0posix.o, lib/onyx-posix.ld, lib/onyx.specs, lib/pkgconfig/
#   make PREFIX=aarch64-onyx-elf-  the same for WP-TC's toolchain: build-onyx/, out/sysroot-onyx
#
# A program links libonyxposix OR ../onyx_syscalls.c (the existing newlib apps), never both:
#   aarch64-none-elf-gcc -isystem <sysroot>/include -specs=<sysroot>/lib/onyx.specs ...
# The two toolchains (docs/POSIX-PLAN.md §2; docs/03 §1), told apart by `$(CC) -dumpmachine`:
#  - aarch64-none-elf (the interim, phase 1): newlib + the headers shadowed through -isystem, the
#    emutls / C++ guard overrides (emutls.c, guard.c), errno per thread by tls.c's __errno;
#  - aarch64-onyx-elf (WP-TC, tools/toolchain): native TLS (PT_TLS, TPIDR_EL0), newlib's errno and
#    _reent per thread, real gthreads in libgcc / libstdc++: no emutls override (the sources key
#    on newlib's _WANT_REENT_THREAD_LOCAL, posix_internal.h). Its own build dir and sysroot, as
#    the objects of the two are not interchangeable.
# PREFIX defaults to $(ONYX_TOOLCHAIN_PREFIX), else aarch64-none-elf-.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see the sources).
#
ONYX_TOOLCHAIN_PREFIX ?= aarch64-none-elf-
PREFIX  ?= $(ONYX_TOOLCHAIN_PREFIX)
# the toolchain's bin, when not on the PATH
ifeq ($(shell command -v $(PREFIX)gcc 2>/dev/null),)
TC_BIN := $(firstword $(wildcard /opt/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin/$(PREFIX)gcc /opt/toolchains/aarch64-onyx-elf-14.2/bin/$(PREFIX)gcc))
TC_DIR  := $(dir $(TC_BIN))
endif
CC       = $(TC_DIR)$(PREFIX)gcc
AR       = $(TC_DIR)$(PREFIX)ar
HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TC_MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
ifeq ($(TC_MACHINE),aarch64-onyx-elf)
TC_NATIVE_TLS = 1
SYSROOT ?= $(abspath $(HERE)/../../../out/sysroot-onyx)
B       ?= build-onyx
TC_UNDEF = -u __cxa_guard_acquire -u __onyx_pthread_anchor -u abort
else
SYSROOT ?= $(abspath $(HERE)/../../../out/sysroot)
B       ?= build
TC_UNDEF = -u __emutls_get_address -u __cxa_guard_acquire -u __onyx_pthread_anchor -u abort
endif
EXTRA_CFLAGS ?=

# -D_GNU_SOURCE: the GNU names (CLOCK_MONOTONIC_RAW...) inside the library; -isystem include: the
# headers it implements first on the path (as for every program built on it).
CFLAGS   = -mcpu=cortex-a72 -O2 -g0 -Wall -Wextra -Wno-unused-parameter -fno-stack-protector \
	   -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-builtin-malloc \
	   -D_GNU_SOURCE -DFD_SETSIZE=1024 -isystem $(HERE)/include -I$(HERE) -I$(HERE)/../.. \
	   -I$(HERE)/../../../kernel/include $(EXTRA_CFLAGS)
# tls.c and start.c take the addresses of linker-script symbols (absolute values, weak
# references): the large code model reaches them anywhere (no ADRP range).
CFLAGS_ABS = -mcmodel=large

SRC  = start.c tls.c emutls.c guard.c pthread.c pthread_sync.c sem.c time.c env.c path.c fd.c ipc.c \
       file.c stat.c dir.c mman.c proc.c signal.c socket.c netdb.c poll.c misc.c libgen.c newlib_syscalls.c
OBJ  = $(SRC:%.c=$(B)/%.o)
HDRS = $(wildcard include/*.h include/*/*.h) posix_internal.h ../../kapi.h \
       ../../../kernel/include/kern/kapi_abi.h

all: $(B)/libonyxposix.a $(B)/crt0posix.o $(B)/onyx.specs $(B)/onyx-posix.ld

$(B)/%.o: %.c $(HDRS) Makefile
	@mkdir -p $(B)
	$(CC) $(CFLAGS) $(if $(filter tls.c start.c,$<),$(CFLAGS_ABS)) -c $< -o $@

$(B)/crt0posix.o: crt0posix.S
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/libonyxposix.a: $(OBJ)
	rm -f $@
	$(AR) rcs $@ $(OBJ)

$(B)/onyx-posix.ld: onyx-posix.ld
	@mkdir -p $(B)
	cp $< $@

# The specs: crt0posix.o and the library first (the library is also given before the program's
# objects, with -u for the overrides, so libgcc's emutls and libsupc++'s guards never win), the
# link script, then newlib and the rest after the program's own libraries. -u abort: signal.o
# (signal, raise, abort) taken before newlib's own (assert -> abort would otherwise pull newlib's
# abort.o and signal.o: duplicate definitions; under aarch64-none-elf emutls.o pulled it first).
define SPECS
%rename link onyx_orig_link
%rename lib onyx_orig_lib

*startfile:
$(1)/crt0posix.o $(1)/libonyxposix.a

*endfile:


*link:
-T $(1)/onyx-posix.ld -z max-page-size=0x10000 --build-id=none --gc-sections $(TC_UNDEF) %(onyx_orig_link)

*lib:
--start-group $(1)/libonyxposix.a -lc -lm --end-group %(onyx_orig_lib)

endef
export SPECS_BUILD = $(call SPECS,$(HERE)/$(B))
export SPECS_SYSROOT = $(call SPECS,$(SYSROOT)/lib)

$(B)/onyx.specs: Makefile
	@mkdir -p $(B)
	@printf '%s\n' "$$SPECS_BUILD" > $@

PC_LIBDIR = $(SYSROOT)/lib/pkgconfig

install: all
	mkdir -p $(SYSROOT)/include $(SYSROOT)/lib $(PC_LIBDIR)
	cp -r include/. $(SYSROOT)/include/
	cp $(B)/libonyxposix.a $(B)/crt0posix.o onyx-posix.ld $(SYSROOT)/lib/
	@printf '%s\n' "$$SPECS_SYSROOT" > $(SYSROOT)/lib/onyx.specs
	@printf 'prefix=%s\nlibdir=$${prefix}/lib\nincludedir=$${prefix}/include\n\nName: onyxposix\nDescription: The POSIX layer of Onyx (threads, files, sockets, mmap) over newlib\nVersion: 76\nLibs: -L$${libdir} -lonyxposix\nCflags: -isystem $${includedir} -DFD_SETSIZE=1024\n' "$(SYSROOT)" > $(PC_LIBDIR)/onyxposix.pc
	@echo "libonyxposix installed in $(SYSROOT)"

clean:
	rm -rf $(B)

.PHONY: all install clean
