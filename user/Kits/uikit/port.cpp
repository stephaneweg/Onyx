//
// uikit/port.cpp -- UIKit's port, the one of the graphics server this UIKit is built for (uikit/port/port.h):
// the desktop's (Elegant: port/port_desktop.cpp, lib/uikit.so) unless UK_PORT_POCKET is defined (PocketUI's:
// port/port_pocket.cpp, lib/pocket/uikit.so -- docs/POCKETUI-TECH-STUDY.md section 5). Every other source of
// UIKit is common to both: the same objects are linked into the two libraries.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifdef UK_PORT_POCKET
#include "port/port_pocket.cpp"
#else
#include "port/port_desktop.cpp"
#endif
