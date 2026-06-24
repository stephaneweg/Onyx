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

PROGS   = demoA.elf demoB.elf demoC.elf demoE.elf demoF.elf \
	  panel.elf applist.elf tinypad.elf inidemo.elf \
	  tetris.elf snake.elf same.elf filer.elf terminal.elf \
	  2048.elf life.elf pong.elf sokoban.elf \
	  calendar.elf mandelbrot.elf minesweeper.elf paint.elf \
	  eyes.elf sheet.elf taskman.elf voronoy.elf theme.elf config.elf \
	  irc.elf memmon.elf cppdemo.elf

# Per-app subfolder C++ apps: user/<name>/main.cpp -> <name>.elf (uikit.hpp toolkit).
# This is the target layout as apps are rewritten in C++ (one folder per app).
SUBAPPS  = tinycalc wpaconf demoD
SUBPROGS = $(SUBAPPS:=.elf)

all: $(PROGS) $(SUBPROGS) bin

# /bin console tools live in bin/ (their own Makefile).
.PHONY: bin
bin:
	@$(MAKE) --no-print-directory -C bin

%.elf: %.c crt0.S user.ld kapi.h applib.h httpc.h uikit.h umm.h
	$(CC) $(CFLAGS) $(LDFLAGS) crt0.S $< -o $@

# C++ apps (e.g. cppdemo): g++ + the C++ runtime support header (onyxpp.hpp).
%.elf: %.cpp crt0.S user.ld kapi.h umm.h onyxpp.hpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) crt0.S $< -o $@

# Subfolder C++ apps: <name>/main.cpp -> <name>.elf. -I. so they find the user-side
# headers (kapi.h, uikit.hpp, ...) from a nested directory.
$(SUBPROGS): %.elf: %/main.cpp crt0.S user.ld kapi.h uikit.hpp umm.h onyxpp.hpp
	$(CXX) $(CXXFLAGS) -I. $(LDFLAGS) crt0.S $< -o $@

clean:
	rm -f *.elf *.o
	@$(MAKE) --no-print-directory -C bin clean
