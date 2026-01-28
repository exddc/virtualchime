#!/usr/bin/env bash
set -euo pipefail

PI_HOST="${1:-}"
BINARY_PATH="${2:-./chime/build/chime}"
REMOTE_PATH="/usr/local/bin/chime"
SSH_USER="root"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=5"

usage() {
    echo "Usage: $0 <pi-ip-or-hostname> [binary-path]"
    echo ""
    echo "Arguments:"
    echo "  pi-ip-or-hostname  IP address or hostname of the Raspberry Pi"
    echo "  binary-path        Path to the chime binary (default: ./chime/build/chime)"
    echo ""
    echo "Examples:"
    echo "  $0 192.168.1.100"
    echo "  $0 chime.local ./build/chime"
    exit 1
}

log() {
    echo "[deploy] $*"
}

error() {
    echo "[deploy] ERROR: $*" >&2
    exit 1
}

if [ -z "$PI_HOST" ]; then
    usage
fi

if [ ! -f "$BINARY_PATH" ]; then
    error "Binary not found at: $BINARY_PATH"
fi

if command -v file &>/dev/null; then
    FILE_TYPE=$(file "$BINARY_PATH")
    if [[ ! "$FILE_TYPE" =~ ARM ]]; then
        echo "[deploy] WARNING: Binary may not be ARM architecture:"
        echo "         $FILE_TYPE"
        echo ""
        read -p "Continue anyway? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
fi

log "Deploying to $PI_HOST..."

log "Testing SSH connection..."
if ! ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "echo 'SSH OK'" &>/dev/null; then
    error "Cannot connect to $PI_HOST via SSH. Check:"
    echo "  - Pi is powered on and connected to Wi-Fi"
    echo "  - Your SSH public key is in /root/.ssh/authorized_keys on the Pi"
    echo "  - IP address is correct"
fi

log "Stopping chime service..."
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime stop" || true

log "Copying binary to $REMOTE_PATH..."
scp $SSH_OPTS "$BINARY_PATH" "$SSH_USER@$PI_HOST:$REMOTE_PATH"

log "Setting permissions..."
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "chmod 755 $REMOTE_PATH"

log "Starting chime service..."
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime start"

sleep 2
log "Verifying service status..."
ssh $SSH_OPTS "$SSH_USER@$PI_HOST" "/etc/init.d/S99chime status"

log "Deploy complete!"
echo ""
echo "To view logs: ssh $SSH_USER@$PI_HOST 'tail -f /var/log/chime.log'"
