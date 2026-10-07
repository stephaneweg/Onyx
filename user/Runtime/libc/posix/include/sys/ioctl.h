/*
 * sys/ioctl.h -- ioctl on Onyx (libonyxposix misc.c): FIONBIO and FIONREAD on any descriptor,
 * TIOCGWINSZ on the console (80 x 25); anything else ENOTTY.
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
#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

#define FIONREAD	0x541B
#define FIONBIO		0x5421
#define FIOCLEX		0x5451
#define FIONCLEX	0x5450
#define TIOCGWINSZ	0x5413
#define TIOCSWINSZ	0x5414
#define SIOCGIFCONF	0x8912
#define SIOCGIFFLAGS	0x8913
#define SIOCGIFADDR	0x8915

struct winsize
{
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

int ioctl (int, unsigned long, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IOCTL_H */
