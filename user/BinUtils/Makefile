#
# Makefile -- /bin console tools. Same EL1 app model as the GUI apps but they read
# stdin / write stdout / exit (no window). Built with the shared crt0.S + user.ld +
# appkit/appkit.h from the parent dir; staged to SD:/bin/<name> (no extension on the card).
#
# (the first rule below is not `all`: prerequisite-only rules come before it)
.DEFAULT_GOAL := all
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++

CFLAGS  = -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only \
	  -O2 -Wall -Wextra -fno-stack-protector -I.. -I../Kits -I../Runtime -I../Include -I../Libs -I../Emulators -I../Ports -I../../kernel/include
# Freestanding C++ console tools (e.g. httpget on the reusable HttpClient, ../Kits/netkit/http.hpp).
# Same integer-only base as CFLAGS; no exceptions/RTTI/STL (none needed).
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
# Floating-point variant: WITHOUT -mgeneral-regs-only so GCC may emit hardware
# FP/SIMD (-mcpu=cortex-a72 = the Pi 4 core). Safe in a userland app -- the kernel
# saves/restores the full FP/SIMD state on every trap (kern/trapframe.h) and FP is
# enabled at EL0 (CPACR_EL1). Opt a tool in via a target-specific CFLAGS override
# below; everything else stays integer-only.
CFLAGS_FP = -ffreestanding -nostdlib -fno-pic -fno-pie -mcpu=cortex-a72 \
	  -O2 -Wall -Wextra -fno-stack-protector -I.. -I../Kits -I../Runtime -I../Include -I../Libs -I../Emulators -I../Ports -I../../kernel/include
# newlib (libc + libm) variant: NOT freestanding/-nostdlib -- we want the real
# <stdio.h>/<stdlib.h>/<string.h>/<math.h> from the toolchain's newlib. -nostartfiles
# keeps OUR entry (crt0libc.S, which calls exit() so stdio is flushed). FP is on. The
# kapi syscall layer (../Runtime/libc/onyx_syscalls.c) maps newlib's _sbrk/_read/_write/... to
# kapi; malloc owns the per-process heap, so do NOT also link umm.h here.
CFLAGS_LIBC = -mcpu=cortex-a72 -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie \
	  -nostartfiles -I.. -I../Kits -I../Runtime -I../Include -I../Libs -I../Emulators -I../Ports -I../../kernel/include
# C++ newlib variant (for TLS apps: mbedTLS is C, the app + http.hpp are C++).
CXXFLAGS_LIBC = $(CFLAGS_LIBC) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
LIBC_SRC  = ../Runtime/libc/crt0libc.S ../Runtime/libc/onyx_syscalls.c
LIBC_DEP  = ../Runtime/libc/crt0libc.S ../Runtime/libc/onyx_syscalls.c ../Kits/appkit/appkit.h ../Runtime/user.ld
# (every program reaches the kernel through AppKit: its import stubs, ../Makefile)
LDFLAGS = -Wl,-T,../Runtime/user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none ../lib/appkit_stubs.o
# Freestanding tools (no libc): alias memset/memcpy/memmove onto the kapi ones (appkit/appkit.h,
# ABI v36). NOT used by the newlib programs, which keep newlib's own.
KAPI_ALIASES = -Wl,--defsym,memset=kapi_memset -Wl,--defsym,memcpy=kapi_memcpy \
	       -Wl,--defsym,memmove=kapi_memmove

# HTTPS apps need a cross-built mbedTLS. It is now vendored in the repo at
# third_party/mbedtls-3.6.3, so they build by default. Pass MBEDTLS_DIR= (empty) to skip
# them, or MBEDTLS_DIR=/path to build against a copy elsewhere.
MBEDTLS_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../third_party/mbedtls-3.6.3)
TLS_PROGS =
ifneq ($(strip $(MBEDTLS_DIR)),)
TLS_PROGS = httpsget.elf ftpfs.elf groq.elf llm.elf
PKG_PROGS = pkg.elf
# (zip, unzip: their rule is at the end; named HERE, before `all`, or `all` does not build them -- they
#  stayed as built on 2026-10-03, reading the kernel's table themselves, until this was seen on 2026-10-05)
ARC_PROGS = zip.elf unzip.elf
endif

