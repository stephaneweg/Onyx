//
// transport_onyx.hpp -- ITransport for Onyx: kapi TCP sockets, optionally wrapped in TLS
// (onyx_tls over mbedTLS). Plain for local dev (ws://host:3000); TLS for prod
// (wss://taatu.world:443) with certificate verification against SD:/res/ca-bundle.
//
#ifndef TAATU_TRANSPORT_ONYX_HPP
#define TAATU_TRANSPORT_ONYX_HPP
#include "../../core/transport.hpp"
#include "kapi.h"
#define ONYX_TLS                       // enable the TLS session helpers
#include "tls/onyx_tls.hpp"
#include <stdlib.h>
#include <string.h>

namespace taatu {

// load SD:/res/ca-bundle once into onyx_tls' trust store
inline void onyx_load_ca_once ()
{
    static bool done = false;
    if (done) return; done = true;
    void *h = kapi_open ("SD:/res/ca-bundle");
    if (!h) return;
    unsigned sz = kapi_fsize (h);
    unsigned char *pem = (unsigned char *) malloc (sz + 1);
    if (pem) { int n = kapi_read (h, pem, sz); if (n < 0) n = 0; pem[n] = 0; onyx_tls::set_ca_bundle (pem, (size_t) n + 1); }
    kapi_close (h);
}

class OnyxTransport : public ITransport
{
    int sock; bool tls_; bool up;
    onyx_tls::Session sess;
public:
    volatile int *cancel;
    OnyxTransport (bool use_tls) : sock (-1), tls_ (use_tls), up (false), cancel (0) {}
    ~OnyxTransport () { close (); }

    bool is_tls () const { return tls_; }

    bool connect (const char *host, int port)
    {
        char ip[40];
        if (!kapi_net_status (ip, sizeof ip)) return false;    // link down
        sock = kapi_tcp_connect (host, (unsigned) port);
        if (sock < 0) return false;
        if (tls_)
        {
            onyx_load_ca_once ();
            memset (&sess, 0, sizeof sess);
            sess.cancel = cancel;
            if (onyx_tls::start (sess, sock, host, onyx_tls::START_VERIFY, 0) != 0)
            { kapi_tcp_close (sock); sock = -1; return false; }
        }
        up = true;
        return true;
    }
    int send (const void *buf, int len)
    {
        if (!up) return -1;
        if (tls_) return onyx_tls::send (sess, buf, len);
        const char *p = (const char *) buf; int off = 0;
        while (off < len)
        {
            int r = kapi_tcp_send (sock, p + off, (unsigned) (len - off));
            if (r > 0) { off += r; continue; }
            if (r == 0) { kapi_msleep (2); continue; }
            return -1;
        }
        return len;
    }
    int recv (void *buf, int len)
    {
        if (!up) return -1;
        if (tls_) return onyx_tls::recv (sess, buf, len);
        return kapi_tcp_recv (sock, buf, (unsigned) len);
    }
    void close ()
    {
        if (up && tls_) onyx_tls::stop (sess);
        if (sock >= 0) kapi_tcp_close (sock);
        sock = -1; up = false;
    }
};

} // namespace taatu
#endif
