#
# Makefile -- the SuperTuxKart port for Onyx, milestone M0 (docs/SUPERTUXKART-PORT.md).
#
# Builds three of SuperTuxKart's bundled libraries, from STK's own tree, as Onyx newlib static
# libraries, and a console proof program that links them:
#   build/libbullet.a       lib/bullet (Bullet 2.79, STK's fork): LinearMath + Collision + Dynamics
#   build/libangelscript.a  lib/angelscript (2.35.1), AS_MAX_PORTABILITY + AS_NO_THREADS (as STK
#                           builds it on AArch64: the generic calling convention, STK's wrappers)
#   build/libirrlicht.a     lib/irrlicht (STK's Irrlicht fork), server-only configuration (null driver)
#   build/stkpoc.elf        stkpoc.cpp: the C++ runtime (exceptions, RTTI, STL, std::thread / mutex /
#                           condition_variable) + Bullet + a script
#
# Unlike the other Onyx apps this is FULL C++: -fexceptions -frtti, libstdc++, and the link script
# stk.ld (keeps the unwind tables) + onyx_eh.c (registers them), and libstdc++'s threads on the
# kapi threads (compat/bits/gthr-default.h, onyx_gthreads*.c*). newlib as Writer / Doom
# (../libc/crt0libc.S + ../libc/onyx_syscalls.c).
#
# STK's source is NOT in the Onyx repository (yet): a shallow clone next to it,
#   git clone --depth 1 https://github.com/supertuxkart/stk-code C:\Users\troll\stk-port\stk-code
# Override with `make STK=<path>`. Nothing here is staged to the SD card.
#
PREFIX ?= aarch64-none-elf-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
AR      = $(PREFIX)ar
STK    ?= ../../../../../stk-port/stk-code
OUT     = build

BASEFLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -nostartfiles \
	    -ffunction-sections -fdata-sections -I.. -I../../kernel/include
CFLAGS    = $(BASEFLAGS)
# Full C++ (STK throws, catches, dynamic_casts). gnu++17: STK's CMake asks gnu++0x, but the tree
# compiles as C++17. The toolchain's libstdc++ is --disable-threads (no std::mutex, no
# std::thread): -Icompat puts our bits/gthr-default.h (kapi threads) in front of its gthr-single.h
# and _GLIBCXX_HAS_GTHREADS turns libstdc++'s thread classes on (onyx_gthreads.c / _cxx.cpp).
CXXFLAGS  = $(BASEFLAGS) -std=gnu++17 -fexceptions -frtti -Icompat -D_GLIBCXX_HAS_GTHREADS=1
LDFLAGS   = -Wl,-T,stk.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none -Wl,--gc-sections

# ---- Bullet (the list of STK's lib/bullet/CMakeLists.txt: every .cpp but Bullet-C-API.cpp) ----
BULLET_DIR  = $(STK)/lib/bullet/src
BULLET_SRC  = $(filter-out %/Bullet-C-API.cpp,$(shell find $(BULLET_DIR) -name '*.cpp'))
BULLET_OBJ  = $(patsubst $(BULLET_DIR)/%.cpp,$(OUT)/bullet/%.o,$(BULLET_SRC))
BULLET_FLAGS = $(CXXFLAGS) -I$(BULLET_DIR) -w

# ---- AngelScript ----
AS_DIR   = $(STK)/lib/angelscript/source
AS_SRC   = $(wildcard $(AS_DIR)/as_*.cpp)
AS_OBJ   = $(patsubst $(AS_DIR)/%.cpp,$(OUT)/angelscript/%.o,$(AS_SRC))
AS_FLAGS = $(CXXFLAGS) -DAS_MAX_PORTABILITY -DAS_NO_THREADS -I$(STK)/lib/angelscript/include -w

