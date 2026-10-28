#!/bin/sh
# Fetch dependencies that are not vendored in this repo.
# - juce_serialport: removed by upstream (unlicensed in their copy); the API
#   matches cpr2323/juce_serialport. Its header is then patched to be
#   self-contained (it uses std::function / juce::String without includes).
set -eu
cd "$(dirname "$0")/.."
DEST=Kick/modules/3rdParty/juce_serialport
if [ ! -f "$DEST/juce_serialport.h" ]; then
    rm -rf "$DEST.tmp"
    git clone --depth 1 https://github.com/cpr2323/juce_serialport "$DEST.tmp"
    rm -rf "$DEST.tmp/.git"
    mkdir -p "$DEST"
    cp -r "$DEST.tmp/." "$DEST/"
    rm -rf "$DEST.tmp"
    python3 - "$DEST/juce_serialport.h" <<'PYEOF'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); t = p.read_text()
if "#include <functional>" not in t:
    t = t.replace("#include <stdint.h>",
                  "#include <stdint.h>\n#include <functional>\n#include <juce_core/juce_core.h>\n#include <juce_events/juce_events.h>")
    p.write_text(t)
print("juce_serialport fetched + header patched")
PYEOF
else
    echo "juce_serialport already present, skipping"
fi
echo "Note: juce_serialport has no Linux implementation (Win/OSX/Android/iOS only)."
