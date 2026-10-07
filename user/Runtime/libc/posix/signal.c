/*
 * signal.c -- signals as Onyx can have them (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * There are no asynchronous signals: the kernel delivers none. A handler table is kept, and a
 * signal the process raises itself (raise, abort, kill (getpid (), sig), pthread_kill (self))
 * runs its handler at once, synchronously. The default actions: SIGCHLD, SIGWINCH, SIGURG and
 * SIGCONT are ignored, every other signal ends the process with status 128 + sig (abort: 134).
 * SIGPIPE is never raised: a write to a closed pipe or socket fails with EPIPE. kill of another
 * process: SIGKILL / SIGTERM (and SIGINT, SIGHUP, SIGQUIT) -> kapi kill_pid (hard / clean),
 * signal 0 -> whether it exists. Masks are kept and change nothing.
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
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "posix_internal.h"

/* <signal.h> defines these as macros: the functions too, for code that takes their address */
#undef sigemptyset
#undef sigfillset
#undef sigaddset
#undef sigdelset
#undef sigismember

static struct sigaction s_act[NSIG];
static sigset_t s_mask;

static int ignored_by_default (int sig)
{
	return sig == SIGCHLD || sig == SIGWINCH || sig == SIGURG || sig == SIGCONT;
}

int __onyx_signal_default (int sig)
{
	if (ignored_by_default (sig))
		return 0;
	_exit (128 + sig);
}

int sigaction (int sig, const struct sigaction *act, struct sigaction *old)
{
	if (sig <= 0 || sig >= NSIG || ((sig == SIGKILL || sig == SIGSTOP) && act))
		return ONYX_ERR (EINVAL);
	if (old)
		*old = s_act[sig];
	if (act)
		s_act[sig] = *act;
	return 0;
}

_sig_func_ptr signal (int sig, _sig_func_ptr fn)
{
	struct sigaction a, old;
	memset (&a, 0, sizeof a);
	a.sa_handler = fn;
	if (sigaction (sig, &a, &old) != 0)
		return SIG_ERR;
	return old.sa_handler;
}

_sig_func_ptr _signal_r (struct _reent *r, int sig, _sig_func_ptr fn) { (void) r; return signal (sig, fn); }

int raise (int sig)
{
	if (sig <= 0 || sig >= NSIG)
		return ONYX_ERR (EINVAL);
	_sig_func_ptr h = s_act[sig].sa_handler;
	if (h == SIG_IGN)
		return 0;
	if (h == SIG_DFL || h == 0)
	{
		__onyx_signal_default (sig);
		return 0;
	}
	if (s_act[sig].sa_flags & SA_RESETHAND)
		s_act[sig].sa_handler = SIG_DFL;
	h (sig);
	return 0;
}

int _raise_r (struct _reent *r, int sig) { (void) r; return raise (sig); }

void abort (void)
{
	raise (SIGABRT);
	_exit (128 + SIGABRT);			/* a handler returned: the end anyway (status 134) */
}

int _kill (int pid, int sig)
{
	if (pid == 0 || pid == getpid () || pid == -1)
		return sig == 0 ? 0 : raise (sig);
	if (pid < 0)
		pid = -pid;				/* (a process group: the process) */
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	if (sig == 0)
	{
		struct kapi_syscall_stats st;
		int r = kapi_proc_stats (pid, &st);
		return r == 0 ? 0 : ONYX_ERR (ESRCH);
	}
	if (sig == SIGKILL || sig == SIGTERM || sig == SIGINT || sig == SIGHUP || sig == SIGQUIT)
	{
		int r = kapi_kill_pid (pid, sig == SIGKILL);
		if (r == 1)
			return 0;
		return ONYX_ERR (r == 0 ? ESRCH : EPERM);
	}
	return ONYX_ERR (ENOTSUP);
}

int kill (pid_t pid, int sig) { return _kill ((int) pid, sig); }
int killpg (pid_t g, int sig) { return _kill (-(int) g, sig); }

int pthread_kill (pthread_t t, int sig)
{
	if (sig < 0 || sig >= NSIG)
		return EINVAL;
	if (t == pthread_self ())
		return sig == 0 ? 0 : (raise (sig) == 0 ? 0 : errno);
	struct __onyx_thread *th = (struct __onyx_thread *) t;
	if (sig == 0)
		return th->magic == ONYX_THREAD_MAGIC && th->state == ONYX_T_RUNNING ? 0 : ESRCH;
	return ENOTSUP;				/* (no asynchronous delivery to another thread) */
}

int sigprocmask (int how, const sigset_t *set, sigset_t *old)
{
	if (old)
		*old = s_mask;
	if (set)
	{
		if (how == SIG_BLOCK)
			s_mask |= *set;
		else if (how == SIG_UNBLOCK)
			s_mask &= ~*set;
		else if (how == SIG_SETMASK)
			s_mask = *set;
		else
			return ONYX_ERR (EINVAL);
	}
	return 0;
}

int pthread_sigmask (int how, const sigset_t *set, sigset_t *old)
{
	return sigprocmask (how, set, old) == 0 ? 0 : errno;
}

int sigemptyset (sigset_t *s) { *s = 0; return 0; }
int sigfillset (sigset_t *s) { *s = ~(sigset_t) 0; return 0; }
int sigaddset (sigset_t *s, int sig)
{
	if (sig <= 0 || sig >= NSIG)
		return ONYX_ERR (EINVAL);
	*s |= (sigset_t) 1 << sig;
	return 0;
}
int sigdelset (sigset_t *s, int sig)
{
	if (sig <= 0 || sig >= NSIG)
		return ONYX_ERR (EINVAL);
	*s &= ~((sigset_t) 1 << sig);
	return 0;
}
int sigismember (const sigset_t *s, int sig)
{
	if (sig <= 0 || sig >= NSIG)
		return ONYX_ERR (EINVAL);
	return (*s >> sig) & 1;
}

int sigpending (sigset_t *s) { *s = 0; return 0; }
int sigsuspend (const sigset_t *s) { (void) s; return ONYX_ERR (ENOSYS); }
int sigwait (const sigset_t *s, int *sig) { (void) s; (void) sig; return ENOSYS; }
int sigaltstack (const stack_t *ss, stack_t *old)
{
	(void) ss;
	if (old)
	{
		memset (old, 0, sizeof *old);
		old->ss_flags = SS_DISABLE;
	}
	return 0;
}
int siginterrupt (int sig, int flag) { (void) sig; (void) flag; return 0; }
int pause (void) { for (;;) __onyx_sleep_ns (1000000000ULL); }
