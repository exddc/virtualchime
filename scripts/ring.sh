#!/usr/bin/env bash
set -euo pipefail

# Usage: ./ring.sh [topic] [payload] [broker_host] [port]
# If broker_host is not specified, uses Docker mqtt container

TOPIC="${1:-doorbell/ring}"
PAYLOAD="${2:-ring}"
BROKER="${3:-}"
PORT="${4:-1883}"

if [ -n "$BROKER" ]; then
    # Publish directly to external broker (requires mosquitto-clients installed)
    mosquitto_pub -h "$BROKER" -p "$PORT" -t "$TOPIC" -q 1 -m "$PAYLOAD"
else
    # Publish via Docker mqtt container
    docker exec -i mqtt sh -lc \
      "mosquitto_pub -h 127.0.0.1 -p 1883 -t '$TOPIC' -q 1 -m '$PAYLOAD'"
fi
