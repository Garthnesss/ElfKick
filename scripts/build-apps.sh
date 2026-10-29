#!/bin/sh
# Build the real Kick application processes (Kick, KickUserInterface,
# KickAudioEngine, KickAudioModel, KickMain, KickDeviceControl).
# All six targets link as of Oct 28, 2026 — see PHASE3_BUILD_LOG.md.
# Requires the JACK development headers (the real apps set JUCE_JACK=1):
# on Debian/Ubuntu, install libjack-jackd2-dev first.
# Usage: scripts/build-apps.sh [build-dir]
set -eu
cd "$(dirname "$0")/.."
BUILD=${1:-build-app}
scripts/fetch-deps.sh
cmake -S Kick -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DCPM_AudIO_SOURCE="$PWD/AudIO" \
    -DCPM_COM_PROTOCOL_SOURCE="$PWD/ComProtocol"
cmake --build "$BUILD" -j2
