#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

DEFAULT_BUILDROOT_VERSION="2024.02.1"
BUILDROOT_VERSION="${BUILDROOT_VERSION:-$DEFAULT_BUILDROOT_VERSION}"
IMAGE_NAME="${IMAGE_NAME:-openchime-builder}"
VOLUME_NAME="${VOLUME_NAME:-openchime-buildroot-cache}"
SKIP_IMAGE_BUILD="${SKIP_IMAGE_BUILD:-0}"
IN_CONTAINER=0

log() { echo "[buildroot-chime] $*"; }
error() { echo "[buildroot-chime] ERROR: $*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: $0 [--in-container] [--buildroot-version <version>]

Builds the Buildroot chime and chime-web-ui packages, then checks the
target install layout.

Host options:
  --buildroot-version <v>  Buildroot release (default: $DEFAULT_BUILDROOT_VERSION)
  --in-container           Internal: run the package build inside the builder image
  -h, --help               Show this help text

Environment:
  BUILDROOT_WORK_DIR       Host directory mounted at /home/builder/work
  JOBS=<n>                 Make jobs inside the container (default: nproc)
  SKIP_IMAGE_BUILD=1       Reuse existing '$IMAGE_NAME' image
  IMAGE_NAME               Builder image name (default: openchime-builder)
  VOLUME_NAME              Docker volume used when BUILDROOT_WORK_DIR is unset
EOF
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --in-container)
                IN_CONTAINER=1
                shift
                ;;
            --buildroot-version)
                [ $# -ge 2 ] || error "--buildroot-version requires a value"
                BUILDROOT_VERSION="$2"
                shift 2
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                error "Unknown argument: $1"
                ;;
        esac
    done
}

require_file() {
    local path="$1"
    [ -e "$path" ] || error "missing $path"
}

run_host() {
    command -v docker >/dev/null 2>&1 || error "docker is required"
    [ -x "$SCRIPT_DIR/write_build_meta.sh" ] || error "missing $SCRIPT_DIR/write_build_meta.sh"
    "$SCRIPT_DIR/write_build_meta.sh"

    case "$SKIP_IMAGE_BUILD" in
        0|1) ;;
        *) error "Invalid SKIP_IMAGE_BUILD='$SKIP_IMAGE_BUILD' (expected 0 or 1)" ;;
    esac
    if [ "$SKIP_IMAGE_BUILD" = "1" ]; then
        docker image inspect "$IMAGE_NAME" >/dev/null 2>&1 || \
            error "Docker image '$IMAGE_NAME' not found while SKIP_IMAGE_BUILD=1"
        log "Skipping Docker image build (SKIP_IMAGE_BUILD=1)"
    else
        log "Building Docker image '$IMAGE_NAME'"
        docker build -t "$IMAGE_NAME" "$PROJECT_DIR/buildroot"
    fi

    local work_mount
    if [ -n "${BUILDROOT_WORK_DIR:-}" ]; then
        mkdir -p "$BUILDROOT_WORK_DIR"
        work_mount="$BUILDROOT_WORK_DIR:/home/builder/work"
        log "Using work directory $BUILDROOT_WORK_DIR"
    else
        docker volume create "$VOLUME_NAME" >/dev/null
        work_mount="$VOLUME_NAME:/home/builder/work"
        log "Using Docker volume $VOLUME_NAME"
    fi

    local docker_args=(
        run --rm
        -v "$PROJECT_DIR:/home/builder/openchime:ro"
        -v "$work_mount"
        -e "BUILDROOT_VERSION=$BUILDROOT_VERSION"
    )
    if [ -n "${JOBS:-}" ]; then
        docker_args+=(-e "JOBS=$JOBS")
    fi

    docker "${docker_args[@]}" \
        "$IMAGE_NAME" \
        bash /home/builder/openchime/scripts/buildroot_chime.sh --in-container
}

