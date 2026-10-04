#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Summarise Atmosphere's dns_mitm_debug.log.

Usage: tools/analyze_dns_log.py <dns_mitm_debug.log> [--program <id>]

Each boot starts with a '---' line. A request that is followed by a
"Redirecting <host> to <ip>" line from the same program was answered by the
hosts file; any other request went to the network's real DNS server.
"""
import re
import sys
from collections import OrderedDict

PROGRAMS = {
    "010000000000000c": "bcat",
    "010000000000000f": "nifm",
    "010000000000001e": "account",
    "010000000000001f": "ns",
    "0100000000000024": "ssl",
    "0100000000000025": "nim",
    "010000000000002e": "friends",
    "010000000000002f": "npns",
    "0100d71004694000": "Minecraft",
}
NINTENDO = re.compile(r"(^|\.)(nintendo\.(com|net|co\.jp|jp)|nintendowifi\.net)$", re.I)
REQ = re.compile(r"^\[([0-9a-f]{16})\]: (GetHostByName\w*|GetAddrInfo\w*)\(([^,)]+)")
RED = re.compile(r"^\[([0-9a-f]{16})\]: Redirecting (\S+) to (\S+)")


def parse(path):
    boots = [[]]
    pending = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if line.strip() == "---":
            boots.append([])
            pending = {}
            continue
        m = REQ.match(line)
        if m:
            prog, _, host = m.groups()
            entry = {"prog": prog, "host": host.strip(), "to": None}
            boots[-1].append(entry)
            pending.setdefault((prog, entry["host"].lower()), []).append(entry)
            continue
        m = RED.match(line)
        if m:
            prog, host, ip = m.groups()
            host = re.sub(r":\d+$", "", host)  # GetAddrInfo redirects log "host:port"
            waiting = pending.get((prog, host.lower()))
            if waiting:
                waiting.pop(0)["to"] = ip
    return [b for b in boots if b]


def name(prog):
    return f"{PROGRAMS.get(prog, prog)}"


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    only = None
    if "--program" in args:
        only = args[args.index("--program") + 1].lower()
    boots = parse(args[0])
    for i, boot in enumerate(boots, 1):
        if only:
            boot = [e for e in boot if e["prog"] == only]
            if not boot:
                continue
        print(f"=== boot {i}: {len(boot)} lookups")
        summary = OrderedDict()
        for e in boot:
            key = (e["prog"], e["host"].lower())
            s = summary.setdefault(key, {"n": 0, "to": set()})
            s["n"] += 1
            s["to"].add(e["to"] or "REAL DNS")
        real_nintendo = []
        for (prog, host), s in summary.items():
            to = ", ".join(sorted(s["to"]))
            flag = ""
            if NINTENDO.search(host) and "REAL DNS" in s["to"]:
                flag = "   <-- Nintendo host, not redirected"
                real_nintendo.append((name(prog), host))
            print(f"  {name(prog):>16}  {host:<48} x{s['n']:<3} -> {to}{flag}")
        if real_nintendo:
            print(f"  Nintendo hosts sent to the real DNS this boot: {len(real_nintendo)}")
            for prog, host in real_nintendo:
                print(f"    {prog}: {host}")


if __name__ == "__main__":
    main()
