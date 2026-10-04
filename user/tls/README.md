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
- the upstream source and its built libraries are **vendored** in the repository at
  `third_party/mbedtls-3.6.3` (the `Makefile`'s `MBEDTLS`). **Keep ≥ 3.6.3**: 3.6.3 added
  re-assembly of TLS handshake messages fragmented across records, which real servers (incl.
  Google) do — 3.6.2 fails them with `-0x7080` ("TLS handshake fragmentation not supported").

## Building

1. Build the libraries again only after a change to the config (needs `python3` for
   `scripts/config.py`):

   ```sh
   make -C user/tls
   ```

   → `third_party/mbedtls-3.6.3/library/libmbed{crypto,x509,tls}.a`.

2. Build an HTTPS app: the `user/bin` and `user` makefiles already point at them, e.g.

   ```sh
   make -C user/bin httpsget.elf
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
- **Certificate verification is available** (not the default of `start()`): pass
  `START_VERIFY` after `set_ca_bundle(pem, len)` (the Mozilla bundle is `SD:/res/ca-bundle`)
  -- the chain, the host name (SNI and the SAN names) and the dates against the Onyx clock
  (`kapi_get_datetime`, skipped while the clock is unset: mbedTLS is built without
  `MBEDTLS_HAVE_TIME_DATE`, so a verify callback checks them); `start()` returns -2 for a
  refused certificate and fills a `Verify` record of the chain. `START_ALPN_H2` /
  `START_ALPN_H1` offer ALPN; `sess_export` / `sess_import` keep the session cache across
  launches. Mail (`user/mail/conn.h`)
  use it; the other users (`user/bin` tools, Courier) still connect without verification.

## Config notes

The library is built from mbedTLS's default config with the platform couplings removed
(`scripts/config.py`, see the `Makefile`): no `NET_C`/`FS_IO`/`TIMING_C`, no platform
entropy. TLS 1.2 and TLS 1.3 client: 1.3 runs on PSA crypto, built with
`MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG` -- its random bytes come from
`mbedtls_psa_external_get_random`, defined (weak) in `onyx_tls.hpp` over `kapi_random`, and
`start()` calls `psa_crypto_init` once per app. The usual ECDHE/AES-GCM/ChaCha20/SHA-2
suites and X.509 parsing remain enabled. Regenerating the library needs Python's
`jsonschema` and `jinja2` (the PSA driver wrappers are generated).
