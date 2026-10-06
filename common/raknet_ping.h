// SPDX-License-Identifier: GPL-2.0-or-later
// One RakNet "unconnected ping" to a Bedrock server: the same status request the
// Servers tab sends. BSD sockets, so it runs on the Switch and on a PC.
#ifndef BBNX_RAKNET_PING_H
#define BBNX_RAKNET_PING_H

#include <stddef.h>

typedef struct {
    char name[96];
    char version[24];
    int players, max_players;
    int rtt_ms;
} raknet_status;

enum { PING_OK = 1, PING_TIMEOUT = 0, PING_NO_ADDRESS = -1, PING_SOCKET_ERROR = -2 };

// Returns one of the PING_* values; fills st on PING_OK.
int raknet_ping(const char *address, unsigned port, int timeout_ms, raknet_status *st);

#endif
