#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/chime/build-ci}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
SCOPE="${CHIME_CI_SCOPE:-all}"
BASE_REF="${CHIME_CI_BASE_REF:-}"
FIX_FORMAT=0
SKIP_FORMAT=0
SKIP_TIDY=0
SKIP_BUILD=0

log() {
  echo "[chime-ci] $*"
}

error() {
  echo "[chime-ci] ERROR: $*" >&2
  exit 1
}

usage() {
  cat <<USAGE
Usage: $(basename "$0") [options]

Runs local checks equivalent to the chime CI pipeline.

Options:
  --scope <all|changed>     File scope for format/tidy checks (default: all)
  --base-ref <git-ref>      Base ref for --scope changed (or CHIME_CI_BASE_REF)
  --build-dir <path>        Build directory (default: chime/build-ci)
  --build-type <type>       CMake build type (default: Release)
  --fix-format              Apply clang-format in place instead of check-only
  --skip-format             Skip clang-format
  --skip-tidy               Skip clang-tidy
  --skip-build              Skip build step
  -h, --help                Show this help text

Examples:
  ./scripts/chime_ci.sh
  ./scripts/chime_ci.sh --fix-format
  CHIME_CI_SCOPE=changed CHIME_CI_BASE_REF=origin/main ./scripts/chime_ci.sh
USAGE
}

parse_args() {
  while [ $# -gt 0 ]; do
    case "$1" in
      --scope)
        [ $# -ge 2 ] || error "--scope requires a value"
        SCOPE="$2"
        shift 2
        ;;
      --base-ref)
        [ $# -ge 2 ] || error "--base-ref requires a value"
        BASE_REF="$2"
        shift 2
        ;;
      --build-dir)
        [ $# -ge 2 ] || error "--build-dir requires a value"
        BUILD_DIR="$2"
        shift 2
        ;;
      --build-type)
        [ $# -ge 2 ] || error "--build-type requires a value"
        BUILD_TYPE="$2"
        shift 2
        ;;
      --fix-format)
        FIX_FORMAT=1
        shift
        ;;
      --skip-format)
        SKIP_FORMAT=1
        shift
        ;;
      --skip-tidy)
        SKIP_TIDY=1
        shift
        ;;
      --skip-build)
        SKIP_BUILD=1
        shift
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

  case "$SCOPE" in
    all|changed) ;;
    *) error "Invalid scope '$SCOPE' (expected all or changed)" ;;
  esac

  if [ "$SCOPE" = "changed" ] && [ -z "$BASE_REF" ]; then
    error "changed scope requires --base-ref or CHIME_CI_BASE_REF"
  fi
}

require_tool() {
  local tool="$1"
  command -v "$tool" >/dev/null 2>&1 || error "Required tool not found: $tool"
}

is_chime_format_file() {
  local path="$1"
  case "$path" in
    chime/*) ;;
    *) return 1 ;;
  esac
  case "$path" in
    *.c|*.cc|*.cpp|*.h|*.hpp) return 0 ;;
    *) return 1 ;;
  esac
}

is_chime_tidy_file() {
  local path="$1"
  case "$path" in
    chime/src/*) ;;
    *) return 1 ;;
  esac
  case "$path" in
    *.cc|*.cpp) return 0 ;;
    *) return 1 ;;
  esac
}

collect_candidates() {
  if [ "$SCOPE" = "all" ]; then
    git ls-files -z -- chime
    return
  fi

  git rev-parse --verify "${BASE_REF}^{commit}" >/dev/null 2>&1 || \
    error "Base ref does not resolve to a commit: $BASE_REF"

  local merge_base
  merge_base="$(git merge-base "$BASE_REF" HEAD)"
  [ -n "$merge_base" ] || error "Failed to find merge-base for $BASE_REF and HEAD"

  log "Using merge-base $merge_base (base ref: $BASE_REF)"
  git diff --name-only --diff-filter=ACMR -z "$merge_base" HEAD -- chime
}

collect_format_files() {
  local file
  while IFS= read -r -d '' file; do
    if is_chime_format_file "$file" && [ -f "$PROJECT_DIR/$file" ]; then
      printf '%s\0' "$file"
    fi
  done < <(collect_candidates)
}

collect_tidy_files() {
  local file
  while IFS= read -r -d '' file; do
    if is_chime_tidy_file "$file" && [ -f "$PROJECT_DIR/$file" ]; then
      printf '%s\0' "$file"
    fi
  done < <(collect_candidates)
}

run_clang_format() {
  [ "$SKIP_FORMAT" = "1" ] && {
    log "Skipping clang-format"
    return
  }

  require_tool clang-format

  local files=()
  while IFS= read -r -d '' file; do
    files+=("$file")
  done < <(collect_format_files)

  if [ "${#files[@]}" -eq 0 ]; then
    log "No chime C/C++ files found for clang-format"
    return
  fi

  clang-format --version
  log "clang-format files: ${#files[@]}"

  if [ "$FIX_FORMAT" = "1" ]; then
    clang-format -i "${files[@]}"
    log "Applied clang-format to ${#files[@]} files"
  else
    clang-format -n -Werror "${files[@]}"
    log "clang-format check passed"
  fi
}

configure_cmake() {
  require_tool cmake
  require_tool ninja

  cmake --version
  cmake -S "$PROJECT_DIR/chime" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
}

run_clang_tidy() {
  [ "$SKIP_TIDY" = "1" ] && {
    log "Skipping clang-tidy"
    return
  }

  require_tool clang-tidy

  local files=()
  while IFS= read -r -d '' file; do
    files+=("$file")
  done < <(collect_tidy_files)

  if [ "${#files[@]}" -eq 0 ]; then
    log "No chime C++ sources found for clang-tidy"
    return
  fi

  clang-tidy --version
  log "clang-tidy files: ${#files[@]}"
  clang-tidy -p "$BUILD_DIR" "${files[@]}"
  log "clang-tidy passed"
}

run_build() {
  [ "$SKIP_BUILD" = "1" ] && {
    log "Skipping build"
    return
  }

  cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"
  log "Build passed"
}

main() {
  parse_args "$@"

  git -C "$PROJECT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1 || \
    error "Must run inside the repository"

  run_clang_format
  if [ "$SKIP_TIDY" = "1" ] && [ "$SKIP_BUILD" = "1" ]; then
    log "Skipping CMake configure (no tidy/build requested)"
  else
    configure_cmake
  fi
  run_clang_tidy
  run_build

  log "All requested checks passed"
}

main "$@"
