# How BetterBedrock NX works (online features)

Technical notes behind the app, for anyone who wants to check what it does or build
on it. Everything here was measured on one console (see "Tested on" in the README).
Vibrant Visuals has its own page: [vibrant-visuals.md](vibrant-visuals.md).

## Atmosphère's hosts file

Atmosphère's `dns.mitm` answers DNS lookups from a hosts file on the SD card before
they reach the network. It reads the first file that exists of
`/atmosphere/hosts/emummc_<id>.txt`, `emummc.txt` (or `sysmmc.txt` on a sysMMC boot)
and `default.txt`. Each line is `<address> <host pattern>`; `*` matches any run of
characters, `%` stands for `lp1`, and **the last matching line wins**. When
`add_defaults_to_dns_hosts` is on, Atmosphère's own entries (Nintendo telemetry ->
`127.0.0.1`) are added first, so lines in the file override them.

`dns.mitm` serves every process, and its `sfdnsres` interface has one extra command,
**65000 `AtmosphereReloadHostsFile`**, which re-reads the hosts file at once. Any
process can send it. BetterBedrock NX uses it after every change, so nothing needs a
restart; if the reload fails, the app asks for a restart instead.

## 1. The Microsoft sign-in fix

Community replacement servers write their own hosts file. Nextendo's Prelude (from
3.5.7) also points `login.live.com`, `xboxlive.com` and `*.xboxlive.com` at its own
server, to give Minecraft Dungeons II a stand-in sign-in. The hosts file is global,
so Minecraft (Bedrock) then sends its real Xbox Live sign-in there and it fails: the
sign-in code screen shows a code from the wrong server and the game reports
"Failed to login".

The fix comments out exactly the lines whose every host is a Microsoft, Xbox or
Minecraft-services host, with the prefix `#[bedrocklink] `. Undoing it removes the
prefix, so the files come back byte for byte. Lines that mix Microsoft and other
hosts, or catch-alls such as `127.0.0.1 *`, are left alone and reported.

Prelude rewrites its hosts files whenever you pick a mode, which brings the Microsoft
lines back: open the app and fix the sign-in again.

## 2. Joining your own server

On the Switch, Minecraft has no "Add Server" button, but the classic featured
servers in **Play > Servers** are just host names the game looks up when you join:

| Featured server | Host |
|---|---|
| Galaxite | `play.galaxite.net` |
| The Hive | `geo.hivebedrock.network` |
| CubeCraft | `mco.cubecraft.net` |
| Lifeboat | `mco.lbsg.net` |
| Mineville | `play.inpvp.net` |
| Enchanted | `play.enchanted.gg` |
| MegaSMP | `play.megasmp.gg` |

Pointing one of them somewhere else with a hosts line redirects that one featured
server and nothing else. Two things limit where it can point:

- **The port is always 19132.** The game takes the port from the featured-server
  list, not from DNS.
- **Minecraft will not join an address on the console itself.** With a featured host
  pointed at `127.0.0.1`, or at the console's own Wi-Fi address, the game still
  sends status pings (the list shows the server's player count) but never sends a
  connection request. Joining another device's address works. So a relay running on
  the console cannot carry the join; an experiment that tried is kept in
  `experiments/relay-sysmodule/`.

That leaves two routes, both supported by the app:

- **BedrockConnect** (default). The featured host points at a
  [BedrockConnect](https://github.com/Pugmatt/BedrockConnect) server. Joining opens
  its menu inside Minecraft; *Connect to a Server* takes any address and port and
  sends a Transfer packet, so the console then connects straight to your server.
  Works with any port. The public instance is `104.238.130.180`; you can run your own.
- **Direct.** The featured host points straight at your server's IPv4 address. Only
  for servers that listen on port 19132.

Routing writes one marked block at the end of the hosts file Atmosphère reads:

```
# --- BedrockLink route (managed by the BedrockLink overlay) ---
104.238.130.180 play.galaxite.net
# --- end of BedrockLink route ---
```

The marker lines, and the sign-in fix's `#[bedrocklink] ` prefix, keep the name the
app had before 2.0 (BedrockLink), so blocks and fixes made by older versions are
still found.

Turning routing off removes the block from every hosts file.

## Safety rules, enforced on every write

- A write is refused unless every known Nintendo host (accounts, baas, dauth, aauth,
  telemetry, error reports, system and game update checks, connection test, ...)
  resolves exactly as before, with and without Atmosphère's defaults.
- A routing write is also refused unless every line outside the marked block is
  unchanged.
- Featured hosts and server addresses are checked: no Nintendo or Microsoft sign-in
  hosts, no loopback, `0.x` or broadcast addresses, no wildcards.
- Before each write the file is backed up (`/switch/BetterBedrockNX/backup/` for the
  sign-in fix, `/config/betterbedrock-nx/backup/` for routing: `.orig` = first version seen,
  `.prev` = the version just before the latest write), and every write is read back
  and compared.
- Routing is for emuMMC boots only.

BetterBedrock NX contacts nothing on its own. *Test connection* sends one status ping to
your server and, for BedrockConnect, one to the BedrockConnect server.

## Files

| Path | What |
|---|---|
| `/switch/BetterBedrockNX/BetterBedrockNX.nro` | the app |
| `/switch/.overlays/betterbedrock-nx.ovl` | the overlay (optional) |
| `/config/betterbedrock-nx/server.ini` | your server and routing settings |
| `/config/betterbedrock-nx/graphics.ini` | the chosen Vibrant Visuals profile |
| `/switch/BetterBedrockNX/log.txt` | what the app did, with every hosts change |

## Development notes

- The hosts logic (`common/`) is plain C and tested on a PC against a hosts file in
  the style community servers write (`tests/fixtures/`): `tests/run_pc_tests.sh`.
- The GUI builds for the PC too, with SDL2's software renderer and a font atlas in
  place of the console font; `tests/run_pc_gui.sh` drives it with scripted button
  presses and saves a screenshot after each one.
- `tools/analyze_dns_log.py` reads Atmosphère's `dns_mitm_debug.log`
  (`enable_dns_mitm_debug_log = u8!0x1`) and shows which lookups went to the hosts
  file and which went to the network's DNS.
