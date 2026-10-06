#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Host-compiles the shared logic and runs it against a community-server style hosts file.
set -e
cd "$(dirname "$0")/.."
mkdir -p build-pc/route
CC="cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g"
$CC -o build-pc/test_hosts_edit tests/test_hosts_edit.c common/hosts_edit.c
./build-pc/test_hosts_edit tests/fixtures/community_server_hosts.txt
$CC -o build-pc/test_route tests/test_route.c common/route.c common/hosts_edit.c common/featured.c
cd build-pc/route && ../test_route ../../tests/fixtures/community_server_hosts.txt
cd ../..
rm -rf build-pc/graphics && mkdir -p build-pc/graphics
$CC -Icommon -o build-pc/test_graphics tests/test_graphics.c common/graphics.c
cd build-pc/graphics && ../test_graphics && python3 ../../tests/check_graphics.py rendered
