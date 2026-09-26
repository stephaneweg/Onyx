Onyx BASIC for Windows
======================

The editor, compiler and runtime of Onyx's BASIC (the same compiler and virtual machine as
on the Raspberry Pi), to write and try BASIC programs on a PC.

Needs: Windows 10 or 11 (64-bit); .NET Framework 4.8 is already part of them.
Keep the three files together: OnyxBasic.exe, OnyxBasic.exe.config, obcore.dll
(help.txt is the keyword list, used when the SD folder has none).

  OnyxBasic.exe [program.bas]            the editor (QBasic style)
  OnyxBasic.exe --run program.bas [args] run a program in its window

SD:/  Programs that read or write files use SD:/ paths, as on Onyx. On the PC, SD:/ is a
      folder: Options > SD Folder... in the editor (or --sd <folder>). By default it is an
      "sdcard" folder beside the program or up to three folders above it -- in the Onyx
      repository, pc/dist finds the repository's sdcard/ by itself.

Editor  The program is edited one module at a time -- the main module and each SUB /
        FUNCTION -- as in QBasic: View > SUBs... (F2) lists them, Edit > New SUB... makes
        one (or type "SUB Name" on a line and press Enter). Run > Start (F5) checks the
        syntax then runs the program; an error jumps to its line. Files are saved in
        Latin-1 with Onyx line ends, so they go to the SD card as they are.

Runtime The program's window has a Program menu (Stop, Restart) and a View menu (Full
        screen, Zoom). FULLSCREEN does nothing on the PC. Ctrl+C or Ctrl+Break stops a
        program. KEYDOWN, the mouse, PLAY / SOUND, the controls (BUTTON, TEXTBOX...),
        MSGBOX, OPENFILE$, the clipboard work as on Onyx; LAUNCH / EXEC do not (Onyx apps);
        SHELL runs a Windows command (cmd.exe) and shows its output.

Built on Linux by pc/build.sh (mingw-w64 + the .NET SDK).
