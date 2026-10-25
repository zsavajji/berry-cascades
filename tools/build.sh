#!/bin/sh
# Builds build/io.github.zsavajji.berrycascades.bar in a Docker image with
# the BlackBerry 10 Native SDK 10.3.1 (qcc, qmake, Cascades, the packager):
# BB10_NDK_IMAGE, by default yamsergey/bb10-ndk:0.6 from Docker Hub.
# Fetches the engine first unless engine/ is there (tools/fetch-engine.sh),
# and today's EasyList and EasyPrivacy unless build/blocklist.txt is there
# (tools/fetch-blocklist.py; delete it, or run that, to update).
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
image=${BB10_NDK_IMAGE:-yamsergey/bb10-ndk:0.6}
[ -x "$repo/engine/content_shell.exe" ] || "$repo/tools/fetch-engine.sh"
[ -f "$repo/build/blocklist.txt" ] || "$repo/tools/fetch-blocklist.py"

exec docker run --rm -v "$repo:/src" -w /src "$image" bash tools/build-inside.sh
