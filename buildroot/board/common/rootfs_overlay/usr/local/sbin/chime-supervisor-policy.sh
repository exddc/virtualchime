#!/bin/sh
# Shared policy for permanent config failures. Sourced by S32, S45, and S99.
# Exit 78 is EX_CONFIG: malformed or future schema_version. Supervisors must
# not restart on that code, or logs grow without recovering.

# shellcheck disable=SC2034
CHIME_CONFIG_FATAL_EXIT="${CHIME_CONFIG_FATAL_EXIT:-78}"
CHIME_CONFIG_FATAL_STAMP="${CHIME_CONFIG_FATAL_STAMP:-/var/lib/chime/config.fatal}"

chime_supervisor_should_restart() {
    [ "$1" -ne "$CHIME_CONFIG_FATAL_EXIT" ]
}

chime_config_fatal_present() {
    [ -f "$CHIME_CONFIG_FATAL_STAMP" ]
}

chime_record_config_fatal() {
    mkdir -p "$(dirname "$CHIME_CONFIG_FATAL_STAMP")"
    echo "${1:-permanent config error}" > "$CHIME_CONFIG_FATAL_STAMP"
}

chime_clear_config_fatal() {
    rm -f "$CHIME_CONFIG_FATAL_STAMP"
}
