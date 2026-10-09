#
# Makefile -- Dynarmic for Onyx: the ARM11 JIT of the 3DS emulator (docs/3DS-EMULATOR-STUDY.md, phase T0).
# Dynarmic (0BSD) with fmt, mcl, oaknut and robin-map (MIT) is in third_party/dynarmic-<commit>, the headers of
# Boost it includes (BSL-1.0) in third_party/boost-1.86.0 -- both copied by tools/n3ds/vendor_dynarmic.py: the
# AArch64 back end and the A32 front end only. Built with the apps' toolchain, no cmake:
#   libdynarmic.a   Dynarmic, fmt and mcl (C++20, with exceptions and RTTI as it wants: it throws nothing in
#                   normal use; the program that links it may be built -fno-exceptions -fno-rtti as the apps are)
#   codemem.o       the code memory on Onyx (onyx/codemem.c: mmap = AppKit's kapi_code_alloc) -- linked by an
#                   Onyx program, not by the PC tests (tools/tests/run_n3ds_t0.sh has Linux's)
# Onyx's own pieces, in onyx/: sys/mman.h (newlib has none) and onyx_std_mutex.h (the bare-metal libstdc++ has
# no std::mutex), put before every source.
#   make -C user/Libs/dynarmic [-j12] [OUT=<folder>]
# A program that uses it compiles with $(DYNARMIC_INC) (see the end) and -std=c++20.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
AR      = $(PREFIX)ar

ROOT    = ../../..
DYN     = $(ROOT)/third_party/dynarmic-a466015
BOOST   = $(ROOT)/third_party/boost-1.86.0
OUT    ?= obj

DYNARMIC_INC = -I$(DYN)/src -I$(DYN)/externals/mcl/include -I$(DYN)/externals/fmt/include -I$(DYN)/externals/oaknut/include \
	       -I$(DYN)/externals/robin-map/include -isystem $(BOOST) -Ionyx -include onyx/onyx_std_mutex.h
ARCH    = -mcpu=cortex-a72 -mno-outline-atomics -fno-pic -fno-pie -fno-stack-protector -ffunction-sections -fdata-sections
CXXFLAGS = $(ARCH) -O3 -DNDEBUG -std=c++20 -DDYNARMIC_ENABLE_CPU_FEATURE_DETECTION=1 -DFMT_USE_USER_DEFINED_LITERALS=1 -DFMT_OS=0 \
	   -Wno-array-bounds -Wno-stringop-overread $(DYNARMIC_INC)

SRCS := $(shell find $(DYN)/src $(DYN)/externals -name '*.cpp' -o -name '*.cc')
OBJS := $(patsubst $(DYN)/%,$(OUT)/%.o,$(SRCS))

all: $(OUT)/libdynarmic.a $(OUT)/codemem.o

$(OUT)/libdynarmic.a: $(OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/%.o: $(DYN)/% onyx/onyx_std_mutex.h onyx/sys/mman.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/codemem.o: onyx/codemem.c onyx/sys/mman.h
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -O2 -Ionyx -I$(ROOT)/user/Kits -I$(ROOT)/kernel/include -c $< -o $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
