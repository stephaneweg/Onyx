//
// corepriv.h -- the window manager, for the files that see its classes (core.cpp, ops.cpp, a server's policy).
//
#ifndef ELEGANT_COREPRIV_H
#define ELEGANT_COREPRIV_H

#include <kern/gui/window.h>
#include "core.h"

extern CWindowManager *g_pElWM;			// 0 until el_core_start
extern CWindow *g_pElWin[EL_WINDOWS_MAX];	// the windows by their number (0: free)

extern "C" unsigned el_port_ticks (void);	// the kernel's ticks (main.cpp)

#endif
