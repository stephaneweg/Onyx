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
  LIBS = -lsecur32 -lws2_32      # secur32: Schannel TLS (tls_win.hpp)
else
  BIN  = taatu_pc
  LIBS =                         # Linux prod TLS (OpenSSL) is a TODO; plain TCP for dev
endif

$(BIN): $(SRC) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LIBS)

# Windows GUI build (GDI+ decode, Win32 window): see what the renderer looks like on the PC.
#   make gui && ./taatu_gui --demo                      # room + sample avatar, no login
#   ./taatu_gui --login --pseudo <p> --password <pw>    # live (prompts if omitted)
gui: gui_win.cpp crypto_pc.cpp $(HDRS) assets_pc.hpp tls_win.hpp
	$(CXX) $(CXXFLAGS) gui_win.cpp crypto_pc.cpp -o taatu_gui.exe -lgdiplus -lole32 -lgdi32 -luser32 -lsecur32 -lws2_32

clean:
	rm -f taatu_pc taatu_pc.exe taatu_gui.exe

.PHONY: clean gui
