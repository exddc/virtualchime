#!/usr/bin/env bash
set -euo pipefail

PI_HOST="${1:-}"
SSH_USER="root"

if [ -z "$PI_HOST" ]; then
    echo "Usage: $0 <pi-ip-or-hostname>"
    echo ""
    echo "Tips for finding your Pi:"
    echo "  - Check your router's DHCP lease table"
    echo "  - Try: ping chime.local (if mDNS is working)"
    echo "  - Use: arp -a | grep -i 'b8:27:eb' (Pi Zero MAC prefix)"
    exit 1
fi

exec ssh -o StrictHostKeyChecking=no "$SSH_USER@$PI_HOST"
