#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="virtualchime-builder"
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
echo "  # Setup (run once):"
echo "  cd /home/builder/output/buildroot-$BUILDROOT_VERSION"
echo "  cp -r /home/builder/virtualchime/buildroot /home/builder/br2-external"
echo "  make BR2_EXTERNAL=/home/builder/br2-external virtualchime_rpi0w_defconfig"
echo ""
echo "  # Customize:"
echo "  make BR2_EXTERNAL=/home/builder/br2-external menuconfig"
echo ""
echo "  # Build:"
echo "  make BR2_EXTERNAL=/home/builder/br2-external -j\$(nproc)"
echo ""

docker run --rm -it \
    -v "$PROJECT_DIR:/home/builder/virtualchime:ro" \
    -v "$OUTPUT_DIR:/home/builder/output" \
    "$IMAGE_NAME" \
    bash
