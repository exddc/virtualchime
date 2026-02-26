#!/usr/bin/env bash
# Build/run chime + chime-webd locally on macOS using runtime sandbox files.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
CHIME_DIR="$PROJECT_DIR/chime"
BUILD_DIR="$CHIME_DIR/build-local"
BIN_DIR="$BUILD_DIR/bin"
CHIME_BIN="$BIN_DIR/chime"
WEBD_BIN="$BIN_DIR/chime-webd"
RUNTIME_DIR="$BUILD_DIR/runtime"
RUNTIME_TLS_DIR="$RUNTIME_DIR/tls"
RUNTIME_CHIME_CONFIG="$RUNTIME_DIR/chime.conf"
RUNTIME_WPA_CONFIG="$RUNTIME_DIR/wpa_supplicant.conf"

DEFAULT_CONFIG="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf"
DEFAULT_WPA_CONFIG="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf"
DEFAULT_WPA_EXAMPLE="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf.example"
BUILDROOT_VERSION_FILE="$PROJECT_DIR/buildroot/version.env"
APP_VERSION_FILE="$PROJECT_DIR/chime/VERSION"

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

get_openssl_flags() {
    if [ -n "${OPENSSL_CFLAGS:-}" ] || [ -n "${OPENSSL_LIBS:-}" ]; then
        echo "${OPENSSL_CFLAGS:-}|${OPENSSL_LIBS:-}"
        return 0
    fi

    if command -v pkg-config &>/dev/null && pkg-config --exists openssl; then
        echo "$(pkg-config --cflags openssl)|$(pkg-config --libs openssl)"
        return 0
    fi

    if command -v brew &>/dev/null; then
        local prefix
        prefix="$(brew --prefix openssl@3 2>/dev/null || true)"
        if [ -n "$prefix" ] && [ -d "$prefix/include" ] && [ -d "$prefix/lib" ]; then
            echo "-I$prefix/include|-L$prefix/lib -lssl -lcrypto"
            return 0
        fi
    fi

    return 1
}

load_versions() {
    CHIME_APP_VERSION="dev"
    VIRTUALCHIME_OS_VERSION="dev"
    CHIME_CONFIG_VERSION="dev"

    if [ -f "$APP_VERSION_FILE" ]; then
        CHIME_APP_VERSION="$(head -n 1 "$APP_VERSION_FILE" | tr -d '[:space:]')"
    fi
    if [ -z "$CHIME_APP_VERSION" ]; then
        CHIME_APP_VERSION="dev"
    fi

    if [ -f "$BUILDROOT_VERSION_FILE" ]; then
        # shellcheck disable=SC1090
        . "$BUILDROOT_VERSION_FILE"
        VIRTUALCHIME_OS_VERSION="${VIRTUALCHIME_OS_VERSION:-$VIRTUALCHIME_OS_VERSION}"
        CHIME_CONFIG_VERSION="${CHIME_CONFIG_VERSION:-$CHIME_CONFIG_VERSION}"
    fi
}

build_chime_binary() {
    local flags
    if ! flags="$(get_mosquitto_flags)"; then
        error "libmosquitto not found. Install with:
  brew install mosquitto pkg-config"
    fi

    local cflags="${flags%%|*}"
    local libs="${flags#*|}"
    read -r -a MOSQ_CFLAGS <<< "$cflags"
    read -r -a MOSQ_LIBS <<< "$libs"

    local sources=()
    while IFS= read -r source; do
        sources+=("$source")
    done < <(
        find \
            "$CHIME_DIR/src" \
            "$PROJECT_DIR/common/src" \
            -type f -name '*.cpp' \
            ! -path "$CHIME_DIR/src/webd/*" \
            | sort
    )

    if [ "${#sources[@]}" -eq 0 ]; then
        error "No chime sources found."
    fi

    log "Compiling chime..."
    "${CXX:-c++}" \
        -std=c++20 \
        -Wall -Wextra -Wpedantic \
        -O2 \
        ${CXXFLAGS:-} \
        "-DCHIME_APP_VERSION=\"$CHIME_APP_VERSION\"" \
        "-DVIRTUALCHIME_OS_VERSION=\"$VIRTUALCHIME_OS_VERSION\"" \
        "-DCHIME_CONFIG_VERSION=\"$CHIME_CONFIG_VERSION\"" \
        -I"$CHIME_DIR/include" \
        -I"$PROJECT_DIR/common/include" \
        "${MOSQ_CFLAGS[@]}" \
        "${sources[@]}" \
        -o "$CHIME_BIN" \
        ${LDFLAGS:-} \
        "${MOSQ_LIBS[@]}"

    log "Built: $CHIME_BIN"
}

