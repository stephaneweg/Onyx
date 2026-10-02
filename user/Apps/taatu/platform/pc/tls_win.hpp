//
// tls_win.hpp -- a TLS ITransport for the Windows PC build, using Schannel (SSPI) so there
// is no external dependency (no OpenSSL/mbedTLS). Lets taatu_pc talk to prod over
// wss://taatu.world:443 and https for the REST login. Certificate validation uses the
// Windows cert store (SNI + name check via pszTargetName). recv() keeps the ITransport
// contract: >0 decrypted bytes, 0 nothing yet, <0 closed.
//
#ifndef TAATU_TLS_WIN_HPP
#define TAATU_TLS_WIN_HPP
#ifdef _WIN32
#include "../../core/transport.hpp"
#include "../../core/buf.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <sspi.h>
#include <string.h>

namespace taatu {

class WinTlsTransport : public ITransport
{
    SOCKET sock;
    CredHandle cred; CtxtHandle ctx;
    bool haveCred, haveCtx, up;
    SecPkgContext_StreamSizes sizes;
    Buf encin;     // raw bytes received, not yet decrypted
    Buf dec;       // decrypted plaintext not yet handed to the caller
    int  decoff;
public:
    WinTlsTransport () : sock (INVALID_SOCKET), haveCred (false), haveCtx (false), up (false), decoff (0) { memset (&sizes, 0, sizeof sizes); }
    ~WinTlsTransport () { close (); }
    bool is_tls () const { return true; }

    bool connect (const char *host, int port)
    {
        static bool wsa = false;
        if (!wsa) { WSADATA w; if (WSAStartup (MAKEWORD (2, 2), &w)) return false; wsa = true; }
        if (!tcp_connect (host, port)) return false;
        if (!acquire_creds ()) { closesock (); return false; }
        if (!handshake (host)) { closesock (); return false; }
        if (QueryContextAttributes (&ctx, SECPKG_ATTR_STREAM_SIZES, &sizes) != SEC_E_OK) return false;
        // short receive timeout so recv() can report "nothing yet"
        DWORD to = 15; setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (const char *) &to, sizeof to);
        up = true;
        return true;
    }

    int send (const void *buf, int len)
    {
        if (!up) return -1;
        const char *p = (const char *) buf; int off = 0;
        char *msg = (char *) malloc (sizes.cbHeader + sizes.cbMaximumMessage + sizes.cbTrailer);
        if (!msg) return -1;
        while (off < len)
        {
            int chunk = len - off; if (chunk > (int) sizes.cbMaximumMessage) chunk = sizes.cbMaximumMessage;
            memcpy (msg + sizes.cbHeader, p + off, chunk);
            SecBuffer b[4];
            b[0].BufferType = SECBUFFER_STREAM_HEADER; b[0].pvBuffer = msg;                                   b[0].cbBuffer = sizes.cbHeader;
            b[1].BufferType = SECBUFFER_DATA;          b[1].pvBuffer = msg + sizes.cbHeader;                   b[1].cbBuffer = chunk;
            b[2].BufferType = SECBUFFER_STREAM_TRAILER;b[2].pvBuffer = msg + sizes.cbHeader + chunk;            b[2].cbBuffer = sizes.cbTrailer;
            b[3].BufferType = SECBUFFER_EMPTY;         b[3].pvBuffer = 0;                                      b[3].cbBuffer = 0;
            SecBufferDesc d; d.ulVersion = SECBUFFER_VERSION; d.cBuffers = 4; d.pBuffers = b;
            if (EncryptMessage (&ctx, 0, &d, 0) != SEC_E_OK) { free (msg); return -1; }
            int total = b[0].cbBuffer + b[1].cbBuffer + b[2].cbBuffer;
            if (!send_all (msg, total)) { free (msg); return -1; }
            off += chunk;
        }
        free (msg);
        return len;
    }

