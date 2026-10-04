/*
 * config.h -- Onyx: nghttp2's configuration, written by hand (no autotools / CMake run): the
 * library alone, built by tools/ports/common.sh for the POSIX ports (the Onyx toolchain).
 *
 */
#ifndef ONYX_NGHTTP2_CONFIG_H
#define ONYX_NGHTTP2_CONFIG_H

#include <stdint.h>

#if defined(__linux__) || defined(__APPLE__)
/* the PC: the C library's byte order functions */
#  define HAVE_ARPA_INET_H 1
#  define HAVE_NETINET_IN_H 1
#else
/* newlib (Onyx): no <arpa/inet.h>; the Pi is little-endian */
#  if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#    error "nghttp2's config.h: a little-endian target is assumed"
#  endif
#  define htonl(x) __builtin_bswap32((uint32_t) (x))
#  define ntohl(x) __builtin_bswap32((uint32_t) (x))
#  define htons(x) __builtin_bswap16((uint16_t) (x))
#  define ntohs(x) __builtin_bswap16((uint16_t) (x))
#endif

#endif /* ONYX_NGHTTP2_CONFIG_H */
