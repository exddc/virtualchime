#!/bin/bash
# Post-build script for Raspberry Pi Zero W image
set -euo pipefail

BOARD_DIR="$(dirname "$0")"
TARGET_DIR="$1"
VERSION_FILE="${BOARD_DIR}/../../version.env"

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

# Generate release metadata for field diagnostics
if [ -f "$VERSION_FILE" ]; then
    # shellcheck disable=SC1090
    . "$VERSION_FILE"
fi

VIRTUALCHIME_OS_VERSION="${VIRTUALCHIME_OS_VERSION:-unknown}"
CHIME_CONFIG_VERSION="${CHIME_CONFIG_VERSION:-unknown}"
BUILDROOT_RELEASE="${BUILDROOT_VERSION:-unknown}"
BUILD_TIMESTAMP_UTC="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
CHIME_CONFIG_SHA256="unknown"
CHIME_APP_VERSION="unknown"

if [ -f "${TARGET_DIR}/etc/chime-app-version" ]; then
    CHIME_APP_VERSION="$(head -n 1 "${TARGET_DIR}/etc/chime-app-version" | tr -d "[:space:]")"
fi

if [ -f "${TARGET_DIR}/etc/chime.conf" ] && command -v sha256sum &>/dev/null; then
    CHIME_CONFIG_SHA256="$(sha256sum "${TARGET_DIR}/etc/chime.conf" | awk '{print $1}')"
fi

cat > "${TARGET_DIR}/etc/virtualchime-release" <<EOF
VIRTUALCHIME_OS_VERSION=${VIRTUALCHIME_OS_VERSION}
CHIME_APP_VERSION=${CHIME_APP_VERSION}
CHIME_CONFIG_VERSION=${CHIME_CONFIG_VERSION}
BUILDROOT_RELEASE=${BUILDROOT_RELEASE}
BUILD_TIMESTAMP_UTC=${BUILD_TIMESTAMP_UTC}
LINUX_KERNEL_RELEASE=${KERNEL_VERSION:-unknown}
CHIME_CONFIG_SHA256=${CHIME_CONFIG_SHA256}
EOF
chmod 644 "${TARGET_DIR}/etc/virtualchime-release"

echo "Post-build complete"