# (imgtest, the old self-test of libpng / libjpeg built apart, was removed on 2026-10-05: ImageKit is the
#  codec, iktest its test)
IMG_PROGS =

# vncd (remote desktop) links the vendored zlib (third_party/zlib-1.3.1, prebuilt
# libz.a). Pass ZLIB_DIR= (empty) to skip it, or ZLIB_DIR=/path for another copy.
ZLIB_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../third_party/zlib-1.3.1)
ZLIB_PROGS =
ifneq ($(strip $(ZLIB_DIR)),)
ZLIB_PROGS = vncd.elf
endif

PROGS   = ls.elf cat.elf echo.elf grep.elf wc.elf page.elf \
	  rm.elf mkdir.elf touch.elf cp.elf mv.elf ps.elf kill.elf \
	  run.elf keyb.elf cmd.elf kmsg.elf netlog.elf verbose.elf net.elf wget.elf heaptest.elf \
	  fptest.elf init.elf telnetd.elf notifytest.elf \
	  ping.elf nslookup.elf netstat.elf whois.elf ftpd.elf ftp.elf wifiscan.elf tone.elf \
	  coretest.elf fsbench.elf shutdown.elf reboot.elf volume.elf \
	  threadtest.elf futextest.elf miditest.elf ringtest.elf ramtest.elf df.elf faulttest.elf el0test.elf sysstat.elf wstest.elf \
	  filetest.elf proctest.elf memtest.elf uname.elf

# kapi v93 the volumes: USB sticks mounted, ejected, formatted (volutil.h: what they share with df)
PROGS  += mount.elf eject.elf mkfs.elf
df.elf mount.elf eject.elf mkfs.elf: volutil.h

# The text tools (tool.h: argv, a buffered stdout, files and stdin by lines; regex.h and subst.h:
# the regular expressions of grep, sed and ed). Tested on the PC: tools/tests/run_tools_test.sh.
TEXT_PROGS = ls.elf cat.elf grep.elf wc.elf head.elf tail.elf sed.elf ed.elf sort.elf uniq.elf cut.elf tr.elf \
	     tee.elf nl.elf find.elf date.elf sleep.elf hexdump.elf diff.elf
PROGS  += $(filter-out ls.elf cat.elf grep.elf wc.elf,$(TEXT_PROGS))
$(TEXT_PROGS): tool.h regex.h subst.h
cmd.elf: cmdparse.h cmdscript.h
telnetd.elf: ../Include/lineedit.h shellend.h

# kapi v75 BSD sockets and poll (docs/POSIX-PLAN.md WP-NET)
PROGS  += nettest.elf

# kapi v77 program images (docs/02 section 7): programs loaded ahead and kept / released
PROGS  += preload.elf unload.elf
preload.elf unload.elf: imgname.h
preload.elf: ../Kits/systemkit/preloadini.h

# Console tools linked against newlib (real libc/libm). Add a tool here, not to the
# integer-only PROGS pattern rule above.
LIBC_PROGS = libctest.elf rdpd.elf

# Freestanding C++ console tools (built from <name>.cpp).
CXX_PROGS = httpget.elf

# kapi v83 shared libraries (docs/SHARED-LIBS-PLAN.md): the loader's tests, against ../lib/demo.so
CXX_PROGS += libtest.elf
libtest.elf: ../Runtime/lib.h ../Libs/demo/demo.h ../Runtime/onyxpp.hpp ../Runtime/umm.h
# kapi v97 a library under a second name (lib_open_as), the server per mode, its switch: the Pi's test
# (docs/POCKETUI-TECH-STUDY.md section 3.9) -- also a stand-in graphics server (copied to SD:/bin/pocketui)
CXX_PROGS += aliastest.elf
aliastest.elf: ../Runtime/lib.h ../Libs/demo/demo.h ../Runtime/onyxpp.hpp ../Runtime/umm.h

