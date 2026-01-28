#!/usr/bin/env bash
# docker_build.sh - Build the Virtual Chime Buildroot image using Docker
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILDROOT_VERSION="${1:-2024.02.1}"

IMAGE_NAME="virtualchime-builder"
VOLUME_NAME="virtualchime-buildroot-cache"
OUTPUT_DIR="$PROJECT_DIR/buildroot/output"

log() { echo "[docker-build] $*"; }
error() { echo "[docker-build] ERROR: $*" >&2; exit 1; }

check_secrets() {
    local wifi_conf="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf"
    local ssh_keys="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/root/.ssh/authorized_keys"
    [ -f "$wifi_conf" ] || error "Wi-Fi config not found! Copy wpa_supplicant.conf.example and edit it."
    [ -f "$ssh_keys" ] || error "SSH authorized_keys not found! Run: cat ~/.ssh/id_ed25519.pub > $ssh_keys"
    log "Secret files found"
}

build_docker_image() {
    log "Building Docker image..."
    docker build -t "$IMAGE_NAME" "$PROJECT_DIR/buildroot"
}

run_build() {
    log "Starting Buildroot build (version $BUILDROOT_VERSION)..."
    mkdir -p "$OUTPUT_DIR"
    docker volume create "$VOLUME_NAME" >/dev/null 2>&1 || true
    
    docker run --rm -it \
        -v "$PROJECT_DIR:/home/builder/virtualchime:ro" \
        -v "$VOLUME_NAME:/home/builder/work" \
        -e BUILDROOT_VERSION="$BUILDROOT_VERSION" \
        "$IMAGE_NAME" \
        bash -c '
set -euo pipefail

# Fix volume permissions
sudo chown -R builder:builder /home/builder/work

cd /home/builder/work

if [ ! -d "buildroot-$BUILDROOT_VERSION" ]; then
    echo "[build] Downloading Buildroot $BUILDROOT_VERSION..."
    wget -q --show-progress "https://buildroot.org/downloads/buildroot-$BUILDROOT_VERSION.tar.gz"
    tar xf "buildroot-$BUILDROOT_VERSION.tar.gz"
    rm "buildroot-$BUILDROOT_VERSION.tar.gz"
else
    echo "[build] Using cached Buildroot"
fi

echo "[build] Setting up external tree and chime source..."
rm -rf /home/builder/br2-external
mkdir -p /home/builder/br2-external

# Copy the buildroot external tree
cp -r /home/builder/virtualchime/buildroot/* /home/builder/br2-external/

# Copy the chime source so the package can find it at ../chime
cp -r /home/builder/virtualchime/chime /home/builder/chime

cd "buildroot-$BUILDROOT_VERSION"

echo "[build] Configuring for Raspberry Pi Zero W..."
make BR2_EXTERNAL=/home/builder/br2-external virtualchime_rpi0w_defconfig

echo "[build] Building (this takes 30-90 min)..."
make BR2_EXTERNAL=/home/builder/br2-external -j$(nproc)

if [ -f "output/images/sdcard.img" ]; then
    cp output/images/sdcard.img /home/builder/work/
    echo "[build] SUCCESS! Image ready."
else
    echo "[build] Build finished but sdcard.img not found"
    ls -la output/images/ 2>/dev/null || true
fi
'
    
    log "Extracting image from volume..."
    docker run --rm \
        -v "$VOLUME_NAME:/work:ro" \
        -v "$OUTPUT_DIR:/out" \
        alpine sh -c "cp /work/sdcard.img /out/ 2>/dev/null && echo 'Done' || echo 'Image not found in volume'"
}

log "Virtual Chime Buildroot Docker Build"
check_secrets
build_docker_image
run_build

echo ""
log "Image: $OUTPUT_DIR/sdcard.img"
echo "Flash: sudo dd if=$OUTPUT_DIR/sdcard.img of=/dev/rdiskN bs=4m status=progress"
echo "Clean: docker volume rm $VOLUME_NAME"
