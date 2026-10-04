// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink app logic. See app.h.
#include "app.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>

#include <switch.h>

#include "featured.h"
#include "nx_control.h"
#include "raknet_ping.h"

#define APP_DIR       "sdmc:/switch/BedrockLink"
#define BACKUP_DIR    APP_DIR "/backup"
#define LOG_PATH      APP_DIR "/log.txt"
#define LOG_MAX_BYTES (256 * 1024)
#define HOSTS_DIR     "sdmc:/atmosphere/hosts"
#define SETTINGS_INI  "sdmc:/atmosphere/config/system_settings.ini"
#define SplConfigItem_ExosphereEmummcType ((SplConfigItem)65007)

enum { F_EMUMMC_ID, F_EMUMMC, F_SYSMMC, F_DEFAULT, F_COUNT };

typedef struct {
    char base[32];
    char path[96];
    char *text;
    size_t len;
    bool present;
    he_stats st;
} HostsFile;

typedef struct {
    const char *host;
    const char *label;
} Probe;

static const Probe k_ms_probes[] = {
    {"login.live.com", "login.live.com"},
    {"user.auth.xboxlive.com", "user.auth.xboxlive.com"},
    {"device.auth.xboxlive.com", "device.auth.xboxlive.com"},
    {"title.auth.xboxlive.com", "title.auth.xboxlive.com"},
    {"xsts.auth.xboxlive.com", "xsts.auth.xboxlive.com"},
};

static const Probe k_nintendo_probes[] = {
    {"accounts.nintendo.com", "Nintendo Account"},
    {"e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com", "Account tokens (baas)"},
    {"dauth-lp1.ndas.srv.nintendo.net", "Device auth (dauth)"},
    {"aauth-lp1.ndas.srv.nintendo.net", "App auth (aauth)"},
    {"receive-lp1.dg.srv.nintendo.net", "Telemetry"},
    {"receive-lp1.er.srv.nintendo.net", "Error reports"},
    {"sun.hac.lp1.d4c.nintendo.net", "System update check"},
    {"tagaya.hac.lp1.eshop.nintendo.net", "Game update versions"},
    {"ctest.cdn.nintendo.net", "Connection test"},
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static HostsFile g_files[F_COUNT];
static int g_boot = -1;
static int g_dns_mitm = 1;
static int g_add_defaults = 1;
static int g_active = -1;
static bool g_restart_needed;
static bool g_sockets;
static status_kind g_status_kind;
static char g_status[256];
static route_state g_route;
static route_config g_cfg;  // what server.ini says, complete or not
static test_result g_test, g_test_bc;

void app_set_status(status_kind kind, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_status, sizeof g_status, fmt, ap);
    va_end(ap);
    g_status_kind = kind;
}

status_kind app_status(const char **text) {
    *text = g_status;
    return g_status_kind;
}

static void log_line(const char *fmt, ...) {
    struct stat st;
    if (stat(LOG_PATH, &st) == 0 && st.st_size > LOG_MAX_BYTES) remove(LOG_PATH);
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = n >= 0 ? (char *)malloc((size_t)n + 1) : NULL;
    if (!b) {
        fclose(f);
        return NULL;
    }
    *len = fread(b, 1, (size_t)n, f);
    b[*len] = '\0';
    fclose(f);
    return b;
}

// Writes, then reads back and compares.
static bool write_file(const char *path, const char *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(buf, 1, len, f) == len;
    if (fclose(f) != 0) ok = false;
    if (!ok) return false;
    size_t rl = 0;
    char *rb = read_file(path, &rl);
    ok = rb && rl == len && memcmp(rb, buf, len) == 0;
    free(rb);
    return ok;
}

