#!/usr/bin/env bash
set -euo pipefail

TOPIC="${1:-doorbell/ring}"
PAYLOAD="${2:-ring}"

docker exec -i mqtt sh -lc \
  "mosquitto_pub -h 127.0.0.1 -p 1883 -t '$TOPIC' -q 1 -m '$PAYLOAD'"
