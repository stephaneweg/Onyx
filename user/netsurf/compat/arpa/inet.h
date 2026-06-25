/*
 * arpa/inet.h -- minimal shim for the NetSurf port on Onyx (newlib). See sys/socket.h.
 * Byte-order helpers + the address-conversion prototypes NetSurf may reference. Part of
 * the brick-9 platform layer.
 */
#ifndef _ONYX_COMPAT_ARPA_INET_H
#define _ONYX_COMPAT_ARPA_INET_H 1

#include <stdint.h>
#include <netinet/in.h>

/* AArch64 is little-endian: network (big-endian) order needs a byte swap. */
static inline uint16_t htons(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static inline uint16_t ntohs(uint16_t x) { return htons(x); }
static inline uint32_t htonl(uint32_t x)
{
	return ((x & 0xFF000000u) >> 24) | ((x & 0x00FF0000u) >> 8) |
	       ((x & 0x0000FF00u) << 8)  | ((x & 0x000000FFu) << 24);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

int inet_aton(const char *cp, struct in_addr *inp);
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

#endif /* _ONYX_COMPAT_ARPA_INET_H */
