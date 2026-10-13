#!/bin/zsh
# Builds Geisterhand.
#
#   ./build.sh            the Mac program and the Windows program
#   ./build.sh mac        only dist/mac/Geisterhand.app   (for Apple Silicon and Intel)
#   ./build.sh windows    only dist/windows/Geisterhand.exe (needs: brew install mingw-w64)
#
# Needs cmake and ninja. The first run downloads SDL3 and Dear ImGui into build/_src.
set -e
cd "$(dirname "$0")"
what=${1:-all}

if [[ $what == mac || $what == all ]]; then
    cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
        "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
    cmake --build build/mac
    rm -rf dist/mac && mkdir -p dist/mac
    cp -R build/mac/Geisterhand.app dist/mac/
    codesign --force --sign - dist/mac/Geisterhand.app    # signed for this Mac only
    echo "→ dist/mac/Geisterhand.app"
fi

if [[ $what == windows || $what == all ]]; then
    if ! command -v x86_64-w64-mingw32-g++ >/dev/null; then
        echo "The Windows build needs mingw-w64: brew install mingw-w64" >&2
        exit 1
    fi
    cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
    cmake --build build/windows
    rm -rf dist/windows && mkdir -p dist/windows
    cp build/windows/Geisterhand.exe dist/windows/
    echo "→ dist/windows/Geisterhand.exe"
fi
