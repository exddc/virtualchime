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

git -C "$PROJECT_DIR" rev-parse --verify "$BASE_REF" >/dev/null 2>&1 || \
    error "Base ref not found: $BASE_REF"

if [ "$TARGET_REF" = "WORKTREE" ]; then
    CHANGED_FILES="$(git -C "$PROJECT_DIR" diff --name-only "$BASE_REF")"
    OS_CONFIG_VERSION_DIFF="$(git -C "$PROJECT_DIR" diff "$BASE_REF" -- "$OS_CONFIG_VERSION_FILE")"
    APP_VERSION_DIFF="$(git -C "$PROJECT_DIR" diff "$BASE_REF" -- "$APP_VERSION_FILE")"
    UNTRACKED_FILES="$(git -C "$PROJECT_DIR" ls-files --others --exclude-standard)"
    if [ -n "$UNTRACKED_FILES" ]; then
        CHANGED_FILES="${CHANGED_FILES}
${UNTRACKED_FILES}"
    fi
    if [ -z "$OS_CONFIG_VERSION_DIFF" ] && [ -f "$PROJECT_DIR/$OS_CONFIG_VERSION_FILE" ] && \
        ! git -C "$PROJECT_DIR" ls-files --error-unmatch "$OS_CONFIG_VERSION_FILE" >/dev/null 2>&1; then
        OS_CONFIG_VERSION_DIFF="$(sed 's/^/+/' "$PROJECT_DIR/$OS_CONFIG_VERSION_FILE")"
    fi
    if [ -z "$APP_VERSION_DIFF" ] && [ -f "$PROJECT_DIR/$APP_VERSION_FILE" ] && \
        ! git -C "$PROJECT_DIR" ls-files --error-unmatch "$APP_VERSION_FILE" >/dev/null 2>&1; then
        APP_VERSION_DIFF="$(sed 's/^/+/' "$PROJECT_DIR/$APP_VERSION_FILE")"
    fi
else
    git -C "$PROJECT_DIR" rev-parse --verify "$TARGET_REF" >/dev/null 2>&1 || \
        error "Target ref not found: $TARGET_REF"
    CHANGED_FILES="$(git -C "$PROJECT_DIR" diff --name-only "$BASE_REF" "$TARGET_REF")"
    OS_CONFIG_VERSION_DIFF="$(git -C "$PROJECT_DIR" diff "$BASE_REF" "$TARGET_REF" -- "$OS_CONFIG_VERSION_FILE")"
    APP_VERSION_DIFF="$(git -C "$PROJECT_DIR" diff "$BASE_REF" "$TARGET_REF" -- "$APP_VERSION_FILE")"
fi

if [ -z "$CHANGED_FILES" ]; then
    log "No file changes between $BASE_REF and $TARGET_REF"
    exit 0
fi

REQUIRES_APP_VERSION_BUMP=0
REQUIRES_OS_CONFIG_VERSION_BUMP=0
while IFS= read -r file; do
    case "$file" in
        chime/*|common/*|buildroot/package/chime/*)
            REQUIRES_APP_VERSION_BUMP=1
            ;;
        buildroot/configs/*|buildroot/board/*)
            REQUIRES_OS_CONFIG_VERSION_BUMP=1
            ;;
    esac
done <<< "$CHANGED_FILES"

if [ "$REQUIRES_APP_VERSION_BUMP" -eq 0 ] && [ "$REQUIRES_OS_CONFIG_VERSION_BUMP" -eq 0 ]; then
    log "No OS/app/config-impacting changes detected"
    exit 0
fi

if [ "$REQUIRES_APP_VERSION_BUMP" -eq 1 ]; then
    if ! printf '%s\n' "$CHANGED_FILES" | grep -qx "$APP_VERSION_FILE"; then
        error "App version bump required: update $APP_VERSION_FILE when changing chime/common/package files"
    fi
    if ! printf '%s\n' "$APP_VERSION_DIFF" | grep -Eq '^[+-][0-9]+\.[0-9]+\.[0-9]+$'; then
        error "App version file changed but no SemVer value update found in $APP_VERSION_FILE"
    fi
fi

if [ "$REQUIRES_OS_CONFIG_VERSION_BUMP" -eq 1 ]; then
    if ! printf '%s\n' "$CHANGED_FILES" | grep -qx "$OS_CONFIG_VERSION_FILE"; then
        error "OS/config version bump required: update $OS_CONFIG_VERSION_FILE when changing buildroot board/configs"
    fi
    if ! printf '%s\n' "$OS_CONFIG_VERSION_DIFF" | \
        grep -Eq '^[+-](VIRTUALCHIME_OS_VERSION|CHIME_CONFIG_VERSION)='; then
        error "OS/config version file changed but no key update found in $OS_CONFIG_VERSION_FILE"
    fi
fi

log "OK: required version files were updated"
