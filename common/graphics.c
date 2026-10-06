// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// The unlock for Minecraft 1.26.44 (main NSO build id GFX_BUILD_ID), IPS32: offsets
// count Atmosphere's 0x100-byte NSO header. Addresses are in the decompressed image.
//
//   RayTracingHardwareOptions constructor - the device's "deferred supported" flag
//     0x5c93068  blr x8 (feature query)  -> mov w0, #1
//     0x5c93070  strb wzr, [x19, #25]    -> strb w0, [x19, #25]   (was always false)
//   RayTracingOptions deferred-availability checks (two, plus their second-interface
//   thunks): return true once the hardware check passes, skipping the pack checks
//     0x5c94234/38, 0x5c94298/9c, 0x5c94548/4c, 0x5c945ac/b0
//                                         -> mov w0, #1 ; b epilogue
//   OptionRegistry::getGraphicsMode - Vibrant Visuals (2) read back as Fancy (1)
//     0x5c90b9c  cset w0, ne             -> mov w0, w8   (the stored mode)
//   MinecraftGame::_updateLightingModel - no deferred case
//     0x41cd26c  ldr x8, [x8, #600]      -> ldr x8, [x8, #112]   (getGraphicsMode)
//     0x41cd278  tst w0, #1              -> subs w9, w0, #1
//     0x41cd27c  mov w9, #2              -> csel w1, w9, wzr, gt (3->2 RT, 2->1 deferred)
//     0x41cd280  csel w1, w9, wzr, ne    -> nop
static const unsigned char k_patch[] = {
    0x49, 0x50, 0x53, 0x33, 0x32, 0x05, 0xc9, 0x31, 0x68, 0x00, 0x04, 0x20,
    0x00, 0x80, 0x52, 0x05, 0xc9, 0x31, 0x70, 0x00, 0x04, 0x60, 0x66, 0x00,
    0x39, 0x05, 0xc9, 0x43, 0x34, 0x00, 0x04, 0x20, 0x00, 0x80, 0x52, 0x05,
    0xc9, 0x43, 0x38, 0x00, 0x04, 0x07, 0x00, 0x00, 0x14, 0x05, 0xc9, 0x43,
    0x98, 0x00, 0x04, 0x20, 0x00, 0x80, 0x52, 0x05, 0xc9, 0x43, 0x9c, 0x00,
    0x04, 0x07, 0x00, 0x00, 0x14, 0x05, 0xc9, 0x46, 0x48, 0x00, 0x04, 0x20,
    0x00, 0x80, 0x52, 0x05, 0xc9, 0x46, 0x4c, 0x00, 0x04, 0x07, 0x00, 0x00,
    0x14, 0x05, 0xc9, 0x46, 0xac, 0x00, 0x04, 0x20, 0x00, 0x80, 0x52, 0x05,
    0xc9, 0x46, 0xb0, 0x00, 0x04, 0x07, 0x00, 0x00, 0x14, 0x05, 0xc9, 0x0c,
    0x9c, 0x00, 0x04, 0xe0, 0x03, 0x08, 0x2a, 0x04, 0x1c, 0xd3, 0x6c, 0x00,
    0x04, 0x08, 0x39, 0x40, 0xf9, 0x04, 0x1c, 0xd3, 0x78, 0x00, 0x04, 0x09,
    0x04, 0x00, 0x71, 0x04, 0x1c, 0xd3, 0x7c, 0x00, 0x04, 0x21, 0xc1, 0x9f,
    0x1a, 0x04, 0x1c, 0xd3, 0x80, 0x00, 0x04, 0x1f, 0x20, 0x03, 0xd5, 0x45,
    0x45, 0x4f, 0x46,
};

// Mojang's mobile choices taken further: Android's weakest tiers render at 480p,
// redraw shadows every 2nd frame and drop cloud shadows.
static const gfx_profile_def k_profiles[GFX_PROFILE_COUNT] = {
    {"Fast", "480p", "540p", 0, "low", 6, "off", "off", 1024, 2, 64, 0, {4, 6, 8, 10, 12, 0}},
    {"Balanced", "540p", "720p", 1, "low", 8, "off", "off", 1024, 2, 96, 1, {6, 8, 10, 12, 14, 0}},
    {"Quality", "720p", "720p", 1, "medium", 10, "low", "low", 2048, 1, 128, 1, {6, 8, 10, 12, 14, 16}},
};