# ---- Irrlicht (STK's fork), the server-only configuration: the scene graph, the mesh loaders,
# the file system and the GUI on the null video driver (CIrrDeviceServer) -- no GL driver, no
# image codecs yet (STK's CMake for SERVER_ONLY). It includes a few of STK's own headers (src/utils,
# src/io): those are resolved when the game itself links. Needs the POSIX layer (compat/dirent.h,
# compat/byteswap.h, onyx_posix.c) and zlib's header (third_party).
IRR_DIR   = $(STK)/lib/irrlicht
IRR_SRC   = $(wildcard $(IRR_DIR)/source/Irrlicht/*.cpp)
IRR_OBJ   = $(patsubst $(IRR_DIR)/source/Irrlicht/%.cpp,$(OUT)/irrlicht/%.o,$(IRR_SRC))
IRR_DEFS  = -DSERVER_ONLY -DNDEBUG=1 -D_IRR_STATIC_LIB_ 	    -DNO_IRR_COMPILE_WITH_LIBPNG_ -DNO_IRR_COMPILE_WITH_LIBJPEG_ -DNO_IRR_COMPILE_WITH_BMP_LOADER_ 	    -DNO_IRR_COMPILE_WITH_PNG_LOADER_ -DNO_IRR_COMPILE_WITH_JPG_LOADER_ -DNO_IRR_COMPILE_WITH_BMP_WRITER_ 	    -DNO_IRR_COMPILE_WITH_PNG_WRITER_ -DNO_IRR_COMPILE_WITH_JPG_WRITER_ -DNO_IRR_COMPILE_WITH_OPENGL_ 	    -DNO_IRR_COMPILE_WITH_X11_ -DNO_IRR_COMPILE_WITH_WAYLAND_DEVICE_ -DNO_IRR_COMPILE_WITH_SDL_DEVICE_ 	    -DNO_IRR_COMPILE_WITH_DIRECT3D_9_ -DNO_IRR_COMPILE_WITH_VULKAN_ -DNO_IRR_COMPILE_WITH_JOYSTICK_EVENTS_
IRR_FLAGS = $(CXXFLAGS) $(IRR_DEFS) -I$(IRR_DIR)/include -I$(IRR_DIR)/source/Irrlicht 	    -I$(STK)/lib/graphics_engine/include -I$(STK)/src -I../../third_party/zlib-1.3.1 -w

all: $(OUT)/libbullet.a $(OUT)/libangelscript.a $(OUT)/libirrlicht.a $(OUT)/stkpoc.elf

$(OUT)/libirrlicht.a: $(IRR_OBJ)
	$(AR) rcs $@ $^
$(OUT)/irrlicht/%.o: $(IRR_DIR)/source/Irrlicht/%.cpp compat/dirent.h compat/byteswap.h
	@mkdir -p $(dir $@)
	$(CXX) $(IRR_FLAGS) -c $< -o $@

$(OUT)/libbullet.a: $(BULLET_OBJ)
	$(AR) rcs $@ $^
$(OUT)/bullet/%.o: $(BULLET_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(BULLET_FLAGS) -c $< -o $@

$(OUT)/libangelscript.a: $(AS_OBJ)
	$(AR) rcs $@ $^
$(OUT)/angelscript/%.o: $(AS_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(AS_FLAGS) -c $< -o $@

$(OUT)/crt0libc.o: ../libc/crt0libc.S
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/onyx_syscalls.o: ../libc/onyx_syscalls.c ../kapi.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/onyx_eh.o: onyx_eh.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/onyx_gthreads.o: onyx_gthreads.c compat/bits/gthr-default.h ../kapi.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I. -Wall -c $< -o $@
$(OUT)/onyx_gthreads_cxx.o: onyx_gthreads_cxx.cpp compat/bits/gthr-default.h
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -Wall -c $< -o $@
$(OUT)/onyx_posix.o: onyx_posix.c compat/dirent.h ../kapi.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I. -Wall -c $< -o $@
$(OUT)/stkpoc.o: stkpoc.cpp compat/bits/gthr-default.h
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -I$(BULLET_DIR) -I$(STK)/lib/angelscript/include -Wall -c $< -o $@

STK_RT_OBJ = $(OUT)/crt0libc.o $(OUT)/onyx_syscalls.o $(OUT)/onyx_eh.o $(OUT)/onyx_gthreads.o \
	     $(OUT)/onyx_gthreads_cxx.o $(OUT)/onyx_posix.o
STKPOC_OBJ = $(STK_RT_OBJ) $(OUT)/stkpoc.o
$(OUT)/stkpoc.elf: $(STKPOC_OBJ) $(OUT)/libbullet.a $(OUT)/libangelscript.a stk.ld
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(STKPOC_OBJ) $(OUT)/libangelscript.a $(OUT)/libbullet.a \
	       -lstdc++ -lm -o $@
	$(PREFIX)size $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