build_webd_binary() {
    local flags
    if ! flags="$(get_openssl_flags)"; then
        error "OpenSSL not found. Install with:
  brew install openssl@3 pkg-config"
    fi

    local cflags="${flags%%|*}"
    local libs="${flags#*|}"
    read -r -a SSL_CFLAGS <<< "$cflags"
    read -r -a SSL_LIBS <<< "$libs"

    local sources=(
        "$CHIME_DIR/src/webd/main.cpp"
        "$CHIME_DIR/src/webd/apply_manager.cpp"
        "$CHIME_DIR/src/webd/config_store.cpp"
        "$CHIME_DIR/src/webd/json.cpp"
        "$CHIME_DIR/src/webd/mdns.cpp"
        "$CHIME_DIR/src/webd/ui_assets.cpp"
        "$CHIME_DIR/src/webd/web_server.cpp"
        "$CHIME_DIR/src/webd/wifi_scan.cpp"
        "$PROJECT_DIR/common/src/logging/logger.cpp"
        "$PROJECT_DIR/common/src/runtime/signal_handler.cpp"
        "$PROJECT_DIR/common/src/util/environment.cpp"
    )

    log "Compiling chime-webd..."
    "${CXX:-c++}" \
        -std=c++20 \
        -Wall -Wextra -Wpedantic \
        -O2 \
        ${CXXFLAGS:-} \
        -I"$CHIME_DIR/include" \
        -I"$PROJECT_DIR/common/include" \
        "${SSL_CFLAGS[@]}" \
        "${sources[@]}" \
        -o "$WEBD_BIN" \
        ${LDFLAGS:-} \
        "${SSL_LIBS[@]}" \
        -lpthread

    log "Built: $WEBD_BIN"
}

build() {
    mkdir -p "$BUILD_DIR" "$BIN_DIR"
    load_versions
    build_chime_binary
    build_webd_binary
}

prepare_runtime() {
    local source_config="$1"

    [ -f "$source_config" ] || error "Config not found: $source_config"
    mkdir -p "$RUNTIME_DIR" "$RUNTIME_TLS_DIR"

    if [ ! -f "$RUNTIME_CHIME_CONFIG" ] || [ "${REFRESH_RUNTIME_FILES:-0}" = "1" ]; then
        cp "$source_config" "$RUNTIME_CHIME_CONFIG"
    fi

    if [ ! -f "$RUNTIME_WPA_CONFIG" ] || [ "${REFRESH_RUNTIME_FILES:-0}" = "1" ]; then
        if [ -f "$DEFAULT_WPA_CONFIG" ]; then
            cp "$DEFAULT_WPA_CONFIG" "$RUNTIME_WPA_CONFIG"
        elif [ -f "$DEFAULT_WPA_EXAMPLE" ]; then
            cp "$DEFAULT_WPA_EXAMPLE" "$RUNTIME_WPA_CONFIG"
        else
            cat > "$RUNTIME_WPA_CONFIG" <<'WPAEOF'
ctrl_interface=/var/run/wpa_supplicant
update_config=1
country=US

network={
    ssid="LOCAL_SSID"
    psk="LOCAL_PASSWORD"
}
WPAEOF
        fi
    fi

    chmod 600 "$RUNTIME_WPA_CONFIG"

    log "Runtime config: $RUNTIME_CHIME_CONFIG"
    log "Runtime WPA file: $RUNTIME_WPA_CONFIG"
}

run_chime_only() {
    if [ ! -x "$CHIME_BIN" ]; then
        build
    fi

    local config_path="${2:-${CHIME_CONFIG:-$DEFAULT_CONFIG}}"
    prepare_runtime "$config_path"

    local client_id="${CHIME_MQTT_CLIENT_ID:-chime-local-$(hostname -s)}"

    log "Starting chime with config: $RUNTIME_CHIME_CONFIG"
    CHIME_CONFIG="$RUNTIME_CHIME_CONFIG" \
    CHIME_MQTT_CLIENT_ID="$client_id" \
    "$CHIME_BIN"
}

run_webd_only() {
    if [ ! -x "$WEBD_BIN" ]; then
        build
    fi

    local config_path="${2:-${CHIME_CONFIG:-$DEFAULT_CONFIG}}"
    prepare_runtime "$config_path"

    local web_bind="${CHIME_WEBD_BIND_ADDRESS:-127.0.0.1}"
    local web_port="${CHIME_WEBD_PORT:-8443}"

    log "Starting chime-webd on https://$web_bind:$web_port"
    CHIME_WEBD_CHIME_CONFIG="$RUNTIME_CHIME_CONFIG" \
    CHIME_WEBD_WPA_SUPPLICANT="$RUNTIME_WPA_CONFIG" \
    CHIME_WEBD_TLS_CERT="${CHIME_WEBD_TLS_CERT:-$RUNTIME_TLS_DIR/cert.pem}" \
    CHIME_WEBD_TLS_KEY="${CHIME_WEBD_TLS_KEY:-$RUNTIME_TLS_DIR/key.pem}" \
    CHIME_WEBD_BIND_ADDRESS="$web_bind" \
    CHIME_WEBD_PORT="$web_port" \
    CHIME_WEBD_MDNS_ENABLED="${CHIME_WEBD_MDNS_ENABLED:-false}" \
    CHIME_WEBD_NETWORK_RESTART_CMD="${CHIME_WEBD_NETWORK_RESTART_CMD:-true}" \
    CHIME_WEBD_CHIME_RESTART_CMD="${CHIME_WEBD_CHIME_RESTART_CMD:-true}" \
    "$WEBD_BIN"
}

