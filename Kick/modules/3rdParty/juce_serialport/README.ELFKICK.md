# juce_serialport goes here

Upstream removed this module from the release ("no license") but the build
still references it. Run `scripts/fetch-deps.sh` from the repo root — it
clones https://github.com/cpr2323/juce_serialport into this folder and
applies a small patch making its header self-contained
(adds `<functional>` and the juce_core/juce_events includes).

Note: that module ships Windows/macOS/Android/iOS implementations only —
there is no Linux serial implementation, which is one of the open issues
for running the simulator on Linux.
