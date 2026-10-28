#!/bin/sh
# Build the Kick simulator (KT_KickEM).
# Configure succeeds with the reconstructed glue in this repo, and the
# simulator code has been reconciled with the published module API —
# every translation unit compiles. See SIM_BUILD_LOG.md for the full
# itemised change log and BUILD_FINDINGS.md for the summary.
# Usage: scripts/build-simulator.sh [build-dir]
set -eu
cd "$(dirname "$0")/.."
BUILD=${1:-build-kick}
scripts/fetch-deps.sh
cmake -S Kick -B "$BUILD" -DKICK_SIMULATOR=ON -DCMAKE_BUILD_TYPE=Release \
    -DCPM_AudIO_SOURCE="$PWD/AudIO" \
    -DCPM_COM_PROTOCOL_SOURCE="$PWD/ComProtocol"
cmake --build "$BUILD" -j2 --target KT_KickEM
