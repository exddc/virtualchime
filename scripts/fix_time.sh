#!/usr/bin/env bash
set -euo pipefail

PI="${1:-${PI:-192.168.178.52}}"
SCP_FLAGS="${SCP_FLAGS:--O}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST_EPOCH="$(date -u +%s)"
HOST_ISO="$(date -u '+%Y-%m-%d %H:%M:%S')"

scp ${SCP_FLAGS} "${ROOT_DIR}/buildroot/board/raspberrypi0w/rootfs_overlay/etc/init.d/S41timesync" "root@${PI}:/etc/init.d/S41timesync"
scp ${SCP_FLAGS} "${ROOT_DIR}/buildroot/board/raspberrypi0w/rootfs_overlay/etc/init.d/S99chime" "root@${PI}:/etc/init.d/S99chime"
scp ${SCP_FLAGS} "${ROOT_DIR}/buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf" "root@${PI}:/etc/chime.conf"

ssh "root@${PI}" "\
set -e; \
chmod 755 /etc/init.d/S41timesync /etc/init.d/S99chime; \
/etc/init.d/S41timesync restart; \
if ! date -u -s @${HOST_EPOCH} >/dev/null 2>&1; then date -u -s '${HOST_ISO}' >/dev/null 2>&1; fi; \
/etc/init.d/S99chime restart; \
date -u; \
if command -v ntpd >/dev/null 2>&1; then echo '[fix-time] ntpd present'; else echo '[fix-time] ntpd missing (fallback mode)'; fi; \
if command -v rdate >/dev/null 2>&1; then echo '[fix-time] rdate present'; else echo '[fix-time] rdate missing'; fi; \
if command -v wget >/dev/null 2>&1; then echo '[fix-time] wget present'; else echo '[fix-time] wget missing'; fi; \
tail -n 60 /var/log/chime.log"
