#!/bin/bash
# Safety net for GPU experiments: if the kernel hangs, the board reboots by itself.
# - systemd pings the hardware watchdog (/dev/watchdog) every few seconds; a hung kernel stops
#   pinging and the watchdog resets the board after ~30 s;
# - kernel.panic=10: reboot 10 s after a kernel panic.
# Usage: sudo bash tools/enable-watchdog.sh      Undo: sudo bash tools/enable-watchdog.sh undo
set -e
[ "$(id -u)" = 0 ] || { echo "Run with sudo."; exit 1; }

if [ "${1:-}" = undo ]; then
    rm -f /etc/systemd/system.conf.d/90-watchdog.conf /etc/sysctl.d/90-panic-reboot.conf
    systemctl daemon-reexec
    sysctl -w kernel.panic=0
    echo "Reverted."
    exit 0
fi

[ -e /dev/watchdog ] || echo "Warning: no /dev/watchdog on this board — only the panic reboot will work."
mkdir -p /etc/systemd/system.conf.d
cat > /etc/systemd/system.conf.d/90-watchdog.conf <<'EOF'
[Manager]
RuntimeWatchdogSec=30s
RebootWatchdogSec=2min
EOF
systemctl daemon-reexec
echo "kernel.panic = 10" > /etc/sysctl.d/90-panic-reboot.conf
sysctl -w kernel.panic=10
systemctl show -p RuntimeWatchdogUSec
echo "Done."
