#!/bin/sh
set -eu

OTA_STATE_DIR="${OTA_STATE_DIR:-/data/ota}"
OTA_PENDING_FILE="${OTA_PENDING_FILE:-$OTA_STATE_DIR/pending.env}"
OTA_STATUS_FILE="${OTA_STATUS_FILE:-$OTA_STATE_DIR/status.env}"
OTA_BOOT_DEVICE="${OTA_BOOT_DEVICE:-/dev/mmcblk0p1}"
OTA_ROOT_A_DEVICE="${OTA_ROOT_A_DEVICE:-/dev/mmcblk0p2}"
OTA_ROOT_B_DEVICE="${OTA_ROOT_B_DEVICE:-/dev/mmcblk0p3}"
OTA_CMDLINE_PATH="${OTA_CMDLINE_PATH:-/boot/cmdline.txt}"
OTA_MAX_ATTEMPTS="${OTA_MAX_ATTEMPTS:-3}"

ota_log() {
    echo "[ota] $*"
}

ensure_ota_dirs() {
    mkdir -p "$OTA_STATE_DIR" /boot
}

mount_boot_if_needed() {
    if grep -q " /boot " /proc/mounts; then
        return 0
    fi
    mount "$OTA_BOOT_DEVICE" /boot
}

slot_from_device() {
    case "${1:-}" in
        "$OTA_ROOT_A_DEVICE")
            echo "A"
            ;;
        "$OTA_ROOT_B_DEVICE")
            echo "B"
            ;;
        *)
            echo ""
            ;;
    esac
}

device_from_slot() {
    case "${1:-}" in
        A)
            echo "$OTA_ROOT_A_DEVICE"
            ;;
        B)
            echo "$OTA_ROOT_B_DEVICE"
            ;;
        *)
            return 1
            ;;
    esac
}

current_root_device() {
    local root_arg
    root_arg="$(sed -n 's/.* root=\([^ ]*\).*/\1/p' /proc/cmdline)"
    case "$root_arg" in
        /dev/mmcblk0p2|/dev/mmcblk0p3)
            echo "$root_arg"
            return 0
            ;;
    esac

    awk '$2=="/" { print $1; exit }' /proc/mounts
}

current_slot() {
    slot_from_device "$(current_root_device)"
}

inactive_slot() {
    local current
    current="$(current_slot)"
    case "$current" in
        A)
            echo "B"
            ;;
        B)
            echo "A"
            ;;
        *)
            echo "A"
            ;;
    esac
}

read_env_value() {
    local file="$1"
    local key="$2"
    [ -f "$file" ] || return 1
    sed -n "s/^${key}=//p" "$file" | tail -n 1
}

set_env_kv() {
    local file="$1"
    local key="$2"
    local value="$3"
    local tmp_file="${file}.tmp"

    mkdir -p "$(dirname "$file")"
    if [ -f "$file" ]; then
        grep -v "^${key}=" "$file" > "$tmp_file" || true
    else
        : > "$tmp_file"
    fi
    printf '%s=%s\n' "$key" "$value" >> "$tmp_file"
    mv -f "$tmp_file" "$file"
}

write_pending_state() {
    local target_slot="$1"
    local previous_slot="$2"
    local fw_version="$3"
    local sha256="$4"
    local apply_time_utc="$5"
    local attempts_left="$6"

    local tmp_file="${OTA_PENDING_FILE}.tmp"
    mkdir -p "$OTA_STATE_DIR"
    cat > "$tmp_file" <<EOF
STATE=pending
TARGET_SLOT=${target_slot}
PREVIOUS_SLOT=${previous_slot}
FW_VERSION=${fw_version}
SHA256=${sha256}
APPLY_TIME_UTC=${apply_time_utc}
ATTEMPTS_LEFT=${attempts_left}
EOF
    mv -f "$tmp_file" "$OTA_PENDING_FILE"
}

set_next_root_device() {
    local root_device="$1"
    local current_line
    local new_line

    mount_boot_if_needed
    [ -f "$OTA_CMDLINE_PATH" ] || {
        ota_log "missing cmdline file: $OTA_CMDLINE_PATH"
        return 1
    }

    current_line="$(cat "$OTA_CMDLINE_PATH")"
    if printf '%s\n' "$current_line" | grep -q ' root='; then
        new_line="$(printf '%s\n' "$current_line" | sed "s# root=[^ ]*# root=$root_device#")"
    else
        new_line="${current_line} root=${root_device}"
    fi

    printf '%s\n' "$new_line" > "${OTA_CMDLINE_PATH}.new"
    mv -f "${OTA_CMDLINE_PATH}.new" "$OTA_CMDLINE_PATH"
    sync
}

utc_now() {
    date -u +%Y-%m-%dT%H:%M:%SZ
}
