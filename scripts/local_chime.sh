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
WEBUI_DIR="$PROJECT_DIR/webui"
WEBUI_DIST_DIR="$WEBUI_DIR/dist"

DEFAULT_CONFIG="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf"
DEFAULT_WPA_CONFIG="$PROJECT_DIR/buildroot/board/common/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf"
DEFAULT_WPA_EXAMPLE="$PROJECT_DIR/buildroot/board/common/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf.example"
BUILDROOT_VERSION_FILE="$PROJECT_DIR/buildroot/version.env"
APP_VERSION_FILE="$PROJECT_DIR/chime/VERSION"

log() { echo "[local-chime] $*"; }
error() { echo "[local-chime] ERROR: $*" >&2; exit 1; }

apply_local_webd_auth_mode() {
    if [ "${LOCAL_CHIME_UNPAIRED:-0}" = "1" ]; then
        unset CHIME_WEBD_BOOTSTRAP_PASSWORD
        return
    fi
    export CHIME_WEBD_BOOTSTRAP_PASSWORD="${CHIME_WEBD_BOOTSTRAP_PASSWORD:-openchime-local}"
}

log_local_webd_auth_mode() {
    if [ "${LOCAL_CHIME_UNPAIRED:-0}" = "1" ]; then
        log "Unpaired mode: no bootstrap password. Pairing code is printed on stdout, or set CHIME_WEBD_PAIRING_CODE."
        return
    fi
    if [ -n "${CHIME_WEBD_BOOTSTRAP_PASSWORD:-}" ]; then
        log "Local admin password: set from CHIME_WEBD_BOOTSTRAP_PASSWORD"
    else
        log "Local admin password: openchime-local"
    fi
}

require_bun() {
    if ! command -v bun &>/dev/null; then
        error "bun is required for web UI commands. Install with:
  curl -fsSL https://bun.sh/install | bash"
    fi
}

get_brew_prefix() {
    local formula="$1"
    if ! command -v brew &>/dev/null; then
        return 1
    fi
    local prefix
    prefix="$(brew --prefix "$formula" 2>/dev/null || true)"
    if [ -n "$prefix" ] && [ -d "$prefix" ]; then
        printf '%s\n' "$prefix"
        return 0
    fi
    return 1
}

path_list_contains() {
    local haystack="$1"
    local needle="$2"
    local sep="$3"
    local old_ifs="$IFS"
    local item
    IFS="$sep"
    for item in $haystack; do
        if [ "$item" = "$needle" ]; then
            IFS="$old_ifs"
            return 0
        fi
    done
    IFS="$old_ifs"
    return 1
}

append_path_list() {
    local current="$1"
    local value="$2"
    local sep="$3"
    if [ -z "$value" ]; then
        printf '%s' "$current"
        return
    fi
    if [ -z "$current" ]; then
        printf '%s' "$value"
        return
    fi
    if path_list_contains "$current" "$value" "$sep"; then
        printf '%s' "$current"
        return
    fi
    printf '%s%s%s' "$current" "$sep" "$value"
}

discover_cmake_prefixes() {
    local prefixes=""
    local pkg_paths=""
    local prefix

    if [ -n "${CMAKE_PREFIX_PATH:-}" ]; then
        local old_ifs="$IFS"
        local cmake_prefix
        IFS=':'
        for cmake_prefix in $CMAKE_PREFIX_PATH; do
            prefixes="$(append_path_list "$prefixes" "$cmake_prefix" ';')"
        done
        IFS="$old_ifs"
    fi

    local formula
    for formula in mosquitto openssl@3 openssl; do
        if prefix="$(get_brew_prefix "$formula")"; then
            prefixes="$(append_path_list "$prefixes" "$prefix" ';')"
            if [ -d "$prefix/lib/pkgconfig" ]; then
                pkg_paths="$(append_path_list "$pkg_paths" "$prefix/lib/pkgconfig" ':')"
            fi
        fi
    done

    if [ -z "${OPENSSL_ROOT_DIR:-}" ]; then
        if prefix="$(get_brew_prefix openssl@3)" || prefix="$(get_brew_prefix openssl)"; then
            OPENSSL_ROOT_DIR="$prefix"
        fi
    fi

    CMAKE_PREFIX_ARGS=()
    if [ -n "$prefixes" ]; then
        CMAKE_PREFIX_ARGS+=("-DCMAKE_PREFIX_PATH=$prefixes")
    fi
    if [ -n "${OPENSSL_ROOT_DIR:-}" ]; then
        CMAKE_PREFIX_ARGS+=("-DOPENSSL_ROOT_DIR=$OPENSSL_ROOT_DIR")
    fi

    if [ -n "$pkg_paths" ]; then
        if [ -n "${PKG_CONFIG_PATH:-}" ]; then
            export PKG_CONFIG_PATH="$pkg_paths:$PKG_CONFIG_PATH"
        else
            export PKG_CONFIG_PATH="$pkg_paths"
        fi
    fi
}

