/*
 * syslog.h -- syslog on Onyx: the message goes to the kernel log (kmsg) through stderr's
 * neighbour, the kapi write of fd 2 (libonyxposix misc.c).
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
#ifndef _SYSLOG_H
#define _SYSLOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_EMERG	0
#define LOG_ALERT	1
#define LOG_CRIT	2
#define LOG_ERR		3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	6
#define LOG_DEBUG	7

#define LOG_KERN	(0 << 3)
#define LOG_USER	(1 << 3)
#define LOG_DAEMON	(3 << 3)
#define LOG_LOCAL0	(16 << 3)

#define LOG_PID		0x01
#define LOG_CONS	0x02
#define LOG_NDELAY	0x08
#define LOG_PERROR	0x20

#define LOG_MASK(p)	(1 << (p))
#define LOG_UPTO(p)	((1 << ((p) + 1)) - 1)

void openlog (const char *, int, int);
void closelog (void);
int setlogmask (int);
void syslog (int, const char *, ...) __attribute__ ((__format__ (__printf__, 2, 3)));
void vsyslog (int, const char *, va_list);

#ifdef __cplusplus
}
#endif

#endif /* _SYSLOG_H */
