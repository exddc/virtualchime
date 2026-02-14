#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="virtualchime-builder"
VOLUME_NAME="virtualchime-buildroot-cache"
OUTPUT_DIR="$PROJECT_DIR/buildroot/output"
BUILDROOT_VERSION="${1:-2024.02.1}"
VERSION_FILE="$PROJECT_DIR/buildroot/version.env"
APP_VERSION_FILE="$PROJECT_DIR/chime/VERSION"

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

[ -f "$VERSION_FILE" ] || error "Version file not found at $VERSION_FILE"
[ -f "$APP_VERSION_FILE" ] || error "App version file not found at $APP_VERSION_FILE"

mkdir -p "$OUTPUT_DIR"

log "Rebuilding chime package..."
docker run --rm \
    -v "$PROJECT_DIR:/home/builder/virtualchime:ro" \
    -v "$VOLUME_NAME:/home/builder/work" \
    -v "$OUTPUT_DIR:/home/builder/output" \
    "$IMAGE_NAME" \
    bash -c '
set -euo pipefail

step_start() { date +%s; }
step_done() {
    local name="$1"
    local start="$2"
    local end
    end="$(date +%s)"
    echo "[timing] ${name}: $((end - start))s"
}

cd /home/builder/work/buildroot-'"$BUILDROOT_VERSION"'

# Update chime source from the mounted project
echo "[rebuild] Syncing chime source..."
mkdir -p /home/builder/chime-src/chime /home/builder/chime-src/common /home/builder/br2-external
sync_start="$(step_start)"
rsync -a --delete /home/builder/virtualchime/chime/ /home/builder/chime-src/chime/
rsync -a --delete /home/builder/virtualchime/common/ /home/builder/chime-src/common/
rsync -a --delete /home/builder/virtualchime/buildroot/ /home/builder/br2-external/
step_done "sync" "$sync_start"

# Rebuild just the chime package
echo "[rebuild] Building chime package..."
build_start="$(step_start)"
make BR2_EXTERNAL=/home/builder/br2-external chime-rebuild
step_done "chime-rebuild" "$build_start"

# Copy binary to output
echo "[rebuild] Copying binary to output..."
export_start="$(step_start)"
CHIME_BINARY_PATH="$(find output/build -type f \( -path "*/chime-*/chime/chime" -o -path "*/chime-*/chime" \) | sort | tail -n 1)"
if [ -z "$CHIME_BINARY_PATH" ] || [ ! -f "$CHIME_BINARY_PATH" ]; then
    echo "[rebuild] ERROR: Could not find built chime binary under output/build/chime-*"
    find output/build -type f | sed "s#^#[rebuild] found: #"
    exit 1
fi
echo "[rebuild] Using binary: $CHIME_BINARY_PATH"
cp "$CHIME_BINARY_PATH" /home/builder/output/chime
step_done "export" "$export_start"

echo "[rebuild] Done! Binary at: buildroot/output/chime"
'

log "Binary ready at: $OUTPUT_DIR/chime"
log "Deploy with: ./scripts/deploy_chime.sh <pi-ip>"
