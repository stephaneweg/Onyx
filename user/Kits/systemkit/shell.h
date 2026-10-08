//
// shell.h -- asking the pocket shell for one of its screens (docs/POCKETUI-TECH-STUDY.md phase P5; docs/03 "The pocket
// shell"). In the pocket mode (PocketUI) the shell is pocketshell: the launcher (Home) behind the apps, the task
// switcher, quick settings and the notifications. It serves the IPC service "shell"; the menu bar asks it from its
// Onyx menu (Home, Open Apps, Quick Settings) and from a click on the time. On the desktop no program serves it:
// shell_ask answers 0. C and C++.
//
//   shell_ask (SHELL_MSG_HOME);        // the launcher (again: back to the app)
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _shell_onyx_h
#define _shell_onyx_h
#include "appkit/appkit.h"
#include "sk_api.h"

#define SHELL_SERVICE		"shell"
// (the message types: apart from notify.h's NOTIFY_MSG_SHOW -- the shell serves "notify" too, one mailbox)
#define SHELL_MSG_HOME		101	// the launcher; asked again while it shows: back to the app in front before
#define SHELL_MSG_SWITCHER	102	// the task switcher (the open apps, their pictures)
#define SHELL_MSG_QUICK		103	// quick settings and the notifications
#define SHELL_MSG_SEARCH	104	// the launcher, its search field focused
// One of the shell's screens asked (SHELL_MSG_*) -> 1 sent, 0 no shell runs (the desktop).
SK_API int shell_ask (int what);
SK_API int shell_running (void);	// 1 a shell serves SHELL_SERVICE (the pocket mode), 0 not

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "shell.inc"
#endif

#endif