cmake_generator_args() {
    if command -v ninja &>/dev/null; then
        printf '%s\n' "-GNinja"
    fi
}

load_versions() {
    CHIME_APP_VERSION="dev"
    OPENCHIME_OS_VERSION="dev"
    CHIME_CONFIG_VERSION="dev"
    CHIME_BUILD_ID="${CHIME_BUILD_ID:-unknown}"

    if [ -f "$APP_VERSION_FILE" ]; then
        CHIME_APP_VERSION="$(head -n 1 "$APP_VERSION_FILE" | tr -d '[:space:]')"
    fi
    if [ -z "$CHIME_APP_VERSION" ]; then
        CHIME_APP_VERSION="dev"
    fi

    if [ -f "$BUILDROOT_VERSION_FILE" ]; then
        # shellcheck disable=SC1090
        . "$BUILDROOT_VERSION_FILE"
        OPENCHIME_OS_VERSION="${OPENCHIME_OS_VERSION:-dev}"
        CHIME_CONFIG_VERSION="${CHIME_CONFIG_VERSION:-dev}"
    fi
}

configure_cmake() {
    command -v cmake &>/dev/null || error "cmake is required. Install with:
  brew install cmake ninja pkg-config openssl@3"

    discover_cmake_prefixes

    # Old handwritten builds left binaries named `chime` in the build dir.
    # CMake needs that path as the `chime/` subdirectory output folder.
    local collide
    for collide in chime platform cmake; do
        if [ -e "$BUILD_DIR/$collide" ] && [ ! -d "$BUILD_DIR/$collide" ]; then
            log "Removing leftover file that conflicts with CMake: $BUILD_DIR/$collide"
            rm -f "$BUILD_DIR/$collide"
        fi
    done

    local generator_arg
    generator_arg="$(cmake_generator_args || true)"

    local build_type="${CMAKE_BUILD_TYPE:-Release}"
    log "Configuring CMake in $BUILD_DIR"
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
        ${generator_arg:+"$generator_arg"} \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DOC_BUILD_TESTS=OFF \
        -DCHIME_APP_VERSION="$CHIME_APP_VERSION" \
        -DOPENCHIME_OS_VERSION="$OPENCHIME_OS_VERSION" \
        -DCHIME_CONFIG_VERSION="$CHIME_CONFIG_VERSION" \
        -DCHIME_BUILD_ID="$CHIME_BUILD_ID" \
        ${CMAKE_PREFIX_ARGS[@]+"${CMAKE_PREFIX_ARGS[@]}"}
}

build() {
    mkdir -p "$BUILD_DIR" "$BIN_DIR"
    load_versions
    configure_cmake

    log "Building chime and chime-webd..."
    cmake --build "$BUILD_DIR" --parallel --target chime --target chime-webd

    [ -x "$CHIME_BIN" ] || error "CMake build did not produce $CHIME_BIN"
    [ -x "$WEBD_BIN" ] || error "CMake build did not produce $WEBD_BIN"
    log "Built: $CHIME_BIN"
    log "Built: $WEBD_BIN"

    if [ "${LOCAL_CHIME_BUILD_WEBUI:-0}" = "1" ]; then
        build_webui
    fi
}

build_webui() {
    [ -d "$WEBUI_DIR" ] || error "Web UI directory not found: $WEBUI_DIR"
    [ -f "$WEBUI_DIR/package.json" ] || error "Missing $WEBUI_DIR/package.json"
    require_bun

    log "Installing web UI dependencies..."
    (
        cd "$WEBUI_DIR"
        bun install --frozen-lockfile 2>/dev/null || bun install
    )

    log "Building web UI..."
    (
        cd "$WEBUI_DIR"
        bun run build
    )

    log "Built web UI dist: $WEBUI_DIST_DIR"
}

run_webui_dev() {
    [ -d "$WEBUI_DIR" ] || error "Web UI directory not found: $WEBUI_DIR"
    [ -f "$WEBUI_DIR/package.json" ] || error "Missing $WEBUI_DIR/package.json"
    require_bun

    local dev_host="${WEBUI_DEV_HOST:-127.0.0.1}"
    local dev_port="${WEBUI_DEV_PORT:-5173}"

    log "Starting web UI dev server at http://$dev_host:$dev_port"
    (
        cd "$WEBUI_DIR"
        bun install --frozen-lockfile 2>/dev/null || bun install
        bun run dev -- --host "$dev_host" --port "$dev_port"
    )
}

