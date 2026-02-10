#!/usr/bin/env bash
# Build/run chime locally on macOS using the device config
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
CHIME_DIR="$PROJECT_DIR/chime"
BUILD_DIR="$CHIME_DIR/build-local"
BIN="$BUILD_DIR/chime"
DEFAULT_CONFIG="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf"

log() { echo "[local-chime] $*"; }
error() { echo "[local-chime] ERROR: $*" >&2; exit 1; }

get_mosquitto_flags() {
    if [ -n "${MOSQUITTO_CFLAGS:-}" ] || [ -n "${MOSQUITTO_LIBS:-}" ]; then
        echo "${MOSQUITTO_CFLAGS:-}|${MOSQUITTO_LIBS:-}"
        return 0
    fi

    if command -v pkg-config &>/dev/null; then
        if pkg-config --exists libmosquitto; then
            echo "$(pkg-config --cflags libmosquitto)|$(pkg-config --libs libmosquitto)"
            return 0
        fi
        if pkg-config --exists mosquitto; then
            echo "$(pkg-config --cflags mosquitto)|$(pkg-config --libs mosquitto)"
            return 0
        fi
    fi

    if command -v brew &>/dev/null; then
        local prefix
        prefix="$(brew --prefix mosquitto 2>/dev/null || true)"
        if [ -n "$prefix" ] && [ -d "$prefix/include" ] && [ -d "$prefix/lib" ]; then
            echo "-I$prefix/include|-L$prefix/lib -lmosquitto"
            return 0
        fi
    fi

    return 1
}

build() {
    local flags
    if ! flags="$(get_mosquitto_flags)"; then
        error "libmosquitto not found. Install with:
  brew install mosquitto pkg-config"
    fi

    local cflags="${flags%%|*}"
    local libs="${flags#*|}"
    read -r -a MOSQ_CFLAGS <<< "$cflags"
    read -r -a MOSQ_LIBS <<< "$libs"

    mkdir -p "$BUILD_DIR"
    SOURCES=()
    while IFS= read -r source; do
        SOURCES+=("$source")
    done < <(find "$CHIME_DIR/src" "$PROJECT_DIR/common/src" -type f -name '*.cpp' | sort)
    if [ "${#SOURCES[@]}" -eq 0 ]; then
        error "No source files found."
    fi

    log "Compiling..."
    "${CXX:-c++}" \
        -std=c++20 \
        -Wall -Wextra -Wpedantic \
        -O2 \
        ${CXXFLAGS:-} \
        -I"$CHIME_DIR/include" \
        -I"$PROJECT_DIR/common/include" \
        "${MOSQ_CFLAGS[@]}" \
        "${SOURCES[@]}" \
        -o "$BIN" \
        ${LDFLAGS:-} \
        "${MOSQ_LIBS[@]}"

    log "Build complete: $BIN"
}

run() {
    if [ ! -x "$BIN" ]; then
        build
    fi

    local config_path="${2:-${CHIME_CONFIG:-$DEFAULT_CONFIG}}"
    if [ ! -f "$config_path" ]; then
        error "Config not found: $config_path"
    fi

    local client_id="${CHIME_MQTT_CLIENT_ID:-chime-local-$(hostname -s)}"
    log "Running with config: $config_path"
    log "Using MQTT client id: $client_id"
    CHIME_CONFIG="$config_path" CHIME_MQTT_CLIENT_ID="$client_id" "$BIN"
}

usage() {
    cat <<EOF
Usage: $0 [build|run] [config_path]

build: Build the local chime binary
run:   Build if needed and run with config (default: $DEFAULT_CONFIG)
       Uses CHIME_MQTT_CLIENT_ID if set, otherwise defaults to
       chime-local-\$(hostname -s)

Examples:
  $0 build
  $0 run
  $0 run /path/to/chime.conf
  CHIME_MQTT_CLIENT_ID=chime-local-dev $0 run
EOF
}

ACTION="${1:-build}"
case "$ACTION" in
    build) build ;;
    run) run "$@" ;;
    *) usage; exit 1 ;;
esac
