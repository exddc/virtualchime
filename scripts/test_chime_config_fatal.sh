#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
POLICY="$PROJECT_DIR/buildroot/board/common/rootfs_overlay/usr/local/sbin/chime-supervisor-policy.sh"
MIGRATE_BIN="${1:-}"

fail() {
    echo "[test-chime-config-fatal] ERROR: $*" >&2
    exit 1
}

[ -f "$POLICY" ] || fail "missing $POLICY"
# shellcheck disable=SC1090,SC2154
. "$POLICY"

tmp="$(mktemp -d)"
trap 'rm -rf "${tmp:-}"' EXIT
export CHIME_CONFIG_FATAL_STAMP="$tmp/config.fatal"

count_starts() {
    grep -c "Starting daemon" "$1" || true
}

test_future_schema_migrate() {
    [ -n "$MIGRATE_BIN" ] || return 0
    [ -x "$MIGRATE_BIN" ] || fail "chime-migrate is not executable: $MIGRATE_BIN"
    local conf="$tmp/future.conf"
    printf 'schema_version=9999\nmqtt_host=broker\n' > "$conf"
    local rc=0
    "$MIGRATE_BIN" "$conf" >/dev/null 2>&1 || rc=$?
    [ "$rc" -eq "$CHIME_CONFIG_FATAL_EXIT" ] || fail "chime-migrate future schema rc=$rc, want $CHIME_CONFIG_FATAL_EXIT"
    grep -q 'schema_version=9999' "$conf" || fail "chime-migrate rewrote a future schema file"
    [ ! -f "$conf.bak" ] || fail "chime-migrate wrote a backup for a future schema file"
}

test_supervisor_stops_on_fatal_exit() {
    local log="$tmp/web.log"
    local fake="$tmp/fake-webd"
    : > "$log"
    cat > "$fake" <<'EOF'
#!/bin/sh
echo "fake-webd start" >&2
exit 78
EOF
    chmod 755 "$fake"

    (
        set +e
        while true; do
            echo "Starting daemon" >> "$log"
            "$fake" >> "$log" 2>&1
            code=$?
            if ! chime_supervisor_should_restart "$code"; then
                echo "exited $code (permanent config error); not restarting" >> "$log"
                break
            fi
            echo "retry scheduled" >> "$log"
            sleep 1
        done
    ) &
    local spid=$!
    sleep 2
    if kill -0 "$spid" 2>/dev/null; then
        kill "$spid" 2>/dev/null || true
        wait "$spid" 2>/dev/null || true
        fail "supervisor kept running after a future-schema exit"
    fi
    wait "$spid" 2>/dev/null || true
    local starts
    starts="$(count_starts "$log")"
    [ "$starts" -eq 1 ] || fail "expected one supervisor start, got $starts: $(cat "$log")"
    grep -q "not restarting" "$log" || fail "missing permanent-failure log line"
    if grep -q "retry scheduled" "$log"; then
        fail "supervisor restarted after a permanent config error: $(cat "$log")"
    fi
}

test_stamp_prevents_start() {
    chime_record_config_fatal "test"
    chime_config_fatal_present || fail "fatal stamp was not created"
    if chime_config_fatal_present; then
        echo "FAIL (permanent config error)" > "$tmp/start.out"
    fi
    grep -q "permanent config error" "$tmp/start.out" || fail "stamp did not block supervisor start"
    chime_clear_config_fatal
    if chime_config_fatal_present; then
        fail "fatal stamp was not cleared"
    fi
}

test_crash_still_restarts() {
    chime_supervisor_should_restart 1 || fail "exit 1 should restart"
    chime_supervisor_should_restart 0 || fail "exit 0 should restart"
    if chime_supervisor_should_restart "$CHIME_CONFIG_FATAL_EXIT"; then
        fail "exit 78 must not restart"
    fi
}

test_future_schema_migrate
test_stamp_prevents_start
test_crash_still_restarts
test_supervisor_stops_on_fatal_exit

echo "[test-chime-config-fatal] passed"
