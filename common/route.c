// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink server routing. See route.h.
#include "route.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hosts_edit.h"

static char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = n >= 0 ? (char *)malloc((size_t)n + 1) : NULL;
    if (b) {
        *len = fread(b, 1, (size_t)n, f);
        b[*len] = '\0';
    }
    fclose(f);
    return b;
}

static int write_all(const char *path, const char *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(buf, 1, len, f) == len;
    if (fclose(f) != 0) ok = 0;
    if (!ok) return 0;
    size_t rl = 0;
    char *rb = read_all(path, &rl);
    ok = rb && rl == len && memcmp(rb, buf, len) == 0;
    free(rb);
    return ok;
}

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void mkdirs(const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s", path);
    char *p = strchr(tmp, ':');
    p = p ? p + 2 : tmp + 1;
    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    char *b = s;
    while (*b == ' ' || *b == '\t') b++;
    if (b != s) memmove(s, b, strlen(b) + 1);
}

// 0 when src does not fit (nothing is cut short silently).
static int copy(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) return 0;
    memcpy(dst, src, n + 1);
    return 1;
}

int route_read_config(route_config *c) {
    memset(c, 0, sizeof *c);
    c->via = ROUTE_VIA_BEDROCKCONNECT;
    size_t len;
    char *text = read_all(ROUTE_CONFIG_PATH, &len);
    if (!text) return 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        *eq = '\0';
        char key[32], val[160];
        snprintf(key, sizeof key, "%s", line);
        snprintf(val, sizeof val, "%s", eq + 1);
        trim(key);
        trim(val);
        int fits = 1;
        if (!strcmp(key, "name")) fits = copy(c->name, sizeof c->name, val);
        else if (!strcmp(key, "address")) fits = copy(c->address, sizeof c->address, val);
        else if (!strcmp(key, "port")) c->port = (unsigned)strtoul(val, NULL, 10);
        else if (!strcmp(key, "replaces")) fits = copy(c->replaces, sizeof c->replaces, val);
        else if (!strcmp(key, "via")) c->via = !strcmp(val, "direct") ? ROUTE_VIA_DIRECT : ROUTE_VIA_BEDROCKCONNECT;
        else if (!strcmp(key, "bedrockconnect")) fits = copy(c->bedrockconnect, sizeof c->bedrockconnect, val);
        if (!fits) {
            free(text);
            return -1;
        }
    }
    free(text);
    for (char *p = c->replaces; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    if (!c->bedrockconnect[0]) snprintf(c->bedrockconnect, sizeof c->bedrockconnect, "%s", ROUTE_PUBLIC_BEDROCKCONNECT);
    return 1;
}

int route_load_config(route_config *c, char *err, size_t err_cap) {
    int r = route_read_config(c);
    if (r == 0) {
        snprintf(err, err_cap, "No %s", ROUTE_CONFIG_PATH + 6);
        return 0;
    }
    if (r < 0) {
        snprintf(err, err_cap, "server.ini: a value is too long");
        return 0;
    }
    if (!he_route_host_ok(c->replaces)) {
        snprintf(err, err_cap, "server.ini: 'replaces' is not a host that can be routed");
        return 0;
    }
    char target[64];
    if (!route_target(c, target, sizeof target, err, err_cap)) return 0;
    if (!c->name[0]) snprintf(c->name, sizeof c->name, "%s", c->address[0] ? c->address : "Your server");
    return 1;
}

int route_is_ipv4(const char *s) {
    unsigned a[4];
    char tail;
    if (sscanf(s, "%u.%u.%u.%u%c", &a[0], &a[1], &a[2], &a[3], &tail) != 4) return 0;
    for (int i = 0; i < 4; i++)
        if (a[i] > 255) return 0;
    return 1;
}

int route_target(const route_config *c, char *out, size_t cap, char *err, size_t err_cap) {
    if (c->via == ROUTE_VIA_DIRECT) {
        if (!route_address_ok(c->address)) {
            snprintf(err, err_cap, "Set your server's address first");
            return 0;
        }
        if (c->port != ROUTE_FEATURED_PORT) {
            snprintf(err, err_cap, "Direct needs your server on port %u (it uses %u): use BedrockConnect",
                     ROUTE_FEATURED_PORT, c->port);
            return 0;
        }
        snprintf(out, cap, "%s", c->address);
        return 1;
    }
    if (!route_address_ok(c->bedrockconnect)) {
        snprintf(err, err_cap, "The BedrockConnect server address can't be used");
        return 0;
    }
    snprintf(out, cap, "%s", c->bedrockconnect);
    return 1;
}

