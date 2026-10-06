// SPDX-License-Identifier: GPL-2.0-or-later
// BetterBedrock NX - hosts file logic. See hosts_edit.h.
#include "hosts_edit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Telemetry entries Atmosphere merges after the file (add_defaults_to_dns_hosts).
const char *const HE_ATMOSPHERE_DEFAULTS =
    "127.0.0.1 receive-%.dg.srv.nintendo.net\n"
    "127.0.0.1 receive-%.er.srv.nintendo.net\n";

// Domains whose redirection breaks the Minecraft / Xbox Live sign-in.
static const char *const k_ms_domains[] = {
    "live.com", "live.net", "xboxlive.com", "xboxservices.com", "xbox.com",
    "minecraftservices.com", "minecraft-services.net", "minecraft.net", "mojang.com",
    "playfabapi.com", "playfab.com", "microsoft.com", "microsoftonline.com",
    "msftauth.net", "msauth.net", "aka.ms",
};
// Hosts the sign-in actually uses; any pattern matching one counts as Microsoft.
static const char *const k_ms_samples[] = {
    "login.live.com", "user.auth.xboxlive.com", "device.auth.xboxlive.com",
    "title.auth.xboxlive.com", "xsts.auth.xboxlive.com", "sisu.xboxlive.com",
};
static const char *const k_nintendo_domains[] = {
    "nintendo.com", "nintendo.net", "nintendo.co.jp", "nintendo.jp",
    "nintendowifi.net", "nintendo.co.uk", "nintendo-europe.com",
};
// A pattern matching any of these is never touched, whatever else it matches.
static const char *const k_nintendo_samples[] = {
    "accounts.nintendo.com", "api.accounts.nintendo.com",
    "e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com",
    "dauth-lp1.ndas.srv.nintendo.net", "aauth-lp1.ndas.srv.nintendo.net",
    "receive-lp1.dg.srv.nintendo.net", "receive-lp1.er.srv.nintendo.net",
    "sun.hac.lp1.d4c.nintendo.net", "atumn.hac.lp1.d4c.nintendo.net",
    "tagaya.hac.lp1.eshop.nintendo.net", "ctest.cdn.nintendo.net",
    "conntest.nintendowifi.net", "bcat-list-lp1.cdn.nintendo.net",
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static int glob_at(const char *p, const char *pe, const char *h) {
    while (p < pe) {
        char c = lc(*p);
        if (c == '*') {
            while (p < pe && *p == '*') p++;
            if (p == pe) return 1;
            for (const char *t = h;; t++) {
                if (glob_at(p, pe, t)) return 1;
                if (!*t) return 0;
            }
        } else if (c == '%') {
            if (lc(h[0]) != 'l' || h[0] == '\0' || lc(h[1]) != 'p' || h[2] != '1') return 0;
            h += 3;
            p++;
        } else {
            if (!*h || lc(*h) != c) return 0;
            h++;
            p++;
        }
    }
    return *h == '\0';
}

int he_glob(const char *pattern, size_t plen, const char *host) {
    return glob_at(pattern, pattern + plen, host);
}

// pattern == dom, or ends with ".dom" or "*dom" (case-insensitive).
static int ends_with_domain(const char *p, size_t plen, const char *dom) {
    size_t dl = strlen(dom);
    if (plen < dl) return 0;
    for (size_t i = 0; i < dl; i++)
        if (lc(p[plen - dl + i]) != dom[i]) return 0;
    if (plen == dl) return 1;
    char before = p[plen - dl - 1];
    return before == '.' || before == '*';
}

static int matches_any(const char *p, size_t plen, const char *const *domains, size_t nd,
                       const char *const *samples, size_t ns) {
    for (size_t i = 0; i < nd; i++)
        if (ends_with_domain(p, plen, domains[i])) return 1;
    for (size_t i = 0; i < ns; i++)
        if (he_glob(p, plen, samples[i])) return 1;
    return 0;
}

static int is_ms(const char *p, size_t plen) {
    return matches_any(p, plen, k_ms_domains, COUNT(k_ms_domains), k_ms_samples, COUNT(k_ms_samples));
}
static int hits_nintendo(const char *p, size_t plen) {
    return matches_any(p, plen, k_nintendo_domains, COUNT(k_nintendo_domains),
                       k_nintendo_samples, COUNT(k_nintendo_samples));
}

// Next whitespace-separated token in [*s, e), stopping at '#'. 0 when none.
static int next_token(const char **s, const char *e, const char **tok, size_t *tlen) {
    const char *p = *s;
    while (p < e && is_ws(*p)) p++;
    if (p >= e || *p == '#') return 0;
    const char *t = p;
    while (p < e && !is_ws(*p) && *p != '#') p++;
    *tok = t;
    *tlen = (size_t)(p - t);
    *s = p;
    return 1;
}

int he_classify(const char *line, size_t len) {
    if (len && line[len - 1] == '\r') len--;
    const size_t ml = strlen(HE_MARK);
    if (len >= ml && memcmp(line, HE_MARK, ml) == 0) return HE_OURS;

    const char *s = line, *e = line + len;
    while (s < e && is_ws(*s)) s++;
    if (s == e) return HE_BLANK;
    if (*s == '#') return HE_COMMENT;

    const char *tok;
    size_t tl;
    if (!next_token(&s, e, &tok, &tl)) return HE_OTHER;  // the IP
    int nhosts = 0, n_ms = 0, n_catch = 0, n_nin = 0;
    while (next_token(&s, e, &tok, &tl)) {
        nhosts++;
        int ms = is_ms(tok, tl), nin = hits_nintendo(tok, tl);
        if (ms && nin) n_catch++;
        else if (ms) n_ms++;
        if (nin) n_nin++;
    }
    if (nhosts == 0) return HE_OTHER;
    if (n_catch) return HE_CATCHALL;
    if (n_ms && n_ms != nhosts) return HE_MIXED;
    if (n_ms) return HE_MS;
    if (n_nin) return HE_NINTENDO;
    return HE_OTHER;
}

// Splits text into lines: [*ls, *ls + *llen) excludes the '\n'.
typedef struct { const char *p, *end; } line_iter;

static int next_line(line_iter *it, const char **ls, size_t *llen, int *has_nl) {
    if (it->p >= it->end) return 0;
    const char *nl = (const char *)memchr(it->p, '\n', (size_t)(it->end - it->p));
    *ls = it->p;
    *llen = (size_t)((nl ? nl : it->end) - it->p);
    *has_nl = nl != NULL;
    it->p = nl ? nl + 1 : it->end;
    return 1;
}

void he_each(const char *text, size_t len, he_line_cb cb, void *ctx) {
    line_iter it = {text, text + len};
    const char *ls;
    size_t llen;
    int has_nl;
    while (next_line(&it, &ls, &llen, &has_nl)) {
        size_t vl = (llen && ls[llen - 1] == '\r') ? llen - 1 : llen;
        cb(ctx, he_classify(ls, llen), ls, vl);
    }
}

void he_scan(const char *text, size_t len, he_stats *out) {
    memset(out, 0, sizeof(*out));
    line_iter it = {text, text + len};
    const char *ls;
    size_t llen;
    int has_nl;
    while (next_line(&it, &ls, &llen, &has_nl)) {
        switch (he_classify(ls, llen)) {
            case HE_MS: out->ms_active++; break;
            case HE_OURS: out->ms_disabled++; break;
            case HE_NINTENDO: out->nintendo_active++; break;
            case HE_CATCHALL: out->catchall++; break;
            case HE_MIXED: out->mixed++; break;
            default: break;
        }
    }
}

static char *rewrite(const char *text, size_t len, int disable, size_t *out_len, int *out_changed) {
    const size_t ml = strlen(HE_MARK);
    size_t lines = 1;
    for (size_t i = 0; i < len; i++)
        if (text[i] == '\n') lines++;
    char *out = (char *)malloc(len + lines * ml + 1);
    if (!out) return NULL;
    size_t o = 0;
    int changed = 0;
    line_iter it = {text, text + len};
    const char *ls;
    size_t llen;
    int has_nl;
    while (next_line(&it, &ls, &llen, &has_nl)) {
        int kind = he_classify(ls, llen);
        const char *src = ls;
        size_t n = llen;
        if (disable && kind == HE_MS) {
            memcpy(out + o, HE_MARK, ml);
            o += ml;
            changed++;
        } else if (!disable && kind == HE_OURS) {
            src += ml;
            n -= ml;
            changed++;
        }
        memcpy(out + o, src, n);
        o += n;
        if (has_nl) out[o++] = '\n';
    }
    out[o] = '\0';
    *out_len = o;
    *out_changed = changed;
    return out;
}

char *he_disable_ms(const char *text, size_t len, size_t *out_len, int *out_changed) {
    return rewrite(text, len, 1, out_len, out_changed);
}

char *he_restore(const char *text, size_t len, size_t *out_len, int *out_changed) {
    return rewrite(text, len, 0, out_len, out_changed);
}

static int resolve_in(const char *text, size_t len, const char *host, char *ip, size_t cap) {
    int found = 0;
    line_iter it = {text, text + len};
    const char *ls;
    size_t llen;
    int has_nl;
    while (next_line(&it, &ls, &llen, &has_nl)) {
        const char *s = ls, *e = ls + llen, *ipt, *tok;
        size_t ipl, tl;
        if (!next_token(&s, e, &ipt, &ipl)) continue;  // blank or comment
        while (next_token(&s, e, &tok, &tl)) {
            if (he_glob(tok, tl, host)) {
                size_t n = ipl < cap - 1 ? ipl : cap - 1;
                memcpy(ip, ipt, n);
                ip[n] = '\0';
                found = 1;
            }
        }
    }
    return found;
}

int he_resolve(const char *text, size_t len, const char *extra, const char *host,
               char *ip_out, size_t ip_cap) {
    // Atmosphere adds its defaults first; lines from the hosts file override them
    // (dns_mitm_startup.log on the console shows the file's receive-% entries winning).
    int found = extra ? resolve_in(extra, strlen(extra), host, ip_out, ip_cap) : 0;
    if (text && resolve_in(text, len, host, ip_out, ip_cap)) found = 1;
    return found;
}

int he_same_nintendo(const char *a, size_t al, const char *b, size_t bl) {
    for (size_t i = 0; i < COUNT(k_nintendo_samples); i++) {
        for (int d = 0; d < 2; d++) {
            const char *extra = d ? HE_ATMOSPHERE_DEFAULTS : NULL;
            char ia[64] = "", ib[64] = "";
            int fa = he_resolve(a, al, extra, k_nintendo_samples[i], ia, sizeof ia);
            int fb = he_resolve(b, bl, extra, k_nintendo_samples[i], ib, sizeof ib);
            if (fa != fb || strcmp(ia, ib) != 0) return 0;
        }
    }
    return 1;
}

static int line_is(const char *ls, size_t llen, const char *s) {
    if (llen && ls[llen - 1] == '\r') llen--;
    return llen == strlen(s) && memcmp(ls, s, llen) == 0;
}

// Copies text without route blocks into out (capacity len + 1); returns its length.
static size_t strip_route(const char *text, size_t len, char *out, int *found) {
    line_iter it = {text, text + len};
    const char *ls;
    size_t llen, o = 0;
    int has_nl, in_block = 0;
    if (found) *found = 0;
    while (next_line(&it, &ls, &llen, &has_nl)) {
        if (!in_block && line_is(ls, llen, HE_ROUTE_BEGIN)) {
            in_block = 1;
            if (found) *found = 1;
            continue;
        }
        if (in_block) {
            if (line_is(ls, llen, HE_ROUTE_END)) in_block = 0;
            continue;
        }
        memcpy(out + o, ls, llen);
        o += llen;
        if (has_nl) out[o++] = '\n';
    }
    out[o] = '\0';
    return o;
}

char *he_set_route(const char *text, size_t len, int enable, const char *ip, const char *host,
                   size_t *out_len) {
    size_t extra = enable ? strlen(HE_ROUTE_BEGIN) + strlen(ip) + strlen(host) + strlen(HE_ROUTE_END) + 8 : 0;
    char *out = (char *)malloc(len + extra + 1);
    if (!out) return NULL;
    size_t o = strip_route(text, len, out, NULL);
    if (enable) {
        if (o && out[o - 1] != '\n') out[o++] = '\n';
        o += (size_t)sprintf(out + o, "%s\n%s %s\n%s\n", HE_ROUTE_BEGIN, ip, host, HE_ROUTE_END);
    }
    *out_len = o;
    return out;
}

int he_get_route(const char *text, size_t len, char *ip, size_t ip_cap, char *host, size_t host_cap) {
    line_iter it = {text, text + len};
    const char *ls;
    size_t llen;
    int has_nl, in_block = 0;
    while (next_line(&it, &ls, &llen, &has_nl)) {
        if (!in_block) {
            in_block = line_is(ls, llen, HE_ROUTE_BEGIN);
            continue;
        }
        if (line_is(ls, llen, HE_ROUTE_END)) return 0;
        const char *s = ls, *e = ls + llen, *a, *b;
        size_t al, bl;
        if (!next_token(&s, e, &a, &al) || !next_token(&s, e, &b, &bl)) continue;
        if (al >= ip_cap || bl >= host_cap) return 0;
        memcpy(ip, a, al);
        ip[al] = '\0';
        memcpy(host, b, bl);
        host[bl] = '\0';
        return 1;
    }
    return 0;
}

int he_same_outside_route(const char *a, size_t al, const char *b, size_t bl) {
    char *sa = (char *)malloc(al + 1), *sb = (char *)malloc(bl + 1);
    int same = 0;
    if (sa && sb) {
        size_t na = strip_route(a, al, sa, NULL), nb = strip_route(b, bl, sb, NULL);
        while (na && (sa[na - 1] == '\n' || sa[na - 1] == '\r')) na--;
        while (nb && (sb[nb - 1] == '\n' || sb[nb - 1] == '\r')) nb--;
        same = na == nb && memcmp(sa, sb, na) == 0;
    }
    free(sa);
    free(sb);
    return same;
}

int he_route_host_ok(const char *host) {
    size_t n = strlen(host);
    if (n < 4 || n > 253 || !strchr(host, '.') || host[0] == '.' || host[0] == '-') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return 0;
    }
    return !is_ms(host, n) && !hits_nintendo(host, n);
}
