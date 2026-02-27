#!/usr/bin/env bash
set -euo pipefail

# Usage: ./ring.sh [topic] [payload] [broker_host] [port]
# If broker_host is not specified, uses Docker mqtt container
# Optional env auth:
#   MQTT_USERNAME=... MQTT_PASSWORD=...

TOPIC="${1:-doorbell/ring}"
PAYLOAD="${2:-ring}"
BROKER="${3:-}"
PORT="${4:-1883}"
MQTT_USERNAME="${MQTT_USERNAME:-}"
MQTT_PASSWORD="${MQTT_PASSWORD:-}"

AUTH_ARGS=()
if [ -n "$MQTT_USERNAME" ]; then
    AUTH_ARGS+=("-u" "$MQTT_USERNAME")
    if [ -n "$MQTT_PASSWORD" ]; then
        AUTH_ARGS+=("-P" "$MQTT_PASSWORD")
    fi
fi

if [ -n "$BROKER" ]; then
    # Publish directly to external broker (requires mosquitto-clients installed)
    mosquitto_pub -h "$BROKER" -p "$PORT" -t "$TOPIC" -q 1 -m "$PAYLOAD" "${AUTH_ARGS[@]}"
else
    # Publish via Docker mqtt container
    AUTH_CLI=""
    if [ -n "$MQTT_USERNAME" ]; then
        AUTH_CLI="-u '$MQTT_USERNAME'"
        if [ -n "$MQTT_PASSWORD" ]; then
            AUTH_CLI="$AUTH_CLI -P '$MQTT_PASSWORD'"
        fi
    fi
    docker exec -i mqtt sh -lc \
      "mosquitto_pub -h 127.0.0.1 -p 1883 -t '$TOPIC' -q 1 -m '$PAYLOAD' $AUTH_CLI"
fi
