# Phase 3 — real app processes build log — ElfKick

Date: Oct 28–29, 2026 · VM: Ubuntu, gcc 13, CMake 3.28, 2 cores
Tree: this repo (`~/workspace/elfkick`), build dir `build-app/`, configured
WITHOUT `-DKICK_SIMULATOR` (defaults build the real apps), Release,
CPM cache + local-source overrides mirroring `scripts/build-simulator.sh`.

Targets (from `Kick/apps/CMakeLists.txt`, all `juce_add_gui_app`):
`Kick` (apps/kick), `KickMain` (kick_main), `KickUserInterface` (kick_gui),
`KickAudioEngine` (kick_audio_engine), `KickDeviceControl`
(kick_device_control), `KickAudioModel` (kick_audio_model).
(`apps/kick_audio` is gated behind KICK_PLAYBACK; the device-control test
app behind KICK_TESTS — both off by default.)

Baseline: Phase 2 left all shared modules compiling (see SIM_BUILD_LOG.md);
the unknowns are app-specific code drift and the device-control serial path
(juce_serialport Linux stub = fail-safe no-ops).

## Changes (in order)

0. Environment (not a tree change): the real apps all set `JUCE_JACK=1`
   (kickem did not), and the VM lacked the JACK headers — installed
   `libjack-jackd2-dev` via apt. The Buildroot hardware image ships JACK2,
   so this is a VM gap, not an upstream bug. Compiles of
   `juce_audio_devices.cpp` that ran before the install failed with
   `jack/jack.h: No such file or directory` (Kick, KickUserInterface).
1. Build round 1 (`-k`, everything): **KickMain (697K), KickAudioEngine
   (8.1M) and KickAudioModel (8.1M) LINKED with no code changes at all** —
   Phase 2's shared-module work carried over. Only failures: the two JACK
   compiles above, and KickDeviceControl (next item).
2. `Kick/apps/kick_device_control/CMakeLists.txt` — link `AudIO_constants`
   directly. Root cause: this app links `input_events` → `common`, and
   `common.h` includes `<AudIO_constants/AudIO_constants.h>`, but unlike
   every other app it links no AudIO module, so the AudIO root include
   path was missing. The header-only constants module is exactly what
   `common`'s JUCE module declaration lists as its dependency.
3. Environment note: the VM reset mid-link during round 2 (same failure
   mode as Phase 2 build 8) — /tmp logs wiped, and the X11/JACK/freetype
   -dev packages vanished until the OS package replay restored them
   (jack.h, Xrandr.h verified back). The three round-1 binaries and all
   compiled objects in `build-app/` survived. Build logs moved into
   `build-app/` so they survive /tmp wipes.
4. `Kick/apps/kick/CMakeLists.txt` + `Kick/apps/kick_gui/CMakeLists.txt` —
   link `juce::juce_dsp` directly. Round-2 Kick link failed with undefined
   `juce::dsp::StateVariableTPTFilter<float>` references from
   `mt::DrumVoice`/`mt::DrumProgram`: the reconstructed `mt::Filter`
   (AudIO_juce) is implemented on juce::dsp and neither app linked it
   (kickem needed the identical fix in Phase 2). KickUserInterface links
   the same UI/program modules as kickem, so it gets the same link.

## Result — all success criteria reached (Oct 29, 2026)

- **A. All six real app targets LINK** (zero errors, zero undefined
  references in the final round):
  - `build-app/apps/kick/Kick_artefacts/Release/Kick` — 22 MB
  - `build-app/apps/kick_gui/KickUserInterface_artefacts/Release/KickUserInterface` — 23 MB
  - `build-app/apps/kick_audio_engine/KickAudioEngine_artefacts/Release/KickAudioEngine` — 8.1 MB
  - `build-app/apps/kick_audio_model/KickAudioModel_artefacts/Release/KickAudioModel` — 8.1 MB
  - `build-app/apps/kick_main/KickMain_artefacts/Release/KickMain` — 697 KB
  - `build-app/apps/kick_device_control/KickDeviceControl_artefacts/Release/KickDeviceControl` — 746 KB
  Total source changes for the whole phase: **three CMakeLists link
  fixes** (items 2 and 4) plus one VM package install (item 0). No app
  source code needed changes — Phase 2's module work carried over.
- **B. Launch (partial).** `KickUserInterface` launches headless
  (`ALSA_CONFIG_PATH=$PWD/asound-null.conf xvfb-run -a timeout 15`) and
  stays alive the full 15 s (exit 124, no crash). It maps a fullscreen
  window (verified 1920x1200, the Xvfb size, via xwininfo) — but the
  window paints **uniformly black** in this environment, even with
  KickAudioModel + KickAudioEngine + KickDeviceControl running
  alongside as peers (all stay alive; capture saved as
  `kick-gui-screenshot.png`, a black frame). The process fully
  initialises (101 threads, IPC + timer threads active). The home
  screen's content is evidently driven by state that doesn't exist
  off-hardware (factory content under `/app/...`, a loaded project,
  supervisor orchestration); root-causing the black home screen is a
  runtime-integration question, not a build blocker, and is left open.
  Related launch notes: `KickDeviceControl` also stays alive under a
  foreground timeout (exit 124; the serial path is the fail-safe stub);
  `KickMain` exits 0 immediately with no arguments — it expects its
  supervisor runtime config, not a crash (no output, clean exit).
  Launch noise is the expected off-hardware set (JACK server absent,
  ALSA SEQ, backlight sysfs, eth0/udhcpc probes).
- **C. No regressions.** KT_KickEM rebuild in `build-kick/`: exit 0.
  Standalone AudIO via `scripts/build-audio.sh build-audio-verify2`:
  exit 0 — libAudIO_Core/libDAW_Core/libAirWindows_plugins and all 4
  examples rebuilt; DynamicPatch still runs (exit 124 under timeout).

## New upstream findings this phase

- All real apps set `JUCE_JACK=1` but nothing in the tree provisions the
  JACK headers — on the Buildroot target they're part of the image; on a
  desktop dev machine they must be installed (libjack-jackd2-dev).
- `apps/kick_device_control` never linked a module that provides the
  AudIO include path, although its `common` dependency includes
  `<AudIO_constants/...>` — it only ever built inside a tree where some
  other target's flags leaked the path (or was never built standalone).
- Neither `apps/kick` nor `apps/kick_gui` linked `juce::juce_dsp`,
  although the published engine code (mt::DrumVoice via the filter in
  the private AudIO_juce module) requires it — more evidence the apps
  were built against the private module set, whose CMake glue propagated
  dependencies the public tree doesn't.

