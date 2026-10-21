#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <repo_root> <dest_webui_src>" >&2
    exit 1
fi

require_tool() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: required tool not found: $1" >&2
        exit 1
    }
}

is_within() {
    local path="$1"
    local parent="$2"
    [ "$path" = "$parent" ] || [[ "$path" == "$parent/"* ]]
}

hash_tree() {
    local dir="$1"
    (
        cd "$dir"
        find . -type f ! -name '.openchime-webui-sync-root' ! -name '.source-id' -print0 \
            | LC_ALL=C sort -z \
            | if command -v sha256sum >/dev/null 2>&1; then
                xargs -0 sha256sum | sha256sum
            else
                xargs -0 shasum -a 256 | shasum -a 256
            fi
    ) | awk '{print $1}'
}

REPO_ROOT="$(cd "$1" && pwd -P)"
if [ -e "$2" ]; then
    [ -d "$2" ] || {
        echo "ERROR: destination is not a directory: $2" >&2
        exit 1
    }
    DEST="$(cd "$2" && pwd -P)"
else
    DEST_PARENT="$(cd "$(dirname "$2")" && pwd -P)"
    DEST="$DEST_PARENT/$(basename "$2")"
fi
MARKER_FILE="$DEST/.openchime-webui-sync-root"
SOURCE_ID_FILE="$DEST/.source-id"
WEBUI_DIR="$REPO_ROOT/webui"

[ -d "$WEBUI_DIR" ] || {
    echo "ERROR: missing $WEBUI_DIR" >&2
    exit 1
}
[ -f "$WEBUI_DIR/package.json" ] || {
    echo "ERROR: missing $WEBUI_DIR/package.json" >&2
    exit 1
}
[ -f "$WEBUI_DIR/bun.lock" ] || {
    echo "ERROR: missing $WEBUI_DIR/bun.lock" >&2
    exit 1
}
[ -f "$WEBUI_DIR/index.html" ] || {
    echo "ERROR: missing $WEBUI_DIR/index.html" >&2
    exit 1
}
[ -d "$WEBUI_DIR/src" ] || {
    echo "ERROR: missing $WEBUI_DIR/src" >&2
    exit 1
}

require_tool rsync

if [ "$DEST" = "/" ] || is_within "$DEST" "$REPO_ROOT" || is_within "$REPO_ROOT" "$DEST"; then
    echo "ERROR: unsafe destination: $DEST" >&2
    exit 1
fi

mkdir -p "$DEST"

if [ ! -f "$MARKER_FILE" ] || ! grep -qx 'openchime-webui-sync-root' "$MARKER_FILE"; then
    if [ -n "$(find "$DEST" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
        echo "ERROR: destination is not managed by sync_webui_src.sh: $DEST" >&2
        exit 1
    fi
    printf 'openchime-webui-sync-root\n' > "$MARKER_FILE"
fi

rsync -a --delete \
    --exclude 'node_modules/' \
    --exclude 'dist/' \
    --exclude '.vite/' \
    --exclude '.DS_Store' \
    "$WEBUI_DIR/" "$DEST/"

rm -rf "$DEST/node_modules" "$DEST/dist" "$DEST/.vite"
printf 'openchime-webui-sync-root\n' > "$MARKER_FILE"

if [ -e "$DEST/dist" ] || [ -e "$DEST/node_modules" ]; then
    echo "ERROR: staged webui still contains dist/ or node_modules/: $DEST" >&2
    exit 1
fi

hash_tree "$DEST" > "$SOURCE_ID_FILE"
[ -s "$SOURCE_ID_FILE" ] || {
    echo "ERROR: failed to write $SOURCE_ID_FILE" >&2
    exit 1
}