static unsigned long emummc_id(void) {
    size_t len;
    char *ini = read_all("sdmc:/emuMMC/emummc.ini", &len);
    unsigned long id = 0;
    if (ini) {
        char *p = strstr(ini, "\nid=");
        if (p) id = strtoul(p + 4, NULL, 0);
        free(ini);
    }
    return id;
}

void route_emummc_id_hosts_path(char *out, size_t cap) {
    snprintf(out, cap, "sdmc:/atmosphere/hosts/emummc_%04lx.txt", emummc_id());
}

int route_active_hosts_path(int emummc_boot, char *out, size_t cap) {
    char cand[3][128];
    int n = 0;
    if (emummc_boot) {
        snprintf(cand[n++], sizeof cand[0], "sdmc:/atmosphere/hosts/emummc_%04lx.txt", emummc_id());
        snprintf(cand[n++], sizeof cand[0], "sdmc:/atmosphere/hosts/emummc.txt");
    } else {
        snprintf(cand[n++], sizeof cand[0], "sdmc:/atmosphere/hosts/sysmmc.txt");
    }
    snprintf(cand[n++], sizeof cand[0], "sdmc:/atmosphere/hosts/default.txt");
    for (int i = 0; i < n; i++) {
        if (exists(cand[i])) {
            snprintf(out, cap, "%s", cand[i]);
            return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

// dns.mitm's startup log lists every redirection it loaded at this boot as
// "    `host` -> ip".
static int dns_loaded(const char *host) {
    size_t len;
    char *log = read_all(ROUTE_DNS_STARTUP_LOG, &len);
    if (!log) return 0;
    char needle[200];
    snprintf(needle, sizeof needle, "`%s` -> ", host);
    int found = strstr(log, needle) != NULL;
    free(log);
    return found;
}

void route_query(int emummc_boot, route_state *st) {
    memset(st, 0, sizeof *st);
    st->config_ok = route_load_config(&st->cfg, st->config_error, sizeof st->config_error);
    if (route_active_hosts_path(emummc_boot, st->hosts_path, sizeof st->hosts_path)) {
        size_t len;
        char *text = read_all(st->hosts_path, &len);
        if (text) {
            st->hosts_route_on = he_get_route(text, len, st->hosts_route_ip, sizeof st->hosts_route_ip,
                                              st->hosts_route_host, sizeof st->hosts_route_host);
            free(text);
        }
    }
    // Atmosphere's startup log lists what dns.mitm loaded, at boot and at each reload.
    const char *host = st->hosts_route_on ? st->hosts_route_host : st->cfg.replaces;
    if (host[0]) st->dns_active = dns_loaded(host);
}

static int backup(const char *path, const char *text, size_t len) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    char b[192];
    mkdirs(ROUTE_BACKUP_DIR);
    snprintf(b, sizeof b, ROUTE_BACKUP_DIR "/%s.orig", name);
    if (!exists(b)) write_all(b, text, len);
    snprintf(b, sizeof b, ROUTE_BACKUP_DIR "/%s.prev", name);
    return write_all(b, text, len);
}

// Puts the route block into (or takes it out of) one hosts file. 1 = ok or nothing to do.
static int edit_hosts(const char *path, int enable, const char *ip, const char *host, char *msg, size_t msg_cap) {
    size_t len = 0;
    char *text = read_all(path, &len);
    if (!text) {
        if (!enable) return 1;
        text = (char *)calloc(1, 1);  // no hosts file at all: Atmosphere reads none, so start one
        if (!text) return 0;
    }
    size_t nl = 0;
    char *nt = he_set_route(text, len, enable, ip, host, &nl);
    int ok = 0;
    if (!nt) {
        snprintf(msg, msg_cap, "Out of memory");
    } else if (nl == len && memcmp(nt, text, len) == 0) {
        ok = 1;
    } else if (!he_same_outside_route(text, len, nt, nl) || !he_same_nintendo(text, len, nt, nl)) {
        snprintf(msg, msg_cap, "Refused: the change would touch other lines");
    } else if (len && !backup(path, text, len)) {
        snprintf(msg, msg_cap, "Backup failed, nothing changed");
    } else if (!write_all(path, nt, nl)) {
        write_all(path, text, len);
        snprintf(msg, msg_cap, "Writing the hosts file failed");
    } else {
        ok = 1;
    }
    free(nt);
    free(text);
    return ok;
}

int route_apply(int emummc_boot, int enable, const char *ip, char *msg, size_t msg_cap) {
    if (enable) {
        route_config c;
        char err[96];
        if (!route_load_config(&c, err, sizeof err)) {
            snprintf(msg, msg_cap, "%s", err);
            return 0;
        }
        if (!ip || !route_is_ipv4(ip) || !route_address_ok(ip)) {
            snprintf(msg, msg_cap, "No usable address to route to");
            return 0;
        }
        char path[128];
        if (!route_active_hosts_path(emummc_boot, path, sizeof path))
            snprintf(path, sizeof path, "sdmc:/atmosphere/hosts/default.txt");
        mkdirs("sdmc:/atmosphere/hosts");
        if (!edit_hosts(path, 1, ip, c.replaces, msg, msg_cap)) return 0;
        snprintf(msg, msg_cap, "%s -> %s", c.replaces, ip);
        return 1;
    }
    // Off: take the block out of every hosts file that has one.
    static const char *const all[] = {
        "sdmc:/atmosphere/hosts/emummc.txt", "sdmc:/atmosphere/hosts/sysmmc.txt",
        "sdmc:/atmosphere/hosts/default.txt", NULL};
    char idpath[128];
    route_emummc_id_hosts_path(idpath, sizeof idpath);
    int ok = 1;
    for (int i = 0; all[i]; i++) ok &= edit_hosts(all[i], 0, "", "", msg, msg_cap);
    ok &= edit_hosts(idpath, 0, "", "", msg, msg_cap);
    if (ok) snprintf(msg, msg_cap, "Routing off");
    return ok;
}

int route_name_ok(const char *name) {
    size_t n = strlen(name);
    if (n == 0 || n >= sizeof(((route_config *)0)->name)) return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)name[i] < 0x20 || name[i] == ';' || name[i] == '[') return 0;
    return 1;
}

