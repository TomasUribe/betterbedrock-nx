// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink server routing: points one Minecraft featured server (Play > Servers)
// at a server of your choice through one marked line in the hosts file Atmosphere
// reads. Shared by the app and the overlay. Plain C + stdio, host-testable: all
// paths start with "sdmc:/", which on a PC is a folder named "sdmc:".
//
// Featured servers always connect on port 19132, and Minecraft will not join an
// address on the console itself. So the line points either at a BedrockConnect
// server (its in-game menu then transfers you to any address and port) or, for a
// server that runs on port 19132, straight at that server.
#ifndef BEDROCKLINK_ROUTE_H
#define BEDROCKLINK_ROUTE_H

#include <stddef.h>

#define ROUTE_CONFIG_PATH           "sdmc:/config/bedrocklink/server.ini"
#define ROUTE_BACKUP_DIR            "sdmc:/config/bedrocklink/backup"
#define ROUTE_DNS_STARTUP_LOG       "sdmc:/atmosphere/logs/dns_mitm_startup.log"
#define ROUTE_FEATURED_PORT         19132
#define ROUTE_PUBLIC_BEDROCKCONNECT "104.238.130.180"  // BedrockConnect's public server

enum { ROUTE_VIA_BEDROCKCONNECT = 0, ROUTE_VIA_DIRECT = 1 };

typedef struct {
    char name[64];            // your server, shown in the app and the overlay
    char address[64];         // your server's address (IPv4 or host name)
    unsigned port;            // your server's port
    char replaces[128];       // featured server host that takes you there
    int via;                  // ROUTE_VIA_*
    char bedrockconnect[64];  // BedrockConnect server used when via = bedrockconnect
} route_config;

typedef struct {
    int config_ok;
    char config_error[96];
    route_config cfg;
    char hosts_path[128];       // file Atmosphere reads on this boot type ("" = none)
    int hosts_route_on;         // that file has a route block
    char hosts_route_ip[32];
    char hosts_route_host[128];
    int dns_active;             // dns.mitm has the route loaded now (its startup log, rewritten on reload)
} route_state;

// Reads ROUTE_CONFIG_PATH without checking it (for editing): 1 = read, 0 = no file,
// -1 = a value too long. Missing via / bedrockconnect get their defaults.
int route_read_config(route_config *c);

// Reads ROUTE_CONFIG_PATH and checks what routing needs. 1, or 0 with a message.
int route_load_config(route_config *c, char *err, size_t err_cap);

// The address the route line must point at: the BedrockConnect server, or your server
// for direct (which needs port 19132). Returns 1, or 0 with a message.
int route_target(const route_config *c, char *out, size_t cap, char *err, size_t err_cap);

int route_is_ipv4(const char *s);

// "sdmc:/atmosphere/hosts/emummc_<id>.txt" for this console's emuMMC id.
void route_emummc_id_hosts_path(char *out, size_t cap);

// The hosts file Atmosphere reads (emummc_<id>.txt, emummc.txt or sysmmc.txt, then
// default.txt - the first that exists). Returns 1 and the path, or 0 when none exists.
int route_active_hosts_path(int emummc_boot, char *out, size_t cap);

void route_query(int emummc_boot, route_state *st);

// Field checks used before saving. address: an IPv4 address (not loopback, 0.x or
// broadcast) or a plain host name that is not a Nintendo or Microsoft sign-in host.
int route_address_ok(const char *address);
int route_name_ok(const char *name);

// Writes ROUTE_CONFIG_PATH from c. Each field that is set must pass its check;
// empty fields are written empty. Returns 1, or 0 with a message.
int route_save_config(const route_config *c, char *msg, size_t msg_cap);

// On: writes the route block "<ip> <replaces>" into the active hosts file. Off: removes
// route blocks from every hosts file. Refused unless every other line and every Nintendo
// host stays the same. ip must be an IPv4 address. Reload the hosts file afterwards.
// Returns 1 on success; msg says what happened either way.
int route_apply(int emummc_boot, int enable, const char *ip, char *msg, size_t msg_cap);

#endif
