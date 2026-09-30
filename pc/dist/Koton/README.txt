Koton for Windows
=================

Koton, the music studio of Onyx -- the same program as on the Raspberry Pi (the same sources:
user/Apps/koton, its toolkit, its synthesizer, its plugins, its AI helper), built for Windows.
A song is thought in harmony: a chord track drives accompaniments, melodic lines, riffs, drums
and polyrhythms, played by a SoundFont synthesizer. The manual: manuals\koton\Koton.pdf
(Koton.fr.pdf in French) -- everything in it holds on Windows, but for the few lines below.

Needs: Windows 10 or 11 (64-bit). Nothing to install: keep the folder whole and run Koton.exe.

The folder is Koton's "SD card": what the manual calls SD:/ is this folder.
  Koton.exe                 the studio
  koton\songs               the songs (.kson; Koton Studio's .sq open too) -- the demo: demo.kson
  koton\soundfonts          the SoundFont (GeneralUser GS; File > SoundFont... to choose another)
  koton\plugins             the plugins: a folder each (main.exe + plugin.json), run as their own
                            processes, as on Onyx
  koton\settings.json       your settings (the SoundFont, the last folder, the AI's provider,
                            model and API KEY -- in plain text: keep the folder to yourself)
  bin\llm.exe               the helper that asks the AI (HTTPS; the server's certificate is not
                            verified, as on Onyx)
  fonts, res, etc, apps     the fonts, the theme and the drum catalogue Koton reads
  manuals\koton             the manual (PDF)

On Windows
  - The window is a Windows window: resize it, maximise it; the menus (File, Edit, Song,
    Track, Transport, View, AI) are its menu bar. Closing it quits (save first: Ctrl+S).
  - Opening a song: File > Open... (Koton's own dialog: SD:/ is this folder; a Windows path
    such as C:/Users/... can be typed), or drop a .kson / .sq file on the window, or
    "Open with" Koton.exe.
  - Sound: the default output of Windows (WASAPI, shared mode, about 10 ms). MIDI: every MIDI
    input Windows knows (a USB keyboard) plays the selected track, as on Onyx.
  - The AI (Compose with AI) needs a network connection and an API key, as on Onyx.

Built on Linux from the Onyx repository: sh pc/Koton/build.sh (MinGW-w64); see the file's header.
