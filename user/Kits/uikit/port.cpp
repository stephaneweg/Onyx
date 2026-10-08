//
// uikit/port.cpp -- UIKit's port, the one of the graphics server this UIKit is built for (uikit/port/port.h):
// the desktop's (Elegant: port/port_desktop.cpp, lib/uikit.so) unless UK_PORT_POCKET is defined (PocketUI's,
// lib/pocket/uikit.so: phase P3 of docs/POCKETUI-TECH-STUDY.md). Every other source of UIKit is common to both.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifdef UK_PORT_POCKET
#error "the pocket port (port/port_pocket.cpp) is phase P3's"
#else
#include "port/port_desktop.cpp"
#endif
