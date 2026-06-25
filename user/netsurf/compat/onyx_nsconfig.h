/*
 * onyx_nsconfig.h -- build-time configuration macros for the NetSurf port on Onyx.
 *
 * NetSurf's buildsystem normally generates these (per frontend/options). We bypass the
 * buildsystem (see the Makefile), so this force-included header (-include) supplies the
 * macros the core references. Grows as more core layers are compiled. Part of brick 9.
 */
#ifndef _ONYX_NSCONFIG_H
#define _ONYX_NSCONFIG_H 1

/* newlib's <limits.h> has no PATH_MAX (no filesystem limit defined) */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* logging filters (utils/nsoption.c defaults, normally -D'd by the buildsystem) */
#ifndef NETSURF_BUILTIN_LOG_FILTER
#define NETSURF_BUILTIN_LOG_FILTER "level:WARNING"
#endif
#ifndef NETSURF_BUILTIN_VERBOSE_FILTER
#define NETSURF_BUILTIN_VERBOSE_FILTER "level:VERBOSE"
#endif

/* default homepage + resource/font search paths (frontends/framebuffer/gui.c) */
#ifndef NETSURF_HOMEPAGE
#define NETSURF_HOMEPAGE "about:welcome"
#endif
#ifndef NETSURF_FB_RESPATH
#define NETSURF_FB_RESPATH "/res"
#endif
#ifndef NETSURF_FB_FONTPATH
#define NETSURF_FB_FONTPATH "/res/fonts"
#endif

#endif /* _ONYX_NSCONFIG_H */
