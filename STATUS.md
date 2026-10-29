# ElfKick — Status

**Last updated:** 2026-10-29 (Phase 3 COMPLETE; web panel bridged)
**Phase:** 1 mostly done, 2 DONE, 3 **COMPLETE** — system runs
together, pad press triggers a voice, and Garth's web panel drives
the real runtime (see below). Repo created by Garth
(Garthnesss/ElfKick, public).

## Web panel front end — BRIDGED + VERIFIED (Oct 29, 2026)
Garth's groovebox panel (vendored in `panel/`, his design) is now a
working front end for the real runtime (full record:
`panel/BRIDGE.md`). The bridge **plays the panel MCU**: it owns a pty
at the hardware serial device name and speaks the real CPktMgr wire
format to the real KickDeviceControl, so web events travel 100% of
the hardware path and LED feedback returns on the same wire. The one
runtime source change: the `juce_serialport` Linux stub became a real
termios backend behind the existing API. Wire discovery: MCU frames
end with `'\n'` after the CRC (upstream's sender has it commented
out; the receiver's slicing proves it). Verified on the staged rig:
web pad 1 → Elf Drums render meters lit ~80% with sample decay;
BROWSE and TRACK_MIX switch the GUI screen; the GUI's LED frames
return to the web client as MPC-shaped events. Mapping covers all 16
pads + 40+ buttons + P1–P4 + jog + strip; gaps flagged honestly in
BRIDGE.md (INPUT button has no runtime counterpart; MASTER/REC qlink
codes invented, upstream leaves them unwired too; banks E–H fold to
A–D). **Caveat: a click inside a real browser page is not yet
verified** — no runnable browser exists on the VM; bridge.js is
proven by inspection, every server-side hop by execution, and
BRIDGE.md documents the exact browser-level re-check. Serve it with
`scripts/serve-panel.sh` (panel :8793, bridge :8794). Screen
streaming into MPC.screen is designed in BRIDGE.md, not built.
**Update (Oct 29, 2026): screen streaming BUILT + verified** — the
bridge captures the GUI window (1024×600) as JPEG frames (~18 KB,
sustained ~4.4 fps of a 5 fps target; capture only while a client is
subscribed, ~0.33 cores) and sends them as binary WS frames;
bridge.js takes over MPC.screen and paints them letterboxed.
Streamed frames verified byte-faithful against direct captures
across screen switches, and the input path is unaffected mid-stream
(pad burst → render meters; BROWSE LED round trip). Remaining gap is
only the in-browser rendering, which Garth's next page load checks
(bridge logs separate page-side from bridge-side failure).

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
  - `KickUserInterface` **launches, stays alive, and DRAWS** (root
    cause found Oct 28, 2026; full log: `RUNTIME_LOG.md`). The black
    window was **not a code bug**: the UI draws exclusively from the
    factory asset tree — `ui::Theme::loadTheme("Default")` parses
    `/app/assets/Default/theme.xml` for every colour and every image
    is file-loaded from the same tree. With `/app` absent, every
    colour falls through to `transparentBlack` and all images are
    null, so a fully initialised UI paints transparent-on-black. The
    repo ships the identical tree at `Kick/assets/`; placing it at the
    hardcoded path (plus `/app/demos` + `/app/templates` content)
    renders the real home screen: Projects page, Demo 01 + Template 01
    tiles, new-project tile (`kick-gui-screenshot.png`, 37,531
    colours — it overwrote the black frame). No source change needed.
  - **The whole system runs together (Phase 3 gate, Oct 28, 2026).**
    `KickMain` supervises by spawning `BASE_PATH + exe` (its own
    directory) with a JSON connection config as argv — staging all
    five binaries into one dir and running `./KickMain` brought up
    GUI + model + engine + device control with the full IPC mesh
    connected (on hardware: systemd `ktkick.service` → `kt_kick.sh`).
    With `KT_KickEM` running alongside as the hardware peer, all six
    processes stayed alive simultaneously, and the simulator's centre
    cut-out renders the themed Projects screen
    (`kick-simulator-screenshot.png`).
  - **Pad-press test (Oct 28, 2026): gate NOT passed, break localised
    exactly** (full record: `PAD_TEST_LOG.md`). Real X11 clicks into
    the simulator: the pad component fires, but nothing downstream
    reacts — because **kickem never calls
    `Controls::InputEvents::initialize()`** (its only callers are
    apps/kick's adapter, kick_device_control's main, _playback), so
    the Phase 2 SIM_BUILD wrapper is never armed in the simulator and
    the panel emits into a signal with zero slots; the
    dispatch→DclSignals bridge (KickDeviceAdapter) isn't compiled
    into kickem either. Not an AudIO/engine/IPC failure — a missing
    initialisation in upstream's simulator app. Fix is scoped to a
    few lines in kickem's init (mirror KickDeviceAdapter) and is in
    progress. Second blocker behind it: Demo 01 is an **empty**
    project (factory demo content wasn't released — only tile
    artwork), so an audible voice also needs a content-bearing
    project; and the VM has no sound device regardless.
  - **Input fix LANDED (Oct 28, 2026)** (record: `PAD_FIX_LOG.md`):
    kickem's `Main.cpp` now holds a `KickDeviceAdapter` (the same
    object apps/kick uses — its ctor calls `InputEvents::initialize()`
    and bridges dispatch→DclSignals), created *after* MainWindow
    because the simulator singleton's `operator new` re-creates the
    `hwInputReceived` signal and drops earlier connections (that's
    upstream's "somehow clearing the connected signals" mystery,
    solved). One file changed; KT_KickEM rebuilt clean; headless
    regression passes. **Proof: the BROWSE panel button now propagates
    end-to-end** — the simulator's centre screen switches to the File
    Browser and stays, the button stays lit from UI feedback over the
    reverse path, and the log gains `KickHome::handleCommand` traces.
    **Pads still don't emit — a second, upstream gate:**
    `KickSimulatorButton::mouseDown/mouseUp` return early while the
    pad is `StateOff`, and pads only leave StateOff when the app
    pushes SetPadState (on hardware the MCU sends pad packets
    unconditionally, so the gate is simulator-only). Whether to change
    that gate is an open decision for Garth (see below). Bonus from
    the retest: content creation works in the GUI — a track and a DRUM
    Program 01 were created by clicking.
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
- [x] Garth: pad-gate decision — made Oct 28, 2026: pads always emit,
      implemented as the `ELFKICK_PADS` runtime switch with upstream
      semantics preserved as the other mode.
