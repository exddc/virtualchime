#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

IMAGE_NAME="virtualchime-builder"
VOLUME_NAME="virtualchime-buildroot-cache"
OUTPUT_DIR="$PROJECT_DIR/buildroot/output"
APP_VERSION_FILE="$PROJECT_DIR/chime/VERSION"
CONFIG_FILE="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf"
WEBUI_DIST_DIR="$PROJECT_DIR/webui/dist"
CHIME_BINARY="$OUTPUT_DIR/chime"
WEBD_BINARY="$OUTPUT_DIR/chime-webd"
BUILDROOT_VERSION="${BUILDROOT_VERSION:-2024.02.1}"
BUILD_META_SCRIPT="$SCRIPT_DIR/write_build_meta.sh"

SSH_USER="root"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10"
REMOTE_BINARY_PATH="/usr/local/bin/chime"
REMOTE_WEBD_BINARY_PATH="/usr/local/bin/chime-webd"
REMOTE_BINARY_STAGING_PATH="/usr/local/bin/.chime.new"
REMOTE_WEBD_BINARY_STAGING_PATH="/usr/local/bin/.chime-webd.new"
REMOTE_WEBD_UI_ROOT="/usr/local/share/chime-web-ui"
REMOTE_WEBD_UI_DIST_PATH="$REMOTE_WEBD_UI_ROOT/dist"
REMOTE_CONFIG_PATH="/etc/chime.conf"

log() { echo "[deploy] $*"; }
error() { echo "[deploy] ERROR: $*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage:
  $0 chime <pi-ip-or-hostname> [--binary <path>] [--no-build] [--with-webd]
  $0 config <pi-ip-or-hostname>
  $0 version <pi-ip-or-hostname>

Commands:
  chime    Build (by default) and deploy chime binary
  config   Deploy chime.conf and restart service
  version  Show OS/app/kernel/binary version info on device
EOF
}

usage_chime() {
    cat <<EOF
Usage: $0 chime <pi-ip-or-hostname> [--binary <path>] [--no-build] [--with-webd]

Options:
  --binary <path>  Deploy this binary and skip build
  --no-build       Deploy existing $CHIME_BINARY without rebuilding
  --with-webd      Also deploy existing/built $WEBD_BINARY
  --webd           Alias for --with-webd
EOF
}

require_host() {
    local host="${1:-}"
    [ -n "$host" ] || error "Missing Pi host. Example: $0 chime 192.168.1.100"
}

read_app_version() {
    [ -f "$APP_VERSION_FILE" ] || error "App version file not found at $APP_VERSION_FILE"
    local app_version
    app_version="$(head -n 1 "$APP_VERSION_FILE" | tr -d '[:space:]')"
    [ -n "$app_version" ] || error "App version is empty in $APP_VERSION_FILE"
    printf '%s\n' "$app_version"
}

write_build_metadata() {
    [ -x "$BUILD_META_SCRIPT" ] || error "Build metadata script not found at $BUILD_META_SCRIPT"
    "$BUILD_META_SCRIPT"
}

ensure_build_environment() {
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        log "Docker image '$IMAGE_NAME' not found, running docker_build.sh..."
        "$SCRIPT_DIR/docker_build.sh" "$BUILDROOT_VERSION"
        return
    fi
    if ! docker volume inspect "$VOLUME_NAME" >/dev/null 2>&1; then
        log "Build cache volume '$VOLUME_NAME' not found, running docker_build.sh..."
        "$SCRIPT_DIR/docker_build.sh" "$BUILDROOT_VERSION"
    fi
}

