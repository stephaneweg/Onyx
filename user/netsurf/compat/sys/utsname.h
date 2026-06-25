/*
 * sys/utsname.h -- minimal shim for the NetSurf port on Onyx (newlib).
 * NetSurf's utils/useragent.c and utils/log.c call uname() to build the User-Agent and
 * log banner. newlib has no <sys/utsname.h>; onyx_compat.c fills in fixed "Onyx" values.
 * Part of the brick-9 platform layer.
 */
#ifndef _ONYX_COMPAT_SYS_UTSNAME_H
#define _ONYX_COMPAT_SYS_UTSNAME_H 1

#define _UTSNAME_LENGTH 65

struct utsname {
	char sysname[_UTSNAME_LENGTH];
	char nodename[_UTSNAME_LENGTH];
	char release[_UTSNAME_LENGTH];
	char version[_UTSNAME_LENGTH];
	char machine[_UTSNAME_LENGTH];
};

int uname(struct utsname *buf);

#endif /* _ONYX_COMPAT_SYS_UTSNAME_H */
