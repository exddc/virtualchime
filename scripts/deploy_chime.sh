#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="$PROJECT_DIR/buildroot/output"
CHIME_BINARY="$OUTPUT_DIR/chime"

PI_HOST="${1:-}"
SSH_USER="root"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10"
REMOTE_PATH="/usr/local/bin/chime"

log() { echo "[deploy] $*"; }
error() { echo "[deploy] ERROR: $*" >&2; exit 1; }

if [ -z "$PI_HOST" ]; then
    echo "Usage: $0 <pi-ip-or-hostname>"
    echo ""
    echo "This script deploys the rebuilt chime binary to the Pi."
    echo "Run ./scripts/rebuild_chime.sh first to build the binary."
    echo ""
    echo "Tips for finding your Pi:"
    echo "  - Check your router's DHCP lease table"
    echo "  - Try: arp -a | grep -i 'b8:27:eb' (Pi Zero MAC prefix)"
    exit 1
fi

# Check binary exists
if [ ! -f "$CHIME_BINARY" ]; then
    error "Binary not found at $CHIME_BINARY. Run ./scripts/rebuild_chime.sh first."
fi

log "Deploying chime to $PI_HOST..."

log "Stopping chime service..."
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime stop" || true

# Copy the binary
log "Copying binary..."
scp -O $SSH_OPTS "$CHIME_BINARY" "$SSH_USER@$PI_HOST:$REMOTE_PATH"

# Set permissions
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "chmod +x $REMOTE_PATH"

# Start the service
log "Starting chime service..."
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime start"

sleep 2

# Show status
log "Service status:"
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime status"

log "Deployment complete!"
log "View logs: ssh $SSH_USER@$PI_HOST tail -f /var/log/chime.log"