    int recv (void *buf, int len)
    {
        if (!up) return -1;
        // hand back leftover plaintext first
        if (decoff < dec.n) { int c = dec.n - decoff; if (c > len) c = len; memcpy (buf, dec.p + decoff, c); decoff += c; if (decoff >= dec.n) { dec.clear (); decoff = 0; } return c; }
        for (;;)
        {
            if (encin.n > 0)
            {
                int r = try_decrypt ();
                if (r == 1) { int c = dec.n - decoff; if (c > len) c = len; memcpy (buf, dec.p + decoff, c); decoff += c; if (decoff >= dec.n) { dec.clear (); decoff = 0; } return c; }
                if (r < 0) { up = false; return -1; }
                // r == 0: need more bytes
            }
            char tmp[8192];
            int n = ::recv (sock, tmp, sizeof tmp, 0);
            if (n > 0) { encin.add (tmp, n); continue; }
            if (n == 0) { up = false; return -1; }
            if (WSAGetLastError () == WSAETIMEDOUT || WSAGetLastError () == WSAEWOULDBLOCK) return 0;   // nothing yet
            up = false; return -1;
        }
    }

    void close ()
    {
        if (haveCtx) { DeleteSecurityContext (&ctx); haveCtx = false; }
        if (haveCred) { FreeCredentialsHandle (&cred); haveCred = false; }
        closesock ();
        up = false;
    }

private:
    void closesock () { if (sock != INVALID_SOCKET) { closesocket (sock); sock = INVALID_SOCKET; } }

    bool send_all (const char *p, int n) { int off = 0; while (off < n) { int r = ::send (sock, p + off, n - off, 0); if (r <= 0) return false; off += r; } return true; }

