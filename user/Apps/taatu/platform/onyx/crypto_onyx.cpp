//
// crypto_onyx.cpp -- Onyx implementation of taatu::crypto, backed by mbedTLS (already in
// third_party/mbedtls-3.6.3) and the Pi hardware RNG (kapi_random). Links into the Onyx ELF.
//
#include "../../core/crypto.hpp"
#include "kapi.h"
#include "mbedtls/md.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include <string.h>

namespace taatu { namespace crypto {

void sha1 (const unsigned char *d, size_t n, unsigned char out[20])
{
    mbedtls_sha1 (d, n, out);
}

void hmac_sha256 (const unsigned char *key, size_t klen, const unsigned char *msg, size_t mlen, unsigned char out[32])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type (MBEDTLS_MD_SHA256);
    mbedtls_md_hmac (info, key, klen, msg, mlen, out);
}

size_t base64 (const unsigned char *d, size_t n, char *out, size_t cap)
{
    size_t olen = 0;
    if (mbedtls_base64_encode ((unsigned char *) out, cap, &olen, d, n) != 0) { if (cap) out[0] = 0; return 0; }
    return olen;
}

void random_bytes (void *out, size_t n)
{
    int got = kapi_random (out, (unsigned) n);
    // top up if the HW RNG returned short (very rare)
    unsigned char *p = (unsigned char *) out;
    for (int i = got < 0 ? 0 : got; i < (int) n; i++) p[i] = (unsigned char) (kapi_get_ticks () * 1103515245u + i);
}

} } // namespace taatu::crypto