- [x] Muse: pad test against authored content — Phase 3 gate PASSED
      Oct 29, 2026 (see above).
- [ ] Muse: Phase 5 — one-command full-rig launcher
      (`scripts/run-emulator.sh`; `scripts/serve-panel.sh` already
      covers the panel + bridge), APP_PLATFORM.md, starter app.
- [ ] Anyone with a browser: the panel/BRIDGE.md browser-level
      re-check (open the served panel, click a pad, watch the LED
      feedback in-page) — the one unverified hop.
- [ ] Muse: content pipeline follow-up — the app's autosave never
      rewrote the generated .kickPrj (byte-identical after 15+ s,
      even after a GUI edit), so the "golden reference" normalised
      file is still outstanding; find the explicit-save path.
      (Symmetric positive: the generator's output is exactly what
      loads and persists.)
- [ ] Muse: create or source a content-bearing project (a drum
      program with synth voices) so a trigger has something to play.
- [ ] Muse: document the staged-system launch recipe as a script
      (RUNTIME_LOG.md has the copy-paste version).
- [ ] Muse: BasicExample `setParameter` caller hunt (Phase 1 leftover).
- [ ] Muse: Subdesign `getObject` registry bug (Phase 1 leftover).
- [ ] Muse: smoke script for the examples (build + ALSA-null timeout runs).
