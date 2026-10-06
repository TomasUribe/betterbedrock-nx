// SPDX-License-Identifier: GPL-2.0-or-later
// BetterBedrock NX graphics: Vibrant Visuals on a Switch 1, and profiles that make it run.
//
// 1. The unlock: an Atmosphere exefs patch (IPS32) for Minecraft 1.26.44 that turns
//    on the Vibrant Visuals path the Switch 1 build has compiled in but switched off
//    (see docs/vibrant-visuals.md). Atmosphere applies it when Minecraft starts, and
//    only to that exact build.
// 2. Profiles: the game's Vibrant Visuals settings for "switch" are Switch 2 numbers.
//    A profile writes three LayeredFS files (resolution, shadows, render distance)
//    with lighter values. Both of the game's tiers get the same values, so the
//    missing preset menu does not matter.
//
// Plain C + stdio, host-testable: paths start with "sdmc:/" (a folder on a PC).
#ifndef BBNX_GRAPHICS_H
#define BBNX_GRAPHICS_H

#include <stddef.h>

#define GFX_GAME_VERSION  "1.26.44"
#define GFX_BUILD_ID      "457118E249B45EA50CA0F4CF70594461613CFDFA"
#define GFX_PATCH_DIR     "sdmc:/atmosphere/exefs_patches/betterbedrock-nx-vv"
#define GFX_PATCH_PATH    GFX_PATCH_DIR "/" GFX_BUILD_ID ".ips"
#define GFX_TITLE_DIR     "sdmc:/atmosphere/contents/0100D71004694000"
#define GFX_TUNING_DIR    GFX_TITLE_DIR "/romfs/renderer/platform_config/switch"
#define GFX_CONFIG_PATH   "sdmc:/config/betterbedrock-nx/graphics.ini"
// BedrockLink 1.5 (never released) used these; moved on the first query
#define GFX_LEGACY_PATCH_DIR   "sdmc:/atmosphere/exefs_patches/bedrocklink-vv"
#define GFX_LEGACY_CONFIG_PATH "sdmc:/config/bedrocklink/graphics.ini"

typedef enum { GFX_FAST, GFX_BALANCED, GFX_QUALITY, GFX_PROFILE_COUNT } gfx_profile;

typedef struct {
    int patch;    // 1 = our patch installed, 0 = none, -1 = a different file at our path
    int tuning;   // gfx_profile installed, -1 = none, -2 = files that are not one of ours
    gfx_profile chosen;  // the profile saved in graphics.ini (used when turning on)
} gfx_state;

typedef struct {
    const char *name;           // "Balanced"
    const char *res_handheld;   // "540p"
    const char *res_docked;     // "720p"
    int bloom;
    const char *clouds;         // low / medium / high / ultra
    int distance;               // default Vibrant Visuals render distance, chunks
    const char *reflections;    // off / low / ...
    const char *fog;            // volumetric fog: off / low / ...
    int shadow_resolution;      // shadow map size
    int shadow_every;           // redraw the shadow map every N frames
    int shadow_far;             // shadow distance, blocks
    int cloud_shadows;
    int levels[6];              // render distance slider steps, 0-terminated
} gfx_profile_def;

const gfx_profile_def *gfx_profile_get(gfx_profile p);

void gfx_query(gfx_state *st);

// Turn Vibrant Visuals on (patch + profile `p` files) or off (removes both).
int gfx_set_vv(int on, gfx_profile p, char *msg, size_t msg_cap);

// Save the chosen profile; rewrites the files when Vibrant Visuals is on.
int gfx_set_profile(gfx_profile p, char *msg, size_t msg_cap);

// The files a profile writes (for tests): 0..2, NULL past the end.
const char *gfx_file_name(int i);
int gfx_render_file(gfx_profile p, int i, char *out, size_t cap);

// The patch itself (for tests).
const unsigned char *gfx_patch_bytes(size_t *len);

#endif
