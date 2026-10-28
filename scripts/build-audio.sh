#!/bin/sh
# Build the AudIO libraries + console examples (Linux).
# Usage: scripts/build-audio.sh [build-dir]
set -eu
cd "$(dirname "$0")/.."
BUILD=${1:-build-audio}
cmake -S AudIO -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
for t in AudIO_Core DAW_Core AirWindows_plugins BasicExample DawExample DynamicPatch Subdesign; do
    cmake --build "$BUILD" -j2 --target "$t"
done
echo "Built: $BUILD/lib/*.a and $BUILD/bin/{BasicExample,DawExample,DynamicPatch,Subdesign}"
echo "Run one (no sound card needed):  ALSA_CONFIG_PATH=\$PWD/asound-null.conf timeout 6 ./$BUILD/bin/DynamicPatch"