    bool tcp_connect (const char *host, int port)
    {
        struct addrinfo hints, *res = 0; memset (&hints, 0, sizeof hints);
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        char ports[16]; sprintf (ports, "%d", port);
        if (getaddrinfo (host, ports, &hints, &res) || !res) return false;
        sock = ::socket (res->ai_family, res->ai_socktype, res->ai_protocol);
        bool ok = sock != INVALID_SOCKET && ::connect (sock, res->ai_addr, (int) res->ai_addrlen) == 0;
        freeaddrinfo (res);
        if (!ok) { closesock (); return false; }
        DWORD to = 10000; setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (const char *) &to, sizeof to);   // handshake timeout
        return true;
    }

    bool acquire_creds ()
    {
        SCHANNEL_CRED sc; memset (&sc, 0, sizeof sc);
        sc.dwVersion = SCHANNEL_CRED_VERSION;
        sc.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
        TimeStamp ts;
        SECURITY_STATUS s = AcquireCredentialsHandleA (0, (SEC_CHAR *) UNISP_NAME_A, SECPKG_CRED_OUTBOUND, 0, &sc, 0, 0, &cred, &ts);
        haveCred = (s == SEC_E_OK);
        return haveCred;
    }

    bool handshake (const char *host)
    {
        const DWORD req = ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT |
                          ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_USE_SUPPLIED_CREDS;
        DWORD out_flags = 0; TimeStamp ts; SECURITY_STATUS s;
        // first call: no input -> produce the ClientHello
        SecBuffer ob; ob.BufferType = SECBUFFER_TOKEN; ob.pvBuffer = 0; ob.cbBuffer = 0;
        SecBufferDesc od; od.ulVersion = SECBUFFER_VERSION; od.cBuffers = 1; od.pBuffers = &ob;
        s = InitializeSecurityContextA (&cred, 0, (SEC_CHAR *) host, req, 0, 0, 0, 0, &ctx, &od, &out_flags, &ts);
        haveCtx = true;
        if (s != SEC_I_CONTINUE_NEEDED) return false;
        if (ob.cbBuffer && ob.pvBuffer) { bool ok = send_all ((char *) ob.pvBuffer, ob.cbBuffer); FreeContextBuffer (ob.pvBuffer); if (!ok) return false; }

        // handshake loop
        for (;;)
        {
            // need at least some bytes from the server
            if (encin.n == 0 || s == SEC_E_INCOMPLETE_MESSAGE)
            {
                char tmp[8192]; int n = ::recv (sock, tmp, sizeof tmp, 0);
                if (n > 0) encin.add (tmp, n);
                else if (n == 0) return false;
                else { int e = WSAGetLastError (); if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK) continue; return false; }
            }
            SecBuffer ib[2];
            ib[0].BufferType = SECBUFFER_TOKEN; ib[0].pvBuffer = encin.p; ib[0].cbBuffer = encin.n;
            ib[1].BufferType = SECBUFFER_EMPTY; ib[1].pvBuffer = 0; ib[1].cbBuffer = 0;
            SecBufferDesc idd; idd.ulVersion = SECBUFFER_VERSION; idd.cBuffers = 2; idd.pBuffers = ib;
            SecBuffer ob2; ob2.BufferType = SECBUFFER_TOKEN; ob2.pvBuffer = 0; ob2.cbBuffer = 0;
            SecBufferDesc od2; od2.ulVersion = SECBUFFER_VERSION; od2.cBuffers = 1; od2.pBuffers = &ob2;
            s = InitializeSecurityContextA (&cred, &ctx, (SEC_CHAR *) host, req, 0, 0, &idd, 0, 0, &od2, &out_flags, &ts);
            if (ob2.cbBuffer && ob2.pvBuffer) { bool ok = send_all ((char *) ob2.pvBuffer, ob2.cbBuffer); FreeContextBuffer (ob2.pvBuffer); if (!ok) return false; }

            if (s == SEC_E_INCOMPLETE_MESSAGE) continue;          // keep the buffer, read more
            if (s == SEC_E_OK || s == SEC_I_CONTINUE_NEEDED)
            {
                // consume what Schannel used; keep EXTRA
                if (ib[1].BufferType == SECBUFFER_EXTRA) { int extra = ib[1].cbBuffer; encin.drop_front (encin.n - extra); }
                else encin.clear ();
                if (s == SEC_E_OK) return true;
                continue;
            }
            return false;    // handshake error (incl. cert validation failure)
        }
    }

    // try to decrypt from encin. returns 1 (dec filled), 0 (need more), -1 (closed/error).
    int try_decrypt ()
    {
        SecBuffer b[4];
        b[0].BufferType = SECBUFFER_DATA; b[0].pvBuffer = encin.p; b[0].cbBuffer = encin.n;
        b[1].BufferType = SECBUFFER_EMPTY; b[2].BufferType = SECBUFFER_EMPTY; b[3].BufferType = SECBUFFER_EMPTY;
        b[1].pvBuffer = b[2].pvBuffer = b[3].pvBuffer = 0; b[1].cbBuffer = b[2].cbBuffer = b[3].cbBuffer = 0;
        SecBufferDesc d; d.ulVersion = SECBUFFER_VERSION; d.cBuffers = 4; d.pBuffers = b;
        SECURITY_STATUS s = DecryptMessage (&ctx, &d, 0, 0);
        if (s == SEC_E_INCOMPLETE_MESSAGE) return 0;
        if (s == SEC_I_CONTEXT_EXPIRED) return -1;               // server closed TLS
        if (s != SEC_E_OK) return -1;
        // gather plaintext (SECBUFFER_DATA) and leftover (SECBUFFER_EXTRA)
        int extra = 0; const char *extrap = 0;
        dec.clear (); decoff = 0;
        for (int i = 0; i < 4; i++)
        {
            if (b[i].BufferType == SECBUFFER_DATA && b[i].cbBuffer) dec.add ((char *) b[i].pvBuffer, b[i].cbBuffer);
            else if (b[i].BufferType == SECBUFFER_EXTRA) { extra = b[i].cbBuffer; extrap = (const char *) b[i].pvBuffer; }
        }
        if (extra && extrap) { Buf keep; keep.add (extrap, extra); encin.clear (); encin.add (keep.p, keep.n); }
        else encin.clear ();
        return dec.n > 0 ? 1 : 0;
    }
};

} // namespace taatu
#endif // _WIN32
#endif
