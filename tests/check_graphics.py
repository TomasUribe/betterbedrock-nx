#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks the files test_graphics wrote: every profile's JSON parses and has the
fields the game reads, and the patch is a well-formed IPS32 with the expected records.

Usage: check_graphics.py <rendered dir>
"""
import json
import os
import struct
import sys

LOD_KEYS = {"bloom", "clouds", "default_deferred_distance", "point_lights", "reflections", "shadows",
            "target_resolution", "upscaling_mode", "volumetric_fog", "lighting_mixed_res_upscale"}
RESOLUTIONS = {"480p", "540p", "720p", "1080p", "1440p", "2160p"}  # seen in the game's own configs
# (address in the decompressed image, new word) - see common/graphics.c
PATCH = {
    0x5C93068: 0x52800020, 0x5C93070: 0x39006660,
    0x5C94234: 0x52800020, 0x5C94238: 0x14000007, 0x5C94298: 0x52800020, 0x5C9429C: 0x14000007,
    0x5C94548: 0x52800020, 0x5C9454C: 0x14000007, 0x5C945AC: 0x52800020, 0x5C945B0: 0x14000007,
    0x5C90B9C: 0x2A0803E0,
    0x41CD26C: 0xF9403908, 0x41CD278: 0x71000409, 0x41CD27C: 0x1A9FC121, 0x41CD280: 0xD503201F,
}

fails = 0


def fail(msg):
    global fails
    fails += 1
    print("FAIL", msg)


def main():
    root = sys.argv[1]
    for prof in sorted(os.listdir(root)):
        d = os.path.join(root, prof)
        if not os.path.isdir(d):
            continue
        plat = json.load(open(os.path.join(d, "platform_configuration.switch.json")))
        tiers = plat["switch"]["tiers"]
        for mode in ("handheld", "docked"):
            names = [t["name"] for t in tiers[mode]]
            if names != ["Performance", "Quality"]:
                fail(f"{prof} {mode}: tiers {names}")
            for t in tiers[mode]:
                if set(t["lods"]) != LOD_KEYS:
                    fail(f"{prof} {mode} {t['name']}: keys {sorted(set(t['lods']) ^ LOD_KEYS)}")
                if t["lods"]["target_resolution"] not in RESOLUTIONS:
                    fail(f"{prof}: resolution {t['lods']['target_resolution']}")
        for k in ("point_light_config", "reflection_config", "render_distance_config", "shadow_config",
                  "volumetric_fog_config"):
            if k not in plat["switch"]:
                fail(f"{prof}: {k} missing")
        sh = json.load(open(os.path.join(d, "shadow_configuration.switch.json")))
        if sorted(sh) != ["high", "low", "medium", "ultra"]:
            fail(f"{prof}: shadow levels {sorted(sh)}")
        c = sh["low"]["cascades"][0][0]
        if c["update_frequency"] not in (1, 2) or c["resolution"] not in (512, 1024, 2048):
            fail(f"{prof}: cascade {c}")
        rd = json.load(open(os.path.join(d, "render_distance_configuration.switch.json")))
        levels = rd["deferred_render_distance_configuration"]["render_distance_levels"]
        if levels != sorted(levels) or not levels:
            fail(f"{prof}: levels {levels}")
        print(f"ok   {prof}: {tiers['handheld'][0]['lods']['target_resolution']} handheld, "
              f"{tiers['docked'][0]['lods']['target_resolution']} docked, shadows {c['resolution']} "
              f"every {c['update_frequency']}, levels {levels}")

    data = open(os.path.join(root, "patch.ips"), "rb").read()
    if data[:5] != b"IPS32" or data[-4:] != b"EEOF":
        fail("patch: not IPS32")
    seen, i = {}, 5
    while i < len(data) - 4:
        off, size = struct.unpack(">IH", data[i:i + 6])
        if size != 4:
            fail(f"patch: record size {size}")
        seen[off - 0x100] = struct.unpack("<I", data[i + 6:i + 10])[0]
        i += 6 + size
    if seen != PATCH:
        fail(f"patch: records differ {sorted(set(seen.items()) ^ set(PATCH.items()))}")
    else:
        print(f"ok   patch: IPS32, {len(seen)} instructions as documented")
    print("check_graphics: all passed" if not fails else f"check_graphics: {fails} FAILED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
