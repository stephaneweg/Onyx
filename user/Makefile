#
# Makefile -- userland ELF programs (EL0), built with the same aarch64-none-elf
# toolchain, linked at USER_VA_BASE (user.ld). Integer-only (-mgeneral-regs-only):
# the kernel's preemptive trap frame does not save FP/SIMD yet.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++

# -I../kernel/include: apps include <kern/kapi_abi.h> (the shared ABI). Apps call
# the kernel through the ABI table at a fixed VA, so they DON'T link against kernel
# addresses and need not be rebuilt when the kernel changes.
CFLAGS  = -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only \
	  -O2 -Wall -Wextra -fno-stack-protector -I../kernel/include
# C++ apps: same base, freestanding C++ subset (no exceptions/RTTI/STL; function-
# local statics are single-threaded; static dtors are not run -- see onyxpp.hpp).
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
LDFLAGS = -Wl,-T,user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none

# All graphical apps live in Apps/<name>/main.{c,cpp} (one folder per app) and build to
# <name>.elf here at user/ (so `make stage` keeps globbing ../user/*.elf). The /bin
# console tools stay in bin/. APPS_CPP are on the C++ uikit toolkit; APPS_C are the
# app-drawn apps still in C (being ported to C++).
APPS_CPP = tinycalc wpaconf tinypad applist panel demoD demoE cppdemo
APPS_C   = demoA demoB demoC demoF inidemo \
	   tetris snake same filer terminal \
	   2048 life pong sokoban \
	   calendar mandelbrot minesweeper paint \
	   eyes sheet taskman voronoy theme config irc memmon

APP_ELFS = $(APPS_CPP:=.elf) $(APPS_C:=.elf)

all: $(APP_ELFS) bin

# /bin console tools live in bin/ (their own Makefile).
.PHONY: bin
bin:
	@$(MAKE) --no-print-directory -C bin

# -I. so a nested Apps/<name>/main.* finds the user-side headers (kapi.h, uikit.hpp, ...).
$(APPS_C:=.elf): %.elf: Apps/%/main.c crt0.S user.ld kapi.h applib.h httpc.h uikit.h umm.h
	$(CC) $(CFLAGS) -I. $(LDFLAGS) crt0.S $< -o $@

$(APPS_CPP:=.elf): %.elf: Apps/%/main.cpp crt0.S user.ld kapi.h uikit.hpp bmp.hpp umm.h onyxpp.hpp
	$(CXX) $(CXXFLAGS) -I. $(LDFLAGS) crt0.S $< -o $@

clean:
	rm -f *.elf *.o
	@$(MAKE) --no-print-directory -C bin clean
