#!/bin/sh
# Builds a portable Linux release in build/release (SDL2 and the C++ runtime linked in;
# only glibc and the usual desktop libraries, loaded at runtime, come from the system)
# and packages dist/sm63-linux-x86_64.tar.gz holding sm63/sm63 (plus the SWF if given).
#
# usage: scripts/build-linux-release.sh path/to/SDL2-2.32.10 [path/to/Super_Mario_63_2010.swf]
set -e
SDL=$(realpath "$1")
SWF=${2:+$(realpath "$2")}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build/release"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSM63_SDL_SOURCE="$SDL"
ninja -C "$BUILD" sm63
STAGE="$BUILD/package/sm63"
rm -rf "$BUILD/package" && mkdir -p "$STAGE" "$ROOT/dist"
cp "$BUILD/sm63" "$STAGE/"
strip "$STAGE/sm63"
[ -n "$SWF" ] && cp "$SWF" "$STAGE/"
ARCH=$(uname -m)
tar -C "$BUILD/package" -czf "$ROOT/dist/sm63-linux-$ARCH.tar.gz" sm63
echo "dist/sm63-linux-$ARCH.tar.gz"
