//
// main_pc.cpp -- console test harness for the TAATU native client (Windows/Linux).
// Exercises the full protocol against a TAATU server over plain WebSocket (local dev:
// ws://127.0.0.1:3000). Proves: handshake, auth, ui-key, join-room, room-players/room-tick,
// chat (receive + signed send), movement. The Onyx GUI build reuses the same core/.
//
//   taatu_pc --host 127.0.0.1 --port 3000 --token <session> --room 4 [--x 27 --z 27]
//            [--say "hello"] [--seconds 30]
//
#include "../../core/client.hpp"
#include "../../core/auth.hpp"
#include "transport_tcp.hpp"
#include "tls_win.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>
static void nap (unsigned ms) { Sleep (ms); }
#else
#  include <unistd.h>
#  include <time.h>
static void nap (unsigned ms) { usleep (ms * 1000); }
#endif

// platform hooks required by the core
namespace taatu {
    void WsClient::plat_sleep_ms (unsigned ms) { nap (ms); }
    void PcTcpTransport::plat_nap () { nap (1); }
    void httpc_sleep_ms (unsigned ms) { nap (ms); }
}

// transport factory (plain TCP, or Schannel TLS on Windows). A fresh transport per call:
// the REST auth flow needs one per request, the WebSocket one for the session.
static bool g_tls = false;
static taatu::ITransport *mk_auto (void *)
{
#ifdef _WIN32
    if (g_tls) return new taatu::WinTlsTransport ();
#endif
    return new taatu::PcTcpTransport ();
}
static unsigned now_ms ()
{
#ifdef _WIN32
    return (unsigned) GetTickCount ();
#else
    struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
#endif
}

using namespace taatu;

bool sio_ready (TaatuClient &c);          // defined below

static const char *arg (int argc, char **argv, const char *k, const char *def)
{
    for (int i = 1; i + 1 < argc; i++) if (!strcmp (argv[i], k)) return argv[i + 1];
    return def;
}
static bool has_flag (int argc, char **argv, const char *k)
{
    for (int i = 1; i < argc; i++) if (!strcmp (argv[i], k)) return true;
    return false;
}
static void read_line (const char *prompt, char *out, int cap, bool hide)
{
    printf ("%s", prompt); fflush (stdout);
#ifdef _WIN32
    HANDLE hin = GetStdHandle (STD_INPUT_HANDLE); DWORD mode = 0;
    if (hide) { GetConsoleMode (hin, &mode); SetConsoleMode (hin, mode & ~ENABLE_ECHO_INPUT); }
#endif
    if (!fgets (out, cap, stdin)) out[0] = 0;
    char *nl = strpbrk (out, "\r\n"); if (nl) *nl = 0;
#ifdef _WIN32
    if (hide) { SetConsoleMode (hin, mode); printf ("\n"); }
#endif
}