# play: a sound file from the command line, through AudioKit (../lib/audiokit.so). Built with the FPU
# off (it only uses the library's integer entries); links the library's import archive.
# tone: AudioKit's voices (the FM synthesizer: ak_fm_*)
tone.elf: tone.cpp ../Runtime/crt0.S ../Runtime/user.ld ../Kits/appkit/appkit.h ../Runtime/onyxpp.hpp ../Kits/audiokit/audiokit.h ../lib/audiokit.imp.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(KAPI_ALIASES) ../Runtime/crt0.S $< ../lib/audiokit.imp.a -o $@
play.elf: play.cpp ../Runtime/crt0.S ../Runtime/user.ld ../Kits/appkit/appkit.h ../Kits/audiokit/audiokit.h ../Runtime/onyxpp.hpp ../lib/audiokit.imp.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(KAPI_ALIASES) ../Runtime/crt0.S $< ../lib/audiokit.imp.a -o $@
AK_PROGS = play.elf
# fktest: FileKit's self-test (../lib/filekit.so): compression, ZIP on the card and in memory, trees, paths
fktest.elf: fktest.cpp ../Runtime/crt0.S ../Runtime/user.ld ../Kits/appkit/appkit.h ../Runtime/onyxpp.hpp ../Kits/filekit/filekit.h ../lib/filekit.imp.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(KAPI_ALIASES) ../Runtime/crt0.S $< ../lib/filekit.imp.a -o $@
AK_PROGS += fktest.elf
# iktest: ImageKit's self-test (../lib/imagekit.so): pictures written and read back, the transforms, the adjustments
iktest.elf: iktest.cpp ../Runtime/crt0.S ../Runtime/user.ld ../Kits/appkit/appkit.h ../Runtime/onyxpp.hpp ../Kits/imagekit/imagekit.h ../lib/imagekit.imp.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(KAPI_ALIASES) ../Runtime/crt0.S $< ../lib/imagekit.imp.a -o $@
AK_PROGS += iktest.elf
# ipp: a network printer asked what it can do (../Kits/printerkit/ipp.h; docs/03 "Printing")
CXX_PROGS += ipp.elf
ipp.elf: ../Kits/printerkit/ipp.h ../Kits/printerkit/ippnet.h ../Kits/imagekit/img/pngsave.hpp ../Runtime/onyxpp.hpp ../Runtime/umm.h

# Console tools on libonyxposix, the POSIX layer (../Runtime/libc/posix, docs/03 Â§5.4): newlib + pthreads,
# BSD sockets, poll, mmap, real files (no whole-file buffering). Its crt0posix, link script and
# library come from ../Runtime/libc/posix/build/onyx.specs (made first: the posixlib target).
POSIX_DIR    = ../Runtime/libc/posix
CFLAGS_POSIX = -mcpu=cortex-a72 -O2 -Wall -Wextra -fno-stack-protector -fno-pic -fno-pie \
	       -ffunction-sections -fdata-sections -isystem $(POSIX_DIR)/include -DFD_SETSIZE=1024 \
	       -I.. -I../Kits -I../Runtime -I../Include -I../Libs -I../Emulators -I../Ports -I../../kernel/include
LDFLAGS_POSIX = -specs=$(POSIX_DIR)/build/onyx.specs
POSIX_PROGS  = posixtest.elf ipctest.elf tcpbench.elf
POSIX_CXX_PROGS = posixtest-cxx.elf
# C on libonyxposix with WP-TC's toolchain: malloctest checks that toolchain's newlib (Web's malloc)
POSIX_TC_PROGS = malloctest.elf

# C++ console tools on the V3D QPU builder (../Libs/v3d/libv3d.a: shaders generated at run time),
# with FP on.
V3D_PROGS = v3dprog.elf

# C tools on the GPU compositing service (../Libs/gpucomp/libgpucomp.a), with FP on.
GPC_PROGS = gpcdemo.elf

all: $(PROGS) $(LIBC_PROGS) $(ARC_PROGS) $(CXX_PROGS) $(V3D_PROGS) $(GPC_PROGS) $(TLS_PROGS) $(IMG_PROGS) $(ZLIB_PROGS) $(PKG_PROGS) $(POSIX_PROGS) $(POSIX_CXX_PROGS) $(POSIX_TC_PROGS) $(PORT_PROGS) $(AK_PROGS)

