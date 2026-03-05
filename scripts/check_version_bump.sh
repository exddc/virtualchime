#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

BASE_REF="${1:-HEAD~1}"
TARGET_REF="${2:-WORKTREE}"
OS_CONFIG_VERSION_FILE="buildroot/version.env"
APP_VERSION_FILE="chime/VERSION"

error() { echo "[version-check] ERROR: $*" >&2; exit 1; }
log() { echo "[version-check] $*"; }

is_semver() {
    [[ "${1:-}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
}

is_integer() {
    [[ "${1:-}" =~ ^[0-9]+$ ]]
}

semver_gt() {
    local lhs="$1"
    local rhs="$2"
    local lmajor lminor lpatch
    local rmajor rminor rpatch
    IFS='.' read -r lmajor lminor lpatch <<< "$lhs"
    IFS='.' read -r rmajor rminor rpatch <<< "$rhs"

    if [ "$lmajor" -gt "$rmajor" ]; then return 0; fi
    if [ "$lmajor" -lt "$rmajor" ]; then return 1; fi
    if [ "$lminor" -gt "$rminor" ]; then return 0; fi
    if [ "$lminor" -lt "$rminor" ]; then return 1; fi
    if [ "$lpatch" -gt "$rpatch" ]; then return 0; fi
    return 1
}

read_key_from_text() {
    local text="$1"
    local key="$2"
    printf '%s\n' "$text" | sed -n "s/^${key}=//p" | head -n 1 | tr -d '[:space:]'
}

read_file_from_ref() {
    local ref="$1"
    local file="$2"
    git -C "$PROJECT_DIR" show "${ref}:${file}" 2>/dev/null || true
}

read_file_from_target() {
    local file="$1"
    if [ "$TARGET_REF" = "WORKTREE" ]; then
        [ -f "$PROJECT_DIR/$file" ] || return 0
        cat "$PROJECT_DIR/$file"
        return 0
    fi
    read_file_from_ref "$TARGET_REF" "$file"
}

is_os_impacting_file() {
    local file="$1"
    case "$file" in
        buildroot/output/*|buildroot/dl/*)
            return 1
            ;;
        buildroot/*.md|buildroot/**/*.md|buildroot/docs/*)
            return 1
            ;;
        buildroot/*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

git -C "$PROJECT_DIR" rev-parse --verify "$BASE_REF" >/dev/null 2>&1 || \
    error "Base ref not found: $BASE_REF"

if [ "$TARGET_REF" = "WORKTREE" ]; then
    CHANGED_FILES="$(git -C "$PROJECT_DIR" diff --name-only "$BASE_REF")"
    UNTRACKED_FILES="$(git -C "$PROJECT_DIR" ls-files --others --exclude-standard)"
    if [ -n "$UNTRACKED_FILES" ]; then
        CHANGED_FILES="${CHANGED_FILES}
${UNTRACKED_FILES}"
    fi
else
    git -C "$PROJECT_DIR" rev-parse --verify "$TARGET_REF" >/dev/null 2>&1 || \
        error "Target ref not found: $TARGET_REF"
    CHANGED_FILES="$(git -C "$PROJECT_DIR" diff --name-only "$BASE_REF" "$TARGET_REF")"
fi

if [ -z "$CHANGED_FILES" ]; then
    log "No file changes between $BASE_REF and $TARGET_REF"
    exit 0
fi

REQUIRES_APP_VERSION_BUMP=0
REQUIRES_OS_CONFIG_VERSION_BUMP=0
REQUIRES_CONFIG_VERSION_BUMP=0
while IFS= read -r file; do
    case "$file" in
        chime/*|common/*|buildroot/package/chime/*)
            REQUIRES_APP_VERSION_BUMP=1
            ;;
    esac

    if is_os_impacting_file "$file"; then
        REQUIRES_OS_CONFIG_VERSION_BUMP=1
    fi

    case "$file" in
        buildroot/board/*/rootfs_overlay/etc/chime.conf)
            REQUIRES_CONFIG_VERSION_BUMP=1
            ;;
    esac
done <<< "$CHANGED_FILES"

if [ "$REQUIRES_APP_VERSION_BUMP" -eq 0 ] && [ "$REQUIRES_OS_CONFIG_VERSION_BUMP" -eq 0 ]; then
    log "No OS/app/config-impacting changes detected"
    exit 0
fi

BASE_OS_CONFIG_TEXT="$(read_file_from_ref "$BASE_REF" "$OS_CONFIG_VERSION_FILE")"
TARGET_OS_CONFIG_TEXT="$(read_file_from_target "$OS_CONFIG_VERSION_FILE")"
BASE_APP_VERSION="$(read_file_from_ref "$BASE_REF" "$APP_VERSION_FILE" | head -n 1 | tr -d '[:space:]')"
TARGET_APP_VERSION="$(read_file_from_target "$APP_VERSION_FILE" | head -n 1 | tr -d '[:space:]')"

if [ "$REQUIRES_APP_VERSION_BUMP" -eq 1 ]; then
    if ! printf '%s\n' "$CHANGED_FILES" | grep -qx "$APP_VERSION_FILE"; then
        error "App version bump required: update $APP_VERSION_FILE when changing chime/common/package files"
    fi

    if ! is_semver "$BASE_APP_VERSION" || ! is_semver "$TARGET_APP_VERSION"; then
        error "Invalid app version format. Both base/target must be SemVer in $APP_VERSION_FILE"
    fi
    if ! semver_gt "$TARGET_APP_VERSION" "$BASE_APP_VERSION"; then
        error "App version must increase: base=$BASE_APP_VERSION target=$TARGET_APP_VERSION"
    fi
fi

if [ "$REQUIRES_OS_CONFIG_VERSION_BUMP" -eq 1 ]; then
    if ! printf '%s\n' "$CHANGED_FILES" | grep -qx "$OS_CONFIG_VERSION_FILE"; then
        error "OS/config version bump required: update $OS_CONFIG_VERSION_FILE when changing OS-impacting files"
    fi

    BASE_OS_VERSION="$(read_key_from_text "$BASE_OS_CONFIG_TEXT" "VIRTUALCHIME_OS_VERSION")"
    TARGET_OS_VERSION="$(read_key_from_text "$TARGET_OS_CONFIG_TEXT" "VIRTUALCHIME_OS_VERSION")"
    BASE_CONFIG_VERSION="$(read_key_from_text "$BASE_OS_CONFIG_TEXT" "CHIME_CONFIG_VERSION")"
    TARGET_CONFIG_VERSION="$(read_key_from_text "$TARGET_OS_CONFIG_TEXT" "CHIME_CONFIG_VERSION")"

    if ! is_semver "$BASE_OS_VERSION" || ! is_semver "$TARGET_OS_VERSION"; then
        error "Invalid VIRTUALCHIME_OS_VERSION format. Both base/target must be SemVer"
    fi
    if ! semver_gt "$TARGET_OS_VERSION" "$BASE_OS_VERSION"; then
        error "VIRTUALCHIME_OS_VERSION must increase for OS-impacting changes: base=$BASE_OS_VERSION target=$TARGET_OS_VERSION"
    fi

    if ! is_integer "$BASE_CONFIG_VERSION" || ! is_integer "$TARGET_CONFIG_VERSION"; then
        error "Invalid CHIME_CONFIG_VERSION format. Both base/target must be integers"
    fi

    if [ "$REQUIRES_CONFIG_VERSION_BUMP" -eq 1 ] && [ "$TARGET_CONFIG_VERSION" -le "$BASE_CONFIG_VERSION" ]; then
        error "CHIME_CONFIG_VERSION must increase when chime.conf defaults/semantics change: base=$BASE_CONFIG_VERSION target=$TARGET_CONFIG_VERSION"
    fi
fi

if [ "$REQUIRES_CONFIG_VERSION_BUMP" -eq 1 ]; then
    log "Config version bump required and validated"
fi

log "OK: required version files were updated and monotonic"
