/*
 * proc.c -- processes: getpid, posix_spawn(p), waitpid (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * Onyx has no fork / exec: a program starts another with kapi spawn_ex (v75: argv and envp
 * blocks, a working directory, stdin / stdout stream handles) and collects it with proc_wait
 * (the exit code and why it ended). posix_spawn maps its file actions onto the two streams the
 * kernel passes: a pipe end (or another stream) dup2'ed onto 0 or 1, or a file opened onto them
 * (kapi file_in / file_out); stderr is the child's stdout; closes are ignored.
 * The pid posix_spawn returns is the child's process id when the kernel reports it (proc_wait,
 * KAPI_WAIT_NOHANG | KAPI_WAIT_KEEP), else the value of its process handle; waitpid knows both.
 * waitpid's status: an exit -> code << 8 (WIFEXITED); a fault -> SIGSEGV, killed or out of
 * memory -> SIGKILL (WIFSIGNALED).
 * On a kernel without spawn_ex / proc_wait: kapi spawn (the arguments joined into one line, an
 * argument holding spaces in double quotes) and kapi wait / proc_done.
 * fork, the exec family, system and popen fail with ENOSYS.
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "posix_internal.h"

/* ---- ids ---- */
static int s_pid;

int _getpid (void)
{
	if (s_pid == 0 && kapi__core () == 0)
	{
		int p = kapi_getpid (0);
		s_pid = p > 0 ? p : 1;
	}
	return s_pid ? s_pid : 1;
}
pid_t getpid (void) { return (pid_t) _getpid (); }
pid_t getppid (void)
{
	int p = kapi__core () == 0 ? kapi_getpid (1) : -1;
	return p > 0 ? (pid_t) p : 1;
}
pid_t getpgrp (void) { return getpid (); }
pid_t getpgid (pid_t p) { return p ? p : getpid (); }
pid_t getsid (pid_t p) { return p ? p : getpid (); }
pid_t setsid (void) { return getpid (); }
int setpgid (pid_t a, pid_t b) { (void) a; (void) b; return 0; }

/* ---- the children ---- */
struct child
{
	struct child *next;
	void *h;				/* the process handle */
	int pid;				/* what posix_spawn returned */
	int legacy;				/* spawned with the old kapi spawn */
};
static struct child *s_children;
static volatile unsigned s_childLock;

struct onyx_spawn_actions			/* posix_spawn_file_actions_t points at one */
{
	void *in, *out;				/* stream handles for the child's 0 / 1 (0: inherited) */
	int in_owned, out_owned;		/* opened here (file_in / file_out): closed after spawn */
	char *cwd;
};

/* newlib declares these types as pointers to its (absent) structures: ours stand in */
int posix_spawn_file_actions_init (posix_spawn_file_actions_t *fa)
{
	struct onyx_spawn_actions *a = (struct onyx_spawn_actions *) calloc (1, sizeof *a);
	if (a == 0)
		return ENOMEM;
	*fa = (posix_spawn_file_actions_t) a;
	return 0;
}

int posix_spawn_file_actions_destroy (posix_spawn_file_actions_t *fa)
{
	struct onyx_spawn_actions *a = (struct onyx_spawn_actions *) *fa;
	if (a)
	{
		if (a->in_owned && a->in)
			kapi_stream_close (a->in);
		if (a->out_owned && a->out)
			kapi_stream_close (a->out);
		free (a->cwd);
		free (a);
	}
	*fa = 0;
	return 0;
}

static void *fd_stream (int fd, int want_read)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return 0;
	void *h = 0;
	if (d->type == ONYX_FD_PIPE_R || d->type == ONYX_FD_PIPE_W)
		h = d->pipe->h;
	else if (d->type == ONYX_FD_STREAM)
		h = d->stream;
	else if (d->type == ONYX_FD_CONSOLE)
		h = want_read ? kapi_stdin () : kapi_stdout ();
	__onyx_fd_put (d);
	return h;
}

int posix_spawn_file_actions_adddup2 (posix_spawn_file_actions_t *fa, int fd, int newfd)
{
	struct onyx_spawn_actions *a = (struct onyx_spawn_actions *) *fa;
	if (a == 0)
		return EINVAL;
	if (newfd == 0 || newfd == 1 || newfd == 2)
	{
		void *h = fd_stream (fd, newfd == 0);
		if (h == 0)
			return EBADF;
		if (newfd == 0)
			a->in = h;
		else if (newfd == 1)
			a->out = h;
		/* (2: the child's stderr is its stdout) */
	}
	return 0;
}

