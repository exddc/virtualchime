#!/bin/bash
# Post-image script for Raspberry Pi Zero W
set -euo pipefail

BOARD_DIR="$(dirname "$0")"
BINARIES_DIR="${BINARIES_DIR:-$1}"

# Copy config.txt and cmdline.txt to images directory
cp "${BOARD_DIR}/config.txt" "${BINARIES_DIR}/"
cp "${BOARD_DIR}/cmdline.txt" "${BINARIES_DIR}/"

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
