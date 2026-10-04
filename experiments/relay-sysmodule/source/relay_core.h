// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink relay core: forwards RakNet/UDP between Minecraft (talking to
// a local address) and one remote server. BSD sockets only, so the same code
// runs in the Switch sysmodule (libnx) and in the PC test.
#ifndef BEDROCKLINK_RELAY_CORE_H
#define BEDROCKLINK_RELAY_CORE_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#define RELAY_MAX_SESSIONS 8
#define RELAY_IDLE_MS 60000
// Largest RakNet MTU the relay lets a client negotiate. Bigger MTU probes are cut
// down to this size: a big probe can reach the server in IP fragments while the
// equally big replies are lost on the way back, and the join then stalls.
#define RELAY_MAX_MTU 1200
#define RELAY_LOG_PACKETS 60

typedef void (*relay_log_fn)(const char *fmt, ...);

typedef struct {
    int used;
    int fd;                       // upstream socket, connected to the remote
    struct sockaddr_in client;    // Minecraft's address
    uint64_t last_ms, up, down;
    unsigned logged, clamped, up_err, down_err;
} relay_session;

typedef struct {
    int listen_fd;
    uint16_t local_port;
    struct sockaddr_in remote;
    uint32_t own_ip;              // the console's IPv4 (network order); only it and 127.0.0.1 may use the relay
    unsigned dropped_foreign;
    relay_session s[RELAY_MAX_SESSIONS];
    relay_log_fn log;
} relay;

// Binds bind_ip:port. Returns 0, or -errno.
int relay_open(relay *r, const char *bind_ip, uint16_t port, const struct sockaddr_in *remote, relay_log_fn log);

// Waits up to timeout_ms for traffic and forwards it. Returns the number of socket
// errors it saw (a session whose network went away is closed, never retried in a
// loop), or -1 when the listening socket broke (close and reopen).
int relay_step(relay *r, int timeout_ms, uint64_t now_ms);

void relay_close(relay *r);

// Rewrites the port fields of an unconnected pong's MOTD (so Minecraft keeps
// talking to the relay). Returns the new length, or 0 when in is not a pong.
size_t relay_rewrite_pong(const uint8_t *in, size_t n, uint8_t *out, size_t cap, uint16_t port);

#endif
