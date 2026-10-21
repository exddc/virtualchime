#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ASSERT_SCRIPT="$PROJECT_DIR/buildroot/board/common/assert_chime_web_ui_dist.sh"

fail() {
    echo "[test-assert-chime-web-ui-dist] ERROR: $*" >&2
    exit 1
}

expect_fail() {
    local dist="$1"
    local needle="$2"
    local output
    if output="$(bash "$ASSERT_SCRIPT" "$dist" 2>&1)"; then
        fail "assertion unexpectedly succeeded for $dist"
    fi
    printf '%s\n' "$output" | grep -Fq "$needle" || \
        fail "assertion error for $dist did not contain '$needle': $output"
}

main() {
    local tmp dist
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp:-}"' EXIT
    dist="$tmp/dist"

    expect_fail "$tmp/missing" "web UI dist directory is missing"

    mkdir -p "$dist"
    expect_fail "$dist" "missing $dist/index.html"

    : > "$dist/index.html"
    expect_fail "$dist" "index.html is empty"

    printf '<script type="module" src="/src/main.ts"></script>\n' > "$dist/index.html"
    expect_fail "$dist" "Vite source entry"

    mkdir -p "$dist/assets"
    printf '<script type="module" src="/assets/index-test.js"></script>\n' > "$dist/index.html"
    expect_fail "$dist" "missing asset"

    printf 'export {}\n' > "$dist/assets/index-test.js"
    bash "$ASSERT_SCRIPT" "$dist" || fail "valid dist was rejected"

    echo "[test-assert-chime-web-ui-dist] passed"
}

main