int route_address_ok(const char *address) {
    size_t n = strlen(address);
    if (n == 0 || n >= sizeof(((route_config *)0)->address)) return 0;
    int digits_and_dots = 1;
    for (size_t i = 0; i < n; i++) {
        char c = address[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '-') return 0;
        if (!isdigit((unsigned char)c) && c != '.') digits_and_dots = 0;
    }
    if (digits_and_dots) {
        unsigned a[4];
        char tail;
        if (sscanf(address, "%u.%u.%u.%u%c", &a[0], &a[1], &a[2], &a[3], &tail) != 4) return 0;
        for (int i = 0; i < 4; i++)
            if (a[i] > 255) return 0;
        if (a[0] == 0 || a[0] == 127 || (a[0] == 255 && a[1] == 255 && a[2] == 255 && a[3] == 255)) return 0;
        return 1;
    }
    char lower[64];
    for (size_t i = 0; i <= n; i++) lower[i] = (char)tolower((unsigned char)address[i]);
    return he_route_host_ok(lower);
}

int route_save_config(const route_config *c, char *msg, size_t msg_cap) {
    if (c->name[0] && !route_name_ok(c->name)) {
        snprintf(msg, msg_cap, "That name can't be used");
        return 0;
    }
    if (c->address[0] && !route_address_ok(c->address)) {
        snprintf(msg, msg_cap, "That address can't be used");
        return 0;
    }
    if (c->port > 65535) {
        snprintf(msg, msg_cap, "Port must be 1-65535");
        return 0;
    }
    if (c->replaces[0] && !he_route_host_ok(c->replaces)) {
        snprintf(msg, msg_cap, "That featured server host can't be used");
        return 0;
    }
    if (c->bedrockconnect[0] && !route_address_ok(c->bedrockconnect)) {
        snprintf(msg, msg_cap, "That BedrockConnect address can't be used");
        return 0;
    }
    char text[1100];
    int n = snprintf(text, sizeof text,
                     "; BedrockLink server routing. Set it in the BedrockLink app; switch it on or off\n"
                     "; in the app or the BedrockLink overlay.\n"
                     "[server]\n"
                     "name = %s\n"
                     "address = %s\n"
                     "port = %u\n"
                     "; Featured server (Play > Servers) that takes you there while routing is on.\n"
                     "replaces = %s\n"
                     "; bedrockconnect: the featured server opens BedrockConnect's menu, where you pick\n"
                     ";                 your server (any port). direct: straight to your server, which\n"
                     ";                 must then use port 19132.\n"
                     "via = %s\n"
                     "bedrockconnect = %s\n",
                     c->name, c->address, c->port, c->replaces,
                     c->via == ROUTE_VIA_DIRECT ? "direct" : "bedrockconnect", c->bedrockconnect);
    mkdirs("sdmc:/config/bedrocklink");
    if (n <= 0 || (size_t)n >= sizeof text || !write_all(ROUTE_CONFIG_PATH, text, (size_t)n)) {
        snprintf(msg, msg_cap, "Saving server.ini failed");
        return 0;
    }
    snprintf(msg, msg_cap, "Saved");
    return 1;
}
