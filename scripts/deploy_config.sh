#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
CONFIG_FILE="$PROJECT_DIR/buildroot/board/raspberrypi0w/rootfs_overlay/etc/chime.conf"

PI_HOST="${1:-}"
SSH_USER="root"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10"
REMOTE_PATH="/etc/chime.conf"

log() { echo "[deploy-config] $*"; }
error() { echo "[deploy-config] ERROR: $*" >&2; exit 1; }

if [ -z "$PI_HOST" ]; then
    echo "Usage: $0 <pi-ip-or-hostname>"
    echo ""
    echo "This script deploys the chime.conf configuration to the Pi."
    echo ""
    echo "Tips for finding your Pi:"
    echo "  - Check your router's DHCP lease table"
    echo "  - Try: arp -a | grep -i 'b8:27:eb' (Pi Zero MAC prefix)"
    exit 1
fi

# Check config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    error "Config file not found at $CONFIG_FILE"
fi

log "Deploying chime.conf to $PI_HOST..."

# Copy the config file
log "Copying config..."
scp -O $SSH_OPTS "$CONFIG_FILE" "$SSH_USER@$PI_HOST:$REMOTE_PATH"

# Restart the service to apply changes
log "Restarting chime service..."
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime restart"

sleep 2

# Show status
log "Service status:"
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime status"

log "Config deployment complete!"
log "View logs: ssh $SSH_USER@$PI_HOST tail -f /var/log/chime.log"