// "key = u8!0x1" in the [atmosphere] section; def when the key is absent.
static int ini_flag(const char *ini, const char *key, int def) {
    if (!ini) return def;
    bool in_section = false;
    size_t kl = strlen(key);
    for (const char *ln = ini; *ln;) {
        const char *eol = strchr(ln, '\n');
        if (!eol) eol = ln + strlen(ln);
        const char *p = ln;
        while (p < eol && (*p == ' ' || *p == '\t')) p++;
        if (p < eol && *p == '[') {
            in_section = (size_t)(eol - p) >= 12 && strncmp(p, "[atmosphere]", 12) == 0;
        } else if (in_section && (size_t)(eol - p) > kl && strncmp(p, key, kl) == 0 &&
                   (p[kl] == ' ' || p[kl] == '\t' || p[kl] == '=')) {
            const char *bang = memchr(p, '!', (size_t)(eol - p));
            if (bang) return strtol(bang + 1, NULL, 0) != 0;
        }
        ln = *eol ? eol + 1 : eol;
    }
    return def;
}

static void load_all(void) {
    char id_path[96];
    route_emummc_id_hosts_path(id_path, sizeof id_path);
    const char *slash = strrchr(id_path, '/');
    snprintf(g_files[F_EMUMMC_ID].base, sizeof g_files[0].base, "%.*s", (int)(strlen(slash + 1) - 4), slash + 1);
    snprintf(g_files[F_EMUMMC].base, sizeof g_files[0].base, "emummc");
    snprintf(g_files[F_SYSMMC].base, sizeof g_files[0].base, "sysmmc");
    snprintf(g_files[F_DEFAULT].base, sizeof g_files[0].base, "default");
    for (int i = 0; i < F_COUNT; i++) {
        HostsFile *f = &g_files[i];
        free(f->text);
        snprintf(f->path, sizeof(f->path), HOSTS_DIR "/%.31s.txt", f->base);
        f->len = 0;
        f->text = read_file(f->path, &f->len);
        f->present = f->text != NULL;
        if (f->present) he_scan(f->text, f->len, &f->st);
        else memset(&f->st, 0, sizeof(f->st));
    }

    size_t il = 0;
    char *ini = read_file(SETTINGS_INI, &il);
    g_dns_mitm = ini_flag(ini, "enable_dns_mitm", 1);
    g_add_defaults = ini_flag(ini, "add_defaults_to_dns_hosts", 1);
    free(ini);

    if (R_SUCCEEDED(splInitialize())) {
        u64 v = 0;
        if (R_SUCCEEDED(splGetConfig(SplConfigItem_ExosphereEmummcType, &v))) g_boot = v != 0;
        splExit();
    }

    // Atmosphere's order: emummc_<id>.txt, emummc.txt (or sysmmc.txt), then default.txt.
    g_active = -1;
    if (g_boot != 0) {
        if (g_files[F_EMUMMC_ID].present) g_active = F_EMUMMC_ID;
        else if (g_files[F_EMUMMC].present) g_active = F_EMUMMC;
    } else if (g_files[F_SYSMMC].present) {
        g_active = F_SYSMMC;
    }
    if (g_active < 0 && g_files[F_DEFAULT].present) g_active = F_DEFAULT;
}

// What the console resolves `host` to with the hosts file as it is now.
static int resolve_now(const char *host, char *ip, size_t cap) {
    if (!g_dns_mitm) return 0;
    const HostsFile *f = g_active >= 0 ? &g_files[g_active] : NULL;
    return he_resolve(f ? f->text : NULL, f ? f->len : 0,
                      g_add_defaults ? HE_ATMOSPHERE_DEFAULTS : NULL, host, ip, cap);
}

static int ms_blocked_count(void) {
    int n = 0;
    char ip[64];
    for (size_t i = 0; i < COUNT(k_ms_probes); i++) n += resolve_now(k_ms_probes[i].host, ip, sizeof ip);
    return n;
}

static void ms_lines(int *on, int *off) {
    *on = *off = 0;
    for (int i = 0; i < F_COUNT; i++) {
        *on += g_files[i].st.ms_active;
        *off += g_files[i].st.ms_disabled;
    }
}

