# tools/onyxrun/Makefile -- the Onyx app runner (docs/APP-RUNNER-STUDY.md), for Linux and Windows (x86-64).
#   make            -> build/onyxrun       (Linux; needs g++ and Unicorn: sh get_unicorn.sh first)
#   make win        -> build/onyxrun.exe   (Windows, cross-built with mingw-w64; one static .exe)
# MIT licence (Onyx). Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
ROOT     := ../..
B        := build
UC       := $(B)/unicorn
SRCS     := main.cpp cpu.cpp hostmem.cpp loader.cpp proc.cpp k_base.cpp k_files.cpp k_threads.cpp k_proc.cpp k_ws.cpp k_ipc.cpp display.cpp
HDRS     := onyxrun.h kobj.h display.h el0blob.inc
INC      := -I. -Istub -I$(ROOT)/kernel/include -I$(UC)/include
CXXFLAGS := -std=gnu++17 -O2 -g -Wall -Wno-unused-function -Wno-cast-function-type $(INC)

all: $(B)/onyxrun

$(B)/onyxrun: $(SRCS) $(HDRS)
	@test -f $(UC)/lin/libunicorn.a || { echo "Unicorn missing: sh get_unicorn.sh"; exit 1; }
	g++ $(CXXFLAGS) -o $@ $(SRCS) $(UC)/lin/libunicorn.a -lpthread -lm

win: $(B)/onyxrun.exe

$(B)/onyxrun.exe: $(SRCS) $(HDRS)
	@test -f $(UC)/win/libunicorn.a || { echo "Unicorn missing: sh get_unicorn.sh"; exit 1; }
	x86_64-w64-mingw32-g++-posix $(CXXFLAGS) -o $@ $(SRCS) $(UC)/win/libunicorn.a -static -lpthread -lgdi32 -luser32

clean:
	rm -f $(B)/onyxrun $(B)/onyxrun.exe

.PHONY: all win clean
