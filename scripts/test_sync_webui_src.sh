#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYNC_SCRIPT="$SCRIPT_DIR/sync_webui_src.sh"

fail() {
    echo "[test-sync-webui-src] ERROR: $*" >&2
    exit 1
}

expect_unsafe_destination() {
    local source="$1"
    local destination="$2"
    local sentinel="$3"

    if bash "$SYNC_SCRIPT" "$source" "$destination"; then
        fail "unsafe destination was accepted: $destination"
    fi
    [ -e "$sentinel" ] || fail "unsafe destination was modified: $destination"
}

seed_webui() {
    local source="$1"
    mkdir -p "$source/webui/src" "$source/webui/dist" "$source/webui/node_modules/pkg"
    printf '{ "name": "openchime-webui" }\n' > "$source/webui/package.json"
    printf '{ "lockfileVersion": 1 }\n' > "$source/webui/bun.lock"
    printf '<html><script type="module" src="/src/main.ts"></script></html>\n' > "$source/webui/index.html"
    printf 'export {}\n' > "$source/webui/src/main.ts"
    printf 'host-dist\n' > "$source/webui/dist/index.html"
    printf 'host-dep\n' > "$source/webui/node_modules/pkg/index.js"
}

main() {
    local tmp source destination first_id second_id
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp:-}"' EXIT
    source="$tmp/repo"
    destination="$tmp/webui-src"

    mkdir -p "$destination/stale-root"
    seed_webui "$source"
    printf 'stale\n' > "$destination/stale-root/leftover"
    printf 'openchime-webui-sync-root\n' > "$destination/.openchime-webui-sync-root"

    bash "$SYNC_SCRIPT" "$source" "$destination"
    [ -f "$destination/package.json" ] || fail "sync dropped package.json"
    [ -f "$destination/bun.lock" ] || fail "sync dropped bun.lock"
    [ -f "$destination/src/main.ts" ] || fail "sync dropped src/main.ts"
    [ -s "$destination/.source-id" ] || fail "sync did not write .source-id"
    [ ! -e "$destination/dist" ] || fail "sync copied host webui/dist"
    [ ! -e "$destination/node_modules" ] || fail "sync copied host webui/node_modules"
    [ ! -e "$destination/stale-root" ] || fail "sync retained obsolete root entry"
    first_id="$(tr -d '[:space:]' < "$destination/.source-id")"

    mkdir -p "$destination/dist" "$destination/node_modules"
    printf 'stale-dist\n' > "$destination/dist/index.html"
    bash "$SYNC_SCRIPT" "$source" "$destination"
    [ ! -e "$destination/dist" ] || fail "sync retained dest dist/"
    [ ! -e "$destination/node_modules" ] || fail "sync retained dest node_modules/"

    printf 'export const changed = 1;\n' > "$source/webui/src/main.ts"
    bash "$SYNC_SCRIPT" "$source" "$destination"
    second_id="$(tr -d '[:space:]' < "$destination/.source-id")"
    [ "$first_id" != "$second_id" ] || fail "source-id did not change after webui source edit"

    local unmanaged="$tmp/unmanaged"
    mkdir -p "$unmanaged"
    printf 'keep\n' > "$unmanaged/sentinel"
    if bash "$SYNC_SCRIPT" "$source" "$unmanaged"; then
        fail "unmanaged destination was accepted"
    fi
    [ -e "$unmanaged/sentinel" ] || fail "unmanaged destination was modified"

    mkdir -p "$source/unsafe-destination"
    printf 'keep\n' > "$source/unsafe-destination/sentinel"
    expect_unsafe_destination "$source" "$source/unsafe-destination" "$source/unsafe-destination/sentinel"
    if bash "$SYNC_SCRIPT" "$source" "$source/new-unsafe-destination"; then
        fail "unsafe new destination was accepted"
    fi
    [ ! -e "$source/new-unsafe-destination" ] || fail "unsafe new destination was created"
    expect_unsafe_destination "$source" "$tmp" "$source/webui/package.json"
    expect_unsafe_destination "$source" "$source" "$source/webui/package.json"
    expect_unsafe_destination "$source" / "$source/webui/package.json"

    echo "[test-sync-webui-src] passed"
}

main
