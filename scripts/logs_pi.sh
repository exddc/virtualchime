#!/usr/bin/env bash
set -euo pipefail

PI_HOST="${1:-}"
LINES="${2:-50}"
SSH_USER="root"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=5"

if [ -z "$PI_HOST" ]; then
    echo "Usage: $0 <pi-ip-or-hostname> [lines]"
    echo ""
    echo "Arguments:"
    echo "  lines  Number of lines to show (default: 50, use 'f' for follow mode)"
    exit 1
fi

if [ "$LINES" = "f" ] || [ "$LINES" = "follow" ]; then
    echo "[logs] Following chime logs on $PI_HOST (Ctrl+C to stop)..."
    ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "tail -f /var/log/chime.log"
else
    echo "[logs] Last $LINES lines from chime on $PI_HOST:"
    ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "tail -n $LINES /var/log/chime.log"
fi
