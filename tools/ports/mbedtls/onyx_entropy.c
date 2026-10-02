/*
 * onyx_entropy.c -- the entropy source of the POSIX build of mbedTLS (tools/ports/mbedtls): the
 * configuration keeps MBEDTLS_NO_PLATFORM_ENTROPY (mbedTLS's own source wants a Unix or Windows
 * target) and sets MBEDTLS_ENTROPY_HARDWARE_ALT, so this poll -- getrandom, i.e. the kernel's
 * kapi random -- feeds mbedtls_entropy_func, the CTR-DRBG and PSA. Appended to libmbedcrypto.a.
 * (kapi random is today a tick-seeded software generator: user/tls/README.md's security note.)
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
#include <stddef.h>
#include <sys/random.h>

int mbedtls_hardware_poll (void *data, unsigned char *output, size_t len, size_t *olen);

int mbedtls_hardware_poll (void *data, unsigned char *output, size_t len, size_t *olen)
{
	(void) data;
	ssize_t n = getrandom (output, len, 0);
	if (n < 0)
	{
		*olen = 0;
		return -0x003C;			/* MBEDTLS_ERR_ENTROPY_SOURCE_FAILED */
	}
	*olen = (size_t) n;
	return 0;
}