rebuild_chime_binary() {
    mkdir -p "$OUTPUT_DIR"
    write_build_metadata
    ensure_build_environment

    log "Rebuilding chime package..."
    docker run --rm \
        -v "$PROJECT_DIR:/home/builder/virtualchime:ro" \
        -v "$VOLUME_NAME:/home/builder/work" \
        -v "$OUTPUT_DIR:/home/builder/output" \
        "$IMAGE_NAME" \
        bash -c '
set -euo pipefail

step_start() { date +%s; }
step_done() {
    local name="$1"
    local start="$2"
    local end
    end="$(date +%s)"
    echo "[timing] ${name}: $((end - start))s"
}

cd /home/builder/work/buildroot-'"$BUILDROOT_VERSION"'

echo "[rebuild] Syncing chime source..."
mkdir -p /home/builder/chime-src/chime /home/builder/chime-src/common /home/builder/br2-external
sync_start="$(step_start)"
rsync -a --delete /home/builder/virtualchime/chime/ /home/builder/chime-src/chime/
rsync -a --delete /home/builder/virtualchime/common/ /home/builder/chime-src/common/
rsync -a --delete /home/builder/virtualchime/buildroot/ /home/builder/br2-external/
step_done "sync" "$sync_start"

echo "[rebuild] Building chime package..."
build_start="$(step_start)"
make BR2_EXTERNAL=/home/builder/br2-external chime-rebuild
step_done "chime-rebuild" "$build_start"

echo "[rebuild] Copying binary to output..."
export_start="$(step_start)"
CHIME_BINARY_PATH="$(find output/build -type f \( -path "*/chime-*/chime/chime" -o -path "*/chime-*/chime" \) | sort | tail -n 1)"
if [ -z "$CHIME_BINARY_PATH" ] || [ ! -f "$CHIME_BINARY_PATH" ]; then
    echo "[rebuild] ERROR: Could not find built chime binary under output/build/chime-*"
    find output/build -type f | sed "s#^#[rebuild] found: #"
    exit 1
fi
echo "[rebuild] Using binary: $CHIME_BINARY_PATH"
cp "$CHIME_BINARY_PATH" /home/builder/output/chime

WEBD_BINARY_PATH="$(find output/build -type f -path "*/chime-*/chime/chime-webd" | sort | tail -n 1)"
if [ -z "$WEBD_BINARY_PATH" ] || [ ! -f "$WEBD_BINARY_PATH" ]; then
    echo "[rebuild] ERROR: Could not find built chime-webd binary under output/build/chime-*"
    find output/build -type f | sed "s#^#[rebuild] found: #"
    exit 1
fi
echo "[rebuild] Using web binary: $WEBD_BINARY_PATH"
cp "$WEBD_BINARY_PATH" /home/builder/output/chime-webd
step_done "export" "$export_start"
'

    [ -f "$CHIME_BINARY" ] || error "Rebuild completed but binary was not found at $CHIME_BINARY"
    [ -f "$WEBD_BINARY" ] || error "Rebuild completed but web binary was not found at $WEBD_BINARY"
}

deploy_binary_to_pi() {
    local host="$1"
    local binary_path="$2"
    local app_version="$3"

    [ -f "$binary_path" ] || error "Binary not found at $binary_path"

    log "Deploying chime binary to $host..."
    log "Stopping chime service..."
    ssh $SSH_OPTS "$SSH_USER@$host" "/etc/init.d/S99chime stop" || true

    log "Copying binary (atomic replace)..."
    scp -O $SSH_OPTS "$binary_path" "$SSH_USER@$host:$REMOTE_BINARY_STAGING_PATH"
    ssh $SSH_OPTS "$SSH_USER@$host" "chmod +x $REMOTE_BINARY_STAGING_PATH && mv -f $REMOTE_BINARY_STAGING_PATH $REMOTE_BINARY_PATH"
    ssh $SSH_OPTS "$SSH_USER@$host" "printf '%s\n' '$app_version' > /etc/chime-app-version"

    log "Starting chime service..."
    ssh $SSH_OPTS "$SSH_USER@$host" "/etc/init.d/S99chime start"

    sleep 2
    log "Service status:"
    ssh $SSH_OPTS "$SSH_USER@$host" "/etc/init.d/S99chime status"

    log "Installed version:"
    ssh $SSH_OPTS "$SSH_USER@$host" "$REMOTE_BINARY_PATH --version || true"
}

deploy_webd_to_pi() {
    local host="$1"
    local binary_path="$2"

    [ -f "$binary_path" ] || error "Web binary not found at $binary_path"

    log "Deploying chime-webd binary to $host..."
    log "Stopping web service..."
    ssh $SSH_OPTS "$SSH_USER@$host" "/etc/init.d/S45webd stop" || true

    log "Copying web binary (atomic replace)..."
    scp -O $SSH_OPTS "$binary_path" "$SSH_USER@$host:$REMOTE_WEBD_BINARY_STAGING_PATH"
    ssh $SSH_OPTS "$SSH_USER@$host" "chmod +x $REMOTE_WEBD_BINARY_STAGING_PATH && mv -f $REMOTE_WEBD_BINARY_STAGING_PATH $REMOTE_WEBD_BINARY_PATH"

    if [ -d "$WEBUI_DIST_DIR" ]; then
        log "Deploying web UI assets from $WEBUI_DIST_DIR..."
        ssh $SSH_OPTS "$SSH_USER@$host" "mkdir -p $REMOTE_WEBD_UI_ROOT"
        scp -O -r $SSH_OPTS "$WEBUI_DIST_DIR" "$SSH_USER@$host:$REMOTE_WEBD_UI_ROOT"
        ssh $SSH_OPTS "$SSH_USER@$host" "ls -la $REMOTE_WEBD_UI_DIST_PATH >/dev/null"
    else
        log "WARNING: $WEBUI_DIST_DIR not found; webd will serve fallback page."
        log "Build UI assets first with: ./scripts/local_chime.sh webui-build"
    fi

    log "Starting web service..."
    ssh $SSH_OPTS "$SSH_USER@$host" "/etc/init.d/S45webd start"

    sleep 2
    log "Web service status:"
    ssh $SSH_OPTS "$SSH_USER@$host" "/etc/init.d/S45webd status"
}

