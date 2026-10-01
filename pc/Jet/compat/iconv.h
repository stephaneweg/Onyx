/*
 * pc/Jet/compat/iconv.h -- iconv for Jet Browser's Windows build: MinGW has none. As on the Pi
 * (user/netsurf/compat/onyx_compat.c): a passthrough (pc/Jet/winkapi.cpp) -- right for UTF-8, the pages'
 * own decoding being libparserutils' codecs.
 */
#ifndef JET_COMPAT_ICONV_H
#define JET_COMPAT_ICONV_H
#include <stddef.h>
typedef void *iconv_t;
#ifdef __cplusplus
extern "C" {
#endif
iconv_t iconv_open(const char *to, const char *from);
int iconv_close(iconv_t cd);
size_t iconv(iconv_t cd, char **inbuf, size_t *inleft, char **outbuf, size_t *outleft);
#ifdef __cplusplus
}
#endif
#endif
