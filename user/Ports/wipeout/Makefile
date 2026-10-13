#
# Makefile -- wipEout for Onyx: wipeout-rewrite (third_party/wipeout-rewrite: the game, its
# software renderer) + the Onyx platform layer (wipeout_onyx.c, wipeout_uikit.cpp), built against
# newlib like Doom (../../Runtime/libc: crt0libc.S + the kapi syscall layer). Output:
# ../../wipeout.elf, staged as SD:/apps/wipeout.app/main.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
WR      = ../../../third_party/wipeout-rewrite/src
OUT     = build

INC     = -I../.. -I../../Kits -I../../Runtime -I../../Include -I../../Libs -I../../Emulators -I../../Ports -I../../../kernel/include
CFLAGS  = -mcpu=cortex-a72 -O3 -fno-math-errno -fno-stack-protector -fno-pic -fno-pie -nostartfiles -std=gnu99 \
	  -DONYX -DRENDERER_SOFTWARE -fmacro-prefix-map=$(WR)/=wipeout-rewrite/ $(INC) -I$(WR) -I$(WR)/libs -w
CXXFLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -nostartfiles \
	  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit $(INC)
LDFLAGS = -Wl,-T,../../Runtime/user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none -Wl,--gc-sections ../../lib/appkit_stubs.o
# (everything is made again when AppKit's stubs change -- a slot moved: user/Makefile; not in $^)
.EXTRA_PREREQS = ../../lib/appkit_stubs.o

WR_SRC  = input mem render_software system types utils
WR_GAME = camera droid game hud image ingame_menus intro main_menu menu object particle race scene sfx \
	  ship ship_ai ship_player title track ui weapon
OBJS = $(addprefix $(OUT)/, $(addsuffix .o, $(WR_SRC))) $(addprefix $(OUT)/wo_, $(addsuffix .o, $(WR_GAME))) \
       $(OUT)/wipeout_onyx.o $(OUT)/wipeout_uikit.o $(OUT)/crt0libc.o $(OUT)/onyx_syscalls.o
WR_HDR = $(wildcard $(WR)/*.h) $(wildcard $(WR)/wipeout/*.h)

all: ../../wipeout.elf

../../wipeout.elf: $(OBJS) ../../lib/uikit.imp.a ../../lib/audiokit.imp.a ../../lib/systemkit.imp.a ../../Runtime/user.ld
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) ../../lib/uikit.imp.a ../../lib/audiokit.imp.a ../../lib/systemkit.imp.a -lm -o $@

$(OUT)/%.o: $(WR)/%.c $(WR_HDR) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/wo_%.o: $(WR)/wipeout/%.c $(WR_HDR) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/wipeout_onyx.o: wipeout_onyx.c $(WR_HDR) ../../Kits/appkit/appkit.h ../../Kits/uikit/win.h ../../Include/gamepad.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/wipeout_uikit.o: wipeout_uikit.cpp ../../Kits/appkit/appkit.h | $(OUT)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/crt0libc.o: ../../Runtime/libc/crt0libc.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/onyx_syscalls.o: ../../Runtime/libc/onyx_syscalls.c ../../Kits/appkit/appkit.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT) ../../wipeout.elf
