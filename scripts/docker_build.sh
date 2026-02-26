#!/usr/bin/env bash
# docker_build.sh - Build the Virtual Chime Buildroot image using Docker
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DEFAULT_BUILDROOT_VERSION="2024.02.1"
BUILDROOT_VERSION="$DEFAULT_BUILDROOT_VERSION"
BUILDROOT_VERSION_SET=0
JOBS="${JOBS:-}"
SKIP_IMAGE_BUILD="${SKIP_IMAGE_BUILD:-0}"
DOCKER_TTY="${DOCKER_TTY:-0}"
CLEAR_DOCKER_CACHE=0

IMAGE_NAME="virtualchime-builder"
VOLUME_NAME="virtualchime-buildroot-cache"
OUTPUT_DIR="$PROJECT_DIR/buildroot/output"
IMAGE_PATH="$OUTPUT_DIR/sdcard.img"
VERSION_FILE="$PROJECT_DIR/buildroot/version.env"
APP_VERSION_FILE="$PROJECT_DIR/chime/VERSION"
BUILD_META_SCRIPT="$SCRIPT_DIR/write_build_meta.sh"

log() { echo "[docker-build] $*"; }
error() { echo "[docker-build] ERROR: $*" >&2; exit 1; }
now() { date +%s; }
elapsed() { local start="$1" end; end="$(now)"; echo $((end - start)); }
log_timing() { local step="$1"; local secs="$2"; log "timing[$step]: ${secs}s"; }

usage() {
    cat <<EOF
Usage: $0 [buildroot-version] [--clear-docker-cache]

Options:
  --clear-docker-cache  Remove Docker volume '$VOLUME_NAME' before building
  -h, --help            Show this help text

Environment:
  JOBS=<n>              Override parallel make jobs inside the container
  SKIP_IMAGE_BUILD=1    Reuse existing '$IMAGE_NAME' image without rebuilding it
  DOCKER_TTY=1          Allocate TTY for docker run (default: 0)
EOF
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --clear-docker-cache)
                CLEAR_DOCKER_CACHE=1
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            --*)
                error "Unknown option: $1"
                ;;
            *)
                if [ "$BUILDROOT_VERSION_SET" -eq 1 ]; then
                    error "Unexpected argument: $1"
                fi
                BUILDROOT_VERSION="$1"
                BUILDROOT_VERSION_SET=1
                shift
                ;;
        esac
    done
}

check_secrets() {
    local wifi_conf="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/etc/wpa_supplicant/wpa_supplicant.conf"
    local ssh_keys="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/root/.ssh/authorized_keys"
    [ -f "$wifi_conf" ] || error "Wi-Fi config not found! Copy wpa_supplicant.conf.example and edit it."
    [ -f "$ssh_keys" ] || error "SSH authorized_keys not found! Run: cat ~/.ssh/id_ed25519.pub > $ssh_keys"
    log "Secret files found"
}

validate_inputs() {
    case "$SKIP_IMAGE_BUILD" in
        0|1) ;;
        *) error "Invalid SKIP_IMAGE_BUILD='$SKIP_IMAGE_BUILD' (expected 0 or 1)" ;;
    esac
    case "$DOCKER_TTY" in
        0|1) ;;
        *) error "Invalid DOCKER_TTY='$DOCKER_TTY' (expected 0 or 1)" ;;
    esac
    if [ -n "$JOBS" ]; then
        [[ "$JOBS" =~ ^[0-9]+$ ]] || error "Invalid JOBS='$JOBS' (expected positive integer)"
        [ "$JOBS" -gt 0 ] || error "Invalid JOBS='$JOBS' (must be > 0)"
    fi
}

build_or_reuse_docker_image() {
    if [ "$SKIP_IMAGE_BUILD" = "1" ]; then
        docker image inspect "$IMAGE_NAME" >/dev/null 2>&1 || \
            error "Docker image '$IMAGE_NAME' not found while SKIP_IMAGE_BUILD=1"
        log "Skipping Docker image build (SKIP_IMAGE_BUILD=1)"
        return
    fi
    local start
    start="$(now)"
    log "Building Docker image..."
    docker build -t "$IMAGE_NAME" "$PROJECT_DIR/buildroot"
    log_timing "docker image" "$(elapsed "$start")"
}

clear_docker_cache_if_requested() {
    if [ "$CLEAR_DOCKER_CACHE" != "1" ]; then
        return
    fi

    log "Clearing Docker cache volume '$VOLUME_NAME'..."
    if docker volume rm "$VOLUME_NAME" >/dev/null 2>&1; then
        log "Removed cache volume '$VOLUME_NAME'"
    else
        log "Cache volume '$VOLUME_NAME' not found or could not be removed; continuing"
    fi
}

print_container_resources() {
    local stats cpus mem_kib swap_kib mem_mib swap_mib
    stats="$(docker run --rm "$IMAGE_NAME" bash -lc 'n=$(nproc); mem=$(awk "/MemTotal/ {print \$2}" /proc/meminfo); swap=$(awk "/SwapTotal/ {print \$2}" /proc/meminfo); echo "$n $mem $swap"')"
    read -r cpus mem_kib swap_kib <<< "$stats"
    mem_mib=$((mem_kib / 1024))
    swap_mib=$((swap_kib / 1024))
    log "Container resources: cpus=$cpus mem=${mem_mib}MiB swap=${swap_mib}MiB"
    if [ "$cpus" -le 2 ]; then
        log "WARNING: Docker has <=2 CPUs. Increase to 6-8 CPUs for faster builds."
    fi
    if [ "$mem_mib" -lt 4096 ]; then
        log "WARNING: Docker has <4GiB RAM. Increase to 8-12GiB for faster builds."
    fi
}

