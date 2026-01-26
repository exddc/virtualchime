#!/usr/bin/env bash
# build_arm.sh - Cross-compile chime for ARM (Raspberry Pi Zero W)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
CHIME_DIR="$PROJECT_DIR/chime"
BUILD_DIR="$CHIME_DIR/build-arm"
BUILDROOT_OUTPUT="${BUILDROOT_OUTPUT:-}"

log() {
    echo "[build-arm] $*"
}

error() {
    echo "[build-arm] ERROR: $*" >&2
    exit 1
}

find_compiler() {
    if [ -n "$BUILDROOT_OUTPUT" ]; then
        local br_gcc="$BUILDROOT_OUTPUT/host/bin/arm-buildroot-linux-gnueabihf-g++"
        if [ -x "$br_gcc" ]; then
            echo "$br_gcc"
            return 0
        fi
    fi
    
    for cc in arm-linux-gnueabihf-g++ arm-none-linux-gnueabihf-g++; do
        if command -v "$cc" &>/dev/null; then
            echo "$cc"
            return 0
        fi
    done
    
    return 1
}

CXX=$(find_compiler) || error "No ARM cross-compiler found.
    
Install one of:
  - Buildroot toolchain: set BUILDROOT_OUTPUT=/path/to/buildroot/output
  - Ubuntu/Debian: sudo apt install g++-arm-linux-gnueabihf
  - macOS: Use a Docker container or remote build"

log "Using compiler: $CXX"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

log "Compiling chime for ARM..."
$CXX \
    -std=c++20 \
    -Wall -Wextra -Wpedantic \
    -O2 \
    -march=armv6 -mfpu=vfp -mfloat-abi=hard \
    -o chime \
    "$CHIME_DIR/src/main.cpp"

log "Build complete: $BUILD_DIR/chime"

if command -v file &>/dev/null; then
    log "Binary info:"
    file "$BUILD_DIR/chime"
fi

echo ""
echo "To deploy: ./scripts/deploy_pi.sh <pi-ip> $BUILD_DIR/chime"
