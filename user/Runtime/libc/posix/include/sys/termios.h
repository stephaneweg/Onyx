/*
 * sys/termios.h -- terminal attributes (newlib's <termios.h> includes this header, which newlib
 * does not ship for this target). Onyx's console is the terminal app's stream: no line
 * discipline to set, so tcgetattr / tcsetattr fail with ENOTTY (libonyxposix misc.c) and code
 * falls back to plain reads. Linux's values.
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
#ifndef _SYS_TERMIOS_H
#define _SYS_TERMIOS_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

#define NCCS	32
struct termios
{
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};

/* c_cc */
#define VINTR	0
#define VQUIT	1
#define VERASE	2
#define VKILL	3
#define VEOF	4
#define VTIME	5
#define VMIN	6
#define VSTART	8
#define VSTOP	9
#define VSUSP	10
#define VEOL	11
/* c_iflag */
#define IGNBRK	0000001
#define BRKINT	0000002
#define IGNPAR	0000004
#define PARMRK	0000010
#define INPCK	0000020
#define ISTRIP	0000040
#define INLCR	0000100
#define IGNCR	0000200
#define ICRNL	0000400
#define IXON	0002000
#define IXANY	0004000
#define IXOFF	0010000
/* c_oflag */
#define OPOST	0000001
#define ONLCR	0000004
/* c_cflag */
#define CSIZE	0000060
#define CS8	0000060
#define CSTOPB	0000100
#define CREAD	0000200
#define PARENB	0000400
#define HUPCL	0002000
#define CLOCAL	0004000
/* c_lflag */
#define ISIG	0000001
#define ICANON	0000002
#define ECHO	0000010
#define ECHOE	0000020
#define ECHOK	0000040
#define ECHONL	0000100
#define NOFLSH	0000200
#define TOSTOP	0000400
#define IEXTEN	0100000
/* tcsetattr */
#define TCSANOW		0
#define TCSADRAIN	1
#define TCSAFLUSH	2
/* tcflush */
#define TCIFLUSH	0
#define TCOFLUSH	1
#define TCIOFLUSH	2
#define B0	0000000
#define B9600	0000015
#define B38400	0000017
#define B115200	0010002

int tcgetattr (int, struct termios *);
int tcsetattr (int, int, const struct termios *);
int tcflush (int, int);
int tcdrain (int);
speed_t cfgetispeed (const struct termios *);
speed_t cfgetospeed (const struct termios *);
int cfsetispeed (struct termios *, speed_t);
int cfsetospeed (struct termios *, speed_t);
void cfmakeraw (struct termios *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TERMIOS_H */