# fptest exercises hardware float across context switches -> build it with FP on.
fptest.elf: CFLAGS := $(CFLAGS_FP)

# The tools that call a kit: its import archive on their link line. A C tool takes the C one
# (lib/<kit>.imp_c.a: tools/libgen --bind-c -- the library opened before main, as in a C++ program).
volume.elf preload.elf notifytest.elf: KITLIBS = ../lib/systemkit.imp_c.a
volume.elf preload.elf notifytest.elf: ../lib/systemkit.imp_c.a
ftp.elf wget.elf: KITLIBS = ../lib/netkit.imp_c.a
ftp.elf wget.elf: ../lib/netkit.imp_c.a
ftpfs.elf: KITLIBS = ../lib/netkit.imp.a
ftpfs.elf: ../lib/netkit.imp.a
%.elf: %.c ../Runtime/crt0.S ../Runtime/user.ld ../Kits/appkit/appkit.h ../Runtime/umm.h
	$(CC) $(CFLAGS) $(LDFLAGS) $(KAPI_ALIASES) ../Runtime/crt0.S $< $(KITLIBS) -o $@

# newlib apps: crt0libc + the kapi syscall layer + the app, linked with -lm (libc is
# pulled automatically since this is not -nostdlib).
$(LIBC_PROGS): %.elf: %.c $(LIBC_DEP)
	$(CC) $(CFLAGS_LIBC) $(LDFLAGS) $(LIBC_SRC) $< -lm -o $@

# libonyxposix programs: the library is built (or brought up to date) first.
.PHONY: posixlib
posixlib:
	@$(MAKE) --no-print-directory -C $(POSIX_DIR)
$(POSIX_PROGS): %.elf: %.c posixlib ../Kits/appkit/appkit.h
	$(CC) $(CFLAGS_POSIX) $(LDFLAGS_POSIX) $< -o $@

# C++ on libonyxposix: WP-TC's aarch64-onyx-elf toolchain (real gthreads, native TLS, errno per
# thread: docs/03 Â§1, tools/toolchain). Built when that toolchain is installed (on the PATH or in
# /opt/toolchains/aarch64-onyx-elf-14.2), skipped with a note otherwise; its libonyxposix is
# ../Runtime/libc/posix/build-onyx. posixtest-cxx: the C++ conformance test (`posixtest cxx` runs it).
ONYX_TC_PREFIX ?= aarch64-onyx-elf-
ONYX_TC_GXX := $(firstword $(shell command -v $(ONYX_TC_PREFIX)g++ 2>/dev/null) \
		$(wildcard /opt/toolchains/aarch64-onyx-elf-14.2/bin/$(ONYX_TC_PREFIX)g++))
CXXFLAGS_POSIX_TC = -std=gnu++20 -mcpu=cortex-a72 -O2 -Wall -Wextra -fno-pic -fno-pie \
	       -ffunction-sections -fdata-sections -isystem $(POSIX_DIR)/include -DFD_SETSIZE=1024 \
	       -I.. -I../Kits -I../Runtime -I../Include -I../Libs -I../Emulators -I../Ports -I../../kernel/include
LDFLAGS_POSIX_TC = -specs=$(POSIX_DIR)/build-onyx/onyx.specs
ifneq ($(ONYX_TC_GXX),)
.PHONY: posixlib-onyx
posixlib-onyx:
	@$(MAKE) --no-print-directory -C $(POSIX_DIR) PREFIX=$(ONYX_TC_PREFIX)
$(POSIX_CXX_PROGS): %.elf: %.cpp posixlib-onyx
	$(ONYX_TC_GXX) $(CXXFLAGS_POSIX_TC) $(LDFLAGS_POSIX_TC) $< -o $@
$(POSIX_TC_PROGS): %.elf: %.c posixlib-onyx
	$(dir $(ONYX_TC_GXX))$(ONYX_TC_PREFIX)gcc $(CFLAGS_POSIX) $(LDFLAGS_POSIX_TC) $< -o $@
