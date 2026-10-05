#
# Makefile -- Elegant, the graphics server (SD:/bin/elegant). A freestanding C++ program as the
# /bin tools (../../BinUtils/Makefile), of several sources: its own, and the kernel's window manager
# and compositor (../../../kernel/gui) built for a user process with the stand-ins of port/ -- which
# come FIRST on the include path (circle/*.h, kern/layout.h, assert.h).
#
PREFIX ?= aarch64-none-elf-
CXX     = $(PREFIX)g++

KGUI     = ../../../kernel/gui
CXXFLAGS = -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -O2 -Wall -Wextra \
	   -fno-stack-protector -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
	   -DWIN_PIXELS_HOOK -Iport -I../.. -I../../Kits -I../../Runtime -I../../Include -I../../../kernel/include
LDFLAGS  = -Wl,-T,../../Runtime/user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none ../../lib/appkit_stubs.o \
	   -Wl,--defsym,memset=kapi_memset -Wl,--defsym,memcpy=kapi_memcpy -Wl,--defsym,memmove=kapi_memmove

OBJS = obj/main.o obj/server.o obj/core.o obj/ops.o obj/window.o obj/gimage.o
HDRS = core.h corepriv.h kws.h ../../Kits/appkit/elegant.h $(wildcard port/*.h port/circle/*.h port/circle/sched/*.h port/kern/*.h) \
       $(wildcard ../../../kernel/include/kern/gui/*.h) ../../../kernel/include/kern/kapi_abi.h \
       ../../Kits/appkit/appkit.h ../../Runtime/onyxpp.hpp ../../Runtime/umm.h

all: elegant.elf

elegant.elf: $(OBJS) ../../Runtime/crt0.S ../../Runtime/user.ld ../../lib/appkit_stubs.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) ../../Runtime/crt0.S $(OBJS) -o $@

obj/%.o: %.cpp $(HDRS)
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/%.o: $(KGUI)/%.cpp $(HDRS)
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -w -c $< -o $@

clean:
	rm -rf obj elegant.elf

.PHONY: all clean
