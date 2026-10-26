onyxrun.exe -- Onyx's app runner for Windows: the Pi 4's binaries (sdcard/) run unchanged on a PC.

  From the repository (it finds sdcard/ by itself):
    tools\onyxrun\dist\onyxrun.exe echo hello
    tools\onyxrun\dist\onyxrun.exe clock
    tools\onyxrun\dist\onyxrun.exe --desktop --root C:\path\to\copy-of-sdcard

  The programs write into the folder SD: stands for: use --root on a copy to keep sdcard/ as it is.
  Options and details: docs/03-DEVELOPER-GUIDE.md section 12.1.

Rebuilt from the sources (Linux, mingw-w64):
    sh tools/onyxrun/get_unicorn.sh && make -C tools/onyxrun win
    x86_64-w64-mingw32-strip -o tools/onyxrun/dist/onyxrun.exe tools/onyxrun/build/onyxrun.exe

Licence: the runner's sources are MIT; this .exe statically links Unicorn Engine 2.1.4 (GPL-2.0) and is
therefore distributed under GPL-2.0. Its complete source: tools/onyxrun/ plus Unicorn 2.1.4 (get_unicorn.sh).
