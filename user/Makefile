#
# Makefile -- userland ELF programs (EL0), built with the same aarch64-none-elf
# toolchain, linked at USER_VA_BASE (user.ld). Integer-only by default
# (-mgeneral-regs-only). An app may opt into hardware float by overriding the
# flags (drop -mgeneral-regs-only, add -mcpu=cortex-a72): the kernel now saves the
# full FP/SIMD state on every trap (kern/trapframe.h), so FP is safe across context
# switches. See user/bin/Makefile (fptest) for the recipe.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
AR      = $(PREFIX)ar

# -I../kernel/include: apps include <kern/kapi_abi.h> (the shared ABI). Apps call
# the kernel through the ABI table at a fixed VA, so they DON'T link against kernel
# addresses and need not be rebuilt when the kernel changes.
CFLAGS  = -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only \
	  -O2 -Wall -Wextra -fno-stack-protector -I../kernel/include
# C++ apps: same base, freestanding C++ subset (no exceptions/RTTI/STL; function-
# local statics are single-threaded; static dtors are not run -- see onyxpp.hpp).
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
LDFLAGS = -Wl,-T,user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none

# All graphical apps live in Apps/<name>/main.cpp (one folder per app) and build to
# <name>.elf here at user/ (so `make stage` keeps globbing ../user/*.elf). Every graphical
# app is C++ on the wtk toolkit now; the /bin console tools stay C in bin/.
APPS = tinycalc wpaconf tinypad applist panel cppdemo \
       demoA demoB demoC demoD demoE demoF inidemo \
       tetris snake same filer terminal \
       2048 life pong sokoban \
       calendar mandelbrot minesweeper paint \
       eyes sheet taskman voronoy theme config irc memmon spin wtkdemo writer shell

APP_ELFS = $(APPS:=.elf)

all: $(APP_ELFS) bin

# /bin console tools live in bin/ (their own Makefile).
.PHONY: bin
bin:
	@$(MAKE) --no-print-directory -C bin

# Apps/<name>/main.cpp -> <name>.elf. -I. so a nested source finds the user-side headers.
# wtk toolkit: compiled once into a static library, linked into every app.
WTK_HDR = $(wildcard wtk/*.h)
WTK_OBJ = $(patsubst %.cpp,%.o,$(wildcard wtk/*.cpp))
wtk/%.o: wtk/%.cpp $(WTK_HDR)
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@
# canvas.cpp is the hot pixel path (clear/fillRect/putOther): build it with NEON/FP
# (drop -mgeneral-regs-only, target the A72). The kernel saves full FP/SIMD on every
# trap, so SIMD is safe even though the rest of wtk and every app stay integer-only --
# the NEON is confined to this TU and Canvas's public signatures use no FP regs, so the
# calling ABI is unchanged. This explicit rule overrides the wtk/%.o pattern above.
CXXFLAGS_NEON = $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72
wtk/canvas.o: wtk/canvas.cpp $(WTK_HDR)
	$(CXX) $(CXXFLAGS_NEON) -I. -c $< -o $@
wtk/libwtk.a: $(WTK_OBJ)
	$(AR) rcs $@ $^

$(APP_ELFS): %.elf: Apps/%/main.cpp crt0.S user.ld kapi.h applib.h httpc.h bmp.hpp umm.h onyxpp.hpp wtk/libwtk.a
	$(CXX) $(CXXFLAGS) -I. $(LDFLAGS) crt0.S $< wtk/libwtk.a -o $@

clean:
	rm -f *.elf *.o
	@$(MAKE) --no-print-directory -C bin clean
