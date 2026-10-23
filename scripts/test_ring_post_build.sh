#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BOARD_DIR="$PROJECT_DIR/buildroot/board/raspberrypizero2w"
COMMON_OVERLAY="$PROJECT_DIR/buildroot/board/common/rootfs_overlay"
POST_BUILD="$BOARD_DIR/post_build.sh"

fail() {
    echo "[test-ring-post-build] ERROR: $*" >&2
    exit 1
}

expect_link() {
    local path="$1"
    local expected="$2"
    [ -L "$path" ] || fail "$path is not a symlink"
    [ "$(readlink "$path")" = "$expected" ] || fail "$path -> $(readlink "$path"), expected $expected"
}

seed_target() {
    local target="$1"
    cp -R "$COMMON_OVERLAY/." "$target/"
    cp -R "$BOARD_DIR/rootfs_overlay/." "$target/"
    rm -f "$target/etc/wpa_supplicant/wpa_supplicant.conf" "$target/root/.ssh/authorized_keys"
    printf 'network={}\n' > "$target/etc/wpa_supplicant/wpa_supplicant.conf"
    mkdir -p "$target/usr/local/bin" "$target/lib/firmware/brcm" "$target/etc/dropbear"
    : > "$target/etc/dropbear/dropbear_ed25519_host_key"
    for bin in chime chime-webd chime-migrate; do
        printf '#!/bin/sh\n' > "$target/usr/local/bin/$bin"
    done
    printf '#!/bin/sh\n' > "$target/etc/init.d/S49ntp"
    : > "$target/lib/firmware/brcm/brcmfmac43436-sdio.bin"
    : > "$target/lib/firmware/brcm/brcmfmac43436-sdio.txt"
    mkdir -p "$target/usr/local/share/chime-web-ui/dist"
    printf '<p>ui</p>\n' > "$target/usr/local/share/chime-web-ui/dist/index.html"
}

main() {
    local tmp target output factory
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp:-}"' EXIT
    target="$tmp/target"
    mkdir -p "$target"
    seed_target "$target"

    output="$(bash "$POST_BUILD" "$target" 2>&1)" || fail "post_build.sh failed: $output"

    rm -f "$target/etc/wpa_supplicant/wpa_supplicant.conf"
    printf 'network={}\n' > "$target/etc/wpa_supplicant/wpa_supplicant.conf"
    output="$(bash "$POST_BUILD" "$target" 2>&1)" || fail "second post_build.sh run failed: $output"

    factory="$target/usr/local/share/openchime/factory"
    [ -f "$factory/ring.conf" ] || fail "factory ring.conf missing"
    [ -f "$factory/wpa_supplicant.conf" ] || fail "factory wpa_supplicant.conf missing"
    grep -qx 'mqtt_client_id=ring' "$factory/ring.conf" || fail "ring.conf keeps the chime client id"
    grep -qx 'schema_version=[0-9]*' "$factory/ring.conf" || fail "ring.conf lost schema_version"
    if grep -q '/etc/init.d/S99chime restart' "$factory/ring.conf"; then
        fail "ring.conf still carries the Chime header"
    fi

    expect_link "$target/etc/ring.conf" /etc/persistent/ring.conf
    expect_link "$target/etc/wpa_supplicant/wpa_supplicant.conf" /etc/persistent/wpa_supplicant.conf
    expect_link "$target/etc/dropbear" /etc/persistent/dropbear
    expect_link "$target/lib/firmware/brcm/brcmfmac43436-sdio.raspberrypi,model-zero-2-w.bin" brcmfmac43436-sdio.bin

    for dir in etc/persistent var/lib/ring var/lib/bluetooth; do
        [ -d "$target/$dir" ] || fail "mount point /$dir missing"
    done
    [ ! -e "$target/etc/init.d/S49ntp" ] || fail "S49ntp was not removed"
    grep -qx 'OPENCHIME_PRODUCT=ring' "$target/etc/openchime-release" || fail "release file does not name ring"

    echo "[test-ring-post-build] passed"
}

main