int posix_spawn_file_actions_addopen (posix_spawn_file_actions_t *__restrict fa, int fd,
				      const char *__restrict path, int flags, mode_t mode)
{
	(void) mode;
	struct onyx_spawn_actions *a = (struct onyx_spawn_actions *) *fa;
	if (a == 0)
		return EINVAL;
	char p[ONYX_PATH_MAX];
	if (__onyx_path (path, p, sizeof p) == 0)
		return errno;
	if (fd == 0)
	{
		a->in = kapi_file_in (p);
		a->in_owned = a->in != 0;
		return a->in ? 0 : ENOENT;
	}
	if (fd == 1 || fd == 2)
	{
		a->out = kapi_file_out (p, (flags & O_APPEND) != 0);
		a->out_owned = a->out != 0;
		return a->out ? 0 : EACCES;
	}
	return 0;
}

int posix_spawn_file_actions_addclose (posix_spawn_file_actions_t *fa, int fd) { (void) fa; (void) fd; return 0; }

int posix_spawn_file_actions_addchdir_np (posix_spawn_file_actions_t *__restrict fa, const char *__restrict path)
{
	struct onyx_spawn_actions *a = (struct onyx_spawn_actions *) *fa;
	if (a == 0)
		return EINVAL;
	free (a->cwd);
	a->cwd = strdup (path);
	return a->cwd ? 0 : ENOMEM;
}

int posix_spawn_file_actions_addfchdir_np (posix_spawn_file_actions_t *fa, int fd)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return EBADF;
	int r = d->type == ONYX_FD_DIR ? posix_spawn_file_actions_addchdir_np (fa, d->path) : ENOTDIR;
	__onyx_fd_put (d);
	return r;
}

/* the attributes: kept, none applies (one process group, no signal masks) */
struct onyx_spawn_attr { short flags; };
int posix_spawnattr_init (posix_spawnattr_t *at)
{
	struct onyx_spawn_attr *a = (struct onyx_spawn_attr *) calloc (1, sizeof *a);
	if (a == 0)
		return ENOMEM;
	*at = (posix_spawnattr_t) a;
	return 0;
}
int posix_spawnattr_destroy (posix_spawnattr_t *at) { free (*at); *at = 0; return 0; }
int posix_spawnattr_setflags (posix_spawnattr_t *at, short f) { ((struct onyx_spawn_attr *) *at)->flags = f; return 0; }
int posix_spawnattr_getflags (const posix_spawnattr_t *at, short *f) { *f = ((struct onyx_spawn_attr *) *at)->flags; return 0; }
int posix_spawnattr_setsigmask (posix_spawnattr_t *at, const sigset_t *s) { (void) at; (void) s; return 0; }
int posix_spawnattr_getsigmask (const posix_spawnattr_t *at, sigset_t *s) { (void) at; *s = 0; return 0; }
int posix_spawnattr_setsigdefault (posix_spawnattr_t *at, const sigset_t *s) { (void) at; (void) s; return 0; }
int posix_spawnattr_getsigdefault (const posix_spawnattr_t *at, sigset_t *s) { (void) at; *s = 0; return 0; }
int posix_spawnattr_setpgroup (posix_spawnattr_t *at, pid_t p) { (void) at; (void) p; return 0; }
int posix_spawnattr_getpgroup (const posix_spawnattr_t *at, pid_t *p) { (void) at; *p = 0; return 0; }
int posix_spawnattr_setschedpolicy (posix_spawnattr_t *at, int p) { (void) at; (void) p; return 0; }
int posix_spawnattr_getschedpolicy (const posix_spawnattr_t *at, int *p) { (void) at; *p = SCHED_OTHER; return 0; }
int posix_spawnattr_setschedparam (posix_spawnattr_t *at, const struct sched_param *p) { (void) at; (void) p; return 0; }
int posix_spawnattr_getschedparam (const posix_spawnattr_t *at, struct sched_param *p) { (void) at; memset (p, 0, sizeof *p); return 0; }

/* a vector of strings -> a block "a\0b\0\0" */
static char *make_block (char *const *v, size_t *len)
{
	size_t n = 1;
	for (int i = 0; v && v[i]; i++)
		n += strlen (v[i]) + 1;
	char *b = (char *) malloc (n);
	if (b == 0)
		return 0;
	size_t o = 0;
	for (int i = 0; v && v[i]; i++)
	{
		size_t l = strlen (v[i]) + 1;
		memcpy (b + o, v[i], l);
		o += l;
	}
	b[o++] = '\0';
	if (len)
		*len = o;
	return b;
}

