#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds BedrockLink.nro and the overlay with devkitPro in Docker (no local install
# needed) and packages dist/BedrockLink-<version>.zip in the SD card layout.
set -e
cd "$(dirname "$0")"
run() { docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w "/src/$1" devkitpro/devkita64:latest make -j"$(nproc)"; }
run .
run overlay
VERSION=$(sed -n 's/^APP_VERSION := //p' Makefile)
STAGE=dist/sd
rm -rf "$STAGE" "dist/BedrockLink-$VERSION.zip"
mkdir -p "$STAGE/switch/BedrockLink" "$STAGE/switch/.overlays"
cp BedrockLink.nro "$STAGE/switch/BedrockLink/"
cp overlay/bedrocklink.ovl "$STAGE/switch/.overlays/"
(cd "$STAGE" && python3 -c "
import zipfile
with zipfile.ZipFile('../BedrockLink-$VERSION.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for p in ('switch/BedrockLink/BedrockLink.nro', 'switch/.overlays/bedrocklink.ovl'):
        z.write(p)
")
ls -l "dist/BedrockLink-$VERSION.zip"
