// SPDX-License-Identifier: GPL-2.0-or-later
// PC test for common/route.c against a fake SD card. Run: tests/run_pc_tests.sh
// Works in the current directory, where a folder named "sdmc:" plays the SD card.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../common/hosts_edit.h"
#include "../common/route.h"
#include "../common/featured.h"

static int g_fail, g_pass;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    *len = fread(b, 1, (size_t)n, f);
    b[*len] = '\0';
    fclose(f);
    return b;
}

static void put(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

static int same_file(const char *a, const char *b) {
    size_t al, bl;
    char *x = slurp(a, &al), *y = slurp(b, &bl);
    int same = x && y && al == bl && !memcmp(x, y, al);
    free(x);
    free(y);
    return same;
}

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

#define HOSTS "sdmc:/atmosphere/hosts/"
#define CONFIG "[server]\nname = My Server\naddress = 203.0.113.10\nport = 40123\nreplaces = play.galaxite.net\n"
#define BC ROUTE_PUBLIC_BEDROCKCONNECT

static void test_block_edits(void) {
    const char *base = "1.1.1.1 *.nintendo.com\r\n2.2.2.2 accounts.nintendo.com";  // CRLF, no final newline
    size_t l1, l2, l3;
    char *on = he_set_route(base, strlen(base), 1, "127.0.0.1", "play.galaxite.net", &l1);
    char ip[32], host[128];
    CHECK(he_get_route(on, l1, ip, sizeof ip, host, sizeof host) && !strcmp(ip, "127.0.0.1") &&
          !strcmp(host, "play.galaxite.net"), "get_route after set");
    CHECK(he_same_outside_route(base, strlen(base), on, l1), "outside unchanged");
    CHECK(he_same_nintendo(base, strlen(base), on, l1), "nintendo unchanged");
    char *again = he_set_route(on, l1, 1, "127.0.0.1", "mco.cubecraft.net", &l2);
    CHECK(strstr(again, "play.galaxite.net") == NULL && strstr(again, "mco.cubecraft.net") != NULL,
          "set replaces the old block");
    char *off = he_set_route(again, l2, 0, "", "", &l3);
    CHECK(!he_get_route(off, l3, ip, sizeof ip, host, sizeof host), "no block after off");
    CHECK(he_same_outside_route(base, strlen(base), off, l3), "off == base outside route");
    const char *moved = "9.9.9.9 *.nintendo.com\r\n2.2.2.2 accounts.nintendo.com";
    CHECK(!he_same_outside_route(base, strlen(base), moved, strlen(moved)), "detects outside change");
    CHECK(he_route_host_ok("play.galaxite.net") && he_route_host_ok("geo.hivebedrock.network"), "ok hosts");
    CHECK(!he_route_host_ok("accounts.nintendo.com") && !he_route_host_ok("login.live.com") &&
          !he_route_host_ok("*") && !he_route_host_ok("*.com") && !he_route_host_ok("Play.Galaxite.net"),
          "refused hosts");
    free(on), free(again), free(off);
}

static void test_route_on_sd(const char *fixture) {
    system("rm -rf sdmc: && mkdir -p 'sdmc:/atmosphere/hosts' 'sdmc:/atmosphere/logs' "
           "'sdmc:/config/betterbedrock-nx' 'sdmc:/emuMMC'");
    char cmd[512];
    for (const char *n = "emummc\0sysmmc\0default\0"; *n; n += strlen(n) + 1) {
        snprintf(cmd, sizeof cmd, "cp '%s' 'sdmc:/atmosphere/hosts/%s.txt' && cp '%s' 'orig_%s.txt'",
                 fixture, n, fixture, n);
        system(cmd);
    }
    put("sdmc:/emuMMC/emummc.ini", "[emummc]\nenabled=1\nid=0x1234abcd\n");
    put(ROUTE_CONFIG_PATH, CONFIG);

    char msg[160], target[64];
    route_state st;
    route_query(1, &st);
    CHECK(st.config_ok && st.cfg.port == 40123 && st.cfg.via == ROUTE_VIA_BEDROCKCONNECT &&
          !strcmp(st.cfg.bedrockconnect, BC) && !strcmp(st.cfg.replaces, "play.galaxite.net"), "config + defaults");
    CHECK(route_target(&st.cfg, target, sizeof target, msg, sizeof msg) && !strcmp(target, BC), "target = BedrockConnect");
    CHECK(!strcmp(st.hosts_path, HOSTS "emummc.txt") && !st.hosts_route_on, "initial state");

    CHECK(!route_apply(1, 1, "127.0.0.1", msg, sizeof msg), "loopback target refused");
    CHECK(!route_apply(1, 1, "play.example.net", msg, sizeof msg), "host name target refused (resolve first)");
    CHECK(route_apply(1, 1, BC, msg, sizeof msg), "enable: %s", msg);
    route_query(1, &st);
    CHECK(st.hosts_route_on && !strcmp(st.hosts_route_ip, BC) && !st.dns_active, "state after enable");
    CHECK(same_file(HOSTS "sysmmc.txt", "orig_sysmmc.txt") && same_file(HOSTS "default.txt", "orig_default.txt"),
          "only the active file changes");
    size_t ol, nl;
    char *o = slurp("orig_emummc.txt", &ol), *n = slurp(HOSTS "emummc.txt", &nl);
    CHECK(he_same_nintendo(o, ol, n, nl) && he_same_outside_route(o, ol, n, nl), "nintendo + other lines unchanged");
    char ip[32];
    CHECK(he_resolve(n, nl, NULL, "play.galaxite.net", ip, sizeof ip) && !strcmp(ip, BC), "galaxite -> BedrockConnect");
    CHECK(!he_resolve(n, nl, NULL, "mco.cubecraft.net", ip, sizeof ip), "other featured servers untouched");
    char ip_before[32] = "", ip_after[32] = "";
    int rb = he_resolve(o, ol, NULL, "login.live.com", ip_before, sizeof ip_before);
    int ra = he_resolve(n, nl, NULL, "login.live.com", ip_after, sizeof ip_after);
    CHECK(rb == ra && !strcmp(ip_before, ip_after), "microsoft resolves as before");
    free(o), free(n);

    put(ROUTE_DNS_STARTUP_LOG, "Redirections:\n    `play.galaxite.net` -> " BC "\n");
    route_query(1, &st);
    CHECK(st.dns_active, "startup log parsed");

    CHECK(route_apply(1, 0, NULL, msg, sizeof msg), "disable: %s", msg);
    route_query(1, &st);
    CHECK(!st.hosts_route_on, "state after disable");
    CHECK(same_file(HOSTS "emummc.txt", "orig_emummc.txt"), "emummc.txt byte-identical after on/off");
    CHECK(exists(ROUTE_BACKUP_DIR "/emummc.txt.orig") && exists(ROUTE_BACKUP_DIR "/emummc.txt.prev"), "backups");

    // Direct needs the server on port 19132.
    put(ROUTE_CONFIG_PATH, "address = 203.0.113.10\nport = 40123\nreplaces = play.galaxite.net\nvia = direct\n");
    route_config c;
    CHECK(!route_load_config(&c, msg, sizeof msg) && strstr(msg, "19132"), "direct on 40123 refused: %s", msg);
    put(ROUTE_CONFIG_PATH, "address = 203.0.113.7\nport = 19132\nreplaces = mco.lbsg.net\nvia = direct\n");
    CHECK(route_load_config(&c, msg, sizeof msg) && route_target(&c, target, sizeof target, msg, sizeof msg) &&
          !strcmp(target, "203.0.113.7"), "direct on 19132 -> the server itself");

    // Atmosphere's emummc_<id>.txt wins when present.
    system("cp orig_emummc.txt 'sdmc:/atmosphere/hosts/emummc_1234abcd.txt'");
    char path[128];
    CHECK(route_active_hosts_path(1, path, sizeof path) && !strcmp(path, HOSTS "emummc_1234abcd.txt"),
          "emummc_<id> first: %s", path);
    CHECK(route_active_hosts_path(0, path, sizeof path) && !strcmp(path, HOSTS "sysmmc.txt"), "sysmmc boot");

    // A config pointing at a Nintendo host is refused.
    put(ROUTE_CONFIG_PATH, "address = 1.2.3.4\nport = 1\nreplaces = accounts.nintendo.com\n");
    CHECK(!route_apply(1, 1, BC, msg, sizeof msg), "nintendo host refused");
    system("rm -f orig_*.txt");
}

static void test_fields(void) {
    CHECK(route_address_ok("203.0.113.10") && route_address_ok("play.example.net") &&
          route_address_ok("my-server.joinmc.link"), "good addresses");
    CHECK(!route_address_ok("") && !route_address_ok("127.0.0.1") && !route_address_ok("0.1.2.3") &&
          !route_address_ok("255.255.255.255") && !route_address_ok("1.2.3.256") && !route_address_ok("1.2.3") &&
          !route_address_ok("accounts.nintendo.com") && !route_address_ok("LOGIN.LIVE.COM") &&
          !route_address_ok("a b.com") && !route_address_ok("x;y.com"), "bad addresses");
    CHECK(route_name_ok("My Server (Season 2!)") && !route_name_ok("") && !route_name_ok("a;b") &&
          !route_name_ok("[x"), "names");
    CHECK(FEATURED_COUNT == 7 && featured_index("play.galaxite.net") == 0 &&
          !strcmp(featured_name("mco.lbsg.net"), "Lifeboat") && featured_name("example.com") == NULL, "featured list");
    for (int i = 0; i < FEATURED_COUNT; i++)
        CHECK(he_route_host_ok(FEATURED_SERVERS[i].host), "featured host routable: %s", FEATURED_SERVERS[i].host);
}

static void test_save_config(void) {
    system("rm -rf sdmc: && mkdir -p sdmc:");
    route_config c = {0};
    char msg[96];
    snprintf(c.name, sizeof c.name, "My Server");
    snprintf(c.address, sizeof c.address, "203.0.113.10");
    c.port = 40123;
    snprintf(c.replaces, sizeof c.replaces, "mco.lbsg.net");
    c.via = ROUTE_VIA_BEDROCKCONNECT;
    snprintf(c.bedrockconnect, sizeof c.bedrockconnect, "bc.example.org");
    CHECK(route_save_config(&c, msg, sizeof msg), "save: %s", msg);
    route_config back;
    CHECK(route_load_config(&back, msg, sizeof msg) && !strcmp(back.name, c.name) && !strcmp(back.address, c.address) &&
          back.port == 40123 && !strcmp(back.replaces, "mco.lbsg.net") && back.via == ROUTE_VIA_BEDROCKCONNECT &&
          !strcmp(back.bedrockconnect, "bc.example.org"), "load what was saved");
    snprintf(c.address, sizeof c.address, "127.0.0.1");
    CHECK(!route_save_config(&c, msg, sizeof msg), "loopback address refused");
    route_config partial = {0};
    snprintf(partial.name, sizeof partial.name, "Only a name");
    CHECK(route_save_config(&partial, msg, sizeof msg) && !route_load_config(&back, msg, sizeof msg),
          "partial config (no featured server) saves but is not usable yet");
    snprintf(c.bedrockconnect, sizeof c.bedrockconnect, "127.0.0.1");
    CHECK(!route_save_config(&c, msg, sizeof msg), "loopback BedrockConnect refused");
}

// BedrockLink 1.x kept server.ini in /config/bedrocklink: read once, copied over
static void test_legacy_config(void) {
    system("rm -rf sdmc: && mkdir -p 'sdmc:/config/bedrocklink'");
    put(ROUTE_LEGACY_CONFIG_PATH, CONFIG);
    route_config c;
    CHECK(route_read_config(&c) == 1 && !strcmp(c.replaces, "play.galaxite.net"), "1.x settings read");
    struct stat st;
    CHECK(stat(ROUTE_CONFIG_PATH, &st) == 0 && stat(ROUTE_LEGACY_CONFIG_PATH, &st) == 0,
          "copied to the new folder, the old file kept");
}

int main(int argc, char **argv) {
    test_legacy_config();
    test_block_edits();
    test_fields();
    if (argc > 1) test_route_on_sd(argv[1]);
    test_save_config();
    printf("route: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