int main (int argc, char **argv)
{
    const char *host = arg (argc, argv, "--host", "127.0.0.1");
    int port = atoi (arg (argc, argv, "--port", "3000"));
    const char *token = arg (argc, argv, "--token", "");
    int room = atoi (arg (argc, argv, "--room", "4"));
    int sx = atoi (arg (argc, argv, "--x", "27"));
    int sz = atoi (arg (argc, argv, "--z", "27"));
    const char *say = arg (argc, argv, "--say", "");
    int seconds = atoi (arg (argc, argv, "--seconds", "30"));
    const char *pseudo = arg (argc, argv, "--pseudo", "");
    const char *password = arg (argc, argv, "--password", "");
    g_tls = (port == 443) || !strcmp (arg (argc, argv, "--tls", "0"), "1");

    char tokbuf[600]; tokbuf[0] = 0;
    if (token && token[0]) { strncpy (tokbuf, token, sizeof tokbuf - 1); tokbuf[sizeof tokbuf - 1] = 0; }

    // interactive login: --login (or a --pseudo) with no token prompts for anything missing.
    char pbuf[64], wbuf[128];
    bool wantLogin = !tokbuf[0] && (pseudo[0] || has_flag (argc, argv, "--login"));
    if (wantLogin)
    {
        if (!pseudo[0])   { read_line ("Pseudo: ", pbuf, sizeof pbuf, false); pseudo = pbuf; }
        if (!password[0]) { read_line ("Mot de passe: ", wbuf, sizeof wbuf, true); password = wbuf; }
    }

    // REST login if no token was given but a pseudo is now set
    if (!tokbuf[0] && pseudo[0])
    {
        TaatuAuth auth;
        auth.set_target (host, port);
        auth.mk = mk_auto;
        char uuid[37]; TaatuAuth::gen_uuid (uuid); strncpy (auth.device_id, uuid, sizeof auth.device_id - 1);
        auth.set_fingerprint ("TaatuOnyx/0.1 (PC)", "fr", "onyx", "Europe/Brussels", 1000, 700, 32, 1, 0, 0);
        printf ("[taatu] login as '%s' ...\n", pseudo);
        int r = auth.login (pseudo, password, true);
        if (r == LOGIN_MFA)
        {
            char code[32];
            printf ("[taatu] MFA required (challenge %s). Enter code: ", auth.challenge_id);
            fflush (stdout);
            if (fgets (code, sizeof code, stdin)) { char *nl = strchr (code, '\n'); if (nl) *nl = 0; r = auth.mfa_verify (auth.challenge_id, code, true); }
        }
        if (r == LOGIN_OK) { strncpy (tokbuf, auth.token, sizeof tokbuf - 1); tokbuf[sizeof tokbuf - 1] = 0; printf ("[taatu] login OK, session token acquired (%d chars)\n", (int) strlen (tokbuf)); }
        else { printf ("[taatu] login FAILED: %s\n", auth.err[0] ? auth.err : "unknown"); return 1; }
    }

    printf ("[taatu] connecting %s://%s:%d  room=%d\n", g_tls ? "wss" : "ws", host, port, room);

    ITransport *tp = mk_auto (0);
    TaatuClient cli;
    cli.now_ms = now_ms;
    cli.set_target (host, port, g_tls, "https://taatu.world");
    cli.set_token (tokbuf);

    if (!cli.connect (*tp)) { printf ("[taatu] connect/handshake FAILED\n"); delete tp; return 1; }
    printf ("[taatu] websocket + engine.io up, waiting for socket.io connect...\n");

    bool joined = false, said = false;
    int printed_chat = 0, last_n = -1;
    unsigned t0 = now_ms (), joined_at = 0;

    while (now_ms () - t0 < (unsigned) seconds * 1000)
    {
        cli.poll ();
        if (cli.closed ()) { printf ("[taatu] connection closed\n"); break; }

        if (cli.connected () && !joined)
        {
            cli.note_input ();
            cli.join_room (room, sx, sz, 1);
            joined = true; joined_at = now_ms ();
            printf ("[taatu] connected -> join-room %d at (%d,%d)\n", room, sx, sz);
        }

        // report avatars + new chat
        if (cli.world.n_av != last_n)
        {
            last_n = cli.world.n_av;
            printf ("[taatu] avatars in room: %d\n", last_n);
            for (int i = 0; i < cli.world.n_av; i++)
            {
                Avatar &a = cli.world.av[i];
                if (a.used) printf ("        - %-16s id=%lld (%d,%d) dir=%d %s\n", a.pseudo, a.user_id, a.x, a.z, a.direction, a.user_status);
            }
        }
        while (printed_chat < cli.world.chat_count)
        {
            const ChatMsg &c = cli.world.chat_at (printed_chat++);
            printf ("  <chat> %s: %s\n", c.pseudo, c.message);
        }

        // after join + ui-key, optionally say something (signed emit)
        if (joined && !said && say[0] && sio_ready (cli) && now_ms () - joined_at > 1500)
        {
            cli.note_input ();
            cli.send_chat (say);
            printf ("[taatu] sent chat: \"%s\"\n", say);
            said = true;
        }
        nap (16);
    }

    if (joined) cli.leave_room ();
    cli.sio.close ();
    delete tp;
    printf ("[taatu] done.\n");
    return 0;
}

// small helper: is the anti-bot key ready (so signed emits carry _ui)?
bool sio_ready (TaatuClient &c) { return c.sio.ui.ready (); }
