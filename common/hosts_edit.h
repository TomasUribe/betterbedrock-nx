// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink - hosts file logic (no libnx, host-testable).
//
// Atmosphere's dns.mitm reads one hosts file per boot. Each active line is
// "<ip> <pattern> [pattern...]"; '*' matches any run of characters, '%' stands
// for "lp1", and the LAST matching line wins. Lines starting with '#' are
// comments.
//
// We only ever touch lines whose every pattern is a Microsoft/Xbox/Minecraft
// host and none of which can match a Nintendo host. Those lines are disabled by
// prefixing HE_MARK (which makes them comments) and restored by removing it.
#ifndef BEDROCKLINK_HOSTS_EDIT_H
#define BEDROCKLINK_HOSTS_EDIT_H

#include <stddef.h>

#define HE_MARK "#[bedrocklink] "

enum {
    HE_BLANK = 0,
    HE_COMMENT,    // ordinary comment
    HE_OURS,       // a Microsoft line we disabled (starts with HE_MARK)
    HE_MS,         // active line, only Microsoft hosts -> we disable these
    HE_NINTENDO,   // active line matching a Nintendo host
    HE_CATCHALL,   // active pattern matching both Microsoft and Nintendo hosts (left alone)
    HE_MIXED,      // active line listing Microsoft and other hosts together (left alone)
    HE_OTHER,      // anything else
};

typedef struct {
    int ms_active;        // HE_MS lines: these break the real Microsoft sign-in
    int ms_disabled;      // HE_OURS lines
    int nintendo_active;  // HE_NINTENDO lines
    int catchall;         // HE_CATCHALL lines
    int mixed;            // HE_MIXED lines
} he_stats;

// Classify one line (without its '\n'; a trailing '\r' is ignored).
int he_classify(const char *line, size_t len);

void he_scan(const char *text, size_t len, he_stats *out);

// Calls cb for every line, with its kind. Line excludes "\r\n".
typedef void (*he_line_cb)(void *ctx, int kind, const char *line, size_t len);
void he_each(const char *text, size_t len, he_line_cb cb, void *ctx);

// Return a new malloc'd buffer (NUL-terminated, *out_len excludes the NUL), or
// NULL on allocation failure. *out_changed = number of lines rewritten.
// Everything not rewritten is copied byte for byte, line endings included.
char *he_disable_ms(const char *text, size_t len, size_t *out_len, int *out_changed);
char *he_restore(const char *text, size_t len, size_t *out_len, int *out_changed);

// Atmosphere-style wildcard match of pattern[0..plen) against a host name.
int he_glob(const char *pattern, size_t plen, const char *host);

// What a host resolves to under `extra` (Atmosphere's defaults, may be NULL) and
// then this file, which overrides them (last matching active line wins). Returns 1
// and copies the IP when some line matches, 0 when the network's DNS would answer.
int he_resolve(const char *text, size_t len, const char *extra, const char *host,
               char *ip_out, size_t ip_cap);

// 1 when every known Nintendo host resolves identically under a and b, with
// and without Atmosphere's defaults. The app refuses any write that fails this.
int he_same_nintendo(const char *a, size_t al, const char *b, size_t bl);

// Atmosphere's built-in entries, added before the file when
// add_defaults_to_dns_hosts is on.
extern const char *const HE_ATMOSPHERE_DEFAULTS;

// Server routing: one marked block at the end of the hosts file,
//   HE_ROUTE_BEGIN / "<ip> <host>" / HE_ROUTE_END
// owned by the BedrockLink overlay. Nothing outside it is ever changed.
#define HE_ROUTE_BEGIN "# --- BedrockLink route (managed by the BedrockLink overlay) ---"
#define HE_ROUTE_END "# --- end of BedrockLink route ---"

// Removes any route block, then appends one for host -> ip when enable is set.
// Returns a new malloc'd buffer (NUL-terminated) or NULL on allocation failure.
char *he_set_route(const char *text, size_t len, int enable, const char *ip, const char *host,
                   size_t *out_len);

// 1 when the text has a route block; copies its first "<ip> <host>" line's fields.
int he_get_route(const char *text, size_t len, char *ip, size_t ip_cap, char *host, size_t host_cap);

// 1 when a and b are identical once route blocks (and trailing newlines) are removed.
int he_same_outside_route(const char *a, size_t al, const char *b, size_t bl);

// 1 when host is a plain host name that may be routed: [a-z0-9.-], has a dot,
// and is neither a Nintendo host nor a Microsoft sign-in host.
int he_route_host_ok(const char *host);

#endif
