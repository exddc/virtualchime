#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="virtualchime-builder"
VOLUME_NAME="virtualchime-buildroot-cache"
OUTPUT_DIR="$PROJECT_DIR/buildroot/output"
BUILDROOT_VERSION="${1:-2024.02.1}"

log() { echo "[rebuild-chime] $*"; }
error() { echo "[rebuild-chime] ERROR: $*" >&2; exit 1; }

# Check Docker image exists
if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
    error "Docker image '$IMAGE_NAME' not found. Run ./scripts/docker_build.sh first."
fi

# Check volume exists (means a build has been done)
if ! docker volume inspect "$VOLUME_NAME" &>/dev/null; then
    error "Build volume '$VOLUME_NAME' not found. Run ./scripts/docker_build.sh first."
fi

mkdir -p "$OUTPUT_DIR"

log "Rebuilding chime package..."
docker run --rm \
    -v "$PROJECT_DIR:/home/builder/virtualchime:ro" \
    -v "$VOLUME_NAME:/home/builder/work" \
    -v "$OUTPUT_DIR:/home/builder/output" \
    "$IMAGE_NAME" \
    bash -c '
set -euo pipefail

cd /home/builder/work/buildroot-'"$BUILDROOT_VERSION"'

# Update chime source from the mounted project
echo "[rebuild] Syncing chime source..."
rm -rf /home/builder/chime-src
mkdir -p /home/builder/chime-src
cp -r /home/builder/virtualchime/chime /home/builder/chime-src/chime
cp -r /home/builder/virtualchime/common /home/builder/chime-src/common

# Update br2-external tree
echo "[rebuild] Syncing br2-external..."
rm -rf /home/builder/br2-external
cp -r /home/builder/virtualchime/buildroot /home/builder/br2-external

# Rebuild just the chime package
echo "[rebuild] Building chime package..."
make BR2_EXTERNAL=/home/builder/br2-external chime-rebuild

# Copy binary to output
echo "[rebuild] Copying binary to output..."
cp output/build/chime-1.0/chime /home/builder/output/

echo "[rebuild] Done! Binary at: buildroot/output/chime"
'

log "Binary ready at: $OUTPUT_DIR/chime"
log "Deploy with: ./scripts/deploy_chime.sh <pi-ip>"
