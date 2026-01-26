#!/bin/bash
# Post-build script for Raspberry Pi Zero W image
set -euo pipefail

BOARD_DIR="$(dirname "$0")"
TARGET_DIR="$1"

# Set permissions for binary
if [ -f "${TARGET_DIR}/usr/local/bin/chime" ]; then
    chmod 755 "${TARGET_DIR}/usr/local/bin/chime"
fi

# Set SSH directory permissions
if [ -d "${TARGET_DIR}/root/.ssh" ]; then
    chmod 700 "${TARGET_DIR}/root/.ssh"
    chmod 600 "${TARGET_DIR}/root/.ssh/authorized_keys" 2>/dev/null || true
fi

# Create firmware symlink for Pi Zero W WiFi
FIRMWARE_DIR="${TARGET_DIR}/lib/firmware/brcm"
if [ -d "$FIRMWARE_DIR" ]; then
    ln -sf brcmfmac43430-sdio.bin "${FIRMWARE_DIR}/brcmfmac43430-sdio.raspberrypi,model-zero-w.bin" 2>/dev/null || true
fi

# Generate modules.dep
KERNEL_VERSION=$(ls "${TARGET_DIR}/lib/modules/" 2>/dev/null | head -1)
if [ -n "$KERNEL_VERSION" ] && [ -d "${TARGET_DIR}/lib/modules/${KERNEL_VERSION}" ]; then
    if command -v depmod &>/dev/null; then
        depmod -a -b "${TARGET_DIR}" "${KERNEL_VERSION}" 2>/dev/null || true
    fi
fi

echo "Post-build complete"
