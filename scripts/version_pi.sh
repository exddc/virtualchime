#!/usr/bin/env bash
set -euo pipefail

PI_HOST="${1:-}"
SSH_USER="root"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10"

if [ -z "$PI_HOST" ]; then
    echo "Usage: $0 <pi-ip-or-hostname>"
    exit 1
fi

ssh $SSH_OPTS "$SSH_USER@$PI_HOST" '
echo "=== /etc/virtualchime-release ==="
if [ -f /etc/virtualchime-release ]; then
    cat /etc/virtualchime-release
else
    echo "missing (flash a newer image that writes this file)"
fi

echo
echo "=== /etc/chime-app-version ==="
if [ -f /etc/chime-app-version ]; then
    cat /etc/chime-app-version
else
    echo "missing"
fi

echo
echo "=== kernel ==="
uname -r

echo
echo "=== chime binary ==="
if [ -x /usr/local/bin/chime ]; then
    if command -v timeout >/dev/null 2>&1; then
        if timeout 3 /usr/local/bin/chime --version; then
            :
        else
            rc=$?
            if [ "$rc" -eq 124 ]; then
                echo "chime --version timed out (binary likely does not support --version yet)"
            else
                echo "chime --version exited with code $rc"
            fi
        fi
    else
        /usr/local/bin/chime --version || true
    fi
else
    echo "missing /usr/local/bin/chime"
fi
'