static void log_state(const char *when) {
    log_line("[%s] boot=%s dns_mitm=%d add_defaults=%d active=%s", when,
             g_boot == 1 ? "emuMMC" : g_boot == 0 ? "sysMMC" : "unknown", g_dns_mitm,
             g_add_defaults, g_active >= 0 ? g_files[g_active].base : "none");
    for (int i = 0; i < F_COUNT; i++) {
        const HostsFile *f = &g_files[i];
        if (!f->present) {
            if (i != F_EMUMMC_ID) log_line("  %s.txt: absent", f->base);
            continue;
        }
        log_line("  %s.txt: ms_on=%d ms_off=%d nintendo=%d catchall=%d mixed=%d bytes=%zu", f->base,
                 f->st.ms_active, f->st.ms_disabled, f->st.nintendo_active, f->st.catchall,
                 f->st.mixed, f->len);
    }
    char ip[64];
    for (size_t i = 0; i < COUNT(k_ms_probes); i++) {
        int found = resolve_now(k_ms_probes[i].host, ip, sizeof ip);
        log_line("  %s -> %s", k_ms_probes[i].host, found ? ip : "network DNS");
    }
    for (size_t i = 0; i < COUNT(k_nintendo_probes); i++) {
        int found = resolve_now(k_nintendo_probes[i].host, ip, sizeof ip);
        log_line("  %s -> %s", k_nintendo_probes[i].host, found ? ip : "network DNS");
    }
}

// ---- Environment ----

int app_boot(void) { return g_boot; }
bool app_dns_mitm(void) { return g_dns_mitm != 0; }

const char *app_active_file(void) {
    static char name[40];
    if (g_active < 0) return "no hosts file";
    snprintf(name, sizeof name, "%.31s.txt", g_files[g_active].base);
    return name;
}

// ---- Microsoft sign-in ----

ms_fix_state app_ms_state(void) {
    int on, off;
    ms_lines(&on, &off);
    if (ms_blocked_count() || on) return MS_OFF;
    return off ? MS_ON : MS_NOT_NEEDED;
}

void app_ms_fix(bool disable) {
    mkdir(APP_DIR, 0777);
    mkdir(BACKUP_DIR, 0777);
    int written = 0, failed = 0, left_alone = 0;
    for (int i = 0; i < F_COUNT; i++) {
        HostsFile *f = &g_files[i];
        if (!f->present) continue;
        left_alone += f->st.catchall + f->st.mixed;
        size_t nl = 0;
        int changed = 0;
        char *nt = disable ? he_disable_ms(f->text, f->len, &nl, &changed)
                           : he_restore(f->text, f->len, &nl, &changed);
        if (!nt) {
            log_line("%s: out of memory", f->path);
            failed++;
            continue;
        }
        if (changed == 0) {
            free(nt);
            continue;
        }
        if (!he_same_nintendo(f->text, f->len, nt, nl)) {
            log_line("%s: REFUSED, the change would move a Nintendo host", f->path);
            free(nt);
            failed++;
            continue;
        }
        char bpath[128];
        struct stat st;
        snprintf(bpath, sizeof bpath, BACKUP_DIR "/%.31s.orig.txt", f->base);
        if (stat(bpath, &st) != 0) write_file(bpath, f->text, f->len);
        snprintf(bpath, sizeof bpath, BACKUP_DIR "/%.31s.prev.txt", f->base);
        if (!write_file(bpath, f->text, f->len)) {
            log_line("%s: backup to %s failed, file left alone", f->path, bpath);
            free(nt);
            failed++;
            continue;
        }
        if (write_file(f->path, nt, nl)) {
            log_line("%s: %s %d Microsoft line(s)", f->path, disable ? "disabled" : "restored", changed);
            written++;
        } else {
            log_line("%s: write failed, putting the original back", f->path);
            write_file(f->path, f->text, f->len);
            failed++;
        }
        free(nt);
    }
    fsdevCommitDevice("sdmc");
    bool live = false;
    if (written) {
        u32 rrc = bl_reload_hosts();
        log_line("hosts reload: %s (0x%x)", rrc == 0 ? "ok" : "failed", rrc);
        live = rrc == 0;
        if (!live) g_restart_needed = true;
    }
    load_all();
    log_state(disable ? "after sign-in fix on" : "after sign-in fix off");

    if (failed)
        app_set_status(ST_ERROR, "%d file(s) could not be changed - see /switch/BedrockLink/log.txt", failed);
    else if (written)
        app_set_status(ST_OK, "Sign-in fix %s%s", disable ? "on" : "off",
                       live ? " - in effect now." : ". Restart the console to use it.");
    else
        app_set_status(ST_INFO, "Nothing to change.");
    if (disable && left_alone) {
        size_t n = strlen(g_status);
        snprintf(g_status + n, sizeof g_status - n, " %d line(s) mixing Microsoft with other hosts were left alone.",
                 left_alone);
    }
}

