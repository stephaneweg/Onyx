#!/bin/sh
# Build fmsplayer.exe (Windows, x86-64) with MinGW-w64 -- one static executable.
#   sudo apt install mingw-w64      (or MSYS2 on Windows: pacman -S mingw-w64-x86_64-gcc)
cd "$(dirname "$0")"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -static -mwindows -I../../kernel/include \
	fmsplayer.cpp -lwinmm -lcomdlg32 -lgdi32 -lshell32 -o fmsplayer.exe