check_versions() {
    [ -f "$VERSION_FILE" ] || error "Version file not found at $VERSION_FILE"
    [ -f "$APP_VERSION_FILE" ] || error "App version file not found at $APP_VERSION_FILE"
    # shellcheck disable=SC1090
    . "$VERSION_FILE"
    local app_version
    app_version="$(head -n 1 "$APP_VERSION_FILE" | tr -d '[:space:]')"
    [ -n "$app_version" ] || error "Missing app version in $APP_VERSION_FILE"
    [[ "$app_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || \
        error "Invalid app version '$app_version' in $APP_VERSION_FILE (expected SemVer like 1.2.3)"
    [ -n "${VIRTUALCHIME_OS_VERSION:-}" ] || error "Missing VIRTUALCHIME_OS_VERSION in $VERSION_FILE"
    [ -n "${CHIME_CONFIG_VERSION:-}" ] || error "Missing CHIME_CONFIG_VERSION in $VERSION_FILE"
    log "Versions: os=$VIRTUALCHIME_OS_VERSION chime=$app_version config=$CHIME_CONFIG_VERSION"
}

write_build_metadata() {
    [ -x "$BUILD_META_SCRIPT" ] || error "Build metadata script not found at $BUILD_META_SCRIPT"
    "$BUILD_META_SCRIPT"
}

run_build() {
    local docker_tty_opt=""
    if [ "$DOCKER_TTY" = "1" ]; then
        docker_tty_opt="-t"
        log "Docker TTY enabled for build container (DOCKER_TTY=1)"
    else
        log "Docker TTY disabled for build container (DOCKER_TTY=0)"
    fi

    log "Starting Buildroot build (version $BUILDROOT_VERSION)..."
    mkdir -p "$OUTPUT_DIR"
    docker volume create "$VOLUME_NAME" >/dev/null 2>&1 || true

    docker run --rm ${docker_tty_opt:+$docker_tty_opt} \
        -v "$PROJECT_DIR:/home/builder/virtualchime:ro" \
        -v "$VOLUME_NAME:/home/builder/work" \
        -e BUILDROOT_VERSION="$BUILDROOT_VERSION" \
        -e JOBS="$JOBS" \
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

# Fix volume permissions
sudo chown -R builder:builder /home/builder/work

cd /home/builder/work

if [ ! -d "buildroot-$BUILDROOT_VERSION" ]; then
    echo "[build] Downloading Buildroot $BUILDROOT_VERSION..."
    wget -q --show-progress "https://buildroot.org/downloads/buildroot-$BUILDROOT_VERSION.tar.gz"
    tar xf "buildroot-$BUILDROOT_VERSION.tar.gz"
    rm "buildroot-$BUILDROOT_VERSION.tar.gz"
else
    echo "[build] Using cached Buildroot"
fi

JOBS="${JOBS:-$(nproc)}"
if ! [[ "$JOBS" =~ ^[0-9]+$ ]] || [ "$JOBS" -le 0 ]; then
    echo "[build] ERROR: invalid JOBS value: $JOBS"
    exit 1
fi
echo "[build] Using make jobs: $JOBS"

echo "[build] Setting up external tree and chime source..."
mkdir -p /home/builder/br2-external /home/builder/chime-src/chime /home/builder/chime-src/common
sync_start="$(step_start)"
rsync -a --delete /home/builder/virtualchime/buildroot/ /home/builder/br2-external/
rsync -a --delete /home/builder/virtualchime/chime/ /home/builder/chime-src/chime/
rsync -a --delete /home/builder/virtualchime/common/ /home/builder/chime-src/common/
step_done "sync" "$sync_start"

cd "buildroot-$BUILDROOT_VERSION"

echo "[build] Configuring for Raspberry Pi Zero W..."
defconfig_start="$(step_start)"
make BR2_EXTERNAL=/home/builder/br2-external virtualchime_rpi0w_defconfig
step_done "defconfig" "$defconfig_start"

echo "[build] Building (this takes 30-90 min)..."
build_start="$(step_start)"
make BR2_EXTERNAL=/home/builder/br2-external -j"$JOBS"
step_done "make" "$build_start"

if [ -f "output/images/sdcard.img" ]; then
    cp output/images/sdcard.img /home/builder/work/
    echo "[build] SUCCESS! Image ready."
else
    echo "[build] Build finished but sdcard.img not found"
    ls -la output/images/ 2>/dev/null || true
fi
'
    
    local export_start
    export_start="$(now)"
    log "Extracting image from volume..."
    docker run --rm \
        -v "$VOLUME_NAME:/work:ro" \
        -v "$OUTPUT_DIR:/out" \
        alpine sh -c "if [ -f /work/sdcard.img ]; then cp /work/sdcard.img /out/ && echo 'Done'; else echo 'ERROR: Image not found in Docker volume' >&2; exit 1; fi"
    log_timing "export" "$(elapsed "$export_start")"
}

print_success_summary() {
    cat <<EOF

[docker-build] SUCCESS
Image: $IMAGE_PATH

Next:
  1) Flash SD card:
     sudo dd if=$IMAGE_PATH of=/dev/rdiskN bs=4m status=progress
  2) Eject and insert card into the Pi
  3) (Optional) Clean Docker cache:
     docker volume rm $VOLUME_NAME
EOF
}

parse_args "$@"
log "Virtual Chime Buildroot Docker Build"
validate_inputs
check_secrets
check_versions
write_build_metadata
build_or_reuse_docker_image
clear_docker_cache_if_requested
print_container_resources
run_build

[ -f "$IMAGE_PATH" ] || error "Build finished but image is missing at $IMAGE_PATH"
print_success_summary
