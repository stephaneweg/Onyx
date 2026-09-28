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
# Freestanding apps have no libc: alias the C memory primitives GCC may emit on its own
# onto the kapi ones (kapi.h, ABI v36 -> the kernel's Circle implementations).
KAPI_ALIASES = -Wl,--defsym,memset=kapi_memset -Wl,--defsym,memcpy=kapi_memcpy \
	       -Wl,--defsym,memmove=kapi_memmove

# All graphical apps live in Apps/<name>/main.cpp (one folder per app) and build to
# <name>.elf here at user/ (so `make stage` keeps globbing ../user/*.elf; it stages
# each one as apps/<name>.app/main, without extension). Every graphical
# app is C++ on the wtk toolkit now; the /bin console tools stay C in bin/.
APPS = tinycalc wpaconf tinypad applist panel cppdemo \
       demoA demoB demoC demoD demoE demoF inidemo \
       tetris snake same terminal \
       2048 life pong sokoban \
       calendar mandelbrot minesweeper paint \
       eyes sheet taskman voronoy theme config irc memmon spin wtkdemo writer shell fileviewer menubar notifyd shutdown plasma shelf ask agenda imageview widgets qbasic fmtracker lisa invaders pipes solitaire freecell graphcalc iconedit rtfview gbemu gamelib padconf gbaemu nesemu snesemu teapot gpudemo n64emu gcemu wifimenu

APP_ELFS = $(APPS:=.elf)

all: $(APP_ELFS) bin bin/basic.elf doom

# Doom (doom/: doomgeneric from third_party + the Onyx layer, against newlib) -> doom.elf.
.PHONY: doom
doom: wtk/libwtk.a
	@$(MAKE) --no-print-directory -C doom

# /bin console tools live in bin/ (their own Makefile).
.PHONY: bin
bin: v3d/libv3d.a
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
# The image codecs (stb_image, simplewebp, PCX) use floating point: FP/SIMD too.
wtk/imgload.o: wtk/imgload.cpp img/imgload.hpp img/stb_image.h img/simplewebp.h
	$(CXX) $(CXXFLAGS_NEON) -Wno-unused-parameter -Wno-sign-compare -I. -c $< -o $@
wtk/libwtk.a: $(WTK_OBJ)
	$(AR) rcs $@ $^

$(APP_ELFS): %.elf: Apps/%/main.cpp crt0.S user.ld kapi.h applib.h launch.h httpc.h bmp.hpp umm.h onyxpp.hpp game.h cards.h rtf.h gamepad.h emucore.h wtk/libwtk.a basic/libbasic.a gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a n64/libn64.a gc/libgc.a v3d/libv3d.a
	$(CXX) $(CXXFLAGS) -I. $(LDFLAGS) $(KAPI_ALIASES) crt0.S $< wtk/libwtk.a basic/libbasic.a gb/libgb.a gba/libgba.a nes/libnes.a snes/libsnes.a n64/libn64.a gc/libgc.a v3d/libv3d.a -o $@

# The Game Boy / Color emulator core (gb/: integer only, no libc), for gbemu and the library.
gb/gb.o: gb/gb.cpp gb/gb.h
	$(CXX) $(CXXFLAGS) -O2 -I. -c $< -o $@
gb/libgb.a: gb/gb.o
	$(AR) rcs $@ $^

# The Game Boy Advance emulator core (gba/: integer only, no libc), for gbaemu and the library.
GBA_OBJS = gba/gba.o gba/gba_cpu.o gba/gba_bios.o gba/gba_ppu.o gba/gba_apu.o
gba/%.o: gba/%.cpp gba/gba.h
	$(CXX) $(CXXFLAGS) -O2 -I. -c $< -o $@
gba/libgba.a: $(GBA_OBJS)
	$(AR) rcs $@ $^

# The NES / Famicom emulator core (nes/: integer only, no libc), for nesemu and the library.
NES_OBJS = nes/nes.o nes/nes_ppu.o nes/nes_apu.o
nes/%.o: nes/%.cpp nes/nes.h nes/nes_optable.inc nes/nes_mix.inc
	$(CXX) $(CXXFLAGS) -O2 -I. -c $< -o $@
nes/libnes.a: $(NES_OBJS)
	$(AR) rcs $@ $^

# The Super Nintendo core (snes/: integer only, no libc), for snesemu and the library.
SNES_OBJS = snes/snes.o snes/snes_cpu.o snes/snes_ppu.o snes/snes_apu.o
snes/%.o: snes/%.cpp snes/snes.h snes/snes_gauss.inc
	$(CXX) $(CXXFLAGS) -O2 -I. -c $< -o $@
# The Nintendo 64 core (n64/: the FPU is used: FP/SIMD flags), for n64emu.
N64_OBJS = n64/n64_cpu.o n64/n64_bus.o n64/n64_gfx.o n64/n64_audio.o
n64/%.o: n64/%.cpp n64/n64.h
	$(CXX) $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72 -fno-math-errno -O3 -I. -c $< -o $@
