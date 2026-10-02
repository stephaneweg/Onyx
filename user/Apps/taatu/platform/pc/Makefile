# Makefile -- PC test client (Windows MinGW / Linux). Builds taatu_pc from the portable core/.
# Connects to a TAATU server over plain WebSocket (local dev ws://127.0.0.1:3000). See DESIGN.md §8.
#
#   make                  # build taatu_pc[.exe]
#   ./taatu_pc --host 127.0.0.1 --port 3000 --token <session> --room 4 --say "salut"
#
USER     ?= ../../../..
CXX      ?= g++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -I$(USER)
SRC       = main_pc.cpp crypto_pc.cpp
HDRS      = $(wildcard ../../core/*.hpp) transport_tcp.hpp

ifeq ($(OS),Windows_NT)
  BIN  = taatu_pc.exe
  LIBS = -lws2_32
else
  BIN  = taatu_pc
  LIBS =
endif

$(BIN): $(SRC) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LIBS)

clean:
	rm -f taatu_pc taatu_pc.exe

.PHONY: clean