cmd_chime() {
    if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
        usage_chime
        exit 0
    fi

    local host="${1:-}"
    shift || true
    require_host "$host"

    local no_build=0
    local with_webd=0
    local binary_override=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --no-build)
                no_build=1
                shift
                ;;
            --with-webd|--webd)
                with_webd=1
                shift
                ;;
            --binary)
                [ $# -ge 2 ] || error "--binary requires a path"
                binary_override="$2"
                shift 2
                ;;
            -h|--help)
                usage_chime
                exit 0
                ;;
            *)
                error "Unknown option for chime: $1"
                ;;
        esac
    done

    if [ -n "$binary_override" ] && [ "$no_build" -eq 1 ]; then
        error "Use either --binary or --no-build, not both"
    fi

    local app_version
    app_version="$(read_app_version)"

    local binary_path
    if [ -n "$binary_override" ]; then
        binary_path="$binary_override"
    elif [ "$no_build" -eq 1 ]; then
        binary_path="$CHIME_BINARY"
    else
        rebuild_chime_binary
        binary_path="$CHIME_BINARY"
    fi

    if [ "$with_webd" -eq 1 ] && [ ! -f "$WEBD_BINARY" ] && { [ -n "$binary_override" ] || [ "$no_build" -eq 1 ]; }; then
        error "Web binary not found at $WEBD_BINARY. Run without --binary/--no-build once to build it, then retry."
    fi

    deploy_binary_to_pi "$host" "$binary_path" "$app_version"
    if [ "$with_webd" -eq 1 ]; then
        deploy_webd_to_pi "$host" "$WEBD_BINARY"
    fi
}

cmd_config() {
    local host="${1:-}"
    require_host "$host"

    [ -f "$CONFIG_FILE" ] || error "Config file not found at $CONFIG_FILE"

    log "Deploying chime.conf to $host..."
    scp -O $SSH_OPTS "$CONFIG_FILE" "$SSH_USER@$host:$REMOTE_CONFIG_PATH"

    log "Restarting chime service..."
    ssh $SSH_OPTS "$SSH_USER@$host" "/etc/init.d/S99chime restart"

    sleep 2
    log "Service status:"
    ssh $SSH_OPTS "$SSH_USER@$host" "/etc/init.d/S99chime status"
}

cmd_version() {
    local host="${1:-}"
    require_host "$host"

    ssh $SSH_OPTS "$SSH_USER@$host" '
echo "=== /etc/virtualchime-release ==="
if [ -f /etc/virtualchime-release ]; then
    cat /etc/virtualchime-release
else
    echo "missing (flash a newer image that writes this file)"
fi

echo
echo "=== /etc/chime-app-version ==="
if [ -f /etc/chime-app-version ]; then
    cat /etc/chime-app-version
else
    echo "missing"
fi

echo
echo "=== kernel ==="
uname -r

echo
echo "=== chime binary ==="
if [ -x /usr/local/bin/chime ]; then
    if command -v timeout >/dev/null 2>&1; then
        if timeout 3 /usr/local/bin/chime --version; then
            :
        else
            rc=$?
            if [ "$rc" -eq 124 ]; then
                echo "chime --version timed out (binary likely does not support --version yet)"
            else
                echo "chime --version exited with code $rc"
            fi
        fi
    else
        /usr/local/bin/chime --version || true
    fi
else
    echo "missing /usr/local/bin/chime"
fi
'
}

main() {
    local cmd="${1:-}"
    case "$cmd" in
        chime)
            shift
            cmd_chime "$@"
            ;;
        config)
            shift
            cmd_config "$@"
            ;;
        version)
            shift
            cmd_version "$@"
            ;;
        -h|--help|"")
            usage
            ;;
        *)
            error "Unknown command: $cmd"
            ;;
    esac
}

main "$@"