static const char *const k_files[] = {
    "platform_configuration.switch.json",
    "shadow_configuration.switch.json",
    "render_distance_configuration.switch.json",
};
#define FILE_COUNT ((int)(sizeof k_files / sizeof k_files[0]))

const gfx_profile_def *gfx_profile_get(gfx_profile p) {
    return p >= 0 && p < GFX_PROFILE_COUNT ? &k_profiles[p] : &k_profiles[GFX_BALANCED];
}

const char *gfx_file_name(int i) {
    return i >= 0 && i < FILE_COUNT ? k_files[i] : NULL;
}

const unsigned char *gfx_patch_bytes(size_t *len) {
    *len = sizeof k_patch;
    return k_patch;
}

// ---- files ----

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

// 1 = the file holds exactly `buf`, 0 = differs, -1 = missing
static int same_as(const char *path, const void *buf, size_t len) {
    size_t rl = 0;
    char *rb = read_all(path, &rl);
    if (!rb) return -1;
    int same = rl == len && memcmp(rb, buf, len) == 0;
    free(rb);
    return same;
}

static int write_all(const char *path, const void *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(buf, 1, len, f) == len;
    if (fclose(f) != 0) ok = 0;
    return ok && same_as(path, buf, len) == 1;
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

// Removes `dir` and its empty parents up to (not including) `stop`.
static void rmdirs(const char *dir, const char *stop) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s", dir);
    while (strlen(tmp) > strlen(stop) && rmdir(tmp) == 0) {
        char *slash = strrchr(tmp, '/');
        if (!slash) break;
        *slash = '\0';
    }
}

static void file_path(int i, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s", GFX_TUNING_DIR, k_files[i]);
}

// ---- the three files ----

static int lods(char *out, size_t cap, const gfx_profile_def *d, const char *res) {
    return snprintf(out, cap,
                    "{\"bloom\": %s, \"clouds\": \"%s\", \"default_deferred_distance\": %d, "
                    "\"point_lights\": \"off\", \"reflections\": \"%s\", \"shadows\": \"low\", "
                    "\"target_resolution\": \"%s\", \"upscaling_mode\": \"bilinear\", "
                    "\"volumetric_fog\": \"%s\", \"lighting_mixed_res_upscale\": 0.5}",
                    d->bloom ? "true" : "false", d->clouds, d->distance, d->reflections, res, d->fog);
}

static int render_platform(const gfx_profile_def *d, char *out, size_t cap) {
    char hh[512], dk[512];
    lods(hh, sizeof hh, d, d->res_handheld);
    lods(dk, sizeof dk, d, d->res_docked);
    return snprintf(out, cap,
                    "{\n"
                    "  \"switch\": {\n"
                    "    \"tiers\": {\n"
                    "      \"handheld\": [\n"
                    "        {\"name\": \"Performance\", \"lods\": %s},\n"
                    "        {\"name\": \"Quality\", \"lods\": %s}\n"
                    "      ],\n"
                    "      \"docked\": [\n"
                    "        {\"name\": \"Performance\", \"lods\": %s},\n"
                    "        {\"name\": \"Quality\", \"lods\": %s}\n"
                    "      ]\n"
                    "    },\n"
                    "    \"point_light_config\": {\"file\": \"point_light_configuration.switch.json\"},\n"
                    "    \"reflection_config\": {\"file\": \"reflection_configuration.switch.json\"},\n"
                    "    \"render_distance_config\": {\"file\": \"render_distance_configuration.switch.json\"},\n"
                    "    \"shadow_config\": {\"file\": \"shadow_configuration.switch.json\"},\n"
                    "    \"volumetric_fog_config\": {\"file\": \"volumetric_fog_configuration.switch.json\"}\n"
                    "  }\n"
                    "}\n",
                    hh, hh, dk, dk);
}

