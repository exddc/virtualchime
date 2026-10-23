#!/bin/bash
# Shared post-build steps for every Open Chime product image. Sourced by
# board/<board>/post_build.sh after it sets TARGET_DIR.

OC_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OC_VERSION_FILE="${OC_COMMON_DIR}/../../version.env"
OC_BUILD_META_FILE="${OC_COMMON_DIR}/../../build_meta.env"

oc_assert_web_ui_dist() {
    bash "${OC_COMMON_DIR}/assert_chime_web_ui_dist.sh" "${TARGET_DIR}/usr/local/share/chime-web-ui/dist"
}

oc_chmod_binaries() {
    local bin
    for bin in "$@"; do
        if [ -f "${TARGET_DIR}/usr/local/bin/${bin}" ]; then
            chmod 755 "${TARGET_DIR}/usr/local/bin/${bin}"
        fi
    done
}

# Usage: oc_require_executables <path relative to TARGET_DIR>...
oc_require_executables() {
    local missing=()
    local script
    for script in "$@"; do
        if [ -f "${TARGET_DIR}/${script}" ]; then
            chmod 755 "${TARGET_DIR}/${script}"
        else
            missing+=("${TARGET_DIR}/${script}")
        fi
    done
    if [ "${#missing[@]}" -gt 0 ]; then
        for script in "${missing[@]}"; do
            echo "ERROR: required script missing: $script" >&2
        done
        exit 1
    fi
}

oc_require_ota_scripts() {
    mkdir -p "${TARGET_DIR}/boot" "${TARGET_DIR}/data/ota" "${TARGET_DIR}/usr/local/sbin"
    oc_require_executables \
        usr/local/sbin/ota-common.sh \
        usr/local/sbin/ota-install \
        usr/local/sbin/ota-confirm \
        usr/local/sbin/ota-rollback \
        etc/init.d/S31persistent \
        etc/init.d/S42otaguard \
        etc/init.d/S99otaconfirm
}

oc_fix_ssh_permissions() {
    if [ -d "${TARGET_DIR}/root/.ssh" ]; then
        chmod 700 "${TARGET_DIR}/root/.ssh"
        chmod 600 "${TARGET_DIR}/root/.ssh/authorized_keys" 2>/dev/null || true
    fi
}

# Steps every product image needs. The board's S31persistent must bind /data/etc
# onto /etc/persistent and create /data/etc/dropbear.
oc_common_post_build() {
    oc_fix_ssh_permissions
    # S41timesync owns clock sync; the ntp package's daemon would fight its ntpd -q.
    rm -f "${TARGET_DIR}/etc/init.d/S49ntp"
    # SSH host keys live on /data so they survive OTA rootfs updates.
    rm -rf "${TARGET_DIR}/etc/dropbear"
    ln -sfn /etc/persistent/dropbear "${TARGET_DIR}/etc/dropbear"
}

oc_kernel_release() {
    local dir=""
    if [ -d "${TARGET_DIR}/lib/modules" ]; then
        dir="$(find "${TARGET_DIR}/lib/modules" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -n 1 || true)"
    fi
    echo "${dir##*/}"
}

# Usage: oc_write_release <product> <product config file relative to TARGET_DIR>
oc_write_release() {
    local product="$1"
    local config_rel="$2"
    local kernel_release
    kernel_release="$(oc_kernel_release)"

    if [ -f "$OC_VERSION_FILE" ]; then
        # shellcheck disable=SC1090
        . "$OC_VERSION_FILE"
    fi
    if [ -f "$OC_BUILD_META_FILE" ]; then
        # shellcheck disable=SC1090
        . "$OC_BUILD_META_FILE"
    fi

    local os_version="${OPENCHIME_OS_VERSION:-unknown}"
    local config_version="${CHIME_CONFIG_VERSION:-unknown}"
    local buildroot_release="${BUILDROOT_VERSION:-unknown}"
    local build_timestamp="${BUILD_TIMESTAMP_UTC:-$(date -u +%Y-%m-%dT%H:%M:%SZ)}"
    local config_sha256="unknown"
    local app_version="unknown"
    local chime_build_id="${CHIME_BUILD_ID:-unknown}"
    local os_build_id="${OPENCHIME_BUILD_ID:-unknown}"

    if [ -f "${TARGET_DIR}/etc/chime-app-version" ]; then
        app_version="$(head -n 1 "${TARGET_DIR}/etc/chime-app-version" | tr -d "[:space:]")"
    fi
    if [ -f "${TARGET_DIR}/etc/chime-build-id" ]; then
        chime_build_id="$(head -n 1 "${TARGET_DIR}/etc/chime-build-id" | tr -d "[:space:]")"
    fi
    if [ -f "${TARGET_DIR}/${config_rel}" ] && command -v sha256sum &>/dev/null; then
        config_sha256="$(sha256sum "${TARGET_DIR}/${config_rel}" | awk '{print $1}')"
    fi

    local app_version_full="$app_version"
    if [ "$chime_build_id" != "unknown" ] && [ -n "$chime_build_id" ]; then
        app_version_full="${app_version}+${chime_build_id}"
    fi
    local os_version_full="$os_version"
    if [ "$os_build_id" != "unknown" ] && [ -n "$os_build_id" ]; then
        os_version_full="${os_version}+${os_build_id}"
    fi

    cat > "${TARGET_DIR}/etc/openchime-release" <<EOF
OPENCHIME_PRODUCT=${product}
OPENCHIME_OS_VERSION=${os_version}
OPENCHIME_OS_VERSION_FULL=${os_version_full}
CHIME_APP_VERSION=${app_version}
CHIME_APP_VERSION_FULL=${app_version_full}
CHIME_CONFIG_VERSION=${config_version}
BUILDROOT_RELEASE=${buildroot_release}
BUILD_TIMESTAMP_UTC=${build_timestamp}
LINUX_KERNEL_RELEASE=${kernel_release:-unknown}
CHIME_CONFIG_SHA256=${config_sha256}
CHIME_BUILD_ID=${chime_build_id}
OPENCHIME_BUILD_ID=${os_build_id}
SOURCE_GIT_SHA=${SOURCE_GIT_SHA:-unknown}
SOURCE_GIT_SHORT=${SOURCE_GIT_SHORT:-unknown}
SOURCE_GIT_DIRTY=${SOURCE_GIT_DIRTY:-unknown}
EOF
    chmod 644 "${TARGET_DIR}/etc/openchime-release"
}
