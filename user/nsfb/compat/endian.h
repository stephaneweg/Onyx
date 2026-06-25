/*
 * endian.h -- minimal <endian.h> shim for newlib bare-metal (Onyx).
 *
 * libnsfb's src/plot.h does  #include <endian.h>  and then detects the byte order from
 * GCC's builtin macros (__BYTE_ORDER__, always defined by the compiler). newlib ships no
 * glibc-style <endian.h>, so this shim just satisfies the include and mirrors the builtins
 * under the glibc names. Provided via -I so the vendored libnsfb stays unpatched.
 */
#ifndef _ONYX_COMPAT_ENDIAN_H
#define _ONYX_COMPAT_ENDIAN_H 1

#ifndef __BYTE_ORDER
#define __LITTLE_ENDIAN	__ORDER_LITTLE_ENDIAN__
#define __BIG_ENDIAN	__ORDER_BIG_ENDIAN__
#define __PDP_ENDIAN	__ORDER_PDP_ENDIAN__
#define __BYTE_ORDER	__BYTE_ORDER__
#endif

#ifndef BYTE_ORDER
#define LITTLE_ENDIAN	__LITTLE_ENDIAN
#define BIG_ENDIAN	__BIG_ENDIAN
#define PDP_ENDIAN	__PDP_ENDIAN
#define BYTE_ORDER	__BYTE_ORDER
#endif

#endif /* _ONYX_COMPAT_ENDIAN_H */