// One shadow cascade for every quality level (only "low" is used by the tiers).
static int render_shadows(const gfx_profile_def *d, char *out, size_t cap) {
    static const char *const levels[] = {"low", "medium", "high", "ultra"};
    char level[1024];
    int n = snprintf(level, sizeof level,
                     "{\n"
                     "    \"cascades\": [[{\"bias\": 0.000275, \"has_dynamic_geometry\": true, "
                     "\"has_static_geometry\": true, \"light_quantization_steps\": 1800, \"pcf_width\": 2, "
                     "\"range\": 1, \"resolution\": %d, \"slope_bias\": 0.00001, \"update_frequency\": %d, "
                     "\"update_offset\": 0}]],\n"
                     "    \"normal_offset_strength\": 0.065,\n"
                     "    \"shadow_range\": {\"near\": 0.025, \"far\": %d.0},\n"
                     "    \"max_shadow_frustum_radius\": %d.0,\n"
                     "    \"max_shadow_distance\": %d.0,\n"
                     "    \"cloud_shadow_contribution\": 0.4,\n"
                     "    \"cloud_shadow_pcf_width\": 1.0,\n"
                     "    \"cloud_shadow_quantization_steps\": 7200,\n"
                     "    \"sun_shadows\": true,\n"
                     "    \"moon_shadows\": true,\n"
                     "    \"cloud_shadows\": %s\n"
                     "  }",
                     d->shadow_resolution, d->shadow_every, d->shadow_far, d->shadow_far * 3 / 2, d->shadow_far,
                     d->cloud_shadows ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof level) return -1;
    size_t used = 0;
    for (int i = 0; i < 4; i++) {
        int w = snprintf(out + used, cap - used, "%s  \"%s\": %s%s", i ? "" : "{\n", levels[i], level,
                         i < 3 ? ",\n" : "\n}\n");
        if (w < 0 || (size_t)w >= cap - used) return -1;
        used += (size_t)w;
    }
    return (int)used;
}

static int render_distance(const gfx_profile_def *d, char *out, size_t cap) {
    char list[64] = "";
    size_t used = 0;
    for (int i = 0; i < 6 && d->levels[i]; i++)
        used += (size_t)snprintf(list + used, sizeof list - used, "%s%d", i ? ", " : "", d->levels[i]);
    return snprintf(out, cap,
                    "{\n  \"deferred_render_distance_configuration\": {\n    \"render_distance_levels\": [%s]\n  }\n}\n",
                    list);
}

int gfx_render_file(gfx_profile p, int i, char *out, size_t cap) {
    const gfx_profile_def *d = gfx_profile_get(p);
    int n = i == 0 ? render_platform(d, out, cap) : i == 1 ? render_shadows(d, out, cap)
          : i == 2 ? render_distance(d, out, cap) : -1;
    return n < 0 || (size_t)n >= cap ? -1 : n;
}

// ---- state ----

static gfx_profile read_chosen(void) {
    size_t len = 0;
    char *b = read_all(GFX_CONFIG_PATH, &len);
    gfx_profile p = GFX_BALANCED;
    if (b) {
        const char *v = strstr(b, "profile=");
        if (v) {
            v += 8;
            for (int i = 0; i < GFX_PROFILE_COUNT; i++) {
                size_t n = strlen(k_profiles[i].name);
                if (!strncmp(v, k_profiles[i].name, n)) p = (gfx_profile)i;
            }
        }
        free(b);
    }
    return p;
}

static int save_chosen(gfx_profile p) {
    char buf[96];
    int n = snprintf(buf, sizeof buf, "[graphics]\nprofile=%s\n", gfx_profile_get(p)->name);
    mkdirs("sdmc:/config/betterbedrock-nx");
    return write_all(GFX_CONFIG_PATH, buf, (size_t)n);
}

// The profile the files on the card match: >= 0, -1 none of them, -2 something else.
static int installed_profile(void) {
    static char want[4096];
    char path[256];
    int present = 0;
    for (int i = 0; i < FILE_COUNT; i++) {
        struct stat st;
        file_path(i, path, sizeof path);
        if (stat(path, &st) == 0) present++;
    }
    if (present == 0) return -1;
    for (int p = 0; p < GFX_PROFILE_COUNT; p++) {
        int all = 1;
        for (int i = 0; i < FILE_COUNT && all; i++) {
            int n = gfx_render_file((gfx_profile)p, i, want, sizeof want);
            file_path(i, path, sizeof path);
            all = n > 0 && same_as(path, want, (size_t)n) == 1;
        }
        if (all) return p;
    }
    return -2;
}

