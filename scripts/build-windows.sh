#!/bin/sh
# Cross-builds Windows releases (32- and 64-bit, Windows XP and newer) with MinGW-w64 and
# packages each as a zip holding sm63/sm63.exe (plus the SWF when one is given).
#
# usage: scripts/build-windows.sh path/to/SDL2-2.32.10 [path/to/Super_Mario_63_2010.swf]
set -e
SDL=$(realpath "$1")
SWF=${2:+$(realpath "$2")}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/dist"
for ARCH in i686 x86_64; do
    BUILD="$ROOT/build-win-$ARCH"
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64-$ARCH.cmake" -DSM63_SDL_SOURCE="$SDL"
    ninja -C "$BUILD" sm63
    STAGE="$BUILD/package/sm63"
    rm -rf "$BUILD/package" && mkdir -p "$STAGE"
    cp "$BUILD/sm63.exe" "$STAGE/"
    "$ARCH-w64-mingw32-strip" "$STAGE/sm63.exe"
    [ -n "$SWF" ] && cp "$SWF" "$STAGE/"
    NAME=$([ "$ARCH" = i686 ] && echo win32 || echo win64)
    rm -f "$ROOT/dist/sm63-$NAME.zip"
    (cd "$BUILD/package" && zip -qr "$ROOT/dist/sm63-$NAME.zip" sm63)
    echo "dist/sm63-$NAME.zip"
done
