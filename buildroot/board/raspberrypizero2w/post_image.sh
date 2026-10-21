#!/bin/bash
# Post-image script for the Ring image (Raspberry Pi Zero 2 W)
set -euo pipefail

BOARD_DIR="$(dirname "$0")"
BINARIES_DIR="${BINARIES_DIR:-$1}"
DTC="${HOST_DIR:?HOST_DIR is not set}/bin/dtc"

cp "${BOARD_DIR}/config.txt" "${BINARIES_DIR}/"
cp "${BOARD_DIR}/cmdline.txt" "${BINARIES_DIR}/"

[ -f "${BINARIES_DIR}/rpi-firmware/bootcode.bin" ] || {
    echo "ERROR: rpi-firmware/bootcode.bin missing; enable BR2_PACKAGE_RPI_FIRMWARE_BOOTCODE_BIN" >&2
    exit 1
}

[ -x "$DTC" ] || {
    echo "ERROR: $DTC missing; enable BR2_PACKAGE_HOST_DTC" >&2
    exit 1
}
"$DTC" -@ -q -I dts -O dtb \
    -o "${BINARIES_DIR}/rpi-firmware/overlays/openchime-ring-audio.dtbo" \
    "${BOARD_DIR}/openchime-ring-audio-overlay.dts"

DATA_IMAGE="${BINARIES_DIR}/data.ext4"
DATA_IMAGE_SIZE_MIB="${DATA_IMAGE_SIZE_MIB:-256}"
command -v mkfs.ext4 >/dev/null 2>&1 || {
    echo "ERROR: mkfs.ext4 not found; cannot create $DATA_IMAGE" >&2
    exit 1
}
rm -f "$DATA_IMAGE"
dd if=/dev/zero of="$DATA_IMAGE" bs=1M count="$DATA_IMAGE_SIZE_MIB" status=none
mkfs.ext4 -F -L data "$DATA_IMAGE" >/dev/null

support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"

echo "SD card image generated: images/sdcard.img"
