# ElfKick — Status

**Last updated:** 2026-10-28 (Phase 3 build verified)
**Phase:** 1 mostly done, 2 DONE, 3 build done / runtime integration
current (see ROADMAP.md). Repo created by Garth (Garthnesss/ElfKick,
public); import `eb954ef`, simulator `5fd706a` + `6fc84d9`, Phase 3
build follows.

## Where things stand
- **Upstream mapped.** MusicTribeOpenSource/0700-AAM @ `2719418` cloned and
  read: JUCE 7.0.12 app, AudIO engine, PktMgr protocol, Buildroot for
  Variscite i.MX8M Mini. No camera/gesture code exists despite the marketing.
- **AudIO standalone — BUILT + RUNNING (Linux, gcc 13, CMake 3.28).**
  Pristine tree doesn't even configure (case-sensitive `main.cpp`/`core`
  paths). After the 21-file patch in this repo: `AudIO_Core`, `DAW_Core`,
  `AirWindows_plugins` and all 4 console examples compile and link.
  - `DynamicPatch` — **RUNS**: continuous meter output (silent graph, engine
    + device thread + meters alive), survived timeout kill.
  - `DawExample` — **RUNS**: survived timeout kill (needs
    `AUDIO_DAW_EXTENSIONS=1`, now set for the standalone DAW lib).
  - `BasicExample` — **BUILDS, CRASHES at runtime**: starts, prints memory
    + console help, then aborts ("terminate called without an active
    exception"). Prime suspect is the only bare `throw;` in the tree,
    `SFSynth::setParameter`'s default case. An earlier crash in the same
    program (null `_tsf` in `SFSynth::prepareToPlay`, caused by the stubbed
    GM SoundFont) is **fixed** (null guard, ASan-verified trace).
  - `Subdesign` — **BUILDS, CRASHES at runtime**: null deref in the example
    itself (Main.cpp:56) — `getObject` returned null for a just-created
    SineWave in a design containing sub-designs. ASan-verified; not yet fixed.
  - Running any example on a machine with no sound card needs
    `asound-null.conf` (the vendored minisdl ignores `SDL_AUDIODRIVER=dummy`
    and null-derefs in its ALSA thread when no card exists).
- **Kick simulator (KT_KickEM) — BUILT + LAUNCHES (verified Oct 28, 2026).**
  23 MB x86-64 binary; `xvfb-run -a timeout 15` run stays alive the full
  15 s (exit 124 = timeout kill, no crash); the hardware panel renders
  fully (see `kickem-screenshot.png`). The first launch found a real
  upstream crash: `ui::RecentProject::visibilityChanged()` dereferences
  `recentItems[selectedIndex]` unconditionally, but on Linux the content
  dirs are hardcoded hardware paths (`/app/demos/`, …), so with no
  factory content the vector is empty → segfault at ~9 s. Fixed with a
  one-line bounds guard in `kick_RecentProject.cpp` (proven both ways:
  unpatched binary + factory dirs present = runs; patched binary + no
  factory content = runs). Standalone AudIO build re-verified after all
  changes: libraries + 4 examples rebuild, DynamicPatch still runs.
  Caveat: the reconstruction is compile/launch-faithful, not yet
  behaviour-verified — nobody has clicked pads end-to-end, and audio
  output is untested (VM has no sound device).
  Full itemised log: `SIM_BUILD_LOG.md`. Pristine configure dies cloning
  AudIO from a placeholder example.com URL; with local-source overrides +
  reconstructed glue the configure succeeds. The build that previously
  failed with ~1,960 error lines now compiles **every translation unit**.
  What it took:
  - `AudIO_constants` turned out to be **load-bearing, not a stub**: the
    tree uses global-namespace constants (MIDI/MIDI_CH/DB/GAIN/PAN/
    POLYPHONY, `UNKNOWN_VALUE = -9999`, `AUDIO_*` geometry 48 kHz / 960 /
    2-in / 2-out / 4 sends). Reconstructed from call sites; values are
    inference and marked RECONSTRUCTED in the header.
  - `AudIO_juce` (private module, never published) reconstructed:
    `mt::Recorder` = `AudIO::AudioRecorder`; `mt::SampleRecorder` /
    `mt::Looper` adapters over `AudIO::DRecorder`; `mt::AudioDevice` =
    `IAudioDevice` backed by `juce::AudioDeviceManager`, silent with no
    sound card; plus `mt::ADSR`, `mt::Filter`, `mt::FilterType`. Required
    enabling `AUDIO_DAW_EXTENSIONS` + `AUDIO_OBJECT_AUDIO_FILE_RECORDER`
    (AudIO.h switches are now `#ifndef`-guarded with those defaults on).
  - kickem app code migrated to the published API: contracts types in
    `contracts::DeviceControlContracts`, `BtnLedSequence` / `PadSequence` /
    `BtnState` enums, palacaze sigslot (signals invoked via `operator()`,
    no `.emit()`), JUCE 7 Displays pointer API, `kick_ui::KickHome`
    singleton. `input_events` SIM_BUILD path repaired to wrap simulator
    events into `CPktMgr::HWMsg_t` (pads = id base 1000, qlinks = 1100).
  - Link blockers fixed: kickem now links the `contracts` module,
    `juce::juce_dsp`, and `ipc` directly (58 + 3 undefined symbols), and
    `juce_serialport` — which has **no Linux implementation upstream** —
    gets a repo-kept fail-safe stub (`juce_serialport_Linux.cpp`,
    .gitignore exception; `fetch-deps.sh` preserves it).
  - One upstream bug found en route: `AudIO/Objects/Recorder.h` only
    included `AudioFile.h` when JUCE_VERSION was undefined, despite
    holding a `unique_ptr<AudioFile<float>>` member.
- **Real app processes — ALL SIX BUILD + LINK (verified Oct 28, 2026).**
  Full log: `PHASE3_BUILD_LOG.md`. `Kick` (22 MB), `KickUserInterface`
  (23 MB), `KickAudioEngine` (8.1 MB), `KickAudioModel` (8.1 MB),
  `KickMain` (697 KB), `KickDeviceControl` (746 KB) — zero errors in the
  final round. Three of them linked in round 1 with **no changes at
  all**; the whole phase needed only three small CMake link fixes
  (`AudIO_constants` into device_control; `juce::juce_dsp` into kick +
  kick_gui) plus JACK headers on the VM (all real apps set `JUCE_JACK=1`;
  the Buildroot image ships JACK2, so that's a VM gap, not a bug).
  - `KickUserInterface` **launches and stays alive** headless (101
    threads, IPC + timers active, fullscreen window mapped) — but the
    window **paints uniformly black**, even with the audio engine,
    audio model, and device control running alongside as peers (all
    stay alive). The home screen's content is evidently driven by
    state that doesn't exist off-hardware (factory content under
    `/app/…`, a loaded project, supervisor orchestration). Root-causing
    the black home screen is the current open thread — a
    runtime-integration question, not a build blocker. Capture:
    `kick-gui-screenshot.png` (a black frame, kept as evidence).
  - `KickDeviceControl` stays alive under timeout (serial = fail-safe
    stub). `KickMain` exits 0 immediately with no arguments — it
    expects its supervisor runtime config; clean exit, not a crash.
  - No regressions: KT_KickEM rebuild + standalone AudIO rebuild both
    pass after all Phase 3 changes.
- **Hardware**: no KICK unit; price/availability unconfirmed. Nothing
  hardware-dependent is claimed anywhere in this repo.

## Decisions made
- Fork as **ElfKick**, public, AGPL preserved; canonical home
  `~/Projects/ElfKick` on Garth's Mac, pushed via Mac git. (2026-10-28, Garth)
- Buildroot `Linux/` tree **not vendored** (unmodified upstream, ~9k files);
  JUCE fetched by CPM; `juce_serialport` fetched by script, not vendored
  (unlicensed in upstream's copy). (2026-10-28)
- Reconstructed private-repo pieces are **stubs marked RECONSTRUCTED**, kept
  as small as the build allows, and grown only when the compiler demands a
  symbol. (2026-10-28)
- AudIO fixes kept **minimal and itemised**; pristine upstream clone is never
  edited, so fixes re-derive as a clean patch. (2026-10-28)

## Open decisions (need Garth's call)
1. ~~**Simulator strategy**~~ — RESOLVED 2026-10-28: Garth asked to run
   the simulator; reconciliation route taken (Phase 2 as written).
2. **How closely to track upstream** — rebase fixes onto new upstream drops
   as they come, or freeze on `2719418` until Phase 3? (No upstream activity
   since Oct 26 release as of this writing; 14 commits total, mostly README.)
3. **macOS build** — worth attempting the AudIO examples + simulator on the
   Mac (Apple Silicon) next, or stay Linux-only until the simulator links?

## Next actions
- [ ] Muse: root-cause the black home screen (first suspects: missing
      factory content dirs, KickMain's expected launch config /
      orchestration, project state the GUI waits on).
- [ ] Muse: exercise the simulator input path end-to-end (pad/knob
      events → PktMgr packets) once a peer process exists to receive them.
- [ ] Muse: BasicExample `setParameter` caller hunt (Phase 1 leftover).
- [ ] Muse: Subdesign `getObject` registry bug (Phase 1 leftover).
- [ ] Muse: smoke script for the examples (build + ALSA-null timeout runs).
