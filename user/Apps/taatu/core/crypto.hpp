//
// crypto.hpp -- the crypto primitives the TAATU core needs, as free functions. Each
// platform provides the implementation: the PC build in platform/pc/crypto_pc.cpp
// (bundled public-domain code), the Onyx build in platform/onyx/crypto_onyx.cpp
// (mbedTLS + kapi_random). The core never calls a TLS/crypto library directly.
//
#ifndef TAATU_CRYPTO_HPP
#define TAATU_CRYPTO_HPP
#include <stddef.h>

namespace taatu { namespace crypto {

// SHA-1 of d[n] -> out[20] (WebSocket handshake accept key).
void sha1 (const unsigned char *d, size_t n, unsigned char out[20]);

// HMAC-SHA256(key[klen], msg[mlen]) -> out[32] (the anti-bot _ui signature).
void hmac_sha256 (const unsigned char *key, size_t klen,
                  const unsigned char *msg, size_t mlen, unsigned char out[32]);

// Standard base64 of d[n] into out (caller-sized, >= 4*((n+2)/3)+1); returns length.
size_t base64 (const unsigned char *d, size_t n, char *out, size_t cap);

// Cryptographic random bytes (WebSocket key, nonces).
void random_bytes (void *out, size_t n);

} } // namespace taatu::crypto
#endif
