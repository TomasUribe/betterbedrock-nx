#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds BetterBedrockNX.nro and the overlay with devkitPro in Docker (no local install
# needed) and packages dist/BetterBedrockNX-<version>.zip in the SD card layout.
set -e
cd "$(dirname "$0")"
run() { docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w "/src/$1" devkitpro/devkita64:latest make -j"$(nproc)"; }
run .
run overlay
VERSION=$(sed -n 's/^APP_VERSION := //p' Makefile)
STAGE=dist/sd
rm -rf "$STAGE" "dist/BetterBedrockNX-$VERSION.zip"
mkdir -p "$STAGE/switch/BetterBedrockNX" "$STAGE/switch/.overlays"
cp BetterBedrockNX.nro "$STAGE/switch/BetterBedrockNX/"
cp overlay/betterbedrock-nx.ovl "$STAGE/switch/.overlays/"
(cd "$STAGE" && python3 -c "
import zipfile
with zipfile.ZipFile('../BetterBedrockNX-$VERSION.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for p in ('switch/BetterBedrockNX/BetterBedrockNX.nro', 'switch/.overlays/betterbedrock-nx.ovl'):
        z.write(p)
")
ls -l "dist/BetterBedrockNX-$VERSION.zip"
