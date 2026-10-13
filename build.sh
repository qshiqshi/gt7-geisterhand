#!/bin/zsh
# Builds Geisterhand.
#
#   ./build.sh            the Mac program and the Windows program
#   ./build.sh mac        only dist/mac/Geisterhand.app   (for Apple Silicon and Intel)
#   ./build.sh windows    only dist/windows/Geisterhand.exe (needs: brew install mingw-w64)
#   ./build.sh release    both, and the downloads in dist/release/ (two ZIPs and their checksums)
#
# Needs cmake and ninja. The first run downloads SDL3 and Dear ImGui into build/_src.
set -e
cd "$(dirname "$0")"
what=${1:-all}

# The licences that have to travel with every copy of the program.
licences() {    # licences <folder>
    cp LICENSE "$1/LICENSE.txt"
    { cat THIRD_PARTY_NOTICES.md
      print "\n\n==== SIL Open Font License 1.1 (Inter) ====\n"
      cat assets/fonts/Inter-OFL.txt
    } > "$1/THIRD-PARTY-LICENSES.txt"
}

if [[ $what == mac || $what == all || $what == release ]]; then
    cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
        "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
    cmake --build build/mac
    rm -rf dist/mac && mkdir -p dist/mac
    cp -R build/mac/Geisterhand.app dist/mac/
    licences dist/mac/Geisterhand.app/Contents/Resources
    codesign --force --sign - dist/mac/Geisterhand.app    # not a Developer ID: macOS asks before the first start
    echo "→ dist/mac/Geisterhand.app"
fi

if [[ $what == windows || $what == all || $what == release ]]; then
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

if [[ $what == release ]]; then
    rm -rf dist/release && mkdir -p dist/release/stage/Geisterhand
    ditto -c -k --keepParent dist/mac/Geisterhand.app dist/release/Geisterhand-mac.zip
    cp dist/windows/Geisterhand.exe dist/release/stage/Geisterhand/
    licences dist/release/stage/Geisterhand
    (cd dist/release/stage && zip -q -r -X ../Geisterhand-windows-x64.zip Geisterhand)
    rm -rf dist/release/stage
    (cd dist/release && shasum -a 256 Geisterhand-mac.zip Geisterhand-windows-x64.zip > SHA256SUMS.txt)
    echo "→ dist/release/"
    cat dist/release/SHA256SUMS.txt
fi
