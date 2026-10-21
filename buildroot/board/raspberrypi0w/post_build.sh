#!/bin/bash
# Post-build script for the Chime image (Raspberry Pi Zero W)
set -euo pipefail

BOARD_DIR="$(dirname "$0")"
TARGET_DIR="$1"

# shellcheck source=../common/post_build_lib.sh
. "${BOARD_DIR}/../common/post_build_lib.sh"

oc_assert_web_ui_dist
oc_chmod_binaries chime chime-webd
oc_require_ota_scripts
oc_common_post_build

# Create firmware symlink for Pi Zero W WiFi
FIRMWARE_DIR="${TARGET_DIR}/lib/firmware/brcm"
if [ -d "$FIRMWARE_DIR" ]; then
    ln -sf brcmfmac43430-sdio.bin "${FIRMWARE_DIR}/brcmfmac43430-sdio.raspberrypi,model-zero-w.bin" 2>/dev/null || true
fi

# Generate modules.dep
KERNEL_VERSION="$(oc_kernel_release)"
if [ -n "$KERNEL_VERSION" ] && [ -d "${TARGET_DIR}/lib/modules/${KERNEL_VERSION}" ]; then
    if command -v depmod &>/dev/null; then
        depmod -a -b "${TARGET_DIR}" "${KERNEL_VERSION}" 2>/dev/null || true
    fi
fi

oc_write_release chime etc/chime.conf

echo "Post-build complete"
