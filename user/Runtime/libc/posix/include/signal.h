/*
 * signal.h -- newlib's, plus the sa_flags values code passes to sigaction (libonyxposix
 * signal.c). Onyx has no asynchronous signals: a handler runs when the process raises the
 * signal itself (raise, abort, kill (getpid (), sig)); SIGPIPE is never raised (a write to a
 * closed socket or pipe fails with EPIPE). newlib's struct sigaction has sa_handler, sa_mask
 * and sa_flags (no sa_sigaction: an SA_SIGINFO handler gets one argument).
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#ifndef _ONYX_SIGNAL_H
#define _ONYX_SIGNAL_H

#include_next <signal.h>

#ifndef SA_SIGINFO
#define SA_SIGINFO	0x2
#endif
#ifndef SA_ONSTACK
#define SA_ONSTACK	0x4
#endif
#ifndef SA_RESTART
#define SA_RESTART	0x10000000
#endif
#ifndef SA_NODEFER
#define SA_NODEFER	0x40000000
#endif
#ifndef SA_RESETHAND
#define SA_RESETHAND	0x80000000
#endif
#ifndef SIG_HOLD
#define SIG_HOLD	((_sig_func_ptr) 2)
#endif

#endif /* _ONYX_SIGNAL_H */
