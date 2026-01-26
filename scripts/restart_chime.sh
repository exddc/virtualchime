#!/usr/bin/env bash
set -euo pipefail

PI_HOST="${1:-}"
SSH_USER="root"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=5"

if [ -z "$PI_HOST" ]; then
    echo "Usage: $0 <pi-ip-or-hostname>"
    exit 1
fi

echo "[restart] Restarting chime on $PI_HOST..."
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime restart"

sleep 2
echo "[restart] Service status:"
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime status"
