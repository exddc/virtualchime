#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
META_FILE="$PROJECT_DIR/buildroot/build_meta.env"

timestamp_utc="$(date -u +%Y%m%dT%H%M%SZ)"
source_git_sha="unknown"
source_git_short="unknown"
source_git_dirty="unknown"

if command -v git >/dev/null 2>&1 && git -C "$PROJECT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    source_git_sha="$(git -C "$PROJECT_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"
    source_git_short="$(git -C "$PROJECT_DIR" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
    if [ -n "$(git -C "$PROJECT_DIR" status --porcelain 2>/dev/null)" ]; then
        source_git_dirty="1"
    else
        source_git_dirty="0"
    fi
fi

build_id="${timestamp_utc}-${source_git_short}"
if [ "$source_git_dirty" = "1" ]; then
    build_id="${build_id}-dirty"
fi

cat > "$META_FILE" <<EOF
VIRTUALCHIME_BUILD_ID=${build_id}
CHIME_BUILD_ID=${build_id}
SOURCE_GIT_SHA=${source_git_sha}
SOURCE_GIT_SHORT=${source_git_short}
SOURCE_GIT_DIRTY=${source_git_dirty}
BUILD_TIMESTAMP_UTC=${timestamp_utc}
EOF

echo "[build-meta] wrote $META_FILE"
echo "[build-meta] CHIME_BUILD_ID=$build_id"
