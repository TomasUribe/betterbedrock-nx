#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the BetterBedrock NX GUI for the PC (SDL2 software renderer, font atlas instead of
# the console's system font) and runs it on a fake SD card holding a
# community-server style hosts file, saving a screenshot after every scripted button press.
# Usage: tests/run_pc_gui.sh <keys> [keyboard entries]
#   keys: A B X Y + U D L R l r, one per frame; entries: "name|address|port" etc.
# Screenshots: build-pc/gui/shots/shot_NN.png
set -e
cd "$(dirname "$0")/.."
OUT=build-pc/gui
mkdir -p "$OUT"
[ -f "$OUT/font_atlas.h" ] || python3 tools/make_font_atlas.py /usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf "$OUT" >/dev/null
cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Itests/pc_stub -Icommon -Isource -I"$OUT" \
   $(sdl2-config --cflags) -o "$OUT/betterbedrock_gui" \
   source/gui.c source/app.c source/gfx_sdl.c tests/pc_gui/text_atlas.c \
   common/hosts_edit.c common/route.c common/featured.c common/raknet_ping.c common/graphics.c \
   tests/pc_stub/nx_control_stub.c \
   $(sdl2-config --libs)
SD="$OUT/sd"
rm -rf "$SD" "$OUT/shots"
mkdir -p "$SD/sdmc:/atmosphere/hosts" "$SD/sdmc:/atmosphere/config" "$SD/sdmc:/atmosphere/logs" \
         "$SD/sdmc:/switch" "$SD/sdmc:/emuMMC" "$SD/romfs" "$OUT/shots"
for f in emummc sysmmc default; do cp tests/fixtures/community_server_hosts.txt "$SD/sdmc:/atmosphere/hosts/$f.txt"; done
printf '[atmosphere]\nenable_dns_mitm = u8!0x1\nadd_defaults_to_dns_hosts = u8!0x0\n' > "$SD/sdmc:/atmosphere/config/system_settings.ini"
printf '[emummc]\nenabled=1\nid=0x1234abcd\n' > "$SD/sdmc:/emuMMC/emummc.ini"
cp romfs/logo.bmp "$SD/romfs/"
ROOT=$(pwd)
cd "$SD"
BL_KEYS="${1:-}+" BL_KBD="${2:-}" BL_ATLAS="$ROOT/$OUT/font_atlas.bin" BL_SHOTS="$ROOT/$OUT/shots" ../betterbedrock_gui
cd "$ROOT"
python3 -c "
import glob
from PIL import Image
for f in sorted(glob.glob('$OUT/shots/*.bmp')):
    Image.open(f).save(f[:-4] + '.png'); import os; os.remove(f)
"
ls "$OUT/shots" | wc -l
