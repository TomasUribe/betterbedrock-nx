// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink app logic: state and actions, no drawing. The GUI (gui.c) shows it.
//
// 1. Microsoft sign-in. Prelude's Nextendo mode also points login.live.com and
//    *.xboxlive.com at the Nextendo server (for Minecraft Dungeons II), which breaks
//    the real Xbox Live sign-in. The fix comments out only those lines.
// 2. Your server. Routing points one featured server (Play > Servers) at a
//    BedrockConnect server, whose in-game menu then takes you to your server (any
//    port), or straight at your server when it runs on port 19132.
// Nintendo lines are never touched: every write is checked first so that each known
// Nintendo host resolves exactly as before. Changes apply at once (hosts reload).
#ifndef BEDROCKLINK_APP_H
#define BEDROCKLINK_APP_H

#include <stdbool.h>
#include <stddef.h>

#include "hosts_edit.h"
#include "route.h"

#define APP_VERSION_STR "1.4.0"

typedef enum { ST_NONE, ST_INFO, ST_OK, ST_ERROR } status_kind;
typedef enum { MS_NOT_NEEDED, MS_OFF, MS_ON } ms_fix_state;
typedef enum { ROUTING_OFF, ROUTING_ON, ROUTING_LOADING, ROUTING_STALE, ROUTING_LINGERING } routing_state;
typedef enum { TEST_NONE, TEST_OK, TEST_FAIL } test_kind;
typedef enum { NIN_REDIRECTED, NIN_BLOCKED, NIN_YOUR_DNS } nintendo_route;

typedef struct {
    test_kind kind;
    char text[128];
} test_result;

void app_init(bool sockets);
void app_exit(void);

// Environment
int app_boot(void);  // 1 = emuMMC, 0 = sysMMC, -1 = unknown
bool app_dns_mitm(void);
const char *app_active_file(void);

// Microsoft sign-in
ms_fix_state app_ms_state(void);
void app_ms_fix(bool on);  // on = comment out Nextendo's Microsoft lines

// Your server (each setter validates, saves server.ini, re-applies routing when on)
const route_config *app_cfg(void);
bool app_set_name(const char *v);
bool app_set_address(const char *v);
bool app_set_port_text(const char *v);
void app_cycle_featured(int dir);
bool app_set_featured_host(const char *v);
void app_toggle_via(void);
bool app_set_bedrockconnect(const char *v);
const char *app_featured_label(void);  // "Galaxite", or the host when not in the list
bool app_direct_needs_19132(void);     // via direct, server not on port 19132

void app_test(void);
const test_result *app_test_server(void);
const test_result *app_test_bc(void);

// Routing
routing_state app_routing(void);
void app_routing_toggle(void);

bool app_restart_needed(void);
void app_restart(void);  // returns only when the restart failed (status says so)

// Last message for the user
status_kind app_status(const char **text);
void app_set_status(status_kind kind, const char *fmt, ...);

// Details page
int app_ms_probe_count(void);
const char *app_ms_probe(int i, bool *redirected, char *ip, size_t cap);
int app_nintendo_probe_count(void);
const char *app_nintendo_probe(int i, nintendo_route *how, char *ip, size_t cap);
int app_hosts_file_count(void);
bool app_hosts_file(int i, char *name, size_t cap, bool *active, he_stats *st);

#endif
