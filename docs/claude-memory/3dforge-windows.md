---
name: 3dforge-windows
description: "3DForge for Windows (pc/3DForge, 2026-10-18) — WPF over forgecore.dll built from user/Apps/3dforge unchanged; test it with --shot, never by capturing the screen"
metadata:
  node_type: memory
  type: project
  originSessionId: 3684f0c1-62c3-4be5-97ae-0904c104682c
  modified: 2026-10-18T09:44:13.318Z
---

The user asked (2026-10-18) for 3DForge as a native Windows program "same features" — WPF or own rendering, my choice: WPF window + `forgecore.dll` (user/Apps/3dforge compiled unchanged over `pc/3DForge/core/shim`, view drawn by a threaded z-buffer). Built, pushed to main, `pc/dist/3DForge/`. Docs: docs/03 (PC tools list), docs/04 "3DForge on a Windows PC".

**Why:** the user works on this PC at the same time (Fusion 360 was open): a screen capture grabbed his screen, not my window.
**How to apply:** check the app with `3DForge.exe --shot out.png --do "..."` (off-screen, Shot.cs) or `sh pc/3DForge/tools/shots.sh`; never SetForegroundWindow / screen captures. Kill a running 3DForge.exe before rebuilding (a locked DLL makes the build keep the old exe silently). The right panel's logic is duplicated in forgecore.cpp (form_build) from fui.h: change both. Translated to French (Lang.cs, FORGE_LANG; 2026-10-18). Related: [[cad-app-decisions]].

MCP (2026-10-18, the user's ask): `3DForge.exe --mcp` (Mcp.cs) relays to the window's pipe \.\pipe\3DForge (Control.cs). Registered for this project (`claude mcp add -s local 3dforge`) on an installed copy in %LOCALAPPDATA%\3DForge (so builds of pc/dist are never locked) -- copy pc/dist/3DForge there again after a rebuild. A launched window shows on the user's screen: for tests use FORGE_OFFSCREEN=1 (`python pc/3DForge/tools/mcp_test.py`), which refuses to run if a 3DForge window is open.

View drawing (2026-10-18): OpenGL (pc/3DForge/core/glview.cpp, 8x MSAA, frender.h's FORGE_HOST_SHOW hook; FORGE_SOFT=1 = CPU threads). frender.h's FORGE_GPU_MAX_* limits are raised by the shim -- the 2048 px limit once made a maximised window fall back to one thread (1.2 s a frame). Build pc/dist from a clean worktree when other sessions have work in progress in the shared checkout.
