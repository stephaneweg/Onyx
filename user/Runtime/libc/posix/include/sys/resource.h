/*
 * sys/resource.h -- resource limits and usage on Onyx (libonyxposix misc.c). Replaces newlib's,
 * which has no rlimit and a two-field rusage (and no getrusage in its library).
 *
 * getrlimit: RLIMIT_STACK from the main thread's stack (kapi thread_info), NOFILE 1024 (the
 * descriptor table), AS / DATA / RSS unlimited. setrlimit accepts what does not raise them.
 * getrusage: the times are the monotonic clock's (no per-task CPU accounting yet), ru_maxrss
 * from vm_stats.
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
#ifndef _SYS_RESOURCE_H_
#define _SYS_RESOURCE_H_

#include <sys/types.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RUSAGE_SELF	0
#define RUSAGE_CHILDREN	(-1)
#define RUSAGE_THREAD	1

struct rusage
{
	struct timeval ru_utime;
	struct timeval ru_stime;
	long ru_maxrss;				/* KB */
	long ru_ixrss, ru_idrss, ru_isrss;
	long ru_minflt, ru_majflt, ru_nswap;
	long ru_inblock, ru_oublock;
	long ru_msgsnd, ru_msgrcv, ru_nsignals;
	long ru_nvcsw, ru_nivcsw;
};

typedef unsigned long long rlim_t;
#define RLIM_INFINITY	(~0ULL)
#define RLIM_SAVED_MAX	RLIM_INFINITY
#define RLIM_SAVED_CUR	RLIM_INFINITY

struct rlimit
{
	rlim_t rlim_cur;
	rlim_t rlim_max;
};

#define RLIMIT_CPU	0
#define RLIMIT_FSIZE	1
#define RLIMIT_DATA	2
#define RLIMIT_STACK	3
#define RLIMIT_CORE	4
#define RLIMIT_RSS	5
#define RLIMIT_NPROC	6
#define RLIMIT_NOFILE	7
#define RLIMIT_MEMLOCK	8
#define RLIMIT_AS	9
#define RLIM_NLIMITS	16

#define PRIO_PROCESS	0
#define PRIO_PGRP	1
#define PRIO_USER	2

int getrusage (int, struct rusage *);
int getrlimit (int, struct rlimit *);
int setrlimit (int, const struct rlimit *);
int getpriority (int, id_t);
int setpriority (int, id_t, int);

#ifdef __cplusplus
}
#endif

#endif /* !_SYS_RESOURCE_H_ */