n64/libn64.a: $(N64_OBJS)
	$(AR) rcs $@ $^
# The GameCube core (gc/: the FPU is used, as n64/), for gcemu.
GC_OBJS = gc/gc_cpu.o gc/gc_mem.o gc/gc_hw.o gc/gc_gx.o gc/gc_gxdraw.o gc/gc_dsp.o gc/gc_boot.o gc/gc_jit.o gc/gc_card.o gc/gc_zelda.o gc/gc_gxgpu.o
gc/%.o: gc/%.cpp gc/gc.h
	$(CXX) $(filter-out -mgeneral-regs-only,$(CXXFLAGS)) -mcpu=cortex-a72 -fno-math-errno -O3 -I. -c $< -o $@
gc/libgc.a: $(GC_OBJS)
	$(AR) rcs $@ $^
# The V3D QPU builder (v3d/: shaders generated at run time, e.g. the GameCube's TEV): the C++
# builder + Mesa's QPU packer (tools/qpu/mesa, MIT) + the few libc functions it calls.
V3D_MESA = ../tools/qpu/mesa
V3D_OBJS = v3d/qpu.o v3d/shaders.o v3d/gxtev.o v3d/qpu_pack.o v3d/qpu_instr.o v3d/shim.o
v3d/qpu.o: v3d/qpu.cpp v3d/qpu.h
	$(CXX) $(CXXFLAGS) -O2 -I. -I$(V3D_MESA) -c $< -o $@
v3d/shaders.o: v3d/shaders.cpp v3d/shaders.h v3d/qpu.h
	$(CXX) $(CXXFLAGS_NEON) -O2 -I. -I$(V3D_MESA) -c $< -o $@
v3d/gxtev.o: v3d/gxtev.cpp v3d/gxtev.h v3d/qpu.h
	$(CXX) $(CXXFLAGS_NEON) -O2 -I. -I$(V3D_MESA) -c $< -o $@
v3d/qpu_pack.o: $(V3D_MESA)/broadcom/qpu/qpu_pack.c
	$(CC) $(CFLAGS) -std=gnu11 -DNDEBUG -w -I$(V3D_MESA) -c $< -o $@
v3d/qpu_instr.o: $(V3D_MESA)/broadcom/qpu/qpu_instr.c
	$(CC) $(CFLAGS) -std=gnu11 -DNDEBUG -w -I$(V3D_MESA) -c $< -o $@
v3d/shim.o: v3d/shim.c
	$(CC) $(CFLAGS) -c $< -o $@
v3d/libv3d.a: $(V3D_OBJS)
	$(AR) rcs $@ $^

snes/libsnes.a: $(SNES_OBJS)
	$(AR) rcs $@ $^

# Onyx BASIC (basic/): the core (compiler + VM, doubles -> FP/SIMD, no libc) as a library
# (linked into the qbasic editor too), and the runtime /bin/basic (basic/runtime.cpp).
BAS_HDR = basic/bas.h basic/basint.h basic/basnum.h basic/bas3d.h basic/bas3dscene.h basic/basscreen.h
BAS_OBJ = basic/basnum.o basic/bascomp.o basic/basvm.o basic/basbax.o
CXXFLAGS_FP = $(CXXFLAGS_NEON) -fno-math-errno
# The graphing calculator computes in double: FP/SIMD for the whole app.
graphcalc.elf: CXXFLAGS := $(CXXFLAGS_FP)
# The teapot demo computes its geometry in float: FP/SIMD too.
teapot.elf: CXXFLAGS := $(CXXFLAGS_FP)
teapot.elf: Apps/teapot/teapot.h Apps/teapot/teapot_data.inc
gpudemo.elf: CXXFLAGS := $(CXXFLAGS_FP)
n64emu.elf: CXXFLAGS := $(CXXFLAGS_FP)
gcemu.elf: CXXFLAGS := $(CXXFLAGS_FP)
graphcalc.elf: Apps/graphcalc/expr.h
basic/%.o: basic/%.cpp $(BAS_HDR)
	$(CXX) $(CXXFLAGS_FP) -I. -c $< -o $@
basic/libbasic.a: $(BAS_OBJ)
	$(AR) rcs $@ $^
bin/basic.elf: basic/runtime.cpp basic/libbasic.a wtk/libwtk.a crt0.S user.ld kapi.h $(BAS_HDR)
	$(CXX) $(CXXFLAGS_FP) -I. $(LDFLAGS) $(KAPI_ALIASES) crt0.S basic/runtime.cpp basic/libbasic.a wtk/libwtk.a -o $@

clean:
	rm -f *.elf *.o basic/*.o basic/*.a gb/*.o gb/*.a gba/*.o gba/*.a nes/*.o nes/*.a snes/*.o snes/*.a n64/*.o n64/*.a gc/*.o gc/*.a v3d/*.o v3d/*.a bin/basic.elf
	@$(MAKE) --no-print-directory -C bin clean
	@$(MAKE) --no-print-directory -C doom clean
