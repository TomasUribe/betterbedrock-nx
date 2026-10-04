// SPDX-License-Identifier: GPL-2.0-or-later
// PC self-test for hosts_edit.c. Run: tests/run_pc_tests.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/hosts_edit.h"

static int g_fail, g_pass;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *k_ms_hosts[] = {
    "login.live.com", "user.auth.xboxlive.com", "device.auth.xboxlive.com",
    "title.auth.xboxlive.com", "xsts.auth.xboxlive.com",
};
static const char *k_nintendo_hosts[] = {
    "accounts.nintendo.com", "api.accounts.nintendo.com",
    "e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com", "m-lp1.baas.nintendo.com",
    "dauth-lp1.ndas.srv.nintendo.net", "aauth-lp1.ndas.srv.nintendo.net",
    "receive-lp1.dg.srv.nintendo.net", "receive-lp1.er.srv.nintendo.net",
    "sun.hac.lp1.d4c.nintendo.net", "atumn.hac.lp1.d4c.nintendo.net",
    "tagaya.hac.lp1.eshop.nintendo.net", "ctest.cdn.nintendo.net",
    "conntest.nintendowifi.net", "bcat-list-lp1.cdn.nintendo.net",
    "g2b309e01-lp1.s.n.srv.nintendo.net", "lp1.nso.nintendo.net",
    "nncs1-lp1.n.n.srv.nintendo.net", "nncs2-lp1.n.n.srv.nintendo.net",
};
#define N(a) (sizeof(a) / sizeof((a)[0]))

