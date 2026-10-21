#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 <web-ui-dist-dir>" >&2
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

if [ "$#" -ne 1 ]; then
    usage
    exit 1
fi

DIST="$1"

error() {
    echo "ERROR: $*" >&2
    exit 1
}

[ -n "$DIST" ] || error "web UI dist path is empty"
[ -d "$DIST" ] || error "web UI dist directory is missing: $DIST"
[ -f "$DIST/index.html" ] || error "missing $DIST/index.html (Svelte production bundle was not installed)"
[ -s "$DIST/index.html" ] || error "$DIST/index.html is empty"

if grep -Fq '/src/main.ts' "$DIST/index.html"; then
    error "$DIST/index.html looks like the Vite source entry, not a production bundle"
fi

missing=0
while IFS= read -r attr; do
    [ -n "$attr" ] || continue
    ref="${attr#*=}"
    ref="${ref#\"}"
    ref="${ref%\"}"
    [ -n "$ref" ] || continue

    case "$ref" in
        http://*|https://*|data:*|//*|mailto:*|javascript:*|\#*)
            continue
            ;;
    esac

    if [ "${ref#/}" != "$ref" ]; then
        asset_path="$DIST$ref"
    else
        asset_path="$DIST/${ref#./}"
    fi

    if [ ! -f "$asset_path" ]; then
        echo "ERROR: index.html references missing asset: $asset_path" >&2
        missing=1
    fi
done < <(grep -oE '(src|href)="[^"]+"' "$DIST/index.html" || true)

if [ "$missing" -ne 0 ]; then
    exit 1
fi
