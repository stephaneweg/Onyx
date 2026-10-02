//
// transport_tcp.hpp -- plain TCP ITransport for the PC build (Windows winsock / POSIX).
// For local dev the TAATU server listens on ws://127.0.0.1:3000 (no TLS), so this is enough
// to exercise the whole protocol. Production (wss://taatu.world) needs a TLS transport
// (OpenSSL/mbedTLS) -- TODO; the Onyx build already has TLS via onyx_tls.
//
#ifndef TAATU_TRANSPORT_TCP_HPP
#define TAATU_TRANSPORT_TCP_HPP
#include "../../core/transport.hpp"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define TAATU_CLOSesock closesocket
#  define TAATU_WOULDBLOCK (WSAGetLastError () == WSAEWOULDBLOCK)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  define TAATU_CLOSesock ::close
#  define TAATU_WOULDBLOCK (errno == EWOULDBLOCK || errno == EAGAIN)
#endif

namespace taatu {

class PcTcpTransport : public ITransport
{
    long long sock;
    bool up;
public:
    PcTcpTransport () : sock (-1), up (false) {}
    ~PcTcpTransport () { close (); }

    bool connect (const char *host, int port)
    {
#ifdef _WIN32
        static bool wsa = false;
        if (!wsa) { WSADATA w; if (WSAStartup (MAKEWORD (2, 2), &w) != 0) return false; wsa = true; }
#endif
        struct addrinfo hints, *res = 0;
        memset (&hints, 0, sizeof hints);
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        char ports[16]; snprintf (ports, sizeof ports, "%d", port);
        if (getaddrinfo (host, ports, &hints, &res) != 0 || !res) return false;
        long long s = (long long) ::socket (res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s < 0) { freeaddrinfo (res); return false; }
        if (::connect ((int) s, res->ai_addr, (int) res->ai_addrlen) != 0) { freeaddrinfo (res); TAATU_CLOSesock ((int) s); return false; }
        freeaddrinfo (res);
        // TCP_NODELAY: the protocol is latency-sensitive (positions, chat)
        int one = 1; setsockopt ((int) s, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof one);
        // non-blocking
#ifdef _WIN32
        u_long nb = 1; ioctlsocket ((int) s, FIONBIO, &nb);
#else
        int fl = fcntl ((int) s, F_GETFL, 0); fcntl ((int) s, F_SETFL, fl | O_NONBLOCK);
#endif
        sock = s; up = true;
        return true;
    }
    int send (const void *buf, int len)
    {
        if (!up) return -1;
        const char *p = (const char *) buf; int off = 0;
        while (off < len)
        {
            int r = (int) ::send ((int) sock, p + off, len - off, 0);
            if (r > 0) { off += r; continue; }
            if (r < 0 && TAATU_WOULDBLOCK) { plat_nap (); continue; }
            return -1;
        }
        return len;
    }
    int recv (void *buf, int len)
    {
        if (!up) return -1;
        int r = (int) ::recv ((int) sock, (char *) buf, len, 0);
        if (r > 0) return r;
        if (r == 0) return -1;                 // peer closed
        if (TAATU_WOULDBLOCK) return 0;        // nothing available yet
        return -1;
    }
    void close ()
    {
        if (sock >= 0) { TAATU_CLOSesock ((int) sock); sock = -1; }
        up = false;
    }
    static void plat_nap ();                   // tiny sleep (defined in main_pc.cpp)
};

} // namespace taatu
#endif
