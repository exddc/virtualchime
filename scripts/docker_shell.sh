#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="virtualchime-builder"
VOLUME_NAME="virtualchime-buildroot-cache"
OUTPUT_DIR="$PROJECT_DIR/buildroot/output"
BUILDROOT_VERSION="${1:-2024.02.1}"

if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
    echo "Docker image not found. Building..."
    docker build -t "$IMAGE_NAME" "$PROJECT_DIR/buildroot"
fi

mkdir -p "$OUTPUT_DIR"

echo "Opening shell in Buildroot build environment..."
echo ""
echo "Quick start commands:"
echo "  # Navigate to buildroot (already set up by docker_build.sh):"
echo "  cd /home/builder/work/buildroot-$BUILDROOT_VERSION"
echo ""
echo "  # Rebuild chime package after editing chime/src/main.cpp:"
echo "  make BR2_EXTERNAL=/home/builder/br2-external chime-rebuild"
echo ""
echo "  # Binary will be at: output/build/chime-1.0/chime"
echo "  # Copy it out: cp output/build/chime-1.0/chime /home/builder/output/"
echo ""

docker run --rm -it \
    -v "$PROJECT_DIR:/home/builder/virtualchime:ro" \
    -v "$VOLUME_NAME:/home/builder/work" \
    -v "$OUTPUT_DIR:/home/builder/output" \
    "$IMAGE_NAME" \
    bash