static char *read_file(const char *path, size_t *len) {
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

static int count_lines(const char *t, size_t n) {
    int c = 0;
    for (size_t i = 0; i < n; i++) c += t[i] == '\n';
    return c;
}

// Nintendo hosts must resolve exactly as before; Microsoft ones must reach the real DNS.
static void check_resolution(const char *name, const char *before, size_t bl, const char *after,
                             size_t al, int expect_ms_real) {
    for (size_t i = 0; i < N(k_nintendo_hosts); i++) {
        for (int d = 0; d < 2; d++) {
            const char *extra = d ? HE_ATMOSPHERE_DEFAULTS : NULL;
            char a[64] = "", b[64] = "";
            int fa = he_resolve(before, bl, extra, k_nintendo_hosts[i], a, sizeof a);
            int fb = he_resolve(after, al, extra, k_nintendo_hosts[i], b, sizeof b);
            CHECK(fa == fb && strcmp(a, b) == 0, "%s: %s changed (%d:%s -> %d:%s)", name,
                  k_nintendo_hosts[i], fa, a, fb, b);
        }
    }
    if (!expect_ms_real) return;
    for (size_t i = 0; i < N(k_ms_hosts); i++) {
        char ip[64] = "";
        int f = he_resolve(after, al, HE_ATMOSPHERE_DEFAULTS, k_ms_hosts[i], ip, sizeof ip);
        CHECK(!f, "%s: %s still redirected to %s", name, k_ms_hosts[i], ip);
    }
}

static void test_fixture(const char *path) {
    size_t len;
    char *t = read_file(path, &len);
    CHECK(t != NULL, "cannot read %s", path);
    if (!t) return;

    he_stats st;
    he_scan(t, len, &st);
    CHECK(st.ms_active == 6, "fixture: ms_active=%d, want 6", st.ms_active);
    CHECK(st.ms_disabled == 0 && st.catchall == 0 && st.mixed == 0,
          "fixture: disabled=%d catchall=%d mixed=%d", st.ms_disabled, st.catchall, st.mixed);
    CHECK(st.nintendo_active > 40, "fixture: nintendo_active=%d", st.nintendo_active);
    printf("fixture: %d Microsoft lines, %d Nintendo lines\n", st.ms_active,
           st.nintendo_active);

    char ip[64];
    CHECK(he_resolve(t, len, NULL, "login.live.com", ip, sizeof ip) && !strcmp(ip, "198.51.100.20"),
          "fixture: login.live.com should go to Nextendo before the change");

    size_t dl;
    int changed;
    char *d = he_disable_ms(t, len, &dl, &changed);
    CHECK(changed == 6, "fixture: changed=%d", changed);
    he_stats sd;
    he_scan(d, dl, &sd);
    CHECK(sd.ms_active == 0 && sd.ms_disabled == 6, "fixture after: active=%d disabled=%d",
          sd.ms_active, sd.ms_disabled);
    CHECK(sd.nintendo_active == st.nintendo_active, "fixture: Nintendo line count moved");
    CHECK(count_lines(d, dl) == count_lines(t, len), "fixture: line count moved");
    check_resolution("fixture", t, len, d, dl, 1);
    CHECK(he_same_nintendo(t, len, d, dl), "fixture: he_same_nintendo says Nintendo moved");
    // The guard must catch a change that does move a Nintendo host.
    const char *moved = "1.2.3.4 *.nintendo.com\n";
    CHECK(!he_same_nintendo(t, len, moved, strlen(moved)), "he_same_nintendo missed a move");

    // The manager of the file can still recognise it (its header and server lines are untouched).
    CHECK(strstr(d, "community replacement") != NULL, "fixture: header lost");
    CHECK(strstr(d, "198.51.100.20    *.nintendo.com") != NULL, "fixture: server line lost");

    // Idempotent, and restore gives back the original byte for byte.
    size_t d2l;
    int c2;
    char *d2 = he_disable_ms(d, dl, &d2l, &c2);
    CHECK(c2 == 0 && d2l == dl && !memcmp(d, d2, dl), "fixture: second disable changed things");
    size_t rl;
    int rc;
    char *r = he_restore(d, dl, &rl, &rc);
    CHECK(rc == 6 && rl == len && !memcmp(r, t, len), "fixture: restore not byte-identical");
    free(t), free(d), free(d2), free(r);
}

static int glob_s(const char *p, const char *h) { return he_glob(p, strlen(p), h); }

static void test_kind(const char *line, int want) {
    int got = he_classify(line, strlen(line));
    CHECK(got == want, "classify(\"%s\") = %d, want %d", line, got, want);
}

static void test_edges(void) {
    test_kind("", HE_BLANK);
    test_kind("   \t", HE_BLANK);
    test_kind("# 1.2.3.4 login.live.com", HE_COMMENT);
    test_kind(HE_MARK "1.2.3.4 login.live.com", HE_OURS);
    test_kind("1.2.3.4 login.live.com", HE_MS);
    test_kind("1.2.3.4\tLOGIN.LIVE.COM\r", HE_MS);
    test_kind("1.2.3.4 login.live.com # note", HE_MS);
    test_kind("  1.2.3.4   *xboxlive.com", HE_MS);
    test_kind("1.2.3.4 *.xboxlive.com", HE_MS);
    test_kind("1.2.3.4 xbox.com", HE_MS);
    test_kind("1.2.3.4 20ca2.playfabapi.com", HE_MS);
    test_kind("1.2.3.4 notlive.com", HE_OTHER);
    test_kind("1.2.3.4 example.org", HE_OTHER);
    test_kind("1.2.3.4", HE_OTHER);
    test_kind("127.0.0.1 *", HE_CATCHALL);
    test_kind("127.0.0.1 *.com", HE_CATCHALL);
    test_kind("127.0.0.1 *.net", HE_NINTENDO);
    test_kind("1.2.3.4 login.live.com accounts.nintendo.com", HE_MIXED);
    test_kind("1.2.3.4 login.live.com example.org", HE_MIXED);
    test_kind("1.2.3.4 *.nintendo.com", HE_NINTENDO);
    test_kind("0.0.0.0 receive-%.dg.srv.nintendo.net", HE_NINTENDO);
    test_kind("1.2.3.4 g2*.s.n.srv.nintendo.net", HE_NINTENDO);

    // Wildcards.
    CHECK(glob_s("receive-%.dg.srv.nintendo.net", "receive-lp1.dg.srv.nintendo.net"), "glob %%");
    CHECK(!glob_s("receive-%.dg.srv.nintendo.net", "receive-dd1.dg.srv.nintendo.net"), "glob %% no");
    CHECK(glob_s("*.xboxlive.com", "xsts.auth.xboxlive.com"), "glob *.");
    CHECK(!glob_s("*.xboxlive.com", "xboxlive.com"), "glob *. bare");
    CHECK(glob_s("g2*.s.n.srv.nintendo.net", "g2b309e01-lp1.s.n.srv.nintendo.net"), "glob mid");

    // CRLF, no final newline, catch-all and mixed lines left alone.
    const char *crlf =
        "198.51.100.20 login.live.com\r\n"
        "198.51.100.20 accounts.nintendo.com\r\n"
        "127.0.0.1 *\r\n"
        "1.2.3.4 login.live.com example.org\r\n"
        "198.51.100.20 *.xboxlive.com";
    size_t dl, rl;
    int ch, rc;
    char *d = he_disable_ms(crlf, strlen(crlf), &dl, &ch);
    const char *want =
        HE_MARK "198.51.100.20 login.live.com\r\n"
        "198.51.100.20 accounts.nintendo.com\r\n"
        "127.0.0.1 *\r\n"
        "1.2.3.4 login.live.com example.org\r\n"
        HE_MARK "198.51.100.20 *.xboxlive.com";
    CHECK(ch == 2 && !strcmp(d, want), "crlf: got [%s]", d);
    he_stats st;
    he_scan(d, dl, &st);
    CHECK(st.catchall == 1 && st.mixed == 1 && st.ms_disabled == 2, "crlf stats");
    check_resolution("crlf", crlf, strlen(crlf), d, dl, 0);
    char *r = he_restore(d, dl, &rl, &rc);
    CHECK(rc == 2 && !strcmp(r, crlf), "crlf: restore");
    free(d), free(r);

    // A user's own commented line is never "restored".
    const char *own = "# 1.2.3.4 login.live.com\n";
    r = he_restore(own, strlen(own), &rl, &rc);
    CHECK(rc == 0 && !strcmp(r, own), "own comment restored");
    free(r);

    // Empty file.
    d = he_disable_ms("", 0, &dl, &ch);
    CHECK(d && dl == 0 && ch == 0, "empty");
    free(d);

    // Atmosphere's defaults come first; the file overrides them.
    const char *sink = "198.51.100.20 receive-%.dg.srv.nintendo.net\n";
    char ip[64];
    CHECK(he_resolve(sink, strlen(sink), HE_ATMOSPHERE_DEFAULTS, "receive-lp1.dg.srv.nintendo.net",
                     ip, sizeof ip) && !strcmp(ip, "198.51.100.20"), "file overrides defaults: %s", ip);
    CHECK(he_resolve("", 0, HE_ATMOSPHERE_DEFAULTS, "receive-lp1.er.srv.nintendo.net", ip, sizeof ip) &&
          !strcmp(ip, "127.0.0.1"), "defaults apply without a file line");
    // Last matching line wins.
    const char *two = "1.1.1.1 *.nintendo.com\n2.2.2.2 accounts.nintendo.com\n";
    CHECK(he_resolve(two, strlen(two), NULL, "accounts.nintendo.com", ip, sizeof ip) &&
          !strcmp(ip, "2.2.2.2"), "last wins");
    CHECK(!he_resolve(two, strlen(two), NULL, "sun.hac.lp1.d4c.nintendo.net", ip, sizeof ip),
          "unmatched host goes to real DNS");
}

int main(int argc, char **argv) {
    test_edges();
    for (int i = 1; i < argc; i++) test_fixture(argv[i]);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
