#!/usr/bin/env bash
# Usage: ./scripts/flash_sd.sh [/dev/diskN]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
IMAGE_PATH="$PROJECT_DIR/buildroot/output/sdcard.img"

log() { echo "[flash] $*"; }
error() { echo "[flash] ERROR: $*" >&2; exit 1; }

# Check image exists
if [ ! -f "$IMAGE_PATH" ]; then
    error "Image not found at: $IMAGE_PATH
Run ./scripts/docker_build.sh first to build the image."
fi

IMAGE_SIZE=$(ls -lh "$IMAGE_PATH" | awk '{print $5}')
log "Image: $IMAGE_PATH ($IMAGE_SIZE)"

# Get disk device
DISK="${1:-}"

if [ -z "$DISK" ]; then
    echo ""
    echo "Available disks:"
    echo "================"
    diskutil list external physical 2>/dev/null || diskutil list
    echo ""
    echo "Enter the disk device (e.g., /dev/disk4):"
    read -r DISK
fi

if [ -z "$DISK" ]; then
    error "No disk specified"
fi

if [[ ! "$DISK" =~ ^/dev/disk[0-9]+$ ]]; then
    error "Invalid disk format: $DISK (expected /dev/diskN)"
fi

BOOT_DISK=$(diskutil info / | grep "Part of Whole" | awk '{print $4}')
if [ "$DISK" = "/dev/$BOOT_DISK" ]; then
    error "Refusing to flash the boot disk!"
fi

# Show disk info
echo ""
log "Target disk: $DISK"
diskutil list "$DISK"
echo ""

# Get disk size for display
DISK_SIZE=$(diskutil info "$DISK" | grep "Disk Size" | awk -F: '{print $2}' | xargs)
log "Disk size: $DISK_SIZE"

# Confirm
echo ""
echo "WARNING: This will ERASE ALL DATA on $DISK"
echo ""
read -p "Type 'yes' to continue: " CONFIRM

if [ "$CONFIRM" != "yes" ]; then
    log "Aborted."
    exit 1
fi

# Unmount all partitions
log "Unmounting $DISK..."
diskutil unmountDisk "$DISK" || true

# Flash
RAW_DISK="${DISK/disk/rdisk}"
log "Flashing to $RAW_DISK (this takes ~1-2 minutes)..."

sudo dd if="$IMAGE_PATH" of="$RAW_DISK" bs=4m status=progress conv=fsync

# Sync
log "Syncing..."
sync

# Eject
log "Ejecting..."
diskutil eject "$DISK"

echo ""
log "✓ Flash complete!"
echo ""
echo "Next steps:"
echo "  1. Insert SD card into Raspberry Pi Zero W"
echo "  2. Connect FTDI for serial console (optional): screen /dev/tty.usbserial* 115200"
echo "  3. Power on the Pi"
echo "  4. Wait ~30 seconds for boot + WiFi connection"
echo "  5. Find the Pi: arp -a | grep b8:27:eb"
echo "  6. SSH in: ssh root@<ip-address>"
