#!/bin/sh
# Attempt the Kick simulator (KT_KickEM) build. EXPERIMENTAL:
# configure succeeds with the reconstructed glue in this repo; the build
# currently fails in apps/kickem/simulator code, which references a module
# API that is not in the published tree. See BUILD_FINDINGS.md.
# Usage: scripts/build-simulator.sh [build-dir]
set -eu
cd "$(dirname "$0")/.."
BUILD=${1:-build-kick}
scripts/fetch-deps.sh
cmake -S Kick -B "$BUILD" -DKICK_SIMULATOR=ON -DCMAKE_BUILD_TYPE=Release \
    -DCPM_AudIO_SOURCE="$PWD/AudIO" \
    -DCPM_COM_PROTOCOL_SOURCE="$PWD/ComProtocol"
cmake --build "$BUILD" -j2 --target KT_KickEM
