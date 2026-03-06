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
    [[ "${1:-}" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$ ]]
}

is_integer() {
    [[ "${1:-}" =~ ^[0-9]+$ ]]
}

semver_parse() {
    local value="$1"
    if [[ ! "$value" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(-([0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*))?(\+([0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*))?$ ]]; then
        return 1
    fi
    local major="${BASH_REMATCH[1]}"
    local minor="${BASH_REMATCH[2]}"
    local patch="${BASH_REMATCH[3]}"
    local prerelease="${BASH_REMATCH[5]:-}"
    printf '%s %s %s %s\n' "$major" "$minor" "$patch" "$prerelease"
}

semver_compare_prerelease() {
    local lhs="$1"
    local rhs="$2"

    if [ -z "$lhs" ] && [ -z "$rhs" ]; then
        echo 0
        return
    fi
    if [ -z "$lhs" ]; then
        echo 1
        return
    fi
    if [ -z "$rhs" ]; then
        echo -1
        return
    fi

    local -a lparts rparts
    local i li ri
    IFS='.' read -r -a lparts <<< "$lhs"
    IFS='.' read -r -a rparts <<< "$rhs"

    for ((i = 0; i < ${#lparts[@]} || i < ${#rparts[@]}; i++)); do
        li="${lparts[i]:-}"
        ri="${rparts[i]:-}"

        if [ -z "$li" ] && [ -z "$ri" ]; then
            continue
        fi
        if [ -z "$li" ]; then
            echo -1
            return
        fi
        if [ -z "$ri" ]; then
            echo 1
            return
        fi

        if is_integer "$li" && is_integer "$ri"; then
            if [ "$li" -gt "$ri" ]; then
                echo 1
                return
            fi
            if [ "$li" -lt "$ri" ]; then
                echo -1
                return
            fi
            continue
        fi
        if is_integer "$li" && ! is_integer "$ri"; then
            echo -1
            return
        fi
        if ! is_integer "$li" && is_integer "$ri"; then
            echo 1
            return
        fi
        if [[ "$li" > "$ri" ]]; then
            echo 1
            return
        fi
        if [[ "$li" < "$ri" ]]; then
            echo -1
            return
        fi
    done

    echo 0
}

semver_compare() {
    local lhs="$1"
    local rhs="$2"
    local lmajor lminor lpatch lpre
    local rmajor rminor rpatch rpre
    read -r lmajor lminor lpatch lpre <<< "$(semver_parse "$lhs")"
    read -r rmajor rminor rpatch rpre <<< "$(semver_parse "$rhs")"

    if [ "$lmajor" -gt "$rmajor" ]; then
        echo 1
        return
    fi
    if [ "$lmajor" -lt "$rmajor" ]; then
        echo -1
        return
    fi
    if [ "$lminor" -gt "$rminor" ]; then
        echo 1
        return
    fi
    if [ "$lminor" -lt "$rminor" ]; then
        echo -1
        return
    fi
    if [ "$lpatch" -gt "$rpatch" ]; then
        echo 1
        return
    fi
    if [ "$lpatch" -lt "$rpatch" ]; then
        echo -1
        return
    fi

    semver_compare_prerelease "$lpre" "$rpre"
}

semver_gt() {
    [ "$(semver_compare "$1" "$2")" -gt 0 ]
}

read_key_from_text() {
    local text="$1"
    local key="$2"
    printf '%s\n' "$text" | sed -n "s/^${key}=//p" | head -n 1 | tr -d '[:space:]'
}

read_file_from_ref() {
    local ref="$1"
    local file="$2"
    local stdout_file stderr_file rc stderr_text
    stdout_file="$(mktemp)"
    stderr_file="$(mktemp)"
    if git -C "$PROJECT_DIR" show "${ref}:${file}" >"$stdout_file" 2>"$stderr_file"; then
        cat "$stdout_file"
        rm -f "$stdout_file" "$stderr_file"
        return 0
    fi

    rc=$?
    stderr_text="$(cat "$stderr_file")"
    rm -f "$stdout_file" "$stderr_file"

    if printf '%s\n' "$stderr_text" | grep -Eq 'does not exist in|exists on disk, but not in'; then
        # Missing file in this ref is expected in some comparisons.
        return 0
    fi

    printf '%s\n' "$stderr_text" >&2
    exit "$rc"
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
        buildroot/*.md|buildroot/docs/*)
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