/* argv[1..] as one line (the old spawn) */
static char *make_line (char *const *argv)
{
	size_t n = 1;
	for (int i = 1; argv && argv[i]; i++)
		n += strlen (argv[i]) + 3;
	char *b = (char *) malloc (n);
	if (b == 0)
		return 0;
	b[0] = '\0';
	for (int i = 1; argv && argv[i]; i++)
	{
		if (i > 1)
			strcat (b, " ");
		int q = strchr (argv[i], ' ') != 0 || argv[i][0] == '\0';
		if (q)
			strcat (b, "\"");
		strcat (b, argv[i]);
		if (q)
			strcat (b, "\"");
	}
	return b;
}

static int do_spawn (pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa,
		     char *const argv[], char *const envp[])
{
	if (kapi__core () != 0)
		return ENOSYS;
	char p[ONYX_PATH_MAX];
	if (__onyx_path (path, p, sizeof p) == 0)
		return errno;
	struct onyx_spawn_actions *a = fa ? (struct onyx_spawn_actions *) *fa : 0;
	struct child *c = (struct child *) calloc (1, sizeof *c);
	if (c == 0)
		return ENOMEM;

	char *ab = make_block (argv, 0);
	char *eb = envp ? make_block (envp, 0) : 0;
	struct kapi_spawn_attr sa;
	memset (&sa, 0, sizeof sa);
	sa.path = p;
	sa.argv = ab;
	sa.envp = eb;
	sa.cwd = a ? a->cwd : 0;
	sa.in = a ? a->in : 0;
	sa.out = a ? a->out : 0;
	long long h = ab ? kapi_spawn_ex (&sa) : -KAPI_ENOMEM;
	free (ab);
	free (eb);
	if (h == -KAPI_ENOSYS)
	{
		char *line = make_line (argv);
		h = line ? (long long) (unsigned long) kapi_spawn (p, line, sa.in, sa.out) : 0;
		free (line);
		if (h == 0)
		{
			free (c);
			struct stat st;
			return __onyx_path_stat (p, &st) == 0 ? ENOEXEC : ENOENT;
		}
		c->legacy = 1;
	}
	else if (h < 0)
	{
		free (c);
		return (int) -h;
	}
	c->h = (void *) (unsigned long) h;
	c->pid = (int) (h & 0x7FFFFFFF);
	if (!c->legacy)
	{
		struct kapi_proc_status st;
		memset (&st, 0, sizeof st);
		if (kapi_proc_wait (c->h, KAPI_WAIT_NOHANG | KAPI_WAIT_KEEP, &st) >= 0 && st.pid > 0)
			c->pid = st.pid;
	}
	__onyx_lock (&s_childLock);
	c->next = s_children;
	s_children = c;
	__onyx_unlock (&s_childLock);
	if (pid)
		*pid = c->pid;
	return 0;
}

int posix_spawn (pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa,
		 const posix_spawnattr_t *attr, char *const argv[], char *const envp[])
{
	(void) attr;
	return do_spawn (pid, path, fa, argv, envp);
}

int posix_spawnp (pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa,
		  const posix_spawnattr_t *attr, char *const argv[], char *const envp[])
{
	(void) attr;
	if (strchr (file, '/') || strchr (file, ':'))
		return do_spawn (pid, file, fa, argv, envp);
	const char *path = getenv ("PATH");
	if (path == 0 || *path == '\0')
		path = "SD:/bin";
	/* PATH entries: ';' separates them; ':' too, unless it ends a volume name ("SD:/bin") */
	char dir[ONYX_PATH_MAX];
	const char *s = path;
	while (*s)
	{
		size_t n = 0;
		while (s[n] && s[n] != ';' && !(s[n] == ':' && s[n + 1] != '/' && s[n + 1] != '\0'))
			n++;
		if (n > 0 && n < sizeof dir - 2)
		{
			char cand[ONYX_PATH_MAX * 2 + 2];
			memcpy (dir, s, n);
			dir[n] = '\0';
			snprintf (cand, sizeof cand, "%s%s%s", dir, dir[n - 1] == '/' ? "" : "/", file);
			struct stat st;
			char p[ONYX_PATH_MAX];
			if (__onyx_path (cand, p, sizeof p) && __onyx_path_stat (p, &st) == 0 && S_ISREG (st.st_mode))
				return do_spawn (pid, cand, fa, argv, envp);
		}
		s += n;
		if (*s)
			s++;
	}
	return ENOENT;
}

