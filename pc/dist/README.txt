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
        Latin-1 with Onyx line ends, so they go to the SD card as they are. Run > Make .bax
        compiles the program (a .bax runs without being parsed, on Onyx too).

Runtime The program's window has a Program menu (Stop, Restart) and a View menu (Full
        screen, Zoom). FULLSCREEN does nothing on the PC. Ctrl+C or Ctrl+Break stops a
        program. KEYDOWN, the mouse, PLAY / SOUND, the controls (BUTTON, TEXTBOX...),
        MSGBOX, OPENFILE$, the clipboard work as on Onyx; LAUNCH / EXEC do not (Onyx apps);
        SHELL runs a Windows command (cmd.exe) and shows its output.

Built on Linux by pc/build.sh (mingw-w64 + the .NET SDK).

Onyx Remote
===========

OnyxRemote.exe (+ OnyxRemote.exe.config): the Onyx session in one window of this PC -- the
client of rdpd (port 3390, started at boot on the Pi). Type the Pi's address, Connect (the
window then takes the size of the Pi's screen, not resizable). The Onyx
menu bar across the top; the Onyx windows as child windows inside (normal windows, or with their
Onyx frame: "Onyx frames" -- its window menu, minimise and maximise buttons pressed on the Pi),
moved freely; their close button closes the Onyx app; clicking a window gives it the keyboard.
The menu bar's menus, the dock, the notifications and the Wi-Fi menu are drawn over the windows,
see-through where they are clear on the Pi. A minimised window, or one on another workspace, is
not shown. "Desktop" shows the Onyx desktop behind them; "16-bit colours" sends half the data
(games). "Console": a telnet console on the Pi (telnetd, port 23; or type
address:port) in its own window -- scroll, select and copy the output, Enter sends the command
line, Up / Down the history, Ctrl+C interrupts. No password, no encryption: trusted LAN only.

NintendoEMU
===========

NintendoEMU.exe (+ NintendoEMU.exe.config, nemucore.dll): the emulators of Onyx on the PC --
Game Boy / Color, Game Boy Advance, NES, Super Nintendo, Nintendo 64, GameCube (the same cores
as on the Pi) -- with a library of your games.

  NintendoEMU.exe            the library
  NintendoEMU.exe <game>     that game added to the library and played

Library  Library > Add Games... / Add Folder... (or drop files and folders on the window): the
         paths are kept in library.txt beside the program (or %APPDATA%\NintendoEMU). Double-click,
         Enter or a gamepad's Start plays; Del removes a game from the list (not from the disk).
Keys     As on Onyx (Help > Controls...): arrows, X / Z = A / B, Enter = Start, Backspace =
         Select, A S Q W... Xbox-style pads (XInput). P pause, F11 / Alt+Enter full screen,
         F12 the speed, Ctrl+R reset.
Saves    <game>.sav beside the game: the same files as on Onyx.
3D       Nintendo 64 and GameCube pictures are drawn in software (Options > 3D Resolution).
         The GameCube is slow (its CPU is interpreted) and silent, as on Onyx for now.
