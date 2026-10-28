# ElfKick

A maintained, buildable fork of the **Behringer KICK** (BE 0700-AAM) open-source
firmware — Music Tribe's MPC-style standalone sampler/groovebox, released
2026-10-26 under AGPL-3.0-or-later at
[MusicTribeOpenSource/0700-AAM](https://github.com/MusicTribeOpenSource/0700-AAM).

The upstream release is a code dump that does not build as shipped (its own
README says so). ElfKick is where we fix that, in the open, one verified step
at a time — starting with the AudIO engine on Linux.

## Status (Oct 28, 2026)

- **AudIO standalone — BUILDS and RUNS on Linux.** All three libraries
  (`AudIO_Core`, `DAW_Core`, `AirWindows_plugins`) and all four console
  examples compile and link. `DynamicPatch` and `DawExample` run
  indefinitely; `BasicExample` and `Subdesign` build but crash at runtime
  (root causes documented in [BUILD_FINDINGS.md](BUILD_FINDINGS.md)).
- **Kick simulator (`KT_KickEM`) — configure succeeds, build does not (yet).**
  Forcing the configure through required reconstructing several pieces the
  release omits (see below). The build then reaches ~50% before failing in
  the simulator's own code, which references a module API
  (`mt::Kick::PadAction`, `TOTAL_DRUM_PADS`, …) that doesn't exist in the
  published modules. Fixing the simulator against the published modules is
  the project's open task.
- The Buildroot `Linux/` tree from upstream is **not** vendored here
  (unmodified, ~9,000 files); get it from the upstream repo.

Full detail, every patch explained: [BUILD_FINDINGS.md](BUILD_FINDINGS.md).
Canonical project docs: [PROJECT_TRUTH.md](PROJECT_TRUTH.md) · [ARCHITECTURE.md](ARCHITECTURE.md) · [ROADMAP.md](ROADMAP.md) · [STATUS.md](STATUS.md).
The AudIO fixes as a single diff against upstream:
[audio-linux-fixes.patch](audio-linux-fixes.patch).

## Layout

| Folder | What it is |
|---|---|
| `AudIO/` | Audio engine library (patchable graph, DAW layer, SoundFont synth, 329 Airwindows effects) — with Linux build fixes, plus reconstructed `juce_modules.cmake`, `AudIO_constants/` and `AudIO_juce/` module stubs (marked RECONSTRUCTED; the private originals were not released) |
| `ComProtocol/` | `PktMgr` framed packet protocol to the hardware MCU (SYSTEM/BUTTON/LED/PAD/KNOBS/MIDI/CV/SLIDER packets, CRC) — plus a reconstructed JUCE module wrapper header `PktMgr/PktMgr.h` |
| `Kick/` | The JUCE application (multi-process: `kick_main` supervisor, `kick_gui`, `kick_audio_engine`, `kick_device_control`; samplers in `modules/engine`) — with the `kickem` config.h fix |
| `scripts/` | Build/fetch scripts (below) |
| `asound-null.conf` | ALSA null-device config so the examples can run on machines with no sound card |

## Build the AudIO examples (Linux)

```sh
scripts/build-audio.sh          # configure + build into ./build-audio
ALSA_CONFIG_PATH=$PWD/asound-null.conf timeout 6 ./build-audio/bin/DynamicPatch
```

Prereqs: cmake ≥ 3.16, a C++17 compiler, ALSA dev headers. The examples are
infinite-loop audio apps — kill them with timeout/Ctrl-C. On a machine with a
real sound card you don't need the ALSA null config.

## Build the simulator (experimental)

```sh
scripts/fetch-deps.sh           # clones cpr2323/juce_serialport (not vendored: no license in upstream's copy)
scripts/build-simulator.sh      # configure + build KT_KickEM (currently fails in simulator code — see findings)
```

## Upstream

Forked from MusicTribeOpenSource/0700-AAM @ `2719418` (Oct 28, 2026).
Hardware: Variscite i.MX8M Mini (aarch64), Buildroot Linux, JUCE 7.0.12 app;
pads/LEDs/CV live on a separate MCU behind the PktMgr USB protocol.
See [UPSTREAM.md](UPSTREAM.md) for provenance and license notes.

## License

AGPL-3.0-or-later, same as upstream — see [LICENSE](LICENSE) and the per-folder
licenses. The Behringer/KICK names and logos are Music Tribe trademarks and
are not covered by the software license. "ElfKick" is our fork name, not a
Music Tribe product.