// ---- Your server ----

static void load_route(void) {
    route_query(g_boot != 0, &g_route);
    if (route_read_config(&g_cfg) != 1) {
        memset(&g_cfg, 0, sizeof g_cfg);
        g_cfg.port = ROUTE_FEATURED_PORT;
        g_cfg.via = ROUTE_VIA_BEDROCKCONNECT;
        snprintf(g_cfg.bedrockconnect, sizeof g_cfg.bedrockconnect, "%s", ROUTE_PUBLIC_BEDROCKCONNECT);
    }
    if (!g_cfg.replaces[0]) snprintf(g_cfg.replaces, sizeof g_cfg.replaces, "%s", FEATURED_SERVERS[0].host);
}

// The route line holds an IPv4 address: a host name is looked up now.
static bool resolve_ipv4(const char *host, char *out, size_t cap) {
    if (route_is_ipv4(host)) {
        snprintf(out, cap, "%.15s", host);  // an IPv4 address is at most 15 characters
        return true;
    }
    if (!g_sockets) return false;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
    bool ok = inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, out, (socklen_t)cap) != NULL;
    freeaddrinfo(res);
    return ok;
}

// Writes the route line for the current settings (or removes it) and reloads the
// hosts file. Sets the status. Returns true when it is in effect now.
static bool apply_route(bool on) {
    char msg[160], target[64], ip[32] = "";
    if (on) {
        char err[128];
        if (!route_target(&g_cfg, target, sizeof target, err, sizeof err)) {
            app_set_status(ST_ERROR, "%s", err);
            return false;
        }
        if (!resolve_ipv4(target, ip, sizeof ip)) {
            app_set_status(ST_ERROR, "Can't find %.60s - check the address and the network.", target);
            return false;
        }
    }
    int ok = route_apply(g_boot != 0, on, ip, msg, sizeof msg);
    log_line("routing %s: %s", on ? "on" : "off", msg);
    if (!ok) {
        app_set_status(ST_ERROR, "%s", msg);
        load_route();
        return false;
    }
    u32 rrc = bl_reload_hosts();
    log_line("hosts reload: %s (0x%x)", rrc == 0 ? "ok" : "failed", rrc);
    if (rrc) g_restart_needed = true;
    load_route();
    app_set_status(ST_OK, "Routing %s%s", on ? "on" : "off",
                   rrc ? ". Restart the console to finish." : " - in effect now.");
    return rrc == 0;
}

// Saves g_cfg; when routing is on, points the route line at the new target.
static bool save_cfg(const char *what) {
    char msg[96];
    if (!route_save_config(&g_cfg, msg, sizeof msg)) {
        app_set_status(ST_ERROR, "%s", msg);
        load_route();
        return false;
    }
    log_line("server.ini: %s set (%s / %s:%u / replaces %s / via %s %s)", what, g_cfg.name, g_cfg.address,
             g_cfg.port, g_cfg.replaces, g_cfg.via == ROUTE_VIA_DIRECT ? "direct" : "bedrockconnect",
             g_cfg.bedrockconnect);
    load_route();
    if (g_route.hosts_route_on) {
        if (apply_route(true)) app_set_status(ST_OK, "%s saved - in effect now.", what);
    } else {
        app_set_status(ST_OK, "%s saved.", what);
    }
    return true;
}

const route_config *app_cfg(void) { return &g_cfg; }

