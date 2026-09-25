#!/usr/bin/env python3
"""onyx-telnet -- minimal client for Onyx's /bin/telnetd remote shell.

    python tools/onyx-telnet.py <pi-ip> [port]      (default port 23)

Any telnet client works too (`telnet <ip>`, PuTTY in Telnet mode); this one just needs
nothing but Python (Windows or Linux/macOS). The server does the echo and the line
editing, so the local terminal is switched to raw mode: every key is sent as typed.
Ctrl-C is sent to the remote shell; quit with `exit`, Ctrl-D on an empty line, or
Ctrl-] (local escape).
"""
import os
import socket
import sys
import threading

IAC = 255
ESCAPE = "\x1d"          # Ctrl-]


def strip_telnet(data, state):
    """Drop telnet command sequences from server output (state carries over)."""
    out = bytearray()
    for b in data:
        s = state[0]
        if s == 0:
            if b == IAC:
                state[0] = 1
            else:
                out.append(b)
        elif s == 1:                    # after IAC
            if b == IAC:
                out.append(b)
                state[0] = 0
            elif 251 <= b <= 254:       # WILL/WONT/DO/DONT + option byte
                state[0] = 2
            elif b == 250:              # SB ... IAC SE
                state[0] = 3
            else:
                state[0] = 0
        elif s == 2:
            state[0] = 0
        elif s == 3:
            if b == IAC:
                state[0] = 4
        elif s == 4:
            state[0] = 0 if b == 240 else 3
    return bytes(out)


def reader(sock, done):
    state = [0]
    out = sys.stdout.buffer
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            out.write(strip_telnet(data, state))
            out.flush()
    except OSError:
        pass
    done.set()


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip().splitlines()[2].strip())
        sys.exit(1)
    host = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 23
    sock = socket.create_connection((host, port))
    done = threading.Event()
    threading.Thread(target=reader, args=(sock, done), daemon=True).start()

    def send(ch):
        if ch == ESCAPE:
            return False
        sock.sendall(ch.encode("latin-1", "replace"))
        return True

    try:
        if os.name == "nt":
            import msvcrt
            os.system("")               # enable ANSI escape handling in the console
            while not done.is_set():
                if not msvcrt.kbhit():
                    done.wait(0.02)
                    continue
                ch = msvcrt.getwch()
                if ch in ("\x00", "\xe0"):  # arrow / function key: ignore
                    msvcrt.getwch()
                    continue
                if not send(ch):
                    break
        else:
            import select
            import termios
            import tty
            fd = sys.stdin.fileno()
            saved = termios.tcgetattr(fd)
            tty.setraw(fd)
            try:
                while not done.is_set():
                    r, _, _ = select.select([fd], [], [], 0.05)
                    if r and not send(os.read(fd, 1).decode("latin-1")):
                        break
            finally:
                termios.tcsetattr(fd, termios.TCSADRAIN, saved)
    except (KeyboardInterrupt, OSError):
        pass
    finally:
        sock.close()
    print("\r\n[disconnected]")


if __name__ == "__main__":
    main()