resolve_webui_dist_dir() {
    if [ -n "${CHIME_WEBD_UI_DIST_DIR:-}" ]; then
        echo "$CHIME_WEBD_UI_DIST_DIR"
        return
    fi
    if [ -d "$WEBUI_DIST_DIR" ]; then
        echo "$WEBUI_DIST_DIR"
        return
    fi
    echo ""
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
    if [ "${LOCAL_CHIME_BUILD_WEBUI:-0}" = "1" ]; then
        build_webui
    fi

    local config_path="${2:-${CHIME_CONFIG:-$DEFAULT_CONFIG}}"
    prepare_runtime "$config_path"

    local web_bind="${CHIME_WEBD_BIND_ADDRESS:-127.0.0.1}"
    local web_port="${CHIME_WEBD_PORT:-8443}"
    local ui_dist_dir
    ui_dist_dir="$(resolve_webui_dist_dir)"

    log "Starting chime-webd on https://$web_bind:$web_port"
    log_local_webd_auth_mode
    apply_local_webd_auth_mode
    if [ -n "$ui_dist_dir" ]; then
        log "Serving web UI from: $ui_dist_dir"
    else
        log "Serving embedded web UI fallback (no dist dir configured/found)"
    fi
    CHIME_WEBD_CHIME_CONFIG="$RUNTIME_CHIME_CONFIG" \
    CHIME_WEBD_WPA_SUPPLICANT="$RUNTIME_WPA_CONFIG" \
    CHIME_WEBD_TLS_CERT="${CHIME_WEBD_TLS_CERT:-$RUNTIME_TLS_DIR/cert.pem}" \
    CHIME_WEBD_TLS_KEY="${CHIME_WEBD_TLS_KEY:-$RUNTIME_TLS_DIR/key.pem}" \
    CHIME_WEBD_UI_DIST_DIR="$ui_dist_dir" \
    CHIME_WEBD_BIND_ADDRESS="$web_bind" \
    CHIME_WEBD_PORT="$web_port" \
    CHIME_WEBD_MDNS_ENABLED="${CHIME_WEBD_MDNS_ENABLED:-false}" \
    CHIME_WEBD_NETWORK_RESTART_CMD="${CHIME_WEBD_NETWORK_RESTART_CMD:-true}" \
    CHIME_WEBD_CHIME_RESTART_CMD="${CHIME_WEBD_CHIME_RESTART_CMD:-true}" \
    CHIME_WEBD_AUTH_DIR="${CHIME_WEBD_AUTH_DIR:-$RUNTIME_DIR/auth}" \
    "$WEBD_BIN"
}

