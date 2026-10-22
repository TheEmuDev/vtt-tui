#!/usr/bin/env python3
"""How much a TCP socket takes toward a peer that never reads, before a write
would block: the kernel's share of a stalled phone's lag (docs/V2.md, step 7,
item 1). For each SO_SNDBUF asked (none: the kernel's own, autotuned), prints
what getsockopt reads back, the bytes accepted before EAGAIN, and the peer's
receive buffer, which holds its own share.

    tools/sockcap.py [SNDBUF...]        default: 131072 and none
    tools/sockcap.py --host ADDR [...]  toward ADDR:PORT given as ADDR (a
                                        listener there that never reads, e.g.
                                        on a phone over adb); loopback else

Loopback only measures this machine's two kernels; a phone's receive buffer
is its own (tools/phonecheck.sh)."""
import socket
import sys


def measure(want, host=None):
    lst = None
    if host:
        h, p = host.rsplit(":", 1)
        peer = None
        snd = socket.create_connection((h, int(p)))
    else:
        lst = socket.socket()
        lst.bind(("127.0.0.1", 0))
        lst.listen(1)
        peer = socket.create_connection(lst.getsockname())
        snd, _ = lst.accept()
    if want:
        snd.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, want)
    got = snd.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF)
    snd.setblocking(False)
    sent, chunk = 0, b"x" * 4096
    try:
        while True:
            sent += snd.send(chunk)
    except BlockingIOError:
        pass
    rcv = peer.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF) if peer else None
    for s in (snd, peer, lst):
        if s:
            s.close()
    return got, sent, rcv


def main(argv):
    host = None
    if argv[:1] == ["--host"]:
        host, argv = argv[1], argv[2:]
    sizes = [int(a) for a in argv] or [131072, 0]
    for want in sizes:
        got, sent, rcv = measure(want, host)
        asked = want if want else "none"
        peer = f", the peer's receive buffer {rcv}" if rcv is not None else ""
        print(f"SO_SNDBUF asked {asked}: reads back {got}, {sent} bytes taken before EAGAIN{peer}")


if __name__ == "__main__":
    main(sys.argv[1:])
