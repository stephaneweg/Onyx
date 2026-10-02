//
// transport.hpp -- the byte pipe the WebSocket layer sits on. Abstract so the same core
// runs over plain TCP (local dev ws://127.0.0.1:3000), over TLS (prod wss://taatu.world),
// and over Onyx's kapi sockets + onyx_tls. recv() is NON-BLOCKING, matching kapi/onyx_tls:
//   >0 bytes read, 0 nothing available yet (poll again), <0 closed or error.
// send() writes all `len` bytes (may block briefly); returns len or <0.
//
#ifndef TAATU_TRANSPORT_HPP
#define TAATU_TRANSPORT_HPP

namespace taatu {

struct ITransport
{
    virtual ~ITransport () {}
    virtual bool connect (const char *host, int port) = 0;
    virtual int  send (const void *buf, int len) = 0;   // len, or <0 on error
    virtual int  recv (void *buf, int len) = 0;         // >0 / 0 none-yet / <0 closed
    virtual void close () = 0;
    virtual bool is_tls () const { return false; }
};

} // namespace taatu
#endif
