#!/bin/bash
# Runs inside the NDK image (tools/build.sh): the app, the engine shim, the
# package.
source /root/bbndk/bbndk-env_10_3_1_995.sh >/dev/null
set -eu
cd "$(dirname "$0")/.."

mkdir -p build/app build/lib
(cd build && qmake -spec blackberry-armv7le-qcc CONFIG+=device ../chromeberry.pro && make -j"$(nproc)")

qcc -Vgcc_ntoarmv7le -O2 -Wall -shared -fPIC -o build/lib/libberryembed.so \
    shim/berry_embed.c -lscreen -lbps

blackberry-nativepackager -package build/chromeberry.bar \
    bar-descriptor.xml -configuration Device-Release
echo "packaged: build/chromeberry.bar"
