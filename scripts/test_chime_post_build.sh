#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
POST_BUILD="$PROJECT_DIR/buildroot/board/raspberrypi0w/post_build.sh"

fail() {
    echo "[test-chime-post-build] ERROR: $*" >&2
    exit 1
}

required_ota_scripts=(
    usr/local/sbin/ota-common.sh
    usr/local/sbin/ota-install
    usr/local/sbin/ota-confirm
    usr/local/sbin/ota-rollback
    etc/init.d/S31persistent
    etc/init.d/S42otaguard
    etc/init.d/S99otaconfirm
)

seed_target() {
    local target="$1"
    local script
    mkdir -p "$target/usr/local/bin" "$target/usr/local/sbin" "$target/etc/init.d" \
        "$target/root/.ssh" "$target/lib/firmware/brcm" "$target/lib/modules" "$target/etc/dropbear"
    printf '#!/bin/sh\n' > "$target/etc/init.d/S49ntp"
    : > "$target/etc/dropbear/dropbear_ed25519_host_key"
    printf '#!/bin/sh\n' > "$target/usr/local/bin/chime"
    printf '#!/bin/sh\n' > "$target/usr/local/bin/chime-webd"
    chmod 755 "$target/usr/local/bin/chime" "$target/usr/local/bin/chime-webd"
    for script in "${required_ota_scripts[@]}"; do
        printf '#!/bin/sh\n' > "$target/$script"
        chmod 755 "$target/$script"
    done
}

main() {
    local tmp target output
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp:-}"' EXIT
    target="$tmp/target"
    seed_target "$target"

    if output="$(bash "$POST_BUILD" "$target" 2>&1)"; then
        fail "post_build.sh succeeded without baked web UI"
    fi
    printf '%s\n' "$output" | grep -Fq "web UI dist directory is missing" || \
        fail "post_build.sh did not fail on missing UI dist: $output"

    mkdir -p "$target/usr/local/share/chime-web-ui/dist/assets"
    printf '<script type="module" src="/assets/index-test.js"></script>\n' \
        > "$target/usr/local/share/chime-web-ui/dist/index.html"
    printf 'export {}\n' > "$target/usr/local/share/chime-web-ui/dist/assets/index-test.js"

    if ! output="$(bash "$POST_BUILD" "$target" 2>&1)"; then
        fail "post_build.sh failed with a valid baked UI: $output"
    fi
    [ -f "$target/etc/openchime-release" ] || fail "post_build.sh did not write /etc/openchime-release"
    [ ! -e "$target/etc/init.d/S49ntp" ] || fail "S49ntp was not removed"
    [ "$(readlink "$target/etc/dropbear")" = /etc/persistent/dropbear ] || \
        fail "/etc/dropbear does not point at /etc/persistent/dropbear"

    echo "[test-chime-post-build] passed"
}

main
