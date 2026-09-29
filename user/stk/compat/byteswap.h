/* byteswap.h -- glibc's byte swaps, for the SuperTuxKart port (Irrlicht's os.cpp). */
#ifndef _ONYX_BYTESWAP_H
#define _ONYX_BYTESWAP_H
#define bswap_16(x) __builtin_bswap16 (x)
#define bswap_32(x) __builtin_bswap32 (x)
#define bswap_64(x) __builtin_bswap64 (x)
#endif
