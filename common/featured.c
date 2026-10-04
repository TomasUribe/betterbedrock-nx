// SPDX-License-Identifier: GPL-2.0-or-later
// Featured server list. See featured.h.
#include "featured.h"

#include <string.h>

const featured_server FEATURED_SERVERS[] = {
    {"Galaxite", "play.galaxite.net"},
    {"The Hive", "geo.hivebedrock.network"},
    {"CubeCraft", "mco.cubecraft.net"},
    {"Lifeboat", "mco.lbsg.net"},
    {"Mineville", "play.inpvp.net"},
    {"Enchanted", "play.enchanted.gg"},
    {"MegaSMP", "play.megasmp.gg"},
};
const int FEATURED_COUNT = (int)(sizeof FEATURED_SERVERS / sizeof FEATURED_SERVERS[0]);

int featured_index(const char *host) {
    for (int i = 0; i < FEATURED_COUNT; i++)
        if (!strcmp(FEATURED_SERVERS[i].host, host)) return i;
    return -1;
}

const char *featured_name(const char *host) {
    int i = featured_index(host);
    return i < 0 ? NULL : FEATURED_SERVERS[i].name;
}
