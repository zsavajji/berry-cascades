#!/bin/sh
# Builds build/io.github.zsavajji.berrycascades.bar in a Docker image with
# the BlackBerry 10 Native SDK 10.3.1 (qcc, qmake, Cascades, the packager):
# BB10_NDK_IMAGE, by default yamsergey/bb10-ndk:0.6 from Docker Hub.
# Fetches the engine first unless engine/ is there (tools/fetch-engine.sh).
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
image=${BB10_NDK_IMAGE:-yamsergey/bb10-ndk:0.6}
[ -x "$repo/engine/content_shell.exe" ] || "$repo/tools/fetch-engine.sh"

exec docker run --rm -v "$repo:/src" -w /src "$image" bash tools/build-inside.sh