bool app_set_name(const char *v) {
    if (!route_name_ok(v)) {
        app_set_status(ST_ERROR, "That name can't be used.");
        return false;
    }
    snprintf(g_cfg.name, sizeof g_cfg.name, "%s", v);
    return save_cfg("Name");
}

bool app_set_address(const char *v) {
    if (!route_address_ok(v)) {
        app_set_status(ST_ERROR, "\"%.60s\" can't be used as the server address.", v);
        return false;
    }
    snprintf(g_cfg.address, sizeof g_cfg.address, "%s", v);
    g_test.kind = TEST_NONE;
    return save_cfg("Address");
}

bool app_set_port_text(const char *v) {
    char *end = NULL;
    unsigned long p = strtoul(v, &end, 10);
    if (!v[0] || (end && *end) || p == 0 || p > 65535) {
        app_set_status(ST_ERROR, "Port must be a number from 1 to 65535.");
        return false;
    }
    g_cfg.port = (unsigned)p;
    g_test.kind = TEST_NONE;
    return save_cfg("Port");
}

void app_cycle_featured(int dir) {
    int i = featured_index(g_cfg.replaces);
    i = i < 0 ? 0 : (i + dir + FEATURED_COUNT) % FEATURED_COUNT;
    snprintf(g_cfg.replaces, sizeof g_cfg.replaces, "%s", FEATURED_SERVERS[i].host);
    save_cfg("Featured server");
}

bool app_set_featured_host(const char *v) {
    char host[sizeof g_cfg.replaces];
    snprintf(host, sizeof host, "%s", v);
    for (char *p = host; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (!he_route_host_ok(host)) {
        app_set_status(ST_ERROR, "\"%.60s\" can't be routed.", host);
        return false;
    }
    snprintf(g_cfg.replaces, sizeof g_cfg.replaces, "%s", host);
    return save_cfg("Featured server");
}

void app_toggle_via(void) {
    g_cfg.via = g_cfg.via == ROUTE_VIA_DIRECT ? ROUTE_VIA_BEDROCKCONNECT : ROUTE_VIA_DIRECT;
    save_cfg("Route");
}

bool app_set_bedrockconnect(const char *v) {
    if (!route_address_ok(v)) {
        app_set_status(ST_ERROR, "\"%.60s\" can't be used.", v);
        return false;
    }
    snprintf(g_cfg.bedrockconnect, sizeof g_cfg.bedrockconnect, "%s", v);
    g_test_bc.kind = TEST_NONE;
    return save_cfg("BedrockConnect server");
}

const char *app_featured_label(void) {
    const char *n = featured_name(g_cfg.replaces);
    return n ? n : g_cfg.replaces;
}

bool app_direct_needs_19132(void) {
    return g_cfg.via == ROUTE_VIA_DIRECT && g_cfg.port != ROUTE_FEATURED_PORT;
}

static void ping_into(test_result *out, const char *address, unsigned port, bool brief) {
    raknet_status st;
    int r = raknet_ping(address, port, 3000, &st);
    out->kind = r == PING_OK ? TEST_OK : TEST_FAIL;
    if (r == PING_OK && brief)
        snprintf(out->text, sizeof out->text, "Reachable - %d ms", st.rtt_ms);
    else if (r == PING_OK)
        snprintf(out->text, sizeof out->text, "Reachable - %d ms - %d/%d players - version %s", st.rtt_ms, st.players,
                 st.max_players, st.version);
    else if (r == PING_NO_ADDRESS)
        snprintf(out->text, sizeof out->text, "Can't find %.60s", address);
    else if (r == PING_TIMEOUT)
        snprintf(out->text, sizeof out->text, "No answer from %.50s:%u", address, port);
    else
        snprintf(out->text, sizeof out->text, "Network error");
    log_line("test %s:%u -> %d (%s, %d ms)", address, port, r, r == PING_OK ? st.name : "-",
             r == PING_OK ? st.rtt_ms : 0);
}

void app_test(void) {
    if (!g_sockets) {
        g_test.kind = TEST_FAIL;
        snprintf(g_test.text, sizeof g_test.text, "Network unavailable");
        return;
    }
    if (!route_address_ok(g_cfg.address) || g_cfg.port == 0) {
        g_test.kind = TEST_FAIL;
        snprintf(g_test.text, sizeof g_test.text, "Set the address and port first");
    } else {
        ping_into(&g_test, g_cfg.address, g_cfg.port, false);
    }
    if (g_cfg.via == ROUTE_VIA_BEDROCKCONNECT && route_address_ok(g_cfg.bedrockconnect))
        ping_into(&g_test_bc, g_cfg.bedrockconnect, ROUTE_FEATURED_PORT, true);
    else
        g_test_bc.kind = TEST_NONE;
}

const test_result *app_test_server(void) { return &g_test; }
const test_result *app_test_bc(void) { return &g_test_bc; }

// ---- Routing ----

routing_state app_routing(void) {
    bool want = g_route.hosts_route_on;
    bool current = want;
    if (want) {
        // The line must be for the chosen featured server and, when the target is an IPv4
        // address, for that address. (A host-name target was looked up when it was applied;
        // the screen never does network lookups.)
        char target[64], err[128];
        if (strcmp(g_route.hosts_route_host, g_cfg.replaces) != 0) current = false;
        else if (route_target(&g_cfg, target, sizeof target, err, sizeof err) && route_is_ipv4(target) &&
                 strcmp(target, g_route.hosts_route_ip) != 0)
            current = false;
    }
    if (want && current && g_route.dns_active) return ROUTING_ON;
    if (!want && !g_route.dns_active) return ROUTING_OFF;
    if (want && !current) return ROUTING_STALE;
    return want ? ROUTING_LOADING : ROUTING_LINGERING;
}

void app_routing_toggle(void) {
    if (g_boot == 0) {
        app_set_status(ST_ERROR, "This is a sysMMC boot: routing is for emuMMC only.");
        return;
    }
    apply_route(!g_route.hosts_route_on);
}

bool app_restart_needed(void) { return g_restart_needed; }

void app_restart(void) {
    log_line("restart requested");
    fsdevCommitDevice("sdmc");
    Result rc = bpcInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = bpcRebootSystem();
        bpcExit();
    }
    app_set_status(ST_ERROR, "Restart failed (0x%x) - restart from the Power menu.", rc);
}

