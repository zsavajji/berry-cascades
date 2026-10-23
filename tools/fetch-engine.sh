#!/bin/sh
# Unpacks the Chromium engine from a Berry Browser package into engine/:
# content_shell.exe, its data files and the runtime libraries it ships with.
# The package is releases/BerryBrowserV3-*.bar in
# https://github.com/zsavajji/chromium-for-bb10 (or upstream sw7ft's).
#
#   tools/fetch-engine.sh path/to/BerryBrowserV3-3.0.2-build84.bar
#
# Without an argument it downloads build 84 and checks its sha256.
set -eu

repo=$(cd "$(dirname "$0")/.." && pwd)
url=https://github.com/zsavajji/chromium-for-bb10/raw/main/releases/BerryBrowserV3-3.0.2-build84.bar
sum=c71d1a72593d4a6b7efb9b6a80e9396dd2acf2e0167d2e4186859fa45207ea6c

bar=${1:-}
if [ -z "$bar" ]; then
    bar=$repo/build/BerryBrowserV3-3.0.2-build84.bar
    mkdir -p "$repo/build"
    [ -f "$bar" ] || curl -fL -o "$bar" "$url"
    [ "$(sha256sum "$bar" | cut -d' ' -f1)" = "$sum" ] || {
        echo "error: $bar does not have the expected sha256" >&2
        exit 1
    }
fi

rm -rf "$repo/engine"
mkdir -p "$repo/engine"
for f in content_shell.exe content_shell.pak shell_resources.pak \
         ui_resources_100_percent.pak icudtl.dat snapshot_blob.bin \
         root_store.certs libstdc++.so.6 libgcc_s.so.1 libm.so.2 libasound.so.2; do
    unzip -p "$bar" "native/$f" > "$repo/engine/$f"
    [ -s "$repo/engine/$f" ] || { echo "error: native/$f missing from $bar" >&2; exit 1; }
done
chmod +x "$repo/engine/content_shell.exe"
echo "engine: $repo/engine"