else
$(POSIX_CXX_PROGS) $(POSIX_TC_PROGS):
	@echo "  SKIP $@: no $(ONYX_TC_PREFIX)g++ (WP-TC toolchain: sh tools/toolchain/fetch.sh, docs/03 Â§1)"
endif

# The ports of the POSIX layer (third_party, built with CMake / the Onyx toolchain file into
# out/sysroot(-onyx) and out/ports(-onyx), tools/ports/build-all.sh): the C smoke ports (SQLite,
# libxml2, curl + mbedTLS) and WebKit's graphics / text / i18n libraries (ICU, FreeType, HarfBuzz,
# libpng, libjpeg-turbo, libwebp, Skia: aarch64-onyx-elf only). OPT-IN (about 15 minutes on 4 cores,
# Skia the most): `make ports`, or PORTS=1 with `all`. The tools: sqlite3, xmllint, curl, icutest,
# hbtest, skiatest, skiademo (docs/04).
PORTS ?=
PORT_TOOLS = sqlite3 xmllint curl icutest hbtest skiatest skiademo
# The toolchain: ONYX_TOOLCHAIN_PREFIX (make ports ONYX_TOOLCHAIN_PREFIX=aarch64-none-elf-), else
# aarch64-onyx-elf when installed, else aarch64-none-elf (tools/onyx-env.sh); the tools come from
# out/ports-onyx/bin or out/ports/bin.
.PHONY: ports
ports:
	PORTS_COPY_TO=$(CURDIR) $(if $(ONYX_TOOLCHAIN_PREFIX),ONYX_TOOLCHAIN_PREFIX=$(ONYX_TOOLCHAIN_PREFIX)) \
		sh ../../tools/ports/build-all.sh
ifneq ($(strip $(PORTS)),)
PORT_PROGS = ports
endif

# jsc: the shell of WebKit's JavaScriptCore (step 1 of the WebKit port, docs/08-WEBKIT-PORT.md).
# WebKit is not in this repository: tools/webkit/fetch.sh checks the pinned revision out beside it
# and applies tools/webkit/patches; build-jsc.sh builds it with aarch64-onyx-elf against the sysroot
# (ICU from `make ports`) and strips it into tools/pkg/extra/jsc/bin/jsc (the optional package jsc: not on the default card). OPT-IN: `make jsc` (about 8 minutes on
# 16 cores, 70 on 4). JSC_INTERP=cloop for the portable C++ interpreter instead of the LLInt (then
# without WebAssembly).
JSC_INTERP ?= llint
.PHONY: jsc
jsc:
	sh ../../tools/webkit/fetch.sh
	INTERP=$(JSC_INTERP) sh ../../tools/webkit/build-jsc.sh install

# Freestanding C++ tools: our crt0.S + the .cpp (uses kapi + ../Kits/netkit/http.hpp only).
$(CXX_PROGS): %.elf: %.cpp ../Runtime/crt0.S ../Runtime/user.ld ../Kits/appkit/appkit.h ../Kits/netkit/http.hpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(KAPI_ALIASES) ../Runtime/crt0.S $< -o $@

CXXFLAGS_FP = $(CFLAGS_FP) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
$(V3D_PROGS): %.elf: %.cpp ../Runtime/crt0.S ../Runtime/user.ld ../Kits/appkit/appkit.h ../Libs/v3d/libv3d.a ../Libs/v3d/qpu.h ../Libs/v3d/shaders.h ../Libs/v3d/gxtev.h ../Libs/v3d/gxtev_ref.h
	$(CXX) $(CXXFLAGS_FP) -I../../tools/qpu/mesa $(LDFLAGS) $(KAPI_ALIASES) ../Runtime/crt0.S $< ../Libs/v3d/libv3d.a -o $@

$(GPC_PROGS): %.elf: %.c ../Runtime/crt0.S ../Runtime/user.ld ../Kits/appkit/appkit.h ../Runtime/umm.h ../Libs/gpucomp/libgpucomp.a ../Libs/gpucomp/gpucomp.h
	$(CC) $(CFLAGS_FP) $(LDFLAGS) $(KAPI_ALIASES) ../Runtime/crt0.S $< ../Libs/gpucomp/libgpucomp.a -o $@

