#
# common.mk -- what the graphics servers share (user/Servers/common/: the loop, the routing, the window manager
# and compositor, the requests' decoding; docs/02-KERNEL-INTERNALS.md section 10), included by each server's
# Makefile (Servers/elegant, Servers/pocketui) after it set SERVER (its name), SERVER_OBJS (its own objects,
# obj/<name>.o from <name>.cpp) and SERVER_HDRS. Freestanding C++ programs as the /bin tools
# (../../BinUtils/Makefile), built with the kernel's image class (../../../kernel/gui/gimage.cpp, shared with the
# kernel) and the stand-ins of common/port/ for the few Circle headers it and the window manager include --
# common/port/ and common/wm/ come FIRST on the include path (circle/*.h, kern/layout.h, assert.h; kern/gui/window.h).
#
PREFIX ?= aarch64-none-elf-
CXX     = $(PREFIX)g++

COMMON   = ../common
KGUI     = ../../../kernel/gui
CXXFLAGS = -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -O2 -Wall -Wextra \
	   -fno-stack-protector -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
	   -DWIN_PIXELS_HOOK -I$(COMMON)/port -I$(COMMON)/wm -I$(COMMON) -I. -I../.. -I../../Kits -I../../Runtime -I../../Include -I../../../kernel/include
LDFLAGS  = -Wl,-T,../../Runtime/user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none ../../lib/appkit_stubs.o \
	   -Wl,--defsym,memset=kapi_memset -Wl,--defsym,memcpy=kapi_memcpy -Wl,--defsym,memmove=kapi_memmove

COMMON_OBJS = obj/serve.o obj/route.o obj/core.o obj/ops.o obj/window.o obj/gimage.o
COMMON_HDRS = $(COMMON)/core.h $(COMMON)/corepriv.h $(COMMON)/kws.h $(COMMON)/policy.h ../../Kits/uikit/port/elegant.h \
	      $(wildcard $(COMMON)/port/*.h $(COMMON)/port/circle/*.h $(COMMON)/port/circle/sched/*.h $(COMMON)/port/kern/*.h) \
	      $(wildcard ../../../kernel/include/kern/gui/*.h $(COMMON)/wm/kern/gui/*.h $(COMMON)/wm/*.inc) ../../../kernel/include/kern/kapi_abi.h \
	      ../../Kits/appkit/appkit.h ../../Runtime/onyxpp.hpp ../../Runtime/umm.h

all: $(SERVER).elf

$(SERVER).elf: $(SERVER_OBJS) $(COMMON_OBJS) ../../Runtime/crt0.S ../../Runtime/user.ld ../../lib/appkit_stubs.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) ../../Runtime/crt0.S $(SERVER_OBJS) $(COMMON_OBJS) -o $@

obj/%.o: %.cpp $(COMMON_HDRS) $(SERVER_HDRS)
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/%.o: $(COMMON)/%.cpp $(COMMON_HDRS)
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/%.o: $(COMMON)/wm/%.cpp $(COMMON_HDRS)
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -w -c $< -o $@

obj/%.o: $(KGUI)/%.cpp $(COMMON_HDRS)
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -w -c $< -o $@

clean:
	rm -rf obj $(SERVER).elf

.PHONY: all clean
