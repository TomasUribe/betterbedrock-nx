#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Make a remote Bedrock server (for example a Geyser server) show up as a LAN game.

Minecraft Bedrock finds LAN games by broadcasting RakNet "unconnected ping"
packets to UDP port 19132. This relay listens on that port on this PC and
answers with the remote server's own pong (refreshed every few seconds), with
the port fields rewritten to ours, so a console on the same network lists the
server as a LAN game and connects here. Every other packet is passed through
to the remote server, one upstream socket per client.

Usage: tools/lan_relay.py 203.0.113.10:40123 [--port 19132] [--self-test]
"""
import argparse
import os
import selectors
import socket
import struct
import sys
import time

MAGIC = bytes.fromhex("00ffff00fefefefefdfdfdfd12345678")
ID_PING, ID_PING_OPEN, ID_PONG = 0x01, 0x02, 0x1C
ID_OPEN_REQ_1, ID_OPEN_REPLY_1 = 0x05, 0x06
REFRESH_S = 5
IDLE_TIMEOUT_S = 60


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def make_ping():
    return bytes([ID_PING]) + struct.pack(">q", int(time.time() * 1000)) + MAGIC + os.urandom(8)


def parse_pong(data):
    """Returns (server_guid, motd) or None."""
    if len(data) < 35 or data[0] != ID_PONG or data[17:33] != MAGIC:
        return None
    guid = data[9:17]
    n = struct.unpack_from(">H", data, 33)[0]
    return guid, data[35:35 + n].decode("utf-8", "replace")


def rewrite_motd(motd, port):
    fields = motd.split(";")
    # MCPE;name;protocol;version;players;max;guid;sub;mode;modeId;port4;port6;
    while len(fields) < 12:
        fields.append("")
    fields[10] = fields[11] = str(port)
    return ";".join(fields)


def build_pong(client_ping, guid, motd):
    body = motd.encode("utf-8")
    return bytes([ID_PONG]) + client_ping[1:9] + guid + MAGIC + struct.pack(">H", len(body)) + body


class Relay:
    def __init__(self, remote, port):
        self.remote = remote
        self.port = port
        self.sel = selectors.DefaultSelector()
        self.listen = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.listen.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listen.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.listen.bind(("0.0.0.0", port))
        self.listen.setblocking(False)
        self.sel.register(self.listen, selectors.EVENT_READ, "listen")
        self.pinger = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.pinger.setblocking(False)
        self.sel.register(self.pinger, selectors.EVENT_READ, "pinger")
        self.pong = None          # (guid, rewritten motd)
        self.last_refresh = 0.0
        self.sessions = {}        # client addr -> [upstream socket, last activity, bytes up, bytes down]
        self.pinged_by = set()

    def refresh(self, now):
        if now - self.last_refresh >= REFRESH_S:
            self.last_refresh = now
            try:
                self.pinger.sendto(make_ping(), self.remote)
            except OSError as e:
                log(f"ping to {self.remote} failed: {e}")

    def on_pinger(self):
        data, _ = self.pinger.recvfrom(4096)
        parsed = parse_pong(data)
        if parsed:
            first = self.pong is None
            self.pong = (parsed[0], rewrite_motd(parsed[1], self.port))
            if first:
                log(f"remote server answered: {parsed[1].split(';')[1:4]}")

    def on_client(self):
        data, addr = self.listen.recvfrom(65535)
        if not data:
            return
        if data[0] in (ID_PING, ID_PING_OPEN) and len(data) >= 33 and data[9:25] == MAGIC:
            if self.pong:
                self.listen.sendto(build_pong(data, *self.pong), addr)
                if addr[0] not in self.pinged_by:
                    self.pinged_by.add(addr[0])
                    log(f"LAN ping from {addr[0]} answered")
            return
        s = self.sessions.get(addr)
        if s is None:
            up = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            up.connect(self.remote)
            up.setblocking(False)
            self.sel.register(up, selectors.EVENT_READ, addr)
            s = self.sessions[addr] = [up, 0.0, 0, 0]
            log(f"session from {addr[0]}:{addr[1]} -> {self.remote[0]}:{self.remote[1]} (first packet 0x{data[0]:02x})")
        s[1] = time.monotonic()
        s[2] += len(data)
        try:
            s[0].send(data)
        except OSError as e:
            log(f"send of {len(data)} bytes upstream for {addr[0]}:{addr[1]} dropped: {e.strerror}")

    def on_upstream(self, addr, sock):
        try:
            data = sock.recv(65535)
        except OSError as e:
            # RakNet probes the path MTU with large packets; the network answers the ones
            # that are too big with an ICMP error, which Linux reports here as EMSGSIZE.
            # Dropping them is what makes the client fall back to a smaller MTU.
            log(f"upstream error for {addr[0]}:{addr[1]} ignored: {e.strerror}")
            return
        s = self.sessions.get(addr)
        if s:
            s[1] = time.monotonic()
            s[3] += len(data)
        self.listen.sendto(data, addr)

    def expire(self, now):
        for addr, s in list(self.sessions.items()):
            if now - s[1] > IDLE_TIMEOUT_S:
                self.sel.unregister(s[0])
                s[0].close()
                del self.sessions[addr]
                log(f"session from {addr[0]}:{addr[1]} ended ({s[2]} bytes up, {s[3]} down)")

    def run(self):
        log(f"listening on UDP {self.port}, relaying to {self.remote[0]}:{self.remote[1]}")
        while True:
            now = time.monotonic()
            self.refresh(now)
            for key, _ in self.sel.select(timeout=1.0):
                try:
                    if key.data == "listen":
                        self.on_client()
                    elif key.data == "pinger":
                        self.on_pinger()
                    else:
                        self.on_upstream(key.data, key.fileobj)
                except OSError as e:
                    log(f"socket error ignored ({key.data}): {e}")
            self.expire(time.monotonic())


def self_test(port):
    """Talks to a relay already running on 127.0.0.1:port like a client would."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(5)
    s.sendto(make_ping(), ("127.0.0.1", port))
    parsed = parse_pong(s.recv(4096))
    assert parsed, "no pong from the relay"
    fields = parsed[1].split(";")
    print("pong via relay:", fields[1], "| version", fields[3], "| ports", fields[10], fields[11])
    assert fields[10] == str(port), "port not rewritten"
    # Open Connection Request 1 (RakNet protocol 11), first at the console's 1492-byte MTU
    # (may be dropped on the way), then at 1400.
    req = bytes([ID_OPEN_REQ_1]) + MAGIC + bytes([11])
    s.sendto(req + bytes(1492 - 28 - len(req)), ("127.0.0.1", port))
    time.sleep(1)
    s.sendto(req + bytes(1400 - 28 - len(req)), ("127.0.0.1", port))
    reply = s.recv(4096)
    assert reply[0] == ID_OPEN_REPLY_1 and reply[1:17] == MAGIC, f"unexpected reply 0x{reply[0]:02x}"
    mtu = struct.unpack_from(">H", reply, len(reply) - 2)[0]
    print(f"Open Connection Reply 1 from the server through the relay (MTU {mtu})")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("server", help="remote Bedrock server as host:port")
    ap.add_argument("--port", type=int, default=19132, help="local UDP port (LAN games use 19132)")
    ap.add_argument("--self-test", action="store_true", help="test a relay already running here")
    a = ap.parse_args()
    if a.self_test:
        self_test(a.port)
        return
    host, _, port = a.server.rpartition(":")
    if not host or not port.isdigit():
        sys.exit("server must be host:port")
    Relay((socket.gethostbyname(host), int(port)), a.port).run()


if __name__ == "__main__":
    main()