# HTTPS apps (newlib C++ + mbedTLS). Built only when MBEDTLS_DIR is set. The C startup
# + syscalls are compiled as C objects (gcc), then linked with g++ alongside the libs.
# (Objects named after the program: a parallel make (-j) builds several at once.)
ifneq ($(strip $(MBEDTLS_DIR)),)
$(TLS_PROGS): %.elf: %.cpp $(LIBC_DEP) ../Kits/netkit/http.hpp ../Libs/tls/onyx_tls.hpp
	$(CC)  $(CFLAGS_LIBC) -c ../Runtime/libc/crt0libc.S -o $*.crt0.o
	$(CC)  $(CFLAGS_LIBC) -c ../Runtime/libc/onyx_syscalls.c -o $*.sys.o
	$(CXX) $(CXXFLAGS_LIBC) -I$(MBEDTLS_DIR)/include $(LDFLAGS) $*.crt0.o $*.sys.o $< $(KITLIBS) \
	  -L$(MBEDTLS_DIR)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@
	@rm -f $*.crt0.o $*.sys.o
# pkg (the packages, docs/pkg/README.md): HTTPS + the Archiver's ZIP engine + zlib
$(PKG_PROGS): %.elf: %.cpp arccli.h ../Libs/pkg/pkglib.h $(wildcard ../Kits/filekit/*.h) $(LIBC_DEP) ../Kits/netkit/http.hpp ../Libs/tls/onyx_tls.hpp
	$(CC)  $(CFLAGS_LIBC) -c ../Runtime/libc/crt0libc.S -o $*.crt0.o
	$(CC)  $(CFLAGS_LIBC) -c ../Runtime/libc/onyx_syscalls.c -o $*.sys.o
	$(CXX) $(CXXFLAGS_LIBC) -DPKG_NET -DONYX_HTTP_TLS -I$(MBEDTLS_DIR)/include -I$(ZLIB_DIR) -Wno-maybe-uninitialized -Wno-format-truncation \
	       -ffunction-sections -fdata-sections -Wl,--gc-sections $(LDFLAGS) $*.crt0.o $*.sys.o $< \
	       -L$(MBEDTLS_DIR)/library -lmbedtls -lmbedx509 -lmbedcrypto -L$(ZLIB_DIR) -lz -lm -o $@
	@rm -f $*.crt0.o $*.sys.o
# llm (the AI helper: Gemini / Groq / Mistral / Claude / OpenAI-compatible) also reads ../Include/json.hpp
llm.elf: ../Include/json.hpp
endif


# zlib apps (newlib C + libz). Built by default against the vendored zlib.
ifneq ($(strip $(ZLIB_DIR)),)
$(ZLIB_PROGS): %.elf: %.c $(LIBC_DEP)
	$(CC) $(CFLAGS_LIBC) -I$(ZLIB_DIR) $(LDFLAGS) $(LIBC_SRC) $< -L$(ZLIB_DIR) -lz -lm -o $@
endif


clean:
	rm -f *.elf *.o

# the remote desktops share the keyboard translation
vncd.elf rdpd.elf: remotekeys.h

# zip / unzip (C++ on newlib, FileKit's archive engine compiled in: ../Kits/filekit) with the vendored zlib
$(ARC_PROGS): %.elf: %.cpp arccli.h $(wildcard ../Kits/filekit/*.h) $(LIBC_DEP)
	$(CC)  $(CFLAGS_LIBC) -c ../Runtime/libc/crt0libc.S -o $*.crt0.o
	$(CC)  $(CFLAGS_LIBC) -c ../Runtime/libc/onyx_syscalls.c -o $*.sys.o
	$(CXX) $(CXXFLAGS_LIBC) -I$(ZLIB_DIR) -Wno-maybe-uninitialized -ffunction-sections -fdata-sections -Wl,--gc-sections \
	       $(LDFLAGS) $*.crt0.o $*.sys.o $< -L$(ZLIB_DIR) -lz -lm -o $@
	@rm -f $*.crt0.o $*.sys.o

# (the headers of one program, beside it)
wget.elf: ../Kits/netkit/httpc.h ../Kits/netkit/httpc.inc
sysstat.elf: kapi_names.h
