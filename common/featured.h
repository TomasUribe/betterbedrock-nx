// SPDX-License-Identifier: GPL-2.0-or-later
// Minecraft Bedrock's classic featured servers: the names the Servers tab shows and
// the host each one connects to (as looked up by Minecraft 1.26.44 on the Switch,
// from Atmosphere's DNS log). Any of them can be pointed at your own server.
#ifndef BBNX_FEATURED_H
#define BBNX_FEATURED_H

typedef struct {
    const char *name;
    const char *host;
} featured_server;

extern const featured_server FEATURED_SERVERS[];
extern const int FEATURED_COUNT;

// Name for a host, or NULL when it is not in the list.
const char *featured_name(const char *host);

// Index of host in the list, or -1.
int featured_index(const char *host);

#endif
