#!/usr/bin/env python3
#
# nettest_peer.py -- the PC side of /bin/nettest (kapi v75 BSD sockets, docs/POSIX-PLAN.md §3.3).
#
#   python3 tools/tests/nettest_peer.py [port]                 (default 7777) then, on the Pi:
#       nettest peer <this PC's IP> [port]
#   python3 tools/tests/nettest_peer.py --client <pi-ip> [port] after, on the Pi:
#       nettest serve [port]
#
# Server mode: TCP on <port>, one command line per connection --
#   "STREAM <n>"  sends n bytes of the pattern (i * 7 + 3) & 255, then closes
#   "ECHO"        echoes until the Pi closes
#   "SINK"        reads everything, slowly (64 KB, 20 ms pause: the Pi's send meets backpressure)
# and a UDP echo on the same port. <port> + 1 must stay CLOSED (the ECONNREFUSED check): allow it
# through the PC's firewall too, or the check sees a timeout instead.
# Client mode: three connections at once to the Pi's "nettest serve", each sends a line and checks
# the echo, then closes (the Pi sees the closes through poll).
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSING.md).
#
import socket
import sys
import threading
import time


def pattern(n):
    return bytes(((i * 7 + 3) & 0xFF) for i in range(n))


def handle(conn, addr):
    try:
        line = b""
        while not line.endswith(b"\n"):
            b = conn.recv(1)
            if not b:
                return
            line += b
        cmd = line.decode(errors="replace").split()
        print(f"{addr[0]}:{addr[1]} {' '.join(cmd)}", flush=True)
        if cmd and cmd[0] == "STREAM":
            conn.sendall(pattern(int(cmd[1])))
        elif cmd and cmd[0] == "ECHO":
            while True:
                b = conn.recv(4096)
                if not b:
                    break
                conn.sendall(b)
        elif cmd and cmd[0] == "SINK":
            total = 0
            while True:
                b = conn.recv(65536)
                if not b:
                    break
                total += len(b)
                time.sleep(0.02)
            print(f"  sink: {total} bytes", flush=True)
    except OSError as e:
        print(f"  {addr}: {e}", flush=True)
    finally:
        conn.close()


def udp_echo(port):
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.bind(("", port))
    while True:
        data, addr = u.recvfrom(2048)
        print(f"udp {addr[0]}:{addr[1]} {len(data)} bytes", flush=True)
        u.sendto(data, addr)


def server(port):
    threading.Thread(target=udp_echo, args=(port,), daemon=True).start()
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", port))
    s.listen(16)
    print(f"nettest peer: TCP + UDP on port {port} (port {port + 1} must stay closed)", flush=True)
    while True:
        conn, addr = s.accept()
        threading.Thread(target=handle, args=(conn, addr), daemon=True).start()


def client(host, port):
    ok = [False] * 3

    def one(i):
        c = socket.create_connection((host, port), timeout=10)
        msg = f"hello from client {i}\n".encode()
        c.sendall(msg)
        got = b""
        while len(got) < len(msg):
            b = c.recv(4096)
            if not b:
                break
            got += b
        ok[i] = got == msg
        time.sleep(0.5)
        c.close()

    threads = [threading.Thread(target=one, args=(i,)) for i in range(3)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print("PASS" if all(ok) else f"FAIL {ok}")
    return 0 if all(ok) else 1


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--client":
        sys.exit(client(a[1], int(a[2]) if len(a) > 2 else 7777))
    server(int(a[0]) if a else 7777)
