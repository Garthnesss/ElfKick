#!/usr/bin/env bash
# Serve the ElfKick web panel and start its bridge to the runtime.
# See panel/BRIDGE.md for the design.
#
#   scripts/serve-panel.sh [--no-restart-dcl]
#
# The bridge creates /dev/ttymxc0 (a pty) which KickDeviceControl opens
# as its hardware UART. If KickDeviceControl is ALREADY running it
# started before the pty existed and logged "Serial Not Found" — so,
# unless --no-restart-dcl is given, this script restarts just that one
# child; KickMain (the supervisor) respawns it, and on the new start it
# finds the port. Start the rig first (RUNTIME_LOG.md recipe), then
# this; or run this first and start the rig after — either order works.
set -u
cd "$(dirname "$0")/.."

RESTART_DCL=1
[ "${1:-}" = "--no-restart-dcl" ] && RESTART_DCL=0

if pgrep -f "panel/bridge.py" > /dev/null; then
    echo "bridge already running (pid $(pgrep -f 'panel/bridge.py' | head -1))"
else
    nohup python3 panel/bridge.py > /tmp/elfkick-bridge.log 2>&1 &
    echo "bridge started (pid $!), log: /tmp/elfkick-bridge.log"
    sleep 1
fi

if [ "$RESTART_DCL" = "1" ] && pgrep -f "KickDeviceControl" > /dev/null; then
    echo "restarting KickDeviceControl so it opens /dev/ttymxc0 (KickMain respawns it)"
    pkill -f "build-app/system/KickDeviceControl" || pkill -f "KickDeviceControl"
    sleep 2
    pgrep -af "KickDeviceControl" || echo "WARNING: KickDeviceControl not back yet — check KickMain"
fi

echo
echo "Panel:  http://127.0.0.1:8793/index.html"
echo "Bridge log: /tmp/elfkick-bridge.log"