static int encode (int code, int reason)
{
	switch (reason)
	{
	case KAPI_PROC_FAULT:	return SIGSEGV;
	case KAPI_PROC_KILLED:
	case KAPI_PROC_OOM:	return SIGKILL;
	default:		return (code & 0xFF) << 8;
	}
}

/* one child: 1 ended (*status), 0 running, -1 error */
static int reap (struct child *c, int nohang, int *status)
{
	if (c->legacy)
	{
		if (nohang && !kapi_proc_done (c->h))
			return 0;
		int code = kapi_wait (c->h);
		*status = encode (code, KAPI_PROC_EXITED);
		return 1;
	}
	struct kapi_proc_status st;
	memset (&st, 0, sizeof st);
	int r = kapi_proc_wait (c->h, nohang ? KAPI_WAIT_NOHANG : 0, &st);
	if (r == 1)
	{
		*status = encode (st.code, st.reason);
		return 1;
	}
	if (r == 0)
		return 0;
	errno = -r;
	return -1;
}

static void forget (struct child *c)
{
	__onyx_lock (&s_childLock);
	for (struct child **pp = &s_children; *pp; pp = &(*pp)->next)
		if (*pp == c)
		{
			*pp = c->next;
			break;
		}
	__onyx_unlock (&s_childLock);
	free (c);
}

pid_t waitpid (pid_t pid, int *status, int options)
{
	int st = 0;
	int nohang = (options & WNOHANG) != 0;
	if (pid > 0)
	{
		struct child *c = 0;
		__onyx_lock (&s_childLock);
		for (c = s_children; c; c = c->next)
			if (c->pid == pid || (int) (unsigned long) c->h == pid)
				break;
		__onyx_unlock (&s_childLock);
		if (c == 0)
			return ONYX_ERR (ECHILD);
		int r = reap (c, nohang, &st);
		if (r <= 0)
			return r;
		int id = c->pid;
		forget (c);
		if (status)
			*status = st;
		return id;
	}
	/* any child */
	for (;;)
	{
		struct child *list[64];
		int n = 0;
		__onyx_lock (&s_childLock);
		for (struct child *c = s_children; c && n < 64; c = c->next)
			list[n++] = c;
		__onyx_unlock (&s_childLock);
		if (n == 0)
			return ONYX_ERR (ECHILD);
		for (int i = 0; i < n; i++)
			if (reap (list[i], 1, &st) == 1)
			{
				int id = list[i]->pid;
				forget (list[i]);
				if (status)
					*status = st;
				return id;
			}
		if (nohang)
			return 0;
		__onyx_sleep_ns (10000000ULL);		/* (no "any child" wait in the kernel) */
	}
}

pid_t wait (int *status) { return waitpid (-1, status, 0); }
int _wait (int *status) { return (int) wait (status); }

/* ---- what Onyx does not have ---- */
int _fork (void) { return ONYX_ERR (ENOSYS); }
pid_t fork (void) { return ONYX_ERR (ENOSYS); }
pid_t vfork (void) { return ONYX_ERR (ENOSYS); }
int _execve (const char *p, char *const a[], char *const e[]) { (void) p; (void) a; (void) e; return ONYX_ERR (ENOSYS); }
int execve (const char *p, char *const a[], char *const e[]) { return _execve (p, a, e); }
int execv (const char *p, char *const a[]) { return _execve (p, a, 0); }
int execvp (const char *p, char *const a[]) { return _execve (p, a, 0); }
int execvpe (const char *p, char *const a[], char *const e[]) { return _execve (p, a, e); }
int execl (const char *p, const char *a, ...) { (void) a; return _execve (p, 0, 0); }
int execlp (const char *p, const char *a, ...) { (void) a; return _execve (p, 0, 0); }
int execle (const char *p, const char *a, ...) { (void) a; return _execve (p, 0, 0); }
int system (const char *cmd) { if (cmd == 0) return 0; return ONYX_ERR (ENOSYS); }
FILE *popen (const char *cmd, const char *mode) { (void) cmd; (void) mode; errno = ENOSYS; return 0; }
int pclose (FILE *f) { (void) f; return ONYX_ERR (ECHILD); }
