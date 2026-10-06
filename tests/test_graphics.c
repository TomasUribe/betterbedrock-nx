// SPDX-License-Identifier: GPL-2.0-or-later
// Host test for common/graphics.c, run in an empty folder (it creates "sdmc:/...").
// Writes every profile's files to ./rendered/<profile>/ for tests/check_graphics.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "graphics.h"

static int g_fail;
#define CHECK(cond, ...)                         \
    do {                                         \
        if (!(cond)) {                           \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                 \
            printf("\n");                        \
            g_fail++;                            \
        }                                        \
    } while (0)

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void put(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

int main(void) {
    char msg[256];
    gfx_state st;

    gfx_query(&st);
    CHECK(st.patch == 0 && st.tuning == -1 && st.chosen == GFX_BALANCED, "fresh card: %d %d %d", st.patch, st.tuning,
          st.chosen);

    // another Minecraft mod in the same title folder must survive
    system("mkdir -p 'sdmc:/atmosphere/contents/0100D71004694000/romfs/textures'");
    put("sdmc:/atmosphere/contents/0100D71004694000/romfs/textures/other_mod.png", "x");

    CHECK(gfx_set_vv(1, GFX_BALANCED, msg, sizeof msg), "turn on: %s", msg);
    gfx_query(&st);
    CHECK(st.patch == 1 && st.tuning == GFX_BALANCED, "on: patch %d tuning %d", st.patch, st.tuning);
    CHECK(exists(GFX_PATCH_PATH), "patch file");

    CHECK(gfx_set_profile(GFX_FAST, msg, sizeof msg), "profile: %s", msg);
    gfx_query(&st);
    CHECK(st.tuning == GFX_FAST && st.chosen == GFX_FAST, "fast: tuning %d chosen %d", st.tuning, st.chosen);

    // a file edited by hand is not one of ours
    put(GFX_TUNING_DIR "/shadow_configuration.switch.json", "{}");
    gfx_query(&st);
    CHECK(st.tuning == -2, "edited file: %d", st.tuning);
    CHECK(gfx_set_profile(GFX_QUALITY, msg, sizeof msg), "rewrite: %s", msg);
    gfx_query(&st);
    CHECK(st.tuning == GFX_QUALITY, "rewritten: %d", st.tuning);

    // off: patch and our files gone, the other mod stays
    CHECK(gfx_set_vv(0, GFX_QUALITY, msg, sizeof msg), "turn off: %s", msg);
    gfx_query(&st);
    CHECK(st.patch == 0 && st.tuning == -1, "off: patch %d tuning %d", st.patch, st.tuning);
    CHECK(!exists(GFX_PATCH_DIR), "patch folder removed");
    CHECK(!exists(GFX_TUNING_DIR), "tuning folder removed");
    CHECK(exists("sdmc:/atmosphere/contents/0100D71004694000/romfs/textures/other_mod.png"), "other mod kept");
    CHECK(st.chosen == GFX_QUALITY, "choice remembered: %d", st.chosen);

    // a profile chosen while off is only saved
    CHECK(gfx_set_profile(GFX_BALANCED, msg, sizeof msg), "save while off: %s", msg);
    gfx_query(&st);
    CHECK(st.tuning == -1 && st.chosen == GFX_BALANCED, "while off: %d %d", st.tuning, st.chosen);

    // BedrockLink 1.5's paths: the patch moves to the new folder, the choice is copied
    size_t plen = 0;
    const unsigned char *pbytes = gfx_patch_bytes(&plen);
    system("rm -rf 'sdmc:/config/betterbedrock-nx/graphics.ini' 'sdmc:/atmosphere/exefs_patches'");
    system("mkdir -p 'sdmc:/atmosphere/exefs_patches/bedrocklink-vv' 'sdmc:/config/bedrocklink'");
    FILE *old = fopen(GFX_LEGACY_PATCH_DIR "/" GFX_BUILD_ID ".ips", "wb");
    fwrite(pbytes, 1, plen, old);
    fclose(old);
    put(GFX_LEGACY_CONFIG_PATH, "[graphics]\nprofile=Quality\n");
    gfx_query(&st);
    CHECK(st.patch == 1 && st.chosen == GFX_QUALITY, "migrated: patch %d chosen %d", st.patch, st.chosen);
    CHECK(!exists(GFX_LEGACY_PATCH_DIR), "old patch folder removed");
    CHECK(exists(GFX_LEGACY_CONFIG_PATH), "old config left alone");

    // every profile's files, for the JSON checks
    static char buf[4096];
    for (int p = 0; p < GFX_PROFILE_COUNT; p++) {
        char dir[128], path[256];
        snprintf(dir, sizeof dir, "mkdir -p rendered/%s", gfx_profile_get((gfx_profile)p)->name);
        system(dir);
        for (int i = 0; gfx_file_name(i); i++) {
            int n = gfx_render_file((gfx_profile)p, i, buf, sizeof buf);
            CHECK(n > 0, "render %d/%d", p, i);
            snprintf(path, sizeof path, "rendered/%s/%s", gfx_profile_get((gfx_profile)p)->name, gfx_file_name(i));
            FILE *f = fopen(path, "wb");
            fwrite(buf, 1, (size_t)n, f);
            fclose(f);
        }
    }
    size_t len = 0;
    const unsigned char *patch = gfx_patch_bytes(&len);
    FILE *f = fopen("rendered/patch.ips", "wb");
    fwrite(patch, 1, len, f);
    fclose(f);

    printf(g_fail ? "graphics: %d FAILED\n" : "graphics: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