run_container() {
    sudo chown -R builder:builder /home/builder/work
    cd /home/builder/work

    if [ ! -d "buildroot-$BUILDROOT_VERSION" ]; then
        log "Downloading Buildroot $BUILDROOT_VERSION"
        wget -q "https://buildroot.org/downloads/buildroot-$BUILDROOT_VERSION.tar.gz"
        tar xf "buildroot-$BUILDROOT_VERSION.tar.gz"
        rm "buildroot-$BUILDROOT_VERSION.tar.gz"
    else
        log "Using cached Buildroot $BUILDROOT_VERSION"
    fi

    local jobs="${JOBS:-$(nproc)}"
    if ! [[ "$jobs" =~ ^[0-9]+$ ]] || [ "$jobs" -le 0 ]; then
        error "invalid JOBS value: $jobs"
    fi
    log "Using make jobs: $jobs"

    mkdir -p /home/builder/br2-external /home/builder/chime-src /home/builder/webui-src
    rsync -a --delete --exclude /output/ /home/builder/openchime/buildroot/ /home/builder/br2-external/
    bash /home/builder/openchime/scripts/sync_chime_src.sh \
        /home/builder/openchime /home/builder/chime-src
    bash /home/builder/openchime/scripts/sync_webui_src.sh \
        /home/builder/openchime /home/builder/webui-src
    require_file /home/builder/br2-external/build_meta.env
    require_file /home/builder/chime-src/CMakeLists.txt
    require_file /home/builder/webui-src/package.json
    require_file /home/builder/webui-src/bun.lock
    require_file /home/builder/webui-src/.source-id
    if [ -e /home/builder/webui-src/dist ] || [ -e /home/builder/webui-src/node_modules ]; then
        error "staged webui-src must not contain dist/ or node_modules/"
    fi

    cd "buildroot-$BUILDROOT_VERSION"
    make BR2_EXTERNAL=/home/builder/br2-external openchime_rpi0w_defconfig

    local toolchain_file="output/host/share/buildroot/toolchainfile.cmake"
    if [ -f "$toolchain_file" ]; then
        log "Reconfiguring chime with existing Buildroot toolchain"
        make BR2_EXTERNAL=/home/builder/br2-external -j"$jobs" chime-reconfigure
    else
        log "Building Buildroot toolchain, chime dependencies, and chime package"
        make BR2_EXTERNAL=/home/builder/br2-external -j"$jobs" chime
    fi

    require_file "$toolchain_file"
    log "Used Buildroot toolchain file $toolchain_file"

    local target_dir="output/target"
    local chime_bin="$target_dir/usr/local/bin/chime"
    local webd_bin="$target_dir/usr/local/bin/chime-webd"
    require_file "$chime_bin"
    require_file "$webd_bin"
    [ -x "$chime_bin" ] || error "chime is not executable: $chime_bin"
    [ -x "$webd_bin" ] || error "chime-webd is not executable: $webd_bin"
    require_file "$target_dir/etc/chime-app-version"
    require_file "$target_dir/etc/chime-build-id"
    [ -s "$target_dir/etc/chime-app-version" ] || error "empty $target_dir/etc/chime-app-version"
    [ -s "$target_dir/etc/chime-build-id" ] || error "empty $target_dir/etc/chime-build-id"

    local expected_build_id installed_build_id
    expected_build_id="$(sed -n 's/^CHIME_BUILD_ID=//p' /home/builder/br2-external/build_meta.env | head -n 1 | tr -d '[:space:]')"
    [ -n "$expected_build_id" ] || error "missing CHIME_BUILD_ID in build_meta.env"
    installed_build_id="$(tr -d '[:space:]' < "$target_dir/etc/chime-build-id")"
    [ "$installed_build_id" = "$expected_build_id" ] || \
        error "installed CHIME_BUILD_ID '$installed_build_id' != '$expected_build_id'"
    grep -aF "$expected_build_id" "$chime_bin" >/dev/null || \
        error "$chime_bin does not contain compile-time CHIME_BUILD_ID=$expected_build_id"
    grep -aF "$expected_build_id" "$webd_bin" >/dev/null || \
        error "$webd_bin does not contain compile-time CHIME_BUILD_ID=$expected_build_id"

    local chime_file webd_file
    chime_file="$(file -b "$chime_bin")"
    webd_file="$(file -b "$webd_bin")"
    log "chime: $chime_file"
    log "chime-webd: $webd_file"
    printf '%s\n' "$chime_file" | grep -qi 'ARM' || \
        error "chime was not cross-compiled for ARM: $chime_file"
    printf '%s\n' "$webd_file" | grep -qi 'ARM' || \
        error "chime-webd was not cross-compiled for ARM: $webd_file"

    log "Rebuilding chime-web-ui from staged webui sources"
    make BR2_EXTERNAL=/home/builder/br2-external chime-web-ui-dirclean
    make BR2_EXTERNAL=/home/builder/br2-external -j"$jobs" chime-web-ui
    require_file "$target_dir/usr/local/share/chime-web-ui/dist/index.html"
    log "Buildroot chime-web-ui installed /usr/local/share/chime-web-ui/dist"

    log "Buildroot chime package installed /usr/local/bin/chime and /usr/local/bin/chime-webd"
}

parse_args "$@"
if [ "$IN_CONTAINER" = "1" ]; then
    run_container
else
    run_host
fi