run_stack() {
    if [ ! -x "$CHIME_BIN" ] || [ ! -x "$WEBD_BIN" ]; then
        build
    fi

    local config_path="${2:-${CHIME_CONFIG:-$DEFAULT_CONFIG}}"
    prepare_runtime "$config_path"

    local client_id="${CHIME_MQTT_CLIENT_ID:-chime-local-$(hostname -s)}"
    local web_bind="${CHIME_WEBD_BIND_ADDRESS:-127.0.0.1}"
    local web_port="${CHIME_WEBD_PORT:-8443}"

    log "Starting local stack"
    log "  chime config: $RUNTIME_CHIME_CONFIG"
    log "  web URL: https://$web_bind:$web_port"

    local restart_delay="${LOCAL_SUPERVISOR_RESTART_DELAY:-2}"

    local chime_supervisor_pid=""
    local webd_supervisor_pid=""

    (
        while true; do
            set +e
            CHIME_CONFIG="$RUNTIME_CHIME_CONFIG" \
            CHIME_MQTT_CLIENT_ID="$client_id" \
            "$CHIME_BIN"
            rc=$?
            set -e
            log "chime exited with code $rc, restarting in ${restart_delay}s"
            sleep "$restart_delay"
        done
    ) &
    chime_supervisor_pid=$!

    (
        while true; do
            set +e
            CHIME_WEBD_CHIME_CONFIG="$RUNTIME_CHIME_CONFIG" \
            CHIME_WEBD_WPA_SUPPLICANT="$RUNTIME_WPA_CONFIG" \
            CHIME_WEBD_TLS_CERT="${CHIME_WEBD_TLS_CERT:-$RUNTIME_TLS_DIR/cert.pem}" \
            CHIME_WEBD_TLS_KEY="${CHIME_WEBD_TLS_KEY:-$RUNTIME_TLS_DIR/key.pem}" \
            CHIME_WEBD_BIND_ADDRESS="$web_bind" \
            CHIME_WEBD_PORT="$web_port" \
            CHIME_WEBD_MDNS_ENABLED="${CHIME_WEBD_MDNS_ENABLED:-false}" \
            CHIME_WEBD_NETWORK_RESTART_CMD="${CHIME_WEBD_NETWORK_RESTART_CMD:-true}" \
            CHIME_WEBD_CHIME_RESTART_CMD="${CHIME_WEBD_CHIME_RESTART_CMD:-true}" \
            "$WEBD_BIN"
            rc=$?
            set -e
            log "chime-webd exited with code $rc, restarting in ${restart_delay}s"
            sleep "$restart_delay"
        done
    ) &
    webd_supervisor_pid=$!

    cleanup() {
        local chime_pid="${chime_supervisor_pid:-}"
        local webd_pid="${webd_supervisor_pid:-}"
        if [ -n "$chime_pid" ]; then
            kill "$chime_pid" 2>/dev/null || true
            pkill -P "$chime_pid" 2>/dev/null || true
            wait "$chime_pid" 2>/dev/null || true
        fi
        if [ -n "$webd_pid" ]; then
            kill "$webd_pid" 2>/dev/null || true
            pkill -P "$webd_pid" 2>/dev/null || true
            wait "$webd_pid" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    wait "$chime_supervisor_pid" "$webd_supervisor_pid" || true
    trap - EXIT INT TERM
    cleanup
}

usage() {
    cat <<EOF
Usage: $0 [build|run|run-chime|run-webd] [config_path]

build:      Build local chime and chime-webd binaries
run:        Build if needed and run both daemons (mirrors Raspberry layout)
run-chime:  Build if needed and run only chime
run-webd:   Build if needed and run only chime-webd

By default, runtime state is stored under:
  $RUNTIME_DIR

Environment overrides:
  CHIME_MQTT_CLIENT_ID            MQTT client id for local chime run
  CHIME_WEBD_BIND_ADDRESS         webd bind address (default: 127.0.0.1)
  CHIME_WEBD_PORT                 webd port (default: 8443)
  CHIME_WEBD_NETWORK_RESTART_CMD  apply command override (default: true locally)
  CHIME_WEBD_CHIME_RESTART_CMD    apply command override (default: true locally)
  REFRESH_RUNTIME_FILES=1         reset runtime config/wpa files from source

Examples:
  $0 build
  $0 run
  $0 run /path/to/chime.conf
  CHIME_WEBD_PORT=9443 $0 run
  $0 run-webd
EOF
}

ACTION="${1:-build}"
case "$ACTION" in
    build)
        build
        ;;
    run)
        run_stack "$@"
        ;;
    run-chime)
        run_chime_only "$@"
        ;;
    run-webd)
        run_webd_only "$@"
        ;;
    -h|--help)
        usage
        ;;
    *)
        usage
        exit 1
        ;;
esac
