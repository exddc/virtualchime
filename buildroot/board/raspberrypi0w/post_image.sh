#!/bin/bash
# Post-image script for Raspberry Pi Zero W
set -euo pipefail

BOARD_DIR="$(dirname "$0")"
BINARIES_DIR="${BINARIES_DIR:-$1}"

# Copy config.txt and cmdline.txt to images directory
cp "${BOARD_DIR}/config.txt" "${BINARIES_DIR}/"
cp "${BOARD_DIR}/cmdline.txt" "${BINARIES_DIR}/"

# Create persistent data partition image without relying on genext2fs.
DATA_IMAGE="${BINARIES_DIR}/data.ext4"
DATA_IMAGE_SIZE_MIB="${DATA_IMAGE_SIZE_MIB:-256}"
if command -v mkfs.ext4 >/dev/null 2>&1; then
    rm -f "$DATA_IMAGE"
    dd if=/dev/zero of="$DATA_IMAGE" bs=1M count="$DATA_IMAGE_SIZE_MIB" status=none
    mkfs.ext4 -F -L data "$DATA_IMAGE" >/dev/null
else
    echo "ERROR: mkfs.ext4 not found; cannot create $DATA_IMAGE"
    exit 1
fi

# Ensure bootcode.bin is in rpi-firmware
if [ ! -f "${BINARIES_DIR}/rpi-firmware/bootcode.bin" ]; then
    BOOTCODE_SRC=$(find "${BINARIES_DIR}/../build/rpi-firmware"* -name "bootcode.bin" 2>/dev/null | head -1)
    if [ -n "$BOOTCODE_SRC" ] && [ -f "$BOOTCODE_SRC" ]; then
        echo "Copying bootcode.bin from build directory..."
        cp "$BOOTCODE_SRC" "${BINARIES_DIR}/rpi-firmware/"
    else
        echo "WARNING: bootcode.bin not found, Pi Zero W may not boot!"
    fi
fi

# Generate SD card image
support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"

echo "SD card image generated: images/sdcard.img"