run_stack() {
    if [ ! -x "$CHIME_BIN" ] || [ ! -x "$WEBD_BIN" ]; then
        build
    fi
    if [ "${LOCAL_CHIME_BUILD_WEBUI:-0}" = "1" ]; then
        build_webui
    fi

    local config_path="${2:-${CHIME_CONFIG:-$DEFAULT_CONFIG}}"
    prepare_runtime "$config_path"

    local client_id="${CHIME_MQTT_CLIENT_ID:-chime-local-$(hostname -s)}"
    local web_bind="${CHIME_WEBD_BIND_ADDRESS:-127.0.0.1}"
    local web_port="${CHIME_WEBD_PORT:-8443}"
    local ui_dist_dir
    ui_dist_dir="$(resolve_webui_dist_dir)"

    log "Starting local stack"
    log "  chime config: $RUNTIME_CHIME_CONFIG"
    log "  web URL: https://$web_bind:$web_port"
    log_local_webd_auth_mode
    if [ -n "$ui_dist_dir" ]; then
        log "  web UI dist: $ui_dist_dir"
    else
        log "  web UI dist: embedded fallback"
    fi

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
        apply_local_webd_auth_mode
        while true; do
            set +e
            CHIME_WEBD_CHIME_CONFIG="$RUNTIME_CHIME_CONFIG" \
            CHIME_WEBD_WPA_SUPPLICANT="$RUNTIME_WPA_CONFIG" \
            CHIME_WEBD_TLS_CERT="${CHIME_WEBD_TLS_CERT:-$RUNTIME_TLS_DIR/cert.pem}" \
            CHIME_WEBD_TLS_KEY="${CHIME_WEBD_TLS_KEY:-$RUNTIME_TLS_DIR/key.pem}" \
            CHIME_WEBD_UI_DIST_DIR="$ui_dist_dir" \
            CHIME_WEBD_BIND_ADDRESS="$web_bind" \
            CHIME_WEBD_PORT="$web_port" \
            CHIME_WEBD_MDNS_ENABLED="${CHIME_WEBD_MDNS_ENABLED:-false}" \
            CHIME_WEBD_NETWORK_RESTART_CMD="${CHIME_WEBD_NETWORK_RESTART_CMD:-true}" \
            CHIME_WEBD_CHIME_RESTART_CMD="${CHIME_WEBD_CHIME_RESTART_CMD:-true}" \
            CHIME_WEBD_AUTH_DIR="${CHIME_WEBD_AUTH_DIR:-$RUNTIME_DIR/auth}" \
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
Usage: $0 [build|webui-build|webui-dev|run|run-chime|run-webd] [config_path]

build:      Build local chime and chime-webd binaries
webui-build: Build Svelte web UI into webui/dist
webui-dev:   Run Vite dev server for fast UI iteration
run:        Build if needed and run both daemons (mirrors Raspberry layout)
run-chime:  Build if needed and run only chime
run-webd:   Build if needed and run only chime-webd

By default, runtime state is stored under:
  $RUNTIME_DIR

The image overlay copies mqtt_host= (not configured). Local ring playback
needs a broker host in the runtime chime.conf (or a custom config path).
Do not point a clean image at a developer LAN by default.

Local chime-webd is already paired. Sign in with password `openchime-local`
(or \$CHIME_WEBD_BOOTSTRAP_PASSWORD). Auth state lives under:
  $RUNTIME_DIR/auth

Deleting that directory is not enough by itself: the script supplies a
bootstrap password again on the next start. Restore the unpaired pairing
flow with:

  rm -rf $RUNTIME_DIR/auth
  LOCAL_CHIME_UNPAIRED=1 $0 run-webd

That does not reset Wi-Fi or MQTT config. Optional fixed pairing code:

  LOCAL_CHIME_UNPAIRED=1 CHIME_WEBD_PAIRING_CODE=ABCD2345 $0 run-webd

Environment overrides:
  CHIME_MQTT_CLIENT_ID            MQTT client id for local chime run
  CHIME_MQTT_USERNAME             MQTT username override for local chime run
  CHIME_MQTT_PASSWORD             MQTT password override for local chime run
  CHIME_WEBD_BIND_ADDRESS         webd bind address (default: 127.0.0.1)
  CHIME_WEBD_PORT                 webd port (default: 8443)
  CHIME_WEBD_UI_DIST_DIR          static UI dist directory (defaults to webui/dist)
  CHIME_WEBD_NETWORK_RESTART_CMD  executable + argv, no shell (default: true locally)
  CHIME_WEBD_CHIME_RESTART_CMD    executable + argv, no shell (default: true locally)
  CHIME_WEBD_AUTH_DIR             admin verifier directory (default: runtime/auth)
  CHIME_WEBD_BOOTSTRAP_PASSWORD   local admin password (default: openchime-local)
  CHIME_WEBD_PAIRING_CODE         override first-boot pairing code (unpaired only)
  LOCAL_CHIME_UNPAIRED=1          skip the bootstrap password; print a pairing code
  CMAKE_PREFIX_PATH               extra CMake prefix dirs (colon-separated)
  OPENSSL_ROOT_DIR                OpenSSL prefix for CMake discovery
  CMAKE_BUILD_TYPE                CMake build type (default: Release)
  CHIME_BUILD_ID                  compile-time build id (default: unknown)
  LOCAL_CHIME_BUILD_WEBUI=1       build webui/dist during build/run
  WEBUI_DEV_HOST                  Vite dev host (default: 127.0.0.1)
  WEBUI_DEV_PORT                  Vite dev port (default: 5173)
  REFRESH_RUNTIME_FILES=1         reset runtime config/wpa files from source

Examples:
  $0 build
  $0 run
  $0 run /path/to/chime.conf
  CHIME_WEBD_PORT=9443 $0 run
  $0 webui-build
  $0 webui-dev
  $0 run-webd
  LOCAL_CHIME_UNPAIRED=1 $0 run-webd
EOF
}

ACTION="${1:-build}"
case "$ACTION" in
    build)
        build
        ;;
    webui-build)
        build_webui
        ;;
    webui-dev)
        run_webui_dev
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