// BedrockLink 1.5 kept the patch and the choice under its old name: move them.
static void migrate(void) {
    char old[256];
    snprintf(old, sizeof old, "%s/%s.ips", GFX_LEGACY_PATCH_DIR, GFX_BUILD_ID);
    if (same_as(old, k_patch, sizeof k_patch) == 1) {
        mkdirs(GFX_PATCH_DIR);
        if (write_all(GFX_PATCH_PATH, k_patch, sizeof k_patch)) {
            unlink(old);
            rmdir(GFX_LEGACY_PATCH_DIR);
        }
    }
    struct stat st;
    if (stat(GFX_CONFIG_PATH, &st) != 0) {
        size_t len = 0;
        char *b = read_all(GFX_LEGACY_CONFIG_PATH, &len);
        if (b) {
            mkdirs("sdmc:/config/betterbedrock-nx");
            write_all(GFX_CONFIG_PATH, b, len);
            free(b);
        }
    }
}

void gfx_query(gfx_state *st) {
    migrate();
    int same = same_as(GFX_PATCH_PATH, k_patch, sizeof k_patch);
    st->patch = same == 1 ? 1 : same == 0 ? -1 : 0;
    st->tuning = installed_profile();
    st->chosen = read_chosen();
}

// ---- actions ----

static int write_profile(gfx_profile p, char *msg, size_t cap) {
    static char buf[4096];
    char path[256];
    mkdirs(GFX_TUNING_DIR);
    for (int i = 0; i < FILE_COUNT; i++) {
        int n = gfx_render_file(p, i, buf, sizeof buf);
        file_path(i, path, sizeof path);
        if (n < 0 || !write_all(path, buf, (size_t)n)) {
            snprintf(msg, cap, "Could not write %s.", k_files[i]);
            return 0;
        }
    }
    return 1;
}

static void remove_profile(void) {
    char path[256];
    for (int i = 0; i < FILE_COUNT; i++) {
        file_path(i, path, sizeof path);
        unlink(path);
    }
    rmdirs(GFX_TUNING_DIR, GFX_TITLE_DIR);
    rmdir(GFX_TITLE_DIR);  // only when empty: other mods for Minecraft stay
}

int gfx_set_vv(int on, gfx_profile p, char *msg, size_t cap) {
    if (on) {
        mkdirs(GFX_PATCH_DIR);
        if (!write_all(GFX_PATCH_PATH, k_patch, sizeof k_patch)) {
            snprintf(msg, cap, "Could not write the patch (%s).", strerror(errno));
            return 0;
        }
        if (!write_profile(p, msg, cap)) return 0;
        save_chosen(p);
        snprintf(msg, cap, "Vibrant Visuals on (%s). Start Minecraft again to use it.", gfx_profile_get(p)->name);
        return 1;
    }
    unlink(GFX_PATCH_PATH);
    rmdir(GFX_PATCH_DIR);
    remove_profile();
    gfx_state st;
    gfx_query(&st);
    if (st.patch != 0 || st.tuning != -1) {
        snprintf(msg, cap, "Could not remove every file - check %s.", GFX_TITLE_DIR);
        return 0;
    }
    snprintf(msg, cap, "Vibrant Visuals off. Start Minecraft again to go back.");
    return 1;
}

int gfx_set_profile(gfx_profile p, char *msg, size_t cap) {
    if (!save_chosen(p)) {
        snprintf(msg, cap, "Could not save %s.", GFX_CONFIG_PATH);
        return 0;
    }
    gfx_state st;
    gfx_query(&st);
    if (st.patch != 1) {
        snprintf(msg, cap, "Profile %s - used when Vibrant Visuals is on.", gfx_profile_get(p)->name);
        return 1;
    }
    if (!write_profile(p, msg, cap)) return 0;
    snprintf(msg, cap, "Profile %s. Start Minecraft again to use it.", gfx_profile_get(p)->name);
    return 1;
}
