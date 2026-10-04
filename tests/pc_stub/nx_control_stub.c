// SPDX-License-Identifier: GPL-2.0-or-later
// PC stand-in for common/nx_control.c: a hosts reload rewrites Atmosphere's startup
// log from the active hosts file the way dns.mitm does.
#include <stdio.h>

#include "../../common/nx_control.h"
#include "../../common/route.h"

uint32_t bl_reload_hosts(void) {
    char path[128];
    FILE *out = fopen(ROUTE_DNS_STARTUP_LOG, "w");
    if (!out) return 1;
    fputs("DNS Mitm:\nRedirections:\n", out);
    if (route_active_hosts_path(1, path, sizeof path)) {
        FILE *in = fopen(path, "r");
        char line[512];
        while (in && fgets(line, sizeof line, in)) {
            char ip[64], host[256];
            if (line[0] != '#' && sscanf(line, "%63s %255s", ip, host) == 2) fprintf(out, "    `%s` -> %s\n", host, ip);
        }
        if (in) fclose(in);
    }
    fclose(out);
    printf("[stub] hosts reloaded\n");
    return 0;
}
