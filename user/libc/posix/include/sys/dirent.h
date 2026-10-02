/*
 * sys/dirent.h -- the directory entry of Onyx (newlib's <dirent.h> includes this header; newlib's
 * own one only says "<dirent.h> not supported"). libonyxposix dir.c, on the kapi's directory
 * handles (opendir; v75 dir_read: 255-character names). No "." / ".." entries.
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
#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12

#define _DIRENT_HAVE_D_TYPE	1
#define _DIRENT_HAVE_D_RECLEN	1
#define _DIRENT_HAVE_D_OFF	1

#ifndef MAXNAMLEN
#define MAXNAMLEN	255
#endif

struct dirent
{
	ino_t d_ino;				/* 16 bits in newlib: the fold of the kernel's 64-bit id */
	off_t d_off;				/* the entry's position (telldir) */
	unsigned short d_reclen;
	unsigned char d_type;			/* DT_DIR / DT_REG */
	char d_name[256];
};

typedef struct __onyx_dir DIR;

#endif /* _SYS_DIRENT_H */
