# Onyx TLS transport (HTTPS)

`onyx_tls.hpp` is the TLS backend for the reusable `HttpClient` (`user/http.hpp`),
built on **mbedTLS** over the kapi TCP sockets. It is how Onyx apps speak `https://`.

NetSurf and other browsers get HTTPS the same way — from an external TLS stack
(libcurl uses OpenSSL); they implement no crypto themselves. Onyx does the equivalent
here: mbedTLS plugged onto our transport primitives (`kapi_tcp_send`/`recv`).

## Layout

- `onyx_tls.hpp` — header-only glue: a `Session`, BIO send/recv → kapi TCP, a
  non-blocking handshake/read/write loop (polls with `kapi_msleep`), and the entropy
  hook. Used by `http.hpp` when `ONYX_HTTP_TLS` is defined.
- `Makefile` — cross-builds the upstream mbedTLS library for `aarch64-none-elf` +
  newlib (bare-metal, TLS 1.2).
- `mbedtls/` — the upstream source (a git submodule, **not** committed here).

## Building

1. Add the mbedTLS source (once), pinned to the tested release. **Use ≥ 3.6.3**:
   3.6.3 added re-assembly of TLS handshake messages fragmented across records, which
   real servers (incl. Google) do — 3.6.2 fails them with `-0x7080` ("TLS handshake
   fragmentation not supported").

   ```sh
   git submodule add https://github.com/Mbed-TLS/mbedtls user/tls/mbedtls
   git -C user/tls/mbedtls checkout mbedtls-3.6.3
   ```

   (A plain `git clone --branch mbedtls-3.6.3` into `user/tls/mbedtls` also works.)

2. Build the libraries (needs `python3` for `scripts/config.py`):

   ```sh
   make -C user/tls
   ```

   → `user/tls/mbedtls/library/libmbed{crypto,x509,tls}.a`.

3. Build an HTTPS app, pointing it at the libs:

   ```sh
   make -C user/bin MBEDTLS_DIR=../tls/mbedtls httpsget.elf
   ```

   `httpsget` is a demo; any newlib C++ app can do the same — `#define ONYX_HTTP_TLS`
   before `#include "http.hpp"`, then link `-lmbedtls -lmbedx509 -lmbedcrypto`.

## Security status (read before trusting this)

This is a **functional** TLS bring-up, **not yet secure**:

- **Entropy is weak.** `onyx_tls.hpp`'s `onyx_entropy()` now draws from the kernel via
  `kapi_random` (ABI v30) — but `kapi_random` is itself a tick-seeded software PRNG
  (splitmix64 over `CTimer` ticks), enough to complete a handshake, **not**
  cryptographically strong. It is *not* the Pi 4 hardware RNG: both Circle's legacy
  `CBcmRandomNumberGenerator` (BCM2835) and the BCM2711 RNG200 block stall the bus on
  this SoC and hang the kernel. Strengthening entropy is a kernel-side change to
  `kapi_random` only — `onyx_entropy()` stays as is.
- **Certificate verification is OFF** (`MBEDTLS_SSL_VERIFY_NONE`): the server identity
  is not checked, so this is open to man-in-the-middle. To fix: ship a CA bundle on the
  SD card, `mbedtls_x509_crt_parse` it, `mbedtls_ssl_conf_ca_chain`, and switch to
  `MBEDTLS_SSL_VERIFY_REQUIRED`.

## Config notes

The library is built from mbedTLS's default config with the platform couplings removed
(`scripts/config.py`, see the `Makefile`): no `NET_C`/`FS_IO`/`TIMING_C`, no platform
entropy, and TLS 1.3 disabled (it would pull the PSA RNG plumbing). TLS 1.2 client with
the usual ECDHE/AES-GCM/SHA-2 suites and X.509 parsing remains enabled.