// ---- Details page ----

int app_ms_probe_count(void) { return (int)COUNT(k_ms_probes); }

const char *app_ms_probe(int i, bool *redirected, char *ip, size_t cap) {
    ip[0] = '\0';
    *redirected = resolve_now(k_ms_probes[i].host, ip, cap) != 0;
    return k_ms_probes[i].label;
}

int app_nintendo_probe_count(void) { return (int)COUNT(k_nintendo_probes); }

const char *app_nintendo_probe(int i, nintendo_route *how, char *ip, size_t cap) {
    ip[0] = '\0';
    if (!resolve_now(k_nintendo_probes[i].host, ip, cap)) *how = NIN_YOUR_DNS;
    else if (!strcmp(ip, "127.0.0.1") || !strcmp(ip, "0.0.0.0")) *how = NIN_BLOCKED;
    else *how = NIN_REDIRECTED;
    return k_nintendo_probes[i].label;
}

int app_hosts_file_count(void) { return F_COUNT; }

bool app_hosts_file(int i, char *name, size_t cap, bool *active, he_stats *st) {
    snprintf(name, cap, "%.31s.txt", g_files[i].base);
    *active = i == g_active;
    *st = g_files[i].st;
    return g_files[i].present;
}

// ---- Lifetime ----

void app_init(bool sockets) {
    g_sockets = sockets;
    mkdir(APP_DIR, 0777);
    log_line("BedrockLink " APP_VERSION_STR " start (sockets %s)", sockets ? "up" : "unavailable");
    load_all();
    load_route();
    log_state("start");
}

void app_exit(void) {
    for (int i = 0; i < F_COUNT; i++) {
        free(g_files[i].text);
        g_files[i].text = NULL;
    }
}
