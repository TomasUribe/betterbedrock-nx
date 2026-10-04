# Relay sysmodule (experiment, not used)

A libnx sysmodule (0x420000000000BEDC) that listened on UDP 19132 on the console and
forwarded Minecraft's traffic to a server on another port. It works as a relay (tested
against a real Geyser server from a PC), but **Minecraft will not join an address on the
console itself**: with a featured server pointed at 127.0.0.1 or at the console's own
Wi-Fi address, the game sends status pings to it but never a connection request
(see docs/how-it-works.md). BedrockLink 1.3.0 routes through BedrockConnect instead.

Kept for reference. It was written against BedrockLink 1.2.0's `common/` code
(relay boot flag, `route_update_ip`, `nx_control` start/stop) and does not build
against the current one. Lessons it left in the code that ships:
- poll()-based loops must end a socket on hard errors (ENETDOWN after a Wi-Fi change
  turned into a busy loop on core 3 and froze the console);
- Atmosphere's sfdnsres command 65000 reloads the hosts file from any process.
